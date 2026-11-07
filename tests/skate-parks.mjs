// skate park check: every park (games/skate/src/parks.c) built as the game builds it, natively, no GPU
// (build/native/games/skate/skate_check parks):
//   nothing floats   each piece's footing (its rest points) stands on another piece within 2 cm, or on the ground
//   every line       a bot skates each line through the real controls (the stick, the face buttons, back for the start
//                    point) and must do its feature: the grind (0.4 s on the line's rail or ledge), the gap, the air
//                    off a quarter pipe or a kicker landed, the manual held. Up to four goes, each a different trick.
//   the controls     each must fail: a piece lifted 30 cm (floats), a wall across a rail's approach (that line is not
//                    skated), a kicker cut to a quarter of its height (its gap is not cleared)
//   the bakes        each park's lightmap (assets/lightmaps/park<n>.cwlm) was baked for the shape the park has now: its
//                    .hash matches the drawn geometry's; the control, a park with a piece lifted, must not match
// Run: node tests/skate-parks.mjs
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
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
/* the bakes: made for these shapes (SKATE_BAKE=<assets/lightmaps> on a native skate build rebakes them all) */
const exeB = join(ROOT, 'build/native/games/skate/skate_check');
const hashes = (v) => Object.fromEntries([...(spawnSync(exeB, ['bakehash', String(v)], { encoding: 'utf8' }).stdout || '').matchAll(/park (\d) ([0-9a-f]{8})/g)].map((m) => [m[1], m[2]]));
const now = hashes(0), lifted = hashes(1);
let stale = 0;
for (let n = 0; n < 5; n++) {
  let baked = '';
  try { baked = readFileSync(join(ROOT, `games/skate/assets/lightmaps/park${n}.hash`), 'utf8').trim(); } catch {}
  if (baked !== now[n]) { console.log(`FAIL park ${n}'s bake is stale (baked for ${baked || 'nothing'}, the park is ${now[n]}): rebake with SKATE_BAKE`); stale++; }
}
const ctlStale = [0, 1, 2, 3, 4].filter((n) => lifted[n] && lifted[n] !== now[n]).length;
if (ctlStale !== 5) { console.log(`CONTROL PASSED (a failure): a lifted piece left ${5 - ctlStale} parks' shapes unchanged`); stale++; }
if (stale) process.exit(1);
console.log('ok   5 bakes current; the control (a piece lifted) changes every park\'s shape');
