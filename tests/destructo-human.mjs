// destructo human gate: can a person new to the game clear every level? Checked by
// build/native/games/destructo/destructo_humancheck (games/destructo/tools/humancheck.c) with the human-like player
// (games/destructo/src/human.c). Unlike the bot (src/bot.c: it reads each crystal's exact place and solves the flight
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
//   control    the cannon before the fix (--old-damage: the ball breaks only the block it touches and flies on, as in
//              round 3 and before) must fail the newcomer check
// Run: node tests/destructo-human.mjs [--seeds n]
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/destructo/destructo_humancheck');
const seedsArg = process.argv.indexOf('--seeds');
const SEEDS = seedsArg > 0 ? +process.argv[seedsArg + 1] : 8;
const run = (args) => {
  const r = spawnSync(exe, args, { encoding: 'utf8', timeout: 3600000, maxBuffer: 64 << 20 });
  if (r.status !== 0) { console.log(`FAIL ${exe} ${args.join(' ')} exited ${r.status}`); process.exit(1); }
  return r.stdout.trim().split('\n').filter((l) => l.startsWith('{')).map((l) => JSON.parse(l));
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
const rows = run(['--seeds', String(SEEDS)]);
for (const [l, rs] of byLevel(rows)) {
  const c = rs.filter((r) => r.cleared);
  console.log(`${c.length >= rs.length - (l < 3 ? 0 : 1) ? 'ok  ' : 'FAIL'} ${String(l + 1).padStart(2)} ${rs[0].name.padEnd(14)} cleared by ${c.length} of ${rs.length}; shots used ${rs.map((r) => `${r.shots}${r.cleared ? '' : 'x'}`).join(' ')} of ${rs[0].ammo} (par ${rs[0].par}); stars ${rs.map((r) => r.stars).join('')}; first crystal down on shot ${rs.map((r) => r.first_down_shot).join(' ')}`);
}
const why = newcomer(rows, 'newcomer');
if (why.length) { bad++; console.log(`FAIL newcomer: ${why.join('; ')}`); } else console.log('ok   newcomer: every first level cleared by every player, every level by 7 of 8 or more, 97% of plays');

const skilled = run(['--seeds', String(Math.max(2, SEEDS >> 1)), '--skill', '0.33']);
const sp = withinPar(skilled), np = withinPar(rows);
const sok = sp >= 2 / 3 && sp > np;
if (!sok) bad++;
console.log(`${sok ? 'ok  ' : 'FAIL'} skill: within par on ${(sp * 100).toFixed(0)}% of a steady player's plays against ${(np * 100).toFixed(0)}% of the newcomer's (stars: ${skilled.map((r) => r.stars).join('')})`);

const control = run(['--seeds', String(SEEDS), '--old-damage']);
const cwhy = newcomer(control, 'control (the cannon before the fix)');
if (cwhy.length) console.log(`ok   control fails as it must: ${cwhy.join('; ')}`);
else { bad++; console.log('CONTROL PASSED (a failure): the cannon before the fix passed the newcomer check'); }
process.exit(bad ? 1 : 0);
