// pinball table select gate, on romdev with the pad alone, as a player does it: Play on the title, then on the table
// select the d-pad, the shoulders (the flippers), the left stick and a second pad change the table, d-pad down counts
// the players down (it must not start a game: in play it is the plunger), and each of the three tables is chosen and
// played: accept, the plunger held and let go (a ball launched), flips, then the pause menu's Change table back to the
// select. After each ball the live table is read back (game.table, with the frame) and a frame kept in out/pinball-select.
//
// It holds:
//   - every change lands on the table the presses ask for, the select stays up through all of them, and each table
//     played is the one chosen (game.table 1, 2 then 0, a ball launched on each);
//   - a change is instant: the worst frame over the select's changes (romdev's frame timing, wall clock) is under
//     BOUND_MS. A change used to rebuild the table's view inside one frame (its art painted, every mesh built), 1.5 to
//     2.5 s on a desktop CPU, so the screen froze on every press and presses piled up across the freezes;
// The frames it keeps in out/pinball-select (each table in play, and the select) are for a person to look at.
// The control is the old build (the published 0.60.0 cart, or --control <dir holding pinball.wasc>): it must fail.
//
// Run: node tests/pinball-table-select.mjs [--gpu gl|webgpu|gl,webgpu] [--control <dir>]
import { mkdirSync, existsSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { ROOT, openCart, call, gpusArg } from './lib/romdev.mjs';

const BOUND_MS = 400;
const OUT = join(ROOT, 'out/pinball-select');
mkdirSync(OUT, { recursive: true });
const argv = process.argv;
const ci = argv.indexOf('--control');
let controlDir = ci > 0 ? argv[ci + 1] : join(ROOT, 'build/control-0.60.0');
if (!existsSync(join(controlDir, 'pinball.wasc'))) {
  mkdirSync(controlDir, { recursive: true });
  const r = spawnSync('gh', ['release', 'download', 'v0.60.0', '-R', 'monteslu/even-more-cartwheel-examples', '-p', 'pinball.wasc', '-D', controlDir], { encoding: 'utf8' });
  if (r.status !== 0) { console.log(`FAIL no control cart in ${controlDir}: ${r.stderr}`); process.exit(3); }
}

// one run: returns the problems found (empty: it passed)
async function run(label, cartDir, gpu) {
  process.env.DESTRUCTO_CART_DIR = cartDir;
  const h = await openCart('pinball', { gpu, tag: `select-${label}-${gpu}` });
  const bad = [];
  let at = 0;
  const st = async () => {
    const s = await h.state();
    at = s['cw.frame'];
    return { frame: at, phase: s['game.phase'], table: s['game.table'], launches: s['game.launches'], players: s['game.players'] };
  };
  const press = async (entries, wait) => { // entries at offsets from the next frame, then `wait` frames after the last
    const f = at + 2;
    const tl = entries.map((e) => ({ frame: f + e.at, buttons: e.buttons, until: f + e.at + (e.hold || 3), ...(e.port ? { port: e.port } : {}) }));
    const end = Math.max(...tl.map((e) => e.until)) + wait;
    await call('input', { op: 'timeline', timeline: tl, untilFrame: end }, h.session);
    return st();
  };
  try {
    await h.step(60);
    await st();
    let s = await press([{ at: 0, buttons: ['south'] }], 30); // the title's Play
    if (s.phase !== 0 || s.table !== 0) bad.push(`after Play on the title: phase ${s.phase} table ${s.table} (want the select, phase 0, on table 0)`);
    // the changes, timed: d-pad, shoulders, a second pad's shoulder, d-pad down and up (players), every frame read
    const sched = [
      { frame: 10, buttons: 'right', hold: 3 }, { frame: 40, buttons: 'right', hold: 3 }, { frame: 70, buttons: 'left', hold: 3 },
      { frame: 100, buttons: 'l', hold: 3 }, { frame: 130, buttons: 'p2.r', hold: 3 }, { frame: 160, buttons: 'down', hold: 3 },
      { frame: 190, buttons: 'up', hold: 3 },
    ];
    const want = [[30, 1], [60, 2], [90, 1], [120, 0], [150, 1], [180, 1], [215, 1]];
    const t = await call('frame', { op: 'timing', frames: 220, warmup: 0, window: 220, input: sched, series: true, debugFields: ['game.table', 'game.phase'] }, h.session, { timeoutMs: 900000 });
    const tables = t.series?.fields?.['game.table'] || [], phases = t.series?.fields?.['game.phase'] || [];
    const worst = t.wallMs?.max;
    if (!(worst < BOUND_MS)) bad.push(`the worst frame over the table changes took ${worst?.toFixed(0)} ms (bound ${BOUND_MS})`);
    else console.log(`     ${label} ${gpu}: worst frame over 5 table changes ${worst.toFixed(0)} ms (bound ${BOUND_MS})`);
    if (!tables.length || !phases.length) bad.push(`frame timing returned no per-frame table and phase (keys: ${Object.keys(t).join(',')})`);
    for (const [f, tb] of want) {
      if (tables.length && tables[f] !== tb) bad.push(`select frame ${f}: table ${tables[f]}, want ${tb}`);
      if (phases.length && phases[f] !== 0) bad.push(`select frame ${f}: phase ${phases[f]}, want the select (0): a press there started a game`);
    }
    await st();
    // each table played: from the select on table 1, then via Change table
    const plays = [
      { want: 1, name: 'star-foundry', choose: [] },
      { want: 2, name: 'neon-fair', choose: 'stick' },
      { want: 0, name: 'coral-deep', choose: [{ at: 0, buttons: ['r'] }] },
    ];
    for (const p of plays) {
      if (p.choose === 'stick') {
        await call('input', { op: 'sequence', steps: [{ input: { ports: [{ axes: { lx: 1 } }] }, frames: 8 }, { input: { ports: [{}] }, frames: 20 }], fields: ['game.table'] }, h.session);
        s = await st();
      } else if (p.choose.length) s = await press(p.choose, 20);
      else s = await st();
      if (s.phase !== 0 || s.table !== p.want) bad.push(`${p.name}: chose table ${s.table} (phase ${s.phase}), want ${p.want} on the select`);
      s = await press([{ at: 0, buttons: ['south'] }, { at: 30, buttons: ['south'], hold: 70 }, { at: 150, buttons: ['l'], hold: 12 }, { at: 200, buttons: ['r'], hold: 12 }, { at: 260, buttons: ['l', 'r'], hold: 12 }], 60);
      await h.shot(join(OUT, `${label}-${gpu}-${p.name}.png`));
      console.log(`     ${label} ${gpu}: ${p.name}: frame ${s.frame} table ${s.table} phase ${s.phase} launches ${s.launches}`);
      if (s.table !== p.want || s.phase !== 1 || !(s.launches >= 1)) bad.push(`${p.name}: in play at frame ${s.frame}: table ${s.table} phase ${s.phase} launches ${s.launches}, want table ${p.want} in play (1) with a ball launched`);
      // Start, down twice to Change table, accept: back on the select (on the table just played)
      s = await press([{ at: 0, buttons: ['start'] }, { at: 30, buttons: ['down'] }, { at: 50, buttons: ['down'] }, { at: 80, buttons: ['south'] }], 30);
      if (s.phase !== 0 || s.table !== p.want) bad.push(`${p.name}: Change table gave phase ${s.phase} table ${s.table}, want the select (0) on ${p.want}`);
    }
    await h.shot(join(OUT, `${label}-${gpu}-select.png`));
  } finally {
    await h.close();
  }
  return bad;
}

let failed = 0;
for (const gpu of gpusArg(argv)) {
  const bad = await run('build', join(ROOT, 'build/cart-wgpu/carts'), gpu);
  if (bad.length) failed++, console.log(`FAIL ${gpu}: the table select\n     ${bad.join('\n     ')}`);
  else console.log(`ok   ${gpu}: every change instant and right, each table chosen on the pad and played (frames in out/pinball-select)`);
  const cbad = await run('control', controlDir, gpu);
  if (cbad.length) console.log(`ok   ${gpu}: control (the old build) fails as it must: ${cbad.length} problems, e.g.\n     ${cbad.slice(0, 3).join('\n     ')}`);
  else failed++, console.log(`CONTROL PASSED (a failure) on ${gpu}: the old build's table select went unnoticed`);
}
process.exit(failed ? 1 : 0);
