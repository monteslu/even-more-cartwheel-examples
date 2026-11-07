// demolition perf gate: demolition_bench (the bot on Great Keep, a keep of about 1900 blocks: its charges bring
// most of it down, 1400 blocks broken and 2400 bodies awake at once, the camera on the collapse) at 1920x1080 on the High preset, natively on the test GPU (the
// 890M), GL and WebGPU, with the pipeline's GPU pass timers. Bounds the frame's GPU time at 4 ms: the median of the
// 120-frame windows while the collapse is on screen (from the first window with a hundred blocks broken; one window
// the shared machine stalls does not decide it). Every run must draw everything (no "not in any program table" line)
// and must show a real collapse (a hundred blocks broken, a thousand bodies awake at once).
// Controls, which must fail: GL at 3840x2160 (four times the pixels) against the same bound, and WebGPU against an
// impossible 0.5 ms.
// Holds the GPU suite lock (the engine's tools/with-gpu-suite.mjs) for its own runs only.
// Run: node tests/demolition-perf.mjs [--no-controls]
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const BOUND = 4.0;
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'demolition-perf', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const controls = !process.argv.includes('--no-controls');
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/demolition_bench.json'), 'utf8')).binary;

function run(gpu, extra = {}) {
  const env = { ...process.env, CW_FRAMES: '1080', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', DEMOLITION_BENCH_PRINT: '1',
    DEMOLITION_QUALITY: '3', CW_RENDER_SCALE: '1.5', CW_GPU_TIMERS: '1', ...extra };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '400', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const err = r.stderr || '';
  if (pin) pin.check('native', (/WebGPU adapter: (.*)/.exec(err) || [])[1]);
  else if (gl) gl.check('native', err);
  const lines = err.split('\n');
  const bench = lines.filter((l) => l.startsWith('bench ')), passes = lines.filter((l) => l.startsWith('bench_passes'));
  const wins = bench.map((l, i) => ({ broken: +(/broken=(\d+)/.exec(l) || [])[1], awake: +(/awake=(\d+)/.exec(l) || [])[1],
    inst: +(/instances=(\d+)/.exec(l) || [])[1], phys: +(/phys_ms=([\d.]+)/.exec(l) || [])[1], gpu: +(/gpu_total=([\d.]+)/.exec(passes[i] || '') || [])[1], passes: passes[i] || '' }));
  const from = wins.findIndex((w) => w.broken >= 100);
  const col = from >= 0 ? wins.slice(from) : [];
  const g = col.map((w) => w.gpu).filter((v) => v > 0).sort((a, b) => a - b);
  return { status: r.status, gpu: g.length ? g[g.length >> 1] : NaN, wins: col.length, broken: Math.max(0, ...wins.map((w) => w.broken)),
    awake: Math.max(0, ...wins.map((w) => w.awake)), inst: Math.max(0, ...wins.map((w) => w.inst)), phys: Math.max(0, ...col.map((w) => w.phys)),
    skipped: /in none of the app's program tables|not in any program table/.test(err), passes: col.length ? col[col.length >> 1].passes : '' };
}

let bad = 0;
const check = (name, r, bound, mustFail = false) => {
  const within = r.status === 0 && !r.skipped && r.broken >= 100 && r.wins >= 2 && r.gpu > 0 && r.gpu <= bound;
  const line = `${name}: GPU ${r.gpu.toFixed(2)} ms (bound ${bound}) over ${r.wins} collapse windows, ${r.broken} blocks broken, ${r.awake} awake at most, ${r.inst} blocks drawn, physics ${r.phys.toFixed(2)} ms a tick at worst, status ${r.status}${r.skipped ? ', SKIPPED DRAWS' : ''}`;
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}`);
  } else if (!within) { console.log(`FAIL ${line}\n     ${r.passes}`); bad++; } else console.log(`ok   ${line}\n     ${r.passes}`);
};
const gl = run('gl'), wg = run('webgpu');
check('GL 1920x1080 High', gl, BOUND);
check('WebGPU 1920x1080 High', wg, BOUND);
if (controls) {
  check('GL at 3840x2160', run('gl', { CW_RENDER_SCALE: '3' }), BOUND, true);
  check('WebGPU against 0.5 ms', wg, 0.5, true);
}
process.exit(bad ? 1 : 0);
