// Generates the world's textures and props into assets/: the terrain's eight layers (Terrain3D packing: albedo with
// height in alpha, normal with roughness in alpha), the building materials the world code builds walls, stairs and
// gates from, and the props as textured glb (the held weapons, keys, the heart vessel, chests, shrines, trees, rocks,
// braziers, crystals, the tonic flask). Every pixel and vertex is made here by our own code. Run:
//   node games/adventure/tools/make-world-assets.mjs [textures] [props]
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { mkdirSync, writeFileSync } from 'node:fs';
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
    let c = mix3([0.07, 0.16, 0.07], [0.42, 0.58, 0.2], clamp(g * 2.0 - 0.5));
    c = mix3(c, [0.52, 0.62, 0.16], smooth(0.6, 0.8, clump) * 0.5);
    c = mul3(c, 0.55 + 0.9 * blade);
    const [f1, , id] = cells(u, v, 48, 14, 1);
    const fl = smooth(0.16, 0.08, f1) * smooth(0.6, 0.8, fbm(u, v, 5, 3, 15));
    c = mix3(c, id < 0.5 ? [0.98, 0.84, 0.2] : id < 0.8 ? [0.66, 0.4, 0.92] : [0.96, 0.96, 0.9], fl);
    return { c, h: 0.3 + 0.5 * blade * clump, ro: 0.92 };
  },
  // 1 snow: wind-packed, faintly blue in the hollows, sparkle-free (it is lit by the sun, not by the texture)
  snow: (u, v) => {
    const g = fbm(u, v, 5, 5, 21), drift = fbm(u * 0.5 + 0.3, v * 2, 3, 3, 22), fine = fbm(u, v, 40, 2, 23);
    const rip = Math.pow(Math.sin((u * 0.15 + v + fbm(u, v, 3, 3, 24) * 0.12) * Math.PI * 18) * 0.5 + 0.5, 3); // wind ripples (sastrugi): long low ridges
    let c = mix3([0.42, 0.52, 0.74], [0.92, 0.94, 0.98], clamp(g * 0.9 + drift * 0.6 - 0.25));
    c = mul3(c, (0.78 + 0.32 * fine) * (0.94 + 0.08 * rip));
    return { c, h: 0.3 + 0.35 * drift + 0.1 * fine + 0.15 * rip, ro: 0.7 };
  },
  // 2 the worn path: packed earth with fine grit, ruts along it, and gravel: small angular grains (the corners of
  // fine cells, a few in each patch), never round stones that read as coins
  dirt: (u, v) => {
    const g = fbm(u, v, 8, 5, 31), grit = fbm(u, v, 64, 2, 34), rut = fbm(u * 0.4, v * 6, 4, 3, 35);
    const [f1, f2, id] = cells(u, v, 110, 32, 1);
    const patch = smooth(0.45, 0.7, fbm(u, v, 6, 3, 36));
    const grain = smooth(0.02, 0.07, f2 - f1) * (id > 0.9 - 0.12 * patch ? 1 : 0); // a cell's polygon, inset from its edges
    let c = mix3([0.24, 0.16, 0.09], [0.56, 0.41, 0.26], clamp(g * 1.1 + rut * 0.4 - 0.25));
    c = mul3(c, 0.75 + 0.45 * grit);
    const stone = mul3(c, 0.62 + 0.5 * hash2(Math.floor(id * 9973), 3, 37)); // the earth's own colour, a little lighter or darker
    c = mix3(c, mix3(stone, [0.45, 0.42, 0.38], 0.3), grain * 0.8);
    return { c, h: 0.35 + 0.25 * g + 0.15 * grit + grain * 0.25 - rut * 0.15, ro: 0.95 - grain * 0.08 };
  },
  // 3 sandstone: the canyon floor and its ramps, layered in bands of amber and rose, crazed by thin wandering cracks
  sand: (u, v) => {
    const g = fbm(u, v, 6, 5, 41), band = Math.sin((v + fbm(u, v, 3, 3, 42) * 0.25) * Math.PI * 10) * 0.5 + 0.5;
    let c = mix3([0.28, 0.16, 0.1], [0.92, 0.72, 0.52], clamp((band * 0.6 + g * 0.4) * 1.3 - 0.15));
    c = mix3(c, [0.7, 0.3, 0.24], smooth(0.62, 0.8, fbm(u, v, 4, 3, 43)) * 0.5);
    const r = 1 - Math.abs(fbm(u, v, 9, 4, 44) * 2 - 1), crack = smooth(0.95, 0.99, r) * smooth(0.3, 0.55, fbm(u, v, 3, 2, 46));
    const grit = fbm(u, v, 64, 2, 45), pit = fbm(u, v, 20, 3, 47);
    c = mul3(c, (1 - crack * 0.65) * (0.6 + 0.55 * grit) * (0.85 + 0.3 * pit));
    return { c, h: clamp(0.4 + 0.3 * g + 0.2 * band - crack * 0.35), ro: 0.88 };
  },
  // 4 cliff rock: strata of violet, grey and rust, each bed its own shade and grain, split by a few long joints and
  // wandering cracks, lichen on the ledges
  rock: (u, v) => {
    const warp = fbm(u, v, 3, 3, 54) - 0.5, BEDS = 14;
    const bv = (v + warp * 0.12) * BEDS, bed = ((Math.floor(bv) % BEDS) + BEDS) % BEDS, inBed = bv - Math.floor(bv);
    const hue = hash2(bed, 1, 55), shade = 0.55 + 0.6 * hash2(bed, 2, 56);
    let c = hue < 0.45 ? [0.42, 0.38, 0.48] : hue < 0.75 ? [0.5, 0.48, 0.46] : [0.56, 0.4, 0.32];
    const g = fbm(u, v, 6, 5, 51), fine = fbm(u, v * 2, 24 + 8 * Math.floor(hash2(bed, 3, 57) * 3), 3, 59);
    c = mul3(c, shade * (0.62 + 0.5 * g) * (0.8 + 0.35 * fine));
    const lip = smooth(0.82, 0.97, inBed), seam = 1 - smooth(0.0, 0.06, Math.min(inBed, 1 - inBed));
    c = mul3(c, 1 - seam * 0.55 + lip * 0.12);
    const joint = smooth(0.985, 0.998, 1 - Math.abs(fbm(u, v, 4, 4, 58) * 2 - 1)) * (hash2(bed, 4, 60) < 0.5 ? 1 : 0); // joints in some beds
    const r = 1 - Math.abs(fbm(u, v, 7, 5, 52) * 2 - 1), crack = smooth(0.975, 0.996, r);
    c = mul3(c, 1 - Math.max(joint, crack) * 0.7);
    c = mix3(c, [0.5, 0.58, 0.28], lip * smooth(0.6, 0.8, fbm(u, v, 9, 3, 53)) * 0.7);
    return { c, h: clamp(0.45 + 0.3 * g - seam * 0.35 + lip * 0.1 - Math.max(joint, crack) * 0.45), ro: 0.85 };
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
    let c = mix3([0.46, 0.4, 0.36], [0.68, 0.6, 0.52], id * 0.7 + g * 0.3);
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

// weathering over any surface: moss in the joints and in patches, damp stains running down, chipped corners
function weather(px, seed, moss = 0.5) {
  return (u, v, o) => {
    px(u, v, o);
    const m = smooth(0.52, 0.72, fbm(u, v, 5, 4, seed)) * moss + (o.h < 0.25 ? 0.35 * moss : 0) * smooth(0.4, 0.7, fbm(u, v, 12, 3, seed + 1));
    const stain = smooth(0.55, 0.8, fbm(u * 3, v * 0.4, 4, 3, seed + 2)) * 0.3;
    const g = fbm(u, v, 30, 3, seed + 3);
    o.r = lerp(o.r, 0.24 + 0.1 * g, m) * (1 - stain); o.g = lerp(o.g, 0.4 + 0.14 * g, m) * (1 - stain); o.b = lerp(o.b, 0.12 + 0.06 * g, m) * (1 - stain * 0.8);
    o.h += m * 0.15 * g; o.ro = Math.min(1, o.ro + m * 0.1);
  };
}
// ── building materials (texture sets: _c, _n, _orm) ─────────────────────
const SETS = {
  stone: () => [bake(512, weather(brick({ cols: [[0.74, 0.7, 0.6], [0.66, 0.62, 0.55], [0.6, 0.58, 0.52]], mortar: [0.42, 0.4, 0.35], seed: 101, rows: 8, nx: 3, soot: 0.25 }), 111, 0.55)), 2.6],
  stone_red: () => [bake(512, weather(brick({ cols: [[0.62, 0.24, 0.18], [0.55, 0.2, 0.16], [0.5, 0.22, 0.2]], mortar: [0.3, 0.22, 0.2], seed: 102, rows: 8, nx: 3, soot: 0.35 }), 112, 0.25)), 2.6],
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
  cloth_sack: { set: null, tint: [0.62, 0.52, 0.36], rough: 1, metal: 0 },
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
function barrel(m) { // staves and iron hoops
  lathe(m, 'wood', [[0, 0.3], [0.2, 0.36], [0.45, 0.38], [0.7, 0.36], [0.9, 0.3]], { n: 14, uv: 0.6 });
  for (const y of [0.12, 0.78]) m.cyl('iron', [0, y, 0], 0.335, 0.06, { n: 14, uv: 0.5, caps: false });
  m.cyl('iron', [0, 0.45, 0], 0.385, 0.06, { n: 14, uv: 0.5, caps: false });
}
function crate(m) { m.box('wood', [0, 0.35, 0], [0.7, 0.7, 0.7], { uv: 0.7 }); for (const y of [0.06, 0.64]) m.box('iron', [0, y, 0], [0.72, 0.06, 0.72], { uv: 0.5 }); }
function cart(m) { // a hand cart, tipped onto its shafts
  m.box('wood', [0, 0.75, 0], [1.3, 0.12, 2.0], { uv: 0.8 });
  for (const sx of [-1, 1]) { m.box('wood', [sx * 0.62, 1.0, 0], [0.08, 0.45, 2.0], { uv: 0.8 }); m.cyl('wood', [sx * 0.75, 0.48, 0.2], 0.48, 0.08, { axis: 'x', n: 14, uv: 0.6 }); m.cyl('iron', [sx * 0.75, 0.48, 0.2], 0.5, 0.05, { axis: 'x', n: 14, uv: 0.6, caps: false }); }
  for (const sx of [-1, 1]) m.box('wood', [sx * 0.4, 0.36, -1.6], [0.07, 0.07, 1.6], { uv: 0.6 });
  m.box('wood', [0, 0.1, -2.35], [0.9, 0.07, 0.07], { uv: 0.6 });
}
function lanternPost(m) { // an iron post with a hanging lantern (the game lights it)
  m.cyl('iron', [0, 1.2, 0], 0.05, 2.4, { n: 8, uv: 0.5 });
  m.box('iron', [0, 2.38, 0.25], [0.06, 0.06, 0.55], { uv: 0.5 });
  m.box('iron', [0, 2.0, 0.48], [0.22, 0.04, 0.22], { uv: 0.4 }); m.box('iron', [0, 1.72, 0.48], [0.24, 0.04, 0.24], { uv: 0.4 });
  m.box('glow_amber', [0, 1.86, 0.48], [0.16, 0.24, 0.16], { uv: 0.3 });
}
function sacks(m) { blob(m, 'cloth_sack', [0, 0.28, 0], 0.32, { nu: 8, nv: 6, rough: 0.1, seed: 31, sy: 0.85, flat: 0.8 }); blob(m, 'cloth_sack', [0.45, 0.24, 0.1], 0.28, { nu: 8, nv: 6, rough: 0.12, seed: 32, sy: 0.85, flat: 0.8 }); }
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
    ['rock', rock], ['rock_tall', rockTall], ['brazier', brazier], ['lever', lever], ['lever_arm', leverArm], ['pillar', pillar], ['banner', banner], ['reeds', grassTuft], ['barrel', barrel], ['crate', crate], ['cart', cart], ['lantern_post', lanternPost], ['sacks', sacks]]) writeProp(n, f);
  { const m0 = new Mesher(); }
  writeProp('key_sun', (m) => key(m, 'gold')());
  writeProp('key_moon', (m) => key(m, 'glow_violet')());
}

