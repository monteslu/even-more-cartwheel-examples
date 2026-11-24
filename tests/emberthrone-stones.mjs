// emberthrone stones: the scattered stones' bodies against the rock drawn, and where the stones grow. On romdev, the
// auto cart's "stones" command (world.c) casts, for every stone of half a metre or more, a ray down onto it and eight
// rays in from each side (against the world and the trees' crowns, what feet, bodies and the camera meet), and checks
// each hit against models/rock.glb at the instance's scale and turn; and counts the stones on ground steeper than 35
// degrees (a cliff face, where a rock reads as stuck on).
// Bars (PASS needs all): 90% of the rays down land on the rock's top (not more than 15% of its scale above it, not
// 45% below), 80% of the rays from the sides stop between 60% and 105% of the rock's outline that way, no stone under
// a camera crown (2% at most: the stones under a real tree's crown), no stone on ground steeper than 35 degrees, at most 1% standing out over falling ground (a third of its height in the air under its outline).
// CONTROL, which must fail: --control <cart dir>, the build before (stones given a tree's trunk and crown, grown on
// slopes up to 70 degrees), with the command.
//
//   node tests/emberthrone-stones.mjs <cart dir> [--control <cart dir>] [--out <file>]
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

async function run(cartDir) {
  process.env.DESTRUCTO_CART_DIR = resolve(cartDir);
  const { openCart } = await import('./lib/romdev.mjs');
  const h = await openCart('emberthrone_auto', { gpu: 'gl', tag: 'stones' });
  try { await h.step(30); return await h.command('stones'); } finally { await h.close(); }
}
const judge = (r) => {
  const checks = { top: r.top_ok >= 0.9, sides: r.side_ok >= 0.8, crowns: r.crowned <= 0.02 * r.stones, map: r.map_current === 1, steep: r.steep === 0, overhang: r.overhang <= 0.01 * r.all, some: r.stones >= 100 };
  return { ...r, checks, pass: Object.values(checks).every(Boolean) };
};
const [, , cart, ...rest] = process.argv;
const opt = (k) => { const i = rest.indexOf(k); return i >= 0 ? rest[i + 1] : null; };
const res = { run: judge(await run(cart)) };
if (opt('--control')) res.control = judge(await run(opt('--control')));
const ok = res.run.pass && (!res.control || !res.control.pass);
const out = JSON.stringify({ ...res, verdict: ok ? 'PASS' : 'FAIL' }, null, 1);
console.log(out);
if (opt('--out')) writeFileSync(opt('--out'), out);
process.exit(ok ? 0 : 1);
