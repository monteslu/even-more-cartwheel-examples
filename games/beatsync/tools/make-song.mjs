#!/usr/bin/env node
// Make a song package for the game from an NI Stems file (Loukai's .stem.mp4) or a directory of stem WAVs:
//
//   node tools/make-song.mjs "<file.stem.mp4 | stems-dir>" --out songs/<id> [--title ..] [--artist ..]
//                            [--bpm-min 115] [--bpm-max 135] [--keep-vocals] [--charts-only]
//
// The pipeline (docs/DESIGN.md, "Charts"): decode, split the drum stem at 150 Hz into the kick and clap tracks,
// track the tempo and beats, find each lane's onsets, quantize them to 16ths, pick the three difficulties, find the
// sections; then write the four Ogg Vorbis tracks (proved sample-aligned), song.json, preview.wav and report.json.
// The master stem is never written, and the vocals only with --vocals (a fifth track, vocals.ogg, the game plays as
// recorded and never reacts to; for a song with its vocals, never for the instrumental songs that ship) (--keep-vocals
// instead mixes the vocals stem into the other track, for a split that filed an instrumental lead under vocals). --charts-only re-charts an existing package from its source:
// the analysis and charts run again and only song.json, report.json and preview.wav are rewritten; the Ogg tracks
// and their alignment proof stay as they are (the source must be the same audio: same length in samples).
import { readFileSync, writeFileSync } from 'node:fs';
import { basename, join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { readStems } from './lib/stems.mjs';
import { analyze, monoOf, bandOnsets, OTHER_ONSETS } from './lib/analyze.mjs';
import { otherLook } from './lib/look.mjs';
import { quantize, makeCharts, makePairCharts, PAIRS, stepTime } from './lib/chart.mjs';
import { writePackage, writeCharts, readSongJson, formatSong } from './lib/package.mjs';

/** "Late Night Deep House.stem.mp4" -> "late-night-deep-house" */
export function slug(s) {
  return s.replace(/\.stem\.(mp4|m4a)$/i, '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
}

/** RMS level in dBFS of a mono signal. */
function dbfs(x) {
  let s = 0;
  for (const v of x) s += v * v;
  return +(10 * Math.log10(s / x.length + 1e-20)).toFixed(1);
}

/**
 * Analyse the stems and write the package.
 * @param {string} input
 * @param {{out: string, id?: string, title?: string, artist?: string, bpmMin?: number, bpmMax?: number, keepVocals?: boolean,
 *   chartsOnly?: boolean}} o
 */
export async function makeSong(input, o) {
  if (o.chartsOnly) {
    // a re-chart keeps the package's title, artist and id, and the flags it was made with, unless told otherwise
    const prev = readSongJson(resolve(o.out));
    const rep = JSON.parse(readFileSync(join(resolve(o.out), 'report.json'), 'utf8'));
    const range = rep.tempo?.bpm_range ?? [];
    o = { ...o, id: o.id ?? prev.id, title: o.title ?? prev.title, artist: o.artist ?? prev.artist,
      keepVocals: o.keepVocals ?? rep.source?.keep_vocals, bpmMin: o.bpmMin ?? range[0], bpmMax: o.bpmMax ?? range[1] };
  }
  const stems = await readStems(input);
  if (o.vocals && !stems.tracks.vocals) throw new Error(`${input}: --vocals, but the stems have no vocals`);
  if (o.keepVocals && stems.tracks.vocals) {
    // some splits file a lead line or chops under vocals: fold it into the other track instead of losing it
    stems.tracks.other = stems.tracks.other.map((c, k) => Float32Array.from(c, (v, i) => v + stems.tracks.vocals[k][i]));
  }
  const a = analyze(stems, { bpmMin: o.bpmMin, bpmMax: o.bpmMax });
  const lanes = ['kick', 'clap', 'bass'];
  const cands = [], quant = {};
  lanes.forEach((name, lane) => {
    const q = quantize(a.lanes[name].onsets, a.beats);
    quant[name] = {
      onsets: a.lanes[name].onsets.length, on_grid: q.length,
      mean_abs_err_16ths: q.length ? +(q.reduce((s, x) => s + Math.abs(x.err), 0) / q.length).toFixed(3) : 0,
    };
    for (const x of q) cands.push({ step: x.step, lane, strength: x.strength });
  });
  cands.sort((x, y) => x.step - y.step || x.lane - y.lane);
  // fill-only onsets for the breaks (docs/DESIGN.md, "Charts" step 8): the hats the clap lane rejects, and the other
  // stem's onsets (chords, stabs, synth lines) for where the drums and the bass are both silent
  const extra = [];
  for (const [src, list] of [['hat', a.lanes.clap.hats ?? []], ['other', a.lanes.other.onsets]]) {
    for (const x of quantize(list, a.beats)) extra.push({ step: x.step, strength: x.strength, src });
  }
  const playSeconds = a.beats[a.beats.length - 1] - a.beats[0];
  const { charts, stats, patterns, span } = makeCharts(cands, a.beats, a.downbeat, playSeconds, extra, 4 * a.beatsAdded);
  // the two-lane charts (docs/DESIGN.md "Two lanes"): the drums (kick and clap, the hats filling their breaks) from the
  // drums' first beat, the groove (bass and the synth: the other stem's onsets) from the top
  const synth = quantize(a.lanes.other.onsets, a.beats).map((x) => ({ step: x.step, lane: 3, strength: x.strength }));
  const partCands = [...cands, ...synth].sort((x, y) => x.step - y.step || x.lane - y.lane);
  const hats = extra.filter((x) => x.src === 'hat');
  // the drums' high hand plays the claps and the hats (the clap track carries both): a hat is a candidate on the clap,
  // weaker than a clap, so Easy keeps the claps and Medium and Hard add the hats between them
  const hatCands = hats.filter((h) => !cands.some((c) => c.step === h.step && c.lane === 1)).map((h) => ({ step: h.step, lane: 1, strength: 0.5 * h.strength }));
  const drums = makePairCharts([...partCands, ...hatCands].sort((x, y) => x.step - y.step || x.lane - y.lane), a.beats, a.downbeat, playSeconds, PAIRS.drums, [], 4 * a.beatsAdded, charts);
  const groove = makePairCharts(partCands, a.beats, a.downbeat, playSeconds, PAIRS.groove, [], 0, charts);
  const pairs = { drums: drums.charts, groove: groove.charts };
  const id = o.id ?? basename(resolve(o.out));
  const title = o.title ?? stems.info?.title ?? basename(input).replace(/\.stem\.(mp4|m4a)$/i, '');
  const report = {
    source: { ...stems.info, keep_vocals: !!o.keepVocals, rate: stems.rate, length_s: +a.length.toFixed(3),
      levels_dbfs: Object.fromEntries(Object.entries(stems.tracks).map(([k, v]) => [k, dbfs(monoOf(v))])) },
    tempo: { bpm: +a.bpm.toFixed(3), autocorr_bpm: +a.bpmAutocorr.toFixed(3), beats: a.beats.length, downbeat: a.downbeat,
      beats_before_the_drums: a.beatsAdded, bpm_range: [o.bpmMin ?? 115, o.bpmMax ?? 135] },
    onsets: quant,
    fill_onsets: { hats: a.lanes.clap.hats?.length ?? 0, other: a.lanes.other.onsets.length, other_on_grid: extra.filter((x) => x.src === 'other').length },
    music_span: { first_step: span.first, last_step: span.last, first_s: +stepTime(a.beats, span.first).toFixed(4), last_s: +stepTime(a.beats, span.last).toFixed(4) },
    clap_rejected: { kick_clicks: a.lanes.clap.rejected?.length ?? 0, hats: a.lanes.clap.hats?.length ?? 0 },
    bass: { bleed_energy_removed: +a.lanes.bass.bleedRemoved.toFixed(4), attacks_rejected_as_kick: a.lanes.bass.rejected.length,
      pitch_change_notes: a.lanes.bass.onsets.filter((x) => x.how === 'pitch').length },
    sections: a.sections,
    charts: stats,
    pairs: { drums: drums.stats, groove: groove.stats },
    patterns,
  };
  const write = o.chartsOnly ? writeCharts : writePackage;
  const r = await write(resolve(o.out), {
    id, title, artist: o.artist ?? stems.info?.artist ?? '', rate: a.rate, tracks: a.tracks, bpm: a.bpm, beats: a.beats,
    downbeat: a.downbeat, sections: a.sections, look: lookOf(a.tracks.other, a.rate, a.beats, a.lanes.other.onsets), charts, pairs, report,
    ...(o.vocals ? { tracks: { ...a.tracks, vocals: stems.tracks.vocals } } : {}),
  });
  return r;
}

/** the picture's reading of the other track (lib/look.mjs), rounded beats as song.json has them */
function lookOf(otherTrack, rate, beats, onsets) {
  return { other: otherLook(monoOf(otherTrack), rate, beats.map((b) => +b.toFixed(4)), onsets) };
}

/**
 * --look-only: add (or redo) song.json's "look" from the package's source, keeping every other key as it is: the
 * charts, beats and tracks are not touched (the source must be the same audio: same length in samples).
 * @param {string} input
 * @param {string} out
 */
export async function addLook(input, out) {
  const dir = resolve(out);
  const prev = readSongJson(dir);
  const rep = JSON.parse(readFileSync(join(dir, 'report.json'), 'utf8'));
  const stems = await readStems(input);
  if (rep.source?.keep_vocals && stems.tracks.vocals) {
    stems.tracks.other = stems.tracks.other.map((c, k) => Float32Array.from(c, (v, i) => v + stems.tracks.vocals[k][i]));
  }
  const n = stems.tracks.other[0].length;
  if (n !== prev.length_samples) throw new Error(`${dir}: the package is ${prev.length_samples} samples, the source ${n}: not the same audio`);
  const otherM = monoOf(stems.tracks.other);
  const onsets = bandOnsets(otherM, stems.rate, OTHER_ONSETS).onsets;
  const look = lookOf(stems.tracks.other, stems.rate, prev.beats, onsets);
  const { charts, pairs, look: _old, ...rest } = prev;
  const song = { ...rest, look, charts, ...(pairs ? { pairs } : {}) };
  writeFileSync(join(dir, 'song.json'), formatSong(song));
  return { song };
}

async function main() {
  const { values: v, positionals } = parseArgs({
    allowPositionals: true,
    options: {
      out: { type: 'string' }, id: { type: 'string' }, title: { type: 'string' }, artist: { type: 'string' },
      'bpm-min': { type: 'string' }, 'bpm-max': { type: 'string' }, 'keep-vocals': { type: 'boolean' }, vocals: { type: 'boolean' },
      'charts-only': { type: 'boolean' }, 'look-only': { type: 'boolean' },
    },
  });
  const input = positionals[0];
  if (!input) {
    console.error('usage: node tools/make-song.mjs <file.stem.mp4 | stems-dir> [--out songs/<id>] [--title ..] [--artist ..]');
    process.exit(2);
  }
  const out = v.out ?? `songs/${slug(basename(input))}`;
  const t0 = Date.now();
  if (v['look-only']) {
    const { song } = await addLook(input, out);
    const o = song.look.other;
    console.log(`${out}: look from the other track: ${o.level.length} beats, ${o.changes.length} harmony changes, ${o.stabs.length / 2} stabs, ${((Date.now() - t0) / 1000).toFixed(0)} s`);
    return;
  }
  const { song, report } = await makeSong(input, {
    out, id: v.id, title: v.title, artist: v.artist, keepVocals: v['keep-vocals'], vocals: v.vocals, chartsOnly: v['charts-only'],
    bpmMin: v['bpm-min'] ? Number(v['bpm-min']) : undefined, bpmMax: v['bpm-max'] ? Number(v['bpm-max']) : undefined,
  });
  const bytes = Object.values(report.alignment).reduce((s, x) => s + x.bytes, 0);
  console.log(`${out}: ${song.title}, ${song.bpm} BPM, ${song.length_s} s, ${song.beats.length} beats, downbeat ${song.downbeat}`);
  console.log(`  sections: ${song.sections.map((s) => `${s.kind}@${s.t.toFixed(1)}`).join(' ')}`);
  for (const [k, s] of Object.entries(report.charts)) {
    console.log(`  ${k}: ${s.notes} notes (${s.fill} in the breaks, ${s.other_steps.length} from the other stem), ${s.per_second}/s${s.holes_left.length ? `, ${s.holes_left.length} holes left` : ''}`);
  }
  for (const [pn, lv] of Object.entries(report.pairs ?? {}))
    console.log(`  two lanes, ${pn}: ${Object.entries(lv).map(([k, s]) => `${k} ${s.notes} (${s.parts.join('+')}) ${s.per_second_playing}/s${s.holes_left.length ? ` ${s.holes_left.length} holes` : ''}`).join(', ')}`);
  console.log(`  tracks ${(bytes / 1e6).toFixed(2)} MB (${((bytes / 1e6) * 60 / song.length_s).toFixed(2)} MB/min), aligned (0 lag, ${song.length_samples} samples each), ${((Date.now() - t0) / 1000).toFixed(0)} s`);
}

if (import.meta.url === `file://${process.argv[1]}`) await main();
