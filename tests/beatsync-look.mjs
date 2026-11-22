// beatsync's look, on romdev, GL and WebGPU: the frame is neon characters on black (the ASCII pass, src/ascii.c), not the
// shapes under them (the measure of tests/detexto-look.mjs, with a stroke test added). beatsync_auto plays the first
// song on Hard; in the drop (frame 900) the play field (the middle 60% of the width, between the HUD's columns: the
// score, the song and the crowd meter are text and bars, not characters) must be mostly black (at least 50% of the
// pixels under luma 14), carry lit characters (at least 3% of the pixels, at most 40%) drawn as strokes (at most 15%
// of the bright pixels inside a fully lit 9 x 9 box: characters are lines, not filled shapes), and the character grid
// must cover the frame.
// The game's style is held at the neon characters (`style neon`; tests/beatsync-styles.mjs checks every style).
// The CONTROL must fail: the same frame with the pass off (the `ascii off` request) is the plain picture of filled
// shapes, which the same measure must reject.
//   node tests/beatsync-look.mjs [--gpu gl|webgpu|both]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { gpusOf, openBeatsync } from './lib/beatsync.mjs';

const { readImage } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const argv = process.argv.slice(2);
const GPUS = gpusOf(argv);
const OUT = join(ROOT, 'out/gate/beatsync-look');
mkdirSync(OUT, { recursive: true });
const AT = 900;

function stats(path) {
  const img = readImage(path);
  const { h } = img, x0 = Math.round(img.w * 0.2), w = Math.round(img.w * 0.6);
  const rgba = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) rgba.set(img.rgba.subarray((y * img.w + x0) * 4, (y * img.w + x0 + w) * 4), y * w * 4);
  const L = new Float32Array(w * h);
  let black = 0, lit = 0, edge = 0, bright = 0;
  for (let i = 0; i < w * h; i++) {
    const l = (L[i] = 0.2126 * rgba[i * 4] + 0.7152 * rgba[i * 4 + 1] + 0.0722 * rgba[i * 4 + 2]);
    if (l < 14) black++;
    else if (l > 40) lit++;
  }
  /* solid: a bright pixel (luma over 110: a character's stroke, not its glow) whose whole 9 x 9 box is bright (the inside of a filled shape); a character's strokes are thin,
   * so its lit pixels are almost never solid */
  for (let y = 4; y < h - 4; y++)
    for (let x = 4; x < w - 4; x++) {
      if (L[y * w + x] <= 110) continue;
      bright++;
      let all = true;
      for (let dy = -4; dy <= 4 && all; dy += 2) for (let dx = -4; dx <= 4 && all; dx += 2) if (L[(y + dy) * w + x + dx] <= 110) all = false;
      if (all) edge++;
    }
  return { black: black / (w * h), lit: lit / (w * h), solid: bright ? edge / bright : 0 };
}

async function run(gpu, off) {
  const r = await openBeatsync('beatsync_auto', { gpu, tag: off ? 'look-control' : 'look' });
  try {
    await r.command('style neon'); // the neon characters (src/styles.c): the look this gate is about, held
    if (off) await r.command('ascii off');
    await r.stepTo(AT);
    const path = join(OUT, `${gpu}-${off ? 'off' : 'on'}.png`);
    await r.shotTo(path);
    const f = await r.fields();
    return { s: stats(path), f, path };
  } finally {
    await r.close();
  }
}

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const good = (s) => s.black >= 0.5 && s.lit >= 0.03 && s.lit <= 0.4 && s.solid <= 0.15;
for (const gpu of GPUS) {
  const on = await run(gpu, false), off = await run(gpu, true);
  const cw = on.f['ascii.cell_w'], ch = on.f['ascii.cell_h'], cols = on.f['ascii.cols'], rows = on.f['ascii.rows'];
  check(on.f['ascii.on'] === 1 && cw > 0 && ch > 0 && cols === Math.ceil(1920 / cw) && rows === Math.ceil(1080 / ch), `look (${gpu}): the pass runs, ${cols} x ${rows} characters of ${cw} x ${ch} px cover the frame`);
  check(ch >= 1080 * 0.024, `look (${gpu}): characters ${ch} px high, readable across a room (at least 2.4% of the height)`);
  check(good(on.s), `look (${gpu}): characters on black: ${(on.s.black * 100).toFixed(1)}% black, ${(on.s.lit * 100).toFixed(1)}% lit, ${(on.s.solid * 100).toFixed(0)}% of it solid`);
  check(!good(off.s), `look control (${gpu}, the pass off): the plain picture fails the measure, as it must (${(off.s.black * 100).toFixed(1)}% black, ${(off.s.lit * 100).toFixed(1)}% lit, ${(off.s.solid * 100).toFixed(0)}% of it solid)`);
}
console.log(failed ? `beatsync-look: ${failed} failed` : 'beatsync-look: ok');
process.exit(failed ? 1 : 0);
