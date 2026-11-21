// From onsets to charts: the 16th grid from the tracked beats, quantizing with the outlier drop, the candidate
// scores (strength, place in the bar, the lane that owns that place, novelty), bar-pattern reuse, and the dynamic
// program that picks each difficulty's notes under its gap rules, and the break fill that keeps a chart from going
// quiet for more than two bars where the song still plays (docs/DESIGN.md, "Charts").

export const LANES = ['kick', 'clap', 'bass'];

/** Every part a player can play: the three lanes of the one-player game and the synth (the other stem's onsets),
 * which only the two-lane charts play. */
export const PARTS = ['kick', 'clap', 'bass', 'synth'];

/**
 * The levels: which steps of a beat they may use and the minimum gaps in 16ths.
 * `density` caps notes per second over the bars that have notes (so a long break cannot pay for a crowded drop).
 * `bass`: the bass notes a bar pattern keeps where the bass plays in it (Medium plays all three lanes; the drums own
 * the beats and would otherwise take every place, leaving the bass river empty). Easy stays kick and clap.
 */
export const LEVELS = {
  easy: { grid: 4, same: 4, change: 4, lambda: 0.75, density: 1.6 },
  medium: { grid: 2, same: 2, change: 4, lambda: 0.5, density: 3.5, bass: 1 },
  hard: { grid: 1, same: 1, change: 2, lambda: 0.25, density: 6.0 },
};

/** The longest gap between two notes a chart keeps where the song has something to play: two bars, in 16ths. */
export const MAX_GAP = 32;

/** How far after the song's first onset (and before its last) a chart's first (and last) note may be: a bar. */
export const LEAD = 16;

/**
 * The break fill per level: where the main chart leaves more than MAX_GAP, notes are added on `grid` at least
 * `gap` 16ths apart (from each other and from the notes around them). Each level's fill is one step coarser than
 * its drops (Easy half notes, Medium quarters, Hard 8ths), so a break is sparser and still breathes.
 */
export const FILL = {
  easy: { grid: 4, gap: 8 },
  medium: { grid: 4, gap: 4 },
  hard: { grid: 2, gap: 2 },
};

/** The lane each kind of fill onset plays on: hats on the clap lane, the other stem's onsets on the bass lane. */
export const FILL_LANE = { hat: 1, other: 2 };

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

/** The part that owns a place in the bar: clap on 2 and 4, kick on 1 and 3, bass off the beat, the synth on the
 * eighths (a stab or chord pattern rides them). */
const OWNER = (lane, pos) => {
  if (lane === 1) return pos === 4 || pos === 12 ? 0.9 : 0;
  if (lane === 0) return pos === 0 || pos === 8 ? 0.8 : pos % 4 === 0 ? 0.1 : 0;
  if (lane === 3) return pos % 2 === 0 ? 0.2 : 0;
  return pos % 4 ? 0.3 : 0;
};

/**
 * Score every candidate. Strength is normalised per lane (its median is 1, and it saturates there, so the place in
 * the bar decides between ordinary hits and only a weak onset is marked down); novelty is how rare the
 * candidate's lane and place in the bar are across the song (a fill beats the hundredth plain kick).
 * @param {{step: number, lane: number, strength: number}[]} cands
 * @param {number} barStep the step of the first bar's downbeat
 * @param {number} [from] the step the drums start on (bars for novelty are counted from the first bar line there)
 */
