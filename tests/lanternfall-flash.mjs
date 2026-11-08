// lanternfall flash check: no strobing. A busy fight (lanternfall_bench: a warden fight in its arena, both players, the
// warden's moves, shots, sparks, particles, hits) is captured every frame for three seconds and measured by the
// engine's flash check (deps/cartwheel-engine/tools/lib/flash.mjs, through tests/lib/flash-run.mjs: WCAG 2.3.1's
// general and red flashes over a sixth of the screen, small lights switching together over a quarter of a 10-degree
// field, regular flicker from 3.5 Hz).
// CONTROLS, which must fail: the same frames under the engine's 8 Hz full-screen strobe, and the same frames with a
// quarter of the screen blinking at 5 Hz (a flash wash, or a blinking sprite the size of a room).
//   node tests/lanternfall-flash.mjs        (inside one hold of the GPU suite lock)
import { fileURLToPath } from 'node:url';
import { inSuiteLock, captureRun, gate, mustFail, withStrobe } from './lib/flash-run.mjs';

inSuiteLock('lanternfall-flash', fileURLToPath(import.meta.url));
const FROM = 240, FRAMES = 419;

let bad = 0;
const run = await captureRun({ app: 'lanternfall_bench', from: FROM, frames: FRAMES, name: 'lanternfall-bench' });
bad += gate(`a warden fight, frames ${FROM}-${FRAMES}`, run) ? 0 : 1;
bad += mustFail('a quarter of the screen blinking at 5 Hz', { ...run, frames: withStrobe(run.frames, { hz: 5, mix: 0.35, area: 0.25 }) }) ? 0 : 1;
process.exit(bad ? 1 : 0);
