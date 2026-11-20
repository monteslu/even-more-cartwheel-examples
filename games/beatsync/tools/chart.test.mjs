// Unit tests of the chart half: quantizing, the gap rules, bar patterns, the break fill, the beat grid carried back
// over a drumless intro, and the checks with their controls.
// Run: node --test tools/
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { quantize, stepTime, selectNotes, enforceGaps, makeCharts, LEVELS, chartDensity, MAX_GAP, fillGaps, FILL, chartHoles } from './lib/chart.mjs';
import { chartProblems, onsetShare, gapProblems, firstNoteProblem } from './check-songs.mjs';
import { extendBeats } from './lib/analyze.mjs';
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

const longest = (notes) => notes.reduce((m, n, i) => (i ? Math.max(m, n.step - notes[i - 1].step) : 0), 0);

test('fillGaps: a 12-bar hole is filled from the lanes first, the other stem only where they are silent', () => {
  const notes = [0, 4, 8, 12, 208, 212].map((step) => ({ step, lane: 0, score: 1 }));
  // hats over bars 2 to 6, the other stem on every 16th from bar 1 to bar 13
  const hats = [];
  for (let st = 32; st < 96; st += 8) hats.push({ step: st, lane: 1, score: 0.6, src: 'hat' });
  const other = [];
  for (let st = 16; st < 208; st++) other.push({ step: st, lane: 2, score: st % 4 ? 0.3 : 0.7, src: 'other' });
  const span = { first: 0, last: 212 };
  for (const [name, fill] of Object.entries(FILL)) {
    const r = fillGaps(notes, [hats, other], fill, span);
    assert.ok(longest(r.notes) <= MAX_GAP, name);
    assert.equal(chartHoles(r.notes, span).length, 0, name);
    const lv = LEVELS[name];
    assert.deepEqual(chartProblems(r.notes.map((n) => ({ ...n, t: stepTime(beats, n.step) })), lv, beats), [], name);
    // the other stem never plays where the hats do, and the fill keeps its own spacing
    assert.ok(!r.added.some((c) => c.src === 'other' && c.step >= 32 + fill.gap && c.step <= 88 - fill.gap), name);
    assert.ok(r.added.some((c) => c.src === 'hat'), name);
    for (let i = 1; i < r.added.length; i++) assert.ok(r.added[i].step - r.added[i - 1].step >= fill.gap, name);
    assert.ok(r.added.every((c) => c.step % fill.grid === 0), name);
  }
});

test('fillGaps: a hole with nothing in it stays, a chart without holes is left as it is', () => {
  const notes = [{ step: 0, lane: 0, score: 1 }, { step: 100, lane: 0, score: 1 }];
  const r = fillGaps(notes, [[], []], FILL.hard, { first: 0, last: 100 });
  assert.equal(r.added.length, 0);
  const dense = Array.from({ length: 20 }, (_, i) => ({ step: 8 * i, lane: 0, score: 1 }));
  assert.equal(fillGaps(dense, [[{ step: 4, lane: 1, score: 1, src: 'hat' }]], FILL.hard, { first: 0, last: 152 }).added.length, 0);
});

test('fillGaps: the first note comes within a bar of the song\'s first onset', () => {
  const notes = [{ step: 200, lane: 0, score: 1 }];
  const other = Array.from({ length: 50 }, (_, i) => ({ step: 4 * i, lane: 2, score: 0.6, src: 'other' }));
  const r = fillGaps(notes, [[], other], FILL.easy, { first: 0, last: 200 });
  assert.ok(r.notes[0].step <= 16);
});

test('makeCharts: a drumless intro is charted by the fill, the drums\' chart is the same as without it', () => {
  const s = makeSong({ length: 40, rate: 8000 });
  const t = s.truth;
  const cands = t.notes.map((n) => ({ step: n.step, lane: n.lane, strength: 1 }));
  const plain = makeCharts(cands, t.beats, 0, 1);
  // the same song with 8 bars of other-stem quarter notes in front of the drums
  const pre = 128, period = t.beats[1] - t.beats[0];
  const beats2 = [...Array.from({ length: 32 }, (_, i) => t.beats[0] - (32 - i) * period), ...t.beats];
  const shifted = cands.map((c) => ({ ...c, step: c.step + pre }));
  const extra = Array.from({ length: 32 }, (_, i) => ({ step: 4 * i, strength: 1, src: 'other' }));
  const r = makeCharts(shifted, beats2, 0, 1, extra, pre);
  for (const name of Object.keys(LEVELS)) {
    const main = r.charts[name].filter((n) => n.step >= pre).map((n) => `${n.step - pre}:${n.lane}`);
    assert.deepEqual(main, plain.charts[name].map((n) => `${n.step}:${n.lane}`), name);
    assert.ok(r.charts[name][0].step < 16, name);
    assert.ok(longest(r.charts[name]) <= MAX_GAP, name);
    assert.ok(r.stats[name].other_steps.length > 0 && r.stats[name].other_steps.every((st) => st < pre), name);
    assert.ok(r.charts[name].filter((n) => n.step < pre).every((n) => n.lane === 2), name);
  }
});

