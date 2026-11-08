// skate flash check: nothing strobes. Eight seconds of skate_auto (bots skating the park: airs, grinds, flips,
// landings and bails, the camera following) is captured every frame (frames 300 to 779) and measured by the engine's
// flash check (deps/cartwheel-engine/tools/lib/flash.mjs, through tests/lib/flash-run.mjs: WCAG 2.3.1's general and
// red flashes over a sixth of the screen, small lights switching together over a quarter of a 10-degree field,
// regular flicker from 3.5 Hz in any twelfth by twelfth of the screen, and a run that never moves proves nothing).
// CONTROL, which must fail: the same frames under the engine's 8 Hz full-screen strobe.
//   node tests/skate-flash.mjs        (inside one hold of the GPU suite lock)
import { fileURLToPath } from 'node:url';
import { inSuiteLock, captureRun, gate } from './lib/flash-run.mjs';

inSuiteLock('skate-flash', fileURLToPath(import.meta.url));
const FROM = 300, FRAMES = 779;
const ok = gate(`skate_auto's bots, frames ${FROM}-${FRAMES}`, await captureRun({ app: 'skate_auto', from: FROM, frames: FRAMES, name: 'skate_auto' }));
process.exit(ok ? 0 : 1);
