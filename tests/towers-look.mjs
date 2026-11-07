// towers look check: the value and colour of play frames, one map a theme (no HUD, no cursors, 1920x1080), against
// the art direction (games/towers/DESIGN.md, the look): mid-key, never dark and never washed out, real contrast, and
// more than one hue family on screen.
//   mean luma 90 to 135 of 255 (the deliberately darker themes, volcano and crystal, from 75)
//   luma standard deviation at least 55 (a punchy frame), and at least 13 of local contrast (the mean absolute
//   difference of luma from its 33 x 33 neighbourhood's mean, about a metre and a half: crisp edges, modelled shapes
//   and textured ground, not a global curve)
//   a complementary accent: 4% or more of the saturated pixels 90 degrees or more round the hue wheel from the
//   commonest hue (no one-hue map)
//   saturation: mean 0.25 to 0.62 (colourful, not neon)
// CONTROL, which must fail: the same frames flattened halfway to their own mean (a washed-out grade).
//
//   node tests/towers-look.mjs <dir of m<n>.png frames> [--json]
//   frames: out/shots.sh-style captures (TOWERS_NO_HUD=1 TOWERS_NO_CURSORS=1 TOWERS_WAVE=14, maps 3 8 13 18 23 28)
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { inflateSync } from 'node:zlib';

const THEME = (n) => ['meadow', 'autumn', 'desert', 'snow', 'volcano', 'crystal'][Math.floor((n - 1) / 5) % 6];
const DARKER = new Set(['volcano', 'crystal']);

/** an 8-bit RGB or RGBA PNG (any filter) as { w, h, px: RGB bytes } */
export function readPng(file) {
  const b = readFileSync(file);
  let w = 0, h = 0, type = 0, depth = 0;
  const idat = [];
  for (let p = 8; p < b.length;) {
    const len = b.readUInt32BE(p), kind = b.toString('latin1', p + 4, p + 8), data = b.subarray(p + 8, p + 8 + len);
    if (kind === 'IHDR') w = data.readUInt32BE(0), h = data.readUInt32BE(4), depth = data[8], type = data[9];
    if (kind === 'IDAT') idat.push(data);
    p += 12 + len;
  }
  if (depth !== 8 || (type !== 2 && type !== 6)) throw new Error(`${file}: an 8-bit RGB or RGBA PNG is needed (type ${type}, depth ${depth})`);
  const bpp = type === 6 ? 4 : 3, stride = w * bpp, raw = inflateSync(Buffer.concat(idat));
  const out = Buffer.alloc(w * h * bpp);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], row = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? out[y * stride + x - bpp] : 0, up = y ? out[(y - 1) * stride + x] : 0, c = x >= bpp && y ? out[(y - 1) * stride + x - bpp] : 0;
      let v = row[x];
      if (f === 1) v += a; else if (f === 2) v += up; else if (f === 3) v += (a + up) >> 1;
      else if (f === 4) { const pp = a + up - c, pa = Math.abs(pp - a), pb = Math.abs(pp - up), pc = Math.abs(pp - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      out[y * stride + x] = v & 255;
    }
  }
  if (bpp === 3) return { w, h, px: out };
  const rgb = Buffer.alloc(w * h * 3);
  for (let i = 0; i < w * h; i++) rgb[i * 3] = out[i * 4], rgb[i * 3 + 1] = out[i * 4 + 1], rgb[i * 3 + 2] = out[i * 4 + 2];
  return { w, h, px: rgb };
}

