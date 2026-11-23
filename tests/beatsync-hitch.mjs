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
// The CONTROL must fail: the same tour on a cart loaded with no prewarm (`style noprewarm`, sent before its first
// frame): each world and look is first drawn in play, and a first use costs a frame over budget.
//
//   node tests/beatsync-hitch.mjs [--gpu gl|webgpu|both]
import { openCart, call } from './lib/romdev.mjs';
import { gpusOf } from './lib/beatsync.mjs';

const BUDGET_MS = 16.7;
const STEP = 75, ROUNDS = 2;
let failed = 0;
const ok = (s) => console.log(`ok   ${s}`);
const fail = (s) => { console.log(`FAIL ${s}`); failed++; };

async function tour(gpu, cold) {
  const r = await openCart('beatsync_auto', { gpu, seed: 1, tag: cold ? 'hitch-cold' : 'hitch', load: { ...(gpu === 'webgpu' ? { gpuTiming: true } : {}), ...(cold ? { firstCommand: 'style noprewarm' } : {}) } });
  try {
    await r.step(600); // the prewarm is done in the first hundred frames
    const st = await r.command('style');
    const n = st.styles.length;
    const res = await call('frame', { op: 'timing', frames: n * ROUNDS * STEP + 60, warmup: 0, series: true, debugFields: ['style.changes', 'style.cur'],
      commands: ['quality high', 'style neon', 'style tour'], maxSeconds: 1700 }, r.session, { timeoutMs: 1800000 });
    const s = res.series || res.frames || {};
    const wall = s.wallMs || s.wall || [], cpu = s.cpuMs || s.cpu || [], gpuMs = s.gpuMs || s.gpu || [];
    const f = s.debugFields || s.fields || {};
    const ch = f['style.changes'] || [], cur = f['style.cur'] || [];
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
    return { rows, gpuName: JSON.stringify(res.gpu?.adapter?.device || res.gpu?.glRenderer || '').slice(0, 80) };
  } finally {
    await r.close();
  }
}

for (const gpu of gpusOf(process.argv.slice(2))) {
  const warm = await tour(gpu, false);
  const bad = warm.rows.filter((x) => x.over > 0);
  for (const x of warm.rows) console.log(`     ${gpu} -> ${x.to} (round ${x.round}): worst wall ${x.wall.toFixed(1)} ms (+${x.at}), CPU ${x.cpu.toFixed(1)}, GPU ${x.gpu.toFixed(1)}${x.over ? `, ${x.over} frame(s) over ${BUDGET_MS} ms` : ''}`);
  if (warm.rows.length < 10) fail(`hitch (${gpu}): only ${warm.rows.length} style changes in the run: the check proves nothing`);
  else if (bad.length) fail(`hitch (${gpu}): ${bad.length} of ${warm.rows.length} style changes cost a frame over ${BUDGET_MS} ms of CPU or GPU: ${bad.map((x) => `${x.to} (round ${x.round}, CPU ${x.cpu.toFixed(1)}, GPU ${x.gpu.toFixed(1)})`).join(', ')}`);
  else ok(`hitch (${gpu}): ${warm.rows.length} style changes (every style twice, its first showing included), no frame over ${BUDGET_MS} ms of CPU or GPU; worst CPU ${Math.max(...warm.rows.map((x) => x.cpu)).toFixed(1)} ms, worst GPU ${Math.max(...warm.rows.map((x) => x.gpu)).toFixed(1)} ms, worst wall ${Math.max(...warm.rows.map((x) => x.wall)).toFixed(1)} ms ${warm.gpuName}`);
  const cold = await tour(gpu, true);
  const cb = cold.rows.filter((x) => x.over > 0);
  if (cb.length) ok(`hitch control (${gpu}): unwarmed, ${cb.length} change(s) cost a frame over budget (worst CPU ${Math.max(...cold.rows.map((x) => x.cpu)).toFixed(1)} ms: ${cb.slice(0, 4).map((x) => x.to).join(', ')}), as it must`);
  else fail(`hitch control (${gpu}): unwarmed changes stayed in budget (worst CPU ${Math.max(...cold.rows.map((x) => x.cpu)).toFixed(1)} ms): the check cannot see a first use`);
}
console.log(failed ? `beatsync-hitch: ${failed} failed` : 'beatsync-hitch: ok');
process.exit(failed ? 1 : 0);
