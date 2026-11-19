// beatsync's mix, measured from the sound the cart hands the host (romdev's audio recording), on GL (romdev records one
// frame of audio on a WebGPU host; the mix is the same code on either API):
//
// 1. A miss touches only the part missed. beatsync_auto plays Test Groove on Hard with one track heard alone (`solo`),
//    twice from the same deterministic load: once hitting every note, once letting one kick go by (`skip 0 1`, a
//    miss). The two recordings are compared in 20 ms windows: the kick must dip (at least 6 dB within 300 ms of the
//    missed note) and come back (within 1 dB from a beat and a half after it); the clap, the bass and the other track
//    must stay within 0.5 dB the whole time.
//    CONTROL (must fail): the same miss with `missall 1` (a miss ducks every lane, the way a mix collapses) moves the
//    clap and the bass by more than 0.5 dB.
// 2. A hit is heard. On Easy (whose chart plays the kicks on 1 and 3, the clap owning 2 and 4) the kick track alone is
//    recorded with the reward on and with it off (`reward 0`: every lane at full between hits, the record as it is),
//    and every kick of the track (the Hard chart's kicks) is measured in its first 90 ms against the reward-off run: the
//    kicks the player hit must stand at least 3 dB over the ones nobody played, and within 1 dB of the record.
//    CONTROL (must fail): a run with the reward off measured the same way shows no lift.
//
//   node tests/beatsync-mix.mjs [--keep-wav]
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { openBeatsync, songs } from './lib/beatsync.mjs';

const argv = process.argv.slice(2);
const OUT = join(ROOT, 'out/gate/beatsync-mix');
mkdirSync(OUT, { recursive: true });
const FRAME_MS = 1000 / 60;
const SONG = songs().find((s) => s.id === 'test-groove');
const BEAT_MS = 60000 / SONG.bpm;
const TRACKS = ['kick', 'clap', 'bass', 'other'];

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

/** RMS in dB of x over [a, b) seconds */
function db(w, a, b) {
  const i0 = Math.max(0, Math.round(a * w.rate)), i1 = Math.min(w.x.length, Math.round(b * w.rate));
  let e = 0;
  for (let i = i0; i < i1; i++) e += w.x[i] * w.x[i];
  return 10 * Math.log10(e / Math.max(1, i1 - i0) + 1e-12);
}

/** one run: the auto cart, `pre` commands, the song started, `at` commands at song.ms >= atMs, then `frames` recorded.
 * Returns the recording, the song clock at its first sample, and the fields after. */
async function record(tag, { pre = [], start = null, atMs, at = [], frames }) {
  const r = await openBeatsync('beatsync_auto', { gpu: 'gl', tag: `mix-${tag}` });
  try {
    for (const c of pre) await r.command(c);
    if (start) await r.command(start);
    await r.run([{ pads: [{ buttons: [] }], frames: 600 }], { stopWhen: { field: 'song.ms', atLeast: atMs } });
    for (const c of at) await r.command(c);
    const m0 = (await r.fields())['song.ms'];
    const path = join(OUT, `${tag}.wav`);
    await r.call('audioDebug', { op: 'record', frames, path }, { timeoutMs: 900000 });
    const w = readWav(path);
    if (!argv.includes('--keep-wav')) rmSync(path);
    if (!w.rate || w.x.length < w.rate) throw new Error(`${tag}: the recording holds ${w.x.length} samples`);
    return { w, m0, f: await r.fields() };
  } finally {
    await r.close();
  }
}
/** song ms -> seconds into a recording that started at clock m0 (the frame after the reading) */
const sec = (rec, ms) => (ms - rec.m0 - FRAME_MS) / 1000;

/** windowed dB difference b - a over [from, to) song ms, windows where a is over the floor */
function diffs(a, b, from, to, win = 20) {
  const out = [];
  for (let t = from; t + win <= to; t += win) {
    const da = db(a.w, sec(a, t), sec(a, t + win)), dbb = db(b.w, sec(b, t), sec(b, t + win));
    if (da > -55) out.push({ t, d: dbb - da });
  }
  return out;
}

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const fmt = (v) => (v >= 0 ? '+' : '') + v.toFixed(2);

// ── 1. a miss ──
const AT = 6000, FRAMES = 360; // in the groove, then 6 s
const miss = {};
for (const variant of ['hit', 'miss', 'missall'])
  for (const t of TRACKS) {
    if (variant === 'missall' && t === 'kick') continue;
    const at = variant === 'hit' ? ['skip 0 0'] : [`skip 0 1 ${AT + 1500}`]; // the kick 1.5 s on, after a stretch both runs share
    const pre = [`solo ${t}`, ...(variant === 'missall' ? ['missall 1'] : [])];
    (miss[variant] ||= {})[t] = await record(`${variant}-${t}`, { pre, atMs: AT, at, frames: FRAMES });
  }
