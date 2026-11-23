// Detexto's 1-bit looks (games/detexto/src/dither.h), on romdev, GL and WebGPU (the one cart on each):
//   two colours  for each look (dither, warm, grid) the frame without the HUD is the look's ink and paper only (at
//                least 99.5% of the pixels within 6 of one of the two); control that must fail: the pass off
//                (DITHER_OFF 1), the plain picture;
//   the look     dither and grid draw different patterns from the same picture (blue noise against Bayer), and warm
//                is the dither's pattern in its warm pair;
//   no crawl     with the camera still (the level held, HOLD 1), the share of pixels that change between frames is at
//                most 0.5% (the grass in the wind, a crystal's pulse); control that must fail: the threshold moved
//                every frame (the engine's CW_TEST_LOOK_CRAWL 1), at least ten times as much;
//   settings     on the game cart the Settings screen has the row "Look": right steps Dither, Dither warm, Dither
//                grid, ASCII and stops at the end (the look follows at once, ASCII puts the scene back at 35%);
//                control: left on the first choice leaves it.
//   menus        every screen under each 1-bit look, at text size 100% and 200%, pad prompts: the title (game cart), the
//                in-game HUD (aiming, and over the result card), pause, Settings and the Graphics page (over a level):
//                the shell in two colours
//                (shell.two_colour 1, shell.ink and shell.paper the look's), every pixel the look's ink or paper (exact),
//                the selected row an ink plate (selection INVERSE, shell.selection 3); readable: round the text (the pixels
//                near the text's edges) each line has as many ink pixels, within 8%, as the blended text's coverage
//                (the engine's CW_TEST_R2D_HARD_OFF 1: the same frame with blended edges), so the one-bit text keeps its
//                strokes; the HUD and the pause screen's controls card (the game's own text, cw_r2d_set_one_bit and
//                cw_prompt_set_two_colour) the same; controls that must fail: the menus and HUD in their own colours
//                and edges (MENUS_TWO_OFF 1) are not two colours, and one-bit edges cut at 0.95 coverage
//                (CW_TEST_R2D_HARD_THIN 1) lose ink on a line;
//   switch       on the game cart's Settings, Look from Dither to Dither warm: the frame after the press is all the warm
//                pair (the menus follow on the next frame, shell.ink 0xffe5bd); to ASCII: the mode off (shell.two_colour 0),
//                the selection still INVERSE;
//   node tests/detexto-dither.mjs [--gpu gl|webgpu|both] [--only colours,crawl,settings,menus,switch] [--scales 1,2] [--looks dither,warm,grid]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, call, ROOT } from './lib/romdev.mjs';
import { readPng } from './lib/png.mjs';

const a = process.argv.slice(2);
const gi = a.indexOf('--gpu');
const gpus = gi >= 0 && a[gi + 1] !== 'both' ? [a[gi + 1]] : ['gl', 'webgpu'];
const oi = a.indexOf('--only');
const only = oi >= 0 ? a[oi + 1].split(',') : ['colours', 'crawl', 'settings', 'menus', 'switch'];
const si = a.indexOf('--scales');
const SCALES = si >= 0 ? a[si + 1].split(',').map(Number) : [1, 2];
const li = a.indexOf('--looks');
const LOOKS = li >= 0 ? a[li + 1].split(',') : ['dither', 'warm', 'grid'];
const OUT = join(ROOT, 'out/gate/detexto-dither');
mkdirSync(OUT, { recursive: true });
const AT = 700; // detexto_auto on its first level, aiming
const INK = { dither: [[255, 255, 255], [0, 0, 0]], grid: [[255, 255, 255], [0, 0, 0]], warm: [[255, 229, 189], [9, 8, 6]] };

let fail = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) fail++; };

// the share of pixels within tol of one of the colours
function twoColour(png, cols, tol = 6) {
  const { w, h, rgb } = png;
  let n = 0;
  for (let i = 0; i < w * h; i++) {
    const r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
    if (cols.some((c) => Math.abs(r - c[0]) <= tol && Math.abs(g - c[1]) <= tol && Math.abs(b - c[2]) <= tol)) n++;
  }
  return n / (w * h);
}
// the share of pixels whose side of mid grey differs between two frames
function flips(p0, p1) {
  let c = 0;
  const n = p0.w * p0.h;
  for (let i = 0; i < n; i++) {
    const l0 = p0.rgb[i * 3] * 54 + p0.rgb[i * 3 + 1] * 183 + p0.rgb[i * 3 + 2] * 19;
    const l1 = p1.rgb[i * 3] * 54 + p1.rgb[i * 3 + 1] * 183 + p1.rgb[i * 3 + 2] * 19;
    if ((l0 > 32640) !== (l1 > 32640)) c++;
  }
  return c / n;
}

