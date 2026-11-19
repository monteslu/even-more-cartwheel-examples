// beatsync's newcomer gate: can someone who has never seen the game finish a song on Easy from what the screen shows?
//
// A bot plays the real cart through romdev, on GL and WebGPU, as a newcomer would:
//   - it reaches the song through the menus with presses (Play on the title, the first song, Easy);
//   - it knows only the on-screen cue: the next note on the sequencer ring (which ring it is on, and how far round
//     it still has to go to 12 o'clock, as the debug fields cue.lane, cue.ms and song.ms give them: what the ring
//     draws) and the how-to of the count-in (each button beside its ring). Never the chart;
//   - it is a human, not a machine: it presses each note with a timing error (human.sigmaMs about a late bias,
//     human.biasMs) and holds the button about human.pressMs; one romdev request a note (the frames up to and through
//     the press, returning the next cue);
//   - it plays on a pad (west, south, east), the device every player has; --input keys plays the lanes' keys (J K L)
//     instead (the keyboard is a fallback: not part of the gate unless asked for).
// It passes when the song is cleared (tests/beatsync-game.json newcomer.goal: the crowd never left) within maxSeconds.
//
// The CONTROL must fail: the same bot ignoring the cue (it presses a lane at random on a steady pulse of its own, as
// a player who cannot read the rings would). A control that clears the song means the song clears itself.
//
//   node tests/beatsync-newcomer.mjs [--gpu gl|webgpu|both] [--input pad|keys] [--seed N] [--no-control]
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { LANE_BUTTON, LANE_KEY, SCREEN, enterSong, gpusOf, openBeatsync } from './lib/beatsync.mjs';

const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const GPUS = gpusOf(argv);
const INPUTS = [opt('input', 'pad')];
const CFG = JSON.parse(readFileSync(join(ROOT, 'tests/beatsync-game.json'), 'utf8')).newcomer;
const HUMAN = { sigmaMs: 30, biasMs: 12, pressMs: 120, ...(CFG.human || {}) };
const OUT = join(ROOT, 'out/gate/beatsync-newcomer');
mkdirSync(OUT, { recursive: true });
const FRAME_MS = 1000 / 60;

function rng(seed) {
  let a = seed >>> 0;
  return () => { a = (a + 0x6d2b79f5) >>> 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t ^= t + Math.imul(t ^ (t >>> 7), 61 | t); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}
const gauss = (r) => Math.sqrt(-2 * Math.log(1 - r())) * Math.cos(2 * Math.PI * r());
const LOOK = ['song.ms', 'cue.lane', 'cue.ms', 'song.done', 'song.failed', 'game.screen', 'game.energy', 'notes.hit', 'notes.miss', 'notes.total'];

async function play(gpu, input, seed, blind) {
  const r = await openBeatsync('beatsync', { gpu, tag: blind ? 'newcomer-control' : 'newcomer' });
  const rand = rng(seed);
  const log = [];
  const hold = (lane, frames) => (input === 'keys' ? { pads: [{ buttons: [] }], keys: [LANE_KEY[lane]], frames } : { pads: [{ buttons: [LANE_BUTTON[lane]] }], frames });
  const idle = (frames) => ({ pads: [{ buttons: [] }], frames });
  const stopWhen = { field: 'game.screen', equals: SCREEN.results };
  try {
    let f = await enterSong(r, { song: CFG.song ?? 0, diff: CFG.difficulty ?? 0 }, LOOK);
    const t0 = r.now(), limit = t0 + Math.round((CFG.maxSeconds || 240) * 60);
    const pressF = Math.max(2, Math.round(HUMAN.pressMs / FRAME_MS));
    while (r.now() < limit && f['game.screen'] === SCREEN.play) {
      let segs;
      if (blind) { // its own steady pulse, a lane at random
        segs = [idle(Math.round(26 + rand() * 4)), hold(Math.floor(rand() * 3), pressF)];
      } else if (f['cue.lane'] < 0) segs = [idle(30)]; // nothing coming: the song plays out
      else {
        const lane = f['cue.lane'];
        const due = f['cue.ms'] - f['song.ms'] + HUMAN.biasMs + gauss(rand) * HUMAN.sigmaMs;
        const wait = Math.max(0, Math.round(due / FRAME_MS) - 1);
        segs = [idle(wait), hold(lane, pressF), idle(2)];
      }
      f = (await r.run(segs, { fields: LOOK, stopWhen })).fields;
      log.push([r.now() - t0, f['song.ms'], f['cue.lane'], f['notes.hit'], f['notes.miss'], +f['game.energy'].toFixed(2)]);
    }
    const done = f['song.done'] === 1;
    if (!done) await r.shotTo(join(OUT, `${gpu}-${input}-${blind ? 'control' : seed}-end.png`));
    return { done, secs: ((r.now() - t0) / 60).toFixed(1), hit: f['notes.hit'], miss: f['notes.miss'], total: f['notes.total'], failed: f['song.failed'], log };
  } finally {
    await r.close();
  }
}

let failed = 0;
const seed0 = +opt('seed', 1);
for (const gpu of GPUS) {
  for (const input of INPUTS) {
    const res = await play(gpu, input, seed0, false);
    const tag = `newcomer (${gpu}, ${input === 'keys' ? 'keyboard' : 'pad'}, Easy, seed ${seed0})`;
    writeFileSync(join(OUT, `${gpu}-${input}-${seed0}.json`), JSON.stringify(res.log));
    if (res.done) console.log(`ok   ${tag}: cleared the song in ${res.secs} s, ${res.hit} hit, ${res.miss} missed of ${res.total}`);
    else { console.log(`FAIL ${tag}: not cleared (${res.failed ? 'the crowd left' : 'time ran out'}; ${res.hit} hit, ${res.miss} missed of ${res.total}); its trace: out/gate/beatsync-newcomer/${gpu}-${input}-${seed0}.json`); failed++; }
  }
  if (!argv.includes('--no-control')) {
    const res = await play(gpu, "pad", seed0, true); // the cue-ignoring control is the same on any device
    if (res.done) { console.log(`FAIL newcomer control (${gpu}, cue ignored) cleared the song: the gate cannot tell a player who reads the rings from one who does not`); failed++; }
    else console.log(`ok   newcomer control (${gpu}, cue ignored): did not clear the song, as it must (${res.failed ? 'the crowd left' : 'not finished'}, ${res.hit} hit, ${res.miss} missed)`);
  }
}
console.log(failed ? `beatsync-newcomer: ${failed} failed` : 'beatsync-newcomer: ok');
process.exit(failed ? 1 : 0);
