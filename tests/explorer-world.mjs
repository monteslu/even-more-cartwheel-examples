// explorer's world check (native tools, no GPU):
//   current  games/explorer/assets/world.bin is what explorer_worldgen makes today, and assets/route.bin is the route
//            explorer_check writes for it (the shipped files are never stale)
//   proven   explorer_check passes on the shipped world: the abilities come in their order, no place reached can be
//            stranded (no soft lock), every pickup and warden is reached, every gate holds without its ability and
//            opens with it, nothing floats
//   controls worlds broken on purpose, each of which must fail for its own reason: the grapple gates' anchors taken
//            out (pickups never reached), the second jump and dash prizes swapped (the order), a pit dug by the start
//            (a soft lock), a lamp lifted off its ceiling (floating), a wall gate lowered to one jump (the gate leaks)
// Run: node tests/explorer-world.mjs   (after TARGETS=native scripts/build.sh explorer_worldgen explorer_check)
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { availableParallelism } from 'node:os';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const BIN = join(ROOT, 'build/native/games/explorer');
const ASSETS = join(ROOT, 'games/explorer/assets');
const OUT = join(ROOT, 'out/explorer-world');
mkdirSync(OUT, { recursive: true });
const threads = String(Math.max(1, Math.min(32, availableParallelism())));
let bad = 0;
const run = (args) => spawnSync(join(BIN, args[0]), args.slice(1), { encoding: 'utf8', maxBuffer: 1 << 26, timeout: 3600000 });
const same = (a, b) => Buffer.compare(readFileSync(a), readFileSync(b)) === 0;

// current
const g = run(['explorer_worldgen', join(OUT, 'world.bin')]);
if (g.status !== 0 || !same(join(OUT, 'world.bin'), join(ASSETS, 'world.bin'))) {
  console.log(`FAIL world.bin is stale: explorer_worldgen makes another world (${(g.stdout || '').trim()})`);
  bad++;
} else console.log(`ok   world.bin is current: ${(g.stdout || '').trim()}`);

// proven (and the route written for it)
const c = run(['explorer_check', join(ASSETS, 'world.bin'), '--threads', threads, '--route', join(OUT, 'route.bin')]);
const lines = (c.stdout || '').split('\n').filter((l) => /^(ok|FAIL)|^check|^stage/.test(l));
if (c.status !== 0) { console.log(`FAIL the check:\n${lines.join('\n')}`); bad++; } else console.log(lines.map((l) => `     ${l}`).join('\n'));
if (c.status === 0 && !same(join(OUT, 'route.bin'), join(ASSETS, 'route.bin'))) { console.log('FAIL route.bin is stale: the check writes another route'); bad++; }
else if (c.status === 0) console.log('ok   route.bin is current');

// controls
const controls = [
  [1, [], /FAIL pickups/, 'the grapple gates without anchors'],
  [2, [], /FAIL order/, 'the second jump and dash prizes swapped'],
  [3, [], /FAIL softlock/, 'a pit by the start with no way out'],
  [4, ['--only', 'grounded'], /FAIL grounded/, 'a lamp lifted off its ceiling'],
  [5, ['--only', 'gates'], /FAIL gates/, 'a wall gate lowered to one jump'],
];
for (const [n, extra, why, what] of controls) {
  const r = run(['explorer_check', join(ASSETS, 'world.bin'), '--threads', threads, '--break', String(n), ...extra]);
  if (r.status === 1 && why.test(r.stdout)) console.log(`ok   control fails as it must: ${what} (${(why.exec(r.stdout) || [''])[0]})`);
  else { console.log(`CONTROL PASSED (a failure): ${what}: status ${r.status}`); bad++; }
}
process.exit(bad ? 1 : 0);
