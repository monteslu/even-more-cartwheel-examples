#!/usr/bin/env node
// towers' maps: writes assets/maps/<nnn>.cwtd (the format is in map.h) and count.txt, then the pack list.
//
//   node tools/gen-maps.mjs [count (default 30)] [--only n,m]
//
// Every map is 36 x 20 tiles of 2 m (the whole map on one 16:9 screen) and made from a seed. Its paths run on a
// lattice of nodes four tiles apart, two tiles wide with two tiles of ground between any two runs, so build slots sit
// between the runs and reach both: the main path is a long self-avoiding walk from a gate on the map's edge to the
// core (a 2 x 2 block inside the map), later maps add gates whose paths join it. Slots go on the ground beside the
// paths where they reach the most path, not touching one another, and each is dealt to a player for two, three and
// four players: bands across the map (four: quarters), balanced by reach. The theme's decor stands on the open
// ground (big props only where nothing walks or builds round them),
// lamps line the paths and light the bake.
//
// The themes come in blocks of five: meadow, autumn woods, desert, snowfields, volcano, crystal vale. The waves, the
// difficulty and the creeps' kinds climb with the map's number. tools/check-maps.mjs fails a broken map.
import { mkdirSync, writeFileSync, rmSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '../assets/maps');
const argv = process.argv.slice(2);
const COUNT = +(argv.find((a) => /^\d+$/.test(a)) || 30);
const onlyArg = argv.indexOf('--only');
const ONLY = onlyArg >= 0 ? new Set(argv[onlyArg + 1].split(',').map(Number)) : null;
const W = 36, H = 20;
const NX = 9, NY = 5; // the lattice: node (i, j) is the 2 x 2 block at tile (1 + 4i, 1 + 4j)
const THEMES = ['meadow', 'autumn', 'desert', 'snow', 'volcano', 'crystal'];

// ── seeded random ───────────────────────────────────────────────────
function rng(seed) {
  let s = seed >>> 0 || 1;
  const f = () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; };
  f.int = (n) => Math.floor(f() * n);
  f.pick = (a) => a[Math.floor(f() * a.length)];
  return f;
}

// ── the theme's names, creeps and light ─────────────────────────────
const NAMES = {
  meadow: [['Clover', 'Bramble', 'Willow', 'Thistle', 'Honey', 'Dandelion', 'Larkspur', 'Mill'], ['Bend', 'Hollow', 'Crossing', 'Green', 'Fields', 'Run', 'Downs', 'Brook']],
  autumn: [['Amber', 'Russet', 'Acorn', 'Ember', 'Maple', 'Hazel', 'Copper', 'Harvest'], ['Wood', 'Glade', 'Vale', 'Thicket', 'Ridge', 'Grove', 'Dell', 'Way']],
  desert: [['Saffron', 'Dune', 'Mirage', 'Sunscorch', 'Cinder', 'Ochre', 'Mesa', 'Oasis'], ['Flats', 'Pass', 'Basin', 'Wells', 'Steps', 'Reach', 'Canyon', 'Track']],
  snow: [['Frost', 'Hoarfrost', 'Glacier', 'Pine', 'Winter', 'Sleet', 'Icicle', 'Snowcap'], ['Fields', 'Drift', 'Pass', 'Hollow', 'Shelf', 'Lake', 'Ridge', 'Sound']],
  volcano: [['Ashen', 'Magma', 'Sulfur', 'Obsidian', 'Scorch', 'Cinder', 'Basalt', 'Furnace'], ['Vents', 'Caldera', 'Flow', 'Rim', 'Throat', 'Fields', 'Slopes', 'Gate']],
  crystal: [['Prism', 'Glimmer', 'Opal', 'Moonlit', 'Starfall', 'Lumen', 'Shimmer', 'Quartz'], ['Vale', 'Garden', 'Spires', 'Hollow', 'Grotto', 'Meadow', 'Terrace', 'Glen']],
};
// the creeps in the order a map's waves bring them, by how far into the game it is
const KINDS = [
  ['skitter', 'ghoul', 'hound'],
  ['ghoul', 'skitter', 'hound', 'imp'],
  ['skitter', 'ghoul', 'imp', 'hound', 'sporeling'],
  ['ghoul', 'skitter', 'hound', 'sporeling', 'bloater', 'imp'],
  ['skitter', 'ghoul', 'shade', 'imp', 'bloater', 'ogre'],
  ['skitter', 'hound', 'ghoul', 'shade', 'sporeling', 'bloater', 'imp', 'ogre'],
];
const LAMP = { meadow: [1, 0.78, 0.45], autumn: [1, 0.65, 0.35], desert: [1, 0.72, 0.4], snow: [1, 0.8, 0.55], volcano: [1, 0.5, 0.2], crystal: [0.5, 0.95, 1] };
const GLOW = { volcano: [1, 0.45, 0.12], crystal: [0.45, 0.9, 1] }; // the glowing decor's bake light

