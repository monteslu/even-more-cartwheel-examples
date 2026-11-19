// ffmpeg compiled to WebAssembly (@ffmpeg/core, the single-thread build), driven in-process from Node: no ffmpeg
// binary on the host. Used for the AAC inside NI Stems files (encode for the test songs, decode for real ones) and
// for the Ogg Opus the game streams. Files go through ffmpeg's in-memory file system.
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';

const require = createRequire(import.meta.url);
const coreDir = join(dirname(dirname(dirname(require.resolve('@ffmpeg/core')))), 'dist', 'esm');
let wasmBinary = null;
// The core is built for browsers and workers; it only needs `self` to exist.
globalThis.self ??= /** @type {any} */ ({ location: { href: import.meta.url } });

/**
 * Run one ffmpeg command on in-memory files. A fresh instance per call: the core is not re-entrant after exit.
 * @param {string[]} args ffmpeg arguments after `ffmpeg -nostdin -y`
 * @param {Record<string, Uint8Array>} inputs files written before the run
 * @param {string[]} outputs files read back after the run
 * @returns {Promise<Record<string, Uint8Array>>}
 */
export async function ffmpeg(args, inputs, outputs) {
  wasmBinary ??= readFileSync(join(coreDir, 'ffmpeg-core.wasm'));
  const { default: createFFmpegCore } = await import(join(coreDir, 'ffmpeg-core.js'));
  const log = [];
  const core = await createFFmpegCore({ wasmBinary, locateFile: (f) => join(coreDir, f) });
  core.setLogger(({ message }) => log.push(message));
  for (const [name, data] of Object.entries(inputs)) core.FS.writeFile(name, new Uint8Array(data));
  const ret = core.exec(...args);
  if (ret !== 0) throw new Error(`ffmpeg ${args.join(' ')} failed (${ret}):\n${log.slice(-20).join('\n')}`);
  /** @type {Record<string, Uint8Array>} */
  const out = {};
  for (const name of outputs) out[name] = core.FS.readFile(name);
  return out;
}