async function frames(gpu, opts, tag, at = AT, after = [1]) {
  // an engine test switch (CW_TEST_*) is read from the first frame on: it goes with the load, before frame 1
  const env = Object.entries(opts).filter(([k]) => k.startsWith('CW_TEST_')).map(([k, v]) => `${k}=${v}`);
  const h = await openCart('detexto_auto', { gpu, tag: `dither-${tag}`, load: env.length ? { firstCommand: `env ${env.join(' ')}` } : {} });
  try {
    for (const [k, v] of Object.entries(opts)) if (!k.startsWith('CW_TEST_')) await h.command(`dm set ${k} ${v}`);
    await h.step(at - h.frame);
    const out = [];
    const f0 = join(OUT, `${gpu}-${tag}-0.png`);
    await h.shot(f0);
    out.push(readPng(f0));
    for (const d of after) {
      await h.step(d);
      const f = join(OUT, `${gpu}-${tag}-${d}.png`);
      await h.shot(f);
      out.push(readPng(f));
    }
    return { pngs: out, st: await h.state(), f0 };
  } finally { await h.close(); }
}

async function settings(gpu) {
  const h = await openCart('detexto', { gpu, tag: 'dither-settings' });
  try {
    const s = h.session;
    // to the title (the deterministic clock: the loading screen ends on a fixed frame)
    const t = await call('frame', { op: 'step', frames: 1200, stopWhen: { field: 'shell.screen', equals: 1, every: 5 }, fields: ['shell.screen'] }, s);
    check(t.stopped, `${gpu}: settings: the title shows (frame ${t.frameCount})`);
    await call('frame', { op: 'step', frames: 30 }, s);
    const tap = (b) => [{ input: { ports: [{ [b]: true }] }, frames: 3 }, { input: { ports: [{}] }, frames: 12 }];
    const seq = async (bs) => (await call('input', { op: 'sequence', steps: bs.flatMap(tap), fields: ['shell.screen', 'shell.item', 'dt.dither.look', 'dt.ascii.scene_scale'] }, s)).fields;
    let f = await seq(['down', 'south', 'up', 'up']); // Settings, then up from the top: Back, then the game's row above it
    check(f['shell.screen'] === 5 && f['dt.dither.look'] === 0, `${gpu}: settings: on the Settings screen (item ${f['shell.item']}), the look Dither (${f['dt.dither.look']})`);
    await call('frame', { op: 'screenshot', path: join(OUT, `${gpu}-settings.png`) }, s);
    f = await seq(['left']);
    check(f['dt.dither.look'] === 0, `${gpu}: settings: control: left on the first choice stays on Dither (${f['dt.dither.look']})`);
    const want = [1, 2, 3, 3];
    for (let i = 0; i < want.length; i++) {
      f = await seq(['right']);
      check(f['dt.dither.look'] === want[i], `${gpu}: settings: right ${i + 1}: the look ${f['dt.dither.look']} (want ${want[i]})`);
    }
    check(Math.abs(f['dt.ascii.scene_scale'] - 0.35) < 1e-3, `${gpu}: settings: ASCII renders the scene at 35% (${f['dt.ascii.scene_scale']})`);
    f = await seq(['left', 'left', 'left']);
    check(f['dt.dither.look'] === 0 && f['dt.ascii.scene_scale'] === 0, `${gpu}: settings: back to Dither, the scene at full size (${f['dt.dither.look']}, ${f['dt.ascii.scene_scale']})`);
  } finally { await h.close(); }
}

