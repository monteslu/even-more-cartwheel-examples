// demolition levels gate: every level, checked by build/native/games/demolition/demolition_levelcheck
// (games/demolition/tools/levelcheck.c):
//   floats    every block as placed stands on another block or the ground, and none is placed inside another
//   rest      the level settles 3 s (as the game does before play; nothing may move more than 0.6 m doing it: the
//             tallest towers squeeze down about 0.4 m), then 60 s alone: no block moves more than 5 mm, nothing
//             breaks, no crystal moves, and everything is asleep at the end
//   solve     the bot, through the controls, brings every crystal down within the level's par
// Controls, each must fail its check: a floating block and two blocks placed in one another (level 1), a plank on a post
// 3 m up in level 1's yard with its middle 1.8 m out over nothing (it tips while settling), the bot aiming 150 milliradians off (8.6 degrees) (level 2).
// Run: node tests/demolition-levels.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/demolition/demolition_levelcheck');
const run = (args) => {
  const r = spawnSync(exe, args, { encoding: 'utf8', timeout: 1800000 });
  if (r.status !== 0) { console.log(`FAIL ${exe} ${args.join(' ')} exited ${r.status}`); process.exit(1); }
  return r.stdout.trim().split('\n').filter((l) => l.startsWith('{')).map((l) => JSON.parse(l));
};
const checks = (r) => ({
  floats: r.floating === 0 && r.overlapping === 0,
  rest: r.settle_move_mm <= 600 && r.drift_mm <= 5 && r.broken_settle === 0 && r.targets_moved === 0 && r.awake_end === 0,
  solve: r.solved && r.shots <= r.par,
});
const describe = (r) => `${r.level + 1} ${r.name} (${r.blocks} blocks, ${r.targets} crystals): floating ${r.floating}, overlapping ${r.overlapping}, settled ${r.settle_move_mm.toFixed(0)} mm, then drift ${r.drift_mm.toFixed(2)} mm, awake ${r.awake_end}, broken ${r.broken_settle}; bot ${r.solved ? 'cleared it' : 'did not clear it'} in ${r.shots} shots (par ${r.par}), ${r.broken} broke, ${r.max_awake} awake at most, step worst ${r.step_ms_max} ms`;
let bad = 0;
for (const r of run([])) {
  const c = checks(r), why = Object.entries(c).filter(([, v]) => !v).map(([k]) => k);
  if (why.length) { bad++; console.log(`FAIL ${describe(r)}\n     fails: ${why.join(', ')}`); } else console.log(`ok   ${describe(r)}`);
}
for (const [ctl, check] of [['float', 'floats'], ['rest', 'rest'], ['solve', 'solve']]) {
  const r = run(['--control', ctl])[0];
  if (!checks(r)[check]) console.log(`ok   control (${ctl}) fails its ${check} check as it must: ${describe(r)}`);
  else { bad++; console.log(`CONTROL PASSED (a failure): ${ctl}: ${describe(r)}`); }
}
process.exit(bad ? 1 : 0);
