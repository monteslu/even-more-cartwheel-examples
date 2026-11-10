// romdev for the gates: a cart loaded on the MCP server's HTTP tool route (127.0.0.1:7331, POST /tool/<name>, one
// session a host), driven in ranges only: one frame step or one input timeline a span, never a call a frame (the server
// is shared). At most two hosts at once (the callers run one, or two at a time).
//
//   const h = await openCart('destructo_check', { gpu: 'gl' | 'webgpu' });
//   await h.step(600); await h.read('check.state'); await h.command('check status'); await h.close();
//   const out = await runCheck('levels', ['--landmarks'], { gpu })   // the check cart: one check, its JSON lines
import { request as httpRequest } from 'node:http';
import { join, resolve, dirname } from 'node:path';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
export const cartPath = (app) => join(ROOT, 'build/cart-wgpu/carts', `${app}.wasc`);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function post(tool, args, session, timeoutMs) {
  const body = JSON.stringify(args || {});
  return new Promise((res, rej) => {
    const req = httpRequest({ host: '127.0.0.1', port: 7331, path: `/tool/${tool}`, method: 'POST', agent: false,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'x-romdev-session': session } }, (r) => {
      let data = '';
      r.setEncoding('utf8');
      r.on('data', (c) => { data += c; });
      r.on('end', () => res({ status: r.statusCode, data }));
    });
    const timer = setTimeout(() => req.destroy(new Error(`romdev ${tool}: no reply in ${timeoutMs} ms`)), timeoutMs);
    req.on('error', (e) => { clearTimeout(timer); rej(e); });
    req.on('close', () => clearTimeout(timer));
    req.end(body);
  });
}

export class RomdevError extends Error {}

export async function call(tool, args, session, { timeoutMs = 600000 } = {}) {
  const r = await post(tool, args, session, timeoutMs);
  let body = r.data;
  try { body = JSON.parse(r.data); } catch { /* text */ }
  if (body && Array.isArray(body.content)) {
    const t = body.content.find((c) => c.type === 'text');
    if (body.isError) throw new RomdevError(`romdev ${tool} ${JSON.stringify(args).slice(0, 200)}: ${t ? t.text : JSON.stringify(body)}`);
    try { return JSON.parse(t.text); } catch { return t ? t.text : body; }
  }
  if (r.status !== 200 || (body && (body.isError || body.error))) throw new RomdevError(`romdev ${tool} ${JSON.stringify(args).slice(0, 200)}: http ${r.status} ${typeof body === 'string' ? body : JSON.stringify(body).slice(0, 2000)}`);
  return body;
}