// ── the paths ───────────────────────────────────────────────────────
const nodeTile = (i, j) => [1 + 4 * i, 1 + 4 * j];
const onEdge = (i, j) => i === 0 || j === 0 || i === NX - 1 || j === NY - 1;
const DIRS = [[1, 0], [-1, 0], [0, 1], [0, -1]];

// a self-avoiding walk on the lattice from `from`, of length lo..hi nodes, avoiding `used`, ending where `goal` says
function walk(R, from, lo, hi, used, goal, tries = 4000) {
  let best = null;
  for (let t = 0; t < tries; t++) {
    const path = [from], seen = new Set([...used, `${from[0]},${from[1]}`]);
    let straight = 0, dir = null;
    while (path.length < hi) {
      const [i, j] = path[path.length - 1];
      if (path.length >= lo && goal(i, j, path)) break;
      let opts = DIRS.filter(([dx, dy]) => {
        const ni = i + dx, nj = j + dy;
        return ni >= 0 && nj >= 0 && ni < NX && nj < NY && !seen.has(`${ni},${nj}`);
      });
      if (!opts.length) break;
      // keep runs of 1 to 3 nodes, then turn: a winding path
      if (dir && straight < 1 + R.int(3) && opts.some((d) => d[0] === dir[0] && d[1] === dir[1])) opts = [dir];
      const d = R.pick(opts);
      straight = dir && d[0] === dir[0] && d[1] === dir[1] ? straight + 1 : 0;
      dir = d;
      path.push([i + d[0], j + d[1]]);
      seen.add(`${i + d[0]},${j + d[1]}`);
    }
    const [i, j] = path[path.length - 1];
    if (path.length >= lo && goal(i, j, path)) {
      if (!best || Math.abs(path.length - (lo + hi) / 2) < Math.abs(best.length - (lo + hi) / 2)) best = path;
      if (t > 200) break;
    }
  }
  return best;
}

function carve(g, path) {
  for (let k = 0; k < path.length; k++) {
    const [x, y] = nodeTile(...path[k]);
    for (let a = 0; a < 2; a++) for (let b = 0; b < 2; b++) g[y + b][x + a] = '.';
    if (k) { // the two tiles between this node and the last
      const [px, py] = nodeTile(...path[k - 1]);
      const mx = (x + px) / 2, my = (y + py) / 2;
      for (let a = 0; a < 2; a++) for (let b = 0; b < 2; b++) g[my + b][mx + a] = '.';
    }
  }
}
// the gate: the path from an edge node out to the map's edge; returns the gate record (its top-left edge tile)
function gate(g, [i, j], R) {
  const [x, y] = nodeTile(i, j);
  const sides = [];
  if (i === 0) sides.push('w');
  if (i === NX - 1) sides.push('e');
  if (j === 0) sides.push('n');
  if (j === NY - 1) sides.push('s');
  const s = R.pick(sides);
  if (s === 'w') { g[y][0] = g[y + 1][0] = '.'; return [0, y]; }
  if (s === 'e') { for (let xx = x + 2; xx < W; xx++) g[y][xx] = g[y + 1][xx] = '.'; return [W - 1, y]; }
  if (s === 'n') { g[0][x] = g[0][x + 1] = '.'; return [x, 0]; }
  for (let yy = y + 2; yy < H; yy++) g[yy][x] = g[yy][x + 1] = '.';
  return [x, H - 1];
}

