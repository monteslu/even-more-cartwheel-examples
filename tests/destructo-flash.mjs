// destructo flash check: no strobing. The game's biggest blast (destructo_bench: Great Keep, whose first charge
// breaks about 800 blocks, with its fire, dust, sparks, shards, crystal bursts and lights, the HUD on) is drawn frame
// by frame from just before the charge goes off for 2 s, and every frame is measured against the photosensitive
// flash rules (WCAG 2.3.1 general and red flash thresholds, the Harding rules' frame-by-frame form):
//   a transition is a change of 0.1 or more in an area's mean relative luminance against its last extreme, the darker
//   side under 0.8; a flash is a pair of opposing transitions (a swell and its fade is one). Red: the same on the
//   area's saturated red (red share 0.8 or more, (R - G - B) x 320, a change of 20).
//   large areas: the screen cut 6 x 6, every 2 x 2 block of it (a ninth of the screen, past the size a flash must
//     cover to count): no blink, that is no flash (a change of 0.1 or more and its opposite change of 0.1 or more)
//     within a third of a second (20 frames) (a blast may swell and fade over its second, as fire and smoke do; a
//     single change, such as the camera turning onto the pile, is not a flash), and no more than 3 general or red
//     flashes in any second;
//   any region: the screen cut 12 x 12, no cell may pass 3 general or 3 red flashes in any second.
// The largest slow swing of a ninth of the screen is reported (the blast's swell and fade: about 0.2 over a second).
// CONTROL, which must fail: the same blast with a white wash over the screen at 10 Hz (DESTRUCTO_STROBE_CONTROL).
//   node tests/destructo-flash.mjs                draws the frames (one run a frame: the bench is deterministic) into
//                                                 out/flash-destructo inside one hold of the GPU suite lock, measures
//   node tests/destructo-flash.mjs --dir <d>      measures f<n>.png frames already in d (and c<n>.png, the control)
import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, readdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const argv = process.argv.slice(2);
const dirArg = argv.includes('--dir') ? argv[argv.indexOf('--dir') + 1] : null;
const BEFORE = 8, COUNT = 120, CONTROL = 48, PAR = 3, BLINK = 20;

