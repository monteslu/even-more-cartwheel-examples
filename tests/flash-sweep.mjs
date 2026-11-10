// The flash sweep: no game strobes. Each entry of tests/flash-sweep.json is a stretch of real play through a game's
// brightest moments (hits, pickups, lights, weather, menus, transitions), the dual cart (build/cart-wgpu) run on romdev
// on GL and on WebGPU with every frame of the stretch captured in ONE frame({op:'flash'}) call and held to the
// photosensitive flash rules (WCAG 2.2 success criterion 2.3.1 and the Harding test's thresholds: at most three
// flashes and three red flashes a second over a sixth of the screen, the same for small lights switching together,
// no regular flicker above 3 Hz), through the engine's tools/flash-check.mjs --host romdev. Every run also has its
// CONTROL, which must fail: the same frames with an 8 Hz full-screen strobe laid over them, judged on the server.
//
// A stretch that hardly moves proves nothing: each run's motion (the mean change of a pixel between frames) must
// reach the floor (an entry's "minMotion", or MIN_MOTION below), or the entry is reported as proving nothing and fails,
// even when nothing in it flashed. A game's own control ("mustFail": its old effect, switched back in by an
// environment variable) must fail the judge on every backend, or the entry it stands beside proves nothing either.
//
// An entry names a gate of tests/*gates*.json (its app, input script, start requests and environment) and how many
// frames to check, or gives those itself: { "name", "gate" | "app", "frames", "from"?, "input"?, "startCmds"?,
// "env"?, "gpu"?, "maxRise"?, "minMotion"?, "mustFail"?, "knownFail"?, "note"? } (maxRise: also bound the hardest
// two-frame rise, for a moment that must swell; knownFail: a failure waiting on a fix outside this repository,
// reported as KNOWN and not counted, and reported again when it starts to pass so the mark comes off).
//
// The engine's --host romdev flash check is v0.46.0 or later: deps/cartwheel-engine, or CW_ENGINE_DIR=<a checkout>.
// The server is 127.0.0.1:7331 (ROMDEV_URL, or ROMDEV_PORT, picks another). The sweep needs a GPU: run it on the
// machine that owns the romdev server, inside one hold of the engine's GPU suite lock.
//   node tests/flash-sweep.mjs [name ...] [--gpu gl|webgpu|both] [--keep]     (default both backends)
// (--keep: each run's checked frames stay in out/flash/<name>.<gpu>.cwfd, a CW_FRAME_DUMP file that
//  tools/flash-check.mjs --dump reads)
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, readdirSync, rmSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = process.env.CW_ENGINE_DIR ? resolve(process.env.CW_ENGINE_DIR) : join(ROOT, 'deps/cartwheel-engine');
const MIN_MOTION = 0.002; // mean |delta Y| per pixel per frame (0..1): four times the engine's floor of 0.0005; a busy stretch of these games runs 0.005 to 0.03, a board game 0.002 to 0.003
const argv = process.argv.slice(2), opt = (k, d) => (argv.includes(`--${k}`) ? argv[argv.indexOf(`--${k}`) + 1] : d);
const valued = new Set(['--gpu']);
const only = argv.filter((a, i) => !a.startsWith('--') && !valued.has(argv[i - 1])), keep = argv.includes('--keep');
const gpuArg = opt('gpu', 'both');
if (!['gl', 'webgpu', 'both'].includes(gpuArg)) { console.log('FAIL --gpu gl|webgpu|both'); process.exit(2); }
const gpus = gpuArg === 'both' ? ['gl', 'webgpu'] : [gpuArg];

const check = join(ENGINE, 'tools/flash-check.mjs'), runCart = join(ENGINE, 'tools/run-cart.mjs');
if (!existsSync(check) || !existsSync(runCart) || !readFileSync(runCart, 'utf8').includes('flash-from')) {
  console.log(`FAIL ${ENGINE}: its tools have no romdev flash check (cartwheel-engine v0.46.0 or later): CW_ENGINE_DIR=<such a checkout>`);
  process.exit(2);
}
const env = { ...process.env, CW_PROJECT_ROOT: ROOT };
if (!env.ROMDEV_URL && env.ROMDEV_PORT) env.ROMDEV_URL = `http://127.0.0.1:${env.ROMDEV_PORT}`;

const gates = new Map();
for (const f of readdirSync(join(ROOT, 'tests')).filter((n) => /gates.*\.json$/.test(n)))
  for (const g of JSON.parse(readFileSync(join(ROOT, 'tests', f), 'utf8'))) if (g.app) gates.set(g.name, g);
const entries = JSON.parse(readFileSync(join(ROOT, 'tests/flash-sweep.json'), 'utf8')).filter((e) => !only.length || only.includes(e.name));
if (!entries.length) { console.log('FAIL no sweep entries (tests/flash-sweep.json, or the names given)'); process.exit(1); }
const dir = join(ROOT, 'out/flash');
mkdirSync(dir, { recursive: true });

