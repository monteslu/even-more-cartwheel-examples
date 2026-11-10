// The camera sweep: does the still world hold still while only the camera moves? Two faces that fight for the depth
// test (tests/coplanar.mjs) swap patch by patch as the view changes, so a pixel of a still surface goes up, down and
// up again from frame to frame, where a still surface under a slowly gliding camera only ever drifts one way.
//
// Each scene loads a level where the build had overlapping same-plane faces (the regions tests/coplanar.mjs named),
// then holds the game still (COPLANAR_SWEEP: the debugger's pause, so nothing in the world steps) and glides the
// camera past those faces for SWEEP frames, slowly: a tenth of a pixel a frame or less, so a texture or an edge only
// ever drifts one way across a pixel, where two surfaces fighting for it swap back and forth. Every frame is captured
// natively, headless, at half size (box-filtered), and each pixel's brightness is followed: a pixel that turns back
// on itself (up a step, then down a step, both steps over STEP) at least TURNS times in the sweep is a flickering
// pixel. Two kinds of pixel are taken out first, from a run of the same build with the camera held still: what
// animates while the game is held (lamps, wind in the leaves, water: it turns back twice there), and the picture's
// own edges (a leaf's outline, a rail, a window frame: a slope of EDGE or more there), which a gliding camera carries
// across pixels. Two surfaces fighting do it in the middle of a face, where the held picture is smooth. The scene
// passes with at most LIMIT per million.
// CONTROLS, one of which must fail in each scene, or the sweep has not shown it can see two faces fight there: the
// build before the separation (COPLANAR_SEPARATE=0, the faces where the level put them), and the fixed build with a
// doubled face drawn half way to where the camera looks (COPLANAR_CONTROL=1: a red square a fifth of the view across
// and a green one turned an eighth round, leaning through it by two steps of the depth buffer): the same judge as the
// fix, over LIMIT per million. (On some scenes the GPU draws the crossing of the doubled face as one clean line, and the
// built faces are what flicker; on others the reverse.)
// Towers is not swept: its camera's depth range is the map's own few metres seen from 180 m, so faces even half a
// millimetre apart lie hundreds of depth steps apart and neither control can flicker there (tests/coplanar.mjs holds
// its faces apart all the same).
// CW_SWEEP_KEEP=<dir> writes, for each scene, its first frame fixed and as built and a map of where the pixels
// flickered (at the capture's size), and two frames a step apart, fixed and as built, cropped at the native size round
// the worst cluster.
//
// Destructo runs as a cart on romdev (no native game runs for it): its switches are run options set over the debug
// channel (dm set LEVEL_NAME, NO_HUD, COPLANAR_SEPARATE, COPLANAR_CONTROL; dm restart; then dm set COPLANAR_SWEEP, whose
// frames count from that command), and each sweep is one recordSession span (tests/lib/flash-run.mjs captureRomdev).
// CW_SWEEP_KEEP's native crops are not made for it.
//
// Run: node tests/coplanar-sweep.mjs [game ...] [webgpu]   (holds the GPU suite lock)
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inSuiteLock, captureRun as captureRunNative, captureRomdev, ENGINE } from './lib/flash-run.mjs';
import { openCart } from './lib/romdev.mjs';

inSuiteLock('coplanar-sweep', fileURLToPath(import.meta.url));
const { writePng } = await import(join(ENGINE, 'tools/lib/png.mjs'));
const args = process.argv.slice(2);
const GPU = args.includes('webgpu') ? 'webgpu' : 'gl';
const only = args.filter((a) => a !== 'webgpu');
const SWEEP = 120, STEP = 10, TURNS = 6, LIMIT = 40, SCALE = 2, STILL_TURNS = 2, EDGE = 12;

