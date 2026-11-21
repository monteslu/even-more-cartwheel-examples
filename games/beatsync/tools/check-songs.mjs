#!/usr/bin/env node
// The song checks (docs/DESIGN.md, "Charts" step 8), over every songs/*/ package or the ones named:
//
//   node tools/check-songs.mjs [songs/<id> ...]
//
// For each package: song.json's shape; the tracks decode (libvorbis) to the same rate and exactly length_samples
// each, and each is at 0 samples' lag against the excerpt of its source kept in report.json; every chart sorted, at
// most one note per step, on its level's grid, every gap of its level held, note times on the beat grid, notes per
// second under the level's cap; the share of each chart's notes that land on an onset found again in the decoded
// track of its lane (within 50 ms; a break note made from the other stem is looked for in the other track); no gap
// between notes longer than two bars (Easy may go to four bars only over a stretch where no track has an onset), and
// the first note within two bars of the song's first onset in any track. The controls must fail, or the check that
// should catch them is reported broken: the Hard chart moved by an 8th scores badly against the audio, a track
// shifted by 64 samples is caught by the alignment check, a chart with a 30 s hole cut into it (every level) is
// caught by the gap check, and a Hard chart missing its first 30 s of notes is caught by the first-note check.
// A level that keeps the bass (LEVELS[name].bass: Medium) has at least MIN_BASS_SHARE of its notes on the bass lane
// wherever the song has a bass line (its Hard chart does); control: the same chart with its bass notes taken out.
// The look (song.json "look.other", lib/look.mjs): a value a beat in every list, all in range, and the same look made
// again from the DECODED other track: each beat's loudness within LOOK_LEVEL_TOL; control: the look moved by two
// beats must fail that.
// Exits 1 on any failure.
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { LEVELS, MAX_GAP, stepTime, chartDensity } from './lib/chart.mjs';
import { decodeVorbisLib } from './lib/ogg.mjs';
import { stbAvailable, decodeVorbisStb } from './lib/stbvorbis.mjs';
import { excerptLag, unpackExcerpt, TRACKS } from './lib/package.mjs';
import { bandOnsets, bassOnsets, monoOf } from './lib/analyze.mjs';
import { biquad, filtfilt } from './lib/dsp.mjs';
import { otherLook } from './lib/look.mjs';

/** how far (0..99) a beat's loudness in the look may sit from the same measure of the decoded other track (AAC to
 * Vorbis moves it a little); the share of beats that must hold it */
export const LOOK_LEVEL_TOL = 4, LOOK_LEVEL_SHARE = 0.95;

/**
 * The look's shape: every per-beat list a value a beat, in range; the bands three a half beat; the stabs pairs.
 * @param {any} look song.json "look"
 * @param {number} beats
 * @returns {string[]}
 */
export function lookProblems(look, beats) {
  const o = look?.other, bad = [];
  if (!o) return ['song.json has no look.other (tools/make-song.mjs --look-only adds it)'];
  for (const [k, hi] of [['level', 99], ['bright', 99], ['tonal', 99], ['hue', 359]]) {
    if (!Array.isArray(o[k]) || o[k].length !== beats) bad.push(`look.other.${k}: ${o[k]?.length} values for ${beats} beats`);
    else if (o[k].some((v) => !Number.isInteger(v) || v < 0 || v > hi)) bad.push(`look.other.${k}: a value outside 0..${hi}`);
  }
  if (!Array.isArray(o.bands) || o.bands.length !== beats * 6) bad.push(`look.other.bands: ${o.bands?.length} values for ${beats * 2} half beats`);
  if (!Array.isArray(o.changes) || o.changes.some((c, i) => c < 0 || c >= beats || (i && c <= o.changes[i - 1]))) bad.push('look.other.changes: not increasing beat indices');
  if (!Array.isArray(o.stabs) || o.stabs.length % 2) bad.push('look.other.stabs: not [beat x 100, strength] pairs');
  return bad;
}

/** the share of beats whose loudness in `level` is within LOOK_LEVEL_TOL of `again` */
export function lookLevelShare(level, again) {
  let ok = 0;
  for (let i = 0; i < level.length; i++) if (Math.abs(level[i] - (again[i] ?? -100)) <= LOOK_LEVEL_TOL) ok++;
  return level.length ? ok / level.length : 0;
}

