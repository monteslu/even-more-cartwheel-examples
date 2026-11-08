// skate feel gate: the game played the way a person plays it, on a pad, for twelve seconds of Sunnyside
// (tests/skate-feel.json: Start through the menus, push with the stick wobbling a little, carve, an ollie, a flip in the
// air, a manual held into the landing and let go, a U-turn carve, an ollie with a grab held and let go, a push, an
// ollie with a flip picked by the stick), then a bot skating Sunnyside's flat bar (a grind) and its north quarter pipe
// (vert air). The native app on the test GPU, headless, through the GPU suite lock. Holds what the frames must show to
// feel right:
//   - every trick landed, no bail, at least six tricks in the pad run; the bar run grinds, the quarter run airs
//   - the camera never turns more than 3 degrees in a frame (no jerk the eye cannot follow) and sits 3 to 8 m from the
//     rider (the pad run)
//   - the feet stay on the deck while it rolls or grinds (2 cm at worst: IK reaches them)
//   - the feet stand where a skater's feet go, in every state (rolling, pushing, carving, the crouch, the air, a grab,
//     a landing, a manual, a grind): each sole's middle, measured on the posed skeleton in the board's frame as drawn,
//     over the front truck's bolts to the nose's kick (front) or the back bolts to the tail (back), within 8 cm of the
//     deck's middle line and 3 cm of its top; 1 cm out at worst. Left out: a foot off the deck on purpose (a flip's
//     flick, the push's foot on the ground)
// The controls: the pad run with the last flip pressed a fifth of a second before the landing (it is still turning when
// the board comes down): it must bail, and fail; and the pad run with the feet put together in the deck's middle
// (SKATE_FEET_CONTROL): it must fail on where the feet stand.
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

function play(name, { app = 'skate', script, frames = 720, env = {} }) {
  const state = join(OUT, `${name}.json`), shot = join(OUT, `${name}.png`);
  const args = [join(ENGINE, 'tools/run-cart.mjs'), '--host', 'native', '--app', app, '--frames', String(frames), '--state', state, '--shot', shot];
  if (script) args.push('--input', join(ROOT, 'tests', script));
  const r = spawnSync('node', args, { encoding: 'utf8', env: { ...process.env, ...env, CW_PROJECT_ROOT: ROOT }, maxBuffer: 1 << 26 });
  if (r.status !== 0) return { error: `run-cart exited ${r.status}: ${(r.stderr || r.stdout || '').split('\n').slice(-6).join(' | ')}` };
  return JSON.parse(readFileSync(state, 'utf8'));
}
const field = (s, k) => (s.fields || s)[k];
const STATES = ['rolling', 'in the air', 'grinding', 'bailing'];
const placeWhere = (code) => `${STATES[Math.floor(code / 100)] ?? '?'}, the ${Math.floor(code / 10) % 10 === 2 ? 'back' : 'front'} foot${code % 10 & 1 ? ', pushing' : ''}${code % 10 & 2 ? ', in a manual' : ''}`;

