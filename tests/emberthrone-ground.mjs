// emberthrone ground check: the terrain's ground has no seams and no visible repeat, in every region, from the game's
// own camera (frames from tests/emberthrone-ground-shots.mjs: romdev, the RELEASE=1 auto cart, its groundcam command).
//
//   seams       td<region>.png, straight down from 14 m (the underway, roofed, 3 m; about 67 px a metre, screen x along world +x, screen y along
//               world +z). A seam is a straight line across the ground where the picture steps. For each boundary
//               between neighbouring pixel columns (and rows): the share of the rows along it where the step across it is
//               the hardest of it and the boundaries 3 px either side. Ground with no seam: about a third, by chance. A
//               seam: most of the column. The wandering edge of a patch of another layer, a shadow or a prop: only a
//               stretch. Reported: the highest share (peak) and the boundaries at 0.55 or more (lines). Pass: none.
//   repetition  wide<region>.png, straight down from 45 m (the underway 4.2 m; about 21 px a metre). Luma, its 2 m mean taken off (light and
//               the layout of the layers are not a repeat), halved to about 10 px a metre; its autocorrelation by FFT,
//               normalized at every lag by the overlap. Reported: the highest correlation at any lag 1.5 m to 15 m off
//               the centre (a tile that comes round again at its period lights that lag up), and where. Pass: under 0.25.
//   edges       the layer textures themselves (assets/textures/terrain/*_ah.png): the luma step across the wrap (last
//               column to first, last row to first) against the mean step between neighbours inside. Pass: under 1.25.
// CONTROL, which must fail: the same measures on the ground as 0.49.0 shipped it (the frames in --control <dir>, and its
// layer textures, --control-textures <dir>), every region; and for the repetition measure, each region's commonest layer
// of 0.49.0 laid plainly at its old scale (0.49.0's frames turn the tile a random sixteenth in every 4 m block, which
// hides a repeat from a translation measure: what it showed was the grid of those blocks, which the seam measure sees).
//
//   node tests/emberthrone-ground.mjs <shots dir> [--control <shots dir>] [--textures <dir>] [--control-textures <dir>] [--json]
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
export const SEAM_PEAK = 0.55, SEAM_LINE = 0.55, REPEAT_MAX = 0.25, EDGE_MAX = 1.25;

/** any 8 or 16-bit PNG (grey, grey+alpha, RGB, RGBA; no palette) as { w, h, ch, px: Float32Array 0..255 } */
export function readPngAny(file) {
  const b = readFileSync(file);
  let w = 0, h = 0, type = 0, depth = 0;
  const idat = [];
  for (let p = 8; p < b.length;) {
    const len = b.readUInt32BE(p), kind = b.toString('latin1', p + 4, p + 8), data = b.subarray(p + 8, p + 8 + len);
    if (kind === 'IHDR') w = data.readUInt32BE(0), h = data.readUInt32BE(4), depth = data[8], type = data[9];
    if (kind === 'IDAT') idat.push(data);
    p += 12 + len;
  }
  const ch = { 0: 1, 4: 2, 2: 3, 6: 4 }[type];
  if (!ch || (depth !== 8 && depth !== 16)) throw new Error(`${file}: PNG type ${type} depth ${depth} is not read here`);
  const bpp = ch * depth / 8, stride = w * bpp, raw = inflateSync(Buffer.concat(idat));
  const out = Buffer.alloc(h * stride);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], row = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1)), o = y * stride;
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? out[o + x - bpp] : 0, up = y ? out[o - stride + x] : 0, c = x >= bpp && y ? out[o - stride + x - bpp] : 0;
      let v = row[x];
      if (f === 1) v += a; else if (f === 2) v += up; else if (f === 3) v += (a + up) >> 1;
      else if (f === 4) { const pp = a + up - c, pa = Math.abs(pp - a), pb = Math.abs(pp - up), pc = Math.abs(pp - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      out[o + x] = v & 255;
    }
  }
  const px = new Float32Array(w * h * ch);
  if (depth === 8) for (let i = 0; i < px.length; i++) px[i] = out[i];
  else for (let i = 0; i < px.length; i++) px[i] = ((out[2 * i] << 8) | out[2 * i + 1]) / 257;
  return { w, h, ch, px };
}
export function luma({ w, h, ch, px }) {
  const L = new Float32Array(w * h);
  for (let i = 0; i < w * h; i++) L[i] = ch < 3 ? px[i * ch] : 0.2126 * px[i * ch] + 0.7152 * px[i * ch + 1] + 0.0722 * px[i * ch + 2];
  return L;
}
const median = (a) => { const s = Float64Array.from(a).sort(); return s.length ? s[s.length >> 1] : 0; };
const trimmedMean = (a, lo = 0.1, hi = 0.9) => { const s = Float64Array.from(a).sort(); const i0 = Math.floor(s.length * lo), i1 = Math.max(i0 + 1, Math.floor(s.length * hi)); let t = 0; for (let i = i0; i < i1; i++) t += s[i]; return t / (i1 - i0); };

