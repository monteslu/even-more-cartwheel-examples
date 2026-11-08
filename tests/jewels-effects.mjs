// jewels effects gate: the effects grow with the size of a clear and with each step of a cascade, and the picture
// shows it. jewels_bench (refills that run long cascades of big clears, every move) logs each clear (JEWELS_FX_LOG):
// its cascade step, cells, shape (crossings and long runs) and the power the effects take from them, with the
// sparks, shards, ring, streak and rumble that power sets.
//   state: for clears of the same size and shape, power rises with each cascade step; for the same step and shape,
//          it rises with the cells; ring, streak and rumble follow; a cascade of four steps or more is seen;
//   frames: the light a clear adds round where it cleared (luma 12 frames after it, less the frame before it, within
//          300 px of its centre, per cell cleared), the strongest clear of the run over a plain three on the first step,
//          must be at least 1.3 times the same ratio in a run whose clears all draw like a plain three (the effects'
//          own growth, not just more of them: the same clears happen in both runs, the effects never touch the board);
//   light: the clears light the scene round them. For the strongest clear and a plain three, 8 ticks after the clear
//          (its lights at their peak) against the tick before it: the median rise in luma over the stones of the cells
//          next to the cleared ones (a disc round each centre; sparks and shards cover a small part of it, so the
//          median is the light, not them), and over the velvet of those cells (round the buttons where four cells
//          meet). The strongest clear must lift its neighbouring stones by at least NEAR_STONE and their velvet by at
//          least NEAR_VELVET levels of 255, and lift its neighbouring stones more than the plain three does.
// CONTROLS, which must fail: the same run with every clear's effects the same (JEWELS_FX_FLAT=1), and the same run
// with no light from the clears (JEWELS_FX_NOLIGHT=1: everything else the same; the light test must fail).
// Run: node tests/jewels-effects.mjs   (the GPU: through the GPU suite lock)
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readPng } from './towers-look.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const OUT = join(ROOT, 'out', 'jewels-effects');
mkdirSync(OUT, { recursive: true });
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/jewels_bench.json'), 'utf8')).binary;

function run(frames, env = {}, shot = null) {
  const e = { ...process.env, CW_FRAMES: String(frames), SDL_AUDIODRIVER: 'dummy', CW_GPU: process.env.CW_GPU || 'gl', JEWELS_FX_LOG: '1', JEWELS_NO_HUD: '1', ...env };
  if (shot) e.CW_SCREENSHOT = shot;
  const r = spawnSync(exe, [], { env: e, encoding: 'utf8', maxBuffer: 1 << 26, timeout: 300000 });
  if (r.status !== 0) throw new Error(`jewels_bench exited ${r.status}: ${(r.stderr || '').slice(-400)}`);
  return [...(r.stderr || '').matchAll(/fx clear frame=(\d+) step=(\d+) cells=(\d+) shape=(\d+) power=([\d.]+) sparks=(\d+) shards=(\d+) ring=(\d) trail=(\d) rumble=([\d.]+) x=(\d+) y=(\d+) hit=([01]+)/g)]
    .map((m) => ({ frame: +m[1], step: +m[2], cells: +m[3], shape: +m[4], power: +m[5], sparks: +m[6], shards: +m[7], ring: +m[8], trail: +m[9], rumble: +m[10], x: +m[11], y: +m[12], hit: m[13] }));
}

/** the light added round (x, y): luma after less before, summed where positive, within 300 px */
function added(before, after, x, y) {
  const a = readPng(before), b = readPng(after);
  const sx = a.w / 1920, sy = a.h / 1080;
  let sum = 0;
  for (let py = Math.max(0, Math.floor((y - 300) * sy)); py < Math.min(a.h, (y + 300) * sy); py++)
    for (let px = Math.max(0, Math.floor((x - 300) * sx)); px < Math.min(a.w, (x + 300) * sx); px++) {
      if ((px / sx - x) ** 2 + (py / sy - y) ** 2 > 300 * 300) continue;
      const i = (py * a.w + px) * 3;
      const la = 0.2126 * a.px[i] + 0.7152 * a.px[i + 1] + 0.0722 * a.px[i + 2], lb = 0.2126 * b.px[i] + 0.7152 * b.px[i + 1] + 0.0722 * b.px[i + 2];
      if (lb > la) sum += lb - la;
    }
  return sum / (sx * sy);
}

const NEAR_STONE = 8, NEAR_VELVET = 12;
/** the light a clear adds to the stones and the velvet of the cells next to it: the median luma rise, after over before */
function nearLight(before, after, hit) {
  const a = readPng(before), b = readPng(after);
  const sx = a.w / 1920, sy = a.h / 1080;
  const lum = (img, x, y) => { const i = (Math.floor(y * sy) * img.w + Math.floor(x * sx)) * 3; return 0.2126 * img.px[i] + 0.7152 * img.px[i + 1] + 0.0722 * img.px[i + 2]; };
  const cleared = (cx, cy) => cx >= 0 && cy >= 0 && cx < 10 && cy < 8 && hit[cy * 10 + cx] === '1';
  const stone = [], velvet = [];
  for (let cy = 0; cy < 8; cy++)
    for (let cx = 0; cx < 10; cx++) {
      if (cleared(cx, cy)) continue;
      let near = false;
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) near ||= cleared(cx + dx, cy + dy);
      if (!near) continue;
      const ox = 72 + cx * 120, oy = 60 + cy * 120;
      for (let y = oy + 4; y < oy + 116; y += 2)
        for (let x = ox + 4; x < ox + 116; x += 2) {
          const dx = x - (ox + 60), dy = y - (oy + 60), r = Math.hypot(dx, dy);
          const d = lum(b, x, y) - lum(a, x, y);
          if (r < 28) stone.push(d);
          else if (Math.abs(dx) > 50 && Math.abs(dy) > 50) velvet.push(d);
        }
    }
  const med = (v) => { const s = [...v].sort((p, q) => p - q); return s.length ? s[s.length >> 1] : 0; };
  return { stone: med(stone), velvet: med(velvet), n: stone.length };
}