// from: the frame the level is loaded and still by; cam: from (x, y, z) at (tx, ty, tz), gliding (dx, dy, dz) m/s
const SCENES = {
  destructo: [{ name: 'The Last Fort (its settled stone courses)', app: 'destructo_auto', from: 190, romdev: true, env: { LEVEL_NAME: 'The Last Fort', NO_HUD: '1' },
    cam: [-3, 3, -16, -3, 2.5, 0, 0.05, 0, 0] }],
  emberthrone: [{ name: 'the meadow\'s buildings', app: 'emberthrone_auto', from: 150, env: { ADV_NO_HUD: '1' },
    cam: [176, 20, 262, 180, 16, 271, 0.05, 0, 0] }],
  skate: [{ name: 'park 1 (the plaza\'s town)', app: 'skate_auto', from: 180, env: { SKATE_PARK: '1', SKATE_PLAYERS: '1', SKATE_NO_HUD: '1' },
    cam: [180, 30, 20, 204, 28, 40, 0.1, 0, 0] }],
  pinball: [{ name: 'table 0 (walls, ramps and the playfield)', app: 'pinball_auto', from: 120, env: { PINBALL_TABLE: '0', PINBALL_NO_HUD: '1' },
    cam: [-0.1, 0.35, 0.45, -0.18, 0.0, 0.1, 0.002, 0, 0] }],
};

/* a romdev scene's capture: its options (env's names without COPLANAR_SWEEP) set over the debug channel, the level started
 * again, then the sweep armed; frames from..to count from the arming, as natively from the start */
async function captureCart(sc, { from, frames, scale, name, env }) {
  const h = await openCart(sc.app, { gpu: GPU, tag: name });
  try {
    for (const [k, v] of Object.entries(env)) if (k !== 'COPLANAR_SWEEP' && k !== 'CW_WGPU_VALIDATION') await h.command(`dm set ${k} "${v}"`);
    await h.command('dm restart');
    await h.command(`dm set COPLANAR_SWEEP ${env.COPLANAR_SWEEP}`);
    const at = h.frame - 1; /* the arming ran in that command's frame */
    return await captureRomdev(h, { from: at + from, frames: at + frames, scale, name });
  } finally { await h.close(); }
}

/** luma of each pixel of each frame (0..255) */
function lumas(run) {
  const n = run.w * run.h;
  return run.frames.map((f) => {
    const l = new Float32Array(n);
    for (let i = 0; i < n; i++) l[i] = 0.2126 * f[i * 3] + 0.7152 * f[i * 3 + 1] + 0.0722 * f[i * 3 + 2];
    return l;
  });
}
/** pixels that turn back on themselves at least TURNS times: their count, a per-pixel map, and the worst cluster's box */
function turnsOf(run) {
  const L = lumas(run), n = run.w * run.h, turns = new Uint16Array(n);
  for (let t = 1; t + 1 < L.length; t++) {
    const a = L[t - 1], b = L[t], c = L[t + 1];
    for (let i = 0; i < n; i++) {
      const d1 = b[i] - a[i], d2 = c[i] - b[i];
      if (d1 * d2 < 0 && Math.abs(d1) > STEP && Math.abs(d2) > STEP) turns[i]++;
    }
  }
  return turns;
}
/** the pixels left out: those that turn back with the camera held, and the held picture's edges (one pixel round) */
function heldMask(held) {
  const w = held.w, h = held.h, still = turnsOf(held), L = lumas({ ...held, frames: [held.frames[0]] })[0], edge = new Uint8Array(w * h), out = new Uint8Array(w * h);
  for (let y = 1; y + 1 < h; y++)
    for (let x = 1; x + 1 < w; x++) {
      const i = y * w + x;
      if (Math.max(Math.abs(L[i + 1] - L[i - 1]), Math.abs(L[i + w] - L[i - w])) / 2 >= EDGE) edge[i] = 1;
    }
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      const i = y * w + x;
      let e = still[i] >= STILL_TURNS;
      for (let dy = -1; dy <= 1 && !e; dy++) for (let dx = -1; dx <= 1 && !e; dx++) { const xx = x + dx, yy = y + dy; e = xx >= 0 && yy >= 0 && xx < w && yy < h && edge[yy * w + xx] === 1; }
      out[i] = e ? 1 : 0;
    }
  return out;
}
function flicker(run, mask) {
  const n = run.w * run.h, turns = turnsOf(run);
  for (let i = 0; i < n; i++) if (mask[i]) turns[i] = 0;
  let count = 0;
  for (let i = 0; i < n; i++) if (turns[i] >= TURNS) count++;
  // the worst cluster: the 32 x 32 cell (capture pixels) with the most flickering pixels
  const C = 32;
  let best = -1, bx = 0, by = 0;
  for (let y0 = 0; y0 < run.h; y0 += C / 2)
    for (let x0 = 0; x0 < run.w; x0 += C / 2) {
      let k = 0;
      for (let y = y0; y < Math.min(run.h, y0 + C); y++) for (let x = x0; x < Math.min(run.w, x0 + C); x++) if (turns[y * run.w + x] >= TURNS) k++;
      if (k > best) best = k, bx = x0, by = y0;
    }
  return { count, perM: count / n * 1e6, turns, box: { x: bx, y: by, w: C, h: C, n: best } };
}

