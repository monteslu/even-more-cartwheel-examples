// dev: the bot's play of the named levels (indices), one row each: placed, solved, shots against par, the share brought down against the mark
// node tests/destructo-headroom-marks.mjs [--gpu gl] level ...
import { runCheck } from './lib/romdev.mjs';
const a = process.argv.slice(2);
const gi = a.indexOf('--gpu');
const gpu = gi >= 0 ? a.splice(gi, 2)[1] : 'gl';
const r = await runCheck('levels', a, { gpu });
for (const x of r.rows) if (x.name !== undefined && x.solved !== undefined)
  console.log(`${x.level} ${x.name}: blocks ${x.blocks} solved ${x.solved} shots ${x.shots}/${x.par} share ${(x.share_after * 100).toFixed(0)}% mark ${(x.mark * 100).toFixed(0)}% ${x.solved && x.shots <= x.par && x.share_after >= x.mark - 0.005 ? 'ok' : 'FAIL'}`);
process.exit(r.status);
