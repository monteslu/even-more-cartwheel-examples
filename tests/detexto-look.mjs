// Detexto's look, on romdev, GL and WebGPU (the one cart on each): the frame is characters on black, not the 3D
// picture under them. detexto_auto plays its first level; at a frame mid-aim the screenshot must be mostly black
// (at least 55% of the pixels under luma 14), carry lit characters (at least 2% of the pixels, at most 30%), and the
// character grid must be the cart's own (dt.ascii.cols x dt.ascii.rows near 1920 / cell w x 1080 / cell h).
// The control that must fail: the same frame with the pass off (dm set ASCII_OFF 1) is the plain 3D picture, far
// from black. Also the shimmer: from a frame and the one after it, at most 6% of the lit characters may change in
// the still moment before the first shot, and the control without the hold (ASCII_KEEP 1, a new character the moment
// one fits better) must change more.
//   node tests/detexto-look.mjs [--gpu gl|webgpu|both]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, ROOT } from './lib/romdev.mjs';
import { readPng } from './lib/png.mjs';

const a = process.argv.slice(2);
const gi = a.indexOf('--gpu');
const gpus = gi >= 0 && a[gi + 1] !== 'both' ? [a[gi + 1]] : ['gl', 'webgpu'];
const OUT = join(ROOT, 'out/gate/detexto-look');
mkdirSync(OUT, { recursive: true });
const AT = 700; // the first level, aiming: the level's name has faded, nothing fired yet

function stats(png) {
  const { w, h, rgb } = png;
  let black = 0, lit = 0;
  for (let i = 0; i < w * h; i++) {
    const r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
    const l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    if (l < 14) black++;
    else if (l > 40) lit++;
  }
  return { black: black / (w * h), lit: lit / (w * h) };
}

// the lit-ness of each character cell (a cell is lit when any of its pixels is), and the share of lit cells whose
// picture changed between two frames
function cellChange(p0, p1, cw, ch) {
  const { w, h } = p0, cols = Math.floor(w / cw), rows = Math.floor(h / ch);
  let litCells = 0, changed = 0;
  for (let cy = 0; cy < rows; cy++)
    for (let cx = 0; cx < cols; cx++) {
      let lit = false, diff = 0;
      for (let y = cy * ch; y < (cy + 1) * ch; y++)
        for (let x = cx * cw; x < (cx + 1) * cw; x++) {
          const i = (y * w + x) * 3;
          const l0 = p0.rgb[i + 1], l1 = p1.rgb[i + 1];
          if (l0 > 40 || l1 > 40) lit = true;
          if (Math.abs(l0 - l1) > 60) diff++;
        }
      if (lit) litCells++, changed += diff > cw * ch * 0.15 ? 1 : 0;
    }
  return litCells ? changed / litCells : 0;
}

async function run(gpu, opts) {
  const h = await openCart('detexto_auto', { gpu, tag: 'look' });
  try {
    await h.command('dm set LOOK ascii'); // (the ASCII look: a 1-bit look may be the default, games/detexto/src/dither.h)
    for (const [k, v] of Object.entries(opts)) await h.command(`dm set ${k} ${v}`);
    await h.step(AT - h.frame);
    const f0 = join(OUT, `${gpu}-${Object.keys(opts).join('-') || 'ascii'}-0.png`), f1 = f0.replace('-0.png', '-1.png');
    await h.shot(f0);
    await h.step(1);
    await h.shot(f1);
    const st = await h.state();
    return { p0: readPng(f0), p1: readPng(f1), st, f0 };
  } finally { await h.close(); }
}

let fail = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) fail++; };
for (const gpu of gpus) {
  const on = await run(gpu, {});
  const off = await run(gpu, { ASCII_OFF: 1 });
  const loose = await run(gpu, { ASCII_KEEP: 1 });
  const s = stats(on.p0), so = stats(off.p0);
  const cw = on.st['dt.ascii.cell_w'], ch = on.st['dt.ascii.cell_h'];
  check(on.st['dt.ascii.on'] === 1 && cw > 0 && ch > 0, `${gpu}: the pass runs (cell ${cw}x${ch}, ${on.st['dt.ascii.cols']}x${on.st['dt.ascii.rows']} characters)`);
  check(Math.abs(on.st['dt.ascii.scene_scale'] - 0.35) < 1e-3 && ch === Math.round(1080 / 54), `${gpu}: the scene renders at ${on.st['dt.ascii.scene_scale']} of the output, the characters at the output's size (cell ${ch} px high)`);
  check(Math.abs(on.st['dt.ascii.cols'] - Math.ceil(1920 / cw)) <= 0 && Math.abs(on.st['dt.ascii.rows'] - Math.ceil(1080 / ch)) <= 0, `${gpu}: the grid covers the frame`);
  check(s.black >= 0.55 && s.lit >= 0.02 && s.lit <= 0.3, `${gpu}: characters on black: ${(s.black * 100).toFixed(1)}% black, ${(s.lit * 100).toFixed(1)}% lit (${on.f0})`);
  check(!(so.black >= 0.55 && so.lit >= 0.02 && so.lit <= 0.3), `${gpu}: control (pass off) fails the look: ${(so.black * 100).toFixed(1)}% black, ${(so.lit * 100).toFixed(1)}% lit`);
  const c = cellChange(on.p0, on.p1, cw, ch), cl = cellChange(loose.p0, loose.p1, cw, ch);
  check(c <= 0.06, `${gpu}: shimmer: ${(c * 100).toFixed(2)}% of the lit characters change in a frame`);
  check(cl > c, `${gpu}: control (no hold) changes more: ${(cl * 100).toFixed(2)}%`);
}
console.log(fail ? `detexto-look: ${fail} failed` : 'detexto-look: ok');
process.exit(fail ? 1 : 0);