let seq = 0;
const STEP_MAX = 300;
// A cart on its own host (seed null: a load on the real clock, which romdev's preferredSize needs). gpu 'gl' loads it as a host without WebGPU (the dual cart runs on GL there), 'webgpu' on
// the integrated GPU (low-power) as the GPU gates run.
export async function openCart(app, { gpu = 'gl', seed = 1, tag = '', load = {} } = {}) {
  const path = cartPath(app);
  if (!existsSync(path)) throw new Error(`no ${path}: build it (ninja -C build/cart-wgpu -j4 ${app})`);
  const session = `destructo-${tag || app}-${process.pid}-${++seq}`;
  const loaded = await call('loadMedia', { platform: 'wasmcart', path, gpuPower: 'low-power', wgpu: gpu === 'webgpu', ...(seed === null ? {} : { deterministicSeed: seed }), ...load }, session);
  const got = loaded && loaded.gpu;
  if (gpu === 'webgpu' && got !== 'webgpu') { await call('host', { op: 'shutdown' }, session).catch(() => {}); throw new Error(`romdev: ${app} asked for WebGPU and runs on ${got}`); }
  if (gpu === 'gl' && got && got !== 'gl') { await call('host', { op: 'shutdown' }, session).catch(() => {}); throw new Error(`romdev: ${app} asked for GL and runs on ${got}`); }
  let frame = 1; // the load runs frame 1
  const h = {
    session, app, gpu: got, loaded,
    get frame() { return frame; },
    // n frames in calls of at most STEP_MAX: a range, never a call a frame; a WebGPU frame can take most of a second on a
    // loaded machine, and romdev evicts a host whose last call began ten minutes ago, even while that call still runs
    async step(n) {
      while (n > 0) {
        const k = Math.min(n, STEP_MAX);
        await call('frame', { op: 'step', frames: k }, session, { timeoutMs: 600000 + k * 3000 });
        frame += k, n -= k;
      }
    },
    // one schedule on absolute frames ([{frame, buttons|button, until}]), then the frames up to `to` stepped in one call
    async timeline(entries, to) {
      await call('input', { op: 'timeline', timeline: entries }, session);
      if (to > frame) await h.step(to - frame);
    },
    async command(text, maxFrames = 8) {
      const r = await call('wasm', { op: 'command', text, maxFrames }, session);
      frame += (r && r.framesRun) || 1;
      const reply = r && 'reply' in r ? r.reply : r;
      if (reply && reply.error) throw new RomdevError(`${app}: ${text}: ${reply.error}`);
      return reply;
    },
    async read(name) { const r = await call('wasm', { op: 'read', name }, session); return r && (r.value ?? r.values ?? r.decoded ?? r); },
    async state() {
      const st = await call('wasm', { op: 'debugState' }, session);
      const o = {};
      for (const f of st.fields || []) o[f.name] = f.values ?? f.value;
      return o;
    },
    // frame({op:'timing'}): a span of frames timed in one call (wall, CPU in the frame call, GPU; romdev 0.153.0+)
    async timing(args) {
      const r = await call('frame', { op: 'timing', ...args }, session, { timeoutMs: 900000 });
      frame += (args.warmup || 0) + (args.frames || 0);
      return r;
    },
    async events() { return call('wasm', { op: 'events' }, session); },
    async shot(file) { return call('frame', { op: 'screenshot', path: resolve(file) }, session); },
    async close() { await call('host', { op: 'shutdown' }, session).catch(() => {}); },
  };
  return h;
}

// One check of the check cart (games/destructo/tools/checkcart.c): `check run <tool> args...` on a fresh host, then
// frame ranges of 300 with the state read between them (a few seconds of wall clock apart: the check runs on the
// cart's job pool, not in its frames), then its output in 64 KiB pieces. Returns { status, ms, threads, text, rows }:
// rows are its JSON lines.
export async function runCheck(tool, args = [], { gpu = 'gl', timeoutMin = 240, log = null } = {}) {
  const h = await openCart('destructo_check', { gpu, tag: `check-${tool}` });
  try {
    const quote = (a) => (/[\s"]/.test(a) ? `"${a.replace(/["\\]/g, (c) => `\\${c}`)}"` : a);
    const started = await h.command(['check', 'run', tool, ...args.map(String)].map(quote).join(' '));
    if (!started || !started.started) throw new Error(`check run ${tool}: ${JSON.stringify(started)}`);
    const t0 = Date.now();
    let st;
    for (;;) {
      await h.step(300);
      st = await h.command('check status');
      if (st.state === 2) break;
      if (Date.now() - t0 > timeoutMin * 60000) throw new Error(`check ${tool} ${args.join(' ')}: still running after ${timeoutMin} minutes`);
      if (log) log(st);
      await sleep(Math.min(4000, 250 + (Date.now() - t0) / 10));
    }
    let text = '';
    for (let from = 0; ;) {
      const o = await h.command(`check out ${from} 65536`);
      text += o.text;
      from = o.to;
      if (from >= o.len) break;
    }
    const rows = text.split('\n').filter((l) => l.startsWith('{')).map((l) => JSON.parse(l));
    return { status: st.status, ms: st.ms, threads: st.threads, text, rows };
  } finally {
    await h.close();
  }
}

// Runs async tasks at most `n` at a time (the gates use two: the server is shared), results in order.
export async function inPairs(tasks, n = 2) {
  const out = new Array(tasks.length);
  let next = 0;
  await Promise.all(Array.from({ length: Math.min(n, tasks.length) }, async () => {
    while (next < tasks.length) { const i = next++; out[i] = await tasks[i](); }
  }));
  return out;
}

// The --gpu option of a destructo gate: 'gl', 'webgpu' or both (the default)
export function gpusArg(argv = process.argv) {
  const i = argv.indexOf('--gpu');
  return i > 0 ? argv[i + 1].split(',') : ['gl', 'webgpu'];
}
