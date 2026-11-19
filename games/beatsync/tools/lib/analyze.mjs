// The analysis half of the pipeline (docs/DESIGN.md, "Charts" steps 2 to 5): the drum crossover, tempo and beats,
// onsets per lane, the bars, the sections. Everything here works on mono mixes of the stems.
import {
  crossover, stft, spectralFlux, adaptiveThreshold, pickPeaks, percentile, tempoAutocorr, dpBeats, lineFit, yin,
  decimate, biquad, filtfilt, median, cancelBleed,
} from './dsp.mjs';

const HOP = 128;

/** @param {Float32Array[]} ch */
export function monoOf(ch) {
  if (ch.length === 1) return ch[0];
  const out = new Float32Array(ch[0].length);
  for (let i = 0; i < out.length; i++) out[i] = 0.5 * (ch[0][i] + ch[1][i]);
  return out;
}

/** Scale to unit standard deviation (zero stays zero). */
function unit(f) {
  let s = 0;
  for (const v of f) s += v * v;
  const k = 1 / (Math.sqrt(s / f.length) || 1);
  return Float32Array.from(f, (v) => v * k);
}

/**
 * Amplitude envelope: the RMS over the trailing `ms` milliseconds, one value per sample. Trailing, so a sharp
 * attack at t0 rises from t0 on (a centred window would start it half a window early).
 * @param {Float32Array} x
 * @param {number} rate
 * @param {number} [ms] at least a period of the band's lowest frequency, or the envelope ripples with the wave
 */
export function envelope(x, rate, ms = 2) {
  const w = Math.max(1, Math.round((ms / 1000) * rate)), out = new Float32Array(x.length);
  let acc = 0;
  for (let i = 0; i < x.length; i++) {
    acc += x[i] * x[i];
    if (i >= w) acc -= x[i - w] * x[i - w];
    out[i] = Math.sqrt(Math.max(0, acc) / w);
  }
  return out;
}

/**
 * Move an onset found on the STFT grid to where its attack really starts: the first sample, walking back from the
 * envelope's peak near the frame, that falls under `frac` of that peak.
 * @param {Float32Array} env per-sample envelope of the band
 * @param {number} rate
 * @param {number} t rough onset time in seconds
 * @param {number} [frac]
 */
export function refineOnset(env, rate, t, frac = 0.25) {
  const c = Math.round(t * rate);
  const lo = Math.max(1, c - Math.round(0.01 * rate)), hi = Math.min(env.length - 1, c + Math.round(0.02 * rate));
  let ip = lo;
  for (let i = lo; i <= hi; i++) if (env[i] > env[ip]) ip = i;
  // the floor before the attack: the minimum in the 30 ms before the peak
  let floor = Infinity;
  for (let i = Math.max(0, ip - Math.round(0.03 * rate)); i < ip; i++) floor = Math.min(floor, env[i]);
  if (!Number.isFinite(floor)) floor = 0;
  const level = floor + frac * (env[ip] - floor);
  // the earliest rise through the level in the 35 ms before the peak: a clap's bursts dip between them, so walking
  // back from the peak would stop at the last burst
  let armed = false;
  for (let i = Math.max(0, ip - Math.round(0.035 * rate)); i <= ip; i++) {
    if (env[i] < level) armed = true;
    else if (armed) return i / rate;
  }
  return ip / rate;
}

/**
 * Onsets of one band: spectral flux of its STFT, the adaptive threshold (moving median plus k deviations, never under
 * `rel` of the loudest flux within 1.5 s, so a kick's beater click does not count as a clap next to real claps),
 * peaks at least `gap` seconds apart, each refined in time on the band's envelope.
 * @param {Float32Array} x
 * @param {number} rate
 * @param {{fLo: number, fHi: number, size?: number, gap?: number, k?: number, floorFrac?: number, rel?: number,
 *   envMs?: number, smooth?: number, lag?: number, frac?: number, minRel?: number, refineBand?: Float32Array}} o
 */
