// destructo feel look: frames at the key moments (aim, fire, flight with the queued mark, strike with the aim back open) for review.
// Run: node tests/destructo-feel-look.mjs <outdir> [--gpu gl|webgpu]
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { openCart, call, gpusArg } from './lib/romdev.mjs';
const out = resolve(process.argv[2] || 'out/feel-look');
mkdirSync(out, { recursive: true });
const tap = (f, b, n = 3) => ({ frame: f, buttons: [b], until: f + n });
const gpu = gpusArg()[0];
const h = await openCart('destructo', { gpu, tag: 'feel-look' });
let at = 1;
const seg = async (e) => { await call('input', { op: 'timeline', timeline: e }, h.session); at = Math.max(at, ...e.map((x) => x.until)); };
try {
  await seg([tap(20, 'south'), tap(40, 'south'), tap(60, 'start'), tap(90, 'south')]);
  await h.step(300 - at); at = 300;
  await h.command('freefire 1'); at++;
  await h.shot(join(out, '1-aim.png'));
  await seg([tap(at + 2, 'south', 2), tap(at + 12, 'south', 2)]);
  await h.step(2); at += 2;
  await h.shot(join(out, '2-fire-queued.png'));
  await h.step(30); at += 30;
  await h.shot(join(out, '3-flight-queued.png'));
  for (let i = 0; i < 20; i++) { await h.step(8); at += 8; if ((await h.state())['game.ready']) break; }
  await h.shot(join(out, '4-strike-aim-open.png'));
  await seg([tap(at + 2, 'west', 2)]); await h.step(2); at += 2;
  await h.shot(join(out, '5-ammo-pressed.png'));
} finally { await h.close(); }
