#!/usr/bin/env node
// Tunes each later map's difficulty (its `diff`) so that two bots hold it but lose some of the core: from map 11 on, the
// core they keep falls from 20 to 6 by map 30 (the early maps keep gen-maps.mjs's gentle curve). For each map it
// bisects diff on one clear run of the soak build, writes the result to tools/map-diffs.json (gen-maps.mjs reads it)
// and into the map. Run after gen-maps.mjs and a native build of towers_soak; then the map check's clear runs (three
// seeds each) confirm every map can still be held.
//   node tools/tune-maps.mjs <towers_soak> [--only n,m] [--jobs n]
import { existsSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import { cpus } from 'node:os';

const HERE = dirname(fileURLToPath(import.meta.url));
const MAPS = join(HERE, '../assets/maps'), TABLE = join(HERE, 'map-diffs.json');
const argv = process.argv.slice(2);
const SOAK = argv[0];
if (!SOAK || !existsSync(SOAK)) { console.error('usage: tune-maps.mjs <towers_soak> [--only n,m] [--jobs n]'); process.exit(2); }
const opt = (k) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : null; };
const ONLY = opt('--only') ? new Set(opt('--only').split(',').map(Number)) : null;
const JOBS = Number(opt('--jobs') || Math.max(1, cpus().length - 2));
const COUNT = +readFileSync(join(MAPS, 'count.txt'), 'utf8');
const FIRST = 11;
const target = (n) => Math.round(20 - 14 * (n - FIRST) / Math.max(COUNT - FIRST, 1));
const table = existsSync(TABLE) ? JSON.parse(readFileSync(TABLE, 'utf8')) : {};

/** the core left after one clear run at this difficulty (-1: it fell or ran out of time) */
function run(file, text, diff) {
  const tmp = `${file}.tune-${diff.toFixed(3)}.cwtd`;
  writeFileSync(tmp, text.replace(/^diff .*$/m, `diff ${diff.toFixed(3)}`));
  return new Promise((res) => {
    const p = spawn(SOAK, ['--clear', tmp, '2', '45', '1']);
    let out = '';
    p.stdout.on('data', (d) => (out += d));
    p.on('close', () => {
      rmSync(tmp, { force: true });
      const m = /held in .* core (\d+) of 20/.exec(out);
      res(m ? +m[1] : -1);
    });
  });
}

async function tune(n) {
  const file = join(MAPS, `${String(n).padStart(3, '0')}.cwtd`), text = readFileSync(file, 'utf8');
  const want = target(n);
  let lo = 0.6, hi = 4.0, best = lo, bestCore = await run(file, text, lo);
  if (bestCore < 0) return { n, diff: lo, core: bestCore, want };
  for (let k = 0; k < 8; k++) { // the hardest difficulty that still leaves the bots at least the core it wants
    const mid = (lo + hi) / 2, core = await run(file, text, mid);
    if (core >= want) { best = mid, bestCore = core, lo = mid; } else hi = mid;
  }
  return { n, diff: +best.toFixed(3), core: bestCore, want };
}

const todo = [];
for (let n = FIRST; n <= COUNT; n++) if (!ONLY || ONLY.has(n)) todo.push(n);
const results = [];
let next = 0;
await Promise.all(Array.from({ length: Math.min(JOBS, todo.length) }, async () => { while (next < todo.length) results.push(await tune(todo[next++])); }));
results.sort((a, b) => a.n - b.n);
for (const r of results) {
  table[r.n] = r.diff;
  const file = join(MAPS, `${String(r.n).padStart(3, '0')}.cwtd`);
  writeFileSync(file, readFileSync(file, 'utf8').replace(/^diff .*$/m, `diff ${r.diff.toFixed(3)}`));
  console.log(`map ${r.n}: diff ${r.diff.toFixed(2)}, the bots keep ${r.core} of 20 (wanted ${r.want})`);
}
writeFileSync(TABLE, JSON.stringify(Object.fromEntries(Object.entries(table).sort((a, b) => +a[0] - +b[0])), null, 1) + '\n');
