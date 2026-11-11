// lanternfall grounding: nothing painted in the backdrop floats. The sky layer's three parallax depths are families of
// painted shapes (src/art_par.h): trees, crystal spires, a colonnade's piers, machinery's stacks. Each family paints
// its own ground, a band from a straight ground line to the texture's bottom row, and the sky draws that ground on
// down to the canvas's bottom edge however far the depth lifts as the camera drops down a tall room (src/view.c
// draw_sky). Two measurements, both in pixels at 1080p:
//
//   texture   the family's pixels as the game paints them (natively, LANTERNFALL_PAR_DUMP), at every feature's foot (the
//             game lists them: lf_par "families"): the gap of clear rows between the foot's lowest painted row and
//             the ground line, scaled by the tallest depth that shows the family (0.79 of 1080 for 512 rows); and the
//             ground band itself, which must be painted solid under every column. Pass: no foot more than 2 px above
//             its ground, no hole in any ground band.
//   frames    the rendered frames, through the game's debug channel, on every host and backend asked for: in every
//             region, views at the bottoms of its tallest rooms (where the depths lift furthest), the top of the
//             tallest, two plain rooms, and the region's two ends. For each view the game reports where every
//             feature's foot lands on the canvas (lf_par "copies"), and the frames are the view drawn whole, drawn
//             with one depth alone and drawn with the hazy gradient alone (those two as the sky layer by itself). Under each foot (4 px below its lowest
//             painted row) the depth-alone frame must be ground, not sky: its luma at most 0.8 of the gradient's at
//             the same pixel. In the whole frame, at feet the game says nothing nearer covers (clear), the same pixel
//             must be darker than the gradient by 15% (the rooms are dense, so few feet are ever clear: that count is
//             reported, the depth-alone frames are the gate). All nine trees of the family must be judged on romdev,
//             six or more in each region that shows trees, and 50 feet at least.
//   CONTROL   the trees painted again with one trunk stopped 7 rows (12 px at 1080p) above its ground (lf_lift 7):
//             the texture measurement must report it, and in two views where that tree's foot is on the canvas the
//             depth-alone frame under the trunk must read as sky.
//
//   node tests/lanternfall-grounding.mjs [--hosts native,romdev] [--gpu gl|webgpu] [--quick] [--no-control]
//     native runs tools/run-cart.mjs per frame (the test GPU, pinned); romdev drives the cart on 127.0.0.1:7331 in one
//     session per backend with ranged calls. The native sweep takes two views per region, romdev the whole list.
//     Frames land in out/lanternfall-grounding-<gpu>-<hosts> and are deleted once measured (the failing ones are kept, up to 20).
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, unlinkSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request as httpRequest } from 'node:http';
import { readPng } from './lanternfall-look.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const hosts = opt('--hosts', 'native,romdev').split(',');
const gpu = opt('--gpu', 'gl');
const quick = argv.includes('--quick');
const control = !argv.includes('--no-control');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'lanternfall-grounding', 'node', fileURLToPath(import.meta.url), ...argv],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { wgpuPin } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const BIN = [join(ROOT, 'build/native/project/games/lanternfall'), join(ROOT, 'build/native/games/lanternfall')].find((d) => existsSync(join(d, 'lanternfall_auto')));
const CART = join(ROOT, 'build/cart-wgpu/carts/lanternfall_auto.wasc');
const OUT = join(ROOT, `out/lanternfall-grounding-${gpu}-${hosts.join('+')}`); /* (jobs for other hosts or backends run beside this one) */
rmSync(OUT, { recursive: true, force: true });
mkdirSync(join(OUT, 'tex'), { recursive: true });
mkdirSync(join(OUT, 'tex-lift'), { recursive: true });
const NAMES = ['Lantern Gate', 'Rootway', 'Glasswater', 'Fen', 'Clockwork', 'Tidal Halls', 'Starfall', 'Skyreach', 'Archive', 'Forge', 'Frostvault', 'Bloom', 'Heartwood'];
const FAMILY = ['ridges', 'piers', 'trees', 'spires', 'stacks', 'hanging growth'];
const TEX_W = 1024, TEX_H = 512, TALLEST = 0.79 * 1080; /* the depth that shows a family tallest: 0.79 of the canvas for the middle one */
const LIFT = 7; /* the control's rows */
const ROWS_TO_PX = TALLEST / TEX_H;
const PASS_PX = 2;
let bad = 0;
const fail = (m) => { bad++; console.log(`FAIL ${m}`); };
const ok = (m) => console.log(`ok   ${m}`);

