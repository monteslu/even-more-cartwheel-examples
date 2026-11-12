// dev: a charge on a real pad timeline: The Empire, its first shot: west twice (shot, heavy ball, charge), then south. Reads the chosen ammunition, shots, booms and blocks broken.
// node tests/destructo-empire-charge.mjs ["<level>"]
import { openCart, call } from './lib/romdev.mjs';
const level = process.argv[2] || 'The Empire';
const h = await openCart('destructo', { gpu: 'gl', tag: 'empire' });
try {
  await h.command(`dm set LEVEL_NAME "${level}"`);
  await h.command('dm restart');
  await h.step(420);
  const tap = (f, b, n = 3) => ({ frame: f, buttons: [b], until: f + n });
  const rd = async () => { const s = await h.state(); return `ammo ${s['aim.ammo']} shots ${s['game.shots']} booms ${s['game.booms']} broken ${s['game.broken']} state ${s['game.state']} tick ${s['game.tick']}`; };
  console.log('start      ', await rd());
  const at = h.frame;
  await call('input', { op: 'timeline', timeline: [tap(at + 20, 'west'), tap(at + 40, 'west'), tap(at + 70, 'south')] }, h.session);
  await h.step(60);
  console.log('after west x2', await rd());
  await h.step(300);
  console.log('after south ', await rd());
} finally { await h.close(); }
