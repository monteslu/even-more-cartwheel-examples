// destructo human gate: can a person new to the game clear every level? Checked by games/destructo/tools/humancheck.c
// in the check cart (destructo_check, games/destructo/tools/checkcart.c) on romdev, with the human-like player
// (games/destructo/src/human.c). Every sweep runs on two hosts at once, the levels split between them (alternate
// levels), the players (seeds) one after another on each. Unlike the bot (src/bot.c: it reads each crystal's exact place and solves the flight
// to a milliradian), this player sees only what the screen shows (the crystals' glow and beacons, where the aim path
// points, the HUD, where its last shot struck), aims with a person's error (0.5 m across and 0.8 m up and down at the
// structure, mostly a lean it keeps, which it corrects from where its shots strike), lets go of the d-pad a quarter
// of a second late, fires once the aim looks right to 4 milliradians, and sends its buttons through the game's own
// pad mapping. Eight such players (seeds) play every level from its start with the ammunition the level gives.
//
//   newcomer   every one of the eight clears levels 1 to 3 (the first levels decide whether a person goes on), every
//              level is cleared by at least 7 of the 8, and 97% of all the plays clear
//   skill      par is the reward for skill: a steadier player (a third of the newcomer's aim error) clears within
//              par (two stars or three) on at least two levels in three, and more often than the newcomer does
//   goes round where a crystal is hidden from where the launcher stands (its glow blocked, its beacon showing where it
//              is), the newcomer moves the launcher round the structure to a side it can see it from (East held with the
//              d-pad, through the game's pad mapping): some plays must do so, and those plays must clear their level as
//              often as the rest. Control: the player who never goes round (--never-moves) must show no such play
//   control    the cannon before the fix (--old-damage: the ball breaks only the block it touches and flies on, as in
//              round 3 and before) must fail the newcomer check
// The physics does not draw: one run, on a GL host (--gpu webgpu: on a WebGPU host).
// Run: node tests/destructo-human.mjs [--seeds n] [--gpu gl|webgpu]
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { runCheck, inPairs, ROOT } from './lib/romdev.mjs';

const seedsArg = process.argv.indexOf('--seeds');
const SEEDS = seedsArg > 0 ? +process.argv[seedsArg + 1] : 8;
const gi = process.argv.indexOf('--gpu');
const gpu = gi > 0 ? process.argv[gi + 1] : 'gl';
const NLEVELS = +/#define NLEVELS (\d+)/.exec(readFileSync(join(ROOT, 'games/destructo/src/game.h'), 'utf8'))[1];
const ALL = Array.from({ length: NLEVELS }, (_, i) => String(i));
const once = async (args) => {
  const r = await runCheck('human', args, { gpu, timeoutMin: 600 });
  if (r.status !== 0) { console.log(`FAIL check run human ${args.join(' ')} exited ${r.status}\n${r.text.slice(-1500)}`); process.exit(1); }
  return r.rows;
};
/* the options, then the levels (all when none are named), split alternately between two hosts */
const run = async (args) => {
  const opts = [], lv = [];
  for (let i = 0; i < args.length; i++) {
    if (['--seeds', '--skill'].includes(args[i])) opts.push(args[i], args[++i]);
    else if (args[i].startsWith('--')) opts.push(args[i]);
    else lv.push(args[i]);
  }
  const levels = lv.length ? lv : ALL;
  const parts = [levels.filter((_, i) => i % 2 === 0), levels.filter((_, i) => i % 2 === 1)].filter((p) => p.length);
  return (await inPairs(parts.map((p) => () => once([...opts, ...p])))).flat().sort((a, b) => a.level - b.level || a.seed - b.seed);
};
const byLevel = (rows) => {
  const m = new Map();
  for (const r of rows) { if (!m.has(r.level)) m.set(r.level, []); m.get(r.level).push(r); }
  return m;
};
const newcomer = (rows, label) => {
  const why = [];
  const lv = byLevel(rows);
  for (const [l, rs] of lv) {
    const c = rs.filter((r) => r.cleared).length;
    if (l < 3 && c < rs.length) why.push(`level ${l + 1} ${rs[0].name}: ${c} of ${rs.length} cleared (a first level: all must)`);
    else if (c < rs.length - 1) why.push(`level ${l + 1} ${rs[0].name}: ${c} of ${rs.length} cleared`);
  }
  const all = rows.filter((r) => r.cleared).length;
  if (all < rows.length * 0.97) why.push(`${all} of ${rows.length} plays cleared (under 97%)`);
  console.log(`     ${label}: ${all} of ${rows.length} plays cleared; per level: ${[...lv].map(([l, rs]) => `${l + 1}:${rs.filter((r) => r.cleared).length}`).join(' ')}`);
  return why;
};
const withinPar = (rows) => rows.filter((r) => r.cleared && r.shots <= r.par).length / rows.length;

