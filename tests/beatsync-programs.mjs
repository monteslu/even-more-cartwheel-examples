// beatsync's program table: does the cart have a program for everything it draws? A material, light or effect drawn in a
// program state the game's table (games/beatsync/programs/beatsync.txt) lacks is NOT DRAWN, and the cart shows a red
// "N draw state(s) have no program" banner over the game. The table goes stale whenever a material, a light setup,
// the scenery or a preset path changes after the table was made, so this check plays the game the ways a player meets it
// and fails on any state it lacks, naming them:
//   - the autopilot cart (beatsync_auto) through its whole run (tests/beatsync-game.json autopilot.frames) at every graphics
//     preset, Very Low to Ultra (each preset draws states of its own), and switched between High and Very Low half
//     way, each way (content made for one preset drawn with the other's renderer), and on the three rungs Auto steps
//     through between presets (High with medium shadows, Medium without ambient occlusion, Low with very low effects);
//   - the autopilot cart and the play cart as a player's machine runs them: loaded with no seed, so the warm-up
//     spreads over frames under the loading screen and Auto picks the graphics, and the play cart goes through its
//     menus into play. A deterministic load does its warm-up inside one frame and never shows
//     what a player's first frames draw.
// Each run is a few romdev requests: load, the preset, ONE ranged step (or input sequence) for the whole run, one read.
//
// The CONTROL must fail: the autopilot run at High with one sphere put in front of its camera (the debug channel's
// spawn and set) in a material no game's table has (clearcoat, sheen, iridescence and anisotropy at once). A control
// that passes means the check cannot see a missing state.
//
// A table that lacks states is filled with the engine's tool: collect them (--collect <file>), then
//   node deps/cartwheel-sdk/linux-x64/tools/programs.mjs --table games/beatsync/programs/beatsync.txt --missing <file> --build build/native
//
//   node tests/beatsync-programs.mjs [--gpu gl|webgpu|both] [--no-control] [--collect <file>]
import { appendFileSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { gpusOf, openBeatsync } from './lib/beatsync.mjs';

const argv = process.argv.slice(2);
// --collect <file>: append every state a run lacked to the file, one a line
const COLLECT = argv.includes('--collect') ? resolve(argv[argv.indexOf('--collect') + 1]) : null;
const GPUS = gpusOf(argv);
const GAME = JSON.parse(readFileSync(join(ROOT, 'tests/beatsync-game.json'), 'utf8'));
const FRAMES = GAME.autopilot?.frames;
if (!(FRAMES > 0)) { console.error('tests/beatsync-game.json: autopilot.frames is missing'); process.exit(2); }
const cart = 'beatsync', autoCart = 'beatsync_auto';
const PRESETS = ['verylow', 'low', 'medium', 'high', 'ultra'];

/** the states a run drew without a program: the pipeline debug request's whole list (romdev's debugState listing cuts
 * a long bytes field short, which once fed the table half a state), else the cw.three.missing_states field */
async function missingStates(r) {
  const rep = await r.command('pipeline');
  const out = new Set(rep && typeof rep.missing_states === 'string' ? rep.missing_states.split('\n').map((l) => l.trim()).filter(Boolean) : missing(await r.fields()));
  // and every state the cart's log names as NOT DRAWN: a Backdrop's world's states reach the pipeline's list only from
  // cartwheel-engine 0.65.1 (before, the list held the last tree to change, and the worlds' draws went unreported)
  const ev = await r.call('wasm', { op: 'events' });
  for (const e of (ev && ev.log) || []) { const m = /program state (\S+) is in none/.exec(e.text || ''); if (m) out.add(m[1]); }
  return [...out];
}
function missing(f) {
  const v = f['cw.three.missing_states'];
  if (!v) return [];
  const bytes = Array.isArray(v) ? v : typeof v === 'string' && /^[0-9a-f]*$/i.test(v) ? v.match(/../g) || [] : null;
  const text = bytes ? String.fromCharCode(...bytes.map((b) => (typeof b === 'number' ? b : parseInt(b, 16))).filter((b) => b)) : String(v);
  return text.split('\n').map((l) => l.trim()).filter(Boolean);
}

/** the first Camera3D in the tree (the `tree` request's JSON, its root first) as a path under the root ("/Level/Camera") */
function findCamera(node, path = null) {
  if (!node || typeof node !== 'object') return null;
  const here = path === null ? '' : `${path}/${node.name}`;
  if (node.type === 'Camera3D') return here;
  for (const c of Array.isArray(node.children) ? node.children : []) {
    const p = findCamera(c, here);
    if (p !== null) return p;
  }
  return null;
}

/** one run: [label, the states it lacked, requests] */
async function run(gpu, { label, cartPath, seed = 1, preset = null, set = [], switchTo = null, menus = false, control = false }) {
  const r = await openBeatsync(cartPath, { gpu, seed, tag: 'programs' });
  try {
    if (preset) await r.command(`quality ${preset}`);
    for (const c of set) await r.command(`quality set ${c}`);
    // every visual style in turn (src/styles.c): each backdrop draws states of its own, at every preset
    await r.command('style tour');
    if (control) {
      const t = await r.command('tree / 64');
      const cam = findCamera(t && t.tree ? t.tree : t);
      if (cam === null) throw new Error('control: no Camera3D in the tree to put the sphere in front of');
      await r.command(`spawn MeshInstance3D ${cam || '/'} ProgramsControl`);
      for (const [k, v] of [['primitive', 'SPHERE'], ['position', '"vec3(0, 0, -2)"'], ['clearcoat', '1'], ['sheen', '1'], ['iridescence', '1'], ['anisotropy', '0.5']])
        await r.command(`set ${cam}/ProgramsControl ${k} ${v}`);
    }
    if (menus) {
      // the shell's screens with presses, as a player goes through them (the loading screen first, as long as the
      // machine makes it): Play on the title, a seat on the join screen, Start
      const tap = (b) => r.run([{ pads: [{ buttons: [b] }], frames: 6 }, { pads: [{ buttons: [] }], frames: 24 }]);
      let screen = -1, joined = false;
      for (let tries = 0; tries < 30 && screen !== 3; tries++) {
        screen = (await r.fields())['shell.screen'];
        if (screen === 1) await tap('south');
        else if (screen === 2) { await tap(joined ? 'start' : 'south'); joined = true; }
        else if (screen !== 3) await r.run([{ pads: [{ buttons: [] }], frames: 30 }]);
      }
      if (screen !== 3) throw new Error('programs: the game did not reach play through its menus');
      await r.run([{ pads: [{ buttons: [] }], frames: 600 }]);
    } else if (switchTo) { /* the preset changed half way through the run, as a player's settings or Auto do */
      await r.stepTo(Math.round(FRAMES / 2));
      await r.command(`quality ${switchTo}`);
      await r.stepTo(FRAMES);
    } else await r.stepTo(FRAMES);
    return [label, await missingStates(r), 0];
  } finally {
    await r.close();
  }
}

let failed = 0, requests = 0;
for (const gpu of GPUS) {
  const runs = [
    ...PRESETS.map((p) => ({ label: `autopilot at ${p}`, cartPath: autoCart, preset: p })),
    // Auto's rungs between the presets (src/render/quality.c LADDER): a player on Auto steps through them
    { label: 'autopilot on Auto\'s High rung with medium shadows', cartPath: autoCart, preset: 'high', set: ['shadows medium'] },
    { label: 'autopilot on Auto\'s Medium rung without ambient occlusion', cartPath: autoCart, preset: 'medium', set: ['ssao off'] },
    { label: 'autopilot on Auto\'s Low rung with very low effects', cartPath: autoCart, preset: 'low', set: ['effects verylow'] },
    // content made for one preset drawn with another's renderer: the two farthest apart, each way
    { label: 'autopilot from high, switched to verylow half way', cartPath: autoCart, preset: 'high', switchTo: 'verylow' },
    { label: 'autopilot from verylow, switched to high half way', cartPath: autoCart, preset: 'verylow', switchTo: 'high' },
    { label: 'autopilot as a player runs it (no seed: the loading screen\'s warm-up, Auto graphics)', cartPath: autoCart, seed: null },
    { label: 'the game through its menus as a player runs it (no seed)', cartPath: cart, seed: null, menus: true },
  ];
  const lacked = new Set();
  for (const spec of runs) {
    const [label, states, calls] = await run(gpu, spec);
    requests += calls;
    if (states.length) {
      console.log(`FAIL programs (${gpu}) ${label}: ${states.length} draw state(s) have no program, so they are NOT DRAWN:\n    ${states.join('\n    ')}`);
      states.forEach((s) => lacked.add(s));
    } else console.log(`ok   programs (${gpu}) ${label}: every state drawn`);
  }
  if (COLLECT) appendFileSync(COLLECT, [...lacked].map((x) => x + '\n').join(''));
  if (lacked.size) {
    console.log(`FAIL programs (${gpu}): the table lacks ${lacked.size} state(s). Fix: --collect <file>, then the engine's tools/programs.mjs --table games/beatsync/programs/beatsync.txt --missing <file> (this file's head)`);
    failed++;
  }
  if (!argv.includes('--no-control')) {
    const [, states, calls] = await run(gpu, { label: 'control', cartPath: autoCart, preset: 'high', control: true });
    requests += calls;
    if (states.length) console.log(`ok   programs control (${gpu}): a sphere in a material no table has is reported missing, as it must (${states.length} state(s))`);
    else { console.log(`FAIL programs control (${gpu}): a sphere in a material no table has drew no missing state: the check cannot see one`); failed++; }
  }
}
console.log(failed ? `beatsync-programs: ${failed} failed` : 'beatsync-programs: ok');
process.exit(failed ? 1 : 0);
