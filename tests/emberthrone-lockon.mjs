// emberthrone lock-on framing: the motion check's pad script (tests/emberthrone-motion.mjs: run at the raiders, lock on,
// strafe both ways, the combo, a roll, a heavy, a back step, a back walk, a cut) on romdev, with the cart's framing
// recorder (view.c "framing") sampling every drawn frame while the hero is locked on: the hero's and the target's screen
// boxes (feet to the top of the head) as the camera draws them. Screenshots at the moments named in SHOTS.
// Bars (PASS needs all): the target whole on the screen (no sample under 98% inside), the hero covering at most a
// quarter of the target's box in at most 5% of the samples, the hero at most 40% of the frame's height on average and
// the target at least 12% (the fight reads from a step back, the foe is not a speck).
// CONTROL, which must fail: the same run on the old camera: --control <cart dir> (that build with the recorder only).
//
//   node tests/emberthrone-lockon.mjs <cart dir> <out dir> [--control <cart dir>] [--gpu gl|webgpu]
import { mkdirSync, writeFileSync, existsSync, unlinkSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { script, COMMANDS } from './emberthrone-motion.mjs';

const SHOTS = process.env.EMBER_SHOTS ? process.env.EMBER_SHOTS.split(",").map(Number) : [960, 1010, 1100, 1220, 1340]; // (EMBER_SHOTS: other moments, for looking)
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
  const h = await openCart('emberthrone', { gpu, tag: 'lockon' });
  const cmds = [...COMMANDS.filter((c) => !/cape/.test(c.command)), { frame: REC_AT, command: `framing rec ${END - REC_AT}` }].sort((x, y) => x.frame - y.frame);
  try {
    let at = 0;
    for (const stop of [...SHOTS, END]) {
      const dump = join(out, 'seg.cwfd');
      await call('frame', { op: 'record', frames: stop - at, steps: steps(at, stop), commandsAt: cmds.filter((c) => c.frame >= at && c.frame < stop).map((c) => ({ frame: c.frame - at, command: c.command })), path: dump, downscale: 16, maxSeconds: 1800 }, h.session, { timeoutMs: 2400000 });
      if (existsSync(dump)) unlinkSync(dump);
      at = stop;
      if (stop !== END) await h.shot(join(out, `lock-${stop}.png`));
    }
    const r = await h.command('framing get');
    writeFileSync(join(out, 'framing.json'), JSON.stringify(r, null, 1));
    return r;
  } finally { await h.close(); }
}

export function judge(r) {
  const checks = {
    samples: r.samples >= 200,
    whole: r.target_vis_min >= 0.98,
    clear: r.hidden <= 0.05 * r.samples,
    hero: r.hero_h <= 0.4,
    target: r.target_h >= 0.12,
  };
  return { ...r, checks, pass: Object.values(checks).every(Boolean) };
}

const [, , cart, out, ...rest] = process.argv;
const opt = (k) => { const i = rest.indexOf(k); return i >= 0 ? rest[i + 1] : null; };
const gpu = opt('--gpu') || 'gl';
const control = opt('--control');
const res = { run: judge(await run(cart, join(out, 'run'), gpu)) };
if (control) res.control = judge(await run(control, join(out, 'control'), gpu));
const ok = res.run.pass && (!control || !res.control.pass);
console.log(JSON.stringify({ ...res, verdict: ok ? 'PASS' : 'FAIL' }, null, 1));
writeFileSync(join(out, 'lockon.json'), JSON.stringify(res, null, 1));
process.exit(ok ? 0 : 1);
