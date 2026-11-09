// towers perf gate: towers_bench (a late map, towers at their third level on every slot, 2400 creeps kept on its paths
// and the waves still coming, four bots at the controls) at 1920x1080, High, natively on the test GPU (the 890M), GL and
// WebGPU, with the pipeline's GPU pass timers. Bounds the frame's GPU time (the mean of the bench's last three 120-frame
// windows) at 4.3 ms (4.5 on WebGPU, whose decals pass is timed since engine v0.38.8), with at least 2000 creeps alive
// and drawn. Each line also reports every frame's GPU time over those windows (the bench's gpu_window and gpu_worst):
// its mean and its worst frame.
//   the bench map (26, the crystal vale) on both GLs, then one late map of every other theme (TOWERS_MAP) the same way.
// Every run must draw everything (no "not in any program table" line) and keep its crowd. Controls, which must fail:
// GL at 3840x2160 (4 times the pixels), WebGPU against an impossible 1 ms bound, and the bench held to 3000 creeps.
// Holds the GPU suite lock (the engine's tools/with-gpu-suite.mjs). Run: node tests/towers-perf.mjs [--quick]
// (--quick: the bench map only, GL and WebGPU, and the controls)
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'towers-perf', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const quick = process.argv.includes('--quick');
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
// the GPU's clock beside every GPU time (an integrated GPU's clock follows how busy it is: the engine's docs/guides/profiling.md)
const { gpuClock, clockLine } = await import(join(ENGINE, 'tools/lib/gpu-clock.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/towers_bench.json'), 'utf8')).binary;
// WebGPU: the decals pass (the scorches) costs about 0.2 ms more there than on GL, and engine v0.38.8 first timed it on
// WebGPU (before, it read 0), so the WebGPU bound carries it
const BOUND = 4.3, BOUND_WGPU = 4.5, CROWD = 2000;

async function run(gpu, extra = {}) {
  const env = { ...process.env, CW_FRAMES: '600', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', TOWERS_BENCH: '1', TOWERS_NO_HUD: '1',
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
  const lines = err.split('\n').filter((l) => l.startsWith('bench_passes')).slice(-3);
  const win = lines.map((l) => +(/gpu_total=([\d.]+)/.exec(l) || [])[1]);
  // (reported beside the bound's samples: every frame's GPU time over those windows, its mean and its worst)
  const every = lines.map((l) => +(/gpu_window=([\d.]+)/.exec(l) || [])[1]), worst = Math.max(...lines.map((l) => +(/gpu_worst=([\d.]+)/.exec(l) || [])[1]));
  const last = err.split('\n').filter((l) => l.startsWith('bench ')).slice(-1)[0] || '';
  return { clock, status: r.status, gpu: win.length ? win.reduce((a, b) => a + b, 0) / win.length : NaN,
    every: every.length ? every.reduce((a, b) => a + b, 0) / every.length : NaN, worst, enemies: +((/enemies=(\d+)/.exec(last) || [])[1]),
    drawn: +((/drawn=(\d+)/.exec(last) || [])[1]), skipped: /in none of the app's program tables|have no program/.test(err),
    passes: err.split('\n').filter((l) => l.startsWith('bench_passes')).slice(-1)[0] || '' };
}

let bad = 0;
const check = (name, r, bound, mustFail = false, most = Infinity) => {
  const within = r.status === 0 && !r.skipped && r.enemies >= CROWD && r.drawn >= CROWD && r.enemies <= most && r.gpu > 0 && r.gpu <= bound;
  const line = `${name}: GPU ${r.gpu.toFixed(2)} ms (bound ${bound}; every frame: mean ${r.every.toFixed(2)}, worst ${r.worst.toFixed(2)}), ${r.enemies} creeps (${r.drawn} drawn), status ${r.status}, ${r.clock}${r.skipped ? ', SKIPPED DRAWS' : ''}`;
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}`);
  } else if (!within) { console.log(`FAIL ${line}\n     ${r.passes}`); bad++; } else console.log(`ok   ${line}\n     ${r.passes}`);
};
const g = await run('gl'), w = await run('webgpu');
check('bench map 26 GL', g, BOUND);
check('bench map 26 WebGPU', w, BOUND_WGPU);
if (!quick)
  for (const map of [5, 10, 15, 20, 30]) for (const gpu of ['gl', 'webgpu']) check(`map ${map} ${gpu === 'gl' ? 'GL' : 'WebGPU'}`, await run(gpu, { TOWERS_MAP: String(map) }), gpu === 'gl' ? BOUND : BOUND_WGPU);
check('GL at 3840x2160 (4 times the pixels)', await run('gl', { CW_RENDER_SCALE: '3' }), BOUND, true);
check('WebGPU against 1 ms', w, 1, true);
check('the bench held to 1500 creeps', w, BOUND_WGPU, true, 1500);
process.exit(bad ? 1 : 0);