export function bandOnsets(x, rate, o) {
  const s = stft(x, rate, { size: o.size ?? 1024, hop: HOP, fLo: o.fLo, fHi: o.fHi });
  // Low notes: neighbouring harmonics leak into the same bins and beat at the fundamental, so a held bass note's
  // magnitudes ripple frame to frame. Averaging each bin over about one period (centred), and comparing against
  // frames that far back, leaves only real arrivals of energy.
  if (o.smooth) smoothFrames(s.frames, o.smooth);
  const flux = spectralFlux(s, o.lag ?? 2);
  const fr = s.frameRate;
  const thr = adaptiveThreshold(flux, Math.round(0.4 * fr), o.k ?? 2, (o.floorFrac ?? 0.12) * percentile(flux, 0.995));
  const rel = o.rel ?? 0.3, w = Math.round(1.5 * fr);
  const localMax = movingMax(flux, w);
  for (let i = 0; i < thr.length; i++) thr[i] = Math.max(thr[i], rel * localMax[i]);
  const peaks = pickPeaks(flux, thr, Math.round((o.gap ?? 0.05) * fr));
  const env = envelope(o.refineBand ?? x, rate, o.envMs ?? 2);
  // and never under `minRel` of the lane's median onset: in a break with no kick, the threshold's local maximum
  // falls to whatever the band leaks (a clap's low end) and would pick that up
  const med = median(peaks.map((k) => flux[k]));
  const onsets = peaks.filter((k) => flux[k] >= (o.minRel ?? 0.25) * med)
    .map((k) => ({ t: refineOnset(env, rate, k / fr, o.frac), strength: flux[k], frame: k }));
  return { onsets, flux, frameRate: fr };
}

/**
 * Centred moving average of each bin over `n` frames, in place.
 * @param {Float32Array[]} frames
 * @param {number} n
 */
export function smoothFrames(frames, n) {
  const bins = frames[0]?.length ?? 0, h = n >> 1, col = new Float32Array(frames.length);
  for (let b = 0; b < bins; b++) {
    for (let k = 0; k < frames.length; k++) col[k] = frames[k][b];
    let acc = 0, cnt = 0;
    for (let k = 0; k < Math.min(frames.length, h); k++) { acc += col[k]; cnt++; }
    for (let k = 0; k < frames.length; k++) {
      if (k + h < frames.length) { acc += col[k + h]; cnt++; }
      if (k - h - 1 >= 0) { acc -= col[k - h - 1]; cnt--; }
      frames[k][b] = acc / cnt;
    }
  }
}

/**
 * Maximum over +-w frames (a monotonic deque, linear time).
 * @param {Float32Array} f
 * @param {number} w
 */
export function movingMax(f, w) {
  const out = new Float32Array(f.length), dq = [];
  let head = 0;
  for (let i = 0; i < f.length + w; i++) {
    if (i < f.length) {
      while (dq.length > head && f[dq[dq.length - 1]] <= f[i]) dq.pop();
      dq.push(i);
    }
    const c = i - w;
    if (c < 0) continue;
    while (dq[head] < c - w) head++;
    out[c] = f[dq[head]];
  }
  return out;
}

/**
 * Tempo and beats. The onset envelope is the kick band's flux (the clap's added at half weight, so a break without a
 * kick still has a pulse); autocorrelation over 115-135 BPM gives the period, the DP tracker places beats, and each
 * beat is then fitted to the kick (or clap) onsets near it by a local straight line over +-8 beats, which keeps the
 * grid exact under a slow drift and steady where a beat has no onset.
 * @param {{kick: ReturnType<typeof bandOnsets>, clap: ReturnType<typeof bandOnsets>}} lanes
 * @param {number} length seconds of audio
 */
