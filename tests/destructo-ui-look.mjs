// destructo UI frames for review, through romdev: the title's way in (Play, join, Start, choose), the level select, the
// start of a level (its name and par fade in), the aiming screen with its hints, the pause card, and the result card.
// Run: node tests/destructo-ui-look.mjs <outdir> [--gpu gl|webgpu]   (DESTRUCTO_CART_DIR: another build, for a "before")
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, gpusArg, call } from './lib/romdev.mjs';

const out = process.argv[2];
if (!out) { console.error('usage: destructo-ui-look.mjs <outdir> [--gpu gl|webgpu]'); process.exit(2); }
mkdirSync(out, { recursive: true });
const gpu = gpusArg()[0] === 'gl' && !process.argv.includes('--gpu') ? 'webgpu' : gpusArg()[0];
const tap = (f, b, n = 3) => ({ frame: f, buttons: [b], until: f + n });
const h = await openCart("destructo", { gpu, tag: "ui-look" });
// h.timeline runs the schedule itself (the host is then at the last window's end), so the frames after it are counted here
let at = 1;
const seg = async (entries, to) => { await call("input", { op: "timeline", timeline: entries }, h.session); at = Math.max(...entries.map((e) => e.until)); if (to > at) await h.step(to - at), at = to; };
const goto = async (to) => { if (to > at) await h.step(to - at), at = to; };
try {
  await seg([tap(20, "south"), tap(40, "south"), tap(60, "start")], 85);
  await h.shot(join(out, "1-select.png"));
  await seg([tap(90, "south")], 150);
  await h.shot(join(out, "2-intro.png"));
  await goto(300);
  await h.shot(join(out, "3-aim-early.png"));
  await seg([tap(310, "right", 40), tap(360, "up", 12)], 400);
  await goto(420);
  await h.shot(join(out, "4-aim-after-turn.png"));
  await seg([tap(430, "start")], 470);
  await h.shot(join(out, "5-pause.png"));
  await h.close();
  const r = await openCart('destructo_auto', { gpu, tag: 'ui-look-result' });
  try {
    for (const [k, v] of Object.entries({ LEVEL_NAME: 'Leaning Tower of Pisa', QUALITY: 3 })) await r.command(`dm set ${k} "${v}"`);
    await r.command('dm restart');
    await r.step(1500 - r.frame);
    await r.shot(join(out, '6-result.png'));
  } finally { await r.close(); }
} catch (e) { console.error(e.message); await h.close(); process.exit(1); }
console.log('frames in', out);
