// The DSP pieces of the song pipeline, each small enough to test against a known signal (tools/dsp.test.mjs):
// filters (RBJ biquads, zero-phase forward-backward filtering, the crossover), the FFT and the STFT, spectral
// flux, the adaptive threshold and peak picking, the tempo autocorrelation, the dynamic-programming beat tracker,
// YIN pitch, and cross-correlation lag.

/** Seeded random numbers (mulberry32): the test songs are the same on every run. */
export function rng(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// ── Filters ────────────────────────────────────────────────────────────

/**
 * RBJ cookbook biquad coefficients, normalised so a0 = 1.
 * @param {'lowpass'|'highpass'|'bandpass'} type
 * @param {number} f0 corner or centre frequency in Hz
 * @param {number} rate sample rate
 * @param {number} [q] quality (0.7071 = Butterworth)
 */
export function biquad(type, f0, rate, q = Math.SQRT1_2) {
  const w = (2 * Math.PI * f0) / rate, cw = Math.cos(w), al = Math.sin(w) / (2 * q);
  let b0, b1, b2;
  if (type === 'lowpass') { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; }
  else if (type === 'highpass') { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; }
  else if (type === 'bandpass') { b0 = al; b1 = 0; b2 = -al; }
  else throw new Error(`unknown biquad ${type}`);
  const a0 = 1 + al;
  return { b0: b0 / a0, b1: b1 / a0, b2: b2 / a0, a1: (-2 * cw) / a0, a2: (1 - al) / a0 };
}

/**
 * Run a biquad over a signal (direct form 1), returning a new array.
 * @param {Float32Array|Float64Array} x
 * @param {ReturnType<typeof biquad>} c
 */
export function filter(x, c) {
  const y = new Float32Array(x.length);
  let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (let i = 0; i < x.length; i++) {
    const x0 = x[i];
    const y0 = c.b0 * x0 + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
    x2 = x1; x1 = x0; y2 = y1; y1 = y0;
    y[i] = y0;
  }
  return y;
}

/**
 * Zero-phase filtering: the biquad forward, then backward. Squares the magnitude response and cancels the phase,
 * so a band keeps its transients exactly where they were (no group delay).
 * @param {Float32Array} x
 * @param {ReturnType<typeof biquad>} c
 */
export function filtfilt(x, c) {
  const y = filter(x, c);
  y.reverse();
  const z = filter(y, c);
  z.reverse();
  return z;
}

/**
 * The drum crossover: a zero-phase Butterworth low pass (forward-backward, so -6 dB at the corner: the
 * Linkwitz-Riley magnitude) and its exact complement. low + high is the input sample for sample, and neither band
 * is delayed, so the kick and clap tracks line up with the other stems.
 * @param {Float32Array} x
 * @param {number} rate
 * @param {number} [fc] crossover frequency in Hz
 * @returns {{low: Float32Array, high: Float32Array}}
 */
export function crossover(x, rate, fc = 150) {
  const low = filtfilt(x, biquad('lowpass', fc, rate));
  const high = new Float32Array(x.length);
  for (let i = 0; i < x.length; i++) high[i] = x[i] - low[i];
  return { low, high };
}

// ── FFT and STFT ───────────────────────────────────────────────────────

/**
 * In-place iterative radix-2 complex FFT.
 * @param {Float64Array} re
 * @param {Float64Array} im
 * @param {boolean} [inverse]
 */
export function fft(re, im, inverse = false) {
  const n = re.length;
  if (n & (n - 1)) throw new Error('fft size must be a power of two');
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = ((inverse ? 2 : -2) * Math.PI) / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2;
        const tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti;
        re[a] += tr; im[a] += ti;
        const t = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = t;
      }
    }
  }
  if (inverse) for (let i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
}

/**
 * Log-compressed STFT magnitudes of the bins in [fLo, fHi]. Frame k is centred on sample k * hop.
 * @param {Float32Array} x
 * @param {number} rate
 * @param {{size?: number, hop?: number, fLo?: number, fHi?: number, gamma?: number}} [o]
 * @returns {{frames: Float32Array[], hop: number, rate: number, frameRate: number, bin0: number}}
 */
