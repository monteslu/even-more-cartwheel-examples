// destructo landmarks gate: every landmark level (games/destructo/src/landmarks.c), checked by
// games/destructo/tools/levelcheck.c --landmarks in the check cart (destructo_check) on romdev:
//   built    as built, every block stands on something (its bottom face on a block or the ground) and none is placed
//            inside another
//   rests    once the level has settled as the game settles it before play, every block lies on something (a contact
//            under it, or a chain of whole welds to a block with one) and nothing moved more than 0.1 m settling
//   beaten   the bot, through the controls, brings every crystal down within the level's par
// Controls, each must fail its check: a block put in the air over the first landmark (as built, and again once it has
// settled, unstepped: nothing under either), two blocks welded to each other and nothing else put there the same way
// (welds alone hold nothing up), and the bot aiming 150 milliradians off.
// The physics does not draw: one run, on a GL host (--gpu webgpu: on a WebGPU host). The controls run two at a time.
// A welded member counts as standing on what it is welded to (a chain of whole welds to a block that stands or rests).
// Run: node tests/destructo-landmarks.mjs [--gpu gl|webgpu]
import { runCheck, inPairs } from './lib/romdev.mjs';

const gi = process.argv.indexOf('--gpu');
const gpu = gi > 0 ? process.argv[gi + 1] : 'gl';
const run = async (args) => {
  const r = await runCheck('levels', ['--landmarks', ...args], { gpu });
  if (r.status !== 0) { console.log(`FAIL check run levels --landmarks ${args.join(' ')} exited ${r.status}\n${r.text.slice(-1500)}`); process.exit(1); }
  return r.rows;
};
const checks = (r) => ({
  built: r.floating === 0 && r.overlapping === 0,
  rests: r.unrested === 0 && r.settle_move_mm <= 100,
  beaten: r.solved && r.shots <= r.par,
});
const at = (p) => p ? `${p.map((v) => v.toFixed(2)).join(' ')}` : '?';
const describe = (r) => `${r.level + 1} ${r.name} (${r.blocks} blocks): floating ${r.floating}, inside another ${r.overlapping}${r.overlapping ? ` (first at ${at(r.over_at)})` : ''}, lying on nothing after the settle ${r.unrested}, settled ${r.settle_move_mm.toFixed(0)} mm${r.settle_move_mm > 100 ? ` (the mover: material ${r.moved_mat} half ${at(r.moved_half)} from ${at(r.moved_from)})` : ''}; bot ${r.solved ? 'cleared it' : 'did not clear it'} in ${r.shots} shots (par ${r.par})`;
let bad = 0;
const all = await run([]);
if (all.length === 0) { console.log('FAIL no landmark levels'); process.exit(1); }
for (const r of all) {
  const why = Object.entries(checks(r)).filter(([, v]) => !v).map(([k]) => k);
  if (why.length) { bad++; console.log(`FAIL ${describe(r)}\n     fails: ${why.join(', ')}`); } else console.log(`ok   ${describe(r)}`);
}
const CONTROLS = [['float', ['built', 'rests']], ['island', ['built', 'rests']], ['solve', ['beaten']]];
const ctlRows = await inPairs(CONTROLS.map(([ctl]) => async () => (await run(['--control', ctl]))[0]));
for (const [i, [ctl, keys]] of CONTROLS.entries()) {
  const r = ctlRows[i];
  const c = checks(r);
  for (const k of keys) {
    if (!c[k]) console.log(`ok   control (${ctl}) fails its ${k} check as it must: ${describe(r)}`);
    else { bad++; console.log(`CONTROL PASSED (a failure): ${ctl} ${k}: ${describe(r)}`); }
  }
}
process.exit(bad ? 1 : 0);
