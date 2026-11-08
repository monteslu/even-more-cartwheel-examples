// pinball flash gate: nothing strobes. Two parts.
//
// 1. Every light, over whole games (build/native/games/pinball/pinball_flashcheck, games/pinball/tools/flashcheck.c):
//    each insert, standup, drop target, pop cap, pop light, flasher and the general illumination stepped exactly as the
//    view draws them, through bot play, every mode, the wizard mode and a storm of big events, on every table. No light
//    may turn round (brighter to dimmer or back) by 10% of its range more than 3 times in any second.
//    Control: pinball_flashcheck_oldstrobe (the lights as they were: 5 Hz blinking inserts, 15 Hz flicker on the
//    flashers) must fail.
// 2. The pictures (pinball_bench with PINBALL_FLASH_STORM: Star Foundry with three balls, the wizard mode's every shot
//    pulsing, the big events fired every few frames and the pops every three) drawn frame by frame at 960 x 540 for
//    1.5 seconds and measured against the photosensitive flash rules (WCAG 2.3.1's general and red flash thresholds):
//      tiles   every 16 x 16 pixel tile's relative luminance followed through time: a turn is a move of 0.1 or more
//              from its last extreme the other way with the darker side under 0.8; no tile may turn more than 3 times
//              in any second
//      blocks  the screen cut into a 6 x 6 grid, every 2 x 2 block of it (a ninth of the screen, past the area a flash
//              must cover to count): no block may flash (two opposing turns) at all, and none may pass 3 red flashes
//              in any second (red share 0.8 or more, (R - G - B) x 320, a change of 20)
//    Controls, which must fail: pinball_bench_oldstrobe (the same scene on the lights as they were), and the new frames
//    with a quarter of the screen brightened on every other 6 frames (a 5 Hz blink of the kind a flasher washing the
//    whole table would make).
//   node tests/pinball-flash.mjs              both parts (the frames inside one hold of the GPU suite lock)
//   node tests/pinball-flash.mjs --cpu        part 1 only
//   node tests/pinball-flash.mjs --dir <d>    part 2 on f<n>.png (and old/f<n>.png) frames already in d
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const BIN = join(ROOT, 'build/native/games/pinball');
const argv = process.argv.slice(2);
const cpuOnly = argv.includes('--cpu');
const dirArg = argv.includes('--dir') ? argv[argv.indexOf('--dir') + 1] : null;
const FIRST = 300, COUNT = 90;
let bad = 0;

if (!dirArg) { /* part 1 */
  const r = spawnSync(join(BIN, 'pinball_flashcheck'), ['120'], { encoding: 'utf8', timeout: 300000 });
  const sum = JSON.parse((r.stdout || '{}').trim().split('\n').pop() || '{}');
  for (const l of (r.stdout || '').split('\n').filter((l) => /^FAIL/.test(l))) console.log(l);
  if (r.status !== 0 || sum.fails !== 0) bad++, console.log(`FAIL lights: ${sum.fails} table phases strobe (exit ${r.status})`);
  else console.log(`ok   lights: ${sum.frames} frames of play, modes, wizard modes and event storms on every table; most turns of one light in a second: inserts ${sum.inserts}, targets and caps ${sum['targets and caps']}, pop lights ${sum['pop lights']}, flashers ${sum.flashers}, general light ${sum['general light']} (bound 3)`);
  const c = spawnSync(join(BIN, 'pinball_flashcheck_oldstrobe'), ['120'], { encoding: 'utf8', timeout: 300000 });
  const cs = JSON.parse((c.stdout || '{}').trim().split('\n').pop() || '{}');
  if (c.status !== 0 && cs.fails > 0) console.log(`ok   control (the old lights) fails as it must: ${cs.fails} table phases, inserts ${cs.inserts}, flashers ${cs.flashers} turns in a second`);
  else bad++, console.log(`CONTROL PASSED (a failure): the old blinking went unseen (exit ${c.status})`);
  if (cpuOnly) process.exit(bad ? 1 : 0);
}

if (!dirArg && !process.env.CW_GPU_SUITE_INNER) { /* the captures run inside one hold of the lock */
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'pinball-flash', 'node', fileURLToPath(import.meta.url), ...argv],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status === 0 && !bad ? 0 : 1);
}
const dir = dirArg || join(ROOT, 'out/flash-pinball');
if (!dirArg) {
  const { glPin, scrubDisplay } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/wgpu-pin.mjs'));
  for (const [app, sub] of [['pinball_bench', ''], ['pinball_bench_oldstrobe', 'old']]) {
    const out = join(dir, sub);
    mkdirSync(out, { recursive: true });
    const env0 = { ...process.env, CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_RENDER_SCALE: '0.75', PINBALL_FLASH_STORM: '1', CW_GPU: 'gl' };
    const gl = glPin();
    if (gl) Object.assign(env0, gl.env);
    scrubDisplay(env0);
    const one = (f) => new Promise((ok) => spawn(join(BIN, app), [], { stdio: 'ignore', env: { ...env0, CW_FRAMES: String(f), CW_SCREENSHOT: join(out, `f${f}.png`) } }).on('exit', ok));
    for (let f = FIRST; f < FIRST + COUNT; f += 2) await Promise.all([one(f), one(f + 1)]);
  }
}
const { readImage } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const LIN = Float32Array.from({ length: 256 }, (_, i) => { const v = i / 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; });
const frames = (d) => existsSync(d) ? readdirSync(d).filter((n) => /^f\d+\.png$/.test(n)).sort((a, b) => +a.slice(1, -4) - +b.slice(1, -4)).map((n) => join(d, n)) : [];
const G = 6, TS = 16;

