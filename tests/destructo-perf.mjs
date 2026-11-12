// destructo perf gate: destructo_bench (the bot on Taj Mahal, a charge breaks about 700 blocks and wakes about 2,000 bodies, the camera on the collapse), as a cart on romdev at
// 1920x1080 on the High preset, on GL and on WebGPU, on the test GPU (romdev's pinned GPU, the 890M).
// The bench runs to frame 360 in one step, then romdev times frames 361 to 1080 in one call (frame({op:'timing'}),
// romdev 0.153.0: wall, CPU in the frame call and GPU time a frame, in 120-frame windows, the server held for the run;
// WebGPU loads with gpuTiming so the host times every pass; GL is one GL_TIME_ELAPSED query round the frame).
// Bounds the frame's GPU time: the median of the 120-frame windows from the first with a hundred blocks broken
// (game.broken read over the same frames), within BOUND_GL on GL and BOUND_WGPU on WebGPU. romdev's GPU time is the
// host's round the whole frame (WebGPU: the union of every pass's timestamps; GL: a GL_TIME_ELAPSED query round the
// frame, which also counts the GPU waiting on the CPU inside the frame), so it includes the HUD and the final copy that
// the native gate's pass sums left out: the bound is set from clean romdev runs on the 890M, a little over the slowest, not the native gate's 4 ms. Every run must be on the pinned GPU, not noisy
// (romdev's contention check: a noisy run is measured again a minute later, for up to twenty minutes, and only a quiet
// run is judged), draw everything (no "not in any
// program table" line in the cart's log) and show a real collapse (a hundred blocks broken, a thousand bodies awake
// at once). Reported beside it: the cart's CPU a frame (romdev's cpuMs), the wall time, the GPU clock.
// Run one timing job at a time and nothing else on romdev beside it: a timing run holds the server.
// Controls, which must fail: GL at 3840x2160 (romdev's preferredSize, on a load with no seed: four times the pixels)
// against the GL bound, and WebGPU against an impossible 0.5 ms.
// Run: node tests/destructo-perf.mjs [--no-controls] [--gpu gl|webgpu]
import { openCart, gpusArg } from './lib/romdev.mjs';

/* a run romdev calls noisy (otherProcessesGpuMs: other processes on the test GPU; a loaded machine) is run again a
 * minute later, for up to QUIET_TRIES runs, and judged only once one is quiet */
const QUIET_TRIES = 20, QUIET_WAIT_MS = 60000;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const BOUND_GL = 6.0, BOUND_WGPU = 6.0;
const controls = !process.argv.includes('--no-controls');
const median = (a) => { const s = a.filter((v) => v > 0).sort((x, y) => x - y); return s.length ? s[s.length >> 1] : NaN; };

async function once(app, gpu, size) {
  const h = await openCart(app, { gpu, tag: 'perf', ...(size ? { seed: null } : {}), load: { ...(gpu === 'webgpu' ? { gpuTiming: true } : {}), ...(size ? { preferredSize: size } : {}) } });
  try {
    await h.command('dm set QUALITY 3');
    await h.step(360 - h.frame);
    const t = await h.timing({ frames: 1080 - h.frame, warmup: 0, window: 120, debugFields: ['game.broken', 'game.awake', 'view.instances'] });
    const ev = await h.events();
    const skipped = (ev.log || []).some((l) => /in none of the app's program tables|not in any program table/.test(l.text));
    const wins = (t.windows || []).filter((w) => (w.fields?.['game.broken']?.max ?? 0) >= 100);
    const g = (w) => (typeof w.gpuMs === 'number' ? w.gpuMs : w.gpuMs?.mean);
    return { t, wins, skipped, gpu: median(wins.map(g)), cpu: median(wins.map((w) => (typeof w.cpuMs === 'number' ? w.cpuMs : w.cpuMs?.mean))),
      wall: median(wins.map((w) => (typeof w.wallMs === 'number' ? w.wallMs : w.wallMs?.mean))), broken: t.fields?.['game.broken']?.max ?? 0, awake: t.fields?.['game.awake']?.max ?? 0,
      inst: t.fields?.['view.instances']?.max ?? 0, size: t.size, noisy: !!t.contention?.noisy, reasons: t.contention?.reasons || [], pinned: !!t.gpu?.onPinnedGpu, clock: t.gpu?.clockMhz,
      gpuAvailable: t.gpuMs && t.gpuMs.available !== false, gpuWhy: t.gpuMs?.reason, all: wins.map(g) };
  } finally { await h.close(); }
}
async function run(app, gpu, size) {
  let r;
  for (let k = 0; k < QUIET_TRIES; k++) {
    if (k) await sleep(QUIET_WAIT_MS); /* wait for the GPU to go quiet */
    try { r = await once(app, gpu, size); } catch (e) { r = { err: e.message, all: [] }; }
    if (r.err || !r.noisy) break;
    console.log(`     (${app} ${gpu} noisy: ${r.reasons.join('; ')}: again in a minute)`);
  }
  return r;
}
let bad = 0;
const check = (name, r, bound, mustFail = false) => {
  const within = !r.err && r.gpuAvailable && r.pinned && !r.noisy && !r.skipped && r.broken >= 100 && r.awake >= 1000 && r.wins.length >= 2 && r.gpu > 0 && r.gpu <= bound;
  const line = r.err ? `${name}: ${r.err}` : `${name}: GPU ${r.gpu.toFixed(2)} ms (bound ${bound}) at ${r.size ? `${r.size.width}x${r.size.height}` : '?'} over ${r.wins.length} collapse windows, cart CPU ${r.cpu.toFixed(2)} ms, wall ${r.wall.toFixed(2)} ms a frame, ${r.broken} blocks broken, ${r.awake} awake at most, ${r.inst} blocks drawn, GPU clock ${r.clock ? `${Math.round(r.clock.min)}-${Math.round(r.clock.max)} MHz` : '?'}${r.pinned ? '' : ', NOT ON THE PINNED GPU'}${r.noisy ? `, NOISY (${r.reasons.join('; ')})` : ''}${r.gpuAvailable ? '' : `, no GPU time (${r.gpuWhy})`}${r.skipped ? ', SKIPPED DRAWS' : ''} (windows: ${r.all.map((v) => v.toFixed(2)).join(' ')})`;
  if (!mustFail && !r.err && r.noisy) { console.log(`NOT JUDGED (noisy for twenty minutes) ${line}`); return; }
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}`);
  } else if (!within) { console.log(`FAIL ${line}`); bad++; } else console.log(`ok   ${line}`);
};
for (const gpu of gpusArg()) {
  const B = gpu === 'gl' ? BOUND_GL : BOUND_WGPU;
  const k = await run('destructo_bench', gpu);
  check(`Taj Mahal ${gpu} 1920x1080 High`, k, B);
  if (controls && gpu === 'gl') check('Taj Mahal GL at 3840x2160 (four times the pixels)', await run('destructo_bench', 'gl', { width: 3840, height: 2160 }), B, true);
  if (controls && gpu === 'webgpu') check('Taj Mahal WebGPU against 0.5 ms', k, 0.5, true);
}
process.exit(bad ? 1 : 0);
