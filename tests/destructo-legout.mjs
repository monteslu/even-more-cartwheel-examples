// destructo leg-out gate: a leg of the structure broken (levelcheck --legout), on romdev, in the check cart.
//   standing   the level left alone for 6 s: no block moves more than 2 cm
//   falls      the structure comes down (chunks let go), and its pieces are rigid while they fall: 0.75 s after a piece goes the distances
//              between its blocks have changed at most 1 cm, over at least 300 pairs (a check that measures nothing fails)
//   shatters   8 s on, the largest piece is under 10% of the structure's blocks
//   rebound    no piece rose again more than 15% of the height it fell
//   charge     a charge at the foot (Iron Lady, Parthenon, Chichen Itza): 8 s on, no lump over 6 blocks lies (only what still stands)
//   hollow     the tall buildings (Black Tower, Empire, Spire, Great Clock, Pisa, Needle, The Tower) are a frame: the share of their volume in
//              blocks thicker than 45 cm in every direction (a solid mass) is under 10%
//   core out   the whole half of the footprint under 2.2 m broken (LEG_HALF=1): the storeys over it come down (a chunk lets go) and 8 s on the largest
//              piece is under 10% of the blocks, for the same buildings and the Redeemer, the Statue and Angkor Wat
//   the frames: at 4 s over 60% of the blocks have moved over 1 m or are gone (what the pictures show)
// Controls, each must fail its check: NO_SUPPORT=1 (nothing is judged, the building stands as the solid core did), SOLIDCORE=1 (a solid block in the middle of the Black Tower: hollow fails), LOOSE=1 (every piece let go breaks into single blocks at once: no rigid piece to measure),
// NO_CHUNKS=1 (the level as loose blocks, the old behaviour: the Needle slumps while it stands alone).
// Run: node tests/destructo-legout.mjs [--gpu gl|webgpu]
import { runCheck, inPairs, gpusArg } from './lib/romdev.mjs';

const TALL = { 3: 'The Black Tower', 4: 'The Empire', 8: 'The Spire', 9: 'The Great Clock', 10: 'Leaning Tower of Pisa', 1: 'The Needle', 17: 'The Tower' };
const HALF = { ...TALL, 15: 'Statue of Liberty', 16: 'The Redeemer', 13: 'Angkor Wat' };
const LEVELS = { needle: 1, iron: 2, parthenon: 11, chichen: 14 };
const NAMES = { 1: 'The Needle', 2: 'Iron Lady', 11: 'Parthenon', 14: 'Chichen Itza' };
const at8 = (rows, level) => rows.filter((r) => NAMES[r.level] === r.name).filter((r) => r.phase === 'legout' && r.level === level && Math.abs(r.t - 8) < 0.01).pop();
const stand = (rows, level) => rows.find((r) => r.phase === 'standing' && r.level === level);
let bad = 0;
for (const gpu of gpusArg()) {
  const run = async (args) => { const r = await runCheck('levels', args, { gpu }); if (r.status !== 0) { console.log(`FAIL check levels ${args} exited ${r.status}\n${r.text.slice(-800)}`); process.exit(1); } return r.rows; };
  const jobs = [() => run(['--legout', LEVELS.needle, LEVELS.iron]), () => run(['--legout', '--charge', LEVELS.iron, LEVELS.parthenon, LEVELS.chichen]),
    () => run(['--legout', ...Object.keys(TALL)]), () => run(['--opt', 'LEG_HALF=1', '--legout', ...Object.keys(HALF)]), () => run(['--opt', 'SOLIDCORE=1', '--legout', 3]), () => run(['--opt', 'NO_SUPPORT=1', '--opt', 'LEG_HALF=1', '--legout', 3]),
    () => run(['--opt', 'LOOSE=1', '--legout', LEVELS.iron]), () => run(['--opt', 'NO_CHUNKS=1', '--legout', LEVELS.needle])];
  const [leg, charge, tall, half, solid, nosup, loose, nochunks] = await inPairs(jobs);
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
  for (const [lv, name] of Object.entries(TALL)) {
    const s = tall.find((r) => r.phase === 'standing' && r.level === +lv && r.name === name);
    if (!s) { bad++; console.log(`FAIL ${gpu} ${name}: no row`); continue; }
    if (s.mass_frac > 0.1) { bad++; console.log(`FAIL ${gpu} ${name}: ${(s.mass_frac * 100).toFixed(0)}% of its volume is solid mass (under 10%)`); } else console.log(`ok   ${gpu} ${name}: ${(s.mass_frac * 100).toFixed(1)}% solid mass`);
  }
  for (const [lv, name] of Object.entries(HALF)) {
    const e = half.filter((r) => r.phase === 'legout' && r.level === +lv && r.name === name && Math.abs(r.t - 8) < 0.01).pop();
    const e4 = half.filter((r) => r.phase === 'legout' && r.level === +lv && r.name === name && Math.abs(r.t - 4) < 0.01).pop();
    const line = `${gpu} core out, ${name}: ${e.released} let go, ${(e4.moved * 100).toFixed(0)}% of its blocks moved over 1 m or gone at 4 s, largest piece ${e.largest} of ${e.placed_alive} blocks at 8 s`;
    if (e.released < 1 || e.largest > 0.1 * e.placed_alive || e4.moved < 0.6) { bad++; console.log(`FAIL ${line}`); } else console.log(`ok   ${line}`);
  }
  const sv = solid.find((r) => r.phase === 'standing');
  if (sv.mass_frac <= 0.1) { bad++; console.log(`CONTROL PASSED (a failure): ${gpu} solid core ${(sv.mass_frac * 100).toFixed(0)}%`); } else console.log(`ok   control (a solid core) fails the hollow check as it must: ${(sv.mass_frac * 100).toFixed(0)}% solid`);
  const ns = nosup.filter((r) => r.phase === 'legout' && Math.abs(r.t - 4) < 0.01).pop();
  if (ns.moved >= 0.6) { bad++; console.log(`CONTROL PASSED (a failure): ${gpu} support graph off, ${(ns.moved * 100).toFixed(0)}% moved`); } else console.log(`ok   control (support graph off: the old solid stand) fails the core-out check as it must: ${(ns.moved * 100).toFixed(0)}% moved`);
  const lv = verdict(loose, LEVELS.iron, 'loose');
  if (lv.rigid) { bad++; console.log(`CONTROL PASSED (a failure): ${gpu} LOOSE rigid ${lv.e.rigid_mm.toFixed(2)} mm over ${lv.e.pairs} pairs`); } else console.log(`ok   control (loose bricks) fails its rigid check as it must: ${lv.e.pairs} pairs`);
  const nv = verdict(nochunks, LEVELS.needle, 'nochunks');
  if (nv.standing) { bad++; console.log(`CONTROL PASSED (a failure): ${gpu} NO_CHUNKS stood ${nv.s.move_mm.toFixed(1)} mm`); } else console.log(`ok   control (no chunks, loose blocks) fails its standing check as it must: moved ${nv.s.move_mm.toFixed(0)} mm`);
}
process.exit(bad ? 1 : 0);
