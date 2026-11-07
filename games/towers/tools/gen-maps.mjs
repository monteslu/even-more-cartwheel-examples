#!/usr/bin/env node
// towers' maps: writes assets/maps/<nnn>.cwtd (the format is in map.h) and count.txt, then the pack list.
//
//   node tools/gen-maps.mjs [count (default 30)] [--only n,m]
//
// Every map is 36 x 20 tiles of 2 m (the whole map on one 16:9 screen) and made from a seed. Its paths are smooth
// curves through waypoints (Catmull-Rom), rasterised to the tiles, mostly two tiles wide with chokes and wide
// stretches, in one of six families of layout taken in turn (FAMILIES: switchbacks, columns, a spiral, a loop past
// the core, an open field, a wandering way); later maps add gates whose paths merge into the first. A layout whose runs
// touch (a short cut) or whose gates are not two tiles on the edge is thrown away. Slots go on the ground beside the
// paths where they reach the most path, not touching one another, and each is dealt to a player for two, three and
// four players: bands across the map (four: quarters), balanced by reach. Smooth rock outcrops and pools with banks
// (feature records) take some of the open ground; the theme's decor stands on the rest (big props only where nothing
// walks or builds round them), lamps line the paths and light the bake. The later maps' difficulty comes from
// tools/map-diffs.json (tune-maps.mjs).
//
// The themes come in blocks of five: meadow, autumn woods, desert, snowfields, volcano, crystal vale. The waves, the
// difficulty and the creeps' kinds climb with the map's number. tools/check-maps.mjs fails a broken map.
import { existsSync, mkdirSync, readFileSync, writeFileSync, rmSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { fields, floorBlocked, CURB, PAD_R } from './fields.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '../assets/maps');
const argv = process.argv.slice(2);
const COUNT = +(argv.find((a) => /^\d+$/.test(a)) || 30);
const onlyArg = argv.indexOf('--only');
const ONLY = onlyArg >= 0 ? new Set(argv[onlyArg + 1].split(',').map(Number)) : null;
const W = 36, H = 20;
const DIFFS = existsSync(join(HERE, 'map-diffs.json')) ? JSON.parse(readFileSync(join(HERE, 'map-diffs.json'), 'utf8')) : {};
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

// ── the paths: smooth routes through waypoints, rasterised to the tiles ──
// A route is a list of waypoints [x, y, width] in tile units (x, y a point on the tiles' grid, width in tiles); it is
// drawn through them as a Catmull-Rom curve and every tile whose middle lies within half its width of the curve is
// path. A route starts off the map's edge (its gate) and ends at the core's block, or on another route (a merge).
function curve(wp, step = 0.2) {
  const out = [];
  const P = [wp[0], ...wp, wp[wp.length - 1]];
  for (let i = 1; i < P.length - 2; i++) {
    const [p0, p1, p2, p3] = [P[i - 1], P[i], P[i + 1], P[i + 2]];
    const len = Math.hypot(p2[0] - p1[0], p2[1] - p1[1]), n = Math.max(1, Math.ceil(len / step));
    for (let k = 0; k < n; k++) {
      const t = k / n, t2 = t * t, t3 = t2 * t;
      const c = (a, b, cc, d) => 0.5 * (2 * b + (-a + cc) * t + (2 * a - 5 * b + 4 * cc - d) * t2 + (-a + 3 * b - 3 * cc + d) * t3);
      out.push([c(p0[0], p1[0], p2[0], p3[0]), c(p0[1], p1[1], p2[1], p3[1]), p1[2] + (p2[2] - p1[2]) * t]);
    }
  }
  out.push(wp[wp.length - 1]);
  return out;
}
const arcLen = (pts) => pts.reduce((s, p, i) => (i ? s + Math.hypot(p[0] - pts[i - 1][0], p[1] - pts[i - 1][1]) : 0), 0);
function raster(g, pts, own, id) {
  for (const [px, py, w] of pts) {
    const r = w / 2 + 0.04;
    for (let y = Math.floor(py - r - 1); y <= Math.ceil(py + r); y++)
      for (let x = Math.floor(px - r - 1); x <= Math.ceil(px + r); x++) {
        if (x < 0 || y < 0 || x >= W || y >= H) continue;
        if (Math.hypot(x + 0.5 - px, y + 0.5 - py) <= r) { g[y][x] = '.'; if (own[y][x] < 0) own[y][x] = id; }
      }
  }
}
const jit = (R, a) => (R() - 0.5) * 2 * a;
// a width along the way: mostly two tiles, now and then a choke or a wide stretch
const width = (R, wide = 0) => { const q = R(); return q < 0.14 ? 1.45 : q < 0.24 + wide ? 3.3 + R() * 0.6 : 2 + R() * 0.5; };

// the families of layouts, each a function of the seed giving { routes, core, merges } (core: the block's top-left
// tile; every route's first waypoint off the map's edge with width 2 on a tile boundary so its gate is two tiles)
const FAMILIES = {
  // switchbacks across the map, three or four runs, the core at the end of the last
  switchback(R) {
    const rows = 3 + R.int(2), gap = (H - 6) / (rows - 1);
    const ys = Array.from({ length: rows }, (_, k) => Math.round(2.5 + k * gap));
    const wp = [[-2, ys[0], 2], [2, ys[0], 2]];
    for (let k = 0; k < rows; k++) {
      const y = ys[k], right = k % 2 === 0, last = k === rows - 1;
      const xe = right ? W - 4 - R.int(3) : 4 + R.int(3);
      const xs = right ? 4 : W - 5;
      for (let q = 1; q <= 3; q++) wp.push([xs + (xe - xs) * q / 4, y + jit(R, 0.8), width(R)]);
      if (last) { const cx = Math.round(W / 2 + (right ? 6 : -6) + jit(R, 2)); wp.push([cx + 1, y, 2]); return { routes: [wp], core: [cx, y - 1] }; }
      wp.push([xe, y, 2.2], [xe, (y + ys[k + 1]) / 2, width(R)]);
    }
  },
  // the same turned: runs up and down the map
  columns(R) {
    const cols = 5 + R.int(2), gap = (W - 7) / (cols - 1);
    const xs = Array.from({ length: cols }, (_, k) => Math.round(3.5 + k * gap));
    const wp = [[xs[0], -2, 2], [xs[0], 2, 2]];
    for (let k = 0; k < cols; k++) {
      const x = xs[k], down = k % 2 === 0, last = k === cols - 1;
      const ye = down ? H - 4 : 3, ys = down ? 3 : H - 4;
      wp.push([x + jit(R, 0.6), (ys + ye) / 2, width(R)]);
      if (last) { const cy = Math.round(H / 2 + jit(R, 2)); wp.push([x, cy + 1, 2]); return { routes: [wp], core: [x - 1, cy] }; }
      wp.push([x, ye, 2.2], [(x + xs[k + 1]) / 2, ye + (down ? 0.6 : -0.6), width(R)]);
    }
  },
  // a spiral in from the edge to the core in the middle
  spiral(R) {
    const a = 3, b = 8 + R.int(2);
    const wp = [[-2, a, 2], [2, a, 2], [W / 2, a + jit(R, 0.6), width(R)], [W - a - 1, a, 2.2], [W - a - 1, H / 2, width(R)], [W - a - 1, H - a - 1, 2.2],
      [W / 2, H - a - 1 + jit(R, 0.5), width(R, 0.2)], [b, H - a - 1, 2.2], [b, H / 2 - 1, width(R)], [b + 1, a + 5, 2.2], [W - b - 2, a + 5, width(R)]];
    const cx = Math.round(W - b - 4), cy = H - a - 6;
    wp.push([W - b - 1, cy + 1, 2.2], [cx + 3, cy + 1, 2], [cx + 1, cy + 1, 2]);
    return { routes: [wp], core: [cx, cy] };
  },
  // past the core and round it: the path runs by the core's north, loops the map and comes in from the south
  loop(R) {
    const cx = Math.round(W / 2 - 1 + jit(R, 3)), cy = Math.round(H / 2 - 1 + jit(R, 1));
    const top = 3 + R.int(2), bot = H - 4 - R.int(2), left = 4 + R.int(3), right = W - 5 - R.int(3);
    const wp = [[-2, top, 2], [2, top, 2], [cx, top + jit(R, 0.7), width(R)], [right, top, 2.2], [right + jit(R, 1), (top + bot) / 2, width(R, 0.2)], [right, bot, 2.2],
      [cx + 2, bot + jit(R, 0.6), width(R)], [left, bot, 2.2], [left + jit(R, 0.8), cy + 1, width(R)], [cx - 3, cy + 1, 2.2], [cx + 1, cy + 1, 2]];
    return { routes: [wp], core: [cx, cy] };
  },
  // an open field the creeps spread over, then a choke into the core
  field(R) {
    const y0 = 3 + R.int(4), cx = W - 6 - R.int(3), cy = Math.round(H / 2 - 1 + jit(R, 3));
    const fx = 10 + R.int(6), fy = Math.round(H / 2 + jit(R, 2));
    const wp = [[-2, y0, 2], [2, y0, 2], [6, y0 + jit(R, 1), width(R)], [fx - 3, fy, 3], [fx + 2, fy + jit(R, 1.5), 6.5 + R()], [fx + 7, fy, 5.5],
      [fx + 11, fy + jit(R, 2), 1.45], [cx - 3, cy + 1, 1.45], [cx + 1, cy + 1, 2]];
    return { routes: [wp], core: [cx, cy] };
  },
  // a long wandering way: waypoints across the map, turning back and forth
  wander(R) {
    const wp = [[-2, 4 + R.int(12), 2]];
    wp.push([2, wp[0][1], 2]);
    let x = 6, up = R() < 0.5;
    while (x < W - 8) {
      wp.push([x, up ? 3 + R() * 3 : H - 4 - R() * 3, width(R, 0.1)]);
      x += 4 + R() * 3, up = !up;
    }
    const cx = W - 6, cy = Math.round(H / 2 - 1 + jit(R, 3));
    wp.push([W - 4, cy + 1 + jit(R, 2), width(R)], [cx + 1, cy + 1, 2]);
    return { routes: [wp], core: [cx, cy] };
  },
};
// two gates that merge: each family's main route, and a second from another edge joining it part way
function mergeRoute(R, main, gates) {
  const pts = curve(main);
  const L = arcLen(pts);
  let acc = 0, target = null;
  const want = L * (0.25 + R() * 0.3);
  for (let i = 1; i < pts.length; i++) {
    acc += Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]);
    if (acc >= want) { target = pts[i]; break; }
  }
  // a start on an edge far from the gates there are
  const starts = [];
  for (let x = 4; x < W - 4; x += 2) starts.push([x, -2, 'n'], [x, H + 2, 's']);
  for (let y = 3; y < H - 3; y += 2) starts.push([-2, y, 'w'], [W + 2, y, 'e']);
  const far = starts.filter(([x, y]) => gates.every(([gx, gy]) => Math.hypot(gx - x, gy - y) > 14));
  if (!far.length || !target) return null;
  const [sx, sy, side] = R.pick(far);
  const ix = side === 'w' ? 2 : side === 'e' ? W - 2 : sx, iy = side === 'n' ? 2 : side === 's' ? H - 2 : sy;
  const mid = [(ix + target[0]) / 2 + jit(R, 3), (iy + target[1]) / 2 + jit(R, 2), width(R)];
  return [[sx, sy, 2], [ix, iy, 2], mid, [target[0], target[1], 2]];
}