let bad = 0;
const rows = await run(['--seeds', String(SEEDS)]);
for (const [l, rs] of byLevel(rows)) {
  const c = rs.filter((r) => r.cleared);
  console.log(`${c.length >= rs.length - (l < 3 ? 0 : 1) ? 'ok  ' : 'FAIL'} ${String(l + 1).padStart(2)} ${rs[0].name.padEnd(14)} cleared by ${c.length} of ${rs.length}; shots used ${rs.map((r) => `${r.shots}${r.cleared ? '' : 'x'}`).join(' ')} of ${rs[0].ammo} (par ${rs[0].par}); stars ${rs.map((r) => r.stars).join('')}; first crystal down on shot ${rs.map((r) => r.first_down_shot).join(' ')}`);
}
const why = newcomer(rows, 'newcomer');
if (why.length) { bad++; console.log(`FAIL newcomer: ${why.join('; ')}`); } else console.log('ok   newcomer: every first level cleared by every player, every level by 7 of 8 or more, 97% of plays');

const orbiters = (rs) => rs.filter((r) => r.orbit_ticks > 0);
{
  const o = orbiters(rows), oc = o.filter((r) => r.cleared).length;
  const ok = o.length > 0 && oc / o.length >= rows.filter((r) => r.cleared).length / rows.length - 0.05;
  if (!ok) bad++;
  const levels = [...new Set(o.map((r) => r.name))];
  console.log(`${ok ? 'ok  ' : 'FAIL'} goes round: ${o.length} plays moved the launcher round to a hidden crystal (${levels.join(', ') || 'none'}), ${oc} of them cleared`);
  const never = await run(['--seeds', String(SEEDS), '--never-moves', ...[...new Set(o.map((r) => String(r.level)))]]);
  const no = orbiters(never);
  if (no.length === 0) console.log(`ok   control (never goes round) fails its goes-round check as it must: 0 plays moved; on those levels it cleared ${never.filter((r) => r.cleared).length} of ${never.length} in ${never.reduce((a, r) => a + r.shots, 0)} shots (the newcomer: ${o.length ? rows.filter((r) => levels.includes(r.name)).reduce((a, r) => a + r.shots, 0) : 0} shots)`);
  else { bad++; console.log(`CONTROL PASSED (a failure): the never-moving player moved round in ${no.length} plays`); }
}

const skilled = await run(['--seeds', String(Math.max(2, SEEDS >> 1)), '--skill', '0.33']);
const sp = withinPar(skilled), np = withinPar(rows);
const sok = sp >= 2 / 3 && sp > np;
if (!sok) bad++;
console.log(`${sok ? 'ok  ' : 'FAIL'} skill: within par on ${(sp * 100).toFixed(0)}% of a steady player's plays against ${(np * 100).toFixed(0)}% of the newcomer's (stars: ${skilled.map((r) => r.stars).join('')})`);

const control = await run(['--seeds', String(SEEDS), '--old-damage']);
const cwhy = newcomer(control, 'control (the cannon before the fix)');
if (cwhy.length) console.log(`ok   control fails as it must: ${cwhy.join('; ')}`);
else { bad++; console.log('CONTROL PASSED (a failure): the cannon before the fix passed the newcomer check'); }
process.exit(bad ? 1 : 0);
