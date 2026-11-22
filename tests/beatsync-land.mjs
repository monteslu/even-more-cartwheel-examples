// beatsync's land, through romdev on GL and WebGPU: the landscape beside the rivers is the other track, read from the
// song's look (song.json "look.other", tools/lib/look.mjs), and the picture shows it.
//
// beatsync_auto plays Late Night on Hard; at moments through the whole song the `land` request returns every row the
// frame drew (its beat, height, hue, bass/mid/treble, strongest stab, crest; src/club.c) and a screenshot is taken.
//   - the rows are the song's analysis: each row's bands are song.json's for its half beat, its stab the strongest
//     song.json stab in its half beat, its crest a harmony change there, its height 0 exactly where the track is
//     silent and rising with the track's loudness (rank correlation over the rows at least 0.8);
//   - the land changes with the music: the rows' loudness (height times bands) and hue vary across the song;
//   - the picture follows the land: the lit share of the land's part of the frame (beside the rivers, clear of the
//     HUD) rises with the rows' loudness (correlation over the moments at least 0.5).
// The CONTROLS must fail: the same moments with `land off` (no land: the picture cannot follow it) and `land flat`
// (one height and colour: the land no longer changes with the music).
//
//   node tests/beatsync-land.mjs [--gpu gl|webgpu|both]
import { mkdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { GAME, gpusOf, openBeatsync } from './lib/beatsync.mjs';

const { readImage } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const argv = process.argv.slice(2);
const GPUS = gpusOf(argv);
const OUT = join(ROOT, 'out/gate/beatsync-land');
mkdirSync(OUT, { recursive: true });
const SONG = 'late-night';
const look = JSON.parse(readFileSync(join(GAME, `songs/${SONG}/song.json`), 'utf8')).look.other;
// frames after the song command: 4 s of count-in, then every 11.7 s of the 210 s song (never a whole number of beats)
const MOMENTS = Array.from({ length: 17 }, (_, i) => 600 + 700 * i);

/** the land's part of a frame: beside the rivers, clear of the HUD (the song box top left, the score top right, the
 * crowd meter at the right edge): the share of pixels lit (luma over 40) */
function landLit(path) {
  const img = readImage(path);
  const box = [[60, 260, 560, 1000], [1360, 260, 1780, 1000]];
  let lit = 0, n = 0;
  for (const [x0, y0, x1, y1] of box)
    for (let y = y0; y < y1; y++)
      for (let x = x0; x < x1; x++) {
        const i = (y * img.w + x) * 4;
        if (0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2] > 40) lit++;
        n++;
      }
  return lit / n;
}

async function sample(gpu, mode) {
  const r = await openBeatsync('beatsync_auto', { gpu, tag: `land-${mode}` });
  try {
    await r.command('style neon'); // the land as the neon characters draw it (src/styles.c; some styles hide it for a backdrop)
    if (mode !== 'game') await r.command(`land ${mode}`);
    await r.command(`song ${SONG} hard`);
    const base = r.now(), out = [];
    for (const [k, m] of MOMENTS.entries()) {
      await r.stepTo(base + m);
      const path = join(OUT, `${gpu}-${mode}-${k}.png`);
      await r.shotTo(path);
      const land = await r.command('land');
      out.push({ rows: land.rows, lit: landLit(path) });
    }
    return out;
  } finally {
    await r.close();
  }
}

const mean = (a) => a.reduce((x, y) => x + y, 0) / (a.length || 1);
function pearson(a, b) {
  const ma = mean(a), mb = mean(b);
  let ab = 0, aa = 0, bb = 0;
  for (let i = 0; i < a.length; i++) { ab += (a[i] - ma) * (b[i] - mb); aa += (a[i] - ma) ** 2; bb += (b[i] - mb) ** 2; }
  return aa > 0 && bb > 0 ? ab / Math.sqrt(aa * bb) : 0;
}
const ranks = (a) => { const o = a.map((v, i) => [v, i]).sort((x, y) => x[0] - y[0]), r = []; o.forEach(([, i], k) => { r[i] = k; }); return r; };
const loudness = (row) => row.height * mean(row.bands);

