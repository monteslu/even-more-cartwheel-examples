// beatsync's highway in depth, through romdev on GL and WebGPU: the rivers recede to a far end near the top of the
// screen, and a note is drawn small far off and grows all the way to its gate.
//
// beatsync_auto plays Test Groove on Easy, Medium and Hard; at several moments of each the `notes` request returns
// every note on the rivers as that frame drew it (its lane, how many beats ahead, its y and width in pixels of the
// 1920 x 1080 frame, src/club.c). Per difficulty:
//   - size follows distance: in each lane, sorted from the far end to the gate, every note is wider than the one
//     behind it (strictly, at every moment), and a note at the gate is at least three times as wide as one at the
//     far end;
//   - the look-ahead: notes are drawn at least 1.9 times as many beats ahead as 0.31.0 showed (4, 3.2 and 2.6 beats),
//     the farthest within the top fifth of the frame;
//   - the near part reads as before: the distance a note covers in its last beat (what the player times against)
//     within 15% of 0.31.0's (290, 351 and 417 pixels), from the notes about a beat from the gate; its speed
//     crossing the gate (one frame of a note in its last tenth of a beat) is reported.
// The CONTROLS must fail: the same runs with `notescale flat` (every note one size) and `notescale reverse` (notes
// shrink as they come, the 0.31.0 condensing taken to its end) break the size rule.
//
//   node tests/beatsync-depth.mjs [--gpu gl|webgpu|both]
import { gpusOf, openBeatsync } from './lib/beatsync.mjs';

const argv = process.argv.slice(2);
const GPUS = gpusOf(argv);
const DIFF = ['easy', 'medium', 'hard'];
const OLD_AHEAD = [4, 3.2, 2.6];
// px a note covered in its last beat in 0.31.0: (TOP_Y - GATE_Y) / (1 - FAR) * (1 - 1 / (1 + c)) * 67.5, c = (1 / FAR - 1) / ahead
const OLD_LAST_BEAT = [290, 351, 417];
// frames after the song starts: 13 s of the groove, 37 frames apart (not a whole number of beats: every phase of the beat)
const MOMENTS = Array.from({ length: 21 }, (_, i) => 600 + 37 * i);

/** the notes at each moment of one difficulty, with a notescale */
async function sample(gpu, diff, scale) {
  const r = await openBeatsync('beatsync_auto', { gpu, tag: `depth-${scale}` });
  try {
    await r.command(`notescale ${scale}`);
    await r.command(`song test-groove ${diff}`);
    const base = r.now(), out = [];
    for (const m of MOMENTS) {
      await r.stepTo(base + m);
      const rep = await r.command('notes');
      rep.next = await r.command('notes'); // the frame after: each note's motion, for the speed at the gate
      out.push(rep);
    }
    return out;
  } finally {
    await r.close();
  }
}

function judge(moments, d) {
  let order = 0, pairs = 0, worst = '';
  let near = [], far = [], maxAhead = 0, topY = 1080, speed = [], lastBeat = [];
  for (const m of moments) {
    const notes = m.notes.filter((n) => n.ahead > 0);
    for (let l = 0; l < 3; l++) {
      const ln = notes.filter((n) => n.lane === l).sort((a, b) => b.ahead - a.ahead); // far end first
      for (let i = 1; i < ln.length; i++) {
        pairs++;
        if (!(ln[i].px > ln[i - 1].px)) { order++; worst ||= `lane ${l}: ${ln[i - 1].px.toFixed(1)} px at ${ln[i - 1].ahead.toFixed(2)} beats, then ${ln[i].px.toFixed(1)} px at ${ln[i].ahead.toFixed(2)}`; }
      }
      for (const n of ln) {
        if (n.ahead < 0.5) near.push(n.px / [1.44, 1.76, 2.0][l]);
        if (n.ahead > m.ahead - 1.2) far.push(n.px / [1.44, 1.76, 2.0][l]);
      }
    }
    for (const n of notes) if (n.ahead > maxAhead) { maxAhead = n.ahead; topY = n.y; }
    // the last beat: a note about a beat out, its distance to the gate a beat
    for (const n of notes.filter((x) => x.ahead > 0.85 && x.ahead < 1.15)) lastBeat.push((m.gate_y - n.y) / n.ahead);
    // the speed crossing the gate: a note in its last tenth of a beat, from this frame to the next
    for (const n of notes.filter((x) => x.ahead < 0.1)) {
      const m2 = m.next.notes.find((x) => x.lane === n.lane && x.ahead < n.ahead && n.ahead - x.ahead < 0.1);
      if (m2) speed.push((m2.y - n.y) / (n.ahead - m2.ahead));
    }
  }
  const mean = (a) => a.reduce((x, y) => x + y, 0) / (a.length || 1);
  return { view: moments[0].ahead, order, pairs, worst, grow: mean(near) / mean(far), maxAhead, topY, speed: mean(speed), nSpeed: speed.length, lastBeat: mean(lastBeat), nLast: lastBeat.length };
}

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
for (const gpu of GPUS)
  for (let d = 0; d < 3; d++) {
    const D = DIFF[d];
    const g = judge(await sample(gpu, D, 'game'), d);
    check(g.pairs >= 20 && g.order === 0, `depth (${gpu}, ${D}): every note wider than the one behind it in its lane (${g.pairs} pairs, ${g.order} out of order${g.worst ? `: ${g.worst}` : ''})`);
    check(g.grow >= 3, `depth (${gpu}, ${D}): a note at the gate is ${g.grow.toFixed(2)} times as wide as one at the far end (at least 3)`);
    check(g.view >= 1.9 * OLD_AHEAD[d] && g.maxAhead >= g.view - 0.6 && g.topY <= 1080 * 0.2, `depth (${gpu}, ${D}): a river shows ${g.view.toFixed(2)} beats (0.31.0: ${OLD_AHEAD[d]}; at least ${(1.9 * OLD_AHEAD[d]).toFixed(1)}), notes drawn up to ${g.maxAhead.toFixed(2)} beats ahead, the farthest at y ${g.topY.toFixed(0)} (the top fifth)`);
    check(g.nLast >= 3 && Math.abs(g.lastBeat / OLD_LAST_BEAT[d] - 1) <= 0.15, `depth (${gpu}, ${D}): a note covers ${g.lastBeat.toFixed(0)} px in its last beat (0.31.0: ${OLD_LAST_BEAT[d]}, within 15%; ${g.nLast} notes); it crosses the gate at ${g.nSpeed ? g.speed.toFixed(0) : '?'} px a beat`);
    for (const scale of ['flat', 'reverse']) {
      const c = judge(await sample(gpu, D, scale), d);
      const bad = c.order > 0 || c.grow < 3;
      check(bad, `depth control (${gpu}, ${D}, notescale ${scale}): ${c.order} of ${c.pairs} pairs out of order, gate/far width ${c.grow.toFixed(2)}: ${bad ? 'fails the size rule, as it must' : 'PASSES the size rule: the gate cannot see the scaling'}`);
    }
  }
console.log(failed ? `beatsync-depth: ${failed} failed` : 'beatsync-depth: ok');
process.exit(failed ? 1 : 0);
