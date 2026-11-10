// towers perf gate through romdev: the native gate (tests/towers-perf.mjs) measured on the built cart instead of a native
// run, with romdev's frame({op:'timing'}) on the server's pinned test GPU (the 890M), GL and WebGPU, through the
// engine's tools/lib/romdev-timing.mjs (engine v0.45.0 or later).
//
// towers_bench (map 26, towers at their third level on every slot, 2400 creeps kept on its paths, four bots) at
// 1920x1080, High, HUD off (the debug channel's env request: TOWERS_NO_HUD). 240 frames of warmup, then three
// 120-frame windows; the bound is the mean of the three windows' GPU time, per map and per API (BOUNDS below), each
// set about 10% over the worst of three clean romdev runs on engine v0.46.2 (a run romdev reported as noisy was run
// again and not counted; the bakes hold on the cart since then, so every map reads under the older numbers). The
// clean means in ms, and the bound:
//   map 26: GL 5.33 4.44 4.44 (5.9)   WebGPU 4.29 4.27 4.22 (4.8)
//   map 5:  GL 4.78 4.75 4.79 (5.3)   WebGPU 5.23 4.74 4.73 (5.8)
//   map 10: GL 4.78 4.76 4.90 (5.4)   WebGPU 4.57 4.64 4.78 (5.3)
//   map 15: GL 4.64 4.81 4.72 (5.3)   WebGPU 4.53 5.08 4.69 (5.6)
//   map 20: GL 4.71 4.64 4.64 (5.2)   WebGPU 4.47 4.83 4.56 (5.4)
//   map 30: GL 4.71 4.92 4.59 (5.5)   WebGPU 4.51 4.72 4.45 (5.2)
// No map is markedly costlier on one API than on the other (the widest gap, map 26, is 0.2 ms); the slowest clean
// runs (map 5 WebGPU 5.23, map 15 WebGPU 5.08) ran with the 890M's clock at 1170 to 1250 MHz against 1500 to 1900
// on the rest, so a run whose mean clock is under CLOCK_MHZ is run again like a noisy one (an integrated GPU's clock
// follows its load and the machine's power state: the engine's docs/guides/profiling.md).
// romdev's GPU time is the host's around the whole frame (WebGPU: the union of every pass's timestamps; GL: a
// GL_TIME_ELAPSED query around the frame), so it includes the frame's UI and final copy, which the native gate's pass
// sums leave out. Every window must keep at least 2000 creeps alive and drawn (game.enemies, view.drawn), skip no draw
// (render.skipped) and miss no program (cw.three.missing). A run romdev reports as noisy (another process on the GPU,
// a playtest window) or whose clock was low is run again, up to three times; a run still so after that fails, naming
// romdev's reasons. Every FAIL line ends with every reason it fails for. The GPU clock is printed beside every time.
// Controls, which must fail: GL at 3840x2160 (romdev's preferred size, a load with no seed: four times the pixels),
// WebGPU against an impossible 1 ms, and the crowd check held to 3000 creeps (the bench keeps 2400).
//
//   node tests/towers-perf-romdev.mjs [--cart build/cart-wgpu/carts/towers_bench.wasc] [--quick] [--maps 5,20] [--gpu gl]
// (--quick: map 26 only, then the controls; otherwise also one late map of every other theme; --maps: only these maps
// of the full list, --gpu: one API; the controls run with every selection). ROMDEV_URL picks the server (default
// 127.0.0.1:7331); CW_ENGINE_DIR an engine checkout to take the timing client from.
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
const GPUS = opt('gpu', 'gl,webgpu').split(',');
// the bound per map (ms), GL and WebGPU: about 10% over the worst clean mean of three runs (the header's table)
const BOUNDS = {
  26: { gl: 5.9, webgpu: 4.8 },
  5: { gl: 5.3, webgpu: 5.8 },
  10: { gl: 5.4, webgpu: 5.3 },
  15: { gl: 5.3, webgpu: 5.6 },
  20: { gl: 5.2, webgpu: 5.4 },
  30: { gl: 5.5, webgpu: 5.2 },
};
const CLOCK_MHZ = 1400; // a run with the GPU's mean clock under this is run again (the header)
const MAPS = (quick ? [] : opt('maps', '5,10,15,20,30').split(',').map(Number)).filter((m) => m !== 26);
for (const m of MAPS) if (!BOUNDS[m]) { console.error(`map ${m} has no bound (BOUNDS)`); process.exit(2); }
const CROWD = 2000;
const FIELDS = ['game.enemies', 'view.drawn', 'render.skipped', 'cw.three.missing', 'game.map'];
const label = (gpu) => (gpu === 'gl' ? 'GL' : 'WebGPU');

