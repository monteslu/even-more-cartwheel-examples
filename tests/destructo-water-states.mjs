// dev: the program states the Hoover Dam scene asks for that the tables lack (debug field cw.three.missing_states), after the spill has started
import { openCart } from './lib/romdev.mjs';
const h = await openCart('destructo_auto', { gpu: 'gl', tag: 'wstates' });
try {
  for (const [k, v] of Object.entries({ LEVEL_NAME: 'Hoover Dam', NO_HUD: 1, QUALITY: 3, SCENE: 'dam', OVERVIEW: 1 })) await h.command(`dm set ${k} "${v}"`);
  await h.command('dm restart');
  await h.step(300);
  const s = await h.state();
  console.log(Object.keys(s).filter((k) => /missing|three/.test(k)).map((k) => k + '=' + String(JSON.stringify(s[k])).slice(0, 1500)).join('\n'));
  const ev = await h.events();
  console.log((ev.log || []).filter((l) => /program|state/i.test(l.text)).map((l) => l.text).slice(0, 12).join('\n'));
} finally { await h.close(); }
