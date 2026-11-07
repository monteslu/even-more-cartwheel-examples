// jewels perf gate: jewels_bench (the bot picks the biggest clear every move and refills run long cascades: the
// worst case, bursts of shards and sparks, flashes, rings and streaks in flight most of the time) at 1920x1080 on
// High, natively on the test GPU (the 890M), GL and WebGPU, with the pipeline's GPU pass timers. Bounds the frame's
// GPU time (the median of the bench's 120-frame windows after the first) at BOUND ms; the run must cascade (a
// deepest step of four or more). Measured on the shared 890M, 2026-11-07: GL 4.5 ms (tray 0.33, stones with their
// glints 2.9, velocity and the temporal resolve 0.8, tone curve 0.23); WebGPU 2.9 ms over the passes its timestamps
// report (the game's own). Controls, which must fail: GL at 3840x2160 (four times the pixels) against the same
// bound, and WebGPU against an impossible 0.5 ms.
// Holds the GPU suite lock (the engine's tools/with-gpu-suite.mjs) once.
// Run: node tests/jewels-perf.mjs
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const BOUND = 5.2;
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'jewels-perf', 'node', fileURLToPath(import.meta.url)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/jewels_bench.json'), 'utf8')).binary;

function run(gpu, extra = {}) {
  const env = { ...process.env, CW_FRAMES: '1200', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', JEWELS_BENCH_PRINT: '1', CW_GPU_TIMERS: '1',
    JEWELS_QUALITY: '3', ...extra };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '300', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const err = r.stderr || '';
  if (pin) pin.check('native', (/WebGPU adapter: (.*)/.exec(err) || [])[1]);
  else if (gl) gl.check('native', err);
  const lines = err.split('\n').filter((l) => l.startsWith('bench '));
  const win = lines.slice(1).map((l) => +(/gpu_total=([\d.]+)/.exec(l) || [])[1]).filter((v) => v > 0).sort((a, b) => a - b);
  const last = lines.slice(-1)[0] || '';
  return { status: r.status, gpu: win.length ? win[win.length >> 1] : NaN, worst: win.length ? win[win.length - 1] : NaN,
    cascade: +((/cascade_max=(\d+)/.exec(last) || [])[1]), passes: last };
}

let bad = 0;
const check = (name, r, bound, mustFail = false) => {
  const within = r.status === 0 && r.cascade >= 4 && r.gpu > 0 && r.gpu <= bound;
  const line = `${name}: GPU ${r.gpu.toFixed(2)} ms (worst window ${r.worst.toFixed(2)}, bound ${bound}), deepest cascade ${r.cascade}, status ${r.status}`;
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}`);
  } else if (!within) { console.log(`FAIL ${line}\n     ${r.passes}`); bad++; } else console.log(`ok   ${line}\n     ${r.passes}`);
};
const gl = run('gl'), wg = run('webgpu');
check('GL 1920x1080 High', gl, BOUND);
check('WebGPU 1920x1080 High', wg, BOUND);
check('GL at 3840x2160', run('gl', { CW_RENDER_SCALE: '2' }), BOUND, true);
check('WebGPU against 0.5 ms', wg, 0.5, true);
process.exit(bad ? 1 : 0);
