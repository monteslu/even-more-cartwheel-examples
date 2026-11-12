// dev runner: the dam's water check on romdev, rows printed. node tests/destructo-water-run.mjs [--control]
import { runCheck } from './lib/romdev.mjs';
const r = await runCheck('levels', ['--water', ...process.argv.slice(2)], {});
console.log(r.text);
process.exit(r.status);
