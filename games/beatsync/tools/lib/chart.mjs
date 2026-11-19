// From onsets to charts: the 16th grid from the tracked beats, quantizing with the outlier drop, the candidate
// scores (strength, place in the bar, the lane that owns that place, novelty), bar-pattern reuse, and the dynamic
// program that picks each difficulty's notes under its gap rules (docs/DESIGN.md, "Charts").

export const LANES = ['kick', 'clap', 'bass'];

/**
 * The levels: which steps of a beat they may use and the minimum gaps in 16ths.
 * `density` caps notes per second over the bars that have notes (so a long break cannot pay for a crowded drop).
 */
export const LEVELS = {
  easy: { grid: 4, same: 4, change: 4, lambda: 0.75, density: 1.6 },
  medium: { grid: 2, same: 2, change: 4, lambda: 0.5, density: 3.5 },
  hard: { grid: 1, same: 1, change: 2, lambda: 0.25, density: 6.0 },
};

/**
 * The time of a 16th step from the beat list (linear between beats, the last interval extended past the end).
 * @param {number[]} beats
 * @param {number} step
 */
export function stepTime(beats, step) {
  const b = Math.floor(step / 4), f = (step - 4 * b) / 4;
  const n = beats.length;
  if (b < 0) return beats[0] + (step / 4) * (beats[1] - beats[0]);
  if (b >= n - 1) return beats[n - 1] + (step / 4 - (n - 1)) * (beats[n - 1] - beats[n - 2]);
  return beats[b] + f * (beats[b + 1] - beats[b]);
}

/**
 * Quantize onset times to the nearest 16th; an onset more than `tol` of a 16th from its step is dropped. Several
 * onsets on one step keep the strongest.
 * @param {{t: number, strength: number}[]} onsets
 * @param {number[]} beats
 * @param {number} [tol]
 * @returns {{step: number, t: number, onset: number, strength: number, err: number}[]} err in 16ths
 */
export function quantize(onsets, beats, tol = 0.4) {
  const out = new Map();
  let b = 0;
  for (const o of onsets) {
    while (b < beats.length - 2 && beats[b + 1] <= o.t) b++;
    const span = beats.length > 1 ? (b < beats.length - 1 ? beats[b + 1] - beats[b] : beats[b] - beats[b - 1]) : 0.5;
    const pos = b + (o.t - beats[b]) / span; // in beats
    const step = Math.round(pos * 4), err = pos * 4 - step;
    if (Math.abs(err) > tol || step < 0) continue;
    const prev = out.get(step);
    if (!prev || o.strength > prev.strength) out.set(step, { step, t: stepTime(beats, step), onset: o.t, strength: o.strength, err });
  }
  return [...out.values()].sort((a, b2) => a.step - b2.step);
}

const METRIC = (pos) => (pos === 0 ? 1 : pos === 8 ? 0.85 : pos % 4 === 0 ? 0.8 : pos % 2 === 0 ? 0.55 : 0.35);

/** The lane that owns a place in the bar: clap on 2 and 4, kick on 1 and 3, bass off the beat. */
const OWNER = (lane, pos) => {
  if (lane === 1) return pos === 4 || pos === 12 ? 0.9 : 0;
  if (lane === 0) return pos === 0 || pos === 8 ? 0.8 : pos % 4 === 0 ? 0.1 : 0;
  return pos % 4 ? 0.3 : 0;
};

/**
 * Score every candidate. Strength is normalised per lane (its median is 1, and it saturates there, so the place in
 * the bar decides between ordinary hits and only a weak onset is marked down); novelty is how rare the
 * candidate's lane and place in the bar are across the song (a fill beats the hundredth plain kick).
 * @param {{step: number, lane: number, strength: number}[]} cands
 * @param {number} barStep the step of the first bar's downbeat
 */
export function scoreCandidates(cands, barStep) {
  const byLane = [[], [], []];
  for (const c of cands) byLane[c.lane].push(c.strength);
  const mid = byLane.map((a) => {
    if (!a.length) return 1;
    const s = [...a].sort((x, y) => x - y);
    return s[Math.floor(0.5 * (s.length - 1))] || 1;
  });
  const pos = (s) => (((s - barStep) % 16) + 16) % 16;
  const count = new Map();
  for (const c of cands) { const k = c.lane * 16 + pos(c.step); count.set(k, (count.get(k) ?? 0) + 1); }
  const bars = Math.max(2, (Math.max(0, ...cands.map((c) => c.step)) - barStep) / 16 + 1);
  return cands.map((c) => {
    const p = pos(c.step);
    const strength = Math.min(1, c.strength / mid[c.lane]);
    const novelty = Math.max(0, Math.log(bars / count.get(c.lane * 16 + p)) / Math.log(bars));
    return { ...c, pos: p, score: 0.3 * strength + 0.35 * METRIC(p) + OWNER(c.lane, p) + 0.4 * novelty };
  });
}

