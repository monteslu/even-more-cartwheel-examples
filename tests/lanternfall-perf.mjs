// lanternfall perf gate: lanternfall_bench (a warden fight in its lamp-lit arena, both players, every ability, the warden's
// shots, lights and particles in flight) at 1920x1080, High, natively on the test GPU (the 890M), GL and WebGPU. The
// bench times each frame's drawing to GPU completion (cw_gpu_finish before and after it: the frame's own CPU
// submission and GPU work, nothing queued before it) and prints the mean every 120 frames; the gate takes the mean of
// the last three windows. Bound: 4 ms a frame, with the scene's lights and sprites drawn. Controls, which must fail:
// the same runs held to an impossible 0.3 ms, and a run that draws nothing of the world (LANTERNFALL_BENCH_EMPTY: no
// lights, no sprites) against the scene's minimums.
// Holds the GPU suite lock (the engine's tools/with-gpu-suite.mjs). Run: node tests/lanternfall-perf.mjs
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'lanternfall-perf', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = [join(ROOT, 'build/native/project/games/lanternfall/lanternfall_bench'), join(ROOT, 'build/native/games/lanternfall/lanternfall_bench')].find(existsSync);
const BOUND = 4.0, LIGHTS = 6, SPRITES = 300;

function run(gpu, extra = {}) {
  const env = { ...process.env, CW_FRAMES: '540', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', LANTERNFALL_BENCH: '1', CW_RENDER_SCALE: '1.5', ...extra };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '400', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const err = r.stderr || '';
  if (pin) pin.check('native', (/WebGPU adapter: (.*)/.exec(err) || [])[1]);
  else if (gl) gl.check('native', err);
  const lines = err.split('\n').filter((l) => l.startsWith('bench '));
  const win = lines.slice(-3).map((l) => +(/draw_ms=([\d.]+)/.exec(l) || [])[1]);
  const last = lines.slice(-1)[0] || '';
  return { status: r.status, ms: win.length === 3 ? win.reduce((a, b) => a + b, 0) / 3 : NaN, lights: +((/lights=(\d+)/.exec(last) || [])[1] || 0),
    sprites: +((/sprites=(\d+)/.exec(last) || [])[1] || 0), particles: +((/particles=(\d+)/.exec(last) || [])[1] || 0), last };
}

let bad = 0;
const check = (name, r, bound, mustFail = false) => {
  const within = r.status === 0 && r.ms > 0 && r.ms <= bound && r.lights >= LIGHTS && r.sprites >= SPRITES;
  const line = `${name}: ${r.ms.toFixed(3)} ms a frame (bound ${bound}), ${r.lights} lights, ${r.sprites} sprites, ${r.particles} particles, status ${r.status}`;
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}`);
  } else if (!within) { console.log(`FAIL ${line}\n     ${r.last}`); bad++; } else console.log(`ok   ${line}`);
};
const g = run('gl'), w = run('webgpu');
check('bench GL 1080p', g, BOUND);
check('bench WebGPU 1080p', w, BOUND);
check('bench GL against 0.3 ms', g, 0.3, true);
check('bench with the world not drawn', run('gl', { LANTERNFALL_BENCH_EMPTY: '1' }), BOUND, true);
process.exit(bad ? 1 : 0);
