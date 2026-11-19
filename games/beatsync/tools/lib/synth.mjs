// A synthetic house song with known notes: the ground truth the chart pipeline is measured against, and a song for
// the game before any real one exists. Every sound is made here from oscillators and seeded noise.
//
// Stems, as a separation model would give them: drums (kick, clap, hats), bass, other (pads), vocals (silent).
// Sections: an intro without bass, a drop, a break without kick (ending in a clap build), a final drop, an outro.
import { rng, biquad, filter } from './dsp.mjs';

/**
 * @typedef {object} SongOptions
 * @property {number} [bpm] tempo at the start (default 124)
 * @property {number} [length] target length in seconds (default 90); rounded to whole bars
 * @property {number} [rate] sample rate (default 44100)
 * @property {number} [drift] relative tempo change over the song, e.g. 0.003 = 0.3% faster by the end
 * @property {number} [bleed] gain of the kick leaking into the bass stem (0 = none)
 * @property {number} [leadIn] seconds of silence before beat 0 (default 0.3)
 * @property {number} [seed]
 */

/**
 * @typedef {object} TruthNote
 * @property {number} t onset time in seconds
 * @property {number} step 16th index from beat 0
 * @property {number} lane 0 kick, 1 clap, 2 bass
 * @property {string} kind e.g. 'attack' or 'legato' for the bass, 'fill' for notes outside the bar's groove
 */

const LANES = { kick: 0, clap: 1, bass: 2 };

/** The section plan for a number of bars. */
export function plan(nBars) {
  const outro = nBars >= 32 ? 2 : 0;
  const rest = nBars - outro;
  const four = (x) => Math.max(4, 4 * Math.round(x / 4));
  const intro = four(rest * 0.18), brk = four(rest * 0.18), drop1 = four(rest * 0.36);
  const drop2 = rest - intro - brk - drop1;
  if (drop2 < 4) throw new Error(`song too short for its sections (${nBars} bars)`);
  /** @type {{kind: string, bars: number}[]} */
  const s = [{ kind: 'intro', bars: intro }, { kind: 'drop', bars: drop1 }, { kind: 'break', bars: brk - 2 },
    { kind: 'build', bars: 2 }, { kind: 'drop', bars: drop2 }];
  if (outro) s.push({ kind: 'outro', bars: outro });
  let bar = 0;
  return s.map((x) => { const r = { ...x, start: bar }; bar += x.bars; return r; });
}

const midiHz = (m) => 440 * 2 ** ((m - 69) / 12);

// The chord loop, one chord a bar: Am7, Fmaj7, Cmaj7, G. Bass roots A1, F1, C2, G1.
const CHORDS = [[57, 60, 64, 67], [53, 57, 60, 64], [48, 52, 55, 59], [55, 59, 62, 67]];
const ROOTS = [33, 29, 36, 31];
// One bar of the bass in the drops: [step, length in 16ths, semitones over the root, legato (no new attack)].
const BASS_BAR = [[2, 2, 0, false], [6, 1, 0, false], [7, 3, 3, true], [10, 2, 0, false], [12, 2, 7, true],
  [14, 1, 12, false]];

/**
 * Make the song.
 * @param {SongOptions} [o]
 */
