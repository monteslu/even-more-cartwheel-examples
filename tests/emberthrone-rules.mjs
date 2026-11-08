// emberthrone combat feel rules: emberthrone_auto with ADV_RULES=1 builds the real world (software GL, nothing drawn)
// and plays small scenes through the real rules (games/emberthrone/rules.c says what each check measures): the
// two-sided hit-stop and its ladder by blow, knockback by blow, the critical after a broken poise, the cut's magnet,
// the shield bearer's guard, the slinger's stones, foes circling to the player's back, a boss spent after a big blow
// and chaining blows in its later phase, a sip in the flask for a beaten group, the marsh lurker, pad rumble on a hit.
// Then each rule is switched off in turn and its own check must fail (the control lines).
// Run: node tests/emberthrone-rules.mjs   (after TARGETS=native scripts/build.sh emberthrone_auto)
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/emberthrone/emberthrone_auto');
if (!existsSync(exe)) { console.log(`FAIL no ${exe}: TARGETS=native scripts/build.sh emberthrone_auto`); process.exit(1); }
const env = { ...process.env, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_WORKERS: '2', SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy',
  CW_GPU: 'gl', ADV_QUALITY: '0', CW_RENDER_SCALE: '0.25', CW_FRAMES: '10', CW_PAK: exe + '.cwpak', CW_WARMUP_SPREAD: '0', ADV_RULES: '1' };
delete env.ADV_RULES_OFF;
const r = spawnSync('nice', ['-n', '10', exe], { env, encoding: 'utf8', timeout: 600000 });
const lines = (r.stdout || '').trim().split('\n').filter((l) => /^(rules|control|controls|\w+\s+(ok|FAIL)\s)/.test(l));
console.log(lines.map((l) => `     ${l}`).join('\n'));
let bad = 0;
if (r.status !== 0 || !/^rules ok$/m.test(r.stdout)) { console.log('FAIL the rules'); bad++; } else console.log('ok   rules');
const controls = lines.filter((l) => l.startsWith('control '));
if (controls.length < 13 || !/^controls ok$/m.test(r.stdout)) { console.log(`FAIL the controls (${controls.length} ran)`); bad++; } else console.log(`ok   ${controls.length} controls fail as they must`);
process.exit(bad ? 1 : 0);
