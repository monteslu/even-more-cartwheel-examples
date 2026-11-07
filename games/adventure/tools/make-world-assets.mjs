// Generates the world's textures and props into assets/: the terrain's eight layers (Terrain3D packing: albedo with
// height in alpha, normal with roughness in alpha), the building materials the world code builds walls, stairs and
// gates from, and the props as textured glb (the held weapons, keys, the heart vessel, chests, shrines, trees, rocks,
// braziers, crystals, the tonic flask). Every pixel and vertex is made here by our own code. Run:
//   node games/adventure/tools/make-world-assets.mjs [textures] [props]
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { mkdirSync } from 'node:fs';
import { Glb } from './chars/gltf-writer.mjs';
import { Mesher } from './chars/mesher.mjs';
import { clamp, lerp, smooth, hash2, fbm, cells, save, writeSet } from './chars/texlib.mjs';
import { bake, brick, slabs, planks, plate, metalTex } from './chars/make-textures.mjs';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets');
const TEX = join(ROOT, 'textures'), MOD = join(ROOT, 'models');
mkdirSync(join(TEX, 'terrain'), { recursive: true });
mkdirSync(MOD, { recursive: true });
const only = process.argv.slice(2);
const want = (k) => !only.length || only.includes(k);

// ── terrain layers ─────────────────────────────────────────────────────
// each look: px(u, v) -> { c: [r, g, b] sRGB, h 0..1, ro roughness }
const mix3 = (a, b, t) => [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];
const mul3 = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
const LAYERS = {
  // 0 lush grass: two greens in clumps, blade streaks, a yellow-green sheen where it is high
  grass: (u, v) => {
    const g = fbm(u, v, 6, 5, 11), clump = fbm(u, v, 14, 3, 12), blade = fbm(u * 4, v * 40, 32, 3, 13);
    let c = mix3([0.16, 0.36, 0.08], [0.36, 0.56, 0.12], clamp(g * 1.2 - 0.1));
    c = mix3(c, [0.52, 0.62, 0.16], smooth(0.6, 0.8, clump) * 0.5);
    c = mul3(c, 0.8 + 0.4 * blade);
    return { c, h: 0.3 + 0.5 * blade * clump, ro: 0.92 };
  },
  // 1 the meadow: grass full of yellow and violet flowers and white clover
  meadow: (u, v) => {
    const base = LAYERS.grass(u * 1.3 + 0.21, v * 1.3 + 0.37);
    const [f1, , id] = cells(u, v, 40, 21, 1);
    const fl = smooth(0.22, 0.12, f1) * smooth(0.45, 0.7, fbm(u, v, 5, 3, 22));
    const col = id < 0.45 ? [0.98, 0.82, 0.12] : id < 0.75 ? [0.62, 0.35, 0.9] : [0.95, 0.95, 0.88];
    return { c: mix3(base.c, col, fl), h: clamp(base.h + fl * 0.3), ro: 0.9 };
  },
  // 2 the worn path: warm packed earth with pebbles
  dirt: (u, v) => {
    const g = fbm(u, v, 8, 5, 31), [f1, , id] = cells(u, v, 24, 32, 0.9);
    const peb = smooth(0.3, 0.18, f1) * smooth(0.5, 0.75, hash2(Math.floor(id * 1000), 1, 33));
    let c = mix3([0.42, 0.29, 0.17], [0.6, 0.45, 0.28], g);
    c = mix3(c, mix3([0.55, 0.52, 0.48], [0.75, 0.7, 0.62], id), peb);
    return { c, h: 0.3 + 0.3 * g + peb * 0.5, ro: 0.95 - peb * 0.2 };
  },
  // 3 sandstone: the canyon floor and its ramps, layered in bands of amber and rose
  sand: (u, v) => {
    const g = fbm(u, v, 6, 5, 41), band = Math.sin((v + fbm(u, v, 3, 3, 42) * 0.25) * Math.PI * 10) * 0.5 + 0.5;
    let c = mix3([0.78, 0.46, 0.22], [0.92, 0.66, 0.38], band * 0.6 + g * 0.4);
    c = mix3(c, [0.82, 0.4, 0.32], smooth(0.62, 0.8, fbm(u, v, 4, 3, 43)) * 0.5);
    return { c, h: 0.4 + 0.3 * g + 0.2 * band, ro: 0.88 };
  },
  // 4 cliff rock: cool violet-grey slabs with dark cracks and lichen
  rock: (u, v) => {
    const g = fbm(u, v, 5, 6, 51), [f1, f2] = cells(u, v, 6, 52, 1), crack = 1 - smooth(0.0, 0.08, f2 - f1);
    let c = mix3([0.3, 0.27, 0.36], [0.5, 0.45, 0.52], g);
    c = mix3(c, [0.55, 0.62, 0.3], smooth(0.65, 0.82, fbm(u, v, 9, 3, 53)) * 0.6);
    c = mul3(c, 1 - crack * 0.6);
    return { c, h: clamp(0.5 + 0.4 * g - crack * 0.5), ro: 0.85 };
  },
  // 5 marsh: dark teal moss and wet mud, glossy where it is wet
  mud: (u, v) => {
    const g = fbm(u, v, 6, 5, 61), wet = smooth(0.45, 0.65, fbm(u, v, 4, 4, 62)), moss = fbm(u * 2, v * 2, 12, 3, 63);
    let c = mix3([0.12, 0.3, 0.24], [0.26, 0.46, 0.3], moss);
    c = mix3(c, [0.2, 0.16, 0.14], wet * 0.7);
    return { c: mul3(c, 0.85 + 0.3 * g), h: 0.4 + 0.3 * moss - wet * 0.3, ro: lerp(0.9, 0.35, wet) };
  },
  // 6 cobbles: the keep's paving, rounded red-brown stones in dark joints
  cobble: (u, v) => {
    const [f1, f2, id] = cells(u, v, 12, 71, 0.85), g = fbm(u, v, 8, 4, 72);
    const stone = smooth(0.0, 0.12, f2 - f1), dome = clamp(1 - f1 * 1.6);
    let c = mix3([0.5, 0.26, 0.2], [0.72, 0.44, 0.34], id * 0.7 + g * 0.3);
    c = mul3(c, lerp(0.35, 1, stone) * (0.85 + 0.2 * dome));
    return { c, h: clamp(stone * (0.5 + 0.5 * dome)), ro: lerp(0.98, 0.8, stone) };
  },
  // 7 ember ash: the keep's hill, dark warm earth with ochre and soot
  ash: (u, v) => {
    const g = fbm(u, v, 6, 5, 81), s = smooth(0.55, 0.75, fbm(u, v, 10, 3, 82));
    let c = mix3([0.3, 0.17, 0.12], [0.5, 0.3, 0.18], g);
    c = mix3(c, [0.78, 0.5, 0.18], s * 0.45);
    return { c, h: 0.3 + 0.4 * g, ro: 0.95 };
  },
};
function terrainLayer(name, f, N = 512) {
  const ah = new Uint8Array(N * N * 4), nr = new Uint8Array(N * N * 4), H = new Float32Array(N * N), R = new Float32Array(N * N);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const o = f(x / N, y / N), i = y * N + x;
    ah[i * 4] = Math.round(clamp(o.c[0]) * 255); ah[i * 4 + 1] = Math.round(clamp(o.c[1]) * 255); ah[i * 4 + 2] = Math.round(clamp(o.c[2]) * 255);
    ah[i * 4 + 3] = Math.round(clamp(o.h) * 255); H[i] = o.h; R[i] = o.ro;
  }
  const S = 3.0;
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const h = (xx, yy) => H[((yy + N) % N) * N + ((xx + N) % N)];
    const dx = (h(x + 1, y) - h(x - 1, y)) * S, dy = (h(x, y + 1) - h(x, y - 1)) * S, l = Math.hypot(dx, dy, 1), i = (y * N + x) * 4;
    nr[i] = Math.round((-dx / l * 0.5 + 0.5) * 255); nr[i + 1] = Math.round((dy / l * 0.5 + 0.5) * 255); nr[i + 2] = Math.round((1 / l * 0.5 + 0.5) * 255);
    nr[i + 3] = Math.round(clamp(R[y * N + x]) * 255);
  }
  save(join(TEX, 'terrain', name + '_ah.png'), N, N, 4, ah);
  save(join(TEX, 'terrain', name + '_nr.png'), N, N, 4, nr);
  console.log('terrain', name);
}