export function makeSong(o = {}) {
  const bpm = o.bpm ?? 124, rate = o.rate ?? 44100, drift = o.drift ?? 0, leadIn = o.leadIn ?? 0.3;
  const R = rng(o.seed ?? 7);
  const barDur = (4 * 60) / bpm;
  const nBars = Math.max(20, Math.round((o.length ?? 90) / barDur));
  const sections = plan(nBars);
  const nBeats = nBars * 4;
  // beat x (fractional) -> seconds: the tempo rises linearly from bpm to bpm * (1 + drift) over the song
  const time = (x) => drift
    ? leadIn + (60 / bpm) * (nBeats / drift) * Math.log(1 + (drift * x) / nBeats)
    : leadIn + (60 * x) / bpm;
  const stepTime = (s) => time(s / 4);
  const tail = 2.0;
  const n = Math.ceil((time(nBeats) + tail) * rate);
  const mk = () => [new Float32Array(n), new Float32Array(n)];
  const drums = mk(), bass = mk(), other = mk(), kickOnly = new Float32Array(n);
  /** @type {TruthNote[]} */
  const notes = [];
  const sectionOf = (bar) => sections.find((s) => bar >= s.start && bar < s.start + s.bars);

  // ── the voices ──
  const kick = (t0, gain) => {
    const i0 = Math.round(t0 * rate), len = Math.round(0.5 * rate), fade = Math.round(0.05 * rate);
    let ph = 0;
    for (let i = 0; i < len && i0 + i < n; i++) {
      const t = i / rate;
      const f = 48 + 110 * Math.exp(-t / 0.028);
      ph += (2 * Math.PI * f) / rate;
      const click = i < 0.002 * rate ? (R() * 2 - 1) * 0.3 * (1 - i / (0.002 * rate)) : 0;
      const v = gain * (Math.sin(ph) * Math.exp(-t / 0.22) * Math.min(1, i / 16, (len - i) / fade) + click);
      drums[0][i0 + i] += v; drums[1][i0 + i] += v; kickOnly[i0 + i] += v;
    }
  };
  const clapBp = biquad('bandpass', 1600, rate, 0.9);
  const clap = (t0, gain) => {
    const i0 = Math.round(t0 * rate), len = Math.round(0.3 * rate), fade = Math.round(0.03 * rate);
    const raw = new Float32Array(len);
    for (let i = 0; i < len; i++) {
      const t = i / rate;
      // three quick bursts then the tail, the shape of a hand clap
      const burst = [0, 0.011, 0.022].reduce((a, b) => a + (t >= b ? Math.exp(-(t - b) / 0.004) : 0), 0);
      const env = (t < 0.03 ? burst : 0) + (t >= 0.022 ? 0.8 * Math.exp(-(t - 0.022) / 0.07) : 0);
      raw[i] = (R() * 2 - 1) * env * Math.min(1, i / 8, (len - i) / fade);
    }
    const y = filter(raw, clapBp);
    for (let i = 0; i < len && i0 + i < n; i++) { drums[0][i0 + i] += gain * 2.2 * y[i]; drums[1][i0 + i] += gain * 2.2 * y[i]; }
  };
  const hatHp = biquad('highpass', 7500, rate, 0.7);
  const hat = (t0, gain, decay, pan) => {
    const i0 = Math.round(t0 * rate), len = Math.round(decay * 5 * rate);
    const raw = new Float32Array(len);
    for (let i = 0; i < len; i++) raw[i] = (R() * 2 - 1) * Math.exp(-i / rate / decay);
    const y = filter(filter(raw, hatHp), hatHp);
    for (let i = 0; i < len && i0 + i < n; i++) {
      drums[0][i0 + i] += gain * (1 - pan) * y[i]; drums[1][i0 + i] += gain * (1 + pan) * y[i];
    }
  };
  // bass: a saw through a one-pole low pass plus a sine sub. A legato note changes pitch without a new attack.
  const bassPhrase = (ns) => {
    // ns: [{t0, t1, midi, legato}] contiguous runs share one envelope
    let ph = 0, ph2 = 0, lp = 0, f = 0;
    for (let k = 0; k < ns.length; k++) {
      const note = ns[k];
      const i0 = Math.round(note.t0 * rate), i1 = Math.round(note.t1 * rate);
      const target = midiHz(note.midi);
      if (!note.legato) f = target;
      const next = ns[k + 1];
      const releases = !next || !next.legato;
      const end = releases ? i1 + Math.round(0.02 * rate) : i1;
      for (let i = i0; i < end && i < n; i++) {
        f += (target - f) * 0.02; // a short glide into a legato note (about 1 ms)
        ph = (ph + f / rate) % 1; ph2 = (ph2 + f / rate) % 1;
        const saw = 2 * ph - 1;
        lp += 0.06 * (saw - lp);
        const att = note.legato ? 1 : Math.min(1, (i - i0) / (0.003 * rate));
        const rel = i >= i1 ? Math.max(0, 1 - (i - i1) / (0.02 * rate)) : 1;
        const v = 0.42 * att * rel * (0.6 * lp + 0.7 * Math.sin(2 * Math.PI * ph2));
        bass[0][i] += v; bass[1][i] += v;
      }
    }
  };
  const pad = (t0, t1, chord, gain) => {
    const i0 = Math.round(t0 * rate), i1 = Math.min(n, Math.round((t1 + 0.3) * rate));
    const voices = chord.flatMap((m) => [-0.08, 0.08].map((d) => ({ f: midiHz(m + d), ph: R() })));
    let lpL = 0, lpR = 0;
    for (let i = i0; i < i1; i++) {
      const t = (i - i0) / rate, rel = t > t1 - t0 ? Math.max(0, 1 - (t - (t1 - t0)) / 0.3) : 1;
      const env = gain * Math.min(1, t / 0.06) * rel;
      let l = 0, r = 0;
      voices.forEach((v, j) => {
        v.ph = (v.ph + v.f / rate) % 1;
        const s = 2 * v.ph - 1;
        if (j & 1) r += s; else l += s;
      });
      lpL += 0.08 * (l - lpL); lpR += 0.08 * (r - lpR);
      other[0][i] += env * lpL; other[1][i] += env * lpR;
    }
  };

  // ── the arrangement ──
  /** @type {{t0: number, t1: number, midi: number, legato: boolean}[]} */
  let phrase = [];
  const flush = () => { if (phrase.length) bassPhrase(phrase); phrase = []; };
  const addNote = (step, lane, kind) => notes.push({ t: stepTime(step), step, lane, kind });
  for (let bar = 0; bar < nBars; bar++) {
    const sec = sectionOf(bar), b0 = bar * 16, inSec = bar - sec.start, chordIx = bar % 4;
    const drop = sec.kind === 'drop';
    const lastOfPhrase = inSec % 8 === 7 && drop;
    // kick: four on the floor, except the break and the build; a fill closes each 8-bar phrase of a drop
    if (sec.kind !== 'break' && sec.kind !== 'build') {
      const steps = lastOfPhrase ? [0, 4, 8, 12, 14, 15] : [0, 4, 8, 12];
      for (const s of steps) { kick(stepTime(b0 + s), 0.95); addNote(b0 + s, LANES.kick, s % 4 ? 'fill' : 'beat'); }
    }
    // clap on 2 and 4 (from the intro's third bar); the build rolls 8ths then 16ths
    let claps = [];
    if (sec.kind === 'build') claps = inSec === 0 ? [0, 2, 4, 6, 8, 10, 12, 14] : [0, 2, 4, 6, 8, 9, 10, 11, 12, 13, 14, 15];
    else if (sec.kind === 'break') claps = inSec >= sec.bars / 2 ? [4, 12] : [];
    else if (sec.kind !== 'intro' || inSec >= 2) claps = [4, 12];
    if (drop && inSec % 8 === 3) claps = [4, 11, 12]; // a syncopated extra clap now and then
    for (const s of claps) {
      clap(stepTime(b0 + s), sec.kind === 'build' ? 0.35 + 0.3 * (s / 16 + inSec) / 2 : 0.6);
      addNote(b0 + s, LANES.clap, s === 4 || s === 12 ? 'beat' : 'fill');
    }
    // hats (drum stem, above the clap band; never notes): open on the off-beats, closed 16ths in the drops
    if (drop || (sec.kind === 'intro' && inSec >= 4) || sec.kind === 'outro') {
      for (const s of [2, 6, 10, 14]) hat(stepTime(b0 + s), 0.22, 0.05, 0.2);
    }
    if (drop) for (let s = 0; s < 16; s++) if (s % 4 !== 2) hat(stepTime(b0 + s), 0.08, 0.012, -0.3);
    // bass: the syncopated bar in the drops; long root notes in the first half of the break; none in the intro
    if (drop) {
      for (const [s, len, semi, legato] of BASS_BAR) {
        if (!legato) flush();
        phrase.push({ t0: stepTime(b0 + s), t1: stepTime(b0 + s + len), midi: ROOTS[chordIx] + semi, legato });
        addNote(b0 + s, LANES.bass, legato ? 'legato' : 'attack');
      }
      flush();
    } else if (sec.kind === 'break' && inSec < sec.bars / 2) {
      phrase.push({ t0: stepTime(b0), t1: stepTime(b0 + 12), midi: ROOTS[chordIx], legato: false });
      addNote(b0, LANES.bass, 'attack');
      flush();
    }
    // the pad: every bar but the first two
    if (bar >= 2) pad(stepTime(b0), stepTime(b0 + 16) - 0.01, CHORDS[chordIx], sec.kind === 'break' ? 0.05 : 0.035);
  }
  // stem bleed, as a separation model leaves it: some of the kick in the bass stem
  if (o.bleed) {
    const lp = filter(filter(kickOnly, biquad('lowpass', 200, rate)), biquad('lowpass', 200, rate));
    for (let i = 0; i < n; i++) { bass[0][i] += o.bleed * lp[i]; bass[1][i] += o.bleed * lp[i]; }
  }
  // one gain for every stem so the master is their sum and peaks under full scale
  const master = mk();
  let peak = 0;
  for (let c = 0; c < 2; c++) {
    for (let i = 0; i < n; i++) {
      const v = drums[c][i] + bass[c][i] + other[c][i];
      master[c][i] = v;
      peak = Math.max(peak, Math.abs(v));
    }
  }
  const g = 0.89 / peak;
  for (const s of [drums, bass, other, master]) for (const c of s) for (let i = 0; i < n; i++) c[i] *= g;
  notes.sort((a, b) => a.t - b.t || a.lane - b.lane);
  const beats = Array.from({ length: nBeats }, (_, i) => time(i));
  return {
    rate,
    stems: { master, drums, bass, other, vocals: mk() },
    truth: {
      bpm, drift, bleed: o.bleed ?? 0, rate, length_s: n / rate, lead_in: leadIn, beats, downbeat: 0,
      sections: sections.map((s) => ({ t: time(s.start * 4), kind: s.kind })),
      notes,
    },
  };
}