// ── the painter's own account: the families and the rooms (one native run, no GPU to speak of) ──
function paintRun(dir, extra = {}) {
  const env = { ...process.env, LIBGL_ALWAYS_SOFTWARE: '1', SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl', CW_FRAMES: '2', CW_WARMUP_SPREAD: '0',
    LANTERNFALL_IDLE: '1', LANTERNFALL_PAR_DUMP: dir, CW_DEBUG_CMDS: 'lf_par;lf_rooms', ...extra };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  const r = spawnSync(join(BIN, 'lanternfall_auto'), [], { env, encoding: 'utf8', timeout: 300000, maxBuffer: 1 << 26 });
  const lines = ((r.stdout || '') + (r.stderr || '')).split('\n').filter((l) => l.startsWith('{'));
  if (r.status !== 0 || lines.length < 2) throw new Error(`the painter's run failed (status ${r.status}): ${(r.stderr || '').slice(-800)}`);
  return { par: JSON.parse(lines[0]), rooms: JSON.parse(lines[1]) };
}
const paint = paintRun(join(OUT, 'tex'));
const families = paint.par.families.filter((f) => f.all.length);
const rooms = paint.rooms.rooms, [CELL_W, CELL_H] = paint.rooms.cell;
const feetOf = (tex) => (families.find((f) => f.tex === tex) || { all: [] }).all;

// ── texture: every foot against its ground line, the ground band under every column ──
function readTex(dir, tex) {
  const b = readFileSync(join(dir, `par${tex}.rgba`));
  if (b.length !== TEX_W * TEX_H * 4) throw new Error(`${dir}/par${tex}.rgba: ${b.length} bytes`);
  return b;
}
const alpha = (px, x, y) => px[4 * (y * TEX_W + (((x % TEX_W) + TEX_W) % TEX_W)) + 3];
/** each foot's gap in rows above the ground line, and the ground band's holes */
function measureTex(dir, fam) {
  const px = readTex(dir, fam.tex), g = Math.ceil(fam.ground * TEX_H);
  const feet = fam.all.map((f) => {
    const x = Math.round(f.x * TEX_W);
    let gap = 0;
    for (let y = g - 2; y >= g - 80 && alpha(px, x, y) < 200; y--) gap++; /* (from the row above the ground line's own half-covered row) */
    return { id: f.id, gap, px: gap * ROWS_TO_PX };
  });
  let holes = 0;
  for (let y = g + 1; y < TEX_H; y++) for (let x = 0; x < TEX_W; x++) if (px[4 * (y * TEX_W + x) + 3] < 250) holes++;
  return { feet, holes };
}
{
  let n = 0, worst = 0, floating = [];
  for (const fam of families) {
    const m = measureTex(join(OUT, 'tex'), fam);
    n += m.feet.length;
    for (const f of m.feet) { worst = Math.max(worst, f.px); if (f.px > PASS_PX) floating.push(`${FAMILY[fam.tex]} ${f.id} ${f.px.toFixed(1)} px`); }
    if (m.holes) fail(`texture: ${FAMILY[fam.tex]}: ${m.holes} clear pixels in the ground band`);
  }
  if (floating.length || n < 30) fail(`texture: ${floating.length} of ${n} feet above their ground (worst ${worst.toFixed(1)} px): ${floating.join(', ')}`);
  else ok(`texture: ${n} feet (${families.map((f) => `${f.all.length} ${FAMILY[f.tex]}`).join(', ')}) on their ground, worst gap ${worst.toFixed(1)} px, every ground band solid`);
  if (control) {
    paintRun(join(OUT, 'tex-lift'), { LANTERNFALL_PAR_LIFT: String(LIFT) });
    const m = measureTex(join(OUT, 'tex-lift'), families.find((f) => f.tex === 2));
    const lifted = m.feet.filter((f) => f.px > PASS_PX);
    if (lifted.length === 1 && lifted[0].px >= 8) ok(`control fails as it must: the texture with tree ${lifted[0].id} lifted reads ${lifted[0].px.toFixed(1)} px above its ground`);
    else fail(`CONTROL PASSED (a failure): the lifted tree was not caught in the texture (${JSON.stringify(m.feet)})`);
  }
}