export function trackBeats(lanes, length, bpmLo = 115, bpmHi = 135) {
  const fr = lanes.kick.frameRate;
  const kf = unit(lanes.kick.flux), cf = unit(lanes.clap.flux);
  const env = Float32Array.from(kf, (v, i) => v + 0.5 * (cf[i] ?? 0));
  const { bpm: bpm0, period } = tempoAutocorr(kf, fr, bpmLo, bpmHi);
  const dp = dpBeats(env, period).map((k) => k / fr);
  // anchors: kick onsets, else clap onsets, within 40 ms of a DP beat
  const anchorsOf = (list) => list.map((o) => o.t);
  const kicks = anchorsOf(lanes.kick.onsets), claps = anchorsOf(lanes.clap.onsets);
  const near = (arr, t, tol) => {
    let best = null;
    for (const a of arr) if (Math.abs(a - t) <= tol && (best === null || Math.abs(a - t) < Math.abs(best - t))) best = a;
    return best;
  };
  const anchor = dp.map((t) => near(kicks, t, 0.04) ?? near(claps, t, 0.04));
  const fitted = dp.map((t, i) => {
    for (let w = 8; w <= 64; w *= 2) {
      const xs = [], ys = [];
      for (let j = Math.max(0, i - w); j <= Math.min(dp.length - 1, i + w); j++) if (anchor[j] !== null) { xs.push(j); ys.push(anchor[j]); }
      if (xs.length < 4) continue;
      let { a, b } = lineFit(xs, ys);
      // one pass of outlier rejection (a stray onset 10 ms off would bend the line)
      const keep = xs.map((x, k) => Math.abs(ys[k] - (a + b * x)) < 0.012);
      if (keep.filter(Boolean).length >= 4) ({ a, b } = lineFit(xs.filter((_, k) => keep[k]), ys.filter((_, k) => keep[k])));
      return a + b * i;
    }
    return t;
  });
  // trim to the music: from just before the first onset to the end of the audio
  const first = Math.min(kicks[0] ?? Infinity, claps[0] ?? Infinity);
  const beatLen = 60 / bpm0;
  const beats = fitted.filter((t) => t >= first - 0.25 * beatLen && t <= length - 0.02);
  const span = beats[beats.length - 1] - beats[0];
  return { beats, bpm: span > 0 ? (60 * (beats.length - 1)) / span : bpm0, bpmAutocorr: bpm0, dpBeats: dp };
}

/**
 * Carry the beat grid back over an intro the drums do not play in (only bass, hats or the other stem's chords and
 * synth lines), so that intro can be charted. trackBeats starts the grid at the first kick or clap; from there each
 * earlier beat is predicted one period back and pulled toward the onsets around its four 16ths (a phase-locked
 * loop: half the median residual on the phase, a tenth on the period, at most 15 ms a beat, the period kept within
 * 3% of the drums' own). On the measured songs this puts the intro's onsets a median 5 and 11% of a 16th off the
 * grid, against 28 and 31% for a straight extrapolation.
 * @param {number[]} beats
 * @param {{t: number}[]} onsets the bass, hat and other-stem onsets
 * @returns {number[]} the beats, with the added ones in front
 */
export function extendBeats(beats, onsets) {
  if (beats.length < 2) return beats;
  const n = Math.min(16, beats.length);
  const p0 = (beats[n - 1] - beats[0]) / (n - 1);
  const ts = onsets.map((o) => o.t).filter((t) => t < beats[0] - 0.1 * p0).sort((a, b) => a - b);
  if (!ts.length) return beats;
  const start = ts[0];
  let p = p0, t = beats[0];
  const out = [];
  while (t - p >= start - 0.25 * p) {
    let pred = t - p;
    const res = [];
    for (const x of ts) {
      if (x < pred - p / 8 || x >= pred + p - p / 8) continue;
      const pos = (x - pred) / (p / 4), r = pos - Math.round(pos);
      if (Math.abs(r) < 0.35) res.push((r * p) / 4);
    }
    if (res.length >= 2) {
      const e = Math.max(-0.015, Math.min(0.015, median(res)));
      pred += 0.5 * e;
      p = Math.max(0.97 * p0, Math.min(1.03 * p0, p - 0.1 * e));
    }
    out.push(pred);
    t = pred;
  }
  return [...out.reverse(), ...beats];
}

