// The robe against the legs as they are drawn: the figure's glb skinned on the CPU from the skeleton the cart drew
// ("hair rec <ticks> <actor> <every>" then "hair bones" and "hair pose <row>", games/emberthrone/view.c). Every corner of
// the legs (the outfit's and the skin's, weighted at least half to the hips, the legs and the feet, below the hips'
// joint) is placed against the cloth's outer surface on a level line from the body's upright axis (through the hips'
// joint) out through it: inside the outermost crossing, or past it by some distance. The leg comes THROUGH the cloth
// where one of its edges runs from a corner inside to one outside, both under the cloth away from its free edges (the
// hem, the top, an opening's edges, rimBand as built) and above the hem band (the hem rides on the feet). What shows is
// every corner past the cloth, under it away from its edges, joined that way to such a place; its farthest is the
// measure. A leg that steps out through the robe's opening or below its hem crosses no cloth and is not counted.
//
//   import { robeLegs } from './lib/robe-legs.mjs';
//   robeLegs(glbPath, { bones, poses }) -> { max_mm, at, p99_mm, ticks, verts, worst: [...], perTick }
import { readFileSync } from 'node:fs';

const CS = { 5126: 4, 5123: 2, 5121: 1, 5125: 4 }, NC = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };
export function readGlb(path) {
  const b = readFileSync(path), n = b.readUInt32LE(12), j = JSON.parse(b.subarray(20, 20 + n).toString('utf8'));
  const bin = b.subarray(20 + n + 8);
  const acc = (i) => {
    const a = j.accessors[i], v = j.bufferViews[a.bufferView], c = NC[a.type], s = CS[a.componentType];
    const st = v.byteStride || c * s, o = (v.byteOffset || 0) + (a.byteOffset || 0), out = new Float64Array(a.count * c);
    const norm = a.normalized ? (s === 1 ? 255 : 65535) : 1;
    for (let k = 0; k < a.count; k++)
      for (let q = 0; q < c; q++) {
        const off = o + k * st + q * s;
        out[k * c + q] = (a.componentType === 5126 ? bin.readFloatLE(off) : a.componentType === 5125 ? bin.readUInt32LE(off) : s === 2 ? bin.readUInt16LE(off) : bin.readUInt8(off)) / (a.componentType === 5126 ? 1 : norm);
      }
    return out;
  };
  return { j, acc };
}

const LEG = new Set(['Hips', 'UpperLegL', 'UpperLegR', 'LowerLegL', 'LowerLegR', 'FootL', 'FootR', 'ToesL', 'ToesR']);

/** the body mesh's primitives by material name: positions, joints, weights (and indices) */
export function prims(g) {
  const { j, acc } = g, body = j.nodes.find((x) => x.mesh !== undefined && x.skin !== undefined && x.name === 'Body') || j.nodes.find((x) => x.mesh !== undefined);
  if (body.translation || body.rotation || body.scale || body.matrix) throw new Error('robe-legs: the body mesh node has a transform of its own (not handled)');
  return j.meshes[body.mesh].primitives.map((p) => ({ mat: j.materials[p.material].name, P: acc(p.attributes.POSITION), J: acc(p.attributes.JOINTS_0), W: acc(p.attributes.WEIGHTS_0), N: p.attributes.NORMAL, I: p.indices !== undefined ? acc(p.indices) : null }));
}

