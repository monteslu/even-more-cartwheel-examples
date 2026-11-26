// emberthrone grass walk: the hero walks through the meadow's grass on a pad, as a player does (Start, Start, skip held
// through the guide's welcome, the debug channel's "from 1" puts her on the meadow road, then the stick off the road to the right into the grass and
// through it at a walk, easing right and left, four seconds), the whole run one ranged romdev call (frame record, 960x540), three times on the same schedule: the grass
// as it is, the grass as 0.51.0 drew it (ADV_PROBE_OLDTUFTS) and no grass (ADV_PROBE_NOGRASS, the ground moving under
// the same camera). On the pixels where the grass shows (luma 16 or more from the no-grass frame):
//   flicker  the mean |L(t+1) - 2 L(t) + L(t-1)| against the no-grass run's on the same pixels (the excess is the
//            grass's own: a blade thinner than a pixel lit then not, a tuft that pops)
//   pops     each frame's share of the picture where the grass steps by 40 or more of 255 in one frame and holds still
//            the frame before and the frame after (a tuft that appears, vanishes or switches LOD at once, not one
//            swaying or the view moving), where the ground does not step; the worst frame and the mean
// Bars (PASS needs all): flicker excess at most 1.2 times the 0.51.0 grass's on the same ground and walk, grass on 2%
// or more of the picture; the pops are reported beside it. CONTROL, which must fail: the grass as 0.59.0 set it (the
// engine's GRASS tuft, seven a square metre out to 16 m, shrinking away, each tuft varied by 0.35, no turf:
// ADV_PROBE_GRASSSET), whose field speckled in motion. The 0.51.0 grass and the grass cut off hard at 9 m with no fade
// (ADV_PROBE_GRASSPOP) are reported beside it.
//
//   node tests/emberthrone-grass-walk.mjs [cart dir] [--gpu gl|webgpu] [--keep <dir>]
import { mkdirSync, openSync, readSync, closeSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const cartDir = argv.find((a, i) => !a.startsWith('--') && !(i && argv[i - 1].startsWith('--')));
if (cartDir) process.env.DESTRUCTO_CART_DIR = resolve(cartDir);
const { call, cartPath } = await import('./lib/romdev.mjs');
const gpu = opt('--gpu', 'gl');
const OUT = resolve(opt('--keep', join(ROOT, `out/grass-walk-${gpu}`)));
mkdirSync(OUT, { recursive: true });
const WARM = 150, FRAMES = 240, MASK = 16, POP = 40;

// the pad: the title, the save slot, the welcome skipped, then walk (frames from the call's first, warm-up included)
function steps() {
  const S = [];
  const add = (frames, port = {}) => S.push({ input: { ports: [port] }, frames });
  // (the guide's welcome plays on a new game: hold skip through it, as a player who has seen it does, frames 50 to 120)
  add(20); add(2, { start: true }); add(18); add(2, { start: true }); add(8); add(70, { east: true }); add(30);
  add(60, { axes: { lx: 0.55, ly: -0.2 } }); // off the road to the right, into the meadow
  for (let k = 0; k < 18; k++) { const t = k / 18; add(10, { axes: { lx: 0.25 * Math.sin(t * Math.PI * 2), ly: -0.3 } }); } // through it, easing right and left
  return S;
}

async function run(variant, env) {
  const session = `emberthrone-grass-walk-${process.pid}-${variant}`;
  const loaded = await call('loadMedia', { platform: 'wasmcart', path: cartPath('emberthrone'), gpuPower: 'low-power', wgpu: gpu === 'webgpu', deterministicSeed: 1,
    firstCommand: `env ADV_NO_HUD=1${env ? ' ' + env : ''}` }, session);
  if (loaded.gpu && loaded.gpu !== gpu) throw new Error(`asked for ${gpu}, the cart runs on ${loaded.gpu}`);
  try {
    const file = join(OUT, `${variant}.cwfd`);
    await call('frame', { op: 'record', path: file, frames: FRAMES, warmup: WARM, downscale: 2, steps: steps(), commandsAt: [{ frame: 60, command: 'from 1' }], maxSeconds: 1500 }, session, { timeoutMs: 1800000 });
    return file;
  } finally { await call('host', { op: 'shutdown' }, session).catch(() => {}); }
}

function frames(file) {
  const fd = openSync(file, 'r'), head = Buffer.alloc(16);
  readSync(fd, head, 0, 16, 0);
  const w = head.readUInt32LE(4), h = head.readUInt32LE(8), n = w * h, buf = Buffer.alloc(n * 3), out = [];
  for (let pos = 16; ; pos += n * 3) {
    if (readSync(fd, buf, 0, n * 3, pos) < n * 3) break;
    const L = new Float32Array(n);
    for (let i = 0; i < n; i++) L[i] = 0.2126 * buf[i * 3] + 0.7152 * buf[i * 3 + 1] + 0.0722 * buf[i * 3 + 2];
    out.push(L);
  }
  closeSync(fd);
  return { w, h, f: out };
}

export function measure(a, none) {
  const n = Math.min(a.f.length, none.f.length), px = a.w * a.h;
  let sa = 0, sn = 0, cnt = 0, popMax = 0, popSum = 0, popAt = -1;
  // frames where the whole view jumps (the camera pulled in by a wall, a cut): judged by the ground run, left out
  const jump = [0];
  for (let t = 1; t < n; t++) { let d = 0; for (let i = 0; i < px; i += 7) d += Math.abs(none.f[t][i] - none.f[t - 1][i]); jump.push(d / (px / 7)); }
  const med = [...jump].sort((x, y) => x - y)[jump.length >> 1];
  let skipped = 0;
  for (let t = 1; t < n; t++) {
    if (t < 8) continue; // (the first frames: TAA's history settling)
    if (jump[t] > 3 * med + 2 || (t + 1 < n && jump[t + 1] > 3 * med + 2) || jump[t - 1] > 3 * med + 2) { skipped++; continue; }
    const A0 = a.f[t - 1], A1 = a.f[t], N0 = none.f[t - 1], N1 = none.f[t], A2 = t + 1 < n ? a.f[t + 1] : null, N2 = t + 1 < n ? none.f[t + 1] : null;
    let pops = 0;
    for (let i = 0; i < px; i++) {
      if (Math.abs(A1[i] - N1[i]) < MASK && Math.abs(A0[i] - N0[i]) < MASK) continue;
      // a pop: a step of POP or more this frame with the frame before and after it still (under 8), where the ground
      // there does not step: an appearing, vanishing or switching tuft, not a blade swaying or the view moving
      if (A2 && t >= 2 && Math.abs(A1[i] - A0[i]) >= POP && Math.abs(A0[i] - a.f[t - 2][i]) < 8 && Math.abs(A2[i] - A1[i]) < 8 && !(Math.abs(N1[i] - N0[i]) >= POP)) pops++;
      if (A2 && Math.abs(A1[i] - N1[i]) >= MASK) sa += Math.abs(A2[i] - 2 * A1[i] + A0[i]), sn += Math.abs(N2[i] - 2 * N1[i] + N0[i]), cnt++;
    }
    const share = pops / px;
    popSum += share;
    if (share > popMax) popMax = share, popAt = t;
  }
  return { excess: +((sa - sn) / Math.max(cnt, 1)).toFixed(2), flicker: +(sa / Math.max(cnt, 1)).toFixed(2), ground: +(sn / Math.max(cnt, 1)).toFixed(2),
    grassPx: +(cnt / Math.max(n - 2 - skipped, 1) / px).toFixed(4), skipped, popPeak: +popMax.toFixed(5), popMean: +(popSum / (n - 1)).toFixed(5), popFrame: popAt };
}
// the bar is the 0.51.0 grass's own flicker on the same ground and walk (the ground's swathes and macro tint raise every
// run's figure: a fixed bound would judge the ground, not the grass): at most 1.2 times it, on 2% of the picture or more
let OLD = null;
const judge = (r) => ({ ...r, checks: { flicker: OLD !== null && r.excess <= 1.2 * OLD, grass: r.grassPx >= 0.02 } }); // (pops reported beside it)
const pass = (r) => Object.values(r.checks).every(Boolean);

const none = frames(await run('none', 'ADV_PROBE_NOGRASS=1'));
const res = {};
const extra = (process.env.WALK_EXTRA || '').split(',').filter(Boolean).map((s) => s.split(':')); // (WALK_EXTRA=name:ENV=1,...: more variants, reported)
const raw = {};
const GRASS059 = 'ADV_PROBE_GRASSSET=mesh=;turf=0;density=7;visible_range=16;fade=6;lod_distance=7;fade_mode=SHRINK;tint_jitter=0.35;root_height=0';
for (const [v, env] of [['old', 'ADV_PROBE_OLDTUFTS=1'], ['grass', ''], ['control', GRASS059], ['hardcut', 'ADV_PROBE_GRASSPOP=1'], ...extra]) raw[v] = measure(frames(await run(v, env)), none);
OLD = raw.old.excess;
for (const [v, r] of Object.entries(raw)) res[v] = judge(r);
const ok = pass(res.grass) && !pass(res.control);
for (const [v, r] of Object.entries(res)) console.log(`${pass(r) ? 'pass' : 'FAIL'} ${v.padEnd(7)} ${gpu}: flicker ${r.flicker} against ${r.ground} with no grass, excess ${r.excess} (bound ${(1.2 * OLD).toFixed(2)}: 1.2 x the 0.51.0 grass's); pops worst ${(r.popPeak * 100).toFixed(3)}% of the picture (frame ${r.popFrame}), mean ${(r.popMean * 100).toFixed(4)}%; grass on ${(r.grassPx * 100).toFixed(1)}%`);
console.log(ok ? `grass walk ${gpu}: PASS (the control fails, as it must)` : `grass walk ${gpu}: FAIL${pass(res.control) ? ' (the control passed: the check cannot see it)' : ''}`);
writeFileSync(join(OUT, 'grass-walk.json'), JSON.stringify(res, null, 1));
if (!argv.includes('--keep')) rmSync(OUT, { recursive: true, force: true });
else if (!process.env.KEEP_DUMPS) for (const v of ['none', 'grass', 'old', 'control', 'hardcut', ...extra.map((e) => e[0])]) rmSync(join(OUT, `${v}.cwfd`), { force: true });
process.exit(ok ? 0 : 1);
