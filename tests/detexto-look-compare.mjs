// Detexto's 1-bit looks against another build of the game (games/detexto/src/dither.h), on romdev, GL and WebGPU:
// the same scenes (detexto_auto on a level, the same frame, the same run options) shot on this build's carts and on
// the carts in DETEXTO_OLD_CARTS (another build's build/cart-wgpu/carts). Two measures:
//   tone     the mean difference of the two pictures' brightness over 16x16 pixel blocks (what a dither draws: its
//            pattern averages out), at most --max (default 2% of full scale);
//   pixels   the share of single pixels that differ, reported (two blue noise or two Bayer matrices of another
//            ordering draw the same tone with other dots: about a fifth of the pixels).
// The control that must fail the tone: the other build drawn with a flat tone (DITHER_GAMMA 1, DITHER_CONTRAST 1,
// DITHER_SHARPEN 0, DITHER_OUTLINE 0), at least four times --max. Frames go to out/gate/detexto-compare.
//   DETEXTO_OLD_CARTS=<dir> node tests/detexto-look-compare.mjs [--gpu gl|webgpu|both] [--max 0.02]
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { openCart, ROOT } from './lib/romdev.mjs';
import { readPng } from './lib/png.mjs';

const a = process.argv.slice(2);
const gi = a.indexOf('--gpu');
const gpus = gi >= 0 && a[gi + 1] !== 'both' ? [a[gi + 1]] : ['gl', 'webgpu'];
const mi = a.indexOf('--max');
const MAX = mi >= 0 ? +a[mi + 1] : 0.02;
const OLD = process.env.DETEXTO_OLD_CARTS;
if (!OLD) { console.error('DETEXTO_OLD_CARTS: the other build\'s carts'); process.exit(2); }
const OUT = join(ROOT, 'out/gate/detexto-compare');
mkdirSync(OUT, { recursive: true });
const SCENES = [
  { name: 'first', at: 700, opts: {} },
  { name: 'desert', at: 700, opts: { LEVEL_NAME: 'Great Sphinx' } },
  { name: 'hud', at: 700, opts: {}, hud: true },
];

let fail = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) fail++; };

// the mean |difference| of the green channel's 16x16 block means, 0..1
function tone(p0, p1, k = 16) {
  if (p0.w !== p1.w || p0.h !== p1.h) return 1;
  const W = Math.floor(p0.w / k), H = Math.floor(p0.h / k);
  let sum = 0;
  for (let by = 0; by < H; by++)
    for (let bx = 0; bx < W; bx++) {
      let a = 0, b = 0;
      for (let y = 0; y < k; y++)
        for (let x = 0; x < k; x++) { const i = ((by * k + y) * p0.w + bx * k + x) * 3 + 1; a += p0.rgb[i]; b += p1.rgb[i]; }
      sum += Math.abs(a - b) / (k * k * 255);
    }
  return sum / (W * H);
}
function diff(p0, p1) {
  if (p0.w !== p1.w || p0.h !== p1.h) return 1;
  let n = 0;
  for (let i = 0; i < p0.w * p0.h; i++)
    if (Math.abs(p0.rgb[i * 3] - p1.rgb[i * 3]) > 8 || Math.abs(p0.rgb[i * 3 + 1] - p1.rgb[i * 3 + 1]) > 8 || Math.abs(p0.rgb[i * 3 + 2] - p1.rgb[i * 3 + 2]) > 8) n++;
  return n / (p0.w * p0.h);
}

async function shot(gpu, dir, look, scene, extra = {}) {
  if (dir) process.env.DESTRUCTO_CART_DIR = resolve(dir);
  else delete process.env.DESTRUCTO_CART_DIR;
  const tag = `${dir ? 'old' : 'new'}-${look}-${scene.name}${Object.keys(extra).length ? '-flat' : ''}`;
  const h = await openCart('detexto_auto', { gpu, tag: `cmp-${tag}` });
  delete process.env.DESTRUCTO_CART_DIR;
  try {
    for (const [k, v] of Object.entries({ LOOK: look, ...(scene.hud ? {} : { NO_HUD: 1 }), ...scene.opts, ...extra })) await h.command(`dm set ${k} ${v}`);
    if (scene.opts.LEVEL_NAME) await h.command('dm restart');
    await h.step(scene.at - h.frame);
    const f = join(OUT, `${gpu}-${tag}.png`);
    await h.shot(f);
    return { png: readPng(f), f };
  } finally { await h.close(); }
}

for (const gpu of gpus) {
  for (const scene of SCENES)
    for (const look of ['dither', 'warm', 'grid']) {
      const n = await shot(gpu, null, look, scene), o = await shot(gpu, OLD, look, scene);
      const t = tone(n.png, o.png), d = diff(n.png, o.png);
      check(t <= MAX, `${gpu} ${scene.name} ${look}: tone ${(t * 100).toFixed(2)}% from the other build; ${(d * 100).toFixed(2)}% of single pixels differ (${n.f})`);
    }
  const flat = { DITHER_GAMMA: 1, DITHER_CONTRAST: 1, DITHER_SHARPEN: 0, DITHER_OUTLINE: 0 };
  const n = await shot(gpu, null, 'dither', SCENES[0]), o = await shot(gpu, OLD, 'dither', SCENES[0], flat);
  const t = tone(n.png, o.png);
  check(t >= 4 * MAX, `${gpu}: control (the other build with a flat tone) differs in tone by ${(t * 100).toFixed(2)}%`);
}
console.log(fail ? `detexto-look-compare: ${fail} failed` : 'detexto-look-compare: ok');
process.exit(fail ? 1 : 0);