const errWhere = (code) => `${STATES[Math.floor(code / 10)] ?? '?'}${code % 10 & 1 ? ', pushing' : ''}${code % 10 & 2 ? ', in a manual' : ''}`;
// the feet: on the deck (reach) and where a skater's feet go (placement)
function feet(s, why) {
  const foot = field(s, 'view.foot_err_max'), place = field(s, 'view.foot_place_max');
  if (!(foot <= 0.02)) why.push(`a foot ${(foot * 100).toFixed(1)} cm off the deck (${errWhere(field(s, 'view.foot_err_state'))})`);
  if (!(place <= 0.01)) why.push(`a foot ${(place * 100).toFixed(1)} cm outside where a skater's foot goes (${placeWhere(field(s, 'view.foot_place_state'))})`);
}
function judgePad(s) {
  const why = [];
  if (s.error) return [s.error];
  const tricks = field(s, 'skater.tricks')[0], bails = field(s, 'skater.bails')[0];
  const turn = field(s, 'view.cam_turn_max'), dist = field(s, 'view.cam_dist');
  if (field(s, 'game.phase') !== 1) why.push(`not in a run (phase ${field(s, 'game.phase')})`);
  if (bails) why.push(`${bails} bails`);
  if (tricks < 6) why.push(`${tricks} tricks (6 at least)`);
  if (!(turn <= 3)) why.push(`the camera turned ${turn.toFixed(2)} degrees in a frame`);
  if (!(dist >= 3 && dist <= 8)) why.push(`the camera ${dist.toFixed(2)} m from the rider`);
  feet(s, why);
  return why;
}
const judgeBot = (need) => (s) => {
  const why = [];
  if (s.error) return [s.error];
  if (field(s, 'skater.bails')[0]) why.push(`${field(s, 'skater.bails')[0]} bails`);
  if (need === 'grind' && !(field(s, 'skater.grinds')[0] >= 1)) why.push('no grind');
  if (need === 'air' && !(field(s, 'skater.max_air')[0] >= 0.3)) why.push(`the highest air ${field(s, 'skater.max_air')[0].toFixed(2)} m (0.3 m at least)`);
  feet(s, why);
  return why;
};
const feetDesc = (s) => `feet ${(field(s, 'view.foot_err_max') * 100).toFixed(1)} cm off at worst, ${(field(s, 'view.foot_place_max') * 100).toFixed(1)} cm out of place at worst`;
const descPad = (s) => s.error ? s.error : `${field(s, 'skater.tricks')[0]} tricks, ${field(s, 'skater.bails')[0]} bails, score ${field(s, 'skater.score')[0]}, camera turn ${field(s, 'view.cam_turn_max').toFixed(2)} deg/frame at most, ${field(s, 'view.cam_dist').toFixed(2)} m back, ${feetDesc(s)}`;
const descBot = (s) => s.error ? s.error : `${field(s, 'skater.grinds')[0]} grinds, highest air ${field(s, 'skater.max_air')[0].toFixed(2)} m, ${field(s, 'skater.bails')[0]} bails, ${feetDesc(s)}`;

const BOT = { app: 'skate_auto', frames: 1500, env: { SKATE_PLAYERS: '1', SKATE_MODE: '0' } };
const runs = [
  { name: 'feel', run: { script: 'skate-feel.json' }, judge: judgePad, desc: descPad },
  { name: 'bar', run: { ...BOT, env: { ...BOT.env, SKATE_LINE: '6' } }, judge: judgeBot('grind'), desc: descBot },
  { name: 'quarter', run: { ...BOT, env: { ...BOT.env, SKATE_LINE: '2' } }, judge: judgeBot('air'), desc: descBot },
];
const controls = [
  { name: 'control', what: 'the late flip', run: { script: 'skate-feel-control.json' }, judge: judgePad, desc: descPad, must: /bails/ },
  { name: 'control-feet', what: 'the feet in the middle', run: { script: 'skate-feel.json', env: { SKATE_FEET_CONTROL: '1' } }, judge: judgePad, desc: descPad, must: /outside where/ },
];
let bad = 0;
for (const c of runs) {
  const s = play(c.name, c.run), why = c.judge(s);
  if (why.length) { bad++; console.log(`FAIL ${c.name}: ${c.desc(s)}\n     ${why.join('; ')}`); } else console.log(`ok   ${c.name}: ${c.desc(s)}`);
}
for (const c of controls) {
  const s = play(c.name, c.run), why = c.judge(s);
  if (why.some((w) => c.must.test(w))) console.log(`ok   control (${c.what}) fails as it must: ${c.desc(s)}\n     ${why.join('; ')}`);
  else { bad++; console.log(`CONTROL PASSED (a failure): ${c.what}: ${c.desc(s)}${why.length ? `\n     failed only on: ${why.join('; ')}` : ''}`); }
}
process.exit(bad ? 1 : 0);
