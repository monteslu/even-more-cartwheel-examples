// destructo's leg of the depth-fighting scan (tests/coplanar.mjs says what it checks): every level as a cart on romdev
// (destructo_auto: dm set LEVEL n, dm restart, 360 frames in one range), then the debug command `coplanar`
// (games/common/coplanar.c): the overlapping same-plane pairs among every drawn depth-tested mesh and instance, by
// region. Every level must have none, but for the settled blocks that lean on each other, held to the counts measured
// for the 30 kit levels (by name: SETTLED in tests/coplanar.mjs, moved here when destructo left that native run).
// CONTROLS, which must fail: `coplanar selftest` (the detector on made-up faces) and the first level with a doubled face
// added to the real scene (dm set COPLANAR_CONTROL 1), which the scan must find.
// --unfixed: every level as built before the separation (dm set COPLANAR_SEPARATE 0).
// Two hosts at a time; the scan reads geometry, so one GPU path is enough (--gpu webgpu: on a WebGPU host).
// Run: node tests/destructo-coplanar.mjs [--unfixed] [--gpu gl|webgpu]
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, inPairs, ROOT } from './lib/romdev.mjs';

const UNFIXED = process.argv.includes('--unfixed');
const gi = process.argv.indexOf('--gpu');
const gpu = gi > 0 ? process.argv[gi + 1] : 'gl';
const src = readFileSync(join(ROOT, 'games/destructo/src/levels.c'), 'utf8');
const table = src.slice(src.indexOf('const level_def LEVELS'));
const NAMES = [...table.slice(0, table.indexOf('\n};')).matchAll(/(?:LANDMARK\(|\{ )"([^"]+)"/g)].map((m) => m[1]);
// Destructo's blocks are physics bodies. The level lays them 8 mm apart and they settle onto each other in the first
// seconds; settled, a block leans a few milliradians and sits a few millimetres off its neighbours, and two faces that
// lean against each other cross somewhere whatever layers they are drawn on. The game draws each block proud by a
// layer per axis that clears its settled neighbours (games/destructo/src/view.c, block_layers): that takes the settled
// structures from 81,015 pairs over the 30 levels to 2,009, where a leaning face crosses its neighbour's.
// They are held to these counts (a quarter over, and ten), so a change that brings the flush faces back fails.
// the kits' settled counts (measured 18 30 34 20 0 1 0 44 39 42 0 0 166 6 65 70 2 27 272 2 0 155 0 87 411 152 0 17 0 349, a quarter over and ten)
const KIT_COUNTS = [32, 47, 52, 35, 0, 11, 0, 65, 58, 62, 0, 0, 217, 17, 91, 97, 12, 43, 350, 12, 0, 203, 0, 118, 523, 200, 0, 31, 0, 446];
const KIT_NAMES = ['First Stones', 'Lookout', 'Garden Wall'];
const kitStart = NAMES.indexOf(KIT_NAMES[0]);
const SETTLED = Object.fromEntries(KIT_COUNTS.map((n, i) => [NAMES[kitStart + i], n]));

async function scan(level, opts, cmds) {
  const h = await openCart('destructo_auto', { gpu, tag: `coplanar-${level}` });
  try {
    for (const [k, v] of Object.entries({ LEVEL: level, ...(UNFIXED ? { COPLANAR_SEPARATE: 0 } : {}), ...opts })) await h.command(`dm set ${k} ${v}`);
    await h.command('dm restart');
    await h.step(360 - h.frame);
    const out = [];
    for (const c of cmds) out.push(await h.command(c, 60));
    out.push(await h.read('game.level'));
    return out;
  } catch (e) { return e; } finally { await h.close(); }
}
const jobs = NAMES.map((name, i) => () => scan(i, {}, ['coplanar']));
jobs.push(() => scan(0, { COPLANAR_CONTROL: 1 }, ['coplanar selftest', 'coplanar']));
const res = await inPairs(jobs);
let bad = 0, total = 0;
for (const [i, name] of NAMES.entries()) {
  const r = res[i];
  if (r instanceof Error) { bad++; console.log(`FAIL level ${i + 1} ${name}: ${r.message}`); continue; }
  const [sc, lv] = r;
  total += sc.pairs;
  const where = `level ${i + 1} ${name} (game.level ${lv}): ${sc.meshes} meshes, ${sc.faces} faces`;
  if (lv !== i) { bad++; console.log(`FAIL ${where}: the cart is on another level`); continue; }
  if (!sc.faces) { bad++; console.log(`FAIL ${where}: nothing drawn to check`); continue; }
  if (!sc.pairs) { console.log(`ok   ${where}, no pairs${sc.moved ? ` (${sc.moved} vertices moved apart at the build)` : ''}`); continue; }
  const allow = UNFIXED ? undefined : SETTLED[name];
  if (allow !== undefined && sc.pairs <= allow) { console.log(`ok   ${where}, ${sc.pairs} pairs of leaning settled blocks (at most ${allow})`); continue; }
  bad++;
  console.log(`FAIL ${where}, ${sc.pairs} overlapping same-plane pairs${allow === undefined ? ' (no allowance: a landmark)' : ` (at most ${allow})`}:`);
  for (const g of sc.regions.slice(0, 8)) console.log(`       ${String(g.pairs).padStart(6)}  ${g.name}  (widest ${g.width_mm} mm, at [${g.at.join(', ')}])`);
}
{
  const r = res[NAMES.length];
  if (r instanceof Error) { bad++; console.log(`FAIL the controls did not run: ${r.message}`); }
  else {
    const [st, sc] = r, found = sc.regions.find((g) => g.name.includes('COPLANAR_CONTROL'));
    if (!st.selftest) { bad++; console.log(`FAIL the detector's selftest: ${st.why}`); } else console.log('ok   the detector\'s selftest (a doubled face, a laid face and a 0.5 mm gap count; a shared edge, a 5 mm gap and a face turned away do not)');
    if (!found) { bad++; console.log('CONTROL PASSED (a failure): level 1 with a doubled face: it went unseen'); } else console.log(`ok   control fails as it must (level 1, dm set COPLANAR_CONTROL 1): ${found.pairs} pairs in the doubled face`);
  }
}
console.log(`pairs: destructo ${total}${UNFIXED ? ' (as built, COPLANAR_SEPARATE 0)' : ''}`);
process.exit(bad ? 1 : 0);
