// dev: the union of cw.three.missing_states over levels x gpus x presets on destructo_auto: node tests/destructo-states.mjs <levels comma> [gl,webgpu] [presets 0,1,2,3,4]
import { openCart } from './lib/romdev.mjs';
const levels = process.argv[2].split(',');
const gpus = (process.argv[3] || 'gl,webgpu').split(',');
const qs = (process.argv[4] || '0,1,2,3,4').split(',').map(Number);
const seen = new Set();
for (const gpu of gpus) for (const lv of levels) for (const q of qs) {
  const h = await openCart('destructo_auto', { gpu, tag: 'st' });
  try {
    for (const [k, v] of Object.entries({ LEVEL_NAME: lv, NO_HUD: 1, QUALITY: q })) await h.command(`dm set ${k} "${v}"`);
    await h.command('dm restart');
    await h.step(150);
    const m = await h.read('cw.three.missing_states');
    const t = typeof m === 'string' ? m : JSON.stringify(m);
    const got = [...t.matchAll(/[a-z]+(?:\+[A-Za-z0-9_]+)+/g)].map((x) => x[0]);
    for (const l of got) seen.add(l);
    console.log(gpu, lv, 'q' + q, got.length, 'missing');
  } catch (e) { console.log('FAIL', gpu, lv, q, e.message.slice(0, 200)); } finally { await h.close(); }
}
console.log('UNION\n' + [...seen].sort().join('\n'));