export function stft(x, rate, o = {}) {
  const size = o.size ?? 1024, hop = o.hop ?? 128, gamma = o.gamma ?? 1000;
  const bin0 = Math.max(1, Math.floor(((o.fLo ?? 0) * size) / rate));
  const bin1 = Math.min(size / 2, Math.ceil(((o.fHi ?? rate / 2) * size) / rate));
  const win = new Float64Array(size);
  for (let i = 0; i < size; i++) win[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / size);
  const nFrames = Math.floor(x.length / hop) + 1;
  const re = new Float64Array(size), im = new Float64Array(size);
  const frames = [];
  for (let k = 0; k < nFrames; k++) {
    const c = k * hop - size / 2;
    for (let i = 0; i < size; i++) {
      const p = c + i;
      re[i] = p >= 0 && p < x.length ? x[p] * win[i] : 0;
      im[i] = 0;
    }
    fft(re, im);
    const m = new Float32Array(bin1 - bin0 + 1);
    for (let b = bin0; b <= bin1; b++) m[b - bin0] = Math.log1p(gamma * Math.hypot(re[b], im[b]) / size);
    frames.push(m);
  }
  return { frames, hop, rate, frameRate: rate / hop, bin0 };
}

/**
 * Spectral flux: the summed rise of each bin's log magnitude against `lag` frames earlier (half-wave rectified, so
 * only energy arriving counts).
 * @param {ReturnType<typeof stft>} s
 * @param {number} [lag]
 * @returns {Float32Array}
 */
export function spectralFlux(s, lag = 2) {
  const f = new Float32Array(s.frames.length);
  for (let k = lag; k < s.frames.length; k++) {
    const a = s.frames[k], b = s.frames[k - lag];
    let sum = 0;
    for (let i = 0; i < a.length; i++) { const d = a[i] - b[i]; if (d > 0) sum += d; }
    f[k] = sum;
  }
  return f;
}

// ── Peak picking ───────────────────────────────────────────────────────

/** Median of a numeric array (copies). */
export function median(a) {
  if (!a.length) return 0;
  const s = Float64Array.from(a).sort();
  const m = s.length >> 1;
  return s.length & 1 ? s[m] : (s[m - 1] + s[m]) / 2;
}

/** The p-th percentile (0..1) of a numeric array. */
export function percentile(a, p) {
  if (!a.length) return 0;
  const s = Float64Array.from(a).sort();
  return s[Math.min(s.length - 1, Math.max(0, Math.round(p * (s.length - 1))))];
}

/**
 * Adaptive threshold: the moving median plus `k` times the moving mean absolute deviation, over +-`win` frames,
 * never below `floor`.
 * @param {Float32Array} f
 * @param {number} win
 * @param {number} k
 * @param {number} floor
 */
export function adaptiveThreshold(f, win, k, floor) {
  const t = new Float32Array(f.length);
  const step = Math.max(1, Math.floor(win / 8)); // the median moves slowly: compute it every few frames
  let med = 0, dev = 0;
  for (let i = 0; i < f.length; i++) {
    if (i % step === 0) {
      const lo = Math.max(0, i - win), hi = Math.min(f.length, i + win + 1);
      const w = f.subarray(lo, hi);
      med = median(w);
      let d = 0;
      for (let j = 0; j < w.length; j++) d += Math.abs(w[j] - med);
      dev = d / w.length;
    }
    t[i] = Math.max(floor, med + k * dev);
  }
  return t;
}

/**
 * Peaks of `f` above the threshold `t` that are the largest within +-`gap` frames.
 * @param {Float32Array} f
 * @param {Float32Array} t
 * @param {number} gap
 * @returns {number[]} frame indices
 */
export function pickPeaks(f, t, gap) {
  const out = [];
  for (let i = 1; i < f.length - 1; i++) {
    if (f[i] <= t[i] || f[i] < f[i - 1] || f[i] < f[i + 1]) continue;
    let best = true;
    for (let j = Math.max(0, i - gap); j <= Math.min(f.length - 1, i + gap); j++) {
      if (f[j] > f[i] || (f[j] === f[i] && j < i)) { best = false; break; }
    }
    if (best) out.push(i);
  }
  return out;
}

// ── Tempo and beats ────────────────────────────────────────────────────

