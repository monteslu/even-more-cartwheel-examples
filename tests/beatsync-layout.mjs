// beatsync's layout: is each button's river where the button sits? The three lane buttons are the west, south and east
// face buttons (left, bottom, right of the diamond), and the rivers carry that mapping by position alone (no button
// stays on screen in play), so the kick's river must be left of the clap's, and the clap's left of the bass's.
//
// Measured from the picture, through romdev, on GL and WebGPU: the play cart is taken through the menus to the count-in
// of the first song on Easy, and each button is pressed alone (a press lifts that river's gate). Every run is the same
// deterministic load and the same frames, so the press is the only difference from a run with no press: the bright
// columns that changed (the lower half of the frame, where the gates are) put each button's river on screen. Passes when
// west's change is left of south's and south's left of east's, each at least a tenth of the width apart, and each
// press changed the picture.
//
// The CONTROL must fail: the same runs with the rivers swapped (the `lanes 2 1 0` debug request draws the kick's river
// on the right and the bass's on the left), a layout where the buttons no longer match the rivers.
//
//   node tests/beatsync-layout.mjs [--gpu gl|webgpu|both]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { LANE_BUTTON, enterSong, gpusOf, openBeatsync } from './lib/beatsync.mjs';

const { readImage } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const argv = process.argv.slice(2);
const GPUS = gpusOf(argv);
const OUT = join(ROOT, 'out/gate/beatsync-layout');
mkdirSync(OUT, { recursive: true });
const NAMES = ['kick', 'clap', 'bass'];

/** one run: the count-in, the layout, then `button` (or none) held for the last frames; the frame's path */
async function shot(gpu, layout, button) {
  const r = await openBeatsync('beatsync', { gpu, tag: 'layout' });
  try {
    await enterSong(r, { song: 0, diff: 0 }, []);
    await r.command(`lanes ${layout}`);
    await r.run([{ pads: [{ buttons: [] }], frames: 40 }, { pads: [{ buttons: button ? [button] : [] }], frames: 5 }]);
    const path = join(OUT, `${gpu}-${layout.replaceAll(' ', '')}-${button || 'none'}.png`);
    await r.shotTo(path);
    return path;
  } finally {
    await r.close();
  }
}

/** where a press changed the picture: the luma-weighted centroid x (0..1) of the changed pixels in the lower half */
function changeAt(base, pressed) {
  const a = readImage(base), b = readImage(pressed);
  let sum = 0, sx = 0;
  for (let y = a.h >> 1; y < a.h; y++)
    for (let x = 0; x < a.w; x++) {
      const i = (y * a.w + x) * 4;
      const la = 0.2126 * a.rgba[i] + 0.7152 * a.rgba[i + 1] + 0.0722 * a.rgba[i + 2];
      const lb = 0.2126 * b.rgba[i] + 0.7152 * b.rgba[i + 1] + 0.0722 * b.rgba[i + 2];
      const d = Math.abs(lb - la);
      if (d < 12) continue;
      sum += d;
      sx += d * x;
    }
  return { x: sum ? sx / sum / a.w : NaN, mass: sum / (a.w * (a.h >> 1)) };
}

async function measure(gpu, layout) {
  const base = await shot(gpu, layout, null);
  const at = [];
  for (const b of LANE_BUTTON) at.push(changeAt(base, await shot(gpu, layout, b)));
  const seen = at.every((c) => c.mass > 0.005 && Number.isFinite(c.x));
  const ordered = seen && at[0].x + 0.1 < at[1].x && at[1].x + 0.1 < at[2].x;
  const text = at.map((c, l) => `${LANE_BUTTON[l]} (${NAMES[l]}) at ${(c.x * 100).toFixed(1)}% of the width (change ${c.mass.toFixed(2)})`).join(', ');
  return { ok: ordered, seen, text };
}

let failed = 0;
for (const gpu of GPUS) {
  const game = await measure(gpu, '0 1 2');
  console.log(`${game.ok ? 'ok  ' : 'FAIL'} layout (${gpu}): ${game.text}`);
  if (!game.ok) failed++;
  const control = await measure(gpu, '2 1 0');
  if (!control.seen) { console.log(`FAIL layout control (${gpu}, rivers swapped): a press did not show (${control.text}): the control proves nothing`); failed++; }
  else if (control.ok) { console.log(`FAIL layout control (${gpu}, rivers swapped) passed: the gate cannot see a wrong layout (${control.text})`); failed++; }
  else console.log(`ok   layout control (${gpu}, rivers swapped): out of order, as it must be (${control.text})`);
}
console.log(failed ? `beatsync-layout: ${failed} failed` : 'beatsync-layout: ok');
process.exit(failed ? 1 : 0);