/**
 * The first bar's downbeat: claps fall on beats 2 and 4, which fixes the beat's parity in the bar; the harmonic
 * instruments (the other stem) change at bar starts, which picks between the two remaining phases.
 * @param {number[]} beats
 * @param {{clap: ReturnType<typeof bandOnsets>, other: ReturnType<typeof bandOnsets>}} lanes
 */
export function findDownbeat(beats, lanes) {
  const at = (lane, t) => {
    const fr = lane.frameRate, k = Math.round(t * fr);
    let m = 0;
    for (let i = Math.max(0, k - Math.round(0.03 * fr)); i <= Math.min(lane.flux.length - 1, k + Math.round(0.03 * fr)); i++) m = Math.max(m, lane.flux[i]);
    return m;
  };
  const clap = beats.map((t) => at(lanes.clap, t)), other = beats.map((t) => at(lanes.other, t));
  const sum = (arr, phase, mod) => arr.reduce((s, v, i) => s + ((i - phase) % mod === 0 && i >= phase ? v : 0), 0) / Math.max(1, arr.filter((_, i) => i >= phase && (i - phase) % mod === 0).length);
  // claps on beats 2 and 4 = odd beats counted from the downbeat
  const parity = sum(clap, 1, 2) >= sum(clap, 0, 2) ? 0 : 1;
  const a = parity, b = parity + 2;
  const score = (p) => sum(other, p, 4) - 0.25 * sum(clap, p, 4);
  const downbeat = score(a) >= score(b) ? a : b;
  return { downbeat, clapParity: parity };
}

/**
 * Bass onsets: spectral-flux attacks plus pitch changes (YIN), since a legato bass note has no attack. An attack that
 * lands on a kick is kept only if it looks like a bass note and not kick bleed: a new pitch, or a steady pitch rising
 * out of quiet.
 * @param {Float32Array} bass mono bass stem
 * @param {number} rate
 * @param {{t: number}[]} kickOnsets
 */