/** what song.json says for a row at beat b */
function expected(b) {
  const h = Math.floor(b * 2), bands = h >= 0 && h < look.bands.length / 3 ? look.bands.slice(h * 3, h * 3 + 3).map((v) => v / 50) : null;
  let stab = 0;
  for (let i = 0; i < look.stabs.length; i += 2) { const sb = look.stabs[i] / 100; if (sb >= b && sb < b + 0.5) stab = Math.max(stab, look.stabs[i + 1]); }
  const crest = look.changes.some((c) => c >= Math.ceil(b) && c < b + 0.5);
  const i = Math.floor(b), level = i >= 0 && i < look.level.length ? look.level[i] : 0;
  return { bands, stab, crest, level };
}

function judge(moments) {
  let rows = 0, badBands = 0, badStab = 0, badCrest = 0, badSilent = 0, worst = '';
  const lv = [], hts = [];
  for (const m of moments)
    for (const row of m.rows) {
      if (row.beat < 0 || row.beat >= look.level.length) continue;
      rows++;
      const e = expected(row.beat);
      if (e.bands && row.bands.some((v, k) => Math.abs(v - e.bands[k]) > 0.011)) { badBands++; worst ||= `beat ${row.beat}: bands ${row.bands} against ${e.bands}`; }
      if (row.stab !== e.stab) { badStab++; worst ||= `beat ${row.beat}: stab ${row.stab} against ${e.stab}`; }
      if (!!row.crest !== e.crest) { badCrest++; worst ||= `beat ${row.beat}: crest ${row.crest} against ${e.crest}`; }
      if ((row.height === 0) !== (e.level === 0)) { badSilent++; worst ||= `beat ${row.beat}: height ${row.height} at level ${e.level}`; }
      lv.push(e.level), hts.push(row.height);
    }
  const rank = pearson(ranks(lv), ranks(hts));
  const loud = moments.map((m) => mean(m.rows.map(loudness)));
  const hues = moments.flatMap((m) => m.rows.map((r) => r.hue));
  return { rows, badBands, badStab, badCrest, badSilent, worst, rank, loudSpread: Math.max(...loud) - Math.min(...loud), hueSpread: Math.max(...hues) - Math.min(...hues), follow: pearson(loud, moments.map((m) => m.lit)), lit: mean(moments.map((m) => m.lit)) };
}

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const changes = (g) => g.loudSpread >= 0.15 && g.hueSpread >= 30;
const follows = (g) => g.follow >= 0.5 && g.lit >= 0.02;
for (const gpu of GPUS) {
  const g = judge(await sample(gpu, 'game'));
  check(g.rows >= 150 && !g.badBands && !g.badStab && !g.badCrest && !g.badSilent, `land (${gpu}): ${g.rows} rows are the song's analysis (bands ${g.badBands}, stabs ${g.badStab}, crests ${g.badCrest}, silence ${g.badSilent} wrong${g.worst ? `: ${g.worst}` : ''})`);
  check(g.rank >= 0.8, `land (${gpu}): a row's height rises with the track's loudness (rank correlation ${g.rank.toFixed(2)}, at least 0.8)`);
  check(changes(g), `land (${gpu}): the land changes with the music (loudness over the moments spans ${g.loudSpread.toFixed(2)}, at least 0.15; hue ${g.hueSpread.toFixed(0)} degrees, at least 30)`);
  check(follows(g), `land (${gpu}): the picture follows the land (lit share ${(g.lit * 100).toFixed(1)}% against the rows' loudness: correlation ${g.follow.toFixed(2)}, at least 0.5)`);
  const off = judge(await sample(gpu, 'off'));
  check(!follows(off), `land control (${gpu}, land off): lit share ${(off.lit * 100).toFixed(1)}%, correlation ${off.follow.toFixed(2)}: ${follows(off) ? 'PASSES: the picture check cannot see the land' : 'fails, as it must'}`);
  const flat = judge(await sample(gpu, 'flat'));
  check(!changes(flat), `land control (${gpu}, land flat): loudness spans ${flat.loudSpread.toFixed(2)}, hue ${flat.hueSpread.toFixed(0)} degrees: ${changes(flat) ? 'PASSES: the check cannot see a land that ignores the music' : 'fails, as it must'}`);
}
console.log(failed ? `beatsync-land: ${failed} failed` : 'beatsync-land: ok');
process.exit(failed ? 1 : 0);
