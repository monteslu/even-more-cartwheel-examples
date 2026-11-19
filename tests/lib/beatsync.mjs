// What beatsync's gates share: its songs (read from games/beatsync/songs/<id>/song.json, as the cart is built from
// them), the lane buttons (games/beatsync/project.cwproj input.*), the way to a song through the menus a player uses,
// and a romdev host with ranged input (one input sequence a span of play, returning the debug fields it was asked for:
// one request a look, never one a frame).
//
//   const r = await openBeatsync('beatsync', { gpu: 'gl' });
//   const f = await enterSong(r, { song: 0, diff: 2 }, ['song.ms']);
//   const { fields } = await r.run([{ pads: [{ buttons: ['west'] }], frames: 4 }], { fields: ['notes.hit'] });
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { openCart, call, ROOT } from './romdev.mjs';

export const GAME = join(ROOT, 'games/beatsync');
export const LANE_BUTTON = ['west', 'south', 'east']; // kick, clap, bass: input.lane_* in project.cwproj
export const LANE_KEY = ['j', 'k', 'l'];
export const DIFFS = ['easy', 'medium', 'hard'];
export const SCREEN = { select: 1, diff: 2, play: 3, results: 4, calib: 5 }; // src/main.c SCR_*

/** every song the cart lists, in its order (CMakeLists.txt: songs/order.txt first, then the rest by name) */
export function songs() {
  const dir = join(GAME, 'songs');
  const found = readdirSync(dir).filter((d) => existsSync(join(dir, d, 'song.json'))).sort();
  const order = existsSync(join(dir, 'order.txt')) ? readFileSync(join(dir, 'order.txt'), 'utf8').split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#')) : [];
  const ids = [...order.filter((id) => found.includes(id)), ...found.filter((id) => !order.includes(id))];
  return ids.map((d) => JSON.parse(readFileSync(join(dir, d, 'song.json'), 'utf8')));
}

/** the song whose Hard chart has the most notes a second */
export function densest() {
  const all = songs();
  let best = 0;
  all.forEach((s, i) => { if (s.charts.hard.length / s.length_s > all[best].charts.hard.length / all[best].length_s) best = i; });
  return { index: best, song: all[best] };
}

const ports = (pads) => pads.map((p) => Object.fromEntries((p?.buttons || []).map((b) => [b, true])));

/** the repository's romdev host (tests/lib/romdev.mjs openCart) with ranged input and debug fields by name */
export async function openBeatsync(app, { gpu = 'gl', seed = 1, tag = '' } = {}) {
  const h = await openCart(app, { gpu, seed, tag: `beatsync-${tag || app}` });
  let extra = 0;
  return Object.assign(h, {
    /** frames run since the load (steps, sequences and debug requests) */
    now: () => h.frame + extra,
    async stepTo(frame) { const n = frame - (h.frame + extra); if (n > 0) await h.step(n); },
    /** debug fields as { name: value } */
    fields: () => h.state(),
    /** a range in ONE request: [{ pads: [{ buttons }], keys?, frames }, ...] held in turn; with `fields` the values
     * after the last frame come back with it, and `stopWhen` ({ field, equals } or { field, atLeast }) ends the range
     * the frame the field gets there. Returns { fields, stopped, framesRun }. */
    async run(segments, { fields = null, stopWhen = null } = {}) {
      const steps = segments.filter((s) => s.frames > 0).map((s) => ({ input: { ports: ports(s.pads), ...(s.keys?.length ? { keys: s.keys } : {}) }, frames: s.frames }));
      if (!steps.length) return { fields: fields ? await h.state() : null, stopped: false, framesRun: 0 };
      const total = steps.reduce((a, s) => a + s.frames, 0);
      const r = await call('input', { op: 'sequence', steps, ...(fields ? { fields } : {}), ...(stopWhen ? { stopWhen } : {}) }, h.session, { timeoutMs: 600000 + total * 3000 });
      if (fields && !r.fields) throw new Error('romdev returned no fields with the input sequence (romdev 0.155.0 or later)');
      extra += r.framesRun;
      return { fields: r.fields || null, stopped: r.stopped || false, framesRun: r.framesRun };
    },
    /** any other romdev tool on this host */
    call: (tool, args, opts) => call(tool, args, h.session, opts),
    shotTo: (file) => h.shot(resolve(file)),
  });
}

/** a pad press for `frames`, then `gap` frames of nothing */
export const tap = (button, frames = 4, gap = 20) => [{ pads: [{ buttons: [button] }], frames }, { pads: [{ buttons: [] }], frames: gap }];

/** from the title to the first frame of a song's play, as a player does it on a pad: Play on the title, the song
 * (index `song` from the top of the list), the difficulty, accept. One request a screen; returns the fields after. */
export async function enterSong(r, { song = 0, diff = 0 }, fields) {
  const look = ['shell.screen', 'game.screen', 'song.index', 'song.pick', ...fields];
  let f = (await r.run([{ pads: [{ buttons: [] }], frames: 30 }], { fields: look })).fields;
  for (let tries = 0; tries < 30; tries++) {
    const shell = f['shell.screen'], scr = f['game.screen'];
    let segs;
    if (shell === 0) segs = [{ pads: [{ buttons: [] }], frames: 30 }]; // loading
    else if (shell === 1) segs = tap('south'); // the title: Play
    else if (shell !== 3) throw new Error(`enterSong: the shell is on screen ${shell}`);
    else if (scr === SCREEN.play) return f;
    else if (scr === SCREEN.select) segs = f['song.index'] !== song ? tap(f['song.index'] < song ? 'down' : 'up') : tap('south');
    else if (scr === SCREEN.diff) segs = f['song.pick'] !== diff ? tap(f['song.pick'] < diff ? 'down' : 'up') : tap('south', 4, 2);
    else throw new Error(`enterSong: the game is on screen ${scr}`);
    f = (await r.run(segs, { fields: look })).fields;
  }
  throw new Error('enterSong: did not reach the song in 30 presses');
}

/** the gpus a gate runs on: --gpu gl|webgpu|both (default both) */
export function gpusOf(argv) {
  const i = argv.indexOf('--gpu');
  return i >= 0 && argv[i + 1] !== 'both' ? [argv[i + 1]] : ['gl', 'webgpu'];
}
