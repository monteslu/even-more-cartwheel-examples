// skate soak gate: bots skate every park in turn for 30 game minutes at 1, 2, 3 and 4 players, natively, no window and
// no GPU (build/native/games/skate/skate_check soak). Each run must end "soak ok": no NaN in any skater, nobody leaves
// the park or ends up under a surface, nobody falls out of the world; it must skate lines (at least 20 a player) and
// keep resident memory within 5% of its first minute. The control puts a NaN in a velocity and the checks must catch
// it (exit 1). Run: node tests/skate-soak.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/skate/skate_check');
const run = (players, env = {}) => spawnSync(exe, ['soak', '30', String(players)], { encoding: 'utf8', timeout: 1800000, env: { ...process.env, ...env } });
let bad = 0;
for (const p of [1, 2, 3, 4]) {
  const r = run(p);
  const out = r.stdout || '';
  const ok = r.status === 0 && /soak ok: 30 minutes/.test(out);
  const lines = +((/(\d+) lines skated/.exec(out) || [])[1] || 0);
  const mem = [...out.matchAll(/rss (\d+) kB \(first minute (\d+)\)/g)].map((m) => +m[1] / +m[2]);
  const growth = mem.length ? Math.max(...mem) : 99;
  const stats = (/soak stats: (.*)/.exec(out) || [])[1] || '';
  if (!ok || lines < 20 * p || growth > 1.05) { console.log(`FAIL soak ${p} players: status ${r.status}, ${lines} lines, memory x${growth.toFixed(3)}\n${out.slice(-600)}`); bad++; }
  else console.log(`ok   soak ${p} players: ${stats} (memory x${growth.toFixed(3)} at worst)`);
}
const c = run(2, { SKATE_SOAK_INJECT: '1' });
if (c.status === 1 && /soak FAIL/.test(c.stdout)) console.log('ok   control: an injected NaN is caught');
else { console.log(`CONTROL PASSED (a failure): the injected NaN went unseen (status ${c.status})`); bad++; }
process.exit(bad ? 1 : 0);
