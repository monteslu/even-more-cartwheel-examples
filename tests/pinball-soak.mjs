// pinball soak gate: the rules and the bot with no window and no GPU (build/native/games/pinball/pinball_soak), game
// after game on every table, 1 to 4 players: 60 game minutes a table at one player and 30 at each of 2, 3 and 4.
// Each run must exit 0 with "soak ok", find no trap in a table's geometry (a free ball still for 8 seconds: "STUCK"),
// play real pinball (at least 15 seconds a ball on average, the bot flipping, ramps made, a mode started), and keep
// resident memory within 5% of the first minute. Then each table again with its toy kept busy (PINBALL_SOAK_TOYS: the
// toy mode restarted whenever no mode runs, the wizard mode every fourth time): no trap, no ball held 10 s, no ball
// through the risen creature, and the toy working (the creature hit, the magnet catching, the bell ringing). The
// controls put a NaN into a ball, and a ball inside the risen creature: the checks must catch both.
// Run: node tests/pinball-soak.mjs [--quick]   (--quick: 10 minutes a run)
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/pinball/pinball_soak');
const quick = process.argv.includes('--quick');
const run = (minutes, players, table, env = {}) => spawnSync(exe, [String(minutes), String(players), String(table), '4242'],
  { encoding: 'utf8', timeout: 900000, env: { ...process.env, ...env } });
let bad = 0;
for (const table of [0, 1, 2])
  for (const players of [1, 2, 3, 4]) {
    const minutes = quick ? 10 : players === 1 ? 60 : 30;
    const r = run(minutes, players, table);
    const out = r.stdout || '';
    const tot = /totals: (\d+) games, (\d+) drains, mean ball ([\d.]+) s, flips\/min ([\d.]+), ramps (\d+), modes (\d+)/.exec(out);
    const stuck = (out.match(/STUCK/g) || []).length;
    const mem = [...out.matchAll(/rss (\d+) kB \(first minute (\d+)\)/g)].map((m) => +m[1] / +m[2]);
    const growth = mem.length ? Math.max(...mem) : 1;
    const why = [];
    if (r.status !== 0 || !/soak ok/.test(out)) why.push(`status ${r.status}`);
    if (!tot) why.push('no totals');
    else {
      if (+tot[3] < 15) why.push(`mean ball ${tot[3]} s`);
      if (+tot[4] < 10) why.push(`${tot[4]} flips a minute`);
      if (+tot[5] < 1) why.push('no ramp made');
      if (+tot[6] < 1) why.push('no mode started');
    }
    if (stuck) why.push(`${stuck} traps: ${(out.match(/STUCK.*/g) || []).slice(0, 3).join(' | ')}`);
    if (growth > 1.05) why.push(`memory x${growth.toFixed(3)}`);
    const line = `table ${table}, ${players} player${players > 1 ? 's' : ''}, ${minutes} min: ${tot ? `${tot[1]} games, mean ball ${tot[3]} s, ${tot[4]} flips/min, ${tot[5]} ramps, ${tot[6]} modes` : '?'}`;
    if (why.length) { bad++; console.log(`FAIL ${line}\n     ${why.join('; ')}`); } else console.log(`ok   ${line}`);
  }
for (const table of [0, 1, 2]) {
  const minutes = quick ? 10 : 30;
  const r = run(minutes, 1, table, { PINBALL_SOAK_TOYS: '1' });
  const out = r.stdout || '';
  const toy = /toys: (\d+) events, (\d+) hits, (\d+) magnet catches, (\d+) bells, (\d+) wizard modes, longest hold ([\d.]+) s/.exec(out);
  const why = [];
  if (r.status !== 0 || !/soak ok/.test(out)) why.push(`status ${r.status}: ${(out.match(/soak FAIL.*/g) || []).slice(0, 2).join(' | ')}`);
  if ((out.match(/STUCK/g) || []).length) why.push(`traps: ${(out.match(/STUCK.*/g) || []).slice(0, 3).join(' | ')}`);
  if (!toy) why.push('no toy totals');
  else {
    const works = [+toy[2], +toy[3], +toy[4]][table];
    if (works < 10) why.push(`the toy worked ${works} times`);
    if (+toy[5] < 1) why.push('no wizard mode');
  }
  const line = `table ${table} toys, ${minutes} min: ${toy ? `${toy[1]} toy events, ${toy[2]} toy hits, ${toy[3]} magnet catches, ${toy[4]} bells, ${toy[5]} wizard modes, longest hold ${toy[6]} s` : '?'}`;
  if (why.length) { bad++; console.log(`FAIL ${line}\n     ${why.join('; ')}`); } else console.log(`ok   ${line}`);
}
const c2 = run(5, 1, 0, { PINBALL_SOAK_TOYS: '1', PINBALL_SOAK_INJECT: '2' });
if (c2.status === 1 && /inside the creature/.test(c2.stdout)) console.log('ok   control: a ball inside the risen creature is caught');
else { bad++; console.log(`CONTROL PASSED (a failure): a ball inside the creature went unseen (status ${c2.status})`); }
const c = run(5, 1, 0, { PINBALL_SOAK_INJECT: '1' });
if (c.status === 1 && /soak FAIL/.test(c.stdout)) console.log('ok   control: an injected NaN is caught');
else { bad++; console.log(`CONTROL PASSED (a failure): the injected NaN went unseen (status ${c.status})`); }
process.exit(bad ? 1 : 0);
