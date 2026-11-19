// The game's track format: Ogg Vorbis (the engine streams it with stb_vorbis). Encoded by libvorbis inside
// ffmpeg-wasm; decoded back two independent ways for the alignment proof: ffmpeg's own Vorbis decoder and
// libvorbis (@wasm-audio-decoders/ogg-vorbis). Opus was the first choice (smaller) but libopus in the ffmpeg-wasm
// build crashes on any non-silent input, and ffmpeg's own Opus encoder is experimental; see docs/TOOLS.md.
import { OggVorbisDecoder } from '@wasm-audio-decoders/ogg-vorbis';
import { ffmpeg } from './ffmpeg.mjs';
import { encodeWav, decodeWav } from './wav.mjs';

/**
 * @typedef {object} VorbisSettings
 * @property {number} [channels] 1 = mono (downmixed here, before encoding), 2 = stereo
 * @property {number} [kbps] average bit rate (libvorbis managed ABR); when absent, `quality` is used
 * @property {number} [quality] libvorbis VBR quality (-1..10)
 */

/**
 * Encode several tracks to Ogg Vorbis in one ffmpeg run.
 * @param {Record<string, Float32Array[]>} tracks name -> planar channels
 * @param {number} rate
 * @param {Record<string, VorbisSettings>} [settings] per track; default stereo quality 4
 * @returns {Promise<Record<string, Uint8Array>>}
 */
export async function encodeVorbis(tracks, rate, settings = {}) {
  const names = Object.keys(tracks), inputs = {}, args = [];
  for (const n of names) {
    let ch = tracks[n];
    if ((settings[n]?.channels ?? 2) === 1 && ch.length > 1) {
      const m = new Float32Array(ch[0].length);
      for (let i = 0; i < m.length; i++) m[i] = 0.5 * (ch[0][i] + ch[1][i]);
      ch = [m];
    }
    inputs[`${n}.wav`] = encodeWav({ rate, ch });
    args.push('-i', `${n}.wav`);
  }
  // -fflags +bitexact: no encoder-version tag, so the same audio gives the same bytes on every run
  names.forEach((n, i) => {
    const s = settings[n] ?? {};
    const rc = s.kbps ? ['-b:a', `${s.kbps}k`] : ['-q:a', String(s.quality ?? 4)];
    args.push('-map', `${i}:a`, '-c:a', 'libvorbis', ...rc, '-fflags', '+bitexact', '-flags:a', '+bitexact', `${n}.ogg`);
  });
  const out = await ffmpeg(args, inputs, names.map((n) => `${n}.ogg`));
  return Object.fromEntries(names.map((n) => [n, out[`${n}.ogg`]]));
}

/**
 * Decode Ogg Vorbis with ffmpeg's decoder.
 * @param {Uint8Array} bytes
 * @returns {Promise<{rate: number, ch: Float32Array[]}>}
 */
export async function decodeVorbisFfmpeg(bytes) {
  const out = await ffmpeg(['-i', 'in.ogg', '-c:a', 'pcm_f32le', '-f', 'wav', 'out.wav'], { 'in.ogg': bytes }, ['out.wav']);
  return decodeWav(out['out.wav']);
}

/**
 * Decode Ogg Vorbis with libvorbis (a second, independent decoder).
 * @param {Uint8Array} bytes
 * @returns {Promise<{rate: number, ch: Float32Array[]}>}
 */
export async function decodeVorbisLib(bytes) {
  const dec = new OggVorbisDecoder();
  await dec.ready;
  const r = await dec.decodeFile(bytes);
  dec.free();
  return { rate: r.sampleRate, ch: r.channelData };
}
