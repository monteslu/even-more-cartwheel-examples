// explorer look check: the value and colour of play frames, one room in each of eight regions (no HUD, 1920x1080),
// against the art direction (games/explorer/DESIGN.md, the look): mid-key, never dark and never washed out, real
// contrast between the background, the playfield and the surfaces you stand on, and more than one hue family on
// screen (each region's key hue with a complementary accent).
//   mean luma 100 to 120 of 255 (the deliberately darker regions, the Tidal Halls and Starfall Reach, from 80)
//   luma standard deviation 55 or more (a punchy frame: dark rock against a bright hazy depth), and 4.5 or more of
//   local contrast (the mean absolute difference of luma from its 33 x 33 neighbourhood's mean: crisp shapes and
//   grain, not a blurred or flat frame; this art is clean shapes in haze, so its floor sits well under a textured 3D
//   scene's)
//   a complementary accent: 3% or more of the saturated pixels 90 degrees or more round the hue wheel from the
//   commonest hue (no one-hue region)
// CONTROL, which must fail: the same frames flattened halfway to their own mean (a washed-out grade).
//
//   node tests/explorer-look.mjs              draws the frames on Mesa's software Vulkan (lavapipe, off the GPU) into
//                                             out/look-explorer, then measures them
//   node tests/explorer-look.mjs --dir <d>    measures r<region>.png frames already in d
//   --gpu                                     draws them on the test GPU through the GPU suite lock instead
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, readdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const BIN = [join(ROOT, 'build/native/project/games/explorer'), join(ROOT, 'build/native/games/explorer')].find((d) => existsSync(join(d, 'explorer_auto')));
const NAMES = ['Lantern Gate', 'Rootway', 'Glasswater', 'Fen', 'Clockwork', 'Tidal Halls', 'Starfall', 'Skyreach', 'Archive', 'Forge', 'Frostvault', 'Bloom', 'Heartwood'];
const REGIONS = [0, 1, 2, 5, 6, 9, 10, 11];
const DARKER = new Set([5, 6]);

/** an 8-bit RGB or RGBA PNG (any filter) as { w, h, px: RGB bytes } */
export function readPng(file) {
  const b = readFileSync(file);
  let w = 0, h = 0, type = 0, depth = 0;
  const idat = [];
  for (let p = 8; p < b.length;) {
    const len = b.readUInt32BE(p), kind = b.toString('latin1', p + 4, p + 8), data = b.subarray(p + 8, p + 8 + len);
    if (kind === 'IHDR') w = data.readUInt32BE(0), h = data.readUInt32BE(4), depth = data[8], type = data[9];
    if (kind === 'IDAT') idat.push(data);
    p += 12 + len;
  }
  if (depth !== 8 || (type !== 2 && type !== 6)) throw new Error(`${file}: an 8-bit RGB or RGBA PNG is needed (type ${type}, depth ${depth})`);
  const bpp = type === 6 ? 4 : 3, stride = w * bpp, raw = inflateSync(Buffer.concat(idat));
  const out = Buffer.alloc(w * h * bpp);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], row = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? out[y * stride + x - bpp] : 0, up = y ? out[(y - 1) * stride + x] : 0, c = x >= bpp && y ? out[(y - 1) * stride + x - bpp] : 0;
      let v = row[x];
      if (f === 1) v += a; else if (f === 2) v += up; else if (f === 3) v += (a + up) >> 1;
      else if (f === 4) { const pp = a + up - c, pa = Math.abs(pp - a), pb = Math.abs(pp - up), pc = Math.abs(pp - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      out[y * stride + x] = v & 255;
    }
  }
  if (bpp === 3) return { w, h, px: out };
  const rgb = Buffer.alloc(w * h * 3);
  for (let i = 0; i < w * h; i++) rgb[i * 3] = out[i * 4], rgb[i * 3 + 1] = out[i * 4 + 1], rgb[i * 3 + 2] = out[i * 4 + 2];
  return { w, h, px: rgb };
}

/** the frame's numbers (flatten: lerp each pixel toward the frame mean by that much, for the control) */
export function measure({ w, h, px }, flatten = 0) {
  const n = w * h, L = new Float32Array(n);
  let sum = 0, nsat = 0;
  const sectors = new Array(12).fill(0), m = [0, 0, 0];
  if (flatten) for (let i = 0; i < n; i++) for (let k = 0; k < 3; k++) m[k] += px[i * 3 + k] / n;
  for (let i = 0; i < n; i++) {
    let r = px[i * 3], g = px[i * 3 + 1], b = px[i * 3 + 2];
    if (flatten) r += (m[0] - r) * flatten, g += (m[1] - g) * flatten, b += (m[2] - b) * flatten;
    const l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    L[i] = l, sum += l;
    const mx = Math.max(r, g, b), mn = Math.min(r, g, b), s = mx > 0 ? (mx - mn) / mx : 0;
    if (s > 0.2 && mx > 40) {
      const d = mx - mn;
      const hue = mx === r ? ((g - b) / d + 6) % 6 : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
      sectors[Math.floor(hue * 2) % 12]++, nsat++;
    }
  }
  const mean = sum / n;
  let v = 0;
  for (let i = 0; i < n; i++) v += (L[i] - mean) ** 2;
  const S = new Float64Array((w + 1) * (h + 1));
  for (let y = 0; y < h; y++) for (let x = 0, row = 0; x < w; x++) row += L[y * w + x], S[(y + 1) * (w + 1) + x + 1] = S[y * (w + 1) + x + 1] + row;
  let lc = 0, nl = 0;
  const R = 16;
  for (let y = R; y < h - R - 1; y += 4)
    for (let x = R; x < w - R - 1; x += 4) {
      const a = S[(y - R) * (w + 1) + x - R], b2 = S[(y - R) * (w + 1) + x + R + 1], c = S[(y + R + 1) * (w + 1) + x - R], d = S[(y + R + 1) * (w + 1) + x + R + 1];
      lc += Math.abs(L[y * w + x] - (d - b2 - c + a) / ((2 * R + 1) * (2 * R + 1))), nl++;
    }
  const top = sectors.indexOf(Math.max(...sectors));
  let far = 0;
  for (let k = 0; k < 12; k++) if (Math.min((k - top + 12) % 12, (top - k + 12) % 12) >= 3) far += sectors[k];
  return { mean, sd: Math.sqrt(v / n), local: lc / nl, accent: nsat ? far / nsat : 0 };
}

