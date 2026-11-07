// towers' ground and path textures: one tiling 512 x 512 sheet each per theme (<theme>_ground.png, <theme>_path.png).
// The map's vertex colours carry each theme's hue (mapbuild.c's palettes), so these carry the detail: values round
// 0.85 with the relief painted in (grass blades, sand ripples, snow drifts, cracked basalt; pebbled dirt, cracked clay,
// trodden snow, flagstones), because the ground draws baked and unlit. All generated here from integer hashes
// (periodic, so the sheets tile seamlessly).
//   node tools/gen-textures.mjs [out dir (default assets/textures)]
import { writeFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateSync } from 'node:zlib';

const N = 512;
const DETAIL = 1.9; // how far each sheet's relief is pushed from its middle value (the look's local contrast)
const out = resolve(process.argv[2] || join(dirname(fileURLToPath(import.meta.url)), '../assets/textures'));

// ── periodic noise (crawler's) ──────────────────────────────────────
function hash(x, y, s) {
  let h = (Math.imul(x | 0, 374761393) + Math.imul(y | 0, 668265263) + Math.imul(s | 0, 2246822519)) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}
const mod = (a, p) => ((a % p) + p) % p;
function vnoise(x, y, p, s) {
  const ix = Math.floor(x), iy = Math.floor(y);
  let fx = x - ix, fy = y - iy;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  const h = (a, b) => hash(mod(a, p), mod(b, p), s);
  return (h(ix, iy) * (1 - fx) + h(ix + 1, iy) * fx) * (1 - fy) + (h(ix, iy + 1) * (1 - fx) + h(ix + 1, iy + 1) * fx) * fy;
}
function fbm(u, v, base, oct, s) {
  let t = 0, a = 0.5, f = base, n = 0;
  for (let o = 0; o < oct; o++) { t += a * vnoise(u * f, v * f, f, s + o * 31); n += a; a *= 0.5; f *= 2; }
  return t / n;
}
function voronoi(u, v, cells, s, jitter = 0.85) {
  const x = u * cells, y = v * cells, ix = Math.floor(x), iy = Math.floor(y);
  let d1 = 9, d2 = 9, id = 0;
  for (let j = -1; j <= 1; j++) for (let i = -1; i <= 1; i++) {
    const cx = ix + i, cy = iy + j;
    const px = cx + 0.5 + (hash(mod(cx, cells), mod(cy, cells), s) - 0.5) * jitter;
    const py = cy + 0.5 + (hash(mod(cx, cells), mod(cy, cells), s + 7) - 0.5) * jitter;
    const d = Math.hypot(x - px, y - py);
    if (d < d1) { d2 = d1; d1 = d; id = mod(cx, cells) * 977 + mod(cy, cells); } else if (d < d2) d2 = d;
  }
  return { d1, d2, edge: d2 - d1, id };
}
const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const mix = (a, b, t) => a + (b - a) * t;
const mix3 = (a, b, t) => [mix(a[0], b[0], t), mix(a[1], b[1], t), mix(a[2], b[2], t)];
const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };
const grey = (k) => [k, k, k];

// ── the grounds ─────────────────────────────────────────────────────
// grass: clumps of blades (short dark-light streaks leaning one way), flowers' specks over it
function grass(u, v, s, speck) {
  const clump = fbm(u, v, 8, 4, s);
  let k = 0.78 + 0.22 * clump;
  const blade = 0.6 * vnoise(u * 128, v * 128, 128, s + 3) + 0.4 * vnoise(u * 256, v * 256, 256, s + 5);
  k *= 0.86 + 0.24 * blade;
  const gap = smooth(0.62, 0.78, fbm(u, v, 6, 3, s + 9));
  k *= 1 - 0.18 * gap;
  let c = grey(k);
  if (speck && hash(Math.floor(u * 256), Math.floor(v * 256), s + 11) > 0.992 && clump > 0.45) c = speck.map((q) => q * 1.4);
  return c;
}
// sand: wind ripples bent by a slow noise, grains, a darker hollow here and there
function sand(u, v) {
  const w = fbm(u, v, 3, 3, 41);
  const rip = 0.5 + 0.5 * Math.sin((v * 34 + u * 6 + w * 3.5) * Math.PI * 2);
  let k = 0.86 + 0.08 * rip + 0.06 * (fbm(u, v, 24, 3, 43) - 0.5);
  k += 0.05 * (hash(Math.floor(u * 512), Math.floor(v * 512), 45) - 0.5);
  return grey(k * (1 - 0.1 * smooth(0.6, 0.75, fbm(u, v, 4, 3, 47))));
}
// snow: soft drifts, blue-shadowed hollows, sparkles
function snow(u, v) {
  const d = fbm(u, v, 4, 5, 51);
  let c = [0.9 + 0.1 * d, 0.92 + 0.08 * d, 0.97];
  const hollow = smooth(0.58, 0.72, fbm(u, v, 6, 3, 53));
  c = mix3(c, [0.8, 0.86, 0.98], hollow * 0.5);
  if (hash(Math.floor(u * 512), Math.floor(v * 512), 55) > 0.994) c = [1, 1, 1];
  return c;
}
// basalt: dark plates cracked in a net, the cracks glowing orange, ash drifted on top
function basalt(u, v) {
  const vo = voronoi(u, v, 10, 61, 0.9);
  const n = fbm(u, v, 8, 5, 63);
  let c = grey(0.72 + 0.2 * n + 0.08 * hash(vo.id, 1, 65));
  const crack = 1 - smooth(0.0, 0.035, vo.edge);
  const hot = smooth(0.4, 0.6, fbm(u, v, 3, 4, 67));
  c = mix3(c, [1.0, 0.55, 0.2], crack * (0.35 + 0.65 * hot));
  const ash = smooth(0.62, 0.8, fbm(u, v, 5, 4, 69));
  return mix3(c, [0.95, 0.9, 0.88], ash * 0.3);
}