// ── the views: where the camera stands in each region ──
const rect = (r) => ({ X0: r.x * CELL_W, X1: (r.x + r.w) * CELL_W, Y0: r.y * CELL_H, Y1: (r.y + r.h) * CELL_H });
const clampX = (r, side) => { const { X0, X1 } = rect(r), a = X0 + CELL_W / 2, b = X1 - CELL_W / 2; return b < a ? (X0 + X1) / 2 : side < 0 ? a : side > 0 ? b : (a + b) / 2; };
const clampY = (r, side) => { const { Y0, Y1 } = rect(r), a = Y0 + CELL_H / 2, b = Y1 - CELL_H / 2; return b < a ? (Y0 + Y1) / 2 : side < 0 ? a : b; };
function viewsOf(region) {
  const rs = rooms.filter((r) => r.region === region);
  const out = [], seen = new Set();
  const add = (r, sx, sy, tag) => { const x = clampX(r, sx), y = clampY(r, sy), k = `${r.i}:${x}:${y}`; if (!seen.has(k)) { seen.add(k); out.push({ room: r.i, x, y, tag, region }); } };
  const tall = rs.filter((r) => r.h >= 2).sort((a, b) => b.h - a.h || a.x - b.x);
  for (const r of tall.slice(0, 3)) add(r, 0, 1, 'tall bottom');
  if (tall.length) add(tall[0], 0, -1, 'tall top');
  const plain = rs.filter((r) => r.kind === 0 && r.w >= 2);
  plain.slice(0, 2).forEach((r, i) => add(r, i ? 1 : -1, 1, 'plain'));
  const byX = [...rs].sort((a, b) => a.x - b.x);
  if (byX.length) add(byX[0], -1, 1, 'left end'), add(byX[byX.length - 1], 1, 1, 'right end');
  return out.slice(0, 8);
}
const regions = quick ? [0, 1] : [...new Set(rooms.map((r) => r.region))].sort((a, b) => a - b);
const allViews = regions.flatMap((g) => (quick ? viewsOf(g).slice(0, 2) : viewsOf(g)));

// ── measuring a view's frames ──
const lum = (img, x, y) => { const i = 3 * (y * img.w + x); return 0.2126 * img.px[i] + 0.7152 * img.px[i + 1] + 0.0722 * img.px[i + 2]; };
/** the median luma of the 3 x 3 pixels round a canvas point */
function lumaAt(img, canvas, cx, cy) {
  const x = Math.round(cx * img.w / canvas[0]), y = Math.round(cy * img.h / canvas[1]), v = [];
  for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) { const px = x + dx, py = y + dy; if (px >= 0 && py >= 0 && px < img.w && py < img.h) v.push(lum(img, px, py)); }
  if (!v.length) return -1;
  v.sort((a, b) => a - b);
  return v[v.length >> 1];
}
const gapRows = {}; /* family tex -> foot id -> rows (the texture measurement, so the frame samples sit under the painted foot) */
for (const fam of families) { gapRows[fam.tex] = {}; for (const f of measureTex(join(OUT, 'tex'), fam).feet) gapRows[fam.tex][f.id] = f.gap; }
let kept = 0;
const drop = (file, keep) => { if (keep && kept < 20) kept++; else try { unlinkSync(file); } catch { /* gone */ } };
/**
 * judge one view: par (the game's lf_par reply), shots: { full, grad, layers: {k: file} }
 * @returns {{ judged: number, floating: string[], treeIds: Set<number>, clear: number, clearBad: string[], worst: number }}
 */
