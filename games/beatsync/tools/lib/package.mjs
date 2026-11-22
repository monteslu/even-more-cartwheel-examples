// Writing and proving a song package (docs/DESIGN.md, "The song package"): the four tracks as Ogg Vorbis, song.json,
// preview.wav (the mix with a click on every Hard note) and report.json. The alignment proof decodes every track
// back with two decoders and cross-correlates it against the audio it was made from: the lag must be 0 samples and
// every track the same length. report.json keeps a short excerpt of each source track so tools/check-songs.mjs can
// repeat the proof later without the sources.
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { encodeVorbis, decodeVorbisFfmpeg, decodeVorbisLib } from './ogg.mjs';
import { stbAvailable, decodeVorbisStb } from './stbvorbis.mjs';
import { encodeWav } from './wav.mjs';
import { xcorrLag } from './dsp.mjs';

export const TRACKS = ['kick', 'clap', 'bass', 'other'];
const TRACKS4 = TRACKS;
const EXCERPT = 16384;

/**
 * What ships: the kick, clap and bass mono (they are centred in a house mix, and their job in the game is to drop
 * out on a miss), the other stem stereo. About 5 MB for 3 minutes.
 * @type {Record<string, import('./ogg.mjs').VorbisSettings>}
 */
export const ENCODE = {
  kick: { channels: 1, kbps: 48 },
  clap: { channels: 1, kbps: 56 },
  bass: { channels: 1, kbps: 56 },
  other: { channels: 2, kbps: 96 },
  vocals: { channels: 2, kbps: 96 }, /* written only for a package made with --vocals */
};

/**
 * Where a track's excerpt starts: the sharpest attack (largest first difference energy over the excerpt), so the
 * correlation peak is narrow.
 * @param {Float32Array} x
 */
function excerptStart(x) {
  const win = EXCERPT, hop = 1024;
  let best = -1, at = 0;
  for (let s = 0; s + win < x.length; s += hop) {
    let e = 0;
    for (let i = s + 1; i < s + win; i++) { const d = x[i] - x[i - 1]; e += d * d; }
    if (e > best) { best = e; at = s; }
  }
  return at;
}

/** int16 little-endian base64 of a float slice */
function packExcerpt(x, start) {
  const a = new Int16Array(EXCERPT);
  let peak = 1e-9;
  for (let i = 0; i < EXCERPT; i++) peak = Math.max(peak, Math.abs(x[start + i] ?? 0));
  for (let i = 0; i < EXCERPT; i++) a[i] = Math.round(((x[start + i] ?? 0) / peak) * 32767);
  return Buffer.from(a.buffer).toString('base64');
}

/** @param {string} b64 */
export function unpackExcerpt(b64) {
  const buf = Buffer.from(b64, 'base64');
  const a = new Int16Array(buf.buffer, buf.byteOffset, buf.length / 2);
  return Float32Array.from(a, (v) => v / 32767);
}

/**
 * Lag of a decoded track against an excerpt of its source, in samples (0 = aligned).
 * @param {Float32Array} decoded
 * @param {Float32Array} excerpt
 * @param {number} start where the excerpt was taken
 * @param {number} [maxLag]
 */
export function excerptLag(decoded, excerpt, start, maxLag = 2048) {
  // correlate the excerpt against the decoded region around the same place
  const lo = Math.max(0, start - maxLag);
  const region = decoded.subarray(lo, Math.min(decoded.length, start + EXCERPT + maxLag));
  const padded = new Float32Array(region.length);
  padded.set(excerpt.subarray(0, Math.min(excerpt.length, region.length - (start - lo))), start - lo);
  const r = xcorrLag(padded, region, maxLag);
  return { lag: r.lag, lagFine: +r.lagFine.toFixed(3), peak: +r.peak.toFixed(4) };
}

