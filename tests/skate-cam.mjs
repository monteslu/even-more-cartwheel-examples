// skate camera gate: the chase camera comes back behind a rider after vert air. On the Timber Yard halfpipe the first
// skater is sent at a vert wall (11.5 m/s, every 20 s) and coasts: it airs out over the coping, comes back down onto the
// wall rolling the other way, rides up the far wall and back, and so on. The native app on the test GPU, headless,
// through the GPU suite lock, logs its camera every frame (SKATE_CAMLOG). Each vert landing is then judged from one to
// two and a half seconds after it, on the frames the rider is moving (1.5 m/s or more):
//   - the camera is behind the rider along its run (the rider's run and the line from the camera to the rider within
//     60 degrees) on at least 80% of them
//   - it is never closer than 2.3 m across the ground
// At least two vert landings must be seen (a run without them measured nothing). The control turns the camera with
// the old lerp toward the run, which never turns when the run is right about: it must fail.
// Run: node tests/skate-cam.mjs
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'skate-cam', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { glPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
mkdirSync(join(ROOT, 'out'), { recursive: true });

function run(extra) {
  const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/skate_auto.json'), 'utf8')).binary;
  const env = { ...process.env, CW_FRAMES: '3700', SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl', CW_WARMUP_SPREAD: '0', SKATE_PARK: '4', SKATE_PLAYERS: '1', SKATE_IDLE: '1',
    SKATE_VERT_EVERY: '1200', SKATE_VERT_AT: '-14,0,0,11.5', SKATE_CAMLOG: '0', SKATE_NO_HUD: '1', ...extra };
  const gl = glPin();
  if (gl) Object.assign(env, gl.env);
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '600', exe], { env, encoding: 'utf8', maxBuffer: 1 << 28 });
  if (r.status) return { error: `skate_auto exited ${r.status}` };
  return { rows: (r.stderr || '').split('\n').filter((l) => l.startsWith('bench cam ')).map((l) => {
    const a = l.split(/\s+/), at = (k) => a.lastIndexOf(k) + 1;
    return { st: +a[at('st')], vert: +a[at('vert')], p: [+a[at('p')], +a[at('p') + 2]], c: [+a[at('cam')], +a[at('cam') + 2]] };
  }) };
}

function judge(res) {
  if (res.error) return { why: [res.error], text: res.error };
  const L = res.rows, why = [];
  let lands = 0, worstShare = 1, worstDist = 99;
  for (let i = 1; i < L.length; i++) {
    if (!(L[i - 1].st === 1 && L[i - 1].vert === 1 && L[i].st === 0)) continue;
    lands++;
    let seen = 0, behind = 0;
    for (let j = i + 60; j <= i + 150 && j < L.length; j++) {
      const q = L[j], o = L[j - 6], run = [q.p[0] - o.p[0], q.p[1] - o.p[1]], sp = Math.hypot(...run) * 10;
      const back = [q.p[0] - q.c[0], q.p[1] - q.c[1]], d = Math.hypot(...back);
      worstDist = Math.min(worstDist, d);
      if (sp < 1.5 || d < 1e-3) continue;
      seen++;
      if ((run[0] * back[0] + run[1] * back[1]) / (Math.hypot(...run) * d) >= 0.5) behind++;
    }
    if (seen) worstShare = Math.min(worstShare, behind / seen);
  }
  if (lands < 2) why.push(`${lands} vert landings (2 at least)`);
  if (worstShare < 0.8) why.push(`behind the rider on ${(worstShare * 100).toFixed(0)}% of a landing's frames (80% at least)`);
  if (worstDist < 2.3) why.push(`${worstDist.toFixed(2)} m from the rider (2.3 at least)`);
  return { why, text: `${lands} vert landings, behind the rider on ${(worstShare * 100).toFixed(0)}% of the frames at worst, ${worstDist.toFixed(2)} m away at the closest` };
}

let bad = 0;
const r = judge(run({}));
if (r.why.length) { bad++; console.log(`FAIL camera: ${r.text}\n     ${r.why.join('; ')}`); } else console.log(`ok   camera: ${r.text}`);
const c = judge(run({ SKATE_CAM_LERP: '1' }));
if (c.why.length) console.log(`ok   control fails as it must (the old lerp): ${c.text}\n     ${c.why.join('; ')}`);
else { bad++; console.log(`CONTROL PASSED (a failure): ${c.text}`); }
process.exit(bad ? 1 : 0);
