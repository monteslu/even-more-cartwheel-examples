// emberthrone tuft check: the grass tufts hold still in motion. The camera walks slowly forward at play height over the
// meadow and the spire's downs (1.2 m a second, 1.8 m over the ground, 20 degrees down; the auto cart's groundcam on
// romdev, 127.0.0.1:7331 or ROMDEV_URL), and every frame of two seconds is recorded at 1920x1080, three times over the
// same path: with the tufts, with the 0.49.0 tufts (ADV_PROBE_OLDTUFTS), and with none (ADV_PROBE_NOGRASS).
//   tuft pixels  where a frame with tufts differs from the same frame with none (luma 16 or more)
//   flicker      on those pixels, the mean of |L(t+1) - 2 L(t) + L(t-1)|: smooth motion is close to linear from frame to
//                frame, a blade that shimmers (thinner than a pixel, lit then not) or pops does not. Reported against the
//                same pixels of the run with no tufts (the ground moving under them): the excess is the tufts' own.
//   Pass: excess under 4 (of 255) in every place. CONTROL, which must fail: the 0.49.0 tufts.
//
//   node tests/emberthrone-tufts.mjs [cart] [--gpu gl|webgpu] [--places meadow,spire] [--keep <dir>]
import { readFileSync, rmSync, mkdirSync, existsSync, openSync, readSync, closeSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request as httpRequest } from 'node:http';
import { spawnSync } from 'node:child_process';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'emberthrone-tufts', 'node', fileURLToPath(import.meta.url), ...argv],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const opt = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const CART = resolve(argv.find((a, i) => !a.startsWith('--') && !(i && argv[i - 1].startsWith('--'))) || join(ROOT, 'build/cart-wgpu/carts/emberthrone_auto.wasc'));
const gpu = opt('--gpu', 'webgpu');
const OUT = resolve(opt('--keep', join(ROOT, `out/tufts-${gpu}`)));
const keep = argv.includes('--keep');
export const BOUND = 4, FRAMES = 120, MASK = 16;
mkdirSync(OUT, { recursive: true });
// x, z, yaw (degrees): open grass with tufts ahead
const PLACES = { meadow: [184, 296, 30], spire: [214, 492, 300] };

const ROMDEV = new URL(process.env.ROMDEV_URL || `http://127.0.0.1:${process.env.ROMDEV_PORT || 7331}`);
function post(tool, a, session) {
  const body = JSON.stringify(a);
  return new Promise((okk, failed) => {
    const req = httpRequest({ host: ROMDEV.hostname, port: +ROMDEV.port || 80, path: `/tool/${tool}`, method: 'POST', agent: false,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'x-romdev-session': session, 'x-romdev-agent': 'emberthrone-tufts' } }, (res) => {
      let text = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { text += c; });
      res.on('end', () => {
        let b;
        try { b = JSON.parse(text); } catch { b = text; }
        if (b && Array.isArray(b.content)) { const t = b.content.find((c) => c.type === 'text'); try { b = JSON.parse(t.text); } catch { b = t ? t.text : b; } }
        if (res.statusCode < 200 || res.statusCode >= 300 || (b && b.isError)) failed(new Error(`romdev ${tool}: ${typeof b === 'string' ? b : JSON.stringify(b).slice(0, 1500)}`));
        else okk(b);
      });
      res.on('error', failed);
    });
    const timer = setTimeout(() => req.destroy(new Error(`romdev ${tool}: no reply in 600 s`)), 600000);
    req.on('close', () => clearTimeout(timer));
    req.on('error', failed);
    req.end(body);
  });
}