/** the skinning matrices (3x4, rows) of one recorded pose: the bone's model matrix times the joint's inverse bind */
export function skinMats(g, names, row) {
  const { j, acc } = g, skin = j.skins[0], ibm = acc(skin.inverseBindMatrices);
  const idx = skin.joints.map((k) => names.indexOf(j.nodes[k].name));
  if (idx.some((x) => x < 0)) throw new Error(`robe-legs: the pose lacks joints ${skin.joints.filter((k, i) => idx[i] < 0).map((k) => j.nodes[k].name).join(' ')}`);
  return skin.joints.map((_, ji) => {
    const M = row.subarray ? row.subarray(1 + idx[ji] * 12, 13 + idx[ji] * 12) : row.slice(1 + idx[ji] * 12, 13 + idx[ji] * 12);
    const B = ibm.subarray(ji * 16, ji * 16 + 16); // column-major
    const out = new Float64Array(12);
    for (let r = 0; r < 3; r++)
      for (let c = 0; c < 4; c++) out[r * 4 + c] = M[r * 4] * B[c * 4] + M[r * 4 + 1] * B[c * 4 + 1] + M[r * 4 + 2] * B[c * 4 + 2] + M[r * 4 + 3] * B[c * 4 + 3];
    return out;
  });
}

export function skinVerts(p, mats, sel) {
  const out = new Float64Array(sel.length * 3);
  sel.forEach((v, k) => {
    let x = 0, y = 0, z = 0;
    const px = p.P[v * 3], py = p.P[v * 3 + 1], pz = p.P[v * 3 + 2];
    for (let q = 0; q < 4; q++) {
      const w = p.W[v * 4 + q];
      if (!w) continue;
      const m = mats[p.J[v * 4 + q]];
      x += w * (m[0] * px + m[1] * py + m[2] * pz + m[3]);
      y += w * (m[4] * px + m[5] * py + m[6] * pz + m[7]);
      z += w * (m[8] * px + m[9] * py + m[10] * pz + m[11]);
    }
    out[k * 3] = x, out[k * 3 + 1] = y, out[k * 3 + 2] = z;
  });
  return out;
}

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const crs = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
function bary(p, a, b, c) {
  const v0 = sub(b, a), v1 = sub(c, a), v2 = sub(p, a), d00 = dot(v0, v0), d01 = dot(v0, v1), d11 = dot(v1, v1), d20 = dot(v2, v0), d21 = dot(v2, v1);
  const den = d00 * d11 - d01 * d01 || 1e-12, v = (d11 * d20 - d01 * d21) / den, w = (d00 * d21 - d01 * d20) / den;
  return [1 - v - w, v, w];
}

/** a ray (origin o, horizontal direction d) against a triangle: the distance along it, or -1 (Moller-Trumbore) */
function rayTri(o, d, a, b, c) {
  const e1 = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], e2 = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
  const p = [d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]];
  const det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
  if (Math.abs(det) < 1e-12) return -1;
  const inv = 1 / det, t0 = [o[0] - a[0], o[1] - a[1], o[2] - a[2]];
  const u = (t0[0] * p[0] + t0[1] * p[1] + t0[2] * p[2]) * inv;
  if (u < 0 || u > 1) return -1;
  const q = [t0[1] * e1[2] - t0[2] * e1[1], t0[2] * e1[0] - t0[0] * e1[2], t0[0] * e1[1] - t0[1] * e1[0]];
  const v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
  if (v < 0 || u + v > 1) return -1;
  const t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
  return t > 0 ? t : -1;
}

