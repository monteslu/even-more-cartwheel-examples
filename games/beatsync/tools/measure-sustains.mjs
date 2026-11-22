#!/usr/bin/env node
// How long the bass and the synth (the other track) hold their notes, per song: the question before held notes
// (sustains) are charted. Each onset found in the decoded track (as the charts find them) rings until its loudness
// falls 6 dB under the peak just after it, or the next onset comes; a note of a beat or more is a sustain.
//
//   node tools/measure-sustains.mjs <songs dir> [<songs dir> ...] [--jobs 8] [--json out.json]
import { readdirSync, readFileSync, existsSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Worker, isMainThread, parentPort, workerData } from 'node:worker_threads';
import { decodeVorbisLib } from './lib/ogg.mjs';
import { bandOnsets, bassOnsets, monoOf } from './lib/analyze.mjs';
import { OTHER_ONSETS } from './lib/analyze.mjs';

/** the RMS envelope of x in hop-second steps over win-second windows */
function envelope(x, rate, hop = 0.01, win = 0.03) {
  const h = Math.round(hop * rate), w = Math.round(win * rate), out = new Float32Array(Math.ceil(x.length / h));
  for (let i = 0, k = 0; i < x.length; i += h, k++) {
    let s = 0, n = 0;
    for (let j = Math.max(0, i - (w >> 1)); j < Math.min(x.length, i + (w >> 1)); j++) s += x[j] * x[j], n++;
    out[k] = Math.sqrt(s / Math.max(1, n));
  }
  return out;
}

/** each onset's length in seconds: to 6 dB under its peak (in the first 80 ms after it), or the next onset */
function lengths(onsets, env, hop = 0.01, endS) {
  const t = onsets.map((o) => o.t).sort((a, b) => a - b), out = [];
  for (let i = 0; i < t.length; i++) {
    const a = Math.round(t[i] / hop), next = i + 1 < t.length ? Math.round(t[i + 1] / hop) : Math.round(endS / hop);
    let peak = 0;
    for (let k = a; k < Math.min(next, a + 8, env.length); k++) peak = Math.max(peak, env[k]);
    if (peak <= 0) continue;
    let k = a;
    while (k < next && k < env.length && env[k] >= peak * 0.5) k++;
    out.push((k - a) * hop);
  }
  return out;
}

async function measure(dir) {
  const song = JSON.parse(readFileSync(join(dir, 'song.json'), 'utf8'));
  const beat = 60 / song.bpm, res = { id: song.id ?? dir, title: song.title, artist: song.artist, bpm: song.bpm };
  for (const [part, file] of [['bass', song.tracks.bass], ['synth', song.tracks.other]]) {
    const d = await decodeVorbisLib(readFileSync(join(dir, file)));
    const x = monoOf(d.ch), env = envelope(x, d.rate);
    const on = part === 'bass' ? bassOnsets(x, d.rate, []).onsets : bandOnsets(x, d.rate, OTHER_ONSETS).onsets;
    const len = lengths(on, env, 0.01, x.length / d.rate).map((s) => s / beat); // in beats
    const held = len.filter((b) => b >= 1);
    res[part] = { notes: len.length, sustains: held.length, share: len.length ? +(held.length / len.length).toFixed(3) : 0,
      long2: len.filter((b) => b >= 2).length, beatsHeld: +held.reduce((a, b) => a + b, 0).toFixed(1),
      perMinute: +(held.length / (song.length_s / 60)).toFixed(1) };
  }
  return res;
}

if (!isMainThread) {
  parentPort.postMessage(await measure(workerData).catch((e) => ({ id: workerData, error: e.message })));
} else {
  const argv = process.argv.slice(2), ji = argv.indexOf('--jobs'), jo = argv.indexOf('--json');
  const JOBS = ji >= 0 ? +argv[ji + 1] : 8, OUT = jo >= 0 ? argv[jo + 1] : null;
  const roots = argv.filter((a, i) => !a.startsWith('--') && argv[i - 1] !== '--jobs' && argv[i - 1] !== '--json');
  const dirs = roots.flatMap((r) => readdirSync(r).map((d) => join(r, d)).filter((d) => existsSync(join(d, 'song.json'))));
  const results = [], q = [...dirs];
  const run = (dir) => new Promise((done) => {
    const w = new Worker(fileURLToPath(import.meta.url), { workerData: dir });
    w.once('message', (m) => { results.push(m); done(); });
    w.once('error', (e) => { results.push({ id: dir, error: e.message }); done(); });
  });
  await Promise.all(Array.from({ length: JOBS }, async () => { while (q.length) await run(q.shift()); }));
  if (OUT) writeFileSync(OUT, `${JSON.stringify(results, null, 1)}\n`);
  const ok = results.filter((r) => !r.error);
  const tot = (p, k) => ok.reduce((a, r) => a + r[p][k], 0);
  for (const p of ['bass', 'synth']) {
    const per = ok.map((r) => r[p].perMinute).sort((a, b) => a - b);
    console.log(`${p}: ${tot(p, 'sustains')} sustains of ${tot(p, 'notes')} notes (${(100 * tot(p, 'sustains') / tot(p, 'notes')).toFixed(1)}%), ${tot(p, 'long2')} of 2+ beats; per song a minute: median ${per[per.length >> 1]}, ${per.filter((v) => v >= 4).length} of ${per.length} songs with 4+ a minute, ${per.filter((v) => v < 1).length} with under 1`);
  }
  for (const r of results.filter((x) => x.error)) console.log(`error ${r.id}: ${r.error}`);
}
