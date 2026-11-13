// The picture's GPU cost per setting: destructo_auto plays one landmark of each setting at 1920x1080 on High, romdev times frames
// 100 to 520 (the aim, the shot and the start of the collapse) in 60-frame windows on GL and on WebGPU. Prints the median and the
// worst window of GPU ms, raw and (on a pool timing lane, from CW_GPU_TIMING_SCALE_GL / _WEBGPU) scaled to the 890M.
// DESTRUCTO_CART_DIR picks another build's carts (the before numbers).
//   CW_GPU_KIND=timing node <pool>/gpu-pool.mjs run perf node tests/destructo-light-perf.mjs [--only city,desert] [--gpu gl,webgpu] [--json out/gate/perf.json]
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import { openCart } from './lib/romdev.mjs';
const a = process.argv.slice(2);
const take = (f, d) => { const i = a.indexOf(f); if (i < 0) return d; const v = a[i + 1]; a.splice(i, 2); return v; };
const cmds = []; for (let i; (i = a.indexOf('--cmd')) >= 0;) cmds.push(a.splice(i, 2)[1]);
const only = take('--only', ''), gpus = take('--gpu', 'gl,webgpu').split(','), json = take('--json', '');
const LEVELS = { desert: 'Great Sphinx', meadow: 'Taj Mahal', city: 'Iron Lady', harbour: 'Statue of Liberty', highland: 'Mount Rushmore', headland: 'Parthenon', snow: 'The Kremlin' };
const median = (v) => { const s = [...v].sort((x, y) => x - y); return s.length ? s[s.length >> 1] : NaN; };
const lane = process.env.CW_GPU_TIMING_LANE || 'local';
const scaleOf = (gpu) => { try { const j = JSON.parse(process.env[`CW_GPU_TIMING_SCALE_${gpu === 'gl' ? 'GL' : 'WEBGPU'}`] || 'null'); return j && typeof j === 'object' ? j : null; } catch { return null; } };
const scaled = (gpu, ms) => { const s = scaleOf(gpu); return s ? (ms - s.a) / s.b : ms; };
const rows = [];
for (const [name, level] of Object.entries(LEVELS)) {
  if (only && !only.split(',').includes(name)) continue;
  for (const gpu of gpus) {
    const h = await openCart('destructo_auto', { gpu, tag: 'lperf', seed: null, load: { preferredSize: { width: 1920, height: 1080 }, ...(gpu === 'webgpu' ? { gpuTiming: true } : {}) } });
    try {
      for (const [k, v] of Object.entries({ LEVEL_NAME: level, QUALITY: 3 })) await h.command(`dm set ${k} "${v}"`);
      await h.command('dm restart');
      await h.step(10);
      for (const c of cmds) await h.command(c);
      await h.step(100 - h.frame);
      const t = await h.timing({ frames: 420, warmup: 0, window: 60, debugFields: ['game.broken'] });
      const g = (w) => (typeof w.gpuMs === 'number' ? w.gpuMs : w.gpuMs?.mean);
      const all = (t.windows || []).map(g).filter((x) => x > 0);
      const row = { setting: name, level, gpu, lane, med: median(all), worst: Math.max(...all), medScaled: scaled(gpu, median(all)), worstScaled: scaled(gpu, Math.max(...all)), broken: t.fields?.['game.broken']?.max ?? 0, noisy: !!t.contention?.noisy, pinned: !!t.gpu?.onPinnedGpu };
      rows.push(row);
      console.log(`${name.padEnd(9)} ${gpu.padEnd(6)} median ${row.med.toFixed(2)} ms worst ${row.worst.toFixed(2)}  scaled(890M) median ${row.medScaled.toFixed(2)} worst ${row.worstScaled.toFixed(2)}  broken ${row.broken}${row.noisy ? ' NOISY' : ''} lane ${lane}`);
    } finally { await h.close(); }
  }
}
if (json) { mkdirSync(dirname(json), { recursive: true }); writeFileSync(json, JSON.stringify(rows, null, 1)); }