// the gate a route opens: its two edge tiles (the first of them, as the format names it), or null
function gateOf(g, own, id, wp0) {
  const [x0, y0] = wp0;
  const cells = [];
  if (x0 < 0 || x0 >= W) { const x = x0 < 0 ? 0 : W - 1; for (let y = 0; y < H; y++) if (g[y][x] === '.' && own[y][x] === id) cells.push([x, y]); }
  else { const y = y0 < 0 ? 0 : H - 1; for (let x = 0; x < W; x++) if (g[y][x] === '.' && own[y][x] === id) cells.push([x, y]); }
  if (cells.length !== 2) return null;
  const side = x0 < 0 || x0 >= W;
  if (side ? cells[1][1] !== cells[0][1] + 1 : cells[1][0] !== cells[0][0] + 1) return null;
  return cells[0];
}

// the steps (4-way) from a tile to the core's block over path tiles; -1 when it cannot get there
function stepsTo(g, from, core) {
  const d = Array.from({ length: H }, () => Array(W).fill(-1));
  const q = [];
  for (let k = 0; k < 4; k++) { const x = core[0] + (k & 1), y = core[1] + (k >> 1); d[y][x] = 0; q.push([x, y]); }
  while (q.length) {
    const [x, y] = q.shift();
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const nx = x + dx, ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= W || ny >= H || g[ny][nx] !== '.' || d[ny][nx] >= 0) continue;
      d[ny][nx] = d[y][x] + 1, q.push([nx, ny]);
    }
  }
  return d[from[1]][from[0]];
}

