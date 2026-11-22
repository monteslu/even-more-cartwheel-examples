// beatsync's haptics gate: the pad is felt on a miss (a short firm bump) and on a loss (a long buzz tapering over
// about 1.35 s), never in the count-in. Played on the real cart through romdev with the game's haptic.* counters
// (every rumble the game asks of the host):
//   - the count-in: no rumble at all (the control a game that buzzed on everything would fail);
//   - no button pressed: the first notes are missed, each with a 120 ms bump;
//   - `leave` (the floor empties): three steps of 450 ms on the playing pad, strong then softer.
//   node tests/beatsync-haptics.mjs [--gpu gl|webgpu|both]
import { enterSong, gpusOf, openBeatsync } from './lib/beatsync.mjs';

const GPUS = gpusOf(process.argv.slice(2));
const F = ['haptic.count', 'haptic.last_ms', 'haptic.total_ms', 'song.ms', 'notes.miss'];
let failed = 0;
const say = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
for (const gpu of GPUS) {
  const r = await openBeatsync('beatsync', { gpu, tag: 'haptics' });
  try {
    const a = await enterSong(r, { song: 0, diff: 0 }, F);
    say(a['haptic.count'] === 0, `haptics count-in (${gpu}): ${a['haptic.count']} rumbles before the first note`);
    const b = (await r.run([{ pads: [{ buttons: [] }], frames: 1200 }], { fields: F, stopWhen: { field: 'notes.miss', atLeast: 3 } })).fields;
    say(b['notes.miss'] >= 3 && b['haptic.count'] >= 3 && b['haptic.last_ms'] === 120,
      `haptics miss (${gpu}): ${b['notes.miss']} missed, ${b['haptic.count']} rumbles, the last ${b['haptic.last_ms']} ms`);
    await r.command('leave');
    const c = (await r.run([{ pads: [{ buttons: [] }], frames: 70 }], { fields: F })).fields;
    const loss = c['haptic.total_ms'] - b['haptic.total_ms'];
    say(loss >= 3 * 450 && c['haptic.last_ms'] === 450, `haptics loss (${gpu}): ${loss} ms of rumble asked after the floor emptied (3 steps of 450)`);
  } finally {
    await r.close();
  }
}
console.log(failed ? `beatsync-haptics: ${failed} failed` : 'beatsync-haptics: ok');
process.exit(failed ? 1 : 0);
