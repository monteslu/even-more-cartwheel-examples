// What the picture reads from the other stem (docs/DESIGN.md, "The look from the music"): the chords, pads, stabs and
// synth lines the player does not play, analysed once when the package is made so the game can draw what is COMING,
// not only what is sounding. One value a beat, on the song's own beats:
//   level   the other track's loudness over the beat, 0 (silent, 36 dB under its loud beats) to 99
//   bright  its tone: the spectral centroid, 0 (about 150 Hz, dark pads) to 99 (about 5 kHz, bright stabs)
//   hue     its harmony as an angle on the circle of fifths (0 to 359): the pitch classes sounding, each at its place
//           on the circle (C 0, G 30, D 60, ...), summed. Related chords sit close, a key change moves it far.
//   tonal   how clearly the beat has one harmony, 0 (noise, a drum hit, silence) to 99 (one chord held)
// and two lists:
//   changes the beats where the harmony moves (a chord change, a key change), as beat indices
//   bands   bass, mid and treble of the track per HALF beat, each against its own average over the song (the way
//           music visualizers feed their presets: 50 is the band's usual energy, 100 twice it, capped at 199), flat
//           [bass, mid, treble, bass, mid, treble, ...]: what each part of the land beside the rivers rises with
//   stabs   the other track's attacks as [beat position x 100, strength 1..9] pairs, flat: the stabs and riffs a player
//           would see coming down the land beside the rivers
import { fft, percentile } from './dsp.mjs';

const SIZE = 8192;
const HOP = 4096;
const F_LO = 55, F_HI = 2000; // the chroma's band: bass notes of chords to the top of most pads
const LEVEL_FLOOR_DB = -36;

/** beat i's start and end, in seconds (the last as long as the one before it) */
function beatSpan(beats, i) {
  const t0 = beats[i];
  const t1 = i + 1 < beats.length ? beats[i + 1] : t0 + (beats[i] - beats[Math.max(0, i - 1)] || 0.5);
  return [t0, t1];
}

/**
 * Per-beat power spectra of a mono signal: the frames whose centre falls in the beat, summed.
 * @param {Float32Array} x
 * @param {number} rate
 * @param {number[]} beats seconds
 * @returns {{power: Float64Array[], rms: Float64Array}}
 */
export function beatSpectra(x, rate, beats) {
  const win = new Float64Array(SIZE);
  for (let i = 0; i < SIZE; i++) win[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / SIZE);
  const re = new Float64Array(SIZE), im = new Float64Array(SIZE);
  const power = [], rms = new Float64Array(beats.length);
  for (let b = 0; b < beats.length; b++) {
    const [t0, t1] = beatSpan(beats, b);
    const s0 = Math.max(0, Math.round(t0 * rate)), s1 = Math.min(x.length, Math.round(t1 * rate));
    let e = 0;
    for (let i = s0; i < s1; i++) e += x[i] * x[i];
    rms[b] = s1 > s0 ? Math.sqrt(e / (s1 - s0)) : 0;
    const p = new Float64Array(SIZE / 2);
    for (let c = s0; c < Math.max(s0 + 1, s1); c += HOP) {
      for (let i = 0; i < SIZE; i++) {
        const k = c - SIZE / 2 + i;
        re[i] = k >= 0 && k < x.length ? x[k] * win[i] : 0;
        im[i] = 0;
      }
      fft(re, im);
      for (let k = 0; k < SIZE / 2; k++) p[k] += re[k] * re[k] + im[k] * im[k];
    }
    power.push(p);
  }
  return { power, rms };
}

/**
 * 12 pitch classes (C first) from a power spectrum: each bin in [F_LO, F_HI] adds its magnitude to the pitch class
 * nearest its frequency.
 * @param {Float64Array} p
 * @param {number} rate
 */
export function chroma(p, rate) {
  const c = new Float64Array(12);
  const k0 = Math.ceil((F_LO * SIZE) / rate), k1 = Math.floor((F_HI * SIZE) / rate);
  for (let k = k0; k <= k1; k++) {
    const f = (k * rate) / SIZE, midi = 69 + 12 * Math.log2(f / 440);
    const pc = ((Math.round(midi) % 12) + 12) % 12;
    c[pc] += Math.sqrt(p[k]);
  }
  return c;
}

/**
 * A chroma's place on the circle of fifths: the angle (degrees, 0..360) and how clearly it points there (0..1).
 * @param {ArrayLike<number>} c
 */
export function fifthsAngle(c) {
  let x = 0, y = 0, sum = 0;
  for (let pc = 0; pc < 12; pc++) {
    const a = (((pc * 7) % 12) * Math.PI) / 6; // C 0, G 30, D 60, ... F 330
    x += c[pc] * Math.cos(a);
    y += c[pc] * Math.sin(a);
    sum += c[pc];
  }
  const deg = ((Math.atan2(y, x) * 180) / Math.PI + 360) % 360;
  return { deg, clarity: sum > 0 ? Math.hypot(x, y) / sum : 0 };
}

const cosSim = (a, b) => {
  let ab = 0, aa = 0, bb = 0;
  for (let i = 0; i < a.length; i++) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
  return aa > 0 && bb > 0 ? ab / Math.sqrt(aa * bb) : 1;
};

