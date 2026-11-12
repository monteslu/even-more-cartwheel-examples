// dev: the worst CPU a frame (ticks and render) of the bot's play of named levels, and the parts of the worst frames (feel.slow_*)
// node tests/destructo-headroom-cpu.mjs [--gpu gl] [--frames 900] "<level>" ...
import { openCart } from './lib/romdev.mjs';
const a = process.argv.slice(2);
const take = (f, d) => { const i = a.indexOf(f); if (i < 0) return d; const v = a[i + 1]; a.splice(i, 2); return v; };
const gpu = take('--gpu', 'gl'), frames = +take('--frames', 900);
for (const level of a) {
  const h = await openCart('destructo_auto', { gpu, tag: 'cpu' });
  try {
    for (const [k, v] of Object.entries({ LEVEL_NAME: level, QUALITY: 3 })) await h.command(`dm set ${k} "${v}"`);
    await h.command('dm restart');
    await h.step(frames);
    const s = await h.state();
    const n = Math.min(s['feel.slow_n'], 48), rows = [];
    for (let i = 0; i < n; i++) rows.push(`tick ${s['feel.slow_tick'][i]} cpu ${s['feel.slow_work'][i].toFixed(1)} tickms ${s['feel.slow_tickms'][i].toFixed(1)} world ${s['feel.slow_world'][i].toFixed(1)} pre ${s['feel.slow_pre'][i].toFixed(1)} post ${s['feel.slow_post'][i].toFixed(1)}`);
    rows.sort((x, y) => parseFloat(y.split(' cpu ')[1]) - parseFloat(x.split(' cpu ')[1])); rows.length = Math.min(rows.length, 6);
    console.log(`${level}: worst CPU ${s['feel.worst_ms'].toFixed(1)} ms, over 16 ms ${s['feel.over33']}, over 6 ms ${s['feel.slow_n']}, broken ${s['game.broken']} blocks ${s['game.blocks']} awake-max ${s['perf.awake_max']}\n   ${rows.join('\n   ')}`);
  } finally { await h.close(); }
}
