// destructo rigidity measurements: `check run levels --rigid ...` in the check cart on romdev, its JSON lines to stdout (one per
// landmark and phase). Run: node tests/destructo-rigid-measure.mjs [--gpu gl|webgpu] [--out file] -- [--charge] [--opt NAME=VALUE]... [level ...]
import { writeFileSync } from 'node:fs';
import { runCheck } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
const dd = argv.indexOf('--');
const mine = dd >= 0 ? argv.slice(0, dd) : [];
const args = dd >= 0 ? argv.slice(dd + 1) : argv;
const opt = (n, d) => { const i = mine.indexOf(n); return i >= 0 ? mine[i + 1] : d; };
const r = await runCheck('levels', ['--rigid', ...args], { gpu: opt('--gpu', 'gl') });
if (r.status !== 0) { console.log(`FAIL exit ${r.status}\n${r.text.slice(-1500)}`); process.exit(1); }
const lines = r.rows.map((x) => JSON.stringify(x)).join('\n');
if (opt('--out')) writeFileSync(opt('--out'), lines + '\n');
console.log(lines);