export function bassOnsets(bass, rate, kickOnsets) {
  const low = filtfilt(bass, biquad('lowpass', 400, rate));
  const att = bandOnsets(bass, rate, { fLo: 25, fHi: 400, size: 2048, gap: 0.06, k: 2, rel: 0.2, envMs: 10, frac: 0.12, smooth: 9, lag: 6, minRel: 0.1, refineBand: low });
  const factor = Math.max(1, Math.round(rate / 11025));
  const dec = decimate(bass, rate, factor), drate = rate / factor;
  const p = yin(dec, drate, { hop: 64, size: 512, fMin: 30, fMax: 300, threshold: 0.2 });
  const midi = Float32Array.from(p.f0, (f) => (f > 0 ? 69 + 12 * Math.log2(f / 440) : NaN));
  const fr = p.frameRate;
  const env = envelope(low, rate, 5);
  const midiAt = (t0, t1) => {
    const vals = [];
    for (let k = Math.max(0, Math.round(t0 * fr)); k <= Math.min(midi.length - 1, Math.round(t1 * fr)); k++) if (!Number.isNaN(midi[k])) vals.push(midi[k]);
    return vals;
  };
  const spread = (v) => { const m = median(v); return median(v.map((x) => Math.abs(x - m))); };
  const level = (t0, t1) => {
    let m = 0;
    for (let i = Math.max(0, Math.round(t0 * rate)); i < Math.min(env.length, Math.round(t1 * rate)); i++) m = Math.max(m, env[i]);
    return m;
  };
  const kicks = kickOnsets.map((o) => o.t);
  const onKick = (t) => kicks.some((k) => Math.abs(k - t) < 0.025);
  const kept = [], rejected = [];
  for (const o of att.onsets) {
    if (!onKick(o.t)) { kept.push({ ...o, how: 'attack' }); continue; }
    const before = midiAt(o.t - 0.06, o.t - 0.01), after = midiAt(o.t + 0.02, o.t + 0.07);
    const newPitch = before.length >= 3 && after.length >= 3 && Math.abs(median(after) - median(before)) > 0.7 && spread(after) < 0.3;
    const fromQuiet = level(o.t - 0.03, o.t - 0.005) < 0.35 * level(o.t + 0.01, o.t + 0.06) && after.length >= 5 && spread(after) < 0.2;
    (newPitch || fromQuiet ? kept : rejected).push({ ...o, how: newPitch ? 'attack+pitch' : 'attack' });
  }
  // pitch changes: split the voiced pitch track into steady runs (each frame within half a semitone of its run's
  // median); two runs at least 25 ms long, at most 80 ms apart and more than 0.7 semitone different are a change of
  // note, placed half way between them. A kick's falling sweep never holds a pitch long enough to make a run, and a
  // run that is itself gliding (its first and last frames apart by more than 0.3 semitone, a kick's tail settling)
  // never makes a change.
  const changes = [];
  const runs = [];
  let cur = null;
  for (let k = 0; k < midi.length; k++) {
    const m = midi[k];
    if (Number.isNaN(m)) { if (cur) { runs.push(cur); cur = null; } continue; }
    if (cur && Math.abs(m - median(cur.vals.slice(-8))) < 0.5) { cur.vals.push(m); cur.end = k; continue; }
    if (cur) runs.push(cur);
    cur = { start: k, end: k, vals: [m] };
  }
  if (cur) runs.push(cur);
  const steady = runs.filter((r) => (r.end - r.start + 1) / fr >= 0.025)
    .map((r) => ({ ...r, pitch: median(r.vals), level: env[Math.min(env.length - 1, Math.round(((r.start + r.end) / 2 / fr) * rate))] }));
  const usual = percentile(steady.map((r) => r.level), 0.9);
  for (let i = 1; i < steady.length; i++) {
    const a = steady[i - 1], b = steady[i];
    if ((b.start - a.end) / fr > 0.12 || Math.abs(b.pitch - a.pitch) <= 0.7) continue;
    // edges excluded: YIN bends a frame or two toward the next note at a boundary
    const drift = (r) => (r.vals.length < 8 ? Math.abs(r.vals[r.vals.length - 1] - r.vals[0]) - 0.2
      : Math.abs(median(r.vals.slice(-6, -2)) - median(r.vals.slice(2, 6))));
    if (drift(a) > 0.3 || drift(b) > 0.3) continue;
    // a step, not a slide: the runs touch with a jump between neighbouring frames, or are split by unvoiced frames
    if (b.start - a.end < 2 && Math.abs(b.vals[0] - a.vals[a.vals.length - 1]) < 0.5) continue;
    if (b.level < 0.3 * usual) continue; // a quiet tail (kick bleed ringing on) is not a note
    changes.push({ t: (a.end + b.start) / 2 / fr, jump: Math.abs(b.pitch - a.pitch) });
  }
  const medStrength = median(kept.map((o) => o.strength)) || 1;
  const all = [...kept];
  for (const c of changes) {
    const t = c.t;
    if (all.some((o) => Math.abs(o.t - t) < 0.05)) continue;
    all.push({ t, strength: 0.8 * medStrength, frame: Math.round(t * att.frameRate), how: 'pitch' });
  }
  all.sort((a, b) => a.t - b.t);
  return { onsets: all, flux: att.flux, frameRate: att.frameRate, rejected, pitch: p };
}

/**
 * Sections from per-bar energy: drop (kick and bass), break (no kick), build (the end of a break where the claps
 * thicken), intro (kick without bass before the first drop), outro (after the last drop).
 * Bars are classed by onset counts, not energy: a kick's tail rings into the next bar, and kick bleed in the bass stem
 * has energy but (after bassOnsets' bleed test) no bass onsets.
 * @param {{kick: {onsets: {t: number}[]}, bass: {onsets: {t: number}[]}, clap: {onsets: {t: number}[]}}} m
 * @param {number[]} beats
 * @param {number} downbeat
 */