/** turns of a series: a move of `step` or more from the last extreme (the darker side under darkMax); moves the same
 * way only stretch the extreme, so turns alternate */
function turns(series, step, darkMax) {
  const out = [];
  let ext = series[0], dir = 0;
  for (let i = 1; i < series.length; i++) {
    const v = series[i];
    if ((dir === 1 && v > ext) || (dir === -1 && v < ext)) { ext = v; continue; }
    if (Math.abs(v - ext) >= step && Math.min(v, ext) < darkMax) dir = v > ext ? 1 : -1, ext = v, out.push(i);
  }
  return out;
}
const inSec = (list) => Math.max(0, ...list.map((t) => list.filter((u) => u >= t && u < t + 60).length));

function measure(files, blink) {
  let tw = 0, th = 0, tiles = null, blocks = [], reds = [];
  files.forEach((f, k) => {
    const { w, h, rgba } = readImage(f);
    const on = blink && Math.floor(k / 6) % 2 === 1;
    if (!tiles) { tw = Math.floor(w / TS), th = Math.floor(h / TS); tiles = Array.from({ length: tw * th }, () => []); }
    const T = new Float64Array(tw * th), L = new Float64Array(G * G), R = new Float64Array(G * G), n = new Float64Array(G * G);
    for (let y = 0; y < th * TS; y++)
      for (let x = 0; x < tw * TS; x++) {
        const i = (y * w + x) * 4;
        let r = LIN[rgba[i]], g = LIN[rgba[i + 1]], b = LIN[rgba[i + 2]];
        if (on && x < w / 2 && y < h / 2) r = Math.min(1, r + 0.35), g = Math.min(1, g + 0.35), b = Math.min(1, b + 0.35);
        const l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        T[Math.floor(y / TS) * tw + Math.floor(x / TS)] += l / (TS * TS);
        const c = Math.min(G - 1, Math.floor(y * G / h)) * G + Math.min(G - 1, Math.floor(x * G / w));
        L[c] += l, n[c]++;
        const s = r + g + b;
        if (s > 0 && r / s >= 0.8) R[c] += Math.max(0, (r - g - b) * 320);
      }
    for (let t = 0; t < tw * th; t++) tiles[t].push(T[t]);
    for (let c = 0; c < G * G; c++) L[c] /= n[c], R[c] /= n[c];
    blocks.push(L), reds.push(R);
  });
  let worstTile = 0, where = '';
  tiles.forEach((s, t) => { const v = inSec(turns(s, 0.1, 0.8)); if (v > worstTile) worstTile = v, where = `tile ${t % tw},${Math.floor(t / tw)}`; });
  let flashes = 0, worstRed = 0;
  for (let by = 0; by < G - 1; by++)
    for (let bx = 0; bx < G - 1; bx++) {
      const ids = [by * G + bx, by * G + bx + 1, (by + 1) * G + bx, (by + 1) * G + bx + 1];
      const L = blocks.map((p) => ids.reduce((a, c) => a + p[c], 0) / 4), R = reds.map((p) => ids.reduce((a, c) => a + p[c], 0) / 4);
      flashes = Math.max(flashes, Math.floor(turns(L, 0.1, 0.8).length / 2));
      worstRed = Math.max(worstRed, Math.floor(inSec(turns(R, 20, Infinity)) / 2));
    }
  return { worstTile, where, flashes, worstRed, n: files.length };
}
const now = frames(dir), old = frames(join(dir, 'old'));
if (now.length < 60) { console.log(`FAIL only ${now.length} frames in ${dir}`); process.exit(1); }
const m = measure(now, false);
const ok = m.worstTile <= 3 && m.flashes === 0 && m.worstRed <= 3;
console.log(`${ok ? 'ok  ' : 'FAIL'} frames: ${m.n} frames of a wizard mode in an event storm: most turns of a 16 px tile in a second ${m.worstTile}${m.where ? ` (${m.where})` : ''} (bound 3), large-area flashes ${m.flashes} (bound 0), red flashes in a second ${m.worstRed} (bound 3)`);
if (!ok) bad++;
if (old.length >= 60) {
  const o = measure(old, false);
  if (o.worstTile > 3 || o.flashes > 0) console.log(`ok   control (the old lights) fails as it must: ${o.worstTile} turns of a tile in a second (${o.where}), ${o.flashes} large-area flashes`);
  else bad++, console.log(`CONTROL PASSED (a failure): the old lights' blinking went unseen (${o.worstTile} turns a second)`);
} else bad++, console.log(`FAIL no frames of the old lights in ${join(dir, 'old')}`);
const c = measure(now, true);
if (c.flashes > 0 && c.worstTile > 3) console.log(`ok   control fails as it must: a quarter of the screen blinking at 5 Hz: ${c.flashes} large-area flashes, ${c.worstTile} turns of a tile in a second`);
else bad++, console.log(`CONTROL PASSED (a failure): the blink went unseen (${c.flashes} flashes)`);
process.exit(bad ? 1 : 0);
