// pinball flipper gate: every flipper binding pressed through the real input path (the host's pad or keyboard, the
// engine's input snapshot, the game's tick), on the game as it ships. For every press it reads the game's flipper
// history (debug fields flip.*: the tick the coil came on, the tick the flipper first moved, its stroke's time to full,
// its drop's time back to rest, how low it sagged while held) and holds:
//   - every press is seen (none lost between ticks, a two-frame tap included)
//   - the flipper moves on the first simulation step after the press arrives (the step of the frame it arrives in)
//   - full stroke 15 to 25 ms after the coil comes on. A real machine reaches full stroke 30 to 50 ms after the button;
//     here a press waits for the next step (half a tick, 8.3 ms, on average) and a 60 Hz screen shows a step a frame
//     (16.7 ms) later at best, so 25 ms of stroke is the most that still looks like a real machine (8.3 + 16.7 + 25 =
//     50); a press that lands just after a step waits the whole tick (16.7 + 25 = 42 ms to full in the simulation)
//   - it holds at full while held (no sag) and is back at rest within two ticks (33.4 ms) of the release
// Bindings: the shoulders, the triggers, the d-pad's sides, the face buttons (west left, east right) and on native the
// keyboard (both shifts, Z and /, the arrows). The ball waits on the plunger meanwhile (the flippers work there too).
// The control: the same pad run with the buttons injected a tick late (PINBALL_TEST_FLIP_DELAY=1, native): it must fail.
//
//   node tests/pinball-flippers.mjs [--hosts native,romdev] [--gpu gl|webgpu] [--no-control]
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'pinball-flippers', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const hosts = opt('--hosts', 'native,romdev').split(',');
const gpu = opt('--gpu', 'gl');
const OUT = join(ROOT, 'out', 'gate', 'pinball-flippers');
mkdirSync(OUT, { recursive: true });

// a person's start (as tests/pinball-play.json): Play on the title, the pad joins, Start, the table with South; the
// ball then waits on the plunger
const START = [{ frame: 20, buttons: ['south'] }, { frame: 23 }, { frame: 40, buttons: ['south'] }, { frame: 43 },
  { frame: 60, buttons: ['start'] }, { frame: 63 }, { frame: 90, buttons: ['south'] }, { frame: 93 }];
const PAD = [
  ['shoulders', { buttons: ['l'] }, { buttons: ['r'] }],
  ['triggers', { axes: { lt: 1 } }, { axes: { rt: 1 } }],
  ['d-pad sides', { buttons: ['left'] }, { buttons: ['right'] }],
  ['face buttons', { buttons: ['west'] }, { buttons: ['east'] }],
];
const KEYS = [
  ['shifts', { keys: ['lshift'] }, { keys: ['rshift'] }],
  ['Z and /', { keys: ['z'] }, { keys: ['slash'] }],
  ['arrows', { keys: ['left'] }, { keys: ['right'] }],
];
// per binding: left held 14 frames, right held 14, both together 10, then a two-frame tap of each; a press is
// [binding, flipper side, frame, frames held]
function script(set) {
  const s = [...START], presses = [];
  let f = 120;
  const hold = (ins, frame, n) => { s.push({ frame, ...ins }); s.push({ frame: frame + n }); };
  for (const [name, L, R] of set) {
    hold(L, f, 14), presses.push([name, 0, f, 14]), f += 22;
    hold(R, f, 14), presses.push([name, 1, f, 14]), f += 22;
    const both = { buttons: [...(L.buttons || []), ...(R.buttons || [])], keys: [...(L.keys || []), ...(R.keys || [])], axes: { ...(L.axes || {}), ...(R.axes || {}) } };
    for (const k of ['buttons', 'keys']) if (!both[k].length) delete both[k];
    if (!Object.keys(both.axes).length) delete both.axes;
    hold(both, f, 10), presses.push([name, 0, f, 10], [name, 1, f, 10]), f += 18;
    hold(L, f, 2), presses.push([name, 0, f, 2]), f += 10;
    hold(R, f, 2), presses.push([name, 1, f, 2]), f += 12;
  }
  return { input: s, presses, frames: f + 10 };
}

function run(host, name, sc, env = {}) {
  const inFile = join(OUT, `${name}.input.json`), state = join(OUT, `${name}.state.json`);
  writeFileSync(inFile, JSON.stringify(sc.input));
  const r = spawnSync('node', [join(ENGINE, 'tools/run-cart.mjs'), '--host', host, '--app', 'pinball', '--gpu', gpu, '--frames', String(sc.frames),
    '--input', inFile, '--state', state], { encoding: 'utf8', env: { ...process.env, CW_PROJECT_ROOT: ROOT, ...env }, maxBuffer: 1 << 26 });
  if (r.status !== 0) return { error: `run-cart exited ${r.status}: ${(r.stderr || r.stdout || '').split('\n').filter(Boolean).slice(-4).join(' | ')}` };
  const st = JSON.parse(readFileSync(state, 'utf8'));
  return st.fields || st;
}

