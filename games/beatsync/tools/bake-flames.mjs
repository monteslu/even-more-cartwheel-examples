// Bakes the fire backdrop's flame flipbook: one tongue of flame over its life (8 x 8 frames of 128 x 128) with its
// motion-vector sheet, from noise carried up a known flow, as cartwheel-engine's tools/fx/bake-flipbooks.mjs bakes its
// fire puffs. A tongue is born at the foot of the cell, stretches up as a tall licking flame with a white-hot core,
// sways, pinches off a tip that rises and cools, and dies down. Many of them side by side, overlapping additively, make
// a wall of flame that licks upward (backdrops/fire.cwscene). Everything is generated here: no source images, CC0.
//
//   node games/beatsync/tools/bake-flames.mjs [out dir]     (default games/beatsync/fx): flame.png, flame_mv.png, bubble.png
//
// Sheets: frame f at column f % 8, row floor(f / 8), row 0 at the top (GPUParticles3D hframes / vframes order);
// colour straight RGBA; motion in red and green as 0.5 + 0.5 * motion / MV_MAX per frame step (x right, y down),
// MV_MAX 0.12 of a frame (motion_strength 1).
import { writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const N = 8, F = 128, FRAMES = N * N, MV_MAX = 0.12;
const out = process.argv[2] || join(dirname(fileURLToPath(import.meta.url)), '../fx');
mkdirSync(out, { recursive: true });

function writePng(file, w, h, rgba) {
  const crcT = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
  const crc = (b) => { let c = -1; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; };
  const chunk = (type, data) => { const l = Buffer.alloc(4); l.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; Buffer.from(rgba.buffer, y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1); }
  writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]));
}

