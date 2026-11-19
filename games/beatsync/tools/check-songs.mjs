#!/usr/bin/env node
// The song checks (docs/DESIGN.md, "Charts" step 8), over every songs/*/ package or the ones named:
//
//   node tools/check-songs.mjs [songs/<id> ...]
//
// For each package: song.json's shape; the tracks decode (libvorbis) to the same rate and exactly length_samples
// each, and each is at 0 samples' lag against the excerpt of its source kept in report.json; every chart sorted, at
// most one note per step, on its level's grid, every gap of its level held, note times on the beat grid, notes per
// second under the level's cap; the share of each chart's notes that land on an onset found again in the decoded
// track of its lane (within 50 ms). Two controls must fail, or the check that should catch them is reported broken:
// the Hard chart moved by an 8th scores badly against the audio, and a track shifted by 64 samples is caught by the
// alignment check. Exits 1 on any failure.
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { LEVELS, stepTime, chartDensity } from './lib/chart.mjs';
import { decodeVorbisLib } from './lib/ogg.mjs';
import { stbAvailable, decodeVorbisStb } from './lib/stbvorbis.mjs';
import { excerptLag, unpackExcerpt, TRACKS } from './lib/package.mjs';
import { bandOnsets, bassOnsets, monoOf } from './lib/analyze.mjs';
import { biquad, filtfilt } from './lib/dsp.mjs';

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
  }

  // onsets found again in the decoded tracks
  if (decoded.kick && decoded.clap && decoded.bass) {
    const clapBand = filtfilt(filtfilt(decoded.clap, biquad('highpass', 1000, rate)), biquad('lowpass', 5000, rate));
    const kick = bandOnsets(decoded.kick, rate, { fLo: 20, fHi: 160, gap: 0.05, k: 2, envMs: 8 }).onsets;
    const clap = bandOnsets(decoded.clap, rate, { fLo: 1000, fHi: 5000, gap: 0.05, k: 2, refineBand: clapBand }).onsets;
    const bass = bassOnsets(decoded.bass, rate, kick).onsets;
    const onsets = [kick, clap, bass].map((l) => l.map((o) => o.t).sort((a, b) => a - b));
    const eighth = (song.beats[song.beats.length - 1] - song.beats[0]) / (song.beats.length - 1) / 2;
    for (const name of Object.keys(LEVELS)) {
      const notes = song.charts?.[name] ?? [];
      const share = onsetShare(notes, onsets);
      info[`${name}_onset_share`] = +share.toFixed(3);
      if (share < MIN_ONSET_SHARE) fails.push(`${name}: only ${(100 * share).toFixed(1)}% of notes on an onset (need ${100 * MIN_ONSET_SHARE}%)`);
    }
    // control: the Hard chart moved by an 8th must score badly
    const hard = song.charts?.hard ?? [];
    const moved = onsetShare(hard.map((n) => ({ ...n, t: n.t + eighth })), onsets);
    info.control_hard_shifted_8th_share = +moved.toFixed(3);
    if (moved > CONTROL_MAX_RATIO * (info.hard_onset_share ?? 1)) {
      fails.push(`CONTROL: the Hard chart moved by an 8th still has ${(100 * moved).toFixed(1)}% of its notes on onsets: the onset check cannot tell a good chart from a bad one`);
    }
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
