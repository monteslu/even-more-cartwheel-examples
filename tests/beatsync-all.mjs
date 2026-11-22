// beatsync's gates for one GPU, several at once (each its own romdev host); the wall-clock sync gate and cart smoke
// last and alone, GL only (as are the recorded-sound gates mix and bass).
//
//   node tests/beatsync-all.mjs gl|webgpu [jobs]   (inside the GPU lock, or on a remote lane: one hold, not one a gate)
import { spawn } from 'node:child_process';
const gpu = process.argv[2], jobs = +(process.argv[3] || 4);
const par = ['duo', 'land', 'streak', 'layout', 'depth', 'wait', 'forgiving', 'newcomer', 'text', 'programs', 'look', 'flash',
  ...(gpu === 'gl' ? ['mix', 'bass'] : [])].map((g) => ['node', [`tests/beatsync-${g}.mjs`, '--gpu', gpu], g]);
par.push(['node', ['tests/gate.mjs', '--gates-file', 'beatsync-gates.json', '--hosts', 'romdev', '--gpu', gpu], 'gates']);
const last = gpu === 'gl' ? [['node', ['tests/beatsync-sync.mjs', '--gpu', 'gl'], 'sync'], ['node', ['tests/cart-smoke.mjs', 'beatsync'], 'smoke']] : [];
const results = [];
const run = ([cmd, args, name]) => new Promise((done) => {
  const t0 = Date.now(), p = spawn(cmd, args, { stdio: ['ignore', 'pipe', 'pipe'] });
  let out = '';
  p.stdout.on('data', (d) => (out += d)); p.stderr.on('data', (d) => (out += d));
  p.on('close', (code) => {
    const fails = out.split('\n').filter((l) => /^FAIL|Error/.test(l)).slice(0, 6);
    results.push({ name, code, s: Math.round((Date.now() - t0) / 1000) });
    console.log(`${code === 0 ? 'ok  ' : 'FAIL'} ${name} (${gpu}, ${Math.round((Date.now() - t0) / 1000)} s)${fails.length ? '\n  ' + fails.join('\n  ') : ''}`);
    done();
  });
});
const q = [...par];
await Promise.all(Array.from({ length: jobs }, async () => { while (q.length) await run(q.shift()); }));
for (const g of last) await run(g);
const bad = results.filter((r) => r.code !== 0);
console.log(bad.length ? `gates (${gpu}): ${bad.length} failed: ${bad.map((b) => b.name).join(' ')}` : `gates (${gpu}): all ok`);
process.exit(bad.length ? 1 : 0);
