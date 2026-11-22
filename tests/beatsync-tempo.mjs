// beatsync's tempo gate: the shoulders change the song's tempo with its pitch kept, and the game stays in time with the
// sound at that tempo. Played through romdev on the real cart, as a player does it (the right shoulder pressed four
// times in the count-in: 120%):
//   - the open-loop bot of the sync gate, on Hard: it reads the song clock once and presses each note at the frame the
//     wall clock says it is due AT 120% ((note time - reading) / 1.2). Every note Perfect proves the clock, the
//     judgement and the sound run at the tempo shown. CONTROL: the same bot timed at the song's own speed must get
//     next to no Perfect (it proves the tempo really changed the song, not just the number on screen).
//   - the sound against the clock at 120%: the kick alone, recorded; each kick heard must be where the clock says,
//     in song time (the sync gate's audio check, at the tempo). Control: the chart moved by 80 ms must fail.
//   - the pitch: the bass alone, recorded at 100% and at 120%; their spectra (on a log-frequency scale) must line
//     up with no shift (keylock: the same notes, faster). CONTROL: 120% by playback rate (`tempomode rate`, a
//     record player sped up) must shift by the 3.2 semitones of 1.2, or the measure cannot hear a pitch change.
//
//   node tests/beatsync-tempo.mjs [--gpu gl|webgpu|both] [--song test-groove] [--no-audio]
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { LANE_BUTTON, SCREEN, enterSong, gpusOf, openBeatsync, songs, tap } from './lib/beatsync.mjs';

const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const GPUS = gpusOf(argv);
const SONGS = songs();
const SONG = SONGS.findIndex((s) => s.id === opt('song', 'test-groove'));
if (SONG < 0) { console.error(`no song ${opt('song', 'test-groove')} in songs/`); process.exit(2); }
const TEMPO = 1.2, STEPS = 4; // four presses of R, 5% each
const FRAME_MS = 1000 / 60;
const OUT = join(ROOT, 'out/gate/beatsync-tempo');
mkdirSync(OUT, { recursive: true });
const RESULT = ['notes.total', 'notes.perfect', 'notes.good', 'notes.miss', 'judge.err_sum', 'judge.err_n', 'judge.err_max', 'game.screen', 'game.tempo_pct'];
const upTaps = () => Array.from({ length: STEPS }, () => tap('r', 3, 5)).flat();

/** the open-loop bot on Hard at 120%, its presses timed for `assume` (TEMPO, or 1 for the control) */
async function botRun(gpu, assume) {
  const r = await openBeatsync('beatsync', { gpu, tag: assume === TEMPO ? 'tempo' : 'tempo-control' });
  try {
    await enterSong(r, { song: SONG, diff: 2 }, []);
    const f0 = (await r.run([...upTaps(), { pads: [{ buttons: [] }], frames: 20 }], { fields: ['song.ms', 'game.tempo_pct'] })).fields;
    if (f0['game.tempo_pct'] !== 120) throw new Error(`four presses of R gave tempo ${f0['game.tempo_pct']}%, not 120%`);
    const m0 = f0['song.ms'];
    const segs = [];
    let at = 0;
    for (const n of SONGS[SONG].charts.hard) {
      const want = Math.round((n.t * 1000 - m0) / assume / FRAME_MS);
      if (want - 1 > at) { segs.push({ pads: [{ buttons: [] }], frames: want - 1 - at }); at = want - 1; }
      segs.push({ pads: [{ buttons: [LANE_BUTTON[n.lane]] }], frames: 3 });
      at += 3;
    }
    segs.push({ pads: [{ buttons: [] }], frames: 600 });
    return (await r.run(segs, { fields: RESULT, stopWhen: { field: 'game.screen', equals: SCREEN.results } })).fields;
  } finally {
    await r.close();
  }
}

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

/** kick onsets (the sync gate's detector): a rise of the 4 ms envelope after a quiet stretch */
function onsets(x, rate) {
  const win = Math.round(rate * 0.004), env = [], out = [];
  for (let i = 0; i + win <= x.length; i += win) {
    let e = 0;
    for (let k = 0; k < win; k++) e += x[i + k] * x[i + k];
    env.push(Math.sqrt(e / win));
  }
  const peak = Math.max(...env);
  let last = -1e9;
  for (let i = 10; i < env.length; i++) {
    const floor = Math.min(...env.slice(i - 10, i));
    if (env[i] < peak * 0.1 || env[i] < floor * 4 + peak * 0.02 || i - last < 38) continue;
    last = i;
    let s = (i - 1) * win;
    const top = env[i] * 0.3;
    while (s < (i + 1) * win && Math.abs(x[s]) < top) s++;
    out.push(s);
  }
  return out;
}

