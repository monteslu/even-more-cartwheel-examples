// destructo soak gate: games/destructo/src/soak.c in the check cart (destructo_check, games/destructo/tools/checkcart.c)
// on romdev (check run soak <minutes> <players> <level> <mode> [inject]: the bot on the controls, nothing drawn), level
// after level: 60 minutes alone, then 20 minutes each with 2, 3 and 4 players taking turns (co-op, then
// versus). Each run must exit 0 with "soak ok" (no NaN, nothing flung out of the world, the block counts adding up),
// win every level it finishes, report no debris left jittering awake 30 s after a turn, and hold its memory: the
// highest memory over its last lap of the levels within 5% of the lap before (it stops growing: in the cart "rss" is
// the cart's linear memory, the heap's high water; --quick runs, and the 20-minute ones, are too short for two laps
// after the first and skip it).
// Free fire: 10 minutes (--quick: 4) shooting at Great Keep with ammunition without end, never judged: soak ok, at
// least 6 shots a minute, the bodies never past the cap (3000) and the memory at the end within 5% of a minute in.
// Party (steady hands): 2 and 4 players, 8 minutes each (--quick: 3): soak ok, at least one round finished (one
// player left in, or every crystal down) and players going out.
// Every level's last shot is replayed as the game replays it (a second world, the logged inputs) and must end on the
// live state.
// The control puts a NaN into a block and the checks must catch it.
// Minutes are game minutes (60 ticks a second), stepped as fast as the cart can. The runs go two at a time.
// Run: node tests/destructo-soak.mjs [--quick] [--gpu gl|webgpu]   (--quick: 8 minutes a run)
import { runCheck, inPairs } from './lib/romdev.mjs';

const quick = process.argv.includes('--quick');
const gi = process.argv.indexOf('--gpu');
const gpu = gi > 0 ? process.argv[gi + 1] : 'gl';
const run = async (minutes, players, level, mode, extra = []) => {
  const r = await runCheck('soak', [minutes, players, level, mode, ...extra].map(String), { gpu, timeoutMin: 720 });
  return { status: r.status, stdout: r.text };
};
let bad = 0;
const SOAKS = [[1, 0, 0], [2, 0, 5], [3, 1, 12], [4, 1, 20]];
const PARTY = [2, 4];
const ffMinutes = quick ? 4 : 10;
/* every run, two at a time: the long one first */
const runs = await inPairs([
  ...SOAKS.map(([players, mode, level]) => () => run(quick ? 8 : players === 1 ? 60 : 20, players, level, mode)),
  () => run(3, 1, 0, 0, ['inject']),
  ...PARTY.map((players) => () => run(quick ? 3 : 8, players, 6, 3)),
  () => run(ffMinutes, 1, 'Great Keep', 2),
]);
for (const [k, [players, mode, level]] of SOAKS.entries()) {
  const minutes = quick ? 8 : players === 1 ? 60 : 20;
  const r = runs[k];
  const out = r.stdout || '';
  const tot = /totals: (\d+) levels, (\d+) won, (\d+) lost, (\d+) stars, (\d+) over par, (\d+) shots, (\d+) broken, (\d+) pieces, (\d+) booms, (\d+) jitter, (\d+) replays on the live state, step mean ([\d.]+) ms worst ([\d.]+) ms, most awake (\d+)/.exec(out);
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
    if (+tot[11] < +tot[1]) why.push(`${+tot[1] - +tot[11]} replays did not end on the live state`);
  }
  if (growth > 1.05) why.push(`memory x${growth.toFixed(3)}`);
  const line = `${players} player${players > 1 ? 's' : ''} ${mode ? 'versus' : 'co-op'}, ${minutes} min from level ${level + 1}: ${tot ? `${tot[1]} levels, ${tot[4]} stars, ${tot[7]} blocks broken into ${tot[8]} pieces, ${tot[9]} charges, ${tot[11]} replays on the live state, physics step mean ${tot[12]} ms (worst ${tot[13]}), most awake ${tot[14]}` : '?'}`;
  if (why.length) { bad++; console.log(`FAIL ${line}\n     ${why.join('; ')}\n${(out.match(/(FAIL|JITTER).*/g) || []).slice(0, 4).join('\n')}`); } else console.log(`ok   ${line}`);
}
const c = runs[SOAKS.length];
if (c.status === 1 && /soak FAIL/.test(c.stdout || '')) console.log('ok   control: an injected NaN is caught');
else { bad++; console.log(`CONTROL PASSED (a failure): the injected NaN went unseen (status ${c.status})`); }
for (const [k, players] of PARTY.entries()) {
  const minutes = quick ? 3 : 8;
  const r = runs[SOAKS.length + 1 + k];
  const out = r.stdout || '';
  const m = /party: (\d+) rounds, (\d+) with one player left, (\d+) out for a crystal, (\d+) out on strikes/.exec(out);
  const why = [];
  if (r.status !== 0 || !/soak ok/.test(out)) why.push(`status ${r.status}`);
  if (!m) why.push('no party line');
  else {
    if (+m[1] < 1) why.push(`no round finished`);
    if (+m[3] + +m[4] < 1) why.push(`nobody went out`);
  }
  const line = `party, ${players} players, ${minutes} min: ${m ? `${m[1]} rounds, ${m[2]} won by the last one in, ${m[3]} out for a crystal, ${m[4]} on strikes` : '?'}`;
  if (why.length) { bad++; console.log(`FAIL ${line}\n     ${why.join('; ')}\n${(out.match(/FAIL.*/g) || []).slice(0, 4).join('\n')}`); } else console.log(`ok   ${line}`);
}
{
  const minutes = ffMinutes;
  const r = runs[runs.length - 1];
  const out = r.stdout || '';
  const m = /free fire: (\d+) shots, (\d+) broken, (\d+) pieces, (\d+) charges, most bodies (\d+), level judged (\w+)/.exec(out);
  const rss = [...out.matchAll(/minute (\d+): rss (\d+) kB/g)].map((x) => +x[2]), end = +(/rss (\d+) kB at the end/.exec(out) || [])[1];
  const first = /minute 1: rss (\d+)/.exec(out) ? +/minute 1: rss (\d+)/.exec(out)[1] : rss[0];
  const why = [];
  if (r.status !== 0 || !/soak ok/.test(out)) why.push(`status ${r.status}`);
  if (!m) why.push('no free fire line');
  else {
    if (+m[1] < minutes * 6) why.push(`only ${m[1]} shots`);
    if (+m[5] > 3000) why.push(`${m[5]} bodies`);
    if (m[6] !== 'never') why.push('the level was judged');
  }
  if (first && end > first * 1.05) why.push(`memory ${first} kB a minute in, ${end} kB at the end`);
  const line = `free fire on Great Keep, ${minutes} min: ${m ? `${m[1]} shots, ${m[2]} blocks broken into ${m[3]} pieces, ${m[4]} charges, most bodies ${m[5]}` : '?'}, rss ${first} kB a minute in, ${end} kB at the end`;
  if (why.length) { bad++; console.log(`FAIL ${line}\n     ${why.join('; ')}`); } else console.log(`ok   ${line}`);
}
process.exit(bad ? 1 : 0);
