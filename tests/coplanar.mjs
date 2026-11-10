// No surface fights another for the depth test. Two faces in one plane, facing the same way and overlapping (a trim
// laid flush on a wall, a deck on a platform top, boxes sharing a face) have no depth order: the depth test picks one
// or the other pixel by pixel, differently as the camera moves, and patches of the picture swap between the two
// surfaces' look frame to frame. The games build their worlds in code, so every level is checked as it is built.
//
// Each scene (every map, level, park and table of the 3D games) runs natively for a few frames and answers the debug
// command `coplanar` (games/common/coplanar.c): the overlapping same-plane pairs among every drawn depth-tested mesh
// and instance in world space, by the region (scene node) they hang under. Every scene must have none.
// The command reads geometry only, so the runs draw with Mesa's software GL (off the shared GPU), under the GPU suite
// lock all the same (a render run loads the machine; the timing suites need it quiet).
//
// CONTROLS, which must fail: `coplanar selftest` (the detector on made-up faces: a doubled face, a face laid on a
// bigger one and a face 0.5 mm over another must count; a neighbour sharing an edge, a face 5 mm over another and a
// single-sided face turned away must not), and the first scene of each game with COPLANAR_CONTROL=1 (a doubled face
// added to the real scene, which the scan must find).
// --unfixed: every scene as built before the separation (COPLANAR_SEPARATE=0): the counts the fix took to zero (this
// run is expected to fail wherever a game had pairs).
//
// Run: node tests/coplanar.mjs [game ...] [--unfixed] [--jobs N]   (holds the GPU suite lock)
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
if (!process.env.CW_GPU_SUITE_INNER) { /* one hold of the lock a game (a hold is cut off after 15 minutes) */
  const argv = process.argv.slice(2), games = argv.filter((x, i) => !x.startsWith('--') && argv[i - 1] !== '--jobs'), flags = argv.filter((x) => !games.includes(x));
  let status = 0;
  for (const g of games.length ? games : ['towers', 'destructo', 'emberthrone', 'skate', 'pinball']) {
    const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), `coplanar-${g}`, 'node', fileURLToPath(import.meta.url), g, ...flags],
      { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
    if (r.status !== 0) status = r.status ?? 1;
  }
  process.exit(status);
}
const args = process.argv.slice(2);
const UNFIXED = args.includes('--unfixed');
const JOBS = Number(args[args.indexOf('--jobs') + 1]) || 4;
const only = args.filter((a, i) => !a.startsWith('--') && args[i - 1] !== '--jobs');

const range = (n, f) => Array.from({ length: n }, (_, i) => f(i));
const towerMaps = Number(readFileSync(join(ROOT, 'games/towers/assets/maps/count.txt'), 'utf8').trim());
const GAMES = {
  towers: range(towerMaps, (i) => ({ name: `map ${i + 1}`, app: 'towers_auto', frames: 90, env: { TOWERS_MAP: String(i + 1) }, field: 'game.map' })),
  destructo: range(40, (i) => ({ name: `level ${i}`, app: 'destructo_auto', frames: 360, env: { DESTRUCTO_LEVEL: String(i) }, field: 'game.level' })),
  emberthrone: [{ name: 'the world', app: 'emberthrone_auto', frames: 120, env: {}, field: 'game.region' }],
  skate: range(5, (i) => ({ name: `park ${i}`, app: 'skate_auto', frames: 150, env: { SKATE_PARK: String(i) }, field: 'game.park' })),
  pinball: range(3, (i) => ({ name: `table ${i}`, app: 'pinball_auto', frames: 90, env: { PINBALL_TABLE: String(i) }, field: 'game.table' })),
};

// Destructo's blocks are physics bodies. The level lays them 8 mm apart and they settle onto each other in the first
// seconds; settled, a block leans a few milliradians and sits a few millimetres off its neighbours, and two faces that
// lean against each other cross somewhere whatever layers they are drawn on. The game draws each block proud by a
// layer per axis that clears its settled neighbours (games/destructo/src/view.c, block_layers): that takes the settled
// structures from 81,015 pairs over the 30 levels to 2,009, where a leaning face crosses its neighbour's.
// They are held to these counts (a quarter over, and ten), so a change that brings the flush faces back fails.
const SETTLED = { /* by level: measured 18 30 34 20 0 1 0 44 39 42 0 0 166 6 65 70 2 27 272 2 0 155 0 87 411 152 0 17 0 349 */
  destructo: [32, 47, 52, 35, 0, 11, 0, 65, 58, 62, 0, 0, 217, 17, 91, 97, 12, 43, 350, 12, 0, 203, 0, 118, 523, 200, 0, 31, 0, 446],
};

function binary(app) {
  const f = join(ROOT, 'build/native/apps', `${app}.json`);
  if (!existsSync(f)) throw new Error(`${app} is not built natively: scripts/build.sh ${app}`);
  return JSON.parse(readFileSync(f, 'utf8')).binary;
}

