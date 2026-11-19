// Unit tests of the DSP pieces against signals with a known answer. Run: node --test tools/
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  rng, fft, crossover, xcorrLag, tempoAutocorr, dpBeats, yin, stft, spectralFlux, adaptiveThreshold, pickPeaks,
  median, cancelBleed, biquad, filter,
} from './lib/dsp.mjs';
import { envelope, refineOnset, bandOnsets, movingMax } from './lib/analyze.mjs';
import { encodeWav, decodeWav } from './lib/wav.mjs';

const RATE = 44100;
const sine = (f, n, a = 1, rate = RATE) => Float32Array.from({ length: n }, (_, i) => a * Math.sin((2 * Math.PI * f * i) / rate));
const noise = (n, seed = 1, a = 1) => { const r = rng(seed); return Float32Array.from({ length: n }, () => a * (2 * r() - 1)); };
const rms = (x) => Math.sqrt(x.reduce((s, v) => s + v * v, 0) / x.length);

test('fft: a sine lands in its bin, and the inverse gives the signal back', () => {
  const n = 1024, re = new Float64Array(n), im = new Float64Array(n);
  for (let i = 0; i < n; i++) re[i] = Math.cos((2 * Math.PI * 37 * i) / n);
  const orig = Float64Array.from(re);
  fft(re, im);
  let best = 0;
  for (let b = 1; b < n / 2; b++) if (Math.hypot(re[b], im[b]) > Math.hypot(re[best], im[best])) best = b;
  assert.equal(best, 37);
  fft(re, im, true);
  for (let i = 0; i < n; i++) assert.ok(Math.abs(re[i] - orig[i]) < 1e-9);
});

test('crossover: the bands sum to the input exactly, split at 150 Hz, and neither is delayed', () => {
  const n = RATE;
  const lo = sine(55, n), hi = sine(2000, n, 0.5), x = Float32Array.from(lo, (v, i) => v + hi[i]);
  const { low, high } = crossover(x, RATE);
  for (let i = 0; i < n; i += 97) assert.ok(Math.abs(low[i] + high[i] - x[i]) < 1e-6);
  const mid = (a) => a.subarray(n / 4, (3 * n) / 4);
  assert.ok(Math.abs(rms(mid(low)) - rms(mid(lo))) / rms(mid(lo)) < 0.05, 'the 55 Hz sine stays in the low band');
  assert.ok(rms(mid(high)) / rms(mid(hi)) > 0.97, 'the 2 kHz sine stays in the high band');
  assert.ok(rms(mid(Float32Array.from(low, (v, i) => v - lo[i]))) < 0.06, 'the low band is the 55 Hz sine, in phase');
  // an impulse comes out of the low band centred where it went in (zero phase)
  const imp = new Float32Array(8192); imp[4096] = 1;
  const l = crossover(imp, RATE).low;
  let peak = 0;
  for (let i = 0; i < l.length; i++) if (Math.abs(l[i]) > Math.abs(l[peak])) peak = i;
  assert.equal(peak, 4096);
});

test('xcorrLag: finds a known shift to the sample, and 0 for identical signals', () => {
  const a = noise(20000, 3), b = new Float32Array(20000);
  b.set(a.subarray(0, 20000 - 37), 37);
  assert.equal(xcorrLag(a, b, 200).lag, 37);
  assert.equal(xcorrLag(a, a, 200).lag, 0);
  assert.ok(Math.abs(xcorrLag(a, a, 200).lagFine) < 1e-6);
});

/** An onset envelope of impulses at the given times. */
const impulses = (times, fr, len) => {
  const e = new Float32Array(Math.ceil(len * fr));
  for (const t of times) { const k = Math.round(t * fr); if (k < e.length) e[k] += 1; }
  return e;
};

test('tempoAutocorr: 124 BPM from a beat train, even with 8th notes between the beats', () => {
  const fr = RATE / 128, beat = 60 / 124;
  const times = [];
  for (let b = 0; b < 120; b++) { times.push(0.5 + b * beat); }
  const env = impulses(times, fr, 70);
  for (let b = 0; b < 120; b++) { const k = Math.round((0.5 + (b + 0.5) * beat) * fr); if (k < env.length) env[k] += 0.6; }
  const { bpm } = tempoAutocorr(env, fr, 115, 135);
  assert.ok(Math.abs(bpm - 124) < 0.5, `bpm ${bpm}`);
});

test('dpBeats: follows the beats of a train with a gap in it', () => {
  const fr = RATE / 128, beat = 60 / 120;
  const times = [];
  for (let b = 0; b < 80; b++) if (b < 30 || b > 40) times.push(1 + b * beat);
  const env = impulses(times, fr, 42);
  const beats = dpBeats(env, beat * fr).map((k) => k / fr);
  for (const t of times) assert.ok(beats.some((b) => Math.abs(b - t) < 0.006), `a beat at ${t}`);
  // through the gap the beats keep the period
  const inGap = beats.filter((b) => b > 1 + 31 * beat && b < 1 + 40 * beat);
  assert.equal(inGap.length, 9);
});