// ── the paths ───────────────────────────────────────────────────────
// dirt: packed and mottled, pebbles poking through
function dirt(u, v, s) {
  let k = 0.84 + 0.12 * (fbm(u, v, 10, 4, s) - 0.5);
  const st = voronoi(u, v, 48, s + 5, 1);
  const peb = (st.d1 < 0.28 ? 1 : 0) * smooth(0.55, 0.7, hash(st.id, 2, s + 7));
  k = mix(k, 0.98 + 0.1 * hash(st.id, 3, s + 9), peb * 0.7);
  k *= 1 - 0.18 * (peb ? smooth(0.2, 0.28, st.d1) : 0);
  return grey(k);
}
// clay: dried and cracked into plates
function clay(u, v) {
  const vo = voronoi(u, v, 16, 71, 0.9);
  let k = 0.86 + 0.08 * hash(vo.id, 1, 73) + 0.06 * (fbm(u, v, 12, 3, 75) - 0.5);
  const crack = 1 - smooth(0.0, 0.05, vo.edge);
  k *= 1 - 0.45 * crack;
  return grey(k);
}
// trodden snow: packed, footprints and runner tracks pressed in blue-grey
function trodden(u, v) {
  let k = 0.88 + 0.1 * (fbm(u, v, 8, 4, 81) - 0.5);
  const prints = voronoi(u, v, 36, 83, 1);
  const pr = (prints.d1 < 0.22 ? 1 : 0) * smooth(0.4, 0.6, hash(prints.id, 1, 85));
  let c = grey(k);
  c = mix3(c, [0.78, 0.84, 0.95], pr * 0.6);
  return c;
}
// flagstones: irregular stones in mortar, each its own shade, worn pale in the middle
function flags(u, v, s, mortar) {
  const vo = voronoi(u, v, 12, s, 0.9);
  const sid = hash(vo.id, 1, s + 1);
  let c = grey(0.8 + 0.14 * (sid - 0.5) + 0.12 * (fbm(u, v, 14, 4, s + 3) - 0.5));
  c = c.map((k) => k * (1 + 0.08 * smooth(0.35, 0.05, vo.d1)));
  const m = 1 - smooth(0.0, 0.06, vo.edge);
  return mix3(c, mortar, m);
}

const THEMES = {
  meadow: { ground: (u, v) => grass(u, v, 101, [1.0, 0.95, 0.6]), path: (u, v) => dirt(u, v, 111) },
  autumn: { ground: (u, v) => grass(u, v, 201, [1.0, 0.55, 0.3]), path: (u, v) => dirt(u, v, 211) },
  desert: { ground: sand, path: clay },
  snow: { ground: snow, path: trodden },
  volcano: { ground: basalt, path: (u, v) => flags(u, v, 301, [0.45, 0.32, 0.28]) },
  crystal: { ground: (u, v) => grass(u, v, 401, [0.6, 1.0, 0.95]), path: (u, v) => flags(u, v, 411, [0.55, 0.5, 0.65]) },
};

// ── PNG (RGB, 8 bit) ────────────────────────────────────────────────
const CRC = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
function crc32(buf) { let c = 0xffffffff; for (const b of buf) c = CRC[(c ^ b) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'latin1'), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function png(w, h, rgb) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 3 + 1)] = 0; rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3); }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

mkdirSync(out, { recursive: true });
let n = 0;
for (const [theme, fns] of Object.entries(THEMES))
  for (const part of ['ground', 'path']) {
    const px = Buffer.alloc(N * N * 3);
    for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
      const c = fns[part]((x + 0.5) / N, (y + 0.5) / N);
      for (let k = 0; k < 3; k++) px[(y * N + x) * 3 + k] = Math.round(clamp(0.84 + (c[k] - 0.84) * DETAIL) * 255); // the detail pushed: crisp ground up close
    }
    writeFileSync(join(out, `${theme}_${part}.png`), png(N, N, px));
    n++;
  }
console.log(`gen-textures: ${n} sheets into ${out}`);
