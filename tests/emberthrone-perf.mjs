// emberthrone perf gate: emberthrone_bench (the bot in the Tidecaller's fight at the marsh temple: the boss, its foes, the
// water, the temple's columns, the sun's cascades; nobody falls, so the fight holds) at 1920x1080, High, natively on
// the test GPU (the 890M), GL and WebGPU, with the pipeline's GPU pass timers; then the same from seven more places
// (the meadow, the canyon ring with the Thornwarden, the glowcap wood, the heights, the underway, the spire, the keep's
// throne with the Ember King: ADV_FROM).
// Bounds the frame's GPU time (the mean of the bench's last three 120-frame windows, each window the mean of its frames'
// GPU totals, not one frame of it; the worst frame is printed beside it) at 4.5 ms, every run drawing
// everything (no "not in any program table" line) with its characters on screen. Controls, which must fail: GL at
// 3840x2160 (4 times the pixels) and WebGPU against an impossible 1 ms bound.
// Holds the GPU suite lock (the engine's tools/with-gpu-suite.mjs). Run: node tests/emberthrone-perf.mjs [--quick]
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'emberthrone-perf', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const quick = process.argv.includes('--quick');
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
// the GPU's clock beside every GPU time: an integrated GPU's clock follows how busy it is, and a run on a loaded machine
// reads every pass at its floor clock (the engine's docs/guides/profiling.md, GPU timing on an integrated GPU)
const { gpuClock, clockLine } = await import(join(ENGINE, 'tools/lib/gpu-clock.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/emberthrone_bench.json'), 'utf8')).binary;
const BOUND = 4.5, RIGS = 3;

async function run(gpu, extra = {}) {
  const env = { ...process.env, CW_FRAMES: '600', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', ADV_BENCH: '1', ADV_NO_HUD: '1', ADV_QUALITY: '3',
    CW_RENDER_SCALE: '1.5', CW_GPU_TIMERS: '1', ...extra };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const clk = gpuClock();
  const r = spawnSync('timeout', ['-s', 'KILL', '400', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const clock = clockLine(await clk.stop());
  const err = r.stderr || '';
  if (pin) pin.check('native', (/WebGPU adapter: (.*)/.exec(err) || [])[1]);
  else if (gl) gl.check('native', err);
  const pl = err.split('\n').filter((l) => l.startsWith('bench_passes')).slice(-3);
  const win = pl.map((l) => +(/gpu_mean=([\d.]+)/.exec(l) || [])[1]);
  const peak = Math.max(...pl.map((l) => +(/gpu_max=([\d.]+)/.exec(l) || [])[1]));
  const last = err.split('\n').filter((l) => l.startsWith('bench ')).slice(-1)[0] || '';
  return { clock, status: r.status, gpu: win.length ? win.reduce((a, b) => a + b, 0) / win.length : NaN, peak, rigs: +((/rigs=(\d+)/.exec(last) || [])[1]),
    tris: +((/tris=(\d+)/.exec(last) || [])[1]), draws: +((/draws=(\d+)/.exec(last) || [])[1]),
    skipped: /in none of the app's program tables|have no program|NOT DRAWN/.test(err), passes: err.split('\n').filter((l) => l.startsWith('bench_passes')).slice(-1)[0] || '' };
}

let bad = 0;
const check = (name, r, bound, mustFail = false) => {
  const within = r.status === 0 && !r.skipped && r.rigs >= RIGS && r.gpu > 0 && r.gpu <= bound;
  const line = `${name}: GPU ${r.gpu.toFixed(2)} ms (bound ${bound}; worst frame ${r.peak.toFixed(2)}), ${r.rigs} characters, ${r.draws} draws, ${(r.tris / 1e6).toFixed(2)} M triangles, status ${r.status}, ${r.clock}${r.skipped ? ', SKIPPED DRAWS' : ''}`;
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}`);
  } else if (!within) { console.log(`FAIL ${line}\n     ${r.passes}`); bad++; } else console.log(`ok   ${line}\n     ${r.passes}`);
};
const g = await run('gl'), w = await run('webgpu');
check('the Tidecaller fight GL', g, BOUND);
check('the Tidecaller fight WebGPU', w, BOUND);
if (!quick)
  for (const [name, from] of [['the meadow', '1'], ['the Thornwarden', '12'], ['the glowcap wood', '28'], ['the heights', '39'], ['the underway', '57'], ['the spire', '61'], ['the Ember King', '183']])
    for (const gpu of ['gl', 'webgpu']) check(`${name} ${gpu === 'gl' ? 'GL' : 'WebGPU'}`, await run(gpu, { ADV_FROM: from }), BOUND);
check('GL at 3840x2160 (4 times the pixels)', await run('gl', { CW_RENDER_SCALE: '3' }), BOUND, true);
check('WebGPU against 1 ms', w, 1, true);
process.exit(bad ? 1 : 0);
