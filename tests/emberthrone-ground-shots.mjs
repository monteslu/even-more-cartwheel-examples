// emberthrone ground shots: the ground of every region from the game's camera on romdev (127.0.0.1:7331, or ROMDEV_URL),
// held by the auto cart's groundcam command, for tests/emberthrone-ground.mjs and for looking at:
//   td<region>.png     straight down from 14 m (the underway 3 m) (the seam measure: about 67 px a metre)
//   wide<region>.png   straight down from 45 m (the underway 4.2 m) (the repetition measure: about 21 px a metre)
//   play<region>.png   play height, 2.6 m over the ground looking 30 degrees down
//   far<region>.png    a wider view, 9 m over the ground looking 18 degrees down
//   move<region>-<k>.png  a short camera move at play height (eight frames, 0.4 m apart)
//
//   node tests/emberthrone-ground-shots.mjs <out dir> [cart] [--gpu gl|webgpu] [--regions meadow,canyon] [--no-move]
//   EMBER_SHOT_ENV="ADV_PROBE_GRASSSET=density=12,fade_mode=DITHER" adds to the cart's env (side by side looks)
import { mkdirSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request as httpRequest } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const pos = argv.filter((a, i) => !a.startsWith('--') && !(i > 0 && argv[i - 1].startsWith('--') && !['--no-move'].includes(argv[i - 1])));
const OUT = resolve(pos[0] || join(ROOT, 'out/ground'));
const CART = resolve(pos[1] || join(ROOT, 'build/cart-wgpu/carts/emberthrone_auto.wasc'));
const gpu = opt('--gpu', 'webgpu');
const only = opt('--regions', '');
if (!existsSync(CART)) throw new Error(`no cart at ${CART}`);
mkdirSync(OUT, { recursive: true });

// one open patch of each region's own ground (x, z), the way the play views look (yaw, degrees), and the heights of the
// straight-down views (the underway is roofed: its views stay under the roof)
export const SPOTS = {
  meadow: [185, 292, 30], canyon: [168, 140, 200], marsh: [262, 200, 120], keep: [44, 172, 60],
  heights: [440, 130, 250], underway: [176, 352, 180, 3, 4.2], spire: [218, 488, 300], wood: [500, 300, 160],
};

const ROMDEV = new URL(process.env.ROMDEV_URL || `http://127.0.0.1:${process.env.ROMDEV_PORT || 7331}`);
function post(tool, a, session) {
  const body = JSON.stringify(a);
  return new Promise((okk, failed) => {
    const req = httpRequest({ host: ROMDEV.hostname, port: +ROMDEV.port || 80, path: `/tool/${tool}`, method: 'POST', agent: false,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'x-romdev-session': session, 'x-romdev-agent': 'emberthrone-ground' } }, (res) => {
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
    const timer = setTimeout(() => req.destroy(new Error(`romdev ${tool}: no reply in 300 s`)), 300000);
    req.on('close', () => clearTimeout(timer));
    req.on('error', failed);
    req.end(body);
  });
}

const session = `emberthrone-ground-${process.pid}-${gpu}`;
const loaded = await post('loadMedia', { platform: 'wasmcart', path: CART, wgpu: gpu === 'webgpu', preferredSize: { width: 1920, height: 1080 },
  firstCommand: `env ADV_FROM=1 ADV_NO_HUD=1 ADV_PROBE_NOCHARS=1${process.env.EMBER_SHOT_ENV ? ' ' + process.env.EMBER_SHOT_ENV : ''}` }, session);
if (loaded.gpu !== gpu) throw new Error(`asked for ${gpu}, the cart runs on ${loaded.gpu}`);
const cmd = async (text) => { const r = await post('wasm', { op: 'command', text }, session); return r && typeof r === 'object' && 'reply' in r ? r.reply : r; };
const shot = (file, frames = 30) => post('frame', { op: 'stepAndShot', frames, path: join(OUT, file) }, session);
const meta = {};
try {
  await post('frame', { op: 'step', frames: 90 }, session);
  for (const [name, [x, z, yaw, tdH = 14, wideH = 45]] of Object.entries(SPOTS)) {
    if (only && !only.split(',').includes(name)) continue;
    // the paging follows the bot: take it to the region first (the route point nearest the spot), then hold the camera
    meta[name] = {};
    meta[name].td = await cmd(`groundcam ${x} ${z} ${tdH}`); await shot(`td${name}.png`, 40);
    meta[name].wide = await cmd(`groundcam ${x} ${z} ${wideH}`); meta[name].wideH = wideH; await shot(`wide${name}.png`);
    await cmd(`groundcam ${x} ${z} 2.6 -30 ${yaw}`); await shot(`play${name}.png`);
    await cmd(`groundcam ${x} ${z} 9 -18 ${yaw}`); await shot(`far${name}.png`);
    if (!argv.includes('--no-move')) {
      const r = yaw * Math.PI / 180;
      for (let k = 0; k < 8; k++) { await cmd(`groundcam ${(x - Math.sin(r) * 0.4 * k).toFixed(3)} ${(z - Math.cos(r) * 0.4 * k).toFixed(3)} 2.6 -30 ${yaw}`); await shot(`move${name}-${k}.png`, 2); }
    }
    console.log(name, JSON.stringify(meta[name].td));
  }
  await cmd('groundcam off');
} finally {
  await post('host', { op: 'shutdown' }, session).catch(() => {});
}
import('node:fs').then(({ writeFileSync }) => writeFileSync(join(OUT, 'shots.json'), JSON.stringify({ cart: CART, gpu, spots: SPOTS, meta }, null, 1)));
