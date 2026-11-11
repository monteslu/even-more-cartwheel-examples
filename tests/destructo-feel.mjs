// destructo feel gate: what a person feels, measured through romdev on the game cart (GL and WebGPU) with ranged input
// only (one input timeline or one axis set a span, the trace read back once; never a call a frame).
//   respond   a press shows in the next frame, even while the game is busy
//     fire        South to the shot leaving (game.shots, state flight) in at most 3 ticks, from rest AND while the last shot's
//                 collapse is still simulating
//     buffered    a South press during a flight or the settle is not lost: it fires within 2 ticks of the aim opening
//     preaim      the d-pad turns the launcher during the settle (the aim moves in the same tick as the press)
//     ammo        West changes the ammunition in the tick it is read, and also while the collapse settles
//   aim       dead zone and curve (a push just past the dead zone moves the launcher, slowly; a full push is quick), a one-frame
//             tap is a fine step (under 0.1 degree), a held d-pad sweeps the span of a structure in under 1.2 s, eased
//             (no step in the speed from one tick to the next beyond a limit)
//   orbit     the right stick moves the camera in the next frames, and the orbit speed eases
//   dead      the launcher is ready to fire again soon after a shot (ticks from launch to a fire that is accepted)
//   cue       a press that nothing else answers (South while the shot is in the air, West, a menu move) shows a mark in the frame it
//             is read (the cart's ui.cue_press / ui.cue_draw frame numbers, difference at most 1)
//   beat      the winning blow runs the world at 0.35x for about 0.4 s, eased in and out (beat.ticks, beat.min)
//   frames    no frame over 16 ms of CPU in a play session on this lane (a timing is the lane's own: recorded, not asserted) except
//             the ones the report names
//   control   --control: the pad and the rules before the change (dm set OLD_FEEL 1: the sim's held-key aim, the squared curve with
//             its dead zone, the aim shut from the shot's launch to the settle's end) must fail the checks it is meant to
// Run: node tests/destructo-feel.mjs [--gpu gl|webgpu] [--control] [--frame-gate] [--json out.json]
import { writeFileSync } from 'node:fs';
import { openCart, call, gpusArg } from './lib/romdev.mjs';

const control = process.argv.includes('--control');
const frameGate = process.argv.includes('--frame-gate'); // the frame count is a timing: asserted only on a quiet lane
const jsonOut = process.argv.includes('--json') ? process.argv[process.argv.indexOf('--json') + 1] : null;
const N = 256;
let bad = 0;
const results = {};
const say = (ok, what) => { if (!ok) bad++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); };
const tap = (f, b, n = 3) => ({ frame: f, buttons: [b], until: f + n });

for (const gpu of gpusArg()) { // the select screen: a held direction runs along the structures, the choice wraps, a press moves the highlight at once
  const h = await openCart('destructo', { gpu, tag: 'feel-menu' });
  try {
    if (control) await h.command('dm set OLD_FEEL 1');
    let at = h.frame;
    const run = async (n) => { await h.step(n); at += n; };
    await call('input', { op: 'timeline', timeline: [tap(20, 'south'), tap(40, 'south'), tap(60, 'start'), tap(70, 'north')] }, h.session);
    at = 73;
    await run(20);
    let s = await h.state();
    const sel0 = s['menu.sel'];
    await call('input', { op: 'timeline', timeline: [tap(at + 2, 'down', 1)] }, h.session);
    at += 3;
    await run(2);
    const one = (await h.state())['menu.sel'] - sel0;
    await call('input', { op: 'timeline', timeline: [{ frame: at + 2, buttons: ['down'], until: at + 62 }] }, h.session);
    at += 62;
    await run(2);
    const held = (await h.state())['menu.sel'];
    const dist = Math.min((held - sel0 + 49) % 49, (sel0 - held + 49) % 49) + 0;
    await call('input', { op: 'timeline', timeline: [tap(at + 2, 'up', 1)] }, h.session);
    at += 3;
    await run(5);
    const upSel = (await h.state())['menu.sel'];
    const chk = (ok, what, disc = true) => (control ? (disc ? say(!ok, `control (old build) ${what} fails as it must`) : console.log(`info ${what}: ${ok ? 'holds' : 'fails'} on the old build too`)) : say(ok, what));
    chk(one === 4, `menu ${gpu}: one press moves the highlight a row in the tick it is read (moved ${one})`, false);
    chk(held >= 28 && held !== sel0, `menu ${gpu}: holding down for a second runs 30 tiles along the list (the highlight is on ${held} from ${sel0 + one})`);
    const mc = (await h.state());
    chk(mc['ui.cue_draw'][2] > 0 && mc['ui.cue_draw'][2] - mc['ui.cue_press'][2] <= 1, `cue ${gpu}: a menu press shows its pressed state in the frame it is read (${mc['ui.cue_draw'][2] - mc['ui.cue_press'][2]} frames after)`);
    chk(upSel !== held, `menu ${gpu}: up moves the highlight back (${held} -> ${upSel})`, false);
    results[gpu + '_menu'] = { sel0, one, held, upSel };
  } catch (e) { say(false, `menu ${gpu}: ${e.stack || e.message}`); } finally { await h.close(); }
}