async function run(gpu, { map, size } = {}) {
  // the HUD switch is read from the first frame: it rides on the load (romdev's firstCommand)
  const commands = ['test_run', ...(map ? [`map ${map}`] : [])];
  let last = null;
  for (let tries = 0; tries < 3; tries++) {
    const t = await romdevTiming({ cart: CART, gpu, firstCommand: 'env TOWERS_NO_HUD=1', commands, warmup: 240, frames: 360, window: 120, debugFields: FIELDS,
      ...(size ? { seed: null, preferredSize: size } : {}) });
    last = t;
    const clock = t.gpu && t.gpu.clockMhz && t.gpu.clockMhz.mean;
    t.lowClock = clock > 0 && clock < CLOCK_MHZ;
    if (!t.contention.noisy && !t.lowClock) break;
    console.log(`     (${t.contention.noisy ? 'noisy' : 'clock-bound'}, run again: ${[...t.contention.reasons, ...(t.lowClock ? [`mean clock ${clock} MHz under ${CLOCK_MHZ}`] : [])].join('; ')})`);
  }
  const t = last, w = t.windows || [];
  const f = (n, k) => Math.min(...w.map((x) => (x.fields && x.fields[n] ? x.fields[n][k] : NaN)));
  return {
    gpu: t.gpuMs && t.gpuMs.mean, windows: w.map((x) => x.gpuMs), worst: t.gpuMs && t.gpuMs.worstFrame && t.gpuMs.worstFrame.gpuMs,
    crowd: f('game.enemies', 'min'), drawn: f('view.drawn', 'min'),
    skipped: Math.max(...w.map((x) => (x.fields && x.fields['render.skipped'] ? x.fields['render.skipped'].max : 0))),
    missing: Math.max(...w.map((x) => (x.fields && x.fields['cw.three.missing'] ? x.fields['cw.three.missing'].max : 0))),
    map: t.fields && t.fields['game.map'] && t.fields['game.map'].last, size: t.size, clock: t.gpu && t.gpu.clockMhz,
    wall: t.wallMs && t.wallMs.mean, cpu: t.cpuMs && t.cpuMs.mean, noisy: t.contention.noisy, noisyWhy: t.contention.reasons || [], lowClock: t.lowClock, gpuSeen: t.gpuSeen,
  };
}

let bad = 0;
/** every reason the run fails the bound and the scene checks (empty: it passes) */
const reasons = (r, bound, crowd) => {
  const why = [];
  if (!(r.gpu > 0)) why.push('no GPU time');
  else if (r.gpu > bound) why.push(`GPU ${r.gpu.toFixed(2)} ms over the bound ${bound}`);
  if (!(r.crowd >= crowd)) why.push(`crowd ${r.crowd} under ${crowd}`);
  if (!(r.drawn >= crowd)) why.push(`drawn ${r.drawn} under ${crowd}`);
  if (r.skipped) why.push(`${r.skipped} skipped draws`);
  if (r.missing) why.push(`${r.missing} missing programs`);
  if (r.noisy) why.push(`noisy after three runs (${r.noisyWhy.join('; ') || 'romdev gave no reason'})`);
  if (r.lowClock) why.push(`clock-bound after three runs (mean clock ${r.clock && r.clock.mean} MHz under ${CLOCK_MHZ})`);
  return why;
};
const check = (name, r, bound, { mustFail = false, crowd = CROWD } = {}) => {
  const why = reasons(r, bound, crowd), within = why.length === 0;
  const line = `${name}: GPU ${r.gpu ? r.gpu.toFixed(2) : '?'} ms (bound ${bound}; windows ${r.windows.map((x) => x && x.toFixed(2)).join(' ')}; worst frame ${r.worst}), `
    + `${r.crowd} creeps (${r.drawn} drawn), map ${r.map}, ${r.size && `${r.size.width}x${r.size.height}`}, clock ${r.clock && r.clock.mean} MHz (${r.clock && r.clock.min}-${r.clock && r.clock.max}), `
    + `wall ${r.wall && r.wall.toFixed(1)} ms, CPU ${r.cpu && r.cpu.toFixed(1)} ms`;
  if (mustFail) {
    if (within) { console.log(`CONTROL PASSED (a failure): ${line}`); bad++; } else console.log(`ok   control fails as it must: ${line}; fails for: ${why.join(', ')}`);
  } else if (!within) { console.log(`FAIL ${line}; fails for: ${why.join(', ')}`); bad++; } else console.log(`ok   ${line}`);
};
const base = {};
for (const gpu of GPUS) check(`bench map 26 ${label(gpu)}`, base[gpu] = await run(gpu), BOUNDS[26][gpu]);
for (const map of MAPS) for (const gpu of GPUS) check(`map ${map} ${label(gpu)}`, await run(gpu, { map }), BOUNDS[map][gpu]);
if (GPUS.includes('gl')) check('GL at 3840x2160 (4 times the pixels)', await run('gl', { size: { width: 3840, height: 2160 } }), BOUNDS[26].gl, { mustFail: true });
const w = base.webgpu || base.gl, wb = base.webgpu ? BOUNDS[26].webgpu : BOUNDS[26].gl;
check(`${base.webgpu ? 'WebGPU' : 'GL'} against 1 ms`, w, 1, { mustFail: true });
check('the crowd check held to 3000 creeps', w, wb, { mustFail: true, crowd: 3000 });
process.exit(bad ? 1 : 0);
