// lanternfall creatures: close-ups of every enemy and warden in every state (the showcase, view.c), at game scale and
// enlarged, at 1080p and 4K, on the test GPU.
//
//   node tests/lanternfall-creatures.mjs --capture <dir>   draws the sheets into <dir> (inside one hold of the GPU lock)
//   node tests/lanternfall-creatures.mjs --dir <dir>       measures sheets already there
//   node tests/lanternfall-creatures.mjs                   both, into out/creatures
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const BIN = [join(ROOT, 'build/native/project/games/lanternfall'), join(ROOT, 'build/native/games/lanternfall')].find((d) => existsSync(join(d, 'lanternfall_auto')));
const argv = process.argv.slice(2);
const opt = (k) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : null);

if (!process.env.CW_GPU_SUITE_INNER && !opt('--dir')) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'lanternfall-creatures', 'node', fileURLToPath(import.meta.url), ...argv],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}

/** the room each region is shown in: its first plain room two or three cells wide (as the look check) */
function rooms() {
  const r = spawnSync(join(BIN, 'lanternfall_worldgen'), ['/dev/null', '--list'], { encoding: 'utf8' });
  const pick = {};
  for (const line of (r.stdout + r.stderr).split('\n')) {
    const m = /^room (\d+) region (\d+) kind (\d+) (\d)x(\d)/.exec(line);
    if (m && +m[3] === 0 && +m[5] === 1 && +m[4] >= 2 && pick[+m[2]] === undefined) pick[+m[2]] = +m[1];
  }
  return pick;
}

function shot(out, env, gpu = 'gl') {
  const e = { ...process.env, CW_FRAMES: String(env.frames || 90), CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_RENDER_SCALE: String(env.scale || 1.5),
    CW_SCREENSHOT: out, CW_GPU: gpu, LANTERNFALL_IDLE: '1', LANTERNFALL_NO_HUD: '1', ...env.vars };
  return new Promise((ok) => spawn(join(BIN, 'lanternfall_auto'), [], { env: e, stdio: 'ignore' }).on('exit', (c) => ok(c)));
}

/** every sheet: name -> { vars, scale, frames } */
export function sheets(pick) {
  const S = {};
  const room = (r) => String(pick[r]);
  for (const r of [0, 2, 4, 5, 9, 11]) S[`enemies-r${r}`] = { vars: { LANTERNFALL_ROOM: room(r), LANTERNFALL_SHOWCASE: 'e' } };
  S['enemies-near-a'] = { vars: { LANTERNFALL_ROOM: room(0), LANTERNFALL_SHOWCASE: 'e', LANTERNFALL_SHOWCASE_ZOOM: '2.4', LANTERNFALL_SHOWCASE_ROWS: '0,1' } };
  S['enemies-near-b'] = { vars: { LANTERNFALL_ROOM: room(0), LANTERNFALL_SHOWCASE: 'e', LANTERNFALL_SHOWCASE_ZOOM: '2.4', LANTERNFALL_SHOWCASE_ROWS: '2,3' } };
  S['enemies-4k'] = { scale: 3, vars: { LANTERNFALL_ROOM: room(0), LANTERNFALL_SHOWCASE: 'e' } };
  for (let f = 0; f < 4; f++) S[`enemies-motion-${f}`] = { frames: 90 + f * 5, vars: { LANTERNFALL_ROOM: room(0), LANTERNFALL_SHOWCASE: 'e', LANTERNFALL_SHOWCASE_ZOOM: '2.4', LANTERNFALL_SHOWCASE_ROWS: '0,1' } };
  for (let st = 0; st < 4; st++) S[`wardens-s${st}`] = { vars: { LANTERNFALL_ROOM: room(9), LANTERNFALL_SHOWCASE: 'b', LANTERNFALL_SHOWCASE_STATE: String(st) } };
  for (let i = 0; i < 6; i++) S[`warden${i}-near`] = { vars: { LANTERNFALL_ROOM: room([3, 2, 4, 5, 7, 12][i] ?? 0), LANTERNFALL_SHOWCASE: 'b', LANTERNFALL_SHOWCASE_ONLY: String(i), LANTERNFALL_SHOWCASE_ZOOM: '1.8', LANTERNFALL_SHOWCASE_STATE: '1' } };
  return S;
}

let dir = opt('--dir');
if (!dir) {
  dir = opt('--capture') || join(ROOT, 'out/creatures');
  mkdirSync(dir, { recursive: true });
  const pick = rooms();
  for (let r = 0; r < 13; r++) if (pick[r] === undefined) pick[r] = pick[0];
  const only = opt('--only');
  const list = Object.entries(sheets(pick)).filter(([n]) => !only || n.includes(only));
  for (let i = 0; i < list.length; i += 2)
    await Promise.all(list.slice(i, i + 2).map(([n, s]) => shot(join(dir, `${n}.png`), s, opt('--gpu') || 'gl')));
  console.log(`captured ${list.length} sheets into ${dir}`);
  if (argv.includes('--capture')) process.exit(0);
}
const files = readdirSync(dir).filter((f) => f.endsWith('.png'));
console.log(`${files.length} sheets in ${dir}`);
