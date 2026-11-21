// skate's split screen in one pass: the views drawn in one pass (a sky dome a camera and the tokens each player has
// not taken said with masks of views, a shared post chain with a vignette in each view) give the picture the old build
// gives (a view hook, a post chain a view) at the same frame, through romdev on GL and WebGPU, at two and four players.
//
//   SKATE_OLD_CARTS=<dir with an older build's skate_auto.wasc> node tests/skate-onepass.mjs [--gpu gl|webgpu|gl,webgpu]
//
// Each case: skate_auto (bots skating Sunnyside, no HUD) on a fixed clock to frames 600 and 1400 in the new cart and the
// old one; the share of pixels more than 16 levels apart, away from the views' seams (a shared chain lays the views
// out before the tone curve, a chain a view after it, so the 3 pixels either side of each seam differ whatever the
// picture does), must be at most 0.3%. The new cart must report the frame drawn in one pass (`pipeline`: views_array
// true, views_array_why empty, no view hook). Controls, which must fail: SKATE_VIEWS_CONTROL=vignette (one vignette over
// the whole screen) and SKATE_VIEWS_CONTROL=masks (each view given the next one's sky dome and tokens).
import { existsSync, mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { call, ROOT, gpusArg } from './lib/romdev.mjs';
import { readPng } from './lib/png.mjs';

const OLD = process.env.SKATE_OLD_CARTS && resolve(process.env.SKATE_OLD_CARTS);
if (!OLD || !existsSync(join(OLD, 'skate_auto.wasc'))) { console.error('SKATE_OLD_CARTS=<dir with an older build\'s skate_auto.wasc>'); process.exit(2); }
const NEW = join(ROOT, 'build/cart-wgpu/carts');
const OUT = resolve(process.env.SKATE_ONEPASS_OUT || join(ROOT, 'build/skate-onepass'));
mkdirSync(OUT, { recursive: true });
const BOUND = 0.003, FRAMES = [600, 1400];
let seq = 0, bad = 0;

// the cart to each frame, a shot at each; then the pipeline's report
async function run(dir, gpu, players, control = '') {
  const session = `skate-onepass-${process.pid}-${++seq}`;
  const env = `env SKATE_PLAYERS=${players} SKATE_NO_HUD=1${control ? ` SKATE_VIEWS_CONTROL=${control}` : ''}`;
  const tag = `${dir === OLD ? 'old' : 'new'}-${gpu}-${players}p${control ? `-${control}` : ''}`;
  const loaded = await call('loadMedia', { platform: 'wasmcart', path: join(dir, 'skate_auto.wasc'), gpuPower: 'low-power', wgpu: gpu === 'webgpu', deterministicSeed: 1, firstCommand: env }, session);
  if (loaded.gpu !== gpu) throw new Error(`${tag}: asked for ${gpu}, runs on ${loaded.gpu}`);
  try {
    const shots = [];
    let at = 1;
    for (const f of FRAMES) {
      await call('frame', { op: 'step', frames: f - at }, session, { timeoutMs: 900000 });
      at = f;
      const file = join(OUT, `${tag}-${f}.png`);
      await call('frame', { op: 'screenshot', path: file }, session);
      shots.push(readPng(file));
    }
    const tokens = (await call('wasm', { op: 'read', name: 'skater.tokens' }, session));
    const pl = (await call('wasm', { op: 'command', text: 'pipeline', maxFrames: 8 }, session)).reply;
    return { tag, shots, pl, tokens: tokens && (tokens.values ?? tokens.value) };
  } finally {
    await call('host', { op: 'shutdown' }, session).catch(() => {});
  }
}

function offSeams(a, b) {
  const { w, h } = a, near = (v, m) => Math.abs(v - m) < 3;
  let n = 0, all = 0;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    if (near(y, h / 2) || near(x, w / 2) || y < 3 || x < 3 || y >= h - 3 || x >= w - 3) continue;
    all++;
    const i = (y * w + x) * 3;
    let d = 0;
    for (let c = 0; c < 3; c++) d = Math.max(d, Math.abs(a.rgb[i + c] - b.rgb[i + c]));
    if (d > 16) n++;
  }
  return n / all;
}
const pct = (x) => `${(100 * x).toFixed(4)}%`;

for (const gpu of gpusArg()) for (const players of [2, 4]) {
  const old = await run(OLD, gpu, players), now = await run(NEW, gpu, players);
  const one = now.pl && now.pl.views_array === true && now.pl.views_array_why === '' && now.pl.view_hook === false;
  console.log(`${one ? 'ok  ' : 'FAIL'} ${gpu} ${players}p: one pass (views_array ${now.pl && now.pl.views_array}, why "${now.pl && now.pl.views_array_why}", view_hook ${now.pl && now.pl.view_hook}, view_masks ${now.pl && now.pl.view_masks}); old views_array ${old.pl && old.pl.views_array} "${old.pl && old.pl.views_array_why}"; tokens new ${now.tokens} old ${old.tokens}`);
  if (!one) bad++;
  FRAMES.forEach((f, i) => {
    const d = offSeams(old.shots[i], now.shots[i]);
    console.log(`${d <= BOUND ? 'ok  ' : 'FAIL'} ${gpu} ${players}p frame ${f}: ${pct(d)} of pixels over 16 levels off the seams (bound ${pct(BOUND)})`);
    if (d > BOUND) bad++;
  });
  for (const control of ['vignette', 'masks']) {
    const c = await run(NEW, gpu, players, control);
    const worst = Math.max(...FRAMES.map((f, i) => offSeams(old.shots[i], c.shots[i])));
    const failed = worst > BOUND;
    console.log(`${failed ? 'ok  ' : 'FAIL'} ${gpu} ${players}p control ${control} fails as it must: ${pct(worst)}`);
    if (!failed) bad++;
  }
}
console.log(bad ? `skate-onepass: ${bad} failed` : 'skate-onepass: all passed');
process.exit(bad ? 1 : 0);
