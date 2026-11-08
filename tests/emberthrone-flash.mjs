// emberthrone flash check: nothing strobes. Eight seconds of emberthrone_auto (the bot on its journey through the
// first region, the camera following) is captured every frame (frames 600 to 1079) and measured by the engine's flash
// check (deps/cartwheel-engine/tools/lib/flash.mjs, through tests/lib/flash-run.mjs: WCAG 2.3.1's general and red
// flashes over a sixth of the screen, small lights switching together over a quarter of a 10-degree field, regular
// flicker from 3.5 Hz in any twelfth by twelfth of the screen, and a run that never moves proves nothing).
// CONTROL, which must fail: the same frames under the engine's 8 Hz full-screen strobe.
//   node tests/emberthrone-flash.mjs        (inside one hold of the GPU suite lock)
import { fileURLToPath } from 'node:url';
import { inSuiteLock, captureRun, gate } from './lib/flash-run.mjs';

inSuiteLock('emberthrone-flash', fileURLToPath(import.meta.url));
const FROM = 600, FRAMES = 1079;
const ok = gate(`emberthrone_auto's journey, frames ${FROM}-${FRAMES}`, await captureRun({ app: 'emberthrone_auto', from: FROM, frames: FRAMES, name: 'emberthrone_auto' }));
process.exit(ok ? 0 : 1);
