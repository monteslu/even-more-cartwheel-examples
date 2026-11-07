// skate physics gate: the board controller (games/skate/src/skater.c) against ramp physics, natively, no GPU
// (build/native/games/skate/skate_check phys). The same shots go to two solvers:
//   game   substeps of at most 5 cm, swept contact, smooth normals from the ramps' corners: must pass every check
//   naive  the control: one discrete step a tick, faceted normals, no sweep (what a rigid body stepped once a tick on the
//          park's mesh does): it MUST fail, or the shots could not tell a tunnel from a landing
// The checks: a quarter pipe's apex is v^2 / 2g within 1.5% at 5 to 13 m/s and it gives the speed back within 1.5%
// (no friction); no tick in the air riding a transition; a 10 m/s ride up a 3.5 m quarter with vert flies out over
// the coping, comes back down onto the ramp (not the deck, no bail) and keeps its speed within 2%; 108 shots at 12 to
// 27 m/s and 10 to 55 degrees into a landing slope: none ends up under it; a rail locks the board on from up to 0.5 m
// and 0.9 rad off its line, and never from 0.9 m or more.
// Also reported, not judged: box3d, the engine's own rigid bodies on the same meshes (a frictionless ball, 60 Hz, 4
// substeps, continuous collision): it does not tunnel, but it loses half the height a quarter pipe should give and
// spends hundreds of ticks bouncing off the facets of a transition. That is why the board has its own controller.
// Run: node tests/skate-physics.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/skate/skate_check');
const r = spawnSync(exe, ['phys'], { encoding: 'utf8' });
if (r.status !== 0) { console.log(`FAIL ${exe} exited ${r.status}\n${r.stderr}`); process.exit(1); }
const lines = r.stdout.trim().split('\n').filter((l) => l.startsWith('{')).map((l) => JSON.parse(l));

function judge(s) {
  const why = [];
  if (s.apex_err > 0.015) why.push(`apex off v^2/2g by ${(s.apex_err * 100).toFixed(2)}%`);
  if (s.back_err > 0.015) why.push(`speed back off by ${(s.back_err * 100).toFixed(2)}%`);
  if (s.hop_ticks) why.push(`${s.hop_ticks} ticks in the air riding a transition`);
  if (s.bails) why.push(`${s.bails} bails riding a quarter pipe`);
  if (s.vert.bail || s.vert.deck) why.push(`vert air ${s.vert.bail ? 'bailed' : 'came down on the deck'}`);
  if (Math.abs(s.vert.apex / s.vert.want - 1) > 0.02) why.push(`vert apex ${s.vert.apex.toFixed(2)} m, want ${s.vert.want.toFixed(2)}`);
  if (Math.abs(s.vert.back / 10 - 1) > 0.02) why.push(`vert gave back ${s.vert.back.toFixed(2)} m/s of 10`);
  if (s.slope.through) why.push(`${s.slope.through} of ${s.slope.total} shots went through the landing slope`);
  if (s.rail.locked < s.rail.of) why.push(`a rail locked ${s.rail.locked} of ${s.rail.of} shots within reach`);
  if (s.rail.wrong) why.push(`a rail locked ${s.rail.wrong} shots from out of reach`);
  return why;
}

let bad = 0;
const b3 = lines.find((s) => s.solver === 'box3d');
if (b3) console.log(`info box3d (the engine's rigid bodies, a frictionless 12 cm ball on the same meshes, for the record): apex off v^2/2g by ${(b3.apex_err * 100).toFixed(1)}%, ${b3.off_ramp_ticks} ticks bouncing off a transition it should hug, slope ${b3.slope.through}/${b3.slope.total} through`);
for (const s of lines.filter((s) => s.solver !== 'box3d')) {
  const why = judge(s);
  const line = `${s.solver}: apex ${(s.apex_err * 100).toFixed(2)}%, back ${(s.back_err * 100).toFixed(2)}%, hops ${s.hop_ticks}, vert ${s.vert.apex.toFixed(2)}/${s.vert.want.toFixed(2)} m back ${s.vert.back.toFixed(2)} m/s, slope ${s.slope.through}/${s.slope.total} through, rail ${s.rail.locked}/${s.rail.of} (${s.rail.wrong} wrong)`;
  if (s.solver === 'game') {
    if (why.length) { bad++; console.log(`FAIL ${line}\n     ${why.join('; ')}`); } else console.log(`ok   ${line}`);
  } else if (why.length) console.log(`ok   control fails as it must: ${line}\n     ${why.join('; ')}`);
  else { bad++; console.log(`CONTROL PASSED (a failure): ${line}`); }
}
if (lines.length !== 3) { console.log(`FAIL expected three solvers, got ${lines.length}`); bad++; }
process.exit(bad ? 1 : 0);
