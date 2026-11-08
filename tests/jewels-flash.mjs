// jewels flash check: nothing strobes. Eight seconds of jewels_bench (refills that run long cascades of big clears on
// every move, the bursts, shards, rings and streaks at their biggest) is captured every frame (frames 120 to 599) and
// measured by the engine's flash check (deps/cartwheel-engine/tools/lib/flash.mjs, through tests/lib/flash-run.mjs:
// WCAG 2.3.1's general and red flashes over a sixth of the screen, small lights switching together over a quarter of
// a 10-degree field, regular flicker from 3.5 Hz in any twelfth by twelfth of the screen, and a run that never moves
// proves nothing).
// CONTROL, which must fail: the same frames under the engine's 8 Hz full-screen strobe.
//   node tests/jewels-flash.mjs        (inside one hold of the GPU suite lock)
import { fileURLToPath } from 'node:url';
import { inSuiteLock, captureRun, gate } from './lib/flash-run.mjs';

inSuiteLock('jewels-flash', fileURLToPath(import.meta.url));
const FROM = 120, FRAMES = 599;
const ok = gate(`jewels_bench's cascades, frames ${FROM}-${FRAMES}`, await captureRun({ app: 'jewels_bench', from: FROM, frames: FRAMES, name: 'jewels_bench' }));
process.exit(ok ? 0 : 1);