/** the mean magnitude spectrum on a log-frequency scale: 48 bins an octave, 40 Hz to 2.5 kHz (a 32768-point FFT: 1.5 Hz
 * apart, fine enough for the bass's lowest octave) */
const BPO = 48, F0 = 40, NB = Math.floor(BPO * Math.log2(2500 / F0));
function logSpectrum(x, rate) {
  const N = 32768, half = N / 2, mag = new Float64Array(half);
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let start = 0; start + N <= x.length; start += N / 2) {
    for (let i = 0; i < N; i++) re[i] = x[start + i] * (0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (N - 1))), im[i] = 0;
    fft(re, im);
    for (let k = 0; k < half; k++) mag[k] += Math.hypot(re[k], im[k]);
  }
  const out = new Float64Array(NB);
  for (let b = 0; b < NB; b++) {
    const lo = (F0 * 2 ** (b / BPO) * N) / rate, hi = (F0 * 2 ** ((b + 1) / BPO) * N) / rate;
    let s = 0, n = 0;
    for (let k = Math.floor(lo); k <= Math.ceil(hi) && k < half; k++) s += mag[k], n++;
    out[b] = Math.log(1e-9 + s / Math.max(1, n));
  }
  return out;
}
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const a = (-2 * Math.PI) / len, wr = Math.cos(a), wi = Math.sin(a);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const ur = re[i + k], ui = im[i + k], vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci, vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
        re[i + k] = ur + vr, im[i + k] = ui + vi, re[i + k + len / 2] = ur - vr, im[i + k + len / 2] = ui - vi;
        [cr, ci] = [cr * wr - ci * wi, cr * wi + ci * wr];
      }
    }
  }
}
/** the shift (in semitones) that best lines spectrum b up with a: the pitch change from a to b */
function pitchShift(a, b) {
  let best = 0, bestC = -Infinity;
  const ma = a.reduce((s, v) => s + v, 0) / a.length, mb = b.reduce((s, v) => s + v, 0) / b.length;
  for (let s = -BPO / 2; s <= BPO / 2; s++) {
    let c = 0, n = 0;
    for (let i = Math.max(0, -s); i < NB && i + s < NB; i++) c += (a[i] - ma) * (b[i + s] - mb), n++;
    if (c / n > bestC) bestC = c / n, best = s;
  }
  return (best * 12) / BPO;
}

/** the auto cart: a track alone at a tempo (pressed in the count-in), recorded once the song is going */
async function record(gpu, { track, steps, mode = 'keylock', frames, name }) {
  const r = await openBeatsync('beatsync_auto', { gpu, tag: `tempo-${name}` });
  try {
    await r.command(`tempomode ${mode}`);
    await r.command(`solo ${track}`);
    await r.command(`song ${SONGS[SONG].id} hard`);
    const taps = Array.from({ length: steps }, () => tap('r', 3, 5)).flat();
    await r.run([...taps, { pads: [{ buttons: [] }], frames: 10 }]);
    // into the song: past the count-in and two seconds of play
    const f = (await r.run([{ pads: [{ buttons: [] }], frames: 600 }], { fields: ['song.ms', 'game.tempo_pct'], stopWhen: { field: 'song.ms', atLeast: 2000 } })).fields;
    const wav = join(OUT, `${name}-${gpu}.wav`);
    await r.call('audioDebug', { op: 'record', frames, path: wav }, { timeoutMs: 900000 });
    const w = readWav(wav);
    if (!argv.includes("--keep-wav")) rmSync(wav);
    return { ...w, m0: f['song.ms'], pct: f['game.tempo_pct'] };
  } finally {
    await r.close();
  }
}

