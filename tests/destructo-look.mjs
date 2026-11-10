// destructo look gate: one frame of each setting (a tower in the highlands, a wall in a meadow, a bridge over a
// river gorge, a castle on sea cliffs, a scaffold in a quarry, a pyramid among desert mesas, a keep in a snowy pass,
// the Eiffel Tower in the city, the Sydney Opera House on the harbour),
// as the player aims, without the HUD, natively on the test GPU at 1920x1080. Each frame's luma (Rec. 709 of the sRGB
// values, 0..255) must average 95 to 125 and spread with a standard deviation of 55 or more: lit, contrasty, never
// washed out. Control, which must fail: the same frames at three times the exposure (DESTRUCTO_LOOK_CONTROL).
// The frames stay in out/look/ for review. Holds the GPU suite lock for its own runs only.
// Run: node tests/destructo-look.mjs [--no-control]
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { luma } from './lib-luma.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'destructo-look', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/destructo_auto.json'), 'utf8')).binary;
const OUT = join(ROOT, 'out/look');
mkdirSync(OUT, { recursive: true });
const KITS = [['tower', 'First Stones'], ['wall', 'Garden Wall'], ['bridge', 'Mill Bridge'], ['castle', 'The Gatehouse'], ['scaffold', 'Glass House'],
  ['pyramid', 'Step Shrine'], ['keep', 'Old Keep'], ['city', 'Eiffel Tower'], ['harbour', 'Sydney Opera House']];
function shot(level, file, extra = {}) {
  const env = { ...process.env, CW_FRAMES: '50', CW_SCREENSHOT: file, SDL_AUDIODRIVER: 'dummy', CW_RENDER_SCALE: '1.5', CW_WARMUP_SPREAD: '0',
    DESTRUCTO_LEVEL_NAME: level, DESTRUCTO_NO_HUD: '1', DESTRUCTO_QUALITY: '3', CW_GPU: 'gl', ...extra };
  const gl = glPin();
  if (gl) Object.assign(env, gl.env);
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '300', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  if (gl) gl.check('native', r.stderr || '');
  const { mean, sd } = r.status === 0 ? luma(file) : { mean: NaN, sd: NaN };
  return { status: r.status, mean, sd };
}
const ok = (r) => r.status === 0 && r.mean >= 95 && r.mean <= 125 && r.sd >= 55;
let bad = 0;
for (const [kit, level] of KITS) {
  const r = shot(level, join(OUT, `${kit}.png`));
  const line = `${kit} (${level}): luma mean ${r.mean.toFixed(1)}, deviation ${r.sd.toFixed(1)} (status ${r.status})`;
  if (ok(r)) console.log(`ok   ${line}`); else { bad++; console.log(`FAIL ${line}: mean 95..125 and deviation 55 or more`); }
}
if (!process.argv.includes('--no-control')) {
  const r = shot(KITS[0][1], join(OUT, 'control.png'), { DESTRUCTO_LOOK_CONTROL: '1' });
  const line = `control, three times the exposure: luma mean ${r.mean.toFixed(1)}, deviation ${r.sd.toFixed(1)}`;
  if (ok(r)) { bad++; console.log(`CONTROL PASSED (a failure): ${line}`); } else console.log(`ok   control fails as it must: ${line}`);
}
process.exit(bad ? 1 : 0);