export function verdict(region, r) {
  const bad = [];
  const lo = DARKER.has(region) ? 80 : 100;
  if (r.mean < lo || r.mean > 120) bad.push(`mean luma ${r.mean.toFixed(0)} (${lo} to 120)`);
  if (r.sd < 55) bad.push(`luma sd ${r.sd.toFixed(1)} (55 or more)`);
  if (r.local < 4.5) bad.push(`local contrast ${r.local.toFixed(1)} (4.5 or more)`);
  if (r.accent < 0.03) bad.push(`complementary accent ${(r.accent * 100).toFixed(1)}% (3% or more)`);
  return bad;
}

/** the room each region is shown by: its first plain room two or three cells wide (worldgen's listing) */
function rooms() {
  const r = spawnSync(join(BIN, 'explorer_worldgen'), ['/dev/null', '--list'], { encoding: 'utf8' });
  const pick = {};
  for (const line of (r.stdout + r.stderr).split('\n')) {
    const m = /^room (\d+) region (\d+) kind (\d+) (\d)x(\d)/.exec(line);
    if (m && +m[3] === 0 && +m[5] === 1 && +m[4] >= 2 && pick[+m[2]] === undefined) pick[+m[2]] = +m[1];
  }
  return pick;
}

function shot(room, out, gpu) {
  const env = { ...process.env, CW_FRAMES: '90', CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', CW_RENDER_SCALE: '1.5', CW_SCREENSHOT: out,
    EXPLORER_IDLE: '1', EXPLORER_NO_HUD: '1', EXPLORER_ROOM: String(room) };
  if (gpu) {
    env.CW_GPU = 'gl';
    return new Promise((ok) => spawn('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'explorer-look', join(BIN, 'explorer_auto')], { env, stdio: 'ignore' }).on('exit', ok));
  }
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  Object.assign(env, { SDL_VIDEODRIVER: 'offscreen', VK_ICD_FILENAMES: '/usr/share/vulkan/icd.d/lvp_icd.json', CW_GPU: 'webgpu' });
  return new Promise((ok) => spawn(join(BIN, 'explorer_auto'), [], { env, stdio: 'ignore' }).on('exit', ok));
}

if (import.meta.url !== `file://${process.argv[1]}`) { /* imported */ } else {
const argv = process.argv.slice(2);
let dir = argv.includes('--dir') ? argv[argv.indexOf('--dir') + 1] : null;
if (!dir) {
  dir = join(ROOT, 'out/look-explorer');
  mkdirSync(dir, { recursive: true });
  const pick = rooms(), gpu = argv.includes('--gpu');
  if (gpu) for (const r of REGIONS) await shot(pick[r], join(dir, `r${r}.png`), true);
  else await Promise.all(REGIONS.map((r) => shot(pick[r], join(dir, `r${r}.png`), false)));
}
const files = readdirSync(dir).filter((f) => /^r\d+\.png$/.test(f)).sort((a, b) => parseInt(a.slice(1)) - parseInt(b.slice(1)));
if (!files.length) { console.error(`no r<region>.png frames in ${dir}`); process.exit(2); }
let failed = 0, caught = 0;
for (const f of files) {
  const region = parseInt(f.slice(1)), img = readPng(join(dir, f)), r = measure(img), bad = verdict(region, r);
  console.log(`${bad.length ? 'FAIL' : 'ok  '} ${NAMES[region].padEnd(12)} mean ${r.mean.toFixed(1)} sd ${r.sd.toFixed(1)} local ${r.local.toFixed(1)} accent ${(r.accent * 100).toFixed(1)}%${bad.length ? `: ${bad.join('; ')}` : ''}`);
  if (bad.length) failed++;
  if (verdict(region, measure(img, 0.5)).length) caught++;
}
if (caught === files.length) console.log('ok   control: every frame flattened halfway to its mean fails');
else { console.log(`CONTROL PASSED (a failure): ${files.length - caught} flattened frame(s) still pass`); failed++; }
process.exit(failed ? 1 : 0);
}
