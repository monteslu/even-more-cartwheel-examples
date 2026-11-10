// destructo look gate: one frame of each setting (a tower in the highlands, a wall in a meadow, a bridge over a
// river gorge, a castle on sea cliffs, a scaffold in a quarry, a pyramid among desert mesas, a keep in a snowy pass,
// the Iron Lady in the city, the Harbour Sails on the harbour), as the player aims, without the HUD:
// destructo_auto as a cart on romdev at 1920x1080 on the High preset, on GL and on WebGPU, the level and the options
// set over the debug channel (dm set LEVEL_NAME ..., NO_HUD, QUALITY; dm restart). Each frame's luma (Rec. 709 of the
// sRGB values, 0..255) must average 95 to 125 and spread with a standard deviation of 55 or more: lit, contrasty,
// never washed out. Control, which must fail: the same frame at three times the exposure (dm set LOOK_CONTROL 1).
// The frames stay in out/look/ for review. Two hosts at a time.
// Run: node tests/destructo-look.mjs [--no-control] [--gpu gl|webgpu]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { luma } from './lib-luma.mjs';
import { openCart, inPairs, gpusArg, ROOT } from './lib/romdev.mjs';

const OUT = join(ROOT, 'out/look');
mkdirSync(OUT, { recursive: true });
const KITS = [['tower', 'First Stones'], ['wall', 'Garden Wall'], ['bridge', 'Mill Bridge'], ['castle', 'The Gatehouse'], ['scaffold', 'Glass House'],
  ['pyramid', 'Step Shrine'], ['keep', 'Old Keep'], ['city', 'Iron Lady'], ['harbour', 'Harbour Sails']];
async function shot(gpu, level, file, opts = {}) {
  const h = await openCart('destructo_auto', { gpu, tag: 'look' });
  try {
    for (const [k, v] of Object.entries({ LEVEL_NAME: level, NO_HUD: 1, QUALITY: 3, ...opts })) await h.command(`dm set ${k} "${v}"`);
    await h.command('dm restart');
    await h.step(50);
    await h.shot(file);
    return { status: 0, ...luma(file) };
  } catch (e) {
    return { status: 1, mean: NaN, sd: NaN, err: e.message };
  } finally { await h.close(); }
}
const ok = (r) => r.status === 0 && r.mean >= 95 && r.mean <= 125 && r.sd >= 55;
let bad = 0;
for (const gpu of gpusArg()) {
  const jobs = KITS.map(([kit, level]) => () => shot(gpu, level, join(OUT, `${kit}-${gpu}.png`)));
  if (!process.argv.includes('--no-control')) jobs.push(() => shot(gpu, KITS[0][1], join(OUT, `control-${gpu}.png`), { LOOK_CONTROL: 1 }));
  const res = await inPairs(jobs);
  for (const [i, [kit, level]] of KITS.entries()) {
    const r = res[i];
    const line = `${gpu} ${kit} (${level}): luma mean ${r.mean.toFixed(1)}, deviation ${r.sd.toFixed(1)}${r.err ? ` (${r.err})` : ''}`;
    if (ok(r)) console.log(`ok   ${line}`); else { bad++; console.log(`FAIL ${line}: mean 95..125 and deviation 55 or more`); }
  }
  if (res.length > KITS.length) {
    const r = res[KITS.length];
    const line = `${gpu} control, three times the exposure: luma mean ${r.mean.toFixed(1)}, deviation ${r.sd.toFixed(1)}`;
    if (r.status !== 0) { bad++; console.log(`FAIL ${line}: the control did not run (${r.err})`); }
    else if (ok(r)) { bad++; console.log(`CONTROL PASSED (a failure): ${line}`); } else console.log(`ok   control fails as it must: ${line}`);
  }
}
process.exit(bad ? 1 : 0);
