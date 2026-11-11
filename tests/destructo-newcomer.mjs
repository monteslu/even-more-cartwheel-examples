// destructo newcomer gate, level 1 to 3 only (the full sweep is tests/destructo-human.mjs, about 15 minutes): the human-like
// player (src/human.c: it sees only what the screen shows and sends its buttons through the game's own pad mapping, the
// d-pad or left stick to aim, the right stick to go round, West for the ammunition, South to fire) clears each of the first
// three levels with all eight seeds, within par plus 4 shots on level 1.
// Run: node tests/destructo-newcomer.mjs [--gpu gl|webgpu]
import { runCheck } from './lib/romdev.mjs';
const gi = process.argv.indexOf('--gpu');
const r = await runCheck('human', ['--seeds', '8', '0', '1', '2'], { gpu: gi > 0 ? process.argv[gi + 1] : 'gl', timeoutMin: 60 });
if (r.status !== 0) { console.log(`FAIL check exited ${r.status}\n${r.text.slice(-1500)}`); process.exit(1); }
let bad = 0;
for (const l of [0, 1, 2]) {
  const rs = r.rows.filter((x) => x.level === l), c = rs.filter((x) => x.cleared).length;
  const ok = rs.length === 8 && c === 8 && (l > 0 || rs.every((x) => x.shots <= x.par + 4));
  if (!ok) bad++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} level ${l + 1} ${rs[0] ? rs[0].name : '?'}: ${c} of ${rs.length} cleared, shots ${rs.map((x) => x.shots).join(',')}, par ${rs[0] ? rs[0].par : '?'}`);
}
process.exit(bad ? 1 : 0);
