// destructo hitch gate: the frame the charge goes off in must not be a dropped frame. destructo_bench (the bot on
// Great Keep: its charge breaks hundreds of blocks and wakes two thousand bodies) runs as a cart on romdev, on GL and on
// WebGPU. A first run steps to frame 760 in ranges and reads where the charge went off (fx.boom_frame); a second run
// steps to 30 frames before it, and romdev times the 210 frames from there in one call (frame({op:'timing'}), every
// frame's wall, CPU in the frame call and GPU time, series kept).
// Judged: the most CPU any frame from the charge on took (romdev's cpuMs: the cart's ticks and render) under 16.6 ms
// (60 Hz). Reported beside it: the worst frame's wall time (CPU and GPU back to back, romdev waits for the GPU each
// frame: an upper bound on a player's frame) and GPU time. A run romdev calls noisy (other work on the GPU, a loaded
// machine) is run again, up to three times; a run still noisy is reported, not judged.
// Control, which must fail: destructo_bench_nobudget, the game as it was before the fix: no fracture queue, no blast
// shells (every break and the whole blast in one tick) and no effects primed at load.
// Run: node tests/destructo-hitch.mjs [--gpu gl|webgpu]
import { openCart, gpusArg } from './lib/romdev.mjs';

const BOUND = 16.6, BEFORE = 30, SPAN = 210;
async function boomFrame(gpu, app) {
  const h = await openCart(app, { gpu, tag: 'hitch' });
  try {
    await h.command('dm set QUALITY 3');
    await h.step(760 - h.frame);
    const f = await h.read('fx.boom_frame'), at = await h.read('fx.frame');
    return f ? h.frame - (at - f) : 0;
  } finally { await h.close(); }
}
async function once(gpu, app, boom) {
  const h = await openCart(app, { gpu, tag: 'hitch', load: gpu === 'webgpu' ? { gpuTiming: true } : {} });
  try {
    await h.command('dm set QUALITY 3');
    await h.step(boom - BEFORE - h.frame);
    const t = await h.timing({ frames: SPAN, warmup: 0, window: 30, series: true, debugFields: ['game.booms', 'game.broken'] });
    const s = t.series, from = s.fields['game.booms'].findIndex((v) => v > 0);
    const after = (a) => a.slice(Math.max(0, from), from + 180).filter((v) => v != null);
    const mx = (a) => (a.length ? Math.max(...a) : NaN);
    return { from, cpu: mx(after(s.cpuMs)), wall: mx(after(s.wallMs)), gpu: mx(after(s.gpuMs)), broken: Math.max(...s.fields['game.broken']), noisy: t.contention.noisy, reasons: t.contention.reasons };
  } finally { await h.close(); }
}
async function run(gpu, app) {
  try {
    const boom = await boomFrame(gpu, app);
    if (!boom) return { err: 'no charge went off in 760 frames' };
    let r;
    for (let k = 0; k < 3; k++) { r = await once(gpu, app, boom); if (!r.noisy) break; console.log(`     (${app} ${gpu} noisy: ${r.reasons.join('; ')}: again)`); }
    return { ...r, boom };
  } catch (e) { return { err: e.message }; }
}
let bad = 0;
const line = (gpu, app, r) => r.err ? `${app} on ${gpu}: ${r.err}` : `${app} on ${gpu}: the charge on frame ${r.boom}; from it the most CPU a frame ${r.cpu.toFixed(2)} ms, the worst wall ${r.wall.toFixed(2)} ms, the worst GPU ${r.gpu.toFixed(2)} ms (${r.broken} blocks broken)${r.noisy ? `, NOISY (${r.reasons.join('; ')})` : ''}`;
for (const gpu of gpusArg()) {
  const r = await run(gpu, 'destructo_bench');
  const good = !r.err && r.from >= 0 && r.broken >= 300 && r.cpu < BOUND;
  if (!r.err && r.noisy) console.log(`NOT JUDGED (noisy three times) ${line(gpu, 'destructo_bench', r)}`);
  else if (good) console.log(`ok   ${line(gpu, 'destructo_bench', r)}`); else { bad++; console.log(`FAIL ${line(gpu, 'destructo_bench', r)} (bound ${BOUND})`); }
  const c = await run(gpu, 'destructo_bench_nobudget');
  const cgood = !c.err && c.from >= 0 && c.cpu < BOUND;
  if (c.err) { bad++; console.log(`FAIL control did not run: ${line(gpu, 'destructo_bench_nobudget', c)}`); }
  else if (cgood) { bad++; console.log(`CONTROL PASSED (a failure): ${line(gpu, 'destructo_bench_nobudget', c)}`); }
  else console.log(`ok   control fails as it must: ${line(gpu, 'destructo_bench_nobudget', c)}`);
}
process.exit(bad ? 1 : 0);
