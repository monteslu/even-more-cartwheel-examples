// emberthrone flash check: nothing strobes. Two runs are captured every frame and measured by the engine's flash
// check (deps/cartwheel-engine/tools/lib/flash.mjs, through tests/lib/flash-run.mjs: WCAG 2.3.1's general and red
// flashes over a sixth of the screen, small lights switching together over a quarter of a 10-degree field, regular
// flicker from 3.5 Hz in any twelfth by twelfth of the screen, and a run that never moves proves nothing):
//   - eight seconds of emberthrone_auto (the bot on its journey through the first region, the camera following);
//   - a boss fight (emberthrone_bench: the Tidecaller in its temple court, its tells, rings and shockwaves, the blade
//     ribbons, sparks, impact lights, staggers and the phase's roar with its hold, the HUD on), from half a second
//     before its busiest moment (the first phase roar, else the first critical or stagger) to three seconds after.
// CONTROLS, which must fail: each run under the engine's 8 Hz full-screen strobe, and the boss fight with the game's
// own white wash over the screen at 10 Hz (ADV_STROBE_CONTROL).
//   node tests/emberthrone-flash.mjs        (inside one hold of the GPU suite lock)
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { inSuiteLock, appBinary, captureRun, gate, mustFail } from './lib/flash-run.mjs';

inSuiteLock('emberthrone-flash', fileURLToPath(import.meta.url));
let bad = 0;

const FROM = 600, FRAMES = 1079;
bad += gate(`emberthrone_auto's journey, frames ${FROM}-${FRAMES}`, await captureRun({ app: 'emberthrone_auto', from: FROM, frames: FRAMES, name: 'emberthrone_auto' })) ? 0 : 1;

/* the boss fight's busiest moment */
const exe = appBinary('emberthrone_bench');
const env = { ADV_QUALITY: '3' };
const { glPin, scrubDisplay } = await import('../deps/cartwheel-engine/tools/lib/wgpu-pin.mjs');
const penv = { ...process.env, CW_FRAMES: '2400', CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl', ADV_FX_LOG: '1', ...env };
const gl = glPin();
if (gl) Object.assign(penv, gl.env);
scrubDisplay(penv);
const probe = spawnSync(exe, [], { env: penv, encoding: 'utf8', maxBuffer: 1 << 26, timeout: 900000 });
const ev = [...(probe.stderr || '').matchAll(/adv-fx frame=(\d+) tick=\d+ kind=(\d+)/g)].map((m) => ({ frame: +m[1], kind: +m[2] }));
const ROAR = 10, CRIT = 22, STAGGER = 23;
const pick = ev.find((e) => e.kind === ROAR) || ev.find((e) => e.kind === CRIT) || ev.find((e) => e.kind === STAGGER);
if (!pick) { console.log(`FAIL the bench showed no roar, critical or stagger in 2400 frames (exit ${probe.status}, ${ev.length} blows)`); process.exit(1); }
const span = { exe, from: Math.max(1, pick.frame - 30), frames: pick.frame + 180, env };
console.log(`the fight's ${pick.kind === ROAR ? 'phase roar' : pick.kind === CRIT ? 'first critical' : 'first stagger'} shows on frame ${pick.frame}; ${ev.filter((e) => e.frame >= span.from && e.frame <= span.frames).length} blows and stuns in the run`);
bad += gate(`the Tidecaller's fight, frames ${span.from}-${span.frames}`, await captureRun({ ...span, name: 'emberthrone-boss' })) ? 0 : 1;
bad += mustFail('the fight under the game\'s 10 Hz white wash, ADV_STROBE_CONTROL',
  await captureRun({ ...span, name: 'emberthrone-boss-strobe', env: { ...env, ADV_STROBE_CONTROL: '1' } })) ? 0 : 1;
process.exit(bad ? 1 : 0);
