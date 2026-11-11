#!/usr/bin/env node
/* node tools/set-counts.mjs: NLEVELS and NSHOWN in src/game.h from the rows of src/levels.c (the rows before LANDMARKS_END are shown, the rest are the hidden kits) */
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const D = join(dirname(fileURLToPath(import.meta.url)), '../src');
const body = readFileSync(join(D, 'levels.c'), 'utf8');
const tab = body.slice(body.indexOf('const level_def LEVELS[NLEVELS] = {'));
const rows = (t) => (t.match(/^  (?:LANDMARK\(|\{ )"/gm) || []).length;
const [shown, hidden] = [rows(tab.slice(0, tab.indexOf('LANDMARKS_END'))), rows(tab.slice(tab.indexOf('LANDMARKS_END'), tab.indexOf('\n};')))];
let h = readFileSync(join(D, 'game.h'), 'utf8');
h = h.replace(/#define NLEVELS \d+/, `#define NLEVELS ${shown + hidden}`).replace(/#define NSHOWN \d+ /, `#define NSHOWN ${shown} `);
writeFileSync(join(D, 'game.h'), h);
console.log(`shown ${shown}, hidden ${hidden}, NLEVELS ${shown + hidden}`);