export function scoreCandidates(cands, barStep, from = 0) {
  const byLane = [[], [], [], []];
  for (const c of cands) byLane[c.lane].push(c.strength);
  const mid = byLane.map((a) => {
    if (!a.length) return 1;
    const s = [...a].sort((x, y) => x - y);
    return s[Math.floor(0.5 * (s.length - 1))] || 1;
  });
  const pos = (s) => (((s - barStep) % 16) + 16) % 16;
  const count = new Map();
  for (const c of cands) { const k = c.lane * 16 + pos(c.step); count.set(k, (count.get(k) ?? 0) + 1); }
  // counted from the first bar line at or after the drums' first beat, so beats added in front of the drums (an
  // intro charted by the break fill) do not change how rare a drum candidate is
  const bar0 = barStep + 16 * Math.max(0, Math.ceil((from - barStep) / 16));
  const bars = Math.max(2, (Math.max(0, ...cands.map((c) => c.step)) - bar0) / 16 + 1);
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
    const pick = (cands) => selectNotes(cands, lv, lambda).filter((c) => c.step >= 16 && c.step < 32).map((c) => ({ lane: c.lane, pos: c.pos, score: c.score }));
    let notes = pick(tiled);
    // a level that keeps the bass (lv.bass): when the pattern has bass on the level's grid and the pick left too
    // little of it, each bass place in the pattern is tried as a must-have note (the rest picked as usual around it)
    // and the one that costs the pattern least is kept; repeated up to lv.bass bass notes
    const forced = new Set();
    for (let k = 0; lv.bass && k < lv.bass && notes.filter((c) => c.lane === 2).length < lv.bass; k++) {
      let best = null, bestVal = -Infinity;
      for (const c of p) {
        if (c.lane !== 2 || c.pos % lv.grid || forced.has(c.pos)) continue;
        const must = new Set([...forced, c.pos]);
        const got = pick(tiled.map((t) => (t.lane === 2 && must.has(t.pos) && t.step >= 16 && t.step < 32 ? { ...t, score: t.score + 100 } : t)));
        if (!must.size || [...must].some((pos) => !got.some((g) => g.lane === 2 && g.pos === pos))) continue;
        const val = got.reduce((v, g) => v + g.score - lambda - (g.lane === 2 && must.has(g.pos) ? 100 : 0), 0);
        if (val > bestVal) bestVal = val, best = { pos: c.pos, got };
      }
      if (!best) break;
      forced.add(best.pos);
      // the forced note keeps a score that wins at a bar boundary (enforceGaps); the others keep their own
      notes = best.got.map((g) => (g.lane === 2 && forced.has(g.pos) ? { ...g, score: g.score - 100 + 2 } : g));
    }
    return notes;
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
 * Score fill candidates (hats, the other stem's onsets): strength against the source's own median, saturating
 * there, and the place in the bar, so the fill prefers notes on the beat.
 * @param {{step: number, lane: number, strength: number, src: string}[]} cands
 * @param {number} barStep
 */
export function scoreFill(cands, barStep) {
  const mid = new Map();
  for (const src of new Set(cands.map((c) => c.src))) {
    const a = cands.filter((c) => c.src === src).map((c) => c.strength).sort((x, y) => x - y);
    mid.set(src, a[Math.floor(0.5 * (a.length - 1))] || 1);
  }
  return cands.map((c) => {
    const pos = (((c.step - barStep) % 16) + 16) % 16;
    return { ...c, pos, score: 0.25 + 0.3 * Math.min(1, c.strength / mid.get(c.src)) + 0.35 * METRIC(pos) };
  });
}

/**
 * The holes in a chart: stretches between two notes longer than MAX_GAP 16ths, the song's first onset counting as
 * a note LEAD 16ths before itself and its last one LEAD after itself (so the first and last notes come within a
 * bar of the music). `a`/`b` are the bounding steps; `real` says whether each is a note (which the fill must keep
 * its gap from) or the song's edge.
 * @param {{step: number}[]} notes sorted
 * @param {{first: number, last: number}} span the steps of the song's first and last onsets
 * @param {number} [maxGap]
 */
export function chartHoles(notes, span, maxGap = MAX_GAP) {
  const pts = [{ step: span.first - LEAD, real: false }, ...notes.map((n) => ({ step: n.step, real: true })), { step: span.last + LEAD, real: false }];
  const holes = [];
  for (let i = 1; i < pts.length; i++) {
    const a = pts[i - 1], b = pts[i];
    if (b.step - a.step > maxGap) holes.push({ a: a.step, b: b.step, aReal: a.real, bReal: b.real });
  }
  return holes;
}

/**
 * Fill a chart's holes, in stages: the first pool (the lanes' own unchosen onsets and the hats) wherever it can,
 * then the next (the other stem) in whatever is still a hole. In each hole the dynamic program picks the best notes
 * under the fill's grid and gap (strong onsets on the beat first). A hole with nothing in it stays.
 * @param {{step: number, lane: number, score: number}[]} notes the level's chart, sorted
 * @param {{step: number, lane: number, score: number, src: string}[][]} stages
 * @param {{grid: number, gap: number}} fill
 * @param {{first: number, last: number}} span
 * @returns {{notes: typeof notes, added: {step: number, lane: number, score: number, src: string}[]}}
 */
export function fillGaps(notes, stages, fill, span) {
  let out = [...notes];
  const added = [];
  const lv = { grid: fill.grid, same: fill.gap, change: fill.gap };
  for (const pool of stages) {
    for (const h of chartHoles(out, span)) {
      const lo = h.aReal ? h.a + fill.gap : Math.max(h.a + 1, span.first);
      const hi = h.bReal ? h.b - fill.gap : Math.min(h.b - 1, span.last);
      const cands = pool.filter((c) => c.step >= lo && c.step <= hi && c.step % fill.grid === 0).sort((x, y) => x.step - y.step);
      if (!cands.length) continue;
      const pick = selectNotes(cands, lv, 0);
      added.push(...pick);
      out = [...out, ...pick].sort((x, y) => x.step - y.step);
    }
  }
  return { notes: out, added: added.sort((x, y) => x.step - y.step) };
}

/**
 * All three charts. The main chart is made from the drums' first beat on (`drumStart`): lambda starts at the
 * level's value and rises until the density cap holds. Then the break fill (fillGaps) fills what is still a hole:
 * first from the lanes' unchosen onsets (those before the drums start included) and the hats, then from the other
 * stem.
 * @param {{step: number, lane: number, strength: number}[]} cands
 * @param {number[]} beats
 * @param {number} downbeat
 * @param {number} playSeconds the seconds the charts span (reported as notes per second of the song)
 * @param {{step: number, strength: number, src: 'hat' | 'other'}[]} [extra] fill-only onsets, quantized
 * @param {number} [drumStart] the step of the drums' first beat (beats before it carry a drumless intro)
 */
export function makeCharts(cands, beats, downbeat, playSeconds, extra = [], drumStart = 0) {
  const barStep = downbeat * 4;
  const scored = scoreCandidates(cands.filter((c) => c.step >= drumStart), barStep, drumStart);
  const pats = barPatterns(scored, barStep);
  const intro = cands.filter((c) => c.step < drumStart).map((c) => ({ ...c, src: LANES[c.lane] }));
  const fillScored = scoreFill([...intro, ...extra.map((c) => ({ ...c, lane: FILL_LANE[c.src] }))], barStep);
  const all = [...cands, ...extra];
  const span = all.length ? { first: Math.min(...all.map((c) => c.step)), last: Math.max(...all.map((c) => c.step)) } : { first: 0, last: 0 };
  /** @type {Record<string, {t: number, step: number, lane: number}[]>} */
  const charts = {};
  const stats = {};
  for (const [name, lv] of Object.entries(LEVELS)) {
    let lambda = lv.lambda, notes = chartLevel(scored, pats, barStep, lv, lambda);
    while (chartDensity(notes, beats, downbeat) > lv.density && lambda < 3) {
      lambda += 0.05;
      notes = chartLevel(scored, pats, barStep, lv, lambda);
    }
    const main = notes.length;
    const taken = new Set(notes.map((n) => n.step));
    const lanesPool = [...scored.filter((c) => !taken.has(c.step)).map((c) => ({ ...c, src: LANES[c.lane] })), ...fillScored.filter((c) => c.src !== 'other')];
    const filled = fillGaps(notes, [lanesPool, fillScored.filter((c) => c.src === 'other')], FILL[name], span);
    notes = filled.notes;
    // o: 1 marks a note the break fill made from the other stem (on the bass lane): the game plays its hit and miss on
    // the other track, the sound it follows
    charts[name] = notes.map((n) => ({ t: +stepTime(beats, n.step).toFixed(4), step: n.step, lane: n.lane, ...(n.src === 'other' ? { o: 1 } : {}) }));
    const bySrc = (src) => filled.added.filter((c) => c.src === src).map((c) => c.step);
    stats[name] = { lambda: +lambda.toFixed(2), notes: notes.length, main, fill: filled.added.length,
      per_second: +(notes.length / playSeconds).toFixed(2), per_second_playing: +chartDensity(notes, beats, downbeat).toFixed(2),
      holes_left: chartHoles(notes, span).map((h) => [h.a, h.b]),
      fill_steps: filled.added.map((c) => c.step), hat_steps: bySrc('hat'), other_steps: bySrc('other') };
  }
  return { charts, stats, patterns: { bars: pats.barCluster.size, distinct: pats.patterns.length }, span };
}

/**
 * The two-lane game: each player plays a pair of parts on two buttons (docs/DESIGN.md "Two lanes"). With two buttons
 * to follow instead of three, each level carries more notes than the three-lane game's: Easy runs on the eighths
 * with a quarter note a hand, Medium eighths on both hands, Hard sixteenths. `lanes` are the parts (PARTS indices).
 */
export const PAIRS = { drums: { lanes: [0, 1] }, groove: { lanes: [2, 3] } };
export const LEVELS2 = {
  easy: { grid: 2, same: 4, change: 2, lambda: 0.6, density: 2.4 },
  medium: { grid: 2, same: 2, change: 2, lambda: 0.4, density: 4.0 },
  hard: { grid: 1, same: 1, change: 1, lambda: 0.2, density: 7.0 },
};
/** the two-lane break fill: a step coarser than each level's drops, as the three-lane fill is */
export const FILL2 = {
  easy: { grid: 4, gap: 4 },
  medium: { grid: 2, gap: 4 },
  hard: { grid: 2, gap: 2 },
};

/**
 * One pair's three charts. `cands` are that pair's onsets with their part (PARTS index) as `lane`; the chart is
 * picked on local lanes 0 and 1 (the dynamic program's state) and written back as parts. The break fill takes the
 * pair's own unchosen onsets (and, for the drums, the hats on the clap); a stretch where neither part plays stays
 * empty (that player rests). Holes are reported against the pair's own music.
 * @param {{step: number, lane: number, strength: number}[]} cands
 * @param {number[]} beats
 * @param {number} downbeat
 * @param {number} playSeconds
 * @param {{lanes: number[]}} pair
 * @param {{step: number, strength: number, src: string}[]} [extra] hats (the drums' fill)
 * @param {number} [start] the step the main chart starts on (the drums' first beat for the drums)
 * @param {Record<string, {step: number, lane: number}[]>} [solo] the three-lane charts: each level keeps every note they
 *   give the pair's parts (where its gaps allow), so a pair never has less of its parts than the three-lane game
 */
export function makePairCharts(cands, beats, downbeat, playSeconds, pair, extra = [], start = 0, solo = {}) {
  const barStep = downbeat * 4;
  const local = (c) => ({ ...c, part: c.lane, lane: pair.lanes.indexOf(c.lane) });
  const own = cands.filter((c) => pair.lanes.includes(c.lane));
  const scored = scoreCandidates(own.filter((c) => c.step >= start), barStep, start).map(local);
  const pats = barPatterns(scored, barStep);
  const intro = own.filter((c) => c.step < start).map((c) => ({ ...local(c), src: PARTS[c.lane] }));
  const fillScored = scoreFill([...intro, ...extra.map((c) => ({ ...c, lane: 1, part: pair.lanes[1] }))], barStep);
  const all = [...own, ...extra];
  const span = all.length ? { first: Math.min(...all.map((c) => c.step)), last: Math.max(...all.map((c) => c.step)) } : { first: 0, last: 0 };
  const charts = {}, stats = {};
  for (const [name, lv] of Object.entries(LEVELS2)) {
    let lambda = lv.lambda, notes = chartLevel(scored, pats, barStep, lv, lambda);
    while (chartDensity(notes, beats, downbeat) > lv.density && lambda < 3) {
      lambda += 0.05;
      notes = chartLevel(scored, pats, barStep, lv, lambda);
    }
    const main = notes.length;
    const taken = new Set(notes.map((n) => n.step));
    const pool = [...scored.filter((c) => !taken.has(c.step)).map((c) => ({ ...c, src: PARTS[pair.lanes[c.lane]] })), ...fillScored];
    const filled = fillGaps(notes, [pool], FILL2[name], span);
    notes = filled.notes;
    // every note the three-lane chart gives these parts, where the level's gaps hold around it
    const have = new Set(notes.map((n) => n.step));
    let kept = 0;
    for (const s of (solo[name] ?? []).filter((n) => !n.o && pair.lanes.includes(n.lane) && !have.has(n.step))) {
      const lane = pair.lanes.indexOf(s.lane), i = notes.findIndex((n) => n.step > s.step);
      const p = i < 0 ? notes[notes.length - 1] : notes[i - 1], q = i < 0 ? null : notes[i];
      const fits = (a, b) => !a || !b || b.step - a.step >= (a.lane === b.lane ? lv.same : lv.change);
      const me = { step: s.step, lane, score: 0 };
      if (s.step % lv.grid === 0 && fits(p, me) && fits(me, q)) { notes.splice(i < 0 ? notes.length : i, 0, me); kept++; }
    }
    charts[name] = notes.map((n) => ({ t: +stepTime(beats, n.step).toFixed(4), step: n.step, lane: pair.lanes[n.lane] }));
    stats[name] = { lambda: +lambda.toFixed(2), notes: notes.length, main, fill: filled.added.length,
      per_second: +(notes.length / playSeconds).toFixed(2), per_second_playing: +chartDensity(notes, beats, downbeat).toFixed(2),
      parts: pair.lanes.map((p) => notes.filter((n) => pair.lanes[n.lane] === p).length), from_three_lanes: kept,
      holes_left: chartHoles(notes, span).map((h) => [h.a, h.b]) };
  }
  return { charts, stats, span };
}
