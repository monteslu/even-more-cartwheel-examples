// skate park check: every park (games/skate/src/parks.c) built as the game builds it, natively, no GPU
// (build/native/games/skate/skate_check parks):
//   nothing floats   each piece's footing (its rest points) stands on another piece within 2 cm, or on the ground
//   every line       a bot skates each line through the real controls (the stick, the face buttons, back for the start
//                    point) and must do its feature: the grind (0.4 s on the line's rail or ledge), the gap, the air
//                    off a quarter pipe or a kicker landed, the manual held. Up to four goes, each a different trick.
//   the controls     each must fail: a piece lifted 30 cm (floats), a wall across a rail's approach (that line is not
//                    skated), a kicker cut to a quarter of its height (its gap is not cleared)
// Run: node tests/skate-parks.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/skate/skate_check');
const r = spawnSync(exe, ['parks'], { encoding: 'utf8', timeout: 900000 });
const out = r.stdout || '';
process.stdout.write(out);
const lines = out.split('\n');
const fails = lines.filter((l) => /FAIL|CONTROL PASSED/.test(l) && !/^parks FAIL/.test(l));
const parks = lines.filter((l) => /^park \d/.test(l)).length;
const ok = lines.filter((l) => /^\s+line .* ok /.test(l)).length;
const ctl = lines.filter((l) => /fails as it must/.test(l)).length;
if (r.status !== 0 || fails.length || parks !== 5 || ctl < 13) {
  console.log(`FAIL parks: status ${r.status}, ${parks} parks, ${ok} lines skated, ${ctl} controls failed as they must, ${fails.length} problems`);
  process.exit(1);
}
console.log(`ok   ${parks} parks, ${ok} lines skated, nothing floats, ${ctl} controls fail as they must`);
