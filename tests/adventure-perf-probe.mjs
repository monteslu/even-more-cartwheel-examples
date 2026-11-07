// adventure perf probe (development): what each part of the picture costs on the test GPU at 1080p High. Runs
// adventure_bench from one place (ADV_FROM, default the meadow) with each part taken out in turn (ADV_PROBE_*) and
// prints the GPU time of each against the full picture. Holds the GPU suite lock for one short run set.
//   node tests/adventure-perf-probe.mjs [--from N] [--gpu gl|webgpu|both] [--only A,B+C,...] [--reps N]
// (B+C: both parts out in one run; each variant runs against the full picture N times, interleaved, and the medians
// are compared: the GPU is shared, one run is noise)
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
const from = arg('--from', '1'), reps = +arg('--reps', '1');
function run(gpu, extra) {
  const env = { ...process.env, CW_FRAMES: '420', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', ADV_BENCH: '1', ADV_NO_HUD: '1', ADV_QUALITY: '3', ADV_FROM: from,
    CW_RENDER_SCALE: '1.5', CW_GPU_TIMERS: '1', ...extra };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' }); else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '300', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const lines = (r.stderr || '').split('\n').filter((l) => l.startsWith('bench_passes')).slice(-2);
  const g = lines.map((l) => +(/gpu_total=([\d.]+)/.exec(l) || [])[1]);
  if (!g.length) console.log(`  (no bench lines: exit ${r.status} ${r.signal || ''})\n  ${(r.stderr || '').split('\n').slice(-6).join('\n  ')}`);
  return { gpu: g.length ? g.reduce((a, b) => a + b, 0) / g.length : NaN, passes: lines.at(-1) || '' };
}
const median = (a) => { const b = a.filter((x) => !Number.isNaN(x)).sort((x, y) => x - y); return b.length ? b[b.length >> 1] : NaN; };
// each variant interleaved with the full picture --reps times (the shared GPU is noisy: the medians, not one run)
const variants = (arg('--only', '') || 'NOGRASS,NOSCATTER,NOTREES,NOSHADOW,NOCHARS,NOBUILD,NOWATER,NOCLOUDS,NOBLOOM,TERRAINQ').split(',');
for (const gpu of arg('--gpu', 'gl') === 'both' ? ['gl', 'webgpu'] : [arg('--gpu', 'gl')]) {
  const bases = [];
  for (const k of variants) {
    const b = [], v = [];
    let last = '';
    for (let i = 0; i < reps; i++) {
      const rb = run(gpu, {}); b.push(rb.gpu); bases.push(rb.gpu);
      const rv = run(gpu, Object.fromEntries(k.split('+').map((p) => [`ADV_PROBE_${p}`, '1']))); v.push(rv.gpu); last = rv.passes;
    }
    console.log(`${gpu} without ${k.toLowerCase()}: ${median(v).toFixed(2)} ms against ${median(b).toFixed(2)} (${(median(b) - median(v)).toFixed(2)} saved; runs ${v.map((x) => x.toFixed(2)).join(' ')} / ${b.map((x) => x.toFixed(2)).join(' ')})\n  ${last}`);
  }
  console.log(`${gpu} full picture: median ${median(bases).toFixed(2)} ms over ${bases.length} runs`);
}
