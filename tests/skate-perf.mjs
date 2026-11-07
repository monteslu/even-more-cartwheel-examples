// skate perf gate: skate_bench (four bots skating the mega ramp park in a four-way split, every skater on screen in
// every view, ragdolls when they bail) at 1920x1080, High, natively on the test GPU (the 890M), GL and WebGPU, with the
// pipeline's GPU pass timers. Bounds the frame's GPU time (the mean of the bench's last three 120-frame windows) at
// 4.5 ms. Then each other park the same way with two players (the split most runs have).
// Every run must draw everything (no "not in any program table" line) and keep four skaters skating (points scored).
// Controls, which must fail: GL at 3840x2160 (4 times the pixels), and WebGPU against an impossible 1 ms bound.
// Holds the GPU suite lock (the engine's tools/with-gpu-suite.mjs). Run: node tests/skate-perf.mjs [--quick]
// (--quick: the bench park only, GL and WebGPU, and the controls)
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'skate-perf', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const quick = process.argv.includes('--quick');
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/skate_bench.json'), 'utf8')).binary;
const BOUND = 4.5;

function run(gpu, extra = {}) {
  const env = { ...process.env, CW_FRAMES: '720', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', SKATE_BENCH: '1', CW_RENDER_SCALE: '1.5', CW_GPU_TIMERS: '1', ...extra };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '400', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const err = r.stderr || '';
  if (pin) pin.check('native', (/WebGPU adapter: (.*)/.exec(err) || [])[1]);
  else if (gl) gl.check('native', err);
  const win = err.split('\n').filter((l) => l.startsWith('bench_passes')).slice(-3).map((l) => +(/gpu_total=([\d.]+)/.exec(l) || [])[1]);
  const last = err.split('\n').filter((l) => l.startsWith('bench ')).slice(-1)[0] || '';
  const scores = ((/score=([\d,]+)/.exec(last) || [])[1] || '').split(',').map(Number);
  return { status: r.status, gpu: win.length ? win.reduce((a, b) => a + b, 0) / win.length : NaN, players: +((/players=(\d+)/.exec(last) || [])[1]),
    scores, skipped: /in none of the app's program tables|have no program/.test(err), passes: err.split('\n').filter((l) => l.startsWith('bench_passes')).slice(-1)[0] || '' };
}

let bad = 0;
const check = (name, r, bound, want_players, mustFail = false) => {
  const skating = r.scores.slice(0, want_players).filter((s) => s > 0).length;
  const within = r.status === 0 && !r.skipped && r.players === want_players && skating === want_players && r.gpu > 0 && r.gpu <= bound;
  const line = `${name}: GPU ${r.gpu.toFixed(2)} ms (bound ${bound}), ${r.players} players, ${skating} scoring, status ${r.status}${r.skipped ? ', SKIPPED DRAWS' : ''}`;
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}`);
  } else if (!within) { console.log(`FAIL ${line}\n     ${r.passes}`); bad++; } else console.log(`ok   ${line}\n     ${r.passes}`);
};
const g = run('gl'), w = run('webgpu');
check('mega park, four-way split, GL', g, BOUND, 4);
check('mega park, four-way split, WebGPU', w, BOUND, 4);
if (!quick)
  for (const park of [0, 1, 3, 4]) for (const gpu of ['gl', 'webgpu']) check(`park ${park}, two players, ${gpu === 'gl' ? 'GL' : 'WebGPU'}`, run(gpu, { SKATE_PARK: String(park), SKATE_PLAYERS: '2' }), BOUND, 2);
check('GL at 3840x2160 (4 times the pixels)', run('gl', { CW_RENDER_SCALE: '3' }), BOUND, 4, true);
check('WebGPU against 1 ms', w, 1, 4, true);
process.exit(bad ? 1 : 0);
