// jewels look check: the value structure a couch needs, measured on play frames (no HUD, 1920x1080) of the bot at
// three moments, on GL and WebGPU:
//   the board is dark and the stones are light: in every cell, the mean luma of the stone (a disc round the cell's
//     centre) stands at least 55 of 255 over the median of the velvet round it (the cell's corners), in 90% of cells
//     or more, and the velvet's median stays under 40;
//   mid-key overall, never washed out: mean luma 25 to 85, standard deviation at least 38, under 2% of pixels blown
//     to 250 or more;
//   the stones keep their colour: the coloured kinds' pixels (all but the round brilliant) average a saturation of 0.35
//     or more.
// CONTROL, which must fail: the same frames flattened halfway to their own mean (a washed-out grade).
// Run: node tests/jewels-look.mjs   (renders natively: through the GPU suite lock; frames in out/jewels-look)
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readPng } from './towers-look.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const OUT = join(ROOT, 'out', 'jewels-look');
mkdirSync(OUT, { recursive: true });
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/jewels_auto.json'), 'utf8')).binary;
const CELL = 120, BX = 72, BY = 60;

// a frame where the board is at rest (the bot plays on: the first idle frame from `frames` on, ten frames apart)
function shot(frames, gpu, file) {
  for (let f = frames; f < frames + 120; f += 10) {
    const s = shot1(f, gpu, file);
    if (s.state === 0) return s;
  }
  throw new Error(`no idle frame from `);
}
function shot1(frames, gpu, file) {
  const r = spawnSync(exe, [], { encoding: 'utf8', timeout: 300000, env: { ...process.env, CW_FRAMES: String(frames), CW_GPU: gpu, SDL_AUDIODRIVER: 'dummy',
    JEWELS_NO_HUD: '1', CW_SCREENSHOT: file, CW_DEBUG_DUMP: file.replace('.png', '.json') } });
  if (r.status !== 0) throw new Error(`jewels_auto exited ${r.status}: ${(r.stderr || '').slice(-300)}`);
  const st = JSON.parse(readFileSync(file.replace('.png', '.json'), 'utf8'));
  return { img: readPng(file), grid: st['game.grid'], state: st['game.state'] };
}
const lum = (p, i) => 0.2126 * p[i] + 0.7152 * p[i + 1] + 0.0722 * p[i + 2];
const median = (a) => { const s = [...a].sort((x, y) => x - y); return s[s.length >> 1]; };

function measure({ img, grid }, flatten = 0) {
  const { w, h } = img;
  const px = Buffer.from(img.px);
  let sum = 0, sum2 = 0, blown = 0;
  for (let i = 0; i < w * h; i++) sum += lum(px, i * 3);
  const mean0 = sum / (w * h);
  if (flatten) for (let i = 0; i < px.length; i++) px[i] = Math.round(px[i] + (mean0 * (1) - px[i]) * flatten);
  sum = 0;
  for (let i = 0; i < w * h; i++) { const l = lum(px, i * 3); sum += l, sum2 += l * l, blown += l >= 250; }
  const mean = sum / (w * h), sd = Math.sqrt(sum2 / (w * h) - mean * mean);
  const sx = w / 1920, sy = h / 1080;
  const bytes = Array.isArray(grid) ? grid : String(grid).match(/../g).map((v) => parseInt(v, 16));
  let good = 0, cells = 0, satSum = 0, satN = 0;
  const velvets = [], weak = {};
  for (let cy = 0; cy < 8; cy++)
    for (let cx = 0; cx < 10; cx++) {
      const ox = BX + cx * CELL, oy = BY + cy * CELL, stone = [], velvet = [];
      for (let y = oy + 6; y < oy + CELL - 6; y += 2)
        for (let x = ox + 6; x < ox + CELL - 6; x += 2) {
          const dx = x - (ox + 60), dy = y - (oy + 60), d = Math.hypot(dx, dy);
          const i = (Math.floor(y * sy) * w + Math.floor(x * sx)) * 3, l = lum(px, i);
          if (d < 26) {
            stone.push(l);
            if (bytes[cy * 10 + cx] !== 6) {
              const r = px[i] / 255, g = px[i + 1] / 255, b = px[i + 2] / 255, mx = Math.max(r, g, b), mn = Math.min(r, g, b);
              if (mx > 0.15) satSum += (mx - mn) / mx, satN++;
            }
          } else if (Math.abs(dx) > 50 && Math.abs(dy) > 50) velvet.push(l);
        }
      const ms = stone.reduce((a, b) => a + b, 0) / stone.length, mv = median(velvet);
      velvets.push(mv);
      cells++, good += ms - mv >= 55;
      if (ms - mv < 55) weak[bytes[cy * 10 + cx]] = (weak[bytes[cy * 10 + cx]] || 0) + 1;
    }
  return { mean, sd, blown: blown / (w * h), step: good / cells, velvet: median(velvets), sat: satN ? satSum / satN : 0, weak };
}
function why(m) {
  const out = [];
  if (m.step < 0.9) out.push(`${(m.step * 100).toFixed(0)}% of cells have a stone 55 over its velvet (short, by kind: ${JSON.stringify(m.weak)})`);
  if (m.velvet >= 40) out.push(`velvet ${m.velvet.toFixed(0)}`);
  if (m.mean < 25 || m.mean > 85) out.push(`mean ${m.mean.toFixed(1)}`);
  if (m.sd < 38) out.push(`sd ${m.sd.toFixed(1)}`);
  if (m.blown > 0.02) out.push(`${(m.blown * 100).toFixed(1)}% blown`);
  if (m.sat < 0.35) out.push(`stone saturation ${m.sat.toFixed(2)}`);
  return out;
}
let bad = 0, controlsFailed = 0, controls = 0;
for (const gpu of ['gl', 'webgpu'])
  for (const f of [240, 610, 1210]) {
    const frame = shot(f, gpu, join(OUT, `look-${gpu}-${f}.png`));
    const m = measure(frame), w = why(m);
    const line = `${gpu} frame ${f}: mean ${m.mean.toFixed(1)}, sd ${m.sd.toFixed(1)}, blown ${(m.blown * 100).toFixed(2)}%, stone over velvet in ${(m.step * 100).toFixed(0)}% of cells, velvet ${m.velvet.toFixed(0)}, saturation ${m.sat.toFixed(2)}`;
    if (w.length) { bad++; console.log(`FAIL ${line}\n     ${w.join('; ')}`); } else console.log(`ok   ${line}`);
    const c = why(measure(frame, 0.5));
    controls++;
    if (c.length) controlsFailed++;
  }
if (controlsFailed === controls) console.log(`ok   control: all ${controls} frames flattened halfway to their mean fail`);
else { bad++; console.log(`CONTROL PASSED (a failure): ${controls - controlsFailed} flattened frames passed`); }
process.exit(bad ? 1 : 0);
