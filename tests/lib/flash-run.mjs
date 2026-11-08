// The games' flash checks: one native run's every frame (the engine's CW_FRAME_DUMP, box-filtered in linear light)
// measured by the engine's flash analyser (deps/cartwheel-engine/tools/lib/flash.mjs: WCAG 2.2 success criterion
// 2.3.1 and the Harding test's thresholds, general and red flashes over a sixth by a sixth of the screen, small lights
// switching together over a quarter of a 10-degree field, regular flicker from 3.5 Hz, and a run that never moves
// proves nothing). Each check is the engine's gate (flashGate: the run passes and the same frames under an 8 Hz
// full-screen strobe fail), and a game adds its own controls (its old effects, built or switched back in) that must
// fail the same judge.
//
//   import { captureRun, gate, mustFail } from './lib/flash-run.mjs';
//   const run = await captureRun({ exe, from: 300, frames: 540, env: { GAME_VAR: '1' } });
//   bad += gate('the bench', run) ? 0 : 1;
//   bad += mustFail('the old lights', await captureRun({ exe: oldExe, from: 300, frames: 540 })) ? 0 : 1;
//
// CW_FLASH_KEEP=<dir> keeps each run's frames there (a CW_FRAME_DUMP file: tools/flash-check.mjs --dump reads it).
//
// The captures need the GPU: a test calls inSuiteLock() first, which re-runs it inside one hold of the engine's
// GPU suite lock (tools/with-gpu-suite.mjs) and exits with its status.
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, renameSync, rmSync } from 'node:fs';
import { basename, join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
export const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const lib = await import(join(ENGINE, 'tools/lib/flash.mjs'));
export const { flashGate, analyseFrames, judgeFlash, describeFlash, readFrameDump, withStrobe } = lib;

/** re-run the calling script inside one hold of the GPU suite lock (returns only when already inside) */
export function inSuiteLock(name, script, argv = process.argv.slice(2)) {
  if (process.env.CW_GPU_SUITE_INNER) return;
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), name, 'node', script, ...argv],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}

/** an app's native binary (build/native/apps/<app>.json) */
export function appBinary(app) {
  const f = join(ROOT, 'build/native/apps', `${app}.json`);
  if (!existsSync(f)) throw new Error(`${app} is not built natively (${f}): scripts/build.sh ${app}`);
  return JSON.parse(readFileSync(f, 'utf8')).binary;
}

/**
 * Run a native app for `frames` frames on the fixed 1/60 s clock and keep every frame from `from` on.
 * @param {{exe?: string, app?: string, from: number, frames: number, env?: object, gpu?: 'gl'|'webgpu', scale?: number,
 *          name?: string, timeoutMs?: number}} o
 * @returns {Promise<{w: number, h: number, frames: Uint8Array[], status: number}>}
 */
export async function captureRun(o) {
  const exe = o.exe || appBinary(o.app);
  const { glPin, wgpuPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
  const dir = join(ROOT, 'out/flash');
  mkdirSync(dir, { recursive: true });
  const dump = join(dir, `${o.name || o.app || 'run'}-${process.pid}-${Date.now()}.cwfd`);
  rmSync(dump, { force: true });
  const env = { ...process.env, CW_WARMUP_SPREAD: '0', SDL_AUDIODRIVER: 'dummy', ...(o.env || {}),
    CW_FRAMES: String(o.frames), CW_FRAME_DUMP: dump, CW_FRAME_DUMP_FROM: String(o.from), CW_FRAME_DUMP_SCALE: String(o.scale || 8) };
  delete env.CW_SCREENSHOT;
  const pin = o.gpu === 'webgpu' ? wgpuPin() : null;
  if (o.gpu === 'webgpu' && !pin) throw new Error('no WebGPU adapter to pin');
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; const gl = glPin(); if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const status = await new Promise((ok) => {
    const p = spawn(exe, [], { stdio: ['ignore', 'ignore', 'pipe'], env });
    let err = '';
    p.stderr.on('data', (d) => { err = (err + d).slice(-4000); });
    const t = setTimeout(() => p.kill('SIGKILL'), o.timeoutMs || Math.max(180000, o.frames * 500));
    p.on('exit', (c) => { clearTimeout(t); if (c !== 0) console.log(`     (${exe} exited ${c}: ${err.trim().split('\n').slice(-3).join(' | ')})`); ok(c ?? 1); });
  });
  if (!existsSync(dump)) throw new Error(`${exe}: no frames dumped (exit ${status})`);
  const data = await readFrameDump(dump);
  if (process.env.CW_FLASH_KEEP) { mkdirSync(resolve(process.env.CW_FLASH_KEEP), { recursive: true }); renameSync(dump, join(resolve(process.env.CW_FLASH_KEEP), basename(dump))); } else rmSync(dump, { force: true });
  const want = o.frames - o.from + 1;
  if (status !== 0 || data.frames.length !== want) throw new Error(`${exe}: ${data.frames.length} frames dumped, want ${want} (exit ${status})`);
  return { ...data, status };
}

/** the reasons a run fails the engine's judge (a frozen run is not one: a control may hold still) */
const fails = (run, opts) => { const m = analyseFrames(run.w, run.h, run.frames, opts); return { m, why: judgeFlash(m).filter((p) => !/proves nothing/.test(p)) }; };

/**
 * The engine's gate on one run (it passes; the same frames under an 8 Hz full-screen strobe fail): true if it holds.
 * fine: also hold the run to the same bounds on a fine grid of that many cells across (the regions a 24th of the
 * screen instead of a sixth), past what WCAG asks: a game that keeps every small light from strobing too.
 */
export function gate(label, run, opts = {}, { fine = 0 } = {}) {
  const g = flashGate(run.w, run.h, run.frames, opts, { label });
  console.log(g.lines.join('\n'));
  if (!fine) return g.ok;
  const f = fails(run, { ...opts, grid: fine });
  console.log(`${f.why.length ? 'FAIL' : 'ok  '} ${label}, on a ${fine} x ${fine} grid: ${describeFlash(f.m)}${f.why.length ? '\n     ' + f.why.join('\n     ') : ''}`);
  return g.ok && !f.why.length;
}

/** a game's own control: frames that strobe (the old effects) must fail the same judge (on the fine grid too, when the
 * gate has one: either failing is enough); true if they do */
export function mustFail(label, run, opts = {}, { fine = 0 } = {}) {
  const a = fails(run, opts), b = fine ? fails(run, { ...opts, grid: fine }) : { why: [] };
  const why = [...a.why, ...b.why.map((p) => `(${fine} x ${fine} grid) ${p}`)];
  if (why.length) console.log(`ok   control fails as it must (${label}): ${why[0]}\n     ${describeFlash(a.m)}`);
  else console.log(`CONTROL PASSED (a failure): ${label} went unseen: ${describeFlash(a.m)}${fine ? '; on the fine grid ' + describeFlash(b.m) : ''}`);
  return why.length > 0;
}
