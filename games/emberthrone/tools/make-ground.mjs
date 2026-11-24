// The terrain's eight ground layers from CC0 photo-scanned PBR sets (Poly Haven and ambientCG, both CC0; see CREDITS),
// graded to each region's palette and packed as Terrain3D packs them: albedo with height in alpha, normal (+Y, OpenGL)
// with roughness in alpha, 1024 square (the engine's layer size, so nothing is resampled).
//
// Per layer: the scan's colour times a little of its ambient occlusion (the cracks and gaps read darker); its tile-sized
// light and dark patches taken mostly out (the engine reads each layer at a random offset per small cell: a blotch the
// size of the tile would come round in every cell and show as a pattern); then graded so its mean colour, saturation and
// contrast are the layer's own (the game's palette); its height stretched over 0..1 (the engine blends two layers by
// height: grass stands in the cracks of rock, sand pools in the hollows); its normal deepened a little so the ground
// catches the low sun. Every step wraps at the edges, and the result is checked: the luma step across the wrap is
// measured against the step between neighbours inside (tests/emberthrone-ground.mjs `edges`), and a layer that does not
// tile fails the run.
//
// The sources are fetched once into out/ground-src (not committed; out/ is ignored). Run:
//   node games/emberthrone/tools/make-ground.mjs [layer ...]
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { mkdirSync, writeFileSync, readFileSync, existsSync } from 'node:fs';
import { inflateRawSync, deflateSync } from 'node:zlib';
import { readPngAny, edges, EDGE_MAX } from '../../../tests/emberthrone-ground.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..', '..', '..');
const OUT = join(HERE, '..', 'assets', 'textures', 'terrain');
const SRC = join(ROOT, 'out', 'ground-src');
mkdirSync(OUT, { recursive: true });
mkdirSync(SRC, { recursive: true });
const N = 1024;

// each layer: its source (ph = Poly Haven id, acg = ambientCG id), and its grade: mean colour (sRGB 0..255, the game's
// palette for that ground), saturation (1 = the scan's own), contrast (of luma about the mean), how much of the tile-sized
// patches to take out, how much AO darkens, the normal's depth, and the mean roughness (the scan's own variation about
// it, halved: scans disagree on what their grey means, and a glossy lawn shows the sun as a white blot)
export const LAYERS = {
  grass: { acg: 'Grass004', mean: [58, 86, 31], sat: 1.0, contrast: 1.2, flat: 0.5, ao: 0.5, nrm: 1.3, ro: 0.88 },
  snow: { acg: 'Snow006', mean: [150, 166, 206], sat: 1.0, contrast: 1.6, flat: 0.5, ao: 0.4, nrm: 1.6, ro: 0.6 },
  dirt: { ph: 'dry_ground_rocks', mean: [104, 78, 52], sat: 1.0, contrast: 1.15, flat: 0.7, ao: 0.6, nrm: 1.3, ro: 0.9 },
  sand: { ph: 'sandstone_cracks', mean: [150, 104, 74], sat: 1.05, contrast: 1.15, flat: 0.7, ao: 0.6, nrm: 1.2, ro: 0.88 },
  rock: { ph: 'rock_face', mean: [96, 86, 96], sat: 0.9, contrast: 1.25, flat: 0.4, ao: 0.6, nrm: 1.3, ro: 0.82 },
  mud: { ph: 'mud_forest', mean: [50, 80, 54], sat: 0.85, contrast: 1.1, flat: 0.5, ao: 0.6, nrm: 1.2, ro: 0.9 }, // (moss is matte: at 0.55 the sky's sheen at a glancing angle turned the patches pale, like snow)
  cobble: { ph: 'cobblestone_floor_04', mean: [134, 115, 99], sat: 1.0, contrast: 1.1, flat: 0.4, ao: 0.7, nrm: 1.2, ro: 0.78 },
  ash: { ph: 'burned_ground_01', mean: [88, 62, 46], sat: 0.8, contrast: 1.1, flat: 0.75, ao: 0.5, nrm: 1.2, ro: 0.93 },
};