if (!dirArg && !process.env.CW_GPU_SUITE_INNER) { /* the captures run inside one hold of the lock */
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'destructo-flash', 'node', fileURLToPath(import.meta.url), ...argv],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { readImage } = await import(join(ENGINE, 'tools/lib/png.mjs'));
const dir = dirArg || join(ROOT, 'out/flash-destructo');
if (!dirArg) {
  const { glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
  const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/destructo_bench.json'), 'utf8')).binary;
  mkdirSync(dir, { recursive: true });
  const envFor = (frames, extra) => {
    const env = { ...process.env, CW_FRAMES: String(frames), CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl', DESTRUCTO_QUALITY: '3', ...extra };
    const gl = glPin();
    if (gl) Object.assign(env, gl.env);
    scrubDisplay(env);
    return env;
  };
  /* where the charge goes off */
  const probe = spawnSync(exe, [], { env: envFor(1500, { DESTRUCTO_FX_LOG: '1' }), encoding: 'utf8', maxBuffer: 1 << 26, timeout: 600000 });
  const m = /destructo boom frame=(\d+)/.exec(probe.stderr || '');
  if (!m) { console.log(`FAIL the bench fired no charge in 1500 frames (exit ${probe.status})`); process.exit(1); }
  const boom = +m[1];
  console.log(`the charge goes off on frame ${boom}`);
  const one = (f, name, extra) => new Promise((ok) => spawn(exe, [], { stdio: 'ignore', env: envFor(f, { CW_SCREENSHOT: join(dir, name), ...extra }) }).on('exit', ok));
  const jobs = [];
  for (let f = boom - BEFORE; f < boom - BEFORE + COUNT; f++) jobs.push([f, `f${f}.png`, {}]);
  for (let f = boom; f < boom + CONTROL; f++) jobs.push([f, `c${f}.png`, { DESTRUCTO_STROBE_CONTROL: '1' }]);
  while (jobs.length) await Promise.all(jobs.splice(0, PAR).map((j) => one(...j)));
}
const list = (p) => readdirSync(dir).filter((n) => new RegExp(`^${p}\\d+\\.png$`).test(n)).sort((a, b) => +a.slice(1, -4) - +b.slice(1, -4));
const files = list('f'), ctl = list('c');
if (files.length < COUNT || ctl.length < CONTROL) { console.log(`FAIL only ${files.length} frames and ${ctl.length} control frames in ${dir}`); process.exit(1); }

const LIN = Float32Array.from({ length: 256 }, (_, i) => { const v = i / 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; });
/** per frame: each cell's mean relative luminance and saturated red, on an n x n grid */
function cells({ w, h, rgba }, n) {
  const L = new Float64Array(n * n), R = new Float64Array(n * n), c = new Float64Array(n * n);
  for (let y = 0; y < h; y += 2)
    for (let x = 0; x < w; x += 2) {
      const i = (y * w + x) * 4, r = LIN[rgba[i]], g = LIN[rgba[i + 1]], b = LIN[rgba[i + 2]];
      const k = Math.min(n - 1, Math.floor(y * n / h)) * n + Math.min(n - 1, Math.floor(x * n / w));
      L[k] += 0.2126 * r + 0.7152 * g + 0.0722 * b, c[k]++;
      const s = r + g + b;
      if (s > 0 && r / s >= 0.8) R[k] += Math.max(0, (r - g - b) * 320);
    }
  for (let k = 0; k < n * n; k++) L[k] /= c[k], R[k] /= c[k];
  return { L, R };
}
/** the frames each flash completes on: a transition is a move of `step` or more from the last extreme (the darker
 * side under darkMax); moves the same way only stretch the extreme, so transitions alternate, and every second one
 * completes a flash */
function flashes(series, step, darkMax) {
  const out = [];
  let ext = series[0], dir = 0, n = 0;
  for (let i = 1; i < series.length; i++) {
    const v = series[i];
    if ((dir === 1 && v > ext) || (dir === -1 && v < ext)) { ext = v; continue; }
    if (Math.abs(v - ext) >= step && Math.min(v, ext) < darkMax) {
      dir = v > ext ? 1 : -1, ext = v;
      if (++n % 2 === 0) out.push(i);
    }
  }
  return out;
}
const inSec = (list) => Math.max(0, ...list.map((t) => list.filter((u) => u >= t && u < t + 60).length));
function measure(names) {
  const imgs = names.map((f) => readImage(join(dir, f)));
  const big = imgs.map((im) => cells(im, 6)), small = imgs.map((im) => cells(im, 12));
  const r = { large: 0, largeRed: 0, where: '', sec: 0, secRed: 0, swell: 0, blink: 0, blinkAt: '' };
  for (let by = 0; by < 5; by++)
    for (let bx = 0; bx < 5; bx++) {
      const ids = [by * 6 + bx, by * 6 + bx + 1, (by + 1) * 6 + bx, (by + 1) * 6 + bx + 1];
      const L = big.map((p) => ids.reduce((a, c) => a + p.L[c], 0) / 4), R = big.map((p) => ids.reduce((a, c) => a + p.R[c], 0) / 4);
      const fl = flashes(L, 0.1, 0.8), fr = flashes(R, 20, Infinity);
      if (inSec(fl) + inSec(fr) > r.large + r.largeRed) r.where = `block ${bx},${by}`;
      r.large = Math.max(r.large, inSec(fl)), r.largeRed = Math.max(r.largeRed, inSec(fr));
      /* a blink: within BLINK frames, a move away and back, the smaller of the two moves */
      for (let i = 0; i < L.length; i++)
        for (let j = i + 1; j < Math.min(L.length, i + BLINK); j++)
          for (let k = j + 1; k <= Math.min(L.length - 1, i + BLINK); k++) {
            const a = L[j] - L[i], b = L[j] - L[k];
            const m = Math.sign(a) === Math.sign(b) ? Math.min(Math.abs(a), Math.abs(b)) : 0;
            if (m > r.blink) r.blink = m, r.blinkAt = `block ${bx},${by} frames ${i}-${j}-${k}`;
          }
      r.swell = Math.max(r.swell, Math.max(...L) - Math.min(...L));
    }
  for (let k = 0; k < 144; k++) {
    r.sec = Math.max(r.sec, inSec(flashes(small.map((p) => p.L[k]), 0.1, 0.8)));
    r.secRed = Math.max(r.secRed, inSec(flashes(small.map((p) => p.R[k]), 20, Infinity)));
  }
  return r;
}
const pass = (m) => m.blink < 0.1 && m.large <= 3 && m.largeRed <= 3 && m.sec <= 3 && m.secRed <= 3;
const show = (m) => `the largest blink of a ninth of the screen (there and back within ${BLINK} frames) ${m.blink.toFixed(3)} (${m.blinkAt}); most flashes in a second in a ninth of the screen ${m.large}${m.where ? ` (${m.where})` : ''}, red ${m.largeRed}; in any 1/144 of it ${m.sec}, red ${m.secRed}; the slow swing of a ninth of the screen ${m.swell.toFixed(3)} relative luminance`;
let bad = 0;
const m = measure(files);
console.log(`${pass(m) ? 'ok  ' : 'FAIL'} ${files.length} frames of Great Keep's blast: ${show(m)}`);
if (!pass(m)) bad++;
const c = measure(ctl);
if (!pass(c)) console.log(`ok   control fails as it must: ${ctl.length} frames of the blast under a 10 Hz white wash: ${show(c)}`);
else { console.log(`CONTROL PASSED (a failure): the strobe went unseen: ${show(c)}`); bad++; }
process.exit(bad ? 1 : 0);
