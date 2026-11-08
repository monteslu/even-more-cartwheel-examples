// jewels photosensitivity gate: the worst case (jewels_bench: long cascades of big clears, the climax of a cascade of
// six) must never strobe. Every tick of seven seconds of it is captured (tests/jewels-frames.mjs, box-filtered to
// 480 x 270) and measured against the general-flash and red-flash thresholds of WCAG 2.2 success criterion 2.3.1
// (the definitions the Harding test applies):
//   a flash is a pair of opposing changes in relative luminance of 0.1 or more (of the maximum, 1.0) where the darker
//   side is below 0.80; a red flash a pair of opposing changes of 20 or more in (R - G - B) x 320 (linear, floored at
//   0) on a saturated red (R / (R + G + B) >= 0.8);
//   more than three flashes in any one-second window fails when they cover an area larger than a quarter of a
//   10-degree field of view together: on a 55 inch 16:9 screen at 2.5 m that is 4.7% of the screen.
// The measure is by area, as the Harding test's is: the screen is covered by regions of about that size (2/9 of its
// width by 2/9 of its height, 4.9%), overlapping by half, 8 x 8 of them, and each region's mean colour is one signal.
// The most flashes any region has in any one-second window must be at most 3, general and red.
// Also reported, not judged: the share of pixels that each flash more than three times a second on their own (a spark
// or a shard crossing a pixel is one change of that pixel; many crossing it count, though no area flashes).
// CONTROL, which must fail: the same stretch with JEWELS_STROBE=1 (a light over the board switched on and off ten
// times a second for 1.5 s from each cascade's second step).
// Run: node tests/jewels-flash.mjs [gl|webgpu]   (renders natively: through the GPU suite lock)
//      node tests/jewels-flash.mjs --dir <frames>  (measure PNG frames already captured, one a tick, in name order)
import { spawnSync } from 'node:child_process';
import { readdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readPng } from './towers-look.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const args = process.argv.slice(2);
const DIR = args.includes('--dir') ? resolve(args[args.indexOf('--dir') + 1]) : null;
if (!DIR && !process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'jewels-flash', 'node', fileURLToPath(import.meta.url), ...args],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const GPU = args[0] === 'webgpu' ? 'webgpu' : 'gl';
const FROM = 355, COUNT = 420; // ticks 356-775: the bench's cascade of six, every step's clears, its climax and the next move
const MAX_FLASHES = 3, WIN = 60, CELLS = 18, SPAN = 4, STRIDE = 2; // regions: SPAN x SPAN of an 18 x 18 grid, every STRIDE cells

const LUT = Float32Array.from({ length: 256 }, (_, i) => { const c = i / 255; return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4; });

/** a transition counter over n signals: opposing changes of `step` or more, the darker side under `dark` */
function makeCounter(n, step, dark) {
  const dir = new Int8Array(n), ext = new Float32Array(n), lo = new Float32Array(n), hi = new Float32Array(n);
  let first = true;
  return (vals, onTransition) => {
    for (let i = 0; i < n; i++) {
      const v = vals[i];
      if (first) { ext[i] = lo[i] = hi[i] = v; continue; }
      if (dir[i] === 0) {
        if (v < lo[i]) lo[i] = v;
        if (v > hi[i]) hi[i] = v;
        if (v - lo[i] >= step && lo[i] < dark) dir[i] = 1, ext[i] = v, onTransition(i);
        else if (hi[i] - v >= step && v < dark) dir[i] = -1, ext[i] = v, onTransition(i);
      } else if (dir[i] > 0) {
        if (v > ext[i]) ext[i] = v;
        else if (ext[i] - v >= step && v < dark) dir[i] = -1, ext[i] = v, onTransition(i);
      } else {
        if (v < ext[i]) ext[i] = v;
        else if (v - ext[i] >= step && ext[i] < dark) dir[i] = 1, ext[i] = v, onTransition(i);
      }
    }
    first = false;
  };
}

