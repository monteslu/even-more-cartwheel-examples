// skate run-out gate: a landing missed by a little at a pace a skater can run off is run out on foot, not a fall
// (build/native/games/skate/skate_check runout, natively, no GPU). Landings on the flat, the board turned 0.9 to 1.15 rad
// from the way it flies (just past what rolls away sketchy):
//   near     at 3 to 6 m/s: every one a run-out, and the rider back on the board rolling (at a jog) when it ends
//   fast     the same misses at 9 to 13 m/s: every one a fall (too fast to run off)
//   across   the board right across the run (1.5 rad) at 4 m/s: a fall
//   clean    0.15 rad off: rolls away
// CONTROL, which must fail: SKATE_NO_RUNOUT=1 (every miss a fall, as before run-outs).
// Run: node tests/skate-runout.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/skate/skate_check');
function run(env = {}) {
  const r = spawnSync(exe, ['runout'], { encoding: 'utf8', env: { ...process.env, ...env } });
  if (r.status !== 0) { console.log(`FAIL ${exe} runout exited ${r.status}\n${r.stderr}`); process.exit(1); }
  return JSON.parse(r.stdout.trim().split('\n').find((l) => l.startsWith('{')));
}
function judge(s) {
  const why = [];
  if (s.near !== s.near_n) why.push(`${s.near_n - s.near} of ${s.near_n} near misses at a runnable pace fell`);
  if (s.near && s.back_ticks > s.runout_ticks + 2) why.push(`back on the board after ${s.back_ticks} ticks (a run-out is ${s.runout_ticks})`);
  if (s.near && !(s.on_speed > 0.3 && s.on_speed <= 1.6)) why.push(`stepped back on at ${s.on_speed} m/s (a jog: 0.3 to 1.6)`);
  if (s.fast_fell !== s.fast_n) why.push(`${s.fast_n - s.fast_fell} of ${s.fast_n} misses too fast to run off were run out`);
  if (!s.across_fell) why.push('a board right across the run was run out');
  if (!s.clean) why.push('a clean landing did not roll away');
  return why;
}
const fmt = (s) => `near misses run out ${s.near} of ${s.near_n} (back on after ${s.back_ticks} ticks at ${s.on_speed} m/s), fast misses fell ${s.fast_fell} of ${s.fast_n}, across fell ${s.across_fell}, clean ${s.clean}`;
let bad = 0;
const g = run(), gw = judge(g);
if (gw.length) bad++, console.log(`FAIL run-outs: ${gw.join('; ')}\n     ${fmt(g)}`);
else console.log(`ok   run-outs: ${fmt(g)}`);
const c = run({ SKATE_NO_RUNOUT: '1' }), cw = judge(c);
if (cw.length) console.log(`ok   control fails as it must (SKATE_NO_RUNOUT, every miss a fall): ${cw[0]}`);
else bad++, console.log(`CONTROL PASSED (a failure): ${fmt(c)}`);
process.exit(bad ? 1 : 0);