// the order the families come in (map 1 the gentlest), and the gates by the map's number
const ORDER = ['switchback', 'loop', 'field', 'columns', 'spiral', 'wander'];

// ── one map ─────────────────────────────────────────────────────────
function makeMap(n) {
  const theme = THEMES[Math.floor((n - 1) / 5) % THEMES.length];
  const seed = 1000 + n * 7919;
  const R = rng(seed);
  const t = (n - 1) / Math.max(COUNT - 1, 1);
  const family = ORDER[(n - 1 + Math.floor((n - 1) / 6)) % ORDER.length];
  const nspawn = n <= 6 ? 1 : n <= 18 ? 2 : 2 + (n % 3 === 0 ? 1 : 0);
  for (let attempt = 0; attempt < 600; attempt++) {
    const g = Array.from({ length: H }, () => Array(W).fill('g'));
    const own = Array.from({ length: H }, () => Array(W).fill(-1));
    const lay = FAMILIES[family](R);
    if (!lay) continue;
    const routes = [lay.routes[0]];
    const gatePts = [[lay.routes[0][0][0], lay.routes[0][0][1]]];
    let ok = true;
    for (let s = 1; s < nspawn && ok; s++) {
      const r = mergeRoute(R, routes[0], gatePts);
      if (!r) ok = false;
      else routes.push(r), gatePts.push([r[0][0], r[0][1]]);
    }
    if (!ok) continue;
    routes.forEach((wp, id) => raster(g, curve(wp), own, id));
    const [cx, cy] = lay.core;
    if (cx < 2 || cy < 2 || cx > W - 4 || cy > H - 4) continue;
    for (let k = 0; k < 4; k++) g[cy + (k >> 1)][cx + (k & 1)] = '.';
    // the gates: two edge tiles each, and no other path on the edge
    const gates = routes.map((wp, id) => gateOf(g, own, id, wp[0]));
    if (gates.some((q) => !q)) continue;
    let edgeOk = true;
    for (let x = 0; x < W; x++) for (const y of [0, H - 1]) if (g[y][x] === '.' && !gates.some(([gx, gy]) => (gx === x || gx + 1 === x) && gy === y)) edgeOk = false;
    for (let y = 0; y < H; y++) for (const x of [0, W - 1]) if (g[y][x] === '.' && !gates.some(([gx, gy]) => gx === x && (gy === y || gy + 1 === y))) edgeOk = false;
    if (!edgeOk) continue;
    // every gate reaches the core, and the way is the route's own length (no run touching another: no short cuts)
    const mainLen = arcLen(curve(routes[0]));
    const steps = gates.map((q) => stepsTo(g, q, lay.core));
    if (steps.some((s) => s < 0)) continue;
    if (steps[0] < mainLen * 0.8) continue;
    if (steps.slice(1).some((s) => s < Math.max(34, steps[0] * 0.6))) continue; // a joining gate's way is long too
    // the core with room round it: no path passing within a tile of its block but its own way in
    const m = { n, theme, seed, family, g, gates, routes, core: lay.core, slots: [], props: [], lights: [], feats: [] };
    m.F = shape(m);
    if (!placeSlots(m, R, t)) continue;
    m.F = shape(m); // the pads pushed into the floor: the creeps still get through
    if (floorBlocked({ w: W, h: H, core: m.core, spawn: m.gates }, m.F).length) continue;
    placeFeatures(m, R);
    placeScenery(m, R);
    return m;
  }
  throw new Error(`map ${n} (${family}): no layout after 600 attempts`);
}

