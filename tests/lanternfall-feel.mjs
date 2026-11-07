// lanternfall's movement feel (native, no GPU): lanternfall_feel plays the real movement code on a test strip and measures
// the held and tapped jump heights (variable height), the jump distances, the second jump, the dash, the coyote
// window (a jump pressed n ticks after running off a ledge), the jump buffer (a jump pressed n ticks before landing)
// and the turn-round time, each within its bound. The controls turn one feature off and must fail: no coyote time,
// no jump buffer, no jump cut (letting go of jump no longer cuts the rise).
// Run: node tests/lanternfall-feel.mjs   (after TARGETS=native scripts/build.sh lanternfall_feel)
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = [join(ROOT, 'build/native/project/games/lanternfall/lanternfall_feel'), join(ROOT, 'build/native/games/lanternfall/lanternfall_feel')].find(existsSync);
let bad = 0;
const r = spawnSync(exe, [], { encoding: 'utf8' });
console.log((r.stdout || '').trim().split('\n').map((l) => `     ${l}`).join('\n'));
if (r.status !== 0 || !/feel ok/.test(r.stdout)) { console.log('FAIL the feel is out of its bounds'); bad++; } else console.log('ok   feel');
for (const [c, field] of [['coyote', 'coyote_ticks'], ['buffer', 'buffer_ticks'], ['cut', 'jump_cut_ratio']]) {
  const k = spawnSync(exe, ['--control', c], { encoding: 'utf8' });
  const failed = k.status === 1 && new RegExp(`${field}.*FAIL`).test(k.stdout);
  if (failed) console.log(`ok   control fails as it must: no ${c} (${field})`);
  else { console.log(`CONTROL PASSED (a failure): no ${c}`); bad++; }
}
process.exit(bad ? 1 : 0);
