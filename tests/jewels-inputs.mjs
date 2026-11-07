// Writes jewels' input scripts for its gates (tests/jewels-gates.json): a person's game on the pad and one on touch
// and the mouse, each with a control that must fail. The board a game deals depends on the frame Play is pressed
// (the seed mixes in the ticks), so this runs the game natively up to its first board, reads that board and the
// generator's state (debug fields game.grid, game.rng), plans moves on a copy of the game's rules (the same as
// games/jewels/src/rules.c and game.c, which tests/jewels-rules.mjs holds to the original), and times each move to
// the frame its cascade comes to rest.
//
//   node tests/jewels-inputs.mjs        (needs build/native: the native game; uses the GPU, so through the GPU lock)
//
// Writes tests/jewels-pad.json, jewels-pad-control.json, jewels-touch.json, jewels-touch-control.json and prints the
// state each script ends in (the gates' expect blocks).
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const OUT = join(ROOT, 'out', 'jewels-inputs');
mkdirSync(OUT, { recursive: true });
const W = 10, H = 8, KINDS = 6, CELL = 120, BX = 72, BY = 60;
const T_SWAP = 13, T_BUMP = 18, T_CLEAR = 24, FALL_MIN = 16, FALL_ROW = 3, FALL_MAX = 34;
const cx = (x) => Math.round(BX + (x + 0.5) * CELL), cy = (y) => Math.round(BY + (y + 0.5) * CELL);

// ── the rules (rules.c) ──
class Rng {
  constructor(s) { this.s = s >>> 0 || 0x9e3779b9; }
  u32() { let x = this.s; x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; this.s = x; return x; }
  int(n) { return (this.u32() % n) + 1; }
}
function matches(g) {
  const hit = Array.from({ length: H }, () => Array(W).fill(0));
  let n = 0;
  const runs = [];
  const scan = (horiz, line) => {
    const len = horiz ? W : H;
    let run = 1;
    for (let i = 1; i <= len; i++) {
      const a = i < len ? (horiz ? g[line][i] : g[i][line]) : -1, b = horiz ? g[line][i - 1] : g[i - 1][line];
      if (a >= 0 && a === b && a !== 0) { run++; continue; }
      if (run >= 3) {
        for (let j = i - run; j < i; j++) { const x = horiz ? j : line, y = horiz ? line : j; if (!hit[y][x]) hit[y][x] = 1, n++; }
        runs.push(run);
      }
      run = 1;
    }
  };
  for (let y = 0; y < H; y++) scan(true, y);
  for (let x = 0; x < W; x++) scan(false, x);
  return { hit, n, runs };
}
const swap = (g, x1, y1, x2, y2) => { const t = g[y1][x1]; g[y1][x1] = g[y2][x2]; g[y2][x2] = t; };
function legal(g, x1, y1, x2, y2) {
  if (x2 < 0 || y2 < 0 || x2 >= W || y2 >= H) return false;
  swap(g, x1, y1, x2, y2);
  const ok = matches(g).n > 0;
  swap(g, x1, y1, x2, y2);
  return ok;
}
function moves(g) {
  const out = [];
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      if (x < W - 1 && legal(g, x, y, x + 1, y)) out.push({ x, y, dx: 1, dy: 0 });
      if (y < H - 1 && legal(g, x, y, x, y + 1)) out.push({ x, y, dx: 0, dy: 1 });
    }
  return out;
}
const runScore = (n, c) => Math.floor((30 * n + 20 * Math.max(0, n - 3) * n) * (c + 1) / 2);
const fallTicks = (rows) => Math.min(FALL_MAX, FALL_MIN + FALL_ROW * Math.max(0, rows - 1));
// a legal swap played out: the game's ticks until it is idle again, and what it scored
function play(st, m) {
  const g = st.g;
  swap(g, m.x, m.y, m.x + m.dx, m.y + m.dy);
  let ticks = T_SWAP, cascade = 0;
  for (;;) {
    const mm = matches(g);
    if (!mm.n) break;
    cascade++;
    for (const r of mm.runs) st.score += runScore(r, cascade);
    ticks += T_CLEAR;
    let longest = 0;
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (mm.hit[y][x]) g[y][x] = 0;
    for (let x = 0; x < W; x++) {
      let write = H - 1;
      for (let y = H - 1; y >= 0; y--) {
        if (!g[y][x]) continue;
        if (write !== y) { g[write][x] = g[y][x]; g[y][x] = 0; longest = Math.max(longest, fallTicks(write - y)); }
        write--;
      }
      let above = 0;
      for (let y = write; y >= 0; y--) { above++; g[y][x] = st.rng.int(KINDS); longest = Math.max(longest, fallTicks(y + above)); }
    }
    ticks += longest + 4;
  }
  if (moves(g).length === 0) throw new Error('the plan reached a board with no move (a reshuffle): pick another move');
  st.moves++, st.cascadeMax = Math.max(st.cascadeMax, cascade);
  return ticks;
}