/**
 * Tempo by autocorrelation of an onset envelope over a BPM range, refined to a fraction of a frame (parabola
 * through the best lag and its neighbours). Also scores the double and half lag so a strong 8th-note pulse does not
 * win over the beat.
 * @param {Float32Array} env onset envelope
 * @param {number} frameRate envelope frames per second
 * @param {number} [lo] lowest BPM
 * @param {number} [hi] highest BPM
 * @returns {{bpm: number, period: number}} period in frames
 */
export function tempoAutocorr(env, frameRate, lo = 115, hi = 135) {
  const mean = env.reduce((a, b) => a + b, 0) / env.length;
  const x = Float64Array.from(env, (v) => v - mean);
  const ac = (lag) => {
    let s = 0;
    for (let i = lag; i < x.length; i++) s += x[i] * x[i - lag];
    return s / (x.length - lag);
  };
  const lagLo = Math.floor((60 * frameRate) / hi) - 1, lagHi = Math.ceil((60 * frameRate) / lo) + 1;
  let best = -Infinity, bestLag = lagLo;
  const vals = new Map();
  for (let lag = lagLo; lag <= lagHi; lag++) {
    const v = ac(lag) + 0.5 * ac(2 * lag) + 0.25 * ac(4 * lag);
    vals.set(lag, v);
    if (v > best) { best = v; bestLag = lag; }
  }
  const a = vals.get(bestLag - 1) ?? best, c = vals.get(bestLag + 1) ?? best;
  const den = a - 2 * best + c;
  const period = bestLag + (den < 0 ? (0.5 * (a - c)) / den : 0);
  return { bpm: (60 * frameRate) / period, period };
}

/**
 * Dynamic-programming beat tracker (Ellis 2007): each frame's score is its onset strength plus the best earlier
 * beat's score, penalised by how far the gap strays from the period (squared log ratio). Backtracking from the best
 * frame near the end gives beats that follow the audio while keeping the tempo.
 * @param {Float32Array} env onset envelope (any scale; normalised here)
 * @param {number} period beat period in frames
 * @param {number} [alpha] tempo tightness
 * @returns {number[]} beat frame indices, ascending
 */
export function dpBeats(env, period, alpha = 400) {
  const n = env.length;
  let sd = 0, mean = 0;
  for (const v of env) mean += v / n;
  for (const v of env) sd += ((v - mean) * (v - mean)) / n;
  const o = Float64Array.from(env, (v) => v / (Math.sqrt(sd) || 1));
  const score = new Float64Array(n), back = new Int32Array(n).fill(-1);
  const lo = Math.round(period / 2), hi = Math.round(period * 2);
  for (let t = 0; t < n; t++) {
    let best = 0, arg = -1;
    for (let tau = t - hi; tau <= t - lo; tau++) {
      if (tau < 0) continue;
      const r = Math.log((t - tau) / period);
      const v = score[tau] - alpha * r * r;
      if (arg < 0 || v > best) { best = v; arg = tau; }
    }
    score[t] = o[t] + (arg >= 0 ? best : 0);
    back[t] = arg;
  }
  // start the backtrace at the best-scoring frame in the last period
  let t = n - 1, bestEnd = -Infinity;
  for (let i = Math.max(0, n - Math.round(period)); i < n; i++) if (score[i] > bestEnd) { bestEnd = score[i]; t = i; }
  const beats = [];
  while (t >= 0) { beats.push(t); t = back[t]; }
  return beats.reverse();
}

/**
 * Least-squares line y = a + b x.
 * @param {number[]} xs
 * @param {number[]} ys
 */
export function lineFit(xs, ys) {
  const n = xs.length;
  let sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (let i = 0; i < n; i++) { sx += xs[i]; sy += ys[i]; sxx += xs[i] * xs[i]; sxy += xs[i] * ys[i]; }
  const den = n * sxx - sx * sx;
  const b = den ? (n * sxy - sx * sy) / den : 0;
  return { a: (sy - b * sx) / n, b };
}

// ── Pitch ──────────────────────────────────────────────────────────────