// ── building materials (texture sets: _c, _n, _orm) ─────────────────────
const SETS = {
  stone: () => [bake(512, brick({ cols: [[0.78, 0.74, 0.64], [0.72, 0.68, 0.6], [0.66, 0.64, 0.58]], mortar: [0.5, 0.48, 0.42], seed: 101, rows: 8, nx: 3, soot: 0.15 })), 2.4],
  stone_red: () => [bake(512, brick({ cols: [[0.62, 0.24, 0.18], [0.55, 0.2, 0.16], [0.5, 0.22, 0.2]], mortar: [0.3, 0.22, 0.2], seed: 102, rows: 8, nx: 3, soot: 0.3 })), 2.4],
  marble: () => [bake(512, slabs({ base: [0.78, 0.86, 0.86], n: 2, seed: 103, tint: [0.95, 1.02, 1.04] })), 1.6],
  flag: () => [bake(512, slabs({ base: [0.62, 0.58, 0.52], n: 3, seed: 104 })), 2.0],
  wood: () => [bake(512, planks({ color: [0.46, 0.3, 0.17], n: 6, seed: 105 })), 2.2],
  iron: () => [bake(512, plate({ color: [0.26, 0.27, 0.3], seed: 106, rust: 0.25, diamonds: 0, panels: 2 })), 2.0],
  metal: () => [bake(256, metalTex({ seed: 107, rust: 0.05 })), 1.5],
  slate: () => [bake(512, brick({ cols: [[0.22, 0.26, 0.4], [0.2, 0.22, 0.36], [0.26, 0.28, 0.44]], mortar: [0.12, 0.12, 0.16], seed: 108, rows: 10, nx: 5, soot: 0.1 })), 2.0],
};

