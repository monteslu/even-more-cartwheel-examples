// Unit tests of the chart half: quantizing, the gap rules, bar patterns, and the checks with their controls.
// Run: node --test tools/
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { quantize, stepTime, selectNotes, enforceGaps, makeCharts, LEVELS, chartDensity } from './lib/chart.mjs';
import { chartProblems, onsetShare } from './check-songs.mjs';
import { rng } from './lib/dsp.mjs';
import { makeSong } from './lib/synth.mjs';
import { excerptLag, unpackExcerpt } from './lib/package.mjs';

const beats = Array.from({ length: 64 }, (_, i) => 0.5 + i * 0.5);

test('stepTime: 16ths between beats, extended past the last beat', () => {
  assert.equal(stepTime(beats, 0), 0.5);
  assert.equal(stepTime(beats, 2), 0.75);
  assert.equal(stepTime(beats, 4 * 63 + 4), 0.5 + 64 * 0.5);
});

test('quantize: snaps to the nearest 16th and drops an onset more than 40% of a 16th away', () => {
  const s16 = 0.125;
  const q = quantize([{ t: 1.0 + 0.3 * s16, strength: 1 }, { t: 1.5 + 0.45 * s16, strength: 1 }, { t: 2.0 - 0.1 * s16, strength: 2 }], beats);
  assert.deepEqual(q.map((x) => x.step), [4, 12]);
  assert.ok(Math.abs(q[0].err - 0.3) < 1e-9);
});

test('selectNotes: never breaks a level gap, on random candidates', () => {
  const r = rng(12);
  for (const [name, lv] of Object.entries(LEVELS)) {
    for (let trial = 0; trial < 20; trial++) {
      const cands = [];
      for (let s = 0; s < 128; s++) for (let lane = 0; lane < 3; lane++) if (r() < 0.4) cands.push({ step: s, lane, score: r() * 2 });
      const notes = selectNotes(cands, lv, 0.2);
      assert.deepEqual(chartProblems(notes.map((n) => ({ ...n, t: stepTime(beats, n.step) })), lv, beats), [], name);
      assert.ok(notes.length > 0);
    }
  }
});

test('selectNotes: picks the better of two notes that cannot both be played', () => {
  const lv = LEVELS.hard;
  const notes = selectNotes([{ step: 0, lane: 0, score: 1 }, { step: 1, lane: 1, score: 2 }], lv, 0);
  assert.deepEqual(notes.map((n) => n.lane), [1]);
  // one note per step, even on the same step
  assert.equal(selectNotes([{ step: 4, lane: 0, score: 1 }, { step: 4, lane: 1, score: 1.5 }], lv, 0).length, 1);
});

test('enforceGaps: where two patterns meet, the weaker note goes', () => {
  const out = enforceGaps([{ step: 0, lane: 0, score: 1 }, { step: 1, lane: 2, score: 3 }, { step: 4, lane: 1, score: 1 }], LEVELS.hard);
  assert.deepEqual(out.map((n) => n.step), [1, 4]);
});

test('makeCharts on the synthetic song: gaps held, easy within hard\'s steps, bars repeat their pattern', () => {
  const s = makeSong({ length: 40, rate: 8000 });
  const t = s.truth;
  const cands = t.notes.map((n) => ({ step: n.step, lane: n.lane, strength: 1 }));
  const { charts, patterns } = makeCharts(cands, t.beats, 0, t.beats.at(-1) - t.beats[0]);
  for (const [name, lv] of Object.entries(LEVELS)) {
    assert.deepEqual(chartProblems(charts[name], lv, t.beats), [], name);
    assert.ok(chartDensity(charts[name], t.beats, 0) <= lv.density + 1e-9, name);
    // every chart note is a real note of its lane
    for (const n of charts[name]) assert.ok(t.notes.some((m) => m.step === n.step && m.lane === n.lane));
  }
  assert.ok(charts.easy.length < charts.medium.length && charts.medium.length < charts.hard.length);
  assert.ok(patterns.distinct < patterns.bars / 2, 'alike bars share patterns');
  // two bars of the same drop section in the same place of the chord loop chart alike
  const barNotes = (b) => charts.hard.filter((n) => Math.floor(n.step / 16) === b).map((n) => `${n.step % 16}:${n.lane}`).join(' ');
  assert.equal(barNotes(5), barNotes(9));
});

test('chartProblems: catches each broken rule (the controls)', () => {
  const lv = LEVELS.medium;
  const ok = [{ t: stepTime(beats, 0), step: 0, lane: 0 }, { t: stepTime(beats, 4), step: 4, lane: 1 }];
  assert.deepEqual(chartProblems(ok, lv, beats), []);
  assert.ok(chartProblems([ok[0], { ...ok[1], step: 2, t: stepTime(beats, 2) }], lv, beats).some((p) => p.includes('lane change')));
  assert.ok(chartProblems([ok[0], { ...ok[0] }], lv, beats).length > 0, 'two notes in a step');
  assert.ok(chartProblems([{ t: stepTime(beats, 1), step: 1, lane: 0 }], lv, beats).some((p) => p.includes('grid')));
  assert.ok(chartProblems([{ t: 9, step: 1 * 4, lane: 0 }], lv, beats).some((p) => p.includes('time')));
});

test('onsetShare: a chart on its onsets scores 1, the same chart an 8th late scores 0', () => {
  const notes = [0, 4, 8].map((s) => ({ t: stepTime(beats, s), lane: 0 }));
  const onsets = [notes.map((n) => n.t + 0.003), [], []];
  assert.equal(onsetShare(notes, onsets), 1);
  assert.equal(onsetShare(notes.map((n) => ({ ...n, t: n.t + 0.25 })), onsets), 0);
});

test('excerptLag: 0 for the source, the shift for a shifted copy', () => {
  const r = rng(2), x = Float32Array.from({ length: 50000 }, () => 2 * r() - 1);
  const start = 20000, ex = new Int16Array(8192);
  for (let i = 0; i < 8192; i++) ex[i] = Math.round(x[start + i] * 32767);
  const data = unpackExcerpt(Buffer.from(ex.buffer).toString('base64'));
  assert.equal(excerptLag(x, data, start).lag, 0);
  const moved = new Float32Array(x.length);
  moved.set(x.subarray(0, x.length - 64), 64);
  assert.equal(excerptLag(moved, data, start).lag, 64);
});
