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
import { readPng } from './lanternfall-look.mjs';

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

const painted = [];
function shot(out, env, gpu = 'gl') {
  const e = { ...process.env, CW_FRAMES: String(env.frames || 90), CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_RENDER_SCALE: String(env.scale || 1.5),
    CW_SCREENSHOT: out, CW_GPU: gpu, LANTERNFALL_IDLE: '1', LANTERNFALL_NO_HUD: '1', ...env.vars };
  if (env.play) delete e.LANTERNFALL_IDLE; /* (the bot plays) */
  return new Promise((ok) => {
    const p = spawn(join(BIN, 'lanternfall_auto'), [], { env: e, stdio: ['ignore', 'ignore', 'pipe'] });
    let err = '';
    p.stderr.on('data', (d) => (err += d));
    p.on('exit', (c) => { const m = /creatures painted in (\d+) ms/.exec(err); if (m) painted.push(+m[1]); ok(c); });
  });
}

/** every sheet: name -> { vars, scale, frames } */
export function sheets(pick) {
  const S = {};
  const room = (r) => String(pick[r]);
  for (const r of [0, 2, 4, 5, 9, 11]) S[`enemies-r${r}`] = { vars: { LANTERNFALL_ROOM: room(r), LANTERNFALL_SHOWCASE: 'e' } };
  for (const [n, rows, cols] of [['a', '0,1', '0,4'], ['b', '2,3', '0,4'], ['c', '0,1', '5,8'], ['d', '2,3', '5,8']])
    S[`enemies-near-${n}`] = { vars: { LANTERNFALL_ROOM: room(0), LANTERNFALL_SHOWCASE: 'e', LANTERNFALL_SHOWCASE_ZOOM: '2.0', LANTERNFALL_SHOWCASE_ROWS: rows, LANTERNFALL_SHOWCASE_COLS: cols } };
  S['enemies-4k'] = { scale: 3, vars: { LANTERNFALL_ROOM: room(0), LANTERNFALL_SHOWCASE: 'e' } };
  for (let f = 0; f < 4; f++) S[`enemies-motion-${f}`] = { frames: 90 + f * 5, vars: { LANTERNFALL_ROOM: room(0), LANTERNFALL_SHOWCASE: 'e', LANTERNFALL_SHOWCASE_ZOOM: '2.0', LANTERNFALL_SHOWCASE_ROWS: '0,1', LANTERNFALL_SHOWCASE_COLS: '0,4' } };
  S['enemies-r0-empty'] = { vars: { ...S['enemies-r0'].vars, LANTERNFALL_SHOWCASE_EMPTY: '1' } };
  S['wardens-s0-empty'] = { vars: { LANTERNFALL_ROOM: room(9), LANTERNFALL_SHOWCASE: 'b', LANTERNFALL_SHOWCASE_STATE: '0', LANTERNFALL_SHOWCASE_EMPTY: '1' } };
  S['enemies-r0-mask'] = { vars: { ...S['enemies-r0'].vars, LANTERNFALL_SHOWCASE_MASK: '1' } };
  S['wardens-s0-mask'] = { vars: { LANTERNFALL_ROOM: room(9), LANTERNFALL_SHOWCASE: 'b', LANTERNFALL_SHOWCASE_STATE: '0', LANTERNFALL_SHOWCASE_MASK: '1' } };
  for (let st = 0; st < 4; st++) S[`wardens-s${st}`] = { vars: { LANTERNFALL_ROOM: room(9), LANTERNFALL_SHOWCASE: 'b', LANTERNFALL_SHOWCASE_STATE: String(st) } };
  S['heroes'] = { vars: { LANTERNFALL_ROOM: room(0), LANTERNFALL_SHOWCASE: 'h' } };
  S['heroes-near'] = { vars: { LANTERNFALL_ROOM: room(0), LANTERNFALL_SHOWCASE: 'h', LANTERNFALL_SHOWCASE_ZOOM: '2.2' } };
  S['heroes-4k'] = { scale: 3, vars: { LANTERNFALL_ROOM: room(0), LANTERNFALL_SHOWCASE: 'h', LANTERNFALL_SHOWCASE_ZOOM: '2.2' } };
  for (const f of [700, 1300, 1900, 2400]) S[`play-${f}`] = { play: true, frames: f, vars: {} };
  for (let i = 0; i < 6; i++) S[`warden${i}-near`] = { vars: { LANTERNFALL_ROOM: room([3, 2, 4, 5, 7, 12][i] ?? 0), LANTERNFALL_SHOWCASE: 'b', LANTERNFALL_SHOWCASE_ONLY: String(i), LANTERNFALL_SHOWCASE_ZOOM: '1.8', LANTERNFALL_SHOWCASE_STATE: '1' } };
  return S;
}

