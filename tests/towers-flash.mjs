// towers photosensitivity check: nothing in the game strobes. Every tick of a busy stretch is captured over the debug
// channel (natively, headless: the app starts paused, runs to the stretch, then steps one tick at a time and takes a
// screenshot after each at 960 x 540) and measured against the general-flash and red-flash thresholds of WCAG 2.2
// success criterion 2.3.1 (the definitions the Harding test applies):
//   a flash is a pair of opposing changes in relative luminance of 0.1 or more where the darker side is below 0.80; a
//   red flash a pair of opposing changes of 20 or more in (R - G - B) x 320 (linear, floored at 0) on a saturated red
//   (R / (R + G + B) >= 0.8).
// Each scene is captured twice from the same deterministic run: as it is, and with the crowd's bodies hidden
// (TOWERS_HIDE=3, the bodies and their blob shadows: every effect, light, beam and bolt the same). Thousands of creeps walking over the path change the
// pixels under them several times a second, which a per-pixel count cannot tell from a flash, so the per-pixel measures
// read the frames without the crowd; the regions read both.
// Three measures:
//   regions: the screen in 5 x 5 tiles (each 4% of it), each tile's mean colour as one signal: the most flashes any
//            tile has in any one-second window (at most 3, general and red; with and without the crowd);
//   area:    every pixel's own signal (at 480 x 270): the share of the screen whose pixels flash more than three times
//            in one second (at most 4.7%, a quarter of a 10 degree field of view on a 55 inch screen at 2.5 m;
//            general and red);
//   flicker: every pixel at 480 x 270: the share of the screen where a pixel keeps flickering, four flashes within
//            30 ticks (8 Hz or faster for half a second, however small: a beam or a glow flickering, lightning
//            re-jagged, a light that blinks), in any one-second window (at most FLICKER_MAX). One spark or a ripple
//            passing over a pixel is one flash and does not count; a speck under two pixels of 960 x 540 drifting (a
//            mote) averages away at this size. Also reported, not judged: the share flashing twice within 12 ticks. Where the pixels
//            flickered is written beside the frames (flicker-<scene>.ppm, red over the first frame) for a look.
// Scenes: towers_bench (map 26: level-3 towers on every pad, 2400 creeps, the busiest the game gets), and the volcano
// (map 23, wave 18, towers on every pad, four bots: red ground under lava, mortar blasts and a crowd).
// CONTROLS, which must fail: the bench with TOWERS_STROBE_CONTROL=1 (the old effects: lightning re-jagged every other
// frame, step-on flash lights, a beam flickering at 7 Hz, hit creeps popping white); and the bench's own frames with
// a quarter of the screen brightened on every other 3 ticks (a 10 Hz blink the size of a flash wash).
// Run: node tests/towers-flash.mjs [webgpu] [--keep]   (holds the GPU suite lock; frames in out/towers-flash, removed
// after each scene unless --keep, which keeps them; a few frames of each scene are always kept in out/towers-flash/keep)
import { spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readPng } from './towers-look.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'towers-flash', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const GPU = process.argv.includes('webgpu') ? 'webgpu' : 'gl';
const KEEP = process.argv.includes('--keep');
const COUNT = 240, WIN = 60, TILES = 5, MAX_FLASHES = 3, AREA = 0.047, FLICKER_GAP = 12, SUSTAIN_GAP = 30, FLICKER_MAX = 0.0025; // (a quarter of a percent: the old effects kept 0.85% flickering)
const HIDE = { TOWERS_HIDE: '3' }; // (the crowd and its blob shadows)
const SCENES = [
  { name: 'bench', app: 'towers_bench', from: 300, env: {} },
  { name: 'volcano', app: 'towers_auto', from: 600, env: { TOWERS_MAP: '23', TOWERS_WAVE: '18', TOWERS_PLAYERS: '4' } },
];

/** every tick of [from + 1, from + count], one PNG each */
async function captureTicks({ app, from, count, env, dir }) {
  const { launchApp } = await import(join(ENGINE, 'tools/lib/edsession.mjs'));
  const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
  const exe = JSON.parse(readFileSync(join(ROOT, `build/native/apps/${app}.json`), 'utf8')).binary;
  mkdirSync(dir, { recursive: true });
  const e = { CW_WGPU_VALIDATION: '0', CW_WARMUP_SPREAD: '0', CW_START_PAUSED: '1', CW_FRAMES: '10000000', CW_RENDER_SCALE: '1.5', TOWERS_NO_HUD: '1', ...env };
  const pin = GPU === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(e, pin.env, { CW_GPU: 'webgpu' });
  else { e.CW_GPU = 'gl'; if (gl) Object.assign(e, gl.env); }
  const full = { ...process.env, ...e };
  scrubDisplay(full);
  const s = await launchApp(exe, { env: full, timeoutMs: 120000 });
  const out = [];
  try {
    const st = await s.request('status', 60000);
    if (st.error) throw new Error(st.error);
    for (let left = from; left > 0;) {
      const n = Math.min(left, 120), r = await s.request(`step ${n}`, 120000);
      if (r.error) throw new Error(r.error);
      left -= n;
    }
    for (let i = 0; i < count; i++) {
      const r = await s.request('step 1', 60000);
      if (r.error) throw new Error(r.error);
      const p = join(dir, `t${String(from + i + 1).padStart(5, '0')}.png`);
      await s.screenshot('all 2', p); // (the scale rides in the layers argument: 1920 x 1080 / 2)
      out.push(p);
    }
  } finally {
    await s.close();
  }
  return out;
}

