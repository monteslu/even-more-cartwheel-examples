// skate board proportions: the deck must read as a thin plank from the game's camera, at least as thin as a real
// street deck in a photo. Measured on frames drawn with SKATE_BOARD_IDS=1 (src/view.c: the deck's underside and rails
// red, its grip magenta, the trucks blue, the wheels green, flat and unlit, the rider hidden), at native scale, the
// first skater coasting on Sunnyside's flat after its pushes (tools/pads/push.txt, tick 170).
//
// The views: the chase camera's distance, height and lens (4.2 m from the board, 18 degrees down, a 62 degree lens),
// from straight behind the run (where the chase camera sits on a straight run), 30 degrees round (a carve, the camera
// swinging after it) and side on (another rider across the park).
//
//   slim   the deck's length on screen over the mean height of its visible underside and rails (length squared over
//          the red pixels). An 11 mm deck with low kicktails shows a thin line of edge; a deeper edge, or tails kicked
//          up so far that their undersides face the camera, shows a band. Side on, a real deck in a CC0 photo
//          measures 72 at the same scale (internal findings); the board must too. Behind and 30 degrees round, the
//          board must reach 25 and 53.
//   open   (reported, not judged) of the board's screen box below the deck, the share that is background, not truck
//          or wheel.
//
// The control, SKATE_BOARD_CONTROL=1 (the earlier board: a 12 mm deck with tails kicked up 6 cm, box trucks, wide
// wheels), must fail every view.
//   node tests/skate-board.mjs                   (frames through the GPU suite lock, natively on the test GPU)
//   node tests/skate-board.mjs --measure a.png   (measures a frame drawn with SKATE_BOARD_IDS=1)
import { spawnSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readImage } from '../deps/cartwheel-engine/tools/lib/png.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const VIEWS = [['behind', 0, 25], ['carve', 30, 53], ['side', 90, 72]];

// a pixel's part: 1 deck (red), 2 grip (magenta), 3 truck (blue), 4 wheel (green), 0 anything else. The colours as
// they come out of the game's grade (unlit and not tone mapped, but the final grade still turns the green a little);
// a pixel within 60 of one of them is that part.
const ID = [[255, 0, 3], [254, 0, 247], [0, 0, 249], [107, 235, 38]];
function part(r, g, b) {
  for (let k = 0; k < 4; k++) if ((r - ID[k][0]) ** 2 + (g - ID[k][1]) ** 2 + (b - ID[k][2]) ** 2 < 3600) return k + 1;
  return 0;
}
// the frame's parts, kept only round the deck (the park has paint of its own): the box of the middle 96% of the deck's
// pixels, grown each way by half its longer side
function parts(path) {
  const { w, h, rgba } = readImage(path);
  const P = new Uint8Array(w * h), xs = [], ys = [];
  for (let i = 0; i < w * h; i++) {
    P[i] = part(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]);
    if (P[i] === 1 || P[i] === 2) xs.push(i % w), ys.push((i / w) | 0);
  }
  if (!xs.length) return { w, h, P };
  xs.sort((a, b) => a - b), ys.sort((a, b) => a - b);
  const q = (a, f) => a[Math.min(a.length - 1, Math.floor(f * a.length))];
  const x0 = q(xs, 0.02), x1 = q(xs, 0.98), y0 = q(ys, 0.02), y1 = q(ys, 0.98), g = 0.5 * Math.max(x1 - x0, y1 - y0) + 4;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) if (x < x0 - g || x > x1 + g || y < y0 - g || y > y1 + g) P[y * w + x] = 0;
  return { w, h, P };
}

export function measure(path) {
  const { w, h, P } = parts(path);
  const deck = (p) => p === 1 || p === 2;
  // slim: the deck's long axis (principal components of its pixels), its length along it, the red pixels over that
  let n = 0, sx = 0, sy = 0, edge = 0;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const p = P[y * w + x]; if (deck(p)) { n++; sx += x; sy += y; if (p === 1) edge++; } }
  if (!n) return { slim: NaN, open: NaN, note: 'no deck in the frame' };
  const mx = sx / n, my = sy / n;
  let cxx = 0, cyy = 0, cxy = 0;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) if (deck(P[y * w + x])) { cxx += (x - mx) ** 2; cyy += (y - my) ** 2; cxy += (x - mx) * (y - my); }
  const a = 0.5 * Math.atan2(2 * cxy, cxx - cyy), ux = Math.cos(a), uy = Math.sin(a);
  let lo = Infinity, hi = -Infinity;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) if (deck(P[y * w + x])) { const t = (x - mx) * ux + (y - my) * uy; lo = Math.min(lo, t); hi = Math.max(hi, t); }
  const len = hi - lo + 1, edgeH = edge / len;
  // open: each column from the deck's lowest pixel down to the board's lowest row
  let x0 = w, x1 = -1, yb = -1;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) if (P[y * w + x]) { x0 = Math.min(x0, x); x1 = Math.max(x1, x); yb = Math.max(yb, y); }
  let open = 0, all = 0;
  for (let x = x0; x <= x1; x++) {
    let d = -1;
    for (let y = 0; y < h; y++) if (deck(P[y * w + x])) d = y;
    if (d < 0) continue;
    for (let y = d + 1; y <= yb; y++) { all++; if (!P[y * w + x]) open++; }
  }
  return { slim: len / Math.max(edgeH, 0.5), len, edgeH, open: all ? open / all : NaN };
}
const show = (m) => `slim ${m.slim.toFixed(1)} (${m.len?.toFixed(0)} px long, its edge ${m.edgeH?.toFixed(2)} px), open ${m.open.toFixed(3)}`;

const args = process.argv.slice(2);
if (args[0] === '--measure') {
  for (const f of args.slice(1)) console.log(`${f}: ${show(measure(f))}`);
  process.exit(0);
}
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'skate-board', 'node', fileURLToPath(import.meta.url), ...args],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const out = join(ROOT, 'out/skate-board');
mkdirSync(out, { recursive: true });
const PAD = join(ROOT, 'games/skate/tools/pads/push.txt');
const base = ['SKATE_PLAYERS=1', 'SKATE_MODE=0', 'CW_RENDER_SCALE=1.5', `SKATE_PAD=${PAD}`, 'SKATE_BOARD_IDS=1'];
const runs = [['board', []], ['control', ['SKATE_BOARD_CONTROL=1']]];
const spec = [];
for (const [name, extra] of runs)
  for (const [view, az] of VIEWS) spec.push(...(spec.length ? ['--'] : []), join(out, `${name}-${view}.png`), 'skate_auto', '170', ...base, ...extra, `SKATE_VIEWCAM=${az},18,4.2,62,0.1`);
const r = spawnSync('node', [join(ROOT, 'games/skate/tools/strip.mjs'), ...spec], { encoding: 'utf8' });
if (r.status) { console.log(`FAIL the frames did not run (status ${r.status})\n${(r.stdout || '').slice(-1500)}`); process.exit(1); }
let bad = 0;
for (const [name] of runs) {
  let passes = 0;
  for (const [view, , min] of VIEWS) {
    const m = measure(join(out, `${name}-${view}.png.d/t00170.png`)), ok = m.slim >= min;
    passes += ok;
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${name} ${view}: ${show(m)}; slim ${min} or more`);
  }
  if (name === 'board' && passes < VIEWS.length) bad = 1;
  if (name === 'control' && passes > 0) { console.log('FAIL the control (the earlier board) passed a view: the measure cannot tell'); bad = 1; }
}
console.log(bad ? 'FAIL' : 'ok   the board reads as a thin deck from the chase camera; the control fails every view');
process.exit(bad);
