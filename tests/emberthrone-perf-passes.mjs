// emberthrone perf by pass (development): the bench's GPU time pass by pass (the pipeline's timers, CW_GPU_TIMERS=1)
// from several places, GL and WebGPU at 1080p High on the test GPU. The places run interleaved, round after round
// (one run of each place a round), so the shared GPU's swings land on all of them alike; each pass's time is the median
// over the rounds of each run's mean of its last three 120-frame windows. Prints a table per API: the passes down, the
// places across, with each place's frame total and its draws.
//   node tests/emberthrone-perf-passes.mjs [--rounds 5] [--gpu gl|webgpu|both] [--places meadow:1,heights:39,...]
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'emberthrone-passes', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const argv = process.argv.slice(2);
const arg = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/emberthrone_bench.json'), 'utf8')).binary;
const rounds = +arg('--rounds', '5');
const places = arg('--places', 'meadow:1,heights:39,tidecaller:22,wood:28,spire:61').split(',').map((p) => p.split(':'));
const extraEnv = Object.fromEntries((arg('--env', '') || '').split(',').filter(Boolean).map((kv) => kv.split('=')));

function run(gpu, from) {
  const env = { ...process.env, CW_FRAMES: '420', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', ADV_BENCH: '1', ADV_NO_HUD: '1', ADV_QUALITY: '3', ADV_FROM: from,
    CW_RENDER_SCALE: '1.5', CW_GPU_TIMERS: '1', ...extraEnv };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' }); else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '300', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const err = r.stderr || '';
  const lines = err.split('\n').filter((l) => l.startsWith('bench_passes')).slice(-3);
  const passes = {};
  for (const l of lines) {
    const total = +(/gpu_total=([\d.]+)/.exec(l) || [])[1];
    (passes.total ||= []).push(total);
    for (const m of l.matchAll(/ (\w+)=([\d.]+)\/([\d.]+)/g)) (passes[m[1]] ||= []).push(+m[2]);
  }
  const mean = (a) => a.reduce((x, y) => x + y, 0) / a.length;
  const out = Object.fromEntries(Object.entries(passes).map(([k, v]) => [k, mean(v)]));
  const b = err.split('\n').filter((l) => l.startsWith('bench ')).at(-1) || '';
  out.draws = +(/draws=(\d+)/.exec(b) || [])[1];
  if (!lines.length) out.failed = `exit ${r.status} ${r.signal || ''}`;
  return out;
}
const median = (a) => { const b = a.filter((x) => Number.isFinite(x)).sort((x, y) => x - y); return b.length ? b[b.length >> 1] : NaN; };
let bad = 0;
for (const gpu of arg('--gpu', 'both') === 'both' ? ['gl', 'webgpu'] : [arg('--gpu', 'both')]) {
  const res = Object.fromEntries(places.map(([n]) => [n, []]));
  for (let i = 0; i < rounds; i++) for (const [n, from] of places) { const r = run(gpu, from); if (r.failed) bad++, console.log(`${gpu} ${n}: run failed (${r.failed})`); res[n].push(r); }
  const keys = [...new Set(places.flatMap(([n]) => res[n].flatMap((r) => Object.keys(r))))].filter((k) => k !== 'failed' && k !== 'draws' && k !== 'total');
  const col = (s) => String(s).padStart(11);
  console.log(`\n${gpu} (median of ${rounds} interleaved rounds, ms)\n${'pass'.padEnd(14)}${places.map(([n]) => col(n)).join('')}`);
  for (const k of [...keys, 'total']) console.log(`${k.padEnd(14)}${places.map(([n]) => col(median(res[n].map((r) => r[k] ?? 0)).toFixed(2))).join('')}`);
  console.log(`${'draws'.padEnd(14)}${places.map(([n]) => col(median(res[n].map((r) => r.draws)))).join('')}`);
  console.log(`${'total spread'.padEnd(14)}${places.map(([n]) => { const t = res[n].map((r) => r.total).sort((a, b) => a - b); return col(`${t[0].toFixed(2)}-${t.at(-1).toFixed(2)}`); }).join('')}`);
}
process.exit(bad ? 1 : 0);