const hash = (x, y, s) => { let h = (Math.imul(x, 374761393) + Math.imul(y, 668265263) + Math.imul(s, 2246822519)) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; };
const sm = (t) => t * t * (3 - 2 * t);
function vnoise(x, y, s) {
  const xi = Math.floor(x), yi = Math.floor(y), fx = sm(x - xi), fy = sm(y - yi);
  const a = hash(xi, yi, s), b = hash(xi + 1, yi, s), c = hash(xi, yi + 1, s), d = hash(xi + 1, yi + 1, s);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
const fbm = (x, y, s, oct = 5) => { let v = 0, a = 0.5, f = 1; for (let i = 0; i < oct; i++) { v += a * vnoise(x * f, y * f, s + i); f *= 2.03; a *= 0.5; } return v / 0.96875; };
const clamp = (v, a = 0, b = 1) => Math.min(b, Math.max(a, v));
const smooth = (e0, e1, x) => sm(clamp((x - e0) / (e1 - e0)));

// height above the cell's foot (0 at the bottom, 1 at the top) from image y (down)
const H = (qy) => 1 - qy;
// the flow, image units per whole life (y down): hot gas rising faster the higher it is, a sway that grows with height
function flow(qx, qy) {
  const h = H(qy);
  const up = 0.55 + 1.1 * h;
  const sway = 0.18 * h * Math.sin(h * 5.0 + 1.3) + (fbm(qx * 3 + 11, qy * 3 + 5, 90, 3) - 0.5) * 0.25 * h;
  return [sway, -up];
}

function density(qx, qy, u) {
  const h = H(qy);
  // the noise this pixel shows was carried up the flow from below: trace it back
  const [vx, vy] = flow(qx, qy);
  const px = qx - vx * u, py = qy - vy * u;
  const n = fbm(px * 3.2 + 7, py * 1.7 + 3, 5);                  /* tall licks: stretched up */
  const n2 = fbm(px * 7 + 2, py * 3.5 + 9, 21, 4);
  // the tongue: tall when grown (a life's first fifth), its foot wide, narrowing to a point that sways
  const grow = smooth(0, 0.22, u), dieDown = 1 - smooth(0.62, 1.0, u);
  const top = 0.3 + 0.62 * grow * (0.7 + 0.3 * dieDown);
  const sway = 0.07 * Math.sin(u * 9.4 + h * 4) * h + 0.05 * Math.sin(u * 4.1 + 2) * h * h;
  // ragged: the noise bends the tongue's sides and lifts or cuts its top (more so higher up)
  const hr = h + (n2 - 0.5) * 0.32 * (0.3 + h) - (n - 0.5) * 0.12;
  const width = 0.21 * Math.pow(clamp(1 - hr / top), 1.25) * (0.75 + 0.25 * smooth(-0.05, 0.12, h)) + 0.004;
  const dx = Math.abs(qx - 0.5 - sway - (n - 0.5) * 0.3 * (0.15 + h)) / width;
  let body = hr < top ? 1 - dx : -1;
  // splits: a cool streak up the middle of the upper tongue now and then makes two licks of one
  body -= 0.6 * smooth(0.5, 0.9, n2) * smooth(top * 0.35, top * 0.8, h) * Math.exp(-dx * dx * 6);
  // a tip pinched off, rising above the body over the second half of the life
  const tipC = top + 0.04 + 0.42 * smooth(0.35, 1.0, u), tipR = 0.07 * (1 - 0.6 * smooth(0.5, 1, u));
  const tip = (1 - Math.hypot((qx - 0.5 - sway * 1.4 - (n - 0.5) * 0.15) / (tipR * 0.7), (hr - tipC) / (tipR * 1.8))) * smooth(0.3, 0.45, u) * (1 - smooth(0.85, 1, u));
  const d = Math.max(body, tip) * (0.75 + 0.5 * n);
  const foot = smooth(0, 0.12, h);                               /* the cell's foot: no hard line */
  const edge = smooth(0, 0.05, Math.min(qx, 1 - qx, qy));
  return clamp(d * 2.2) * Math.max(dieDown, 0) * foot * edge;
}

// heat to colour: deep red, orange, yellow, a white core
const ramp = [[0, [0.05, 0.005, 0.0]], [0.2, [0.5, 0.05, 0.01]], [0.42, [0.95, 0.28, 0.03]], [0.65, [1, 0.62, 0.12]], [0.85, [1, 0.9, 0.5]], [1, [1, 0.98, 0.9]]];
function fireColor(t) {
  t = clamp(t);
  for (let i = 1; i < ramp.length; i++) if (t <= ramp[i][0]) {
    const [t0, c0] = ramp[i - 1], [t1, c1] = ramp[i], k = (t - t0) / (t1 - t0);
    return [c0[0] + (c1[0] - c0[0]) * k, c0[1] + (c1[1] - c0[1]) * k, c0[2] + (c1[2] - c0[2]) * k];
  }
  return ramp[ramp.length - 1][1];
}

const W = N * F, col = new Uint8Array(W * W * 4), mv = new Uint8Array(W * W * 4);
for (let f = 0; f < FRAMES; f++) {
  const u = f / (FRAMES - 1), ox = (f % N) * F, oy = Math.floor(f / N) * F;
  for (let y = 0; y < F; y++) for (let x = 0; x < F; x++) {
    const qx = (x + 0.5) / F, qy = (y + 0.5) / F, h = H(qy);
    const d = density(qx, qy, u);
    // hottest low and in the middle: the core near the foot, cooling up the tongue and toward its edges
    const heat = clamp(d * (1.1 - 0.8 * h) * (0.95 - 0.3 * u) * (0.8 + 0.4 * fbm(qx * 6, qy * 3 + u * 4, 61, 3)));
    const [r, g, b] = fireColor(heat);
    const a = smooth(0.03, 0.3, d);
    const i = ((oy + y) * W + ox + x) * 4;
    col[i] = Math.round(clamp(r) * 255); col[i + 1] = Math.round(clamp(g) * 255); col[i + 2] = Math.round(clamp(b) * 255); col[i + 3] = Math.round(clamp(a) * 255);
    const [vx, vy] = flow(qx, qy), s = 1 / (FRAMES - 1);
    mv[i] = Math.round(clamp(0.5 + 0.5 * (vx * s) / MV_MAX) * 255);
    mv[i + 1] = Math.round(clamp(0.5 + 0.5 * (vy * s) / MV_MAX) * 255);
    mv[i + 2] = 128; mv[i + 3] = 255;
  }
}
writePng(join(out, 'flame.png'), W, W, col);
writePng(join(out, 'flame_mv.png'), W, W, mv);
console.log(`flame.png, flame_mv.png (${W}x${W}, ${FRAMES} frames) in ${out}`);

// the underwater backdrop's bubble (backdrops/deep.cwscene): a clear sphere seen against the light, its rim bright and a
// small highlight up and to the left, white on transparent
{
  const S = 64, px = new Uint8Array(S * S * 4);
  for (let y = 0; y < S; y++) for (let x = 0; x < S; x++) {
    const qx = (x + 0.5) / S * 2 - 1, qy = (y + 0.5) / S * 2 - 1, r = Math.hypot(qx, qy);
    const rim = smooth(0.55, 0.85, r) * (1 - smooth(0.88, 0.98, r));
    const hi = Math.exp(-(((qx + 0.32) ** 2 + (qy + 0.32) ** 2) / 0.02));
    const a = clamp(rim * 0.9 + hi + 0.08 * (1 - smooth(0.85, 0.98, r)));
    const i = (y * S + x) * 4;
    px[i] = px[i + 1] = px[i + 2] = 255; px[i + 3] = Math.round(a * 255);
  }
  writePng(join(out, 'bubble.png'), S, S, px);
  console.log('bubble.png (64x64)');
}
