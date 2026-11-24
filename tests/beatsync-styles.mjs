// beatsync's visual styles (games/beatsync/src/styles.c): every style shown, every fade free of flashes, the notes
// readable in every style. On GL and WebGPU (--gpu gl|webgpu|both, default both), the autopilot cart on its fixed clock.
//
// 1. The survey: each style held (`style <id>`), then a still from the play camera and three frames of motion a quarter
//    of a second apart, saved to out/beatsync-styles/<gpu>/ for review (a person looks at them: a style must read as good
//    art, not just pass). Each still must show the style: the backdrop a style asks for drawn (its weight 1 in
//    style.backdrop), the characters on or off as the style says (ascii.mix), and the picture not one flat colour.
// 2. Readability, per style: every pending note near enough to read (at least NOTE_MIN_PX wide on a 1920 x 1080 frame,
//    from the `notes` request: lane, x, y, width; read before it reaches the gate, where the hit's burst and the
//    judgement are) against what is round it on its river, as WCAG measures text: the note's colour (the brightest tenth
//    of its core: in the characters a note is glyph strokes on black) against the river's typical colour just above and
//    below it (the median), as a WCAG contrast ratio
//    ((L1 + 0.05) / (L2 + 0.05)). A style passes when the median note reaches MIN_CONTRAST (4:1, over WCAG's 3:1 bound
//    for graphics that must be seen). Each style has its own CONTROL, which must fail: the same style with its roads and
//    pockets off and the notes dimmed (`style readctl 1`); and the fire with the same (the brightest backdrop).
// 3. Flashes: romdev's frame({op:'flash'}) over a run through every fade (the styles in turn, each change the game's own
//    fade, a drop's fast one included), WCAG 2.3.1 and Harding: at most 3 flashes a second, regular flicker under
//    0.02, and the hardest two-frame rise of a sixth of the screen under MAX_RISE; the game's own flash meter for the
//    fades (fl.style) at most 3 a second. The CONTROL must fail: the same run with every change a one-frame cut
//    (`style cut 1`).
//
// 3b. Each style's own fades (--only flash1): faded in from the neon characters and back out by the game's fade, held to
//    the same rules; its CONTROL the same two changes as one-frame cuts (`style cut 1`), which must fail.
//
// 2b. The one-bit style's HUD (src/main.c, cartwheel-engine 0.67.0's one-bit edges and two-colour prompts): on that
//    style's play frames every pixel inside the HUD's boxes (the `hud` request) is the print's ink or its paper (within
//    HUD_TOL of one, sRGB), at least HUD_TWO of them over HUD_FRAMES frames of play, and the ink against the paper reads
//    at least 7:1 (WCAG); the three judgements in two colours differ in shape (their binarized crops apart by more than
//    JUDGE_APART of the pixels: Perfect in an ink frame, Good plain, Miss struck through). Its CONTROL must fail: the same
//    frames with the coloured HUD over the print (`hud onebit 0`).
//
// 4. The HUD: no two of its lines of text drawn over each other (hud.overlap_max, the most a frame, over the whole
//    survey: the middle gate's judgement and the multiplier or the streak called out under it overlapped before). The
//    CONTROL must fail: the same play with `hud old 1` (the old placement) for 1200 frames.
//
//   node tests/beatsync-styles.mjs [--gpu gl|webgpu|both] [--only survey|read|flash|flash1] [--style <id>]
import { mkdirSync, rmSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { gpusOf, openBeatsync } from './lib/beatsync.mjs';
import { readPng } from './lib/png.mjs';

const argv = process.argv.slice(2);
const GPUS = gpusOf(argv);
const ONLY = argv.includes('--only') ? argv[argv.indexOf('--only') + 1] : null;
const ONE = argv.includes('--style') ? argv[argv.indexOf('--style') + 1] : null;
const OUT = join(ROOT, 'out/beatsync-styles');
const START = 900;        // frames into the autopilot's first song: in play, notes on every river
const HOLD = 150;         // frames a style is held before its still (its fade is at most 2 beats, about a second)
const NOTE_MIN_PX = 40;   // a note this wide or wider is read (the far ones are smaller than a character)
const READ_FRAMES = 8;    // frames each style's notes are read on (a quarter of a second apart)
const MIN_CONTRAST = 4.0; // over WCAG 1.4.11's 3:1 for graphics a person must see: a showcase reads comfortably
const MAX_RISE = 0.2;     // the hardest two-frame rise of a sixth of the screen a fade may make (a hit's burst makes about
                          // 0.1; a cut between two styles makes 0.5)
const MAX_FLASHES = 3;
const HUD_TOL = 40, HUD_TWO = 0.995, HUD_FRAMES = 8, JUDGE_APART = 0.08;
const INK = [255, 237, 204], PAPER = [3, 3, 4]; // src/main.c OB_INK, OB_PAPER as bytes
// a style's readability held for a known engine issue (reported, its fix in the release named): measured and printed,
// not failed. Empty it when the pin moves past the fix.
const HELD_READ = {}; // (the arcade maze's checkerboard was fixed in cartwheel-engine 0.71.1: 7.88:1 again on GL and WebGPU)
const SONG_IN = 420;      // frames from a song's start into its notes (8 beats of lead-in, then a few bars)

let failed = 0;
const ok = (s) => console.log(`ok   ${s}`);
const fail = (s) => { console.log(`FAIL ${s}`); failed++; };

/** sRGB byte to linear, and a pixel's relative luminance */
const lin = (c) => { const v = c / 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; };
const LUT = Float64Array.from({ length: 256 }, (_, i) => lin(i));
function lumAt(img, x, y) {
  const i = (y * img.w + x) * 3;
  return 0.2126 * LUT[img.rgb[i]] + 0.7152 * LUT[img.rgb[i + 1]] + 0.0722 * LUT[img.rgb[i + 2]];
}
function pct(a, q) { if (!a.length) return NaN; const s = [...a].sort((p, r) => p - r); return s[Math.min(s.length - 1, Math.floor(s.length * q))]; }
function mean(a) { return a.length ? a.reduce((p, q) => p + q, 0) / a.length : NaN; }
function median(a) { if (!a.length) return NaN; const s = [...a].sort((p, q) => p - q); return s[s.length >> 1]; }
function boxLums(img, x0, y0, x1, y1, skip = null) {
  const out = [];
  for (let y = Math.max(0, Math.round(y0)); y < Math.min(img.h, Math.round(y1)); y++)
    for (let x = Math.max(0, Math.round(x0)); x < Math.min(img.w, Math.round(x1)); x++) if (!skip || !skip(x, y)) out.push(lumAt(img, x, y));
  return out;
}
/** the backdrop's most note-like shape: on a frame with the field hidden, every compact bright blob about the size of a
 * coming note the player reads (28 to 160 pixels across at 1080, filling half its box or more, no longer than twice its width), outside
 * the HUD's boxes, scored as WCAG measures the notes: its mean against the median round it. Returns the highest. */
function rgbAt(img, x, y) { const i = (y * img.w + x) * 3; return [img.rgb[i], img.rgb[i + 1], img.rgb[i + 2]]; }
/** hue (degrees) and saturation of an sRGB colour */
function hueSat([r, g, b]) { const mx = Math.max(r, g, b), mn = Math.min(r, g, b), s = mx ? (mx - mn) / mx : 0; let h = 0; if (mx !== mn) { h = mx === r ? (g - b) / (mx - mn) : mx === g ? 2 + (b - r) / (mx - mn) : 4 + (r - g) / (mx - mn); h = (h * 60 + 360) % 360; } return [h, s]; }
/** a colour like one of the notes': both nearly grey, or hues within 35 degrees and both saturated */
function likeNote(c, noteCols) { const [h, s] = hueSat(c); return noteCols.some((n) => { const [hn, sn] = hueSat(n); if (s < 0.25 || sn < 0.25) return s < 0.35 && sn < 0.35; const d = Math.abs(h - hn); return Math.min(d, 360 - d) < 35; }); }
function noteLike(img, noteCols = null, dither = false) {
  const S = 4, w = Math.floor(img.w / S), h = Math.floor(img.h / S), k = img.w / 1920;
  const L = new Float32Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    let s = 0;
    for (let yy = 0; yy < S; yy++) for (let xx = 0; xx < S; xx++) s += lumAt(img, x * S + xx, y * S + yy);
    L[y * w + x] = s / (S * S);
  }
  // (the HUD's boxes, and the strip of the gates where the judgements and the multiplier are drawn even with the field hidden)
  const hud = (x, y) => { const X = x * S / k, Y = y * S / k; return (X < 400 && Y < 190) || (X > 1730 && Y < 145) || (X > 1830 && Y > 300 && Y < 830) || Y > 870; };
  const med = pct([...L].filter((_, i) => i % 7 === 0), 0.5), paper = pct([...L].filter((_, i) => i % 7 === 0), 0.995);
  const thr = Math.max(0.03, med * 4);
  const seen = new Uint8Array(w * h);
  let best = { ratio: 1, at: null };
  for (let i = 0; i < w * h; i++) {
    if (seen[i] || L[i] < thr || hud(i % w, Math.floor(i / w))) continue;
    const stack = [i], px = [];
    seen[i] = 1;
    let x0 = w, x1 = 0, y0 = h, y1 = 0;
    while (stack.length && px.length < 4000) {
      const j = stack.pop(), x = j % w, y = Math.floor(j / w);
      px.push(j); x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y);
      for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const nx = x + dx, ny = y + dy, q = ny * w + nx;
        if (nx >= 0 && ny >= 0 && nx < w && ny < h && !seen[q] && L[q] >= thr) { seen[q] = 1; stack.push(q); }
      }
    }
    const bw = x1 - x0 + 1, bh = y1 - y0 + 1, size = Math.max(bw, bh) * S / k;
    if (size < 28 || size > 160 || px.length / (bw * bh) < 0.5 || Math.max(bw, bh) > 2 * Math.min(bw, bh)) continue;
    const inside = px.reduce((a, j) => a + L[j], 0) / px.length;
    // its colour (the brightest pixel's): a shape in none of the notes' colours is not taken for one
    let bj = px[0]; for (const j of px) if (L[j] > L[bj]) bj = j;
    const col = rgbAt(img, (bj % w) * S + 1, Math.floor(bj / w) * S + 1);
    if (noteCols && noteCols.length && !likeNote(col, noteCols)) continue;
    // in a one-bit picture a shape reads only where it prints solid (a scatter of dots over a patch is a tone, not a shape)
    if (dither && inside < 0.6 * paper) continue;
    // what is round it, side by side: a shape stands out only where it is darker on every side (an edge between dark land
    // and a bright sky is not a shape), so the brightest side's median is the ground it is read against
    const side = [[], [], [], []];
    for (let y = Math.max(0, y0 - bh); y < Math.min(h, y1 + bh + 1); y++) for (let x = Math.max(0, x0 - bw); x < Math.min(w, x1 + bw + 1); x++) {
      if (x >= x0 && x <= x1 && y >= y0 && y <= y1) continue;
      side[x < x0 ? 0 : x > x1 ? 1 : y < y0 ? 2 : 3].push(L[y * w + x]);
    }
    const ground = Math.max(...side.filter((a) => a.length).map((a) => median(a)));
    const ratio = (inside + 0.05) / (ground + 0.05);
    if (ratio > best.ratio) best = { ratio, at: [Math.round((x0 + x1) / 2 * S / k), Math.round((y0 + y1) / 2 * S / k)], size: Math.round(size), dims: [bw * S, bh * S], col };
  }
  return best;
}
const NOTE_LIKE_SHARE = 0.5; // the backdrop's most note-like shape at most half the notes' own contrast

