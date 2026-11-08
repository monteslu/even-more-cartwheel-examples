// destructo result check, in the game itself (destructo_auto: the bot through the controls, natively on the test
// GPU): a level is judged once the world is still, and the instant replay of its last shot ends where the level did.
//   Stone Giant (level 24: one charge brings a six-storey tower down) and First Stones (level 1: two shots, so the
//   replay re-runs the first one before it shows the second):
//     judged    when the game says won or lost (result.last), at most 10 bodies were awake (result.awake)
//     replay    the replay ran (replay.ticks) and ended on the live game's judged state (replay.match: every block's
//               pose to the bit, the counters and the scores)
//   Frames of each level's result card and of its replay are left in out/result/ for review.
// Controls, each must fail its check: the result rules before the fix (DESTRUCTO_OLD_RULES: judged 1.5 s after the
// last crystal fell) on Stone Giant, and a replay whose last shot is nudged 3 milliradians (DESTRUCTO_REPLAY_PERTURB).
// Holds the GPU suite lock for its own runs only.
// Run: node tests/destructo-result.mjs
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'destructo-result', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/destructo_auto.json'), 'utf8')).binary;
const OUT = join(ROOT, 'out/result');
mkdirSync(OUT, { recursive: true });

function run(level, frames, name, extra = {}) {
  const dump = join(OUT, `${name}.json`);
  const env = { ...process.env, CW_FRAMES: String(frames), CW_DEBUG_DUMP: dump, CW_SCREENSHOT: join(OUT, `${name}.png`), SDL_AUDIODRIVER: 'dummy',
    CW_WARMUP_SPREAD: '0', CW_GPU: 'gl', DESTRUCTO_LEVEL: String(level), DESTRUCTO_QUALITY: '3', ...extra };
  const gl = glPin();
  if (gl) Object.assign(env, gl.env);
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '600', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  if (r.status !== 0) return { status: r.status };
  const d = JSON.parse(readFileSync(dump, 'utf8'));
  const f = (k) => d[k] ?? d.fields?.[k];
  return { status: 0, phase: f('game.phase'), result: f('result.last'), awake: f('result.awake'), count: f('result.count'), match: f('replay.match'), ticks: f('replay.ticks'), level: f('game.level') };
}
const judged = (r) => r.status === 0 && r.result > 0 && r.awake <= 10;
const replayed = (r) => r.status === 0 && r.ticks > 0 && r.match === 1;
const show = (r) => r.status !== 0 ? `exit ${r.status}` : `level ${r.level + 1}, phase ${r.phase}, result ${['playing', 'brought down', 'crystals down', 'out of ammunition'][r.result]}, ${r.awake} awake when judged, replay ${r.ticks} ticks, match ${r.match}`;
let bad = 0;
const say = (ok, what) => { if (!ok) bad++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); };
for (const [level, frames, name] of [[23, 1500, 'stone-giant'], [0, 1500, 'first-stones']]) {
  /* a frame of the replay's slow motion and one of the card, for review (the same deterministic run, cut short) */
  run(level, name === 'stone-giant' ? 1000 : 1150, `${name}-replay`);
  const r = run(level, frames, name);
  say(judged(r) && replayed(r), `${name}: ${show(r)}`);
}
const c1 = run(23, 1500, 'control-early', { DESTRUCTO_OLD_RULES: '1' });
say(!judged(c1), `control (the rules before the fix) fails its judged check as it must: ${show(c1)}`);
const c2 = run(0, 1500, 'control-replay', { DESTRUCTO_REPLAY_PERTURB: '1' });
say(!replayed(c2), `control (a nudged replay) fails its replay check as it must: ${show(c2)}`);
process.exit(bad ? 1 : 0);