/** one run: the engine's romdev flash check of the entry on one backend; what it found */
function runOne(e, g, gpu) {
  const app = e.app || g.app, frames = e.frames ?? g.frames, from = e.from ?? 2;
  const input = e.input ?? g.input, start = e.startCmds ?? g.startCmds, runEnv = { ...(g.env || {}), ...(e.env || {}) };
  const cart = join(ROOT, 'build/cart-wgpu/carts', `${app}.wasc`);
  if (!existsSync(cart)) return { ok: false, lines: [`FAIL ${e.name} (${gpu}): no cart ${cart}: scripts/build.sh ${app}`], error: true };
  const label = `${e.name} (romdev ${gpu})`, json = join(dir, `${e.name}.${gpu}.json`);
  rmSync(json, { force: true });
  const args = [check, '--host', 'romdev', '--app', app, '--frames', String(frames), '--from', String(from), '--gpu', gpu, '--label', label, '--json', json];
  if (input) args.push('--input', join(ROOT, 'tests', input));
  if (start && start.length) args.push('--start-cmds', start.join(';'));
  if (Object.keys(runEnv).length) args.push('--env', JSON.stringify(runEnv));
  if (e.maxRise != null) args.push('--max-rise', String(e.maxRise));
  if (keep) args.push('--keep', join(dir, `${e.name}.${gpu}.cwfd`));
  const t0 = Date.now();
  const r = spawnSync('node', args, { encoding: 'utf8', env });
  const lines = (r.stdout || '').split('\n').filter(Boolean);
  if (!existsSync(json)) {
    lines.push(`FAIL ${label}: the flash check wrote no result (exit ${r.status})`, ...(r.stderr || '').trim().split('\n').slice(-6).map((l) => '     ' + l));
    return { ok: false, lines, error: true };
  }
  const f = JSON.parse(readFileSync(json, 'utf8'));
  rmSync(json, { force: true });
  const m = f.result || {}, motion = +m.motion || 0, floor = e.minMotion ?? MIN_MOTION;
  const still = motion < floor;
  const flashes = m.regions ? `${m.regions.flashes}/s` : '?';
  if (still) lines.push(`FAIL ${label}: motion ${motion.toFixed(4)} is under the floor ${floor}: the stretch hardly moves, this run proves nothing`);
  lines.push(`     ${label}: ${((Date.now() - t0) / 1000).toFixed(0)} s`);
  return { ok: f.ok && !still, flashes, motion, strobes: !f.ok && !!(f.control && f.control.failsAsItMust), controlFails: !!(f.control && f.control.failsAsItMust), lines, still };
}

let bad = 0, known = 0, runs = 0;
const table = [];
for (const e of entries) {
  const g = e.gate ? gates.get(e.gate) : {};
  if (!g) { console.log(`FAIL ${e.name}: no gate named ${e.gate}`); bad++; continue; }
  const legs = e.gpu ? [e.gpu] : gpus;
  let failed = 0, errors = 0, stills = 0, controlsPassed = 0;
  for (const gpu of legs) {
    runs++;
    const r = runOne(e, g, gpu);
    for (const l of r.lines) console.log(l);
    if (r.error) errors++;
    else if (!r.controlFails) controlsPassed++;
    if (r.still) stills++;
    if (!r.ok) failed++;
    table.push({ name: e.name, gpu, ok: r.ok, flashes: r.flashes || '-', motion: r.motion == null ? '-' : r.motion.toFixed(4), control: r.error ? 'error' : r.controlFails ? 'fails' : 'PASSED', kind: e.mustFail ? 'must fail' : e.knownFail ? 'known' : 'gate' });
  }
  if (e.mustFail) {
    // the game's own control: its frames must fail the judge on every backend, with motion (a frozen control fails
    // nothing), and the strobe control must still fail over it
    const ok = failed === legs.length && !errors && !stills && !controlsPassed;
    console.log(ok ? `ok   ${e.name} fails as it must (${e.mustFail})` : `FAIL ${e.name}: a control that must fail (${e.mustFail}) ${errors ? 'did not run' : stills ? 'hardly moves' : controlsPassed ? 'hid the strobe control' : `passed on ${legs.length - failed} of ${legs.length} backends`}`);
    if (!ok) bad++;
    continue;
  }
  if (e.knownFail) {
    console.log(failed ? `KNOWN ${e.name} fails, waiting on: ${e.knownFail}` : `NOTE ${e.name} passes now: take its knownFail mark off`);
    if (failed) known++;
    continue;
  }
  if (failed) bad++;
}
console.log('\nentry                      gpu     result  flashes/s  motion   strobe control');
for (const t of table) console.log(`${t.name.padEnd(26)} ${t.gpu.padEnd(7)} ${(t.kind === 'must fail' ? (t.ok ? 'PASSED' : 'fails') : t.ok ? 'ok' : 'FAIL').padEnd(7)} ${t.flashes.padEnd(10)} ${t.motion.padEnd(8)} ${t.control}${t.kind === 'must fail' ? '  (the game\'s own control: must fail)' : ''}`);
console.log(bad ? `FAIL flash sweep: ${bad} of ${entries.length} entries` : `ok   flash sweep: ${entries.length - known} entries (${runs} runs), none strobes, every stretch moves, every control fails${known ? ` (${known} known failure(s) waiting on fixes elsewhere)` : ''}`);
process.exit(bad ? 1 : 0);
