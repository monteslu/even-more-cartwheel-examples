// Unit tests of the picture's reading of the other track (lib/look.mjs): the circle of fifths, the loudness, the
// harmony's moves against known chords, each with a control that must fail.
// Run: node --test tools/
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { fifthsAngle, otherLook, harmonyChanges, chroma, beatSpectra, halfBeatBands } from './lib/look.mjs';
import { makeSong } from './lib/synth.mjs';
import { monoOf } from './lib/analyze.mjs';

const RATE = 44100;
const midiHz = (m) => 440 * 2 ** ((m - 69) / 12);

/** chords (midi notes) one after another, `beats` each at 120 BPM, after `lead` silent beats */
function chords(list, beats = 4, lead = 4) {
  const beat = 0.5, n = Math.round((lead + list.length * beats + 1) * beat * RATE);
  const x = new Float32Array(n);
  list.forEach((ch, c) => {
    const s0 = Math.round((lead + c * beats) * beat * RATE), s1 = Math.round((lead + (c + 1) * beats) * beat * RATE);
    for (const m of ch) for (let i = s0; i < s1; i++) x[i] += 0.1 * Math.sin((2 * Math.PI * midiHz(m) * (i - s0)) / RATE);
  });
  const beatsT = Array.from({ length: lead + list.length * beats }, (_, i) => i * beat);
  return { x, beats: beatsT };
}

test('fifthsAngle: C at 0, G at 30, a C major triad between them, noise points nowhere', () => {
  const one = (pc) => Array.from({ length: 12 }, (_, i) => (i === pc ? 1 : 0));
  assert.ok(Math.abs(fifthsAngle(one(0)).deg - 0) < 1e-6 || Math.abs(fifthsAngle(one(0)).deg - 360) < 1e-6);
  assert.ok(Math.abs(fifthsAngle(one(7)).deg - 30) < 1e-6);
  assert.ok(Math.abs(fifthsAngle(one(2)).deg - 60) < 1e-6);
  const triad = fifthsAngle([1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0]); // C E G
  assert.ok(triad.deg > 20 && triad.deg < 70, `C major at ${triad.deg}`);
  assert.ok(triad.clarity > 0.5);
  assert.ok(fifthsAngle(Array(12).fill(1)).clarity < 1e-9);
});

test('chroma: a pure A sits on pitch class 9', () => {
  const { x, beats } = chords([[57]], 4, 0);
  const { power } = beatSpectra(x, RATE, beats);
  const c = chroma(power[1], RATE);
  assert.equal(c.indexOf(Math.max(...c)), 9);
});

test('otherLook: silence is level 0, chords loud; the C to G move is found on its beat and moves the hue', () => {
  const C = [48, 60, 64, 67], G = [43, 59, 62, 67];
  const { x, beats } = chords([C, C, G, G]);
  const look = otherLook(x, RATE, beats, []);
  assert.ok(look.level.slice(0, 4).every((v) => v === 0), `lead ${look.level.slice(0, 4)}`);
  assert.ok(look.level.slice(4).every((v) => v > 80), `chords ${look.level.slice(4)}`);
  assert.deepEqual(look.changes, [12], `changes ${look.changes}`);
  const hc = look.hue[8], hg = look.hue[16];
  const d = Math.abs(((hg - hc + 540) % 360) - 180);
  assert.ok(d >= 20, `hue C ${hc}, G ${hg}`);
  // control: the same chord all along has no change
  const same = otherLook(chords([C, C, C, C]).x, RATE, beats, []);
  assert.deepEqual(same.changes, []);
});

test('otherLook: on the synthetic song the harmony changes land on the bar lines (one chord a bar)', () => {
  const s = makeSong({ length: 30 });
  const t = s.truth;
  const other = monoOf(s.stems.other);
  const look = otherLook(other, s.rate, t.beats, []);
  assert.equal(look.level.length, t.beats.length);
  const on = look.changes.filter((b) => (b - t.downbeat) % 4 === 0).length;
  assert.ok(look.changes.length >= 8, `${look.changes.length} changes`);
  assert.ok(on / look.changes.length >= 0.9, `${on} of ${look.changes.length} on a bar line: ${look.changes}`);
  // control: the same chroma moved by two beats puts the changes off the bar lines
  const moved = harmonyChanges(look.level.map((_, i) => beatChroma(other, s.rate, t.beats, i + 2)), look.level);
  const onMoved = moved.filter((b) => (b - t.downbeat) % 4 === 0).length;
  assert.ok(onMoved / Math.max(1, moved.length) < 0.5, `moved: ${onMoved} of ${moved.length} on a bar line`);
});

const cache = new Map();
function beatChroma(x, rate, beats, i) {
  if (!cache.has(x)) cache.set(x, beatSpectra(x, rate, beats).power.map((p) => chroma(p, rate)));
  const all = cache.get(x);
  return all[Math.min(all.length - 1, i)];
}

test('halfBeatBands: a low note lifts the bass band, a high one the treble, each against its own average', () => {
  const beat = 0.5, n = Math.round(9 * beat * RATE), x = new Float32Array(n);
  // four beats of a 80 Hz note, then four of a 4 kHz note
  for (let i = 0; i < n; i++) {
    const t = i / RATE;
    if (t < 4 * beat) x[i] = 0.2 * Math.sin(2 * Math.PI * 80 * t);
    else if (t < 8 * beat) x[i] = 0.2 * Math.sin(2 * Math.PI * 4000 * t);
  }
  const beats = Array.from({ length: 8 }, (_, i) => i * beat);
  const b = halfBeatBands(x, RATE, beats);
  assert.equal(b.length, 8 * 2 * 3);
  const at = (h, k) => b[h * 3 + k];
  assert.ok(at(2, 0) > 80 && at(2, 2) < 20, `low half: bass ${at(2, 0)}, treble ${at(2, 2)}`);
  assert.ok(at(12, 2) > 80 && at(12, 0) < 20, `high half: bass ${at(12, 0)}, treble ${at(12, 2)}`);
  // control: the bands read the other way round are wrong
  assert.ok(!(at(2, 2) > 80 && at(12, 0) > 80));
});
