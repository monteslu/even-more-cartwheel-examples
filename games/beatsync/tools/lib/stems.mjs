// Stems in and out: a directory of WAVs, or an NI Stems file (.stem.mp4) as Loukai writes it: one MP4 with five
// AAC tracks, track 0 the master mix, then the stems named in the NI `stem` atom (moov/udta/stem: drums, bass,
// other, vocals). The container is read and written by stem-mp4 (MIT); the AAC is encoded and decoded by ffmpeg
// compiled to WebAssembly.
import { existsSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { basename, join } from 'node:path';
import { StemMp4Writer, StemMp4Reader, Atoms } from 'stem-mp4';
import * as Extractor from 'stem-mp4/extractor';
import { ffmpeg } from './ffmpeg.mjs';
import { encodeWav, decodeWav } from './wav.mjs';

export const STEM_NAMES = ['drums', 'bass', 'other', 'vocals'];

/**
 * @typedef {object} Stems
 * @property {number} rate
 * @property {Record<string, Float32Array[]>} tracks master, drums, bass, other, vocals (planar channels)
 * @property {object} [info] where they came from
 */

/**
 * Write the stems as WAVs: master.wav, drums.wav, bass.wav, other.wav, vocals.wav.
 * @param {string} dir
 * @param {Stems} s
 */
export function writeStemsDir(dir, s) {
  mkdirSync(dir, { recursive: true });
  for (const [name, ch] of Object.entries(s.tracks)) writeFileSync(join(dir, `${name}.wav`), encodeWav({ rate: s.rate, ch }));
}

/**
 * Write the stems as an NI Stems .stem.mp4, the way Loukai does: each track encoded to AAC-LC in its own MP4 by
 * ffmpeg's native encoder, then muxed by stem-mp4's writer (master as the mixdown, the four stems in NI order).
 * @param {string} path
 * @param {Stems} s
 * @param {{title?: string, artist?: string, tempo?: number, bitrate?: number}} [meta]
 */
export async function writeStemMp4(path, s, meta = {}) {
  const names = ['master', ...STEM_NAMES];
  const inputs = {}, args = [];
  for (const name of names) {
    inputs[`${name}.wav`] = encodeWav({ rate: s.rate, ch: s.tracks[name] });
    args.push('-i', `${name}.wav`);
  }
  names.forEach((name, i) => args.push('-map', `${i}:a`, '-c:a', 'aac', '-b:a', String(meta.bitrate ?? 192000), `${name}.m4a`));
  const out = await ffmpeg(args, inputs, names.map((n) => `${n}.m4a`));
  const r = await StemMp4Writer.write({
    outputPath: path,
    mixdownAac: out['master.m4a'],
    stemsAac: Object.fromEntries(STEM_NAMES.map((n) => [n, out[`${n}.m4a`]])),
    metadata: { title: meta.title ?? 'Untitled', artist: meta.artist ?? '', tempo: meta.tempo },
    sampleRate: s.rate,
  });
  if (!r.success) throw new Error(`stem-mp4 could not write ${path}`);
}

/**
 * Decode a single-track MP4/M4A (or anything ffmpeg reads) to planar float stereo.
 * @param {Uint8Array} bytes
 * @returns {Promise<{rate: number, ch: Float32Array[]}>}
 */
export async function decodeAudio(bytes, name = 'in.m4a') {
  const out = await ffmpeg(['-i', name, '-c:a', 'pcm_f32le', '-ac', '2', '-f', 'wav', 'out.wav'], { [name]: bytes }, ['out.wav']);
  return decodeWav(out['out.wav']);
}

/**
 * Read a song's stems from an NI Stems file or a directory of WAVs (drums.wav, bass.wav, other.wav required;
 * master.wav optional). All tracks come back the same rate and length (the shortest wins).
 * @param {string} input
 * @returns {Promise<Stems>}
 */
export async function readStems(input) {
  /** @type {Record<string, {rate: number, ch: Float32Array[]}>} */
  const got = {};
  let info;
  if (existsSync(join(input, 'drums.wav'))) {
    for (const name of ['master', ...STEM_NAMES]) {
      const p = join(input, `${name}.wav`);
      if (existsSync(p)) got[name] = decodeWav(readFileSync(p));
    }
    info = { kind: 'stems-dir', path: basename(input) }; // the name only: a report never carries a machine's paths
  } else {
    const data = readFileSync(input);
    const count = Extractor.getTrackCount(data);
    // stem names from the NI atom; track 0 is the master
    let names = STEM_NAMES;
    try {
      const ni = await Atoms.readNiStemsMetadata(input);
      if (ni?.stems?.length) names = ni.stems.map((x) => String(x.name).toLowerCase());
    } catch { /* no NI atom: assume the NI order */ }
    const order = ['master', ...names];
    if (count < 4) throw new Error(`${input}: ${count} audio tracks, an NI Stems file has 5 (master + 4 stems)`);
    for (let i = 0; i < Math.min(count, order.length); i++) got[order[i]] = await decodeAudio(Extractor.extractTrack(data, i));
    let meta = {};
    try { meta = (await StemMp4Reader.load(input)).metadata ?? {}; } catch { /* titles are optional */ }
    info = { kind: 'stem.mp4', path: basename(input), tracks: order.slice(0, count), title: meta.title || undefined, artist: meta.artist || undefined };
  }
  for (const need of ['drums', 'bass', 'other']) if (!got[need]) throw new Error(`${input}: no ${need} stem`);
  const rate = got.drums.rate;
  for (const [k, v] of Object.entries(got)) if (v.rate !== rate) throw new Error(`${input}: ${k} is ${v.rate} Hz, drums ${rate} Hz`);
  const n = Math.min(...Object.values(got).map((v) => v.ch[0].length));
  const tracks = {};
  for (const [k, v] of Object.entries(got)) {
    const ch = v.ch.length === 1 ? [v.ch[0], v.ch[0]] : v.ch.slice(0, 2);
    tracks[k] = ch.map((c) => c.subarray(0, n));
  }
  return { rate, tracks, info };
}