/** one recorded walk: the CW_FRAME_DUMP file's path */
async function walk(variant, env, place) {
  const session = `emberthrone-tufts-${process.pid}-${variant}-${place}`;
  const loaded = await post('loadMedia', { platform: 'wasmcart', path: CART, wgpu: gpu === 'webgpu', preferredSize: { width: 1920, height: 1080 },
    firstCommand: `env ADV_FROM=1 ADV_NO_HUD=1 ADV_PROBE_NOCHARS=1${env ? ' ' + env : ''}` }, session);
  if (loaded.gpu !== gpu) throw new Error(`asked for ${gpu}, the cart runs on ${loaded.gpu}`);
  try {
    await post('frame', { op: 'step', frames: 90 }, session);
    const [x, z, yaw] = PLACES[place], r = yaw * Math.PI / 180, v = 0.02;
    const file = join(OUT, `${place}-${variant}.cwfd`);
    await post('frame', { op: 'record', path: file, frames: FRAMES, warmup: 30, downscale: 1,
      commands: [`groundcam ${x} ${z} 1.8 -20 ${yaw} ${(-Math.sin(r) * v).toFixed(4)} ${(-Math.cos(r) * v).toFixed(4)}`] }, session);
    return file;
  } finally {
    await post('host', { op: 'shutdown' }, session).catch(() => {});
  }
}

/** frames of a CW_FRAME_DUMP file as luma planes */
function frames(file) {
  const fd = openSync(file, 'r'), head = Buffer.alloc(16);
  readSync(fd, head, 0, 16, 0);
  if (head.toString('latin1', 0, 4) !== 'CWFD') throw new Error(`${file}: not a CW_FRAME_DUMP file`);
  const w = head.readUInt32LE(4), h = head.readUInt32LE(8), n = w * h, buf = Buffer.alloc(n * 3), out = [];
  for (let pos = 16; ; pos += n * 3) {
    if (readSync(fd, buf, 0, n * 3, pos) < n * 3) break;
    const L = new Float32Array(n);
    for (let i = 0; i < n; i++) L[i] = 0.2126 * buf[i * 3] + 0.7152 * buf[i * 3 + 1] + 0.0722 * buf[i * 3 + 2];
    out.push(L);
  }
  closeSync(fd);
  return { w, h, f: out };
}
/** flicker of the tufts in run a against run none: { excess, tuftPx (mean share of the frame), a, none } */
export function flicker(a, none) {
  const n = Math.min(a.f.length, none.f.length), px = a.w * a.h;
  let sa = 0, sn = 0, cnt = 0;
  for (let t = 1; t + 1 < n; t++) {
    const A0 = a.f[t - 1], A1 = a.f[t], A2 = a.f[t + 1], N0 = none.f[t - 1], N1 = none.f[t], N2 = none.f[t + 1];
    for (let i = 0; i < px; i++) {
      if (Math.abs(A1[i] - N1[i]) < MASK) continue;
      sa += Math.abs(A2[i] - 2 * A1[i] + A0[i]), sn += Math.abs(N2[i] - 2 * N1[i] + N0[i]), cnt++;
    }
  }
  return { excess: +((sa - sn) / Math.max(cnt, 1)).toFixed(2), tuftPx: +(cnt / (n - 2) / px).toFixed(4), a: +(sa / Math.max(cnt, 1)).toFixed(2), none: +(sn / Math.max(cnt, 1)).toFixed(2) };
}

let bad = 0;
for (const place of Object.keys(PLACES).filter((p) => !opt('--places', '') || opt('--places', '').split(',').includes(p))) {
  const none = frames(await walk('none', 'ADV_PROBE_NOGRASS=1', place));
  for (const [variant, env, control] of [['tufts', '', false], ['old', 'ADV_PROBE_OLDTUFTS=1', true]]) {
    const r = flicker(frames(await walk(variant, env, place)), none);
    const pass = r.excess < BOUND && r.tuftPx > 0.01;
    const good = control ? !pass : pass;
    if (!good) bad++;
    console.log(`${good ? 'ok  ' : 'FAIL'} ${control ? 'CONTROL ' : ''}${place.padEnd(7)} ${variant.padEnd(5)} ${gpu}: tuft flicker ${r.a} against ${r.none} with none, excess ${r.excess} (bound ${BOUND}) over ${(r.tuftPx * 100).toFixed(1)}% of the frame${control ? (pass ? '  (the control passed: the check cannot see it)' : '  (control fails, as it must)') : ''}`);
  }
}
if (!keep) rmSync(OUT, { recursive: true, force: true });
console.log(bad ? `${bad} failed` : 'tufts: all pass');
process.exit(bad ? 1 : 0);