/** the share of the pixels inside the HUD's boxes that are the ink or the paper (each channel within HUD_TOL), and
 * the boxes' pixel count */
function hudTwoColour(img, boxes) {
  const k = img.w / 1920;
  let n = 0, two = 0;
  const near = (i, c) => Math.abs(img.rgb[i] - c[0]) <= HUD_TOL && Math.abs(img.rgb[i + 1] - c[1]) <= HUD_TOL && Math.abs(img.rgb[i + 2] - c[2]) <= HUD_TOL;
  for (const [bx, by, bw, bh] of boxes) {
    // (a box's own rounded corners and its edge line are drawn too: the box shrunk by its corner radius)
    const x0 = Math.ceil((bx + 10) * k), y0 = Math.ceil((by + 10) * k), x1 = Math.floor((bx + bw - 10) * k), y1 = Math.floor((by + bh - 10) * k);
    for (let y = Math.max(0, y0); y < Math.min(img.h, y1); y++) for (let x = Math.max(0, x0); x < Math.min(img.w, x1); x++) {
      const i = (y * img.w + x) * 3;
      n++;
      if (near(i, INK) || near(i, PAPER)) two++;
    }
  }
  return { share: n ? two / n : 0, px: n };
}
/** a crop binarized (lighter than mid grey or not) */
function binCrop(img, x, y, w, h) {
  const k = img.w / 1920, out = [];
  for (let yy = Math.round(y * k); yy < Math.round((y + h) * k); yy++) for (let xx = Math.round(x * k); xx < Math.round((x + w) * k); xx++) out.push(lumAt(img, xx, yy) > 0.2 ? 1 : 0);
  return out;
}