// ── props ───────────────────────────────────────────────────────────────
const DEFS = {
  stone: { set: 'stone' }, stone_red: { set: 'stone_red' }, marble: { set: 'marble' }, flag: { set: 'flag' }, wood: { set: 'wood' }, iron: { set: 'iron', rough: 0.7 },
  steel: { set: 'metal', tint: [0.86, 0.88, 0.92], rough: 0.25, metal: 1 }, bronze: { set: 'metal', tint: [0.8, 0.52, 0.24], rough: 0.35, metal: 1 },
  gold: { set: 'metal', tint: [1.0, 0.76, 0.3], rough: 0.25, metal: 1, emissive: [0.12, 0.08, 0.02] },
  leather: { set: null, tint: [0.3, 0.16, 0.08], rough: 0.7, metal: 0 }, cloth_red: { set: null, tint: [0.7, 0.08, 0.1], rough: 0.95, metal: 0 },
  bark: { set: 'wood', tint: [0.55, 0.42, 0.34], rough: 0.95 },
  leaf: { set: null, tint: [0.2, 0.52, 0.12], rough: 0.85, metal: 0 }, leaf_gold: { set: null, tint: [0.85, 0.6, 0.12], rough: 0.85, metal: 0 },
  leaf_teal: { set: null, tint: [0.1, 0.45, 0.4], rough: 0.85, metal: 0 }, pine: { set: null, tint: [0.08, 0.3, 0.16], rough: 0.9, metal: 0 },
  rock: { set: 'flag', tint: [0.72, 0.68, 0.74], rough: 0.9 },
  heart: { set: null, tint: [1.0, 0.2, 0.32], rough: 0.15, metal: 0, emissive: [2.2, 0.3, 0.5] },
  glow_cyan: { set: null, tint: [0.3, 0.85, 1.0], rough: 0.2, metal: 0, emissive: [0.35, 1.1, 1.3] },
  glow_amber: { set: null, tint: [1.0, 0.7, 0.3], rough: 0.3, metal: 0, emissive: [4, 2.2, 0.5] },
  glow_violet: { set: null, tint: [0.7, 0.4, 1.0], rough: 0.2, metal: 0, emissive: [2.2, 0.9, 4.0] },
  ember: { set: null, tint: [1.0, 0.45, 0.1], rough: 0.5, metal: 0, emissive: [6, 2.2, 0.4] },
  glass: { set: null, tint: [0.45, 0.9, 0.5], rough: 0.05, metal: 0, emissive: [0.4, 1.6, 0.6] },
};
class Mats {
  constructor(glb) { this.glb = glb; this.idx = {}; }
  get(n) { if (this.idx[n] === undefined) { const d = DEFS[n]; if (!d) throw new Error('no material ' + n); this.idx[n] = this.glb.pbr(n, TEX, d.set, d); } return this.idx[n]; }
}
function writeProp(name, fn) {
  const glb = new Glb(), M = new Mats(glb), m = new Mesher();
  fn(m);
  const mi = {};
  for (const k of m.groups.keys()) mi[k] = M.get(k);
  glb.sceneNodes.push(glb.node({ name, mesh: glb.mesh(name, m.prims(mi)) }));
  glb.write(join(MOD, name + '.glb'));
  console.log('prop', name);
}
/** a turned shape along y: profile [[y, r], ...] */
function lathe(m, mat, prof, o = {}) {
  const n = o.n ?? 16, c = o.c ?? [0, 0, 0];
  for (let i = 0; i + 1 < prof.length; i++) {
    const [y0, r0] = prof[i], [y1, r1] = prof[i + 1];
    if (y1 - y0 < 1e-5) continue;
    m.cyl(mat, [c[0], c[1] + (y0 + y1) / 2, c[2]], r0, y1 - y0, { r1, n, uv: o.uv ?? 0.3, caps: false });
  }
  const [yb, rb] = prof[0], [yt, rt] = prof[prof.length - 1];
  if (rb > 0) m.cyl(mat, [c[0], c[1] + yb - 0.0005, c[2]], rb, 0.001, { n, uv: o.uv ?? 0.3 });
  if (rt > 0) m.cyl(mat, [c[0], c[1] + yt + 0.0005, c[2]], rt, 0.001, { n, uv: o.uv ?? 0.3 });
}
/** a lumpy ball (foliage, boulders): an icosphere-like lat/long ball with noise on the radius, flat shaded per quad */
function blob(m, mat, c, r, o = {}) {
  const nu = o.nu ?? 10, nv = o.nv ?? 7, seed = o.seed ?? 1, sy = o.sy ?? 1, rough = o.rough ?? 0.22, flat = o.flat ?? 0;
  const P = (i, j) => {
    const a = (i / nu) * Math.PI * 2, b = (j / nv) * Math.PI - Math.PI / 2;
    const k = 1 + rough * (hash2(i % nu, j, seed) - 0.5) * 2 * (j === 0 || j === nv ? 0 : 1);
    let y = Math.sin(b) * r * k * sy;
    if (flat && y < -r * flat) y = -r * flat;
    return [c[0] + Math.cos(a) * Math.cos(b) * r * k, c[1] + y, c[2] + Math.sin(a) * Math.cos(b) * r * k];
  };
  for (let j = 0; j < nv; j++) for (let i = 0; i < nu; i++) {
    const a = P(i, j), b = P(i + 1, j), cc = P(i + 1, j + 1), d = P(i, j + 1);
    const e1 = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], e2 = [d[0] - a[0], d[1] - a[1], d[2] - a[2]];
    let n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
    const mid = [(a[0] + cc[0]) / 2 - c[0], (a[1] + cc[1]) / 2 - c[1], (a[2] + cc[2]) / 2 - c[2]];
    if (n[0] * mid[0] + n[1] * mid[1] + n[2] * mid[2] < 0) n = n.map((x) => -x);
    const l = Math.hypot(...n) || 1; n = n.map((x) => x / l);
    const uv = (p) => [(p[0] + p[2]) / 1.5, p[1] / 1.5];
    m.quad(mat, a, b, cc, d, n, uv(a), uv(b), uv(cc), uv(d));
  }
}
// held weapons: modelled along +z from the grip at the origin (the hand closes round the origin; the blade or head out
// along +z), so the game hangs them on HandR as they are
function sword(m) {
  m.cyl('leather', [0, 0, 0.0], 0.018, 0.2, { axis: 'z', n: 10, uv: 0.2 });
  m.cyl('gold', [0, 0, -0.11], 0.03, 0.03, { axis: 'z', n: 10, uv: 0.2 });
  m.box('gold', [0, 0, 0.11], [0.2, 0.035, 0.035], { uv: 0.2 });
  m.box('steel', [0, 0, 0.55], [0.065, 0.012, 0.82], { uv: 0.3 });
  m.box('steel', [0, 0, 0.55], [0.02, 0.022, 0.82], { uv: 0.3 }); // the fuller ridge
  m.cyl('steel', [0, 0, 1.0], 0.034, 0.1, { axis: 'z', r1: 0.002, n: 4, uv: 0.2 });
}
function maul(m) { // the stonebreaker: a long haft and a heavy bronze head
  m.cyl('wood', [0, 0, 0.3], 0.025, 1.1, { axis: 'z', n: 10, uv: 0.4 });
  m.cyl('leather', [0, 0, -0.05], 0.03, 0.28, { axis: 'z', n: 10, uv: 0.3 });
  m.box('bronze', [0, 0, 0.88], [0.4, 0.2, 0.22], { uv: 0.3 });
  for (const sx of [-1, 1]) m.box('iron', [sx * 0.21, 0, 0.88], [0.04, 0.24, 0.26], { uv: 0.3 });
}
function cleaver(m) {
  m.cyl('wood', [0, 0, 0.05], 0.02, 0.28, { axis: 'z', n: 8, uv: 0.3 });
  m.box('iron', [0, 0.06, 0.42], [0.03, 0.2, 0.5], { uv: 0.3 });
  m.box('steel', [0, -0.045, 0.42], [0.012, 0.02, 0.5], { uv: 0.3 });
}
function spear(m) {
  m.cyl('wood', [0, 0, 0.45], 0.02, 2.0, { axis: 'z', n: 8, uv: 0.4 });
  m.box('gold', [0, 0, 1.43], [0.06, 0.06, 0.06], { uv: 0.3 });
  m.box('steel', [0, 0, 1.6], [0.07, 0.012, 0.3], { uv: 0.3 });
  m.cyl('steel', [0, 0, 1.79], 0.035, 0.1, { axis: 'z', r1: 0.002, n: 4, uv: 0.2 });
}
function bigaxe(m) { // the bosses' weapon: a broad crescent on a long haft
  m.cyl('iron', [0, 0, 0.45], 0.035, 1.5, { axis: 'z', n: 10, uv: 0.4 });
  for (let i = 0; i < 7; i++) { const a = (i / 6 - 0.5) * 1.6, r = 0.32; m.box('steel', [0, Math.cos(a) * r - 0.12, 1.05 + Math.sin(a) * r], [0.03, 0.12, 0.16], { uv: 0.3 }); }
  m.box('iron', [0, 0.05, 1.05], [0.06, 0.22, 0.18], { uv: 0.3 });
}
// pickups: base at the origin, up +y
function key(m, mat) {
  return () => {
    m.cyl(mat, [0, 0.4, 0], 0.09, 0.03, { axis: 'z', n: 16, uv: 0.2 });
    m.cyl(mat, [0, 0.4, 0.02], 0.05, 0.035, { axis: 'z', n: 16, uv: 0.2 });
    m.box(mat, [0, 0.19, 0], [0.035, 0.32, 0.03], { uv: 0.2 });
    m.box(mat, [0.045, 0.07, 0], [0.06, 0.03, 0.03], { uv: 0.2 }); m.box(mat, [0.045, 0.13, 0], [0.05, 0.03, 0.03], { uv: 0.2 });
  };
}
function heart(m) { // a heart-shaped crystal: two lobes and a point, glowing rose
  blob(m, 'heart', [-0.11, 0.42, 0], 0.15, { nu: 10, nv: 7, rough: 0.05, seed: 3 });
  blob(m, 'heart', [0.11, 0.42, 0], 0.15, { nu: 10, nv: 7, rough: 0.05, seed: 4 });
  m.cyl('heart', [0, 0.27, 0], 0.22, 0.26, { r1: 0.24, n: 10, uv: 0.3 });
  m.cyl('heart', [0, 0.1, 0], 0.005, 0.1, { r1: 0.22, n: 10, uv: 0.3 });
}
function charm(m) { // the stamina charm: a gold ring round a cyan stone
  m.cyl('gold', [0, 0.35, 0], 0.16, 0.05, { axis: 'z', n: 18, uv: 0.2 });
  blob(m, 'glow_cyan', [0, 0.35, 0.0], 0.11, { nu: 8, nv: 6, rough: 0.05, seed: 5 });
}
function tonic(m) { // the flask: a round glass belly, a cork, a strap
  lathe(m, 'glass', [[0, 0.0], [0.02, 0.09], [0.1, 0.12], [0.18, 0.1], [0.24, 0.04], [0.3, 0.035]], { n: 14 });
  m.cyl('wood', [0, 0.32, 0], 0.03, 0.05, { n: 10, uv: 0.2 });
}
function chest(m) { // the base: an iron-banded box, open at the top (the lid is chest_lid)
  m.box('wood', [0, 0.3, 0], [1.0, 0.6, 0.62], { uv: 0.6 });
  for (const x of [-0.42, 0.42]) m.box('iron', [x, 0.3, 0], [0.07, 0.62, 0.64], { uv: 0.4 });
  m.box('gold', [0, 0.52, 0.32], [0.12, 0.12, 0.03], { uv: 0.2 });
}
function chestLid(m) { // hinged along its back edge at the origin
  m.box('wood', [0, 0.1, 0.31], [1.0, 0.2, 0.62], { uv: 0.6 });
  m.cyl('wood', [0, 0.2, 0.31], 0.31, 1.0, { axis: 'x', n: 12, uv: 0.6 });
  for (const x of [-0.42, 0.42]) m.cyl('iron', [x, 0.2, 0.31], 0.325, 0.07, { axis: 'x', n: 12, uv: 0.4 });
}
function shrine(m) { // a save point: a stepped plinth, a standing ring of stone and a floating crystal (the crystal is its own glb)
  m.cyl('flag', [0, 0.12, 0], 1.6, 0.24, { n: 24, uv: 1 });
  m.cyl('stone', [0, 0.36, 0], 1.1, 0.24, { n: 20, uv: 1 });
  for (const sx of [-1, 1]) m.box('stone', [sx * 0.85, 1.5, 0], [0.32, 2.1, 0.4], { uv: 1 });
  m.box('stone', [0, 2.65, 0], [2.1, 0.32, 0.46], { uv: 1 });
  for (const sx of [-1, 1]) m.box('glow_cyan', [sx * 0.85, 1.5, 0.21], [0.08, 1.4, 0.02], { uv: 0.3 });
}
function crystal(m) { blob(m, 'glow_cyan', [0, 0, 0], 0.32, { nu: 6, nv: 4, rough: 0.0, sy: 1.8, seed: 6 }); }
function tree(m) { // a broad leafy tree: a leaning trunk, three limbs, foliage in lumpy clusters
  lathe(m, 'bark', [[-0.3, 0.42], [0.2, 0.34], [1.8, 0.26], [3.4, 0.2]], { n: 10, uv: 1 });
  const limbs = [[1.2, 3.4, 0.4], [-1.0, 3.6, -0.6], [0.2, 4.1, 1.0]];
  for (const [x, y, z] of limbs) m.cyl('bark', [x * 0.5, (y + 3.0) / 2, z * 0.5], 0.14, y - 2.6, { r1: 0.08, n: 7, uv: 1 });
  const clusters = [[0, 5.0, 0, 2.0], [1.4, 4.4, 0.5, 1.5], [-1.3, 4.6, -0.6, 1.5], [0.3, 4.4, 1.4, 1.4], [-0.4, 5.6, -0.2, 1.3], [0.6, 4.0, -1.3, 1.2]];
  clusters.forEach(([x, y, z, r], i) => blob(m, 'leaf', [x, y, z], r, { nu: 9, nv: 6, rough: 0.25, seed: 20 + i, sy: 0.8 }));
}
function treeGold(m) { // the canyon's autumn tree: the same build in amber
  lathe(m, 'bark', [[-0.3, 0.36], [0.2, 0.3], [2.4, 0.2]], { n: 9, uv: 1 });
  [[0, 3.6, 0, 1.6], [1.0, 3.1, 0.4, 1.2], [-0.9, 3.3, -0.4, 1.2], [0.2, 4.2, 0.2, 1.0]].forEach(([x, y, z, r], i) => blob(m, 'leaf_gold', [x, y, z], r, { nu: 9, nv: 6, rough: 0.25, seed: 40 + i, sy: 0.8 }));
}
function mangrove(m) { // the marsh: a tree up on stilt roots, a teal crown
  for (let i = 0; i < 5; i++) { const a = (i / 5) * Math.PI * 2; m.cyl('bark', [Math.cos(a) * 0.5, 0.45, Math.sin(a) * 0.5], 0.08, 1.3, { r1: 0.12, n: 6, uv: 1 }); }
  lathe(m, 'bark', [[0.8, 0.3], [3.2, 0.2]], { n: 8, uv: 1 });
  [[0, 4.0, 0, 1.8], [1.2, 3.5, 0.3, 1.3], [-1.1, 3.7, -0.3, 1.3]].forEach(([x, y, z, r], i) => blob(m, 'leaf_teal', [x, y, z], r, { nu: 9, nv: 6, rough: 0.3, seed: 60 + i, sy: 0.7 }));
}
function pine(m) { // the keep's hill: a dark pine, stacked cones
  lathe(m, 'bark', [[-0.3, 0.26], [1.5, 0.18]], { n: 8, uv: 1 });
  for (let i = 0; i < 4; i++) m.cyl('pine', [0, 1.6 + i * 1.05, 0], 1.7 - i * 0.35, 1.5, { r1: 0.15, n: 10, uv: 1 });
}
function rock(m) { blob(m, 'rock', [0, 0.35, 0], 1.0, { nu: 9, nv: 6, rough: 0.28, seed: 7, sy: 0.65, flat: 0.5 }); }
function rockTall(m) { blob(m, 'rock', [0, 1.1, 0], 0.9, { nu: 8, nv: 6, rough: 0.25, seed: 8, sy: 1.6, flat: 0.75 }); }
function brazier(m) { // an iron bowl on three legs with a glowing ember bed (the flame is particles)
  for (let i = 0; i < 3; i++) { const a = (i / 3) * Math.PI * 2; m.cyl('iron', [Math.cos(a) * 0.28, 0.45, Math.sin(a) * 0.28], 0.035, 0.9, { n: 6, uv: 0.5 }); }
  lathe(m, 'iron', [[0.82, 0.12], [0.9, 0.42], [1.08, 0.5]], { n: 14, uv: 0.5 });
  m.cyl('ember', [0, 1.0, 0], 0.42, 0.06, { n: 14, uv: 0.5 });
  for (let i = 0; i < 5; i++) { const a = i * 1.26, r = i ? 0.2 : 0; m.cyl('ember', [Math.cos(a) * r, 1.3 + (i ? 0 : 0.1), Math.sin(a) * r], i ? 0.14 : 0.22, i ? 0.5 : 0.75, { r1: 0.01, n: 8, uv: 0.5 }); } // the flame: glowing tongues
}
function lever(m) { // a stone post with an iron lever (the handle is the game's to turn)
  m.box('stone', [0, 0.5, 0], [0.5, 1.0, 0.5], { uv: 1 });
  m.box('iron', [0, 1.0, 0], [0.12, 0.08, 0.3], { uv: 0.3 });
}
function leverArm(m) { m.cyl('iron', [0, 0.35, 0], 0.035, 0.7, { n: 8, uv: 0.3 }); blob(m, 'cloth_red', [0, 0.72, 0], 0.08, { nu: 8, nv: 5, rough: 0, seed: 9 }); }
function pillar(m) { // a fluted column with a base and capital
  m.box('marble', [0, 0.15, 0], [1.1, 0.3, 1.1], { uv: 1 });
  m.cyl('marble', [0, 2.3, 0], 0.4, 4.0, { n: 16, uv: 1 });
  m.box('marble', [0, 4.45, 0], [1.0, 0.3, 1.0], { uv: 1 });
}
function banner(m) { // a crimson banner on a pole (the keep)
  m.cyl('iron', [0, 2.0, 0], 0.05, 4.0, { n: 8, uv: 0.5 });
  m.cyl('iron', [0, 3.9, 0.35], 0.03, 0.8, { axis: 'z', n: 6, uv: 0.5 });
  m.box('cloth_red', [0, 3.0, 0.38], [0.04, 1.7, 0.66], { uv: 1 });
  m.box('gold', [0, 3.0, 0.38], [0.05, 0.3, 0.3], { uv: 0.3 });
}
function grassTuft(m) { // reeds for the marsh edge
  for (let i = 0; i < 9; i++) { const a = i * 2.4, r = 0.15 + 0.08 * (i % 3); m.cyl('leaf_teal', [Math.cos(a) * r, 0.55, Math.sin(a) * r], 0.02, 1.1, { r1: 0.004, n: 4, uv: 1 }); }
}

if (want('textures')) {
  for (const [n, f] of Object.entries(LAYERS)) terrainLayer(n, f);
  for (const [n, f] of Object.entries(SETS)) { const [t, s] = f(); writeSet(TEX, n, t, s); console.log('set', n); }
}
if (want('props')) {
  for (const [n, f] of [['sword', sword], ['maul', maul], ['cleaver', cleaver], ['spear', spear], ['bigaxe', bigaxe], ['heart', heart], ['charm', charm], ['tonic', tonic],
    ['chest', chest], ['chest_lid', chestLid], ['shrine', shrine], ['crystal', crystal], ['tree', tree], ['tree_gold', treeGold], ['mangrove', mangrove], ['pine', pine],
    ['rock', rock], ['rock_tall', rockTall], ['brazier', brazier], ['lever', lever], ['lever_arm', leverArm], ['pillar', pillar], ['banner', banner], ['reeds', grassTuft]]) writeProp(n, f);
  { const m0 = new Mesher(); }
  writeProp('key_sun', (m) => key(m, 'gold')());
  writeProp('key_moon', (m) => key(m, 'glow_violet')());
}
