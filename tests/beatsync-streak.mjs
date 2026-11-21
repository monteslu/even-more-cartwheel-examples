// beatsync's streak, through romdev on GL and WebGPU: the streak is seen where the player looks, and breaking it is
// seen too.
//
// beatsync_auto plays Test Groove on Hard twice on the deterministic clock, frame for frame the same: once as the game
// shows the streak, once with `streakfx off` (no charge, bands, drain or badge). Every difference between the two
// frames is the streak's picture. At three moments:
//   - early (a streak of 5): the banks have hardly charged: the frames differ by little;
//   - charged (a streak of 50, the top multiplier): the banks are lit to the far end: the frames differ at least 2.5
//     times as much as early over the rivers (an early streak has already lit the banks' first tenth by the gates, where
//     they are widest), and at least 4 times as much in the far half (where an early streak never reaches);
//   - broken (the autopilot lets one note go, 0.2 s after): the charge drains down the banks in red and its pieces
//     fall: the frame holds far more red than the same frame without the streak's picture.
// The fields agree: three multiplier steps by a streak of 48, the streak of 50 called out, one break.
// The CONTROL must fail: the run with `streakfx off` measured against itself (the streak's picture missing) passes
// none of the picture checks.
//
//   node tests/beatsync-streak.mjs [--gpu gl|webgpu|both]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { gpusOf, openBeatsync } from './lib/beatsync.mjs';

const { readImage } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const argv = process.argv.slice(2);
const GPUS = gpusOf(argv);
const OUT = join(ROOT, 'out/gate/beatsync-streak');
mkdirSync(OUT, { recursive: true });
const FIELDS = ['game.combo', 'streak.charge', 'streak.tiers', 'streak.breaks', 'streak.milestone', 'streak.drain_t'];

/** the rivers' part of the frame (the middle, above the HUD under the gates), split at its middle height */
const AREA = { x0: 480, x1: 1440, y0: 60, y1: 840 };
function diff(a, b, y0 = AREA.y0, y1 = AREA.y1) {
  let s = 0, n = 0;
  for (let y = y0; y < y1; y++)
    for (let x = AREA.x0; x < AREA.x1; x++) {
      const i = (y * a.w + x) * 4;
      const la = 0.2126 * a.rgba[i] + 0.7152 * a.rgba[i + 1] + 0.0722 * a.rgba[i + 2];
      const lb = 0.2126 * b.rgba[i] + 0.7152 * b.rgba[i + 1] + 0.0722 * b.rgba[i + 2];
      s += Math.abs(la - lb), n++;
    }
  return s / n;
}
function red(a) {
  let n = 0;
  for (let y = AREA.y0; y < AREA.y1; y++)
    for (let x = AREA.x0; x < AREA.x1; x++) {
      const i = (y * a.w + x) * 4, r = a.rgba[i], g = a.rgba[i + 1], b = a.rgba[i + 2];
      if (r > 140 && r > 1.8 * g && r > 1.8 * b) n++;
    }
  return n;
}

/** the game's run: finds the moments; returns their frames, fields and shots */
async function gameRun(gpu) {
  const r = await openBeatsync('beatsync_auto', { gpu, tag: 'streak' });
  try {
    await r.command('song test-groove hard');
    const at = {}, f = {};
    const until = async (name, stopWhen) => {
      const res = await r.run([{ pads: [{ buttons: [] }], frames: 6000 }], { fields: FIELDS, stopWhen });
      if (!res.stopped) throw new Error(`streak: ${name} never came (${JSON.stringify(res.fields)})`);
      at[name] = r.now(), f[name] = res.fields;
      await r.shotTo(join(OUT, `${gpu}-on-${name}.png`));
    };
    await until('early', { field: 'game.combo', atLeast: 5 });
    await until('charged', { field: 'game.combo', atLeast: 50 });
    at.skip = r.now(); // the frame the request is sent from: the off run sends it from the same one
    await r.command('skip 1 1');
    await until('broken', { field: 'streak.drain_t', atLeast: 0.2 });
    return { at, f };
  } finally {
    await r.close();
  }
}

/** the same frames with `streakfx off` */
async function offRun(gpu, at) {
  const r = await openBeatsync('beatsync_auto', { gpu, tag: 'streak-off' });
  try {
    await r.command('streakfx off');
    await r.command('song test-groove hard');
    for (const name of ['early', 'charged', 'skip', 'broken']) {
      if (r.now() > at[name]) throw new Error(`streak: the off run is past frame ${at[name]} (${r.now()}): the runs are not frame for frame the same`);
      await r.stepTo(at[name]);
      if (name === 'skip') { await r.command('skip 1 1'); continue; }
      await r.shotTo(join(OUT, `${gpu}-off-${name}.png`));
    }
  } finally {
    await r.close();
  }
}

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const mid = (AREA.y0 + AREA.y1) / 2;
function measure(on, off) {
  const d = (k, y0, y1) => diff(on[k], off[k], y0, y1);
  return { early: d('early'), charged: d('charged'), chargedFar: d('charged', AREA.y0, mid), earlyFar: d('early', AREA.y0, mid), redOn: red(on.broken), redOff: red(off.broken) };
}
const pictureOk = (m) => ({
  charge: m.charged >= 2.5 * Math.max(m.early, 0.05) && m.charged >= 2,
  far: m.chargedFar >= 1.5 && m.chargedFar >= 4 * Math.max(m.earlyFar, 0.05),
  drain: m.redOn >= 3 * m.redOff + 500,
});
for (const gpu of GPUS) {
  const { at, f } = await gameRun(gpu);
  await offRun(gpu, at);
  const shots = (kind) => Object.fromEntries(['early', 'charged', 'broken'].map((k) => [k, readImage(join(OUT, `${gpu}-${kind}-${k}.png`))]));
  const on = shots('on'), off = shots('off');
  check(f.charged['streak.tiers'] >= 3 && f.charged['streak.milestone'] === 50 && f.charged['streak.charge'] >= 0.99, `streak (${gpu}): by a streak of 50, ${f.charged['streak.tiers']} multiplier steps, the 50 called out (${f.charged['streak.milestone']}), the banks charged ${f.charged['streak.charge'].toFixed(2)}`);
  check(f.broken['streak.breaks'] === 1 && f.broken['game.combo'] <= 2, `streak (${gpu}): one note let go breaks it (breaks ${f.broken['streak.breaks']}, streak now ${f.broken['game.combo']})`);
  const m = measure(on, off), p = pictureOk(m);
  check(p.charge, `streak (${gpu}): the charge shows: the frames differ by ${m.charged.toFixed(2)} luma at a streak of 50 against ${m.early.toFixed(2)} at 5 (at least 2.5 times, and 2)`);
  check(p.far, `streak (${gpu}): the charge reaches the far end: the far half differs by ${m.chargedFar.toFixed(2)} at 50 against ${m.earlyFar.toFixed(2)} at 5`);
  check(p.drain, `streak (${gpu}): the break shows: ${m.redOn} red pixels draining against ${m.redOff} without the streak's picture`);
  const c = measure(off, off), pc = pictureOk(c);
  check(!pc.charge && !pc.far && !pc.drain, `streak control (${gpu}, streakfx off against itself): charge ${pc.charge}, far ${pc.far}, drain ${pc.drain}: ${pc.charge || pc.far || pc.drain ? 'PASSES: the checks cannot see a missing streak' : 'every picture check fails, as it must'}`);
}
console.log(failed ? `beatsync-streak: ${failed} failed` : 'beatsync-streak: ok');
process.exit(failed ? 1 : 0);
