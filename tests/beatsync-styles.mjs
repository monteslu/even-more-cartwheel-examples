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
//    ((L1 + 0.05) / (L2 + 0.05)). A style passes when the median note reaches MIN_CONTRAST (3:1, WCAG's bound for
//    graphics that must be seen). The CONTROL must fail: the brightest backdrop with its roads and pockets off and the notes dimmed
//    (`style readctl 1`).
// 3. Flashes: romdev's frame({op:'flash'}) over a run through every fade (the styles in turn, each change the game's own
//    fade, a drop's fast one included), WCAG 2.3.1 and Harding: at most 3 flashes a second, regular flicker under
//    0.02, and the hardest two-frame rise of a sixth of the screen under MAX_RISE; the game's own flash meter for the
//    fades (fl.style) at most 3 a second. The CONTROL must fail: the same run with every change a one-frame cut
//    (`style cut 1`).
//
// 4. The HUD: no two of its lines of text drawn over each other (hud.overlap_max, the most a frame, over the whole
//    survey: the middle gate's judgement and the multiplier or the streak called out under it overlapped before). The
//    CONTROL must fail: the same play with `hud old 1` (the old placement) for 1200 frames.
//
//   node tests/beatsync-styles.mjs [--gpu gl|webgpu|both] [--only survey|read|flash] [--style <id>]
import { mkdirSync, rmSync } from 'node:fs';
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
const MIN_CONTRAST = 3.0; // WCAG 1.4.11's bound for graphics a person must see
const MAX_RISE = 0.2;     // the hardest two-frame rise of a sixth of the screen a fade may make (a hit's burst makes about
                          // 0.1; a cut between two styles makes 0.5)
const MAX_FLASHES = 3;
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
/** each readable note's contrast against its river round it: [{lane, ratio}] */
function noteContrasts(img, notes) {
  const k = img.w / 1920, res = [];
  for (const n of notes) {
    // (a note at the gate shares it with the hit's burst and the judgement: read before it gets there)
    if (n.px < NOTE_MIN_PX || n.y > 1000 || n.ahead < 0.3) continue;
    const x = n.x * k, y = n.y * k, w = n.px * k, h = n.lane === 2 ? w * 0.33 : w; // the bass note is a bar a third as tall
    const core = boxLums(img, x - w * 0.18, y - h * 0.12, x + w * 0.18, y + h * 0.12);
    // the river above and below the note: the road it rides (another note there is rare and the median ignores it)
    // (the pixels of the other notes left out: on a run of eighths the next note is right there)
    const others = notes.filter((o) => o !== n).map((o) => [o.x * k, o.y * k, o.px * k * 0.7, (o.lane === 2 ? o.px * 0.33 : o.px) * k * 0.7]);
    const skip = (px, py) => others.some(([ox, oy, rx, ry]) => Math.abs(px - ox) < rx && Math.abs(py - oy) < ry);
    const bg = [...boxLums(img, x - w * 0.3, y - h * 1.5, x + w * 0.3, y - h * 0.8, skip), ...boxLums(img, x - w * 0.3, y + h * 0.8, x + w * 0.3, y + h * 1.5, skip)];
    if (core.length < 16 || bg.length < 16) continue;
    // as WCAG measures text: the note's own colour (its brightest tenth: in the characters a note is glyph strokes on
    // black, and the strokes are its colour) against the river's typical colour round it (the median)
    const lc = pct(core, 0.9), lb = median(bg);
    res.push({ lane: n.lane, ratio: (Math.max(lc, lb) + 0.05) / (Math.min(lc, lb) + 0.05), note: lc, back: lb });
  }
  return res;
}

async function styleList(r) {
  const st = await r.command('style');
  return st.styles.map((s, i) => ({ ...s, index: i }));
}

for (const gpu of GPUS) {
  const dir = join(OUT, gpu);
  rmSync(dir, { recursive: true, force: true });
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
        const BD = ['none', 'fire', 'sea', 'grid', 'lasers', 'stage', 'clean', 'maze']; // src/backdrop.h BD_*
        const want = BD.indexOf(s.backdrop);
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
          const c = reads.flatMap(([im, ns]) => noteContrasts(im, ns));
          const med = median(c.map((x) => x.ratio)), worst = Math.min(...c.map((x) => x.ratio));
          if (c.length < 3) fail(`styles readability (${gpu}) ${s.id}: only ${c.length} readable notes on screen: the check proves nothing`);
          else if (med >= MIN_CONTRAST) ok(`styles readability (${gpu}) ${s.id}: notes ${med.toFixed(2)}:1 against their river (median of ${c.length}, worst ${worst.toFixed(2)}:1, bound ${MIN_CONTRAST}:1)`);
          else fail(`styles readability (${gpu}) ${s.id}: notes only ${med.toFixed(2)}:1 against their river (median of ${c.length}, worst ${worst.toFixed(2)}:1, bound ${MIN_CONTRAST}:1)`);
        }
      }
      if (!ONLY || ONLY === 'survey') {
        const ov = (await r.fields())['hud.overlap_max'];
        if (ov === 0) ok(`styles hud (${gpu}): no HUD lines drawn over each other through the survey`);
        else fail(`styles hud (${gpu}): ${ov} HUD lines drawn over each other in a frame`);
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

  // ── 3: flashes over every fade ──
  if (!ONLY || ONLY === 'flash') {
    // the styles in turn, every change the game's own fade: each held 2.5 s, then the next (`style next` picks the
    // song's order; the drop's one-beat fade comes with `style next drop`, a few of them)
    const hold = 150, changes = 9;
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
