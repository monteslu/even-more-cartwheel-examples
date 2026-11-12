// destructo levels gate: every level, checked by games/destructo/tools/levelcheck.c in the check cart
// (destructo_check, games/destructo/tools/checkcart.c) on romdev: check run levels [...]. The levels run on two hosts
// at once, half each; the controls after them.
//   floats    every block as placed stands on another block or the ground, and none is placed inside another
//   rest      the level settles 3 s, a landmark 5.5 s (as the game does before play; nothing may move more than 0.6 m doing it: the
//             tallest towers squeeze down about 0.4 m), then 60 s alone: no block moves more than 5 mm, nothing
//             breaks, no crystal moves, and everything is asleep at the end
//   solve     the bot, through the controls, brings every crystal down within the level's par
//   result    the level is judged once the world is still: at most 10 bodies awake when the game says won or lost,
//             and no more than 2% more of the structure down 5 s later. Three stars and the headline "brought
//             down" only when the tool's own count of the structure 5 s on (every placed block by volume: gone, or
//             more than its own size and 0.6 m from where it settled) reaches the level's mark
//   replay    the instant replay of the level's last shot (the level made again and stepped with the inputs the bot
//             gave, src/replay.c) ends on the same state as the live game where it was judged: every block's pose to
//             the bit, the counters, the scores
//   marks     each landmark has a mark of its own, and the bot brings that landmark that far down within par
// Controls, each must fail its check: the result rules before the fix (judged 1.5 s after the last crystal fell,
// stars by shots alone) on Hagia Sophia (three stars with the
// building standing); the old wait (judged only once the world is still) on the Colosseum; a replay whose last shot is fired 3 milliradians off (level 1) must end elsewhere; a floating block and two blocks placed in one another (level 1), a plank on a post
// 3 m up in level 1's yard with its middle 1.8 m out over nothing (it tips while settling), the bot aiming 150 milliradians off (8.6 degrees) (level 2).
// The physics does not draw: one run, on a GL host (--gpu webgpu: on a WebGPU host).
// Run: node tests/destructo-levels.mjs [--gpu gl|webgpu]
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { runCheck, inPairs, ROOT } from './lib/romdev.mjs';

const gi = process.argv.indexOf('--gpu');
const gpu = gi > 0 ? process.argv[gi + 1] : 'gl';
const run = async (args) => {
  const r = await runCheck('levels', args, { gpu });
  if (r.status !== 0) { console.log(`FAIL check run levels ${args.join(' ')} exited ${r.status}\n${r.text.slice(-1500)}`); process.exit(1); }
  return r.rows;
};
const NLEVELS = +/#define NLEVELS (\d+)/.exec(readFileSync(join(ROOT, 'games/destructo/src/game.h'), 'utf8'))[1];
const PARTS = Math.ceil(NLEVELS / 20); /* one check process stops after about 29 levels (the cart's memory): hosts of at most 20 levels */
const every = (k) => Array.from({ length: NLEVELS }, (_, i) => i).filter((i) => i % PARTS === k).map(String); /* alternate levels: the landmarks split between the hosts */
const checks = (r) => ({
  floats: r.floating === 0 && r.overlapping === 0,
  rest: r.settle_move_mm <= 600 && r.drift_mm <= 5 && r.broken_settle === 0 && r.targets_moved === 0 && r.awake_end === 0,
  solve: r.solved && r.shots <= r.par,
  replay: r.replay === 1,
  result: r.judge_lag <= 60 && (!r.stars3 || r.share_after >= r.mark - 0.005) && (r.result !== 1 || r.share_after >= r.mark - 0.005), /* judged the instant the last crystal is down (a second at most), the stars by the share the rubble reaches */
});
const describe = (r) => `${r.level + 1} ${r.name} (${r.blocks} blocks, ${r.targets} crystals): floating ${r.floating}, overlapping ${r.overlapping}, settled ${r.settle_move_mm.toFixed(0)} mm, then drift ${r.drift_mm.toFixed(2)} mm, awake ${r.awake_end}, broken ${r.broken_settle}; bot ${r.solved ? 'cleared it' : 'did not clear it'} in ${r.shots} shots (par ${r.par}), ${r.broken} broke, ${r.max_awake} awake at most, step worst ${r.step_ms_max} ms; replay ${r.replay === 1 ? 'ended on the live state' : r.replay === 0 ? 'ENDED ELSEWHERE' : 'NOT RUN'} (${r.replay_ticks} ticks); judged ${r.judge_lag} ticks after the last crystal with ${r.judged_awake} awake, ${(r.share_judged * 100).toFixed(0)}% down then ${(r.share_after * 100).toFixed(0)}% 5 s on (mark ${(r.mark * 100).toFixed(0)}%), ${r.stars} stars, ${["", "brought down", "crystals down", "lost"][r.result]}`;
let bad = 0;
const all = (await inPairs(Array.from({ length: PARTS }, (_, k) => () => run(every(k))))).flat().sort((a, b) => a.level - b.level);
if (all.length !== NLEVELS) { console.log(`FAIL ${all.length} levels checked, not ${NLEVELS}`); process.exit(1); }
for (const r of all) {
  const c = checks(r), why = Object.entries(c).filter(([, v]) => !v).map(([k]) => k);
  if (why.length) { bad++; console.log(`FAIL ${describe(r)}\n     fails: ${why.join(', ')}`); } else console.log(`ok   ${describe(r)}`);
}
for (const r of all) { /* a landmark's mark is its own: the bot brings that one that far down */
  const ok = r.solved && r.shots <= r.par && r.share_after >= r.mark - 0.005;
  if (!ok) bad++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} marks: ${r.name}: mark ${(r.mark * 100).toFixed(0)}%, the bot within par ${(r.share_after * 100).toFixed(0)}%`);
}
console.log(`     three stars on ${all.filter((r) => r.stars === 3).length} of ${all.length} levels; shots against par: ${all.map((r) => `${r.shots}/${r.par}`).join(' ')}`);
const CONTROLS = [['float', 'floats'], ['rest', 'rest'], ['solve', 'solve'], ['stars', 'result'], ['replay', 'replay'], ['settle', 'result']];
const ctlRows = await inPairs(CONTROLS.map(([ctl]) => async () => (await run(['--control', ctl]))[0]));
for (const [k, [ctl, check]] of CONTROLS.entries()) {
  const r = ctlRows[k];
  if (!checks(r)[check]) console.log(`ok   control (${ctl}) fails its ${check} check as it must: ${describe(r)}`);
  else { bad++; console.log(`CONTROL PASSED (a failure): ${ctl}: ${describe(r)}`); }
}
process.exit(bad ? 1 : 0);
