// Bakes every park's lightmap (the sky's light, its occlusion, the sun's bounce) into games/skate/assets/lightmaps/
// (park<n>.cwlm and park<n>.hash, the shape it was baked for): the native skate_auto build, headless on the test GPU
// through the GPU suite lock. The bake itself runs on the CPU; the GPU only draws the few frames before it.
//   node games/skate/tools/bake.mjs
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'skate-bake', 'node', fileURLToPath(import.meta.url)], { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/skate_auto.json'), 'utf8')).binary;
const env = { ...process.env, SKATE_BAKE: join(ROOT, 'games/skate/assets/lightmaps'), SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl' };
const gl = glPin();
if (gl) Object.assign(env, gl.env);
scrubDisplay(env);
const r = spawnSync('timeout', ['-s', 'KILL', '1500', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
console.log(((r.stderr || '') + (r.stdout || '')).split('\n').filter((l) => /baked|error/i.test(l)).join('\n'));
process.exit(r.status ?? 1);