let dir = opt('--dir');
if (!dir) {
  dir = opt('--capture') || join(ROOT, 'out/creatures');
  mkdirSync(dir, { recursive: true });
  const pick = rooms();
  for (let r = 0; r < 13; r++) if (pick[r] === undefined) pick[r] = pick[0];
  /* the check needs four sheets and the same four with the old flat shapes; --capture draws every sheet */
  const CHECK = ['enemies-r0', 'enemies-r0-empty', 'enemies-r0-mask', 'wardens-s0', 'wardens-s0-empty', 'wardens-s0-mask'];
  const only = opt('--only'), all = sheets(pick);
  const list = Object.entries(all).filter(([n]) => (argv.includes('--capture') ? !only || n.includes(only) : CHECK.includes(n)));
  if (!argv.includes('--capture')) {
    mkdirSync(join(dir, 'flat'), { recursive: true });
    for (const n of CHECK.filter((c) => !c.endsWith('-mask'))) list.push([`flat/${n}`, { ...all[n], vars: { ...all[n].vars, LANTERNFALL_CREATURES_FLAT: '1' } }]);
  }
  for (let i = 0; i < list.length; i += 2)
    await Promise.all(list.slice(i, i + 2).map(([n, s]) => shot(join(dir, `${n}.png`), s, opt('--gpu') || 'gl')));
  console.log(`captured ${list.length} sheets into ${dir}; creatures painted in ${painted.length ? Math.min(...painted) : '?'} to ${painted.length ? Math.max(...painted) : '?'} ms`);
  if (argv.includes('--capture')) process.exit(0);
}
/* ── the measurement: each creature's own pixels (the sheet against the same frame with no creature in it), in the
 * game-scale sheets (1080p): how many distinct colours (4 bits a channel) and how much fine detail (the mean absolute
 * difference of luma from its 3 x 3 neighbourhood, inside the creature) it is painted with. Bounds: 120 colours and
 * 4 levels of detail for every enemy in every state and every warden. CONTROL, which must fail: the old flat shapes
 * (LANTERNFALL_CREATURES_FLAT). */
