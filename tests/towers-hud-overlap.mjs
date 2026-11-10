// towers HUD overlap check, through romdev, GL and WebGPU: no label ever covers a deploy button. The game draws the
// four build pills (the deploy list: north, west, east, south round the cursor) and a tower's action pills, each
// cursor's P1..P4 label, the players' corner panels and the top line (wave, core, the next wave). Every case reads the
// HUD's rectangles back from the cart (the `hud` request: every label's box, every button's box and text box, in
// canvas pixels) and fails on any overlap of a label, a panel or the top line with a button, of one cursor's pills
// with another's, or of anything with the screen's edge. Then the pixels: the frame with the labels drawn against
// the same frame with them hidden (`hud labels 0`, the game paused between): the pixels inside every button's text
// box must agree, so a label really drawn over a button fails even when the rectangles lie.
// Cases, each on GL and on WebGPU, at the cart's own 1280x720 (a deterministic load) and at the sizes a host can ask
// for (1920x1080, and 1024x768 for a narrower screen; romdev's preferredSize, a load with no seed):
//   - one to four seats, the match just begun (every cursor's deploy list open, the map's first wave counting down)
//   - two seats with their cursors on the closest pair of slots across the shares, the closest in one row and in one
//     column (the pills of one cursor against the other's; `hud cursor` puts a cursor on a slot)
//   - the deploy lists closed: two seats three and a half seconds into the first wave, sampled through the fade
//     (the label moves then, settling in over the cursor)
//   - a seat joining mid-match (one seat starts, a second pad presses South during play)
//   - the whole-map sweep (`hud sweep`): every slot of every map alone in every tower state, and in pairs with every
//     slot of another seat, laid out as the HUD lays them out, for one to four seats (at 1280x720 and 1024x768)
// CONTROL, which must fail: `hud push 20`, the labels forced 20 px into their nearest button (the rectangles overlap
// and the pixels differ).
//
// Both deploy-pill layouts are checked (`hud style 0`: each pill with its button's glyph; `hud style 1`: the text
// alone, one diamond of the four face buttons at the cursor and a one-time hint over the top pill).
//
//   node tests/towers-hud-overlap.mjs [--gpu gl|webgpu|both] [--cart build/cart-wgpu/carts/towers.wasc] [--quick] [--style 0|1|both]
//        [--compare DIR] [--maps 1,8,22]
// (--quick: 1280x720 only, the sweep on maps 1, 8 and 22; --compare DIR: no check, three native-scale crops of one
// seat's cursor at 1920x1080 on GL into DIR: style0.png, style1-hint.png, style1.png). ROMDEV_URL picks the server (default 127.0.0.1:7331, or
// ROMDEV_PORT). Input goes as ranged calls (one input({op:'sequence'}) per run: romdev's timeline op drives two
// ports only, and four seats need four).
import { request as httpRequest } from 'node:http';
import { existsSync, mkdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { readPng } from './towers-look.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = resolve(process.env.CW_ENGINE_DIR || join(ROOT, 'deps/cartwheel-engine'));
const { wgpuPin } = await import(pathToFileURL(join(ENGINE, 'tools/lib/wgpu-pin.mjs')).href);
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const CART = resolve(opt('cart', join(ROOT, 'build/cart-wgpu/carts/towers.wasc')));
if (!existsSync(CART)) { console.error(`no cart ${CART} (build the cart tree)`); process.exit(2); }
const GPUS = opt('gpu', 'both') === 'both' ? ['gl', 'webgpu'] : [opt('gpu', 'both')];
const QUICK = argv.includes('--quick');
const STYLES = opt('style', 'both') === 'both' ? [0, 1] : [+opt('style', 'both')];
const COMPARE = opt('compare', null);
const BISECT = argv.includes('--bisect'); // the pairs sweep one slot a request, so a trap names its slot
/** the pairs sweep slot by slot, summed */
async function sweepSlots(s) {
  const n = (await s.cmd('hud slots')).slots.length, sum = { cases: 0, bad: 0, first: [] };
  for (let a = 0; a < n; a++) { const r = await s.cmd(`hud sweep pairs ${a}`); sum.cases += r.cases; sum.bad += r.bad; sum.first.push(...r.first); }
  return sum;
}
const OUT = join(ROOT, 'out', 'gate', 'towers-hud-overlap');
rmSync(OUT, { recursive: true, force: true });
mkdirSync(OUT, { recursive: true });
const BASE = new URL(process.env.ROMDEV_URL || `http://127.0.0.1:${process.env.ROMDEV_PORT || 7331}`);
// the cart's own size on a deterministic load (1280x720, cw_main), then the sizes a host asks for (preferredSize, a load with no seed)
const SIZES = QUICK ? [{ w: 1280, h: 720, det: true }] : [{ w: 1280, h: 720, det: true }, { w: 1920, h: 1080 }, { w: 1024, h: 768 }];
const SWEEP_MAPS = opt('maps', null) ? opt('maps', null).split(',').map(Number) : QUICK ? [1, 8, 22] : Array.from({ length: 30 }, (_, i) => i + 1); // (--maps a,b: these maps)
const DIFF = 40, DIFF_SHARE = 0.01; // a pixel differs past 40 of 255 in a channel; a text box fails past 1% of its pixels

function post(tool, args, session) {
  const body = JSON.stringify(args);
  return new Promise((ok, fail) => {
    const req = httpRequest({ host: BASE.hostname, port: BASE.port || 80, path: `/tool/${tool}`, method: 'POST', agent: false,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'x-romdev-session': session, 'x-romdev-agent': 'towers-hud-overlap' } }, (res) => {
      let text = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { text += c; });
      res.on('end', () => {
        clearTimeout(timer);
        let b;
        try { b = JSON.parse(text); } catch { b = text; }
        if (b && Array.isArray(b.content)) { const t = b.content.find((c) => c.type === 'text'); try { b = JSON.parse(t.text); } catch { b = t ? t.text : b; } }
        if (res.statusCode < 200 || res.statusCode >= 300 || (b && b.isError)) fail(new Error(`romdev ${tool}: ${typeof b === 'string' ? b : JSON.stringify(b).slice(0, 1500)}`));
        else ok(b);
      });
    });
    const timer = setTimeout(() => req.destroy(new Error(`romdev ${tool}: no reply in 600 s`)), 600000);
    req.on('error', (e) => { clearTimeout(timer); fail(new Error(`romdev at ${BASE.origin} ${tool}: ${e.message} (is the server running?)`)); });
    req.end(body);
  });
}

