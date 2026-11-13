// dev: the real aim view of each named level (the level's own camera, no HUD, High), one frame each: node tests/destructo-lm-sheet.mjs <out dir> <prefix> [--gpu gl|webgpu] [--t seconds] "<level>" ...
// DESTRUCTO_CART_DIR points at another build's carts (before and after sheets)
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { openCart } from './lib/romdev.mjs';
const a = process.argv.slice(2);
const take = (flag, def) => { const i = a.indexOf(flag); if (i < 0) return def; const v = a[i + 1]; a.splice(i, 2); return v; };
const gpu = take('--gpu', 'gl'), t = Number(take('--t', '0')), cam = take('--cam', '');
const [out, prefix, ...levels] = a;
mkdirSync(resolve(out), { recursive: true });
const h = await openCart('destructo_auto', { gpu, tag: 'lmsheet' });
try {
  for (const level of levels) {
    const opts = { LEVEL_NAME: level, NO_HUD: 1, QUALITY: 3, SCENE_AIM: 1, ...(cam ? { CAM: cam } : {}) };
    for (const [k, v] of Object.entries(opts)) await h.command(`dm set ${k} "${v}"`);
    await h.command('dm restart');
    await h.step(5);
    const want = 40 + Math.round(t * 60);
    for (let k = 0; k < 40; k++) { const now = Number(await h.read('game.tick')); if (now >= want) break; await h.step(Math.min(want - now, 120)); }
    await h.step(30);
    const f = join(resolve(out), `${prefix}-${level.replace(/[^A-Za-z]+/g, '_')}-${gpu}.png`);
    await h.shot(f);
    console.log('shot', f);
  }
} finally { await h.close(); }
