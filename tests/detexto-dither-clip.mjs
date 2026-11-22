// Detexto's look trial: a motion clip of real play per look (games/detexto/src/dither.h), through romdev at 1920x1080.
// The game cart (not the bot) is driven by a pad schedule: through the title into the first level, free fire on, the
// charge picked (west twice) and fired at the foot on the aim the level opens with, a second charge a little higher
// while the rubble falls, then heavy balls. romdev's frame record captures every frame on the fixed clock (one
// call), and ffmpeg makes a 60 fps MP4 of it (real speed). The raw dump is deleted once encoded.
//   node tests/detexto-dither-clip.mjs <out dir> [--gpu gl|webgpu] [--looks ascii,bayer,blue,feedback] [--frames 540]
//     [--set KEY=VALUE,...] [--tag name] [--flash] (the flash check of the same play instead of a clip)
import { mkdirSync, rmSync, writeFileSync, openSync, readSync, closeSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call } from './lib/romdev.mjs';

const a = process.argv.slice(2);
const arg = (k, d) => { const i = a.indexOf(k); return i >= 0 ? a[i + 1] : d; };
const gpu = arg('--gpu', 'gl');
const out = resolve(a[0]);
const looks = arg('--looks', 'ascii,bayer,blue,feedback').split(',');
const frames = +arg('--frames', 540);
const extra = Object.fromEntries((arg('--set', '') || '').split(',').filter(Boolean).map((kv) => kv.split('=')));
const tag = arg('--tag', '');
const flash = a.includes('--flash');
mkdirSync(out, { recursive: true });

// the shimmer: each frame's share of pixels that changed side of mid grey since the frame before (1-bit looks flip
// whole pixels), averaged over the still opening (frames 1-30, the camera at rest before the first shot) and the
// whole clip; and the share of pixels that are neither near black nor near white (a 1-bit look: only the HUD's
// anti-aliased text)
function shimmer(dump) {
  const fd = openSync(dump, 'r');
  const hdr = Buffer.alloc(16); readSync(fd, hdr, 0, 16, 0);
  const w = hdr.readUInt32LE(4), h = hdr.readUInt32LE(8), n = w * h * 3;
  const buf = Buffer.alloc(n);
  let prev = null, k = 0, still = 0, all = 0, grey = 0, nf = 0;
  for (;;) {
    if (readSync(fd, buf, 0, n, 16 + k * n) < n) break;
    const bits = new Uint8Array(w * h);
    let g = 0;
    for (let i = 0, j = 0; i < w * h; i++, j += 3) {
      const l = (buf[j] * 54 + buf[j + 1] * 183 + buf[j + 2] * 19) >> 8;
      bits[i] = l > 127 ? 1 : 0;
      if (l > 40 && l < 215) g++;
    }
    grey += g / (w * h);
    if (prev) {
      let c = 0;
      for (let i = 0; i < w * h; i++) c += bits[i] ^ prev[i];
      const r = c / (w * h);
      all += r;
      if (k <= 30) still += r;
    }
    prev = bits, k++, nf++;
  }
  closeSync(fd);
  return { frames: nf, stillFlip: still / 30, meanFlip: all / Math.max(1, nf - 1), greyShare: grey / nf };
}

const tap = (f, b, n = 3) => ({ frame: f, buttons: [b], until: f + n });
// the play, in frames from the start of the capture (romdev input string: frame:buttons:hold)
const PLAY = [
  '6:west:3', '18:west:3',          // shot -> heavy ball -> charge
  '40:south:3',                     // the charge at the foot, on the aim the level opens with
  '150:up:5',                       // a little higher
  '200:south:3',                    // the second charge, as the first one's rubble falls
  '300:east:3',                     // back to the heavy ball
  '312:right:5', '322:up:4',
  '340:south:3',                    // a heavy ball into the rest
  '420:left:8',
  '440:south:3',                    // and another
].join(',');

for (const look of looks) {
  const h = await openCart('detexto', { gpu, tag: `dither-clip-${look}` });
  try {
    for (const [k, v] of Object.entries({ LOOK: look, ...extra })) await h.command(`dm set ${k} "${v}"`);
    await call('input', { op: 'timeline', timeline: [tap(20, 'south'), tap(40, 'south'), tap(60, 'start'), tap(90, 'south')] }, h.session);
    await h.step(300 - h.frame);
    await h.command('freefire 1');
    const name = `clip-${look}${tag ? '-' + tag : ''}-${gpu}`;
    if (flash) {
      const r = await call('frame', { op: 'flash', frames, input: PLAY }, h.session, { timeoutMs: 1800000 });
      writeFileSync(join(out, `flash-${look}${tag ? '-' + tag : ''}-${gpu}.json`), JSON.stringify(r, null, 1));
      console.log(`${look} ${gpu} flash: verdict ${r.verdict} checkOk ${r.checkOk} control ${r.control && r.control.verdict} problems ${JSON.stringify(r.problems || []).slice(0, 400)} worst ${JSON.stringify(r.result && r.result.worstSecond || {}).slice(0, 300)}`);
      continue;
    }
    const dump = join(out, `${name}.cwfd`);
    const r = await call('frame', { op: 'record', path: dump, frames, input: PLAY, downscale: 1 }, h.session, { timeoutMs: 1800000 });
    const sh = shimmer(dump);
    writeFileSync(join(out, `${name}-shimmer.json`), JSON.stringify(sh));
    console.log(`${name} shimmer: still ${(sh.stillFlip * 100).toFixed(3)}% a frame, whole clip ${(sh.meanFlip * 100).toFixed(2)}% a frame, grey pixels ${(sh.greyShare * 100).toFixed(2)}%`);
    const mp4 = join(out, `${name}.mp4`);
    const ff = spawnSync('bash', ['-c', `tail -c +17 '${dump}' | ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s 1920x1080 -framerate 60 -i - -c:v libx264 -preset slow -crf 12 -tune grain -pix_fmt yuv420p -movflags +faststart '${mp4}'`], { stdio: 'inherit' });
    rmSync(dump, { force: true });
    if (ff.status !== 0) throw new Error(`ffmpeg failed for ${name}`);
    console.log(`${mp4} (${r.frames || frames} frames, ${JSON.stringify(r).slice(0, 200)})`);
  } finally { await h.close(); }
}
