// destructo real-shot gate: gravity, through the real weapons. levelcheck --realshot fires a shot, a heavy ball and a charge (the launcher's own aim and fire, game_aim_shot: the nearest
// low block of the structure) at a leg or base of Iron Lady, the Needle, the Black Tower, the Empire, the Pisa tower, the Taj, the Statue of Liberty and the Redeemer; and LOWHALF breaks the lower half of each at once
// (what a few good shots do). Checked every tick of every run:
//   floating   no standing assembly is left without a load path to the ground for more than 6 ticks (the layer a tick that judges a collapse), no loose chunk lies asleep in the air for more than 6 ticks
//   falls      with the lower half gone, over 60% of the blocks have moved over 1 m or are gone at 4 s
//   toppling   the footing's half out (HALFOUT: the base to 1.5 m on one side) of Iron Lady, the Needle, the Black Tower, the Empire and Pisa: the structure goes over as ONE rigid body (the biggest
//              piece 1 s on is at least 35% of its blocks), and at 2 s the top third has drifted sideways over 0.5 m and is tipped over 15 degrees on average, its sideways drift at least 0.3 of its drop (going over, not sinking into itself)
//   one leg     a shot or a heavy ball at ONE leg of Iron Lady takes its legs' load sharing past capacity (the others take 1.33 of their share, 1.25 is what a support is good for): the footing
//              crushes progressively and the tower goes over toward the missing leg: at 2 s the top is over 1 m sideways, over half of it (sideways over drop), and 60% of its blocks have moved at 4 s
// Controls, must fail: OLD_LOAD=1 (the old rule, a leg is judged by where the centre stands over the hull of what is left: a shot at one leg leaves the tower standing), OLD_SLEEP=1 (the rule that put the whole contact group to sleep, towers hung in the air), OLD_RELEASE=1 (the structure let go as sections of 150 blocks, not one rigid body).
// Run: node tests/destructo-realshot.mjs [--gpu gl|webgpu]
import { runCheck, inPairs, gpusArg } from './lib/romdev.mjs';
import { levelIndex } from './lib/levelindex.mjs';