const skipped = miss.miss.kick.f['bot.skipped_ms'];
check(skipped > AT && miss.miss.kick.f['notes.miss'] === 1 && miss.hit.kick.f['notes.miss'] === 0, `mix: the skipped kick at ${skipped} ms was missed (misses ${miss.miss.kick.f['notes.miss']}, ${miss.hit.kick.f['notes.miss']} without the skip)`);
const end = miss.hit.kick.m0 + FRAMES * FRAME_MS - 100;
{
  const d = diffs(miss.hit.kick, miss.miss.kick, AT + 100, end);
  const dip = Math.min(...d.filter((x) => x.t >= skipped - 20 && x.t < skipped + 300).map((x) => x.d));
  const after = d.filter((x) => x.t >= skipped + 1.5 * BEAT_MS);
  const back = Math.max(...after.map((x) => Math.abs(x.d)));
  const before = Math.max(...d.filter((x) => x.t < skipped - 80).map((x) => Math.abs(x.d)));
  check(dip <= -6, `mix: the missed kick dips ${fmt(dip)} dB within 300 ms (at least 6 dB down)`);
  check(back <= 1 && after.length > 20, `mix: the kick is back within ${back.toFixed(2)} dB from a beat and a half after the miss (at most 1 dB, ${after.length} windows)`);
  check(before <= 0.5 && Number.isFinite(before), `mix: before the miss the two kick runs match within ${before.toFixed(2)} dB (the runs are the same)`);
}
for (const t of TRACKS.slice(1)) {
  const d = diffs(miss.hit[t], miss.miss[t], AT + 100, end);
  const worst = Math.max(...d.map((x) => Math.abs(x.d)));
  check(worst <= 0.5 && d.length > 50, `mix: after the kick miss the ${t} track stays within ${worst.toFixed(2)} dB (at most 0.5, ${d.length} windows)`);
  const c = diffs(miss.hit[t], miss.missall[t], AT + 100, end);
  const cw = Math.max(...c.map((x) => Math.abs(x.d)));
  if (t === 'other') { console.log(`info mix control: the other track (never ducked) moves ${cw.toFixed(2)} dB under missall`); continue; }
  check(cw > 0.5, `mix control (missall: a miss ducks every lane): the ${t} track moves ${cw.toFixed(2)} dB, over 0.5, so the gate fails it, as it must`);
}

// ── 2. a hit ──
const easyKicks = SONG.charts.easy.filter((n) => n.lane === 0).map((n) => n.t * 1000);
const allKicks = SONG.charts.hard.filter((n) => n.lane === 0).map((n) => n.t * 1000);
const RFRAMES = 900;
async function lift(tag, pre) {
  const ref = await record(`ref-${tag}`, { pre: ['solo kick', 'reward 0'], start: 'song test-groove easy', atMs: 1000, frames: RFRAMES });
  const run = await record(`run-${tag}`, { pre: ['solo kick', ...pre], start: 'song test-groove easy', atMs: 1000, frames: RFRAMES });
  const until = ref.m0 + RFRAMES * FRAME_MS - 150;
  const played = [], idle = [];
  for (const k of allKicks) {
    if (k < ref.m0 + 50 || k > until) continue;
    const d = db(run.w, sec(run, k - 5), sec(run, k + 90)) - db(ref.w, sec(ref, k - 5), sec(ref, k + 90));
    (easyKicks.some((e) => Math.abs(e - k) < 25) ? played : idle).push(d);
  }
  const mean = (a) => a.reduce((x, y) => x + y, 0) / (a.length || 1);
  return { played: mean(played), idle: mean(idle), n: [played.length, idle.length], lift: mean(played) - mean(idle) };
}
{
  const g = await lift('on', []);
  check(g.n[0] >= 8 && g.n[1] >= 8, `mix: ${g.n[0]} kicks the player hit and ${g.n[1]} nobody played in the recording`);
  check(g.lift >= 3 && Math.abs(g.played) <= 1, `mix: a hit kick stands ${fmt(g.lift)} dB over an unplayed one (at least 3), at ${fmt(g.played)} dB against the record (within 1)`);
  const c = await lift('off', ['reward 0']);
  check(c.lift < 3, `mix control (reward off): a hit kick stands ${fmt(c.lift)} dB over an unplayed one, under 3, so the gate fails it, as it must`);
}
console.log(failed ? `beatsync-mix: ${failed} failed` : 'beatsync-mix: ok');
process.exit(failed ? 1 : 0);
