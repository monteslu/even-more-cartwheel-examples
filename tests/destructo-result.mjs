// destructo result check, in the game itself: destructo_auto (the bot through the controls) as a cart on romdev, on GL
// and on WebGPU: a level is judged once the world is still, and the instant replay of its last shot ends where the
// level did.
//   Stone Giant (one charge brings a six-storey tower down) and First Stones (two shots, so the
//   replay re-runs the first one before it shows the second):
//     judged    when the game says won or lost (result.last), at most 10 bodies were awake (result.awake)
//     replay    the replay ran (replay.ticks) and ended on the live game's judged state (replay.match: every block's
//               pose to the bit, the counters and the scores)
//   Frames of each level's replay and of its result card are left in out/result/ for review.
// The level and the controls are set over the debug channel (dm set ..., dm restart: games/destructo/src/opts.h).
// Controls, each must fail its check: the result rules before the fix (dm set OLD_RULES 1: judged 1.5 s after the
// last crystal fell) on Stone Giant, and a replay whose last shot is nudged 3 milliradians (dm set REPLAY_PERTURB 1).
// Run: node tests/destructo-result.mjs [--gpu gl|webgpu]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { openCart, inPairs, gpusArg, ROOT } from './lib/romdev.mjs';

const OUT = join(ROOT, 'out/result');
mkdirSync(OUT, { recursive: true });

async function run(gpu, level, name, opts = {}) {
  const h = await openCart('destructo_auto', { gpu, tag: `result-${name}` });
  try {
    for (const [k, v] of Object.entries({ LEVEL_NAME: level, QUALITY: 3, ...opts })) await h.command(`dm set ${k} "${v}"`);
    await h.command('dm restart');
    /* a frame of the replay's slow motion and one of the card, for review */
    await h.step((name.startsWith('stone') ? 1000 : 1150) - h.frame);
    await h.shot(join(OUT, `${name}-replay-${gpu}.png`));
    await h.step(1500 - h.frame);
    await h.shot(join(OUT, `${name}-${gpu}.png`));
    const d = await h.state();
    return { status: 0, phase: d['game.phase'], result: d['result.last'], awake: d['result.awake'], count: d['result.count'], match: d['replay.match'], ticks: d['replay.ticks'], level: d['game.level'] };
  } catch (e) {
    return { status: 1, err: e.message };
  } finally {
    await h.close();
  }
}
const judged = (r) => r.status === 0 && r.result > 0 && r.awake <= 10;
const replayed = (r) => r.status === 0 && r.ticks > 0 && r.match === 1;
const show = (r) => r.status !== 0 ? `failed: ${r.err}` : `level ${r.level + 1}, phase ${r.phase}, result ${['playing', 'brought down', 'crystals down', 'out of ammunition'][r.result]}, ${r.awake} awake when judged, replay ${r.ticks} ticks, match ${r.match}`;
let bad = 0;
const say = (ok, what) => { if (!ok) bad++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); };
for (const gpu of gpusArg()) {
  const [a, b, c1, c2] = await inPairs([
    () => run(gpu, 'Stone Giant', 'stone-giant'),
    () => run(gpu, 'First Stones', 'first-stones'),
    () => run(gpu, 'Stone Giant', 'control-early', { OLD_RULES: 1 }),
    () => run(gpu, 'First Stones', 'control-replay', { REPLAY_PERTURB: 1 }),
  ]);
  say(judged(a) && replayed(a), `${gpu} stone-giant: ${show(a)}`);
  say(judged(b) && replayed(b), `${gpu} first-stones: ${show(b)}`);
  say(c1.status === 0 && !judged(c1), `${gpu} control (the rules before the fix) fails its judged check as it must: ${show(c1)}`);
  say(c2.status === 0 && !replayed(c2), `${gpu} control (a nudged replay) fails its replay check as it must: ${show(c2)}`);
}
process.exit(bad ? 1 : 0);