export function analyse(files) {
  const f0 = readPng(files[0]);
  const { w, h } = f0, n = w * h, T = files.length;
  const per = Math.floor((CELLS - SPAN) / STRIDE) + 1, NR = per * per;
  const cellOf = new Int32Array(n);
  for (let i = 0; i < n; i++) cellOf[i] = Math.min(CELLS - 1, Math.floor((i % w) / w * CELLS)) + CELLS * Math.min(CELLS - 1, Math.floor(Math.floor(i / w) / h * CELLS));
  const cR = new Float64Array(CELLS * CELLS), cG = new Float64Array(CELLS * CELLS), cB = new Float64Array(CELLS * CELLS), cN = new Float64Array(CELLS * CELLS);
  const lumC = makeCounter(n, 0.1, 0.8), regC = makeCounter(NR, 0.1, 0.8), regRedC = makeCounter(NR, 20, Infinity);
  const pixT = Array.from({ length: n }, () => []), regT = Array.from({ length: NR }, () => []), regR = Array.from({ length: NR }, () => []);
  const Y = new Float32Array(n), rY = new Float32Array(NR), rRD = new Float32Array(NR);
  let maxMeanStep = 0, prevMean = -1;
  for (let t = 0; t < T; t++) {
    const { px } = t ? readPng(files[t]) : f0;
    cR.fill(0), cG.fill(0), cB.fill(0), cN.fill(0);
    let mean = 0;
    for (let i = 0; i < n; i++) {
      const r = LUT[px[i * 3]], g = LUT[px[i * 3 + 1]], b = LUT[px[i * 3 + 2]];
      Y[i] = 0.2126 * r + 0.7152 * g + 0.0722 * b;
      mean += Y[i];
      const c = cellOf[i];
      cR[c] += r, cG[c] += g, cB[c] += b, cN[c]++;
    }
    mean /= n;
    if (prevMean >= 0) maxMeanStep = Math.max(maxMeanStep, Math.abs(mean - prevMean));
    prevMean = mean;
    for (let ry = 0; ry < per; ry++)
      for (let rx = 0; rx < per; rx++) {
        let r = 0, g = 0, b = 0, m = 0;
        for (let y = ry * STRIDE; y < ry * STRIDE + SPAN; y++)
          for (let x = rx * STRIDE; x < rx * STRIDE + SPAN; x++) { const c = y * CELLS + x; r += cR[c], g += cG[c], b += cB[c], m += cN[c]; }
        r /= m, g /= m, b /= m;
        const k = ry * per + rx, s = r + g + b;
        rY[k] = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        rRD[k] = s > 0 && r / s >= 0.8 ? Math.max(0, (r - g - b) * 320) : 0;
      }
    lumC(Y, (i) => pixT[i].push(t));
    regC(rY, (k) => regT[k].push(t));
    regRedC(rRD, (k) => regR[k].push(t));
  }
  const flashesIn = (list, a) => { let c = 0; for (const x of list) if (x >= a && x < a + WIN) c++; return Math.floor(c / 2); };
  let regMax = 0, regRedMax = 0, regAt = 0, pixMax = 0, regTrans = 0;
  for (let a = 0; a + WIN <= T; a += 3) {
    for (let k = 0; k < NR; k++) {
      const f = flashesIn(regT[k], a);
      if (f > regMax) regMax = f, regAt = a;
      regRedMax = Math.max(regRedMax, flashesIn(regR[k], a));
    }
    let c = 0;
    for (let i = 0; i < n; i++) if (pixT[i].length > 2 * MAX_FLASHES && flashesIn(pixT[i], a) > MAX_FLASHES) c++;
    pixMax = Math.max(pixMax, c / n);
  }
  for (const l of regT) regTrans = Math.max(regTrans, l.length);
  return { regMax, regRedMax, regAt, pixMax, maxMeanStep, regTrans, regions: NR, frames: T };
}

const judge = (m) => {
  const why = [];
  if (m.regMax > MAX_FLASHES) why.push(`a region of the screen flashes ${m.regMax} times in one second (window from tick ${FROM + 1 + m.regAt}; at most ${MAX_FLASHES})`);
  if (m.regRedMax > MAX_FLASHES) why.push(`a region of the screen red-flashes ${m.regRedMax} times in one second (at most ${MAX_FLASHES})`);
  return why;
};
const line = (m) => `${m.frames} ticks, ${m.regions} regions: most flashes in one second in a region ${m.regMax} (red ${m.regRedMax}), most transitions in a region in the whole stretch ${m.regTrans}; ` +
  `largest change of the screen's mean luminance in one tick ${m.maxMeanStep.toFixed(4)}; pixels flashing more than ${MAX_FLASHES} a second on their own ${(m.pixMax * 100).toFixed(2)}% (not judged)`;

if (DIR) {
  const files = readdirSync(DIR).filter((f) => f.endsWith('.png')).sort().map((f) => join(DIR, f));
  const m = analyse(files), w = judge(m);
  console.log(`${w.length ? 'FAIL' : 'ok  '} ${DIR}: ${line(m)}${w.length ? '\n     ' + w.join('; ') : ''}`);
  process.exit(w.length ? 1 : 0);
}
const { captureTicks } = await import('./jewels-frames.mjs');
let bad = 0;
const run = async (label, env) => {
  const dir = join(ROOT, 'out', 'jewels-flash', `${label}-${GPU}`);
  rmSync(dir, { recursive: true, force: true });
  const files = await captureTicks({ app: 'jewels_bench', from: FROM, count: COUNT, scale: 4, gpu: GPU, env, dir });
  const m = analyse(files);
  rmSync(dir, { recursive: true, force: true });
  return m;
};
const real = await run('real', {});
const wr = judge(real);
if (wr.length) { bad++; console.log(`FAIL ${GPU} ${line(real)}\n     ${wr.join('; ')}`); } else console.log(`ok   ${GPU} no strobe: ${line(real)}`);
const ctl = await run('strobe', { JEWELS_STROBE: '1' });
const wc = judge(ctl);
if (wc.length) console.log(`ok   control fails as it must (JEWELS_STROBE): ${wc.join('; ')}`);
else { bad++; console.log(`CONTROL PASSED (a failure): ${line(ctl)}`); }
process.exit(bad ? 1 : 0);