/** each readable note's contrast against its river round it: [{lane, ratio}] */
function noteContrasts(img, notes, dither = false) {
  const k = img.w / 1920, res = [];
  for (const n of notes) {
    // (a note at the gate shares it with the hit's burst and the judgement: read before it gets there)
    if (n.px < NOTE_MIN_PX || n.y > 1000 || n.ahead < 0.3) continue;
    const x = n.x * k, y = n.y * k, w = n.px * k, h = w; // (every note about as tall as wide: the bass a triangle, the synth a square)
    const core = boxLums(img, x - w * 0.18, y - h * 0.12, x + w * 0.18, y + h * 0.12);
    // the river above and below the note: the road it rides (another note there is rare and the median ignores it)
    // (the pixels of the other notes left out: on a run of eighths the next note is right there)
    const others = notes.filter((o) => o !== n).map((o) => [o.x * k, o.y * k, o.px * k * 0.7, o.px * k * 0.7]);
    const skip = (px, py) => others.some(([ox, oy, rx, ry]) => Math.abs(px - ox) < rx && Math.abs(py - oy) < ry);
    const bg = [...boxLums(img, x - w * 0.3, y - h * 1.5, x + w * 0.3, y - h * 0.8, skip), ...boxLums(img, x - w * 0.3, y + h * 0.8, x + w * 0.3, y + h * 1.5, skip)];
    if (core.length < 16 || bg.length < 16) continue;
    // as WCAG measures text: the note's own colour (its brightest tenth: in the characters a note is glyph strokes on
    // black, and the strokes are its colour) against the river's typical colour round it (the median)
    // (a one-bit picture: every pixel is ink or paper, so a note is how much of it is lit: its mean)
    const lc = dither ? mean(core) : pct(core, 0.9), lb = dither ? mean(bg) : median(bg);
    res.push({ lane: n.lane, ratio: (Math.max(lc, lb) + 0.05) / (Math.min(lc, lb) + 0.05), note: lc, back: lb });
  }
  return res;
}

