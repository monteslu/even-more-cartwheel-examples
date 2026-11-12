// Aim-view camera looks for a level with its own camera (level_def.cam): the real aim state held (SCENE_AIM), a frame per camera
// variant given as back,up,side,short,aty,fov (dm set CAM). Frames go to out/gate/scene/<name><n>-<gpu>-<t>.png.
// Run: node tests/destructo-cam-sheet.mjs [--gpu gl|webgpu] [--name cam] [--t 0] "<level>" <variant> [<variant> ...]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, ROOT } from './lib/romdev.mjs';
const a = process.argv.slice(2);
const take = (flag, def) => { const i = a.indexOf(flag); if (i < 0) return def; const v = a[i + 1]; a.splice(i, 2); return v; };
const extra = []; for (let i; (i = a.indexOf('--set')) >= 0;) extra.push(a.splice(i, 2)[1]);
const settle = Number(take('--settle', '30')), gpu = take('--gpu', 'gl'), name = take('--name', 'cam'), t = Number(take('--t', '0')), scene = take('--scene', 'dam');
const [level, ...variants] = a;
const OUT = join(ROOT, 'out/gate/scene');
mkdirSync(OUT, { recursive: true });
const h = await openCart('destructo_auto', { gpu, tag: 'scene' });
try {
  let n = 0;
  for (const v of variants) {
    n++;
    const opts = { LEVEL_NAME: level, NO_HUD: 1, QUALITY: 3, SCENE: scene, SCENE_AIM: 1, CAM: v };
    for (const kv of extra) { const [k, val] = kv.split('='); opts[k] = val; }
    for (const [k, val] of Object.entries(opts)) await h.command(`dm set ${k} "${val}"`);
    await h.command('dm restart');
    await h.step(5);
    const want = 40 + Math.round(t * 60);
    for (let k = 0; k < 40; k++) { const now = Number(await h.read('game.tick')); if (now >= want) break; await h.step(Math.min(want - now, 120)); }
    await h.step(settle); // the camera eases to its place
    await h.shot(join(OUT, `${name}${n}-${gpu}-${String(t).replace('.', '_')}.png`));
    console.log(`${name}${n} = ${v}`);
  }
} finally { await h.close(); }
