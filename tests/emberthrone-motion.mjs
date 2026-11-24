// emberthrone motion check: how the characters move, measured from the bones, not judged from stills. One run of the
// game on romdev (the RELEASE cart, seed 1) with a human-like pad script: run, stop, a lock-on, strafes both ways, a
// three-cut combo, a roll, a heavy, a back step, a back walk, a cut, then run, turn and stop again. The cart's motion
// recorder (view.c "motion rec") keeps the player's bones in the model frame each tick; the measures:
//   overlap  through each sword cut, every upper-body joint's own turning speed (the spine, chest, head, shoulder,
//            elbow, wrist, each against its parent): sync, the mean correlation of those speed curves, and spread, how
//            many ticks their peaks scatter. Keyed poses on shared ease curves turn every joint together (sync near 1,
//            every peak on one tick); a body that whips breaks its joints one after another.
//   jerk     the peak and mean angular jerk of the measured bones through the cuts (rad/s^3), reported
//   pops     a bone's turn in one tick above the mean of its turns the ticks either side (an isolated jump, not a fast
//            move): the largest at a clip change, against the largest anywhere else
//   cape     the Ember King's cape (the heroes wear none), worn by the player for the scripted stops ("motion cape 1":
//            the King never stands still in a fight): how far the middle of its hem travels against the chest in the
//            two seconds after a run stops, and
//            how much less it moves in the last half second than in the first: a cape that swings and settles; and in
//            a second run (the bot at the King's throne) how fast his own cape moves against his chest (m/s, >= 0.05)
//   lean     the trunk's sideways tilt (the line from the hips to the head) against the turn's sideways pull (speed x
//            turn rate) while running, both averaged over three quarters of a second (a held lean, not each step's sway)
//   aim      the head's angle off the locked target while locked on and moving or standing (not mid-blow)
//   idle     in the stand, how many upper-body joints move as one pure sine wave (97% or more of their motion one
//            frequency): a sine loop's every joint does, a breathing, shifting body's do not
// Bars (PASS needs all): over at least three cuts, sync <= 0.6 (mean) and spread >= 2 ticks (median), pops at changes
// <= 15 degrees, cape travel >= 0.08 m with the last half second under 0.35 of the first, lean
// correlation >= 0.5 with at least 2 degrees, at most one pure-sine joint in the stand (of six or more measured,
// over 60 ticks or more).
// CONTROL, which must fail: the same run on the old build (the procedural clips): --control <cart dir>.
//
//   node tests/emberthrone-motion.mjs run <cart dir> <out dir> [--video] [--gpu gl|webgpu]   the run (trace.json, video, strips)
//   node tests/emberthrone-motion.mjs measure <run dir> [--control <run dir>] [--json]   (trace.json, boss.json)
import { writeFileSync, readFileSync, mkdirSync, existsSync, statSync, unlinkSync, openSync, readSync, closeSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';

const TPS = 60;

// the pad script: [frames, {buttons, lx, ly}] steps from the load
export function script() {
  const S = [];
  const add = (n, p = {}) => S.push([n, p]);
  add(20); add(2, { b: ['start'] }); add(18); add(2, { b: ['start'] }); add(68); // the title and the save slot; 60: "from 3", the road
  add(120, { ly: -1 });                          // 110: run up the road
  add(120);                                      // 230: stop (the cape settles)
  add(60, { ly: -1 });                           // 350: run
  for (let k = 1; k <= 6; k++) { const t = (k / 6) * Math.PI / 2; add(10, { lx: Math.sin(t), ly: -Math.cos(t) }); } // 410: turn right
  add(60, { lx: 1 });                            // 470: run right
  add(120);                                      // 530: stop; 650: "from 4", the raiders' meadow
  add(20); add(240, { ly: -1 });                 // 670: run at them
  add(2, { b: ['r'] }); add(8);                  // 910: lock on
  add(60, { lx: -1 }); add(60, { lx: 1 });       // 920: strafe left, then right
  add(2, { b: ['west'] }); add(20); add(2, { b: ['west'] }); add(22); add(2, { b: ['west'] }); add(52); // 1040: the combo
  add(2, { b: ['south'], lx: 1 }); add(58);      // 1140: a roll
  add(2, { b: ['north'] }); add(78);             // 1200: a heavy
  add(2, { b: ['south'] }); add(38);             // 1280: a back step
  add(60, { ly: 1 });                            // 1320: walk back
  add(2, { b: ['west'] }); add(58);              // 1380: a cut
  return S;
}
export const COMMANDS = [{ frame: 60, command: 'from 3' }, { frame: 64, command: 'motion cape 1' }, { frame: 650, command: 'from 4' }, { frame: 652, command: 'motion cape 0' }]; // (the test cape for the stops only: the fight is as the game plays)
export const FRAMES = script().reduce((s, [n]) => s + n, 0);
const REC_AT = 100, REC_N = FRAMES - REC_AT - 4;

function steps() {
  return script().map(([frames, p]) => {
    const port = {};
    for (const b of p.b || []) port[b] = true;
    if (p.lx || p.ly) port.axes = { lx: p.lx || 0, ly: p.ly || 0 };
    return { input: { ports: [port] }, frames };
  });
}

async function run(cartDir, out, { video = false, gpu = 'gl' } = {}) {
  process.env.DESTRUCTO_CART_DIR = resolve(cartDir);
  const { openCart, call } = await import('./lib/romdev.mjs');
  mkdirSync(out, { recursive: true });
  const h = await openCart('emberthrone', { gpu, tag: 'motion' });
  try {
    const dump = join(out, 'run.cwfd');
    const r = await call('frame', { op: 'record', frames: FRAMES - 1, steps: steps(), commandsAt: [...COMMANDS, { frame: REC_AT, command: `motion rec ${REC_N}` }].sort((x, y) => x.frame - y.frame), path: dump, downscale: video ? 1 : 8, maxSeconds: 1800 }, h.session, { timeoutMs: 2400000 });
    const info = await h.command('motion info');
    const rows = [];
    for (let k = 0; k < info.rows; k += 40) rows.push(...(await h.command(`motion get ${k} 40`)));
    writeFileSync(join(out, 'trace.json'), JSON.stringify({ bones: info.bones, rows }));
    console.log(`${out}: ${rows.length} rows, record ${JSON.stringify(r).slice(0, 200)}`);
    if (video) frames(dump, out);
    if (existsSync(dump)) unlinkSync(dump);
  } finally { await h.close(); }
  // the cape: the Ember King's (the heroes wear none), as the bot fights him at his throne
  const b = await openCart('emberthrone_auto', { gpu, tag: 'motion-boss' });
  try {
    const dump = join(out, 'boss.cwfd'), o = join(out, 'boss');
    mkdirSync(o, { recursive: true });
    await call('frame', { op: 'record', frames: BOSS_FRAMES, commandsAt: [{ frame: 30, command: 'from 183' }, { frame: 90, command: `motion rec ${BOSS_FRAMES - 100} boss` }], path: dump, downscale: video ? 1 : 8, maxSeconds: 1800 }, b.session, { timeoutMs: 2400000 });
    const info = await b.command('motion info');
    const rows = [];
    for (let k = 0; k < info.rows; k += 40) rows.push(...(await b.command(`motion get ${k} 40`)));
    writeFileSync(join(out, 'boss.json'), JSON.stringify({ bones: info.bones, rows }));
    if (video) frames(dump, o, { whole: true });
    if (existsSync(dump)) unlinkSync(dump);
    console.log(`${o}: ${rows.length} rows`);
  } finally { await b.close(); }
}
const BOSS_FRAMES = 1300;

// the dump: an mp4 at half size, and native-scale crops round the figure for the strips (every 4th frame of each named moment)
export const MOMENTS = { stop: [226, 330], turn: [400, 540], settle: [526, 640], strafe: [916, 1040], combo: [1038, 1140], roll: [1138, 1200], heavy: [1198, 1280], backstep: [1278, 1320], back: [1318, 1440] };
function frames(dump, out, { whole = false } = {}) {
  const fd = openSync(dump, 'r'), hd = Buffer.alloc(16);
  readSync(fd, hd, 0, 16, 0);
  const w = hd.readUInt32LE(4), hh = hd.readUInt32LE(8), fsz = w * hh * 3, n = Math.floor((statSync(dump).size - 16) / fsz);
  closeSync(fd);
  // streamed (a dump is gigabytes): the header skipped by tail
  const ff = spawnSync('bash', ['-c', `tail -c +17 "$0" | ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s ${w}x${hh} -r 60 -i - -vf scale=iw/2:ih/2 -c:v libx264 -crf 20 -pix_fmt yuv420p "$1"`, dump, join(out, 'run.mp4')]);
  if (ff.status) throw new Error(`ffmpeg: ${ff.stderr}`);
  const fd2 = openSync(dump, 'r'), buf = Buffer.alloc(fsz);
  mkdirSync(join(out, 'frames'), { recursive: true });
  if (whole) { // a fight: every 30th frame, the whole picture at half size (the bot's camera wanders)
    const list = [];
    for (let f = 30; f < n; f += 30) {
      readSync(fd2, buf, 0, fsz, 16 + f * fsz);
      const png = join(out, 'frames', `f-${String(f).padStart(4, '0')}.png`);
      spawnSync('convert', ['-size', `${w}x${hh}`, '-depth', '8', 'rgb:-', png], { input: buf });
      list.push(png);
    }
    spawnSync('montage', [...list, '-tile', '6x', '-geometry', `${w / 2}x${hh / 2}+2+2`, '-background', '#111', join(out, 'strip.png')]);
    closeSync(fd2);
    return;
  }
  for (const [name, [a, b]] of Object.entries(MOMENTS)) {
    const list = [];
    for (let f = a; f < Math.min(b, n); f += 4) {
      readSync(fd2, buf, 0, fsz, 16 + f * fsz);
      const png = join(out, 'frames', `${name}-${String(f).padStart(4, '0')}.png`);
      spawnSync('convert', ['-size', `${w}x${hh}`, '-depth', '8', 'rgb:-', '-crop', `${Math.round(w * 0.3125)}x${Math.round(hh * 0.5556)}+${Math.round(w * 0.3438)}+${Math.round(hh * 0.2778)}`, '+repage', png], { input: buf }); // native pixels round the figure (the camera keeps it there)
      list.push(png);
    }
    spawnSync('montage', [...list, '-tile', '8x', '-geometry', '+2+2', '-background', '#111', join(out, `strip-${name}.png`)]);
  }
  closeSync(fd2);
}

// ── measures ──
const qmul = (a, b) => [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0], a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
const qinv = (q) => [-q[0], -q[1], -q[2], q[3]];
const qrot = (q, v) => { const r = qmul(qmul(q, [v[0], v[1], v[2], 0]), qinv(q)); return [r[0], r[1], r[2]]; };
const qangle = (a, b) => { const d = Math.abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]); return 2 * Math.acos(Math.min(1, d)); };
// the rotation vector taking a to b (rad), in the model frame
const qdelta = (a, b) => { let d = qmul(b, qinv(a)); if (d[3] < 0) d = d.map((x) => -x); const s = Math.hypot(d[0], d[1], d[2]), th = 2 * Math.atan2(s, d[3]); return s < 1e-9 ? [0, 0, 0] : [d[0] / s * th, d[1] / s * th, d[2] / s * th]; };
const deg = (r) => (r * 180) / Math.PI;
const mean = (a) => (a.length ? a.reduce((s, x) => s + x, 0) / a.length : 0);

