#!/usr/bin/env node
// Make a song package for the game from an NI Stems file (Loukai's .stem.mp4) or a directory of stem WAVs:
//
//   node tools/make-song.mjs "<file.stem.mp4 | stems-dir>" --out songs/<id> [--title ..] [--artist ..]
//                            [--bpm-min 115] [--bpm-max 135] [--keep-vocals]
//
// The pipeline (docs/DESIGN.md, "Charts"): decode, split the drum stem at 150 Hz into the kick and clap tracks,
// track the tempo and beats, find each lane's onsets, quantize them to 16ths, pick the three difficulties, find the
// sections; then write the four Ogg Vorbis tracks (proved sample-aligned), song.json, preview.wav and report.json.
// The master and vocals stems are never written (--keep-vocals mixes the vocals stem into the other track, for a
// split that filed an instrumental lead under vocals).
import { basename, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { readStems } from './lib/stems.mjs';
import { analyze, monoOf } from './lib/analyze.mjs';
import { quantize, makeCharts } from './lib/chart.mjs';
import { writePackage } from './lib/package.mjs';

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
 * @param {{out: string, id?: string, title?: string, artist?: string, bpmMin?: number, bpmMax?: number, keepVocals?: boolean}} o
 */
export async function makeSong(input, o) {
  const stems = await readStems(input);
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
  const playSeconds = a.beats[a.beats.length - 1] - a.beats[0];
  const { charts, stats, patterns } = makeCharts(cands, a.beats, a.downbeat, playSeconds);
  const id = o.id ?? basename(resolve(o.out));
  const title = o.title ?? stems.info?.title ?? basename(input).replace(/\.stem\.(mp4|m4a)$/i, '');
  const report = {
    source: { ...stems.info, keep_vocals: !!o.keepVocals, rate: stems.rate, length_s: +a.length.toFixed(3),
      levels_dbfs: Object.fromEntries(Object.entries(stems.tracks).map(([k, v]) => [k, dbfs(monoOf(v))])) },
    tempo: { bpm: +a.bpm.toFixed(3), autocorr_bpm: +a.bpmAutocorr.toFixed(3), beats: a.beats.length, downbeat: a.downbeat },
    onsets: quant,
    clap_rejected: { kick_clicks: a.lanes.clap.rejected?.length ?? 0, hats: a.lanes.clap.hats?.length ?? 0 },
    bass: { bleed_energy_removed: +a.lanes.bass.bleedRemoved.toFixed(4), attacks_rejected_as_kick: a.lanes.bass.rejected.length,
      pitch_change_notes: a.lanes.bass.onsets.filter((x) => x.how === 'pitch').length },
    sections: a.sections,
    charts: stats,
    patterns,
  };
  const r = await writePackage(resolve(o.out), {
    id, title, artist: o.artist ?? stems.info?.artist ?? '', rate: a.rate, tracks: a.tracks, bpm: a.bpm, beats: a.beats,
    downbeat: a.downbeat, sections: a.sections, charts, report,
  });
  return r;
}

async function main() {
  const { values: v, positionals } = parseArgs({
    allowPositionals: true,
    options: {
      out: { type: 'string' }, id: { type: 'string' }, title: { type: 'string' }, artist: { type: 'string' },
      'bpm-min': { type: 'string' }, 'bpm-max': { type: 'string' }, 'keep-vocals': { type: 'boolean' },
    },
  });
  const input = positionals[0];
  if (!input) {
    console.error('usage: node tools/make-song.mjs <file.stem.mp4 | stems-dir> [--out songs/<id>] [--title ..] [--artist ..]');
    process.exit(2);
  }
  const out = v.out ?? `songs/${slug(basename(input))}`;
  const t0 = Date.now();
  const { song, report } = await makeSong(input, {
    out, id: v.id, title: v.title, artist: v.artist, keepVocals: v['keep-vocals'],
    bpmMin: v['bpm-min'] ? Number(v['bpm-min']) : undefined, bpmMax: v['bpm-max'] ? Number(v['bpm-max']) : undefined,
  });
  const bytes = Object.values(report.alignment).reduce((s, x) => s + x.bytes, 0);
  console.log(`${out}: ${song.title}, ${song.bpm} BPM, ${song.length_s} s, ${song.beats.length} beats, downbeat ${song.downbeat}`);
  console.log(`  sections: ${song.sections.map((s) => `${s.kind}@${s.t.toFixed(1)}`).join(' ')}`);
  for (const [k, s] of Object.entries(report.charts)) console.log(`  ${k}: ${s.notes} notes, ${s.per_second}/s`);
  console.log(`  tracks ${(bytes / 1e6).toFixed(2)} MB (${((bytes / 1e6) * 60 / song.length_s).toFixed(2)} MB/min), aligned (0 lag, ${song.length_samples} samples each), ${((Date.now() - t0) / 1000).toFixed(0)} s`);
}

if (import.meta.url === `file://${process.argv[1]}`) await main();
