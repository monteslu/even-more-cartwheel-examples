// Frame strips of the riders in motion, for judging the posing: the native app (headless, on the test GPU through the
// GPU suite lock), paused and stepped over the debug channel, a frame captured at each tick asked for, the rider cropped
// from the middle of each and laid side by side (ImageMagick).
//   node games/skate/tools/strip.mjs <out.png> <app> <ticks: a,b,c or a:b:step> [KEY=VALUE ...]
// SKATE_VIEWCAM=az,el,dist,fov (src/view.c) puts the camera at a set angle round the rider; CROP=WxH the crop
// (default 360x480), INPUT=<tests/x.json> a pad script. Run several strips in one lock hold with " -- " between them.
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ENGINE, 'tools/with-gpu-suite.mjs'), 'skate-strip', 'node', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const { launch } = await import(join(ENGINE, 'tests/agent/channel.mjs'));
const { glPin, wgpuPin, scrubDisplay } = await import(join(ENGINE, 'tools/lib/wgpu-pin.mjs'));
const specs = [[]];
for (const a of process.argv.slice(2)) a === '--' ? specs.push([]) : specs.at(-1).push(a);
let bad = 0;
for (const [out, app, tickSpec, ...kv] of specs) {
  const env = { SDL_AUDIODRIVER: 'dummy', SDL_VIDEODRIVER: 'offscreen', CW_WGPU_VALIDATION: '0', CW_WARMUP_SPREAD: '0', CW_START_PAUSED: '1', SKATE_NO_HUD: '1' };
  for (const p of kv) { const i = p.indexOf('='); env[p.slice(0, i)] = p.slice(i + 1); }
  if (env.INPUT) env.CW_INPUT = resolve(ROOT, env.INPUT);
  const pin = env.CW_GPU === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env); else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  const full = { ...process.env, ...env };
  scrubDisplay(full);
  let ticks = [];
  const exe = JSON.parse(readFileSync(join(ROOT, `build/native/apps/${app}.json`), 'utf8')).binary;
  if (tickSpec.startsWith('find')) {
    // find<state>@<from>:<before>:<after>:<step>: the first tick from <from> that the first skater is in <state> (0 roll,
    // 1 air, 2 grind, 3 bail; 4 its first manual), found by a first run, then the frames round it
    const [st, rest] = tickSpec.slice(4).split('@'), [from, before, after, step] = rest.split(':').map(Number);
    const g0 = await launch(exe, [], full, { timeoutMs: 60000 });
    let t = (await g0.call(`step ${from}`, 120000)).tick ?? from, hit = -1;
    for (let k = 0; k < 4000 && hit < 0; k++) {
      const r = await g0.call('fields skater.state skater.manuals', 30000), f = r.fields || r;
      if ((+st === 4 && (f['skater.manuals'] || [])[0] > 0) || (+st < 4 && (f['skater.state'] || [])[0] === +st)) hit = t;
      else t = (await g0.call('step 1')).tick ?? t + 1;
    }
    g0.close();
    if (hit < 0) { console.log(`${out}: state ${st} not reached`); bad = 1; continue; }
    for (let u = Math.max(1, hit - before); u <= hit + after; u += step) ticks.push(u);
    console.log(`${out}: state ${st} at tick ${hit}`);
  } else if (tickSpec.includes(':')) { const [a, b, s] = tickSpec.split(':').map(Number); for (let t = a; t <= b; t += s) ticks.push(t); }
  else ticks = tickSpec.split(',').map(Number);
  const dir = resolve(out + '.d');
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const g = await launch(exe, [], full, { timeoutMs: 60000 });
  try {
    let now = (await g.call('status')).tick ?? 0;
    const files = [];
    const cmds = (env.CMDS || '').split(';').filter(Boolean).map((c) => { const i = c.indexOf(':'); return [+c.slice(0, i), { text: c.slice(i + 1) }]; });
    for (const t of ticks) {
      for (const [at, cmd] of cmds) if (at <= t && !cmd.done) { // CMDS=tick:request;tick:request: debug requests at those ticks
        if (at > now) { const r = await g.call(`step ${at - now}`, 120000); now = r.tick ?? at; }
        console.log(`${cmd.text}: ${JSON.stringify(await g.call(cmd.text, 60000))}`), cmd.done = true;
      }
      if (t > now) { const r = await g.call(`step ${t - now}`, 120000); now = r.tick ?? t; }
      const png = await g.screenshot('world', 1);
      const f = join(dir, `t${String(t).padStart(5, '0')}.png`);
      writeFileSync(f, png);
      files.push(f);
    }
    const crop = env.CROP || '360x480';
    writeFileSync(join(dir, 'crop.txt'), crop);
    const r = spawnSync('convert', [...files.flatMap((f) => ['(', f, '-gravity', 'center', '-crop', `${crop}+0+0`, '+repage', ')']), '+append', resolve(out)], { encoding: 'utf8' });
    if (r.status !== 0) console.log(`(no strip made: ${r.error ? 'ImageMagick missing' : r.stderr}; the frames are in ${dir}, games/skate/tools/assemble.sh lays them out)`);
    console.log(`${out}: ${ticks.length} frames, ticks ${ticks[0]}..${ticks.at(-1)}`);
  } catch (e) { bad = 1; console.log(`${out}: ${e.message}\n${g.output().slice(-1500)}`); }
  g.close();
}
process.exit(bad);