// ── one map ─────────────────────────────────────────────────────────
function makeMap(n) {
  const theme = THEMES[Math.floor((n - 1) / 5) % THEMES.length];
  const seed = 1000 + n * 7919;
  const R = rng(seed);
  const t = (n - 1) / Math.max(COUNT - 1, 1); // 0 .. 1 through the game
  const nspawn = n <= 7 ? 1 : n <= 19 ? 2 : 2 + (n % 3 === 0 ? 1 : 0);
  for (let attempt = 0; attempt < 400; attempt++) {
    const g = Array.from({ length: H }, () => Array(W).fill('g'));
    // the core: an inner node, away from the middle a little
    const core = [1 + R.int(NX - 2), 1 + R.int(NY - 2)];
    // the main path, walked from the core out to an edge (then turned round), long
    const lo = n <= 2 ? 22 : 15 + R.int(6), hi = lo + 6;
    const main = walk(R, core, lo, hi, [], (i, j) => onEdge(i, j));
    if (!main) continue;
    carve(g, main);
    const used = new Set(main.map(([i, j]) => `${i},${j}`));
    const gates = [gate(g, main[main.length - 1], R)];
    let ok = true;
    for (let s = 1; s < nspawn && ok; s++) {
      // another gate: an edge node not on a path, walked to join the paths (not next to the core)
      const starts = [];
      for (let i = 0; i < NX; i++) for (let j = 0; j < NY; j++) if (onEdge(i, j) && !used.has(`${i},${j}`)) starts.push([i, j]);
      let joined = null;
      for (let k = 0; k < 40 && !joined; k++) {
        const st = R.pick(starts);
        const far = gates.every(([gx, gy]) => Math.abs(nodeTile(...st)[0] - gx) + Math.abs(nodeTile(...st)[1] - gy) > 14);
        if (!far) continue;
        const p = walk(R, st, 4, 12, used, (i, j, path) => {
          const nb = DIRS.map(([dx, dy]) => `${i + dx},${j + dy}`).filter((q) => used.has(q));
          if (!nb.length) return false;
          const [ci, cj] = core;
          return nb.every((q) => q !== `${ci},${cj}`) && path.length >= 4;
        }, 600);
        if (p) joined = p;
      }
      if (!joined) { ok = false; break; }
      // join: carve its nodes and the link to the path node beside its end
      const [ei, ej] = joined[joined.length - 1];
      const link = DIRS.map(([dx, dy]) => [ei + dx, ej + dy]).find(([a, b]) => used.has(`${a},${b}`) && !(a === core[0] && b === core[1]));
      carve(g, [...joined, link]);
      for (const [i, j] of joined) used.add(`${i},${j}`);
      gates.push(gate(g, joined[0], R));
    }
    if (!ok) continue;
    // a gate's 2-tile mouth must not run along the edge into another path
    const coreTile = nodeTile(...core);
    const m = { n, theme, seed, g, gates, core: coreTile, slots: [], props: [], lights: [] };
    if (!placeSlots(m, R, t)) continue;
    placeScenery(m, R);
    return m;
  }
  throw new Error(`map ${n}: no layout after 400 attempts`);
}

