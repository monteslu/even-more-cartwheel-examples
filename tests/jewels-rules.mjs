// jewels rules gate: the C game plays exactly as the original Lua game. Each trace in tests/jewels-rules/ is the
// original's own main.lua and board.lua driven through their pad input path by tests/jewels-rules-driver.lua (every
// animation landing at once), one line per button: the whole board, the score, the best, the moves, the cursor,
// the jewel held, the hint and any reshuffle; and the reshuffle of every deadlocked board of a family, from the
// same generator. build/native/games/jewels/jewels_rules_check replays the buttons through games/jewels/src/game.c
// and must match every line. Controls (JEWELS_RULES_BREAK): a cascade's second step paid like its first, refills
// drawn differently, the hint always the first move. Each must fail at least one trace.
//
//   node tests/jewels-rules.mjs                         check the committed traces
//   node tests/jewels-rules.mjs --lua <app dir> [--lua-bin <lua 5.4>]
//                                                       write the traces again from the original's app directory
import { spawnSync } from 'node:child_process';
import { readdirSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const DIR = join(ROOT, 'tests', 'jewels-rules');
const exe = join(ROOT, 'build/native/games/jewels/jewels_rules_check');
const SEEDS = [12345, 7, 20261107];
const STEPS = 1400;
const arg = (name) => (process.argv.includes(name) ? process.argv[process.argv.indexOf(name) + 1] : null);

const luaDir = arg('--lua');
if (luaDir) {
  const lua = arg('--lua-bin') || 'lua';
  mkdirSync(DIR, { recursive: true });
  for (const seed of SEEDS) {
    const r = spawnSync(lua, [join(ROOT, 'tests', 'jewels-rules-driver.lua'), luaDir, String(seed), String(STEPS)], { encoding: 'utf8', maxBuffer: 1 << 28 });
    if (r.status !== 0) { console.log(`FAIL the Lua driver (seed ${seed}): ${r.stderr}`); process.exit(1); }
    const out = [`seed ${seed}`];
    for (const l of r.stdout.trim().split('\n')) {
      const j = JSON.parse(l);
      if ('reshuffle' in j) { out.push(`reshuffle ${j.reshuffle} ${j.after} ${j.kept ? 1 : 0}`); continue; }
      if (j.state !== 'idle') { console.log(`FAIL the Lua game was not idle after step ${j.step}`); process.exit(1); }
      out.push(['step', j.button || '-', j.grid, j.score, j.best, j.moves, j.cur[0], j.cur[1], j.sel ? j.sel[0] : '-', j.sel ? j.sel[1] : '-',
        j.hint ? j.hint.join(',') : '-', j.reshuffled ? 1 : 0].join(' '));
    }
    writeFileSync(join(DIR, `${seed}.txt`), out.join('\n') + '\n');
    console.log(`wrote tests/jewels-rules/${seed}.txt (${out.length - 1} lines)`);
  }
}

const traces = readdirSync(DIR).filter((f) => f.endsWith('.txt')).sort();
if (!traces.length) { console.log('FAIL no traces in tests/jewels-rules'); process.exit(1); }
let bad = 0;
const check = (trace, env = {}) => spawnSync(exe, [join(DIR, trace)], { encoding: 'utf8', env: { ...process.env, ...env } });
for (const t of traces) {
  const r = check(t);
  if (r.error) { console.log(`FAIL ${exe}: ${r.error.message} (build it: scripts/build.sh jewels_rules_check)`); process.exit(1); }
  const out = (r.stdout || '').trim();
  if (r.status === 0) console.log(out.replace(join(DIR, ''), ''));
  else { bad++; console.log(`FAIL ${t}\n${out}${r.stderr || ''}`); }
}
for (const brk of ['score', 'refill', 'hint']) {
  const failed = traces.filter((t) => check(t, { JEWELS_RULES_BREAK: brk }).status === 1);
  if (failed.length) console.log(`ok   control "${brk}" fails as it must (${failed.length} of ${traces.length} traces)`);
  else { bad++; console.log(`CONTROL PASSED (a failure): "${brk}" matched every trace`); }
}
process.exit(bad ? 1 : 0);
