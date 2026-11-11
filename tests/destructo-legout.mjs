// destructo leg-out gate: a leg of the structure broken (levelcheck --legout), on romdev, in the check cart.
//   standing   the level left alone for 6 s: no block moves more than 2 cm
//   falls      the structure comes down (chunks let go), and its pieces are rigid while they fall: 0.75 s after a piece goes the distances
//              between its blocks have changed at most 1 cm, over at least 300 pairs (a check that measures nothing fails)
//   shatters   8 s on, the largest piece is under 10% of the structure's blocks
//   rebound    no piece rose again more than 15% of the height it fell
//   charge     a charge at the foot (Iron Lady, Parthenon, Chichen Itza): 8 s on, no lump over 6 blocks lies (only what still stands)
// Controls, each must fail its check: LOOSE=1 (every piece let go breaks into single blocks at once: no rigid piece to measure),
// NO_CHUNKS=1 (the level as loose blocks, the old behaviour: the Needle slumps while it stands alone).
// Run: node tests/destructo-legout.mjs [--gpu gl|webgpu]
import { runCheck, inPairs, gpusArg } from './lib/romdev.mjs';

const LEVELS = { needle: 1, iron: 2, parthenon: 11, chichen: 14 };
const NAMES = { 1: 'The Needle', 2: 'Iron Lady', 11: 'Parthenon', 14: 'Chichen Itza' };
const at8 = (rows, level) => rows.filter((r) => NAMES[r.level] === r.name).filter((r) => r.phase === 'legout' && r.level === level && Math.abs(r.t - 8) < 0.01).pop();
const stand = (rows, level) => rows.find((r) => r.phase === 'standing' && r.level === level);
let bad = 0;
for (const gpu of gpusArg()) {
  const run = async (args) => { const r = await runCheck('levels', args, { gpu }); if (r.status !== 0) { console.log(`FAIL check levels ${args} exited ${r.status}\n${r.text.slice(-800)}`); process.exit(1); } return r.rows; };
  const jobs = [() => run(['--legout', LEVELS.needle, LEVELS.iron]), () => run(['--legout', '--charge', LEVELS.iron, LEVELS.parthenon, LEVELS.chichen]),
    () => run(['--opt', 'LOOSE=1', '--legout', LEVELS.iron]), () => run(['--opt', 'NO_CHUNKS=1', '--legout', LEVELS.needle])];
  const [leg, charge, loose, nochunks] = await inPairs(jobs);
  const verdict = (rows, level, name) => {
    const s = stand(rows, level), e = at8(rows, level);
    return { name, s, e, standing: s.move_mm <= 20, falls: e.released > 0, rigid: e.rigid_mm <= 10 && e.pairs >= 300, shatters: e.largest <= 0.1 * e.placed_alive, rebound: e.rebound <= 0.15 && e.fallers >= 1 };
  };
  for (const [name, level] of [['The Needle', LEVELS.needle], ['Iron Lady', LEVELS.iron]]) {
    const v = verdict(leg, level, name);
    const why = ['standing', 'falls', 'rigid', 'shatters', 'rebound'].filter((k) => !v[k]);
    const line = `${gpu} ${name}: stood ${v.s.move_mm.toFixed(1)} mm; leg out: ${v.e.released} let go, rigid ${v.e.rigid_mm.toFixed(2)} mm over ${v.e.pairs} pairs, largest piece ${v.e.largest} of ${v.e.placed_alive} blocks, rebound ${(v.e.rebound * 100).toFixed(1)}% of ${v.e.fallers} pieces that fell over 1 m`;
    if (why.length) { bad++; console.log(`FAIL ${line} (fails: ${why.join(', ')})`); } else console.log(`ok   ${line}`);
  }
  for (const [name, level] of [['Iron Lady', LEVELS.iron], ['Parthenon', LEVELS.parthenon], ['Chichen Itza', LEVELS.chichen]]) {
    const e = at8(charge, level);
    const line = `${gpu} charge at the foot of ${name}: ${e.over6} lumps over 6 blocks at 8 s (largest ${e.largest}), ${e.placed_alive} of ${e.placed} blocks`;
    if (e.over6 > 0) { bad++; console.log(`FAIL ${line}`); } else console.log(`ok   ${line}`);
  }
  const lv = verdict(loose, LEVELS.iron, 'loose');
  if (lv.rigid) { bad++; console.log(`CONTROL PASSED (a failure): ${gpu} LOOSE rigid ${lv.e.rigid_mm.toFixed(2)} mm over ${lv.e.pairs} pairs`); } else console.log(`ok   control (loose bricks) fails its rigid check as it must: ${lv.e.pairs} pairs`);
  const nv = verdict(nochunks, LEVELS.needle, 'nochunks');
  if (nv.standing) { bad++; console.log(`CONTROL PASSED (a failure): ${gpu} NO_CHUNKS stood ${nv.s.move_mm.toFixed(1)} mm`); } else console.log(`ok   control (no chunks, loose blocks) fails its standing check as it must: moved ${nv.s.move_mm.toFixed(0)} mm`);
}
process.exit(bad ? 1 : 0);
