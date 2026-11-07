// adventure soak: adventure_auto plays the whole game, start to the Ember King, off the shared GPU (software GL on
// llvmpipe, the job pool at 2, nice 10) with many rule ticks per frame (ADV_TURBO), reading the status line it prints
// every minute of game time (ADV_LOG=1). It must show:
//   - the process exits 0 and every status line has finite positions (nan=0);
//   - the camera never has a wall between it and the player (camblock=0);
//   - the bot wins (an adv-won line) inside the game-time budget, having taken all three bosses, both keys, the maul;
//   - no stall: the route point moves on at least every 6 minutes of game time;
//   - resident memory grows less than 8 percent from minute 5 to the end.
// CONTROL that must fail: the same run with the maul's chest empty (ADV_NO_MAUL=1): the cracked wall never breaks, so
// the route stalls and the game is never won, and the soak has to say so.
//
//   node tests/adventure-soak.mjs [--minutes 60] [--turbo 30] [--no-control | --control-only]
// (each under its own hold of the GPU suite lock: run the two halves as two suites to keep the holds short)
import { spawn, spawnSync } from 'node:child_process';
import { readFileSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
// software GL, but under the GPU suite lock all the same (a render run loads the machine; the timing suites need it quiet)
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'adventure-soak', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const argv = process.argv.slice(2);
const arg = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const exe = join(ROOT, 'build/native/games/adventure/adventure_auto'), pak = exe + '.cwpak';
if (!existsSync(exe)) { console.error(`no ${exe}: TARGETS=native scripts/build.sh adventure_auto`); process.exit(2); }

async function run(minutes, turbo, extra = {}) {
  const ticks = Math.round(minutes * 3600);
  const env = { ...process.env, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_WORKERS: '2', SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy',
    CW_GPU: 'gl', ADV_QUALITY: '0', CW_RENDER_SCALE: '0.25', CW_FRAMES: String(Math.ceil(ticks / turbo)), CW_PAK: pak, ADV_TURBO: String(turbo), ADV_LOG: '1', CW_WARMUP_SPREAD: '0', ...extra };
  const kv = (line) => Object.fromEntries([...line.matchAll(/(\w+)=(-?\d+)/g)].map((m) => [m[1], +m[2]]));
  const status = [];
  let won = null, buf = '', rss0 = null, rssEnd = null;
  const p = spawn('nice', ['-n', '10', exe], { env, stdio: ['ignore', 'pipe', 'pipe'] });
  const rss = () => { try { return +/VmRSS:\s+(\d+)/.exec(readFileSync(`/proc/${p.pid}/status`, 'utf8'))[1]; } catch { return null; } };
  const onData = (d) => {
    buf += d;
    let i;
    while ((i = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, i); buf = buf.slice(i + 1);
      if (line.startsWith('adv-status')) { const s = kv(line); if (process.env.SOAK_VERBOSE) console.log(line); s.rss = rss(); status.push(s); if (s.tick >= 5 * 3600 && rss0 == null) rss0 = s.rss; rssEnd = s.rss; }
      else if (line.startsWith('adv-won')) won = kv(line);
      else if (/\[error\]/.test(line) && !/program table|NOT DRAWN|^(depth|standard|physical|basic|lambert)/.test(line)) console.log('  ' + line);
    }
    if (won) p.kill('SIGTERM'); /* the game is over: the rest of the frames add nothing */
  };
  p.stdout.on('data', onData); p.stderr.on('data', onData);
  const code = await new Promise((r) => p.on('close', (c, sig) => r(won && sig ? 0 : c)));
  const problems = [];
  if (code !== 0) problems.push(`exit ${code}`);
  if (status.some((s) => s.nan > 0)) problems.push('NaN positions');
  if (status.some((s) => s.camblock > 0)) problems.push(`the camera saw through a wall (${status.at(-1).camblock} ticks)`);
  if (!won) problems.push(`not won in ${minutes} minutes of game time (last route point ${status.at(-1)?.route}, region ${status.at(-1)?.region})`);
  const last = status.at(-1) || {};
  if (won && (last.keys !== 3 || last.maul !== 1)) problems.push(`won without both keys and the maul (keys ${last.keys}, maul ${last.maul})`);
  let stall = 0, lastRoute = -1, since = 0;
  for (const s of status) { if (s.route !== lastRoute) lastRoute = s.route, since = s.tick; stall = Math.max(stall, s.tick - since); }
  if (stall > 6 * 3600) problems.push(`the route stalled for ${(stall / 3600).toFixed(1)} minutes`);
  const growth = rss0 && rssEnd ? (rssEnd - rss0) / rss0 : null;
  if (growth != null && growth > 0.08) problems.push(`memory grew ${(growth * 100).toFixed(1)} percent from minute 5`);
  return { problems, won, status, growth };
}

let bad = 0;
const minutes = +arg('--minutes', 60), turbo = +arg('--turbo', 30);
const only = argv.includes('--control-only');
const r = only ? null : await run(minutes, turbo);
const s = r?.status.at(-1) || {};
if (r) { const line = `${r.won ? `won at ${(r.won.tick / 3600).toFixed(1)} min` : 'not won'}, ${s.kills ?? 0} kills, ${s.deaths ?? 0} falls, ${s.rests ?? 0} rests, stuck ${s.stuck ?? 0}, memory ${r.growth == null ? 'n/a' : (r.growth * 100).toFixed(1) + '%'}`;
if (r.problems.length) { bad++; console.log(`FAIL soak: ${line}\n  ${r.problems.join('\n  ')}`); } else console.log(`ok   soak: ${line}`); }
if (!argv.includes('--no-control')) {
  const c = await run(25, turbo, { ADV_NO_MAUL: '1' });
  if (!c.problems.length) { bad++; console.log('FAIL control: a game with no maul (a wall that never breaks) passed'); } else console.log(`ok   control fails as it must: ${c.problems[0]}`);
}
process.exit(bad ? 1 : 0);