const lin = (v) => { const c = v / 255; return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4; };
const LUT = Float32Array.from({ length: 256 }, (_, i) => lin(i));

/** a transition counter over n signals: opposing changes of `step` or more, the darker side under `dark`; calls
 * on(i) at each transition */
function makeCounter(n, step, dark) {
  const dir = new Int8Array(n), ext = new Float32Array(n), lo = new Float32Array(n), hi = new Float32Array(n);
  let first = true;
  return (vals, on) => {
    for (let i = 0; i < n; i++) {
      const v = vals[i];
      if (first) { ext[i] = lo[i] = hi[i] = v; continue; }
      if (dir[i] === 0) {
        if (v < lo[i]) lo[i] = v;
        if (v > hi[i]) hi[i] = v;
        if (v - lo[i] >= step && lo[i] < dark) dir[i] = 1, ext[i] = v, on(i);
        else if (hi[i] - v >= step && v < dark) dir[i] = -1, ext[i] = v, on(i);
      } else if (dir[i] > 0) {
        if (v > ext[i]) ext[i] = v;
        else if (ext[i] - v >= step && v < dark) dir[i] = -1, ext[i] = v, on(i);
      } else {
        if (v < ext[i]) ext[i] = v;
        else if (v - ext[i] >= step && ext[i] < dark) dir[i] = 1, ext[i] = v, on(i);
      }
    }
    first = false;
  };
}
/** the last k transition times of n signals: has(i, t, m, span) says whether the m-th last lies within span of t */
function makeRing(n, k) {
  const t = new Int32Array(n * k).fill(-1e6), c = new Int32Array(n);
  return {
    push(i, tick) { t[i * k + (c[i]++ % k)] = tick; },
    within(i, tick, m, span) { return c[i] >= m && tick - t[i * k + ((c[i] - m) % k)] < span; },
    count(i, tick, span) { let r = 0; for (let q = 0; q < k; q++) if (tick - t[i * k + q] < span) r++; return r; },
  };
}

