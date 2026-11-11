#!/usr/bin/env node
// romdev-shot.mjs: one frame of a cart through romdev, for the launcher art (no native game runs; the cart is what ships).
//
//   node scripts/romdev-shot.mjs <cart.wasc> <frames> <out.png> [--size WxH] [--gl] [--cmd "<request>"]... [KEY=VALUE ...]
//
// Loads the cart as a player's host would (no deterministic seed, so the host's preferred size sets the cart's render
// scale: --size 3840x2160 draws 4K frames, --size 2880x1620 a 1.5x frame; default 1920x1080), sends the environment
// (KEY=VALUE, the engine's `env` request, which the game reads from frame 2) and each --cmd request at the start of
// the frames after the load, one request a frame, then steps to frame N in ONE frame step (never a loop over frames:
// romdev is shared), writes the frame as a PNG and shuts the host down. One session, one host, one job at a time.
//
// ROMDEV_PORT (default 7331) names the server; CW_GPU_POWER ('low-power' | 'high-performance') the GPU the cart gets
// (a pool lane sets it; unset: the server's own default). --gl runs the dual cart on its GL renderer.
import { request as httpRequest } from 'node:http';
import { mkdirSync } from 'node:fs';
import { basename, dirname, resolve } from 'node:path';

const args = process.argv.slice(2);
const pos = [], cmds = [], env = {};
let size = '1920x1080', gl = false;
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--size') size = args[++i];
  else if (a === '--gl') gl = true;
  else if (a === '--cmd') cmds.push(args[++i]);
  else if (/^[A-Z_][A-Z0-9_]*=/.test(a)) { const k = a.indexOf('='); env[a.slice(0, k)] = a.slice(k + 1); }
  else pos.push(a);
}
const [cart, framesArg, out] = pos;
const frames = +framesArg;
if (!cart || !(frames >= 1) || !out) { console.error('usage: romdev-shot.mjs <cart.wasc> <frames> <out.png> [--size WxH] [--gl] [--cmd "<request>"]... [KEY=VALUE ...]'); process.exit(2); }
const m = /^(\d+)x(\d+)$/.exec(size);
if (!m) { console.error(`--size ${size}: WxH`); process.exit(2); }
// the server: ROMDEV_URL (the pool sets it per lane), or ROMDEV_PORT, else 127.0.0.1:7331
const ROMDEV = new URL(process.env.ROMDEV_URL || `http://127.0.0.1:${process.env.ROMDEV_PORT || 7331}`);
const session = `art-shot-${basename(cart, '.wasc')}-${process.pid}`;

function post(tool, a) {
  const body = JSON.stringify(a);
  const steps = tool === 'frame' && a.op === 'step' ? a.frames : 0;
  const limit = 300000 + steps * 500;
  return new Promise((ok, fail) => {
    const req = httpRequest({ host: ROMDEV.hostname, port: +ROMDEV.port || 80, path: `/tool/${tool}`, method: 'POST', agent: false,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'x-romdev-session': session, 'x-romdev-agent': process.env.CW_ROMDEV_AGENT || 'cartwheel-art-shot' } }, (res) => {
      let text = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { text += c; });
      res.on('end', () => { clearTimeout(timer); ok({ ok: res.statusCode >= 200 && res.statusCode < 300, text }); });
      res.on('error', (e) => { clearTimeout(timer); fail(e); });
    });
    const timer = setTimeout(() => req.destroy(new Error(`romdev ${tool}: no reply in ${limit} ms`)), limit);
    req.on('error', (e) => { clearTimeout(timer); fail(e); });
    req.end(body);
  });
}
async function call(tool, a) {
  const res = await post(tool, a);
  let body;
  try { body = JSON.parse(res.text); } catch { body = res.text; }
  if (!res.ok || (body && body.isError)) throw new Error(`romdev ${tool}: ${typeof body === 'string' ? body : JSON.stringify(body).slice(0, 2000)}`);
  if (body && Array.isArray(body.content)) { /* the tool route may wrap the result MCP-style */
    const t = body.content.find((c) => c.type === 'text');
    try { return JSON.parse(t.text); } catch { return t ? t.text : body; }
  }
  return body;
}

// the env request: `env A=1 B="two words"` (the engine's debug channel), a request of its own: the first request
// after the load runs before the game's own commands are registered, so one riding on it as `env ... -- room 3`
// is "unknown"; sent alone it costs one frame
const quote = (v) => (/^[^\s"\\()]+$/.test(v) ? v : `"${v.replace(/[\\"]/g, (c) => '\\' + c)}"`);
const envReq = Object.keys(env).length ? 'env ' + Object.entries(env).map(([k, v]) => `${k}=${quote(v)}`).join(' ') : null;
const requests = envReq ? [envReq, ...cmds] : cmds;

mkdirSync(dirname(resolve(out)), { recursive: true });
let loaded = false;
try {
  const r = await call('loadMedia', { platform: 'wasmcart', path: resolve(cart), preferredSize: { width: +m[1], height: +m[2] },
    ...(process.env.CW_GPU_POWER ? { gpuPower: process.env.CW_GPU_POWER } : {}), ...(gl ? { wgpu: false } : {}) });
  loaded = true;
  let at = 1; // the load runs frame 1
  for (const text of requests) { // each answered at the start of the next frame
    const reply = await call('wasm', { op: 'command', text });
    at++;
    const answer = reply && typeof reply === 'object' && 'reply' in reply ? reply.reply : reply;
    if (answer && typeof answer === 'object' && answer.error) throw new Error(`request "${text}": ${answer.error}`);
  }
  if (frames > at) await call('frame', { op: 'step', frames: frames - at });
  await call('frame', { op: 'screenshot', path: resolve(out) });
  console.log(`${out}: frame ${Math.max(frames, at)} of ${basename(cart)} on ${r && r.gpu ? r.gpu : 'romdev'}, ${size} asked`);
} finally {
  if (loaded) await call('host', { op: 'shutdown' }).catch(() => {});
}