// ── the first board ──
const START = [{ frame: 20, buttons: ['south'] }, { frame: 24 }];
const FIRST = 100;
const startFile = join(OUT, 'start.json'), stateFile = join(OUT, 'start-state.json');
writeFileSync(startFile, JSON.stringify([...START, { frame: FIRST }]));
const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs'), '--host', 'native', '--app', 'jewels', '--frames', String(FIRST),
  '--input', startFile, '--state', stateFile], { encoding: 'utf8', env: { ...process.env, CW_PROJECT_ROOT: ROOT } });
if (r.status !== 0) { console.log(`FAIL the native run: ${r.stderr || r.stdout}`); process.exit(1); }
const s0 = JSON.parse(readFileSync(stateFile, 'utf8'));
const bytes = Array.isArray(s0['game.grid']) ? s0['game.grid'] : String(s0['game.grid']).match(/../g).map((h) => parseInt(h, 16));
if (s0['game.playing'] !== 1 || bytes.length !== W * H) { console.log(`FAIL no game at frame ${FIRST}: ${JSON.stringify(s0).slice(0, 300)}`); process.exit(1); }
const fresh = () => ({ g: Array.from({ length: H }, (_, y) => bytes.slice(y * W, y * W + W)), rng: new Rng(s0['game.rng']), score: 0, moves: 0, cascadeMax: 0,
  cur: { x: s0['game.cur_x'], y: s0['game.cur_y'] } });

// ── script writing ──
class Script {
  constructor() { this.e = [...START]; this.f = FIRST; }
  hold(state, frames) { this.e.push({ frame: this.f, ...state }); this.f += frames; this.e.push({ frame: this.f }); this.f += 1; }
  pad(b, gap = 6) { this.hold({ buttons: [b] }, 3); this.f += gap; }
  axis(ax, v, gap = 6) { this.hold({ axes: { [ax]: v } }, 4); this.f += gap; }
  tap(id, x, y, gap = 10) { this.hold({ touches: [{ id, x, y }] }, 3); this.f += gap; }
  click(x, y, gap = 10) { this.hold({ mouse: { x, y, buttons: ['left'] } }, 3); this.f += gap; }
  drag(id, x0, y0, x1, y1, gap = 10) {
    for (let k = 0; k <= 10; k++) this.e.push({ frame: this.f + k, touches: [{ id, x: Math.round(x0 + (x1 - x0) * k / 10), y: Math.round(y0 + (y1 - y0) * k / 10) }] });
    this.f += 11; this.e.push({ frame: this.f }); this.f += 1 + gap;
  }
  wait(n) { this.f += n; }
  end(n = 30) { this.f += n; this.e.push({ frame: this.f }); return this.e; }
}
// the pad walks the cursor to a cell (the d-pad, or the stick)
function walk(sc, st, x, y, stick = false) {
  while (st.cur.x !== x) { const d = x > st.cur.x ? 1 : -1; if (stick) sc.axis('lx', d); else sc.pad(d > 0 ? 'right' : 'left'); st.cur.x += d; }
  while (st.cur.y !== y) { const d = y > st.cur.y ? 1 : -1; if (stick) sc.axis('ly', d); else sc.pad(d > 0 ? 'down' : 'up'); st.cur.y += d; }
}
const dirOf = (m) => (m.dx > 0 ? 'right' : m.dx < 0 ? 'left' : m.dy > 0 ? 'down' : 'up');
const pickMove = (st, k) => { const ms = moves(st.g); return ms[(k * 7 + 3) % ms.length]; };
function illegalPair(st) {
  for (let y = 0; y < H; y++) for (let x = 0; x < W - 1; x++) if (!legal(st.g, x, y, x + 1, y)) return { x, y, dx: 1, dy: 0 };
  throw new Error('no illegal pair');
}

