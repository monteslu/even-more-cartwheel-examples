// destructo controls gate: the game is the sticks, the d-pad and the face buttons, and nothing else. Through romdev, on
// the game cart, level 1 aiming:
//   ignores   each shoulder held, then both triggers pulled (the analog channel), each for 60 frames: the aim (turn,
//             raise), the power (fixed: it never moves), the ammunition, the shots fired, the controls-used mask and the
//             wide view must all stay where they were
//   reacts    the controls it does read do act: the d-pad raises the launcher, North toggles the wide view on and off,
//             South fires a shot (a fixed power: aim.power is the same before and after)
//   control   the build before the change (DESTRUCTO_CART_DIR=<its carts>, relative to the tree) reacts to the shoulders
//             (its power moves), so the "ignores" check must fail on it: run with --control
// Run: node tests/destructo-controls.mjs [--gpu gl|webgpu] [--control]
import { openCart, call, gpusArg } from './lib/romdev.mjs';

const control = process.argv.includes('--control');
const KEYS = ['aim.yaw', 'aim.pitch', 'aim.power', 'aim.ammo', 'game.shots', 'ui.used', 'ui.view'];
let bad = 0;
const say = (ok, what) => { if (!ok) bad++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); };
const tap = (f, b, n = 3) => ({ frame: f, buttons: [b], until: f + n });

for (const gpu of gpusArg()) {
  const h = await openCart('destructo', { gpu, tag: 'controls' });
  let at = 1;
  const seg = async (entries) => { await call('input', { op: 'timeline', timeline: entries }, h.session); at = Math.max(...entries.map((e) => e.until)); };
  const goto = async (to) => { if (to > at) { await h.step(to - at); at = to; } };
  const pick = async () => { const s = await h.state(); return Object.fromEntries(KEYS.map((k) => [k, s[k]])); };
  try {
    await seg([tap(20, 'south'), tap(40, 'south'), tap(60, 'start'), tap(90, 'south')]);
    await goto(250);
    const s0 = await pick();
    await seg([{ frame: 251, buttons: ['r'], until: 281 }, { frame: 285, buttons: ['l'], until: 295 }]);
    await call('input', { op: 'set', ports: [{ axes: { lt: 1, rt: 1 } }] }, h.session);
    await h.step(60), at += 60;
    await call('input', { op: 'set', ports: [{}] }, h.session);
    await h.step(10), at += 10;
    const s1 = await pick();
    const moved = KEYS.filter((k) => JSON.stringify(s0[k]) !== JSON.stringify(s1[k]));
    say(!moved.length, `${control ? 'control (old build)' : 'ignores'} ${gpu}: shoulders and triggers change ${moved.length ? moved.map((k) => `${k} ${s0[k]} -> ${s1[k]}`).join(', ') : 'nothing'}`);
    if (!control) {
      await seg([{ frame: at + 2, buttons: ['up'], until: at + 22 }]);
      const s2 = await pick();
      say(s2['aim.pitch'] > s1['aim.pitch'], `reacts ${gpu}: the d-pad raises the launcher (${s1['aim.pitch']} -> ${s2['aim.pitch']})`);
      await seg([{ frame: at + 2, buttons: ['north'], until: at + 5 }]);
      const s3 = await pick();
      await seg([{ frame: at + 2, buttons: ['north'], until: at + 5 }]);
      const s4 = await pick();
      say(s3['ui.view'] === 1 && s4['ui.view'] === 0, `reacts ${gpu}: North toggles the wide view (${s3['ui.view']} then ${s4['ui.view']})`);
      await seg([{ frame: at + 2, buttons: ['south'], until: at + 5 }]);
      const s5 = await pick();
      say(s5['game.shots'] === s4['game.shots'] + 1 && s5['aim.power'] === s0['aim.power'], `reacts ${gpu}: South fires one shot at the fixed power (${s4['game.shots']} -> ${s5['game.shots']}, power ${s5['aim.power']})`);
    }
  } catch (e) { say(false, `${gpu}: ${e.message}`); } finally { await h.close(); }
}
process.exit(control ? 0 : bad ? 1 : 0);
