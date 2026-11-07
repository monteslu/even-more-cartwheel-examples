#!/usr/bin/env node
// The map check's controls: writes tools/maps-broken/<check>-<what>.cwtd, each map 1 broken on purpose in one way
// (tools/check-maps.mjs must fail each on the check its name starts with). Run after gen-maps.mjs.
//   node tools/gen-broken.mjs
import { mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const B = join(HERE, 'maps-broken');
const L = readFileSync(join(HERE, '../assets/maps/001.cwtd'), 'utf8').split('\n');
const gi = L.indexOf('grid');
const [, W, H] = L.find((l) => l.startsWith('size ')).split(' ').map(Number);
const rows = L.slice(gi + 1, gi + 1 + H).map((r) => r.split(''));
const core = L.find((l) => l.startsWith('core ')).split(' ').slice(1).map(Number);
const renum = (lines, n) => lines.map((l) => (l === 'map 1' ? `map ${n}` : l));
const withRows = (rs) => [...L.slice(0, gi + 1), ...rs.map((r) => r.join('')), ...L.slice(gi + 1 + H)];
const before = (lines, prefix, line) => { const out = [...lines]; out.splice(out.findIndex((l) => l.startsWith(prefix)), 0, line); return out; };
const put = (name, why, lines) => writeFileSync(join(B, name), [`# broken on purpose: ${why}`, ...lines].join('\n'));

mkdirSync(join(B, 'lightmaps'), { recursive: true });
for (const f of readdirSync(B)) if (f.endsWith('.cwtd')) rmSync(join(B, f));
{ // path: a ring of ground cut across every way into the core
  const r = rows.map((x) => x.slice());
  for (let y = core[1] - 2; y <= core[1] + 3; y++)
    for (let x = core[0] - 2; x <= core[0] + 3; x++)
      if (y >= 0 && x >= 0 && y < H && x < W && (y === core[1] - 2 || y === core[1] + 3 || x === core[0] - 2 || x === core[0] + 3) && r[y][x] === '.') r[y][x] = 'g';
  put('path-cut.cwtd', 'the path is cut round the core', renum(withRows(r), 901));
}
{ // slots: a pad nowhere near the path
  const r = rows.map((x) => x.slice());
  let p = null;
  for (let y = 0; y < H && !p; y++)
    for (let x = 0; x < W && !p; x++) {
      let near = false;
      for (let b = -3; b <= 3; b++) for (let a = -3; a <= 3; a++) { const q = r[y + b]?.[x + a]; if (q === '.' || q === 'o') near = true; }
      if (!near && r[y][x] === 'g') p = [x, y];
    }
  r[p[1]][p[0]] = 'o';
  put('slots-far.cwtd', 'a slot nowhere near the path', before(renum(withRows(r), 902), 'slot ', `slot ${p[0]} ${p[1]} 0 0 0`));
}
put('shares-one-seat.cwtd', 'two players, every slot dealt to the first', renum(L, 903).map((l) => l.replace(/^slot (\d+) (\d+) \d/, 'slot $1 $2 0')));
{ // grounded: a tree standing in the path
  let p = null;
  for (let y = 1; y < H - 1 && !p; y++) for (let x = 1; x < W - 1 && !p; x++) if (rows[y][x] === '.' && rows[y][x - 1] === 'g') p = [x, y];
  put('grounded-tree-in-path.cwtd', 'a tree standing in the path, below the ground it was made for', before(renum(L, 904), 'prop ', `prop ${p[0]} ${p[1]} decor 1 0`));
}
{ // terrain: a pool over the core's block of path (the creeps would walk under its water)
  put('terrain-pool-on-path.cwtd', "a pool cut into the core's block of path", before(renum(L, 909), 'prop ', `feature water ${core[0] + 1} ${core[1] + 1} 2.2 0`));
}
put('floor-narrow.cwtd', 'its routes drawn too narrow for the creeps to pass (the tiles as they were)', renum(L, 910).map((l) => (l.startsWith('route ') ? l.split(' ').map((v, i) => (i && i % 3 === 0 ? String(+(+v * 0.3).toFixed(3)) : v)).join(' ') : l)));
put('bake-missing.cwtd', 'no bake', renum(L, 905));
put('bake-stale.cwtd', 'its bake hash is of another map', renum(L, 906));
writeFileSync(join(B, 'lightmaps/906.cwlm'), 'x');
writeFileSync(join(B, 'lightmaps/906.hash'), '0000000000000000\n');
put('clear-overwhelmed.cwtd', 'far too strong for anyone to hold', renum(L, 907).map((l) => l.replace(/^diff .*/, 'diff 12').replace(/^waves .*/, 'waves 18')));
console.log('gen-broken: 9 controls');
