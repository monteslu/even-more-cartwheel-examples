// A short check of each game's dual cart: its self-playing <game>_auto cart
// runs FRAMES frames headless in wasmcart's Node host (`wasmcart play
// --frames N --shot out.png`) and must exit cleanly, report every frame, and
// leave a picture that is the cart's size and not blank (a cart that renders
// nothing, or only a clear colour, fails; a dithered 1-bit picture, two colours each a real share of the frame,
// is a picture). CI runs it on Mesa's software
// Vulkan (lavapipe), so the cart takes its WebGPU path.
//
//   node tests/cart-smoke.mjs [--frames N] [--carts <dir>] [--out <dir>] [game ...]
//
// WASMCART (default `npx --yes wasmcart@<pinned>`) names the player command.
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, existsSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const VALUED = new Set(['--frames', '--carts', '--out']);
const FRAMES = +opt('--frames', 60);
// a game whose cart draws every world once under a loading screen first runs at least this many frames, so the shot
// is of the game, not the loading bar (beatsync: its style prewarm, about 90 frames)
const LOADING = { beatsync: 150 };
const CARTS = resolve(opt('--carts', join(ROOT, 'build/cart-wgpu/carts')));
const OUT = resolve(opt('--out', join(ROOT, 'out/cart-smoke')));
const PLAYER = (process.env.WASMCART || 'npx --yes wasmcart@0.36.2').split(' ');
const ALL = ['beatsync', 'destructo', 'detexto', 'emberthrone', 'jewels', 'lanternfall', 'pinball', 'skate', 'towers'];
const games = argv.filter((a, i) => !a.startsWith('--') && !VALUED.has(argv[i - 1]));

/** the distinct colours of an RGB PNG as wasmcart writes it (8 bit, filter none on every row) */
function colours(file) {
  const b = readFileSync(file);
  let w = 0, h = 0, type = 0;
  const idat = [];
  for (let p = 8; p < b.length;) {
    const len = b.readUInt32BE(p), kind = b.toString('latin1', p + 4, p + 8), data = b.subarray(p + 8, p + 8 + len);
    if (kind === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); type = data[9]; }
    if (kind === 'IDAT') idat.push(data);
    p += 12 + len;
  }
  if (type !== 2) throw new Error(`${file}: colour type ${type}, expected RGB`);
  const raw = inflateSync(Buffer.concat(idat));
  const seen = new Map();
  for (let y = 0; y < h; y++) {
    const row = y * (1 + w * 3);
    if (raw[row] !== 0) throw new Error(`${file}: row filter ${raw[row]}, expected none`);
    for (let x = 0; x < w; x += 2) { const c = (raw[row + 1 + x * 3] << 16) | (raw[row + 2 + x * 3] << 8) | raw[row + 3 + x * 3]; seen.set(c, (seen.get(c) || 0) + 1); }
  }
  // the two commonest colours' shares (a 1-bit look: ink and paper make nearly all of it)
  const top = [...seen.values()].sort((p, q) => q - p), all = top.reduce((p, q) => p + q, 0);
  return { w, h, n: seen.size, ink: (top[0] || 0) / all, paper: (top[1] || 0) / all };
}

mkdirSync(OUT, { recursive: true });
let failed = 0;
for (const game of games.length ? games : ALL) {
  // the self-playing cart where the game has one, else its own cart
  const auto = join(CARTS, `${game}_auto.wasc`);
  const cart = existsSync(auto) ? auto : join(CARTS, `${game}.wasc`), shot = join(OUT, `${game}.png`);
  if (!existsSync(cart)) { console.error(`${game}: no ${cart}`); failed++; continue; }
  const t0 = Date.now();
  const frames = Math.max(FRAMES, LOADING[game] || 0);
  const r = spawnSync(PLAYER[0], [...PLAYER.slice(1), 'play', cart, '--frames', String(frames), '--shot', shot],
    { encoding: 'utf8', timeout: +(process.env.CW_RUN_TIMEOUT_MS || 900000), maxBuffer: 64 << 20 });
  const secs = ((Date.now() - t0) / 1000).toFixed(1);
  const out = `${r.stdout || ''}${r.stderr || ''}`;
  const problems = [];
  if (r.error || r.status !== 0) problems.push(r.error ? r.error.message : `exited ${r.status ?? r.signal}`);
  const ran = /ran (\d+) frames\s+(\d+)x(\d+)/.exec(r.stdout || '');
  if (!ran || +ran[1] !== frames) problems.push(`did not report ${frames} frames`);
  if (!/render pipeline \(WebGPU\)/.test(out)) problems.push('did not start its WebGPU pipeline');
  if (/no program and are not drawn|\[error\]/.test(out)) problems.push('logged an error');
  let pic = null;
  if (existsSync(shot)) {
    pic = colours(shot);
    if (ran && (pic.w !== +ran[2] || pic.h !== +ran[3])) problems.push(`picture ${pic.w}x${pic.h}, the cart ran at ${ran[2]}x${ran[3]}`);
    // a picture, or a dithered 1-bit picture (two colours, each a real share of the frame: Detexto's default look)
    const oneBit = pic.ink + pic.paper >= 0.9 && pic.paper >= 0.05;
    if (pic.n < 1000 && !oneBit) problems.push(`picture has only ${pic.n} colours (blank or nearly)`);
  } else problems.push('wrote no picture');
  if (problems.length) {
    failed++;
    console.error(`${game}: FAIL (${secs} s)\n  ${problems.join('\n  ')}\n${out.split('\n').filter((l) => !/\[debug\]/.test(l)).slice(-15).join('\n')}`);
  } else console.log(`${game}: ok (${frames} frames, ${pic.w}x${pic.h}, ${pic.n} colours${pic.n < 1000 ? `, 1-bit: the two commonest ${(100 * (pic.ink + pic.paper)).toFixed(1)}%, the second ${(100 * pic.paper).toFixed(1)}%` : ''}, ${secs} s)`);
}
process.exit(failed ? 1 : 0);