/** blink: the control's 10 Hz quarter-screen blink (top left), brightened on every other 3 ticks */
function analyse(files, blink = false) {
  const f0 = readPng(files[0]);
  const W = f0.w, H = f0.h, n = W * H, w2 = W >> 1, h2 = H >> 1, n2 = w2 * h2, T = files.length;
  const Y = new Float32Array(n), Y2 = new Float32Array(n2), R2 = new Float32Array(n2), tY = new Float32Array(TILES * TILES), tR = new Float32Array(TILES * TILES);
  const sR = new Float64Array(TILES * TILES), sG = new Float64Array(TILES * TILES), sB = new Float64Array(TILES * TILES), sN = new Float64Array(TILES * TILES);
  const flickC = makeCounter(n2, 0.1, 0.8), areaC = makeCounter(n2, 0.1, 0.8), areaRC = makeCounter(n2, 20, Infinity);
  const tileC = makeCounter(TILES * TILES, 0.1, 0.8), tileRC = makeCounter(TILES * TILES, 20, Infinity);
  const flickR = makeRing(n2, 8), areaR = makeRing(n2, 8), areaRR = makeRing(n2, 8), tileR = makeRing(TILES * TILES, 32), tileRR = makeRing(TILES * TILES, 32);
  const lastFlick = new Int32Array(n2).fill(-1e6), lastPair = new Int32Array(n2).fill(-1e6), tileOf = new Uint8Array(n);
  for (let i = 0; i < n; i++) tileOf[i] = Math.min(TILES - 1, Math.floor((i % W) * TILES / W)) + TILES * Math.min(TILES - 1, Math.floor(Math.floor(i / W) * TILES / H));
  const m = { pairs: 0, tileMax: 0, tileRedMax: 0, area: 0, areaRed: 0, flicker: 0, flickerAt: 0, flickerEver: 0, frames: T, maxMeanStep: 0 };
  let prevMean = -1;
  const ever = new Uint8Array(n2);
  for (let t = 0; t < T; t++) {
    const { px } = t ? readPng(files[t]) : f0;
    const lit = blink && Math.floor(t / 3) % 2 === 1;
    sR.fill(0), sG.fill(0), sB.fill(0), sN.fill(0), Y2.fill(0), R2.fill(0);
    let mean = 0;
    for (let y = 0; y < H; y++)
      for (let x = 0; x < W; x++) {
        const i = y * W + x;
        let r = LUT[px[i * 3]], g = LUT[px[i * 3 + 1]], b = LUT[px[i * 3 + 2]];
        if (lit && x < W / 2 && y < H / 2) r = Math.min(1, r + 0.3), g = Math.min(1, g + 0.3), b = Math.min(1, b + 0.3);
        const yy = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        Y[i] = yy, mean += yy;
        const k = tileOf[i];
        sR[k] += r, sG[k] += g, sB[k] += b, sN[k]++;
        if (x < w2 * 2 && y < h2 * 2) Y2[(y >> 1) * w2 + (x >> 1)] += yy * 0.25; // (red is worked out from the 2 x 2 mean below)
      }
    // the red signal at 480 x 270 from the 2 x 2 mean colour
    for (let y = 0; y < h2; y++)
      for (let x = 0; x < w2; x++) {
        let r = 0, g = 0, b = 0;
        for (let q = 0; q < 4; q++) {
          const i = ((y << 1) + (q >> 1)) * W + (x << 1) + (q & 1);
          let rr = LUT[px[i * 3]], gg = LUT[px[i * 3 + 1]], bb = LUT[px[i * 3 + 2]];
          if (lit && (x << 1) < W / 2 && (y << 1) < H / 2) rr = Math.min(1, rr + 0.3), gg = Math.min(1, gg + 0.3), bb = Math.min(1, bb + 0.3);
          r += rr / 4, g += gg / 4, b += bb / 4;
        }
        const s = r + g + b;
        R2[y * w2 + x] = s > 0 && r / s >= 0.8 ? Math.max(0, (r - g - b) * 320) : 0;
      }
    mean /= n;
    if (prevMean >= 0) m.maxMeanStep = Math.max(m.maxMeanStep, Math.abs(mean - prevMean));
    prevMean = mean;
    for (let k = 0; k < TILES * TILES; k++) {
      const r = sR[k] / sN[k], g = sG[k] / sN[k], b = sB[k] / sN[k], s = r + g + b;
      tY[k] = 0.2126 * r + 0.7152 * g + 0.0722 * b;
      tR[k] = s > 0 && r / s >= 0.8 ? Math.max(0, (r - g - b) * 320) : 0;
    }
    // regions: a tile over 3 flashes (8 transitions) in the second ending now
    tileC(tY, (k) => { tileR.push(k, t); m.tileMax = Math.max(m.tileMax, tileR.count(k, t, WIN) >> 1); });
    tileRC(tR, (k) => { tileRR.push(k, t); m.tileRedMax = Math.max(m.tileRedMax, tileRR.count(k, t, WIN) >> 1); });
    // area: pixels at 480 x 270 over 3 flashes in the second ending now
    areaC(Y2, (j) => areaR.push(j, t));
    areaRC(R2, (j) => areaRR.push(j, t));
    let over = 0, overR = 0;
    for (let j = 0; j < n2; j++) {
      if (areaR.within(j, t, 8, WIN)) over++;
      if (areaRR.within(j, t, 8, WIN)) overR++;
    }
    m.area = Math.max(m.area, over / n2), m.areaRed = Math.max(m.areaRed, overR / n2);
    // flicker: a pixel flashing twice (4 transitions) within FLICKER_GAP ticks
    flickC(Y2, (i) => {
      flickR.push(i, t);
      if (flickR.within(i, t, 4, FLICKER_GAP)) lastPair[i] = t;
      if (flickR.within(i, t, 8, SUSTAIN_GAP)) lastFlick[i] = t, ever[i] = 1;
    });
    let fl = 0, pr = 0;
    for (let j = 0; j < n2; j++) { if (t - lastFlick[j] < WIN) fl++; if (t - lastPair[j] < WIN) pr++; }
    m.pairs = Math.max(m.pairs, pr / n2);
    if (fl / n2 > m.flicker) m.flicker = fl / n2, m.flickerAt = t;
  }
  m.flickerEver = ever.reduce((a, v) => a + v, 0) / n2;
  m.map = (path) => { /* where it flickered: red over the first frame (480 x 270) */
    const out = Buffer.alloc(n2 * 3);
    for (let j = 0; j < n2; j++) {
      const i = ((j / w2) | 0) * 2 * W + (j % w2) * 2, g = f0.px[i * 3 + 1] >> 1;
      out[j * 3] = ever[j] ? 255 : g, out[j * 3 + 1] = ever[j] ? 0 : g, out[j * 3 + 2] = ever[j] ? 0 : g;
    }
    writeFileSync(path, Buffer.concat([Buffer.from(`P6 ${w2} ${h2} 255\n`), out]));
  };
  return m;
}
function judge(m) {
  const why = [];
  if (m.tileMax > MAX_FLASHES) why.push(`a 4% region of the screen flashes more than ${MAX_FLASHES} times in a second`);
  if (m.tileRedMax > MAX_FLASHES) why.push(`a 4% region red-flashes more than ${MAX_FLASHES} times in a second`);
  if (m.area > AREA) why.push(`${(m.area * 100).toFixed(2)}% of the screen flashes more than ${MAX_FLASHES} times in a second (at most ${AREA * 100}%)`);
  if (m.areaRed > AREA) why.push(`${(m.areaRed * 100).toFixed(2)}% of the screen red-flashes more than ${MAX_FLASHES} times in a second`);
  if (m.flicker > FLICKER_MAX) why.push(`${(m.flicker * 100).toFixed(3)}% of the screen keeps flickering (8 Hz or faster for half a second) within one second (tick ${m.flickerAt}; at most ${(FLICKER_MAX * 100).toFixed(2)}%)`);
  return why;
}
const line = (m) => `${m.frames} ticks: most flashes in a second in any of ${TILES * TILES} regions ${m.tileMax} (red ${m.tileRedMax}); ` +
  `screen flashing over ${MAX_FLASHES} a second ${(m.area * 100).toFixed(3)}% (red ${(m.areaRed * 100).toFixed(3)}%); flickering (8 Hz+ for half a second) in a second ${(m.flicker * 100).toFixed(4)}% (two flashes inside 12 ticks: ${(m.pairs * 100).toFixed(3)}%) ` +
  `(ever ${(m.flickerEver * 100).toFixed(4)}%); largest one-tick change of the mean luminance ${m.maxMeanStep.toFixed(4)}`;

