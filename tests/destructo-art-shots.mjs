// The launcher art's frames (games/destructo/tools/make-art.sh), taken through romdev (no native run): destructo_bench is
// Taj Mahal's collapse, destructo_auto plays a landmark by name; each frame at the size asked (3840x2160 for the key
// frames, 1920x1080 with the HUD for the play frames), High quality.
//   node tests/destructo-art-shots.mjs <out dir> [--gpu gl|webgpu]
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { openCart } from './lib/romdev.mjs';

const a = process.argv.slice(2);
const gi = a.indexOf('--gpu');
const gpu = gi >= 0 ? a[gi + 1] : 'gl';
const out = resolve(a[0]);
mkdirSync(out, { recursive: true });
const SHOTS = [
  ['play-0', 'destructo_bench', null, 550, false, [1920, 1080]],
  ['play-1', 'destructo_auto', 'Iron Lady', 70, false, [1920, 1080]],
  ['play-2', 'destructo_auto', 'Notre-Dame', 40, false, [1920, 1080]],
  ['play-3', 'destructo_bench', null, 700, false, [1920, 1080]],
  ['key-0', 'destructo_bench', null, 545, true, [3840, 2160]],
  ['key-1', 'destructo_auto', 'Notre-Dame', 110, true, [3840, 2160]],
];
for (const [name, app, level, frames, noHud, [width, height]] of SHOTS) {
  const h = await openCart(app, { gpu, tag: 'art', seed: null, load: { preferredSize: { width, height } } });
  try {
    const opts = { QUALITY: 4, ...(level ? { LEVEL_NAME: level } : {}), ...(noHud ? { NO_HUD: 1 } : {}) };
    for (const [k, v] of Object.entries(opts)) await h.command(`dm set ${k} "${v}"`);
    if (level) await h.command('dm restart');
    await h.step(frames - h.frame);
    await h.shot(join(out, `${name}.png`));
    console.log(`${name}: ${app} ${level || 'Taj Mahal'} frame ${h.frame}`);
  } finally { await h.close(); }
}
