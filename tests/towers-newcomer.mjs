// towers newcomer check: the difficulty as a first-time player meets it. towers_soak's clear run with every seat played
// by games/towers/newcomer.c (TOWERS_BOT=newcomer): a bot that reads only what the screen shows (its gold, the costs on
// the diamond and the pills, its pads, the "next" line's word on a new kind, a leak at the core) and acts at a person's
// pace (a second or two to decide, a hop a press, a moment to read before it builds, a few seconds watching). It never
// calls a wave early, sells, changes aim, and finds the storm late. Native, no GPU.
//   early    maps 1 to EARLY, alone and in two, three seeds each: every run holds the core
//   later    maps LATER_FROM to 30, alone: the newcomer holds at most LATER_HELD of the runs (the later maps still
//            challenge); the map check's perfect bots (tools/check-maps.mjs --soak) hold every one of them
// CONTROL, which must fail: TOWERS_OLD_DIFFICULTY=1, the waves as they were before this tuning (every boss at full
// strength from the first map, a gate beside the core sending as many as the far one, no word on a new kind).
// Run: node tests/towers-newcomer.mjs [--jobs n]
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { cpus } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const SOAK = join(ROOT, 'build/native/games/towers/towers_soak'), MAPS = join(ROOT, 'games/towers/assets/maps');
if (!existsSync(SOAK)) { console.log(`FAIL no ${SOAK} (scripts/build.sh towers_soak)`); process.exit(1); }
const argv = process.argv.slice(2);
const JOBS = Number(argv.includes('--jobs') ? argv[argv.indexOf('--jobs') + 1] : Math.max(1, Math.min(10, cpus().length - 2)));
const EARLY = 7, LATER_FROM = 16, LATER_HELD = 0.5, SEEDS = [1, 2, 3];

/** one clear run: the core it kept, or -1 when it fell (or ran out of time) */
function clear(map, players, seed, env) {
  return new Promise((ok) => {
    const p = spawn(SOAK, ['--clear', join(MAPS, `${String(map).padStart(3, '0')}.cwtd`), String(players), '45', '1'],
      { env: { ...process.env, TOWERS_BOT: 'newcomer', TOWERS_SEED: String(seed), ...env } });
    let out = '';
    p.stdout.on('data', (d) => (out += d));
    p.on('close', () => { const m = /held in .* core (\d+) of 20/.exec(out); ok(m ? +m[1] : -1); });
  });
}
async function all(jobs) {
  const res = new Array(jobs.length);
  let next = 0;
  await Promise.all(Array.from({ length: Math.min(JOBS, jobs.length) }, async () => {
    while (next < jobs.length) { const k = next++; res[k] = await clear(...jobs[k]); }
  }));
  return res;
}
async function judge(env, label) {
  const early = [], later = [];
  for (let m = 1; m <= EARLY; m++) for (const p of [1, 2]) for (const s of SEEDS) early.push([m, p, s, env]);
  for (let m = LATER_FROM; m <= 30; m++) for (const s of SEEDS) later.push([m, 1, s, env]);
  const e = await all(early), l = await all(later);
  const fellEarly = early.filter((_, k) => e[k] < 0).map(([m, p, s]) => `map ${m} ${p}p seed ${s}`);
  const held = l.filter((c) => c >= 0).length / l.length;
  const why = [];
  if (fellEarly.length) why.push(`the core fell on an early map: ${fellEarly.join(', ')}`);
  if (held > LATER_HELD) why.push(`the newcomer held ${(held * 100).toFixed(0)}% of the later maps' runs (at most ${LATER_HELD * 100}%)`);
  const kept = e.filter((c) => c >= 0);
  const line = `${label}: maps 1-${EARLY} held ${e.filter((c) => c >= 0).length} of ${e.length} (core kept on average ${kept.length ? (kept.reduce((a, b) => a + b, 0) / kept.length).toFixed(1) : '-'}); ` +
    `maps ${LATER_FROM}-30 alone held ${l.filter((c) => c >= 0).length} of ${l.length}`;
  return { why, line };
}
let bad = 0;
const g = await judge({}, 'newcomer');
if (g.why.length) bad++, console.log(`FAIL ${g.line}\n     ${g.why.join('\n     ')}`);
else console.log(`ok   ${g.line}`);
const c = await judge({ TOWERS_OLD_DIFFICULTY: '1' }, 'the old waves');
if (c.why.length) console.log(`ok   control fails as it must (TOWERS_OLD_DIFFICULTY): ${c.why[0]}\n     ${c.line}`);
else bad++, console.log(`CONTROL PASSED (a failure): ${c.line}`);
process.exit(bad ? 1 : 0);