/**
 * The harmony's moves: at each beat, the chroma of the two beats from it against the two beats before it; a change
 * is a local peak of that difference over `minNovelty`, at least two beats from the last, where the track sounds.
 * @param {Float64Array[]} chromas
 * @param {number[]} level 0..99 a beat
 * @param {number} [minNovelty]
 */
export function harmonyChanges(chromas, level, minNovelty = 0.12) {
  const n = chromas.length, nov = new Float64Array(n);
  const sum = (from, to) => {
    const s = new Float64Array(12);
    for (let i = Math.max(0, from); i < Math.min(n, to); i++) for (let k = 0; k < 12; k++) s[k] += chromas[i][k];
    return s;
  };
  for (let i = 2; i < n - 1; i++) {
    if (level[i] < 20 || level[i - 1] < 20) continue;
    nov[i] = 1 - cosSim(sum(i - 2, i), sum(i, i + 2));
  }
  const out = [];
  for (let i = 2; i < n - 1; i++) {
    if (nov[i] < minNovelty || nov[i] < nov[i - 1] || nov[i] < nov[i + 1]) continue;
    if (out.length && i - out[out.length - 1] < 2) {
      if (nov[i] > nov[out[out.length - 1]]) out[out.length - 1] = i;
      continue;
    }
    out.push(i);
  }
  return out;
}

const BANDS = [[20, 250], [250, 2000], [2000, 8000]];

/**
 * Bass, mid and treble per half beat, each relative to its own song average (see the top of this file).
 * @param {Float32Array} x
 * @param {number} rate
 * @param {number[]} beats
 */
export function halfBeatBands(x, rate, beats) {
  const grid = [];
  for (let i = 0; i < beats.length; i++) {
    const [t0, t1] = beatSpan(beats, i);
    grid.push(t0, (t0 + t1) / 2);
  }
  const { power } = beatSpectra(x, rate, grid);
  const e = power.map((p) => BANDS.map(([lo, hi]) => {
    let v = 0;
    for (let k = Math.ceil((lo * SIZE) / rate); k <= Math.floor((hi * SIZE) / rate); k++) v += p[k];
    return Math.sqrt(v);
  }));
  const loud = e.map((b) => b[0] + b[1] + b[2]), floor = 0.02 * Math.max(...loud);
  const mean = BANDS.map((_, k) => {
    const on = e.filter((_, i) => loud[i] > floor).map((b) => b[k]);
    return on.length ? on.reduce((a, v) => a + v, 0) / on.length : 1;
  });
  const out = [];
  for (const b of e) for (let k = 0; k < 3; k++) out.push(Math.min(199, Math.round((50 * b[k]) / (mean[k] || 1))));
  return out;
}

/** a time in seconds as a fractional beat index on the song's beats */
function beatPos(beats, t) {
  if (t <= beats[0]) return (t - beats[0]) / ((beats[1] ?? beats[0] + 0.5) - beats[0]);
  let lo = 0, hi = beats.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (beats[m] <= t) lo = m; else hi = m; }
  const len = beats[hi] - beats[lo] || 0.5;
  return lo + (t - beats[lo]) / len;
}

/**
 * The other stem's look, one value a beat (see the top of this file).
 * @param {Float32Array} other mono other track
 * @param {number} rate
 * @param {number[]} beats seconds
 * @param {{t: number, strength: number}[]} onsets the other track's onsets (analyze.mjs, lanes.other)
 */
export function otherLook(other, rate, beats, onsets) {
  const { power, rms } = beatSpectra(other, rate, beats);
  const ref = percentile(Array.from(rms).filter((v) => v > 0), 0.95) || 1;
  const level = Array.from(rms, (v) => {
    const db = 20 * Math.log10(v / ref + 1e-12);
    return Math.max(0, Math.min(99, Math.round(((db - LEVEL_FLOOR_DB) / -LEVEL_FLOOR_DB) * 99)));
  });
  const bright = power.map((p) => {
    let w = 0, s = 0;
    const k0 = Math.ceil((80 * SIZE) / rate), k1 = Math.floor((8000 * SIZE) / rate);
    for (let k = k0; k <= k1; k++) { const m = Math.sqrt(p[k]); w += m * k; s += m; }
    const f = s > 0 ? ((w / s) * rate) / SIZE : 150;
    return Math.max(0, Math.min(99, Math.round((Math.log2(Math.max(150, f) / 150) / Math.log2(5000 / 150)) * 99)));
  });
  const chromas = power.map((p) => chroma(p, rate));
  const hue = [], tonal = [];
  chromas.forEach((c, i) => {
    const { deg, clarity } = fifthsAngle(c);
    hue.push(Math.round(deg) % 360);
    tonal.push(level[i] < 10 ? 0 : Math.min(99, Math.round(clarity * 99)));
  });
  const changes = harmonyChanges(chromas, level);
  const strong = percentile(onsets.map((o) => o.strength), 0.9) || 1;
  const stabs = [];
  for (const o of onsets) {
    const s = Math.round((o.strength / strong) * 9);
    if (s < 3) continue;
    const b = beatPos(beats, o.t);
    if (b < 0 || b > beats.length) continue;
    stabs.push(Math.round(b * 100), Math.min(9, s));
  }
  const bands = halfBeatBands(other, rate, beats);
  return { level, bright, hue, tonal, changes, bands, stabs };
}