/** one native run on the software rasterizer: the replies of CW_DEBUG_CMDS, in order */
function run(app, frames, env, cmds) {
  const e = { ...process.env, ...env, CW_FRAMES: String(frames), CW_DEBUG_CMDS: cmds.join(';'), CW_GPU: 'gl', LIBGL_ALWAYS_SOFTWARE: '1',
    SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', LP_NUM_THREADS: '4', CW_WARMUP_SPREAD: '0' };
  for (const k of ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'DRI_PRIME', 'CW_SCREENSHOT', 'CW_FRAME_DUMP']) delete e[k];
  if (!UNFIXED) delete e.COPLANAR_SEPARATE;
  return new Promise((ok, no) => {
    const p = spawn(binary(app), [], { stdio: ['ignore', 'pipe', 'pipe'], env: e });
    let out = '', err = '';
    p.stdout.on('data', (d) => { out += d; });
    let glLine = '';
    p.stderr.on('data', (d) => { err = (err + d).slice(-4000); if (!glLine) glLine = (/GL: [^|\n]*\| ([^|\n]*)/.exec(err) || [])[1] || ''; });
    const t = setTimeout(() => p.kill('SIGKILL'), 600000);
    p.on('exit', (c) => {
      clearTimeout(t);
      const replies = (out + '\n' + err).split('\n').filter((l) => l.startsWith('{')).map((l) => JSON.parse(l));
      if (!/llvmpipe|softpipe/.test(glLine)) { no(new Error(`${app}: drew on ${glLine.trim() || 'no GL renderer it named'}, not the software rasterizer`)); return; }
      if (c !== 0 || replies.length < cmds.length) no(new Error(`${app} ${JSON.stringify(env)}: exit ${c}, ${replies.length} replies of ${cmds.length}: ${err.trim().split('\n').slice(-3).join(' | ')}`));
      else ok(replies.slice(-cmds.length));
    });
  });
}

async function pool(jobs, n, fn) {
  const out = new Array(jobs.length);
  let next = 0;
  await Promise.all(range(Math.min(n, jobs.length), async () => { while (next < jobs.length) { const i = next++; out[i] = await fn(jobs[i]); } }));
  return out;
}

const names = only.length ? only : Object.keys(GAMES);
let bad = 0;
const totals = {};
for (const game of names) {
  const scenes = GAMES[game];
  if (!scenes) { console.log(`no such game: ${game} (${Object.keys(GAMES).join(', ')})`); process.exit(2); }
  const env = UNFIXED ? { COPLANAR_SEPARATE: '0' } : {};
  const jobs = scenes.map((s) => ({ s, cmds: ['coplanar', `fields ${s.field}`] }));
  // the controls: the detector on made-up faces, and a doubled face added to the first scene
  jobs.push({ s: { ...scenes[0], name: `${scenes[0].name}, COPLANAR_CONTROL`, env: { ...scenes[0].env, COPLANAR_CONTROL: '1' } }, cmds: ['coplanar selftest', 'coplanar'], control: true });
  const res = await pool(jobs, JOBS, async (j) => { try { return await run(j.s.app, j.s.frames, { ...env, ...j.s.env }, j.cmds); } catch (e) { return e; } });
  totals[game] = 0;
  for (let i = 0; i < jobs.length; i++) {
    const j = jobs[i], r = res[i];
    if (r instanceof Error) { bad++; console.log(`FAIL ${game} ${j.s.name}: ${r.message}`); continue; }
    if (j.control) {
      const [st, sc] = r, found = sc.regions.find((g) => g.name.includes('COPLANAR_CONTROL'));
      if (!st.selftest) { bad++; console.log(`FAIL ${game}: the detector's selftest: ${st.why}`); } else console.log(`ok   ${game}: the detector's selftest (a doubled face, a laid face and a 0.5 mm gap count; a shared edge, a 5 mm gap and a face turned away do not)`);
      if (!found) { bad++; console.log(`CONTROL PASSED (a failure): ${game} ${j.s.name}: the doubled face went unseen`); } else console.log(`ok   control fails as it must (${game} ${j.s.name}): ${found.pairs} pairs in the doubled face`);
      continue;
    }
    const [sc, fl] = r;
    totals[game] += sc.pairs;
    const where = `${game} ${j.s.name} (${j.s.field} ${JSON.stringify(fl[j.s.field])}): ${sc.meshes} meshes, ${sc.faces} faces`;
    if (!sc.faces) { bad++; console.log(`FAIL ${where}: nothing drawn to check`); continue; }
    if (!sc.pairs) { console.log(`ok   ${where}, no pairs${sc.moved ? ` (${sc.moved} vertices moved apart at the build)` : ''}`); continue; }
    const allow = !UNFIXED && SETTLED[game] ? SETTLED[game][scenes.indexOf(j.s)] : undefined;
    if (allow !== undefined && sc.pairs <= allow) { console.log(`ok   ${where}, ${sc.pairs} pairs of leaning settled blocks (at most ${allow})`); continue; }
    bad++;
    console.log(`FAIL ${where}, ${sc.pairs} overlapping same-plane pairs:`);
    for (const g of sc.regions.slice(0, 12)) console.log(`       ${String(g.pairs).padStart(6)}  ${g.name}  (widest ${g.width_mm} mm, at [${g.at.join(", ")}])`);
  }
}
console.log(`pairs: ${Object.entries(totals).map(([g, n]) => `${g} ${n}`).join(', ')}${UNFIXED ? ' (as built, COPLANAR_SEPARATE=0)' : ''}`);
process.exit(bad ? 1 : 0);
