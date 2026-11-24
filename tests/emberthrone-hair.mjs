// emberthrone hair check: the hair, the beards and the garments (the Tidecaller's robe, the lancers' coats) on spring
// bones (view.c SWING, cartwheel's SpringBone3D), measured from the bones on romdev with a pad script played as one
// ranged call (seed 1, the play camera): run up the road, stop, run, turn right, run, stop, then turn about at a run and
// stop. The cart's recorder ("hair rec") keeps, each tick, each chain's lag (its tip from where its bone alone would
// carry it: the old rig, whose hair was skinned to the head and whose robe to the hips and the legs) and its depth in
// the body (hair: its moving joints and the points halfway between them in the skull, the jaw, the neck or the
// shoulders; a garment: its surface, sampled five by five in each panel, in the thighs, the shins or the feet; the same
// capsules the springs keep them out of), and the figure's skeleton modifier time. Each figure with parts that swing
// is measured, the player wearing it ("hair wear"): the wayfarer (her own, her two locks), the warden and the Ember
// King (beards), a lancer (long hair, a coat) and the Tidecaller (long hair, a robe). Per group of chains:
//   turn    each turn (the easing turn right, the turn about): the largest swing of the tips from where they hang at
//           rest, and the shift at which their swing's rise best follows the head's turn rate (reported)
//   settle  each stop from a run: the largest swing in the two seconds after it, and the ticks until the swing stays
//           under a fifth of that peak (and under 2 cm)
//   rest    the last second of each stand (up to 8 ticks before the next start): jitter, what is left of each tip's
//           motion after a 5-tick running mean (the idle's slow sway passes; a tick-to-tick shake does not), root mean
//           square and largest (mm)
//   clip    the deepest any chain or garment gets into the body over the whole run (mm; below 0 it stays clear by that
//           much), and the old rig's (the same chains carried by their bone) over the same run, reported
// Bars (PASS needs all, for every group): each turn swings the tips 10 mm or more (short chains, the locks and the
// beards, 6); each stop swings them 5 mm or more (short 3) and they settle within 60 ticks; at rest jitter at most
// 0.1 mm root mean square and 0.3 mm largest; clip at most 1 mm.
// CONTROLS, each of which must fail its check: "springs 0" (the old rig: every part carried by its bone; turn and
// settle), "collide 0" (the springs go through the body; clip; a beard never reaches the neck even so, reported),
// "soft 1" (every chain slack and undamped; settle), "skirt 0" (a garment's chains on their own; clip).
//
//   node tests/emberthrone-hair.mjs run <cart dir> <out dir> [--video] [--gpu gl|webgpu] [--only wayfarer,warden,...] [--variants on,springs0,collide0,soft,skirt0]
//   node tests/emberthrone-hair.mjs measure <out dir> [--json]
import { writeFileSync, readFileSync, mkdirSync, existsSync, statSync, unlinkSync, openSync, readSync, closeSync, readdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';

export const FIGURES = { wayfarer: -1, warden: 1, lancer: 4, tidecaller: 6, emberking: 7 }; // game.h K_WARDEN, K_LANCER, K_TIDE, K_EMBER (-1: her own)
export const VARIANTS = { on: [], springs0: ['hair springs 0'], collide0: ['hair collide 0'], soft: ['hair soft 1'], skirt0: ['hair skirt 0'] };
const GARMENT = {}; // the figures whose garment swings (skirt0 runs only on them): none yet (view.c SWING: the robe and the coats wait)

export function script() {
  const S = [];
  const add = (n, p = {}) => S.push([n, p]);
  add(20); add(2, { b: ['start'] }); add(18); add(2, { b: ['start'] }); add(68); // the title and the save slot; 60: "from 3", the road
  add(120, { ly: -1 });                          // 110: run up the road
  add(120);                                      // 230: stop
  add(60, { ly: -1 });                           // 350: run
  for (let k = 1; k <= 6; k++) { const t = (k / 6) * Math.PI / 2; add(10, { lx: Math.sin(t), ly: -Math.cos(t) }); } // 410: turn right
  add(60, { lx: 1 });                            // 470: run right
  add(120);                                      // 530: stop
  add(60, { lx: -1 });                           // 650: turn about at a run
  add(120);                                      // 710: stop
  return S;
}
export const FRAMES = script().reduce((s, [n]) => s + n, 0); // 830
const REC_AT = 100, REC_N = FRAMES - REC_AT - 4;
export const TURNS = { right: [400, 540], about: [646, 716] };

function steps() {
  return script().map(([frames, p]) => {
    const port = {};
    for (const b of p.b || []) port[b] = true;
    if (p.lx || p.ly) port.axes = { lx: p.lx || 0, ly: p.ly || 0 };
    return { input: { ports: [port] }, frames };
  });
}

async function run(cartDir, out, { video = false, gpu = 'gl', only = null, variants = Object.keys(VARIANTS) } = {}) {
  process.env.DESTRUCTO_CART_DIR = resolve(cartDir);
  const { openCart, call } = await import('./lib/romdev.mjs');
  for (const [fig, kind] of Object.entries(FIGURES)) {
    if (only && !only.includes(fig)) continue;
    for (const v of variants) {
      if (v === 'skirt0' && !GARMENT[fig]) continue;
      const o = join(out, `${fig}-${v}`);
      mkdirSync(o, { recursive: true });
      const h = await openCart('emberthrone', { gpu, tag: `hair-${fig}-${v}` });
      try {
        const cmds = [{ frame: 60, command: 'from 3' }];
        if (kind >= 0) cmds.push({ frame: 62, command: `hair wear ${kind}` });
        VARIANTS[v].forEach((c, k) => cmds.push({ frame: 64 + k, command: c }));
        cmds.push({ frame: REC_AT, command: `hair rec ${REC_N}` });
        const dump = join(o, 'run.cwfd');
        const keep = video && v === 'on';
        await call('frame', { op: 'record', frames: FRAMES - 1, steps: steps(), commandsAt: cmds, path: dump, downscale: keep ? 1 : 8, maxSeconds: 1800 }, h.session, { timeoutMs: 2400000 });
        const info = await h.command('hair info');
        const rows = [];
        for (let k = 0; k < info.rows; k += 12) rows.push(...(await h.command(`hair get ${k} 12`)));
        writeFileSync(join(o, 'trace.json'), JSON.stringify({ figure: fig, variant: v, info, rows }));
        console.log(`${o}: ${rows.length} rows, kind ${info.kind}, chains ${info.chains.map((c) => c.name).join(' ')}, resets ${info.resets}`);
        if (keep) crops(dump, o);
        if (existsSync(dump)) unlinkSync(dump);
      } finally { await h.close(); }
    }
  }
}

// native-scale crops round the player's head and shoulders (the camera keeps the figure in the middle of the frame,
// its head a little above the centre): every 2nd frame of each turn and each stop, a strip of each
export const MOMENTS = { turn: [404, 470], stop: [528, 592], about: [646, 700], stop2: [708, 770], rest: [790, 826] };
function crops(dump, out) {
  const fd = openSync(dump, 'r'), hd = Buffer.alloc(16);
  readSync(fd, hd, 0, 16, 0);
  const w = hd.readUInt32LE(4), hh = hd.readUInt32LE(8), fsz = w * hh * 3, n = Math.floor((statSync(dump).size - 16) / fsz);
  const buf = Buffer.alloc(fsz);
  mkdirSync(join(out, 'frames'), { recursive: true });
  const cw = Math.round(w * 0.125), ch = Math.round(hh * 0.222), cx = Math.round(w * 0.4375), cy = Math.round(hh * 0.33); // 240 x 240 at 1080p round the head
  for (const [name, [a, b]] of Object.entries(MOMENTS)) {
    const list = [];
    for (let f = a; f < Math.min(b, n); f += 2) {
      readSync(fd, buf, 0, fsz, 16 + f * fsz);
      const png = join(out, 'frames', `${name}-${String(f).padStart(4, '0')}.png`);
      spawnSync('convert', ['-size', `${w}x${hh}`, '-depth', '8', 'rgb:-', '-crop', `${cw}x${ch}+${cx}+${cy}`, '+repage', png], { input: buf });
      list.push(png);
    }
    spawnSync('montage', [...list, '-tile', '11x', '-geometry', '+2+2', '-background', '#111', join(out, `strip-${name}.png`)]);
  }
  // one whole frame mid-turn (where the crops come from)
  readSync(fd, buf, 0, fsz, 16 + 440 * fsz);
  spawnSync('convert', ['-size', `${w}x${hh}`, '-depth', '8', 'rgb:-', join(out, 'frame-0440.png')], { input: buf });
  closeSync(fd);
}

// ── measures ──
const mean = (a) => (a.length ? a.reduce((s, x) => s + x, 0) / a.length : 0);
export function parse(trace) {
  const n = trace.info.chains.length;
  return trace.rows.map((r) => {
    const o = { tick: r[0], state: r[1], speed: r[2], yaw: r[3], hyaw: r[4], at: [r[5], r[6], r[7]], us: r[8], lag: [], depth: [], rigid: [], deep: [] };
    for (let c = 0; c < n; c++) { const k = 9 + c * 6; o.lag.push([r[k], r[k + 1], r[k + 2]]); o.depth.push(r[k + 3]); o.rigid.push(r[k + 4]); o.deep.push(r[k + 5]); }
    return o;
  });
}
export function measure(trace) {
  const all = parse(trace), chains = trace.info.chains;
  const groups = [...new Set(chains.map((c) => c.group))];
  const at = (tick) => all.findIndex((r) => r.tick >= tick);
  // the head's turn rate (rad/tick)
  const turnRate = all.map((r, i) => { if (!i) return 0; let d = r.hyaw - all[i - 1].hyaw; while (d > Math.PI) d -= 2 * Math.PI; while (d < -Math.PI) d += 2 * Math.PI; return Math.abs(d); });
  const G = {};
  for (const g of groups) {
    const idx = chains.map((c, i) => (c.group === g ? i : -1)).filter((i) => i >= 0), n = idx.length;
    const short = chains[idx[0]].bones <= 2; // the locks and the beards: short chains, small swings
    const R = all.map((r) => ({ ...r, lag: idx.map((i) => r.lag[i]), depth: idx.map((i) => r.depth[i]), rigid: idx.map((i) => r.rigid[i]), deep: idx.map((i) => r.deep[i]) }));
    // where each tip hangs at rest: its lag over the last half second of the run (the last stand)
    const rest = Array.from({ length: n }, (_, c) => [0, 1, 2].map((x) => mean(R.slice(-30).map((r) => r.lag[c][x]))));
    const S = R.map((r) => Math.max(...r.lag.map((l, c) => Math.hypot(l[0] - rest[c][0], l[1] - rest[c][1], l[2] - rest[c][2]))));
    const turns = {};
    for (const [name, [a, b]] of Object.entries(TURNS)) {
      const i0 = at(a), i1 = Math.min(R.length, at(b) < 0 ? R.length : at(b));
      if (i0 < 0) { turns[name] = null; continue; }
      let pk = 0;
      for (let i = i0; i < i1; i++) pk = Math.max(pk, S[i]);
      // how far the swing trails the head's turn: the shift (0 to 30 ticks) at which the swing's rise follows the turn
      // rate best (their correlation); a part carried by the head moves with it, at no shift
      const rise = S.map((x, i) => (i ? Math.max(0, x - S[i - 1]) : 0));
      let best = -2, lagT = 0;
      for (let d = 0; d <= 30; d++) {
        const a2 = [], b2 = [];
        for (let i = i0; i < i1 && i + d < R.length; i++) a2.push(turnRate[i]), b2.push(rise[i + d]);
        const ma = mean(a2), mb = mean(b2);
        let num = 0, da = 0, db = 0;
        for (let k = 0; k < a2.length; k++) num += (a2[k] - ma) * (b2[k] - mb), da += (a2[k] - ma) ** 2, db += (b2[k] - mb) ** 2;
        const c = da && db ? num / Math.sqrt(da * db) : -1;
        if (c > best) best = c, lagT = d;
      }
      turns[name] = { swing_mm: +(pk * 1000).toFixed(1), lag_ticks: lagT, corr: +best.toFixed(2) };
    }
    const stops = [];
    for (let i = 1; i < R.length; i++) {
      if (!(R[i - 1].speed > 2 && R[i].speed < 0.35)) continue;
      if (stops.length && R[i].tick - stops[stops.length - 1].tick < 120) continue;
      // the stand: two seconds, or up to 8 ticks before the figure moves off again (the clips lean into a start a few
      // ticks before the place moves)
      let end = Math.min(R.length, i + 120);
      for (let k = i + 1; k < Math.min(R.length, end + 8); k++) if (R[k].speed > 0.35) { end = Math.min(end, Math.max(i + 1, k - 8)); break; }
      // where the tips hang in this stand: their mean over its last second (the stand's own idle moves the body a
      // little, and the parts with it)
      const L = R.slice(i + 60, end);
      const hang = Array.from({ length: n }, (_, c) => [0, 1, 2].map((x) => mean(L.map((r) => r.lag[c][x]))));
      const sw = (r) => Math.max(...r.lag.map((l, c) => Math.hypot(l[0] - hang[c][0], l[1] - hang[c][1], l[2] - hang[c][2])));
      let pk = 0, settled = null;
      for (let k = i; k < end; k++) pk = Math.max(pk, sw(R[k]));
      // settled: from then on the swing stays under a fifth of its peak (and under 2 cm)
      const bar = Math.min(0.02, pk / 5);
      for (let k = i; k < end; k++) if (R.slice(k, end).every((r) => sw(r) <= bar)) { settled = R[k].tick - R[i].tick; break; }
      // at rest (the last second of the stand): jitter, what is left of each tip's motion after a 5-tick running mean
      // (the idle's slow sway passes through it; a tick-to-tick shake does not), its root mean square and its largest
      const J = [];
      for (let k = 2; k < L.length - 2; k++) for (let c = 0; c < n; c++) J.push(Math.hypot(...[0, 1, 2].map((x) => L[k].lag[c][x] - mean(L.slice(k - 2, k + 3).map((r) => r.lag[c][x])))));
      stops.push({ tick: R[i].tick, swing_mm: +(pk * 1000).toFixed(1), settle_ticks: settled, rest_jitter_max_mm: +(Math.max(0, ...J) * 1000).toFixed(3), rest_jitter_mm: +(Math.sqrt(mean(J.map((x) => x * x))) * 1000).toFixed(4), rest_ticks: L.length });
    }
    let clip = -1, clipAt = null, rigid = -1;
    R.forEach((r) => r.depth.forEach((d, c) => { if (d > clip) clip = d, clipAt = { tick: r.tick, chain: chains[idx[c]].name, shape: trace.info.shapes?.[Math.floor(r.deep[c] / 10)], point: r.deep[c] % 10 }; }));
    R.forEach((r) => r.rigid.forEach((d) => { rigid = Math.max(rigid, d); })); // the old rig's parts (carried by their bone) over the same run
    const tb = short ? 6 : 10, sb = short ? 3 : 5;
    const m = { group: g, chains: n, turns, stops, clip_mm: +(clip * 1000).toFixed(1), clip_at: clipAt, old_rig_clip_mm: +(rigid * 1000).toFixed(1) };
    const T = Object.values(turns);
    m.checks = {
      turn: T.length === 2 && T.every((t) => t && t.swing_mm >= tb),
      settle: stops.length >= 3 && stops.every((s) => s.swing_mm >= sb && s.settle_ticks !== null && s.settle_ticks <= 60),
      rest: stops.length >= 3 && stops.every((s) => s.rest_ticks >= 50 && s.rest_jitter_max_mm <= 0.3 && s.rest_jitter_mm <= 0.1),
      clip: m.clip_mm <= 1,
    };
    m.pass = Object.values(m.checks).every(Boolean);
    G[g] = m;
  }
  const checks = {};
  for (const k of ['turn', 'settle', 'rest', 'clip']) checks[k] = Object.values(G).every((m) => m.checks[k]);
  // the cost: the figure's skeleton modifiers a tick (the feet, the look and the springs; host clock), its mean and 95th
  const us = all.map((r) => r.us).filter((x) => x > 0).sort((a, b) => a - b);
  return { figure: trace.figure, variant: trace.variant, rows: all.length, chains: chains.length, modifier_us: { mean: +mean(us).toFixed(1), p95: us.length ? us[Math.floor(us.length * 0.95)] : null }, groups: G, checks, pass: Object.values(checks).every(Boolean) };
}

const [mode, ...args] = process.argv.slice(2);
const opt = (k) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : null; };
if (mode === 'run') {
  await run(args[0], args[1], { video: args.includes('--video'), gpu: opt('--gpu') || 'gl', only: opt('--only') ? opt('--only').split(',') : null, variants: opt('--variants') ? opt('--variants').split(',') : Object.keys(VARIANTS) });
} else if (mode === 'measure') {
  const dir = args[0];
  const runs = readdirSync(dir).filter((d) => existsSync(join(dir, d, 'trace.json'))).sort();
  const res = runs.map((d) => measure(JSON.parse(readFileSync(join(dir, d, 'trace.json'), 'utf8'))));
  if (args.includes('--json')) console.log(JSON.stringify(res, null, 1));
  let ok = true;
  for (const m of res) {
    console.log(`${(m.figure + ' ' + m.variant).padEnd(22)} ${m.pass ? 'PASS' : 'FAIL'} ${JSON.stringify(m.checks)}  modifiers ${m.modifier_us.mean} us (p95 ${m.modifier_us.p95}), ${m.chains} chains`);
    for (const g of Object.values(m.groups)) {
      const t = Object.entries(g.turns).map(([k, x]) => (x ? `${k} ${x.swing_mm}mm +${x.lag_ticks}t (r ${x.corr})` : `${k} -`)).join(', ');
      const s = g.stops.map((x) => `${x.swing_mm}mm/${x.settle_ticks ?? 'never'}t jitter ${x.rest_jitter_mm}/${x.rest_jitter_max_mm}mm`).join(', ');
      console.log(`    ${g.group.padEnd(6)} ${g.pass ? 'pass' : 'FAIL'}  turn ${t} | stops ${s} | clip ${g.clip_mm}mm${g.clip_at ? ` (${g.clip_at.chain} in ${g.clip_at.shape} t${g.clip_at.tick})` : ''}, old rig ${g.old_rig_clip_mm}mm`);
    }
    // a control fails its own check (listed below); 'on' passes every check
    if (m.variant === 'on' && !m.pass) { ok = false; console.log('  FAILED'); }
  }
  // each control must fail the check it is the control of
  const by = (f, v) => res.find((m) => m.figure === f && m.variant === v);
  for (const f of Object.keys(FIGURES)) {
    const c = [['springs0', ['turn', 'settle']], ['collide0', ['clip']], ['soft', ['settle']], ['skirt0', ['clip']]];
    for (const [v, ks] of c) {
      const m = by(f, v);
      if (!m) continue;
      for (const k of ks) {
        if (!m.checks[k]) continue;
        // a beard never reaches the neck, even without collisions: its clip check has nothing to stop (reported, not a failure)
        if (k === 'clip' && Object.values(m.groups).every((g) => g.group === 'Beard')) { console.log(`  ${f} ${v}: clip ${Object.values(m.groups)[0].clip_mm} mm without collisions (the beard never reaches the neck)`); continue; }
        ok = false; console.log(`  ${f} ${v}: ${k} passed (its control must fail)`);
      }
    }
  }
  process.exit(ok ? 0 : 1);
} else {
  console.error('usage: emberthrone-hair.mjs run <cart dir> <out dir> [--video] [--gpu gl|webgpu] [--only figs] [--variants list] | measure <out dir> [--json]');
  process.exit(2);
}
