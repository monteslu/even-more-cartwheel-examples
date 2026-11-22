// Detexto's 1-bit looks (games/detexto/src/dither.h), on romdev, GL and WebGPU (the one cart on each):
//   two colours  for each look (dither, warm, grid) the frame without the HUD is the look's ink and paper only (at
//                least 99.5% of the pixels within 6 of one of the two); control that must fail: the pass off
//                (DITHER_OFF 1), the plain picture;
//   the look     dither and grid draw different patterns from the same picture (blue noise against Bayer), and warm
//                is the dither's pattern in its warm pair;
//   no crawl     with the camera still (the level held, HOLD 1), the share of pixels that change between frames is at
//                most 0.5% (the grass in the wind, a crystal's pulse); control that must fail: the threshold moved
//                every frame (DITHER_CRAWL 1), at least ten times as much;
//   settings     on the game cart the Settings screen has the row "Look": right steps Dither, Dither warm, Dither
//                grid, ASCII and stops at the end (the look follows at once, ASCII puts the scene back at 35%);
//                control: left on the first choice leaves it.
//   node tests/detexto-dither.mjs [--gpu gl|webgpu|both] [--only colours,crawl,settings]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, call, ROOT } from './lib/romdev.mjs';
import { readPng } from './lib/png.mjs';

const a = process.argv.slice(2);
const gi = a.indexOf('--gpu');
const gpus = gi >= 0 && a[gi + 1] !== 'both' ? [a[gi + 1]] : ['gl', 'webgpu'];
const oi = a.indexOf('--only');
const only = oi >= 0 ? a[oi + 1].split(',') : ['colours', 'crawl', 'settings'];
const OUT = join(ROOT, 'out/gate/detexto-dither');
mkdirSync(OUT, { recursive: true });
const AT = 700; // detexto_auto on its first level, aiming
const INK = { dither: [[255, 255, 255], [0, 0, 0]], grid: [[255, 255, 255], [0, 0, 0]], warm: [[255, 230, 189], [9, 8, 6]] };

let fail = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) fail++; };

// the share of pixels within tol of one of the colours
function twoColour(png, cols, tol = 6) {
  const { w, h, rgb } = png;
  let n = 0;
  for (let i = 0; i < w * h; i++) {
    const r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
    if (cols.some((c) => Math.abs(r - c[0]) <= tol && Math.abs(g - c[1]) <= tol && Math.abs(b - c[2]) <= tol)) n++;
  }
  return n / (w * h);
}
// the share of pixels whose side of mid grey differs between two frames
function flips(p0, p1) {
  let c = 0;
  const n = p0.w * p0.h;
  for (let i = 0; i < n; i++) {
    const l0 = p0.rgb[i * 3] * 54 + p0.rgb[i * 3 + 1] * 183 + p0.rgb[i * 3 + 2] * 19;
    const l1 = p1.rgb[i * 3] * 54 + p1.rgb[i * 3 + 1] * 183 + p1.rgb[i * 3 + 2] * 19;
    if ((l0 > 32640) !== (l1 > 32640)) c++;
  }
  return c / n;
}

async function frames(gpu, opts, tag, at = AT, after = [1]) {
  const h = await openCart('detexto_auto', { gpu, tag: `dither-${tag}` });
  try {
    for (const [k, v] of Object.entries(opts)) await h.command(`dm set ${k} ${v}`);
    await h.step(at - h.frame);
    const out = [];
    const f0 = join(OUT, `${gpu}-${tag}-0.png`);
    await h.shot(f0);
    out.push(readPng(f0));
    for (const d of after) {
      await h.step(d);
      const f = join(OUT, `${gpu}-${tag}-${d}.png`);
      await h.shot(f);
      out.push(readPng(f));
    }
    return { pngs: out, st: await h.state(), f0 };
  } finally { await h.close(); }
}

