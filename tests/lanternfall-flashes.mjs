// lanternfall flash check: nothing in the game strobes. Two seconds (120 consecutive frames) of each of three scenes
// that hold the game's brightest changes: every enemy struck every 10 ticks (faster than one player strikes) with
// their tells and attacks running, the wardens struck the same way, and both heroes just hurt beside a strike and a
// mend (the showcase, view.c). Every frame is measured by the engine's flash check
// (deps/cartwheel-engine/tools/lib/flash.mjs, through tests/lib/flash-run.mjs: WCAG 2.3.1's general and red flashes
// over a sixth of the screen, small lights switching together over a quarter of a 10-degree field, and regular
// flicker from 3.5 Hz in any twelfth by twelfth of the screen), and held to the same bounds again on a 48 x 48 grid
// (regions a 24th of the screen: a creature-sized strobe).
// CONTROLS, which must fail: each scene's frames under the engine's 8 Hz full-screen strobe, and the same scenes with
// LANTERNFALL_STROBE_CONTROL (each hit a full white flash, the hurt hero blinking as the game once drew it).
//
//   node tests/lanternfall-flashes.mjs [--scene heroes]     (inside one hold of the GPU suite lock)
import { fileURLToPath } from 'node:url';
import { inSuiteLock, captureRun, gate, mustFail } from './lib/flash-run.mjs';

inSuiteLock('lanternfall-flashes', fileURLToPath(import.meta.url));
const argv = process.argv.slice(2);
const opt = (k) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : null);
const START = 120, FRAMES = 120, FINE = { fine: 48 };
const SCENES = { enemies: { LANTERNFALL_SHOWCASE: 'e' }, wardens: { LANTERNFALL_SHOWCASE: 'b', LANTERNFALL_SHOWCASE_STATE: '3' }, heroes: { LANTERNFALL_SHOWCASE: 'h' } };
if (opt('--scene')) for (const k of Object.keys(SCENES)) if (k !== opt('--scene')) delete SCENES[k];

let bad = 0;
for (const [name, vars] of Object.entries(SCENES)) {
  const o = { app: 'lanternfall_auto', from: START, frames: START + FRAMES - 1, name: `lanternfall-${name}`, scale: 4,
    env: { CW_RENDER_SCALE: '1', LANTERNFALL_IDLE: '1', LANTERNFALL_NO_HUD: '1', ...vars } };
  bad += gate(`${name}, frames ${o.from}-${o.frames}`, await captureRun(o), {}, FINE) ? 0 : 1;
  bad += mustFail(`${name} with LANTERNFALL_STROBE_CONTROL, the old hit flashes and blink`,
    await captureRun({ ...o, name: `${o.name}-strobe`, env: { ...o.env, LANTERNFALL_STROBE_CONTROL: '1' } }), {}, FINE) ? 0 : 1;
}
process.exit(bad ? 1 : 0);
