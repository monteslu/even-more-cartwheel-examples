// jewels effects gate: the effects grow with the size of a clear and with each step of a cascade, and the picture
// shows it. jewels_bench (refills that run long cascades of big clears, every move) logs each clear (JEWELS_FX_LOG):
// its cascade step, cells, shape (crossings and long runs) and the power the effects take from them, with the
// sparks, shards, ring, streak and rumble that power sets.
//   state: for clears of the same size and shape, power rises with each cascade step; for the same step and shape,
//          it rises with the cells; ring, streak and rumble follow; a cascade of four steps or more is seen;
//   frames: the light a clear adds round where it cleared (luma 12 frames after it, less the frame before it, within
//          300 px of its centre, per cell cleared), the strongest clear of the run over a plain three on the first step,
//          must be at least 1.3 times the same ratio in a run whose clears all draw like a plain three (the effects'
//          own growth, not just more of them: the same clears happen in both runs, the effects never touch the board).
// CONTROL, which must fail: the same run with every clear's effects the same (JEWELS_FX_FLAT=1).
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
  return [...(r.stderr || '').matchAll(/fx clear frame=(\d+) step=(\d+) cells=(\d+) shape=(\d+) power=([\d.]+) sparks=(\d+) shards=(\d+) ring=(\d) trail=(\d) rumble=([\d.]+) x=(\d+) y=(\d+)/g)]
    .map((m) => ({ frame: +m[1], step: +m[2], cells: +m[3], shape: +m[4], power: +m[5], sparks: +m[6], shards: +m[7], ring: +m[8], trail: +m[9], rumble: +m[10], x: +m[11], y: +m[12] }));
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
  console.log(`     ${label}: ${clears.length} clears, deepest step ${deepest}, power ${Math.min(...clears.map((c) => c.power)).toFixed(2)} to ` +
    `${Math.max(...clears.map((c) => c.power)).toFixed(2)}; light added a cell: plain three ${es.toFixed(0)}, step ${big.step} with ${big.cells} cells ${eb.toFixed(0)} (x${(eb / es).toFixed(2)})`);
  return { why, ratio: eb / Math.max(es, 1) };
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
const real = measure('effects', {}), flat = measure('flat', { JEWELS_FX_FLAT: '1' });
const w1 = judge(real, flat);
if (w1.length) { bad++; console.log(`FAIL effects: ${w1.join('; ')}`); } else console.log(`ok   the effects grow with the clear and the cascade (x${(real.ratio / flat.ratio).toFixed(2)} over flat)`);
const w2 = judge(flat, flat);
if (w2.length) console.log(`ok   control fails as it must (flat effects): ${w2.join('; ')}`);
else { bad++; console.log('CONTROL PASSED (a failure): flat effects passed'); }
process.exit(bad ? 1 : 0);
