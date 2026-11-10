// destructo flash check: no strobing. The game's biggest blast (destructo_bench as a cart on romdev: Great Keep, whose
// first charge breaks about 800 blocks, with its fire, dust, sparks, shards, crystal bursts and lights, the HUD on) is
// captured every frame from half a second before the charge goes off to two and a half seconds after it (one
// recordSession span, each frame box-filtered down in linear light as the native frame dump did), on GL and WebGPU, and
// measured by the engine's flash check (deps/cartwheel-engine/tools/lib/flash.mjs, through tests/lib/flash-run.mjs:
// WCAG 2.3.1's general and red flashes over a sixth of the screen, small lights switching together over a quarter of a
// 10-degree field, regular flicker from 3.5 Hz; the hardest two-frame rise is reported).
// The charge's frame is the cart's own count (fx.boom_frame), read from a first run.
// CONTROLS, which must fail: the same frames under the engine's 8 Hz full-screen strobe, and the same blast with a
// white wash over the screen at 10 Hz from the game (dm set STROBE_CONTROL 1).
//   node tests/destructo-flash.mjs [--gpu gl|webgpu]
import { openCart, gpusArg } from './lib/romdev.mjs';
import { captureRomdev, gate, mustFail } from './lib/flash-run.mjs';

const BEFORE = 30, AFTER = 150;
async function bench(gpu, opts = {}) {
  const h = await openCart('destructo_bench', { gpu, tag: 'flash' });
  for (const [k, v] of Object.entries({ QUALITY: 3, ...opts })) await h.command(`dm set ${k} ${v}`);
  return h;
}
let bad = 0;
for (const gpu of gpusArg()) {
  /* where the charge goes off: the cart's own frame count, from a first run stepped in one range */
  let boom = 0;
  {
    const h = await bench(gpu);
    try {
      await h.step(1500 - h.frame);
      const f = await h.read('fx.boom_frame'), at = await h.read('fx.frame');
      boom = f ? h.frame - (at - f) : 0; /* the host's frame of it */
    } finally { await h.close(); }
  }
  if (!boom) { console.log(`FAIL ${gpu}: the bench fired no charge in 1500 frames`); bad++; continue; }
  console.log(`${gpu}: the charge goes off on frame ${boom}`);
  const span = { from: boom - BEFORE, frames: boom + AFTER };
  for (const [label, opts, control] of [[`Great Keep's blast on ${gpu}, frames ${span.from}-${span.frames}`, {}, false],
    [`the blast under the game's 10 Hz white wash on ${gpu}, dm set STROBE_CONTROL 1`, { STROBE_CONTROL: 1 }, true]]) {
    const h = await bench(gpu, opts);
    let run;
    try { run = await captureRomdev(h, { ...span, name: `destructo-${gpu}${control ? '-strobe' : ''}` }); } finally { await h.close(); }
    bad += (control ? mustFail(label, run) : gate(label, run)) ? 0 : 1;
  }
}
process.exit(bad ? 1 : 0);
