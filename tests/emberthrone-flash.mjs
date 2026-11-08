// emberthrone flash check: no strobing. A boss fight (emberthrone_bench: the Tidecaller in its temple court, the bot
// cutting and rolling, the boss's tells, rings and shockwaves, the blade ribbons, sparks, impact lights, staggers and
// a phase's roar, the HUD on) is drawn frame by frame for 2 seconds from just before the busiest moment of the fight
// (its first phase roar, else its first critical or stagger), and every frame is measured against the photosensitive
// flash rules (WCAG 2.3.1's general and red flash thresholds, in the Harding rules' frame-by-frame form):
//   a transition is a change of 0.1 or more in an area's mean relative luminance against its last extreme, the darker
//   side under 0.8; a flash is a pair of opposing transitions (a swell and its fade is one). Red: the same on the
//   area's saturated red (red share 0.8 or more, (R - G - B) x 320, a change of 20).
//   large areas: the screen cut 6 x 6, every 2 x 2 block of it (a ninth of the screen, past the size a flash must
//     cover to count) must not flash at all, general or red;
//   any region: the screen cut 12 x 12, no cell may pass 3 general or 3 red flashes in any second.
// CONTROL, which must fail: the same fight with a white wash over the screen at 10 Hz (ADV_STROBE_CONTROL).
//   node tests/emberthrone-flash.mjs             draws the frames (one run a frame: the bench is deterministic) into
//                                                out/flash-emberthrone inside one hold of the GPU suite lock, measures
//   node tests/emberthrone-flash.mjs --dir <d>   measures f<n>.png frames already in d (and c<n>.png, the control)
import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, readdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const argv = process.argv.slice(2);
const dirArg = argv.includes('--dir') ? argv[argv.indexOf('--dir') + 1] : null;
const BEFORE = 12, COUNT = 120, CONTROL = 48, PAR = 3;

if (!dirArg && !process.env.CW_GPU_SUITE_INNER) { /* the captures run inside one hold of the lock */
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'emberthrone-flash', 'node', fileURLToPath(import.meta.url), ...argv],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { readImage } = await import(join(ENGINE, 'tools/lib/png.mjs'));
const dir = dirArg || join(ROOT, 'out/flash-emberthrone');
if (!dirArg) {
  const { glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
  const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/emberthrone_bench.json'), 'utf8')).binary;
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const envFor = (frames, extra) => {
    const env = { ...process.env, CW_FRAMES: String(frames), CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl', ADV_QUALITY: '3', ...extra };
    const gl = glPin();
    if (gl) Object.assign(env, gl.env);
    scrubDisplay(env);
    return env;
  };
  /* where the fight is busiest */
  const probe = spawnSync(exe, [], { env: envFor(2400, { ADV_FX_LOG: '1' }), encoding: 'utf8', maxBuffer: 1 << 26, timeout: 900000 });
  const ev = [...(probe.stderr || '').matchAll(/adv-fx frame=(\d+) tick=\d+ kind=(\d+)/g)].map((m) => ({ frame: +m[1], kind: +m[2] }));
  const ROAR = 10, CRIT = 22, STAGGER = 23;
  const pick = ev.find((e) => e.kind === ROAR) || ev.find((e) => e.kind === CRIT) || ev.find((e) => e.kind === STAGGER);
  if (!pick) { console.log(`FAIL the bench showed no roar, critical or stagger in 2400 frames (exit ${probe.status}, ${ev.length} blows)`); process.exit(1); }
  const at = pick.frame;
  console.log(`the fight's ${pick.kind === ROAR ? 'phase roar' : pick.kind === CRIT ? 'first critical' : 'first stagger'} shows on frame ${at}; ${ev.filter((e) => e.frame >= at - BEFORE && e.frame < at - BEFORE + COUNT).length} blows and stuns in the window`);
  const one = (f, name, extra) => new Promise((ok) => spawn(exe, [], { stdio: 'ignore', env: envFor(f, { CW_SCREENSHOT: join(dir, name), ...extra }) }).on('exit', ok));
  const jobs = [];
  for (let f = at - BEFORE; f < at - BEFORE + COUNT; f++) jobs.push([f, `f${f}.png`, {}]);
  for (let f = at; f < at + CONTROL; f++) jobs.push([f, `c${f}.png`, { ADV_STROBE_CONTROL: '1' }]);
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
  const r = { large: 0, largeRed: 0, where: '', sec: 0, secRed: 0, swell: 0, cellSwell: 0 };
  for (let by = 0; by < 5; by++)
    for (let bx = 0; bx < 5; bx++) {
      const ids = [by * 6 + bx, by * 6 + bx + 1, (by + 1) * 6 + bx, (by + 1) * 6 + bx + 1];
      const L = big.map((p) => ids.reduce((a, c) => a + p.L[c], 0) / 4), R = big.map((p) => ids.reduce((a, c) => a + p.R[c], 0) / 4);
      const fl = flashes(L, 0.1, 0.8), fr = flashes(R, 20, Infinity);
      if (fl.length + fr.length > r.large + r.largeRed) r.where = `block ${bx},${by}`;
      r.large = Math.max(r.large, fl.length), r.largeRed = Math.max(r.largeRed, fr.length);
      r.swell = Math.max(r.swell, Math.max(...L) - Math.min(...L));
    }
  for (let k = 0; k < 144; k++) {
    const L = small.map((p) => p.L[k]);
    r.sec = Math.max(r.sec, inSec(flashes(L, 0.1, 0.8)));
    r.secRed = Math.max(r.secRed, inSec(flashes(small.map((p) => p.R[k]), 20, Infinity)));
    r.cellSwell = Math.max(r.cellSwell, Math.max(...L) - Math.min(...L));
  }
  return r;
}
const pass = (m) => m.large === 0 && m.largeRed === 0 && m.sec <= 3 && m.secRed <= 3;
const show = (m) => `large-area flashes ${m.large}${m.where ? ` (${m.where})` : ''}, red ${m.largeRed}; most in a second in any 1/144 of the screen ${m.sec}, red ${m.secRed}; the largest swing of a ninth of the screen ${m.swell.toFixed(3)}, of a 1/144 ${m.cellSwell.toFixed(3)} relative luminance`;
let bad = 0;
const m = measure(files);
console.log(`${pass(m) ? 'ok  ' : 'FAIL'} ${files.length} frames of a boss fight: ${show(m)}`);
if (!pass(m)) bad++;
const c = measure(ctl);
if (!pass(c)) console.log(`ok   control fails as it must: ${ctl.length} frames of the fight under a 10 Hz white wash: ${show(c)}`);
else { console.log(`CONTROL PASSED (a failure): the strobe went unseen: ${show(c)}`); bad++; }
process.exit(bad ? 1 : 0);