const KINDS = ['crawler', 'spitter', 'moth', 'hopper', 'knight', 'puff', 'dropper', 'burrower', 'eye'], WARDENS = ['Moss Warden', 'Glass Mantis', 'Brass Sentinel', 'Tide Serpent', 'Sky Warden', 'Heart of Thorns'];
function boxes(kind) {
  const T = 60, out = [];
  if (kind === 'e')
    for (let r = 0; r < 4; r++)
      for (let k = 0; k < KINDS.length; k++) {
        const x = 960 + (k - (KINDS.length - 1) / 2) * 3 * T, y = 540 + ((r - 1.5) * 2.5 + 1 - (k === 2 ? 0.5 : 0)) * T;
        out.push({ name: `${KINDS[k]} ${['idle', 'tell', 'act', 'hurt'][r]}`, x0: x - 1.4 * T, x1: x + 1.4 * T, y0: y - 2.6 * T, y1: y + 0.3 * T });
      }
  else
    for (let i = 0; i < 6; i++) {
      const x = 960 + ((i % 3) - 1) * 9.5 * T, y = 540 + (Math.floor(i / 3) * 8 - 2) * T;
      out.push({ name: WARDENS[i], x0: x - 3.2 * T, x1: x + 3.2 * T, y0: y - 5.8 * T, y1: y + 0.5 * T });
    }
  return out;
}
/** a: the frame, b: the frame with no creature, m: the creature drawn as magenta silhouettes (null: a against b) */
export function measureCreature(a, b, box, m = null) {
  const { w, px } = a, q = b.px, mp = m && m.px, x0 = Math.max(1, Math.round(box.x0)), x1 = Math.min(w - 2, Math.round(box.x1)), y0 = Math.max(1, Math.round(box.y0)), y1 = Math.min(a.h - 2, Math.round(box.y1));
  const on = mp
    ? (x, y) => { const k = (y * w + x) * 3; return mp[k] - mp[k + 1] > 100 && mp[k + 2] - mp[k + 1] > 100; }
    : (x, y) => { const k = (y * w + x) * 3; return Math.abs(px[k] - q[k]) + Math.abs(px[k + 1] - q[k + 1]) + Math.abs(px[k + 2] - q[k + 2]) > 30; };
  const L = (x, y) => { const k = (y * w + x) * 3; return 0.2126 * px[k] + 0.7152 * px[k + 1] + 0.0722 * px[k + 2]; };
  const cols = new Set();
  let n = 0, det = 0, nd = 0;
  for (let y = y0; y <= y1; y++)
    for (let x = x0; x <= x1; x++) {
      if (!on(x, y)) continue;
      n++;
      const k = (y * w + x) * 3;
      cols.add((px[k] >> 4) << 8 | (px[k + 1] >> 4) << 4 | px[k + 2] >> 4);
      let all = true, s = 0;
      for (let j = -1; j <= 1 && all; j++) for (let i = -1; i <= 1; i++) { if (!on(x + i, y + j)) { all = false; break; } s += L(x + i, y + j); }
      if (all) det += Math.abs(L(x, y) - s / 9), nd++;
    }
  return { n, colours: cols.size, detail: nd ? det / nd : 0 };
}
function judge(dirA, tag) {
  let bad = 0;
  for (const [sheet, kind] of [['enemies-r0', 'e'], ['wardens-s0', 'b']]) {
    const a = readPng(join(dirA, `${sheet}.png`)), b = readPng(join(dirA, `${sheet}-empty.png`));
    const mf = join(dirA, `${sheet}-mask.png`), m = existsSync(mf) ? readPng(mf) : null;
    for (const box of boxes(kind)) {
      const r = measureCreature(a, b, box, m), fail = r.n < 400 || r.colours < 120 || r.detail < 4;
      if (fail) bad++;
      if (tag === 'art') console.log(`${fail ? 'FAIL' : 'ok  '} ${box.name.padEnd(16)} ${String(r.n).padStart(6)} px  ${String(r.colours).padStart(4)} colours  detail ${r.detail.toFixed(1)}`);
    }
  }
  return bad;
}
const need = ['enemies-r0', 'enemies-r0-empty', 'wardens-s0', 'wardens-s0-empty'];
let failed = 0;
if (need.every((n) => existsSync(join(dir, `${n}.png`)))) {
  failed += judge(dir, 'art');
  const flat = join(dir, 'flat');
  if (need.every((n) => existsSync(join(flat, `${n}.png`)))) {
    const caught = judge(flat, 'control');
    const total = KINDS.length * 4 + WARDENS.length;
    if (caught >= total - 4) console.log(`ok   control: ${caught} of ${total} old flat creatures fail`);
    else { console.log(`CONTROL PASSED (a failure): only ${caught} of ${total} old flat creatures fail`); failed++; }
  } else { console.log(`no control frames in ${flat}`); failed++; }
} else console.log(`${need.join(', ')} are needed in ${dir} to measure`), failed++;
process.exit(failed ? 1 : 0);
