// The look sheets of a collapse: destructo_auto on romdev at 1920x1080 (High, no HUD), the level, a scene (dm set SCENE legout:
// a foot of the structure broken at tick 40; boom: a charge at its middle), frames at t = 0, 0.5, 1, 2 and 4 s after the blow.
// Frames go to out/gate/scene/<name>-<gpu>-<t>.png (native size; the sheet's crops are made from them).
// Run: node tests/destructo-scene-sheet.mjs [--gpu gl|webgpu] [--orbit R] [--times 0,0.5,1,2,4] "<level>" <legout|boom> [name]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, ROOT } from './lib/romdev.mjs';

const a = process.argv.slice(2);
const take = (flag, def) => { const i = a.indexOf(flag); if (i < 0) return def; const v = a[i + 1]; a.splice(i, 2); return v; };
const gpu = take('--gpu', 'gl'), orbit = take('--orbit', null), times = take('--times', '0,0.5,1,2,4').split(',').map(Number);
const [level, scene, name = level.toLowerCase().replace(/[^a-z0-9]+/g, '-') + '-' + scene] = a;
const OUT = join(ROOT, 'out/gate/scene');
mkdirSync(OUT, { recursive: true });
const h = await openCart('destructo_auto', { gpu, tag: 'scene' });
try {
  const opts = { LEVEL_NAME: level, NO_HUD: 1, QUALITY: 3, SCENE: scene, OVERVIEW: 1 };
  if (orbit !== null) opts.ORBIT = orbit;
  for (const [k, v] of Object.entries(opts)) await h.command(`dm set ${k} "${v}"`);
  await h.command('dm restart');
  await h.step(5);
  // the game's tick (game.tick) counts the blow at 40; frames are stepped until it reads the tick wanted
  const goto = async (t) => { for (let k = 0; k < 40; k++) { const now = Number(await h.read('game.tick')); if (now >= t) return now; await h.step(Math.min(t - now, 120)); } throw new Error('tick ' + t + ' not reached'); };
  for (const t of times) {
    const at = await goto(40 + Math.round(t * 60));
    await h.shot(join(OUT, `${name}-${gpu}-${String(t).replace('.', '_')}.png`));
    if (at !== 40 + Math.round(t * 60)) console.log(`  (t=${t}: at tick ${at})`);
  }
  console.log(`done ${name} ${gpu}`);
} finally { await h.close(); }