const tap = (b) => [{ input: { ports: [{ [b]: true }] }, frames: 3 }, { input: { ports: [{}] }, frames: 14 }];
const HEX = { dither: [0xffffff, 0x000000], grid: [0xffffff, 0x000000], warm: [0xffe5bd, 0x090806] };
// pixels that are neither colour (exact), the ink count, the commonest others and where they are
// (rows from..to only: a part of the screen)
function purity(png, [ink, paper], from = 0, to = png.h) {
  const { w, h, rgb } = png;
  let other = 0, inks = 0, x0 = w, y0 = h, x1 = -1, y1 = -1;
  const seen = new Map();
  for (let i = from * w; i < to * w; i++) {
    const r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
    if (r === ink[0] && g === ink[1] && b === ink[2]) { inks++; continue; }
    if (r === paper[0] && g === paper[1] && b === paper[2]) continue;
    other++;
    const x = i % w, y = (i / w) | 0;
    x0 = Math.min(x0, x), y0 = Math.min(y0, y), x1 = Math.max(x1, x), y1 = Math.max(y1, y);
    const k = `${r},${g},${b}`;
    seen.set(k, (seen.get(k) || 0) + 1);
  }
  const top = [...seen].sort((p, q) => q[1] - p[1]).slice(0, 3).map(([k, n]) => `${k} x${n}`).join('; ');
  return { other, inks, top, box: other ? `${x0},${y0}..${x1},${y1}` : '' };
}
// readable: the pixels where the one-bit frame and the blended one differ (the text's edges), grown by r pixels (the
// strokes round them, inside the text's paper plate), in bands of rows; each band's ink count (one-bit) against the
// blended frame's coverage (its colour projected from paper to ink) there
function lines(hard, soft, [ink, paper], r) {
  const { w, h } = hard, diff = new Uint8Array(w * h);
  for (let i = 0; i < w * h; i++)
    for (let k = 0; k < 3; k++) if (hard.rgb[i * 3 + k] !== soft.rgb[i * 3 + k]) { diff[i] = 1; break; }
  const reg = new Uint8Array(w * h);
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      if (!diff[y * w + x]) continue;
      for (let yy = Math.max(0, y - r); yy <= Math.min(h - 1, y + r); yy++)
        for (let xx = Math.max(0, x - r); xx <= Math.min(w - 1, x + r); xx++) reg[yy * w + xx] = 1;
    }
  const d = ink.map((v, k) => v - paper[k]), dd = d.reduce((s, v) => s + v * v, 0);
  const bands = [];
  for (let y = 0; y < h;) {
    let any = false;
    for (let x = 0; x < w && !any; x++) any = reg[y * w + x] === 1;
    if (!any) { y++; continue; }
    let yb = y, c = 0, n = 0;
    for (; yb < h; yb++) {
      let row = false;
      for (let x = 0; x < w; x++) {
        const i = yb * w + x;
        if (!reg[i]) continue;
        row = true;
        let t = 0;
        for (let k = 0; k < 3; k++) t += (soft.rgb[i * 3 + k] - paper[k]) * d[k];
        c += Math.min(1, Math.max(0, t / dd));
        if (hard.rgb[i * 3] === ink[0] && hard.rgb[i * 3 + 1] === ink[1] && hard.rgb[i * 3 + 2] === ink[2]) n++;
      }
      if (!row) break;
    }
    if (c >= 20) bands.push({ y0: y, y1: yb - 1, cov: c, ink: n, ratio: n / c });
    y = yb;
  }
  return bands;
}
const rgb3 = (x) => [(x >> 16) & 255, (x >> 8) & 255, x & 255];

// one look at one text size: the title on the game cart; the HUD, pause, Settings and Graphics over a level on the
// auto cart. opts: run options (dm set), env: engine test switches (from the first frame). Returns {name: {f, png, st}}
async function screens(gpu, look, scale, tag, opts = {}, env = {}) {
  const ev = Object.entries(env).map(([k, v]) => `${k}=${v}`);
  const load = ev.length ? { firstCommand: `env ${ev.join(' ')}` } : {};
  const out = {};
  const prep = async (h) => {
    await h.command(`dm set LOOK ${look}`);
    for (const [k, v] of Object.entries(opts)) await h.command(`dm set ${k} ${v}`);
    await h.command(`text scale ${scale}`);
    await h.command('prompt device all pad');
  };
  const shoot = async (h, name) => {
    const f = join(OUT, `${gpu}-${tag}-${look}-${scale * 100}-${name}.png`);
    await call('frame', { op: 'screenshot', path: f }, h.session);
    out[name] = { f, png: readPng(f), st: await h.state() };
  };
  let h = await openCart('detexto', { gpu, tag: `m-title-${tag}-${look}`, load });
  try {
    await prep(h);
    await call('frame', { op: 'step', frames: 1200, stopWhen: { field: 'shell.screen', equals: 1, every: 5 } }, h.session);
    await call('frame', { op: 'step', frames: 30 }, h.session);
    await shoot(h, 'title');
  } finally { await h.close(); }
  h = await openCart('detexto_auto', { gpu, tag: `m-play-${tag}-${look}`, load });
  try {
    await prep(h);
    // the HUD while aiming (the ammunition, the hints), then over the result card
    await call('frame', { op: 'step', frames: AT - h.frame - 40, stopWhen: { field: 'game.phase', equals: 1, every: 2 } }, h.session);
    await h.step(30);
    await shoot(h, 'hud-aim');
    await h.step(AT - h.frame);
    await shoot(h, 'hud');
    const seq = (bs) => call('input', { op: 'sequence', steps: bs.flatMap(tap) }, h.session);
    await seq(['start']);
    await shoot(h, 'pause');
    await seq(['down', 'south']);
    await shoot(h, 'settings');
    await seq(['south']);
    await shoot(h, 'graphics');
  } finally { await h.close(); }
  return out;
}
const SCREEN = { title: 1, pause: 4, settings: 5, graphics: 5 };

