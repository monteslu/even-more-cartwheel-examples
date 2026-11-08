// lanternfall flash check: no strobing. A busy fight (lanternfall_bench: a warden fight in its arena, both players, the
// warden's moves, shots, sparks, particles, hits) is drawn frame by frame for 1.5 seconds, and every frame is measured
// against the photosensitive flash rules (WCAG 2.3.1's general and red flash thresholds):
//   the screen is cut into a 6 x 6 grid, and every 2 x 2 block of it (a ninth of the screen, past the size a flash
//   must cover to count) is followed through time; a transition is a change in the block's relative luminance of 0.1
//   or more against its last extreme with the darker side under 0.8, and a flash is a pair of opposing transitions;
//   red flashes count the same on the block's saturated red (red share 0.8 or more, (R - G - B) x 320, a change of 20)
//   bounds: no block may flash at all (this game has no large-area flashes; WCAG allows 3 a second, so this is
//   stricter), and no block may pass 3 red or general flashes in any second.
// CONTROL, which must fail: the same frames with a quarter of the screen brightened on every other 6 frames (a 5 Hz
// blink of the kind a flash wash or a blinking sprite the size of a room would make).
//   node tests/lanternfall-flash.mjs              draws the frames (one run per frame: the bench is deterministic) into
//                                              out/flash-lanternfall inside one hold of the GPU suite lock, then measures
//   node tests/lanternfall-flash.mjs --dir <d>    measures f<n>.png frames already in d
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readPng } from './lanternfall-look.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const BIN = [join(ROOT, 'build/native/project/games/lanternfall'), join(ROOT, 'build/native/games/lanternfall')].find((d) => existsSync(join(d, 'lanternfall_bench')));
const argv = process.argv.slice(2);
const dirArg = argv.includes('--dir') ? argv[argv.indexOf('--dir') + 1] : null;
const FIRST = 240, COUNT = 90;

if (!dirArg && !process.env.CW_GPU_SUITE_INNER) { /* the captures run inside one hold of the lock */
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'lanternfall-flash', 'node', fileURLToPath(import.meta.url), ...argv],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const dir = dirArg || join(ROOT, 'out/flash-lanternfall');
if (!dirArg) {
  mkdirSync(dir, { recursive: true });
  const one = (f) => new Promise((ok) => spawn(join(BIN, 'lanternfall_bench'), [], {
    stdio: 'ignore', env: { ...process.env, CW_FRAMES: String(f), CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_SCREENSHOT: join(dir, `f${f}.png`) },
  }).on('exit', ok));
  for (let f = FIRST; f < FIRST + COUNT; f += 2) await Promise.all([one(f), one(f + 1)]);
}
const files = readdirSync(dir).filter((n) => /^f\d+\.png$/.test(n)).sort((a, b) => +a.slice(1, -4) - +b.slice(1, -4));
if (files.length < 60) { console.log(`FAIL only ${files.length} frames in ${dir}`); process.exit(1); }

const lin = (v) => { v /= 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; };
const LIN = Float32Array.from({ length: 256 }, (_, i) => lin(i));
const G = 6;
/** per frame: each grid cell's mean relative luminance and saturated red */
function cells({ w, h, px }, blink) {
  const L = new Float64Array(G * G), R = new Float64Array(G * G), n = new Float64Array(G * G);
  for (let y = 0; y < h; y += 2)
    for (let x = 0; x < w; x += 2) {
      const i = (y * w + x) * 3;
      let r = LIN[px[i]], g = LIN[px[i + 1]], b = LIN[px[i + 2]];
      if (blink && x < w / 2 && y < h / 2) r = Math.min(1, r + 0.35), g = Math.min(1, g + 0.35), b = Math.min(1, b + 0.35);
      const c = Math.min(G - 1, Math.floor(y * G / h)) * G + Math.min(G - 1, Math.floor(x * G / w));
      L[c] += 0.2126 * r + 0.7152 * g + 0.0722 * b, n[c]++;
      const s = r + g + b;
      if (s > 0 && r / s >= 0.8) R[c] += Math.max(0, (r - g - b) * 320);
    }
  for (let c = 0; c < G * G; c++) L[c] /= n[c], R[c] /= n[c];
  return { L, R };
}
/** the flash times (frame indices) of one series: a transition is a move of `step` or more from the last extreme
 * (the darker side under darkMax); moves the same way only stretch the extreme, so transitions alternate, and every
 * second one completes a flash */
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
function measure(blinkControl) {
  const per = files.map((f, k) => cells(readPng(join(dir, f)), blinkControl && Math.floor(k / 6) % 2 === 1));
  let worstAny = 0, worstSec = 0, worstRed = 0, where = '';
  for (let by = 0; by < G - 1; by++)
    for (let bx = 0; bx < G - 1; bx++) {
      const ids = [by * G + bx, by * G + bx + 1, (by + 1) * G + bx, (by + 1) * G + bx + 1];
      const L = per.map((p) => ids.reduce((a, c) => a + p.L[c], 0) / 4), R = per.map((p) => ids.reduce((a, c) => a + p.R[c], 0) / 4);
      const fl = flashes(L, 0.1, 0.8), fr = flashes(R, 20, Infinity);
      const inSec = (list) => Math.max(0, ...list.map((t) => list.filter((u) => u >= t && u < t + 60).length));
      if (fl.length > worstAny) worstAny = fl.length, where = `block ${bx},${by}`;
      worstSec = Math.max(worstSec, inSec(fl)), worstRed = Math.max(worstRed, inSec(fr));
    }
  return { worstAny, worstSec, worstRed, where };
}
let bad = 0;
const m = measure(false);
const ok = m.worstAny === 0 && m.worstSec <= 3 && m.worstRed <= 3;
console.log(`${ok ? 'ok  ' : 'FAIL'} ${files.length} frames of a warden fight: large-area flashes ${m.worstAny}${m.where ? ` (${m.where})` : ''}, most in a second ${m.worstSec}, red ${m.worstRed}`);
if (!ok) bad++;
const c = measure(true);
if (c.worstAny > 0 && c.worstSec > 3) console.log(`ok   control fails as it must: a quarter of the screen blinking at 5 Hz: ${c.worstAny} flashes, ${c.worstSec} in a second`);
else { console.log(`CONTROL PASSED (a failure): the blink went unseen (${c.worstAny} flashes)`); bad++; }
process.exit(bad ? 1 : 0);