export function parse(trace) {
  const B = trace.bones, nb = B.length;
  return trace.rows.map((r) => {
    const o = { tick: r[0], state: r[1], atk: r[2], st_t: r[3], target: r[4], clip: r[5], speed: r[6], yaw: r[7], pos: [r[8], r[9], r[10]], tgt: [r[11], r[12], r[13]], cape: [r[14], r[15], r[16]], q: {}, p: {} };
    for (let b = 0; b < nb; b++) { const k = 17 + b * 7; o.q[B[b]] = r.slice(k, k + 4); o.p[B[b]] = r.slice(k + 4, k + 7); }
    return o;
  });
}

const ST_MOVE = 0, ST_ATTACK = 1; // game.h states
export function measure(trace, boss = null) {
  const R = parse(trace), bones = trace.bones;
  // angular velocity of a bone (model frame) per tick, rad/tick
  const omega = (b) => R.map((r, i) => (i ? qdelta(R[i - 1].q[b], r.q[b]) : [0, 0, 0]));
  const W = Object.fromEntries(bones.map((b) => [b, omega(b)]));
  const mag = (v) => Math.hypot(v[0], v[1], v[2]);
  // the cuts: the player's sword blows (the rules' attack state, one blow from its first tick to its last), and the
  // clip they play
  const isCut = (c) => /^(slash1|slash2|slash3|heavy)$/.test(c);
  const cuts = [];
  for (let i = 0; i < R.length;) {
    if (R[i].state !== ST_ATTACK || !isCut(R[i].clip)) { i++; continue; }
    let j = i + 1;
    while (j < R.length && R[j].state === ST_ATTACK && R[j].atk === R[i].atk && R[j].st_t > R[j - 1].st_t) j++;
    if (j - i >= 8) cuts.push([i, Math.min(R.length, j + 4), R[i].clip]);
    i = j;
  }
  // overlap: each upper-body joint's own turning speed (against its parent) through a cut; keyed poses on shared ease
  // curves move every joint together (their speed curves alike, every peak on the same tick), a body that whips does
  // not (the hips and spine lead, the shoulder, elbow and wrist break one after another, the head follows)
  const PARENT = { Spine: 'Hips', Chest: 'Spine', Head: 'Neck', UpperArmR: 'Chest', LowerArmR: 'UpperArmR', HandR: 'LowerArmR' };
  const UPPER = Object.keys(PARENT);
  const local = (k, b) => qmul(qinv(R[k].q[PARENT[b]]), R[k].q[b]);
  const corrOf = (a, b) => { const ma = mean(a), mb = mean(b); let n = 0, da = 0, db = 0; for (let i = 0; i < a.length; i++) n += (a[i] - ma) * (b[i] - mb), da += (a[i] - ma) ** 2, db += (b[i] - mb) ** 2; return da && db ? n / Math.sqrt(da * db) : null; };
  const sync = [], spread = [], peakTicks = [];
  const jerks = [];
  for (const [i, j] of cuts) {
    const S = {};
    for (const bn of UPPER) { S[bn] = []; for (let k = i + 1; k < j; k++) S[bn].push(qangle(local(k - 1, bn), local(k, bn))); }
    const cs = [];
    for (let x = 0; x < UPPER.length; x++) for (let y = x + 1; y < UPPER.length; y++) { const c = corrOf(S[UPPER[x]], S[UPPER[y]]); if (c !== null) cs.push(c); }
    sync.push(+mean(cs).toFixed(2));
    const pk = UPPER.map((bn) => S[bn].indexOf(Math.max(...S[bn])));
    const mp = mean(pk);
    spread.push(+Math.sqrt(mean(pk.map((p) => (p - mp) ** 2))).toFixed(1));
    peakTicks.push(pk.join(','));
    for (const bn of bones) for (let k = i + 2; k < j - 1; k++) {
      const a = W[bn][k + 1], m = W[bn][k], c = W[bn][k - 1];
      jerks.push(mag([a[0] - 2 * m[0] + c[0], a[1] - 2 * m[1] + c[1], a[2] - 2 * m[2] + c[2]]) * TPS * TPS * TPS);
    }
  }
  const median = (a) => { const t = [...a].sort((x, y) => x - y); return t.length ? t[t.length >> 1] : 0; };
  // pops: a bone's turn in one tick standing out from its turns the ticks either side (an isolated jump, not a fast
  // move), the largest at a clip change (that tick and the next two) and the largest anywhere else; a teleport is skipped
  let popAt = 0, popElse = 0, popWhere = null;
  const change = new Set();
  for (let i = 1; i < R.length; i++) if (R[i].clip !== R[i - 1].clip || (R[i].state !== R[i - 1].state)) for (let k = i; k < Math.min(R.length, i + 3); k++) change.add(k);
  const jumped = (i) => Math.hypot(R[i].pos[0] - R[i - 1].pos[0], R[i].pos[2] - R[i - 1].pos[2]) > 1;
  for (let i = 2; i < R.length - 1; i++) {
    if (R[i].tick !== R[i - 1].tick + 1 || jumped(i) || jumped(i + 1) || jumped(i - 1)) continue;
    let m = 0, mb = '';
    for (const b of bones) { const a = deg(mag(W[b][i]) - (mag(W[b][i - 1]) + mag(W[b][i + 1])) / 2); if (a > m) m = a, mb = b; }
    if (change.has(i)) { if (m > popAt) popAt = m, popWhere = { tick: R[i].tick, bone: mb, from: R[i - 1].clip, to: R[i].clip }; } else popElse = Math.max(popElse, m);
  }
  // cape: the second after each stop from a run
  const capes = [];
  const CR = R; // the cape's settle: the player's scripted stops, wearing the King's cape ("motion cape 1")
  const BR = boss ? parse(boss) : null; // the King's own cape in his fight: how much it moves against his chest
  let bossCape = null;
  if (BR) { let p = 0; for (let k = 1; k < BR.length; k++) p += Math.hypot(BR[k].cape[0] - BR[k - 1].cape[0], BR[k].cape[1] - BR[k - 1].cape[1], BR[k].cape[2] - BR[k - 1].cape[2]); bossCape = +(p / Math.max(1, BR.length - 1) * TPS).toFixed(3); }
  const WIN = 2 * TPS; // two seconds: a cape's swing is longer than a second
  for (let i = 10; i < CR.length - WIN; i++) {
    if (!(CR[i - 1].speed > 2 && CR[i].speed < 0.35 && CR[i].state === ST_MOVE && CR[i - 1].state === ST_MOVE && CR[i].target < 0)) continue;
    if (capes.length && CR[i].tick - capes[capes.length - 1].tick < WIN) continue;
    // the hem against the chest (a cape skinned to the chest never moves there)
    const hem = (k) => CR[k].cape;
    let path = 0, first = 0, last = 0;
    for (let k = i + 1; k < i + WIN; k++) {
      const a = hem(k), b = hem(k - 1), d = Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
      path += d;
      if (k < i + WIN / 4) first += d; else if (k >= i + (3 * WIN) / 4) last += d;
    }
    capes.push({ tick: CR[i].tick, path, first, last });
  }
  // lean: chest roll (its up vector's sideways share, model frame) against speed x turn rate while running
  const xs = [], ys = [];
  for (let i = 1; i < R.length; i++) {
    if (R[i].speed < 2.5 || R[i].state !== R[i - 1].state || R[i].target >= 0) continue;
    let dy = R[i].yaw - R[i - 1].yaw; while (dy > Math.PI) dy -= 2 * Math.PI; while (dy < -Math.PI) dy += 2 * Math.PI;
    const hp = R[i].p.Hips, hd = R[i].p.Head; // the trunk's line from the hips to the head, its sideways tilt
    xs.push(R[i].speed * dy * TPS); ys.push(deg(Math.atan2(hd[0] - hp[0], hd[1] - hp[1])));
  }
  // a lean is a held tilt: both series over a stride (half a second) so a run's own sway from step to step averages out
  const box = (a, n = 45) => a.map((_, i) => { let t = 0, c = 0; for (let k = Math.max(0, i - (n >> 1)); k <= Math.min(a.length - 1, i + (n >> 1)); k++) t += a[k], c++; return t / c; });
  const xsS = box(xs), ysS = box(ys);
  const corr = (a, b) => { const ma = mean(a), mb = mean(b); let n = 0, da = 0, db = 0; for (let i = 0; i < a.length; i++) n += (a[i] - ma) * (b[i] - mb), da += (a[i] - ma) ** 2, db += (b[i] - mb) ** 2; return da && db ? n / Math.sqrt(da * db) : 0; };
  // which way leans into the turn: a turn to the left (yaw rising) pulls the body to the left (+x in the model)
  const turning = xsS.map((x, i) => [x, ysS[i]]).filter(([x]) => Math.abs(x) > 1.5);
  const leanCorr = corr(xsS, ysS), leanAmp = turning.length ? mean(turning.map(([x, y]) => Math.abs(y - mean(ysS)))) : 0;
  // aim: head forward (+z) against the target while locked
  const aims = [];
  for (const r of R) {
    if (r.target < 0 || r.state !== ST_MOVE) continue; // locked on, standing, walking or sidestepping (a blow turns the head its own way)
    const c = Math.cos(r.yaw), s = Math.sin(r.yaw), toW = (v) => [v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c];
    const f = toW(qrot(r.q.Head, [0, 0, 1])), hp = toW(r.p.Head), hw = [r.pos[0] + hp[0], r.pos[1] + hp[1], r.pos[2] + hp[2]];
    const d = [r.tgt[0] - hw[0], r.tgt[1] - hw[1], r.tgt[2] - hw[2]], dl = Math.hypot(...d);
    if (dl < 0.5) continue;
    aims.push(deg(Math.acos(Math.max(-1, Math.min(1, (f[0] * d[0] + f[1] * d[1] + f[2] * d[2]) / dl)))));
  }
  // idle: the stand (from a second after it stops, for at least two thirds of a second more): for each upper-body joint
  // (against its parent), how much of its motion one sine wave explains (the strongest frequency's share of the
  // variance of its main axis of turn). A sine loop scores 1 on every joint it moves; a breathing, shifting body does not
  const IDLE_J = { Spine: 'Hips', Chest: 'Spine', Head: 'Neck', UpperArmR: 'Chest', LowerArmR: 'UpperArmR', HandR: 'LowerArmR', UpperArmL: 'Chest', LowerArmL: 'UpperArmL' };
  let idleSine = 0, idleJoints = 0, idleTicks = 0;
  {
    let i = 0; const spans = [];
    const still = (k) => R[k].speed < 0.2 && R[k].state === ST_MOVE && !(k && Math.hypot(R[k].pos[0] - R[k - 1].pos[0], R[k].pos[2] - R[k - 1].pos[2]) > 1); // (a jump ahead ends a stand)
    while (i < R.length) { if (still(i)) { let j = i + 1; while (j < R.length && still(j)) j++; if (j - i >= 100) spans.push([i + 60, j]); i = j; } else i++; }
    for (const [s0, e] of spans) {
      idleTicks += e - s0;
      for (const [bn, par] of Object.entries(IDLE_J)) {
        const loc = (k) => qmul(qinv(R[k].q[par]), R[k].q[bn]), ref = loc(s0), comps = [[], [], []];
        for (let k = s0; k < e; k++) { let d = qmul(qinv(ref), loc(k)); if (d[3] < 0) d = d.map((x) => -x); for (let c = 0; c < 3; c++) comps[c].push(2 * d[c]); }
        let best = null;
        for (const x of comps) { const m = mean(x), y = x.map((v) => v - m), tot = y.reduce((t, v) => t + v * v, 0); if (!best || tot > best.tot) best = { y, tot }; }
        if (best.tot < 1e-8) continue; // a joint that does not move
        const n = best.y.length;
        let pk = 0;
        for (let fq = 1; fq < n / 2; fq++) { let re = 0, im = 0; for (let t = 0; t < n; t++) re += best.y[t] * Math.cos((2 * Math.PI * fq * t) / n), im += best.y[t] * Math.sin((2 * Math.PI * fq * t) / n); pk = Math.max(pk, (2 * (re * re + im * im)) / n); }
        idleJoints++;
        if (pk / best.tot >= 0.97) idleSine++;
      }
    }
  }
  const capeOk = capes.length > 0 && capes.every((c) => c.path >= 0.08 && c.last <= c.first * 0.35);
  const m = {
    rows: R.length, cuts: cuts.map(([i, j, c]) => `${c}@${R[i].tick}+${j - i}`),
    sync: +mean(sync).toFixed(2), spread: +median(spread).toFixed(1), per_cut: { sync, spread, peak_ticks: peakTicks },
    jerk_peak: Math.round(Math.max(0, ...jerks)), jerk_mean: Math.round(mean(jerks)),
    pop_at_change: +popAt.toFixed(1), pop_elsewhere: +popElse.toFixed(1), pop_where: popWhere,
    boss_cape_speed: bossCape,
    cape: capes.map((c) => ({ tick: c.tick, path: +c.path.toFixed(3), first: +c.first.toFixed(3), last: +c.last.toFixed(3) })),
    lean_corr: +leanCorr.toFixed(2), lean_deg: +leanAmp.toFixed(1), lean_samples: xs.length,
    aim_mean_deg: +mean(aims).toFixed(1), aim_samples: aims.length,
    idle_sine: idleSine, idle_joints: idleJoints, idle_ticks: idleTicks,
  };
  m.checks = {
    overlap: cuts.length >= 3 && m.sync <= 0.6 && m.spread >= 2,
    pops: m.pop_at_change <= 15,
    cape: capeOk && (bossCape === null || bossCape >= 0.05),
    lean: m.lean_corr >= 0.5 && m.lean_deg >= 2,
    idle: m.idle_joints >= 6 && m.idle_ticks >= 60 && m.idle_sine <= 1,
  };
  m.pass = Object.values(m.checks).every(Boolean);
  return m;
}

