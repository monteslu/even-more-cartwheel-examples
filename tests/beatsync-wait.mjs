// beatsync never keeps a player waiting, through romdev:
//
// 1. The start: every song on every level starts play 8 beats before its first note (src/main.c play_from_ms), or
//    from the top when that note comes within 8 beats and two seconds: the autopilot cart starts each in turn and the
//    first note (cue.ms) must come at most 8 beats (plus 2 s when it plays from the top) after where play starts
//    (song.from_ms), after the 4 s count-in.
//    CONTROL (must fail): `fromzero 1` plays every song from its top, the way 0.31.0 started a song whose drums come
//    in late, and some song and level then waits longer. (When every chart starts early, nothing can fail it; the
//    check then says so and passes on the chart rule, tools/check-songs.mjs, alone.)
// 2. The wait indicator: Test Groove on Hard with a 20 s hole cut out of its chart (`hole`), on GL and WebGPU: the
//    banks fill and the countdown shows during the wait, and once the notes are back on the rivers it is gone. Read
//    from the picture: each frame is compared with the same deterministic run with the indicator off (`gap off`):
//    in the wait over 3% of the frame differs (the indicator is on screen), two beats after the notes come back on at
//    most 1% (the ASCII pass keeps a cell's glyph while it is nearly as good a match, so a frame carries a little of
//    its past: 0.25% here). And from the game: gap.shown counts frames it was fully up, gap.overlap frames it still showed after
//    the notes came on (0).
//    CONTROL (must fail): `gap force` (the indicator always up) differs from the off run by over 1% after the notes are back,
//    and its gap.overlap is not 0.
//
//   node tests/beatsync-wait.mjs [--gpu gl|webgpu|both]
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { gpusOf, openBeatsync, songs } from './lib/beatsync.mjs';

const { readImage } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const argv = process.argv.slice(2);
const GPUS = gpusOf(argv);
const OUT = join(ROOT, 'out/gate/beatsync-wait');
mkdirSync(OUT, { recursive: true });
const DIFFS = ['easy', 'medium', 'hard'];
const SONGS = songs();

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };

// ── 1. the start ──
async function starts(control) {
  const r = await openBeatsync('beatsync_auto', { gpu: 'gl', tag: control ? 'wait-start-control' : 'wait-start' });
  const out = [];
  try {
    if (control) await r.command('fromzero 1');
    for (const s of SONGS)
      for (const d of DIFFS) {
        await r.command(`song ${s.id} ${d}`);
        const f = (await r.run([{ pads: [{ buttons: [] }], frames: 2 }], { fields: ['song.from_ms', 'cue.ms', 'notes.total'] })).fields;
        const beat = 60000 / s.bpm, first = s.charts[d][0].t * 1000;
        const wait = f['cue.ms'] - f['song.from_ms'];
        const allowed = 8 * beat + (f['song.from_ms'] === 0 ? 2000 : 0) + 30;
        out.push({ id: s.id, d, from: f['song.from_ms'], cue: f['cue.ms'], first, wait, allowed, ok: wait <= allowed && Math.abs(f['cue.ms'] - first) < 2 });
      }
  } finally {
    await r.close();
  }
  return out;
}
{
  const game = await starts(false);
  for (const s of game) check(s.ok, `wait start: ${s.id} ${s.d} plays from ${(s.from / 1000).toFixed(1)} s, its first note at ${(s.cue / 1000).toFixed(1)} s: ${(s.wait / 1000).toFixed(1)} s to wait after the count-in (at most ${(s.allowed / 1000).toFixed(1)})`);
  const ctl = await starts(true);
  const bad = ctl.filter((s) => !s.ok);
  if (bad.length) console.log(`ok   wait start control (fromzero): ${bad.length} song levels wait too long, as they must (${bad.slice(0, 4).map((s) => `${s.id} ${s.d} ${(s.wait / 1000).toFixed(1)} s`).join(', ')})`);
  else console.log('info wait start control (fromzero): every chart starts within 8 beats of the top, so no start can wait; the chart rule (tools/check-songs.mjs) holds this');
}

