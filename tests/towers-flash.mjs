// towers photosensitivity check: nothing in the game strobes. Every frame of four seconds of each busy stretch is
// captured natively, headless, and measured by the engine's flash check (deps/cartwheel-engine/tools/lib/flash.mjs,
// through tests/lib/flash-run.mjs: WCAG 2.3.1's general and red flashes over a sixth of the screen, small lights
// switching together over a quarter of a 10-degree field, which a crowd walking over the path does not trip because
// its pixels go up and down in equal numbers, and regular flicker from 3.5 Hz in any twelfth by twelfth of the screen:
// a beam or a glow flickering, lightning re-jagged, a light that blinks). Frames are kept at 480 x 270.
// Each scene is captured twice from the same deterministic run: as it is, and with the crowd's bodies hidden
// (TOWERS_HIDE=3, the bodies and their blob shadows: every effect, light, beam and bolt the same), so the effects are
// also judged on their own, and held to the same bounds again on a 48 x 48 grid (regions a 24th of the screen), past
// WCAG's sixth: the old effects flickered over areas smaller than that. (The crowd is left out of the fine grid: creeps
// walking through a region that small change it as a flash would.)
// Scenes: towers_bench (map 26: level-3 towers on every pad, 2400 creeps, the busiest the game gets), and the volcano
// (map 23, wave 18, towers on every pad, four bots: red ground under lava, mortar blasts and a crowd).
// The volcano's lightning with margin: the coils' (level-3 STORM branches on its pads, firing one to two times a second)
// and the storm's (a player's power: four called within 1.3 seconds, TOWERS_STORMS, one a seat at set ticks), each
// judged effects alone on the fine grid past WCAG: at most 2 flashes in a second in any region (WCAG allows 3), the
// strikes and their light swelling in and fading slowly. Its control, TOWERS_OLD_STORM=1 (the old quick strikes and
// lights, which reached 4 a second on the fine grid), must fail that margin.
// CONTROLS, which must fail: every capture under the engine's 8 Hz full-screen strobe, and the bench with
// TOWERS_STROBE_CONTROL=1 (the old effects: lightning re-jagged every other frame, step-on flash lights, a beam
// flickering at 7 Hz, hit creeps popping white).
// Run: node tests/towers-flash.mjs [webgpu] [--storm]   (holds the GPU suite lock; --storm: the volcano's lightning alone)
import { fileURLToPath } from 'node:url';
import { inSuiteLock, captureRun, gate, mustFail, analyseFrames, judgeFlash, describeFlash } from './lib/flash-run.mjs';

inSuiteLock('towers-flash', fileURLToPath(import.meta.url));
const GPU = process.argv.includes('webgpu') ? 'webgpu' : 'gl';
const COUNT = 240, FINE = { fine: 48 }; // (the fine grid: regions a 24th of the screen, where the old effects flickered)
const HIDE = { TOWERS_HIDE: '3' }; // (the crowd and its blob shadows)
const BASE = { CW_WGPU_VALIDATION: '0', CW_RENDER_SCALE: '1.5', TOWERS_NO_HUD: '1' };
const SCENES = [
  { name: 'bench', app: 'towers_bench', from: 300, env: {} },
  { name: 'volcano', app: 'towers_auto', from: 600, env: { TOWERS_MAP: '23', TOWERS_WAVE: '18', TOWERS_PLAYERS: '4' } },
];
const capture = (sc, label, extra = {}) => captureRun({ app: sc.app, from: sc.from + 1, frames: sc.from + COUNT, gpu: GPU,
  name: `towers-${sc.name}-${label}`, scale: 4, env: { ...BASE, ...sc.env, ...extra } });

let bad = 0;
const STORM_ONLY = process.argv.includes('--storm'); // (the storm scene and its control alone)
for (const sc of STORM_ONLY ? [] : SCENES) {
  bad += gate(`${sc.name} ${GPU} with the crowd, ${COUNT} frames`, await capture(sc, 'real')) ? 0 : 1;
  bad += gate(`${sc.name} ${GPU} effects alone, ${COUNT} frames`, await capture(sc, 'effects', HIDE), {}, FINE) ? 0 : 1;
}
{ /* the storm, with margin */
  const STORM = { name: 'storm', app: 'towers_auto', from: 600, env: { TOWERS_MAP: '23', TOWERS_WAVE: '18', TOWERS_PLAYERS: '4', TOWERS_STORMS: '640,655,700,715' } };
  const MARGIN = { maxFlashes: 2 };
  const margin = (label, run) => {
    const m = analyseFrames(run.w, run.h, run.frames, MARGIN), why = judgeFlash(m).filter((p) => !/proves nothing/.test(p));
    const f = analyseFrames(run.w, run.h, run.frames, { ...MARGIN, grid: FINE.fine }), wf = judgeFlash(f).filter((p) => !/proves nothing/.test(p));
    return { why: [...why, ...wf.map((p) => `(${FINE.fine} x ${FINE.fine} grid) ${p}`)], line: `${describeFlash(m)}; on the fine grid ${describeFlash(f)}` };
  };
  const s = await capture(STORM, 'real'), e = await capture(STORM, 'effects', HIDE), v = await capture(SCENES[1], 'effects', HIDE);
  bad += gate(`storm ${GPU} with the crowd, ${COUNT} frames`, s) ? 0 : 1;
  for (const [label, run] of [['storms called, with the crowd', s], ['storms called, effects alone', e], ['as the bots play, effects alone', v]]) {
    const r = margin(label, run);
    if (r.why.length) bad++, console.log(`FAIL volcano ${GPU} ${label}, at most 2 flashes a second: ${r.why.join('; ')}\n     ${r.line}`);
    else console.log(`ok   volcano ${GPU} ${label}, at most 2 flashes a second: ${r.line}`);
  }
  const old = margin('old', await capture(SCENES[1], 'old-storm', { ...HIDE, TOWERS_OLD_STORM: '1' }));
  if (old.why.length) console.log(`ok   control fails as it must (TOWERS_OLD_STORM, the old quick storm): ${old.why[0]}\n     ${old.line}`);
  else bad++, console.log(`CONTROL PASSED (a failure): the old storm keeps the margin: ${old.line}`);
}
if (!STORM_ONLY) bad += mustFail('the bench\'s effects as they were, TOWERS_STROBE_CONTROL', await capture(SCENES[0], 'strobe', { ...HIDE, TOWERS_STROBE_CONTROL: '1' }), {}, FINE) ? 0 : 1;
process.exit(bad ? 1 : 0);
