// emberthrone moss: the meadow's moss patches (terrain layer 5) from a standing eye at a glancing angle, where a glossy
// layer catches the sky and turns pale, like snow. On romdev (the auto cart's "moss" command gives points deep inside
// moss patches, the headings from which an eye 1.7 m up 12 m back sees each one and the pitch onto it; "groundcam" holds
// the camera): the box on the point in the middle of the picture, its mean colour in linear light, at each clear
// heading, and straight down from 14 m.
// Measure: the box's saturation ((max - min) / max of its mean colour): moss is a deep green straight down (about 0.8);
// a pale sheen washes it toward grey-white.
// Bars (PASS needs all): 8 or more clear views over 3 or more patches, and in every one the moss keeps half its
// straight-down saturation or more.
// CONTROL, which must fail: --control <cart dir>, the build before (the moss's roughness 0.55) with the commands.
//
//   node tests/emberthrone-moss.mjs <cart dir> <out dir> [--control <cart dir>] [--gpu gl|webgpu]
import { mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { execFileSync } from 'node:child_process';

const W = 1920, H = 1080, BOX = { w: 120, h: 16 };
const sat = (png, box) => {
  const crop = `${box.w}x${box.h}+${Math.round(W / 2 - box.w / 2)}+${Math.round(H / 2 - box.h / 2)}`;
  const [r, g, b] = execFileSync('convert', [png, '-crop', crop, '+repage', '-colorspace', 'RGB', '-scale', '1x1!', '-format', '%[fx:r] %[fx:g] %[fx:b]', 'info:']).toString().trim().split(' ').map(Number);
  const mx = Math.max(r, g, b), mn = Math.min(r, g, b);
  return { rgb: [r, g, b].map((x) => +x.toFixed(4)), sat: mx > 0 ? +((mx - mn) / mx).toFixed(3) : 0, luma: +(0.2126 * r + 0.7152 * g + 0.0722 * b).toFixed(4) };
};

async function run(cartDir, out, gpu) {
  process.env.DESTRUCTO_CART_DIR = resolve(cartDir);
  const { openCart } = await import('./lib/romdev.mjs');
  mkdirSync(out, { recursive: true });
  const h = await openCart('emberthrone_auto', { gpu, tag: 'moss' });
  const rows = [];
  try {
    await h.step(60);
    const { points } = await h.command('moss');
    for (const [pi, p] of points.slice(0, 6).entries()) {
      const [x, z] = p.moss;
      await h.command(`groundcam ${x} ${z} 14`);
      await h.step(40);
      await h.shot(join(out, `p${pi}-down.png`));
      const row = { at: p.moss, down: sat(join(out, `p${pi}-down.png`), { w: 60, h: 60 }), glance: [] };
      for (let k = 0; k < 4; k++) {
        if (!(p.clear & (1 << k))) continue;
        const yaw = k * 90, r = (yaw * Math.PI) / 180;
        await h.command(`groundcam ${(x + Math.sin(r) * 12).toFixed(2)} ${(z + Math.cos(r) * 12).toFixed(2)} 1.7 ${p.pitch[k]} ${yaw}`);
        await h.step(40);
        const f = join(out, `p${pi}-y${yaw}.png`);
        await h.shot(f);
        row.glance.push({ yaw, ...sat(f, BOX) });
      }
      rows.push(row);
    }
  } finally { await h.close(); }
  return rows;
}

const judge = (rows) => {
  const views = rows.flatMap((r) => r.glance.map((g) => ({ at: r.at, yaw: g.yaw, keep: +(g.sat / r.down.sat).toFixed(3), sat: g.sat, luma: g.luma })));
  const worst = views.reduce((w, v) => (!w || v.keep < w.keep ? v : w), null);
  const checks = { views: views.length >= 8 && rows.filter((r) => r.glance.length).length >= 3, saturation: views.every((v) => v.keep >= 0.5) };
  return { rows, views, worst, checks, pass: Object.values(checks).every(Boolean) };
};

const [, , cart, out, ...rest] = process.argv;
const opt = (k) => { const i = rest.indexOf(k); return i >= 0 ? rest[i + 1] : null; };
const gpu = opt('--gpu') || 'gl';
const res = { run: judge(await run(cart, join(out, 'run'), gpu)) };
if (opt('--control')) res.control = judge(await run(opt('--control'), join(out, 'control'), gpu));
const ok = res.run.pass && (!res.control || !res.control.pass);
const brief = (x) => x && { worst: x.worst, views: x.views.length, checks: x.checks, pass: x.pass };
console.log(JSON.stringify({ run: brief(res.run), control: brief(res.control), verdict: ok ? 'PASS' : 'FAIL' }, null, 1));
writeFileSync(join(out, 'moss.json'), JSON.stringify(res, null, 1));
process.exit(ok ? 0 : 1);
