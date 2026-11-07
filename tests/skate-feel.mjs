// skate feel gate: the game played the way a person plays it, on a pad, for twelve seconds of Sunnyside
// (tests/skate-feel.json: Start through the menus, push with the stick wobbling a little, carve, an ollie, a flip in the
// air, a manual held into the landing and let go, a U-turn carve, an ollie with a grab held and let go, a push, an
// ollie with a flip picked by the stick). The native app on the test GPU, headless, through the GPU suite lock. Holds
// what the frames must show to feel right:
//   - every trick landed, no bail, at least six tricks in the run
//   - the camera never turns more than 3 degrees in a frame (no jerk the eye cannot follow) and sits 3 to 8 m from the
//     rider
//   - the feet stay on the deck while it rolls (2 cm at worst: IK reaches them)
// The control: the same run with the last flip pressed a fifth of a second before the landing (it is still turning when
// the board comes down): it must bail, and fail.
// Run: node tests/skate-feel.mjs
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'skate-feel', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const OUT = join(ROOT, 'out', 'skate-feel');
mkdirSync(OUT, { recursive: true });

function play(script, name) {
  const state = join(OUT, `${name}.json`), shot = join(OUT, `${name}.png`);
  const r = spawnSync('node', [join(ENGINE, 'tools/run-cart.mjs'), '--host', 'native', '--app', 'skate', '--frames', '720', '--input', join(ROOT, 'tests', script),
    '--state', state, '--shot', shot], { encoding: 'utf8', env: { ...process.env, CW_PROJECT_ROOT: ROOT }, maxBuffer: 1 << 26 });
  if (r.status !== 0) return { error: `run-cart exited ${r.status}: ${(r.stderr || r.stdout || '').split('\n').slice(-6).join(' | ')}` };
  return JSON.parse(readFileSync(state, 'utf8'));
}
const field = (s, k) => (s.fields || s)[k];

function judge(s) {
  const why = [];
  if (s.error) return [s.error];
  const tricks = field(s, 'skater.tricks')[0], bails = field(s, 'skater.bails')[0];
  const turn = field(s, 'view.cam_turn_max'), dist = field(s, 'view.cam_dist'), foot = field(s, 'view.foot_err_max');
  if (field(s, 'game.phase') !== 1) why.push(`not in a run (phase ${field(s, 'game.phase')})`);
  if (bails) why.push(`${bails} bails`);
  if (tricks < 6) why.push(`${tricks} tricks (6 at least)`);
  if (!(turn <= 3)) why.push(`the camera turned ${turn.toFixed(2)} degrees in a frame`);
  if (!(dist >= 3 && dist <= 8)) why.push(`the camera ${dist.toFixed(2)} m from the rider`);
  if (!(foot <= 0.02)) why.push(`a foot ${(foot * 100).toFixed(1)} cm off the deck`);
  return why;
}
const desc = (s) => s.error ? s.error : `${field(s, 'skater.tricks')[0]} tricks, ${field(s, 'skater.bails')[0]} bails, score ${field(s, 'skater.score')[0]}, camera turn ${field(s, 'view.cam_turn_max').toFixed(2)} deg/frame at most, ${field(s, 'view.cam_dist').toFixed(2)} m back, feet ${(field(s, 'view.foot_err_max') * 100).toFixed(1)} cm off at worst`;

let bad = 0;
const run = play('skate-feel.json', 'feel'), why = judge(run);
if (why.length) { bad++; console.log(`FAIL feel: ${desc(run)}\n     ${why.join('; ')}`); } else console.log(`ok   feel: ${desc(run)}`);
const ctl = play('skate-feel-control.json', 'control'), cwhy = judge(ctl);
if (cwhy.length) console.log(`ok   control fails as it must: ${desc(ctl)}\n     ${cwhy.join('; ')}`);
else { bad++; console.log(`CONTROL PASSED (a failure): ${desc(ctl)}`); }
process.exit(bad ? 1 : 0);