for (const gpu of gpusArg()) {
  const h = await openCart('destructo', { gpu, tag: 'feel' });
  let at = 1;
  const seg = async (entries) => { await call('input', { op: 'timeline', timeline: entries }, h.session); at = Math.max(at, ...entries.map((e) => e.until)); };
  const goto = async (to) => { if (to > at) { await h.step(to - at); at = to; } };
  const run = async (n) => { await h.step(n); at += n; };
  const axes = async (a) => call('input', { op: 'set', ports: [{ axes: a }] }, h.session);
  const S = async () => h.state();
  // the ring in tick order: [{tick, state, flags, yaw, pitch, orbit, shots, ammo}] for the ticks it still holds
  const ring = (s) => {
    const rows = [];
    for (let i = 0; i < N; i++) if (s['feel.ring_tick'][i] > 0) rows.push({ tick: s['feel.ring_tick'][i], state: s['feel.ring_state'][i], flags: s['feel.ring_flags'][i], yaw: s['feel.ring_yaw'][i], pitch: s['feel.ring_pitch'][i], orbit: s['feel.ring_orbit'][i], shots: s['feel.ring_shots'][i], ammo: s['feel.ring_ammo'][i] });
    return rows.sort((a, b) => a.tick - b.tick);
  };
  const cam = (s) => {
    const rows = [];
    for (let i = 0; i < N; i++) rows.push({ tick: s['feel.cam_tick'][i], x: s['feel.cam_x'][i], y: s['feel.cam_y'][i], z: s['feel.cam_z'][i] });
    return rows.filter((r) => r.tick > 0).sort((a, b) => a.tick - b.tick);
  };
  const R = { gpu };
  try {
    await seg([tap(20, 'south'), tap(40, 'south'), tap(60, 'start'), tap(90, 'south')]);
    await goto(300);
    if (control) await h.command('dm set OLD_FEEL 1'), at += 1;
    await h.command('freefire 1'), at += 1;
    let s = await S();
    if (s['game.phase'] !== 1) throw new Error(`not in play at frame 300 (phase ${s['game.phase']})`);
    // ---- aim: a one-frame tap at the middle ----
    await run(20);
    let s0 = await S();
    await seg([{ frame: at + 3, buttons: ['left'], until: at + 4 }]);
    await run(30);
    s = await S();
    const tapDeg = Math.abs(s['aim.yaw'] - s0['aim.yaw']) * 180 / Math.PI;
    R.tap_deg = tapDeg;
    // ---- aim: the analog push, alternating sides, 30 ticks each ----
    const analog = {};
    let side = 1;
    for (const v of [0.3, 0.6, 1.0]) {
      await run(20);
      s0 = await S();
      await axes({ lx: -side * v });
      await run(30);
      await axes({});
      await run(40);
      s = await S();
      const r = ring(s).filter((x) => x.tick > s0['game.tick']);
      const first = r.findIndex((x) => x.flags & 4);
      if (first < 0) { analog[v] = { latency: 99, v10: 0, v25: 0, ignored: true }; side = -side; continue; }
      const move = r.findIndex((x, i) => i > 0 && Math.abs(x.yaw - r[i - 1].yaw) > 1e-7);
      const speed = (i) => Math.abs(r[i].yaw - r[i - 1].yaw) * 60; // rad/s
      analog[v] = { latency: move - first, v10: speed(first + 10), v25: speed(first + 25) };
      side = -side;
    }
    R.analog = analog;
    const a1 = analog[1];
    // ---- aim: a held d-pad sweep from the left end across half a radian, and the release ----
    await seg([{ frame: at + 2, buttons: ['left'], until: at + 130 }]);
    await run(5);
    s0 = await S();
    await seg([{ frame: at + 2, buttons: ['right'], until: at + 47 }]);
    await run(30);
    s = await S();
    const rows = ring(s).filter((x) => x.tick > s0['game.tick']);
    const i0 = rows.findIndex((x) => x.flags & 4);
    const y0 = rows[i0].yaw;
    const hit = rows.findIndex((x, i) => i >= i0 && Math.abs(x.yaw - y0) >= 0.5);
    R.sweep_s = hit < 0 ? 99 : (hit - i0) / 60;
    const spd = rows.slice(i0).map((x, i, a) => (i ? Math.abs(x.yaw - a[i - 1].yaw) : 0)).slice(1);
    const vmax = Math.max(...spd);
    R.speed_step = Math.max(...spd.map((v, i) => (i ? Math.abs(v - spd[i - 1]) : 0))) / vmax; // the largest step in the speed, as a share of the top speed (a stop on release counts)
    // ---- orbit: the right stick, the camera ----
    await run(60);
    s0 = await S();
    const cam0 = cam(s0).at(-1);
    await axes({ rx: 1 });
    await run(40);
    await axes({});
    await run(40);
    s = await S();
    const r2 = ring(s).filter((x) => x.tick > s0['game.tick']);
    const f0 = r2.findIndex((x) => x.flags & 8);
    const o0 = r2.findIndex((x, i) => i > 0 && Math.abs(x.orbit - r2[i - 1].orbit) > 1e-7);
    const cm = cam(s).filter((c) => c.tick >= r2[f0].tick);
    const cmove = cm.findIndex((c) => Math.hypot(c.x - cm[0].x, c.z - cm[0].z) > 0.05);
    R.orbit = { sim_latency: o0 - f0, cam_latency: cmove < 0 ? 99 : cm[cmove].tick - r2[f0].tick, rate: (r2[f0 + 40].orbit - r2[f0 + 10].orbit) / 0.5 };
    // ---- ammo: West ----
    await run(10);
    s0 = await S();
    await seg([tap(at + 2, 'west', 2)]);
    await run(10);
    s = await S();
    const r3 = ring(s).filter((x) => x.tick > s0['game.tick']);
    const ia = r3.findIndex((x) => x.flags & 2), ic = ia < 1 ? -1 : r3.findIndex((x, i) => i >= ia && x.ammo !== r3[ia - 1].ammo);
    R.ammo_seen = [...new Set(r3.map((x) => x.ammo))].join(',');
    R.ammo_latency = ic < 0 ? 99 : ic - ia;
    await seg([tap(at + 2, 'west', 2)]); // back to the shot (the ammunition cycles)
    await seg([tap(at + 8, 'west', 2)]);
    await run(20);
    s = await S();
    if (s['aim.ammo'] !== 0) { await seg([tap(at + 2, 'west', 2)]); await run(10); }
    // ---- fire from rest ----
    s0 = await S();
    const f_press = at + 3;
    await seg([tap(f_press, 'south', 2)]);
    await run(20);
    s = await S();
    R.fire_latency = s['feel.launch_tick'] - s['feel.fire_press_tick'];
    const s_launch = (await S())['feel.launch_tick'];
    // ---- mash South from the launch on: when is the next shot accepted, and how fast after each press ----
    {
      const mb = (await S())['game.tick'];
      const mash = [];
      for (let k = 0; k < 40; k++) mash.push(tap(at + 4 + k * 6, 'south', 2));
      await seg(mash);
      s = await S();
      const rm = ring(s).filter((x) => x.tick > mb);
      const first = rm.find((x) => x.shots === rm[0].shots + 1);
      R.dead_ticks = first ? first.tick - s_launch : 999; // from the launch to the next shot that is accepted (the mash presses until one is)
      R.mash_shots = rm.at(-1).shots - rm[0].shots;
    }
    await run(300);
    // ---- during the flight and the settle: the aim, the ammunition and a press ----
    const tAimStart = await S();
    const base = tAimStart['game.tick'];
    await seg([tap(at + 3, 'south', 2)]); // a shot
    let waited = 0;
    for (; waited < 40; waited++) { await run(8); if ((await S())['game.ready']) break; }
    R.ready_after_frames = (waited + 1) * 8;
    await seg([{ frame: at + 2, buttons: ['up'], until: at + 14 }, tap(at + 4, 'west', 2), tap(at + 20, 'south', 2), tap(at + 30, 'south', 2)]);
    await run(160);
    s = await S();
    const r4 = ring(s).filter((x) => x.tick > base);
    const shotsAfter = s['game.shots'];
    const preaim = r4.some((x, i) => i && x.state !== 0 && Math.abs(x.pitch - r4[i - 1].pitch) > 1e-7);
    const ammoDuring = r4.some((x, i) => i && x.state !== 0 && x.ammo !== r4[i - 1].ammo);
    R.second_shot = { shots_after: shotsAfter, preaim, ammo_during: ammoDuring };
    R.cue = { press: s['ui.cue_press'], draw: s['ui.cue_draw'] };
    R.buffered = shotsAfter - tAimStart['game.shots'] >= 3;
    { const fl = r4.filter((x) => x.flags & 1).map((x) => x.tick), rises = r4.filter((x, i) => i && x.shots > r4[i - 1].shots).map((x) => x.tick); R.fire_in_collapse = Math.min(99, ...fl.map((p) => { const r = rises.find((x) => x >= p); return r === undefined ? 99 : r - p; })); R.dbg = { base, fl, rises, st: (() => { const rl = []; for (const x of r4) { const l = rl.at(-1); if (l && l.s === x.state && l.sh === x.shots) l.n++; else rl.push({ s: x.state, sh: x.shots, n: 1, t: x.tick }); } return rl.map((x) => `${x.t}:${x.s}/${x.sh}x${x.n}`).join(' '); })() }; }
    // ---- frames ----
    R.frames = { n: s['feel.frames'], over33: s['feel.over33'], worst_ms: s['feel.worst_ms'] };
    R.slow = [];
    for (let i = 0; i < Math.min(s['feel.slow_n'], 12); i++) R.slow.push({ tick: s['feel.slow_tick'][i], state: s['feel.slow_state'][i], ms: +s['feel.slow_ms'][i].toFixed(1), work: +s['feel.slow_work'][i].toFixed(1), tick_ms: +s['feel.slow_tickms'][i].toFixed(1), world: +s['feel.slow_world'][i].toFixed(1), post: +s['feel.slow_post'][i].toFixed(1) });
    // ---- report ----
    console.log(JSON.stringify(R, null, 1));
    const ctl = control ? 'control (old build) ' : '';
    // in the control only the checks the old behaviour must fail count (disc); the rest are reported
    const chk = (ok, what, disc = true) => (control ? (disc ? say(!ok, `${ctl}${what} fails as it must`) : console.log(`info ${what}: ${ok ? 'holds' : 'fails'} on the old build too`)) : say(ok, what));
    chk(a1.latency <= 1, `aim ${gpu}: a full push moves the launcher within 1 tick (${a1.latency})`, false);
    chk(analog[0.3].latency <= 1 && Math.abs(analog[0.3].v10) > 0.01, `aim ${gpu}: a push just past the dead zone moves it, slowly (${analog[0.3].v10.toFixed(3)} rad/s at 10 ticks)`);
    chk(a1.v25 >= 0.9 && a1.v25 > 4 * analog[0.3].v25, `aim ${gpu}: a full push is quick (${a1.v25.toFixed(2)} rad/s) and a light one fine (${analog[0.3].v25.toFixed(3)})`);
    chk(tapDeg < 0.1 && tapDeg > 0.02, `aim ${gpu}: a one-frame tap is a fine step (${tapDeg.toFixed(3)} deg)`, false);
    chk(R.sweep_s <= 1.2, `aim ${gpu}: a held d-pad sweeps half a radian in ${R.sweep_s.toFixed(2)} s (at most 1.2)`);
    chk(R.speed_step <= 0.3, `aim ${gpu}: the speed eases, on and off (largest tick-to-tick change ${(R.speed_step * 100).toFixed(0)}% of the top speed, at most 30%)`);
    chk(R.orbit.cam_latency <= 6, `orbit ${gpu}: the camera moves ${R.orbit.cam_latency} ticks after the stick (at most 6)`, false);
    chk(R.ammo_latency <= 0, `ammo ${gpu}: West changes it in the tick it is read (${R.ammo_latency})`, false);
    chk(R.fire_latency >= 0 && R.fire_latency <= 3, `fire ${gpu}: South to the shot leaving in ${R.fire_latency} ticks (at most 3)`, false);
    chk(R.fire_in_collapse <= 3, `fire ${gpu}: South while the last shot's rubble still settles: the shot leaves in ${R.fire_in_collapse} ticks (at most 3)`, false);
    chk(R.dead_ticks <= 120, `dead ${gpu}: the launcher is ready ${R.dead_ticks} ticks after a shot (at most 120)`);
    const cd = (k) => (R.cue.draw[k] > 0 ? R.cue.draw[k] - R.cue.press[k] : 99);
    chk(cd(0) <= 1, `cue ${gpu}: a press that is only remembered shows its "fire queued" mark in the frame it is read (${cd(0)} frames after)`);
    chk(cd(1) <= 1, `cue ${gpu}: an ammunition press shows its pressed state in the frame it is read (${cd(1)} frames after)`);
    chk(R.buffered && R.second_shot.preaim && R.second_shot.ammo_during, `respond ${gpu}: a press, the aim and the ammo during the flight and settle reach the game (shots ${shotsAfter}, preaim ${preaim}, ammo ${ammoDuring}, ignored ${s['feel.ignored']})`);
    if (!control && frameGate) say(R.frames.over33 <= 2, `frames ${gpu}: ${R.frames.over33} of ${R.frames.n} frames over 16 ms of CPU, worst ${R.frames.worst_ms.toFixed(1)} ms on this lane`);
  } catch (e) { say(false, `${gpu}: ${e.stack || e.message}`); } finally { await h.close(); }
  results[gpu] = R;
}
for (const gpu of gpusArg()) { // the winning blow's slow-motion beat: about 0.4 s at 0.35x, eased in and out, the card's keys never held
  const h = await openCart('destructo_auto', { gpu, tag: 'feel-beat' });
  try {
    for (const [k, v] of Object.entries({ LEVEL_NAME: 'First Stones', QUALITY: 3, ...(control ? { OLD_FEEL: 1 } : {}) })) await h.command(`dm set ${k} "${v}"`);
    await h.command('dm restart');
    await h.step(1500 - h.frame);
    const s = await h.state();
    const ok = s['beat.ticks'] >= 12 && s['beat.ticks'] <= 26 && s['beat.min'] > 0.3 && s['beat.min'] <= 0.37;
    console.log(`${control ? 'control (old build) ' : ''}beat ${gpu}: ${s['beat.ticks']} ticks slowed, slowest clock ${(+s['beat.min']).toFixed(2)}, result ${s['result.last']}`);
    if (control) say(!ok, 'control (old build) beat: no slow-motion beat fails as it must'); else say(ok && s['result.last'] > 0, `beat ${gpu}: the winning blow runs ${s['beat.ticks']} ticks at down to ${(+s['beat.min']).toFixed(2)}x`);
  } catch (e) { say(false, `beat ${gpu}: ${e.stack || e.message}`); } finally { await h.close(); }
}
if (jsonOut) writeFileSync(jsonOut, JSON.stringify(results, null, 1));
process.exit(control ? 0 : bad ? 1 : 0);
