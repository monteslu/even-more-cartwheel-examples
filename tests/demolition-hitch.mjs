// demolition hitch gate: the frame the charge goes off in must not be a dropped frame. demolition_bench (the bot on
// Great Keep: its charge breaks hundreds of blocks and wakes two thousand bodies) runs on the native host and as a cart
// on romdev; the cart reports, over the three seconds from the first charge (debug fields, the real monotonic clock):
//   perf.blast_frame_max  the longest frame, from one rendered frame's end to the next's (ticks, render, the host)
//   perf.blast_work_max   the most CPU one frame took (its ticks and its render)
//   perf.blast_tick_max   the slowest game tick
// The longest frame must be under 16.6 ms (60 Hz) on every host. Control, which must fail:
// demolition_bench_nobudget, the same game with the fracture queue and the blast's shells off (every break and the whole
// blast in one tick, as before), on the cart. The controls of the native host are reported, not judged: natively the
// unspread blast fits in a frame too.
// Holds the GPU suite lock for its own runs only.
// Run: node tests/demolition-hitch.mjs
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const BOUND = 16.6;
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'demolition-hitch', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const OUT = join(ROOT, 'out/hitch');
mkdirSync(OUT, { recursive: true });
function run(host, app) {
  const state = join(OUT, `${app}-${host}.json`);
  const r = spawnSync('node', [join(ENGINE, 'tools/run-cart.mjs'), '--host', host, '--app', app, '--frames', '760', '--state', state],
    { encoding: 'utf8', timeout: 900000, env: { ...process.env, CW_PROJECT_ROOT: ROOT }, maxBuffer: 1 << 26 });
  let s = {};
  try { s = JSON.parse(readFileSync(state, 'utf8')); } catch { /* no state: the run failed */ }
  return { status: r.status, frame: +s['perf.blast_frame_max'], work: +s['perf.blast_work_max'], tick: +s['perf.blast_tick_max'], booms: +s['game.booms'], broken: +s['game.broken'] };
}
let bad = 0;
const line = (host, app, r) => `${app} on ${host}: longest frame ${r.frame.toFixed(2)} ms, most CPU a frame ${r.work.toFixed(2)} ms, slowest tick ${r.tick.toFixed(2)} ms (${r.booms} charges, ${r.broken} blocks broken, status ${r.status})`;
for (const host of ['native', 'romdev']) {
  const r = run(host, 'demolition_bench');
  const good = r.status === 0 && r.booms >= 1 && r.broken >= 300 && r.frame > 0 && r.frame < BOUND;
  if (good) console.log(`ok   ${line(host, 'demolition_bench', r)}`); else { bad++; console.log(`FAIL ${line(host, 'demolition_bench', r)} (bound ${BOUND})`); }
  const c = run(host, 'demolition_bench_nobudget');
  const cgood = c.status === 0 && c.booms >= 1 && c.frame > 0 && c.frame < BOUND;
  if (host === 'native') console.log(`info control ${line(host, 'demolition_bench_nobudget', c)}`);
  else if (cgood) { bad++; console.log(`CONTROL PASSED (a failure): ${line(host, 'demolition_bench_nobudget', c)}`); }
  else console.log(`ok   control fails as it must: ${line(host, 'demolition_bench_nobudget', c)}`);
}
process.exit(bad ? 1 : 0);
