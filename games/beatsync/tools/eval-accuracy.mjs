#!/usr/bin/env node
// Accuracy of the analysis against the synthetic songs' ground truth: tempo error, beat-time error, the downbeat,
// and per-lane onset precision and recall before any difficulty selection (raw onsets within 50 ms, and quantized
// steps exactly). Makes the plain, drift (+0.3%) and bleed (kick in the bass stem) variants under out/test-songs/
// if they are missing, and reads each through the real import path (the .stem.mp4, AAC decoded) by default.
//
//   node tools/eval-accuracy.mjs [--length 90] [--source mp4|wav] [--json out/accuracy.json]
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { VARIANTS, writeTestSong } from './make-test-song.mjs';
import { readStems } from './lib/stems.mjs';
import { analyze, monoOf } from './lib/analyze.mjs';
import { quantize, stepTime } from './lib/chart.mjs';
import { xcorrLag } from './lib/dsp.mjs';
import { decodeWav } from './lib/wav.mjs';

/**
 * Match detected times to true times one to one within `tol` seconds (greedy by distance).
 * @param {number[]} det
 * @param {number[]} truth
 * @param {number} tol
 */
export function matchTimes(det, truth, tol) {
  const pairs = [];
  for (let i = 0; i < det.length; i++) for (let j = 0; j < truth.length; j++) {
    const d = Math.abs(det[i] - truth[j]);
    if (d <= tol) pairs.push([d, i, j]);
  }
  pairs.sort((a, b) => a[0] - b[0]);
  const ud = new Set(), ut = new Set(), errs = [];
  for (const [, i, j] of pairs) {
    if (ud.has(i) || ut.has(j)) continue;
    ud.add(i); ut.add(j); errs.push(det[i] - truth[j]);
  }
  const tp = errs.length;
  return {
    tp, fp: det.length - tp, fn: truth.length - tp,
    precision: det.length ? tp / det.length : 1, recall: truth.length ? tp / truth.length : 1,
    meanAbsMs: tp ? (1000 * errs.reduce((s, e) => s + Math.abs(e), 0)) / tp : 0,
    meanMs: tp ? (1000 * errs.reduce((s, e) => s + e, 0)) / tp : 0,
  };
}

const r3 = (x) => +x.toFixed(3);

/**
 * Evaluate one variant directory (stems/, song.stem.mp4, truth.json).
 * @param {string} dir
 * @param {'mp4'|'wav'} source
 */
export async function evaluate(dir, source = 'mp4') {
  const truth = JSON.parse(readFileSync(join(dir, 'truth.json'), 'utf8'));
  const stems = await readStems(source === 'mp4' ? join(dir, 'song.stem.mp4') : join(dir, 'stems'));
  // the AAC encoder's priming delays the decoded audio; measure it against the true master and take it off
  const ref = monoOf(decodeWav(readFileSync(join(dir, 'stems', 'master.wav'))).ch);
  const lag = source === 'mp4' ? xcorrLag(ref, monoOf(stems.tracks.master ?? stems.tracks.drums), 4096, { start: Math.round(5 * stems.rate), len: 10 * stems.rate }).lag : 0;
  const shift = lag / stems.rate;
  const a = analyze(stems);
  const beats = a.beats.map((t) => t - shift);
  const tb = truth.beats;
  const trueBpm = (60 * (tb.length - 1)) / (tb[tb.length - 1] - tb[0]);
  const beatErr = tb.map((t) => Math.min(...beats.map((b) => Math.abs(b - t))));
  const beatLen = 60 / trueBpm;
  // downbeat: the detected first downbeat must fall on a true bar line (truth's downbeat is beat 0)
  const db = beats[a.downbeat];
  const trueIx = tb.findIndex((t) => Math.abs(t - db) < beatLen / 4);
  const lanes = {};
  for (const [lane, name] of [[0, 'kick'], [1, 'clap'], [2, 'bass']]) {
    const det = a.lanes[name].onsets.map((o) => o.t - shift);
    const tru = truth.notes.filter((n) => n.lane === lane);
    const raw = matchTimes(det, tru.map((n) => n.t), 0.05);
    const tight = matchTimes(det, tru.map((n) => n.t), 0.025);
    // quantized: detected steps (on the detected grid) mapped to true steps through their grid time
    const q = quantize(a.lanes[name].onsets, a.beats);
    const qt = q.map((x) => stepTime(a.beats, x.step) - shift);
    const qm = matchTimes(qt, tru.map((n) => n.t), 0.02);
    const legato = tru.filter((n) => n.kind === 'legato');
    lanes[name] = {
      truth: tru.length, detected: det.length,
      precision: r3(raw.precision), recall: r3(raw.recall), mean_abs_ms: r3(raw.meanAbsMs), mean_ms: r3(raw.meanMs),
      recall_25ms: r3(tight.recall),
      quantized: { precision: r3(qm.precision), recall: r3(qm.recall), dropped_off_grid: a.lanes[name].onsets.length - q.length },
      ...(legato.length ? { legato_recall: r3(matchTimes(det, legato.map((n) => n.t), 0.05).recall) } : {}),
      ...(name === 'bass' ? { bleed_rejected: a.lanes.bass.rejected.length } : {}),
    };
  }
  return {
    variant: dir.split('/').pop(), source, aac_priming_samples: lag,
    tempo: { truth_bpm: r3(trueBpm), detected_bpm: r3(a.bpm), autocorr_bpm: r3(a.bpmAutocorr), error_pct: r3((100 * (a.bpm - trueBpm)) / trueBpm) },
    beats: {
      truth: tb.length, detected: beats.length,
      mean_abs_ms: r3((1000 * beatErr.reduce((s, e) => s + e, 0)) / beatErr.length),
      max_abs_ms: r3(1000 * Math.max(...beatErr)),
      within_10ms: r3(beatErr.filter((e) => e < 0.01).length / beatErr.length),
    },
    downbeat: { detected_beat: a.downbeat, on_bar_line: trueIx >= 0 && trueIx % 4 === truth.downbeat % 4 },
    sections: { truth: truth.sections.map((s) => `${s.kind}@${s.t.toFixed(1)}`).join(' '), detected: a.sections.map((s) => `${s.kind}@${(s.t - shift).toFixed(1)}`).join(' ') },
    lanes,
  };
}

async function main() {
  const { values: o } = parseArgs({ options: { length: { type: 'string' }, source: { type: 'string' }, json: { type: 'string' }, root: { type: 'string' } } });
  const root = resolve(o.root ?? 'out/test-songs');
  const results = [];
  for (const [name, v] of Object.entries(VARIANTS)) {
    const dir = join(root, name);
    if (!existsSync(join(dir, 'truth.json'))) await writeTestSong(dir, { ...v, length: o.length ? Number(o.length) : undefined });
    const r = await evaluate(dir, /** @type {'mp4'|'wav'} */ (o.source ?? 'mp4'));
    results.push(r);
    console.log(JSON.stringify(r, null, 1));
  }
  if (o.json) writeFileSync(o.json, `${JSON.stringify(results, null, 1)}\n`);
}

if (import.meta.url === `file://${process.argv[1]}`) await main();