function toRgba(run, k) {
  const n = run.w * run.h, rgba = new Uint8Array(n * 4);
  for (let i = 0; i < n; i++) rgba[i * 4] = run.frames[k][i * 3], rgba[i * 4 + 1] = run.frames[k][i * 3 + 1], rgba[i * 4 + 2] = run.frames[k][i * 3 + 2], rgba[i * 4 + 3] = 255;
  return rgba;
}
function cropPng(path, run, k, x0, y0, w, h) {
  const rgba = toRgba(run, k), crop = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const sx = Math.max(0, Math.min(run.w - 1, x0 + x)), sy = Math.max(0, Math.min(run.h - 1, y0 + y)), s = (sy * run.w + sx) * 4, d = (y * w + x) * 4;
    crop[d] = rgba[s], crop[d + 1] = rgba[s + 1], crop[d + 2] = rgba[s + 2], crop[d + 3] = 255;
  }
  writePng(path, w, h, crop);
}
function keep(dir, label, run, f) {
  const n = run.w * run.h, rgba = new Uint8Array(n * 4), map = new Uint8Array(n * 4);
  for (let i = 0; i < n; i++) {
    rgba[i * 4] = run.frames[0][i * 3], rgba[i * 4 + 1] = run.frames[0][i * 3 + 1], rgba[i * 4 + 2] = run.frames[0][i * 3 + 2], rgba[i * 4 + 3] = 255;
    const hot = f.turns[i] >= TURNS;
    map[i * 4] = hot ? 255 : rgba[i * 4] >> 2, map[i * 4 + 1] = hot ? 40 : rgba[i * 4 + 1] >> 2, map[i * 4 + 2] = hot ? 40 : rgba[i * 4 + 2] >> 2, map[i * 4 + 3] = 255;
  }
  writePng(join(dir, `${label}-frame.png`), run.w, run.h, rgba);
  writePng(join(dir, `${label}-flicker.png`), run.w, run.h, map);
}

