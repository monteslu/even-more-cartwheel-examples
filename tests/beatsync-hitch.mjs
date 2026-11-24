// beatsync's style changes never hitch: frame times across every style change on the autopilot cart (the release
// cart when the tree is a RELEASE=1 build), through romdev's frame timing in one call a run (per-frame wall, CPU and
// GPU time, the style fields read after every frame).
//
// The run: the cart's own prewarm done (src/styles.c: every world and look drawn once before a song), then `style tour`
// (every style faded in in turn by the game's fade, one every 75 ticks: each world and look's first showing in play
// included) at High, twice round. Each change's frames (from the change to the next) must stay in the frame's budget:
// no frame's CPU or GPU time over BUDGET_MS (a 60 Hz frame). Wall time is reported (it also holds the host's own
// stalls, which a shared machine adds).
//
// Wall time is not held to the budget: on a shared machine romdev's own event loop stalls now and then (a steady style
// with no change shows the same 200-300 ms walls, with the host's event loop delay as long); the cart's CPU and the GPU
// are what a change costs.
//
// The fades themselves stay light: every frame while a fade runs (style.t under 1: two worlds drawn, and the glass
// roads' environment capture of a world that lights the field, cartwheel-engine 0.66.0, its faces and its filter
// spread over the frames) holds the GPU under FADE_GPU_MS, the fade budget the scheduler's costs are set for.
//
// The CONTROL must fail: the same tour on a cart loaded with no prewarm, neither the engine's (cartwheel-engine 0.70.0)
// nor the game's (`env CW_TEST_NO_PREWARM=1`, sent before its first frame; the game reads it too): each world and look is first drawn in play, and a first use costs a frame over budget.
//
//   node tests/beatsync-hitch.mjs [--gpu gl|webgpu|both]
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, call, ROOT } from './lib/romdev.mjs';
import { gpusOf } from './lib/beatsync.mjs';

const BUDGET_MS = 16.7, FADE_GPU_MS = 8;
// a fade bound held: measured and printed, not failed. On WebGPU on a Radeon RX 7600 an occasional fade frame takes 8 to
// 11 ms of GPU in 890M units, a different frame and fade each run (repeated runs of the same fades on cartwheel-engine 0.76.0
// put it at a different frame each time, or nowhere): pauses of the host, which no build of the game or the engine
// moves. GL holds the bound (cartwheel-engine 0.76.0 fixed the costs at a fade's end).
const HELD_FADE = {
  webgpu: 'an occasional one-frame GPU spike in a fade that lands somewhere else each run, from pauses of the host (8 to 11 ms on a Radeon RX 7600 in 890M units): neither the game nor the engine',
};
// --pre "a;b": debug commands before the tour (a look at what a cost comes from, e.g. the worlds' environment off)
const PRE = process.argv.includes('--pre') ? process.argv[process.argv.indexOf('--pre') + 1].split(';').filter(Boolean) : [];
const NO_COLD = process.argv.includes('--no-control');
// --first "<request>": a debug request before the warm run's first frame (a look at what a prewarm covers)
const FIRST = process.argv.includes('--first') ? process.argv[process.argv.indexOf('--first') + 1] : null;
// GPU times in Radeon 890M units: on a remote timing lane the pool gives the lane's fit (CW_GPU_TIMING_SCALE_GL /
// _WEBGPU, {a, b}: lane ms = a + b x 890M ms), and the bounds hold the scaled GPU ms (CPU ms are held as measured)
const LANE = process.env.CW_GPU_TIMING_LANE || 'local';
function scaleOf(gpu) {
  const v = process.env[`CW_GPU_TIMING_SCALE_${gpu === 'webgpu' ? 'WEBGPU' : 'GL'}`];
  if (!v) return null;
  try { const j = JSON.parse(v); return typeof j === 'number' ? { a: 0, b: 1 / j } : j; } catch { return null; }
}
const toRef = (gpu, ms) => { const sc = scaleOf(gpu); return sc && sc.b ? Math.max(0, (ms - (sc.a || 0)) / sc.b) : ms; };
const STEP = 75, ROUNDS = 2;
let failed = 0;
const ok = (s) => console.log(`ok   ${s}`);
const fail = (s) => { console.log(`FAIL ${s}`); failed++; };

