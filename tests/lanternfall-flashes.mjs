// lanternfall flash check: nothing in the game strobes. One second (60 consecutive frames) of each of three scenes
// that hold the game's brightest changes: every enemy struck every 10 ticks (faster than one player strikes) with
// their tells and attacks running, the wardens struck the same way, and both heroes just hurt beside a strike and a
// mend (the showcase, view.c). Each frame is cut into 48 x 48 pixel cells (far smaller than the area the flash
// guidelines count, so a creature-sized strobe is caught) and a cell's relative luminance is followed through the
// second: a flash is a pair of opposing changes of 0.1 or more where the darker state is under 0.8 (WCAG 2.3.1's
// general flash); more than 3 flashes in the second in any cell fails, and so does any saturated red flash (a red
// share over 0.8 in either state).
// CONTROL, which must fail: the same scenes with LANTERNFALL_STROBE_CONTROL (each hit a full white flash, the hurt
// hero blinking as the game once drew it).
//
//   node tests/lanternfall-flashes.mjs          captures into out/flashes (inside one hold of the GPU lock) and measures
//   node tests/lanternfall-flashes.mjs --dir d  measures frames already in d (<scene>-<frame>.png, <scene>-control-...)
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readPng } from './lanternfall-look.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const BIN = [join(ROOT, 'build/native/project/games/lanternfall'), join(ROOT, 'build/native/games/lanternfall')].find((d) => existsSync(join(d, 'lanternfall_auto')));
const argv = process.argv.slice(2);
const opt = (k) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : null);
if (!process.env.CW_GPU_SUITE_INNER && !opt('--dir')) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'lanternfall-flashes', 'node', fileURLToPath(import.meta.url), ...argv],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const FRAMES = 60, START = 120, CELL = 48;
const SCENES = { enemies: { LANTERNFALL_SHOWCASE: 'e' }, wardens: { LANTERNFALL_SHOWCASE: 'b', LANTERNFALL_SHOWCASE_STATE: '3' }, heroes: { LANTERNFALL_SHOWCASE: 'h' } };

function shot(out, vars) {
  const env = { ...process.env, CW_FRAMES: String(vars.frames), CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_RENDER_SCALE: '1', CW_SCREENSHOT: out, CW_GPU: 'gl',
    LANTERNFALL_IDLE: '1', LANTERNFALL_NO_HUD: '1', ...vars };
  delete env.frames;
  return new Promise((ok) => spawn(join(BIN, 'lanternfall_auto'), [], { env, stdio: 'ignore' }).on('exit', ok));
}

const lin = (c) => { c /= 255; return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4; };
/** per cell: relative luminance and red share of each frame */
function cells(file) {
  const { w, h, px } = readPng(file), cw = Math.floor(w / CELL), ch = Math.floor(h / CELL);
  const L = new Float32Array(cw * ch), R = new Float32Array(cw * ch);
  for (let cy = 0; cy < ch; cy++)
    for (let cx = 0; cx < cw; cx++) {
      let y = 0, rs = 0, n = 0;
      for (let j = 0; j < CELL; j += 2)
        for (let i = 0; i < CELL; i += 2) {
          const k = ((cy * CELL + j) * w + cx * CELL + i) * 3, r = lin(px[k]), g = lin(px[k + 1]), b = lin(px[k + 2]);
          y += 0.2126 * r + 0.7152 * g + 0.0722 * b, rs += r + g + b > 0.02 ? r / (r + g + b) : 0, n++;
        }
      L[cy * cw + cx] = y / n, R[cy * cw + cx] = rs / n;
    }
  return { L, R, cw, ch };
}
/** the worst cell of a sequence: its flash count and whether a red one */
export function flashes(seq) {
  let worst = { n: 0, red: false, cell: -1 };
  const N = seq[0].L.length;
  for (let c = 0; c < N; c++) {
    let ext = seq[0].L[c], extR = seq[0].R[c], dir = 0, trans = 0, red = false;
    for (let f = 1; f < seq.length; f++) {
      const v = seq[f].L[c], r = seq[f].R[c];
      if (dir > 0 && v > ext) ext = v, extR = r; /* (the extreme of the current swing) */
      else if (dir < 0 && v < ext) ext = v, extR = r;
      if (Math.abs(v - ext) >= 0.1 && Math.min(v, ext) < 0.8) {
        const d = v > ext ? 1 : -1;
        if (d !== dir) { trans++; if (Math.max(r, extR) > 0.8) red = true; dir = d; }
        ext = v, extR = r;
      }
    }
    const n = Math.floor(trans / 2);
    if (n > worst.n || (red && !worst.red)) worst = { n, red: worst.red || (red && n > 0), cell: c };
  }
  return worst;
}

let dir = opt('--dir');
if (!dir) {
  dir = join(ROOT, 'out/flashes');
  mkdirSync(dir, { recursive: true });
  const jobs = [];
  for (const [name, vars] of Object.entries(SCENES))
    for (const control of [false, true])
      for (let f = 0; f < FRAMES; f++)
        jobs.push([join(dir, `${name}${control ? '-control' : ''}-${String(f).padStart(2, '0')}.png`), { ...vars, frames: START + f, ...(control ? { LANTERNFALL_STROBE_CONTROL: '1' } : {}) }]);
  for (let i = 0; i < jobs.length; i += 4) await Promise.all(jobs.slice(i, i + 4).map(([o, v]) => shot(o, v)));
}
const files = readdirSync(dir);
let failed = 0;
for (const name of Object.keys(SCENES))
  for (const control of [false, true]) {
    const tag = `${name}${control ? '-control' : ''}`, seq = files.filter((f) => new RegExp(`^${tag}-\\d+\\.png$`).test(f)).sort().map((f) => cells(join(dir, f)));
    if (seq.length < FRAMES) { console.log(`FAIL ${tag}: ${seq.length} of ${FRAMES} frames`); failed++; continue; }
    const w = flashes(seq), bad = w.n > 3 || w.red;
    const line = `${tag}: worst cell ${w.cell} flashes ${w.n} in the second${w.red ? ' (saturated red)' : ''}`;
    if (control) { if (bad) console.log(`ok   control fails as it must: ${line}`); else { console.log(`CONTROL PASSED (a failure): ${line}`); failed++; } }
    else if (bad) { console.log(`FAIL ${line}`); failed++; } else console.log(`ok   ${line}`);
  }
process.exit(failed ? 1 : 0);
