// pinball rules gate: each table plays its own rules. Runs build/native/games/pinball/pinball_rulecheck
// (games/pinball/tools/rulecheck.c): every table's four modes started in turn and checked by how each plays (a roving
// shot, a hurry-up, a target sweep, an all-shots mode, a two-ball mode, the table's toy: the creature, the magnet, the
// high striker), an unlit shot never counting, each mode played to its goal; then the wizard mode the four light (four
// balls, every shot lit, a growing award, the modes starting over when it ends); and the three tables' modes and toys
// all different. The control: pinball_rulecheck_oneset, every table on one shared rule set with no toy (the rules as
// they were), which must fail.
// Run: node tests/pinball-rules.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const run = (n) => spawnSync(join(ROOT, 'build/native/games/pinball', n), [], { encoding: 'utf8', timeout: 120000 });
let bad = 0;
const r = run('pinball_rulecheck');
const sum = JSON.parse((r.stdout || '{}').trim().split('\n').pop() || '{}');
for (const l of (r.stdout || '').split('\n').filter((l) => /^FAIL/.test(l))) console.log(l);
if (r.status !== 0 || sum.fails !== 0 || !(sum.checks >= 60)) bad++, console.log(`FAIL pinball_rulecheck: exit ${r.status}, ${sum.fails} of ${sum.checks} checks failed`);
else console.log(`ok   ${sum.checks} rule checks: every table's modes, toy and wizard mode play their own way`);
const c = run('pinball_rulecheck_oneset');
const cf = (c.stdout || '').split('\n').filter((l) => /^FAIL/.test(l));
if (c.status !== 0 && cf.length) console.log(`ok   control (one shared rule set, no toys) fails as it must: ${cf.length} checks, e.g.\n     ${cf[0]}`);
else bad++, console.log(`CONTROL PASSED (a failure): one shared rule set went unnoticed (exit ${c.status})`);
process.exit(bad ? 1 : 0);
