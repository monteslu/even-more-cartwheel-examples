// towers_bench on every map through romdev: the bench's scene (towers on every slot, 2400 creeps kept on the paths, four
// bots) taken from its own map to each map the perf gate runs (5, 10, 15, 20, 26, 30; with --all every map), run for a
// span of frames on the fixed clock, GL and WebGPU. A map fails when the cart traps on the way there (a wasm
// 'unreachable': an abort, as a failed allocation gives), when its crowd is not kept alive and drawn, when a draw is
// skipped or a program missing, or when a mesh of its draws unbaked ("lightmap: ... changed since the bake"): the
// cart's last log lines (wasm({op:'events'})) are printed under a failure. The control, which must fail: map 20 (a snow
// map) with the coplanar grid's old cell cap (COPLANAR_CELL_CAP=60000000, games/common/coplanar.c), which traps the
// cart in a 300 MB realloc as it did before the cap was bounded by memory.
//
//   node tests/towers-maps-romdev.mjs [--cart build/cart-wgpu/carts/towers_bench.wasc] [--all] [--maps 5,10,20]
//                                     [--gpu gl|webgpu] [--frames 600] [--log]
// ROMDEV_URL picks the server (default 127.0.0.1:7331); CW_ENGINE_DIR an engine checkout to take the GPU pin from.
import { existsSync } from 'node:fs';
import { request as httpRequest } from 'node:http';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = resolve(process.env.CW_ENGINE_DIR || join(ROOT, 'deps/cartwheel-engine'));
const { wgpuPin } = await import(pathToFileURL(join(ENGINE, 'tools/lib/wgpu-pin.mjs')).href);
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const CART = resolve(opt('cart', join(ROOT, 'build/cart-wgpu/carts/towers_bench.wasc')));
if (!existsSync(CART)) { console.error(`no cart ${CART} (build the cart tree)`); process.exit(2); }
const MAPS = argv.includes('--all') ? Array.from({ length: 30 }, (_, i) => i + 1) : opt('maps', '5,10,15,20,26,30').split(',').map(Number);
const GPUS = opt('gpu', 'gl,webgpu').split(',');
const FRAMES = +opt('frames', 600), CROWD = 2000;
const BASE = new URL(process.env.ROMDEV_URL || 'http://127.0.0.1:7331');

function post(tool, args, session, limitMs = 600000) {
  const body = JSON.stringify(args);
  return new Promise((ok, fail) => {
    const req = httpRequest({ host: BASE.hostname, port: BASE.port || 80, path: `/tool/${tool}`, method: 'POST', agent: false,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'x-romdev-session': session, 'x-romdev-agent': 'towers-maps-romdev' } }, (res) => {
      let text = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { text += c; });
      res.on('end', () => {
        clearTimeout(timer);
        let b;
        try { b = JSON.parse(text); } catch { b = text; }
        if (res.statusCode < 200 || res.statusCode >= 300 || (b && b.isError)) fail(new Error(`romdev ${tool}: ${typeof b === 'string' ? b : JSON.stringify(b).slice(0, 3000)}`));
        else ok(b);
      });
    });
    const timer = setTimeout(() => req.destroy(new Error(`romdev ${tool}: no reply in ${limitMs} ms`)), limitMs);
    req.on('error', (e) => { clearTimeout(timer); fail(new Error(`romdev at ${BASE.origin} ${tool}: ${e.message} (is the server running?)`)); });
    req.end(body);
  });
}

const pin = wgpuPin();
const FIELDS = ['game.enemies', 'view.drawn', 'render.skipped', 'cw.three.missing', 'game.map'];

