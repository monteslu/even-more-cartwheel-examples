// The engine's own Ogg Vorbis decoder (stb_vorbis, driven as cartwheel's src/audio/decode.c drives it), built to
// WASI by tools/stb-vorbis/build.mjs and run here under node:wasi. Optional: when the build is missing (no cartwheel
// SDK fetched yet), `stbAvailable()` is false and the checks say the engine-decoder leg was skipped.
import { existsSync, readFileSync, writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { join, resolve } from 'node:path';

export const STB_WASM = resolve(import.meta.dirname, '..', '..', 'out', 'stbdec.wasm');
let mod = null;

export function stbAvailable() { return existsSync(STB_WASM); }

/**
 * Decode Ogg Vorbis bytes with stb_vorbis.
 * @param {Uint8Array} bytes
 * @returns {Promise<{rate: number, ch: Float32Array[], streamLength: number}>}
 */
export async function decodeVorbisStb(bytes) {
  mod ??= await WebAssembly.compile(readFileSync(STB_WASM));
  const io = resolve(import.meta.dirname, '..', '..', 'out', `stb-io-${process.pid}`);
  mkdirSync(io, { recursive: true });
  writeFileSync(join(io, 'in.ogg'), bytes);
  const out = join(io, 'stdout.txt');
  const { openSync, closeSync } = await import('node:fs');
  const fd = openSync(out, 'w');
  try {
    const warn = process.emitWarning;
    process.emitWarning = () => {}; // node:wasi's experimental notice
    const { WASI } = await import('node:wasi');
    const wasi = new WASI({ version: 'preview1', args: ['stbdec', '/io/in.ogg', '/io/out.f32'], preopens: { '/io': io }, stdout: fd, returnOnExit: true });
    process.emitWarning = warn;
    const inst = await WebAssembly.instantiate(mod, wasi.getImportObject());
    const code = wasi.start(inst);
    if (code !== 0) throw new Error(`stbdec exited ${code}`);
  } finally { closeSync(fd); }
  const [chs, rate, frames, streamLength] = readFileSync(out, 'utf8').trim().split(/\s+/).map(Number);
  const raw = readFileSync(join(io, 'out.f32'));
  const f = new Float32Array(raw.buffer, raw.byteOffset, raw.length / 4);
  const ch = Array.from({ length: chs }, () => new Float32Array(frames));
  for (let i = 0; i < frames; i++) for (let c = 0; c < chs; c++) ch[c][i] = f[i * chs + c];
  rmSync(io, { recursive: true, force: true });
  return { rate, ch, streamLength };
}
