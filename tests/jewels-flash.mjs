// jewels flash check: nothing strobes. jewels_bench (refills that run long cascades of big clears on every move) is
// captured every frame from 120 to 779: every step of its cascade of six (each clear's lights swelling and fading, the
// shards, dust, rings, rays and shockwaves at their biggest) and the climax that ends it, measured by the engine's
// flash check (deps/cartwheel-engine/tools/lib/flash.mjs, through tests/lib/flash-run.mjs: WCAG 2.3.1's general and
// red flashes over a sixth of the screen, small lights switching together over a quarter of a 10-degree field, regular
// flicker from 3.5 Hz in any twelfth by twelfth of the screen, and a run that never moves proves nothing).
// CONTROLS, which must fail: the same frames under the engine's 8 Hz full-screen strobe, and the game's own strobe
// (JEWELS_STROBE=1: a light over the board switched on and off ten times a second from each cascade's second step).
//   node tests/jewels-flash.mjs [gl|webgpu]     (inside one hold of the GPU suite lock)
import { fileURLToPath } from 'node:url';
import { inSuiteLock, captureRun, gate, mustFail } from './lib/flash-run.mjs';

inSuiteLock('jewels-flash', fileURLToPath(import.meta.url));
const GPU = process.argv[2] === 'webgpu' ? 'webgpu' : 'gl';
const FROM = 120, FRAMES = 779;
const run = (label, env = {}) => captureRun({ app: 'jewels_bench', from: FROM, frames: FRAMES, gpu: GPU, name: `jewels_bench-${label}`, env });
let bad = 0;
bad += gate(`jewels_bench's cascades ${GPU}, frames ${FROM}-${FRAMES}`, await run('real')) ? 0 : 1;
bad += mustFail('a light switched on and off ten times a second, JEWELS_STROBE', await run('strobe', { JEWELS_STROBE: '1' })) ? 0 : 1;
process.exit(bad ? 1 : 0);