// the foes and the bosses as the bot fights them (emberthrone_auto jumped to each place), for the eye: a video and
// strips of each
export const FIGHTS = [['raiders', 4, 900], ['thornwarden', 12, 900], ['tidecaller', 22, 900], ['emberking', 183, 900]];
async function fights(cartDir, out, only) {
  process.env.DESTRUCTO_CART_DIR = resolve(cartDir);
  const { openCart, call } = await import('./lib/romdev.mjs');
  for (const [name, from, n] of FIGHTS) {
    if (only && only.length && !only.includes(name)) continue;
    const o = join(out, name);
    mkdirSync(o, { recursive: true });
    const h = await openCart('emberthrone_auto', { gpu: 'gl', tag: 'fight-' + name });
    try {
      const dump = join(o, 'run.cwfd');
      await call('frame', { op: 'record', frames: n, commandsAt: [{ frame: 30, command: `from ${from}` }], path: dump, downscale: 1, maxSeconds: 1800 }, h.session, { timeoutMs: 2400000 });
      frames(dump, o, { whole: true });
      unlinkSync(dump);
      console.log(o);
    } finally { await h.close(); }
  }
}

// what the motion costs: the bot in the Tidecaller's fight (the boss, its foes, the players), 1080p High, frame
// time on romdev (CPU in the frame call, GPU), GL and WebGPU, after a warm-up
async function cost(cartDir) {
  process.env.DESTRUCTO_CART_DIR = resolve(cartDir);
  const { openCart } = await import('./lib/romdev.mjs');
  const out = {};
  for (const gpu of ['gl', 'webgpu']) {
    const h = await openCart('emberthrone_auto', { gpu, tag: 'cost-' + gpu, load: gpu === 'webgpu' ? { gpuTiming: true } : {} });
    try {
      await h.command('from 22');
      const r = await h.timing({ frames: 600, warmup: 300, window: 120 });
      const pick = (k) => (r[k] ? { mean: r[k].mean, p95: r[k].p95 } : null);
      out[gpu] = { wall: pick('wallMs'), cpu: pick('cpuMs'), gpu: pick('gpuMs'), contention: r.contention ? JSON.stringify(r.contention).slice(0, 200) : null };
    } finally { await h.close(); }
  }
  return out;
}