export function robeLegs(glbPath, dump, { robeMat = /robe/, bodyMats = /outfit|skin_skin/, every = 1, hemBand = 0.1, rimBand = 0.03 } = {}) {
  const g = readGlb(glbPath), P = prims(g), names = dump.bones, jn = g.j.skins[0].joints.map((k) => g.j.nodes[k].name);
  const robes = P.filter((p) => robeMat.test(p.mat));
  if (!robes.length) throw new Error(`robe-legs: no robe primitive in ${glbPath}`);
  const hipsJ = jn.indexOf('Hips');
  // the hips' joint at bind: its inverse bind matrix's inverse translation (a rigid joint: -R^T t)
  const ibm = g.acc(g.j.skins[0].inverseBindMatrices).subarray(hipsJ * 16, hipsJ * 16 + 16);
  const s2 = ibm[0] ** 2 + ibm[1] ** 2 + ibm[2] ** 2;
  const hipsY = -(ibm[1] * ibm[12] + ibm[5] * ibm[13] + ibm[9] * ibm[14]) / s2;
  const legs = P.filter((p) => bodyMats.test(p.mat) && !robeMat.test(p.mat)).map((p) => {
    const sel = [];
    for (let v = 0; v < p.P.length / 3; v++) {
      let w = 0;
      for (let q = 0; q < 4; q++) if (LEG.has(jn[p.J[v * 4 + q]])) w += p.W[v * 4 + q];
      if (w >= 0.5 && p.P[v * 3 + 1] < hipsY) sel.push(v);
    }
    return { p, sel };
  });
  const nverts = legs.reduce((s, l) => s + l.sel.length, 0);
  // the cloth's outer surface: the triangles whose corners' normals face away from the body's upright axis (the
  // cloth has a thickness; its inner face looks in), wound to face out; and its edges that only one of them has (the
  // hem, the top, an opening's edges), welded by place (a seam in the texture splits a corner, not the cloth)
  const outer = [];
  for (const rp of robes) {
    const N = g.acc(rp.N), I = rp.I || Array.from({ length: rp.P.length / 3 }, (_, k) => k);
    const key = (v) => [0, 1, 2].map((k) => Math.round(rp.P[v * 3 + k] * 2000)).join(',');
    const tris = [], count = new Map();
    for (let t = 0; t < I.length; t += 3) {
      const c = [I[t], I[t + 1], I[t + 2]];
      const out = c.every((v) => N[v * 3] * rp.P[v * 3] + N[v * 3 + 2] * rp.P[v * 3 + 2] > 0);
      if (!out) continue;
      const P3 = c.map((v) => [rp.P[v * 3], rp.P[v * 3 + 1], rp.P[v * 3 + 2]]);
      const e1 = sub(P3[1], P3[0]), e2 = sub(P3[2], P3[0]), fn = crs(e1, e2), vn = c.reduce((m, v) => [m[0] + N[v * 3], m[1] + N[v * 3 + 1], m[2] + N[v * 3 + 2]], [0, 0, 0]);
      if (dot(fn, vn) < 0) c.reverse();
      tris.push(c);
      for (let k = 0; k < 3; k++) { const e = [key(c[k]), key(c[(k + 1) % 3])].sort().join('|'); count.set(e, (count.get(e) || 0) + 1); }
    }
    const hem = Math.min(...Array.from({ length: rp.P.length / 3 }, (_, k) => rp.P[k * 3 + 1]));
    // the cloth's free edges (the hem, the top, an opening's edges): each triangle's distance from them as built
    const rim = [];
    for (const c of tris) for (let k = 0; k < 3; k++) if (count.get([key(c[k]), key(c[(k + 1) % 3])].sort().join('|')) === 1) rim.push(c[k], c[(k + 1) % 3]);
    const rimP = [...new Set(rim)].map((v) => [rp.P[v * 3], rp.P[v * 3 + 1], rp.P[v * 3 + 2]]);
    const fromRim = (v) => Math.sqrt(Math.min(...rimP.map((q) => (q[0] - rp.P[v * 3]) ** 2 + (q[1] - rp.P[v * 3 + 1]) ** 2 + (q[2] - rp.P[v * 3 + 2]) ** 2)));
    for (const c of tris) outer.push({ rp, c, up: c.map((v) => rp.P[v * 3 + 1] - hem), rim: Math.min(...c.map(fromRim)) });
  }
  // the legs' own edges (between selected corners), for where the leg's surface crosses the cloth
  for (const l of legs) {
    const at = new Map(l.sel.map((v, k) => [v, k])), E = new Set();
    const I = l.p.I || Array.from({ length: l.p.P.length / 3 }, (_, k) => k);
    for (let t = 0; t < I.length; t += 3)
      for (let k = 0; k < 3; k++) {
        const x = at.get(I[t + k]), y = at.get(I[t + (k + 1) % 3]);
        if (x !== undefined && y !== undefined && x !== y) E.add(x < y ? x * 65536 + y : y * 65536 + x);
      }
    l.edges = [...E].map((e) => [Math.floor(e / 65536), e % 65536]);
  }
  const hb = names.indexOf('Hips');
  let max = 0, at = null;
  const perTick = [], all = [];
  for (let r = 0; r < dump.poses.length; r += every) {
    const row = dump.poses[r], mats = skinMats(g, names, row);
    const SV = new Map(robes.map((rp) => [rp, skinVerts(rp, mats, Array.from({ length: rp.P.length / 3 }, (_, k) => k))]));
    const rows = new Map();
    for (const t of outer) {
      const V = SV.get(t.rp), p = t.c.map((v) => [V[v * 3], V[v * 3 + 1], V[v * 3 + 2]]);
      const tt = { p, up: t.up, rim: t.rim, lo: Math.min(p[0][1], p[1][1], p[2][1]), hi: Math.max(p[0][1], p[1][1], p[2][1]) };
      for (let k = Math.floor(tt.lo / 0.02); k <= Math.floor(tt.hi / 0.02); k++) (rows.get(k) || rows.set(k, []).get(k)).push(tt);
    }
    let tmax = 0, n3 = 0;
    const hm = row.subarray ? row.subarray(1 + hb * 12, 13 + hb * 12) : row.slice(1 + hb * 12, 13 + hb * 12);
    const hip = [hm[3], hm[7], hm[11]], scale = Math.hypot(hm[0], hm[4], hm[8]);
    for (const l of legs) {
      const { p, sel } = l, V = skinVerts(p, mats, sel), n = sel.length;
      // each corner of the legs on a level line from the body's upright axis (the hips' joint) out through it: where the
      // line crosses the cloth's outer surface, the corner lies inside the outermost crossing (pen < 0) or past it
      const pen = new Float64Array(n).fill(NaN), interior = new Uint8Array(n);
      for (let k = 0; k < n; k++) {
        const v = [V[k * 3], V[k * 3 + 1], V[k * 3 + 2]];
        const dx = v[0] - hip[0], dz = v[2] - hip[2], L = Math.hypot(dx, dz);
        if (L < 1e-6) continue;
        const o = [hip[0], v[1], hip[2]], d = [dx / L, 0, dz / L];
        let far = -1, ft = null;
        for (const t of rows.get(Math.floor(v[1] / 0.02)) || []) {
          if (v[1] < t.lo || v[1] > t.hi) continue;
          const h = rayTri(o, d, t.p[0], t.p[1], t.p[2]);
          if (h > far) far = h, ft = t;
        }
        if (far < 0) continue; // no cloth on that line: below the hem, above the top, or in an opening
        const bc = bary([o[0] + d[0] * far, o[1], o[2] + d[2] * far], ft.p[0], ft.p[1], ft.p[2]);
        if (bc[0] * ft.up[0] + bc[1] * ft.up[1] + bc[2] * ft.up[2] < hemBand) continue; // the hem rides on the feet
        pen[k] = (L - far) / scale;
        interior[k] = ft.rim >= rimBand ? 1 : 0;
      }
      // the leg comes through the cloth where one of its edges runs from a corner inside the cloth to one outside it,
      // both under the cloth's surface away from its free edges (a leg that steps out through the robe's opening, or
      // under its hem, crosses no cloth). What shows: every corner outside the cloth's surface away from its edges joined,
      // so, to such a place
      const out = (k) => pen[k] > 0, adj = Array.from({ length: n }, () => []), seed = [];
      for (const [x, y] of l.edges) {
        if (out(x) && out(y) && interior[x] && interior[y]) adj[x].push(y), adj[y].push(x);
        else if (interior[x] && interior[y] && ((out(x) && pen[y] <= 0) || (out(y) && pen[x] <= 0))) seed.push(out(x) ? x : y);
      }
      const seen = new Uint8Array(n), stack = [...seed];
      for (const k of seed) seen[k] = 1;
      while (stack.length) {
        const k = stack.pop();
        if (pen[k] > 0.003) n3++;
        if (pen[k] > tmax) tmax = pen[k];
        if (pen[k] > max) max = pen[k], at = { tick: row[0], mat: p.mat, bone: jn[p.J[sel[k] * 4]], x: +V[k * 3].toFixed(3), y: +V[k * 3 + 1].toFixed(3), z: +V[k * 3 + 2].toFixed(3) };
        for (const q of adj[k]) if (!seen[q]) seen[q] = 1, stack.push(q);
      }
    }
    perTick.push([row[0], +(tmax * 1000).toFixed(1), n3]);
    all.push(tmax);
  }
  all.sort((a, b) => a - b);
  return { max_mm: +(max * 1000).toFixed(1), at, p99_mm: +(all[Math.floor(all.length * 0.99)] * 1000 || 0).toFixed(1), ticks: perTick.length, verts: nverts, worst: perTick.slice().sort((a, b) => b[1] - a[1]).slice(0, 8), perTick };
}