test('yin: the pitch of a bass note, and nothing for noise', () => {
  const r = 11025;
  const x = Float32Array.from(sine(55, r, 0.5, r), (v, i) => v + 0.2 * Math.sin((2 * Math.PI * 110 * i) / r));
  const p = yin(x, r, { hop: 64, size: 512, fMin: 30, fMax: 300 });
  const voiced = [...p.f0].filter((f) => f > 0);
  assert.ok(voiced.length > p.f0.length * 0.6);
  assert.ok(Math.abs(median(voiced) - 55) < 0.3, `f0 ${median(voiced)}`);
  const q = yin(noise(r, 5, 0.5), r, { hop: 64, size: 512, fMin: 30, fMax: 300 });
  assert.ok([...q.f0].filter((f) => f > 0).length < q.f0.length * 0.1);
});

test('onsets: spectral flux, the adaptive threshold and the refinement find clicks to the millisecond', () => {
  const n = RATE * 4, x = noise(n, 9, 0.002);
  const truth = [0.5, 0.9, 1.37, 2.0, 2.61, 3.3];
  const burst = noise(2000, 11, 1);
  for (const t of truth) {
    const i0 = Math.round(t * RATE);
    for (let i = 0; i < 2000; i++) x[i0 + i] += burst[i] * Math.exp(-i / 300);
  }
  const { onsets } = bandOnsets(x, RATE, { fLo: 1000, fHi: 5000, gap: 0.05, k: 2 });
  assert.equal(onsets.length, truth.length);
  onsets.forEach((o, i) => assert.ok(Math.abs(o.t - truth[i]) < 0.0015, `${o.t} vs ${truth[i]}`));
});

test('refineOnset: the earliest burst of a clap, not its loudest', () => {
  const x = new Float32Array(RATE);
  const r = rng(4);
  for (const [t0, a] of [[0.5, 0.5], [0.511, 0.7], [0.522, 1]]) {
    for (let i = 0; i < 300; i++) x[Math.round(t0 * RATE) + i] += a * (2 * r() - 1) * Math.exp(-i / 150);
  }
  const t = refineOnset(envelope(x, RATE, 2), RATE, 0.52);
  assert.ok(Math.abs(t - 0.5) < 0.001, `${t}`);
});

test('adaptive threshold and peak picking: a steady level is not an onset', () => {
  const f = new Float32Array(1000).fill(1);
  f[300] = 5; f[310] = 4; f[700] = 6;
  const peaks = pickPeaks(f, adaptiveThreshold(f, 50, 2, 0.5), 20);
  assert.deepEqual(peaks, [300, 700]);
  assert.deepEqual([...movingMax(Float32Array.from([1, 3, 2, 0, 0, 0, 5]), 1)], [3, 3, 3, 2, 0, 5, 5]);
});

test('cancelBleed: removes a filtered copy of the kick from the bass and leaves the bass', () => {
  const n = 40000, kick = new Float32Array(n), bass = sine(49, n, 0.4, 11025);
  for (let t = 0; t < n; t += 5000) for (let i = 0; i < 2000 && t + i < n; i++) kick[t + i] = Math.sin(i * 0.08) * Math.exp(-i / 600);
  const leak = filter(kick, biquad('lowpass', 200, 11025));
  const y = Float32Array.from(bass, (v, i) => v + 0.3 * leak[i]);
  const { clean } = cancelBleed(y, kick, 96);
  const err = Float32Array.from(clean, (v, i) => v - bass[i]);
  assert.ok(rms(err) < 0.02 * rms(bass), `residual ${rms(err)}`);
  const { removed } = cancelBleed(bass, kick, 96);
  assert.ok(removed < 0.01, 'no bleed: almost nothing removed');
});

test('wav: 16-bit round trip', () => {
  const x = sine(440, 1000, 0.5);
  const d = decodeWav(encodeWav({ rate: RATE, ch: [x, x] }));
  assert.equal(d.rate, RATE);
  assert.equal(d.ch.length, 2);
  for (let i = 0; i < 1000; i++) assert.ok(Math.abs(d.ch[1][i] - x[i]) < 1e-4);
});

test('stft + flux: energy arriving counts, energy leaving does not', () => {
  const x = new Float32Array(RATE);
  x.set(sine(1000, RATE / 2, 0.5), RATE / 4);
  // a 30 ms fade out (an abrupt stop would splatter energy into every bin, which is a real arrival of energy)
  for (let i = 0; i < 1323; i++) x[(3 * RATE) / 4 - 1323 + i] *= 1 - i / 1323;
  const s = stft(x, RATE, { fLo: 500, fHi: 2000 });
  const f = spectralFlux(s, 2);
  const kOn = Math.round(0.25 * s.frameRate), kOff = Math.round(0.75 * s.frameRate);
  const around = (k) => Math.max(...f.subarray(k - 10, k + 10));
  assert.ok(around(kOn) > 10 * around(kOff) + 1);
});