let failed = 0;
const say = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
for (const gpu of GPUS) {
  const a = await botRun(gpu, TEMPO);
  const mean = a['judge.err_n'] ? (a['judge.err_sum'] / a['judge.err_n']).toFixed(1) : 0;
  say(a['notes.perfect'] === a['notes.total'] && a['notes.total'] > 0 && a['game.screen'] === SCREEN.results,
    `tempo (${gpu}, ${SONGS[SONG].id} Hard at 120%, open-loop bot): ${a['notes.perfect']} / ${a['notes.total']} Perfect (Good ${a['notes.good']}, Miss ${a['notes.miss']}); error mean ${mean} song ms, worst ${a['judge.err_max']}`);
  const c = await botRun(gpu, 1);
  say(c['notes.perfect'] < c['notes.total'] * 0.1, `tempo control (${gpu}, the bot timed at the song's own speed): ${c['notes.perfect']} of ${c['notes.total']} Perfect${c['notes.perfect'] < c['notes.total'] * 0.1 ? ', as it must be' : ': the tempo did not change the song'}`);
  if (argv.includes('--no-audio')) continue;
  if (gpu === 'webgpu') { console.log('skip tempo audio (webgpu): romdev records one frame of audio on a WebGPU host; the recording runs on GL'); continue; }
  // the kick against the clock at 120%: every charted kick in the recording heard within 30 ms (song time) of the clock
  const k = await record(gpu, { track: 'kick', steps: STEPS, frames: 60 * 20, name: 'kick' });
  const kicks = SONGS[SONG].charts.hard.filter((n) => n.lane === 0).map((n) => n.t * 1000);
  const heard = onsets(k.x, k.rate).map((s) => k.m0 + (FRAME_MS + (s / k.rate) * 1000) * TEMPO); // song ms
  const match = (shift) => {
    const d = [];
    for (const h of heard) {
      let best = null;
      for (const t of kicks) if (best === null || Math.abs(h - (t + shift)) < Math.abs(best)) best = h - (t + shift);
      if (best !== null && Math.abs(best) < 120) d.push(best);
    }
    const m = d.reduce((x, y) => x + y, 0) / (d.length || 1);
    return { n: d.length, mean: +m.toFixed(1), sd: +Math.sqrt(d.reduce((x, y) => x + (y - m) ** 2, 0) / (d.length || 1)).toFixed(1) };
  };
  const span = [heard[0] - 50, heard[heard.length - 1] + 50];
  const due = kicks.filter((t) => t >= span[0] && t <= span[1]);
  const found = (shift) => due.filter((t) => heard.some((h) => Math.abs(h - (t + shift)) <= 30)).length;
  const ok = match(0), got = found(0), ctl = found(80);
  say(due.length >= 10 && got >= due.length * 0.9 && Math.abs(ok.mean) <= 15 * TEMPO && ok.sd <= 10,
    `tempo audio (${gpu}, kick at ${k.pct}%): ${got} of ${due.length} charted kicks heard where the clock says; mean ${ok.mean} song ms, sd ${ok.sd}`);
  say(ctl < due.length * 0.5, `tempo audio control (${gpu}, the chart moved 80 ms): ${ctl} of ${due.length} found, off as it must be`);
  // the pitch: the bass at 100%, at 120% with keylock, at 120% by rate
  // the same stretch of the song each time (its notes change): 12 s of song is 12 s at 100% and 10 s at 120%
  const base = await record(gpu, { track: 'bass', steps: 0, frames: Math.round(60 * 10 * TEMPO), name: 'bass100' });
  const kl = await record(gpu, { track: 'bass', steps: STEPS, frames: 60 * 10, name: 'bass120' });
  const rt = await record(gpu, { track: 'bass', steps: STEPS, mode: 'rate', frames: 60 * 10, name: 'bass120rate' });
  const sb = logSpectrum(base.x, base.rate);
  const sk = pitchShift(sb, logSpectrum(kl.x, kl.rate)), sr = pitchShift(sb, logSpectrum(rt.x, rt.rate));
  const want = 12 * Math.log2(TEMPO);
  say(Math.abs(sk) <= 0.5 && kl.pct === 120, `tempo pitch (${gpu}, the bass at ${kl.pct}% with keylock): ${sk.toFixed(2)} semitones from 100%, the pitch kept`);
  say(Math.abs(sr - want) <= 0.75, `tempo pitch control (${gpu}, 120% by playback rate): ${sr.toFixed(2)} semitones (${want.toFixed(2)} expected), the measure hears a pitch change`);
}
console.log(failed ? `beatsync-tempo: ${failed} failed` : 'beatsync-tempo: ok');
process.exit(failed ? 1 : 0);
