// emberthrone blade trail: the motion check's pad script (tests/emberthrone-motion.mjs: a three-cut combo, a heavy and
// a cut locked on to a raider) on romdev, with the cart's trail recorder (view.c "trail") checking, every fixed tick the
// hero's ribbon draws, how far its head is from the tip of the blade in her hand: the tip from the skeleton as posed
// (the right hand's model transform, the sword where it hangs in the hand, its length), placed by the figure's place
// and facing. Screenshots in the swings (SHOTS).
// Bars (PASS needs all): 20 ticks or more drawn, the head 6 cm or less from the tip on average and 15 cm at most.
// CONTROL, which must fail: --control <cart dir>, the build before (the ribbon on the rules' arc round the body) with the
// recorder.
//
//   node tests/emberthrone-trail.mjs <cart dir> <out dir> [--control <cart dir>] [--gpu gl|webgpu]
import { mkdirSync, writeFileSync, existsSync, unlinkSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { script, COMMANDS } from './emberthrone-motion.mjs';

const SHOTS = [1047, 1072, 1098, 1205, 1386];
const REC_AT = 905, END = 1440;

function steps(from, to) {
  const out = [];
  let f = 0;
  for (const [n, p] of script()) {
    const a = Math.max(f, from), b = Math.min(f + n, to);
    if (b > a) {
      const port = {};
      for (const k of p.b || []) port[k] = true;
      if (p.lx || p.ly) port.axes = { lx: p.lx || 0, ly: p.ly || 0 };
      out.push({ input: { ports: [port] }, frames: b - a });
    }
    f += n;
  }
  return out;
}

async function run(cartDir, out, gpu) {
  process.env.DESTRUCTO_CART_DIR = resolve(cartDir);
  const { openCart, call } = await import('./lib/romdev.mjs');
  mkdirSync(out, { recursive: true });
  const h = await openCart('emberthrone', { gpu, tag: 'trail' });
  const cmds = [...COMMANDS.filter((c) => !/cape/.test(c.command)), { frame: REC_AT, command: `trail rec ${END - REC_AT}` }].sort((x, y) => x.frame - y.frame);
  try {
    let at = 0;
    for (const stop of [...SHOTS, END]) {
      const dump = join(out, 'seg.cwfd');
      await call('frame', { op: 'record', frames: stop - at, steps: steps(at, stop), commandsAt: cmds.filter((c) => c.frame >= at && c.frame < stop).map((c) => ({ frame: c.frame - at, command: c.command })), path: dump, downscale: 16, maxSeconds: 1800 }, h.session, { timeoutMs: 2400000 });
      if (existsSync(dump)) unlinkSync(dump);
      at = stop;
      if (stop !== END) await h.shot(join(out, `swing-${stop}.png`));
    }
    const r = await h.command('trail get');
    writeFileSync(join(out, 'trail.json'), JSON.stringify(r, null, 1));
    return r;
  } finally { await h.close(); }
}

const judge = (r) => {
  const checks = { ticks: r.ticks >= 20, mean: r.mean_m <= 0.06, max: r.max_m <= 0.15 };
  return { ...r, checks, pass: Object.values(checks).every(Boolean) };
};

const [, , cart, out, ...rest] = process.argv;
const opt = (k) => { const i = rest.indexOf(k); return i >= 0 ? rest[i + 1] : null; };
const gpu = opt('--gpu') || 'gl';
const res = { run: judge(await run(cart, join(out, 'run'), gpu)) };
if (opt('--control')) res.control = judge(await run(opt('--control'), join(out, 'control'), gpu));
const ok = res.run.pass && (!res.control || !res.control.pass);
console.log(JSON.stringify({ ...res, verdict: ok ? 'PASS' : 'FAIL' }, null, 1));
writeFileSync(join(out, 'trail.json'), JSON.stringify(res, null, 1));
process.exit(ok ? 0 : 1);
