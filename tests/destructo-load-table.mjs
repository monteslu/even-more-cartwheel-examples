// destructo load table: every level restarted cold on one cart (dm set LEVEL n, dm restart: the level built where the
// player waits, nothing prebuilt), the milliseconds of each phase (load.* in the debug state) and the CPU of the
// frame that made the level and the one after it: the table findings/destructo-fast.md keeps. --old: the old path.
// Run: node tests/destructo-load-table.mjs [--gpu gl|webgpu] [--old]
import { openCart } from './lib/romdev.mjs';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';

const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const gpu = arg('--gpu', 'gl'), OLD = process.argv.includes('--old');
const NLEVELS = +/#define NLEVELS (\d+)/.exec(readFileSync(join(ROOT, 'games/destructo/src/game.h'), 'utf8'))[1];
const PH = ['world', 'build', 'prestress', 'sleep', 'settle', 'view', 'prime', 'replay', 'job', 'wait', 'take', 'total', 'frame0', 'first'];
const f = (x) => (typeof x === 'number' ? x.toFixed(0) : '?').padStart(7);
const h = await openCart('destructo_auto', { gpu, tag: 'load-table' });
try {
  await h.command('dm set QUALITY 3');
  if (OLD) await h.command('dm set OLD_LOAD 1');
  await h.step(30);
  console.log('lvl' + PH.map((p) => p.padStart(7)).join(''));
  for (let lv = 0; lv < NLEVELS; lv++) {
    await h.command(`dm set LEVEL ${lv}`);
    await h.command('dm restart');
    await h.step(3);
    const s = await h.state();
    console.log(String(lv + 1).padStart(3) + PH.map((p) => f(s[`load.${p}_ms`])).join(''));
  }
} finally { await h.close(); }