/** A level that keeps the bass: the least share of its notes on the bass lane, when the song has a bass line. */
export const MIN_BASS_SHARE = 0.15;

/**
 * The bass rule for a level that keeps the bass.
 * @param {{lane: number}[]} notes the level's chart
 * @param {{lane: number}[]} hard the song's Hard chart (has the song a bass line at all)
 * @returns {string|null}
 */
export function bassProblem(notes, hard) {
  const share = (list) => (list.length ? list.filter((n) => n.lane === 2).length / list.length : 0);
  if (share(hard) < MIN_BASS_SHARE) return null; // no bass line to speak of
  const got = share(notes);
  return got < MIN_BASS_SHARE ? `only ${(100 * got).toFixed(1)}% of notes on the bass lane (need ${100 * MIN_BASS_SHARE}%; Hard has ${(100 * share(hard)).toFixed(1)}%)` : null;
}

/** Minimum share of a chart's notes on a re-found onset, and the most the 8th-shifted control may keep of it. */
export const MIN_ONSET_SHARE = 0.9, CONTROL_MAX_RATIO = 0.6;

/**
 * Check a chart's structure: sorted, one note per step, on the grid, gaps held, times on the beat grid.
 * @param {{t: number, step: number, lane: number}[]} notes
 * @param {{grid: number, same: number, change: number}} lv
 * @param {number[]} beats
 * @returns {string[]} problems
 */
export function chartProblems(notes, lv, beats) {
  const bad = [];
  for (let i = 0; i < notes.length; i++) {
    const n = notes[i], p = notes[i - 1];
    if (![0, 1, 2].includes(n.lane)) bad.push(`note ${i}: lane ${n.lane}`);
    if (n.step % lv.grid) bad.push(`note ${i}: step ${n.step} off the level's grid of ${lv.grid}`);
    if (Math.abs(n.t - stepTime(beats, n.step)) > 0.002) bad.push(`note ${i}: t ${n.t} is not step ${n.step}'s time`);
    if (!p) continue;
    if (n.t < p.t || n.step <= p.step) bad.push(`note ${i}: not after note ${i - 1} (one note per step, sorted)`);
    const gap = n.step - p.step, need = p.lane === n.lane ? lv.same : lv.change;
    if (gap < need) bad.push(`note ${i}: ${gap}/16 after note ${i - 1} (${p.lane === n.lane ? 'same lane' : 'lane change'} needs ${need})`);
  }
  return bad;
}

/**
 * The gap rule: no two consecutive notes more than MAX_GAP 16ths (two bars) apart; on Easy up to twice that where
 * `silent(t0, t1)` says no track has an onset between the two notes.
 * @param {{t: number, step: number}[]} notes
 * @param {string} level
 * @param {(t0: number, t1: number) => boolean} silent
 * @returns {string[]} problems
 */
export function gapProblems(notes, level, silent) {
  const bad = [];
  for (let i = 1; i < notes.length; i++) {
    const p = notes[i - 1], n = notes[i], gap = n.step - p.step;
    if (gap <= MAX_GAP) continue;
    if (level === 'easy' && gap <= 2 * MAX_GAP && silent(p.t, n.t)) continue;
    bad.push(`${(gap / 16).toFixed(1)} bars without a note from ${p.t.toFixed(2)} s to ${n.t.toFixed(2)} s (the most is 2${level === 'easy' ? ', 4 over silence' : ''})`);
  }
  return bad;
}

/**
 * The first-note rule: the chart's first note within `maxDelay` seconds of the song's first onset in any track.
 * @param {{t: number}[]} notes
 * @param {number} firstOnset seconds
 * @param {number} maxDelay seconds
 */
export function firstNoteProblem(notes, firstOnset, maxDelay) {
  if (!notes.length) return 'no notes';
  const d = notes[0].t - firstOnset;
  return d > maxDelay ? `first note at ${notes[0].t.toFixed(2)} s, ${d.toFixed(2)} s after the song's first onset at ${firstOnset.toFixed(2)} s (the most is ${maxDelay.toFixed(2)} s, two bars)` : null;
}

/**
 * Share of notes with an onset of their lane within `tol` seconds.
 * @param {{t: number, lane: number}[]} notes
 * @param {number[][]} onsets per lane, sorted times
 */
