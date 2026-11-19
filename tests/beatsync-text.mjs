// beatsync's text size, on romdev: the engine's check (tools/check-text.mjs) over every screen of
// tests/beatsync-text.json (title, songs, difficulty, calibration, count-in, play, results, pause, settings, graphics),
// each at least the engine's floor. The CONTROL must fail: the same screens at half the text size.
//   node tests/beatsync-text.mjs [--gpu gl|webgpu|both]
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { gpusOf } from './lib/beatsync.mjs';

const tool = join(ROOT, 'deps/cartwheel-engine/tools/check-text.mjs');
const env = { ...process.env, CW_PROJECT_ROOT: ROOT };
let failed = 0;
for (const gpu of gpusOf(process.argv.slice(2))) {
  const r = spawnSync('node', [tool, 'beatsync-text.json', '--gpu', gpu], { cwd: ROOT, env, stdio: 'inherit' });
  if (r.status !== 0) failed++;
}
const c = spawnSync('node', [tool, 'beatsync-text.json', '--gpu', 'gl', '--scales', '0.5'], { cwd: ROOT, env, encoding: 'utf8' });
if (c.status === 0) { console.log('FAIL text control: the screens at half text size passed: the check cannot see small text'); failed++; }
else console.log('ok   text control: the screens at half text size fail, as they must');
console.log(failed ? `beatsync-text: ${failed} failed` : 'beatsync-text: ok');
process.exit(failed ? 1 : 0);
