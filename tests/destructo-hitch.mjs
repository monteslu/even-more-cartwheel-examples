// destructo hitch gate: the frame the charge goes off in must not be a dropped frame. destructo_bench (the bot on
// Great Keep: its charge breaks hundreds of blocks and wakes two thousand bodies) runs as a cart on romdev, on GL and on
// WebGPU, stepped in one range of 760 frames; the cart reports, over the three seconds from the first charge (debug
// fields, the real monotonic clock):
//   perf.blast_frame_max  the longest frame, from one rendered frame's end to the next's (ticks, render, the host)
//   perf.blast_work_max   the most CPU one frame took (its ticks and its render)
//   perf.blast_tick_max   the slowest game tick
// The longest frame must be under 16.6 ms (60 Hz). Control, which must fail: destructo_bench_nobudget, the game as it
// was before the fix: no fracture queue, no blast shells (every break and the whole blast in one tick) and no effects
// primed at load (the explosion kit compiles its programs in the frame of the first charge).
// romdev steps frames as fast as it can, so "the longest frame" is the cart's own work plus the host's between two
// frames; romdev has no GPU timer yet, so a GPU-bound frame shows only as the host's wait.
// Run: node tests/destructo-hitch.mjs [--gpu gl|webgpu]
import { openCart, gpusArg } from './lib/romdev.mjs';

const BOUND = 16.6;
async function run(gpu, app) {
  const h = await openCart(app, { gpu, tag: 'hitch' });
  try {
    await h.command('dm set QUALITY 3');
    await h.step(760 - h.frame);
    const s = await h.state();
    return { status: 0, frame: +s['perf.blast_frame_max'], work: +s['perf.blast_work_max'], tick: +s['perf.blast_tick_max'], booms: +s['game.booms'], broken: +s['game.broken'] };
  } catch (e) {
    return { status: 1, err: e.message, frame: NaN, work: NaN, tick: NaN };
  } finally { await h.close(); }
}
let bad = 0;
const line = (gpu, app, r) => `${app} on ${gpu}: longest frame ${r.frame.toFixed(2)} ms, most CPU a frame ${r.work.toFixed(2)} ms, slowest tick ${r.tick.toFixed(2)} ms (${r.booms} charges, ${r.broken} blocks broken${r.err ? `, ${r.err}` : ''})`;
for (const gpu of gpusArg()) {
  const r = await run(gpu, 'destructo_bench');
  const good = r.status === 0 && r.booms >= 1 && r.broken >= 300 && r.frame > 0 && r.frame < BOUND;
  if (good) console.log(`ok   ${line(gpu, 'destructo_bench', r)}`); else { bad++; console.log(`FAIL ${line(gpu, 'destructo_bench', r)} (bound ${BOUND})`); }
  const c = await run(gpu, 'destructo_bench_nobudget');
  const cgood = c.status === 0 && c.booms >= 1 && c.frame > 0 && c.frame < BOUND;
  if (c.status !== 0) { bad++; console.log(`FAIL control did not run: ${line(gpu, 'destructo_bench_nobudget', c)}`); }
  else if (cgood) { bad++; console.log(`CONTROL PASSED (a failure): ${line(gpu, 'destructo_bench_nobudget', c)}`); }
  else console.log(`ok   control fails as it must: ${line(gpu, 'destructo_bench_nobudget', c)}`);
}
process.exit(bad ? 1 : 0);