async function tour(gpu, cold) {
  const r = await openCart('beatsync_auto', { gpu, seed: 1, tag: cold ? 'hitch-cold' : 'hitch', load: { ...(gpu === 'webgpu' ? { gpuTiming: true } : {}), ...(cold ? { firstCommand: 'env CW_TEST_NO_PREWARM=1' } : FIRST ? { firstCommand: FIRST } : {}) } });
  try {
    await r.step(600); // the prewarm is done in the first hundred frames
    const st = await r.command('style');
    const n = st.styles.length;
    const res = await call('frame', { op: 'timing', frames: n * ROUNDS * STEP + 60, warmup: 0, series: true, debugFields: ['style.changes', 'style.cur', 'style.t', 'style.from', 'cw.pass_builds'],
      commands: ['quality high', ...PRE, 'style neon', 'style tour'], maxSeconds: 1700 }, r.session, { timeoutMs: 1800000 });
    const s = res.series || res.frames || {};
    const wall = s.wallMs || s.wall || [], cpu = s.cpuMs || s.cpu || [], gpuMs = (s.gpuMs || s.gpu || []).map((x) => (x == null ? x : toRef(gpu, x)));
    const f = s.debugFields || s.fields || {};
    const pb = f['cw.pass_builds'] || [];
    const ch = f['style.changes'] || [], cur = f['style.cur'] || [], tt = f['style.t'] || [], frm = f['style.from'] || [];
    // the worst GPU frame while a fade runs, and which fade
    let fadeGpu = 0, fadeAt = -1, fadeFrames = 0;
    for (let i = 0; i < tt.length; i++) if (tt[i] < 1 && i > 0) { fadeFrames++; if ((gpuMs[i] || 0) > fadeGpu) fadeGpu = gpuMs[i] || 0, fadeAt = i; }
    // (where in its fade, and the frames round it: a one-frame spike or a heavy fade)
    // every fade frame over the fade budget: its frame in the run, the fade, how far into it (a one-frame spike at a
    // fade's start or end, or a heavy fade)
    const spikes = [];
    for (let i = 1; i < tt.length; i++) {
      if (!(tt[i] < 1) || !((gpuMs[i] || 0) >= FADE_GPU_MS)) continue;
      let k = 0;
      for (let j = i; j > 0 && ch[j] === ch[j - 1]; j--) k++;
      spikes.push({ frame: i, gpu: +(gpuMs[i] || 0).toFixed(2), fade: `${st.styles[frm[i]]?.id ?? frm[i]} to ${st.styles[cur[i]]?.id ?? cur[i]}`, into: k, t: +tt[i].toFixed(3) });
    }
    mkdirSync(join(ROOT, 'out/beatsync-hitch'), { recursive: true });
    writeFileSync(join(ROOT, `out/beatsync-hitch/${gpu}${cold ? '-cold' : ''}.json`), JSON.stringify({ lane: LANE, spikes, gpu: gpuMs.map((x) => (x == null ? null : +x.toFixed(2))), t: tt, changes: ch, cur, from: frm }));
    let since = 0;
    for (let i = fadeAt; i > 0 && ch[i] === ch[i - 1]; i--) since++;
    const around = fadeAt >= 0 ? gpuMs.slice(Math.max(0, fadeAt - 3), fadeAt + 4).map((x) => (x || 0).toFixed(1)).join(' ') : '';
    if (!wall.length || !ch.length) throw new Error(`timing returned no series: ${JSON.stringify(res).slice(0, 600)}`);
    const rows = [];
    let at = -1;
    for (let i = 1; i <= ch.length; i++) {
      if (i < ch.length && ch[i] === ch[i - 1]) continue;
      if (at >= 0) {
        let w = 0, c = 0, g = 0, over = 0, wi = at;
        for (let k = at; k < i; k++) {
          if (wall[k] > w) w = wall[k], wi = k;
          c = Math.max(c, cpu[k] || 0), g = Math.max(g, gpuMs[k] || 0);
          if ((cpu[k] || 0) > BUDGET_MS || (gpuMs[k] || 0) > BUDGET_MS) over++;
        }
        rows.push({ to: st.styles[cur[at]]?.id ?? cur[at], round: rows.filter((x) => x.to === (st.styles[cur[at]]?.id ?? cur[at])).length + 1, wall: w, at: wi - at, cpu: c, gpu: g, over });
      }
      at = i;
    }
    return { rows, spikes, builds: pb.length ? { first: pb[0], last: pb[pb.length - 1], fadeBuilds: pb.reduce((a, v, i) => a + (i && tt[i] < 1 && v > pb[i - 1] ? v - pb[i - 1] : 0), 0) } : null, fadeGpu, fadeFrames, fadeSince: since, fadeAround: around, fadeTo: fadeAt >= 0 ? `${st.styles[frm[fadeAt]]?.id ?? frm[fadeAt]} to ${st.styles[cur[fadeAt]]?.id ?? cur[fadeAt]}` : '', gpuName: JSON.stringify(res.gpu?.adapter?.device || res.gpu?.glRenderer || '').slice(0, 80) };
  } finally {
    await r.close();
  }
}

