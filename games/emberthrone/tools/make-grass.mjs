// The meadow's grass (assets/models/meadow_blades.glb: curved tufts with a far level of their own, engineTuft and
// writeLods below) and its short turf (assets/models/meadow_turf.glb), drawn by the Grass and Turf scatter layers
// (assets/scenes/main.cwscene): the turf thirty-six fine blades a hand high, curled over, spread over a disc of 20 cm,
// filling the ground between the tufts near the eye.
//
// makeBlades: a clump of curved, tapered blades. Each blade is a strip of `seg` segments to a point: it leaves the ground
// leaning a little out of the clump and arches over along its length (its angle from upright grows with the length to a
// power, so the bend is in the upper half, as a real blade's is), a few drooping past level; it twists a little about its
// spine; its width swells just above the root and tapers to the tip. Heights run from short to tall with most in the
// middle; widths, leans, bends and shades all vary blade by blade, a few blades drier at the tip. Normals lean up and,
// across the blade, toward each edge (`edge`: a rounded blade, not a flat card, lit softly from either side). Every
// triangle is in the index both ways round on the same vertices, with the same normals (a front-side material: no
// flipped dark back face, the program state the engine's GRASS draws with). Colours are linear vertex colours over a
// white material: a dark root (the scatter's root_from_terrain takes the lower part to the ground's colour anyway) to a
// lighter, slightly yellower tip.
//
// (Tufts of twenty makeBlades blades, 1 to 2 cm wide and 16 to 60 cm tall, replaced the engine's GRASS tufts for a while:
// on tests/emberthrone-grass-walk.mjs they shimmered twice as much in motion; the meadow's curved tufts start from the
// engine tuft's proportions instead.)
//
//   node games/emberthrone/tools/make-grass.mjs
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Glb } from './chars/gltf-writer.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const MODELS = join(HERE, '..', 'assets', 'models');
const s2l = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
const lin = (c) => c.map((v) => s2l(v / 255));
const norm = (v) => { const l = Math.hypot(...v) || 1; return v.map((x) => x / l); };
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];

/**
 * @param {object} o
 * @param {number} o.blades @param {number} o.seg segments a blade @param {number} o.seed
 * @param {[number, number]} o.height metres, shortest and tallest @param {[number, number]} o.width metres at the widest
 * @param {number} o.clump radius of the clump's roots @param {[number, number]} o.tilt start angle from upright, radians
 * @param {[number, number]} o.arch the extra angle the blade turns over by its tip, radians
 * @param {number} o.droop share of blades that arch past level @param {number} [o.edge] how far the normals lean to the edges: 0.25
 * @param {number[]} o.root @param {number[]} o.tip @param {number[]} o.dry sRGB 0..255
 */