// how many path cells (1 m) lie within r metres of a tile's middle
function reach(g, x, y, r = 7) {
  let c = 0;
  for (let ty = Math.max(0, y - 4); ty <= Math.min(H - 1, y + 4); ty++)
    for (let tx = Math.max(0, x - 4); tx <= Math.min(W - 1, x + 4); tx++) {
      if (g[ty][tx] !== '.') continue;
      for (let a = 0; a < 2; a++) for (let b = 0; b < 2; b++) {
        const dx = (tx * 2 + a + 0.5) - (x * 2 + 1), dy = (ty * 2 + b + 0.5) - (y * 2 + 1);
        if (dx * dx + dy * dy < r * r) c++;
      }
    }
  return c;
}
const near = (g, x, y, ch, d = 1) => { for (let b = -d; b <= d; b++) for (let a = -d; a <= d; a++) { const yy = y + b, xx = x + a; if (yy >= 0 && xx >= 0 && yy < H && xx < W && g[yy][xx] === ch) return true; } return false; };

function placeSlots(m, R, t) {
  const g = m.g, [cx, cy] = m.core;
  const cand = [];
  for (let y = 1; y < H - 1; y++)
    for (let x = 1; x < W - 1; x++) {
      if (g[y][x] !== 'g' || !near(g, x, y, '.', 1)) continue;
      if (Math.abs(x - cx - 0.5) < 2.6 && Math.abs(y - cy - 0.5) < 2.6) continue; // room round the core
      if (m.gates.some(([gx, gy]) => Math.abs(gx - x) + Math.abs(gy - y) < 4)) continue; // and round the gates
      cand.push({ x, y, r: reach(g, x, y) + R() * 3 });
    }
  cand.sort((a, b) => b.r - a.r);
  const want = Math.round(34 - 8 * t); // fewer pads on the later maps: every one counts
  for (const c of cand) {
    if (m.slots.length >= want) break;
    if (m.slots.some((s) => Math.max(Math.abs(s.x - c.x), Math.abs(s.y - c.y)) < 2)) continue; // none touching
    m.slots.push(c);
  }
  if (m.slots.length < 18) return false;
  for (const s of m.slots) g[s.y][s.x] = 'o';
  // the shares: two and three players in bands across the map, four in quarters, each balanced by reach
  const by = (arr, k) => [...arr].sort((a, b) => a[k] - b[k] || a.x - b.x || a.y - b.y);
  const bands = (arr, nb, key) => {
    const sorted = by(arr, key), total = sorted.reduce((s, q) => s + q.r, 0);
    let acc = 0;
    for (const q of sorted) { q.band = Math.min(nb - 1, Math.floor((acc + q.r * 0.5) / total * nb)); acc += q.r; }
    return sorted;
  };
  bands(m.slots, 2, 'x').forEach((q) => (q.o2 = q.band));
  bands(m.slots, 3, 'x').forEach((q) => (q.o3 = q.band));
  for (const half of [0, 1]) {
    const part = m.slots.filter((q) => q.o2 === half);
    bands(part, 2, 'y').forEach((q) => (q.o4 = half + 2 * q.band));
  }
  // each share needs pads and a fair part of the reach
  for (const [k, np] of [['o2', 2], ['o3', 3], ['o4', 4]]) {
    const sums = Array(np).fill(0), cnt = Array(np).fill(0);
    for (const q of m.slots) sums[q[k]] += q.r, cnt[q[k]]++;
    const mean = sums.reduce((a, b) => a + b) / np;
    if (cnt.some((c) => c < 4) || sums.some((s) => Math.abs(s - mean) > mean * 0.25)) return false;
  }
  return true;
}

