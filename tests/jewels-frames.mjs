// jewels frame sequences: every tick of a stretch of play, captured over the debug channel (natively, headless). The
// app starts paused, runs `from` ticks, then steps one tick at a time and takes a screenshot after each, box-filtered
// down by `scale`. The frames are the game's own, tick by tick (the effects run on the fixed tick); between two ticks
// the pipeline renders one more frame than a free-running game would, so the temporal resolve settles a little faster.
//   import { captureTicks } from './jewels-frames.mjs';
//   const frames = await captureTicks({ app: 'jewels_bench', from: 600, count: 360, scale: 4, gpu: 'gl', env: {}, dir });
//   node tests/jewels-frames.mjs <app> <from> <count> <scale> <dir> [gl|webgpu] [KEY=VALUE ...]   (PNGs into dir)
// The CLI holds the GPU suite lock (the engine's tools/with-gpu-suite.mjs).
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');

/** @returns {Promise<string[]>} the PNG paths, one a tick */
export async function captureTicks({ app = 'jewels_bench', from, count, scale = 4, gpu = 'gl', env = {}, dir, every = 1 }) {
  const { launchApp } = await import(join(ENGINE, 'tools/lib/edsession.mjs'));
  const { wgpuPin, glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
  const exe = JSON.parse(readFileSync(join(ROOT, `build/native/apps/${app}.json`), 'utf8')).binary;
  mkdirSync(dir, { recursive: true });
  const e = { CW_WGPU_VALIDATION: '0', CW_WARMUP_SPREAD: '0', CW_START_PAUSED: '1', CW_FRAMES: '10000000', JEWELS_NO_HUD: '1', ...env };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(e, pin.env, { CW_GPU: 'webgpu' });
  else { e.CW_GPU = 'gl'; if (gl) Object.assign(e, gl.env); }
  const full = { ...process.env, ...e };
  scrubDisplay(full);
  const s = await launchApp(exe, { env: full, timeoutMs: 120000 });
  const out = [];
  try {
    const st = await s.request('status', 60000);
    if (st.error) throw new Error(st.error);
    let left = from;
    while (left > 0) {
      const n = Math.min(left, 120);
      const r = await s.request(`step ${n}`, 120000);
      if (r.error) throw new Error(r.error);
      left -= n;
    }
    for (let i = 0; i < count; i++) {
      const r = await s.request(`step ${every}`, 60000);
      if (r.error) throw new Error(r.error);
      const p = join(dir, `t${String(from + (i + 1) * every).padStart(5, '0')}.png`);
      await s.screenshot(`all ${scale}`, p); // edsession asks `screenshot <layers> 1`: the scale rides in the layers argument
      out.push(p);
    }
  } finally {
    await s.close();
  }
  return out;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  if (!process.env.CW_GPU_SUITE_INNER) {
    const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'jewels-frames', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
      { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
    process.exit(r.status ?? 1);
  }
  const [app, from, count, scale, dir, gpu = 'gl', ...kv] = process.argv.slice(2);
  const env = {};
  for (const p of kv) { const i = p.indexOf('='); env[p.slice(0, i)] = p.slice(i + 1); }
  const files = await captureTicks({ app, from: +from, count: +count, scale: +scale, gpu, env, dir: resolve(dir) });
  console.log(`${files.length} frames in ${resolve(dir)}`);
}
