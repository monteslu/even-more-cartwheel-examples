#!/usr/bin/env node
/* node tools/spots.mjs <lm_data name> [n] [minFrac]: crystal spots for a landmark made from cells: flat tops (a 5x5 block of columns with one top) at least
 * minFrac of the tallest top, spread apart (farthest first); prints C initialiser pairs (x, z) in the grid's own metres */
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const [name, nS = '2', fracS = '0.45'] = process.argv.slice(2);
const src = readFileSync(join(dirname(fileURLToPath(import.meta.url)), '../src/lm_data.c'), 'utf8');
const runs = src.match(new RegExp(`static const uint8_t ${name}_runs\\[\\] = \\{([^}]*)\\}`))[1].split(',').map((t) => t.trim()).filter(Boolean).map(Number);
const m = src.match(new RegExp(`const lm_vox LMV_${name} = \\{ (\\d+), (\\d+), (\\d+), ([\\d.]+)f, ([\\d.]+)f, (-?[\\d.]+)f, (-?[\\d.]+)f`));
const [nx, ny, nz] = [+m[1], +m[2], +m[3]], cell = +m[4], course = +m[5], x0 = +m[6], z0 = +m[7];
const g = new Uint8Array(nx * ny * nz);
let c = 0;
for (let r = 0; r < runs.length; r += 2) for (let t = 0; t < runs[r + 1]; t++) g[c++] = runs[r];
const top = new Int16Array(nx * nz).fill(-1);
for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) for (let k = 0; k < nz; k++) if (g[(j * nx + i) * nz + k]) top[i * nz + k] = j;
const maxTop = Math.max(...top);
const R = 2, cand = [];
for (let i = R; i < nx - R; i++) for (let k = R; k < nz - R; k++) {
  const t = top[i * nz + k];
  if (t < maxTop * +fracS) continue;
  let ok = true;
  for (let a = -R; a <= R && ok; a++) for (let b = -R; b <= R && ok; b++) ok = top[(i + a) * nz + k + b] === t;
  if (ok) cand.push([x0 + (i + 0.5) * cell, z0 + (k + 0.5) * cell, t]);
}
const pick = [];
cand.sort((a, b) => b[2] - a[2]);
if (cand.length) pick.push(cand[0]);
while (pick.length < +nS && cand.length > pick.length) {
  let best = null, bd = -1;
  for (const p of cand) { const d = Math.min(...pick.map((q) => Math.hypot(p[0] - q[0], p[1] - q[1]))); if (d > bd) bd = d, best = p; }
  if (bd < 0.8) break;
  pick.push(best);
}
console.log(`${name}: ${cand.length} flat spots; maxTop ${(maxTop * course).toFixed(1)} m; { ${pick.map((p) => `{ ${p[0].toFixed(2)}f, ${p[1].toFixed(2)}f }`).join(', ')} } tops ${pick.map((p) => (p[2] * course).toFixed(1)).join(' ')}`);