async function settings(gpu) {
  const h = await openCart('detexto', { gpu, tag: 'dither-settings' });
  try {
    const s = h.session;
    // to the title (the deterministic clock: the loading screen ends on a fixed frame)
    const t = await call('frame', { op: 'step', frames: 1200, stopWhen: { field: 'shell.screen', equals: 1, every: 5 }, fields: ['shell.screen'] }, s);
    check(t.stopped, `${gpu}: settings: the title shows (frame ${t.frameCount})`);
    await call('frame', { op: 'step', frames: 30 }, s);
    const tap = (b) => [{ input: { ports: [{ [b]: true }] }, frames: 3 }, { input: { ports: [{}] }, frames: 12 }];
    const seq = async (bs) => (await call('input', { op: 'sequence', steps: bs.flatMap(tap), fields: ['shell.screen', 'shell.item', 'dt.dither.look', 'dt.ascii.scene_scale'] }, s)).fields;
    let f = await seq(['down', 'south', 'up', 'up']); // Settings, then up from the top: Back, then the game's row above it
    check(f['shell.screen'] === 5 && f['dt.dither.look'] === 0, `${gpu}: settings: on the Settings screen (item ${f['shell.item']}), the look Dither (${f['dt.dither.look']})`);
    await call('frame', { op: 'screenshot', path: join(OUT, `${gpu}-settings.png`) }, s);
    f = await seq(['left']);
    check(f['dt.dither.look'] === 0, `${gpu}: settings: control: left on the first choice stays on Dither (${f['dt.dither.look']})`);
    const want = [1, 2, 3, 3];
    for (let i = 0; i < want.length; i++) {
      f = await seq(['right']);
      check(f['dt.dither.look'] === want[i], `${gpu}: settings: right ${i + 1}: the look ${f['dt.dither.look']} (want ${want[i]})`);
    }
    check(Math.abs(f['dt.ascii.scene_scale'] - 0.35) < 1e-3, `${gpu}: settings: ASCII renders the scene at 35% (${f['dt.ascii.scene_scale']})`);
    f = await seq(['left', 'left', 'left']);
    check(f['dt.dither.look'] === 0 && f['dt.ascii.scene_scale'] === 0, `${gpu}: settings: back to Dither, the scene at full size (${f['dt.dither.look']}, ${f['dt.ascii.scene_scale']})`);
  } finally { await h.close(); }
}

for (const gpu of gpus) {
  if (only.includes('colours')) {
    const shots = {};
    for (const look of ['dither', 'warm', 'grid']) {
      const r = await frames(gpu, { LOOK: look, NO_HUD: 1 }, look, AT, []);
      shots[look] = r.pngs[0];
      const s = twoColour(r.pngs[0], INK[look]);
      check(r.st['dt.dither.w'] === 1920 && r.st['dt.dither.h'] === 1080, `${gpu} ${look}: the pass at the output's full size (${r.st['dt.dither.w']}x${r.st['dt.dither.h']})`);
      check(s >= 0.995, `${gpu} ${look}: two colours: ${(s * 100).toFixed(3)}% of the pixels ink or paper (${r.f0})`);
    }
    const off = await frames(gpu, { LOOK: 'dither', NO_HUD: 1, DITHER_OFF: 1 }, 'off', AT, []);
    const so = twoColour(off.pngs[0], INK.dither);
    check(so < 0.995, `${gpu}: control (the pass off) fails two colours: ${(so * 100).toFixed(2)}%`);
    const dg = flips(shots.dither, shots.grid), dw = flips(shots.dither, shots.warm);
    check(dg > 0.05, `${gpu}: dither and grid differ: ${(dg * 100).toFixed(2)}% of the pixels`);
    check(dw < 0.02, `${gpu}: warm is the dither's pattern: ${(dw * 100).toFixed(3)}% of the pixels differ in side`);
  }
  if (only.includes('crawl')) {
    for (const look of ['dither', 'grid']) {
      const still = await frames(gpu, { LOOK: look, HOLD: 1, NO_HUD: 1 }, `still-${look}`, AT, [1, 1]);
      const crawl = await frames(gpu, { LOOK: look, HOLD: 1, NO_HUD: 1, DITHER_CRAWL: 1 }, `crawl-${look}`, AT, [1, 1]);
      const s = Math.max(flips(still.pngs[0], still.pngs[1]), flips(still.pngs[1], still.pngs[2]));
      const c = Math.min(flips(crawl.pngs[0], crawl.pngs[1]), flips(crawl.pngs[1], crawl.pngs[2]));
      check(s <= 0.005, `${gpu} ${look}: no crawl: ${(s * 100).toFixed(3)}% of the pixels change a frame with the camera still`);
      check(c >= Math.max(10 * s, 0.02), `${gpu} ${look}: control (the threshold moving) changes ${(c * 100).toFixed(2)}% a frame`);
    }
  }
  if (only.includes('settings')) await settings(gpu);
}
console.log(fail ? `detexto-dither: ${fail} failed` : 'detexto-dither: ok');
process.exit(fail ? 1 : 0);