let bad = 0;
const capture = async (sc, label, extra = {}) => {
  const dir = join(ROOT, 'out', 'towers-flash', `${sc.name}-${label}-${GPU}`);
  rmSync(dir, { recursive: true, force: true });
  const files = await captureTicks({ ...sc, count: COUNT, env: { ...sc.env, ...extra }, dir });
  const keep = join(ROOT, 'out', 'towers-flash', 'keep');
  mkdirSync(keep, { recursive: true });
  for (const k of [0, 1, 2, 3, 4, 5, 6, 7, 8, 60]) copyFileSync(files[k], join(keep, `${sc.name}-${label}-${GPU}-${k}.png`));
  return { files, done: () => { if (!KEEP) rmSync(dir, { recursive: true, force: true }); } };
};
for (const sc of SCENES) {
  { /* as it is: the regions */
    const cap = await capture(sc, 'real');
    const m = analyse(cap.files);
    if (m.tileMax > MAX_FLASHES || m.tileRedMax > MAX_FLASHES) { bad++; console.log(`FAIL ${sc.name} ${GPU} with the crowd: ${line(m)}`); }
    else console.log(`ok   ${sc.name} ${GPU} with the crowd, no region flashes: ${line(m)}`);
    if (sc.name === 'bench') {
      const b = analyse(cap.files, true), wb = judge(b);
      if (b.tileMax > MAX_FLASHES) console.log(`ok   control fails as it must (a quarter of the screen blinking at 10 Hz): ${wb.join('; ')}`);
      else { bad++; console.log(`CONTROL PASSED (a failure): the 10 Hz blink went unseen: ${line(b)}`); }
    }
    cap.done();
  }
  { /* the effects alone: every measure */
    const cap = await capture(sc, 'effects', HIDE);
    const m = analyse(cap.files), w = judge(m);
    m.map(join(ROOT, 'out', 'towers-flash', 'keep', `flicker-${sc.name}-${GPU}.ppm`));
    if (w.length) { bad++; console.log(`FAIL ${sc.name} ${GPU} effects ${line(m)}\n     ${w.join('; ')}`); } else console.log(`ok   ${sc.name} ${GPU} effects, no strobe: ${line(m)}`);
    cap.done();
  }
}
{
  const cap = await capture(SCENES[0], 'strobe', { ...HIDE, TOWERS_STROBE_CONTROL: '1' });
  const m = analyse(cap.files), w = judge(m);
  m.map(join(ROOT, 'out', 'towers-flash', 'keep', `flicker-strobe-${GPU}.ppm`));
  if (w.length) console.log(`ok   control fails as it must (TOWERS_STROBE_CONTROL, the old effects): ${w.join('; ')}\n     ${line(m)}`);
  else { bad++; console.log(`CONTROL PASSED (a failure): the old flicker went unseen: ${line(m)}`); }
  cap.done();
}
process.exit(bad ? 1 : 0);