function measure(label, env) {
  const why = [];
  const clears = run(1500, env);
  if (clears.length < 10) return { why: [`only ${clears.length} clears`] };
  let stepPairs = 0, stepRise = 0, cellPairs = 0, cellRise = 0;
  for (const a of clears)
    for (const b of clears) {
      if (a.cells === b.cells && a.shape === b.shape && b.step === a.step + 1) stepPairs++, stepRise += b.power > a.power;
      if (a.step === b.step && b.shape >= a.shape && b.cells > a.cells) cellPairs++, cellRise += b.power > a.power;
    }
  if (stepPairs < 3 || stepRise < stepPairs) why.push(`power rose with the cascade step in ${stepRise} of ${stepPairs} pairs`);
  if (cellPairs < 2 || cellRise < cellPairs) why.push(`power rose with the cells in ${cellRise} of ${cellPairs} pairs`);
  const deepest = Math.max(...clears.map((c) => c.step));
  if (deepest < 4) why.push(`deepest cascade ${deepest}`);
  if (clears.some((c) => c.step >= 2 && !c.trail)) why.push('a cascade step without its streak');
  /* the frames: a plain first-step three, and the clear the real run finds strongest (the same clear in every run:
   * the effects never touch the board) */
  const small = clears.find((c) => c.step === 1 && c.cells === 3 && c.shape === 0);
  if (!small) return { why: [...why, 'no plain three on a first step'] };
  const big = clears[strongest ?? (strongest = clears.reduce((m, c, i) => (c.power > clears[m].power ? i : m), 0))];
  const shot = (f, tag) => { const p = join(OUT, `${label}-${tag}.png`); run(f, env, p); return p; };
  const es = added(shot(small.frame - 1, 's0'), shot(small.frame + 12, 's1'), small.x, small.y) / small.cells;
  const eb = added(shot(big.frame - 1, 'b0'), shot(big.frame + 12, 'b1'), big.x, big.y) / big.cells;
  const lb = nearLight(join(OUT, `${label}-b0.png`), shot(big.frame + 8, 'b8'), big.hit);
  const ls = nearLight(join(OUT, `${label}-s0.png`), shot(small.frame + 8, 's8'), small.hit);
  console.log(`     ${label}: light 8 ticks after the strongest clear: its neighbouring stones +${lb.stone.toFixed(1)}, their velvet +${lb.velvet.toFixed(1)}; ` +
    `after a plain three: stones +${ls.stone.toFixed(1)}, velvet +${ls.velvet.toFixed(1)} (median luma rise, of 255)`);
  const light = [];
  if (lb.stone < NEAR_STONE) light.push(`the strongest clear lifts its neighbouring stones by ${lb.stone.toFixed(1)} (at least ${NEAR_STONE})`);
  if (lb.velvet < NEAR_VELVET) light.push(`the strongest clear lifts the velvet round it by ${lb.velvet.toFixed(1)} (at least ${NEAR_VELVET})`);
  if (!(lb.stone > ls.stone)) light.push(`the strongest clear lights its neighbouring stones no more than a plain three does (${lb.stone.toFixed(1)} against ${ls.stone.toFixed(1)})`);
  console.log(`     ${label}: ${clears.length} clears, deepest step ${deepest}, power ${Math.min(...clears.map((c) => c.power)).toFixed(2)} to ` +
    `${Math.max(...clears.map((c) => c.power)).toFixed(2)}; light added a cell: plain three ${es.toFixed(0)}, step ${big.step} with ${big.cells} cells ${eb.toFixed(0)} (x${(eb / es).toFixed(2)})`);
  return { why, ratio: eb / Math.max(es, 1), light };
}
let strongest = null;

/* the candidate's big clear must add, per cell, at least 1.3 times what the same clear adds when every clear draws
 * like a plain three (the effects' own growth, not just more of them) */
function judge(cand, base) {
  const why = [...cand.why];
  if (cand.ratio === undefined || base.ratio === undefined) return why.length ? why : ['no frames'];
  const k = cand.ratio / base.ratio;
  if (k < 1.3) why.push(`the strongest clear's light a cell over a plain three's is x${k.toFixed(2)} of the same with flat effects (at least x1.3)`);
  return why;
}

let bad = 0;
const real = measure('effects', {}), flat = measure('flat', { JEWELS_FX_FLAT: '1' }), dark = measure('nolight', { JEWELS_FX_NOLIGHT: '1' });
const w1 = judge(real, flat);
if (w1.length) { bad++; console.log(`FAIL effects: ${w1.join('; ')}`); } else console.log(`ok   the effects grow with the clear and the cascade (x${(real.ratio / flat.ratio).toFixed(2)} over flat)`);
if (!real.light || real.light.length) { bad++; console.log(`FAIL light: ${(real.light || ['no frames']).join('; ')}`); }
else console.log('ok   the clears light their neighbouring stones and the velvet round them');
const w2 = judge(flat, flat);
if (w2.length) console.log(`ok   control fails as it must (flat effects): ${w2.join('; ')}`);
else { bad++; console.log('CONTROL PASSED (a failure): flat effects passed'); }
if (dark.light?.length) console.log(`ok   control fails as it must (no light from the clears): ${dark.light.join('; ')}`);
else { bad++; console.log('CONTROL PASSED (a failure): with no light from the clears the light test passed'); }
process.exit(bad ? 1 : 0);