/** the seam measure of one straight-down frame */
export function seams(img) {
  const { w, h } = img, L = luma(img);
  // for each boundary between neighbouring columns (rows): the share of rows (columns) where the step across it is the
  // hardest of it and the boundaries 3 px either side. Ground with no seam: about a third (whichever of three is
  // hardest, by chance). A seam steps hard all along: most of them. The edge of a patch of another layer wanders, so it
  // is the hardest at any one boundary only for a stretch.
  const share = (n, m, at) => {
    const out = new Float64Array(n - 1);
    const step = (i, j) => (i < 0 || i >= n - 1 ? 0 : Math.abs(at(i + 1, j) - at(i, j)));
    for (let i = 0; i < n - 1; i++) { let k = 0; for (let j = 0; j < m; j++) { const v = step(i, j); if (v > step(i - 3, j) && v > step(i + 3, j)) k++; } out[i] = k / m; }
    return out;
  };
  const rc = share(w, h, (x, y) => L[y * w + x]), rr = share(h, w, (y, x) => L[y * w + x]);
  const lines = (r) => { const at = []; for (let i = 3; i < r.length - 3; i++) if (r[i] >= SEAM_LINE && r[i] >= r[i - 1] && r[i] >= r[i + 1] && !at.some((j) => Math.abs(j - i) <= 3)) at.push(i); return at; };
  const lc = lines(rc), lr = lines(rr);
  let peak = 0, where = '';
  for (let i = 3; i < rc.length - 3; i++) if (rc[i] > peak) peak = rc[i], where = `column ${i}`;
  for (let i = 3; i < rr.length - 3; i++) if (rr[i] > peak) peak = rr[i], where = `row ${i}`;
  return { peak: +peak.toFixed(3), where, lines: lc.length + lr.length, columns: lc, rows: lr };
}

// ── FFT (radix 2, in place) ──
function fft1(re, im, inv) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
  for (let len = 2; len <= n; len <<= 1) {
    const a = (inv ? 2 : -2) * Math.PI / len, wr = Math.cos(a), wi = Math.sin(a);
    for (let i = 0; i < n; i += len) { let cr = 1, ci = 0; for (let k = 0; k < len / 2; k++) { const ur = re[i + k], ui = im[i + k], vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci, vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr; re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi; const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } }
  }
}
function fft2(re, im, W, H, inv) {
  const rr = new Float64Array(W), ri = new Float64Array(W);
  for (let y = 0; y < H; y++) { for (let x = 0; x < W; x++) rr[x] = re[y * W + x], ri[x] = im[y * W + x]; fft1(rr, ri, inv); for (let x = 0; x < W; x++) re[y * W + x] = rr[x], im[y * W + x] = ri[x]; }
  const cr = new Float64Array(H), ci = new Float64Array(H);
  for (let x = 0; x < W; x++) { for (let y = 0; y < H; y++) cr[y] = re[y * W + x], ci[y] = im[y * W + x]; fft1(cr, ci, inv); for (let y = 0; y < H; y++) re[y * W + x] = cr[y], im[y * W + x] = ci[y]; }
}
const autocorr = (a, W, H) => { const re = Float64Array.from(a), im = new Float64Array(W * H); fft2(re, im, W, H, false); for (let i = 0; i < re.length; i++) re[i] = re[i] * re[i] + im[i] * im[i], im[i] = 0; fft2(re, im, W, H, true); return re; };

