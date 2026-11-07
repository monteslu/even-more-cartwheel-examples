// Writes assets/roots.txt: every file the carts pack (the scenes, the project, the models, the textures).
//   node games/adventure/tools/gen-roots.mjs
import { readdirSync, statSync, writeFileSync } from 'node:fs';
import { join, dirname, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
const A = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets');
const out = [];
const walk = (d) => { for (const f of readdirSync(d).sort()) { const p = join(d, f); if (f.startsWith('.')) continue; if (statSync(p).isDirectory()) walk(p); else if (!f.endsWith('roots.txt')) out.push(relative(A, p)); } };
walk(A);
writeFileSync(join(A, 'roots.txt'), '# adventure: what its carts pack. Written by tools/gen-roots.mjs (edit that, not this file).\n' + out.join('\n') + '\n');
console.log(out.length, 'files');