/**
 * Write the package and prove its alignment. Throws when a track is not sample-aligned or lengths differ.
 * @param {string} dir
 * @param {object} p
 * @param {string} p.id
 * @param {string} p.title
 * @param {string} p.artist
 * @param {number} p.rate
 * @param {Record<string, Float32Array[]>} p.tracks kick, clap, bass, other: planar stereo, all the same length
 * @param {number} p.bpm
 * @param {number[]} p.beats
 * @param {number} p.downbeat
 * @param {{t: number, kind: string}[]} p.sections
 * @param {Record<string, {t: number, step: number, lane: number}[]>} p.charts
 * @param {object} [p.report] extra report fields
 * @param {Record<string, import('./ogg.mjs').VorbisSettings>} [p.encode] per track; default ENCODE
 */
export async function writePackage(dir, p) {
  mkdirSync(dir, { recursive: true });
  const n = p.tracks.kick[0].length;
  // the four tracks, and the vocals when the package keeps them (make-song.mjs --vocals)
  const TRACKS = [...TRACKS4, ...(p.tracks.vocals ? ['vocals'] : [])];
  for (const t of TRACKS) if (p.tracks[t][0].length !== n) throw new Error(`track ${t} is ${p.tracks[t][0].length} samples, kick ${n}`);
  const encode = p.encode ?? ENCODE;
  const ogg = await encodeVorbis(Object.fromEntries(TRACKS.map((t) => [t, p.tracks[t]])), p.rate, encode);
  const files = {};
  for (const t of TRACKS) { files[t] = `${t}.ogg`; writeFileSync(join(dir, files[t]), ogg[t]); }

  // the proof: decode back two ways, same length everywhere, 0 lag against the source over the whole song
  const alignment = {};
  for (const t of TRACKS) {
    const src = monoOf(p.tracks[t]);
    const res = {};
    const decoders = [['ffmpeg', decodeVorbisFfmpeg], ['libvorbis', decodeVorbisLib]];
    if (stbAvailable()) decoders.push(['stb_vorbis (the engine\'s)', decodeVorbisStb]);
    for (const [name, dec] of decoders) {
      const d = await dec(ogg[t]);
      const m = monoOf(d.ch);
      const whole = xcorrLag(src, m, 2048, { len: Math.min(src.length, p.rate * 60) });
      res[name] = { rate: d.rate, samples: m.length, lag: whole.lag, lagFine: +whole.lagFine.toFixed(3), peak: +whole.peak.toFixed(4) };
      if (d.rate !== p.rate) throw new Error(`${t}.ogg decodes at ${d.rate} Hz, expected ${p.rate}`);
      if (m.length !== n) throw new Error(`${t}.ogg decodes to ${m.length} samples (${name}), expected ${n}`);
      if (whole.lag !== 0 || Math.abs(whole.lagFine) > 0.5) throw new Error(`${t}.ogg is ${whole.lagFine} samples off its source (${name})`);
    }
    const start = excerptStart(src);
    alignment[t] = { ...res, bytes: ogg[t].length, excerpt: { start, data: packExcerpt(src, start) } };
  }

  return writeSongFiles(dir, p, files, { encode: { codec: 'vorbis', tracks: encode }, alignment });
}

/**
 * Re-chart a package: write song.json, report.json and preview.wav from new analysis and charts, keeping the
 * package's tracks as they are (their bytes are not touched) and the alignment proof and encoding facts from its
 * report.json. The tracks must be the very audio analysed: the same length in samples as the package says.
 * @param {string} dir
 * @param {Parameters<typeof writePackage>[1]} p
 */
export function writeCharts(dir, p) {
  const old = readSongJson(dir);
  const oldReport = JSON.parse(readFileSync(join(dir, 'report.json'), 'utf8'));
  const n = p.tracks.kick[0].length;
  if (old.length_samples !== n) throw new Error(`${dir}: the package's tracks are ${old.length_samples} samples, the source ${n}: not the same audio, make the package again`);
  if (old.sample_rate !== p.rate) throw new Error(`${dir}: the package is at ${old.sample_rate} Hz, the source ${p.rate}`);
  return writeSongFiles(dir, p, old.tracks, { encode: oldReport.encode, alignment: oldReport.alignment });
}

