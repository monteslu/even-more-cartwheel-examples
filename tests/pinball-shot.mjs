// pinball screenshots for review: natively, headless, on the test GPU, through the GPU suite lock.
//   node tests/pinball-shot.mjs <app> <frames> <out.png> [gl|webgpu] [KEY=VALUE ...] [-- <app> <frames> <out.png> ...]
// (several shots, " -- " between them, share one lock hold)
// CW_RENDER_SCALE=1.5 makes 1920x1080 from the app's 1280x720. Prints the run's last log lines.
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'pinball-shot', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const specs = [[]];
for (const a of process.argv.slice(2)) a === '--' ? specs.push([]) : specs.at(-1).push(a);
let worst = 0;
for (const [app, frames, out, gpu = 'gl', ...kv] of specs) {
  const exe = JSON.parse(readFileSync(join(ROOT, `build/native/apps/${app}.json`), 'utf8')).binary;
  const env = { ...process.env, CW_FRAMES: frames, CW_SCREENSHOT: resolve(out), SDL_AUDIODRIVER: 'dummy', CW_WGPU_VALIDATION: '0', CW_WARMUP_SPREAD: '0' };
  for (const p of kv) { const i = p.indexOf('='); env[p.slice(0, i)] = p.slice(i + 1); }
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '300', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  const err = (r.stderr || '') + (r.stdout || '');
  console.log(`${out}: status ${r.status}`);
  console.log(err.split('\n').filter((l) => /error|warn|missing|bench|FAIL/i.test(l)).slice(-12).join('\n'));
  if (r.status) worst = r.status;
}
process.exit(worst);
