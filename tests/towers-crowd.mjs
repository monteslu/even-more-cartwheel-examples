// towers crowd check: a late crowd reads as bodies on every theme, not as a carpet. One late map a theme (the perf
// gate's: 5, 10, 15, 20, 25, 30) with a late wave's crowd held on its paths (TOWERS_CROWD=1500, towers on every pad,
// four bots) is drawn twice from the same deterministic run: as it is, and with the crowd hidden
// (TOWERS_HIDE=1, the ground and everything else the same). The crowd's pixels are those the crowd changed; over them:
//   value step:  the mean relative luminance of the crowd against the ground under it, as a ratio (brighter over
//                darker): at least VALUE_MIN, so the crowd stands off its ground;
//   the way:     on a dark path (its luminance under DARK_PATH, the snowfields' frozen earth) the crowd must be the
//                lighter of the two: a dark-inked crowd on a dark path reads as one dark band;
//   parting:     the crowd's own local contrast (each pixel's luma against its 9 x 9 neighbourhood, crowd pixels
//                only), reported: how far the bodies part from one another.
// The volcano's own late wave (map 23, wave 18 as the waves bring it: a column of crimson ogres on the pale road, no
// crowd held) is judged as figures, not one red carpet: its parting at least PART_MIN (the old crowd's was 0.044), and
// its value step at least WAVE_VALUE_MIN (one kind's mid crimson on a mid road: the rim that parts the bodies brings
// the mass a little toward the road, so this scene asks the value step of a column, not of a carpet's).
// CONTROLS, which must fail: the snowfields with TOWERS_OLD_INK=1 and TOWERS_OLD_CROWD=1 (the old thin dark ink edge on
// their dark path, no rim), and the volcano's wave with TOWERS_OLD_CROWD=1 (the crowd as it was: no rim, no size or shade of its own, not drawn apart).
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
const MAPS = { meadow: 5, autumn: 10, desert: 15, snow: 20, volcano: 25, crystal: 30 };
const VALUE_MIN = 1.35, DARK_PATH = 0.14, WAVE = 18, FRAMES = 600, CROWD = { TOWERS_CROWD: '1500' }, PART_MIN = 0.058, WAVE_VALUE_MIN = 1.15;
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
const ok = (m) => m.share > 0.01 && m.value >= VALUE_MIN && (m.ground >= DARK_PATH || m.crowd > m.ground);
let bad = 0;
const run = async (name, map, env = {}) => {
  const o = { map, wave: WAVE, frames: FRAMES, gpu: GPU };
  env = { ...CROWD, ...env };
  const a = await shot(join(dir, `${name}.png`), { ...o, env });
  const b = await shot(join(dir, `${name}-ground.png`), { ...o, env: { ...env, TOWERS_HIDE: '1' } });
  return measure(a, b);
};
for (const [theme, map] of Object.entries(MAPS)) {
  const m = await run(theme, map);
  if (ok(m)) console.log(`ok   ${theme} (map ${map}) ${fmt(m)}`);
  else { bad++; console.log(`FAIL ${theme} (map ${map}) ${fmt(m)} (value at least x${VALUE_MIN}, and lighter than a path darker than ${DARK_PATH})`); }
}
{ /* the volcano's ogres: figures, not a carpet */
  const own = async (name, env) => {
    const o = { map: 23, wave: WAVE, frames: FRAMES, gpu: GPU };
    return measure(await shot(join(dir, `${name}.png`), { ...o, env }), await shot(join(dir, `${name}-ground.png`), { ...o, env: { ...env, TOWERS_HIDE: '1' } }));
  };
  const wave = (x) => x.share > 0.002 && x.value >= WAVE_VALUE_MIN && x.part >= PART_MIN;
  const m = await own('volcano-wave', {}), okv = wave(m);
  console.log(`${okv ? 'ok  ' : 'FAIL'} the volcano's own wave 18 (map 23) ${fmt(m)} (parting at least ${PART_MIN}, value at least x${WAVE_VALUE_MIN})`);
  if (!okv) bad++;
  const o = await own('volcano-wave-old', { TOWERS_OLD_CROWD: '1' });
  if (!wave(o)) console.log(`ok   control fails as it must (the volcano's wave, TOWERS_OLD_CROWD): ${fmt(o)}`);
  else { bad++; console.log(`CONTROL PASSED (a failure): the old crowd parts as well: ${fmt(o)}`); }
}
const c = await run('snow-old-ink', MAPS.snow, { TOWERS_OLD_INK: '1', TOWERS_OLD_CROWD: '1' }); // (the old crowd too: the rim alone lifts it off the dark path)
if (!ok(c)) console.log(`ok   control fails as it must (the snowfields with the old dark ink): ${fmt(c)}`);
else { bad++; console.log(`CONTROL PASSED (a failure): the old ink went unseen: ${fmt(c)}`); }
process.exit(bad ? 1 : 0);
