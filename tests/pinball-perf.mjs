// pinball perf gate: pinball_bench (the bot with three balls on the table all the time: the flashers, the pops, the
// inserts and their lights, the mirror ball's probe) at 1920x1080, High, natively on the test GPU (the 890M), GL and
// WebGPU, with the pipeline's GPU pass timers. Bounds the frame's GPU time (each 120-frame window's mean of its
// frames' GPU totals; the median of the bench's five windows, so one window the shared machine stalls does not decide
// it) at 4.1 ms on every table (crawler's bound). A window's mean, not one frame of it: the mirror ball's probe
// captures every third frame, and one frame read at the same point of every window landed on a capture on one table
// and between captures on the others. The window's worst frame is printed beside it. Every run must draw everything (no "not in any program table" line)
// and keep its three balls. Controls, which must fail: GL at 3840x2160 (four times the pixels) against the same
// bound, and WebGPU against an impossible 0.5 ms.
// Holds the GPU suite lock (the engine's tools/with-gpu-suite.mjs) for one table at a time.
// Run: node tests/pinball-perf.mjs [--quick]   (--quick: the bench's own table only)
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const BOUND = 4.1;
if (!process.env.CW_GPU_SUITE_INNER) { // one lock hold per table, so the GPU is shared in short turns
  const quick = process.argv.includes('--quick');
  let status = 0;
  for (const t of quick ? ['1'] : ['0', '1', '2']) {
    const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), `pinball-perf-${t}`, 'node', fileURLToPath(import.meta.url), '--table', t, ...(t === '1' ? ['--controls'] : [])],
      { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
    if (r.status !== 0) status = 1;
  }
  process.exit(status);
}
const table = process.argv[process.argv.indexOf('--table') + 1];
const controls = process.argv.includes('--controls');
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/pinball_bench.json'), 'utf8')).binary;

function run(gpu, extra = {}) {
  const env = { ...process.env, CW_FRAMES: '600', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', PINBALL_BENCH_PRINT: '1', PINBALL_TABLE: table,
    CW_RENDER_SCALE: '1.5', CW_GPU_TIMERS: '1', ...extra };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '300', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const err = r.stderr || '';
  if (pin) pin.check('native', (/WebGPU adapter: (.*)/.exec(err) || [])[1]);
  else if (gl) gl.check('native', err);
  const lines = err.split('\n');
  const pl = lines.filter((l) => l.startsWith('bench_passes')).slice(-5);
  const win = pl.map((l) => +(/gpu_mean=([\d.]+)/.exec(l) || [])[1]).sort((a, b) => a - b);
  const peak = Math.max(...pl.map((l) => +(/gpu_max=([\d.]+)/.exec(l) || [])[1]));
  const last = lines.filter((l) => l.startsWith('bench ')).slice(-1)[0] || '';
  return { status: r.status, gpu: win.length ? win[win.length >> 1] : NaN, peak, balls: +((/balls=(\d+)/.exec(last) || [])[1]),
    lights: +((/lights=(\d+)/.exec(last) || [])[1]), skipped: /in none of the app's program tables|not in any program table/.test(err),
    passes: lines.filter((l) => l.startsWith('bench_passes')).slice(-1)[0] || '' };
}

let bad = 0;
const check = (name, r, bound, mustFail = false) => {
  const within = r.status === 0 && !r.skipped && r.balls >= 2 && r.gpu > 0 && r.gpu <= bound;
  const line = `table ${table} ${name}: GPU ${r.gpu.toFixed(2)} ms (bound ${bound}; worst frame ${r.peak.toFixed(2)}), ${r.balls} balls, ${r.lights} lights, status ${r.status}${r.skipped ? ', SKIPPED DRAWS' : ''}`;
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}`);
  } else if (!within) { console.log(`FAIL ${line}\n     ${r.passes}`); bad++; } else console.log(`ok   ${line}\n     ${r.passes}`);
};
const gl = run('gl'), wg = run('webgpu');
check('GL 1920x1080', gl, BOUND);
check('WebGPU 1920x1080', wg, BOUND);
if (controls) {
  check('GL at 3840x2160', run('gl', { CW_RENDER_SCALE: '3' }), BOUND, true);
  check('WebGPU against 0.5 ms', wg, 0.5, true);
}
process.exit(bad ? 1 : 0);