// the ground's shape (fields.mjs, map.c) as the map stands
const shape = (m) => fields({ w: W, h: H, tile: (x, y) => m.g[y]?.[x] ?? 'g', routes: m.routes, core: m.core, slots: m.slots, feats: m.feats });

// ── the terrain's features: smooth rock outcrops and pools with banks, on open ground away from paths and pads ──
const FEAT = {
  meadow: { water: 2, rock: 2 }, autumn: { water: 2, rock: 3 }, desert: { water: 2, rock: 3 }, snow: { water: 2, rock: 3 },
  volcano: { water: 2, rock: 3 }, crystal: { water: 2, rock: 2 },
};
function placeFeatures(m, R) {
  const g = m.g;
  const clear = (x, y, r) => {
    for (let ty = Math.floor(y - r - 2); ty <= Math.ceil(y + r + 2); ty++)
      for (let tx = Math.floor(x - r - 2); tx <= Math.ceil(x + r + 2); tx++) {
        if (tx < 0 || ty < 0 || tx >= W || ty >= H) continue;
        if ((g[ty][tx] === '.' || g[ty][tx] === 'o') && Math.hypot(tx + 0.5 - x, ty + 0.5 - y) < r + 1.6) return false;
      }
    if (Math.hypot(m.core[0] + 1 - x, m.core[1] + 1 - y) < r + 3) return false;
    return m.feats.every((f) => Math.hypot(f.x - x, f.y - y) > f.r + r + 0.5 || (f.kind === 'rock' && f.kind === kindNow));
  };
  let kindNow = '';
  const want = FEAT[m.theme];
  for (const [kind, count] of [['water', want.water], ['rock', want.rock]]) {
    kindNow = kind;
    for (let k = 0, tries = 0; k < count && tries < 300; tries++) {
      const r = kind === 'water' ? (m.theme === 'desert' || m.theme === 'autumn' ? 2.6 : 1.8) + R() * 1.8 : 1.4 + R() * 1.6; // the warm themes' pools wider: their cool accent
      const x = 1 + R() * (W - 2), y = 1 + R() * (H - 2);
      if (!clear(x, y, r)) continue;
      m.feats.push({ kind, x, y, r, h: kind === 'rock' ? 1.1 + R() * 1.1 : 0 });
      if (kind === 'rock') { // an outcrop: two or three domes run together into a ridge
        const n2 = 1 + R.int(2);
        for (let j = 0; j < n2; j++) {
          const a = R() * 6.283, d = r * (0.6 + R() * 0.5), r2 = r * (0.55 + R() * 0.3), x2 = x + Math.cos(a) * d, y2 = y + Math.sin(a) * d;
          if (clear(x2, y2, r2)) m.feats.push({ kind, x: x2, y: y2, r: r2, h: 0.8 + R() * 1.4 });
        }
      }
      k++;
    }
  }
  // a pool's glow in the bake where the water glows (lava, crystal light)
  for (const f of m.feats)
    if (f.kind === 'water' && (m.theme === 'volcano' || m.theme === 'crystal'))
      m.lights.push({ x: Math.min(W - 1, Math.max(0, Math.floor(f.x))), y: Math.min(H - 1, Math.max(0, Math.floor(f.y))), h: 0.8, c: m.theme === 'volcano' ? [1, 0.45, 0.12] : [0.4, 0.9, 1], e: 35, r: f.r * 2 + 4 });
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
      if (m.F.floorD(x + 0.5, y + 0.5).d < PAD_R + CURB - 0.35) continue; // the pad would push too far into the floor
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
  // a tile whose corner mapbuild.c cuts (path on two sides and across their corner): no lamp on its slope
  const notched = (x, y) => [[1, -1], [1, 1], [-1, 1], [-1, -1]].some(([dx, dy]) => g[y]?.[x + dx] === '.' && g[y + dy]?.[x] === '.' && g[y + dy]?.[x + dx] === '.');
  const feat = (x, y) => m.feats.some((q) => Math.hypot(x + 0.5 - q.x, y + 0.5 - q.y) < q.r + 1.0);
  const blocked = (x, y) => near(g, x, y, '.', 1) || near(g, x, y, 'o', 1) || feat(x, y);
  // lamps along the paths: a ground tile beside the path every few tiles of it
  const lamp = LAMP[m.theme];
  let since = 3;
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      if (g[y][x] !== 'g' || !near(g, x, y, '.', 1) || near(g, x, y, 'o', 1) || feat(x, y) || notched(x, y)) continue;
      if (++since < 5 || R() < 0.4 || m.props.some((p) => p.kind === 'lamp' && Math.abs(p.x - x) + Math.abs(p.y - y) < 5)) continue;
      // turned so its first side toward the path is the one it leans out over
      const turn = [[0, -1], [1, 0], [0, 1], [-1, 0]].findIndex(([dx, dy]) => g[y + dy]?.[x + dx] === '.');
      if (turn < 0) continue;
      const [ldx, ldy] = [[0, -1], [1, 0], [0, 1], [-1, 0]][turn];
      if (m.F.floorD(x + 0.5 + ldx * 0.125, y + 0.5 + ldy * 0.125).d < CURB + 0.3) continue; // on level ground by the curb
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
      if (open && R() < (edge ? 0.5 : 0.26)) {
        const frame = R.int(4);
        m.props.push({ x, y, kind: 'decor', frame, turn: R.int(4) });
        if (GLOW[m.theme] && R() < 0.25 && m.lights.length < 90) m.lights.push({ x, y, h: 1.2, c: GLOW[m.theme], e: 30, r: 5 });
      } else if (!feat(x, y) && !near(g, x, y, '.', 1) && !near(g, x, y, '#', 1) && !near(g, x, y, '~', 1) && !near(g, x, y, 'o', 0) && R() < 0.16)
        m.props.push({ x, y, kind: 'decor', frame: 4 + R.int(4), turn: R.int(4) });
    }
}