async function menus(gpu) {
  const ALL = [...Object.keys(SCREEN), 'hud-aim', 'hud'];
  for (const look of LOOKS)
    for (const scale of SCALES) {
      const cols = HEX[look].map(rgb3);
      const s = await screens(gpu, look, scale, 'two');
      for (const [name, { f, png, st }] of Object.entries(s)) {
        const p = purity(png, cols);
        const msg = `${gpu} ${look} ${scale * 100}% ${name}: ${p.other} pixels neither ink nor paper${p.other ? ` (${p.top}; in ${p.box})` : ''}, ${p.inks} ink (${f})`;
        const shell = st['shell.two_colour'] === 1 && st['shell.ink'] === HEX[look][0] && st['shell.paper'] === HEX[look][1];
        if (name.startsWith('hud')) { check(p.other === 0 && p.inks > 200 && st['shell.screen'] === 3, `${msg}; screen ${st['shell.screen']}`); continue; }
        check(p.other === 0 && p.inks > 200 && shell && st['shell.screen'] === SCREEN[name] && st['shell.selection'] === 3,
          `${msg}; shell two_colour ${st['shell.two_colour']} ink ${Number(st['shell.ink']).toString(16)} paper ${Number(st['shell.paper']).toString(16)}, screen ${st['shell.screen']}, selection ${st['shell.selection']}`);
      }
      if (look !== 'dither') continue;
      // readable, and its control (the same screens, the edges blended; and cut at 0.95)
      const soft = await screens(gpu, look, scale, 'soft', {}, { CW_TEST_R2D_HARD_OFF: 1 });
      const thin = await screens(gpu, look, scale, 'thin', {}, { CW_TEST_R2D_HARD_THIN: 1 });
      const r = Math.round(3 * scale);
      for (const name of ALL) {
        const L = lines(s[name].png, soft[name].png, cols, r), bad = L.filter((b) => b.ratio < 0.92 || b.ratio > 1.08);
        check(L.length >= 2 && bad.length === 0, `${gpu} ${look} ${scale * 100}% ${name}: readable: ${L.length} lines, ink against the blended coverage ${L.map((b) => b.ratio.toFixed(2)).join(' ')}`);
        const LT = lines(thin[name].png, soft[name].png, cols, r), badT = LT.filter((b) => b.ratio < 0.92 || b.ratio > 1.08);
        check(badT.length > 0, `${gpu} ${look} ${scale * 100}% ${name}: control (cut at 0.95): ${badT.length} of ${LT.length} lines out of range (${LT.map((b) => b.ratio.toFixed(2)).join(' ')})`);
      }
      if (scale !== 1) continue;
      const off = await screens(gpu, look, scale, 'off', { MENUS_TWO_OFF: 1 });
      for (const name of ALL) {
        const p = purity(off[name].png, cols);
        check(p.other > 0 && (name.startsWith('hud') || off[name].st['shell.two_colour'] === 0), `${gpu} ${look} ${name}: control (the menus and HUD in their own colours and edges, MENUS_TWO_OFF): ${p.other} pixels neither ink nor paper`);
      }
    }
}

