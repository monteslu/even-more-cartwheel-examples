// jewels soak gate: the game and its effects with no window and no GPU (build/native/games/jewels/jewels_soak), game
// minutes on end through the same input paths a player uses (pad presses, taps on every pointer slot, drags, mouse
// clicks, illegal swaps, picks put down, hints). Each run must exit 0 with "soak ok" after checking every tick: the
// score never goes down, an idle board holds no match and has a legal move, every move comes to rest, every cell
// holds a stone, every stone's look is a finite number, the effects stay in their pools. The totals must show real
// play: swaps made by the pad, by taps and by drags, cascades, hints and illegal swaps that sprang back.
// The control breaks a cell mid-run (JEWELS_SOAK_INJECT=1) and must be caught.
// Run: node tests/jewels-soak.mjs [--quick]   (--quick: 10 minutes a seed)
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/jewels/jewels_soak');
const quick = process.argv.includes('--quick');
const run = (minutes, seed, env = {}) => spawnSync(exe, [String(minutes), String(seed)], { encoding: 'utf8', timeout: 900000, env: { ...process.env, ...env } });
let bad = 0;
for (const seed of [1, 4242, 20261107]) {
  const minutes = quick ? 10 : 60;
  const r = run(minutes, seed);
  if (r.error) { console.log(`FAIL ${exe}: ${r.error.message} (build it: TARGETS=native scripts/build.sh jewels_soak)`); process.exit(1); }
  const out = r.stdout || '';
  const t = /totals: (\d+) moves, score (\d+), (\d+) clears, (\d+) reshuffles, (\d+) hints, (\d+) illegal, deepest cascade (\d+), largest group (\d+), swaps pad (\d+) touch (\d+) \(tap (\d+), drag (\d+)\), finales (\d+)/.exec(out);
  const why = [];
  if (r.status !== 0 || !/soak ok/.test(out)) why.push(`status ${r.status}: ${(out.match(/soak FAIL.*/) || [''])[0]}`);
  if (!t) why.push('no totals');
  else {
    const [moves, , clears, , hints, illegal, deepest, , pad, , tap, drag] = t.slice(1).map(Number);
    if (moves < minutes * 8) why.push(`${moves} moves in ${minutes} minutes`);
    if (clears < moves) why.push(`${clears} clears for ${moves} moves`);
    if (!hints || !illegal) why.push(`hints ${hints}, illegal swaps ${illegal}`);
    if (deepest < 3) why.push(`deepest cascade ${deepest}`);
    if (!pad || !tap || !drag) why.push(`swaps by pad ${pad}, tap ${tap}, drag ${drag}`);
  }
  const line = `seed ${seed}, ${minutes} min: ${t ? t[0].replace('totals: ', '') : '?'}`;
  if (why.length) { bad++; console.log(`FAIL ${line}\n     ${why.join('; ')}`); } else console.log(`ok   ${line}`);
}
const c = run(5, 1, { JEWELS_SOAK_INJECT: '1' });
if (c.status === 1 && /soak FAIL/.test(c.stdout)) console.log(`ok   control: a broken cell is caught (${(c.stdout.match(/soak FAIL.*/) || [''])[0]})`);
else { bad++; console.log(`CONTROL PASSED (a failure): the broken cell went unseen (status ${c.status})`); }
process.exit(bad ? 1 : 0);
