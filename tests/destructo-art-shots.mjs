// The launcher art's frames (games/destructo/tools/make-art.sh), taken through romdev (no native run): destructo_auto
// plays a landmark by name with a real charge fired at its foot (dm set SCENE shot2, seen through the game's own collapse
// camera) or, at the Hoover Dam, a charge set at the dam's foot (SCENE dam, the water's spill); each frame at the size
// asked (3840x2160 without the HUD for the key frames, 1920x1080 with it for the play frames), High quality.
//   node tests/destructo-art-shots.mjs <out dir> [--gpu gl|webgpu]
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { openCart } from './lib/romdev.mjs';

const a = process.argv.slice(2);
const gi = a.indexOf('--gpu');
const gpu = gi >= 0 ? a[gi + 1] : 'gl';
const out = resolve(a[0]);
mkdirSync(out, { recursive: true });
const SHOT = 'shot2', DAM = 'dam';
// name, level, scene, frame, no HUD, size
const SHOTS = [
  ['play-0', 'Iron Lady', SHOT, 240, false, [1920, 1080]],   // the lattice tower mid-topple
  ['play-1', 'Hoover Dam', DAM, 300, false, [1920, 1080]],   // the water spilling through the breach
  ['play-2', 'Mount Rushmore', SHOT, 140, false, [1920, 1080]], // the faces, a charge going off at the foot
  ['play-3', 'Taj Mahal', SHOT, 240, false, [1920, 1080]],   // the dome opening, the dust drifting
  ['key-0', 'Iron Lady', SHOT, 240, true, [3840, 2160]],     // cover
  ['key-1', 'Hoover Dam', DAM, 380, true, [3840, 2160]],     // background
  ['key-2', 'Taj Mahal', SHOT, 140, true, [3840, 2160]],     // title screen
  ['key-3', 'Iron Lady', SHOT, 140, true, [3840, 2160]],     // icon
];
for (const [name, level, scene, frames, noHud, [width, height]] of SHOTS) {
  const h = await openCart('destructo_auto', { gpu, tag: 'art', seed: null, load: { preferredSize: { width, height } } });
  try {
    const opts = { QUALITY: 4, LEVEL_NAME: level, SCENE: scene, SCENE_CAM: 1, ...(noHud ? { NO_HUD: 1 } : {}) };
    for (const [k, v] of Object.entries(opts)) await h.command(`dm set ${k} "${v}"`);
    await h.command('dm restart');
    await h.step(frames - h.frame);
    await h.shot(join(out, `${name}.png`));
    console.log(`${name}: ${level} frame ${h.frame}`);
  } finally { await h.close(); }
}
