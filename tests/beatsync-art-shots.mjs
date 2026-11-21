// beatsync played on a pad through romdev, a song on each difficulty, for a look at every screen and for the launcher
// art (games/beatsync/tools/make-art.sh): the title, the song select, Late Night on Easy, Chicago Jack on Medium, Acid
// House Rave on Hard and Test Groove on Hard to its results. Each song is reached through the menus with presses and
// played from its chart: the song clock read once at the first frame of play, then every note pressed on its lane's
// button at the frame it is due, with a human's timing error (a 12 ms late bias, up to 30 ms either side: inside
// Good, mostly Perfect), all of it one input sequence. Loaded as a player's host runs it (no seed), at the given size.
//   node tests/beatsync-art-shots.mjs <out dir> [--gpu gl|webgpu] [--size 3840x2160]
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { openCart, call } from './lib/romdev.mjs';
import { LANE_BUTTON, SCREEN, enterSong, songs } from './lib/beatsync.mjs';

const argv = process.argv.slice(2);
const OUT = resolve(argv.find((a, i) => !a.startsWith('--') && !['--gpu', '--size'].includes(argv[i - 1])) || 'out/gate/beatsync-art');
const opt = (k, d) => (argv.includes(`--${k}`) ? argv[argv.indexOf(`--${k}`) + 1] : d);
const gpu = opt('gpu', 'webgpu');
const [width, height] = opt('size', '3840x2160').split('x').map(Number);
mkdirSync(OUT, { recursive: true });
const SONGS = songs();
const FRAME_MS = 1000 / 60;

function rng(seed) {
  let a = seed >>> 0;
  return () => { a = (a + 0x6d2b79f5) >>> 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t ^= t + Math.imul(t ^ (t >>> 7), 61 | t); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

async function open(app) {
  return wrap(await openCart(app, { gpu, seed: null, tag: 'beatsync-art', load: { preferredSize: { width, height } } }));
}
async function wrap(c) { // the ranged input of tests/lib/beatsync.mjs on a host opened with a preferred size
  let extra = 0;
  const ports = (pads) => pads.map((p) => Object.fromEntries((p?.buttons || []).map((b) => [b, true])));
  return Object.assign(c, {
    now: () => c.frame + extra,
    async run(segs, { fields = null, stopWhen = null } = {}) {
      const steps = segs.filter((s) => s.frames > 0).map((s) => ({ input: { ports: ports(s.pads) }, frames: s.frames }));
      const total = steps.reduce((a, s) => a + s.frames, 0);
      const r = await call('input', { op: 'sequence', steps, ...(fields ? { fields } : {}), ...(stopWhen ? { stopWhen } : {}) }, c.session, { timeoutMs: 600000 + total * 3000 });
      extra += r.framesRun;
      return { fields: r.fields || null, stopped: r.stopped || false, framesRun: r.framesRun };
    },
  });
}

/** play `song` on `diff` through the menus until the song clock reaches untilMs (or the results); returns the fields */
async function play(r, songIndex, diff, untilMs, seed) {
  const f0 = await enterSong(r, { song: songIndex, diff }, ['song.ms']);
  const m0 = f0['song.ms'], rand = rng(seed);
  const chart = SONGS[songIndex].charts[['easy', 'medium', 'hard'][diff]];
  const segs = [];
  let at = 0;
  for (const n of chart) {
    if (n.t * 1000 > untilMs) break;
    const jitter = 12 + (rand() + rand() + rand() - 1.5) * 20;
    const want = Math.round((n.t * 1000 + jitter - m0) / FRAME_MS);
    if (want - 1 > at) { segs.push({ pads: [{ buttons: [] }], frames: want - 1 - at }); at = want - 1; }
    segs.push({ pads: [{ buttons: [LANE_BUTTON[n.lane]] }], frames: 4 });
    at += 4;
  }
  const end = Math.round((untilMs - m0) / FRAME_MS);
  if (end > at) segs.push({ pads: [{ buttons: [] }], frames: end - at });
  const look = ['song.ms', 'game.screen', 'notes.perfect', 'notes.good', 'notes.miss', 'notes.total', 'game.score', 'song.done'];
  return (await r.run(segs, { fields: look, stopWhen: { field: 'game.screen', equals: SCREEN.results } })).fields;
}

const idx = (id) => SONGS.findIndex((s) => s.id === id);
const report = (name, f) => console.log(`${name}: ${JSON.stringify(f)}`);

// the title and the song select
{
  const r = await open('beatsync');
  try {
    await r.run([{ pads: [{ buttons: [] }], frames: 240 }]);
    await r.shot(join(OUT, 'title.png'));
    await r.run([{ pads: [{ buttons: ['south'] }], frames: 4 }, { pads: [{ buttons: [] }], frames: 30 }, { pads: [{ buttons: ['start'] }], frames: 4 }, { pads: [{ buttons: [] }], frames: 30 }, { pads: [{ buttons: ['down'] }], frames: 4 }, { pads: [{ buttons: [] }], frames: 30 }, { pads: [{ buttons: ['down'] }], frames: 4 }, { pads: [{ buttons: [] }], frames: 90 }]);
    await r.shot(join(OUT, 'select.png'));
  } finally { await r.close(); }
}
// a song on each difficulty, mid-song
for (const [name, id, diff, ms] of [['easy', 'late-night', 0, 52000], ['medium', 'chicago-jack', 1, 61000], ['hard', 'acid-house', 2, 64000], ['hard2', 'filter-disco', 2, 90000]]) {
  const r = await open('beatsync');
  try {
    const f = await play(r, idx(id), diff, ms, diff + 1);
    report(`${name} (${id})`, f);
    await r.shot(join(OUT, `play-${name}.png`));
  } finally { await r.close(); }
}
// a whole song to its results
{
  const r = await open('beatsync');
  try {
    const f = await play(r, idx('test-groove'), 2, SONGS[idx('test-groove')].length_s * 1000 + 8000, 7);
    await r.run([{ pads: [{ buttons: [] }], frames: 90 }]);
    report('results (test-groove, hard)', f);
    await r.shot(join(OUT, 'results.png'));
  } finally { await r.close(); }
}