export function findSections(m, beats, downbeat) {
  const bars = [];
  for (let b = downbeat; b + 4 < beats.length + 1; b += 4) {
    const t0 = beats[b], t1 = b + 4 < beats.length ? beats[b + 4] : beats[beats.length - 1] + (beats[1] - beats[0]);
    const count = (lane) => lane.onsets.filter((o) => o.t >= t0 - 0.02 && o.t < t1 - 0.02).length;
    bars.push({ t: t0, kick: count(m.kick), bass: count(m.bass), claps: count(m.clap) });
  }
  if (!bars.length) return [];
  const cls = bars.map((b) => (b.kick < 2 ? 'break' : b.bass >= 2 ? 'drop' : 'nobass'));
  const firstDrop = cls.indexOf('drop'), lastDrop = cls.lastIndexOf('drop');
  const kinds = cls.map((c, i) => {
    if (c !== 'nobass') return c;
    if (firstDrop < 0 || i < firstDrop) return 'intro';
    if (i > lastDrop) return 'outro';
    return 'drop';
  });
  // a break's last bars become a build where the claps run at twice the break's usual count or more
  for (let i = 0; i < kinds.length; i++) {
    if (kinds[i] !== 'break') continue;
    let j = i;
    while (j < kinds.length && kinds[j] === 'break') j++;
    if (j < kinds.length && kinds[j] === 'drop') {
      const usual = percentile(bars.slice(i, j).map((b) => b.claps), 0.25);
      for (let k = j - 1; k >= i && bars[k].claps >= Math.max(4, 2 * usual); k--) kinds[k] = 'build';
    }
    i = j - 1;
  }
  const out = [];
  kinds.forEach((k, i) => { if (!out.length || out[out.length - 1].kind !== k) out.push({ t: +bars[i].t.toFixed(4), kind: k, bar: i }); });
  // a one-bar section is noise: fold it into the one before (the first into the one after, from the first bar on);
  // whatever follows the last drop is the outro, the song's ringing tail included
  const end = bars.length;
  let kept = out.filter((s, i) => i === 0 || (i + 1 < out.length ? out[i + 1].bar : end) - s.bar > 1);
  if (kept.length > 1 && kept[1].bar - kept[0].bar <= 1) kept = [{ ...kept[1], t: kept[0].t, bar: 0 }, ...kept.slice(2)];
  const lastDropIx = kept.map((s) => s.kind).lastIndexOf('drop');
  if (lastDropIx >= 0) kept = kept.map((s, i) => (i > lastDropIx ? { ...s, kind: 'outro' } : s));
  return kept.filter((s, i) => i === 0 || s.kind !== kept[i - 1].kind).map(({ t, kind }) => ({ t, kind }));
}

/**
 * Keep the clap lane to claps (and snares): an onset whose rise is mostly above 6 kHz (per bin, more than
 * HAT_RATIO of its 1-4 kHz rise) is a hi-hat or shaker; a clap-band onset on a kick has to be a real clap's size
 * (at least 45% of the lane's 90th-percentile strength), or it is the kick's beater click.
 * @param {ReturnType<typeof bandOnsets>} clap
 * @param {ReturnType<typeof bandOnsets>} kick
 * @param {Float32Array} clapTrack mono clap track (the drum stem above 150 Hz)
 * @param {number} rate
 */
export function clapFilter(clap, kick, clapTrack, rate) {
  const HAT_RATIO = 0.75;
  const sMid = stft(clapTrack, rate, { size: 1024, hop: HOP, fLo: 1000, fHi: 4000 });
  const sHi = stft(clapTrack, rate, { size: 1024, hop: HOP, fLo: 6000, fHi: Math.min(14000, rate / 2 - 500) });
  const mid = spectralFlux(sMid, 2), hi = spectralFlux(sHi, 2);
  const nMid = sMid.frames[0].length, nHi = sHi.frames[0].length;
  const ref = percentile(clap.onsets.map((o) => o.strength), 0.9);
  const kicks = kick.onsets.map((o) => o.t);
  const onsets = [], hats = [], clicks = [];
  for (const o of clap.onsets) {
    let m = 0, h = 0;
    for (let j = o.frame - 2; j <= o.frame + 2; j++) { m = Math.max(m, mid[j] ?? 0); h = Math.max(h, hi[j] ?? 0); }
    if (h / nHi > HAT_RATIO * (m / nMid)) hats.push(o);
    else if (kicks.some((k) => Math.abs(k - o.t) < 0.015) && o.strength < 0.45 * ref) clicks.push(o);
    else onsets.push(o);
  }
  return { onsets, rejected: clicks, hats };
}

