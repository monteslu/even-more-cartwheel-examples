// dev runner: the physics cost of every landmark after a charge (or a leg taken out), as rows of JSON:
// node tests/destructo-headroom.mjs [--leg] [--gpu gl|webgpu] [level ...]   (level numbers from 0)
import { runCheck } from './lib/romdev.mjs';
const a = process.argv.slice(2);
const gi = a.indexOf('--gpu');
const gpu = gi >= 0 ? a.splice(gi, 2)[1] : 'gl';
const leg = a.includes('--leg') ? (a.splice(a.indexOf('--leg'), 1), true) : false;
const r = await runCheck('levels', ['--legout', ...(leg ? [] : ['--charge']), ...a], { gpu });
for (const row of r.rows) {
  if (row.phase === 'standing') console.log(JSON.stringify({ level: row.level, name: row.name, phase: row.phase, placed: row.placed, blocks: row.blocks, chunks_standing: row.chunks_standing }));
  else if (row.phase === 'legout' && row.t === 25) console.log(JSON.stringify({ level: row.level, name: row.name, phase: 'spikes', tick_ms_max: row.tick_ms_max, ms_settype: row.ms_settype, ms_carve: row.ms_carve, ms_resolve: row.ms_resolve, ms_support: row.ms_support }));
  else if (row.phase === 'window') console.log(JSON.stringify(row));
}
process.stderr.write(`threads ${r.threads} status ${r.status}\n`);
process.exit(r.status);
