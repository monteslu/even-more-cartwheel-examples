// beatsync's style costs (the COST table and PAIR_BUDGET in games/beatsync/src/styles.c), measured on the autopilot cart
// at High through romdev's frame timing (GPU ms in Radeon 890M units on a remote timing lane, as tests/beatsync-hitch.mjs).
//
//   --alone [id,id,...]   each style held (after its fade): the mean GPU ms of FRAMES frames (default every style)
//   --pairs a:b,c:d       each fade, from a held a (shown as a cut, then held HOLD frames) into b by the game's own fade,
//                         every frame's GPU ms: the fade's start, its mean, its worst frame and where, its end frame (the
//                         frame style.t reaches 1) and the frames after it, against the held b after
//   --pin a:b,c:d         each fade held half way (`style pin a b 0.5`: two worlds and two looks at once): the mean GPU ms
//   --pre "c1;c2"         debug commands before each measure (e.g. `envin old 1`, the fade-end control)
//   --gpu gl|webgpu|both
//
//   node tests/beatsync-cost.mjs --gpu gl --pairs maze:onebit,lava:metal
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, call, ROOT } from './lib/romdev.mjs';
import { gpusOf } from './lib/beatsync.mjs';

const argv = process.argv.slice(2);
const arg = (k) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : null);
const PRE = (arg('--pre') || '').split(';').filter(Boolean);
const PAIRS = (arg('--pairs') || '').split(',').filter(Boolean).map((p) => p.split(':'));
const ALONE = argv.includes('--alone') ? ((arg('--alone') || '').startsWith('--') ? '' : arg('--alone') || '') : null;
const PINS = (arg('--pin') || '').split(',').filter(Boolean).map((p) => p.split(':'));
const FRAMES = Number(arg('--frames') || 180), HOLD = 240, FADE_FRAMES = 300;
const TAG = arg('--tag') || 'cost';
function scaleOf(gpu) {
  const v = process.env[`CW_GPU_TIMING_SCALE_${gpu === 'webgpu' ? 'WEBGPU' : 'GL'}`];
  if (!v) return null;
  try { const j = JSON.parse(v); return typeof j === 'number' ? { a: 0, b: 1 / j } : j; } catch { return null; }
}
const toRef = (gpu, ms) => { const sc = scaleOf(gpu); return ms == null ? ms : sc && sc.b ? Math.max(0, (ms - (sc.a || 0)) / sc.b) : ms; };
const mean = (a) => (a.length ? a.reduce((p, q) => p + q, 0) / a.length : NaN);
const f2 = (x) => (x == null || Number.isNaN(x) ? '-' : x.toFixed(2));

async function timing(r, gpu, commands, frames) {
  const res = await call('frame', { op: 'timing', frames, warmup: 0, series: true, debugFields: ['style.t', 'style.cur', 'style.from'], commands, maxSeconds: 600 }, r.session, { timeoutMs: 700000 });
  const s = res.series || res.frames || {};
  return { gpu: (s.gpuMs || s.gpu || []).map((x) => toRef(gpu, x)), cpu: s.cpuMs || s.cpu || [], t: (s.debugFields || s.fields || {})['style.t'] || [], device: JSON.stringify(res.gpu?.adapter?.device || res.gpu?.glRenderer || '').slice(0, 60) };
}

const out = { lane: process.env.CW_GPU_TIMING_LANE || 'local', pre: PRE, results: {} };
for (const gpu of gpusOf(argv)) {
  const sc = scaleOf(gpu);
  console.log(`note cost (${gpu}): lane ${out.lane}, GPU ms ${sc ? `in 890M units (lane = ${sc.a} + ${sc.b} x 890M)` : 'as measured'}${PRE.length ? `, pre: ${PRE.join('; ')}` : ''}`);
  const r = await openCart('beatsync_auto', { gpu, seed: 1, tag: `beatsync-${TAG}`, load: gpu === 'webgpu' ? { gpuTiming: true } : {} });
  const res = (out.results[gpu] = { alone: {}, pairs: {} });
  try {
    await r.step(600);
    await r.command('quality high');
    for (const c of PRE) await r.command(c);
    const ids = ALONE === null ? [] : ALONE ? ALONE.split(',') : (await r.command('style')).styles.map((s) => s.id);
    for (const id of ids) {
      await r.command('style cut 1'); await r.command(`style ${id}`); await r.command('style cut 0');
      await r.step(HOLD);
      const m = await timing(r, gpu, [], FRAMES);
      const g = m.gpu.filter((x) => x != null);
      res.alone[id] = { mean: mean(g), max: Math.max(...g) };
      console.log(`     ${gpu} ${id}: GPU mean ${f2(mean(g))} ms, max ${f2(Math.max(...g))} ${m.device}`);
    }
    for (const [a, b] of PINS) {
      await r.command(`style pin ${a} ${b} 0.5`);
      await r.step(60);
      const m = await timing(r, gpu, [], FRAMES);
      const g = m.gpu.filter((x) => x != null);
      (res.pins ||= {})[`${a}:${b}`] = { mean: mean(g), max: Math.max(...g) };
      console.log(`     ${gpu} pin ${a} to ${b} at 0.5: GPU mean ${f2(mean(g))} ms, max ${f2(Math.max(...g))}`);
      await r.command('style pin off');
    }
    for (const [a, b] of PAIRS) {
      await r.command('style cut 1'); await r.command(`style ${a}`); await r.command('style cut 0');
      await r.step(HOLD);
      const m = await timing(r, gpu, [`style ${b}`], FADE_FRAMES);
      const s = m.t.findIndex((x) => x < 1), e = s < 0 ? -1 : m.t.findIndex((x, i) => i > s && x >= 1);
      if (s < 0 || e < 0) { console.log(`FAIL ${gpu} ${a} to ${b}: no whole fade in ${FADE_FRAMES} frames (start ${s}, end ${e})`); continue; }
      const g = m.gpu.map((x) => x ?? 0);
      const inFade = g.slice(s + 3, e - 1);
      let wi = s; for (let i = s; i <= e + 3; i++) if (g[i] > g[wi]) wi = i;
      const row = { start: s, end: e, fadeMean: mean(inFade), worst: g[wi], worstAt: wi - s, endFrame: g[e], startFrames: g.slice(s, s + 3), endFrames: g.slice(e - 2, e + 4), held: mean(g.slice(e + 20, e + 120)), heldMax: Math.max(...g.slice(e + 20, e + 120)), cpuEnd: m.cpu[e], series: g.map((x) => +x.toFixed(2)), cpuSeries: m.cpu.map((x) => +(x ?? 0).toFixed(2)), cpuEndFrames: m.cpu.slice(e - 2, e + 4).map((x) => +(x ?? 0).toFixed(2)), t: m.t.map((x) => +x.toFixed(3)) };
      res.pairs[`${a}:${b}`] = row;
      console.log(`     ${gpu} ${a} to ${b}: fade ${e - s} frames, mean ${f2(row.fadeMean)} ms, worst ${f2(row.worst)} at +${row.worstAt}; start ${row.startFrames.map(f2).join(' ')}; end (t=1 at +${e - s}) ${row.endFrames.map(f2).join(' ')} [the 3rd is the end frame]; ${b} held ${f2(row.held)} (max ${f2(row.heldMax)}); CPU at the end ${row.cpuEndFrames.map(f2).join(' ')}`);
    }
  } finally {
    await r.close();
  }
}
const dir = join(ROOT, 'out/beatsync-cost');
mkdirSync(dir, { recursive: true });
writeFileSync(join(dir, `${TAG}.json`), JSON.stringify(out, null, 1));
