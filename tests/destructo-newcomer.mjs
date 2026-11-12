// destructo newcomer gate, level 1 only (the full sweep is tests/destructo-human.mjs, about 15 minutes): the human-like
// player (src/human.c: it sees only what the screen shows and sends its buttons through the game's own pad mapping, the
// d-pad or left stick to aim, the right stick to go round, West for the ammunition, South to fire) clears the first
// level (a real landmark, the easiest) with all eight seeds, within par plus 4 shots.
// Control, which must fail: a player with eight times the newcomer's aim error (--skill 8) does not clear level 1 with every
// seed (five of eight do), so the gate does tell a steady newcomer from one who cannot aim.
// Run: node tests/destructo-newcomer.mjs [--gpu gl|webgpu]
import { runCheck } from './lib/romdev.mjs';
const gi = process.argv.indexOf('--gpu');
const gpu = gi > 0 ? process.argv[gi + 1] : 'gl';
const play = async (extra) => {
  const r = await runCheck('human', ['--seeds', '8', ...extra, '0'], { gpu, timeoutMin: 60 });
  if (r.status !== 0) { console.log(`FAIL check exited ${r.status}\n${r.text.slice(-1500)}`); process.exit(1); }
  return r.rows.filter((x) => x.level === 0);
};
const passes = (rs) => rs.length === 8 && rs.every((x) => x.cleared && x.shots <= x.par + 4);
const say = (rs) => `${rs.filter((x) => x.cleared).length} of ${rs.length} cleared, shots ${rs.map((x) => x.shots).join(',')}, par ${rs[0] ? rs[0].par : '?'}`;
let bad = 0;
const rs = await play([]);
if (!passes(rs)) bad++;
console.log(`${passes(rs) ? 'ok  ' : 'FAIL'} level 1 ${rs[0] ? rs[0].name : '?'}: ${say(rs)} (every seed within par plus 4)`);
const ctl = await play(['--skill', '8']);
if (passes(ctl)) { bad++; console.log(`CONTROL PASSED (a failure): eight times the aim error: ${say(ctl)}`); } else console.log(`ok   control fails as it must: eight times the aim error: ${say(ctl)}`);
process.exit(bad ? 1 : 0);
