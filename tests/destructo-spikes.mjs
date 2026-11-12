// destructo spike hunt: the bot's collapse scenes (Taj Mahal) on romdev; every frame whose CPU (ticks and render)
// passes 6 ms is recorded in the cart (feel.slow_*: the tick, the game state, the CPU, the tick's parts). Reports the worst
// frame and the list; the gate (--max-ms N) holds the worst CPU a frame under N ms.
// Run: node tests/destructo-spikes.mjs [--gpu gl|webgpu] [--max-ms 16] [--frames 1500]
import { openCart, gpusArg } from './lib/romdev.mjs';

const arg = (k, d) => (process.argv.includes(k) ? process.argv[process.argv.indexOf(k) + 1] : d);
const maxMs = +arg('--max-ms', 0), frames = +arg('--frames', 1500);
let bad = 0;
for (const gpu of gpusArg()) for (const app of ['destructo_bench']) {
  const h = await openCart(app, { gpu, tag: 'spikes' });
  try {
    await h.command('dm set QUALITY 3');
    await h.step(frames - h.frame);
    const s = await h.state();
    const n = Math.min(s['feel.slow_n'], 48), rows = [];
    for (let i = 0; i < n; i++) rows.push({ tick: s['feel.slow_tick'][i], st: s['feel.slow_state'][i], cpu: +s['feel.slow_work'][i].toFixed(1), tick_ms: +s['feel.slow_tickms'][i].toFixed(1), world: +s['feel.slow_world'][i].toFixed(1), pre: +s['feel.slow_pre'][i].toFixed(1), post: +s['feel.slow_post'][i].toFixed(1) });
    console.log(`${app} ${gpu}: ${s['feel.frames']} frames, worst CPU ${s['feel.worst_ms'].toFixed(1)} ms, ${s['feel.over33']} over 16 ms, ${s['feel.slow_n']} over 6 ms; broken ${s['game.broken']} awake-max ${s['perf.awake_max']}`);
    for (const r of rows) console.log('  ' + JSON.stringify(r));
    if (maxMs && s['feel.worst_ms'] > maxMs) { bad++; console.log(`FAIL ${app} ${gpu}: worst ${s['feel.worst_ms'].toFixed(1)} ms > ${maxMs}`); }
  } finally { await h.close(); }
}
process.exit(bad ? 1 : 0);
