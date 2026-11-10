// destructo landmarks gate: every landmark level (games/destructo/src/landmarks.c), checked by
// build/native/games/destructo/destructo_levelcheck --landmarks:
//   built    as built, every block stands on something (its bottom face on a block or the ground) and none is placed
//            inside another
//   rests    once the level has settled as the game settles it before play, every block lies on something (a contact
//            under it) and nothing moved more than 0.6 m settling
//   beaten   the bot, through the controls, brings every crystal down within the level's par
// Controls, each must fail its check: a block put in the air over the first landmark (as built, and again once it has
// settled, unstepped: nothing under either), and the bot aiming 150 milliradians off.
// Run: node tests/destructo-landmarks.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/destructo/destructo_levelcheck');
const run = (args) => {
  const r = spawnSync(exe, ['--landmarks', ...args], { encoding: 'utf8', timeout: 1800000 });
  if (r.status !== 0) { console.log(`FAIL ${exe} --landmarks ${args.join(' ')} exited ${r.status}`); process.exit(1); }
  return r.stdout.trim().split('\n').filter((l) => l.startsWith('{')).map((l) => JSON.parse(l));
};
const checks = (r) => ({
  built: r.floating === 0 && r.overlapping === 0,
  rests: r.unrested === 0 && r.settle_move_mm <= 600,
  beaten: r.solved && r.shots <= r.par,
});
const describe = (r) => `${r.level + 1} ${r.name} (${r.blocks} blocks): floating ${r.floating}, inside another ${r.overlapping}, lying on nothing after the settle ${r.unrested}, settled ${r.settle_move_mm.toFixed(0)} mm; bot ${r.solved ? 'cleared it' : 'did not clear it'} in ${r.shots} shots (par ${r.par})`;
let bad = 0;
const all = run([]);
if (all.length === 0) { console.log('FAIL no landmark levels'); process.exit(1); }
for (const r of all) {
  const why = Object.entries(checks(r)).filter(([, v]) => !v).map(([k]) => k);
  if (why.length) { bad++; console.log(`FAIL ${describe(r)}\n     fails: ${why.join(', ')}`); } else console.log(`ok   ${describe(r)}`);
}
for (const [ctl, keys] of [['float', ['built', 'rests']], ['solve', ['beaten']]]) {
  const r = run(['--control', ctl])[0];
  const c = checks(r);
  for (const k of keys) {
    if (!c[k]) console.log(`ok   control (${ctl}) fails its ${k} check as it must: ${describe(r)}`);
    else { bad++; console.log(`CONTROL PASSED (a failure): ${ctl} ${k}: ${describe(r)}`); }
  }
}
process.exit(bad ? 1 : 0);