// ── flora: trees with leaf-card crowns, one glb per species with two nodes, Trunk and Crown (the game instances them
// per region and sways the crown in the wind about the top of the trunk) ──
function leafTexture(path, N = 512, seed = 5) {
  const px = new Uint8Array(N * N * 4);
  const leaves = [];
  for (let i = 0; i < 90; i++) {
    const a = hash2(i, 1, seed) * Math.PI * 2, r = Math.sqrt(hash2(i, 2, seed)) * 0.42;
    leaves.push({ x: 0.5 + Math.cos(a) * r, y: 0.5 + Math.sin(a) * r, rot: hash2(i, 3, seed) * Math.PI, len: 0.07 + 0.04 * hash2(i, 4, seed), k: 0.62 + 0.38 * hash2(i, 5, seed) });
  }
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = x / N, v = y / N;
    let best = null, bd = 1;
    for (const l of leaves) {
      const dx = u - l.x, dy = v - l.y, c = Math.cos(l.rot), s = Math.sin(l.rot);
      const lx = (dx * c + dy * s) / l.len, ly = (-dx * s + dy * c) / (l.len * 0.45);
      const d = lx * lx + ly * ly;
      if (d < 1 && d < bd) bd = d, best = { l, lx, ly };
    }
    const o = (y * N + x) * 4;
    if (!best) { px[o] = 90; px[o + 1] = 120; px[o + 2] = 60; px[o + 3] = 0; continue; }
    const vein = Math.abs(best.ly) < 0.08 ? 0.82 : 1, edge = 1 - 0.25 * bd;
    const k = best.l.k * vein * edge;
    px[o] = Math.round(200 * k); px[o + 1] = Math.round(230 * k); px[o + 2] = Math.round(170 * k); px[o + 3] = 255;
  }
  save(path, N, N, 4, px);
}
const LEAF_PNG = join(TEX, 'leaves_c.png');
/** a cluster of crossed leaf cards: three quads through c, normals pointing out from the crown's middle (soft light) */
function cards(m, mat, c, r, mid, seed, droop = 0) {
  const rot = hash2(seed, 7, 3) * Math.PI;
  const axes = [[Math.cos(rot), 0, Math.sin(rot)], [-Math.sin(rot), 0, Math.cos(rot)]];
  const nrm = (p) => { const d = [p[0] - mid[0], (p[1] - mid[1]) * 0.6 + 0.4, p[2] - mid[2]], l = Math.hypot(...d) || 1; return d.map((x) => x / l); };
  const quad = (A, B, C, D) => m.quad(mat, A, B, C, D, null, [0, 1], [1, 1], [1, 0], [0, 0], [nrm(A), nrm(B), nrm(C), nrm(D)]);
  for (const ax of axes) { // two upright cards
    const h = r * (1 + droop);
    quad([c[0] - ax[0] * r, c[1] - h, c[2] - ax[2] * r], [c[0] + ax[0] * r, c[1] - h, c[2] + ax[2] * r], [c[0] + ax[0] * r, c[1] + r * 0.9, c[2] + ax[2] * r], [c[0] - ax[0] * r, c[1] + r * 0.9, c[2] - ax[2] * r]);
  }
  const a = axes[0], b = axes[1]; // and one lying flat
  quad([c[0] - a[0] * r - b[0] * r, c[1], c[2] - a[2] * r - b[2] * r], [c[0] + a[0] * r - b[0] * r, c[1], c[2] + a[2] * r - b[2] * r], [c[0] + a[0] * r + b[0] * r, c[1], c[2] + a[2] * r + b[2] * r], [c[0] - a[0] * r + b[0] * r, c[1], c[2] - a[2] * r + b[2] * r]);
}
/** clusters spread over an ellipsoid crown */
function crownOf(m, mat, mid, rad, n, r, seed, droop = 0) {
  cards(m, mat, mid, r * 1.2, mid, seed, droop);
  for (let i = 0; i < n; i++) {
    const u = hash2(i, 1, seed) * 2 - 1, a = hash2(i, 2, seed) * Math.PI * 2, k = 0.55 + 0.45 * hash2(i, 3, seed), s = Math.sqrt(1 - u * u);
    cards(m, mat, [mid[0] + Math.cos(a) * s * rad[0] * k, mid[1] + u * rad[1] * k, mid[2] + Math.sin(a) * s * rad[2] * k], r * (0.8 + 0.4 * hash2(i, 4, seed)), mid, seed * 31 + i, droop);
  }
}
const SPECIES = {
  oak: { bark: [0.55, 0.42, 0.34], leaf: [0.34, 0.6, 0.16], trunk: (m) => { lathe(m, 'bark', [[-0.3, 0.42], [0.3, 0.32], [2.4, 0.24], [3.6, 0.18]], { n: 10, uv: 1 }); for (const [x, z] of [[1, 0.3], [-0.8, -0.6], [0.1, 0.9]]) m.cyl('bark', [x * 0.5, 3.6, z * 0.5], 0.12, 1.4, { r1: 0.06, n: 6, uv: 1 }); }, crown: { mid: [0, 4.9, 0], rad: [2.5, 1.7, 2.5], n: 9, r: 1.4 } },
  birch: { bark: [0.92, 0.9, 0.84], leaf: [0.6, 0.76, 0.22], trunk: (m) => lathe(m, 'bark', [[-0.3, 0.22], [0.3, 0.18], [4.5, 0.1]], { n: 8, uv: 1 }), crown: { mid: [0, 5.2, 0], rad: [1.5, 2.2, 1.5], n: 12, r: 0.9 } },
  aspen: { bark: [0.85, 0.8, 0.7], leaf: [0.98, 0.66, 0.14], trunk: (m) => lathe(m, 'bark', [[-0.3, 0.24], [0.3, 0.2], [3.4, 0.12]], { n: 8, uv: 1 }), crown: { mid: [0, 4.0, 0], rad: [1.7, 1.6, 1.7], n: 12, r: 0.95 } },
  willow: { bark: [0.45, 0.36, 0.3], leaf: [0.24, 0.6, 0.48], trunk: (m) => { for (let i = 0; i < 5; i++) { const a = (i / 5) * Math.PI * 2; m.cyl('bark', [Math.cos(a) * 0.45, 0.45, Math.sin(a) * 0.45], 0.08, 1.3, { r1: 0.12, n: 6, uv: 1 }); } lathe(m, 'bark', [[0.8, 0.3], [3.4, 0.2]], { n: 8, uv: 1 }); }, crown: { mid: [0, 4.0, 0], rad: [2.4, 1.2, 2.4], n: 11, r: 1.25, droop: 0.9 } },
  pine: { bark: [0.42, 0.3, 0.24], leaf: [0.14, 0.38, 0.2], trunk: (m) => lathe(m, 'bark', [[-0.3, 0.26], [6.0, 0.08]], { n: 8, uv: 1 }), crown: { tiers: [[1.6, 2.0], [2.6, 1.7], [3.6, 1.35], [4.6, 1.0], [5.5, 0.65], [6.2, 0.35]], r: 0.8 } },
  fir: { bark: [0.4, 0.3, 0.26], leaf: [0.62, 0.74, 0.74], trunk: (m) => lathe(m, 'bark', [[-0.3, 0.24], [5.4, 0.08]], { n: 8, uv: 1 }), crown: { tiers: [[1.4, 1.8], [2.4, 1.5], [3.4, 1.2], [4.4, 0.85], [5.2, 0.5], [5.8, 0.3]], r: 0.75 } },
  windswept: { bark: [0.5, 0.42, 0.36], leaf: [0.4, 0.64, 0.2], trunk: (m) => { lathe(m, 'bark', [[-0.3, 0.3], [1.5, 0.22]], { n: 8, uv: 1 }); m.cyl('bark', [0.6, 2.4, 0], 0.18, 2.0, { r1: 0.1, n: 6, uv: 1 }); }, crown: { mid: [1.6, 3.6, 0], rad: [2.6, 1.0, 1.6], n: 12, r: 1.0 } },
  deepwood: { bark: [0.36, 0.3, 0.3], leaf: [0.12, 0.5, 0.42], trunk: (m) => lathe(m, 'bark', [[-0.3, 0.5], [0.4, 0.4], [6.5, 0.22]], { n: 10, uv: 1 }), crown: { mid: [0, 8.0, 0], rad: [3.2, 2.4, 3.2], n: 9, r: 1.85 } },
};
function writeSpecies(name, sp) {
  const glb = new Glb();
  const bark = glb.pbr('bark_' + name, TEX, 'wood', { tint: sp.bark, rough: 0.95 });
  const leaf = glb.pbr('leaf_' + name, TEX, null, { tint: sp.leaf, rough: 0.82, metal: 0, alphaMode: 'MASK', alphaCutoff: 0.45, doubleSided: true });
  glb.materials[leaf].pbrMetallicRoughness.baseColorTexture = { index: glb.tex(LEAF_PNG) };
  const t = new Mesher(), c = new Mesher();
  sp.trunk(t);
  const cr = sp.crown;
  if (cr.tiers) cr.tiers.forEach(([y, rr], i) => { for (let k = 0; k < 6; k++) { const a = k / 6 * Math.PI * 2 + i; cards(c, 'leaf', [Math.cos(a) * rr * 0.6, y, Math.sin(a) * rr * 0.6], cr.r * (rr / 1.6 + 0.4), [0, y - 0.6, 0], i * 13 + k, 0.2); } });
  else crownOf(c, 'leaf', cr.mid, cr.rad, cr.n, cr.r, name.length * 7 + 3, cr.droop ?? 0);
  glb.sceneNodes.push(glb.node({ name: 'Trunk', mesh: glb.mesh(name + '_trunk', t.prims({ bark })) }));
  glb.sceneNodes.push(glb.node({ name: 'Crown', mesh: glb.mesh(name + '_crown', c.prims({ leaf })) }));
  // the impostor crown for far tiles: the same silhouette from a few large clusters (a tenth of the cards)
  const fc = new Mesher();
  if (cr.tiers) [0, 2, 4].forEach((ti) => { const [y, rr] = cr.tiers[ti]; cards(fc, 'leaf', [0, y + 0.3, 0], cr.r * (rr / 1.6 + 0.4) * 1.9, [0, y - 0.6, 0], ti * 13, 0.2); });
  else crownOf(fc, 'leaf', cr.mid, cr.rad, 2, cr.r * 1.9, name.length * 7 + 3, cr.droop ?? 0);
  glb.sceneNodes.push(glb.node({ name: 'CrownFar', mesh: glb.mesh(name + '_crown_far', fc.prims({ leaf })) }));
  glb.write(join(MOD, 'flora_' + name + '.glb'));
  console.log('flora', name);
}
function glowcap() { // the wood's giant mushroom: a pale stem, a glowing cap (the crown)
  const glb = new Glb();
  const stem = glb.pbr('stem', TEX, null, { tint: [0.78, 0.7, 0.74], rough: 0.7, metal: 0 });
  const cap = glb.pbr('cap', TEX, null, { tint: [0.3, 0.07, 0.42], rough: 0.55, metal: 0, emissive: [0.025, 0.0, 0.04] });
  const t = new Mesher(), c = new Mesher();
  lathe(t, 'stem', [[-0.2, 0.5], [0.3, 0.38], [3.2, 0.3], [3.6, 0.34]], { n: 12, uv: 1 });
  lathe(c, 'cap', [[3.4, 0.4], [3.5, 2.0], [3.75, 2.1], [4.2, 1.7], [4.7, 0.9], [4.95, 0.0]], { n: 18, uv: 1 });
  glb.sceneNodes.push(glb.node({ name: 'Trunk', mesh: glb.mesh('glowcap_stem', t.prims({ stem })) }));
  glb.sceneNodes.push(glb.node({ name: 'Crown', mesh: glb.mesh('glowcap_cap', c.prims({ cap })) }));
  glb.write(join(MOD, 'flora_glowcap.glb'));
  console.log('flora glowcap');
}
if (want('flora')) {
  leafTexture(LEAF_PNG);
  for (const [n, sp] of Object.entries(SPECIES)) writeSpecies(n, sp);
  glowcap();
}

