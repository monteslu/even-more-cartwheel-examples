// beatsync, no strobe: every effect that changes the light on screen feeds a flash meter (src/flash_meter.c), and this check
// reads the meters after a ranged romdev run of the autopilot cart on GL and WebGPU: the whole first song on Hard
// (kick shockwaves, clap sparks, the bass waveform, the hit flares), then the crowd sent home (the dip to dark). Each
// effect must have shown (its peak) and flashed at most 3 times in any second (WCAG 2.3.1 and the Harding test's bound).
//
// The CONTROL must fail: the `flash_strobe` debug request switches every effect on and off every 4 ticks for two
// seconds, and every meter must then read more than 3 flashes a second.
//
// Then the picture itself: romdev's frame({op:'flash'}) checks every frame of the densest songs played whole on Hard by
// the autopilot, from the count-in to the results, each with its 8 Hz strobe control that must fail: the song with the
// most notes a second on Hard (tests/lib/beatsync.mjs densest) and the one with the most notes (Acid House Rave, 726),
// and the densest song on Easy too, where the notes are drawn brighter (src/main.c NOTE_LIGHT).
//
//   node tests/beatsync-flash.mjs [--gpu gl|webgpu|both] [--no-picture | --picture-only] [--song <id>] [--diff easy|hard]
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/romdev.mjs';
import { densest, gpusOf, openBeatsync, songs } from './lib/beatsync.mjs';

const argv = process.argv.slice(2);
const GPUS = gpusOf(argv);
const CFG = JSON.parse(readFileSync(join(ROOT, 'tests/beatsync-game.json'), 'utf8')).flash;
const MAX = 3;

let failed = 0;
const only = argv.includes('--picture-only');
for (const gpu of GPUS) {
  if (!only) {
    // the run: the autopilot plays the first song on Hard, then the crowd is sent home (the failed song's dip)
    const r = await openBeatsync('beatsync_auto', { gpu, tag: 'flash' });
    try {
      for (const step of CFG.run) {
        await r.stepTo(step.frame);
        if (step.command) await r.command(step.command);
      }
      await r.stepTo(CFG.frames);
      const f = await r.fields();
      for (const fx of CFG.effects) {
        const n = f[`fl.${fx}_flashes`], peak = f[`fl.${fx}_peak`];
        if (n === undefined) { console.log(`FAIL flash (${gpu}): no meter fl.${fx}_flashes in the game`); failed++; continue; }
        if (!(peak >= 0.3)) { console.log(`FAIL flash ${fx} (${gpu}): the effect never showed in the run (peak ${peak}): the check proves nothing`); failed++; continue; }
        if (n > MAX) { console.log(`FAIL flash ${fx} (${gpu}): ${n} flashes in one second (at most ${MAX}): make it swell and fade`); failed++; }
        else console.log(`ok   flash ${fx} (${gpu}): at most ${n} flash${n === 1 ? '' : 'es'} a second, peak ${peak.toFixed(2)}`);
      }
    } finally {
      await r.close();
    }
    // the control: every effect strobing at 7.5 Hz must read as a strobe
    const c = await openBeatsync('beatsync_auto', { gpu, tag: 'flash-control' });
    try {
      await c.stepTo(60);
      for (const fx of CFG.effects) await c.command(`flash_strobe ${fx}`);
      await c.step(130);
      const f = await c.fields();
      for (const fx of CFG.effects) {
        const n = f[`fl.${fx}_flashes`];
        if (n > MAX) console.log(`ok   flash control ${fx} (${gpu}): strobing reads ${n} flashes a second, as it must`);
        else { console.log(`FAIL flash control ${fx} (${gpu}): strobing read ${n} flashes a second: the meter cannot see a strobe`); failed++; }
      }
    } finally {
      await c.close();
    }
  }
  // the picture itself: romdev's flash check over each of those songs whole on Hard as the autopilot plays it (every
  // frame, box-filtered, WCAG 2.3.1 and Harding), with its own control (an 8 Hz strobe mixed in) that must fail
  if (CFG.picture && !argv.includes('--no-picture')) {
    const all = songs(), most = all.reduce((a, b) => (b.charts.hard.length > a.charts.hard.length ? b : a));
    const songsHard = argv.includes('--song') ? [all.find((x) => x.id === argv[argv.indexOf('--song') + 1])] : [densest().song, most].filter((s, i, a) => a.findIndex((x) => x.id === s.id) === i);
    const diffs = argv.includes('--diff') ? [argv[argv.indexOf('--diff') + 1]] : ['hard', 'easy'];
    const pick = diffs.flatMap((diff) => (diff === 'hard' ? songsHard : songsHard.slice(0, 1)).map((song) => ({ song, diff })));
    for (const { song, diff } of pick) {
      const D = diff === 'easy' ? 'Easy' : 'Hard';
      const frames = Math.ceil((song.length_s + 4) * 60) + (CFG.picture.tail || 120); // the 4 s count-in, the song, the results
      const q = await openBeatsync('beatsync_auto', { gpu, tag: 'flash-picture' });
      try {
        const res = await q.call('frame', { op: 'flash', frames, commands: [`song ${song.id} ${diff}`], maxSeconds: 1800 }, { timeoutMs: 1900000 });
        const m = res.result || {}, w = m.worstSecond;
        const nums = `worst second ${JSON.stringify(w)}, motion ${m.motion}`;
        if (res.checkOk) console.log(`ok   flash picture (${gpu}, ${song.id} on ${D}, ${frames} frames): ${res.verdict}, ${nums}; its strobe control fails (${JSON.stringify(res.control?.problems ?? res.control?.verdict ?? res.control).slice(0, 160)}), as it must`);
        else { console.log(`FAIL flash picture (${gpu}, ${song.id} on ${D}): verdict ${res.verdict}, problems ${JSON.stringify(res.problems)}, control ${JSON.stringify(res.control?.verdict)}, ${nums}`); failed++; }
        // the run was the song it names, played to the end (else the picture proves nothing about it)
        const f = await q.fields();
        const played = f['song.done'] === 1 && f['notes.total'] === song.charts[diff].length && f['notes.hit'] === song.charts[diff].length;
        console.log(`${played ? 'ok  ' : 'FAIL'} flash picture (${gpu}): the autopilot played ${song.id} on ${D} to the end (${f['notes.hit']} of ${f['notes.total']} notes hit, chart ${song.charts[diff].length}, done ${f['song.done']})`);
        if (!played) failed++;
      } finally {
        await q.close();
      }
    }
  }
}
console.log(failed ? `beatsync-flash: ${failed} failed` : 'beatsync-flash: ok');
process.exit(failed ? 1 : 0);