// The robe's hem as drawn, every recorded pose: how high it rises (its highest corner, from where it hangs at the
// floor toward the hips' joint, as a share of the robe's length: 0 hanging, 1 level with the hips) and how far any of
// the robe reaches out from the hips' upright axis (the figure's own metres). A panel carried rigidly by a raised thigh
// stands out level with the hips (a share near 1, well over a metre out); cloth that swings out on a turn lifts its hem
// part of the way.
//   robeSail(glbPath, { bones, poses }) -> { max_rise, at, max_reach_m, reach_at, perTick: [[tick, rise, reach], ...] }
export function robeSail(glbPath, dump, { robeMat = /robe/, hemBand = 0.06 } = {}) {
  const g = readGlb(glbPath), jn = g.j.skins[0].joints.map((k) => g.j.nodes[k].name), names = dump.bones;
  const rp = prims(g).find((p) => robeMat.test(p.mat));
  if (!rp) throw new Error(`robe-sail: no robe primitive in ${glbPath}`);
  const ibm = g.acc(g.j.skins[0].inverseBindMatrices), hj = jn.indexOf('Hips');
  const hipsY = -ibm[hj * 16 + 13];
  const n = rp.P.length / 3, all = Array.from({ length: n }, (_, k) => k);
  const hem = Math.min(...all.map((k) => rp.P[k * 3 + 1])), L = hipsY - hem;
  const hemV = all.filter((k) => rp.P[k * 3 + 1] < hem + hemBand);
  const hb = names.indexOf('Hips');
  let max_rise = -1, at = null, max_reach = 0, reach_at = null;
  const perTick = [];
  for (const row of dump.poses) {
    const r = row.subarray ? row : Float64Array.from(row), mats = skinMats(g, names, r), V = skinVerts(rp, mats, all);
    const o = 1 + hb * 12, hip = [r[o + 3], r[o + 7], r[o + 11]], scale = Math.hypot(r[o], r[o + 4], r[o + 8]);
    let rise = -1, reach = 0, rv = -1, fv = -1;
    for (const k of hemV) { const h = (V[k * 3 + 1] - hip[1]) / scale, s = (h + L) / L; if (s > rise) rise = s, rv = k; }
    for (const k of all) { const d = Math.hypot(V[k * 3] - hip[0], V[k * 3 + 2] - hip[2]) / scale; if (d > reach) reach = d, fv = k; }
    const dom = (v) => { let b = -1, w = 0; for (let q = 0; q < 4; q++) if (rp.W[v * 4 + q] > w) w = rp.W[v * 4 + q], b = rp.J[v * 4 + q]; return jn[b]; };
    perTick.push([r[0], +rise.toFixed(3), +reach.toFixed(3)]);
    if (rise > max_rise) max_rise = rise, at = { tick: r[0], bone: dom(rv) };
    if (reach > max_reach) max_reach = reach, reach_at = { tick: r[0], bone: dom(fv) };
  }
  return { max_rise: +max_rise.toFixed(3), at, max_reach_m: +max_reach.toFixed(3), reach_at, perTick };
}