/**
 * Run the whole analysis on a song's stems.
 * @param {import('./stems.mjs').Stems} stems
 * @param {{bpmMin?: number, bpmMax?: number}} [o] the tempo search range (default 115-135)
 */
export function analyze(stems, o = {}) {
  const rate = stems.rate;
  const split = stems.tracks.drums.map((c) => crossover(c, rate));
  const tracks = {
    kick: split.map((s) => s.low), clap: split.map((s) => s.high), bass: stems.tracks.bass, other: stems.tracks.other,
  };
  const kickM = monoOf(tracks.kick), clapM = monoOf(tracks.clap), bassM = monoOf(tracks.bass), otherM = monoOf(tracks.other);
  const length = kickM.length / rate;
  const clapBand = filtfilt(filtfilt(clapM, biquad('highpass', 1000, rate)), biquad('lowpass', 5000, rate));
  const lanes = {
    kick: bandOnsets(kickM, rate, { fLo: 20, fHi: 160, gap: 0.05, k: 2, envMs: 8, minRel: 0.2 }),
    clap: bandOnsets(clapM, rate, { fLo: 1000, fHi: 5000, gap: 0.05, k: 2, refineBand: clapBand }),
    other: bandOnsets(otherM, rate, { fLo: 80, fHi: 4000, gap: 0.1, k: 2 }),
  };
  lanes.clap = { ...lanes.clap, ...clapFilter(lanes.clap, lanes.kick, clapM, rate) };
  // Kick bleed in the bass stem (a separation model's usual leak) is cancelled before the bass is analysed: a
  // least-squares filtered copy of the kick track, fitted at a quarter of the rate (the bass band only).
  const f4 = Math.max(1, Math.round(rate / 11025));
  const bassD = decimate(bassM, rate, f4), kickD = decimate(kickM, rate, f4);
  const bleed = cancelBleed(bassD, kickD, 96);
  const bassClean = new Float32Array(bassM.length);
  const est = (j) => (j < bassD.length ? bassD[j] - bleed.clean[j] : 0); // the bleed estimate, 0 past the end
  for (let i = 0; i < bassM.length; i++) {
    const p = i / f4, j = Math.floor(p), fr = p - j;
    bassClean[i] = bassM[i] - est(j) * (1 - fr) - est(j + 1) * fr;
  }
  // one NaN here would spread through every zero-phase filter after it and silence the whole bass lane
  if (!bassClean.every(Number.isFinite)) throw new Error('bleed cancelling produced a non-finite sample');
  lanes.bass = bassOnsets(bassClean, rate, lanes.kick.onsets);
  lanes.bass.bleedRemoved = bleed.removed;
  const beat = trackBeats(lanes, length, o.bpmMin ?? 115, o.bpmMax ?? 135);
  // the bar phase is found on the drums' grid; the grid is then carried back over a drumless intro, which moves
  // the downbeat's index by the beats added
  const found = findDownbeat(beat.beats, lanes).downbeat;
  const beats = extendBeats(beat.beats, [...lanes.bass.onsets, ...(lanes.clap.hats ?? []), ...lanes.other.onsets]);
  const added = beats.length - beat.beats.length;
  const downbeat = (found + added) % 4;
  const sections = findSections(lanes, beats, downbeat);
  return { rate, tracks, length, lanes, ...beat, beats, beatsAdded: added, downbeat, sections };
}
