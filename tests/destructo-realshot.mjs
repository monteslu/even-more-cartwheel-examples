// destructo real-shot gate: gravity, through the real weapons. levelcheck --realshot fires a shot, a heavy ball and a charge (the launcher's own aim and fire, game_aim_shot: the nearest
// low block of the structure) at a leg or base of Iron Lady, the Needle, the Black Tower, the Empire, the Pisa tower, the Taj, the Statue of Liberty and the Redeemer; and LOWHALF breaks the lower half of each at once
// (what a few good shots do). Checked every tick of every run:
//   floating   no standing assembly is left without a load path to the ground for more than 6 ticks (the layer a tick that judges a collapse), no loose chunk lies asleep in the air for more than 6 ticks
//   falls      with the lower half gone, over 60% of the blocks have moved over 1 m or are gone at 4 s
//   toppling   Iron Lady shot at a leg: the top third's lateral drift over its drop at 2 s is at least 0.4 (a tower going over, not sinking into itself)
// Control, must fail: OLD_SLEEP=1 (the rule that put the whole contact group to sleep, towers hung in the air).
// Run: node tests/destructo-realshot.mjs [--gpu gl|webgpu]
import { runCheck, inPairs, gpusArg } from './lib/romdev.mjs';

const L = { needle: 1, iron: 2, black: 3, empire: 4, taj: 7, pisa: 10, liberty: 15, redeemer: 16 };
let bad = 0;
for (const gpu of gpusArg()) {
  const run = async (args) => { const r = await runCheck('levels', args, { gpu }); if (r.status !== 0) { console.log(`FAIL check levels ${args} exited ${r.status}`); process.exit(1); } return r.rows; };
  const all = Object.values(L).map(String);
  const jobs = [0, 1, 2].map((a) => () => run(['--opt', `AMMO=${a}`, '--realshot', ...all]));
  jobs.push(() => run(['--opt', 'LOWHALF=1', '--realshot', ...all]));
  jobs.push(() => run(['--opt', 'LOWHALF=1', '--opt', 'OLD_SLEEP=1', '--realshot', L.black, L.iron, L.empire]));
  const [shot, heavy, charge, low, oldsleep] = await inPairs(jobs);
  const last = (rows, lv) => rows.filter((r) => r.phase === 'realshot' && r.level === lv).pop();
  const at = (rows, lv, t) => rows.filter((r) => r.phase === 'realshot' && r.level === lv && Math.abs(r.t - t) < 0.01).pop();
  for (const [name, rows] of [['shot', shot], ['heavy ball', heavy], ['charge', charge], ['lower half out', low]]) {
    for (const lv of all.map(Number)) {
      const r = last(rows, lv);
      if (!r || r.error) { bad++; console.log(`FAIL ${gpu} ${name} level ${lv}: ${r ? r.error : 'no row'}`); continue; }
      const why = [];
      if (r.floating_ticks > 6) why.push(`${r.floating_ticks} ticks with a standing assembly with no path to the ground`);
      if (r.asleep_ticks > 6) why.push(`${r.asleep_ticks} ticks with a loose chunk asleep in the air`);
      if (name === 'lower half out' && at(rows, lv, 4).moved < 0.6) why.push(`only ${(at(rows, lv, 4).moved * 100).toFixed(0)}% moved at 4 s`);
      const line = `${gpu} ${r.name}, ${name}: ${(at(rows, lv, 4).moved * 100).toFixed(0)}% moved at 4 s, floating ${r.floating_ticks} ticks, asleep in the air ${r.asleep_ticks} ticks`;
      if (why.length) { bad++; console.log(`FAIL ${line}: ${why.join('; ')}`); } else console.log(`ok   ${line}`);
    }
  }
  const t2 = at(shot, L.iron, 2);
  const ratio = t2.top_lateral / Math.max(0.1, t2.top_drop);
  if (ratio < 0.4) { bad++; console.log(`FAIL ${gpu} Iron Lady shot at a leg: lateral ${t2.top_lateral} over drop ${t2.top_drop} at 2 s: ${ratio.toFixed(2)} (a tower sinking into itself)`); } else console.log(`ok   ${gpu} Iron Lady shot at a leg tips: lateral over drop ${ratio.toFixed(2)} at 2 s`);
  const worst = Math.max(...[L.black, L.iron, L.empire].map((lv) => last(oldsleep, lv).asleep_ticks + last(oldsleep, lv).floating_ticks));
  if (worst <= 6) { bad++; console.log(`CONTROL PASSED (a failure): ${gpu} the old sleeping rule left ${worst} ticks of anything floating`); } else console.log(`ok   control (the old sleeping rule) fails the floating check as it must: ${worst} ticks`);
}
process.exit(bad ? 1 : 0);
