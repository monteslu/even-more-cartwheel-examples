// pinball physics gate: the ball solver must never let the ball through a thin wall or a moving flipper, and a
// flipper shot's speed must follow where the ball sat on the flipper (rigid-body theory: (1 + e) w d on the first
// contact). Runs build/native/games/pinball/pinball_physcheck, which fires the same shots at the game's solver, the
// engine's Box2D and Box3D worlds and a naive discrete solver (games/pinball/tools/physcheck.c).
//   game   must pass every check
//   naive  the control: one discrete step a tick; it MUST fail (or the shots could not tell a tunnel)
//   box2d, box3d  judged the same way and reported: the answer to "is the engine's physics enough for pinball?"
// Run: node tests/pinball-physics.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/pinball/pinball_physcheck');
const r = spawnSync(exe, [], { encoding: 'utf8' });
if (r.status !== 0) { console.log(`FAIL ${exe} exited ${r.status}\n${r.stderr}`); process.exit(1); }
const out = JSON.parse(r.stdout.trim().split('\n').pop());
const expect = out.cradle_expect;

function judge(s) {
  const why = [];
  if (s.wall[0]) why.push(`through the wall in ${s.wall[0]} of ${s.wall[1]} shots`);
  if (s.flipper[0]) why.push(`through the firing flipper in ${s.flipper[0]} of ${s.flipper[1]} shots`);
  if (s.cradle_tunnels) why.push(`through the flipper from the cradle in ${s.cradle_tunnels} of 7`);
  const sp = s.cradle_speed;
  sp.forEach((v, i) => { if (v < 0.9 * expect[i]) why.push(`cradle ${i + 1} cm: ${v.toFixed(2)} m/s, under 0.9 x ${expect[i].toFixed(2)}`); });
  const tip = sp[6] / expect[6];
  if (tip < 0.85 || tip > 1.2) why.push(`tip shot ${sp[6].toFixed(2)} m/s is ${tip.toFixed(2)} x theory`);
  if (!(sp[6] > sp[3] && sp[3] > sp[0])) why.push(`shot speed does not grow from base to tip (${sp[0].toFixed(2)}, ${sp[3].toFixed(2)}, ${sp[6].toFixed(2)})`);
  return why;
}

let bad = 0;
for (const [name, s] of Object.entries(out.solvers)) {
  const why = judge(s);
  const line = `${name}: wall ${s.wall[0]}/${s.wall[1]} through, flipper ${s.flipper[0]}/${s.flipper[1]}, cradle ${s.cradle_tunnels}/7, shot m/s ${s.cradle_speed.map((v) => v.toFixed(2)).join(' ')}`;
  if (name === 'game') {
    if (why.length) { bad++; console.log(`FAIL ${line}\n     ${why.join('; ')}`); } else console.log(`ok   ${line}`);
  } else if (name === 'naive') {
    if (why.length) console.log(`ok   control fails as it must: ${line}`); else { bad++; console.log(`CONTROL PASSED (a failure): ${line}`); }
  } else console.log(`info ${why.length ? 'not enough' : 'enough'} for pinball: ${line}${why.length ? `\n     ${why.join('; ')}` : ''}`);
}
console.log(`     theory (1 + e) w d: ${expect.map((v) => v.toFixed(2)).join(' ')}; game's deepest overlap ${out.game_max_overlap_mm} mm`);
if (out.game_max_overlap_mm > 0.1) { bad++; console.log('FAIL the game solver left the ball more than 0.1 mm inside a surface'); }
process.exit(bad ? 1 : 0);
