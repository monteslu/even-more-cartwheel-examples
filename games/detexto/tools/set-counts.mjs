#!/usr/bin/env node
/* node tools/set-counts.mjs: NLEVELS in src/game.h from the rows of src/levels.c */
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const D = join(dirname(fileURLToPath(import.meta.url)), '../src');
const body = readFileSync(join(D, 'levels.c'), 'utf8');
const tab = body.slice(body.indexOf('const level_def LEVELS[NLEVELS] = {'));
const n = (tab.slice(0, tab.indexOf('\n};')).match(/^  LANDMARK\(/gm) || []).length;
let h = readFileSync(join(D, 'game.h'), 'utf8');
h = h.replace(/#define NLEVELS \d+/, `#define NLEVELS ${n}`);
writeFileSync(join(D, 'game.h'), h);
console.log(`NLEVELS ${n}`);