function write(m) {
  const n = m.n, t = (n - 1) / Math.max(COUNT - 1, 1);
  const R = rng(m.seed + 3);
  const [a, b] = NAMES[m.theme];
  const name = `${R.pick(a)} ${R.pick(b)}`;
  const waves = Math.round(10 + 15 * t);
  const diff = (DIFFS[n] ?? 0.85 + 1.3 * Math.pow(t, 1.2)).toFixed(3); // tools/map-diffs.json: the later maps tuned by tune-maps.mjs
  const kinds = KINDS[Math.min(KINDS.length - 1, Math.floor(t * KINDS.length))];
  const f = (v) => +v.toFixed(3);
  const L = ['towers-map 1', `# written by tools/gen-maps.mjs (seed ${m.seed}, a ${m.family} layout)`, `map ${n}`, `name ${name}`, `theme ${m.theme}`, `size ${W} ${H}`, `seed ${m.seed}`,
    `waves ${waves}`, `diff ${diff}`, `kinds ${kinds.join(' ')}`, 'grid', ...m.g.map((r) => r.join('')), 'end'];
  for (const [x, y] of m.gates) L.push(`spawn ${x} ${y}`);
  L.push(`core ${m.core[0]} ${m.core[1]}`);
  for (const s of m.slots) L.push(`slot ${s.x} ${s.y} ${s.o2} ${s.o3} ${s.o4}`);
  for (const r of m.routes) L.push(`route ${r.map((p) => p.map((v) => +v.toFixed(3)).join(' ')).join(' ')}`);
  for (const q of m.feats) L.push(`feature ${q.kind} ${q.x.toFixed(2)} ${q.y.toFixed(2)} ${q.r.toFixed(2)} ${q.h.toFixed(2)}`);
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
