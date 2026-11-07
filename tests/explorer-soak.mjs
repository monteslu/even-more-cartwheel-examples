// explorer soak (native, no window, no GPU): explorer_soak plays from the start through the input struct the pad
// fills (the bot, along the checker's route, with the assist) for up to 240 game minutes, one player and then two.
// Each run must exit 0 and end with "soak ok", keep positions and health finite and every player out of the rock every
// tick, keep resident memory within 5% of the first minute, and clear the game: every pickup, all six wardens, the
// heart down (won 1). The control puts a NaN into the player's speed and the checks must catch it (exit 1).
// Run: node tests/explorer-soak.mjs   (after TARGETS=native scripts/build.sh explorer_soak)
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = [join(ROOT, 'build/native/project/games/explorer/explorer_soak'), join(ROOT, 'build/native/games/explorer/explorer_soak')].find(existsSync);
const assets = join(ROOT, 'games/explorer/assets');
const MINUTES = 240, PICKUPS = 38, WARDENS = 6;
const run = (players, env = {}) => spawnSync(exe, [assets, String(MINUTES), String(players)], { encoding: 'utf8', timeout: 3600000, env: { ...process.env, ...env } });
let bad = 0;
for (const p of [1, 2]) {
  const r = run(p);
  const out = r.stdout || '';
  const m = /soak ok: (\d+) minutes, (\d+) pickups, (\d+) wardens, won (\d)/.exec(out);
  const mem = [...out.matchAll(/rss (\d+) kB \(first minute (\d+)\)/g)].map((x) => +x[1] / +x[2]);
  const growth = mem.length ? Math.max(...mem) : 99;
  const pick = m ? +m[2] : 0, ward = m ? +m[3] : 0;
  const won = m ? +m[4] : 0;
  const at = (/minute (\d+): .*pickups 38\/38/.exec(out) || [])[1];
  if (r.status !== 0 || !m || growth > 1.05 || pick < PICKUPS || ward < WARDENS || won !== 1) {
    console.log(`FAIL soak ${p} player${p > 1 ? 's' : ''}: status ${r.status}, ${pick} pickups, ${ward} wardens, won ${won}, memory x${growth.toFixed(3)}\n${out.split('\n').filter((l) => !l.startsWith('t')).slice(-6).join('\n')}`);
    bad++;
  } else console.log(`ok   soak ${p} player${p > 1 ? 's' : ''}: a full clear, ${pick} pickups, ${ward} wardens, won (every pickup by game minute ${at}; memory x${growth.toFixed(3)} at worst)`);
}
const c = run(1, { EXPLORER_SOAK_INJECT: '1' });
if (c.status === 1 && /soak FAIL/.test(c.stdout)) console.log('ok   control: an injected NaN is caught');
else { console.log(`CONTROL PASSED (a failure): the injected NaN went unseen (status ${c.status})`); bad++; }
process.exit(bad ? 1 : 0);
