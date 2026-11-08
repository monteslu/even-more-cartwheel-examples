// destructo levels gate: every level, checked by build/native/games/destructo/destructo_levelcheck
// (games/destructo/tools/levelcheck.c):
//   floats    every block as placed stands on another block or the ground, and none is placed inside another
//   rest      the level settles 3 s (as the game does before play; nothing may move more than 0.6 m doing it: the
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
//   kegs      two powder kegs 1.6 m apart, the first set off: the second goes off too (a chain of two blasts);
//             control: 8 m apart, one blast only
//   marks     each kit's mark is reachable: the bot (which aims at crystals, not at a collapse) brings at least one
//             level of every kit that far down within par
// Controls, each must fail its check: the result rules before the fix (judged 1.5 s after the last crystal fell,
// stars by shots alone) on Stone Giant (judged while 1600 bodies still fall) and Garden Wall (three stars with the
// wall standing); a replay whose last shot is fired 3 milliradians off (level 1) must end elsewhere; a floating block and two blocks placed in one another (level 1), a plank on a post
// 3 m up in level 1's yard with its middle 1.8 m out over nothing (it tips while settling), the bot aiming 150 milliradians off (8.6 degrees) (level 2).
// Run: node tests/destructo-levels.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/destructo/destructo_levelcheck');
const run = (args) => {
  const r = spawnSync(exe, args, { encoding: 'utf8', timeout: 1800000 });
  if (r.status !== 0) { console.log(`FAIL ${exe} ${args.join(' ')} exited ${r.status}`); process.exit(1); }
  return r.stdout.trim().split('\n').filter((l) => l.startsWith('{')).map((l) => JSON.parse(l));
};
const checks = (r) => ({
  floats: r.floating === 0 && r.overlapping === 0,
  rest: r.settle_move_mm <= 600 && r.drift_mm <= 5 && r.broken_settle === 0 && r.targets_moved === 0 && r.awake_end === 0,
  solve: r.solved && r.shots <= r.par,
  replay: r.replay === 1,
  result: r.judged_awake <= 10 && r.share_after - r.share_judged <= 0.02 && (!r.stars3 || r.share_after >= r.mark - 0.005) && (r.result !== 1 || r.share_after >= r.mark - 0.005),
});
const describe = (r) => `${r.level + 1} ${r.name} (${r.blocks} blocks, ${r.kegs} kegs, ${r.targets} crystals): floating ${r.floating}, overlapping ${r.overlapping}, settled ${r.settle_move_mm.toFixed(0)} mm, then drift ${r.drift_mm.toFixed(2)} mm, awake ${r.awake_end}, broken ${r.broken_settle}; bot ${r.solved ? 'cleared it' : 'did not clear it'} in ${r.shots} shots (par ${r.par}), ${r.broken} broke, ${r.max_awake} awake at most, step worst ${r.step_ms_max} ms; replay ${r.replay === 1 ? 'ended on the live state' : r.replay === 0 ? 'ENDED ELSEWHERE' : 'NOT RUN'} (${r.replay_ticks} ticks); judged with ${r.judged_awake} awake, ${(r.share_judged * 100).toFixed(0)}% down then ${(r.share_after * 100).toFixed(0)}% 5 s on (mark ${(r.mark * 100).toFixed(0)}%), ${r.stars} stars, ${["", "brought down", "crystals down", "lost"][r.result]}`;
let bad = 0;
const all = run([]);
const kits = {};
for (const r of all) {
  const c = checks(r), why = Object.entries(c).filter(([, v]) => !v).map(([k]) => k);
  if (why.length) { bad++; console.log(`FAIL ${describe(r)}\n     fails: ${why.join(', ')}`); } else console.log(`ok   ${describe(r)}`);
  const k = (kits[r.kind] ??= { best: 0, mark: r.mark, three: 0, names: [] });
  if (r.solved && r.shots <= r.par) k.best = Math.max(k.best, r.share_after);
  k.three += r.stars === 3, k.names.push(r.name);
}
const KITS = ['tower', 'wall', 'bridge', 'castle', 'scaffold', 'pyramid', 'keep'];
for (const [kind, k] of Object.entries(kits)) {
  const ok = k.best >= k.mark;
  if (!ok) bad++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} marks: ${KITS[kind]} (${k.names.length} levels): mark ${(k.mark * 100).toFixed(0)}%, the bot's best within par ${(k.best * 100).toFixed(0)}%, three stars on ${k.three}`);
}
console.log(`     three stars on ${all.filter((r) => r.stars === 3).length} of ${all.length} levels; shots against par: ${all.map((r) => `${r.shots}/${r.par}`).join(' ')}`);
for (const [ctl, check] of [['float', 'floats'], ['rest', 'rest'], ['solve', 'solve'], ['early', 'result'], ['stars', 'result'], ['replay', 'replay']]) {
  const r = run(['--control', ctl])[0];
  if (!checks(r)[check]) console.log(`ok   control (${ctl}) fails its ${check} check as it must: ${describe(r)}`);
  else { bad++; console.log(`CONTROL PASSED (a failure): ${ctl}: ${describe(r)}`); }
}
{
  const [chain, apart] = run(['--kegs']);
  const ok = chain.booms >= 2 && chain.kegs_left === 0, cok = apart.booms === 1 && apart.kegs_left === 1;
  if (!ok) bad++;
  if (!cok) bad++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} kegs ${chain.kegs_gap} m apart: ${chain.booms} blasts, ${chain.kegs_left} kegs left`);
  console.log(`${cok ? 'ok   control fails as it must:' : 'CONTROL PASSED (a failure):'} kegs ${apart.kegs_gap} m apart: ${apart.booms} blasts, ${apart.kegs_left} kegs left`);
}
process.exit(bad ? 1 : 0);
