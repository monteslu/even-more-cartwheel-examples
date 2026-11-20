// beatsync's forgiving gate: does the judgement take a person's timing, not a machine's?
//
// A bot plays the real cart through romdev, on GL and WebGPU, as a person on a living-room setup does, from the
// next note's cue (cue.lane, cue.ms against song.ms, what the rivers draw; never the chart):
//   - each press lands late on average (human.biasMs: reaction plus a sound path nobody has calibrated) with a wide
//     spread (human.sigmaMs), held about human.pressMs;
//   - now and then a thumb slips (human.slipEvery): the wrong button first, the right one human.slipFixMs later.
// It passes when it clears the first song on Easy and on Medium with at least forgiving.minAccuracy for that
// difficulty (Perfect counts 1, Good a half: the game's own grade measure; 0.70 is a C, 0.55 a D).
//
// Two CONTROLS must fail:
//   - the same player, same seed, under the old judgement (the `rules old` request: 35/70 ms on every difficulty, a
//     wrong button misses the note) must fall under the bar on Easy. A control that passes means the bot is too good
//     to tell the rules apart.
//   - a masher pressing every button on every note, exactly on time, must score no Perfect and no combo past 1.
//
//   node tests/beatsync-forgiving.mjs [--gpu gl|webgpu|both] [--seed N]
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { LANE_BUTTON, SCREEN, enterSong, gpusOf, openBeatsync } from './lib/beatsync.mjs';

const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const GPUS = gpusOf(argv);
const CFG = JSON.parse(readFileSync(join(ROOT, 'tests/beatsync-game.json'), 'utf8')).forgiving;
const HUMAN = CFG.human;
const OUT = join(ROOT, 'out/gate/beatsync-forgiving');
mkdirSync(OUT, { recursive: true });
const FRAME_MS = 1000 / 60;
const DIFF = ['Easy', 'Medium', 'Hard'];

function rng(seed) {
  let a = seed >>> 0;
  return () => { a = (a + 0x6d2b79f5) >>> 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t ^= t + Math.imul(t ^ (t >>> 7), 61 | t); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}
const gauss = (r) => Math.sqrt(-2 * Math.log(1 - r())) * Math.cos(2 * Math.PI * r());
const LOOK = ['song.ms', 'cue.lane', 'cue.ms', 'song.done', 'song.failed', 'game.screen', 'notes.total', 'notes.perfect', 'notes.good', 'notes.miss', 'game.max_combo'];

/** mode: 'human' | 'mash'; rules: 'new' | 'old' */
async function play(gpu, diff, seed, mode, rules) {
  const r = await openBeatsync('beatsync', { gpu, tag: `forgiving-${mode}-${rules}` });
  const rand = rng(seed);
  const pressF = Math.max(2, Math.round(HUMAN.pressMs / FRAME_MS));
  const idle = (frames) => ({ pads: [{ buttons: [] }], frames });
  const hold = (buttons, frames) => ({ pads: [{ buttons }], frames });
  const stopWhen = { field: 'game.screen', equals: SCREEN.results };
  try {
    const set = await r.command(`rules ${rules}`);
    if (!JSON.stringify(set).includes(`"${rules}"`)) throw new Error(`rules ${rules}: the cart answered ${JSON.stringify(set)}`);
    let f = await enterSong(r, { song: CFG.song, diff }, LOOK);
    const t0 = r.now(), limit = t0 + CFG.maxSeconds * 60;
    let notes = 0;
    while (r.now() < limit && f['game.screen'] === SCREEN.play) {
      let segs;
      if (f['cue.lane'] < 0) segs = [idle(30)];
      else {
        const lane = f['cue.lane'];
        const exact = f['cue.ms'] - f['song.ms'];
        if (mode === 'mash') {
          segs = [idle(Math.max(0, Math.round(exact / FRAME_MS) - 1)), hold(LANE_BUTTON, pressF), idle(2)];
        } else {
          const due = exact + HUMAN.biasMs + gauss(rand) * HUMAN.sigmaMs;
          const wait = Math.max(0, Math.round(due / FRAME_MS) - 1);
          notes++;
          if (notes % HUMAN.slipEvery === 0) { // a slip: the next river's button, then the right one
            const fixF = Math.max(1, Math.round(HUMAN.slipFixMs / FRAME_MS));
            segs = [idle(Math.max(0, wait - fixF)), hold([LANE_BUTTON[(lane + 1) % 3]], Math.min(pressF, fixF)), idle(Math.max(0, fixF - Math.min(pressF, fixF))), hold([LANE_BUTTON[lane]], pressF), idle(2)];
          } else segs = [idle(wait), hold([LANE_BUTTON[lane]], pressF), idle(2)];
        }
      }
      f = (await r.run(segs, { fields: LOOK, stopWhen })).fields;
    }
    const total = f['notes.total'] || 1;
    const acc = (f['notes.perfect'] + 0.5 * f['notes.good']) / total;
    return { done: f['song.done'] === 1, failed: f['song.failed'], acc, perfect: f['notes.perfect'], good: f['notes.good'], miss: f['notes.miss'], total: f['notes.total'], maxCombo: f['game.max_combo'] };
  } finally {
    await r.close();
  }
}

const seed = +opt('seed', 1);
const fmt = (x) => `${x.perfect} Perfect, ${x.good} Good, ${x.miss} Miss of ${x.total}, accuracy ${x.acc.toFixed(2)}, best combo ${x.maxCombo}`;
const results = [];
let failed = 0;
for (const gpu of GPUS) {
  for (const diff of CFG.difficulties) {
    const x = await play(gpu, diff, seed, 'human', 'new');
    results.push({ gpu, diff, run: 'human', ...x });
    const ok = x.done && x.acc >= CFG.minAccuracy[diff];
    console.log(`${ok ? 'ok  ' : 'FAIL'} forgiving (${gpu}, ${DIFF[diff]}, late ${HUMAN.biasMs} ms, spread ${HUMAN.sigmaMs} ms, a slip every ${HUMAN.slipEvery}): ${x.done ? 'cleared' : 'not cleared'}, ${fmt(x)}`);
    if (!ok) failed++;
  }
  const old = await play(gpu, 0, seed, 'human', 'old');
  results.push({ gpu, diff: 0, run: 'control-old', ...old });
  const oldPass = old.done && old.acc >= CFG.minAccuracy[0];
  console.log(`${oldPass ? 'FAIL' : 'ok  '} control (${gpu}, Easy, the old judgement): ${old.done ? 'cleared' : 'not cleared'}, ${fmt(old)}${oldPass ? ': the gate cannot tell the old rules from the new' : ', under the bar as it must be'}`);
  if (oldPass) failed++;
  const mash = await play(gpu, 0, seed, 'mash', 'new');
  results.push({ gpu, diff: 0, run: 'control-mash', ...mash });
  const mashOk = mash.perfect === 0 && mash.maxCombo <= 1;
  console.log(`${mashOk ? 'ok  ' : 'FAIL'} control (${gpu}, Easy, every button on every note): ${fmt(mash)}${mashOk ? ', no Perfect and no combo, as it must be' : ': mashing pays'}`);
  if (!mashOk) failed++;
}
writeFileSync(join(OUT, `results-${seed}.json`), JSON.stringify(results, null, 1));
console.log(failed ? `beatsync-forgiving: ${failed} failed` : 'beatsync-forgiving: ok');
process.exit(failed ? 1 : 0);