// the look changed in Settings: the menus in the new colours from the next frame
async function lookSwitch(gpu) {
  const h = await openCart('detexto', { gpu, tag: 'm-switch' });
  try {
    const s = h.session;
    await call('frame', { op: 'step', frames: 1200, stopWhen: { field: 'shell.screen', equals: 1, every: 5 } }, s);
    await call('frame', { op: 'step', frames: 30 }, s);
    await call('input', { op: 'sequence', steps: ['down', 'south', 'up', 'up'].flatMap(tap) }, s);
    let st = await h.state();
    check(st['shell.screen'] === 5 && st['dt.dither.look'] === 0 && st['shell.two_colour'] === 1, `${gpu} switch: Settings on the Look row under Dither, two colours (${st['shell.two_colour']})`);
    // right for 3 frames (the press is read on its first tick), then one released frame: the frame after the change
    await call('input', { op: 'sequence', steps: [{ input: { ports: [{ right: true }] }, frames: 1 }, { input: { ports: [{}] }, frames: 1 }] }, s);
    const f = join(OUT, `${gpu}-switch-warm.png`);
    await call('frame', { op: 'screenshot', path: f }, s);
    st = await h.state();
    const p = purity(readPng(f), HEX.warm.map(rgb3));
    check(st['dt.dither.look'] === 1 && p.other === 0 && p.inks > 200 && st['shell.ink'] === HEX.warm[0] && st['shell.paper'] === HEX.warm[1],
      `${gpu} switch: Dither warm on the frame after the press: ${p.other} pixels not the warm pair${p.other ? ` (${p.top}; in ${p.box})` : ''}, shell.ink ${Number(st['shell.ink']).toString(16)} (${f})`);
    const pc = purity(readPng(f), HEX.dither.map(rgb3));
    check(pc.other > 0, `${gpu} switch: control: the same frame is not the white pair (${pc.other} pixels)`);
    await call('input', { op: 'sequence', steps: ['right', 'right'].flatMap(tap) }, s);
    st = await h.state();
    check(st['dt.dither.look'] === 3 && st['shell.two_colour'] === 0 && st['shell.selection'] === 3,
      `${gpu} switch: ASCII: two colours off (${st['shell.two_colour']}), the selection INVERSE (${st['shell.selection']})`);
    await call('frame', { op: 'screenshot', path: join(OUT, `${gpu}-switch-ascii.png`) }, s);
  } finally { await h.close(); }
}

for (const gpu of gpus) {
  if (only.includes('colours')) {
    const shots = {};
    for (const look of ['dither', 'warm', 'grid']) {
      const r = await frames(gpu, { LOOK: look, NO_HUD: 1 }, look, AT, []);
      shots[look] = r.pngs[0];
      const s = twoColour(r.pngs[0], INK[look]);
      check(r.st['dt.dither.w'] === 1920 && r.st['dt.dither.h'] === 1080, `${gpu} ${look}: the pass at the output's full size (${r.st['dt.dither.w']}x${r.st['dt.dither.h']})`);
      check(s >= 0.995, `${gpu} ${look}: two colours: ${(s * 100).toFixed(3)}% of the pixels ink or paper (${r.f0})`);
    }
    const off = await frames(gpu, { LOOK: 'dither', NO_HUD: 1, DITHER_OFF: 1 }, 'off', AT, []);
    const so = twoColour(off.pngs[0], INK.dither);
    check(so < 0.995, `${gpu}: control (the pass off) fails two colours: ${(so * 100).toFixed(2)}%`);
    const dg = flips(shots.dither, shots.grid), dw = flips(shots.dither, shots.warm);
    check(dg > 0.05, `${gpu}: dither and grid differ: ${(dg * 100).toFixed(2)}% of the pixels`);
    check(dw < 0.02, `${gpu}: warm is the dither's pattern: ${(dw * 100).toFixed(3)}% of the pixels differ in side`);
  }
  if (only.includes('crawl')) {
    for (const look of ['dither', 'grid']) {
      const still = await frames(gpu, { LOOK: look, HOLD: 1, NO_HUD: 1 }, `still-${look}`, AT, [1, 1]);
      const crawl = await frames(gpu, { LOOK: look, HOLD: 1, NO_HUD: 1, CW_TEST_LOOK_CRAWL: 1 }, `crawl-${look}`, AT, [1, 1]);
      const s = Math.max(flips(still.pngs[0], still.pngs[1]), flips(still.pngs[1], still.pngs[2]));
      const c = Math.min(flips(crawl.pngs[0], crawl.pngs[1]), flips(crawl.pngs[1], crawl.pngs[2]));
      check(s <= 0.005, `${gpu} ${look}: no crawl: ${(s * 100).toFixed(3)}% of the pixels change a frame with the camera still`);
      check(c >= Math.max(10 * s, 0.02), `${gpu} ${look}: control (the threshold moving) changes ${(c * 100).toFixed(2)}% a frame`);
    }
  }
  if (only.includes('settings')) await settings(gpu);
  if (only.includes('menus')) await menus(gpu);
  if (only.includes('switch')) await lookSwitch(gpu);
}
console.log(fail ? `detexto-dither: ${fail} failed` : 'detexto-dither: ok');
process.exit(fail ? 1 : 0);
