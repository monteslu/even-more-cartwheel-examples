// adventure grounding: nothing in the world floats. The game builds the whole world (every region at once) and checks
// every prop's base against the walkable ground under it (terrain, steps, floors) and every building box's foot
// against what is under its corners; anything more than 3 cm off counts (ADV_GROUND_CHECK=1 prints the totals).
// The check must find none, over at least 60 props and 100 boxes. CONTROL that must fail: the same build with one
// meadow tree lifted half a metre (ADV_FLOAT_BUG=1). Software GL, off the shared GPU.
//
//   node tests/adventure-grounding.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/adventure/adventure_auto');
function run(extra = {}) {
  const env = { ...process.env, LIBGL_ALWAYS_SOFTWARE: '1', SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl', CW_FRAMES: '3', CW_WARMUP_SPREAD: '0',
    CW_PAK: exe + '.cwpak', ADV_GROUND_CHECK: '1', ...extra };
  const r = spawnSync(exe, [], { env, encoding: 'utf8', timeout: 300000, maxBuffer: 1 << 26 });
  const out = (r.stdout || '') + (r.stderr || '');
  const m = /adv-ground props=(\d+) boxes=(\d+) floating=(\d+) worst_cm=(\d+)/.exec(out);
  return { status: r.status, m: m && { props: +m[1], boxes: +m[2], floating: +m[3], worst: +m[4] }, lines: out.split('\n').filter((l) => /floats/.test(l)) };
}
let bad = 0;
const r = run();
if (!r.m || r.status !== 0) { bad++; console.log(`FAIL no check ran (status ${r.status})`); }
else if (r.m.floating || r.m.props < 60 || r.m.boxes < 100) { bad++; console.log(`FAIL ${r.m.floating} floating of ${r.m.props} props and ${r.m.boxes} boxes (worst ${r.m.worst} cm)\n  ${r.lines.slice(0, 10).join('\n  ')}`); }
else console.log(`ok   ${r.m.props} props and ${r.m.boxes} boxes all on the ground (worst ${r.m.worst} cm)`);
const c = run({ ADV_FLOAT_BUG: '1' });
if (!c.m || !c.m.floating) { bad++; console.log('FAIL control: a tree lifted half a metre was not caught'); }
else console.log(`ok   control fails as it must: ${c.m.floating} floating, worst ${c.m.worst} cm`);
process.exit(bad ? 1 : 0);
