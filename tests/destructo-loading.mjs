// destructo loading gate: a level nothing was built for (dm restart onto a landmark, as a menu accept does) must never
// leave the screen standing still. destructo_auto on romdev, ranged frame recordings only (recordSession spans, no call a
// frame). The cart logs every frame from the ask to a playable level (ui.log_*: the frame, whether the loading screen was
// drawn, its progress, the real time since the frame before) and publishes the rectangles of the spinner and of the text.
//   spinner   every pair of consecutive frames of the wait differs inside the spinner's rectangle
//   text      every frame of the wait has the loading text on it (bright pixels in the text's rectangle), and ui.loading is set
//   progress  the progress is never lower than on the frame before and ends at 1 when the level is playable
//   frame     the slowest frame (its CPU time: ticks and render) from the ask to the level playable is under GAP_MS (a frame
//             that blocks on a build or does a level's heavy work shows here; ui.load_*), nothing recording
// Control, which must fail: the old path (dm set OLD_WAIT 1: the main thread waits for the build, the screen still).
// Run: node tests/destructo-loading.mjs [--gpu gl|webgpu] [--level "The Empire"] [--gap ms] [--keep dir]
import { mkdirSync, readdirSync, rmSync, copyFileSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';
const { readImage } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const LEVEL = arg('--level', 'The Empire'), GAP_MS = +arg('--gap', 100), KEEP = arg('--keep', '');
const SPAN = 60, MAX_SPANS = 80, STEP = 300;

async function transition(gpu, opts, record = true) {
  const h = await openCart('destructo_auto', { gpu, tag: 'loading' });
  try {
    await h.command('dm set QUALITY 3');
    await h.command(`dm set LEVEL_NAME "${LEVEL}"`);
    await h.command('dm set ASYNC_RESTART 1');
    for (const [k, v] of Object.entries(opts)) await h.command(`dm set ${k} "${v}"`);
    await h.step(30);
    await h.command('dm restart');
    const s0 = await h.state();
    const n0 = s0['ui.log_n'];
    const rect = s0['ui.loading_rect'], trect = s0['ui.loading_text_rect'];
    const frames = []; // PNG pairs measured as they come: { spin, text }
    let prev = null, st = s0, shots = 0, lastN = -1;
    const dir = join(ROOT, 'out/loading', `${gpu}-${process.pid}`);
    mkdirSync(dir, { recursive: true });
    for (let span = 0; span < MAX_SPANS; span++) {
      const d = join(dir, `s${span}`);
      mkdirSync(d, { recursive: true });
      if (!record) { await h.step(STEP); } else
      await call('recordSession', { frames: SPAN, sampleEvery: 1, outputDir: d }, h.session, { timeoutMs: 900000 });
      const names = !record ? [] : readdirSync(d).filter((f) => f.endsWith('.png')).sort((a, b) => +a.match(/\d+/)[0] - +b.match(/\d+/)[0]);
      for (const f of names) {
        const img = readImage(join(d, f));
        const px = (r) => ({ x: Math.floor(r[0] * img.w), y: Math.floor(r[1] * img.h), w: Math.ceil(r[2] * img.w), h: Math.ceil(r[3] * img.h) });
        const a = px(rect), t = px(trect);
        const crop = (b) => { const o = new Uint8Array(b.w * b.h * 4); for (let y = 0; y < b.h; y++) o.set(img.rgba.subarray(((b.y + y) * img.w + b.x) * 4, ((b.y + y) * img.w + b.x + b.w) * 4), y * b.w * 4); return o; };
        const cur = crop(a);
        let diff = 0;
        if (prev && prev.length === cur.length) for (let i = 0; i < cur.length; i += 4) if (Math.abs(cur[i] - prev[i]) + Math.abs(cur[i + 1] - prev[i + 1]) + Math.abs(cur[i + 2] - prev[i + 2]) > 6) diff++;
        prev = cur;
        let bright = 0;
        for (let y = 0; y < t.h; y++) for (let x = 0; x < t.w; x++) { const p = ((t.y + y) * img.w + t.x + x) * 4; if (img.rgba[p] > 225 && img.rgba[p + 1] > 225 && img.rgba[p + 2] > 225) bright++; }
        frames.push({ diff, bright });
        if (KEEP && shots++ % 40 === 0) { mkdirSync(KEEP, { recursive: true }); copyFileSync(join(d, f), join(KEEP, `${gpu}-${opts.OLD_WAIT ? 'old' : 'new'}-${String(frames.length).padStart(5, '0')}.png`)); }
      }
      rmSync(d, { recursive: true, force: true });
      st = await h.state();
      console.log(`     span ${span}: ui.loading ${st["ui.loading"]}, log_n ${st["ui.log_n"]} (from ${n0}), phase ${st["game.phase"]}`);
      if (!st['ui.loading'] && (st['ui.log_n'] === lastN || st['ui.log_n'] > n0 + 4)) break;
      lastN = st['ui.log_n'];
    }
    rmSync(dir, { recursive: true, force: true });
    const n = st['ui.log_n'];
    const log = [];
    for (let i = 0; i < n; i++) log.push({ shown: st['ui.log_shown'][i], p: st['ui.log_progress'][i], gap: st['ui.log_gap_ms'][i] });
    return { n0, n, log, frames, nwork: st['ui.load_frames'], over33: st['ui.load_over33'], workMax: st['ui.load_work_max_ms'], gapMax: st['ui.gap_max_ms'], parts: `take ${st['load.take_ms']?.toFixed?.(0)} view ${st['load.view_ms']?.toFixed?.(0)} prime ${st['load.prime_ms']?.toFixed?.(0)} settle ${st['load.settle_ms']?.toFixed?.(0)} job ${st['load.job_ms']?.toFixed?.(0)} ms, view parts ${(st['load.view_parts_ms'] || []).map((x) => x.toFixed(0)).join(',')}`, final: st['ui.loading_progress'], gapMax: st['ui.gap_max_ms'], loading: st['ui.loading'], done: st['game.phase'] === 1 && st['game.level'] >= 0 };
  } finally { await h.close(); }
}

function judge(r, { frames = true, gapOnly = false } = {}) {
  const why = [];
  if (gapOnly) { const gap = r.control ? Math.max(r.workMax, r.gapMax) : r.workMax; if (gap >= GAP_MS) why.push(`the slowest frame of the transition took ${gap.toFixed(0)} ms (limit ${GAP_MS})`); if (!r.nwork) why.push("no frames logged"); return { why, top: `${r.nwork} frames, ${r.over33} over 33 ms (${r.parts})`, gap, shown: 0, pairs: 0, last: 0 }; }
  const shown = r.log.map((e, i) => ({ ...e, i })).filter((e) => e.shown);
  if (!shown.length) why.push('the loading screen was never drawn');
  // PNG j is log entry n0 + j (the recording starts on the frame after the ask)
  let still = 0, textless = 0, pairs = 0;
  for (let j = 1; j < r.frames.length; j++) {
    const e = r.log[r.n0 + j], e1 = r.log[r.n0 + j - 1];
    if (!e || !e.shown || !e1 || !e1.shown) continue;
    pairs++;
    if (r.frames[j].diff === 0) still++;
    if (r.frames[j].bright < 20) textless++;
  }
  if (!pairs) why.push('no two consecutive loading frames recorded');
  if (still) why.push(`${still} of ${pairs} loading frames identical to the one before in the spinner's rectangle`);
  if (textless) why.push(`${textless} loading frames without the text`);
  let back = 0;
  for (let k = 1; k < shown.length; k++) if (shown[k].p < shown[k - 1].p - 1e-6) back++;
  if (back) why.push(`progress went back ${back} times`);
  if (!(r.final >= 0.999)) why.push(`progress ends at ${r.final}, not 1`);
  const gap = Math.max(0, ...r.log.map((e) => e.gap)), gi = r.log.findIndex((e) => e.gap === gap);
  const top = r.log.map((e, i) => `${i}:${e.gap.toFixed(0)}${e.shown ? "" : "n"}`).filter((s, i) => r.log[i].gap > 40).join(" ");
  return { why, top, shown: shown.length, pairs, gap, last: shown.length ? shown[shown.length - 1].p : 0 };
}

let bad = 0;
const say = (good, text, why = []) => { if (!good) bad++; console.log(`${good ? "ok  " : "FAIL"} ${text}${why.length ? "\n     " + why.join("\n     ") : ""}`); };
for (const gpu of gpusArg()) {
  // frames recorded one by one: the spinner moves, the text is there, the progress only goes up (a recorded frame takes
  // the host a few hundred ms, so the gap is measured on the unrecorded run below)
  const r = judge(await transition(gpu, {}), {});
  say(!r.why.length, `${gpu} ${LEVEL}: ${r.shown} loading frames, ${r.pairs} consecutive pairs checked, last progress shown ${r.last.toFixed(2)}`, r.why);
  const c = judge(await transition(gpu, { OLD_WAIT: 1 }), {});
  say(c.why.length > 0, `${gpu} control (OLD_WAIT, the main thread waiting for the build) fails as it must: ${c.why.join("; ") || "nothing failed"}`);
  // the longest real time between two rendered frames from the ask to the playable level, nothing recording
  const g = judge(await transition(gpu, {}, false), { gapOnly: true });
  say(!g.why.length, `${gpu} ${LEVEL}: slowest frame (its CPU time, ticks and render) ${g.gap.toFixed(0)} ms (limit ${GAP_MS}); ${g.top}`, g.why);
  const gc = judge({ ...(await transition(gpu, { OLD_WAIT: 1 }, false)), control: true }, { gapOnly: true });
  say(gc.why.length > 0, `${gpu} control (OLD_WAIT) slowest frame ${gc.gap.toFixed(0)} ms fails as it must`);
}
process.exit(bad ? 1 : 0);