/** the frame's numbers (flatten: lerp each pixel toward the frame mean by that much, for the control) */
export function measure({ w, h, px }, flatten = 0) {
  const n = w * h, L = new Float32Array(n);
  let sum = 0, sat = 0, nsat = 0;
  const sectors = new Array(12).fill(0);
  const m = [0, 0, 0];
  if (flatten) for (let i = 0; i < n; i++) for (let k = 0; k < 3; k++) m[k] += px[i * 3 + k] / n;
  for (let i = 0; i < n; i++) {
    let r = px[i * 3], g = px[i * 3 + 1], b = px[i * 3 + 2];
    if (flatten) r += (m[0] - r) * flatten, g += (m[1] - g) * flatten, b += (m[2] - b) * flatten;
    const l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    L[i] = l, sum += l;
    const mx = Math.max(r, g, b), mn = Math.min(r, g, b), s = mx > 0 ? (mx - mn) / mx : 0;
    sat += s;
    if (s > 0.2 && mx > 40) {
      let hue;
      const d = mx - mn;
      if (mx === r) hue = ((g - b) / d + 6) % 6; else if (mx === g) hue = (b - r) / d + 2; else hue = (r - g) / d + 4;
      sectors[Math.floor(hue * 2) % 12]++, nsat++;
    }
  }
  const mean = sum / n;
  let v = 0;
  for (let i = 0; i < n; i++) v += (L[i] - mean) ** 2;
  // local contrast on a 1/4 grid: |luma - mean of its 33 x 33 neighbourhood| (an integral image)
  const S = new Float64Array((w + 1) * (h + 1));
  for (let y = 0; y < h; y++) for (let x = 0, row = 0; x < w; x++) row += L[y * w + x], S[(y + 1) * (w + 1) + x + 1] = S[y * (w + 1) + x + 1] + row;
  let lc = 0, nl = 0;
  const R = 16;
  for (let y = R; y < h - R - 1; y += 4)
    for (let x = R; x < w - R - 1; x += 4) {
      const a = S[(y - R) * (w + 1) + x - R], b2 = S[(y - R) * (w + 1) + x + R + 1], c = S[(y + R + 1) * (w + 1) + x - R], d = S[(y + R + 1) * (w + 1) + x + R + 1];
      lc += Math.abs(L[y * w + x] - (d - b2 - c + a) / ((2 * R + 1) * (2 * R + 1))), nl++;
    }
  // the accent: the share of the saturated pixels 90 degrees or more round the hue wheel from the commonest hue
  const top = sectors.indexOf(Math.max(...sectors));
  let far = 0;
  for (let k = 0; k < 12; k++) if (Math.min((k - top + 12) % 12, (top - k + 12) % 12) >= 3) far += sectors[k];
  return { mean, sd: Math.sqrt(v / n), local: lc / nl, sat: sat / n, hues: sectors.filter((c) => c >= nsat * 0.05).length, accent: nsat ? far / nsat : 0 };
}

export function verdict(theme, r) {
  const bad = [];
  const lo = DARKER.has(theme) ? 75 : 90;
  if (r.mean < lo || r.mean > 135) bad.push(`mean luma ${r.mean.toFixed(0)} (${lo} to 135)`);
  if (r.sd < 55) bad.push(`luma sd ${r.sd.toFixed(1)} (55 or more)`);
  if (r.local < 13) bad.push(`local contrast ${r.local.toFixed(1)} (13 or more)`);
  if (r.accent < 0.04) bad.push(`complementary accent ${(r.accent * 100).toFixed(1)}% (4% or more)`);
  if (r.sat < 0.25 || r.sat > 0.62) bad.push(`saturation ${r.sat.toFixed(2)} (0.25 to 0.62)`);
  return bad;
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const dir = process.argv[2];
  if (!dir) { console.error('usage: node tests/towers-look.mjs <frames dir> [--json]'); process.exit(2); }
  const files = readdirSync(dir).filter((f) => /^m\d+\.png$/.test(f)).sort((a, b) => parseInt(a.slice(1)) - parseInt(b.slice(1)));
  if (!files.length) { console.error(`no m<n>.png frames in ${dir}`); process.exit(2); }
  let failed = 0, controlCaught = 0;
  const rows = [];
  for (const f of files) {
    const n = parseInt(f.slice(1)), theme = THEME(n), img = readPng(join(dir, f));
    const r = measure(img), bad = verdict(theme, r);
    rows.push({ map: n, theme, ...r, bad });
    console.log(`${bad.length ? 'FAIL' : 'ok  '} map ${n} ${theme.padEnd(8)} mean ${r.mean.toFixed(1)} sd ${r.sd.toFixed(1)} local ${r.local.toFixed(1)} sat ${r.sat.toFixed(2)} accent ${(r.accent * 100).toFixed(1)}%${bad.length ? `: ${bad.join('; ')}` : ''}`);
    if (bad.length) failed++;
    const c = verdict(theme, measure(img, 0.5));
    if (c.length) controlCaught++;
  }
  if (controlCaught === files.length) console.log(`ok   control: every frame flattened halfway to its mean fails`);
  else { console.log(`CONTROL PASSED (a failure): ${files.length - controlCaught} flattened frame(s) still pass`); failed++; }
  if (process.argv.includes('--json')) console.log(JSON.stringify(rows));
  process.exit(failed ? 1 : 0);
}
