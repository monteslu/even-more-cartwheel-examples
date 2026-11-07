// adventure perf probe (development): what each part of the picture costs on the test GPU at 1080p High. Runs
// adventure_bench from one place (ADV_FROM, default the meadow) with each part taken out in turn (ADV_PROBE_*) and
// prints the GPU time of each against the full picture. Holds the GPU suite lock for one short run set.
//   node tests/adventure-perf-probe.mjs [--from N] [--gpu gl|webgpu]
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'adventure-probe', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const argv = process.argv.slice(2);
const arg = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/adventure_bench.json'), 'utf8')).binary;
const gpu = arg('--gpu', 'gl'), from = arg('--from', '1');
function run(extra) {
  const env = { ...process.env, CW_FRAMES: '420', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', ADV_BENCH: '1', ADV_NO_HUD: '1', ADV_QUALITY: '3', ADV_FROM: from,
    CW_RENDER_SCALE: '1.5', CW_GPU_TIMERS: '1', ...extra };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' }); else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '300', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const lines = (r.stderr || '').split('\n').filter((l) => l.startsWith('bench_passes')).slice(-2);
  const g = lines.map((l) => +(/gpu_total=([\d.]+)/.exec(l) || [])[1]);
  return { gpu: g.length ? g.reduce((a, b) => a + b, 0) / g.length : NaN, passes: lines.at(-1) || '' };
}
const base = run({});
console.log(`full picture: ${base.gpu.toFixed(2)} ms\n  ${base.passes}`);
for (const k of ['NOGRASS', 'NOSCATTER', 'NOSHADOW', 'NOCHARS', 'NOBUILD', 'NOWATER', 'TERRAINQ']) {
  const r = run({ [`ADV_PROBE_${k}`]: k === 'TERRAINQ' ? '1' : '1' });
  console.log(`without ${k.toLowerCase()}: ${r.gpu.toFixed(2)} ms (${(base.gpu - r.gpu).toFixed(2)} saved)\n  ${r.passes}`);
}
