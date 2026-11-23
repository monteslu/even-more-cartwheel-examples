// beatsync with one or two players, through romdev on GL and WebGPU (docs/DESIGN.md "One or two players").
//
// 1. The charts: the autopilot plays Test Groove to its results as two players (player 1 on the drums, player 2 on the
//    groove, at two difficulties; and swapped) and as one player on each pair. Every note is hit, and each player's
//    notes are exactly their pair's chart at their difficulty in song.json (pairs.<pair>.<level>). The CONTROL: the
//    same counts held to the two players' difficulties swapped must fail.
// 2. The menus, as two people do it on two pads: Play, both pads join, Start, the song; player 1 moves to Medium on
//    pad 1, both press ready on their own pad, and the song plays as two players at Medium and Easy. CONTROL: the same
//    presses without player 1's ready stay on the difficulties.
// 3. A pad plays only its own player's lanes: in that two-player song, pad 2 alone presses its two buttons through
//    the song's first notes (`crowdstays 1`: player 1's misses never end the song); player 2 hits notes and player 1
//    hits none. CONTROL: with `padswap 1` (each player on the
//    other's pad) the same presses hit player 1's notes, which the check must catch.
//
// 4. Every river the layout shows is drawn (from the picture): mid-song, one player on three lanes, on each pair, and
//    two players: over each shown river's gate (`rivers` says where) the frame is lit, at least RIVER_LUMA. CONTROL:
//    `rivers hidden draw` (a river not shown drawn dark where it rests, as this release's first build did, which
//    covered the three-lane game's clap river) must fail it.
// 5. Each player keeps their own streak: two players at Hard, the autopilot lets one of player 2's bass notes go by
//    after both have a streak of at least 16; player 2's streak breaks (and drains), player 1's goes on unbroken. CONTROL: with
//    `sharedstreak 1` (one streak for both) the same miss breaks player 1's, which the check must catch.
//
//   node tests/beatsync-duo.mjs [--gpu gl|webgpu|both]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { gpusOf, openBeatsync, songs, SCREEN } from './lib/beatsync.mjs';

const { readImage } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const OUT = join(ROOT, 'out/gate/beatsync-duo');
mkdirSync(OUT, { recursive: true });

const GPUS = gpusOf(process.argv.slice(2));
const LV = ['easy', 'medium', 'hard'];
const SONG = songs().find((s) => s.id === 'test-groove');
const FIELDS = ['song.done', 'notes.total', 'notes.hit', 'notes.miss', 'game.mode', 'game.players', 'game.part', 'game.pdiff', 'player.total', 'player.perfect', 'player.good', 'player.miss'];

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const pairOf = ['drums', 'groove'];

/** what each player should play: [count for player 1, count for player 2 (0: none)] */
const expected = (parts, diffs) => parts.map((p, i) => (p < 0 ? 0 : SONG.pairs[pairOf[p]][LV[diffs[i]]].length));

async function autopilot(gpu) {
  // [mode, player 1 difficulty, player 2 difficulty, parts, mode id]
  const runs = [['duo', 2, 0, [0, 1], 2], ['duo-swap', 1, 2, [1, 0], 2], ['drums', 2, 2, [0, -1], 1], ['groove', 0, 0, [1, -1], 1]];
  for (const [mode, d1, d2, parts, modeId] of runs) {
    const r = await openBeatsync('beatsync_auto', { gpu, tag: `duo-${mode}` });
    try {
      await r.command(`song test-groove ${LV[d1]} ${mode} ${LV[d2]}`);
      const res = await r.run([{ pads: [{ buttons: [] }], frames: 9000 }], { fields: FIELDS, stopWhen: { field: 'song.done', equals: 1 } });
      const f = res.fields, want = expected(parts, [d1, d2]), got = f['player.total'];
      const okCounts = got[0] === want[0] && got[1] === want[1];
      check(res.stopped && f['game.mode'] === modeId && f['notes.hit'] === f['notes.total'] && f['notes.miss'] === 0 && okCounts,
        `duo (${gpu}, ${mode} ${LV[d1]}/${LV[d2]}): mode ${f['game.mode']}, ${f['notes.hit']} of ${f['notes.total']} hit, player notes ${got.join('+')} (the pairs' charts: ${want.join('+')})`);
      if (mode === 'duo') { // control: the counts held to the difficulties swapped
        const swapped = expected(parts, [d2, d1]);
        check(!(got[0] === swapped[0] && got[1] === swapped[1]), `duo control (${gpu}): the counts held to the two players' difficulties swapped (${swapped.join('+')}) fail, as they must`);
      }
    } finally { await r.close(); }
  }
}

