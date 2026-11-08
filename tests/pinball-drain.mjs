// pinball drain gate: the flipper buttons work until the ball has really gone, as on a real machine. Runs
// build/native/games/pinball/pinball_flipcheck (games/pinball/tools/flipcheck.c): scripted balls on every table, the
// rules and the solver headless, both flipper buttons pressed and let go over and over, every press checked (the
// flipper must move on the tick of the press):
//   middle    down the middle, ball save off: through the drain and the bonus count until the next ball is served
//   kickback  down the left outlane with the kickback lit: kicked back up and falling again
//   bounce    already past the flipper tips, sent back up between them, flipped as it comes down
//   saved     down the middle in ball save: the new ball comes up the shooter lane
//   tilt      tilted: here every press must do nothing (the one thing that kills the flippers)
// The control: pinball_flipcheck_oldrule, the same program on the rule the game had (the flippers died the moment a
// ball crossed the drain line, about 0.1 s after it passed them). It must fail.
// Run: node tests/pinball-drain.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const bin = (n) => join(ROOT, 'build/native/games/pinball', n);
const run = (n) => spawnSync(bin(n), [], { encoding: 'utf8', timeout: 120000 });

let bad = 0;
const r = run('pinball_flipcheck');
const lines = (r.stdout || '').split('\n').filter((l) => /^(ok|FAIL)/.test(l));
const sum = JSON.parse((r.stdout || '{}').trim().split('\n').pop() || '{}');
for (const l of lines) console.log(l);
if (r.status !== 0 || sum.fails !== 0 || lines.length !== 15) bad++, console.log(`FAIL pinball_flipcheck: exit ${r.status}, ${sum.fails} failed, ${lines.length} of 15 cases ran${r.stderr ? `\n${r.stderr}` : ''}`);
else console.log('ok   every press worked until the ball had gone (and none in a tilt), on all three tables');

const c = run('pinball_flipcheck_oldrule');
const cf = (c.stdout || '').split('\n').filter((l) => /^FAIL/.test(l));
if (c.status !== 0 && cf.length) console.log(`ok   control (the old rule) fails as it must: ${cf.length} cases, e.g.\n     ${cf[0]}`);
else bad++, console.log(`CONTROL PASSED (a failure): the old rule went unnoticed (exit ${c.status})`);
process.exit(bad ? 1 : 0);