/**
 * Group bars that sound alike: bars whose candidate sets (lane and place) overlap by at least `minJaccard` share a
 * pattern. Returns, per bar index, its cluster, and per cluster the averaged candidates it plays.
 * @param {ReturnType<typeof scoreCandidates>} scored
 * @param {number} barStep
 * @param {number} [minJaccard]
 */
export function barPatterns(scored, barStep, minJaccard = 0.75) {
  const bars = new Map();
  for (const c of scored) {
    const bar = Math.floor((c.step - barStep) / 16);
    if (!bars.has(bar)) bars.set(bar, []);
    bars.get(bar).push(c);
  }
  const keys = (list) => new Set(list.map((c) => c.lane * 16 + c.pos));
  const jac = (a, b) => {
    let i = 0;
    for (const x of a) if (b.has(x)) i++;
    const u = a.size + b.size - i;
    return u ? i / u : 1;
  };
  /** @type {{rep: Set<number>, members: number[]}[]} */
  const clusters = [];
  const barCluster = new Map();
  for (const [bar, list] of [...bars.entries()].sort((a, b) => a[0] - b[0])) {
    const k = keys(list);
    let ix = clusters.findIndex((c) => jac(c.rep, k) >= minJaccard);
    if (ix < 0) { ix = clusters.length; clusters.push({ rep: k, members: [] }); }
    clusters[ix].members.push(bar);
    barCluster.set(bar, ix);
  }
  // the cluster's pattern: each lane+place present in at least 60% of its bars, with the mean score where present
  const patterns = clusters.map((cl) => {
    const acc = new Map();
    for (const bar of cl.members) {
      for (const c of bars.get(bar)) {
        const k = c.lane * 16 + c.pos;
        const a = acc.get(k) ?? { lane: c.lane, pos: c.pos, sum: 0, n: 0 };
        a.sum += c.score; a.n++;
        acc.set(k, a);
      }
    }
    return [...acc.values()].filter((a) => a.n >= 0.6 * cl.members.length).map((a) => ({ lane: a.lane, pos: a.pos, score: a.sum / a.n }));
  });
  return { barCluster, patterns, clusters: clusters.map((c) => c.members) };
}

/**
 * The dynamic program: over the steps in order, with the state (lane of the last note, 16ths since it, capped at
 * the largest gap), choose at most one candidate per step so every same-lane and lane-change gap holds and the
 * summed value (score - lambda) is the largest.
 * @param {{step: number, lane: number, score: number}[]} cands sorted by step, any number per step
 * @param {{grid: number, same: number, change: number}} lv
 * @param {number} lambda
 * @param {{prevLane?: number, prevAge?: number}} [init]
 * @returns {typeof cands}
 */
export function selectNotes(cands, lv, lambda, init = {}) {
  const ok = cands.filter((c) => c.step % lv.grid === 0 && c.score - lambda > 0);
  if (!ok.length) return [];
  const steps = [...new Set(ok.map((c) => c.step))].sort((a, b) => a - b);
  const at = new Map();
  for (const c of ok) { if (!at.has(c.step)) at.set(c.step, []); at.get(c.step).push(c); }
  const CAP = Math.max(lv.same, lv.change);
  const NS = 4 * (CAP + 1); // lane 0..2 or 3 = none, age 0..CAP
  const idx = (lane, age) => lane * (CAP + 1) + Math.min(age, CAP);
  let val = new Float64Array(NS).fill(-Infinity);
  val[idx(init.prevLane ?? 3, init.prevAge ?? CAP)] = 0;
  const hist = [];
  let prevStep = steps[0];
  for (const s of steps) {
    const dt = s - prevStep;
    // age every state by the steps skipped
    const aged = new Float64Array(NS).fill(-Infinity), from = new Int32Array(NS).fill(-1);
    for (let lane = 0; lane < 4; lane++) {
      for (let age = 0; age <= CAP; age++) {
        const v = val[idx(lane, age)];
        if (v === -Infinity) continue;
        const j = idx(lane, age + dt);
        if (v > aged[j]) { aged[j] = v; from[j] = idx(lane, age); }
      }
    }
    const next = new Float64Array(NS).fill(-Infinity), back = new Int32Array(NS).fill(-1), pick = new Array(NS).fill(null);
    for (let lane = 0; lane < 4; lane++) {
      for (let age = 0; age <= CAP; age++) {
        const j = idx(lane, age), v = aged[j];
        if (v === -Infinity) continue;
        if (v > next[j]) { next[j] = v; back[j] = from[j]; pick[j] = null; } // no note here
        for (const c of at.get(s)) {
          const allowed = lane === 3 || (lane === c.lane ? age >= lv.same : age >= lv.change);
          if (!allowed) continue;
          const k = idx(c.lane, 0), w = v + c.score - lambda;
          if (w > next[k]) { next[k] = w; back[k] = from[j]; pick[k] = c; }
        }
      }
    }
    hist.push({ back, pick });
    val = next;
    prevStep = s;
  }
  let j = 0;
  for (let i = 1; i < NS; i++) if (val[i] > val[j]) j = i;
  const out = [];
  for (let h = hist.length - 1; h >= 0; h--) {
    const { back, pick } = hist[h];
    if (pick[j]) out.push(pick[j]);
    j = back[j];
  }
  return out.reverse();
}

