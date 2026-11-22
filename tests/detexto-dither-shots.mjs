// Detexto's look trial: stills of each look (the ASCII pass and the 1-bit dither methods, games/detexto/src/dither.h)
// at 1920x1080 through romdev, on GL or WebGPU. Scenes: the title (detexto, the shell's title over the structure), play
// (detexto_auto aiming at its first level), collapse, dust and boom (a charge at the Taj Mahal's foot, seen through the
// game's own collapse camera: the blast, its smoke and dust, then the dome coming down), sphinx and giza (the desert levels, aiming).
//   node tests/detexto-dither-shots.mjs <out dir> [--gpu gl|webgpu] [--looks dither,warm,grid,ascii] [--scenes title,play,boom,collapse,sphinx,giza]
//     [--set KEY=VALUE,...] (run options for every shot) [--tag name] (a suffix for the files)
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { openCart } from './lib/romdev.mjs';

const a = process.argv.slice(2);
const arg = (k, d) => { const i = a.indexOf(k); return i >= 0 ? a[i + 1] : d; };
const gpu = arg('--gpu', 'gl');
const out = resolve(a[0]);
const looks = arg('--looks', 'dither,warm,grid,ascii').split(',');
const scenes = arg('--scenes', 'title,play,boom,collapse,sphinx,giza').split(',');
const extra = Object.fromEntries((arg('--set', '') || '').split(',').filter(Boolean).map((kv) => kv.split('=')));
const tag = arg('--tag', '');
mkdirSync(out, { recursive: true });

// name: app, options, frame
const SCENES = {
  title: ['detexto', {}, 420],
  play: ['detexto_auto', {}, 700],
  boom: ['detexto_auto', { LEVEL_NAME: 'Taj Mahal', SCENE: 'shot2', SCENE_CAM: 1 }, 132],
  collapse: ['detexto_auto', { LEVEL_NAME: 'Taj Mahal', SCENE: 'shot2', SCENE_CAM: 1 }, 240],
  dust: ['detexto_auto', { LEVEL_NAME: 'Taj Mahal', SCENE: 'shot2', SCENE_CAM: 1 }, 175],
  sphinx: ['detexto_auto', { LEVEL_NAME: 'Great Sphinx' }, 200],
  giza: ['detexto_auto', { LEVEL_NAME: 'Pyramids of Giza' }, 200],
};
for (const sc of scenes) {
  const [app, opts, at] = SCENES[sc];
  for (const look of looks) {
    const h = await openCart(app, { gpu, tag: `dither-${sc}-${look}` });
    try {
      const all = { LOOK: look, ...opts, ...extra };
      for (const [k, v] of Object.entries(all)) await h.command(`dm set ${k} "${v}"`);
      if (Object.keys(opts).length) await h.command('dm restart');
      await h.step(at - h.frame);
      const f = join(out, `${sc}-${look}${tag ? '-' + tag : ''}-${gpu}.png`);
      await h.shot(f);
      const st = await h.state();
      console.log(`${f} frame ${h.frame} look ${st['dt.dither.look']} pass ${st['dt.dither.w']}x${st['dt.dither.h']} scene_scale ${st['dt.ascii.scene_scale']}`);
    } finally { await h.close(); }
  }
}