// ── the sky's clouds: a tileable sheet of soft cumulus (white, alpha the cloud), drawn on a dome round the camera ──
if (want('clouds')) {
  const N = 1024, px = new Uint8Array(N * N * 4);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = x / N, v = y / N;
    const base = fbm(u, v, 4, 6, 91), detail = fbm(u, v, 16, 4, 92);
    const d = smooth(0.5, 0.78, base * 0.8 + detail * 0.3);
    const shade = 0.78 + 0.22 * smooth(0.3, 0.9, detail); // brighter tops, greyer bellies
    const o = (y * N + x) * 4;
    px[o] = Math.round(255 * shade); px[o + 1] = Math.round(255 * shade); px[o + 2] = Math.round(255 * Math.min(1, shade * 1.04)); px[o + 3] = Math.round(255 * d);
  }
  save(join(TEX, 'clouds_c.png'), N, N, 4, px);
  console.log('clouds');
}

// ── the grade: a contrast S-curve and a little saturation, as a 33^3 .cube the PostProcess applies after tone mapping
if (want('luts')) {
  mkdirSync(join(ROOT, 'grade'), { recursive: true });
  const N = 33, a = 10.5, mid = 0.4;
  const sig = (x) => 1 / (1 + Math.exp(-a * (x - mid)));
  const s0 = sig(0), s1 = sig(1), curve = (x) => (sig(x) - s0) / (s1 - s0);
  const lines = ['TITLE "adventure contrast"', `LUT_3D_SIZE ${N}`];
  for (let b = 0; b < N; b++) for (let g = 0; g < N; g++) for (let r = 0; r < N; r++) {
    let c = [r / (N - 1), g / (N - 1), b / (N - 1)].map((x) => Math.max(0, (x - 0.06) / 0.94)).map((x) => x * 0.1 + curve(x) * 0.9); // a black point under the fog and the sky fill, then the S-curve
    const l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
    c = c.map((x) => Math.min(1, Math.max(0, l + (x - l) * 0.82)));
    lines.push(c.map((x) => x.toFixed(5)).join(' '));
  }
  writeFileSync(join(ROOT, 'grade', 'contrast.cube'), lines.join('\n') + '\n');
  console.log('lut');
}
