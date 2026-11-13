// dev: the program states every setting asks for that the table lacks (debug field cw.three.missing_states), one landmark per
// setting, every graphics preset, the aim view and the collapse, on GL and on WebGPU (the dual cart through romdev). Prints the
// union, one state a line: append it to games/destructo/programs/destructo.txt, regenerate (tools/programs.mjs), rebuild, run again.
//   node tests/destructo-states.mjs [--gpu gl|webgpu|both] [--quality 0,1,2,3,4] [--only desert,city] [--app destructo|destructo_auto]
import { openCart } from './lib/romdev.mjs';
const a = process.argv.slice(2);
const take = (f, d) => { const i = a.indexOf(f); if (i < 0) return d; const v = a[i + 1]; a.splice(i, 2); return v; };
const gpus = take('--gpu', 'both') === 'both' ? ['gl', 'webgpu'] : [a.includes('webgpu') ? 'webgpu' : 'gl'];
const qs = take('--quality', '0,1,2,3,4').split(',').map(Number), only = take('--only', ''), app = take('--app', 'destructo_auto');
const LEVELS = { desert: 'Great Sphinx', meadow: 'Taj Mahal', city: 'Iron Lady', harbour: 'Statue of Liberty', highland: 'Mount Rushmore', headland: 'Parthenon', snow: 'The Kremlin' };
const all = new Map();
// the field is a byte string of newline separated states (16 KiB at most); read whole with wasm({op:"read"})
const missing = async (h) => { const r = await h.read("cw.three.missing_states").catch((e) => { if (/not found/.test(e.message)) return ""; throw e; }); /* (the field exists only once a state is missing) */ const t = typeof r === "string" ? r : (r && (r.text ?? r.decoded ?? JSON.stringify(r))) || ""; return t.split("\n").map((x) => x.trim()).filter(Boolean); };
for (const gpu of gpus) for (const [name, level] of Object.entries(LEVELS)) {
  if (only && !only.split(',').includes(name)) continue;
  for (const q of qs) {
    const h = await openCart(app, { gpu, tag: 'states', seed: null });
    try {
      for (const [k, v] of Object.entries({ LEVEL_NAME: level, NO_HUD: 1, QUALITY: q })) await h.command(`dm set ${k} "${v}"`);
      await h.command('dm restart');
      for (const n of [100, 120, 200]) {
        await h.step(n);
        for (const st of await missing(h)) if (st && !all.has(st)) all.set(st, `${gpu} ${name} q${q} f${h.frame}`);
      }
      // a mid-run preset switch, High to each other and back
      if (q === 3) for (const to of [0, 2, 4]) { await h.command(`quality set ${['verylow', 'low', 'medium', 'high', 'ultra'][to]}`).catch(() => {}); await h.step(30); for (const st of await missing(h)) if (st && !all.has(st)) all.set(st, `${gpu} ${name} switch->${to}`); }
    } finally { await h.close(); }
    console.error(`${gpu} ${name} q${q}: ${all.size} states so far`);
  }
}
for (const [st, where] of all) console.log(`${st}\t# ${where}`);