/** pads: two entries every step (a pad left out reads as unplugged and the shell frees its seat) */
const two = (a = [], b = []) => [{ buttons: a }, { buttons: b }];
const press = (a, b, gap = 24) => [{ pads: two(a, b), frames: 4 }, { pads: two(), frames: gap }];

/** title to the difficulties with both pads seated; returns the open host */
async function toDuoDiff(gpu, tag) {
  const r = await openBeatsync('beatsync', { gpu, tag });
  await r.command('crowdstays 1'); // player 1 stays idle in the pad check: the song must not end on its misses
  // the loading screen (every world and look drawn once behind it) until the title, then a moment
  await r.run([{ pads: two(), frames: 3000 }], { stopWhen: { field: 'shell.screen', equals: 1 } });
  await r.run([{ pads: two(), frames: 60 }]);
  for (const [a, b] of [[['south'], []], [['south'], []], [[], ['south']], [['start'], []]]) await r.run(press(a, b));
  await r.run([{ pads: two(), frames: 30 }]);
  await r.run(press(['south'], [])); // Test Groove, the first song
  await r.run(press(['down'], [])); // player 1: Medium
  return r;
}

async function menus(gpu) {
  const r = await toDuoDiff(gpu, 'duo-menus');
  try {
    await r.run(press([], ['south'])); // player 2 ready
    const f = (await r.run(press(['south'], [], 30), { fields: ['game.screen', ...FIELDS] })).fields; // player 1 ready
    check(f['game.screen'] === SCREEN.play && f['game.mode'] === 2 && f['game.players'] === 2 && f['game.pdiff'][0] === 1 && f['game.pdiff'][1] === 0 && f['game.part'][0] === 0,
      `duo menus (${gpu}): two pads join, choose and ready: screen ${f['game.screen']}, mode ${f['game.mode']}, players ${f['game.players']}, difficulties ${f['game.pdiff'].map((d) => LV[d]).join('/')}, player 1 on the ${pairOf[f['game.part'][0]]}`);
    await isolation(r, gpu, false);
  } finally { await r.close(); }
  const c = await toDuoDiff(gpu, 'duo-menus-control');
  try {
    const f = (await c.run(press([], ['south'], 30), { fields: ['game.screen', 'game.ready'] })).fields; // only player 2 ready
    check(f['game.screen'] === SCREEN.diff, `duo menus control (${gpu}): with only player 2 ready the song does not start (screen ${f['game.screen']}, ready ${f['game.ready'].join('/')}), as it must`);
    await c.run(press(['south'], [], 30));
    await c.command('padswap 1');
    await isolation(c, gpu, true);
  } finally { await c.close(); }
}

/** pad 2 alone presses its two buttons, one every 7 frames, through the first notes */
async function isolation(r, gpu, control) {
  const steps = [];
  for (let i = 0; i < 200; i++) steps.push({ pads: two([], [i % 2 ? 'south' : 'west']), frames: 2 }, { pads: two(), frames: 5 });
  const f = (await r.run(steps, { fields: FIELDS })).fields;
  const hits = [0, 1].map((p) => f['player.perfect'][p] + f['player.good'][p]);
  const ok = hits[0] === 0 && hits[1] > 0;
  if (!control) check(ok, `duo pads (${gpu}): pad 2 alone hits player 2's notes (${hits[1]}) and none of player 1's (${hits[0]})`);
  else check(!ok, `duo pads control (${gpu}, padswap 1): the same presses hit player 1's notes (${hits[0]}, player 2 ${hits[1]}): the check ${ok ? 'PASSES: it cannot see a pad playing the other player\'s lanes' : 'fails, as it must'}`);
}

