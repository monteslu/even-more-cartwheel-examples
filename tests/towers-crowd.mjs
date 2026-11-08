// towers crowd check: a late crowd reads as bodies on every theme, not as a carpet. One map a theme at wave 18 (four
// bots, towers on every pad) is drawn twice from the same deterministic run: as it is, and with the crowd hidden
// (TOWERS_HIDE=1, the ground and everything else the same). The crowd's pixels are those the crowd changed; over them:
//   value step:  the mean relative luminance of the crowd against the ground under it, as a ratio (brighter over
//                darker): at least VALUE_MIN, so the crowd stands off its ground (dark on the pale grounds, lit on the
//                dark ones);
//   parting:     the crowd's own local contrast (each pixel's luma against its 9 x 9 neighbourhood, crowd pixels only):
//                at least PART_MIN, so the bodies part from one another (an edge round each) instead of running
//                together into one band.
// CONTROL, which must fail: the volcano with TOWERS_OLD_INK=1 (the dark ink edge on every theme, as before).
// Run: node tests/towers-crowd.mjs [webgpu]   (holds the GPU suite lock; frames in out/towers-crowd)
import { spawnSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readPng } from './towers-look.mjs';
import { shot } from './towers-shots.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'towers-crowd', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const GPU = process.argv.includes('webgpu') ? 'webgpu' : 'gl';
const MAPS = { meadow: 3, autumn: 8, desert: 13, snow: 18, volcano: 23, crystal: 28 };
const VALUE_MIN = 1.35, PART_MIN = 0.03, WAVE = 18, FRAMES = 1200;
const dir = join(ROOT, 'out', 'towers-crowd', GPU);
mkdirSync(dir, { recursive: true });

const lin = (v) => { const c = v / 255; return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4; };
const LUT = Float32Array.from({ length: 256 }, (_, i) => lin(i));
function measure(withF, without) {
  const a = readPng(withF), b = readPng(without), { w, h } = a, n = w * h;
  const Ya = new Float32Array(n), Yb = new Float32Array(n), mask = new Uint8Array(n);
  let cnt = 0, sa = 0, sb = 0;
  for (let i = 0; i < n; i++) {
    Ya[i] = 0.2126 * LUT[a.px[i * 3]] + 0.7152 * LUT[a.px[i * 3 + 1]] + 0.0722 * LUT[a.px[i * 3 + 2]];
    Yb[i] = 0.2126 * LUT[b.px[i * 3]] + 0.7152 * LUT[b.px[i * 3 + 1]] + 0.0722 * LUT[b.px[i * 3 + 2]];
    const d = Math.max(Math.abs(a.px[i * 3] - b.px[i * 3]), Math.abs(a.px[i * 3 + 1] - b.px[i * 3 + 1]), Math.abs(a.px[i * 3 + 2] - b.px[i * 3 + 2]));
    if (d > 10) mask[i] = 1, cnt++, sa += Ya[i], sb += Yb[i];
  }
  const ma = sa / cnt + 0.01, mb = sb / cnt + 0.01, value = Math.max(ma, mb) / Math.min(ma, mb);
  // the crowd's own local contrast: |Y - mean of its 9 x 9 neighbourhood| over crowd pixels (summed-area table)
  const S = new Float64Array((w + 1) * (h + 1));
  for (let y = 0; y < h; y++) for (let x = 0, row = 0; x < w; x++) { row += Ya[y * w + x]; S[(y + 1) * (w + 1) + x + 1] = S[y * (w + 1) + x + 1] + row; }
  let part = 0;
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      const i = y * w + x;
      if (!mask[i]) continue;
      const x0 = Math.max(0, x - 4), x1 = Math.min(w, x + 5), y0 = Math.max(0, y - 4), y1 = Math.min(h, y + 5);
      const m = (S[y1 * (w + 1) + x1] - S[y0 * (w + 1) + x1] - S[y1 * (w + 1) + x0] + S[y0 * (w + 1) + x0]) / ((x1 - x0) * (y1 - y0));
      part += Math.abs(Ya[i] - m);
    }
  return { share: cnt / n, value, part: part / cnt, crowd: ma, ground: mb };
}
const fmt = (m) => `crowd ${(m.share * 100).toFixed(1)}% of the screen, luminance ${m.crowd.toFixed(3)} on ${m.ground.toFixed(3)} (x${m.value.toFixed(2)}), parting ${m.part.toFixed(4)}`;
const ok = (m) => m.share > 0.01 && m.value >= VALUE_MIN && m.part >= PART_MIN;
let bad = 0;
const run = async (name, map, env = {}) => {
  const o = { map, wave: WAVE, frames: FRAMES, gpu: GPU };
  const a = await shot(join(dir, `${name}.png`), { ...o, env });
  const b = await shot(join(dir, `${name}-ground.png`), { ...o, env: { ...env, TOWERS_HIDE: '1' } });
  return measure(a, b);
};
for (const [theme, map] of Object.entries(MAPS)) {
  const m = await run(theme, map);
  if (ok(m)) console.log(`ok   ${theme} (map ${map}) ${fmt(m)}`);
  else { bad++; console.log(`FAIL ${theme} (map ${map}) ${fmt(m)} (value at least x${VALUE_MIN}, parting at least ${PART_MIN})`); }
}
const c = await run('volcano-old-ink', MAPS.volcano, { TOWERS_OLD_INK: '1' });
if (!ok(c)) console.log(`ok   control fails as it must (the volcano with the old dark ink): ${fmt(c)}`);
else { bad++; console.log(`CONTROL PASSED (a failure): the old ink went unseen: ${fmt(c)}`); }
process.exit(bad ? 1 : 0);
