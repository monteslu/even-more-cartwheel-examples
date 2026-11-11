// dev runner: one --legout (or any levels check) on romdev, rows printed. node tests/destructo-legout-run.mjs [--gpu gl|webgpu] <levels args...>
import { runCheck } from './lib/romdev.mjs';
const a = process.argv.slice(2);
const gi = a.indexOf('--gpu');
const gpu = gi >= 0 ? a.splice(gi, 2)[1] : 'gl';
const r = await runCheck('levels', a, { gpu });
console.log(r.text);
process.exit(r.status);
