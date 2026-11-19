// beatsync's sync gate: does the game's song clock run with the song, and does it judge a press against the note's time in
// the song (not the frame)? Played through romdev on GL and WebGPU, on the real cart from the title as a player gets
// there (tests/lib/beatsync.mjs enterSong), on Hard:
//   - the bot reads the song clock ONCE (song.ms at the first frame of play) and from then on presses each note's
//     lane at the frame the wall clock says the note is due: frame = (note time - that reading) / (1000/60). The
//     whole song is one romdev request. If the game's clock drifted from real time (the audio running fast or slow,
//     a clock that stalls or jumps between mixer blocks), the late notes would land outside the Perfect window;
//     every note Perfect proves the clock and the judgement are in step with the audio from start to end.
//   - the notes' own judgement errors (judge.err_* fields) give the accuracy: the mean, the spread and the worst.
// The CONTROL must fail: the same bot pressing every note 50 ms late (past the 35 ms Perfect window; not 80 ms, which
// on a dense Hard chart of 16ths 117 ms apart lands within Perfect of the NEXT note and is judged as that) must get no
// Perfect at all.
//
// A second part measures the sound itself: the auto cart plays the song with the kick track alone (`solo kick`),
// romdev records what the cart hands the host, and every kick heard is matched to the game's clock at that moment:
// the clock must say the kick's chart time within a block of audio. Control: the same recording matched against the
// chart moved by an 80 ms (it must fail).
//
//   node tests/beatsync-sync.mjs [--gpu gl|webgpu|both] [--song test-groove] [--no-audio] [--keep-wav]
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { LANE_BUTTON, SCREEN, enterSong, gpusOf, openBeatsync, songs } from './lib/beatsync.mjs';

const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const GPUS = gpusOf(argv);
const SONGS = songs();
const LATE_MS = 50;
const SONG = SONGS.findIndex((s) => s.id === opt('song', 'test-groove'));
if (SONG < 0) { console.error(`no song ${opt('song', 'test-groove')} in songs/`); process.exit(2); }
const FRAME_MS = 1000 / 60;
const OUT = join(ROOT, 'out/gate/beatsync-sync');
mkdirSync(OUT, { recursive: true });

const RESULT = ['notes.total', 'notes.perfect', 'notes.good', 'notes.miss', 'judge.err_sum', 'judge.err_sq', 'judge.err_n', 'judge.err_max', 'game.screen', 'song.done'];

/** the open-loop bot on Hard, every press `late` ms after its note */
async function botRun(gpu, late) {
  const r = await openBeatsync('beatsync', { gpu, tag: late ? 'sync-control' : 'sync' });
  try {
    const f0 = await enterSong(r, { song: SONG, diff: 2 }, ['song.ms']);
    const m0 = f0['song.ms'];
    const notes = SONGS[SONG].charts.hard;
    const segs = [];
    let at = 0; // frames into the sequence so far
    for (const n of notes) {
      const want = Math.round((n.t * 1000 + late - m0) / FRAME_MS); // the frame (1 = the next) whose tick sees the press
      if (want - 1 > at) { segs.push({ pads: [{ buttons: [] }], frames: want - 1 - at }); at = want - 1; }
      segs.push({ pads: [{ buttons: [LANE_BUTTON[n.lane]] }], frames: 3 });
      at += 3;
    }
    segs.push({ pads: [{ buttons: [] }], frames: 600 });
    const res = await r.run(segs, { fields: RESULT, stopWhen: { field: 'game.screen', equals: SCREEN.results } });
    return { ...res.fields, m0 };
  } finally {
    await r.close();
  }
}

const stats = (f) => {
  const n = f['judge.err_n'] || 0, mean = n ? f['judge.err_sum'] / n : 0;
  return { mean: +mean.toFixed(2), sd: n ? +Math.sqrt(Math.max(0, f['judge.err_sq'] / n - mean * mean)).toFixed(2) : 0, max: f['judge.err_max'] };
};

/** kick onsets in a mono signal: a rise of the 30 ms energy envelope after a quiet stretch */
function onsets(x, rate) {
  const win = Math.round(rate * 0.004), out = [];
  const env = [];
  for (let i = 0; i + win <= x.length; i += win) {
    let e = 0;
    for (let k = 0; k < win; k++) e += x[i + k] * x[i + k];
    env.push(Math.sqrt(e / win));
  }
  const peak = Math.max(...env);
  let last = -1e9;
  for (let i = 10; i < env.length; i++) {
    const floor = Math.min(...env.slice(i - 10, i));
    if (env[i] < peak * 0.1 || env[i] < floor * 4 + peak * 0.02 || i - last < 38) continue; // a rise, 150 ms after the last
    last = i;
    // refine to the first sample over a third of this window's level
    let s = (i - 1) * win;
    const top = env[i] * 0.3;
    while (s < (i + 1) * win && Math.abs(x[s]) < top) s++;
    out.push(s);
  }
  return out;
}

