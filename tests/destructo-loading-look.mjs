// destructo loading screen look: a level asked for with nothing built (dm restart onto it), the loading screen kept at
// three moments (the first frame, the middle, the last before the level plays), native scale, to out/gate/loading-look/.
// Run: node tests/destructo-loading-look.mjs "The Empire" [--gpu gl|webgpu]
import { mkdirSync, readdirSync, rmSync, copyFileSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, call, ROOT } from './lib/romdev.mjs';
const name = process.argv[2] || 'The Empire';
const gi = process.argv.indexOf('--gpu'), gpu = gi > 0 ? process.argv[gi + 1] : 'gl';
const out = join(ROOT, 'out/gate/loading-look');
mkdirSync(out, { recursive: true });
const h = await openCart('destructo_auto', { gpu, tag: 'look' });
try {
  await h.command('dm set QUALITY 3');
  await h.command(`dm set LEVEL_NAME "${name}"`);
  await h.command('dm set ASYNC_RESTART 1');
  await h.step(30);
  await h.command('dm restart');
  const kept = [];
  let st;
  for (let span = 0; span < 60; span++) {
    const d = join(out, `tmp${span}`);
    mkdirSync(d, { recursive: true });
    await call('recordSession', { frames: 20, sampleEvery: 1, outputDir: d }, h.session, { timeoutMs: 900000 });
    st = await h.state();
    const names = readdirSync(d).filter((f) => f.endsWith('.png')).sort((a, b) => +a.match(/\d+/)[0] - +b.match(/\d+/)[0]);
    names.forEach((f, i) => kept.push({ p: join(d, f), prog: st['ui.loading'] ? null : 1 }));
    if (!st['ui.loading']) break;
  }
  const n = kept.length, pick = [2, Math.floor(n / 2), Math.max(2, n - 6)];
  const slug = name.toLowerCase().replace(/[^a-z0-9]+/g, '-');
  pick.forEach((k, i) => copyFileSync(kept[k].p, join(out, `${slug}-${gpu}-${i + 1}.png`)));
  console.log(`${name} ${gpu}: ${n} frames recorded, kept ${pick.join(', ')}`);
  for (let span = 0; span < 60; span++) rmSync(join(out, `tmp${span}`), { recursive: true, force: true });
} finally { await h.close(); }
