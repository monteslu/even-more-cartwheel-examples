// towers soak gate: 30 game minutes with the bots at 1, 2, 3 and 4 players, map after map, no window and no GPU
// (towers_soak in build/native). Each run must exit 0 and end with "soak ok", hold at least two maps, and keep resident
// memory within 5% of the first minute (maps load and unload all run long). The control puts a NaN into a creep's
// health and the checks must catch it (exit 1). Run: node tests/towers-soak.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/towers/towers_soak');
const run = (players, env = {}) => spawnSync(exe, ['30', String(players), '1'], { encoding: 'utf8', timeout: 1800000, env: { ...process.env, ...env } });
let bad = 0;
for (const p of [1, 2, 3, 4]) {
  const r = run(p);
  const out = r.stdout || '';
  const ok = r.status === 0 && /soak ok: 30 minutes/.test(out);
  const maps = +((/(\d+) maps held/.exec(out) || [])[1] || 0);
  const mem = [...out.matchAll(/rss (\d+) kB \(first minute (\d+)\)/g)].map((m) => +m[1] / +m[2]);
  const growth = mem.length ? Math.max(...mem) : 99;
  if (!ok || maps < 2 || growth > 1.05) { console.log(`FAIL soak ${p} players: status ${r.status}, ${maps} maps, memory x${growth.toFixed(3)}\n${out.slice(-600)}`); bad++; }
  else console.log(`ok   soak ${p} players: ${maps} maps held (memory x${growth.toFixed(3)} at worst)`);
}
const c = run(2, { TOWERS_SOAK_INJECT: '1' });
if (c.status === 1 && /soak FAIL/.test(c.stdout)) console.log('ok   control: an injected NaN is caught');
else { console.log(`CONTROL PASSED (a failure): the injected NaN went unseen (status ${c.status})`); bad++; }
process.exit(bad ? 1 : 0);