function readWav(path) {
  const b = readFileSync(path);
  let p = 12, rate = 0, ch = 1, bits = 16, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString('ascii', p, p + 4), len = b.readUInt32LE(p + 4);
    if (id === 'fmt ') { ch = b.readUInt16LE(p + 10); rate = b.readUInt32LE(p + 12); bits = b.readUInt16LE(p + 22); }
    if (id === 'data') data = b.subarray(p + 8, p + 8 + len);
    p += 8 + len + (len & 1);
  }
  const fmt = b.readUInt16LE(20 + 0);
  const n = data.length / (bits / 8) / ch, x = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let s = 0;
    for (let c = 0; c < ch; c++) {
      const o = (i * ch + c) * (bits / 8);
      s += bits === 32 && fmt === 3 ? data.readFloatLE(o) : bits === 32 ? data.readInt32LE(o) / 2147483648 : data.readInt16LE(o) / 32768;
    }
    x[i] = s / ch;
  }
  return { rate, x };
}

/** the sound against the clock: the auto cart, the kick alone, recorded from a known clock reading */
async function audioRun(gpu) {
  const r = await openBeatsync('beatsync_auto', { gpu, tag: 'sync-audio' });
  try {
    await r.command('solo kick');
    const f = (await r.run([{ pads: [{ buttons: [] }], frames: 30 }], { fields: ['song.ms'] })).fields;
    const m0 = f['song.ms'], song = SONGS[0];
    const frames = Math.ceil((song.length_s * 1000 - m0) / FRAME_MS) + 30;
    const wav = join(OUT, `kick-${gpu}.wav`);
    await r.call('audioDebug', { op: 'record', frames, path: wav }, { timeoutMs: 900000 });
    const { rate, x } = readWav(wav);
    if (!argv.includes('--keep-wav')) rmSync(wav); // 10 MB a run: kept only when asked
    const kicks = song.charts.hard.filter((n) => n.lane === 0).map((n) => n.t * 1000);
    // the recording starts with the frame after the reading, whose tick sees the clock at m0 + one frame
    const heard = onsets(x, rate).map((s) => m0 + FRAME_MS + (s / rate) * 1000); // the clock when each kick reached the host
    const match = (shift) => {
      const d = [];
      for (const h of heard) {
        let best = null;
        for (const k of kicks) if (best === null || Math.abs(h - (k + shift)) < Math.abs(best)) best = h - (k + shift);
        if (best !== null && Math.abs(best) < 120) d.push(best);
      }
      const mean = d.reduce((a, b) => a + b, 0) / (d.length || 1);
      return { n: d.length, mean: +mean.toFixed(2), sd: +Math.sqrt(d.reduce((a, b) => a + (b - mean) ** 2, 0) / (d.length || 1)).toFixed(2), max: +Math.max(...d.map(Math.abs)).toFixed(1) };
    };
    return { heard: heard.length, kicks: kicks.length, ok: match(0), control: match(80), wav };
  } finally {
    await r.close();
  }
}

let failed = 0;
for (const gpu of GPUS) {
  const a = await botRun(gpu, 0);
  const s = stats(a);
  const tag = `sync (${gpu}, ${SONGS[SONG].id} Hard, open-loop bot)`;
  if (a['notes.perfect'] === a['notes.total'] && a['notes.total'] > 0 && a['game.screen'] === SCREEN.results)
    console.log(`ok   ${tag}: ${a['notes.perfect']} / ${a['notes.total']} Perfect; error mean ${s.mean} ms, sd ${s.sd} ms, worst ${s.max} ms`);
  else { console.log(`FAIL ${tag}: Perfect ${a['notes.perfect']}, Good ${a['notes.good']}, Miss ${a['notes.miss']} of ${a['notes.total']}; error mean ${s.mean} ms, sd ${s.sd}, worst ${s.max} (screen ${a['game.screen']})`); failed++; }
  const c = await botRun(gpu, LATE_MS);
  if (c['notes.perfect'] === 0 && c['notes.total'] > 0) console.log(`ok   sync control (${gpu}, every press ${LATE_MS} ms late): no Perfect, as it must (Good ${c['notes.good']}, Miss ${c['notes.miss']})`);
  else { console.log(`FAIL sync control (${gpu}, every press ${LATE_MS} ms late): ${c['notes.perfect']} Perfect: the judgement cannot tell late from on time`); failed++; }
  if (argv.includes('--no-audio')) continue;
  if (gpu === 'webgpu') { // romdev's audio recording on a WebGPU host returns one frame (reported in romdev's feedback); the sound is the same mix on either API
    console.log('skip sync audio (webgpu): romdev records one frame of audio on a WebGPU host; the recording runs on GL');
    continue;
  }
  const au = await audioRun(gpu);
  const okA = au.ok.n >= au.kicks * 0.9 && Math.abs(au.ok.mean) <= 15 && au.ok.sd <= 8;
  console.log(`${okA ? 'ok  ' : 'FAIL'} sync audio (${gpu}): ${au.ok.n} of ${au.kicks} kicks heard; the clock against the sound: mean ${au.ok.mean} ms, sd ${au.ok.sd} ms, worst ${au.ok.max} ms`);
  if (!okA) failed++;
  const badC = Math.abs(au.control.mean) <= 15 && au.control.n >= au.kicks * 0.9;
  console.log(`${badC ? 'FAIL' : 'ok  '} sync audio control (${gpu}, the chart moved 80 ms): mean ${au.control.mean} ms${badC ? ': the measure cannot see a shift' : ', off as it must be'}`);
  if (badC) failed++;
}
console.log(failed ? `beatsync-sync: ${failed} failed` : 'beatsync-sync: ok');
process.exit(failed ? 1 : 0);