const [mode, ...args] = process.argv.slice(2);
if (mode === 'cost') {
  console.log(JSON.stringify(await cost(args[0]), null, 1));
} else if (mode === 'fights') {
  await fights(args[0], args[1], args.slice(2));
} else if (mode === 'frames') {
  frames(args[0], args[1]);
} else if (mode === 'run') {
  const gi = args.indexOf('--gpu');
  await run(args[0], args[1], { video: args.includes('--video'), gpu: gi >= 0 ? args[gi + 1] : 'gl' });
} else if (mode === 'measure') {
  const load = (d) => measure(JSON.parse(readFileSync(join(d, 'trace.json'), 'utf8')), existsSync(join(d, 'boss.json')) ? JSON.parse(readFileSync(join(d, 'boss.json'), 'utf8')) : null);
  const m = load(args[0]);
  const ci = args.indexOf('--control');
  const c = ci >= 0 ? load(args[ci + 1]) : null;
  if (args.includes('--json')) console.log(JSON.stringify({ run: m, control: c }, null, 1));
  else {
    const line = (n, x) => console.log(`${n.padEnd(8)} ${x.pass ? 'PASS' : 'FAIL'} sync ${x.sync} spread ${x.spread} jerk ${x.jerk_peak}/${x.jerk_mean} pops ${x.pop_at_change} (else ${x.pop_elsewhere}) cape ${JSON.stringify(x.cape)} king's cape ${x.boss_cape_speed} m/s lean ${x.lean_corr} ${x.lean_deg}deg aim ${x.aim_mean_deg} idle ${x.idle_sine}/${x.idle_joints} pure sines (${x.idle_ticks} ticks) checks ${JSON.stringify(x.checks)}`);
    line('run', m); if (c) line('control', c);
  }
  const ok = m.pass && (!c || !c.pass);
  if (c && c.pass) console.log('CONTROL PASSED: the measure proves nothing');
  process.exit(ok ? 0 : 1);
} else {
  console.error('usage: emberthrone-motion.mjs run <cart dir> <out dir> [--video] [--gpu gl|webgpu] | measure <run dir> [--control <run dir>] [--json] | fights <cart dir> <out dir> [name ...] | cost <cart dir>');
  process.exit(2);
}