// ── the pad game ──
function padGame(control) {
  const sc = new Script(), st = fresh();
  const tally = { illegal: 0, hints: 0, picks: 0 };
  for (let k = 0; k < 5; k++) {
    const m = pickMove(st, k);
    walk(sc, st, m.x, m.y, k === 2); /* the third move's cursor goes by the stick */
    sc.pad(control ? 'west' : k % 2 ? 'east' : 'south', 8); /* pick it up (the control: the hint button instead) */
    tally.picks++;
    if (control) { tally.hints++; tally.picks--; const ms = moves(st.g); st.rng.int(ms.length); }
    sc.pad(dirOf(m), 0);
    if (!control) sc.wait(play(st, m) + 12);
    else sc.wait(12);
    st.cur = { x: Math.max(0, Math.min(W - 1, st.cur.x + m.dx)), y: Math.max(0, Math.min(H - 1, st.cur.y + m.dy)) };
    if (k === 1) { /* a hint, then an illegal swap that springs back */
      sc.pad('west', 20); tally.hints++;
      const ms = moves(st.g); st.rng.int(ms.length);
      const b = illegalPair(st);
      walk(sc, st, b.x, b.y);
      sc.pad('south', 8); tally.picks++; sc.pad('right', 0); tally.illegal++; sc.wait(T_BUMP + 12);
      st.cur.x = Math.min(W - 1, st.cur.x + 1);
    }
    if (k === 3) { sc.pad('south', 8); sc.pad('south', 12); tally.picks++; } /* pick up and put down again */
  }
  return { script: sc.end(), st, tally };
}

// ── the touch game (the pad only presses Play) ──
function touchGame(control) {
  const sc = new Script(), st = fresh();
  const tally = { taps: 0, drags: 0, hints: 0 };
  /* the control: every touch and click on the frame under the board, so nothing is picked and no swap lands */
  const Y = (y) => (control ? 1062 : y);
  for (let k = 0; k < 6; k++) {
    const m = pickMove(st, k + 11);
    const ax = cx(m.x), ay = Y(cy(m.y)), bx = cx(m.x + m.dx), by = Y(cy(m.y + m.dy));
    if (k % 3 === 0) { sc.tap(1 + k, ax, ay, 12); sc.tap(9 - k, bx, by, 0); tally.taps++; }       /* tap, tap: two fingers' slots */
    else if (k % 3 === 1) { sc.drag(2 + k, ax, ay, bx, by, 0); tally.drags++; }                    /* drag it over */
    else { sc.click(ax, ay, 12); sc.click(bx, by, 0); tally.taps++; }                               /* the mouse */
    if (!control) sc.wait(play(st, m) + 12);
    else sc.wait(20);
    if (k === 2) { sc.tap(5, 1600, Y(932), 20); if (!control) { tally.hints++; const ms = moves(st.g); st.rng.int(ms.length); } }
  }
  /* pick a stone up by tapping it, and put it down by tapping it again */
  const m = pickMove(st, 40);
  sc.tap(3, cx(m.x), Y(cy(m.y)), 12);
  sc.tap(3, cx(m.x), Y(cy(m.y)), 12);
  return { script: sc.end(), st, tally };
}

const out = {};
for (const [name, fn, control] of [['jewels-pad', padGame, false], ['jewels-pad-control', padGame, true], ['jewels-touch', touchGame, false],
  ['jewels-touch-control', touchGame, true]]) {
  const { script, st, tally } = fn(control);
  writeFileSync(join(ROOT, 'tests', `${name}.json`), '[' + script.map((e) => JSON.stringify(e)).join(',\n ') + ']\n');
  out[name] = { frames: script[script.length - 1].frame, moves: st.moves, score: st.score, cascade_max: st.cascadeMax, ...tally };
  console.log(`${name}: ${JSON.stringify(out[name])}`);
}