// ── fetching ──────────────────────────────────────────────────────────
async function get(url, file) {
  if (existsSync(file)) return readFileSync(file);
  const r = await fetch(url, { redirect: 'follow' });
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  const b = Buffer.from(await r.arrayBuffer());
  writeFileSync(file, b);
  return b;
}
/** the members of a zip archive, by name (stored or deflated) */
function unzip(b) {
  let e = b.length - 22;
  while (e >= 0 && b.readUInt32LE(e) !== 0x06054b50) e--;
  if (e < 0) throw new Error('not a zip');
  const n = b.readUInt16LE(e + 10);
  let p = b.readUInt32LE(e + 16);
  const out = {};
  for (let i = 0; i < n; i++) {
    const method = b.readUInt16LE(p + 10), size = b.readUInt32LE(p + 20), nl = b.readUInt16LE(p + 28), xl = b.readUInt16LE(p + 30), cl = b.readUInt16LE(p + 32), off = b.readUInt32LE(p + 42);
    const name = b.toString('utf8', p + 46, p + 46 + nl);
    const lnl = b.readUInt16LE(off + 26), lxl = b.readUInt16LE(off + 28), data = b.subarray(off + 30 + lnl + lxl, off + 30 + lnl + lxl + size);
    out[name] = method === 8 ? inflateRawSync(data) : Buffer.from(data);
    p += 46 + nl + xl + cl;
  }
  return out;
}
/** a set's maps as PNG files on disk: { color, normal, rough, disp, ao } */
async function fetchSet(L) {
  const dir = join(SRC, L.ph || L.acg);
  mkdirSync(dir, { recursive: true });
  const maps = {};
  if (L.ph) {
    const base = `https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/${L.ph}/${L.ph}`;
    for (const [k, s] of Object.entries({ color: 'diff', normal: 'nor_gl', rough: 'rough', disp: 'disp', ao: 'ao' })) {
      maps[k] = join(dir, `${s}.png`);
      await get(`${base}_${s}_1k.png`, maps[k]);
    }
  } else {
    const zip = await get(`https://ambientcg.com/get?file=${L.acg}_1K-PNG.zip`, join(dir, 'set.zip'));
    const files = unzip(zip);
    for (const [k, s] of Object.entries({ color: '_Color.png', normal: '_NormalGL.png', rough: '_Roughness.png', disp: '_Displacement.png', ao: '_AmbientOcclusion.png' })) {
      const name = Object.keys(files).find((f) => f.endsWith(s));
      if (!name) { if (k === 'ao') continue; throw new Error(`${L.acg}: no ${s} in the archive`); }
      maps[k] = join(dir, `${k}.png`);
      if (!existsSync(maps[k])) writeFileSync(maps[k], files[name]);
    }
  }
  return maps;
}

// ── image helpers (planes of floats, N x N, wrapping) ─────────────────
const s2l = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
const l2s = (c) => { c = Math.min(1, Math.max(0, c)); return c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055; };
/** a map resampled (wrapping, bilinear) to N x N, each channel a Float32Array 0..1 */
function planes(file) {
  const im = readPngAny(file), out = [];
  for (let c = 0; c < im.ch; c++) {
    const p = new Float32Array(N * N);
    for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
      const fx = (x + 0.5) * im.w / N - 0.5, fy = (y + 0.5) * im.h / N - 0.5, x0 = Math.floor(fx), y0 = Math.floor(fy), u = fx - x0, v = fy - y0;
      const at = (xx, yy) => im.px[((((yy % im.h) + im.h) % im.h) * im.w + (((xx % im.w) + im.w) % im.w)) * im.ch + c] / 255;
      p[y * N + x] = (at(x0, y0) * (1 - u) + at(x0 + 1, y0) * u) * (1 - v) + (at(x0, y0 + 1) * (1 - u) + at(x0 + 1, y0 + 1) * u) * v;
    }
    out.push(p);
  }
  return out;
}
/** a wrapping box blur of radius r, three passes (near Gaussian) */
function blur(p, r) {
  let a = Float32Array.from(p), b = new Float32Array(N * N);
  const k = 1 / (2 * r + 1);
  for (let pass = 0; pass < 3; pass++) {
    for (let y = 0; y < N; y++) { let s = 0; for (let i = -r; i <= r; i++) s += a[y * N + ((i + N) % N)]; for (let x = 0; x < N; x++) { b[y * N + x] = s * k; s += a[y * N + ((x + r + 1) % N)] - a[y * N + ((x - r + N) % N)]; } }
    for (let x = 0; x < N; x++) { let s = 0; for (let i = -r; i <= r; i++) s += b[((i + N) % N) * N + x]; for (let y = 0; y < N; y++) { a[y * N + x] = s * k; s += b[((y + r + 1) % N) * N + x] - b[((y - r + N) % N) * N + x]; } }
  }
  return a;
}
/** the step across the wrap spread over a band each side (a scan that tiles all but a faint step at its edges then
 * tiles cleanly; one that already tiles is left all but unchanged): per row (and column) the difference between the last
 * and first pixel, smoothed along the edge, taken off in a ramp B pixels wide each side */