function placeScenery(m, R) {
  const g = m.g;
  const blocked = (x, y) => near(g, x, y, '.', 1) || near(g, x, y, 'o', 1);
  // lamps along the paths: a ground tile beside the path every few tiles of it
  const lamp = LAMP[m.theme];
  let since = 3;
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      if (g[y][x] !== 'g' || !near(g, x, y, '.', 1) || near(g, x, y, 'o', 1)) continue;
      if (++since < 5 || R() < 0.4 || m.props.some((p) => p.kind === 'lamp' && Math.abs(p.x - x) + Math.abs(p.y - y) < 5)) continue;
      // turned so its first side toward the path is the one it leans out over
      const turn = [[0, -1], [1, 0], [0, 1], [-1, 0]].findIndex(([dx, dy]) => g[y + dy]?.[x + dx] === '.');
      if (turn < 0) continue;
      m.props.push({ x, y, kind: 'lamp', frame: 0, turn });
      m.lights.push({ x, y, h: 1.5, c: lamp, e: 45, r: 7 });
      since = 0;
    }
  // the decor: big props where nothing walks or builds round them, small ones on the other open ground
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      if (g[y][x] !== 'g' || m.props.some((p) => p.x === x && p.y === y)) continue;
      if (Math.abs(x - m.core[0] - 0.5) < 2.6 && Math.abs(y - m.core[1] - 0.5) < 2.6) continue;
      const open = !blocked(x, y) && !near(g, x, y, '~', 1) && !near(g, x, y, '#', 1);
      const edge = x === 0 || y === 0 || x === W - 1 || y === H - 1;
      if (open && R() < (edge ? 0.6 : 0.42)) {
        const frame = R.int(4);
        m.props.push({ x, y, kind: 'decor', frame, turn: R.int(4) });
        if (GLOW[m.theme] && R() < 0.25 && m.lights.length < 90) m.lights.push({ x, y, h: 1.2, c: GLOW[m.theme], e: 30, r: 5 });
      } else if (!near(g, x, y, '.', 1) && !near(g, x, y, '#', 1) && !near(g, x, y, '~', 1) && !near(g, x, y, 'o', 0) && R() < 0.3)
        m.props.push({ x, y, kind: 'decor', frame: 4 + R.int(4), turn: R.int(4) });
    }
}

function write(m) {
  const n = m.n, t = (n - 1) / Math.max(COUNT - 1, 1);
  const R = rng(m.seed + 3);
  const [a, b] = NAMES[m.theme];
  const name = `${R.pick(a)} ${R.pick(b)}`;
  const waves = Math.round(10 + 15 * t);
  const diff = (0.75 + 0.75 * t).toFixed(2);
  const kinds = KINDS[Math.min(KINDS.length - 1, Math.floor(t * KINDS.length))];
  const f = (v) => +v.toFixed(3);
  const L = ['towers-map 1', `# written by tools/gen-maps.mjs (seed ${m.seed})`, `map ${n}`, `name ${name}`, `theme ${m.theme}`, `size ${W} ${H}`, `seed ${m.seed}`,
    `waves ${waves}`, `diff ${diff}`, `kinds ${kinds.join(' ')}`, 'grid', ...m.g.map((r) => r.join('')), 'end'];
  for (const [x, y] of m.gates) L.push(`spawn ${x} ${y}`);
  L.push(`core ${m.core[0]} ${m.core[1]}`);
  for (const s of m.slots) L.push(`slot ${s.x} ${s.y} ${s.o2} ${s.o3} ${s.o4}`);
  for (const p of m.props) L.push(`prop ${p.x} ${p.y} ${p.kind} ${p.frame} ${p.turn}`);
  for (const l of m.lights) L.push(`light ${l.x} ${l.y} ${f(l.h)} ${f(l.c[0])} ${f(l.c[1])} ${f(l.c[2])} ${f(l.e)} ${f(l.r)}`);
  writeFileSync(join(OUT, `${String(n).padStart(3, '0')}.cwtd`), L.join('\n') + '\n');
}

mkdirSync(OUT, { recursive: true });
if (!ONLY) for (const f of readdirSync(OUT)) if (/^\d{3}\.cwtd$/.test(f) && +f.slice(0, 3) > COUNT) rmSync(join(OUT, f));
for (let n = 1; n <= COUNT; n++) {
  if (ONLY && !ONLY.has(n)) continue;
  write(makeMap(n));
}
writeFileSync(join(OUT, 'count.txt'), `${COUNT}\n`);
console.log(`gen-maps: ${ONLY ? ONLY.size : COUNT} maps into ${OUT}`);
spawnSync('node', [join(HERE, 'gen-roots.mjs')], { stdio: 'inherit' });