/** the repetition measure of one straight-down frame taken pxPerM pixels a metre */
export function repetition(img, pxPerM) {
  const { w, h } = img, L = luma(img);
  // the 2 m mean off (an integral image), then halved
  const r = Math.round(pxPerM), S = new Float64Array((w + 1) * (h + 1));
  for (let y = 0; y < h; y++) for (let x = 0, row = 0; x < w; x++) row += L[y * w + x], S[(y + 1) * (w + 1) + x + 1] = S[y * (w + 1) + x + 1] + row;
  const F = Math.max(2, Math.round(pxPerM / 10)); /* down to about 10 px a metre */
  const hw = Math.floor(w / F), hh = Math.floor(h / F), A = new Float64Array(hw * hh);
  for (let y = 0; y < hh; y++) for (let x = 0; x < hw; x++) {
    let v = 0; for (let k = 0; k < F * F; k++) { const xx = F * x + (k % F), yy = F * y + Math.floor(k / F); const x0 = Math.max(0, xx - r), x1 = Math.min(w, xx + r + 1), y0 = Math.max(0, yy - r), y1 = Math.min(h, yy + r + 1);
      v += L[yy * w + xx] - (S[y1 * (w + 1) + x1] - S[y0 * (w + 1) + x1] - S[y1 * (w + 1) + x0] + S[y0 * (w + 1) + x0]) / ((x1 - x0) * (y1 - y0)); }
    A[y * hw + x] = v / (F * F);
  }
  let mean = 0; for (const v of A) mean += v; mean /= A.length;
  let W = 1, H = 1; while (W < hw * 2) W <<= 1; while (H < hh * 2) H <<= 1;
  const P = new Float64Array(W * H), M = new Float64Array(W * H), Q = new Float64Array(W * H);
  for (let y = 0; y < hh; y++) for (let x = 0; x < hw; x++) { const v = A[y * hw + x] - mean; P[y * W + x] = v; Q[y * W + x] = v * v; M[y * W + x] = 1; }
  const ac = autocorr(P, W, H), cnt = autocorr(M, W, H);
  // normalized: the covariance at a lag over the variance (both over the overlap)
  let vv = 0; for (const v of A) vv += (v - mean) ** 2; vv /= A.length;
  const ppm = pxPerM / F, lo = 1.5 * ppm, hi = 15 * ppm;
  let peak = -1, at = [0, 0];
  for (let dy = -Math.floor(Math.min(hi, hh * 0.6)); dy <= Math.min(hi, hh * 0.6); dy++) for (let dx = 0; dx <= Math.min(hi, hw * 0.6); dx++) {
    const d = Math.hypot(dx, dy); if (d < lo || d > hi || (dx === 0 && dy < 0)) continue;
    const i = ((dy + H) % H) * W + dx, n = cnt[i] / (W * H);
    if (n < A.length * 0.25) continue;
    const c = ac[i] / (W * H) / n / vv;
    if (c > peak) peak = c, at = [dx / ppm, dy / ppm];
  }
  return { peak: +peak.toFixed(3), lag: at.map((v) => +v.toFixed(2)) };
}

/** the wrap measure of one layer texture */
export function edges(img) {
  const { w, h } = img, L = luma(img);
  let ins = 0, n = 0, wrapX = 0, wrapY = 0;
  for (let y = 0; y < h; y++) for (let x = 0; x < w - 1; x++) ins += Math.abs(L[y * w + x + 1] - L[y * w + x]), n++;
  for (let y = 0; y < h - 1; y++) for (let x = 0; x < w; x++) ins += Math.abs(L[(y + 1) * w + x] - L[y * w + x]), n++;
  ins /= n;
  for (let y = 0; y < h; y++) wrapX += Math.abs(L[y * w] - L[y * w + w - 1]);
  for (let x = 0; x < w; x++) wrapY += Math.abs(L[x] - L[(h - 1) * w + x]);
  return { x: +(wrapX / h / ins).toFixed(3), y: +(wrapY / w / ins).toFixed(3) };
}

/** a layer texture laid plainly over the ground (no offsets, no turns: the repeat at its purest), as a straight-down
 * frame of w x h taken pxPerM pixels a metre, the layer repeating every 1 / uvScale metres (4 x 4 samples a pixel) */
export function plainTile(file, uvScale, pxPerM, w = 1920, h = 1080) {
  const t = readPngAny(file), px = new Float32Array(w * h * 3), k = t.w * uvScale / pxPerM; /* texels a pixel */
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) for (let c = 0; c < 3; c++) {
    let v = 0;
    for (let j = 0; j < 16; j++) { const u = Math.floor((x + ((j & 3) + 0.5) / 4) * k) % t.w, vv = Math.floor((y + ((j >> 2) + 0.5) / 4) * k) % t.h; v += t.px[(vv * t.w + u) * t.ch + c]; }
    px[(y * w + x) * 3 + c] = v / 16;
  }
  return { w, h, ch: 3, px };
}
// each region's commonest ground, and the layers' scales as 0.49.0 shipped them (the plain-tile control)
export const REGION_LAYER = { meadow: 'grass', canyon: 'sand', marsh: 'mud', keep: 'ash', heights: 'snow', underway: 'dirt', spire: 'grass', wood: 'grass' };
export const OLD_UV = { grass: 0.22, snow: 0.2, dirt: 0.25, sand: 0.12, rock: 0.1, mud: 0.18, cobble: 0.3, ash: 0.18 };

