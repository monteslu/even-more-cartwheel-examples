// dev: the two views a blind judge sees of each named landmark (aim: the level's opening view; def: the other one, orbit 0 or 0.8),
// destructo_auto on romdev at 1920x1080 (High, no HUD), cropped to 1440x880, into out/headroom/<slug>-<aim|def>.png (the 1440x880 crop is made locally: convert -gravity center -crop 1440x880+0+0)
// node tests/destructo-headroom-look.mjs [--gpu gl|webgpu] [--scene legout|boom --t 1] "<level>" ["<level>" ...]
import { mkdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, ROOT } from './lib/romdev.mjs';

const a = process.argv.slice(2);
const take = (flag, def) => { const i = a.indexOf(flag); if (i < 0) return def; const v = a[i + 1]; a.splice(i, 2); return v; };
const gpu = take('--gpu', 'gl'), scene = take('--scene', 'legout'), t = +take('--t', '0'), tag = take('--tag', '');
const OUT = join(ROOT, 'out/headroom');
mkdirSync(OUT, { recursive: true });
const src = readFileSync(join(ROOT, 'games/destructo/src/levels.c'), 'utf8');
const orbitOf = (name) => { const m = new RegExp(`LANDMARK(?:_HELD)?\\("${name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}"[^\\n]*?, (-?[\\d.]+)f\\)`).exec(src); return m ? +m[1] : 0; };
for (const level of a) {
  const slug = level.toLowerCase().replace(/[^a-z0-9]+/g, '-');
  const o = orbitOf(level);
  for (const [view, orbit] of [['aim', null], ['def', o === 0 ? 0.8 : 0]]) {
    const h = await openCart('destructo_auto', { gpu, tag: 'hrlook' });
    try {
      const opts = { LEVEL_NAME: level, NO_HUD: 1, QUALITY: 3, SCENE: scene, OVERVIEW: 1 };
      if (orbit !== null) opts.ORBIT = orbit;
      for (const [k, v] of Object.entries(opts)) await h.command(`dm set ${k} "${v}"`);
      await h.command('dm restart');
      await h.step(5);
      const want = 40 + Math.round(t * 60);
      for (let k = 0; k < 40; k++) { const now = Number(await h.read('game.tick')); if (now >= want) break; await h.step(Math.min(want - now, 120)); }
      const png = join(OUT, `${slug}${tag}-${view}.png`);
      await h.shot(png);
      console.log(`done ${slug} ${view}`);
    } finally { await h.close(); }
  }
}
