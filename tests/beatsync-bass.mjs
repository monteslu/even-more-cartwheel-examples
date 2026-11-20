// beatsync's bass gate: is a bass-lane note heard when it is hit or missed, in the whole mix as the player hears it?
// (the mix gate measures the kick on its own track; the bass sits 8 to 11 dB under the drums, and a bass-lane note in
// a break is made from the other track, so neither shows there). Recorded through romdev on GL from beatsync_auto
// playing on Hard, every note hit, against the same run letting one bass-lane note go by (`skip 2 1`):
//
// 1. A real bass note missed: the mix under 250 Hz is at least BASS_MISS_DB under the run that hit it, over the 200 ms
//    from 50 ms after the note (a press reaches the sound a few mixer blocks on: the note itself, unpunched, then the
//    dip; Late Night).
//    CONTROL (must fail): the same with `bassmix old` (the bass at the kick and clap's levels, as 0.32.2 had it).
// 2. A real bass note hit: under 250 Hz, the bass notes played stand at least BASS_HIT_DB over the record (`reward 0`,
//    every lane at full) in their first 120 ms. CONTROL (must fail): the same with `bassmix old`.
// 3. A bass-lane note made from the other track (a break note, `o` in song.json) missed: the whole mix drops at least
//    OTHER_MISS_DB from 300 Hz to 4 kHz (where its chords and stabs sound; the drums fill the rest) in the 200 ms
//    from 50 ms after it (Tech House). CONTROL (must fail): `othersrc 0` (it acts on the bass lane,
//    which is silent there, as 0.32.2 did).
//
//   node tests/beatsync-bass.mjs [--keep-wav]
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { openBeatsync, songs } from './lib/beatsync.mjs';

const argv = process.argv.slice(2);
const OUT = join(ROOT, 'out/gate/beatsync-bass');
mkdirSync(OUT, { recursive: true });
const FRAME_MS = 1000 / 60;
const BASS_MISS_DB = -8, BASS_HIT_DB = 2, OTHER_MISS_DB = -3;
const SONGS = Object.fromEntries(songs().map((s) => [s.id, s]));

function readWav(path) {
  const b = readFileSync(path);
  let p = 12, rate = 0, ch = 1, bits = 16, fmt = 1, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString('ascii', p, p + 4), len = b.readUInt32LE(p + 4);
    if (id === 'fmt ') { fmt = b.readUInt16LE(p + 8); ch = b.readUInt16LE(p + 10); rate = b.readUInt32LE(p + 12); bits = b.readUInt16LE(p + 22); }
    if (id === 'data') data = b.subarray(p + 8, p + 8 + len);
    p += 8 + len + (len & 1);
  }
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
/** the signal under fc Hz (two one-pole low-passes) */
function lowband(w, fc) {
  const a = Math.exp((-2 * Math.PI * fc) / w.rate), y = new Float32Array(w.x.length);
  let s1 = 0, s2 = 0;
  for (let i = 0; i < y.length; i++) { s1 = (1 - a) * w.x[i] + a * s1; s2 = (1 - a) * s1 + a * s2; y[i] = s2; }
  return { rate: w.rate, x: y };
}
function db(w, a, b) {
  const i0 = Math.max(0, Math.round(a * w.rate)), i1 = Math.min(w.x.length, Math.round(b * w.rate));
  let e = 0;
  for (let i = i0; i < i1; i++) e += w.x[i] * w.x[i];
  return 10 * Math.log10(e / Math.max(1, i1 - i0) + 1e-12);
}

/** the auto cart on `song` Hard, `pre` commands first, at song.ms >= at the `skip`, then `frames` recorded */
async function record(tag, { song, pre = [], at, skip = null, frames = 300 }) {
  const r = await openBeatsync('beatsync_auto', { gpu: 'gl', tag: `bass-${tag}` });
  try {
    for (const c of pre) await r.command(c);
    await r.command(`song ${song} hard`);
    await r.run([{ pads: [{ buttons: [] }], frames: 60 * 60 }], { stopWhen: { field: 'song.ms', atLeast: at } });
    await r.command(skip ? `skip 2 1 ${skip}` : 'skip 2 0');
    const m0 = (await r.fields())['song.ms'];
    const path = join(OUT, `${tag}.wav`);
    await r.call('audioDebug', { op: 'record', frames, path }, { timeoutMs: 900000 });
    const w = readWav(path);
    if (!argv.includes('--keep-wav')) rmSync(path);
    if (!w.rate || w.x.length < w.rate) throw new Error(`${tag}: the recording holds ${w.x.length} samples`);
    const lo300 = lowband(w, 300), lo4k = lowband(w, 4000);
    const mid = { rate: w.rate, x: lo4k.x.map((v, i) => v - lo300.x[i]) }; // 300 Hz to 4 kHz: where the other track's chords and stabs sit
    return { w, low: lowband(w, 250), mid, m0, f: await r.fields() };
  } finally {
    await r.close();
  }
}
const sec = (rec, ms) => (ms - rec.m0 - FRAME_MS) / 1000;
const win = (rec, w, ms, len) => db(w, sec(rec, ms), sec(rec, ms + len));

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const fmt = (v) => (v >= 0 ? '+' : '') + v.toFixed(2);
const noteAt = (song, ms) => SONGS[song].charts.hard.find((n) => n.lane === 2 && Math.abs(n.t * 1000 - ms) < 30);

