// pinball flash gate: nothing strobes. Two parts.
//
// 1. Every light, over whole games (build/native/games/pinball/pinball_flashcheck, games/pinball/tools/flashcheck.c):
//    each insert, standup, drop target, pop cap, pop light, flasher and the general illumination stepped exactly as the
//    view draws them, through bot play, every mode, the wizard mode and a storm of big events, on every table. No light
//    may turn round (brighter to dimmer or back) by 10% of its range more than 3 times in any second.
//    Control: pinball_flashcheck_oldstrobe (the lights as they were: 5 Hz blinking inserts, 15 Hz flicker on the
//    flashers) must fail.
// 2. The pictures (pinball_bench with PINBALL_FLASH_STORM: Star Foundry with three balls, the wizard mode's every shot
//    pulsing, the big events fired every few frames and the pops every three) captured every frame at 960 x 540 (kept at 480 x 270) for
//    three seconds and measured by the engine's flash check (deps/cartwheel-engine/tools/lib/flash.mjs, through
//    tests/lib/flash-run.mjs: WCAG 2.3.1's general and red flashes over a sixth of the screen, small lights switching
//    together over a quarter of a 10-degree field, regular flicker from 3.5 Hz in any twelfth by twelfth of it). The
//    table is held to those bounds on the engine's sixth of the screen and again on a 48 x 48 grid (regions a 24th of
//    it: a single insert or flasher).
//    Controls, which must fail: the same frames under the engine's 8 Hz full-screen strobe, and pinball_bench_oldstrobe
//    (the same scene on the lights as they were).
//   node tests/pinball-flash.mjs              both parts (the frames inside one hold of the GPU suite lock)
//   node tests/pinball-flash.mjs --cpu        part 1 only
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { ROOT, inSuiteLock, captureRun, gate, mustFail } from './lib/flash-run.mjs';

const BIN = join(ROOT, 'build/native/games/pinball');
const argv = process.argv.slice(2);
const cpuOnly = argv.includes('--cpu');
const FROM = 300, FRAMES = 479;
const FINE = { fine: 48 };
let bad = 0;

if (!process.env.CW_GPU_SUITE_INNER) { /* part 1 */
  const r = spawnSync(join(BIN, 'pinball_flashcheck'), ['120'], { encoding: 'utf8', timeout: 300000 });
  const sum = JSON.parse((r.stdout || '{}').trim().split('\n').pop() || '{}');
  for (const l of (r.stdout || '').split('\n').filter((l) => /^FAIL/.test(l))) console.log(l);
  if (r.status !== 0 || sum.fails !== 0) bad++, console.log(`FAIL lights: ${sum.fails} table phases strobe (exit ${r.status})`);
  else console.log(`ok   lights: ${sum.frames} frames of play, modes, wizard modes and event storms on every table; most turns of one light in a second: inserts ${sum.inserts}, targets and caps ${sum['targets and caps']}, pop lights ${sum['pop lights']}, flashers ${sum.flashers}, general light ${sum['general light']} (bound 3)`);
  const c = spawnSync(join(BIN, 'pinball_flashcheck_oldstrobe'), ['120'], { encoding: 'utf8', timeout: 300000 });
  const cs = JSON.parse((c.stdout || '{}').trim().split('\n').pop() || '{}');
  if (c.status !== 0 && cs.fails > 0) console.log(`ok   control (the old lights) fails as it must: ${cs.fails} table phases, inserts ${cs.inserts}, flashers ${cs.flashers} turns in a second`);
  else bad++, console.log(`CONTROL PASSED (a failure): the old blinking went unseen (exit ${c.status})`);
  if (cpuOnly) process.exit(bad ? 1 : 0);
  const p = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'pinball-flash', 'node', fileURLToPath(import.meta.url), ...argv],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(p.status === 0 && !bad ? 0 : 1);
}
inSuiteLock('pinball-flash', fileURLToPath(import.meta.url)); /* (inside already) */

/* part 2 */
const o = { from: FROM, frames: FRAMES, scale: 2, env: { CW_RENDER_SCALE: '0.75', PINBALL_FLASH_STORM: '1' } };
bad += gate(`a wizard mode in an event storm, frames ${FROM}-${FRAMES}`,
  await captureRun({ ...o, exe: join(BIN, 'pinball_bench'), name: 'pinball-bench' }), {}, FINE) ? 0 : 1;
bad += mustFail('the same storm on the old lights, pinball_bench_oldstrobe',
  await captureRun({ ...o, exe: join(BIN, 'pinball_bench_oldstrobe'), name: 'pinball-old' }), {}, FINE) ? 0 : 1;
process.exit(bad ? 1 : 0);
