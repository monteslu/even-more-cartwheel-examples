// skate look gate: each park's frames (three, through the chase camera of a bot's run) must have contrast and not be washed
// out: its mean luma (Rec. 601, 0..255) within 95..128 and the standard deviation of its luma 55 or more (the towers
// pass's measure), with no HUD. The control raises the exposure two stops on one park: washed out, it must fail.
//   node tests/skate-look.mjs            (shots through the GPU suite lock, natively on the test GPU)
//   node tests/skate-look.mjs a.png ...  (only measures the given frames)
import { spawnSync } from 'node:child_process';
import { mkdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readImage } from '../deps/cartwheel-engine/tools/lib/png.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const MEAN = [95, 128], STD = 55;

export function measure(path) {
  const img = readImage(path);
  const { w, h, rgba: data } = img;
  let s = 0, s2 = 0, n = 0;
  for (let i = 0; i < w * h; i++) {
    const y = 0.299 * data[i * 4] + 0.587 * data[i * 4 + 1] + 0.114 * data[i * 4 + 2];
    s += y, s2 += y * y, n++;
  }
  const mean = s / n;
  return { mean, std: Math.sqrt(Math.max(0, s2 / n - mean * mean)) };
}
const pass = (m) => m.mean >= MEAN[0] && m.mean <= MEAN[1] && m.std >= STD;

const files = process.argv.slice(2);
if (files.length) {
  for (const f of files) { const m = measure(f); console.log(`${pass(m) ? 'ok  ' : 'FAIL'} ${f}: mean luma ${m.mean.toFixed(1)}, sd ${m.std.toFixed(1)}`); }
  process.exit(0);
}
const out = join(ROOT, 'out/skate-look');
mkdirSync(out, { recursive: true });
const args = [];
const FRAMES = [500, 700, 900];
for (let p = 0; p < 5; p++) for (const f of FRAMES) args.push(...(args.length ? ['--'] : []), 'skate_auto', String(f), join(out, `park${p}-${f}.png`), 'gl', `SKATE_PARK=${p}`, 'SKATE_PLAYERS=1', 'SKATE_NO_HUD=1');
args.push('--', 'skate_auto', '700', join(out, 'control.png'), 'gl', 'SKATE_PARK=0', 'SKATE_PLAYERS=1', 'SKATE_NO_HUD=1', 'SKATE_EXPOSURE=2');
const r = spawnSync('node', [join(ROOT, 'tests/skate-shot.mjs'), ...args], { encoding: 'utf8', env: { ...process.env, CW_RENDER_SCALE: '1.5' } });
if (r.status) { console.log(`FAIL the shots did not run (status ${r.status})\n${(r.stdout || '').slice(-800)}`); process.exit(1); }
let bad = 0;
for (let p = 0; p < 5; p++) {
  /* the park's three frames: their mean luma and their spread, averaged */
  const ms = FRAMES.map((f) => measure(join(out, `park${p}-${f}.png`)));
  const m = { mean: ms.reduce((a, x) => a + x.mean, 0) / ms.length, std: ms.reduce((a, x) => a + x.std, 0) / ms.length };
  if (pass(m)) console.log(`ok   park ${p}: mean luma ${m.mean.toFixed(1)} (${MEAN.join('..')}), sd ${m.std.toFixed(1)} (${STD} or more)`);
  else { console.log(`FAIL park ${p}: mean luma ${m.mean.toFixed(1)} (${MEAN.join('..')}), sd ${m.std.toFixed(1)} (${STD} or more)`); bad++; }
}
const c = measure(join(out, 'control.png'));
if (pass(c)) { console.log(`CONTROL PASSED (a failure): two stops over still measured mean ${c.mean.toFixed(1)}, sd ${c.std.toFixed(1)}`); bad++; }
else console.log(`ok   control fails as it must: two stops over: mean luma ${c.mean.toFixed(1)}, sd ${c.std.toFixed(1)}`);
if (!process.env.SKATE_KEEP) rmSync(out, { recursive: true, force: true });
process.exit(bad ? 1 : 0);
