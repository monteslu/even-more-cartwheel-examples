// destructo play and seat gates, through the game's own controls: the game cart (destructo) on romdev, on GL and on
// WebGPU, driven by an input script (tests/destructo-*.json) through the engine's tools/run-cart.mjs --host romdev (one
// input set a script entry, the frames between stepped in one range).
//   play   A person's first level: Play on the title, a pad joins, Start, the first structure chosen with South, the
//          launcher turned on the d-pad, raised, the power set on a shoulder and fired (600 frames). The shot's flight,
//          every block it breaks and the whole collapse (game.hash: the world's hash of every moved body and event)
//          must be the same bits on GL and WebGPU, with a shot fired. Control: the script that never fires
//          (destructo-play-control.json) must end on another state.
//   seat   Two pads join and the second walks away: co-op is one team, so the first pad aims and fires on the second
//          seat's turn too (game.phase 1 and two shots by frame 800, on GL and WebGPU). Control: the seat rule before
//          the fix (dm set SEAT_CONTROL 1, sent before the first frame): the turn waits on the idle pad and only one
//          shot is fired.
// Run: node tests/destructo-play.mjs [play|seat] [--gpu gl|webgpu]
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT, gpusArg } from './lib/romdev.mjs';

const OUT = join(ROOT, 'out/gate');
mkdirSync(OUT, { recursive: true });
const FIELDS = ['game.phase', 'game.level', 'game.state', 'game.shots', 'game.targets_left', 'game.blocks', 'game.broken', 'game.fragments', 'game.hash', 'aim.yaw', 'aim.pitch', 'aim.power', 'player.score'];
function run(name, gpu, frames, input, cmds = []) {
  const state = join(OUT, `${name}.romdev-${gpu}.json`), shot = join(OUT, `${name}.romdev-${gpu}.png`);
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs'), '--host', 'romdev', '--gpu', gpu, '--app', 'destructo', '--frames', String(frames),
    '--input', join(ROOT, 'tests', input), '--state', state, '--shot', shot, ...(cmds.length ? ['--start-cmds', cmds.join(';')] : [])],
  { encoding: 'utf8', env: { ...process.env, CW_PROJECT_ROOT: ROOT }, timeout: 1200000, maxBuffer: 1 << 26 });
  if (r.status !== 0) return { err: `run-cart exited ${r.status}: ${(r.stderr || '').trim().split('\n').slice(-3).join(' | ')}` };
  return JSON.parse(readFileSync(state, 'utf8'));
}
const pick = (s) => Object.fromEntries(FIELDS.map((k) => [k, s[k]]));
const only = process.argv.slice(2).filter((a, i, all) => !a.startsWith('--') && all[i - 1] !== '--gpu');
const want = (g) => !only.length || only.includes(g);
let bad = 0;
const say = (ok, what) => { if (!ok) bad++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); };
const gpus = gpusArg();
if (want('play')) {
  const runs = gpus.map((gpu) => [gpu, run('destructo-play', gpu, 600, 'destructo-play.json')]);
  for (const [gpu, s] of runs) say(!s.err && s['game.shots'] >= 1 && s['game.broken'] > 0, `play ${gpu}: ${s.err || `${s['game.shots']} shot, ${s['game.broken']} blocks broken, hash ${s['game.hash']}`}`);
  if (runs.length === 2 && !runs.some(([, s]) => s.err)) {
    const a = pick(runs[0][1]), b = pick(runs[1][1]);
    const diff = FIELDS.filter((k) => JSON.stringify(a[k]) !== JSON.stringify(b[k]));
    say(!diff.length, `play: GL and WebGPU end on the same state${diff.length ? `; differ in ${diff.map((k) => `${k} ${JSON.stringify(a[k])} / ${JSON.stringify(b[k])}`).join(', ')}` : ` (hash ${a['game.hash']})`}`);
  }
  const c = run('destructo-play-control', gpus[0], 600, 'destructo-play-control.json');
  const ref = runs[0][1];
  const same = !c.err && !ref.err && FIELDS.every((k) => JSON.stringify(c[k]) === JSON.stringify(ref[k]));
  say(!c.err && !same, `play control (never fires) ends elsewhere as it must: ${c.err || `${c['game.shots']} shots, hash ${c['game.hash']}`}`);
}
if (want('seat')) {
  for (const gpu of gpus) {
    const s = run('destructo-seat', gpu, 800, 'destructo-seat.json');
    say(!s.err && s['game.phase'] === 1 && s['game.shots'] === 2, `seat ${gpu}: ${s.err || `phase ${s['game.phase']}, ${s['game.shots']} shots (expected phase 1, 2 shots)`}`);
  }
  const c = run('destructo-seat-control', gpus[0], 800, 'destructo-seat.json', ['dm set SEAT_CONTROL 1']);
  say(!c.err && c['game.shots'] !== 2, `seat control (the rule before the fix) fails as it must: ${c.err || `${c['game.shots']} shots`}`);
}
process.exit(bad ? 1 : 0);