function heal(p, B = 48) {
  for (const axis of [0, 1]) {
    const at = (u, v) => (axis ? u * N + v : v * N + u); /* u along the step, v along the edge */
    const d = new Float32Array(N);
    for (let v = 0; v < N; v++) d[v] = p[at(0, v)] - p[at(N - 1, v)];
    const ds = new Float32Array(N);
    for (let v = 0; v < N; v++) { let t = 0; for (let k = -4; k <= 4; k++) t += d[(v + k + N) % N]; ds[v] = t / 9; }
    for (let v = 0; v < N; v++) for (let u = 0; u < B; u++) { const w = 0.5 * (1 - u / B); p[at(u, v)] -= ds[v] * w; p[at(N - 1 - u, v)] += ds[v] * w; }
  }
  return p;
}
const mean = (p) => { let s = 0; for (const v of p) s += v; return s / p.length; };
const pct = (p, q) => { const s = Float32Array.from(p).sort(); return s[Math.floor(q * (s.length - 1))]; };

// ── PNG out (RGBA8) ────────────────────────────────────────────────────
const CRC = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
const crc32 = (b) => { let c = -1; for (const x of b) c = CRC[(c ^ x) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; };
function png(bytes) {
  const raw = Buffer.alloc(N * (N * 4 + 1));
  for (let y = 0; y < N; y++) { raw[y * (N * 4 + 1)] = 0; Buffer.from(bytes.buffer, bytes.byteOffset + y * N * 4, N * 4).copy(raw, y * (N * 4 + 1) + 1); }
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td)); return Buffer.concat([l, td, c]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(N, 0); ih.writeUInt32BE(N, 4); ih[8] = 8; ih[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

// ── one layer ──────────────────────────────────────────────────────────
async function make(name, L) {
  const m = await fetchSet(L);
  const col = planes(m.color).slice(0, 3).map((p) => heal(p).map(s2l));
  const ao = m.ao ? heal(planes(m.ao)[0]) : null, nrm = planes(m.normal).map((p) => heal(p)), rough = heal(planes(m.rough)[0]), disp = heal(planes(m.disp)[0]);
  // ambient occlusion into the colour
  if (ao) for (let i = 0; i < N * N; i++) { const k = 1 - L.ao * (1 - ao[i]); for (const c of col) c[i] *= k; }
  // the tile-sized patches out: luma over its wide blur, pulled toward 1 by `flat`
  const lum = new Float32Array(N * N);
  for (let i = 0; i < N * N; i++) lum[i] = 0.2126 * col[0][i] + 0.7152 * col[1][i] + 0.0722 * col[2][i];
  const wide = blur(lum, 160), lm = mean(lum); /* (about a sixth of the tile and up: the scan's clumps and stones stay) */
  for (let i = 0; i < N * N; i++) { const k = Math.pow(lm / Math.max(wide[i], 1e-4), L.flat); for (const c of col) c[i] *= k; }
  // the grade: contrast and saturation about the mean (in sRGB, where they read evenly), then the mean colour
  const S = col.map((p) => p.map(l2s));
  const mu = S.map(mean);
  const ml = 0.2126 * mu[0] + 0.7152 * mu[1] + 0.0722 * mu[2];
  for (let i = 0; i < N * N; i++) {
    const l = 0.2126 * S[0][i] + 0.7152 * S[1][i] + 0.0722 * S[2][i];
    const l2 = ml + (l - ml) * L.contrast;
    for (let c = 0; c < 3; c++) S[c][i] = l2 + (S[c][i] - l) * L.sat;
  }
  const mu2 = S.map(mean), target = L.mean.map((v) => v / 255);
  for (let c = 0; c < 3; c++) { const g = s2l(target[c]) / Math.max(s2l(mu2[c]), 1e-4); for (let i = 0; i < N * N; i++) S[c][i] = l2s(s2l(Math.max(0, S[c][i])) * g); }
  // height over 0..1 (its 1st to 99th percentile), roughness about the layer's mean, normal deepened
  const lo = pct(disp, 0.01), hi = pct(disp, 0.99), rm = mean(rough);
  const ah = new Uint8Array(N * N * 4), nr = new Uint8Array(N * N * 4);
  for (let i = 0; i < N * N; i++) {
    for (let c = 0; c < 3; c++) ah[i * 4 + c] = Math.round(Math.min(1, Math.max(0, S[c][i])) * 255);
    ah[i * 4 + 3] = Math.round(Math.min(1, Math.max(0, (disp[i] - lo) / Math.max(hi - lo, 1e-4))) * 255);
    let x = (nrm[0][i] * 2 - 1) * L.nrm, y = (nrm[1][i] * 2 - 1) * L.nrm, z = nrm[2][i] * 2 - 1;
    const l = Math.hypot(x, y, z) || 1;
    x /= l, y /= l, z /= l;
    nr[i * 4] = Math.round((x * 0.5 + 0.5) * 255), nr[i * 4 + 1] = Math.round((y * 0.5 + 0.5) * 255), nr[i * 4 + 2] = Math.round((z * 0.5 + 0.5) * 255);
    nr[i * 4 + 3] = Math.round(Math.min(1, Math.max(0.05, L.ro + (rough[i] - rm) * 0.5)) * 255);
  }
  const fa = join(OUT, `${name}_ah.png`), fn = join(OUT, `${name}_nr.png`);
  writeFileSync(fa, png(ah));
  writeFileSync(fn, png(nr));
  const e = edges(readPngAny(fa));
  const ok = e.x < EDGE_MAX && e.y < EDGE_MAX;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name.padEnd(7)} ${L.ph ? 'Poly Haven ' + L.ph : 'ambientCG ' + L.acg}: wrap step x ${e.x} y ${e.y}`);
  return ok;
}

const only = process.argv.slice(2);
let bad = 0;
for (const [name, L] of Object.entries(LAYERS)) if (!only.length || only.includes(name)) if (!(await make(name, L))) bad++;
if (bad) { console.log(`${bad} layer(s) do not tile`); process.exit(1); }

// ── the grass tuft the Grass scatter draws (assets/models/grass_tuft.glb) ─────
// Blades, not cards: each a narrow curved strip (two segments to a point) leaning out from a small clump, every one its
// own height, lean, width and shade, coloured from the grass layer's own dark at the root (so the base melts into the
// ground) to a lighter, slightly yellower tip. Normals lean up (three parts up to one part the blade's face) so a tuft
// lights like the ground it stands on, and each blade is drawn both ways with the same normals (a double-sided
// material flips the normal on the back face, which turned half of every tuft dark). No alpha: nothing to alias.
import { Glb } from './chars/gltf-writer.mjs';
export function makeTuft(file, o = {}) {
  const blades = o.blades ?? 6, seed = o.seed ?? 11;
  let s = seed; const rnd = () => ((s = (s * 1103515245 + 12345) >>> 0) / 4294967296);
  const lin = (c) => c.map((v) => s2l(v / 255));
  const root = lin([38, 58, 20]), tipA = lin([84, 116, 42]), tipB = lin([102, 114, 44]);
  const g = { pos: [], nrm: [], col: [], idx: [] };
  const vert = (p, n, c) => { g.pos.push(...p); g.nrm.push(...n); g.col.push(c[0], c[1], c[2], 1); return g.pos.length / 3 - 1; };
  for (let b = 0; b < blades; b++) {
    const az = (b / blades) * Math.PI * 2 + rnd() * 0.9, r0 = 0.015 + 0.05 * rnd();
    const out = [Math.cos(az), 0, Math.sin(az)], side = [-Math.sin(az), 0, Math.cos(az)];
    const h = 0.2 + 0.22 * rnd(), lean = 0.25 + 0.45 * rnd(), w = 0.012 + 0.008 * rnd();
    const tip = tipA.map((v, k) => v + (tipB[k] - v) * rnd()), shade = 0.8 + 0.35 * rnd();
    const at = (t) => [out[0] * (r0 + lean * h * t * t), h * t * (1 - 0.25 * lean * t), out[2] * (r0 + lean * h * t * t)];
    // the face normal leans out and up with the curve; three parts up to one part face
    const nrm = (t) => { const f = [out[0] * (1 - t * 0.5), 0.3 + t * 0.6, out[2] * (1 - t * 0.5)]; const n = [f[0] * 0.25, 0.75 + f[1] * 0.25, f[2] * 0.25]; const l = Math.hypot(...n); return n.map((v) => v / l); };
    const col = (t) => root.map((v, k) => (v + (tip[k] - v) * Math.pow(t, 0.8)) * shade);
    const ring = [0, 0.5].map((t) => { const p = at(t), ww = w * (1 - t * 0.55); return [[p[0] - side[0] * ww, p[1], p[2] - side[2] * ww], [p[0] + side[0] * ww, p[1], p[2] + side[2] * ww], t]; });
    const tp = at(1);
    for (const dir of [1, -1]) { // both faces, the same upward normals
      const v = ring.map(([a, c, t]) => [vert(a, nrm(t), col(t)), vert(c, nrm(t), col(t))]);
      const vt = vert(tp, nrm(1), col(1));
      const tri = (a, bb, c) => (dir > 0 ? g.idx.push(a, bb, c) : g.idx.push(a, c, bb));
      tri(v[0][0], v[0][1], v[1][1]); tri(v[0][0], v[1][1], v[1][0]); tri(v[1][0], v[1][1], vt);
    }
  }
  const glb = new Glb();
  const mat = glb.material('grass_blade', 0.85, 0);
  glb.sceneNodes.push(glb.node({ name: 'grass_tuft', mesh: glb.mesh('grass_tuft', g, mat) }));
  glb.write(file);
  console.log(`ok   tuft    ${blades} blades, ${g.idx.length / 3} triangles: ${file}`);
}
if (!only.length || only.includes('tuft')) makeTuft(join(HERE, '..', 'assets', 'models', 'grass_tuft.glb'));

// ── the pebble the Pebbles scatter draws (assets/models/pebble.glb): a low lumpy stone of 16 triangles in the paths'
// own greys and browns (the game's rock prop is 100-odd triangles, too many for a stone a few centimetres across)
export function makePebble(file) {
  const lin = (c) => c.map((v) => s2l(v / 255));
  const g = { pos: [], nrm: [], col: [], idx: [] }, n = 8, top = [0, 0.55, 0], bot = [0, -0.25, 0];
  const ring = []; for (let i = 0; i < n; i++) { const a = (i / n) * Math.PI * 2, r = 0.85 + 0.25 * Math.sin(i * 2.7); ring.push([Math.cos(a) * r, 0.12 * Math.sin(i * 1.9), Math.sin(a) * r * 0.8]); }
  const tri = (a, b, c, col) => {
    const u = b.map((v, k) => v - a[k]), w = c.map((v, k) => v - a[k]);
    let nn = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]; const l = Math.hypot(...nn) || 1; nn = nn.map((v) => v / l);
    for (const p of [a, b, c]) { g.pos.push(...p); g.nrm.push(...nn); g.col.push(col[0], col[1], col[2], 1); }
    const i0 = g.pos.length / 3 - 3; g.idx.push(i0, i0 + 1, i0 + 2);
  };
  const light = lin([132, 118, 102]), dark = lin([92, 82, 72]);
  for (let i = 0; i < n; i++) { const a = ring[i], b = ring[(i + 1) % n]; tri(top, b, a, i % 3 ? light : dark); tri(bot, a, b, dark); }
  const glb = new Glb();
  const mat = glb.material('pebble', 0.8, 0);
  glb.sceneNodes.push(glb.node({ name: 'pebble', mesh: glb.mesh('pebble', g, mat) }));
  glb.write(file);
  console.log(`ok   pebble  ${g.idx.length / 3} triangles: ${file}`);
}
if (!only.length || only.includes('pebble')) makePebble(join(HERE, '..', 'assets', 'models', 'pebble.glb'));