/**
 * song.json, preview.wav and report.json.
 * @param {string} dir
 * @param {Parameters<typeof writePackage>[1]} p
 * @param {Record<string, string>} files the track files
 * @param {{encode: object, alignment: object}} proof
 */
function writeSongFiles(dir, p, files, proof) {
  const n = p.tracks.kick[0].length;
  const song = {
    format: 1, id: p.id, title: p.title, artist: p.artist ?? '', bpm: +p.bpm.toFixed(3), sample_rate: p.rate,
    length_s: +(n / p.rate).toFixed(4), length_samples: n, tracks: files,
    beats: p.beats.map((b) => +b.toFixed(4)), downbeat: p.downbeat, sections: p.sections, ...(p.look ? { look: p.look } : {}), charts: p.charts,
    ...(p.pairs ? { pairs: p.pairs } : {}),
  };
  writeFileSync(join(dir, 'song.json'), formatSong(song));
  writePreview(join(dir, 'preview.wav'), p.rate, p.tracks, p.charts.hard);
  const report = { ...(p.report ?? {}), ...proof };
  writeFileSync(join(dir, 'report.json'), `${JSON.stringify(report, null, 1)}\n`);
  return { song, report };
}

/**
 * song.json with one key a line and one chart a line: readable in a diff, still small.
 * @param {Record<string, any>} song
 */
export function formatSong(song) {
  const lines = Object.entries(song).map(([k, v]) => {
    if (k === 'pairs') { /* one pair a key, one chart a line */
      const pairs = Object.entries(v).map(([pn, lv]) => `    ${JSON.stringify(pn)}: {\n${Object.entries(lv).map(([l, notes]) => `      ${JSON.stringify(l)}: ${JSON.stringify(notes)}`).join(',\n')}\n    }`);
      return `  "pairs": {\n${pairs.join(',\n')}\n  }`;
    }
    if (k !== 'charts') return `  ${JSON.stringify(k)}: ${JSON.stringify(v)}`;
    const levels = Object.entries(v).map(([l, notes]) => `    ${JSON.stringify(l)}: ${JSON.stringify(notes)}`);
    return `  "charts": {\n${levels.join(',\n')}\n  }`;
  });
  return `{\n${lines.join(',\n')}\n}\n`;
}

/** @param {Float32Array[]} ch */
function monoOf(ch) {
  if (ch.length === 1) return ch[0];
  const out = new Float32Array(ch[0].length);
  for (let i = 0; i < out.length; i++) out[i] = 0.5 * (ch[0][i] + ch[1][i]);
  return out;
}

/**
 * The preview: the four tracks summed, with a short click on every note of a chart (a different pitch per lane:
 * kick low, clap high, bass middle), for checking the chart by ear.
 * @param {string} path
 * @param {number} rate
 * @param {Record<string, Float32Array[]>} tracks
 * @param {{t: number, lane: number}[]} notes
 */
export function writePreview(path, rate, tracks, notes) {
  const n = tracks.kick[0].length;
  const ch = [new Float32Array(n), new Float32Array(n)];
  for (const t of TRACKS) for (let c = 0; c < 2; c++) for (let i = 0; i < n; i++) ch[c][i] += 0.8 * tracks[t][c][i];
  const freq = [880, 2640, 1320];
  for (const note of notes) {
    const i0 = Math.round(note.t * rate), len = Math.round(0.03 * rate);
    for (let i = 0; i < len && i0 + i < n; i++) {
      const v = 0.25 * Math.sin((2 * Math.PI * freq[note.lane] * i) / rate) * Math.exp(-i / (0.006 * rate));
      ch[0][i0 + i] += v; ch[1][i0 + i] += v;
    }
  }
  writeFileSync(path, encodeWav({ rate, ch }));
}

/** @param {string} dir */
export function readSongJson(dir) {
  return JSON.parse(readFileSync(join(dir, 'song.json'), 'utf8'));
}
