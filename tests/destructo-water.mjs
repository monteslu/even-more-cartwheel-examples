// destructo water gate: the lake behind the Hoover Dam (games/destructo/src/water.c), checked by levelcheck --water in the check cart on romdev.
// A charge goes off at the dam's foot one second in; then 12 s of the lake draining through the breach, then the lake is shut off and the pool left 15 s.
//   starts     the first parcel of water is made within 1 s of the breach
//   flows      the flow was measured (a peak of at least 1 m^3/s, at least 100 parcels made, more than 20 m^3 spilled) and the lake fell
//   downhill   no parcel ever rose above the lake's surface (plus half a metre), and by the end the water lies on the ground (pooled: every parcel retired)
//   loses      the water's kinetic energy at the end is under a tenth of its peak (a fall, a pile, a pool: it does not keep its speed)
//   cheap      the bodies awake in all (the dam's rubble and the water) stay under 2,500; the mean physics step under 8 ms, the worst under 40
//   capped     at most 240 parcels alive at once
// Control, which must fail "starts" and "flows": the same run with the dam left standing makes no water.
// Run: node tests/destructo-water.mjs [--gpu gl|webgpu]
import { runCheck } from './lib/romdev.mjs';
const gi = process.argv.indexOf('--gpu');
const gpu = gi > 0 ? process.argv[gi + 1] : 'gl';
const run = async (args) => {
  const r = await runCheck('levels', ['--water', ...args], { gpu });
  if (r.status !== 0) { console.log(`FAIL check run levels --water ${args.join(' ')} exited ${r.status}\n${r.text.slice(-1500)}`); process.exit(1); }
  return r.rows.find((x) => x.phase === 'water');
};
const checks = (r) => ({
  starts: r.spill_start_s >= 0 && r.spill_start_s <= 1.0,
  flows: r.flow_peak >= 1 && r.spawned >= 100 && r.vol_out > 20 && r.level_end < r.level0,
  downhill: r.max_y_all <= r.level0 + 0.5 && r.alive_end === 0 && r.pooled > 0,
  loses: r.ke_last < 0.1 * r.ke_peak,
  cheap: r.awake_max < 2500 && r.step_mean_ms < 8 && r.step_max_ms < 40,
  capped: r.alive_max <= 240,
});
const describe = (r) => `spill starts ${r.spill_start_s.toFixed(2)} s after the breach, ${r.spawned} parcels, ${r.vol_out.toFixed(0)} m^3, flow peak ${r.flow_peak.toFixed(0)} m^3/s over ${r.open_peak} open bins, lake ${r.level0.toFixed(1)} to ${r.level_end.toFixed(1)} m, highest parcel ${r.max_y_all.toFixed(1)} m, KE ${r.ke_peak.toFixed(0)} J peak to ${r.ke_last.toFixed(0)} at the end, ${r.pooled} pooled, awake at most ${r.awake_max}, step ${r.step_mean_ms.toFixed(1)} ms mean ${r.step_max_ms.toFixed(1)} worst`;
let bad = 0;
const r = await run([]);
const why = Object.entries(checks(r)).filter(([, v]) => !v).map(([k]) => k);
if (why.length) { bad++; console.log(`FAIL ${describe(r)}\n     fails: ${why.join(', ')}`); } else console.log(`ok   ${describe(r)}`);
const c = await run(['--control']);
const cc = checks(c);
for (const k of ['starts', 'flows']) {
  if (!cc[k]) console.log(`ok   control (dam standing) fails its ${k} check as it must: ${c.spawned} parcels`);
  else { bad++; console.log(`CONTROL PASSED (a failure): ${k}`); }
}
process.exit(bad ? 1 : 0);
