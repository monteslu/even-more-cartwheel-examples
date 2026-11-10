// towers perf gate through romdev: the native gate (tests/towers-perf.mjs) measured on the built cart instead of a native
// run, with romdev's frame({op:'timing'}) on the server's pinned test GPU (the 890M), GL and WebGPU, through the
// engine's tools/lib/romdev-timing.mjs (engine v0.45.0 or later).
//
// towers_bench (map 26, towers at their third level on every slot, 2400 creeps kept on its paths, four bots) at
// 1920x1080, High, HUD off (the debug channel's env request: TOWERS_NO_HUD). 240 frames of warmup, then three
// 120-frame windows; the bound is the mean of the three windows' GPU time at 4.3 ms on GL and 4.5 on WebGPU, as
// natively. romdev's GPU time is the host's around the whole frame (WebGPU: the union of every pass's timestamps; GL: a
// GL_TIME_ELAPSED query around the frame), so it includes the frame's UI and final copy, which the native gate's pass
// sums leave out. Every window must keep at least 2000 creeps alive and drawn (game.enemies, view.drawn), skip no draw
// (render.skipped) and miss no program (cw.three.missing). A run romdev reports as noisy (another process on the GPU,
// a playtest window) is run again, up to three times. The GPU clock is printed beside every time.
// Controls, which must fail: GL at 3840x2160 (romdev's preferred size, a load with no seed: four times the pixels),
// WebGPU against an impossible 1 ms, and the crowd check held to 3000 creeps (the bench keeps 2400).
//
//   node tests/towers-perf-romdev.mjs [--cart build/cart-wgpu/carts/towers_bench.wasc] [--quick]
// (--quick: map 26 only, then the controls; otherwise also one late map of every other theme). ROMDEV_URL picks the
// server (default 127.0.0.1:7331); CW_ENGINE_DIR an engine checkout to take the timing client from.
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = resolve(process.env.CW_ENGINE_DIR || join(ROOT, 'deps/cartwheel-engine'));
const LIB = join(ENGINE, 'tools/lib/romdev-timing.mjs');
if (!existsSync(LIB)) { console.error(`${LIB} is missing: the romdev gate needs cartwheel-engine v0.45.0 or later (or CW_ENGINE_DIR)`); process.exit(2); }
const { romdevTiming } = await import(pathToFileURL(LIB).href);
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const CART = resolve(opt('cart', join(ROOT, 'build/cart-wgpu/carts/towers_bench.wasc')));
if (!existsSync(CART)) { console.error(`no cart ${CART} (build the cart tree)`); process.exit(2); }
const quick = argv.includes('--quick');
const BOUND = 4.3, BOUND_WGPU = 4.5, CROWD = 2000;
const FIELDS = ['game.enemies', 'view.drawn', 'render.skipped', 'cw.three.missing', 'game.map'];

async function run(gpu, { map, size } = {}) {
  // the HUD switch is read from the first frame: it rides on the load (romdev's firstCommand)
  const commands = ['test_run', ...(map ? [`map ${map}`] : [])];
  let last = null;
  for (let tries = 0; tries < 3; tries++) {
    const t = await romdevTiming({ cart: CART, gpu, firstCommand: 'env TOWERS_NO_HUD=1', commands, warmup: 240, frames: 360, window: 120, debugFields: FIELDS,
      ...(size ? { seed: null, preferredSize: size } : {}) });
    last = t;
    if (!t.contention.noisy) break;
    console.log(`     (noisy, run again: ${t.contention.reasons.join('; ')})`);
  }
  const t = last, w = t.windows || [];
  const f = (n, k) => Math.min(...w.map((x) => (x.fields && x.fields[n] ? x.fields[n][k] : NaN)));
  return {
    gpu: t.gpuMs && t.gpuMs.mean, windows: w.map((x) => x.gpuMs), worst: t.gpuMs && t.gpuMs.worstFrame && t.gpuMs.worstFrame.gpuMs,
    crowd: f('game.enemies', 'min'), drawn: f('view.drawn', 'min'),
    skipped: Math.max(...w.map((x) => (x.fields && x.fields['render.skipped'] ? x.fields['render.skipped'].max : 0))),
    missing: Math.max(...w.map((x) => (x.fields && x.fields['cw.three.missing'] ? x.fields['cw.three.missing'].max : 0))),
    map: t.fields && t.fields['game.map'] && t.fields['game.map'].last, size: t.size, clock: t.gpu && t.gpu.clockMhz,
    wall: t.wallMs && t.wallMs.mean, cpu: t.cpuMs && t.cpuMs.mean, noisy: t.contention.noisy, gpuSeen: t.gpuSeen,
  };
}

let bad = 0;
const check = (name, r, bound, { mustFail = false, crowd = CROWD } = {}) => {
  const within = r.gpu > 0 && r.gpu <= bound && r.crowd >= crowd && r.drawn >= crowd && !r.skipped && !r.missing && !r.noisy;
  const line = `${name}: GPU ${r.gpu ? r.gpu.toFixed(2) : '?'} ms (bound ${bound}; windows ${r.windows.map((x) => x && x.toFixed(2)).join(' ')}; worst frame ${r.worst}), `
    + `${r.crowd} creeps (${r.drawn} drawn), map ${r.map}, ${r.size && `${r.size.width}x${r.size.height}`}, clock ${r.clock && r.clock.mean} MHz (${r.clock && r.clock.min}-${r.clock && r.clock.max}), `
    + `wall ${r.wall && r.wall.toFixed(1)} ms, CPU ${r.cpu && r.cpu.toFixed(1)} ms${r.skipped ? ', SKIPPED DRAWS' : ''}${r.missing ? ', MISSING PROGRAMS' : ''}${r.noisy ? ', NOISY' : ''}`;
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}`);
  } else if (!within) { console.log(`FAIL ${line}`); bad++; } else console.log(`ok   ${line}`);
};
const g = await run('gl'), w = await run('webgpu');
check('bench map 26 GL', g, BOUND);
check('bench map 26 WebGPU', w, BOUND_WGPU);
if (!quick)
  for (const map of [5, 10, 15, 20, 30]) for (const gpu of ['gl', 'webgpu']) check(`map ${map} ${gpu === 'gl' ? 'GL' : 'WebGPU'}`, await run(gpu, { map }), gpu === 'gl' ? BOUND : BOUND_WGPU);
check('GL at 3840x2160 (4 times the pixels)', await run('gl', { size: { width: 3840, height: 2160 } }), BOUND, { mustFail: true });
check('WebGPU against 1 ms', w, 1, { mustFail: true });
check('the crowd check held to 3000 creeps', w, BOUND_WGPU, { mustFail: true, crowd: 3000 });
process.exit(bad ? 1 : 0);