const DITHER_LOOKS = new Set([3, 4, 5]); // src/styles.c PX_BIT, PX_AMBER, PX_NEWS: one-bit pictures
async function styleList(r) {
  const st = await r.command('style');
  return st.styles.map((s, i) => ({ ...s, index: i }));
}

for (const gpu of GPUS) {
  const dir = join(OUT, gpu);
  if (!ONLY || ONLY === 'survey') rmSync(dir, { recursive: true, force: true }); // (the stills stay for --only read, flash and flash1: flash1 picks each style's base from them)
  mkdirSync(dir, { recursive: true });

  // ── 1 and 2: the survey and readability ──
  if (!ONLY || ONLY === 'survey' || ONLY === 'read') {
    const r = await openBeatsync('beatsync_auto', { gpu, tag: 'styles' });
    try {
      await r.stepTo(START);
      // the HUD's control first (the old placement must overlap), then the survey must draw none
      if (!ONLY || ONLY === 'survey') {
        await r.command('hud old 1');
        await r.call('wasm', { op: 'write', name: 'hud.overlap_max', value: 0 });
        await r.step(1200);
        const ovc = (await r.fields())['hud.overlap_max'];
        await r.command('hud old 0');
        if (ovc > 0) ok(`styles hud control (${gpu}): the old placement draws ${ovc} lines over each other, as it must`);
        else fail(`styles hud control (${gpu}): the old placement drew no overlap in 1200 frames: the check cannot see one`);
        await r.call('wasm', { op: 'write', name: 'hud.overlap_max', value: 0 });
      }
      const all = await styleList(r);
      const list = ONE ? all.filter((s) => s.id === ONE) : all;
      for (const s of list) {
        // each style on a fresh start of the song (the survey is longer than one song): into its notes, then the style
        await r.command('song 0 hard');
        await r.step(SONG_IN);
        await r.command(`style ${s.id}`);
        await r.step(HOLD);
        const f = await r.fields();
        const still = join(dir, `${s.index}-${s.id}.png`);
        await r.shotTo(still);
        const img = readPng(still);
        // the notes: read on the still and on READ_FRAMES more frames (the first three kept as the motion frames)
        const reads = [[img, (await r.command('notes')).notes || []]];
        for (let m = 1; m <= READ_FRAMES; m++) {
          await r.step(15);
          const p = join(dir, m <= 3 ? `${s.index}-${s.id}-motion${m}.png` : `read-${m}.png`);
          await r.shotTo(p);
          reads.push([readPng(p), (await r.command('notes')).notes || []]);
          if (m > 3) rmSync(p);
        }
        // the style shows: its backdrop at full weight, the characters on or off, the picture not flat
        const bw = f['style.backdrop'] || [];
        const want = s.bd; // the style's backdrop (src/backdrop.h BD_*, from the style request)
        const mix = f['ascii.mix'];
        let lo = 1, hi = 0;
        for (let y = 0; y < img.h; y += 7) for (let x = 0; x < img.w; x += 7) { const l = lumAt(img, x, y); lo = Math.min(lo, l); hi = Math.max(hi, l); }
        const shows = f['style.cur'] === s.index && f['style.t'] >= 1 && (want <= 0 || bw[want] >= 0.999) && (s.ascii ? mix >= 0.999 : mix <= 0.001) && hi - lo > 0.2;
        if (s.held) console.log(`note styles (${gpu}) ${s.id}: held out of the rotation and the setting (an engine gap), still checked`);
        if (!ONLY || ONLY === 'survey') {
          if (shows) ok(`styles survey (${gpu}) ${s.id}: shown (backdrop ${s.backdrop}, characters ${mix}), ${still}`);
          else fail(`styles survey (${gpu}) ${s.id}: not shown as the style asks (style.cur ${f['style.cur']}, t ${f['style.t']}, backdrop ${JSON.stringify(bw)}, ascii.mix ${mix}, luminance ${lo.toFixed(3)}..${hi.toFixed(3)})`);
        }
        if (!ONLY || ONLY === 'read') {
          const c = reads.flatMap(([im, ns]) => noteContrasts(im, ns, DITHER_LOOKS.has(s.look)));
          const med = median(c.map((x) => x.ratio)), worst = Math.min(...c.map((x) => x.ratio));
          // this style's own control: the same style with no roads or pockets and dim notes must read under the bound
          {
            await r.command('style readctl 1');
            await r.step(30);
            const cc = [];
            for (let m = 0; m < 4; m++) {
              await r.step(15);
              const p = join(dir, 'read-ctl.png');
              await r.shotTo(p);
              cc.push(...noteContrasts(readPng(p), (await r.command('notes')).notes || [], DITHER_LOOKS.has(s.look)));
              rmSync(p);
            }
            await r.command('style readctl 0');
            const cm = median(cc.map((x) => x.ratio));
            if (cc.length >= 3 && cm < MIN_CONTRAST) ok(`styles readability control (${gpu}) ${s.id}: no roads, dim notes read ${cm.toFixed(2)}:1, under the bound, as it must`);
            else fail(`styles readability control (${gpu}) ${s.id}: no roads and dim notes still read ${cm?.toFixed?.(2)}:1 over ${cc.length} notes: the check cannot see unreadable notes in this style`);
          }
          // nothing in the backdrop may look like a note: its most note-like shape well under the notes' own contrast
          {
            await r.command('notes hide 1');
            await r.step(2);
            const p = join(dir, `${s.index}-${s.id}-backdrop.png`);
            await r.shotTo(p);
            const noteCols = [];
            for (const [im, ns] of reads) for (const n of ns) { if (n.px < NOTE_MIN_PX || n.ahead < 0.3) continue; const kk = im.w / 1920; noteCols.push(rgbAt(im, Math.round(n.x * kk), Math.round(n.y * kk))); }
            const b = noteLike(readPng(p), noteCols, DITHER_LOOKS.has(s.look));
            await r.command('notes hide 0');
            const notes = median(c.map((x) => x.ratio));
            if (b.ratio <= NOTE_LIKE_SHARE * notes) ok(`styles note-like (${gpu}) ${s.id}: the backdrop's most note-like shape ${b.ratio.toFixed(2)}:1${b.at ? ` (${b.size} px at ${b.at})` : ''}, at most ${NOTE_LIKE_SHARE} of the notes' ${notes.toFixed(2)}:1`);
            else fail(`styles note-like (${gpu}) ${s.id}: a backdrop shape ${b.ratio.toFixed(2)}:1 (${b.dims} px at ${b.at}, rgb ${b.col}) against the notes' ${notes.toFixed(2)}:1 (bound ${NOTE_LIKE_SHARE} of it): it reads as a note`);
          }
          if (s.id === 'onebit') { // 2b: the HUD in two colours, and its control
            const runHud = async (on) => {
              await r.command(`hud onebit ${on ? 1 : 0}`);
              await r.step(4);
              let n = 0, two = 0;
              for (let m = 0; m < HUD_FRAMES; m++) {
                await r.step(15);
                const p = join(dir, on ? `hud-onebit-${m}.png` : 'hud-onebit-ctl.png');
                await r.shotTo(p);
                const hb = await r.command('hud');
                const t = hudTwoColour(readPng(p), hb.boxes || []);
                n += t.px, two += t.share * t.px;
                if (on && m > 1) rmSync(p);
              }
              return { share: n ? two / n : 0, px: n };
            };
            const on = await runHud(true), off = await runHud(false);
            await r.command('hud onebit 1');
            const L = (c) => 0.2126 * LUT[c[0]] + 0.7152 * LUT[c[1]] + 0.0722 * LUT[c[2]];
            const cr = (L(INK) + 0.05) / (L(PAPER) + 0.05);
            if (on.px > 20000 && on.share >= HUD_TWO && cr >= 7) ok(`styles hud two colours (${gpu}) onebit: ${(on.share * 100).toFixed(2)}% of ${on.px} HUD pixels over ${HUD_FRAMES} frames are the ink or the paper (bound ${HUD_TWO * 100}%), ink on paper ${cr.toFixed(1)}:1`);
            else fail(`styles hud two colours (${gpu}) onebit: ${(on.share * 100).toFixed(2)}% of ${on.px} HUD pixels are the ink or the paper (bound ${HUD_TWO * 100}%), ink on paper ${cr.toFixed(1)}:1`);
            if (off.px > 20000 && off.share < HUD_TWO) ok(`styles hud two colours control (${gpu}) onebit: the coloured HUD over the print has only ${(off.share * 100).toFixed(2)}% ink or paper, as it must fail`);
            else fail(`styles hud two colours control (${gpu}) onebit: the coloured HUD read ${(off.share * 100).toFixed(2)}% ink or paper: the check cannot see a HUD out of two colours`);
            // the judgements by shape: each on every river for a moment, the middle one's word read binarized
            const crops = [];
            for (const j of [1, 2, 3]) {
              await r.command(`hud judge ${j}`);
              await r.step(3);
              const p = join(dir, `hud-onebit-judge${j}.png`);
              await r.shotTo(p);
              const rv = await r.command('rivers');
              const mid = (rv.rivers || []).filter((x) => x.shown > 0.5).sort((a, b) => Math.abs(a.x - 960) - Math.abs(b.x - 960))[0];
              crops.push(binCrop(readPng(p), (mid?.x ?? 960) - 110, (mid?.y ?? 860) + 10, 220, 90));
            }
            let least = 1;
            for (let a = 0; a < 3; a++) for (let b = a + 1; b < 3; b++) { let d = 0; for (let i = 0; i < crops[a].length; i++) d += crops[a][i] !== crops[b][i]; least = Math.min(least, d / crops[a].length); }
            if (least > JUDGE_APART) ok(`styles hud judgements (${gpu}) onebit: Perfect, Good and Miss in two colours differ in at least ${(least * 100).toFixed(1)}% of their pixels (bound ${JUDGE_APART * 100}%)`);
            else fail(`styles hud judgements (${gpu}) onebit: two judgements in two colours differ in only ${(least * 100).toFixed(1)}% of their pixels`);
          }
          if (c.length < 3) fail(`styles readability (${gpu}) ${s.id}: only ${c.length} readable notes on screen: the check proves nothing`);
          else if (med >= MIN_CONTRAST) ok(`styles readability (${gpu}) ${s.id}: notes ${med.toFixed(2)}:1 against their river (median of ${c.length}, worst ${worst.toFixed(2)}:1, bound ${MIN_CONTRAST}:1)`);
          else if (HELD_READ[s.id]) console.log(`held styles readability (${gpu}) ${s.id}: notes ${med.toFixed(2)}:1 (bound ${MIN_CONTRAST}:1): ${HELD_READ[s.id]}`);
          else fail(`styles readability (${gpu}) ${s.id}: notes only ${med.toFixed(2)}:1 against their river (median of ${c.length}, worst ${worst.toFixed(2)}:1, bound ${MIN_CONTRAST}:1)`);
        }
      }
      if (!ONLY || ONLY === 'survey') {
        const ov = (await r.fields())['hud.overlap_max'];
        if (ov === 0) ok(`styles hud (${gpu}): no HUD lines drawn over each other through the survey`);
        else fail(`styles hud (${gpu}): ${ov} HUD lines drawn over each other in a frame`);
      }
      // the note-like control: the arcade maze as it was (bright critters wandering its halls): must fail
      if (!ONLY || ONLY === 'read') {
        await r.command('song 0 hard');
        await r.step(SONG_IN);
        await r.command('style maze');
        await r.command('maze old 1');
        await r.step(HOLD);
        let worst = { ratio: 0 };
        for (let m = 0; m < 6; m++) {
          await r.step(20);
          await r.command('notes hide 1');
          await r.step(2);
          const p = join(dir, 'control-notelike.png');
          await r.shotTo(p);
          const b = noteLike(readPng(p));
          if (b.ratio > worst.ratio) worst = b;
          await r.command('notes hide 0');
        }
        await r.command('maze old 0');
        if (worst.ratio > 3.4) ok(`styles note-like control (${gpu}): the old maze's critters read ${worst.ratio.toFixed(2)}:1 (${worst.size} px at ${worst.at}), over half the notes' contrast, as they must`);
        else fail(`styles note-like control (${gpu}): the old maze's critters read only ${worst.ratio.toFixed(2)}:1: the check cannot see a note-like shape`);
      }
      // the control: the brightest backdrop, no roads, the notes dimmed: must fail
      if (!ONLY || ONLY === 'read') {
        await r.command('song 0 hard');
        await r.step(SONG_IN);
        await r.command('style fire');
        await r.command('style readctl 1');
        await r.step(HOLD);
        const shot = join(dir, 'control-readability.png');
        const c = [];
        for (let m = 0; m < READ_FRAMES; m++) {
          await r.step(15);
          await r.shotTo(shot);
          c.push(...noteContrasts(readPng(shot), (await r.command('notes')).notes || []));
        }
        const med = median(c.map((x) => x.ratio));
        if (c.length >= 3 && med < MIN_CONTRAST) ok(`styles readability control (${gpu}): fire without roads or pockets and with dim notes reads ${med.toFixed(2)}:1 (under ${MIN_CONTRAST}:1), as it must`);
        else fail(`styles readability control (${gpu}): fire without roads or pockets and with dim notes read ${med?.toFixed?.(2)}:1 over ${c.length} notes: the check cannot see unreadable notes`);
        await r.command('style readctl 0');
      }
    } finally {
      await r.close();
    }
  }

  // ── 3b: each style's own fades: in from the neon characters and back out, the game's fade; its control the same
  // two changes as one-frame cuts, which must fail ──
  if (!ONLY || ONLY === 'flash1') {
    const q = await openBeatsync('beatsync_auto', { gpu, tag: 'styles-flash1' });
    try {
      await q.stepTo(START);
      const all = await styleList(q);
      for (const s of (ONE ? all.filter((x) => x.id === ONE) : all)) {
        // from the neon characters, or for a style in characters itself (a cut between two of those changes little: its
        // control could not fail) from the fire
        // from the style whose picture differs most from this one in brightness (the survey's stills), so the control's
        // cuts are real changes of light; the change made five times in half a second: the game fades (one at a time,
        // the last asked for coming after), the control cuts each time, a flicker that must fail
        let base = s.id === 'neon' ? 'fire' : 'neon', far = -1;
        const lumOf = (x) => { const p = join(dir, `${x.index}-${x.id}.png`); if (!existsSync(p)) return null; const im = readPng(p); let t = 0, n = 0; for (let y = 0; y < im.h; y += 9) for (let xx = 0; xx < im.w; xx += 9) { t += lumAt(im, xx, y); n++; } return t / n; };
        const ls = lumOf(s);
        if (ls !== null) for (const b of all) { if (b.id === s.id) continue; const lb = lumOf(b); if (lb !== null && Math.abs(lb - ls) > far) far = Math.abs(lb - ls), base = b.id; }
        for (const cut of [false, true]) {
          await q.command(`style cut ${cut ? 1 : 0}`);
          await q.command(`style ${base}`);
          await q.step(150);
          const commandsAt = [30, 38, 46, 54, 62].map((f, i) => ({ frame: f, command: `style ${i % 2 ? base : s.id}` }));
          commandsAt.push({ frame: 260, command: `style ${base}` });
          const dump = process.env.CW_FLASH_DUMP && !cut ? { path: join(ROOT, process.env.CW_FLASH_DUMP, `${gpu}-${s.id}.cwfd`) } : {}; // (CW_FLASH_DUMP=<dir>: keep each style's checked frames)
          if (dump.path) mkdirSync(join(ROOT, process.env.CW_FLASH_DUMP), { recursive: true });
          const res = await q.call('frame', { op: 'flash', frames: 400, commandsAt, flash: { maxFlashes: MAX_FLASHES, flickerAmp: 0.02, maxRise: MAX_RISE }, control: cut ? false : undefined, maxSeconds: 600, ...dump }, { timeoutMs: 700000 });
          const m = res.result || {};
          const nums = `rise ${JSON.stringify(m.rise)}, worst second ${JSON.stringify(m.worstSecond)}`;
          if (!cut) {
            if (res.checkOk) ok(`styles flash (${gpu}) ${s.id}: in and out of ${base} by its fade, ${res.verdict}; ${nums}`);
            else fail(`styles flash (${gpu}) ${s.id}: verdict ${res.verdict}, problems ${JSON.stringify(res.problems)}; ${nums}`);
          } else if (res.verdict !== 'ok' && (res.problems || []).length) ok(`styles flash control (${gpu}) ${s.id}: the same changes as cuts fail, as they must`);
          else fail(`styles flash control (${gpu}) ${s.id}: cuts passed (${res.verdict}; ${nums}): the check cannot see this style cut in`);
        }
      }
      await q.command('style cut 0');
    } finally {
      await q.close();
    }
  }

  // ── 3: flashes over every fade ──
  if (!ONLY || ONLY === 'flash') {
    // the styles in turn, every change the game's own fade: each held 2.5 s, then the next (`style next` picks the
    // song's order; the drop's one-beat fade comes with `style next drop`, a few of them)
    const hold = 150, changes = 21; // every style once and a round into the next
    const commandsAt = [];
    for (let i = 0; i < changes; i++) commandsAt.push({ frame: 60 + i * hold, command: i % 3 === 2 ? 'style next drop' : 'style next' });
    const frames = 60 + changes * hold + 120;
    for (const cut of [false, true]) {
      const q = await openBeatsync('beatsync_auto', { gpu, tag: cut ? 'styles-cut' : 'styles-flash' });
      try {
        await q.stepTo(START);
        if (cut) await q.command('style cut 1');
        const res = await q.call('frame', { op: 'flash', frames, commandsAt, flash: { maxFlashes: MAX_FLASHES, flickerAmp: 0.02, maxRise: MAX_RISE }, control: cut ? false : undefined, maxSeconds: 1200 }, { timeoutMs: 1300000 });
        const f = await q.fields();
        const m = res.result || {};
        const nums = `worst second ${JSON.stringify(m.worstSecond)}, rise ${JSON.stringify(m.rise)}, flicker ${JSON.stringify(m.flicker)}`;
        const changed = f['style.changes'];
        if (!cut) {
          if (!(changed >= changes)) fail(`styles flash (${gpu}): only ${changed} style changes in the run (wanted ${changes}): the check proves nothing`);
          if (res.checkOk) ok(`styles flash (${gpu}): ${changed} fades, ${res.verdict}; ${nums}; its 8 Hz strobe control fails, as it must`);
          else fail(`styles flash (${gpu}): verdict ${res.verdict}, problems ${JSON.stringify(res.problems)}; ${nums}`);
          const n = f['fl.style_flashes'];
          if (n <= MAX_FLASHES && f['fl.style_peak'] >= 0.9) ok(`styles flash meter (${gpu}): the fades read ${n} flash(es) a second at most`);
          else fail(`styles flash meter (${gpu}): fl.style_flashes ${n}, peak ${f['fl.style_peak']}`);
        } else if (res.verdict !== 'ok' && (res.problems || []).length) ok(`styles flash control (${gpu}): the same changes as one-frame cuts fail (${JSON.stringify(res.problems).slice(0, 200)}), as they must`);
        else fail(`styles flash control (${gpu}): one-frame cuts passed (${res.verdict}; ${nums}): the check cannot see a cut`);
      } finally {
        await q.close();
      }
    }
  }
}
console.log(failed ? `beatsync-styles: ${failed} failed` : 'beatsync-styles: ok');
process.exit(failed ? 1 : 0);
