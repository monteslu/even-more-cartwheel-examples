#!/usr/bin/env node
// Build the engine's Ogg Vorbis decoder (stb_vorbis + stbdec.c) to WASI, into out/stbdec.wasm, with the wasi-sdk
// the cartwheel cart SDK ships (fetched by scripts/fetch-deps.sh into deps/cartwheel-sdk/cart-wgpu/deps/wasi-sdk):
//
//   node tools/stb-vorbis/build.mjs --stb <path to stb_vorbis.c> [--wasi-sdk <dir>]
//
// stb_vorbis.c is the engine's copy (cartwheel-engine deps/stb/stb_vorbis.c, public domain / MIT); the cart SDK
// links it in but does not ship the source.
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { parseArgs } from 'node:util';

const root = resolve(import.meta.dirname, '..', '..');
const { values: v } = parseArgs({ options: { stb: { type: 'string' }, 'wasi-sdk': { type: 'string' } } });
const sdk = resolve(v['wasi-sdk'] ?? `${root}/../../deps/cartwheel-sdk/cart-wgpu/deps/wasi-sdk`); // the repository's deps/
if (!v.stb || !existsSync(v.stb)) { console.error('--stb <stb_vorbis.c> is required'); process.exit(2); }
if (!existsSync(`${sdk}/bin/clang`)) { console.error(`no wasi-sdk at ${sdk} (run scripts/fetch-deps.sh to fetch the SDK, or pass --wasi-sdk)`); process.exit(2); }
mkdirSync(`${root}/out`, { recursive: true });
execFileSync(`${sdk}/bin/clang`, ['--target=wasm32-wasip1', `--sysroot=${sdk}/share/wasi-sysroot`, '-O2', '-w',
  `-I${dirname(resolve(v.stb))}`, '-o', `${root}/out/stbdec.wasm`, `${import.meta.dirname}/stbdec.c`], { stdio: 'inherit' });
console.log(`built ${root}/out/stbdec.wasm`);
