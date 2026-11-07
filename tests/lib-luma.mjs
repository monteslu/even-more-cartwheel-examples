// The luma of a PNG as the engine writes it (8-bit RGB or RGBA, any filter): mean and standard deviation of Rec. 709
// luma over the sRGB values, 0..255. Used by tests/demolition-look.mjs.
import { readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';

export function luma(file) {
  const b = readFileSync(file);
  let w = 0, h = 0, type = 0, depth = 0;
  const idat = [];
  for (let p = 8; p < b.length;) {
    const len = b.readUInt32BE(p), kind = b.toString('latin1', p + 4, p + 8), data = b.subarray(p + 8, p + 8 + len);
    if (kind === 'IHDR') w = data.readUInt32BE(0), h = data.readUInt32BE(4), depth = data[8], type = data[9];
    if (kind === 'IDAT') idat.push(data);
    p += 12 + len;
  }
  if (depth !== 8 || (type !== 2 && type !== 6)) throw new Error(`${file}: ${depth}-bit colour type ${type}`);
  const bpp = type === 6 ? 4 : 3, stride = w * bpp, raw = inflateSync(Buffer.concat(idat));
  const cur = Buffer.alloc(stride), prev = Buffer.alloc(stride);
  let sum = 0, sum2 = 0;
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], row = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let i = 0; i < stride; i++) {
      const a = i >= bpp ? cur[i - bpp] : 0, up = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      let v = row[i];
      if (f === 1) v += a; else if (f === 2) v += up; else if (f === 3) v += (a + up) >> 1;
      else if (f === 4) { const pp = a + up - c, pa = Math.abs(pp - a), pb = Math.abs(pp - up), pc = Math.abs(pp - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      cur[i] = v & 255;
    }
    for (let x = 0; x < w; x++) {
      const l = 0.2126 * cur[x * bpp] + 0.7152 * cur[x * bpp + 1] + 0.0722 * cur[x * bpp + 2];
      sum += l, sum2 += l * l;
    }
    cur.copy(prev);
  }
  const n = w * h, mean = sum / n;
  return { mean, sd: Math.sqrt(Math.max(0, sum2 / n - mean * mean)) };
}