export function measureShots(dir) {
  const meta = JSON.parse(readFileSync(join(dir, 'shots.json'), 'utf8'));
  const out = {};
  for (const name of Object.keys(meta.spots)) {
    if (!existsSync(join(dir, `td${name}.png`))) continue;
    const ppm = (h) => 540 / (h * Math.tan(Math.PI / 6)); /* fov 60, 1080 lines: pixels a metre on the ground h below */
    out[name] = { seams: seams(readPngAny(join(dir, `td${name}.png`))), repetition: repetition(readPngAny(join(dir, `wide${name}.png`)), ppm(meta.meta?.[name]?.wideH || 45)) };
  }
  return out;
}
export function measureTextures(dir) {
  const out = {};
  for (const f of readdirSync(dir).filter((f) => f.endsWith('_ah.png')).sort()) out[f.replace('_ah.png', '')] = edges(readPngAny(join(dir, f)));
  return out;
}
const verdictShots = (m) => Object.entries(m).map(([k, v]) => ({ region: k, ...v, ok: v.seams.peak < SEAM_PEAK && v.repetition.peak < REPEAT_MAX }));
const verdictTex = (m) => Object.entries(m).map(([k, v]) => ({ layer: k, ...v, ok: v.x < EDGE_MAX && v.y < EDGE_MAX }));

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const argv = process.argv.slice(2);
  const opt = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
  const dir = argv[0], ctl = opt('--control', null);
  const tex = opt('--textures', join(ROOT, 'games/emberthrone/assets/textures/terrain')), ctex = opt('--control-textures', null);
  let bad = 0;
  const show = (tag, rows, ctlRun) => {
    for (const r of rows) {
      const what = r.region ? `${r.region.padEnd(9)} seam peak ${r.seams.peak} (${r.seams.where}) lines ${r.seams.lines}  repeat ${r.repetition.peak} at ${r.repetition.lag.join(',')} m`
        : `${r.layer.padEnd(9)} wrap step x ${r.x} y ${r.y}`;
      const good = ctlRun ? !r.ok : r.ok;
      if (!good) bad++;
      console.log(`${good ? 'ok  ' : 'FAIL'} ${tag} ${what}${ctlRun ? (r.ok ? '  (the control passed: the measure cannot see it)' : '  (control fails, as it must)') : ''}`);
    }
  };
  const res = { shots: verdictShots(measureShots(dir)), textures: verdictTex(measureTextures(tex)) };
  show('ground ', res.shots, false);
  show('texture', res.textures, false);
  if (ctl) { res.control = verdictShots(measureShots(ctl)); show('CONTROL', res.control, true); }
  if (ctex) {
    // the repetition measure must see a repeat: each region's commonest layer of 0.49.0 laid plainly at its old scale
    res.plain = Object.entries(REGION_LAYER).map(([region, layer]) => {
      const r = repetition(plainTile(join(ctex, `${layer}_ah.png`), OLD_UV[layer], 540 / (45 * Math.tan(Math.PI / 6))), 540 / (45 * Math.tan(Math.PI / 6)));
      return { region, layer, ...r, ok: r.peak < REPEAT_MAX };
    });
    for (const r of res.plain) { const good = !r.ok; if (!good) bad++; console.log(`${good ? 'ok  ' : 'FAIL'} CONTROL plain   ${r.region.padEnd(9)} ${r.layer} tiled plainly: repeat ${r.peak} at ${r.lag.join(',')} m${r.ok ? '  (the control passed: the measure cannot see a repeat)' : '  (control fails, as it must)'}`); }
  }
  if (ctex) {
    // the wrap measure must catch the old layers that did not tile (most of them did: their noise wrapped, but the snow's
    // ripples, the path's ruts and the cobbles' cells did not)
    res.controlTextures = verdictTex(measureTextures(ctex));
    const caught = res.controlTextures.filter((r) => !r.ok);
    for (const r of res.controlTextures) console.log(`     CONTROL texture ${r.layer.padEnd(9)} wrap step x ${r.x} y ${r.y}${r.ok ? '' : '  (caught)'}`);
    if (!caught.length) bad++;
    console.log(`${caught.length ? 'ok  ' : 'FAIL'} CONTROL texture: ${caught.length} of the old layers caught as not tiling${caught.length ? ' (control fails, as it must)' : ': the measure cannot see a seam'}`);
  }
  if (argv.includes('--json')) console.log(JSON.stringify(res));
  console.log(bad ? `${bad} failed` : 'ground: all pass');
  process.exit(bad ? 1 : 0);
}