function judge(view, par, shots, liftRows = 0) {
  const canvas = par.canvas, grad = readPng(shots.grad), full = shots.full ? readPng(shots.full) : null;
  const res = { judged: 0, floating: [], treeIds: new Set(), clear: 0, clearBad: [], worst: 0 };
  const below = 4 * canvas[1] / 1080; /* 4 px at 1080p */
  let keep = false;
  for (const l of par.layers) {
    if (!shots.layers[l.k]) continue;
    const img = readPng(shots.layers[l.k]);
    for (const c of l.copies) {
      const rows = liftRows && l.tex === 2 && c.id === 3 ? liftRows : (gapRows[l.tex] || {})[c.id] || 0;
      const py = c.sy - rows * l.h / TEX_H + below;
      if (py < 2 || py >= canvas[1] - 2 || c.sx < 2 || c.sx >= canvas[0] - 2) continue;
      const g = lumaAt(grad, canvas, c.sx, py), v = lumaAt(img, canvas, c.sx, py);
      if (g < 8) continue; /* (a black pixel says nothing) */
      const ratio = v / g;
      res.judged++;
      if (l.tex === 2) res.treeIds.add(c.id);
      res.worst = Math.max(res.worst, ratio);
      if (ratio > 0.8) res.floating.push(`${NAMES[view.region]} room ${view.room} ${view.tag}: ${FAMILY[l.tex]} ${c.id} at (${c.sx.toFixed(0)},${py.toFixed(0)}) sky under it (luma ${ratio.toFixed(2)} of the gradient)`), keep = true;
      if (full && c.clear) {
        const f = lumaAt(full, canvas, c.sx, py), fr = f / g;
        res.clear++;
        if (fr > 0.85) res.clearBad.push(`${NAMES[view.region]} room ${view.room} ${view.tag}: ${FAMILY[l.tex]} ${c.id} in the whole frame (luma ${fr.toFixed(2)} of the gradient)`), keep = true;
      }
    }
    drop(shots.layers[l.k], keep);
  }
  drop(shots.grad, keep);
  if (shots.full) drop(shots.full, keep);
  return res;
}
const merge = (a, b) => { a.judged += b.judged; a.floating.push(...b.floating); b.treeIds.forEach((t) => a.treeIds.add(t)); a.clear += b.clear; a.clearBad.push(...b.clearBad); a.worst = Math.max(a.worst, b.worst); };
const fresh = () => ({ judged: 0, floating: [], treeIds: new Set(), clear: 0, clearBad: [], worst: 0 });

// ── native: one tools/run-cart.mjs run per frame (the test GPU, pinned and sandboxed by the runner) ──
function nativeShot(view, k, file, lift, outJson) {
  const cmds = ['@2 lf_hud 0', ...(lift ? [`@3 lf_lift ${lift}`] : []), `@4 lf_view ${view.room} ${view.x} ${view.y}`, ...(k !== undefined ? [`@5 lf_par_only ${k}`, '@6 lf_sky_only 1'] : [])];
  const args = [join(ENGINE, 'tools/run-cart.mjs'), '--host', 'native', '--app', 'lanternfall_auto', '--frames', '7', '--gpu', gpu, '--start-cmds', cmds.join(';'), '--shot', file,
    ...(outJson ? ['--end-cmds', 'lf_par', '--end-out', outJson] : [])];
  const r = spawnSync('node', args, { encoding: 'utf8', timeout: 300000, env: { ...process.env, CW_PROJECT_ROOT: ROOT }, maxBuffer: 1 << 26 });
  if (r.status !== 0 || !existsSync(file)) throw new Error(`native run failed (status ${r.status}): ${((r.stdout || '') + (r.stderr || '')).slice(-1500)}`);
}
function nativeView(view, i, lift = 0) {
  const base = join(OUT, `n-${gpu}-${i}`), parFile = `${base}.json`;
  nativeShot(view, undefined, `${base}-full.png`, lift, parFile);
  const par = JSON.parse(readFileSync(parFile, 'utf8').split('\n').find((l) => l.startsWith('{')));
  unlinkSync(parFile);
  nativeShot(view, -1, `${base}-grad.png`, lift);
  const layers = {};
  for (const l of par.layers) if (l.copies.length) nativeShot(view, l.k, `${base}-l${l.k}.png`, lift), layers[l.k] = `${base}-l${l.k}.png`;
  return { par, shots: { full: `${base}-full.png`, grad: `${base}-grad.png`, layers } };
}