/**
 * YIN pitch track (de Cheveigne and Kawahara 2002): the cumulative-mean-normalised difference function, the first
 * dip under `threshold`, parabolic refinement.
 * @param {Float32Array} x
 * @param {number} rate
 * @param {{hop?: number, size?: number, fMin?: number, fMax?: number, threshold?: number, fallback?: number}} [o]
 *   fallback: when no dip is under the threshold, take the deepest one if it is under this
 * @returns {{f0: Float32Array, aperiodicity: Float32Array, frameRate: number, hop: number}} f0 = 0 when unvoiced;
 *   frame k is centred on sample k * hop
 */
export function yin(x, rate, o = {}) {
  const hop = o.hop ?? 64, size = o.size ?? 512, fMin = o.fMin ?? 30, fMax = o.fMax ?? 250;
  const thr = o.threshold ?? 0.15;
  const tauMax = Math.min(size - 1, Math.ceil(rate / fMin)), tauMin = Math.max(2, Math.floor(rate / fMax));
  const nFrames = Math.floor(x.length / hop) + 1;
  const f0 = new Float32Array(nFrames), ap = new Float32Array(nFrames).fill(1);
  const d = new Float64Array(tauMax + 2);
  for (let k = 0; k < nFrames; k++) {
    // the difference function reads size + tauMax samples: centre that span on the frame's time
    const start = k * hop - ((size + tauMax) >> 1);
    if (start < 0 || start + size + tauMax + 1 >= x.length) continue;
    let energy = 0;
    for (let i = 0; i < size; i++) energy += x[start + i] * x[start + i];
    if (energy / size < 1e-7) continue;
    d[0] = 1;
    let run = 0;
    for (let tau = 1; tau <= tauMax + 1; tau++) {
      let s = 0;
      for (let i = 0; i < size; i++) { const v = x[start + i] - x[start + i + tau]; s += v * v; }
      run += s;
      d[tau] = run ? (s * tau) / run : 1;
    }
    let tau = -1;
    for (let t = tauMin; t <= tauMax; t++) {
      if (d[t] < thr) {
        while (t + 1 <= tauMax && d[t + 1] < d[t]) t++;
        tau = t;
        break;
      }
    }
    if (tau < 0 && o.fallback) {
      // no dip under the threshold: the deepest dip, if it is periodic enough (a note under a kick, say)
      let best = Infinity;
      for (let t = tauMin; t <= tauMax; t++) if (d[t] < best && d[t] <= d[t - 1] && d[t] <= d[t + 1]) { best = d[t]; tau = t; }
      if (best > o.fallback) tau = -1;
    }
    if (tau < 0) continue;
    const a = d[tau - 1], b = d[tau], c = d[tau + 1], den = a - 2 * b + c;
    const t = tau + (den > 0 ? (0.5 * (a - c)) / den : 0);
    f0[k] = rate / t;
    ap[k] = b;
  }
  return { f0, aperiodicity: ap, frameRate: rate / hop, hop };
}

/**
 * Decimate by an integer factor after a zero-phase low pass at 0.4 of the new Nyquist band edge.
 * @param {Float32Array} x
 * @param {number} rate
 * @param {number} factor
 */
export function decimate(x, rate, factor) {
  let y = x;
  const fc = (0.4 * rate) / factor;
  y = filtfilt(filtfilt(y, biquad('lowpass', fc, rate)), biquad('lowpass', fc, rate));
  const out = new Float32Array(Math.floor(y.length / factor));
  for (let i = 0; i < out.length; i++) out[i] = y[i * factor];
  return out;
}

// ── Cross-correlation ──────────────────────────────────────────────────

/**
 * The lag (in samples, within +-maxLag) at which `b` best matches `a`: positive means b is late. Computed by FFT over
 * the first `len` samples, normalised, refined by a parabola.
 * @param {Float32Array} a
 * @param {Float32Array} b
 * @param {number} maxLag
 * @param {{start?: number, len?: number}} [o]
 * @returns {{lag: number, lagFine: number, peak: number}} peak is the normalised correlation at the lag
 */
