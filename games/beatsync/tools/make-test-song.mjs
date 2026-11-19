#!/usr/bin/env node
// Make a synthetic house song with known notes (tools/lib/synth.mjs), as the chart pipeline's ground truth:
//
//   node tools/make-test-song.mjs [--out out/test-songs/plain] [--bpm 124] [--length 90] [--drift 0.003]
//                                 [--bleed 0.3] [--seed 7] [--title ..]
//   node tools/make-test-song.mjs --variants [--length 90]     plain, drift (+0.3%) and bleed into out/test-songs/
//
// Writes <out>/stems/{master,drums,bass,other,vocals}.wav, <out>/song.stem.mp4 (NI Stems: AAC in MP4, the layout
// Loukai writes) and <out>/truth.json (beats, sections and every kick, clap and bass note with its time and step).
import { mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { makeSong } from './lib/synth.mjs';
import { writeStemsDir, writeStemMp4 } from './lib/stems.mjs';
import { crossover } from './lib/dsp.mjs';
import { makeCharts } from './lib/chart.mjs';
import { writePackage } from './lib/package.mjs';

export const VARIANTS = {
  plain: {},
  drift: { drift: 0.003 },
  bleed: { bleed: 0.3 },
};

/**
 * Make one test song and write it out.
 * @param {string} out directory
 * @param {import('./lib/synth.mjs').SongOptions & {title?: string}} o
 */
export async function writeTestSong(out, o) {
  const song = makeSong(o);
  mkdirSync(out, { recursive: true });
  const stems = { rate: song.rate, tracks: song.stems };
  writeStemsDir(join(out, 'stems'), stems);
  await writeStemMp4(join(out, 'song.stem.mp4'), stems, { title: o.title ?? 'Test Groove', tempo: o.bpm ?? 124 });
  writeFileSync(join(out, 'truth.json'), JSON.stringify(song.truth, null, 1));
  return song;
}

/**
 * A package straight from the ground truth (no analysis): the charts are made from the true notes. Only for a first
 * playable song; tools/make-song.mjs makes the real ones from the audio.
 * @param {string} dir
 * @param {ReturnType<typeof makeSong>} song
 */
export async function packageFromTruth(dir, song, id = 'test-groove', title = 'Test Groove') {
  const drums = song.stems.drums.map((c) => crossover(c, song.rate));
  const tracks = { kick: drums.map((d) => d.low), clap: drums.map((d) => d.high), bass: song.stems.bass, other: song.stems.other };
  const t = song.truth;
  const cands = t.notes.map((n) => ({ step: n.step, lane: n.lane, strength: n.kind === 'legato' ? 0.6 : 1 }));
  const play = t.beats[t.beats.length - 1] - t.beats[0];
  const { charts, stats } = makeCharts(cands, t.beats, t.downbeat, play);
  return writePackage(dir, { id, title, artist: '', rate: song.rate, tracks, bpm: t.bpm, beats: t.beats, downbeat: t.downbeat,
    sections: t.sections.map((x) => ({ t: +x.t.toFixed(4), kind: x.kind })), charts, report: { source: 'ground truth', charts: stats } });
}

async function main() {
  const { values: a } = parseArgs({
    options: {
      out: { type: 'string' }, bpm: { type: 'string' }, length: { type: 'string' }, drift: { type: 'string' },
      bleed: { type: 'string' }, seed: { type: 'string' }, title: { type: 'string' }, variants: { type: 'boolean' },
      'truth-package': { type: 'string' },
    },
  });
  const base = {
    bpm: a.bpm ? Number(a.bpm) : undefined, length: a.length ? Number(a.length) : undefined,
    seed: a.seed ? Number(a.seed) : undefined, title: a.title,
  };
  if (a.variants) {
    const root = resolve(a.out ?? 'out/test-songs');
    for (const [name, v] of Object.entries(VARIANTS)) {
      const s = await writeTestSong(join(root, name), { ...base, ...v });
      console.log(`${name}: ${s.truth.notes.length} notes, ${s.truth.length_s.toFixed(1)} s -> ${join(root, name)}`);
    }
    return;
  }
  const out = resolve(a.out ?? 'out/test-songs/plain');
  const s = await writeTestSong(out, {
    ...base, drift: a.drift ? Number(a.drift) : undefined, bleed: a.bleed ? Number(a.bleed) : undefined,
  });
  console.log(`${s.truth.notes.length} notes, ${s.truth.length_s.toFixed(1)} s -> ${out}`);
  if (a['truth-package']) {
    const r = await packageFromTruth(resolve(a['truth-package']), s);
    console.log('package', a['truth-package'], r.report.charts);
  }
}

if (import.meta.url === `file://${process.argv[1]}`) await main();
