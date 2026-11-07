// demolition soak gate: build/native/games/demolition/demolition_soak (the bot on the controls, no window, no GPU),
// level after level: 60 minutes alone, then 20 minutes each with 2, 3 and 4 players taking turns (co-op, then
// versus). Each run must exit 0 with "soak ok" (no NaN, nothing flung out of the world, the block counts adding up),
// win every level it finishes, report no debris left jittering awake 30 s after a turn, and hold its memory: the
// highest resident size over its last lap of the thirty levels within 5% of the lap before (it stops growing; one malloc
// arena: glibc gives each job thread an arena of its own, and those grow by fragments the game does not hold; --quick
// runs, and the 20-minute ones, are too short for two laps after the first and skip it).
// The control puts a NaN into a block and the checks must catch it.
// Run: node tests/demolition-soak.mjs [--quick]   (--quick: 8 minutes a run)
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/demolition/demolition_soak');
const quick = process.argv.includes('--quick');
const run = (minutes, players, level, mode, env = {}) => spawnSync(exe, [String(minutes), String(players), String(level), String(mode)],
  { encoding: 'utf8', timeout: 3600000 * 2, env: { ...process.env, MALLOC_ARENA_MAX: '1', ...env } }); // one malloc arena: the job threads' own arenas grow by fragments that are not the game's
let bad = 0;
for (const [players, mode, level] of [[1, 0, 0], [2, 0, 5], [3, 1, 12], [4, 1, 20]]) {
  const minutes = quick ? 8 : players === 1 ? 60 : 20;
  const r = run(minutes, players, level, mode);
  const out = r.stdout || '';
  const tot = /totals: (\d+) levels, (\d+) won, (\d+) lost, (\d+) stars, (\d+) over par, (\d+) shots, (\d+) broken, (\d+) pieces, (\d+) booms, (\d+) jitter, step mean ([\d.]+) ms worst ([\d.]+) ms, most awake (\d+)/.exec(out);
  // memory: the highest resident size over the run's last lap of levels against the lap before
  const again = [...out.matchAll(/again: rss (\d+) kB \(first time (\d+)\)/g)].map((m) => [+m[1], +m[2]]);
  const lap = (k) => Math.max(...again.slice(again.length - 30 * k, again.length - 30 * (k - 1)).map((a) => a[0]));
  const growth = again.length >= 60 ? lap(1) / lap(2) : 1;
  const why = [];
  if (r.status !== 0 || !/soak ok/.test(out)) why.push(`status ${r.status}`);
  if (!tot) why.push('no totals');
  else {
    if (+tot[3] > 0) why.push(`${tot[3]} levels lost`);
    if (+tot[10] > 0) why.push(`${tot[10]} levels left debris jittering`);
    if (+tot[1] < 3) why.push(`only ${tot[1]} levels`);
  }
  if (growth > 1.05) why.push(`memory x${growth.toFixed(3)}`);
  const line = `${players} player${players > 1 ? 's' : ''} ${mode ? 'versus' : 'co-op'}, ${minutes} min from level ${level + 1}: ${tot ? `${tot[1]} levels, ${tot[4]} stars, ${tot[7]} blocks broken into ${tot[8]} pieces, ${tot[9]} charges, physics step mean ${tot[11]} ms (worst ${tot[12]}), most awake ${tot[13]}` : '?'}`;
  if (why.length) { bad++; console.log(`FAIL ${line}\n     ${why.join('; ')}\n${(out.match(/(FAIL|JITTER).*/g) || []).slice(0, 4).join('\n')}`); } else console.log(`ok   ${line}`);
}
const c = run(3, 1, 0, 0, { DEMOLITION_SOAK_INJECT: '1' });
if (c.status === 1 && /soak FAIL/.test(c.stdout)) console.log('ok   control: an injected NaN is caught');
else { bad++; console.log(`CONTROL PASSED (a failure): the injected NaN went unseen (status ${c.status})`); }
process.exit(bad ? 1 : 0);