// ── romdev: one session per backend on 127.0.0.1:7331, ranged calls (never a request per frame) ──
let calls = 0;
// the server: ROMDEV_URL (the pool sets it per lane; one host can run a romdev per GPU), or ROMDEV_PORT, else 127.0.0.1:7331
const ROMDEV = new URL(process.env.ROMDEV_URL || `http://127.0.0.1:${process.env.ROMDEV_PORT || 7331}`);
function romdevPost(tool, a, session) {
  calls++;
  const body = JSON.stringify(a);
  return new Promise((okk, failed) => {
    const req = httpRequest({ host: ROMDEV.hostname, port: +ROMDEV.port || 80, path: `/tool/${tool}`, method: 'POST', agent: false,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'x-romdev-session': session, 'x-romdev-agent': 'lanternfall-grounding' } }, (res) => {
      let text = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { text += c; });
      res.on('end', () => {
        let b;
        try { b = JSON.parse(text); } catch { b = text; }
        if (b && Array.isArray(b.content)) { const t = b.content.find((c) => c.type === 'text'); try { b = JSON.parse(t.text); } catch { b = t ? t.text : b; } }
        if (res.statusCode < 200 || res.statusCode >= 300 || (b && b.isError)) failed(new Error(`romdev ${tool}: ${typeof b === 'string' ? b : JSON.stringify(b).slice(0, 1500)}`));
        else okk(b);
      });
      res.on('error', failed);
    });
    const timer = setTimeout(() => req.destroy(new Error(`romdev ${tool}: no reply in 300 s`)), 300000);
    req.on('close', () => clearTimeout(timer));
    req.on('error', failed);
    req.end(body);
  });
}
async function romdevSweep(views, controlViews) {
  const session = `lanternfall-grounding-${gpu}-${process.pid}`, pin = wgpuPin();
  const loaded = await romdevPost('loadMedia', { platform: 'wasmcart', path: CART, deterministicSeed: 1, deterministicStepMs: 1000 / 60, gpuPower: pin.power, wgpu: gpu === 'webgpu' }, session);
  const cmd = async (text) => { const r = await romdevPost('wasm', { op: 'command', text }, session); return r && typeof r === 'object' && 'reply' in r ? r.reply : r; };
  const step = (n) => romdevPost('frame', { op: 'step', frames: n }, session);
  const shot = (file) => romdevPost('frame', { op: 'screenshot', path: file }, session);
  try {
    if (gpu === 'gl' && loaded.gpu !== 'gl') throw new Error(`romdev: asked for GL, the cart runs on ${loaded.gpu}`);
    if (gpu === 'webgpu') { const ga = loaded.gpuAdapter; pin.check('romdev', ga && (ga.device || ga.description)); if (loaded.gpu !== 'webgpu') throw new Error(`romdev: the cart runs on ${loaded.gpu}`); }
    await cmd('lf_hud 0');
    const total = fresh(), perRegion = {};
    const parOf = {};
    const one = async (view, i, lift) => {
      const base = join(OUT, `r-${gpu}-${i}${lift ? '-lift' : ''}`);
      await cmd(`lf_view ${view.room} ${view.x} ${view.y}`);
      await step(2);
      let par = await cmd('lf_par');
      if (typeof par === 'string') par = JSON.parse(par);
      await shot(`${base}-full.png`);
      await cmd('lf_sky_only 1'); /* (the depths alone and the gradient alone: nothing of the room over them) */
      const layers = {};
      for (const l of par.layers) {
        if (!l.copies.length) continue;
        await cmd(`lf_par_only ${l.k}`);
        await step(1);
        await shot(`${base}-l${l.k}.png`);
        layers[l.k] = `${base}-l${l.k}.png`;
      }
      await cmd('lf_par_only -1');
      await step(1);
      await shot(`${base}-grad.png`);
      await cmd('lf_par_only -2');
      await cmd('lf_sky_only 0');
      return { par, shots: { full: `${base}-full.png`, grad: `${base}-grad.png`, layers } };
    };
    for (let i = 0; i < views.length; i++) {
      const v = views[i], { par, shots } = await one(v, i, 0);
      parOf[i] = par;
      const r = judge(v, par, shots);
      merge(total, r);
      (perRegion[v.region] ||= fresh()), merge(perRegion[v.region], r);
    }
    let ctl = null;
    if (controlViews) {
      const picks = controlViews(parOf);
      await cmd(`lf_lift ${LIFT}`);
      ctl = fresh();
      for (const i of picks) { const { par, shots } = await one(views[i], i, LIFT); merge(ctl, judge(views[i], par, shots, LIFT)); }
      await cmd('lf_lift 0');
      ctl.picks = picks.length;
    }
    return { total, perRegion, ctl };
  } finally {
    await romdevPost('host', { op: 'shutdown' }, session).catch(() => {});
  }
}

