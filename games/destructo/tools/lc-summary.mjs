// prints destructo_levelcheck's JSON lines as a table (development aid)
import { readFileSync } from 'node:fs';
const l = readFileSync(0, 'utf8').trim().split('\n').filter((s) => s.startsWith('{')).map((s) => JSON.parse(s));
for (const r of l) console.log(r.level, r.name.padEnd(15), 'blk', r.blocks, 'fl', r.floating, 'set', r.settle_move_mm.toFixed(0), r.settle_worst, 'brk', r.broken_settle, 'drift', r.drift_mm.toFixed(2), 'aw', r.awake_end, 'tm', r.targets_moved, 'solved', r.solved, `${r.shots}/${r.par}`, 'maxaw', r.max_awake, 'ms', r.step_ms_max);
