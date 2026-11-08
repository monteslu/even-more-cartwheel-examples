// towers frames for the look checks: one late frame of a map, natively (towers_auto, 1920 x 1080, no HUD, no cursors).
//   node tests/towers-shots.mjs <dir> [--wave n] [--frames n] [--mask] [--hud] [--maps 3,8,13,18,23,28] [--gpu webgpu] [KEY=VALUE ...]
// (--hud: with the HUD and the cursors, for the launcher art)
// writes <dir>/m<n>.png (and with --mask, mask<n>.png: TOWERS_LOOK_MASK=1, for tests/towers-look.mjs's edge check).
// Every map is a deterministic run, so frames from two builds (or two settings) line up pixel for pixel.
// Holds the GPU suite lock (the engine's tools/with-gpu-suite.mjs).
import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER && process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'towers-shots', 'node', fileURLToPath(import.meta.url), ...argv],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}

/** one frame: the map after `frames` frames of towers_auto, with env on top */
export async function shot(out, { map, wave = 14, frames = 900, gpu = 'gl', env = {}, app = 'towers_auto', hud = false }) {
  const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
  const exe = JSON.parse(readFileSync(join(ROOT, `build/native/apps/${app}.json`), 'utf8')).binary;
  const e = { ...process.env, CW_FRAMES: String(frames), CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', CW_RENDER_SCALE: '1.5',
    TOWERS_NO_HUD: '1', TOWERS_NO_CURSORS: '1', TOWERS_MAP: String(map), TOWERS_WAVE: String(wave), TOWERS_PLAYERS: '4', CW_SCREENSHOT: out, ...env };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(e, pin.env, { CW_GPU: 'webgpu' });
  else { e.CW_GPU = 'gl'; if (gl) Object.assign(e, gl.env); }
  if (hud) delete e.TOWERS_NO_HUD, delete e.TOWERS_NO_CURSORS;
  scrubDisplay(e);
  const code = await new Promise((ok) => spawn('timeout', ['-s', 'KILL', '300', exe], { env: e, stdio: 'ignore' }).on('exit', ok));
  if (code !== 0) throw new Error(`${app} map ${map} exited ${code}`);
  return out;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const opt = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
  const dir = resolve(argv[0]);
  mkdirSync(dir, { recursive: true });
  const maps = opt('--maps', '3,8,13,18,23,28').split(',').map(Number);
  const env = {};
  for (const p of argv.filter((a) => /^[A-Z_]+=/.test(a))) env[p.slice(0, p.indexOf('='))] = p.slice(p.indexOf('=') + 1);
  const o = { wave: +opt('--wave', 14), frames: +opt('--frames', 900), gpu: opt('--gpu', 'gl'), env, hud: argv.includes('--hud') };
  for (const map of maps) {
    await shot(join(dir, `m${map}.png`), { ...o, map });
    if (argv.includes('--mask')) await shot(join(dir, `mask${map}.png`), { ...o, map, env: { ...env, TOWERS_LOOK_MASK: '1' } });
  }
  console.log(`${maps.length} maps in ${dir}`);
}
