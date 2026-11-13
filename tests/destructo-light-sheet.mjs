// The picture's light, one landmark per setting: the real aim view (the opening shot's camera) and the structure mid-collapse
// (destructo_auto at a frame after its first shot), 1920x1080, no HUD. Frames go to <out dir>/<setting>-aim.png and -fall.png.
//   node tests/destructo-light-sheet.mjs <out dir> [--gpu gl|webgpu] [--only desert,city] [--fall 150] [--set KEY=VAL ...]
// DESTRUCTO_CART_DIR picks another build's carts (the old build for a before sheet).
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { openCart } from './lib/romdev.mjs';
const a = process.argv.slice(2);
const take = (f, d) => { const i = a.indexOf(f); if (i < 0) return d; const v = a[i + 1]; a.splice(i, 2); return v; };
const extra = []; for (let i; (i = a.indexOf('--set')) >= 0;) extra.push(a.splice(i, 2)[1]);
const cmds = []; for (let i; (i = a.indexOf('--cmd')) >= 0;) cmds.push(a.splice(i, 2)[1]);
const logre = take('--logre', ''), suffix = take('--suffix', ''), aimF = Number(take('--aim', '45'));
const kinds = take('--kinds', 'aim,fall').split(','), gpu = take('--gpu', 'gl'), fall = Number(take('--fall', '150')), only = take('--only', '');
const out = resolve(a[0]); // under out/gate (the pool copies only that back)
mkdirSync(out, { recursive: true });
const SETTINGS = { desert: 'Great Sphinx', meadow: 'Taj Mahal', city: 'Iron Lady', harbour: 'Statue of Liberty', highland: 'Mount Rushmore', headland: 'Parthenon', snow: 'The Kremlin' };
for (const [name, level] of Object.entries(SETTINGS)) {
  if (only && !only.split(',').includes(name)) continue;
  for (const [kind, app, frames] of [['aim', 'destructo_auto', 0], ['fall', 'destructo_auto', fall]]) {
    if (!kinds.includes(kind)) continue;
    const h = await openCart(app, { gpu, tag: 'light', seed: null, load: { preferredSize: { width: 1920, height: 1080 } } });
    try {
      const opts = { LEVEL_NAME: level, NO_HUD: 1, QUALITY: 3, ...(kind === 'aim' ? { SCENE: 'dam', SCENE_AIM: 1 } : {}) };
      for (const kv of extra) { const [k, v] = kv.split('='); opts[k] = v; }
      for (const [k, v] of Object.entries(opts)) await h.command(`dm set ${k} "${v}"`);
      await h.command('dm restart');
      await h.step(5);
      for (const c of cmds) console.log(c, JSON.stringify(await h.command(c)).slice(0, 300));
      await h.step((kind === 'aim' ? aimF : frames) - 5);
      await h.shot(join(out, `${name}-${kind}${suffix}.png`));
      if (logre) console.log(((await h.events()).log || []).filter((l) => new RegExp(logre, 'i').test(l.text)).map((l) => l.text).slice(0, 30).join('\n'));
      console.log(`${name}-${kind}: ${level}`);
    } finally { await h.close(); }
  }
}