export function makeBlades(o) {
  let s = o.seed;
  const rnd = () => ((s = (Math.imul(s, 1103515245) + 12345) >>> 0) / 4294967296);
  const root = lin(o.root), tipA = lin(o.tip), dry = lin(o.dry);
  const g = { pos: [], nrm: [], col: [], idx: [] };
  const vert = (p, n, c) => { g.pos.push(...p); g.nrm.push(...n); g.col.push(c[0], c[1], c[2], 1); return g.pos.length / 3 - 1; };
  for (let b = 0; b < o.blades; b++) {
    // where it grows and which way it leans: mostly out of the clump, never all the same way
    const a0 = ((b + rnd() * 0.8) / o.blades) * Math.PI * 2, r0 = o.clump * Math.sqrt(rnd());
    const ox = r0 * Math.cos(a0), oz = r0 * Math.sin(a0);
    const az = a0 + (rnd() - 0.5) * 1.4, out = [Math.cos(az), 0, Math.sin(az)];
    // height: most in the middle, a few short and a few tall (the mean of two draws)
    const h = o.height[0] + (o.height[1] - o.height[0]) * (0.5 * (rnd() + rnd()));
    const w = o.width[0] + (o.width[1] - o.width[0]) * rnd();
    const t0 = o.tilt[0] + (o.tilt[1] - o.tilt[0]) * rnd();
    const droop = rnd() < o.droop;
    const t1 = t0 + (droop ? o.arch[1] + 0.35 * rnd() : o.arch[0] + (o.arch[1] - o.arch[0]) * rnd());
    const pw = (o.pw ?? 1.6) + 0.8 * rnd(); // where the bend sits: a higher power keeps the lower blade straighter
    const twist = (rnd() - 0.5) * (o.twist ?? 1.2); // radians about the spine over the length
    const isDry = rnd() < 0.15, shade = 0.82 + 0.3 * rnd(), hue = rnd();
    const tip = tipA.map((v, k) => (isDry ? v + (dry[k] - v) * 0.8 : v * (0.92 + 0.16 * hue) + (k === 2 ? 0.004 * (1 - hue) : 0)));
    // the spine by arc length: N steps of h / N, the angle from upright th(t) = t0 + (t1 - t0) t^pw
    const N = o.seg, step = h / N, pts = [[ox, 0, oz]], tang = [];
    for (let i = 0; i <= N; i++) {
      const t = i / N, th = t0 + (t1 - t0) * Math.pow(t, pw);
      tang.push([out[0] * Math.sin(th), Math.cos(th), out[2] * Math.sin(th)]);
      if (i < N) {
        const tm = (i + 0.5) / N, thm = t0 + (t1 - t0) * Math.pow(tm, pw), p = pts[i];
        pts.push([p[0] + out[0] * Math.sin(thm) * step, p[1] + Math.cos(thm) * step, p[2] + out[2] * Math.sin(thm) * step]);
      }
    }
    const base = [];
    for (let i = 0; i <= N; i++) {
      const t = i / N;
      // across the blade: the horizontal side, turned about the spine by the twist so far
      const side0 = [-out[2], 0, out[0]], fn0 = norm(cross(tang[i], side0)), a = twist * t;
      const side = side0.map((v, k) => v * Math.cos(a) + fn0[k] * Math.sin(a));
      let fn = norm(cross(tang[i], side)); if (fn[1] < 0) fn = fn.map((v) => -v);
      // width: swells to its widest a fifth of the way up, then tapers to the point
      const hw = i === N ? 0 : 0.5 * w * (t < 0.2 ? 0.7 + 1.5 * t : Math.pow((1 - t) / 0.8, 0.85));
      const c = root.map((v, k) => (v + (tip[k] - v) * Math.pow(t, 0.75)) * shade);
      const ring = [];
      for (const sg of i === N ? [0] : [-1, 1]) {
        // the normal: the face's, leant toward its edge (a rounded blade), then bent most of the way up
        const e = fn.map((v, k) => v + side[k] * sg * (o.edge ?? 0.25)), up = norm([e[0] * 0.45, e[1] * 0.45 + 0.8, e[2] * 0.45]);
        ring.push(vert(pts[i].map((v, k) => v + side[k] * hw * sg), up, c));
      }
      base.push(ring);
    }
    const tri = (a, bb, c) => g.idx.push(a, bb, c, a, c, bb); // both ways round
    for (let i = 0; i < N; i++) {
      const [l0, r0i] = base[i], nx = base[i + 1];
      if (nx.length === 2) { tri(l0, r0i, nx[1]); tri(l0, nx[1], nx[0]); } else tri(l0, r0i, nx[0]);
    }
  }
  return g;
}

// a mesh with an authored far level of detail: the near blades and the far ones in one vertex buffer, the near ones'
// triangles the primitive's index, the far ones' its extras.cw_lods (the engine's glTF LOD chain, cartwheel-engine
// src/resource/gltf.c; its .cwimport keeps cwpack from dropping the far vertices or simplifying the near ones)
function writeLods(name, near, far, rough) {
  const nv = near.pos.length / 3, g = { pos: [...near.pos, ...far.pos], nrm: [...near.nrm, ...far.nrm], col: [...near.col, ...far.col], idx: near.idx };
  const glb = new Glb();
  const mat = glb.material(name, rough, 0), mi = glb.mesh(name, g, mat);
  const farIdx = glb.acc(new Uint16Array(far.idx.map((i) => i + nv)), 'SCALAR', 5123, 34963);
  glb.meshes[mi].primitives[0].extras = { cw_lods: [{ indices: farIdx, error: 0.05 }] };
  glb.sceneNodes.push(glb.node({ name, mesh: mi }));
  const file = join(MODELS, `${name}.glb`);
  glb.write(file);
  console.log(`ok   ${name}  near ${nv} vertices, ${near.idx.length / 6} triangles a side; far ${far.pos.length / 3} vertices, ${far.idx.length / 6} triangles a side: ${file}`);
}