let bad = 0;
const KEEP = process.env.CW_SWEEP_KEEP ? resolve(process.env.CW_SWEEP_KEEP) : '';
if (KEEP) mkdirSync(KEEP, { recursive: true });
for (const game of only.length ? only : Object.keys(SCENES)) {
  for (const sc of SCENES[game] || []) {
    const env = { ...sc.env, CW_WGPU_VALIDATION: '0', COPLANAR_SWEEP: [sc.from, ...sc.cam].join(',') };
    const captureRun = sc.romdev ? (o) => captureCart(sc, o) : captureRunNative;
    const cap = (extra, label) => captureRun({ app: sc.app, from: sc.from + 2, frames: sc.from + 1 + SWEEP, gpu: GPU, scale: SCALE, name: `coplanar-${game}-${label}`, env: { ...env, ...extra } });
    const held = await captureRun({ app: sc.app, from: sc.from + 2, frames: sc.from + 1 + SWEEP, gpu: GPU, scale: SCALE, name: `coplanar-${game}-held`,
      env: { ...env, COPLANAR_SWEEP: [sc.from, ...sc.cam.slice(0, 6), 0, 0, 0].join(',') } });
    const mask = heldMask(held);
    held.frames.length = 0;
    const fixed = await cap({}, 'fixed'), f = flicker(fixed, mask);
    const built = await cap({ COPLANAR_SEPARATE: '0' }, 'built'), c = flicker(built, mask);
    const heldCtl = await captureRun({ app: sc.app, from: sc.from + 2, frames: sc.from + 1 + SWEEP, gpu: GPU, scale: SCALE, name: `coplanar-${game}-held-control`,
      env: { ...env, COPLANAR_CONTROL: '1', COPLANAR_SWEEP: [sc.from, ...sc.cam.slice(0, 6), 0, 0, 0].join(',') } });
    const ctl = flicker(await cap({ COPLANAR_CONTROL: '1' }, 'control'), heldMask(heldCtl)); /* (left out by its own held picture: the square hides what is behind it) */
    heldCtl.frames.length = 0;
    const moved = fixed.frames.length > 1 && fixed.frames[0].some((v, i) => Math.abs(v - fixed.frames[fixed.frames.length - 1][i]) > 8);
    let masked = 0;
    for (let i = 0; i < mask.length; i++) masked += mask[i];
    const what = `${game} ${GPU}, ${sc.name}: ${f.perM.toFixed(0)} flickering pixels per million (${f.count}), as built ${c.perM.toFixed(0)} (${c.count}); ${(masked / mask.length * 100).toFixed(0)}% of the picture left out (edges, what animates)`;
    if (!moved) { bad++; console.log(`FAIL ${what}: the camera never moved (a sweep that holds still proves nothing)`); continue; }
    const ok = f.perM <= LIMIT, controlOk = ctl.perM > LIMIT || c.perM > LIMIT;
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${ok ? '' : `: over ${LIMIT} per million, worst at capture pixels ${f.box.x},${f.box.y} (${f.box.n} in a ${f.box.w}-pixel square)`}`);
    console.log(controlOk ? `ok   controls fail as they must (${game}): as built ${c.perM.toFixed(0)} per million (worst at capture pixels ${c.box.x},${c.box.y}), a doubled face in view ${ctl.perM.toFixed(0)}`
      : `CONTROLS PASSED (a failure): ${game} as built shows ${c.perM.toFixed(0)} per million and with a doubled face in view ${ctl.perM.toFixed(0)}, both within ${LIMIT}`);
    bad += (ok ? 0 : 1) + (controlOk ? 0 : 1);
    if (KEEP && !sc.romdev) {
      const box = c.box.n >= f.box.n ? c.box : f.box, cx = (box.x + box.w / 2) * SCALE, cy = (box.y + box.h / 2) * SCALE;
      keep(KEEP, `${game}-${GPU}-fixed`, fixed, f);
      keep(KEEP, `${game}-${GPU}-built`, built, c);
      for (const [label, extra] of [['fixed', {}], ['built', { COPLANAR_SEPARATE: '0' }]]) {
        /* two frames of the sweep at the native size, cropped round the worst cluster */
        const nat = await captureRun({ app: sc.app, from: sc.from + 40, frames: sc.from + 41, gpu: GPU, scale: 1, name: `coplanar-${game}-${label}-native`, env: { ...env, ...extra } });
        for (let k = 0; k < 2; k++) cropPng(join(KEEP, `${game}-${GPU}-${label}-native-${k}.png`), nat, k, Math.round(cx - 160), Math.round(cy - 120), 320, 240);
        /* and the game's own view of the same moment (its camera, nothing held), the whole frame at the native size */
        const own = { ...env, ...extra };
        delete own.COPLANAR_SWEEP;
        const game1 = await captureRun({ app: sc.app, from: sc.from, frames: sc.from, gpu: GPU, scale: 1, name: `coplanar-${game}-${label}-game`, env: own });
        writePng(join(KEEP, `${game}-${GPU}-${label}-game.png`), game1.w, game1.h, toRgba(game1, 0));
      }
    }
  }
}
process.exit(bad ? 1 : 0);
