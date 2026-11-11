// a level's index by its name, read from the order of the rows in games/destructo/src/levels.c (levels move as landmarks are added)
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const src = readFileSync(join(ROOT, 'games/destructo/src/levels.c'), 'utf8');
const body = src.slice(src.indexOf('const level_def LEVELS[NLEVELS] = {'));
const NAMES = [...body.slice(0, body.indexOf('\n};')).matchAll(/^  (?:LANDMARK\(|\{ )"([^"]+)"/gm)].map((m) => m[1]);
export const levelIndex = (name) => { const i = NAMES.indexOf(name); if (i < 0) throw new Error(`no level named ${name}`); return i; };
