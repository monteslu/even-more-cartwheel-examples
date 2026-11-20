// towers: the stars. Two checks, each with a control that must fail.
//
//  1. An old save keeps its maps (native, software GL, no GPU lock, a few frames). The save from before the stars is
//     the shell's block with an 8-byte game struct ({ best_map, maps_won }, version 1); the game now asks for that
//     struct with the stars appended. It must load with best_map 7 and maps_won 9 kept and no stars. Control
//     (TOWERS_SAVE_VERSION=2): the game asks for another version with no migrate hook, the shell starts the game's
//     data over, and the check must fail. (CW_PLAYER_TIMING: a CW_FRAMES run is otherwise deterministic and starts
//     on an empty save.)
//  2. A map held earns the stars its core earned, and they are saved (romdev, the auto cart, on GL and WebGPU). Map 1
//     from its last wave with towers on every pad (TOWERS_WAVE=10) and the core started at 20, 14 and 6
//     (TOWERS_CORE): 3, 2 and 1 stars, the save's byte for map 1 the same. Control (TOWERS_STARS_NO_SAVE=1, core 20):
//     the stars are shown but never written, so the saved byte is 0 and the check must fail.
//
//   node tests/towers-stars.mjs [--gpu gl|webgpu|gl,webgpu] [--no-romdev]
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT, call, gpusArg, openCart } from './lib/romdev.mjs';

const OUT = join(ROOT, 'out', 'towers-stars');
mkdirSync(OUT, { recursive: true });
let bad = 0;
const say = (ok, msg) => { if (!ok) bad++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); };

// ── 1: the old save ──────────────────────────────────────────────────
// The shell's block (cartwheel-engine src/shell/shell.c): magic "WSHL", shell version 1, game version, game size, an
// FNV-1a checksum of everything from the settings on (the whole block), the settings, then the game's struct at 32.
const GAME_MAX = 6144, GAME_AT = 32;
function oldSave(file) {
  const b = Buffer.alloc(GAME_AT + GAME_MAX);
  b.writeUInt32LE(0x4c485357, 0);
  b.writeUInt16LE(1, 4);
  b.writeUInt16LE(1, 6); // game version 1
  b.writeUInt32LE(8, 8); // { best_map, maps_won }
  b[17] = 6, b[18] = 7; // music and effects volumes (not the defaults)
  b.writeInt32LE(7, GAME_AT), b.writeInt32LE(9, GAME_AT + 4);
  b.fill(0xff, GAME_AT + 8, GAME_AT + 8 + 64); // where the stars now go: the shell must zero what the old size lacked
  let h = 2166136261;
  for (let i = 16; i < b.length; i++) h = Math.imul(h ^ b[i], 16777619) >>> 0;
  b.writeUInt32LE(h, 12);
  writeFileSync(file, b);
}
function nativeRun(extra) {
  const dir = join(ROOT, 'build/native/games/towers'), bin = join(dir, 'towers'), state = join(OUT, 'state.json'), save = join(OUT, 'save.dat');
  rmSync(state, { force: true });
  oldSave(save);
  const env = { ...process.env, SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', LIBGL_ALWAYS_SOFTWARE: '1', CW_GPU: 'gl', CW_TEST_GPU: 'software',
    CW_FRAMES: '5', CW_PLAYER_TIMING: '1', CW_DEBUG_DUMP: state, CW_ASSETS: bin + '.assets', CW_SAVE_FILE: save, ...extra };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY']) delete env[k];
  const r = spawnSync(bin, [], { cwd: dir, env, encoding: 'utf8', timeout: 300000 });
  let s;
  try { s = JSON.parse(readFileSync(state, 'utf8')); } catch { return { err: `no state (exit ${r.status}): ${(r.stderr || '').split('\n').slice(-5).join(' | ')}` }; }
  const f = (k) => s[k] ?? s.fields?.[k];
  return { best: f('game.best_map'), won: f('game.maps_won'), stars: f('game.stars_total') };
}
const keeps = (r) => r.best === 7 && r.won === 9 && r.stars === 0;
{
  const r = nativeRun({});
  if (r.err) say(false, `old save: ${r.err}`);
  else say(keeps(r), `old save keeps its maps: best_map ${r.best}, maps_won ${r.won}, stars ${r.stars} (want 7, 9, 0)`);
  const c = nativeRun({ TOWERS_SAVE_VERSION: '2' });
  if (c.err) say(false, `control: ${c.err}`);
  else say(!keeps(c), `control (another save version, no migrate hook): best_map ${c.best}, maps_won ${c.won} (must differ)`);
}

// ── 2: stars earned and saved, through romdev ───────────────────────────
const STAR_AT = GAME_AT + 8; // stars[0]: map 1
async function hold(gpu, core, extraEnv = '') {
  const h = await openCart('towers_auto', { gpu, seed: null, tag: `stars-${core}`, load: { firstCommand: `env TOWERS_WAVE=10 TOWERS_CORE=${core}${extraEnv}` } });
  try {
    await h.command('map 1', 30);
    const r = await call('frame', { op: 'step', frames: 6000, stopWhen: { field: 'game.state', atLeast: 1, every: 10 }, fields: ['game.state', 'game.core', 'game.won_stars', 'game.stars_total'] }, h.session, { timeoutMs: 900000 });
    await h.step(4); // the stars are counted and the save written on the win's first tick after it
    const stars = await h.read('game.won_stars');
    const info = (await call('wasm', { op: 'info' }, h.session)).info; // the save the host keeps: the cart's declared save bytes
    const sv = await call('wasm', { op: 'read', offset: info.savePtr + STAR_AT, length: 1 }, h.session);
    const saved = sv && sv.hex ? parseInt(sv.hex, 16) : -1;
    return { state: r.fields['game.state'], core: r.fields['game.core'], stars, saved, raw: sv && sv.hex ? '' : JSON.stringify(sv).slice(0, 300) };
  } finally { await h.close(); }
}
if (!process.argv.includes('--no-romdev')) {
  for (const gpu of gpusArg()) {
    for (const [core, want] of [[20, 3], [14, 2], [6, 1]]) {
      const r = await hold(gpu, core);
      if (r.raw) console.log(`     (save reply: ${r.raw})`);
      say(r.state === 1 && r.stars === want && r.saved === want, `${gpu}: map 1 held from core ${core}: core ${r.core}, ${r.stars} stars, saved ${r.saved} (want held, ${want}, ${want})`);
    }
    const c = await hold(gpu, 20, ' TOWERS_STARS_NO_SAVE=1');
    say(c.state === 1 && c.saved !== 3, `${gpu}: control (stars never written): saved ${c.saved} (must not be 3)`);
  }
}
rmSync(OUT, { recursive: true, force: true });
console.log(bad ? `FAIL towers-stars: ${bad}` : 'ok   towers-stars');
process.exit(bad ? 1 : 0);