function write(name, g, rough) {
  const glb = new Glb();
  const mat = glb.material(name, rough, 0);
  glb.sceneNodes.push(glb.node({ name, mesh: glb.mesh(name, g, mat) }));
  const file = join(MODELS, `${name}.glb`);
  glb.write(file);
  console.log(`ok   ${name}  ${g.pos.length / 3} vertices, ${g.idx.length / 6} triangles a side: ${file}`);
}

// colours: the engine's GRASS tuft times the meadow's tint (0.42, 0.52, 0.26), in sRGB, a touch brighter at the tip
const ROOT = [34, 58, 12], TIP = [74, 122, 30], DRY = [128, 128, 52];
write('meadow_turf', makeBlades({ blades: 36, seg: 2, seed: 19, height: [0.08, 0.2], width: [0.012, 0.022], clump: 0.2,
  tilt: [0.2, 0.6], arch: [0.4, 1.2], droop: 0.3, root: ROOT, tip: [66, 112, 28], dry: DRY }), 1);

// the engine's GRASS tuft (cartwheel-engine src/terrain/terrain_node.c tuft()), its colours times the meadow's tint, so a
// development set can start from exactly what the engine draws: `blades` blades of `seg` segments, `width` at the root,
// leaning out by a share of the height that grows with its square; `curl` (0: the engine's) bends the upper blade over
// further, its angle from upright growing with the height to the power `pw`
const TINT = [0.42, 0.52, 0.26];
function tuftU(b, k) {
  let h = (Math.imul(b, 0x9e3779b1) ^ Math.imul(k, 0x85ebca77)) >>> 0;
  h ^= h >>> 15; h = Math.imul(h, 0x2c1b3c6d) >>> 0; h ^= h >>> 12; h = Math.imul(h, 0x297a2d39) >>> 0; h ^= h >>> 15;
  return (h >>> 8) / 16777216;
}
export function engineTuft({ blades, seg, width, curl = 0, pw = 2, colour = 1, overMul = 1, dryShare = 0.18, vary = 0.3 }) {
  const g = { pos: [], nrm: [], col: [], idx: [] };
  for (let b = 0; b < blades; b++) {
    const r0 = 0.09 * Math.sqrt(tuftU(b, 1)), a0 = (2 * Math.PI * (b + tuftU(b, 2) * 0.6)) / blades;
    const ox = r0 * Math.cos(a0), oz = r0 * Math.sin(a0), la = a0 + (tuftU(b, 3) - 0.5) * 1.6, ldx = Math.cos(la), ldz = Math.sin(la), sx = -ldz, sz = ldx;
    const hgt = 0.3 + 0.3 * tuftU(b, 4), over = overMul * hgt * (0.18 + 0.42 * tuftU(b, 5)), w = width * (0.75 + 0.5 * tuftU(b, 6));
    const dry = tuftU(b, 7) < dryShare, root = [0.04, 0.075, 0.018], tip = [dry ? 0.28 : 0.14, 0.27, dry ? 0.08 : 0.05].map((v) => v * colour), v = 1 - vary / 2 + vary * tuftU(b, 8);
    const base = g.pos.length / 3;
    // the spine: the engine's (curl 0), or arched over in its upper part by an extra angle growing as t^pw
    let P = [ox, 0, oz];
    const pts = [], tans = [];
    for (let i = 0; i <= seg; i++) {
      const t = i / seg;
      let px = ox + ldx * over * t * t, pz = oz + ldz * over * t * t, py = hgt * (t - 0.22 * (over / hgt) ** 2 * t * t);
      let tx = ldx * over * 2 * t, tz = ldz * over * 2 * t, ty = hgt * (1 - 0.44 * (over / hgt) ** 2 * t);
      if (curl > 0) { // integrate a spine whose angle from upright is the engine's plus curl * t^pw
        if (i > 0) {
          const tm = (i - 0.5) / seg, th = Math.atan2(2 * over * tm, hgt * (1 - 0.44 * (over / hgt) ** 2 * tm)) + curl * tm ** pw, step = hgt / seg;
          P = [P[0] + ldx * Math.sin(th) * step, P[1] + Math.cos(th) * step, P[2] + ldz * Math.sin(th) * step];
        }
        [px, py, pz] = P;
        const th = Math.atan2(2 * over * t, hgt * (1 - 0.44 * (over / hgt) ** 2 * t)) + curl * t ** pw;
        tx = ldx * Math.sin(th), ty = Math.cos(th), tz = ldz * Math.sin(th);
      }
      pts.push([px, py, pz]); tans.push([tx, ty, tz]);
    }
    for (let i = 0; i <= seg; i++) {
      const t = i / seg, [px, py, pz] = pts[i], [tx, ty, tz] = tans[i];
      let fn = [ty * sz, tz * sx - tx * sz, -ty * sx];
      if (fn[1] < 0) fn = fn.map((q) => -q);
      fn = norm(fn);
      const n = norm([fn[0] * 0.45, fn[1] * 0.45 + 0.8, fn[2] * 0.45]);
      const c = root.map((r, q) => (r + (tip[q] - r) * t ** 0.8) * v * TINT[q]);
      const hw = i === seg ? 0 : w * 0.5 * (1 - t) ** 0.7;
      for (const sg of i === seg ? [0] : [-1, 1]) { g.pos.push(px + sx * hw * sg, py, pz + sz * hw * sg); g.nrm.push(...n); g.col.push(...c, 1); }
    }
    for (let i = 0; i < seg; i++) {
      const l0 = base + 2 * i, r0i = l0 + 1;
      const q = i + 1 < seg ? [l0, r0i, l0 + 3, l0, l0 + 3, l0 + 2] : [l0, r0i, base + 2 * seg];
      for (let e = 0; e < q.length; e += 3) g.idx.push(q[e], q[e + 1], q[e + 2], q[e], q[e + 2], q[e + 1]);
    }
  }
  return g;
}

