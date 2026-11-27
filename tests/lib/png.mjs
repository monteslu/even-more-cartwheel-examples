// A PNG as the engine and romdev write it (8-bit RGB or RGBA, any filter), decoded to RGB bytes.
import { readFileSync, writeFileSync } from 'node:fs';
import { inflateSync, deflateSync } from 'node:zlib';

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
  if (depth !== 8 || (type !== 2 && type !== 6)) throw new Error(`${file}: ${depth}-bit colour type ${type}`);
  const bpp = type === 6 ? 4 : 3, stride = w * bpp, raw = inflateSync(Buffer.concat(idat));
  const rgb = Buffer.alloc(w * h * 3), cur = Buffer.alloc(stride), prev = Buffer.alloc(stride);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], row = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let i = 0; i < stride; i++) {
      const a = i >= bpp ? cur[i - bpp] : 0, up = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      let v = row[i];
      if (f === 1) v += a; else if (f === 2) v += up; else if (f === 3) v += (a + up) >> 1;
      else if (f === 4) { const pp = a + up - c, pa = Math.abs(pp - a), pb = Math.abs(pp - up), pc = Math.abs(pp - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      cur[i] = v & 255;
    }
    for (let x = 0; x < w; x++) for (let k = 0; k < 3; k++) rgb[(y * w + x) * 3 + k] = cur[x * bpp + k];
    cur.copy(prev);
  }
  return { w, h, rgb };
}

// An 8-bit RGB PNG of w x h from rgb bytes (no filter): for crops on a host without ImageMagick.
const CRC = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
function chunk(kind, data) {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0);
  out.write(kind, 4, 'latin1');
  data.copy(out, 8);
  let c = -1;
  for (let i = 4; i < 8 + data.length; i++) c = CRC[(c ^ out[i]) & 255] ^ (c >>> 8);
  out.writeInt32BE(~c, 8 + data.length);
  return out;
}
export function writePng(file, w, h, rgb) {
  const raw = Buffer.alloc(h * (w * 3 + 1));
  for (let y = 0; y < h; y++) rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
  const hd = Buffer.alloc(13);
  hd.writeUInt32BE(w, 0), hd.writeUInt32BE(h, 4), hd[8] = 8, hd[9] = 2;
  writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', hd), chunk('IDAT', deflateSync(raw, { level: 6 })), chunk('IEND', Buffer.alloc(0))]));
}
// a crop (x, y, cw, ch) of an rgb image of width w
export function cropRgb(rgb, w, x, y, cw, ch) {
  const out = Buffer.alloc(cw * ch * 3);
  for (let r = 0; r < ch; r++) rgb.copy(out, r * cw * 3, ((y + r) * w + x) * 3, ((y + r) * w + x + cw) * 3);
  return out;
}
// images of one size side by side, `cols` a row, 2 px of #111 between them
export function montageRgb(imgs, cw, ch, cols) {
  const rows = Math.ceil(imgs.length / cols), W = cols * (cw + 4), H = rows * (ch + 4), out = Buffer.alloc(W * H * 3, 0x11);
  imgs.forEach((im, i) => {
    const ox = (i % cols) * (cw + 4) + 2, oy = Math.floor(i / cols) * (ch + 4) + 2;
    for (let r = 0; r < ch; r++) im.copy(out, ((oy + r) * W + ox) * 3, r * cw * 3, (r + 1) * cw * 3);
  });
  return { w: W, h: H, rgb: out };
}