/** one load (env: a switch set in the load's first frame), the bench scene on `map`, FRAMES frames */
async function runMap(gpu, map, env) {
  const session = `towers-maps-${process.pid}-${gpu}-${map}-${Date.now().toString(36)}`;
  const out = { gpu, map, why: [] };
  try {
    const loaded = await post('loadMedia', { platform: 'wasmcart', path: CART, deterministicSeed: 1, deterministicStepMs: 1000 / 60,
      firstCommand: `env ${env || 'TOWERS_NO_HUD=1'}`, gpuPower: pin.power, wgpu: gpu === 'webgpu' }, session);
    if (loaded.gpu !== gpu) throw new Error(`the cart runs on ${loaded.gpu}, not ${gpu}`);
    const fr = loaded.firstCommandReply;
    if (fr === undefined) throw new Error('romdev: firstCommand was not delivered (romdev too old for it)');
    if (fr && typeof fr.error === 'string') throw new Error(`the env request answered: ${fr.error}`);
    const seen = gpu === 'webgpu' ? loaded.gpuAdapter && (loaded.gpuAdapter.device || loaded.gpuAdapter.description) : loaded.glRenderer;
    pin.check('romdev', seen);
    try {
      const t = await post('frame', { op: 'timing', frames: FRAMES, warmup: 0, window: FRAMES, commands: ['test_run', `map ${map}`], debugFields: FIELDS }, session);
      const w = t.windows || [], f = (n, k, fn) => fn(...w.map((x) => (x.fields && x.fields[n] ? x.fields[n][k] : NaN)));
      out.crowd = f('game.enemies', 'min', Math.min); out.drawn = f('view.drawn', 'min', Math.min);
      out.skipped = f('render.skipped', 'max', Math.max); out.missing = f('cw.three.missing', 'max', Math.max);
      out.mapSeen = t.fields && t.fields['game.map'] && t.fields['game.map'].last;
      if (!(out.crowd >= CROWD)) out.why.push(`crowd ${out.crowd} under ${CROWD}`);
      if (!(out.drawn >= CROWD)) out.why.push(`drawn ${out.drawn} under ${CROWD}`);
      if (out.skipped) out.why.push(`${out.skipped} skipped draws`);
      if (out.missing) out.why.push(`${out.missing} missing programs`);
      if (out.mapSeen !== map) out.why.push(`on map ${out.mapSeen}, not ${map}`);
    } catch (e) { out.why.push(`trapped: ${e.message}`); }
    const ev = await post('wasm', { op: 'events' }, session, 60000).catch(() => null);
    out.log = ev && ev.log ? ev.log.map((l) => `${l.frame}: ${l.text}`) : [];
    const stale = out.log.filter((l) => /lightmap: .*changed since the bake/.test(l));
    if (stale.length) out.why.push(`${stale.length} mesh(es) drawn unbaked (${stale[0].replace(/^\d+: /, '')})`);
  } catch (e) { out.why.push(e.message); }
  finally { await post('host', { op: 'shutdown' }, session, 60000).catch(() => {}); }
  out.ok = out.why.length === 0;
  return out;
}

let bad = 0;
const show = (name, r, mustFail) => {
  const scene = r.crowd === undefined ? '' : `${r.crowd} creeps (${r.drawn} drawn), map ${r.mapSeen}`;
  if (mustFail) {
    if (r.ok) { console.log(`CONTROL PASSED (a failure): ${name}: ${scene}`); bad++; } else console.log(`ok   control fails as it must: ${name}: ${r.why.join(', ')}`);
  } else if (!r.ok) { console.log(`FAIL ${name}: ${scene}${scene ? '; ' : ''}fails for: ${r.why.join(', ')}`); bad++; } else console.log(`ok   ${name}: ${scene}`);
  if (!r.ok || argv.includes('--log')) for (const l of (r.log || []).slice(-14)) console.log(`       log ${l}`);
};
for (const gpu of GPUS) for (const map of MAPS) show(`map ${map} ${gpu === 'gl' ? 'GL' : 'WebGPU'}`, await runMap(gpu, map));
show(`map 20 ${GPUS[0] === 'gl' ? 'GL' : 'WebGPU'} with the coplanar grid's old cell cap`, await runMap(GPUS[0], 20, 'COPLANAR_CELL_CAP=60000000'), true);
process.exit(bad ? 1 : 0);