for (const gpu of gpusOf(process.argv.slice(2))) {
  const sc = scaleOf(gpu);
  console.log(`note hitch (${gpu}): lane ${LANE}, GPU ms ${sc ? `in 890M units through its fit (lane = ${sc.a} + ${sc.b} x 890M${sc.unreliable ? ', marked UNRELIABLE' : ''})` : 'as measured'}`);
  const warm = await tour(gpu, false);
  const bad = warm.rows.filter((x) => x.over > 0);
  for (const x of warm.rows) console.log(`     ${gpu} -> ${x.to} (round ${x.round}): worst wall ${x.wall.toFixed(1)} ms (+${x.at}), CPU ${x.cpu.toFixed(1)}, GPU ${x.gpu.toFixed(1)}${x.over ? `, ${x.over} frame(s) over ${BUDGET_MS} ms` : ''}`);
  if (warm.rows.length < 10) fail(`hitch (${gpu}): only ${warm.rows.length} style changes in the run: the check proves nothing`);
  else if (bad.length) fail(`hitch (${gpu}): ${bad.length} of ${warm.rows.length} style changes cost a frame over ${BUDGET_MS} ms of CPU or GPU: ${bad.map((x) => `${x.to} (round ${x.round}, CPU ${x.cpu.toFixed(1)}, GPU ${x.gpu.toFixed(1)})`).join(', ')}`);
  else ok(`hitch (${gpu}): ${warm.rows.length} style changes (every style twice, its first showing included), no frame over ${BUDGET_MS} ms of CPU or GPU; worst CPU ${Math.max(...warm.rows.map((x) => x.cpu)).toFixed(1)} ms, worst GPU ${Math.max(...warm.rows.map((x) => x.gpu)).toFixed(1)} ms, worst wall ${Math.max(...warm.rows.map((x) => x.wall)).toFixed(1)} ms ${warm.gpuName}`);
  if (warm.fadeFrames < 100) fail(`hitch fades (${gpu}): only ${warm.fadeFrames} frames in a fade: the check proves nothing`);
  else if (warm.fadeGpu < FADE_GPU_MS) ok(`hitch fades (${gpu}): the worst GPU frame in ${warm.fadeFrames} fade frames ${warm.fadeGpu.toFixed(2)} ms (fading ${warm.fadeTo}, ${warm.fadeSince} frames into it; round it ${warm.fadeAround}), under ${FADE_GPU_MS} ms`);
  else if (HELD_FADE[gpu]) console.log(`held hitch fades (${gpu}): a fade frame took ${warm.fadeGpu.toFixed(2)} ms of GPU (fading ${warm.fadeTo}, ${warm.fadeSince} frames into it; GPU ms round it ${warm.fadeAround}), over ${FADE_GPU_MS} ms: ${HELD_FADE[gpu]}`);
  else fail(`hitch fades (${gpu}): a fade frame took ${warm.fadeGpu.toFixed(2)} ms of GPU (fading ${warm.fadeTo}, ${warm.fadeSince} frames into it; GPU ms round it ${warm.fadeAround}), over ${FADE_GPU_MS} ms`);
  for (const x of warm.spikes) console.log(`     ${gpu} fade frame over ${FADE_GPU_MS} ms: frame ${x.frame} of the run, ${x.fade}, ${x.into} frames into it (t ${x.t}): ${x.gpu} ms`);
  // no engine pass (post, looks, backdrops) built in play after the warm-up (cartwheel-engine 0.76.0 cw.pass_builds):
  // a build is milliseconds of CPU in the frame it happens
  if (!warm.builds) fail(`hitch pass builds (${gpu}): the cart reports no cw.pass_builds`);
  else if (warm.builds.last > warm.builds.first) fail(`hitch pass builds (${gpu}): ${warm.builds.last - warm.builds.first} engine pass build(s) in the tour after the warm-up (${warm.builds.fadeBuilds} in fades)`);
  else ok(`hitch pass builds (${gpu}): no engine pass built in the tour after the warm-up (${warm.builds.last} made in the warm-up)`);
  if (NO_COLD) continue;
  const cold = await tour(gpu, true);
  const cb = cold.rows.filter((x) => x.over > 0);
  if (cold.builds && cold.builds.last > cold.builds.first) ok(`hitch pass builds control (${gpu}): unwarmed, ${cold.builds.last - cold.builds.first} engine pass build(s) in the tour (${cold.builds.fadeBuilds} in fades), as there must be`);
  else fail(`hitch pass builds control (${gpu}): unwarmed, no engine pass built in the tour: the check cannot see a build`);
  if (cb.length) ok(`hitch control (${gpu}): unwarmed, ${cb.length} change(s) cost a frame over budget (worst CPU ${Math.max(...cold.rows.map((x) => x.cpu)).toFixed(1)} ms: ${cb.slice(0, 4).map((x) => x.to).join(', ')}), as it must`);
  else fail(`hitch control (${gpu}): unwarmed changes stayed in budget (worst CPU ${Math.max(...cold.rows.map((x) => x.cpu)).toFixed(1)} ms): the check cannot see a first use`);
}
console.log(failed ? `beatsync-hitch: ${failed} failed` : 'beatsync-hitch: ok');
process.exit(failed ? 1 : 0);
