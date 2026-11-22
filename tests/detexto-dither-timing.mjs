// Detexto's look trial: the GPU cost of each look at 1920x1080 (romdev frame timing, A/B variants interleaved over
// rounds, with an A/A pair), and the look passes' own timers (cartwheel's `pipeline` command: dither, ascii,
// ascii_text). detexto_auto on the Taj Mahal with a charge at its foot (the collapse in view).
//   node tests/detexto-dither-timing.mjs [--gpu gl|webgpu] [--out file.json]
import { writeFileSync } from 'node:fs';
import { openCart, call } from './lib/romdev.mjs';

const a = process.argv.slice(2);
const arg = (k, d) => { const i = a.indexOf(k); return i >= 0 ? a[i + 1] : d; };
const gpu = arg('--gpu', 'webgpu');
const outFile = arg('--out', null);
const h = await openCart('detexto_auto', { gpu, tag: 'dither-timing', load: gpu === 'webgpu' ? { gpuTiming: true } : {} });
try {
  for (const c of ['dm set LEVEL_NAME "Taj Mahal"', 'dm set SCENE shot2', 'dm set SCENE_CAM 1', 'dm restart']) await h.command(c);
  await h.step(100);
  const v = (name, look) => ({ name, commands: [`dm set LOOK ${look}`] });
  const r = await call('frame', {
    op: 'timing', warmup: 40, frames: 240, rounds: 3, window: 120, passTimers: true,
    variants: [v('ascii', 'ascii'), v('bayer', 'bayer'), v('blue', 'blue'), v('feedback', 'feedback'), v('blue-AA', 'blue')],
  }, h.session, { timeoutMs: 1800000 });
  if (outFile) writeFileSync(outFile, JSON.stringify(r, null, 1));
  for (const vr of r.variants || []) {
    const g = vr.gpuMs || {}, w = vr.wallMs || {};
    const pt = JSON.stringify(vr.passTimers || vr.passTimer || '').match(/"(dither|ascii|ascii_text)"[^}]*?"gpu_ms_avg":\s*[-\d.]+/g);
    console.log(`${gpu} ${vr.name}: gpu mean ${g.mean} p95 ${g.p95} wall mean ${w.mean} passes ${pt ? pt.join(' | ') : '-'}`);
  }
} finally { await h.close(); }