/** one romdev session on the cart: deterministic at 1920x1080 (cartwheel keeps 1080 lines on a deterministic load), a
 * load with no seed at any other size (the host's preferred size) */
async function open(gpu, size) {
  const session = `towers-hud-${process.pid}-${Date.now().toString(36)}`;
  const pin = wgpuPin();
  const det = !!size.det;
  const loaded = await post('loadMedia', { platform: 'wasmcart', path: CART, ...(det ? { deterministicSeed: 1, deterministicStepMs: 1000 / 60 } : { preferredSize: { width: size.w, height: size.h } }),
    gpuPower: pin.power, wgpu: gpu === 'webgpu' }, session);
  if (loaded.gpu !== gpu) throw new Error(`romdev: the cart runs on ${loaded.gpu}, not ${gpu}`);
  if (gpu === 'webgpu') pin.check('romdev', loaded.gpuAdapter && (loaded.gpuAdapter.device || loaded.gpuAdapter.description));
  let at = 1, calls = 1;
  const s = {
    session, gpu, size, loaded, view: null,
    async cmd(text) {
      calls++;
      let r;
      try { r = await post('wasm', { op: 'command', text }, session); } catch (e) { /* a trap: the cart's last log lines say where */
        let tail = '';
        try { const ev = await post('wasm', { op: 'events' }, session); tail = ev.hasDebugEvents === false ? ev.note : (ev.log || []).slice(-8).map((q) => `${q.frame}: ${q.text}`).join(' | ') || '(no log lines)'; } catch (e2) { tail = `events failed: ${e2.message}`; }
        throw new Error(`'${text}': ${e.message}${tail ? ` (the cart's last lines: ${tail})` : ''}`);
      }
      at += r.framesRun || 1;
      const a = r && typeof r === 'object' && 'reply' in r ? r.reply : r;
      if (a && a.error) throw new Error(`'${text}': ${a.error}`);
      return a;
    },
    async step(n) { if (n > 0) { calls++; await post('frame', { op: 'step', frames: n }, session); at += n; } },
    async seq(steps) { calls++; await post('input', { op: 'sequence', steps }, session); at += steps.reduce((a, b) => a + b.frames, 0); },
    async shot(name) { calls++; const path = join(OUT, name + '.png'); await post('frame', { op: 'screenshot', path }, session); return path; },
    async fields() { calls++; const st = await post('wasm', { op: 'debugState' }, session); const o = {}; for (const f of st.fields || []) o[f.name] = f.values ?? f.value; return o; },
    get at() { return at; }, get calls() { return calls; },
    async close() { await post('host', { op: 'shutdown' }, session).catch(() => {}); },
  };
  return s;
}

/** the shell's way into a match for n seats, as one ranged input call: Play on the title, every pad joins with South,
 * Start plays, South on the map screen starts map 1; then idle to frame `until` */
function startSteps(n, until) {
  const pads = (held) => ({ ports: Array.from({ length: Math.max(n, 1) }, (_, i) => (held(i) || {})) });
  const s = [], add = (frames, held = () => null) => { if (frames > 0) s.push({ input: pads(held), frames }); };
  add(18); add(2, (i) => (i === 0 ? { start: true } : null)); // frames 2..19 idle, 20-21: Play
  add(18); add(2, () => ({ south: true }));                    // 40-41: every pad joins
  add(18); add(2, (i) => (i === 0 ? { start: true } : null)); // 60-61: Start plays
  add(28); add(2, (i) => (i === 0 ? { south: true } : null)); // 90-91: the map screen starts map 1
  add(until - 92);
  return s;
}

/** the rectangles' verdict: labels (every rect that is not a button) against buttons, one seat's buttons against
 * another's, everything on screen; returns the problems */
function judgeRects(h) {
  const why = [];
  const hit = (a, b) => a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
  const box = (r) => ({ x: r.box[0], y: r.box[1], w: r.box[2], h: r.box[3] });
  const buttons = h.rects.filter((r) => r.kind === 'button'), others = h.rects.filter((r) => r.kind !== 'button');
  for (const r of h.rects) {
    const b = box(r);
    if (b.x < 0 || b.y < 0 || b.x + b.w > h.canvas[0] || b.y + b.h > h.canvas[1]) why.push(`${r.kind} "${r.s}" of P${r.p} off screen at ${r.box.map((v) => v.toFixed(0))}`);
  }
  for (const l of others) for (const b of buttons) if (hit(box(l), box(b))) why.push(`${l.kind} "${l.s}"${l.p > 0 ? ` of P${l.p}` : ''} over P${b.p}'s "${b.s}" (${l.box.map((v) => v.toFixed(0))} on ${b.box.map((v) => v.toFixed(0))})`);
  for (const a of buttons) for (const b of buttons) if (a.p < b.p && hit(box(a), box(b))) why.push(`P${a.p}'s "${a.s}" on P${b.p}'s "${b.s}"`);
  return why;
}

/** the frame with the labels against the frame without them: the share of differing pixels in every button's text
 * box (the worst and which), and outside every label (the sanity line) */
function judgePixels(withPath, withoutPath, h, view) {
  const A = readPng(withPath), B = readPng(withoutPath);
  if (A.w !== B.w || A.h !== B.h) throw new Error(`frames differ in size: ${A.w}x${A.h} vs ${B.w}x${B.h}`);
  const sx = view ? view[2] / view[0] : A.w / h.canvas[0], sy = view ? view[3] / view[1] : A.h / h.canvas[1], ox = view ? view[4] : 0, oy = view ? view[5] : 0;
  const differs = (i) => Math.max(Math.abs(A.px[i] - B.px[i]), Math.abs(A.px[i + 1] - B.px[i + 1]), Math.abs(A.px[i + 2] - B.px[i + 2])) > DIFF;
  const share = (r) => {
    const x0 = Math.max(0, Math.round(ox + r[0] * sx)), y0 = Math.max(0, Math.round(oy + r[1] * sy)), x1 = Math.min(A.w, Math.round(ox + (r[0] + r[2]) * sx)), y1 = Math.min(A.h, Math.round(oy + (r[1] + r[3]) * sy));
    let n = 0, d = 0;
    for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) { n++; if (differs((y * A.w + x) * 3)) d++; }
    return n ? d / n : 0;
  };
  const rows = h.rects.filter((r) => r.kind === 'button').map((r) => ({ r, share: share(r.text) }));
  const worst = rows.reduce((w, q) => (q.share > w.share ? q : w), { share: 0, r: null });
  // outside every label and button: how much else moved between the two frames (nothing should: the game is paused)
  const inside = h.rects.map((r) => ({ x0: Math.round(ox + r.box[0] * sx), y0: Math.round(oy + r.box[1] * sy), x1: Math.round(ox + (r.box[0] + r.box[2]) * sx), y1: Math.round(oy + (r.box[1] + r.box[3]) * sy) }));
  let n = 0, d = 0;
  for (let y = 0; y < A.h; y += 2) for (let x = 0; x < A.w; x += 2) {
    if (inside.some((q) => x >= q.x0 && x < q.x1 && y >= q.y0 && y < q.y1)) continue;
    n++; if (differs((y * A.w + x) * 3)) d++;
  }
  return { rows, worst, outside: n ? d / n : 0 };
}

let bad = 0;
const ok = (name, line) => console.log(`ok   ${name}${line ? `: ${line}` : ''}`);
const fail = (name, why) => { bad++; console.log(`FAIL ${name}: ${why}`); };

/** the full check on the session's current frame: rectangles, then the pixels with the labels hidden (paused) */
async function checkHere(s, name, { control = false } = {}) {
  await s.cmd('pause');
  const h = await s.cmd('hud');
  if (!h || !Array.isArray(h.rects)) throw new Error(`${name}: no rectangles from the hud request: ${JSON.stringify(h).slice(0, 300)}`);
  if (!s.view) { const f = await s.fields(); s.view = f['cw.view']; }
  const labels = h.rects.filter((r) => r.kind === 'label').length, buttons = h.rects.filter((r) => r.kind === 'button').length;
  const why = judgeRects(h);
  const a = await s.shot(`${name}-labels`);
  await s.cmd('hud labels 0');
  const b = await s.shot(`${name}-hidden`);
  await s.cmd('hud labels 1');
  await s.cmd('resume');
  const px = judgePixels(a, b, h, s.view);
  const pxBad = px.worst.share > DIFF_SHARE;
  const line = `${labels} labels, ${buttons} buttons, ${h.canvas[0]}x${h.canvas[1]}; the worst text box ${(px.worst.share * 100).toFixed(1)}% changed` + (px.worst.r ? ` (P${px.worst.r.p} "${px.worst.r.s}")` : '') + `, ${(px.outside * 100).toFixed(2)}% elsewhere`;
  if (px.outside > 0.02) why.push(`${(px.outside * 100).toFixed(1)}% of the frame changed outside the HUD between the two frames (the pause did not hold)`);
  if (control) {
    if (why.length && pxBad) ok(`control fails as it must, ${name}`, `${why[0]}; ${line}`);
    else fail(`CONTROL PASSED, ${name}`, `${why.length ? 'the rectangles saw it but the pixels did not' : 'no overlap seen'}; ${line}`);
    return;
  }
  if (pxBad) why.push(`P${px.worst.r.p}'s "${px.worst.r.s}" text changed ${(px.worst.share * 100).toFixed(1)}% with the labels hidden: something draws over it`);
  if (why.length) fail(name, `${why.slice(0, 4).join('; ')}${why.length > 4 ? ` (+${why.length - 4} more)` : ''}; ${line}`);
  else ok(name, line);
  if (!control) { rmSync(a, { force: true }); rmSync(b, { force: true }); }
}

if (COMPARE) { /* the two layouts at one cursor, native-scale crops for a side-by-side */
  mkdirSync(resolve(COMPARE), { recursive: true });
  const s = await open('gl', { w: 1920, h: 1080 });
  try {
    await s.seq(startSteps(1, 150));
    await s.cmd('pause');
    await s.cmd('hud');
    const f = await s.fields(), view = f['cw.view'], slot = f['player.slot'][0];
    const sl = (await s.cmd('hud slots')).slots.find((q) => q.slot === slot);
    const sx = view[2] / view[0], sy = view[3] / view[1]; // canvas units to framebuffer pixels
    const cx = view[4] + sl.sx * sx, cy = view[5] + sl.sy * sy, W = 600, H = 400;
    const crop = { x: Math.max(0, Math.round(cx - W / 2)), y: Math.max(0, Math.round(cy - H / 2)), w: W, h: H };
    const shot = async (name) => { await post('frame', { op: 'screenshot', path: join(resolve(COMPARE), name + '.png'), crop }, s.session); };
    await shot('style0');
    await s.cmd('hud style 1'); // (the hint shows for 120 frames from the style change)
    await s.cmd('hud');
    await shot('style1-hint');
    await s.cmd('resume');
    await s.step(130);
    await s.cmd('pause');
    await s.cmd('hud');
    await shot('style1');
    console.log(`crops of ${crop.w}x${crop.h} at ${crop.x},${crop.y} in ${resolve(COMPARE)}`);
  } finally { await s.close(); }
  process.exit(0);
}
for (const gpu of GPUS) for (const size of SIZES) for (const style of STYLES) {
  const tag = `${gpu} ${size.w}x${size.h} style ${style}`;
  for (let n = 1; n <= 4; n++) {
    const s = await open(gpu, size);
    try {
      await s.cmd(`hud style ${style}`);
      await s.seq(startSteps(n, 150));
      const f = await s.fields();
      if (f['game.players'] !== n || f['game.phase'] !== 1) throw new Error(`the match did not start as asked: players ${f['game.players']}, phase ${f['game.phase']}`);
      await checkHere(s, `${tag} ${n} seat${n > 1 ? 's' : ''}, the deploy lists open`);
      if (n === 1) { await s.cmd('hud push 20'); await checkHere(s, `${tag} the labels pushed 20 px into the top button`, { control: true }); await s.cmd('hud push 0'); }
      if (size.det || (size.w === 1024 && (n === 2 || n === 4))) { /* the whole-map sweep */
        for (const map of SWEEP_MAPS) {
          if (map !== 1 || s.at > 200) await s.cmd(`map ${map}`);
          for (const half of n > 1 ? ['singles', 'pairs'] : ['singles']) {
            const r = half === 'pairs' && BISECT ? await sweepSlots(s) : await s.cmd(`hud sweep ${half}`);
            if (r.bad) {
              fail(`${tag} ${n} seat${n > 1 ? 's' : ''} map ${map} sweep, ${half}`, `${r.bad} of ${r.cases} layouts overlap or leave the screen: ${JSON.stringify(r.first.slice(0, 3))}`);
              if (half === 'singles' && r.first.length) { /* the search's candidates for the first one */
                const f0 = r.first[0], tr = await s.cmd(`hud place ${f0.slot} ${f0.state}`);
                console.log(`     place ${f0.slot} ${f0.state}: at ${tr.at}, lay ${tr.lay}, hits ${tr.hits}, fixed ${JSON.stringify(r.fixed)}, placed ${tr.placed}; candidates ${tr.candidates.map((c) => `e${c.ext}${c.side ? '-' : '+'}${c.c.toFixed(0)}:${c.off ? 'off' : c.on >= 0 ? 'on' + c.on : 'ok'}`).join(' ')}`);
              }
            }
            else ok(`${tag} ${n} seat${n > 1 ? 's' : ''} map ${map} sweep, ${half}`, `${r.cases} layouts clear`);
          }
        }
      }
      if (n === 2) {
        await s.cmd('map 1');
        const m0 = s.at; // map 1 began again here: its first wave comes 24 s on
        // the closest pairs of slots across the two shares: overall, in one row, in one column
        const sl = (await s.cmd('hud slots')).slots, mine = sl.filter((q) => q.owner === 0), theirs = sl.filter((q) => q.owner === 1);
        const pairs = [];
        const closest = (pred) => { let best = null; for (const a of mine) for (const b of theirs) { if (!pred(a, b)) continue; const d = Math.hypot(a.x - b.x, a.y - b.y); if (!best || d < best.d) best = { a, b, d }; } return best; };
        for (const [what, pred] of [['closest', () => true], ['in one row', (a, b) => a.y === b.y], ['in one column', (a, b) => a.x === b.x], ['diagonal', (a, b) => Math.abs(a.x - b.x) <= 2 && Math.abs(a.y - b.y) === 1]]) {
          const p = closest(pred);
          if (p && !pairs.some((q) => q.a.slot === p.a.slot && q.b.slot === p.b.slot)) pairs.push({ what, ...p });
        }
        for (const p of pairs) {
          await s.cmd(`hud cursor 0 ${p.a.slot}`);
          await s.cmd(`hud cursor 1 ${p.b.slot}`);
          await checkHere(s, `${tag} 2 seats on the ${p.what} slots across the shares (${p.a.x},${p.a.y} and ${p.b.x},${p.b.y})`);
        }
        // the deploy lists closing: the first wave comes at 24 s, the lists fade 3 to 3.5 s of stillness later
        await s.step(m0 + 1440 - s.at);
        let came = (await s.fields())['game.wave'] >= 1;
        for (let tries = 0; !came && tries < 12; tries++) { await s.step(30); came = (await s.fields())['game.wave'] >= 1; }
        if (!came) fail(`${tag} 2 seats, the lists closing`, `the first wave had not come by frame ${s.at}`);
        else {
          const w0 = s.at; // the wave came within the 30 frames before this; the lists fade 180 to 210 frames of stillness after it
          for (const dt of [150, 200, 215, 260]) { await s.step(w0 + dt - s.at); await checkHere(s, `${tag} 2 seats ${dt} frames into the first wave (the lists closing)`); }
        }
      }
    } catch (e) { fail(`${tag} ${n} seat${n > 1 ? 's' : ''} at frame ${s.at}`, e.message); }
    finally { await s.close(); }
  }
  { /* a seat joining mid-match */
    const s = await open(gpu, size);
    try {
      await s.cmd(`hud style ${style}`);
      await s.seq(startSteps(1, 150));
      await s.seq([{ input: { ports: [{}, { south: true }] }, frames: 2 }, { input: { ports: [{}, {}] }, frames: 12 }]);
      const f = await s.fields();
      if (f['game.players'] !== 2) fail(`${tag} a second seat joins mid-match`, `players ${f['game.players']} after the second pad pressed South`);
      else await checkHere(s, `${tag} a second seat joined mid-match`);
    } catch (e) { fail(`${tag} a second seat joining mid-match, at frame ${s.at}`, e.message); }
    finally { await s.close(); }
  }
}
process.exit(bad ? 1 : 0);