export function onsetShare(notes, onsets, tol = 0.05) {
  if (!notes.length) return 1;
  let hit = 0;
  for (const n of notes) {
    const list = onsets[n.lane];
    let lo = 0, hi = list.length;
    while (lo < hi) { const m = (lo + hi) >> 1; if (list[m] < n.t) lo = m + 1; else hi = m; }
    if ((lo < list.length && list[lo] - n.t <= tol) || (lo > 0 && n.t - list[lo - 1] <= tol)) hit++;
  }
  return hit / notes.length;
}

/**
 * Check one package directory.
 * @param {string} dir
 */
export async function checkSong(dir) {
  const fails = [], info = {};
  const song = JSON.parse(readFileSync(join(dir, 'song.json'), 'utf8'));
  const report = JSON.parse(readFileSync(join(dir, 'report.json'), 'utf8'));
  for (const k of ['format', 'id', 'title', 'bpm', 'sample_rate', 'length_s', 'tracks', 'beats', 'downbeat', 'sections', 'charts']) {
    if (song[k] === undefined) fails.push(`song.json has no ${k}`);
  }
  if (song.format !== 1) fails.push(`format ${song.format}, expected 1`);
  if (song.beats.some((b, i) => i && b <= song.beats[i - 1])) fails.push('beats not increasing');

  // tracks: decode, lengths, alignment against the source excerpts
  const decoded = {};
  for (const t of TRACKS) {
    const f = join(dir, song.tracks?.[t] ?? `${t}.ogg`);
    if (!existsSync(f)) { fails.push(`no ${t} track`); continue; }
    const bytes = readFileSync(f);
    const d = await decodeVorbisLib(bytes);
    decoded[t] = monoOf(d.ch);
    // the engine's decoder (stb_vorbis), when tools/stb-vorbis/build.mjs has built it: same length, same samples
    if (stbAvailable()) {
      const s = await decodeVorbisStb(bytes);
      const m = monoOf(s.ch);
      let diff = 0;
      for (let i = 0; i < Math.min(m.length, decoded[t].length); i++) diff = Math.max(diff, Math.abs(m[i] - decoded[t][i]));
      info[`${t}_stb_vs_libvorbis`] = +diff.toExponential(1);
      if (m.length !== decoded[t].length || diff > 1e-4) fails.push(`${t}: stb_vorbis decodes ${m.length} samples (max difference ${diff}), libvorbis ${decoded[t].length}`);
    } else info.stb_vorbis = 'not built (node tools/stb-vorbis/build.mjs)';
    if (d.rate !== song.sample_rate) fails.push(`${t}: ${d.rate} Hz, song.json says ${song.sample_rate}`);
    if (song.length_samples !== undefined && decoded[t].length !== song.length_samples) fails.push(`${t}: ${decoded[t].length} samples, song.json says ${song.length_samples}`);
    const ex = report.alignment?.[t]?.excerpt;
    if (!ex) { fails.push(`${t}: no alignment excerpt in report.json`); continue; }
    const lag = excerptLag(decoded[t], unpackExcerpt(ex.data), ex.start);
    info[`${t}_lag`] = lag.lagFine;
    // make-song proved 0 lag over the whole song against the full source; this re-check has only a 0.37 s excerpt,
    // and a lossy low band (the kick is little more than a 50 Hz sine) blurs the peak by a sample or so, so it
    // allows one sample: what it guards against is a codec delay or a resample, hundreds of samples
    if (Math.abs(lag.lag) > 1 || lag.peak < 0.3) fails.push(`${t}: ${lag.lagFine} samples off its source (correlation ${lag.peak})`);
  }
  const lens = new Set(Object.values(decoded).map((x) => x.length));
  if (lens.size > 1) fails.push(`tracks differ in length: ${[...lens].join(', ')}`);
  // control: a track moved by 64 samples must be caught
  if (decoded.kick && report.alignment?.kick?.excerpt) {
    const ex = report.alignment.kick.excerpt, moved = new Float32Array(decoded.kick.length);
    moved.set(decoded.kick.subarray(0, moved.length - 64), 64);
    const lag = excerptLag(moved, unpackExcerpt(ex.data), ex.start);
    info.control_shifted_track_lag = lag.lag;
    if (lag.lag === 0) fails.push('CONTROL: a kick track shifted by 64 samples passed the alignment check');
  }

  // charts
  const rate = song.sample_rate;
  for (const [name, lv] of Object.entries(LEVELS)) {
    const notes = song.charts?.[name];
    if (!Array.isArray(notes) || !notes.length) { fails.push(`no ${name} chart`); continue; }
    for (const p of chartProblems(notes, lv, song.beats).slice(0, 5)) fails.push(`${name}: ${p}`);
    const span = song.beats[song.beats.length - 1] - song.beats[0];
    const nps = notes.length / span;
    const playing = chartDensity(notes, song.beats, song.downbeat);
    info[`${name}_notes`] = notes.length;
    info[`${name}_per_second`] = +nps.toFixed(2);
    info[`${name}_per_second_playing`] = +playing.toFixed(2);
    if (playing > lv.density + 0.01) fails.push(`${name}: ${playing.toFixed(2)} notes/s in the bars with notes, the cap is ${lv.density}`);
    if (lv.bass) {
      const hard = song.charts?.hard ?? [];
      info[`${name}_bass_share`] = +(notes.filter((n) => n.lane === 2).length / notes.length).toFixed(3);
      const bp = bassProblem(notes, hard);
      if (bp) fails.push(`${name}: ${bp}`);
      // control: the same chart without its bass notes must fail (when the song has a bass line: an empty chart fails)
      if (bassProblem([], hard) && !bassProblem(notes.filter((n) => n.lane !== 2), hard)) fails.push(`CONTROL: ${name} with its bass notes taken out passed the bass check`);
    }
  }

  // onsets found again in the decoded tracks
  if (decoded.kick && decoded.clap && decoded.bass) {
    const clapBand = filtfilt(filtfilt(decoded.clap, biquad('highpass', 1000, rate)), biquad('lowpass', 5000, rate));
    const kick = bandOnsets(decoded.kick, rate, { fLo: 20, fHi: 160, gap: 0.05, k: 2, envMs: 8 }).onsets;
    const clap = bandOnsets(decoded.clap, rate, { fLo: 1000, fHi: 5000, gap: 0.05, k: 2, refineBand: clapBand }).onsets;
    const bass = bassOnsets(decoded.bass, rate, kick).onsets;
    const other = decoded.other ? bandOnsets(decoded.other, rate, { fLo: 80, fHi: 4000, gap: 0.1, k: 2 }).onsets : [];
    // lane 3 here is the other track: a break note the chart made from the other stem (report.json lists their steps)
    const onsets = [kick, clap, bass, other].map((l) => l.map((o) => o.t).sort((a, b) => a - b));
    const beat = (song.beats[song.beats.length - 1] - song.beats[0]) / (song.beats.length - 1), eighth = beat / 2;
    const bySource = (name) => {
      const fromOther = new Set(report.charts?.[name]?.other_steps ?? []);
      return (song.charts?.[name] ?? []).map((n) => (fromOther.has(n.step) ? { ...n, lane: 3 } : n));
    };
    for (const name of Object.keys(LEVELS)) {
      const notes = bySource(name);
      const share = onsetShare(notes, onsets);
      info[`${name}_onset_share`] = +share.toFixed(3);
      if (share < MIN_ONSET_SHARE) fails.push(`${name}: only ${(100 * share).toFixed(1)}% of notes on an onset (need ${100 * MIN_ONSET_SHARE}%)`);
      // the break notes on their own: they must land on real onsets as well as the rest
      const fill = new Set(report.charts?.[name]?.fill_steps ?? []);
      if (fill.size) {
        const fillShare = onsetShare(notes.filter((n) => fill.has(n.step)), onsets);
        info[`${name}_fill_notes`] = fill.size;
        info[`${name}_fill_onset_share`] = +fillShare.toFixed(3);
        if (fillShare < MIN_ONSET_SHARE) fails.push(`${name}: only ${(100 * fillShare).toFixed(1)}% of the break notes on an onset (need ${100 * MIN_ONSET_SHARE}%)`);
        const fromOther = notes.filter((n) => n.lane === 3);
        if (fromOther.length) info[`${name}_other_onset_share`] = +onsetShare(fromOther, onsets).toFixed(3);
      }
    }

    // gaps and the first note, against the onsets of all four tracks
    const any = onsets.flat().sort((a, b) => a - b);
    const silent = (t0, t1) => !any.some((t) => t > t0 + 0.05 && t < t1 - 0.05);
    const firstOnset = any[0] ?? 0, maxDelay = (2 * 16 * beat) / 4;
    info.first_onset_s = +firstOnset.toFixed(3);
    for (const name of Object.keys(LEVELS)) {
      const notes = song.charts?.[name] ?? [];
      let longest = 0;
      for (let i = 1; i < notes.length; i++) longest = Math.max(longest, notes[i].step - notes[i - 1].step);
      info[`${name}_first_note_s`] = notes[0]?.t;
      info[`${name}_longest_gap_bars`] = +(longest / 16).toFixed(2);
      for (const p of gapProblems(notes, name, silent).slice(0, 3)) fails.push(`${name}: ${p}`);
      const fp = firstNoteProblem(notes, firstOnset, maxDelay);
      if (fp) fails.push(`${name}: ${fp}`);
      // control: the same chart with a 30 s hole cut into its middle (notes left on both sides) must fail the gap check
      const mid = notes.length ? (notes[0].t + notes[notes.length - 1].t - 30) / 2 : 0;
      const holed = notes.filter((n) => n.t < mid || n.t > mid + 30);
      info[`control_${name}_30s_hole_problems`] = gapProblems(holed, name, silent).length;
      if (!gapProblems(holed, name, silent).length) fails.push(`CONTROL: the ${name} chart with a 30 s hole at ${mid.toFixed(1)} s passed the gap check`);
    }
    // control: the Hard chart without its first 30 s of notes must fail the first-note check
    const late = (song.charts?.hard ?? []).filter((n) => n.t > firstOnset + 30);
    info.control_hard_late_first_note_s = late[0]?.t;
    if (!firstNoteProblem(late, firstOnset, maxDelay)) fails.push('CONTROL: the Hard chart missing its first 30 s passed the first-note check');

    // control: the Hard chart moved by an 8th must score badly
    const hard = bySource('hard');
    const moved = onsetShare(hard.map((n) => ({ ...n, t: n.t + eighth })), onsets);
    info.control_hard_shifted_8th_share = +moved.toFixed(3);
    if (moved > CONTROL_MAX_RATIO * (info.hard_onset_share ?? 1)) {
      fails.push(`CONTROL: the Hard chart moved by an 8th still has ${(100 * moved).toFixed(1)}% of its notes on onsets: the onset check cannot tell a good chart from a bad one`);
    }
  }
  // the look: its shape, and the same look again from the decoded other track
  const lp = lookProblems(song.look, song.beats.length);
  fails.push(...lp);
  if (!lp.length && decoded.other) {
    const again = otherLook(decoded.other, song.sample_rate, song.beats, []);
    const lv = song.look.other.level;
    info.look_level_share = +lookLevelShare(lv, again.level).toFixed(3);
    if (info.look_level_share < LOOK_LEVEL_SHARE) fails.push(`look: only ${(100 * info.look_level_share).toFixed(1)}% of beats' loudness match the decoded other track (need ${100 * LOOK_LEVEL_SHARE}%)`);
    const moved = [...lv.slice(2), 0, 0];
    info.control_look_moved_2_beats_share = +lookLevelShare(moved, again.level).toFixed(3);
    if (info.control_look_moved_2_beats_share >= LOOK_LEVEL_SHARE) fails.push('CONTROL: the look moved by two beats still matches the decoded other track: the look check cannot see a misplaced look');
  }
  return { dir, fails, info };
}

async function main() {
  const args = process.argv.slice(2);
  const root = resolve(dirname(fileURLToPath(import.meta.url)), '../songs'); // the game's songs, from any directory
  const dirs = args.length ? args.map((a) => resolve(a))
    : readdirSync(root, { withFileTypes: true }).filter((d) => d.isDirectory() && existsSync(join(root, d.name, 'song.json'))).map((d) => join(root, d.name));
  let failed = 0;
  for (const dir of dirs) {
    const r = await checkSong(dir);
    console.log(`${r.fails.length ? 'FAIL' : 'ok  '} ${dir}`);
    console.log(`     ${Object.entries(r.info).map(([k, v]) => `${k}=${v}`).join(' ')}`);
    for (const f of r.fails) console.log(`     - ${f}`);
    if (r.fails.length) failed++;
  }
  if (!dirs.length) { console.log('no song packages found'); process.exit(1); }
  process.exit(failed ? 1 : 0);
}

if (import.meta.url === `file://${process.argv[1]}`) await main();
