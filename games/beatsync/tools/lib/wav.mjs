// WAV files: 16-bit PCM or 32-bit float in, 16-bit PCM out. Audio in the tools is planar Float32Array channels.

/**
 * @typedef {object} Audio
 * @property {number} rate sample rate in Hz
 * @property {Float32Array[]} ch one array per channel, all the same length
 */

/**
 * Encode planar float audio as a 16-bit PCM WAV (dithered rounding is not worth it here: plain clamp and round).
 * @param {Audio} a
 * @returns {Uint8Array}
 */
export function encodeWav(a) {
  const nch = a.ch.length, n = a.ch[0].length;
  const buf = new ArrayBuffer(44 + n * nch * 2);
  const v = new DataView(buf);
  const str = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
  str(0, 'RIFF'); v.setUint32(4, 36 + n * nch * 2, true); str(8, 'WAVE');
  str(12, 'fmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, nch, true);
  v.setUint32(24, a.rate, true); v.setUint32(28, a.rate * nch * 2, true); v.setUint16(32, nch * 2, true);
  v.setUint16(34, 16, true); str(36, 'data'); v.setUint32(40, n * nch * 2, true);
  let o = 44;
  for (let i = 0; i < n; i++) {
    for (let c = 0; c < nch; c++) {
      const s = Math.max(-1, Math.min(1, a.ch[c][i]));
      v.setInt16(o, Math.round(s * 32767), true);
      o += 2;
    }
  }
  return new Uint8Array(buf);
}

/**
 * Decode a PCM (8/16/24/32-bit int) or IEEE float WAV.
 * @param {Uint8Array} bytes
 * @returns {Audio}
 */
export function decodeWav(bytes) {
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const tag = (o) => String.fromCharCode(bytes[o], bytes[o + 1], bytes[o + 2], bytes[o + 3]);
  if (tag(0) !== 'RIFF' || tag(8) !== 'WAVE') throw new Error('not a WAV file');
  let o = 12, fmt = null;
  while (o + 8 <= bytes.length) {
    const id = tag(o), size = v.getUint32(o + 4, true), body = o + 8;
    if (id === 'fmt ') {
      fmt = { format: v.getUint16(body, true), nch: v.getUint16(body + 2, true), rate: v.getUint32(body + 4, true),
        bits: v.getUint16(body + 14, true) };
      if (fmt.format === 0xfffe) fmt.format = v.getUint16(body + 24, true);
    } else if (id === 'data') {
      if (!fmt) throw new Error('WAV data before fmt');
      const bps = fmt.bits / 8, n = Math.floor(Math.min(size, bytes.length - body) / (bps * fmt.nch));
      const ch = Array.from({ length: fmt.nch }, () => new Float32Array(n));
      for (let i = 0; i < n; i++) {
        for (let c = 0; c < fmt.nch; c++) {
          const p = body + (i * fmt.nch + c) * bps;
          let s;
          if (fmt.format === 3) s = bps === 4 ? v.getFloat32(p, true) : v.getFloat64(p, true);
          else if (bps === 2) s = v.getInt16(p, true) / 32768;
          else if (bps === 3) s = ((v.getUint8(p) | (v.getUint8(p + 1) << 8) | (v.getInt8(p + 2) << 16))) / 8388608;
          else if (bps === 4) s = v.getInt32(p, true) / 2147483648;
          else s = (v.getUint8(p) - 128) / 128;
          ch[c][i] = s;
        }
      }
      return { rate: fmt.rate, ch };
    }
    o = body + size + (size & 1);
  }
  throw new Error('WAV without data');
}

/**
 * Average the channels to mono.
 * @param {Audio} a
 * @returns {Float32Array}
 */
export function mono(a) {
  if (a.ch.length === 1) return a.ch[0];
  const n = a.ch[0].length, out = new Float32Array(n), k = 1 / a.ch.length;
  for (const c of a.ch) for (let i = 0; i < n; i++) out[i] += c[i] * k;
  return out;
}