/** the mean luma over a river's gate: a box its width wide, from a little above the gate line to it */
const RIVER_LUMA = +(process.env.RIVER_LUMA || 14);
function gateLuma(img, x, y) {
  let s = 0, n = 0;
  for (let yy = Math.round(y - 110); yy < y - 20; yy++)
    for (let xx = Math.round(x - 45); xx < x + 45; xx++) {
      const i = (yy * img.w + xx) * 4;
      s += 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2], n++;
    }
  return s / n;
}
async function rivers(gpu) {
  const layouts = [['solo', 3], ['drums', 2], ['groove', 2], ['duo', 4]];
  const measure = async (mode, control) => {
    const r = await openBeatsync('beatsync_auto', { gpu, tag: `duo-rivers-${mode}` });
    try {
      if (control) await r.command('rivers hidden draw');
      await r.command(`song test-groove hard ${mode} hard`);
      await r.stepTo(1500);
      const file = join(OUT, `${gpu}-${mode}${control ? '-control' : ''}.png`);
      await r.shotTo(file);
      const img = readImage(file), rv = (await r.command('rivers')).rivers.filter((x) => x.shown > 0.99);
      return rv.map((x) => ({ lane: x.lane, luma: +gateLuma(img, x.x, x.y).toFixed(1) }));
    } finally { await r.close(); }
  };
  for (const [mode, want] of layouts) {
    const m = await measure(mode, false);
    check(m.length === want && m.every((x) => x.luma >= RIVER_LUMA), `duo rivers (${gpu}, ${mode}): ${m.length} rivers shown (${want}), each lit over its gate: ${m.map((x) => `${x.lane}:${x.luma}`).join(' ')} (at least ${RIVER_LUMA})`);
  }
  const c = await measure('solo', true);
  const passes = c.every((x) => x.luma >= RIVER_LUMA);
  check(!passes, `duo rivers control (${gpu}, solo, rivers hidden draw): ${c.map((x) => `${x.lane}:${x.luma}`).join(' ')}: ${passes ? 'PASSES: the check cannot see a river covered' : 'a river covered fails, as it must'}`);
}

async function ownStreaks(gpu) {
  const run = async (shared) => {
    const r = await openBeatsync('beatsync_auto', { gpu, tag: `duo-streaks${shared ? '-shared' : ''}` });
    try {
      if (shared) await r.command('sharedstreak 1');
      await r.command('song test-groove hard duo hard');
      const F = ['game.combo', 'streak2.combo', 'streak.breaks', 'streak2.breaks'];
      const a = await r.run([{ pads: [{ buttons: [] }], frames: 6000 }], { fields: F, stopWhen: { field: 'streak2.combo', atLeast: 30 } });
      if (!a.stopped || a.fields['game.combo'] < 16) throw new Error(`duo streaks: both streaks never reached 16 (${JSON.stringify(a.fields)})`);
      await r.command('skip 2 1'); // player 2 lets their next bass note go by
      const b = await r.run([{ pads: [{ buttons: [] }], frames: 6000 }], { fields: F, stopWhen: { field: 'streak2.breaks', atLeast: 1 } });
      const c = (await r.run([{ pads: [{ buttons: [] }], frames: 60 }], { fields: F })).fields;
      return { before: a.fields, at: b.fields, after: c };
    } finally { await r.close(); }
  };
  const ok = (m) => m.at['streak2.breaks'] === 1 && m.at['streak.breaks'] === 0 && m.after['game.combo'] > m.before['game.combo'];
  const m = await run(false);
  check(ok(m), `duo streaks (${gpu}): player 2 misses one note: their streak breaks (breaks ${m.at['streak2.breaks']}, now ${m.after['streak2.combo']}), player 1's goes on (${m.before['game.combo']} to ${m.after['game.combo']}, breaks ${m.at['streak.breaks']})`);
  const c = await run(true);
  check(!ok(c), `duo streaks control (${gpu}, sharedstreak 1): the same miss ${ok(c) ? 'leaves player 1\'s streak: the check PASSES and cannot see a shared streak' : `breaks player 1's too (breaks ${c.at['streak.breaks']}, now ${c.after['game.combo']}), which the check catches, as it must`}`);
}

for (const gpu of GPUS) {
  await ownStreaks(gpu);
  await rivers(gpu);
  await autopilot(gpu);
  await menus(gpu);
}
console.log(failed ? `beatsync-duo: ${failed} failed` : 'beatsync-duo: ok');
process.exit(failed ? 1 : 0);