/**
 * Drop notes that break a level's gaps (where two bar patterns meet), keeping the higher-scoring one.
 * @param {{step: number, lane: number, score: number}[]} notes sorted
 * @param {{same: number, change: number}} lv
 */
export function enforceGaps(notes, lv) {
  const out = [];
  for (const n of notes) {
    const p = out[out.length - 1];
    if (p && n.step - p.step < (p.lane === n.lane ? lv.same : lv.change)) {
      if (n.score > p.score) out[out.length - 1] = n;
      continue;
    }
    out.push(n);
  }
  return out;
}

/**
 * Make one level's chart. Bars that share a pattern get the same notes: the pattern is chosen once (on the bar
 * repeated three times, keeping the middle copy, so its own wrap-around gaps hold) and laid into every member bar.
 * @param {ReturnType<typeof scoreCandidates>} scored
 * @param {ReturnType<typeof barPatterns>} pats
 * @param {number} barStep
 * @param {{grid: number, same: number, change: number}} lv
 * @param {number} lambda
 */
export function chartLevel(scored, pats, barStep, lv, lambda) {
  const chosen = pats.patterns.map((p) => {
    const tiled = [];
    for (let r = 0; r < 3; r++) for (const c of p) tiled.push({ ...c, step: r * 16 + c.pos });
    tiled.sort((a, b) => a.step - b.step);
    return selectNotes(tiled, lv, lambda).filter((c) => c.step >= 16 && c.step < 32).map((c) => ({ lane: c.lane, pos: c.pos, score: c.score }));
  });
  const notes = [];
  const bars = [...pats.barCluster.keys()].sort((a, b) => a - b);
  for (const bar of bars) {
    const real = new Set(scored.filter((c) => Math.floor((c.step - barStep) / 16) === bar).map((c) => c.lane * 16 + c.pos));
    for (const c of chosen[pats.barCluster.get(bar)]) {
      // only where this bar really has that onset (a shared pattern never invents a note)
      if (real.has(c.lane * 16 + c.pos)) notes.push({ step: barStep + bar * 16 + c.pos, lane: c.lane, score: c.score });
    }
  }
  notes.sort((a, b) => a.step - b.step);
  return enforceGaps(notes, lv);
}

/**
 * Notes per second over the bars that hold notes.
 * @param {{step: number}[]} notes
 * @param {number[]} beats
 * @param {number} downbeat
 */
export function chartDensity(notes, beats, downbeat) {
  const barStep = downbeat * 4, bars = new Set(notes.map((n) => Math.floor((n.step - barStep) / 16)));
  let secs = 0;
  for (const b of bars) secs += stepTime(beats, barStep + 16 * (b + 1)) - stepTime(beats, barStep + 16 * b);
  return secs ? notes.length / secs : 0;
}

/**
 * All three charts. Lambda starts at the level's value and rises until the density cap holds.
 * @param {{step: number, lane: number, strength: number}[]} cands
 * @param {number[]} beats
 * @param {number} downbeat
 * @param {number} playSeconds the seconds the charts span (reported as notes per second of the song)
 */
export function makeCharts(cands, beats, downbeat, playSeconds) {
  const barStep = downbeat * 4;
  const scored = scoreCandidates(cands, barStep);
  const pats = barPatterns(scored, barStep);
  /** @type {Record<string, {t: number, step: number, lane: number}[]>} */
  const charts = {};
  const stats = {};
  for (const [name, lv] of Object.entries(LEVELS)) {
    let lambda = lv.lambda, notes = chartLevel(scored, pats, barStep, lv, lambda);
    while (chartDensity(notes, beats, downbeat) > lv.density && lambda < 3) {
      lambda += 0.05;
      notes = chartLevel(scored, pats, barStep, lv, lambda);
    }
    charts[name] = notes.map((n) => ({ t: +stepTime(beats, n.step).toFixed(4), step: n.step, lane: n.lane }));
    stats[name] = { lambda: +lambda.toFixed(2), notes: notes.length, per_second: +(notes.length / playSeconds).toFixed(2),
      per_second_playing: +chartDensity(notes, beats, downbeat).toFixed(2) };
  }
  return { charts, stats, patterns: { bars: pats.barCluster.size, distinct: pats.patterns.length } };
}
