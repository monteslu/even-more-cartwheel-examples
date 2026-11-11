// lanternfall's world check (native tools, no GPU):
//   current  games/lanternfall/assets/world.bin is what lanternfall_worldgen makes today, and assets/route.bin is the route
//            lanternfall_check writes for it (the shipped files are never stale)
//   proven   lanternfall_check passes on the shipped world: the abilities come in their order, no place reached can be
//            stranded (no soft lock), every pickup and warden is reached, every gate holds without its ability and
//            opens with it, nothing floats, every ability reaches two or more rewards already seen on screen, every
//            ability opens a gate between places already reached (a shortcut), every region has a set piece, no
//            drop falls into thorns that were out of view, every ledge in the air is held up
//   controls worlds broken on purpose, each of which must fail for its own reason: the grapple gates' anchors taken
//            out (pickups never reached), the second jump and dash prizes swapped (the order), a pit dug by the start
//            (a soft lock), a lamp lifted off its ceiling (floating), a wall gate lowered to one jump (the gate leaks),
//            the set pieces' rewards taken out (nothing seen before reach), thorns laid beside trapdoor landings (blind
//            drops), a region's set pieces forgotten, the gates inside a region walled up (no shortcuts), the ledges' chains and
//            posts taken away (ledges hang on nothing)
// Run: node tests/lanternfall-world.mjs [--part main|controls-A|controls-B|controls-C]   (after TARGETS=native scripts/build.sh lanternfall_worldgen lanternfall_check)
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { availableParallelism } from 'node:os';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const BIN = [join(ROOT, 'build/native/project/games/lanternfall'), join(ROOT, 'build/native/games/lanternfall')].find((d) => existsSync(join(d, 'lanternfall_check')));
const ASSETS = join(ROOT, 'games/lanternfall/assets');
const OUT = join(ROOT, 'out/lanternfall-world');
mkdirSync(OUT, { recursive: true });
const threads = String(Math.max(1, Math.min(32, availableParallelism())));
let bad = 0;
// --part runs one share of the check (CI runs the shares as parallel jobs: the main check and the ten controls are
// each a full run of the checker, and one 4-core runner needs 28 minutes for all of them); no --part runs everything
const part = process.argv.includes('--part') ? process.argv[process.argv.indexOf('--part') + 1] : 'all';
const want = (p) => part === 'all' || part === p;
const run = (args) => spawnSync(join(BIN, args[0]), args.slice(1), { encoding: 'utf8', maxBuffer: 1 << 26, timeout: 3600000 });
const same = (a, b) => Buffer.compare(readFileSync(a), readFileSync(b)) === 0;

// current
if (want('main')) {
const g = run(['lanternfall_worldgen', join(OUT, 'world.bin')]);
if (g.status !== 0 || !same(join(OUT, 'world.bin'), join(ASSETS, 'world.bin'))) {
  console.log(`FAIL world.bin is stale: lanternfall_worldgen makes another world (${(g.stdout || '').trim()})`);
  bad++;
} else console.log(`ok   world.bin is current: ${(g.stdout || '').trim()}`);

// proven (and the route written for it)
const c = run(['lanternfall_check', join(ASSETS, 'world.bin'), '--threads', threads, '--route', join(OUT, 'route.bin')]);
const lines = (c.stdout || '').split('\n').filter((l) => /^(ok|FAIL)|^check|^stage/.test(l));
if (c.status !== 0) { console.log(`FAIL the check:\n${lines.join('\n')}`); bad++; } else console.log(lines.map((l) => `     ${l}`).join('\n'));
if (c.status === 0 && !same(join(OUT, 'route.bin'), join(ASSETS, 'route.bin'))) { console.log('FAIL route.bin is stale: the check writes another route'); bad++; }
else if (c.status === 0) console.log('ok   route.bin is current');
}

// controls
const controls = [
  [1, [], /FAIL pickups/, 'the grapple gates without anchors'],
  [2, [], /FAIL order/, 'the second jump and dash prizes swapped'],
  [3, [], /FAIL softlock/, 'a pit by the start with no way out'],
  [4, ['--only', 'grounded'], /FAIL grounded/, 'a lamp lifted off its ceiling'],
  [5, ['--only', 'gates'], /FAIL gates/, 'a wall gate lowered to one jump'],
  [6, [], /FAIL see before reach/, 'the set pieces\' rewards taken out'],
  [7, [], /FAIL blind drops/, 'thorns beside the landing under trapdoors'],
  [8, [], /FAIL set pieces/, 'a region with its set pieces forgotten'],
  [9, [], /FAIL shortcuts/, 'the gates inside a region walled up'],
  [10, ['--only', 'grounded'], /FAIL ledges held/, 'every ledge\'s chains and posts taken away'],
];
const SHARE = { 'controls-A': [1, 2, 3, 4], 'controls-B': [5, 6, 7], 'controls-C': [8, 9, 10] };
for (const [n, extra, why, what] of controls) {
  if (part !== 'all' && !(SHARE[part] || []).includes(n)) continue;
  const r = run(['lanternfall_check', join(ASSETS, 'world.bin'), '--threads', threads, '--break', String(n), ...extra]);
  if (r.status === 1 && why.test(r.stdout)) console.log(`ok   control fails as it must: ${what} (${(why.exec(r.stdout) || [''])[0]})`);
  else { console.log(`CONTROL PASSED (a failure): ${what}: status ${r.status}`); bad++; }
}
process.exit(bad ? 1 : 0);