const L = Object.fromEntries(Object.entries({ needle: 'The Needle', iron: 'Iron Lady', black: 'The Black Tower', empire: 'The Empire', taj: 'Taj Mahal', pisa: 'Leaning Tower of Pisa', liberty: 'Statue of Liberty', redeemer: 'The Redeemer' }).map(([k, n]) => [k, levelIndex(n)]));
let bad = 0;
for (const gpu of gpusArg()) {
  const run = async (args) => { const r = await runCheck('levels', args, { gpu }); if (r.status !== 0) { console.log(`FAIL check levels ${args} exited ${r.status}`); process.exit(1); } return r.rows; };
  const all = Object.values(L).map(String);
  const jobs = [0, 1, 2].map((a) => () => run(['--opt', `AMMO=${a}`, '--realshot', ...all]));
  jobs.push(() => run(['--opt', 'LOWHALF=1', '--realshot', ...all]));
  jobs.push(() => run(['--opt', 'LOWHALF=1', '--opt', 'OLD_SLEEP=1', '--realshot', L.black, L.iron, L.empire]));
  jobs.push(() => run(['--opt', 'HALFOUT=1', '--realshot', L.iron, L.needle, L.black, L.empire, L.pisa]));
  jobs.push(() => run(['--opt', 'HALFOUT=1', '--opt', 'OLD_RELEASE=1', '--realshot', L.iron, L.black]));
  jobs.push(() => run(['--opt', 'OLD_LOAD=1', '--opt', 'AMMO=0', '--realshot', L.iron]));
  jobs.push(() => run(['--opt', 'AMMO=1', '--opt', 'SHOTS=3', '--realshot', L.liberty, L.redeemer, L.taj]));
  const [shot, heavy, charge, low, oldsleep, half, oldrel, oldload, prog] = await inPairs(jobs);
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
  for (const lv of [L.iron, L.needle, L.black, L.empire, L.pisa]) {
    const a1 = at(half, lv, 1), a2 = at(half, lv, 2), placed = a1.placed;
    const ratio = a2.top_lateral / Math.max(0.5, a2.top_drop);
    const why = [];
    if (a1.largest < 0.35 * placed) why.push(`the biggest piece at 1 s is ${a1.largest} of ${placed} blocks (not one rigid body)`);
    if (a2.top_lateral < 0.5) why.push(`the top drifted only ${a2.top_lateral} m sideways`);
    if (a2.top_rot < 15) why.push(`the top is tipped only ${a2.top_rot} degrees`);
    if (ratio < 0.3) why.push(`sideways over drop ${ratio.toFixed(2)}`);
    const line = `${gpu} ${a2.name}, footing's half out: one body of ${a1.largest} at 1 s, top ${a2.top_lateral} m sideways and tipped ${a2.top_rot} degrees at 2 s`;
    if (why.length) { bad++; console.log(`FAIL ${line}: ${why.join('; ')}`); } else console.log(`ok   ${line}`);
  }
  for (const [name, rows] of [['shot', shot], ['heavy ball', heavy]]) {
    const a2 = at(rows, L.iron, 2), a4 = at(rows, L.iron, 4);
    const ratio = a2.top_lateral / Math.max(0.5, a2.top_drop);
    const why = [];
    if (a2.top_lateral < 1) why.push(`the top drifted only ${a2.top_lateral} m sideways at 2 s`);
    if (ratio < 0.5) why.push(`sideways over drop ${ratio.toFixed(2)}`);
    if (a4.moved < 0.6) why.push(`only ${(a4.moved * 100).toFixed(0)}% moved at 4 s`);
    const line = `${gpu} Iron Lady, one leg shot out (${name}): top ${a2.top_lateral} m sideways at 2 s, ${(a4.moved * 100).toFixed(0)}% moved at 4 s`;
    if (why.length) { bad++; console.log(`FAIL ${line}: ${why.join('; ')}`); } else console.log(`ok   ${line}`);
  }
  const ol = last(oldload, L.iron);
  if (ol.moved > 0.2) { bad++; console.log(`CONTROL PASSED (a failure): ${gpu} the old rule let Iron Lady go over (${(ol.moved * 100).toFixed(0)}% moved)`); } else console.log(`ok   control (the old rule) leaves Iron Lady standing after a shot at one leg as it must fail: ${(ol.moved * 100).toFixed(0)}% moved`);
  for (const lv of [L.liberty, L.redeemer, L.taj]) { /* three heavy balls at the base, one after another */
    const r = last(prog, lv);
    const why = [];
    if (r.floating_ticks > 6) why.push(`${r.floating_ticks} ticks floating`);
    if (r.asleep_ticks > 6) why.push(`${r.asleep_ticks} ticks asleep in the air`);
    const line = `${gpu} ${r.name}, three heavy balls at the base: floating ${r.floating_ticks} ticks, asleep in the air ${r.asleep_ticks} ticks, ${(r.moved * 100).toFixed(0)}% moved`;
    if (why.length) { bad++; console.log(`FAIL ${line}: ${why.join('; ')}`); } else console.log(`ok   ${line}`);
  }
  const orb = at(oldrel, L.iron, 1);
  if (orb.largest >= 0.35 * orb.placed) { bad++; console.log(`CONTROL PASSED (a failure): ${gpu} the old release left a body of ${orb.largest}`); } else console.log(`ok   control (the old release, sections of 150) fails the one-rigid-body check as it must: biggest piece ${orb.largest} of ${orb.placed}`);
  const worst = Math.max(...[L.black, L.iron, L.empire].map((lv) => last(oldsleep, lv).asleep_ticks + last(oldsleep, lv).floating_ticks));
  if (worst <= 6) { bad++; console.log(`CONTROL PASSED (a failure): ${gpu} the old sleeping rule left ${worst} ticks of anything floating`); } else console.log(`ok   control (the old sleeping rule) fails the floating check as it must: ${worst} ticks`);
}
process.exit(bad ? 1 : 0);