// ── 2. the indicator ──
const HOLE = [12000, 32000]; // song ms: the notes cut out (Test Groove: 64 bars of groove at 124 BPM)
const SONG = SONGS.find((s) => s.id === 'test-groove');
const BEAT = 60000 / SONG.bpm, AHEAD = 5.2; // Hard's look-ahead (src/main.c AHEAD_BEATS)
const notes = SONG.charts.hard.map((n) => n.t * 1000);
const back = notes.find((t) => t >= HOLE[1]), before = [...notes].reverse().find((t) => t < HOLE[0]);
const appear = back - AHEAD * BEAT; // when the first note after the hole comes onto its river
const SHOTS = { wait: (before + appear) / 2, count: appear - 2 * BEAT, after: appear + 2 * BEAT };

/** one run with a gap mode, shots at song times; returns the shot paths and the fields at the end */
async function run(gpu, mode) {
  const r = await openBeatsync('beatsync_auto', { gpu, tag: `wait-${mode}` });
  try {
    await r.command(`gap ${mode}`);
    await r.command('song test-groove hard');
    await r.run([{ pads: [{ buttons: [] }], frames: 600 }], { stopWhen: { field: 'song.ms', atLeast: 1000 } });
    await r.command(`hole ${HOLE[0]} ${HOLE[1]}`);
    const shots = {}, f = {};
    for (const [k, ms] of Object.entries(SHOTS)) {
      const at = (await r.run([{ pads: [{ buttons: [] }], frames: 3000 }], { stopWhen: { field: 'song.ms', atLeast: Math.round(ms) }, fields: ['song.ms', 'gap.state', 'gap.alpha', 'gap.count', 'gap.unit'] })).fields;
      shots[k] = join(OUT, `${gpu}-${mode}-${k}.png`);
      await r.shotTo(shots[k]);
      f[k] = at;
    }
    return { shots, f, end: await r.fields() };
  } finally {
    await r.close();
  }
}
/** share of the frame's pixels that differ by more than 12 in luma */
function differ(a, b) {
  const A = readImage(a), B = readImage(b);
  let n = 0;
  for (let i = 0; i < A.w * A.h; i++) {
    const la = 0.2126 * A.rgba[i * 4] + 0.7152 * A.rgba[i * 4 + 1] + 0.0722 * A.rgba[i * 4 + 2];
    const lb = 0.2126 * B.rgba[i * 4] + 0.7152 * B.rgba[i * 4 + 1] + 0.0722 * B.rgba[i * 4 + 2];
    if (Math.abs(la - lb) > 12) n++;
  }
  return n / (A.w * A.h);
}
for (const gpu of GPUS) {
  const off = await run(gpu, 'off'), game = await run(gpu, 'game'), force = await run(gpu, 'force');
  const dw = differ(game.shots.wait, off.shots.wait), dc = differ(game.shots.count, off.shots.count), da = differ(game.shots.after, off.shots.after);
  check(game.f.wait['gap.state'] >= 1 && game.f.wait['gap.alpha'] > 0.99 && dw > 0.03, `wait (${gpu}): in the wait (song ${game.f.wait['song.ms']} ms) the indicator is up (state ${game.f.wait['gap.state']}, ${(dw * 100).toFixed(2)}% of the frame differs from the run without it)`);
  check(game.f.count['gap.state'] === 2 && dc > 0.03, `wait (${gpu}): two beats before the notes come on it counts down (${game.f.count['gap.count']}${game.f.count['gap.unit'] ? ' bars' : ''}, ${(dc * 100).toFixed(2)}% differs)`);
  check(da <= 0.01 && game.f.after['gap.alpha'] === 0, `wait (${gpu}): two beats after the notes are back it is gone (alpha ${game.f.after['gap.alpha']}, ${(da * 100).toFixed(3)}% of the frame differs from the run without it)`);
  check(game.end['gap.shown'] > 300 && game.end['gap.overlap'] === 0, `wait (${gpu}): fully up ${game.end['gap.shown']} frames, ${game.end['gap.overlap']} frames up with notes on the rivers`);
  const df = differ(force.shots.after, off.shots.after);
  const bad = df > 0.01 || force.end['gap.overlap'] > 0;
  check(bad, `wait control (${gpu}, gap force): after the notes are back ${(df * 100).toFixed(2)}% of the frame differs and ${force.end['gap.overlap']} frames overlap: ${bad ? 'fails, as it must' : 'PASSES: the gate cannot see the indicator'}`);
  check(off.end['gap.shown'] === 0, `wait control (${gpu}, gap off): the indicator never shows (gap.shown ${off.end['gap.shown']}), the gate's reference`);
}
console.log(failed ? `beatsync-wait: ${failed} failed` : 'beatsync-wait: ok');
process.exit(failed ? 1 : 0);
