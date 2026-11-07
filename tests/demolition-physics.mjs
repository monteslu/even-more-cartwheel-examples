// demolition physics gate: what the engine's 3D physics (Box3D) does for stacked blocks and heavy shot, measured by
// build/native/games/demolition/demolition_physcheck (games/demolition/tools/physcheck.c). Each measurement has a
// control that must fail.
//   rest     the game's four kinds of stack (brick tower, a 30-block column, a 385-block pyramid, a plank scaffold)
//            left alone 60 s at the game's settings: no block moves more than 1 mm after the first 3 s, all asleep
//            within 5 s and still asleep at the end. Reported too: the same stacks with sleeping off (the solver's
//            own creep). control: the tower on a 4 degree slope with ice for friction must creep.
//   budget   all-awake colliding blocks on the job pool (best of three runs: the machine is shared): 2000 bodies (a big
//            collapse's awake count; the game caps bodies at 3000) must step within 4 ms mean.
//            control: 6000 bodies on one thread must not.
//   determ   one collapse stepped with 1, 2, 4 and every worker: one hash. control: a block 1 micrometre off must
//            hash differently.
//   tunnel   a 0.18 m shot at 30 to 200 m/s into a 2 cm glass pane, a 6 cm plank (both dynamic) and a 6 cm wall:
//            with the game's settings (bullet, 4 sub-steps) never through. Reported: bullet off (through the thin
//            dynamic targets) and 8 sub-steps (no help). control: continuous collision off must go through the wall.
// Run: node tests/demolition-physics.mjs
import { spawnSync } from 'node:child_process';
import os from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/demolition/demolition_physcheck');
const r = spawnSync(exe, [], { encoding: 'utf8', timeout: 600000 });
if (r.status !== 0) { console.log(`FAIL ${exe} exited ${r.status}\n${r.stderr}`); process.exit(1); }
const rows = r.stdout.trim().split('\n').filter((l) => l.startsWith('{')).map((l) => JSON.parse(l));
let bad = 0;
const ok = (line) => console.log(`ok   ${line}`);
const fail = (line) => { bad++; console.log(`FAIL ${line}`); };
const control = (failed, line) => (failed ? ok(`control fails as it must: ${line}`) : fail(`CONTROL PASSED (a failure): ${line}`));

for (const s of rows.filter((x) => x.m === 'rest')) {
  const line = `rest ${s.stack} (${s.blocks} blocks, ${s.tag}): drift ${s.drift_mm.toFixed(3)} mm, top speed ${s.vmax_mm_s.toFixed(2)} mm/s, asleep at ${s.asleep_s} s, awake at the end ${s.awake_end}`;
  const good = s.drift_mm <= 1 && s.asleep_s >= 0 && s.asleep_s <= 5 && s.awake_end === 0;
  if (s.tag === 'game') (good ? ok : fail)(line);
  else if (s.tag === 'control') control(!good, line);
  else console.log(`info ${line}`);
}
const bud = rows.filter((x) => x.m === 'budget');
for (const b of bud) console.log(`info budget ${b.bodies} awake bodies on ${b.threads} thread(s): ${b.mean_ms.toFixed(2)} ms mean, ${b.worst_ms.toFixed(2)} worst, ${b.contacts_max} contacts`);
const cap = bud.find((b) => b.bodies === 2000 && b.workers === 0);
const load = os.loadavg()[0], cpus = os.cpus().length;
if (!cap) fail('budget: no 2000-body run');
else if (load > cpus * 0.6 && cap.mean_ms > 4) console.log(`NOT JUDGED budget: 2000 bodies on the pool ${cap.mean_ms.toFixed(2)} ms mean, but the machine's load is ${load.toFixed(1)} on ${cpus} threads (other work holds the pool's threads): run it again when the machine is quieter`);
else (cap.mean_ms <= 4 ? ok : fail)(`budget: 2000 bodies on the pool ${cap.mean_ms.toFixed(2)} ms mean (bound 4; load ${load.toFixed(1)})`);
const big = bud.find((b) => b.bodies === 6000 && b.workers === 1);
if (!big) fail('budget: no control run'); else control(big.mean_ms > 4, `6000 bodies on one thread ${big.mean_ms.toFixed(2)} ms mean against 4`);
const d = rows.find((x) => x.m === 'determ');
if (!d) fail('determ: no run');
else {
  const same = d.w1 === d.w2 && d.w1 === d.w4 && d.w1 === d.wall;
  (same ? ok : fail)(`determinism: hash ${d.w1} with 1 worker, ${d.w2} with 2, ${d.w4} with 4, ${d.wall} with every one`);
  control(d.control !== d.w1, `a block 1 micrometre off hashes ${d.control}`);
}
for (const t of rows.filter((x) => x.m === 'tunnel')) {
  const line = `tunnel ${t.tag} ${t.target}: ${t.through} of ${t.shots} shots through (by speed 30..200 m/s: ${t.by_speed.join(' ')})`;
  if (t.tag === 'bullet') (t.through === 0 ? ok : fail)(line);
  else if (t.tag === 'control') { if (t.target === 'wall6cm') control(t.through > 0, line); else console.log(`info ${line}`); }
  else console.log(`info ${line}`);
}
process.exit(bad ? 1 : 0);