test('extendBeats: carries the grid back over a drumless intro and follows onsets that sit off a straight line', () => {
  const p = 0.5, drums = Array.from({ length: 40 }, (_, i) => 20 + i * p);
  // the intro's 16ths drift: 12 ms late by its start
  const onsets = [];
  for (let k = 1; k <= 160; k++) onsets.push({ t: 20 - k * (p / 4) + 0.012 * (k / 160) });
  const b = extendBeats(drums, onsets);
  assert.equal(b.length - drums.length, 40);
  assert.deepEqual(b.slice(40), drums);
  assert.ok(Math.abs(b[0] - (0 + 0.012)) < 0.004, `first beat ${b[0]}`);
  // nothing before the drums: no change
  assert.deepEqual(extendBeats(drums, [{ t: 25 }]), drums);
});

test('gapProblems and firstNoteProblem: the chart rules, and the controls they must catch', () => {
  const quarter = (steps) => steps.map((step) => ({ step, t: stepTime(beats, step), lane: 0 }));
  const steady = quarter(Array.from({ length: 60 }, (_, i) => 4 * i));
  const never = () => false, always = () => true;
  for (const name of Object.keys(LEVELS)) assert.deepEqual(gapProblems(steady, name, never), [], name);
  // control: a hole cut into the chart is caught at every level
  const holed = steady.filter((n) => n.step < 40 || n.step > 120);
  for (const name of Object.keys(LEVELS)) assert.ok(gapProblems(holed, name, never).length > 0, name);
  // Easy may go four bars over silence, never more; the other levels never past two
  const three = steady.filter((n) => n.step < 40 || n.step >= 88);
  assert.deepEqual(gapProblems(three, 'easy', always), []);
  assert.ok(gapProblems(three, 'easy', never).length > 0);
  assert.ok(gapProblems(three, 'hard', always).length > 0);
  assert.ok(gapProblems(holed, 'easy', always).length > 0);
  // the first note
  assert.equal(firstNoteProblem(steady, steady[0].t, 4), null);
  assert.ok(firstNoteProblem(steady.filter((n) => n.t > 10), steady[0].t, 4));
});

test('Medium keeps the bass: every pattern with a bass line gets bass notes, and the bass check fails a chart without', async () => {
  const { bassProblem, MIN_BASS_SHARE } = await import('./check-songs.mjs');
  // a bar of four-on-the-floor kicks, claps on 2 and 4, and an off-beat bass on every 8th "and": the drums own every
  // beat, so without the bass rule Medium picks no bass at all
  const cands = [];
  for (let bar = 0; bar < 16; bar++) {
    for (const pos of [0, 4, 8, 12]) cands.push({ step: bar * 16 + pos, lane: pos % 8 ? 1 : 0, strength: 1 });
    for (const pos of [2, 6, 10, 14]) cands.push({ step: bar * 16 + pos, lane: 2, strength: 1 });
  }
  const b = Array.from({ length: 70 }, (_, i) => 0.5 + i * 0.5);
  const { charts } = makeCharts(cands, b, 0, b.at(-1) - b[0]);
  const bars = new Set(charts.medium.filter((n) => n.lane === 2).map((n) => Math.floor(n.step / 16)));
  assert.equal(bars.size, 16, 'every bar of Medium has a bass note');
  assert.ok(charts.medium.some((n) => n.lane === 0) && charts.medium.some((n) => n.lane === 1), 'and the drums too');
  assert.deepEqual(chartProblems(charts.medium, LEVELS.medium, b), []);
  assert.equal(bassProblem(charts.medium, charts.hard), null);
  // control: the same chart without its bass notes fails
  assert.ok(bassProblem(charts.medium.filter((n) => n.lane !== 2), charts.hard));
  // a song with no bass line asks nothing of the bass
  assert.equal(bassProblem([{ lane: 0 }], [{ lane: 0 }, { lane: 1 }]), null);
  assert.ok(MIN_BASS_SHARE > 0);
});