// the meadow's grass (assets/models/meadow_blades.glb, the Grass scatter layer's mesh, set in assets/scenes/main.cwscene
// so it loads with the scene): the engine's tuft curved over. Twelve blades of four segments, 2.6 to 4.3 cm wide, each
// turning over through its upper half (1.4 radians more than the engine's by its tip, growing as the height share to
// the 2.4), few drier blades (8%) and each blade's shade varied by 16% (the scatter's tint_jitter varies each tuft).
// Its far level (from the layer's lod_distance) is six wide blades of two segments curved over the same way, drawn
// from the same vertex buffer (writeLods). On tests/emberthrone-grass-walk.mjs this tuft shimmers less in motion than
// the engine's (flicker excess 3.89 against 4.5 on GL and WebGPU); the straight engine tuft drawn from a glTF the same
// way does worse (6.6), and the more each blade curves over, the calmer the field (no curl 6.6, 0.5 rad 5.6, 0.8 4.97,
// 1.1 4.71, 1.4 with the quieter shades and the curved far level 3.89).
const NEAR = { blades: 12, seg: 4, width: 0.034, curl: 1.4, pw: 2.4, dryShare: 0.08, vary: 0.16 };
const FAR = { blades: 6, seg: 2, width: 0.05, curl: 0.6, pw: 2, overMul: 1.6, dryShare: 0.08, vary: 0.16 };
writeLods('meadow_blades', engineTuft(NEAR), engineTuft(FAR), 1);
