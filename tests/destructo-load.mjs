// destructo load gate: the wait between a level ending and the next one playable, through the game itself
// (destructo_auto on romdev: the bot plays a level, the result card shows with its replay, and the same level begins
// again; the cart times it, load.* in its debug state). The next level is built on a background job while the card shows
// (games/destructo/src/loader.c), so what is left is the move into the live game, the render's side and the first frame.
//   ready    the wall time from the card's accept (the level asked for) to the level playable, ms (load.ready_ms)
//   frames   the CPU of the frame that made the level and of the one after it (load.frame0_ms, load.first_ms)
//   job      what the background build took (load.job_ms), and wait, what the main thread blocked on it (zero here)
// Held: ready under READY_MS and both frames under FRAME_MS on every level tried, which are the slowest to build and
// the first levels of the kits. Reported with the lane's timing scale.
// Control, which must fail: the old path (dm set OLD_LOAD 1: the level and its shadow built and settled on the main
// thread, the field scanned for coplanar faces) against the same bound.
// The per-level phase table of the old path (every level, restarted cold: tests/destructo-load.mjs --table) is in
// internal-cartwheel/findings/destructo-fast.md.
// Run: node tests/destructo-load.mjs [--gpu gl|webgpu] [--no-controls] [--ready ms] [--frame ms] [--levels "A,B"]
import { openCart } from './lib/romdev.mjs';
import { inPairs } from './lib/romdev.mjs';

const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const gpu = arg('--gpu', 'gl');
const READY_MS = +arg('--ready', 700), FRAME_MS = +arg('--frame', 700);
const LEVELS = arg('--levels', 'The Empire,Iron Lady,Neuschwanstein Castle,Great Keep,Stone Giant,First Stones').split(',');

async function transition(name, opts) {
  const h = await openCart('destructo_auto', { gpu, tag: 'load' });
  try {
    await h.command('dm set QUALITY 3');
    await h.command(`dm set LEVEL_NAME "${name}"`);
    for (const [k, v] of Object.entries(opts)) await h.command(`dm set ${k} "${v}"`);
    await h.command('dm restart');
    await h.step(2);
    const c0 = (await h.state())['load.count'];
    let s;
    for (let n = 0; n < 60; n++) { // up to 18,000 frames: the bot plays the level, the card shows, the level begins again
      await h.step(300);
      s = await h.state();
      if (s['load.count'] > c0 && s['game.phase'] === 1) break;
    }
    if (!(s['load.count'] > c0)) return { name, err: `no second load (phase ${s['game.phase']}, result ${s['result.last']})` };
    await h.step(3);
    const t = await h.state();
    return { name, ready: t['load.ready_ms'], frame0: t['load.frame0_ms'], first: t['load.first_ms'], job: t['load.job_ms'], wait: t['load.wait_ms'], total: t['load.total_ms'], async: t['load.async'], view: t['load.view_ms'] };
  } catch (e) {
    return { name, err: e.message };
  } finally { await h.close(); }
}
const f = (x) => (typeof x === 'number' ? x.toFixed(0) : '?').padStart(6);
const show = (r) => r.err ? `${r.name}: ${r.err}` : `${r.name.padEnd(24)} ready ${f(r.ready)} ms | frames ${f(r.frame0)} ${f(r.first)} | main-thread load ${f(r.total)} (view ${f(r.view)}, wait ${f(r.wait)}) | background build ${f(r.job)} | ${r.async ? 'async' : 'old path'}`;
const ok = (r) => !r.err && r.ready <= READY_MS && r.frame0 <= FRAME_MS && r.first <= FRAME_MS;
let bad = 0;
const say = (good, what) => { if (!good) bad++; console.log(`${good ? 'ok  ' : 'FAIL'} ${what}`); };
const scale = process.env.CW_GPU_TIMING_SCALE_GL ? ` (lane ${process.env.CW_GPU_TIMING_LANE || '?'}, scale ${process.env.CW_GPU_TIMING_SCALE_GL})` : '';
const rows = await inPairs(LEVELS.map((n) => () => transition(n, {})));
for (const r of rows) say(ok(r), `${gpu} ${show(r)}${scale}`);
if (!process.argv.includes('--no-controls')) {
  const old = await inPairs(LEVELS.slice(0, 2).map((n) => () => transition(n, { OLD_LOAD: 1 })));
  for (const r of old) say(!r.err && !ok(r), `${gpu} control (the old path) fails the bound as it must: ${show(r)}`);
}
process.exit(bad ? 1 : 0);