/** the views whose Rootway-family trees include tree 3's foot on the canvas: the control's views (two) */
function controlPicks(parOf, views) {
  const picks = [];
  for (const [i, par] of Object.entries(parOf)) {
    const l = par.layers.find((x) => x.tex === 2);
    if (l && l.copies.some((c) => c.id === 3 && c.sy > 10 && c.sy < par.canvas[1] - 10)) picks.push(+i);
    if (picks.length === 2) break;
  }
  return picks;
}
function report(host, r, views, treeRegions) {
  const line = `${host} ${gpu}: ${r.total.judged} feet judged over ${views.length} views (${r.total.clear} in whole frames), worst luma ${r.total.worst.toFixed(2)} of the gradient`;
  const missing = treeRegions.filter((g) => !r.perRegion[g] || r.perRegion[g].treeIds.size < 6).map((g) => `${NAMES[g]} (${r.perRegion[g] ? r.perRegion[g].treeIds.size : 0} of 9 trees)`);
  const seen = r.total.treeIds.size;
  if (r.total.floating.length) fail(`${line}: ${r.total.floating.length} feet over sky:\n  ${r.total.floating.slice(0, 12).join('\n  ')}`);
  else if (r.total.clearBad.length) fail(`${line}: ${r.total.clearBad.length} feet not over ground in the whole frame:\n  ${r.total.clearBad.slice(0, 12).join('\n  ')}`);
  else if (r.total.judged < (host === 'romdev' && !quick ? 50 : 8)) fail(`${line}: too few feet judged`);
  else if (host === 'romdev' && !quick && (seen < 9 || missing.length)) fail(`${line}: ${seen} of 9 trees seen; ${missing.length ? `fewer than 6 in ${missing.join(', ')}` : 'every region fine'}`);
  else ok(line);
  if (r.ctl) {
    if (r.ctl.picks && r.ctl.floating.length) ok(`control fails as it must on ${host} ${gpu}: the lifted trunk reads sky under it in ${r.ctl.floating.length} of ${r.ctl.picks} views (${r.ctl.floating[0].replace(/^.*?: /, '')})`);
    else fail(`CONTROL PASSED (a failure) on ${host} ${gpu}: the lifted trunk was not caught in ${r.ctl.picks} views`);
  }
}
const treeRegions = quick ? [0, 1] : [0, 1, 3, 11, 12];
for (const host of hosts) {
  if (host === 'romdev') {
    const r = await romdevSweep(allViews, control ? (parOf) => controlPicks(parOf, allViews) : null);
    report(host, r, allViews, treeRegions);
    console.log(`     romdev calls: ${calls}`);
  } else if (host === 'native') {
    const views = regions.flatMap((g) => viewsOf(g).slice(0, 2));
    const total = fresh(), perRegion = {}, parOf = {};
    views.forEach((v, i) => {
      const { par, shots } = nativeView(v, i);
      parOf[i] = par;
      const r = judge(v, par, shots);
      merge(total, r);
      (perRegion[v.region] ||= fresh()), merge(perRegion[v.region], r);
    });
    let ctl = null;
    if (control) {
      const picks = controlPicks(parOf, views);
      ctl = fresh();
      for (const i of picks) { const { par, shots } = nativeView(views[i], i, LIFT); merge(ctl, judge(views[i], par, shots, LIFT)); }
      ctl.picks = picks.length;
    }
    report(host, { total, perRegion, ctl }, views, treeRegions);
  } else throw new Error(`host ${host}: native or romdev`);
}
process.exit(bad ? 1 : 0);
