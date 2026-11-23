// beatsync's notes are equal targets: every river's note covers about the same area on screen, measured in pixels from
// the play camera. On GL and WebGPU (--gpu gl|webgpu|both, default both), the autopilot cart on its fixed clock.
//
// The run: the plain picture with its gems left out and nothing but the notes drawn, on black (`style clean`, `lit 1`,
// `style blank 1`, `notes solo 1`: each coming note is its flat shape in its lane's colour), on Hard in the three-lane game (kick, clap, bass) and in the
// groove on two lanes (bass and the synth). On READS frames a quarter of a second apart every coming note big enough to
// measure (NOTE_MIN_PX wide, 0.4 to 3 beats ahead) is cut from the frame at native scale and its lit pixels counted (over
// half its own brightest), divided by the square of its size (the `notes` request's unit: pixels a unit of shape at its
// distance, the same for every river), so a note far off and one near count alike. Each river's median must reach
// MIN_SHARE of the largest river's: no river's note reads as the smaller target. A crop of each river's note is kept in
// out/beatsync-footprint/<gpu>/ for review.
//
// The CONTROL must fail: the same with the old shapes (`notes old`: the bass a flat bar across its river, the synth a
// hexagon), where the bass bar covered about 0.79 of the hexagon.
//
//   node tests/beatsync-footprint.mjs [--gpu gl|webgpu|both]
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { gpusOf, openBeatsync, songs } from './lib/beatsync.mjs';
import { readPng } from './lib/png.mjs';

const MIN_SHARE = 0.9, NOTE_MIN_PX = 36, READS = 10;
const LANE = ['kick', 'clap', 'bass', 'synth'];
let failed = 0;
const ok = (s) => console.log(`ok   ${s}`);
const fail = (s) => { console.log(`FAIL ${s}`); failed++; };
const lin = (c) => { const v = c / 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; };
const LUT = Float64Array.from({ length: 256 }, (_, i) => lin(i));
const lum = (img, x, y) => { const i = (y * img.w + x) * 3; return 0.2126 * LUT[img.rgb[i]] + 0.7152 * LUT[img.rgb[i + 1]] + 0.0722 * LUT[img.rgb[i + 2]]; };
const median = (a) => { const s = [...a].sort((p, q) => p - q); return s.length ? s[s.length >> 1] : NaN; };

/** a note's lit area over its size squared, and its crop (a small PPM-free png is not needed: raw rgb kept for a crop) */
function footprint(img, n, others) {
  const k = img.w / 1920, x = n.x * k, y = n.y * k, half = n.px * k * 0.75;
  const x0 = Math.max(0, Math.round(x - half)), x1 = Math.min(img.w, Math.round(x + half)), y0 = Math.max(0, Math.round(y - half)), y1 = Math.min(img.h, Math.round(y + half));
  let peak = 0;
  for (let yy = y0; yy < y1; yy++) for (let xx = x0; xx < x1; xx++) peak = Math.max(peak, lum(img, xx, yy));
  if (peak < 0.02) return null;
  let lit = 0;
  for (let yy = y0; yy < y1; yy++) for (let xx = x0; xx < x1; xx++) {
    if (others.some(([ox, oy, r]) => Math.abs(xx - ox) < r && Math.abs(yy - oy) < r)) continue;
    if (lum(img, xx, yy) > 0.5 * peak) lit++;
  }
  const unit = n.unit * k;
  return { area: lit / (unit * unit), box: [x0, y0, x1 - x0, y1 - y0] };
}

async function measure(gpu, old, dir) {
  const r = await openBeatsync('beatsync_auto', { gpu, tag: old ? 'footprint-old' : 'footprint' });
  const all = songs(), grooviest = all.reduce((a, b) => (b.pairs.groove.hard.length > a.pairs.groove.hard.length ? b : a));
  const per = { 0: [], 1: [], 2: [], 3: [] }, crops = {};
  try {
    await r.stepTo(300);
    for (const run of [`song 0 hard`, `song ${grooviest.id} hard groove hard`]) {
      await r.command(run);
      await r.command('style clean');
      await r.command('lit 1');
      await r.command('style blank 1');
      await r.command('notes solo 1');
      await r.command(old ? 'notes old' : 'notes new');
      await r.step(600);
      for (let m = 0; m < READS; m++) {
        await r.step(15);
        const p = join(dir, 'frame.png');
        await r.shotTo(p);
        const img = readPng(p), ns = ((await r.command('notes')).notes || []), k = img.w / 1920;
        for (const n of ns) {
          if (n.px < NOTE_MIN_PX || n.ahead < 0.4 || n.ahead > 3) continue;
          const others = ns.filter((o) => o !== n).map((o) => [o.x * k, o.y * k, o.px * k * 0.6]);
          const f = footprint(img, n, others);
          if (!f) continue;
          per[n.lane].push(f.area);
          if (!crops[n.lane] || n.px > crops[n.lane].px) crops[n.lane] = { px: n.px, png: `${dir}/${old ? 'old-' : ''}${LANE[n.lane]}.png`, data: cropPng(img, f.box) };
        }
        rmSync(p);
      }
    }
  } finally {
    await r.close();
  }
  for (const c of Object.values(crops)) if (c.data) writeFileSync(c.png, c.data);
  return per;
}

// a minimal PNG writer for the crops (rgb, no filter)
import { deflateSync } from 'node:zlib';
function cropPng(img, [x0, y0, w, h]) {
  const crcT = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
  const crc = (b) => { let c = -1; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; };
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) for (let c = 0; c < 3; c++) raw[y * (w * 3 + 1) + 1 + x * 3 + c] = img.rgb[((y0 + y) * img.w + x0 + x) * 3 + c];
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

for (const gpu of gpusOf(process.argv.slice(2))) {
  const dir = join(ROOT, 'out/beatsync-footprint', gpu);
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  for (const old of [false, true]) {
    const per = await measure(gpu, old, dir);
    const med = Object.fromEntries(Object.entries(per).map(([l, a]) => [l, a.length >= 3 ? median(a) : NaN]));
    const known = Object.entries(med).filter(([, v]) => Number.isFinite(v));
    const top = Math.max(...known.map(([, v]) => v));
    const line = Object.entries(med).map(([l, v]) => `${LANE[l]} ${Number.isFinite(v) ? v.toFixed(2) : '-'} (${per[l].length})`).join(', ');
    const small = known.filter(([, v]) => v < MIN_SHARE * top);
    if (!old) {
      if (known.length < 4) fail(`footprint (${gpu}): only ${known.length} of 4 rivers' notes measured (${line}): the check proves nothing`);
      else if (small.length) fail(`footprint (${gpu}): ${small.map(([l, v]) => `${LANE[l]} ${(v / top).toFixed(2)}`).join(', ')} of the largest note's area (bound ${MIN_SHARE}); areas in units: ${line}`);
      else ok(`footprint (${gpu}): every river's note at least ${MIN_SHARE} of the largest's area (units of shape squared: ${line}), crops in ${dir}`);
    } else if (small.length) ok(`footprint control (${gpu}): the old shapes leave ${small.map(([l, v]) => `${LANE[l]} at ${(v / top).toFixed(2)}`).join(', ')} of the largest note's area, under ${MIN_SHARE}, as it must (${line})`);
    else fail(`footprint control (${gpu}): the old bar and hexagon passed (${line}): the check cannot see a smaller target`);
  }
}
console.log(failed ? `beatsync-footprint: ${failed} failed` : 'beatsync-footprint: ok');
process.exit(failed ? 1 : 0);