/** a miss of the next bass-lane note after skipAt, against the same run hitting it: dB in the 200 ms from 50 ms after it */
async function missDip(tag, song, at, skipAt, pre, band, frames = 300) {
  const hit = await record(`${tag}-hit`, { song, pre, at, frames });
  const miss = await record(`${tag}-miss`, { song, pre, at, skip: skipAt, frames });
  const t = miss.f['bot.skipped_ms'];
  const pick = (r) => r[band];
  // what the player hears is the note itself: punched on a hit, left at the lane's level between hits on a miss and
  // then dipped. A press reaches the sound a few mixer blocks after it is made (the mixer runs ahead), so the note's
  // first 50 ms are the same either way: the 200 ms from 50 ms on (a stab rings on a hit and is cut short on a miss)
  const m = t + 50;
  return { t, note: noteAt(song, t), missed: miss.f['notes.miss'] - hit.f['notes.miss'], d: win(miss, pick(miss), m, 200) - win(hit, pick(hit), m, 200), before: win(miss, pick(miss), t - 700, 250) - win(hit, pick(hit), t - 700, 250), hit };
}

// 1. a real bass note missed (Late Night, 30 s in)
const LN = 'late-night', AT = 30000, SKIP = 31000;
const real = await missDip('real', LN, AT, SKIP, [], 'low', 600);
check(real.missed === 1 && real.note && !real.note.o, `bass: the note let go by at ${real.t} ms is a real bass note (missed ${real.missed}, ${real.note?.o ? 'made from the other track' : 'from the bass track'})`);
check(Math.abs(real.before) < 0.5, `bass: before the miss the two runs match under 250 Hz (${fmt(real.before)} dB)`);
check(real.d <= BASS_MISS_DB, `bass: a missed bass note drops the mix under 250 Hz ${fmt(real.d)} dB in the 200 ms from 50 ms after it (at least ${-BASS_MISS_DB} dB down)`);
const realOld = await missDip('real-old', LN, AT, SKIP, ['bassmix old'], 'low', 600);
check(realOld.d > BASS_MISS_DB, `bass control (bassmix old): the same miss drops it ${fmt(realOld.d)} dB, short of ${-BASS_MISS_DB}, so the gate fails it, as it must`);

// 2. real bass notes hit, against the record
async function lift(hitRun, pre, tag) {
  const ref = await record(`${tag}-record`, { song: LN, pre: [...pre, 'reward 0'], at: AT, frames: 600 });
  const until = hitRun.m0 + 600 * FRAME_MS - 200;
  const ds = SONGS[LN].charts.hard.filter((n) => n.lane === 2 && !n.o && n.t * 1000 > hitRun.m0 + 50 && n.t * 1000 < until)
    .map((n) => win(hitRun, hitRun.low, n.t * 1000, 120) - win(ref, ref.low, n.t * 1000, 120));
  return { n: ds.length, mean: ds.reduce((a, b) => a + b, 0) / (ds.length || 1) };
}
const up = await lift(real.hit, [], 'lift');
check(up.n >= 6 && up.mean >= BASS_HIT_DB, `bass: ${up.n} bass notes hit stand ${fmt(up.mean)} dB over the record under 250 Hz (at least +${BASS_HIT_DB})`);
const upOld = await lift(realOld.hit, ['bassmix old'], 'lift-old');
check(upOld.mean < BASS_HIT_DB, `bass control (bassmix old): bass notes hit stand ${fmt(upOld.mean)} dB over the record, under +${BASS_HIT_DB}, so the gate fails it, as it must`);

// 3. a bass-lane note made from the other track missed (Tech House, 30 s in)
const TH = 'tech-house';
const oth = await missDip('other', TH, AT, SKIP, [], 'mid');
check(oth.missed === 1 && oth.note?.o === 1, `bass: the note let go by at ${oth.t} ms is made from the other track (missed ${oth.missed})`);
check(oth.d <= OTHER_MISS_DB, `bass: a missed note made from the other track drops the mix from 300 Hz to 4 kHz (where it sounds) ${fmt(oth.d)} dB in the 200 ms from 50 ms after it (at least ${-OTHER_MISS_DB} dB down)`);
const othOld = await missDip('other-old', TH, AT, SKIP, ['othersrc 0'], 'mid');
check(othOld.d > OTHER_MISS_DB, `bass control (othersrc 0): the same miss on the bass lane drops it ${fmt(othOld.d)} dB, short of ${-OTHER_MISS_DB}, so the gate fails it, as it must`);

console.log(failed ? `beatsync-bass: ${failed} failed` : 'beatsync-bass: ok');
process.exit(failed ? 1 : 0);