const FLH = 24;
function judge(s, sc) {
  if (s.error) return { why: [s.error], rows: [] };
  const why = [], rows = [];
  const frame = s['cw.frame'], ticks = s['flip.ticks'];
  // the frame a game tick ran in: one tick a frame in these runs and the game is never paused once it has begun
  // (the menus before it run no game ticks), so frame = the last frame minus the game ticks since
  const frameOf = (t) => frame - (ticks - t);
  for (const side of [0, 1]) {
    const mine = sc.presses.filter((p) => p[1] === side), n = s['flip.presses'][side];
    if (n !== mine.length) why.push(`${side ? 'right' : 'left'}: ${n} presses seen of ${mine.length}`);
    const k = Math.min(n, FLH), first = n - k; // the history keeps the last FLH presses
    for (let j = 0; j < k; j++) {
      const p = mine[first + j];
      if (!p) break;
      const at = side * FLH + j;
      const press = frameOf(s['flip.press_tick'][at]), move = s['flip.move_tick'][at] >= 0 ? frameOf(s['flip.move_tick'][at]) : NaN;
      const full = s['flip.full_ms'][at], drop = s['flip.drop_ms'][at], sag = s['flip.hold_min'][at];
      const steps = move - p[2] + 1; // 1: it moved in the step of the frame the press arrived in
      rows.push({ binding: p[0], side, at: p[2], held: p[3], seenFrame: press, steps, full, drop, sag });
      const tag = `${p[0]} ${side ? 'right' : 'left'} at frame ${p[2]}`;
      if (press !== p[2]) why.push(`${tag}: seen at frame ${press}`);
      if (!(steps <= 1)) why.push(`${tag}: moved ${Number.isNaN(steps) ? 'never' : `on step ${steps}`} after the press (1 at most)`);
      if (p[3] >= 6) {
        if (!(full >= 15 && full <= 25)) why.push(`${tag}: full stroke ${full.toFixed(1)} ms after the coil (15 to 25)`);
        if (!(sag >= 0.999)) why.push(`${tag}: sagged to ${(sag * 100).toFixed(1)}% while held`);
      }
      if (!(drop >= 0 && drop <= 33.4)) why.push(`${tag}: back at rest ${drop.toFixed(1)} ms after the release (33.4 at most)`);
    }
  }
  return { why, rows };
}
function summary(rows) {
  const by = new Map();
  for (const r of rows) {
    const b = by.get(r.binding) || { n: 0, steps: 0, full: [], drop: [] };
    b.n++, b.steps = Math.max(b.steps, r.steps);
    if (r.held >= 6) b.full.push(r.full);
    b.drop.push(r.drop);
    by.set(r.binding, b);
  }
  const mm = (a) => a.length ? `${Math.min(...a).toFixed(1)}-${Math.max(...a).toFixed(1)}` : '-';
  return [...by].map(([k, b]) => `${k}: ${b.n} presses, motion on step ${b.steps} at worst, full ${mm(b.full)} ms, back ${mm(b.drop)} ms`).join('\n       ');
}

let bad = 0;
const pad = script(PAD), keys = script(KEYS);
for (const host of hosts) {
  const sets = [['pad', pad], ...(host === 'native' ? [['keys', keys]] : [])]; // romdev has no keyboard op
  for (const [what, sc] of sets) {
    const name = `${host}-${gpu}-${what}`;
    const { why, rows } = judge(run(host, name, sc), sc);
    if (why.length) bad++, console.log(`FAIL ${name}: ${why.slice(0, 8).join('; ')}${why.length > 8 ? ` (+${why.length - 8} more)` : ''}`);
    else console.log(`ok   ${name}\n       ${summary(rows)}`);
  }
}
if (!argv.includes('--no-control') && hosts.includes('native')) {
  const { why } = judge(run('native', `control-${gpu}-delay`, pad, { PINBALL_TEST_FLIP_DELAY: '1' }), pad);
  if (why.length) console.log(`ok   control (the buttons a tick late) fails as it must: ${why[0]}`);
  else bad++, console.log('CONTROL PASSED (a failure): a tick of injected delay went unnoticed');
}
process.exit(bad ? 1 : 0);