export function xcorrLag(a, b, maxLag, o = {}) {
  const start = o.start ?? 0;
  const len = Math.min(o.len ?? 1 << 20, a.length - start, b.length - start);
  let n = 1;
  while (n < 2 * (len + maxLag)) n <<= 1;
  const ar = new Float64Array(n), ai = new Float64Array(n), br = new Float64Array(n), bi = new Float64Array(n);
  let ea = 0, eb = 0;
  for (let i = 0; i < len; i++) {
    ar[i] = a[start + i]; br[i] = b[start + i];
    ea += ar[i] * ar[i]; eb += br[i] * br[i];
  }
  fft(ar, ai); fft(br, bi);
  // corr[k] = sum a[i] b[i + k]  ->  conj(A) * B
  for (let i = 0; i < n; i++) {
    const r = ar[i] * br[i] + ai[i] * bi[i], im = ar[i] * bi[i] - ai[i] * br[i];
    ar[i] = r; ai[i] = im;
  }
  fft(ar, ai, true);
  const at = (k) => ar[(k + n) % n];
  let best = -Infinity, lag = 0;
  for (let k = -maxLag; k <= maxLag; k++) if (at(k) > best) { best = at(k); lag = k; }
  const p = at(lag - 1), q = at(lag + 1), den = p - 2 * best + q;
  const lagFine = lag + (den < 0 ? (0.5 * (p - q)) / den : 0);
  return { lag, lagFine, peak: best / (Math.sqrt(ea * eb) || 1) };
}

// ── Bleed cancelling ───────────────────────────────────────────────────

/**
 * Remove the part of `y` that is a filtered copy of `x` (a stem bleeding into another): the least-squares FIR h of
 * `taps` taps (lags -taps/4 .. 3taps/4, so a slightly early or zero-phase copy is caught too) minimising
 * |y - h * x|^2, then y - h * x. Independent parts (a bass line against a kick) correlate to almost nothing, so h is
 * near zero when there is no bleed. Solved by Cholesky on the normal equations.
 * @param {Float32Array} y
 * @param {Float32Array} x
 * @param {number} [taps]
 * @returns {{clean: Float32Array, removed: number}} removed = the fraction of y's energy taken away
 */
export function cancelBleed(y, x, taps = 96) {
  const n = Math.min(x.length, y.length), off = Math.floor(taps / 4);
  const xa = (i) => (i >= 0 && i < n ? x[i] : 0);
  // autocorrelation of x (Toeplitz) and cross-correlation with y
  const r = new Float64Array(taps), p = new Float64Array(taps);
  for (let k = 0; k < taps; k++) { let s = 0; for (let i = k; i < n; i++) s += x[i] * x[i - k]; r[k] = s; }
  for (let k = 0; k < taps; k++) { let s = 0; const lag = k - off; for (let i = 0; i < n; i++) s += y[i] * xa(i - lag); p[k] = s; }
  const A = new Float64Array(taps * taps);
  for (let i = 0; i < taps; i++) for (let j = 0; j < taps; j++) A[i * taps + j] = r[Math.abs(i - j)] + (i === j ? 1e-6 * r[0] + 1e-12 : 0);
  // Cholesky A = L L^T
  for (let j = 0; j < taps; j++) {
    let s = A[j * taps + j];
    for (let k = 0; k < j; k++) s -= A[j * taps + k] ** 2;
    const d = Math.sqrt(Math.max(s, 1e-30));
    A[j * taps + j] = d;
    for (let i = j + 1; i < taps; i++) {
      let t = A[i * taps + j];
      for (let k = 0; k < j; k++) t -= A[i * taps + k] * A[j * taps + k];
      A[i * taps + j] = t / d;
    }
  }
  const z = new Float64Array(taps), h = new Float64Array(taps);
  for (let i = 0; i < taps; i++) { let s = p[i]; for (let k = 0; k < i; k++) s -= A[i * taps + k] * z[k]; z[i] = s / A[i * taps + i]; }
  for (let i = taps - 1; i >= 0; i--) { let s = z[i]; for (let k = i + 1; k < taps; k++) s -= A[k * taps + i] * h[k]; h[i] = s / A[i * taps + i]; }
  const clean = new Float32Array(y.length);
  let e0 = 0, e1 = 0;
  for (let i = 0; i < y.length; i++) {
    let est = 0;
    for (let k = 0; k < taps; k++) est += h[k] * xa(i - (k - off));
    clean[i] = y[i] - est;
    e0 += y[i] * y[i]; e1 += clean[i] * clean[i];
  }
  return { clean, removed: e0 ? 1 - e1 / e0 : 0 };
}
