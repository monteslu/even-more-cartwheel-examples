// destructo flash check: no strobing. The game's biggest blast (destructo_bench: Great Keep, whose first charge
// breaks about 800 blocks, with its fire, dust, sparks, shards, crystal bursts and lights, the HUD on) is captured
// every frame from half a second before the charge goes off to two and a half seconds after it, and measured by the
// engine's flash check (deps/cartwheel-engine/tools/lib/flash.mjs, through tests/lib/flash-run.mjs: WCAG 2.3.1's
// general and red flashes over a sixth of the screen, small lights switching together over a quarter of a 10-degree
// field, regular flicker from 3.5 Hz; the hardest two-frame rise is reported).
// CONTROLS, which must fail: the same frames under the engine's 8 Hz full-screen strobe, and the same blast with a
// white wash over the screen at 10 Hz from the game (DESTRUCTO_STROBE_CONTROL).
//   node tests/destructo-flash.mjs        (inside one hold of the GPU suite lock)
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { inSuiteLock, appBinary, captureRun, gate, mustFail } from './lib/flash-run.mjs';

inSuiteLock('destructo-flash', fileURLToPath(import.meta.url));
const BEFORE = 30, AFTER = 150;
const exe = appBinary('destructo_bench');
const env = { DESTRUCTO_QUALITY: '3' };

/* where the charge goes off */
const { glPin, scrubDisplay } = await import('../deps/cartwheel-engine/tools/lib/wgpu-pin.mjs');
const penv = { ...process.env, CW_FRAMES: '1500', CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl', DESTRUCTO_FX_LOG: '1', ...env };
const gl = glPin();
if (gl) Object.assign(penv, gl.env);
scrubDisplay(penv);
const probe = spawnSync(exe, [], { env: penv, encoding: 'utf8', maxBuffer: 1 << 26, timeout: 600000 });
const m = /destructo boom frame=(\d+)/.exec(probe.stderr || '');
if (!m) { console.log(`FAIL the bench fired no charge in 1500 frames (exit ${probe.status})`); process.exit(1); }
const boom = +m[1];
console.log(`the charge goes off on frame ${boom}`);

let bad = 0;
const span = { exe, from: boom - BEFORE, frames: boom + AFTER, env };
bad += gate(`Great Keep's blast, frames ${span.from}-${span.frames}`, await captureRun({ ...span, name: 'destructo' })) ? 0 : 1;
bad += mustFail('the blast under the game\'s 10 Hz white wash, DESTRUCTO_STROBE_CONTROL',
  await captureRun({ ...span, name: 'destructo-strobe', env: { ...env, DESTRUCTO_STROBE_CONTROL: '1' } })) ? 0 : 1;
process.exit(bad ? 1 : 0);
