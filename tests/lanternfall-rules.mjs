// lanternfall's combat rules (native, no GPU): lanternfall_rules plays the real simulation on a small test world and
// measures: a blow that lands holds the world still a few ticks (both sides), pushes the enemy away and the striker
// back; a puff turns a strike from the side and gives to one from above (which bounces you); creatures struck down stay
// down until a rest; an upgrade's room locks for its trial and its find waits for the guardians; a warden staggers at
// half health and uses its second phase's move; the first relic's charm reaches what a plain strike cannot; a fall
// leaves a measure of energy with your flame where you fell, and touching it gives it back. Each control turns one rule
// off and its own check must fail.
// Run: node tests/lanternfall-rules.mjs   (after TARGETS=native scripts/build.sh lanternfall_rules)
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = [join(ROOT, 'build/native/project/games/lanternfall/lanternfall_rules'), join(ROOT, 'build/native/games/lanternfall/lanternfall_rules')].find(existsSync);
if (!exe) { console.log('FAIL lanternfall_rules is not built'); process.exit(1); }
let bad = 0;
const r = spawnSync(exe, [], { encoding: 'utf8' });
console.log((r.stdout || '').trim().split('\n').map((l) => `     ${l}`).join('\n'));
if (r.status !== 0 || !/rules ok/.test(r.stdout)) { console.log('FAIL the rules'); bad++; } else console.log('ok   rules');
const CONTROLS = [['hitstop', 'hitstop'], ['knockback', 'knockback'], ['recoil', 'recoil'], ['puff', 'puff_side'], ['slain', 'slain'], ['trial', 'trial'],
  ['phase', 'phase'], ['charms', 'charms'], ['echo', 'echo']];
for (const [c, check] of CONTROLS) {
  const k = spawnSync(exe, ['--control', c], { encoding: 'utf8' });
  const failed = k.status === 1 && new RegExp(`^${check}\\s+FAIL`, 'm').test(k.stdout);
  if (failed) console.log(`ok   control fails as it must: no ${c} (${check})`);
  else { console.log(`CONTROL PASSED (a failure): no ${c}`); bad++; }
}
process.exit(bad ? 1 : 0);
