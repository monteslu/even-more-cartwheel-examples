// Character builds: every human and humanoid figure the 3D games ship has athletic proportions; a big role (a brute)
// is broad and muscular. Each figure is measured in its rest pose from its front and side silhouettes (its
// triangles drawn flat, clothes included, as the camera sees them):
//
//   H                  stature: the soles to the crown of the head (hair, hats and helmets left out)
//   heads              H over the head's height, chin to crown
//   shoulder / waist   the breadth over the deltoids over the narrowest breadth of the trunk between the hips and ribs
//   waist / H          that breadth over the stature
//   upper arm, thigh   the limb's greatest thickness (front or side) across the middle half of its length, over that
//                      length; armour plates and bracers left out, sleeves and trouser legs kept, robes, coats and
//                      capes left out of the legs (they hang free of them); both sides measured, the leaner kept
//                      (gear worn on one side, a single pauldron or a holster, is not the limb)
//   foot / H           the foot's length heel to toe, shoe and all
//   hand / H           wrist to fingertip
//
// The limits, from an athletic adult (front breadths, clothed): shoulder over waist at least 1.4; the waist at most
// 0.19 H (0.205 H for a big role); the upper arm at most 0.42 of its length thick (0.5 big), the thigh at most
// 0.5 (0.58 big); the foot at most 0.17 H shod, the hand at most 0.12 H; 6.8 to 8.2 heads.
// A big role is built broad on purpose and keeps a waist and tapered limbs all the same.
// Creatures that are not humanoid (towers' bloater) are not listed.
// Towers' creeps (CREEPS) are .cwchar files with no skeleton and no rest pose: measureCreep finds their parts and joints
// from how the frames move, measures each limb on its own part and the trunk, head and stature in the walk frame
// standing tallest (see measureCreep).
//
// CONTROL: --control <rev> reads the figures from that commit (the figures as they shipped before the build pass,
// 1751b7a); every one of them must fail. --sil <dir> writes each figure's front and side silhouettes with the measured lines on them.
//
// Run: node tests/char-builds.mjs [--json] [--control <rev>] [--sil <dir>] [game ...]
import { readFileSync, existsSync, writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
export const CAST = {
  skate: { dir: 'games/skate/assets/models', figures: { kai: {}, nova: { woman: true }, dex: {}, marlo: { woman: true } } },
  emberthrone: { dir: 'games/emberthrone/assets/models', figures: {
    wayfarer: { woman: true }, warden: {}, raider: {}, lancer: {}, tidecaller: { woman: true },
    brute: { big: true }, thornwarden: { big: true }, emberking: { big: true } } },
};
// towers' creeps (.cwchar: frames of one mesh, no skeleton in the file; measured by measureCreep below). The humanoid
// ones only: skitter (a six-legged bug), hound (four legs), sporeling (a walking mushroom: a stalk under a cap) and
// bloater (round by design) are not humanoid. The ogre is a muscular brute. The shade is robed to the hem (no legs to
// measure). The control (1751b7a) holds crawler's creeps as towers first shipped them; every one of them fails.
export const CREEPS = {
  towers: { dir: 'games/towers/assets/models', figures: { ghoul: {}, imp: {}, shade: { robe: true }, ogre: { big: true } } },
};
export const LIMITS = {
  shoulderWaist: { min: 1.4 }, waistH: { max: 0.19, big: 0.205 }, upperArm: { max: 0.42, big: 0.5 },
  thigh: { max: 0.5, big: 0.58 }, footH: { max: 0.17 }, handH: { max: 0.12 }, heads: { min: 6.8, max: 8.2 },
};

/** a glb from disk, or from git (`<rev>:<path>`, the figures as they were at a commit) */
function readGlb(file) {
  const b = file.includes(':') && !existsSync(file) ? execFileSync('git', ['-C', ROOT, 'show', file], { maxBuffer: 1 << 30 }) : readFileSync(file);
  const jl = b.readUInt32LE(12), j = JSON.parse(b.subarray(20, 20 + jl).toString());
  const bin = b.subarray(20 + jl + 8);
  const acc = (i) => {
    const a = j.accessors[i], v = j.bufferViews[a.bufferView], n = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 }[a.type];
    const C = { 5126: Float32Array, 5123: Uint16Array, 5121: Uint8Array, 5125: Uint32Array }[a.componentType];
    const off = (v.byteOffset || 0) + (a.byteOffset || 0), stride = v.byteStride || n * C.BYTES_PER_ELEMENT, out = new Array(a.count);
    for (let k = 0; k < a.count; k++) { const s = bin.byteOffset + off + k * stride; out[k] = Array.from(new C(bin.buffer.slice(s, s + n * C.BYTES_PER_ELEMENT))); }
    return out;
  };
  return { j, acc };
}
/** the translation of a column-major affine 4x4's inverse (an inverse bind matrix's joint position) */
function jointPos(m) {
  const r = [m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]];
  const det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[3] * (r[1] * r[8] - r[2] * r[7]) + r[6] * (r[1] * r[5] - r[2] * r[4]);
  // R^-1 (column-major) = adj(R) / det
  const Ri = [r[4] * r[8] - r[5] * r[7], r[2] * r[7] - r[1] * r[8], r[1] * r[5] - r[2] * r[4],
    r[5] * r[6] - r[3] * r[8], r[0] * r[8] - r[2] * r[6], r[2] * r[3] - r[0] * r[5],
    r[3] * r[7] - r[4] * r[6], r[1] * r[6] - r[0] * r[7], r[0] * r[4] - r[1] * r[3]].map((x) => x / det);
  const t = [m[12], m[13], m[14]];
  return [0, 1, 2].map((i) => -(Ri[i] * t[0] + Ri[3 + i] * t[1] + Ri[6 + i] * t[2]));
}

const HAIRY = /hair|hat|helmet|band|hood|lash|fur|crown|plume/i; // never part of the body's outline
const PLATE = /plate|metal|armou?r|bracer|pad/i;                 // armour: kept for the outline, left out of a limb's own thickness
const DRAPE = /robe|cape|coat|skirt/i;                           // cloth hanging free of the legs (robes, coats, capes): the outline, not a limb
const SKIN = /skin|face|eye/i;
const sideOf = (b) => (/^Fing([LR])\d$/.exec(b) || /([LR])$/.exec(b))[1];
const chainOf = (b) => /^(UpperArm|LowerArm|Hand|Fing|Thumb)/.test(b) ? 'arm' + sideOf(b)
  : /^(UpperLeg|LowerLeg|Foot)/.test(b) ? 'leg' + b.slice(-1)
  : b === 'Head' || b === 'Neck' || b === 'HairA' || b === 'HairB' || /^(Jaw|Lid|Brow|Mouth|Lip)/.test(b) ? 'head' : 'trunk';

/** the rest-pose figure: triangles with their chain (the majority of their corners' dominant joints) and material */
export function loadFigure(file) {
  const { j, acc } = readGlb(file);
  const skin = j.skins[0], names = skin.joints.map((i) => j.nodes[i].name), ibm = acc(skin.inverseBindMatrices);
  const joint = Object.fromEntries(names.map((n, i) => [n, jointPos(ibm[i])]));
  const tris = [];
  for (const node of j.nodes) {
    if (node.mesh === undefined || node.skin === undefined) continue;
    for (const p of j.meshes[node.mesh].primitives) {
      const mat = j.materials?.[p.material]?.name ?? '';
      const P = acc(p.attributes.POSITION), Jt = acc(p.attributes.JOINTS_0), W = acc(p.attributes.WEIGHTS_0), I = acc(p.indices).map((x) => x[0]);
      const dom = (k) => { let best = 0; for (let q = 1; q < 4; q++) if (W[k][q] > W[k][best]) best = q; return names[Jt[k][best]]; };
      for (let k = 0; k + 2 < I.length; k += 3) {
        const c = [I[k], I[k + 1], I[k + 2]], bones = c.map(dom), chains = bones.map(chainOf);
        const chain = chains[0] === chains[1] || chains[0] === chains[2] ? chains[0] : chains[1] === chains[2] ? chains[1] : chains[0];
        tris.push({ v: c.map((i) => P[i]), chain, bones, mat });
      }
    }
  }
  return { joint, tris };
}

// ── rasterising: a triangle set flattened onto a plane (u across, w along), as rows of [min, max] at RES metres ──
const RES = 0.002;
function rows(tris, U, Wv, O = [0, 0, 0]) {
  const R = new Map();
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  for (const t of tris) {
    const P = t.v.map((p) => { const d = [p[0] - O[0], p[1] - O[1], p[2] - O[2]]; return [dot(d, U), dot(d, Wv)]; });
    const w0 = Math.ceil(Math.min(...P.map((p) => p[1])) / RES), w1 = Math.floor(Math.max(...P.map((p) => p[1])) / RES);
    for (let wi = w0; wi <= w1; wi++) {
      const w = wi * RES; let lo = Infinity, hi = -Infinity;
      for (let e = 0; e < 3; e++) {
        const a = P[e], b = P[(e + 1) % 3];
        if ((a[1] - w) * (b[1] - w) > 0 || a[1] === b[1] && a[1] !== w) continue;
        const s = a[1] === b[1] ? [a[0], b[0]] : [a[0] + (b[0] - a[0]) * (w - a[1]) / (b[1] - a[1])];
        for (const x of s) { lo = Math.min(lo, x); hi = Math.max(hi, x); }
      }
      if (hi < lo) continue;
      const r = R.get(wi); if (r) { r[0] = Math.min(r[0], lo); r[1] = Math.max(r[1], hi); } else R.set(wi, [lo, hi]);
    }
  }
  return R;
}
const X = [1, 0, 0], Y = [0, 1, 0], Z = [0, 0, 1];
const width = (R, w) => { const r = R.get(Math.round(w / RES)); return r ? r[1] - r[0] : 0; };

/** a limb segment A -> B: its greatest thickness, front and side, across the middle half of its length */
function limb(tris, A, B) {
  const ax = [B[0] - A[0], B[1] - A[1], B[2] - A[2]], L = Math.hypot(...ax), u = ax.map((x) => x / L);
  let f = [-u[1], u[0], 0]; const fl = Math.hypot(...f) || 1; f = f.map((x) => x / fl);
  const s = [u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]];
  const RF = rows(tris, f, u, A), RS = rows(tris, s, u, A);
  let front = 0, side = 0;
  for (let t = 0.25 * L; t <= 0.75 * L; t += RES) { front = Math.max(front, width(RF, t)); side = Math.max(side, width(RS, t)); }
  return { len: L, front, side };
}

export function measure(file) {
  const { joint: J, tris } = loadFigure(file);
  const body = tris.filter((t) => !HAIRY.test(t.mat));
  const all = (fn) => { let lo = Infinity, hi = -Infinity; for (const t of body) for (const p of t.v) { const x = fn(p); lo = Math.min(lo, x); hi = Math.max(hi, x); } return [lo, hi]; };
  const sole = all((p) => p[1])[0];
  // the head: its skin (under any hair or helmet), crown to chin; the chin is the lowest point of the face's middle
  const head = body.filter((t) => t.chain === 'head' && SKIN.test(t.mat));
  const hp = head.flatMap((t) => t.v);
  const crown = Math.max(...hp.map((p) => p[1]));
  const zf = Math.max(...hp.map((p) => p[2])), yNeck = J.Neck[1];
  const chinPts = hp.filter((p) => Math.abs(p[0]) < 0.02 && p[2] > zf - 0.06 && p[1] > yNeck - 0.02);
  const chin = Math.min(...chinPts.map((p) => p[1]));
  const H = crown - sole;
  // shoulders: the trunk and upper arms over the deltoids
  const shY = (J.UpperArmL[1] + J.UpperArmR[1]) / 2;
  const delt = body.filter((t) => t.chain === 'trunk' || t.chain.startsWith('arm') && t.bones.some((b) => /^UpperArm/.test(b)));
  const RD = rows(delt, X, Y);
  let shoulders = 0; for (let y = shY - 0.06 * H; y <= shY + 0.03 * H; y += RES) shoulders = Math.max(shoulders, width(RD, y));
  // the waist: the narrowest trunk row between a little over the hip joints and the chest joint (arms left out)
  const trunk = body.filter((t) => t.chain === 'trunk');
  const RT = rows(trunk, X, Y), RTs = rows(trunk, Z, Y);
  let waist = Infinity, waistY = 0;
  for (let y = J.Hips[1] + 0.02 * H; y <= J.Chest[1]; y += RES) { const w = width(RT, y); if (w > 0.05 * H && w < waist) { waist = w; waistY = y; } }
  const waistD = width(RTs, waistY);
  const hipsR = rows(body.filter((t) => t.chain === 'trunk' || t.chain.startsWith('leg')), X, Y);
  let hips = 0; for (let y = J.Hips[1] - 0.08 * H; y <= J.Hips[1]; y += RES) hips = Math.max(hips, width(hipsR, y));
  const out = { H, heads: H / (crown - chin), crown, chin, shY, shoulders, waist, waistY, waistD, hips };
  // each limb on both sides, the leaner side kept: a piece worn on one side only (a single pauldron, a holster) is
  // gear, not the limb; the left side is kept for the foot and the hand
  const lean = (a, b) => (Math.max(a.front, a.side) <= Math.max(b.front, b.side) ? a : b);
  const limbs = (sd) => {
    const arm = body.filter((t) => t.chain === 'arm' + sd && !PLATE.test(t.mat)), leg = body.filter((t) => t.chain === 'leg' + sd && !PLATE.test(t.mat) && !DRAPE.test(t.mat));
    return { ua: limb(arm, J['UpperArm' + sd], J['LowerArm' + sd]), fa: limb(arm, J['LowerArm' + sd], J['Hand' + sd]),
      th: limb(leg, J['UpperLeg' + sd], J['LowerLeg' + sd]), ca: limb(leg, J['LowerLeg' + sd], J['Foot' + sd]), leg };
  };
  const Ls = limbs('L'), Rs = limbs('R');
  for (const k of ['ua', 'fa', 'th', 'ca']) out[k] = lean(Ls[k], Rs[k]);
  const sd = 'L', leg = Ls.leg;
  const fp = leg.filter((t) => t.bones.some((b) => b === 'Foot' + sd)).flatMap((t) => t.v).filter((p) => p[1] < J['Foot' + sd][1] + 0.03);
  out.foot = Math.max(...fp.map((p) => p[2])) - Math.min(...fp.map((p) => p[2]));
  const hj = J['Hand' + sd], hv = body.filter((t) => t.chain === 'arm' + sd && t.bones.some((b) => /^(Hand|Fing|Thumb)/.test(b))).flatMap((t) => t.v);
  out.hand = Math.max(...hv.map((p) => Math.hypot(p[0] - hj[0], p[1] - hj[1], p[2] - hj[2])));
  out.J = J; out.body = body;
  return out;
}

export function judge(m, role = {}) {
  const big = !!role.big, r = {
    heads: m.heads, shoulderWaist: m.shoulders / m.waist, waistH: m.waist / m.H, waistDepthH: m.waistD / m.H, hipH: m.hips / m.H, shoulderH: m.shoulders / m.H,
    upperArm: Math.max(m.ua.front, m.ua.side) / m.ua.len, thigh: Math.max(m.th.front, m.th.side) / m.th.len,
    forearmH: Math.max(m.fa.front, m.fa.side) / m.H, calfH: Math.max(m.ca.front, m.ca.side) / m.H, footH: m.foot / m.H, handH: m.hand / m.H,
  };
  const fails = [];
  if (r.shoulderWaist < LIMITS.shoulderWaist.min) fails.push('shoulder/waist');
  if (r.waistH > (big ? LIMITS.waistH.big : LIMITS.waistH.max)) fails.push('waist');
  if (r.upperArm > (big ? LIMITS.upperArm.big : LIMITS.upperArm.max)) fails.push('upper arm');
  if (r.thigh > (big ? LIMITS.thigh.big : LIMITS.thigh.max)) fails.push('thigh');
  if (r.footH > LIMITS.footH.max) fails.push('foot');
  if (r.handH > LIMITS.handH.max) fails.push('hand');
  if (r.heads < LIMITS.heads.min || r.heads > LIMITS.heads.max) fails.push('heads');
  return { r, fails };
}

// ── towers' creeps: .cwchar models (frames of the same vertices moved, no skeleton in the file) ──
const v3 = {
  sub: (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], add: (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]],
  dot: (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2], len: (a) => Math.hypot(a[0], a[1], a[2]), scl: (a, k) => [a[0] * k, a[1] * k, a[2] * k],
  cross: (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]],
};
const inv3 = (m) => {
  const [a, b, c, d, e, f, g, h, i] = m, A = e * i - f * h, B = f * g - d * i, C = d * h - e * g, det = a * A + b * B + c * C;
  if (Math.abs(det) < 1e-9) return null;
  return [A, c * h - b * i, b * f - c * e, B, a * i - c * g, c * d - a * f, C, b * g - a * h, a * e - b * d].map((x) => x / det);
};
const mm = (A, B) => { const R = new Array(9).fill(0); for (let r = 0; r < 3; r++) for (let q = 0; q < 3; q++) for (let k = 0; k < 3; k++) R[r * 3 + q] += A[r * 3 + k] * B[k * 3 + q]; return R; };
const mv = (A, v) => [A[0] * v[0] + A[1] * v[1] + A[2] * v[2], A[3] * v[0] + A[4] * v[1] + A[5] * v[2], A[6] * v[0] + A[7] * v[1] + A[8] * v[2]];
const cols = (u, v, w) => [u[0], v[0], w[0], u[1], v[1], w[1], u[2], v[2], w[2]];

/** a .cwchar (tools/blender/cwchar_lib.py): every group's vertices, triangles and per-frame positions, as one mesh */
export function readCwchar(b) {
  if (b.toString('latin1', 0, 4) !== 'CWCH') throw new Error('not a .cwchar');
  let o = 8; const u32 = () => { const v = b.readUInt32LE(o); o += 4; return v; };
  const nclip = u32(), ng = u32(), nf = []; for (let k = 0; k < nclip; k++) nf.push(u32());
  const F = nf.reduce((a, x) => a + x, 0), frames = Array.from({ length: F }, () => []), idx = [];
  let base = 0;
  for (let g = 0; g < ng; g++) {
    const nv = u32(), nt = u32();
    o += 20 * nv;
    for (let t = 0; t < nt * 3; t++) idx.push(base + b.readUInt16LE(o + 2 * t));
    o += (nt * 6 + 3) & ~3;
    for (let f = 0; f < F; f++) { for (let i = 0; i < nv * 3; i++) frames[f].push(b.readFloatLE(o + 4 * i)); o += 24 * nv; }
    base += nv;
  }
  return { nf, F, nv: base, idx, frames };
}

/** The parts of a creep, recovered from how its frames move: pieces of mesh (connected triangles) that keep their shape
 * in every frame are rigid; the rigid pieces carried by one transform in every frame are one bone's part; two parts
 * meet at the point both carry to the same place in every frame (a joint). The parts and joints make a tree. */
export function creepRig(c) {
  const { F, nv, idx } = c, P = (f, i) => [c.frames[f][i * 3], c.frames[f][i * 3 + 1], c.frames[f][i * 3 + 2]];
  const par = [...Array(nv).keys()], find = (x) => { while (par[x] !== x) x = par[x] = par[par[x]]; return x; };
  for (let t = 0; t < idx.length; t += 3) for (const k of [1, 2]) { const a = find(idx[t]), b = find(idx[t + k]); if (a !== b) par[a] = b; }
  const groups = () => { const m = new Map(); for (let i = 0; i < nv; i++) { const r = find(i); if (!m.has(r)) m.set(r, []); m.get(r).push(i); } return m; };
  const xforms = (vs) => { // the affine map of frame 0 onto each frame, from four spread points of the piece (null when it is flat)
    const p = (i) => P(0, i), ctr = [0, 1, 2].map((k) => vs.reduce((s, i) => s + p(i)[k], 0) / vs.length);
    const far = (fn) => { let b = vs[0], best = -1; for (const i of vs) { const d = fn(p(i)); if (d > best) { best = d; b = i; } } return b; };
    const a = far((q) => v3.len(v3.sub(q, ctr))), b = far((q) => v3.len(v3.sub(q, p(a)))), ab = v3.sub(p(b), p(a));
    const cc = far((q) => v3.len(v3.cross(ab, v3.sub(q, p(a))))), n = v3.cross(ab, v3.sub(p(cc), p(a))), dd = far((q) => Math.abs(v3.dot(n, v3.sub(q, p(a)))));
    const Pi = inv3(cols(v3.sub(p(b), p(a)), v3.sub(p(cc), p(a)), v3.sub(p(dd), p(a))));
    if (!Pi) return null;
    return Array.from({ length: F }, (_, f) => {
      const L = mm(cols(v3.sub(P(f, b), P(f, a)), v3.sub(P(f, cc), P(f, a)), v3.sub(P(f, dd), P(f, a))), Pi);
      return { L, t: v3.sub(P(f, a), mv(L, p(a))) };
    });
  };
  const apply = (T, x) => v3.add(mv(T.L, x), T.t);
  const err = (T, vs) => { let e = 0; for (const i of vs) for (let f = 0; f < F; f++) e = Math.max(e, v3.len(v3.sub(apply(T[f], P(0, i)), P(f, i)))); return e; };
  const TOL = 2e-3;
  // seams and flat-shaded faces split a piece: pieces sharing a corner in every frame, rigid together, are one piece
  const key = new Map();
  for (let i = 0; i < nv; i++) { let k = ''; for (let f = 0; f < F; f++) k += P(f, i).map((x) => Math.round(x * 2e3)).join(',') + ';'; if (!key.has(k)) key.set(k, []); key.get(k).push(i); }
  const mem = groups();
  for (const ids of key.values()) for (let q = 1; q < ids.length; q++) {
    const a = find(ids[0]), b = find(ids[q]); if (a === b) continue;
    const vs = [...mem.get(a), ...mem.get(b)], T = xforms(vs);
    if (!T || err(T, vs) < TOL) { par[a] = b; mem.set(b, vs); mem.delete(a); }
  }
  const pieces = [...groups().values()].sort((a, b) => b.length - a.length);
  const parts = [], loose = [], pieceOf = new Int32Array(nv);
  pieces.forEach((vs, k) => { for (const i of vs) pieceOf[i] = k; });
  for (const vs of pieces) {
    const T = xforms(vs);
    if (T && err(T, vs) >= TOL) { loose.push(...vs); continue; }   // skinned across two bones: the trunk's cloth and flesh
    const B = parts.find((B) => err(B.T, vs) < TOL);
    if (B) B.vs.push(...vs); else if (T) parts.push({ T, vs: [...vs] }); else loose.push(...vs);
  }
  for (const B of parts) {
    B.c = [0, 1, 2].map((k) => B.vs.reduce((s, i) => s + P(0, i)[k], 0) / B.vs.length);
    const C = new Array(9).fill(0); for (const i of B.vs) { const d = v3.sub(P(0, i), B.c); for (let r = 0; r < 3; r++) for (let q = 0; q < 3; q++) C[r * 3 + q] += d[r] * d[q]; }
    let d = [0.3, 1, 0.2]; for (let it = 0; it < 40; it++) { d = mv(C, d); const l = v3.len(d) || 1; d = v3.scl(d, 1 / l); }
    B.axis = d;   // the part's long axis (a limb's bone line)
  }
  const onAxis = (B, p) => v3.add(B.c, v3.scl(B.axis, v3.dot(v3.sub(p, B.c), B.axis)));
  // joints: every close pair's common point (least squares over the frames, pulled a little toward their long axes);
  // the tree keeps the best fits (the least slip, then the least gap)
  const cand = [];
  for (let a = 0; a < parts.length; a++) for (let b = a + 1; b < parts.length; b++) {
    const A = parts[a], B = parts[b], step = (vs) => Math.max(1, Math.ceil(vs.length / 150));
    let gap = Infinity, mid = null;
    for (let x = 0; x < A.vs.length; x += step(A.vs)) for (let y = 0; y < B.vs.length; y += step(B.vs)) {
      const p = P(0, A.vs[x]), q = P(0, B.vs[y]), d = v3.len(v3.sub(p, q)); if (d < gap) { gap = d; mid = v3.scl(v3.add(p, q), 0.5); }
    }
    if (gap > 0.12) continue;
    mid = v3.scl(v3.add(onAxis(A, mid), onAxis(B, mid)), 0.5);   // what the frames leave open (a hinge's axis) is settled on the parts' own lines
    const lam = 0.02, N = [lam, 0, 0, 0, lam, 0, 0, 0, lam], r = v3.scl(mid, lam), rows = [];
    for (let f = 0; f < F; f++) {
      const D = A.T[f].L.map((x, k) => x - B.T[f].L[k]), e = v3.sub(B.T[f].t, A.T[f].t); rows.push([D, e]);
      for (let i = 0; i < 3; i++) { for (let j = 0; j < 3; j++) for (let k = 0; k < 3; k++) N[i * 3 + j] += D[k * 3 + i] * D[k * 3 + j]; for (let k = 0; k < 3; k++) r[i] += D[k * 3 + i] * e[k]; }
    }
    const Ni = inv3(N); if (!Ni) continue;
    const p = mv(Ni, r); let slip = 0; for (const [D, e] of rows) slip = Math.max(slip, v3.len(v3.sub(mv(D, p), e)));
    if (slip < 0.01) cand.push({ a, b, p, score: slip + 0.05 * gap });
  }
  cand.sort((x, y) => x.score - y.score);
  const tp = [...parts.keys()], tf = (x) => { while (tp[x] !== x) x = tp[x] = tp[tp[x]]; return x; }, joints = new Map();
  for (const B of parts) B.n = [];
  for (const j of cand) { const a = tf(j.a), b = tf(j.b); if (a === b) continue; tp[a] = b; parts[j.a].n.push(j.b); parts[j.b].n.push(j.a); joints.set(`${Math.min(j.a, j.b)},${Math.max(j.a, j.b)}`, j.p); }
  const joint = (a, b) => joints.get(`${Math.min(a, b)},${Math.max(a, b)}`);
  return { F, nf: c.nf, P, idx, parts, loose, joint, pieces, pieceOf };
}

/** a piece shaped as a spike (a cone to a point: horns, spines, claws, a hood's point) */
function spiky(R, vs) {
  const p = vs.map((i) => R.P(0, i)); if (p.length < 4) return false;
  let a = p[0], b = p[0], best = -1; for (const q of p) for (const s of p) { const d = v3.len(v3.sub(q, s)); if (d > best) { best = d; a = q; b = s; } }
  const ax = v3.scl(v3.sub(b, a), 1 / best), rad = (q) => v3.len(v3.cross(v3.sub(q, a), ax));
  const rmax = Math.max(...p.map(rad)), end = (t0, t1) => Math.max(0, ...p.filter((q) => { const t = v3.dot(v3.sub(q, a), ax) / best; return t >= t0 && t <= t1; }).map(rad));
  const r0 = end(0, 0.1), r1 = end(0.9, 1);
  return best > 1.5 * rmax && Math.min(r0, r1) < 0.15 * rmax && Math.max(r0, r1) > 0.6 * rmax;
}

/** a creep measured as measure() measures a glb figure. Its parts are found from the frames (creepRig), the limbs
 * labelled from the tree (a foot touches the ground, a shin and a thigh lead up from it to the pelvis; an arm hangs from
 * the chest's side, its forearm a leaf); the trunk, head and stature from the walk frame standing tallest. Limb
 * thickness is the part's own shape, the same in every frame. Spikes (horns, spines, a hood's point) are left out of
 * the crown and the breadths, as hair and helmets are on a person; claws count in the hand. role.robe: no legs. */
export function measureCreep(buf, role = {}) {
  const R = creepRig(readCwchar(buf)), { parts, P, idx } = R;
  const spike = new Uint8Array(R.pieceOf.length);
  R.pieces.forEach((vs) => { if (spiky(R, vs)) for (const i of vs) spike[i] = 1; });
  const partOf = new Int32Array(R.pieceOf.length).fill(-1); parts.forEach((B, k) => { for (const i of B.vs) partOf[i] = k; });
  const minY = (B, f = 0) => Math.min(...B.vs.map((i) => P(f, i)[1]));
  const ground = Math.min(...parts.map((B) => minY(B)));
  // legs: foot (on the ground) -> shin -> thigh -> pelvis, each step higher
  const legs = {};
  if (!role.robe) for (const [k, B] of parts.entries()) {
    if (B.n.length !== 1 || minY(B) > ground + 0.08) continue;
    const shin = B.n[0], S = parts[shin]; if (S.n.length < 2 || S.c[1] < B.c[1] + 0.1) continue;
    const thigh = S.n.find((x) => x !== k), T = parts[thigh]; if (T.c[1] < S.c[1] + 0.1) continue;
    const up = T.n.filter((x) => x !== shin); if (!up.length) continue;
    const pelvis = up.sort((x, y) => Math.abs(parts[x].c[0]) - Math.abs(parts[y].c[0]))[0];
    const side = B.c[0] > 0 ? 'L' : 'R';
    legs[side] = { foot: k, shin, thigh, pelvis, hip: R.joint(thigh, pelvis), knee: R.joint(thigh, shin), ankle: R.joint(shin, k) };
  }
  const inLeg = new Set(Object.values(legs).flatMap((l) => [l.foot, l.shin, l.thigh]));
  // arms: a leaf hanging (its middle below the joint) from a part at the side; its parent is the upper arm when that
  // in turn hangs from a part nearer the middle (else the arm is one piece: the elbow taken at 0.47 of its length)
  const arms = {};
  for (const [k, B] of parts.entries()) {
    if (B.n.length !== 1 || inLeg.has(k)) continue;
    const u = B.n[0], U = parts[u], j = R.joint(k, u); if (B.c[1] > j[1] - 0.05 || Math.abs(j[0]) < 0.08) continue;
    const side = j[0] > 0 ? 'L' : 'R';
    let arm;
    const up = U.n.filter((x) => x !== k);
    if (up.length === 1 && Math.abs(parts[up[0]].c[0]) < Math.abs(U.c[0]) && R.joint(u, up[0])[1] > j[1]) arm = { farm: k, uarm: u, chest: up[0], shoulder: R.joint(u, up[0]), elbow: j };
    else arm = { farm: k, uarm: null, chest: u, shoulder: j, elbow: null };
    if (!arms[side] || B.vs.length > parts[arms[side].farm].vs.length) arms[side] = arm;
  }
  const inArm = new Set(Object.values(arms).flatMap((a) => [a.farm, a.uarm].filter((x) => x !== null)));
  // wings: a leaf at the side rising from its joint (left out of the trunk's breadths, as an arm is)
  const wing = new Set(parts.map((B, k) => k).filter((k) => parts[k].n.length === 1 && !inArm.has(k) && !inLeg.has(k)
    && (() => { const j = R.joint(k, parts[k].n[0]); return Math.abs(j[0]) > 0.05 && parts[k].c[1] > j[1]; })()));
  // the head: the highest part on the middle line above the shoulders that is not a limb (wings sit to the sides)
  const shY0 = Math.max(...Object.values(arms).map((a) => a.shoulder[1]));
  const head = parts.map((B, k) => k).filter((k) => !inArm.has(k) && !inLeg.has(k) && parts[k].c[1] > shY0 && Math.abs(parts[k].c[0]) < 0.1)
    .sort((a, b) => Math.max(...parts[b].vs.map((i) => P(0, i)[1])) - Math.max(...parts[a].vs.map((i) => P(0, i)[1])))[0];
  // the frame standing tallest (of the walk): crown over the soles
  const nwalk = R.nf[0];
  const headTop = (f) => Math.max(...parts[head].vs.filter((i) => !spike[i]).map((i) => P(f, i)[1]));
  const soleAt = (f) => role.robe ? Math.min(...R.idx.map((i) => P(f, i)[1])) : Math.min(...Object.values(legs).flatMap((l) => parts[l.foot].vs.map((i) => P(f, i)[1])));
  let fr = 0; for (let f = 1; f < nwalk; f++) if (headTop(f) - soleAt(f) > headTop(fr) - soleAt(fr)) fr = f;
  const Tf = (B, p) => v3.add(mv(B.T[fr].L, p), B.T[fr].t);   // a frame-0 point of a part, carried to the measuring frame
  const tri = (pred) => { const out = []; for (let t = 0; t < idx.length; t += 3) { const c3 = [idx[t], idx[t + 1], idx[t + 2]]; if (c3.every(pred)) out.push({ v: c3.map((i) => P(fr, i)), mat: c3.some((i) => spike[i]) ? 'hat' : 'body' }); } return out; };
  const sole = soleAt(fr), crown = headTop(fr), H = crown - sole;
  const hv = parts[head].vs.filter((i) => !spike[i]).map((i) => P(fr, i)[1]), chin = Math.min(...hv);
  const out = { H, heads: H / (crown - chin), crown, chin, frame: fr, legs, arms };
  // limbs (left side: the right hand may hold a weapon), measured on their own parts in frame 0
  const partTris = (k) => { const out2 = []; for (let t = 0; t < idx.length; t += 3) { const c3 = [idx[t], idx[t + 1], idx[t + 2]]; if (c3.every((i) => partOf[i] === k && !spike[i])) out2.push({ v: c3.map((i) => P(0, i)) }); } return out2; };
  const a = arms.L || arms.R;
  const fv = parts[a.farm].vs.map((i) => P(0, i));
  let elbow = a.elbow, uarmTris = a.uarm !== null ? partTris(a.uarm) : null;
  const tipOf = (from, vs) => vs.reduce((m, p) => (v3.len(v3.sub(p, from)) > v3.len(v3.sub(m, from)) ? p : m), vs[0]);
  if (!elbow) { const tip = tipOf(a.shoulder, fv), ax = v3.sub(tip, a.shoulder); elbow = v3.add(a.shoulder, v3.scl(ax, 0.47)); uarmTris = partTris(a.farm); }
  const tip = tipOf(elbow, fv.filter((p, n) => !spike[parts[a.farm].vs[n]] || true));
  out.ua = limb(uarmTris, a.shoulder, elbow);
  out.fa = limb(partTris(a.farm), elbow, tip);
  // the hand: the pieces of the forearm's part that start past its first sixth and reach past seven tenths of it
  { const ax = v3.sub(tip, elbow), L = v3.len(ax), u = v3.scl(ax, 1 / L); let start = L;
    const byPiece = new Map(); for (const i of parts[a.farm].vs) { const k = R.pieceOf[i], t = v3.dot(v3.sub(P(0, i), elbow), u); const r = byPiece.get(k) || [Infinity, -Infinity]; byPiece.set(k, [Math.min(r[0], t), Math.max(r[1], t)]); }
    for (const [lo, hi] of byPiece.values()) if (lo > L / 6 && hi > 0.7 * L) start = Math.min(start, lo);
    out.hand = L - Math.min(start, L); }
  const leg = legs.L || legs.R;
  if (leg) {
    out.th = limb(partTris(leg.thigh), leg.hip, leg.knee);
    const fp = parts[leg.foot].vs.map((i) => P(0, i));
    out.ca = limb(partTris(leg.shin), leg.knee, leg.ankle);
    // the foot: heel to toe, its part's extent along its own long axis (the same in every frame)
    const F0 = parts[leg.foot], ts = fp.map((p) => v3.dot(v3.sub(p, F0.c), F0.axis));
    out.foot = Math.max(...ts) - Math.min(...ts);
  } else { out.th = out.ca = { len: 1, front: NaN, side: NaN }; out.foot = NaN; }
  // shoulders and waist in the measuring frame: the trunk is every part that is no limb and no head, and the cloth
  const shY = Tf(parts[a.chest], a.shoulder)[1]; out.shY = shY;
  const hipY = leg ? Tf(parts[leg.pelvis], leg.hip)[1] : sole + 0.5 * H;
  const trunkV = (i) => !spike[i] && (partOf[i] < 0 || !inArm.has(partOf[i]) && !inLeg.has(partOf[i]) && !wing.has(partOf[i]) && partOf[i] !== head);
  const trunk = tri(trunkV), delt = tri((i) => trunkV(i) || !spike[i] && partOf[i] >= 0 && (partOf[i] === a.uarm || a.uarm === null && partOf[i] === a.farm));
  const RD = rows(delt, X, Y);
  let shoulders = 0; for (let y = shY - 0.06 * H; y <= shY + 0.03 * H; y += RES) shoulders = Math.max(shoulders, width(RD, y));
  const RT = rows(trunk, X, Y), RTs = rows(trunk, Z, Y);
  let waist = Infinity, waistY = 0;
  for (let y = hipY + 0.02 * H; y <= shY - 0.1 * H; y += RES) { const w = width(RT, y); if (w > 0.05 * H && w < waist) { waist = w; waistY = y; } }
  Object.assign(out, { shoulders, waist, waistY, waistD: width(RTs, waistY), hips: 0 });
  const hipsR = rows(tri((i) => !spike[i] && (trunkV(i) || inLeg.has(partOf[i]))), X, Y);
  for (let y = hipY - 0.08 * H; y <= hipY; y += RES) out.hips = Math.max(out.hips, width(hipsR, y));
  out.J = { Hips: [0, hipY, 0] };
  out.allTris = tri(() => true);
  return out;
}

/** front and side silhouettes (body dark, hair and helmets grey) with the measured lines: crown, chin, shoulders, waist */
function silhouette(m, file) {
  const S = 520, top = m.crown + 0.12, bot = m.sole ?? -0.02, k = S / (top - bot), Wd = Math.round((m.silW ?? 0.9) * k);
  const img = new Uint8Array(Wd * 2 * S).fill(255);
  for (const [ax, ox] of [[0, 0], [2, Wd]]) for (const { v, mat } of m.allTris) {
    const P = v.map((p) => [ox + Wd / 2 + p[ax] * k, S - (p[1] - bot) * k]);
    const x0 = Math.max(ox, Math.floor(Math.min(...P.map((p) => p[0])))), x1 = Math.min(ox + Wd - 1, Math.ceil(Math.max(...P.map((p) => p[0]))));
    const y0 = Math.max(0, Math.floor(Math.min(...P.map((p) => p[1])))), y1 = Math.min(S - 1, Math.ceil(Math.max(...P.map((p) => p[1]))));
    const e = (p, q, x, y) => (q[0] - p[0]) * (y - p[1]) - (q[1] - p[1]) * (x - p[0]), ar = e(P[0], P[1], P[2][0], P[2][1]);
    if (Math.abs(ar) < 1e-9) continue;
    const g = HAIRY.test(mat) ? 160 : 40;
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
      const a = e(P[1], P[2], x + 0.5, y + 0.5) / ar, b = e(P[2], P[0], x + 0.5, y + 0.5) / ar;
      if (a >= 0 && b >= 0 && a + b <= 1) img[y * Wd * 2 + x] = Math.min(img[y * Wd * 2 + x], g);
    }
  }
  for (const y of [m.crown, m.chin, m.shY, m.waistY, m.J.Hips[1]]) { const r = Math.round(S - (y - bot) * k); if (r >= 0 && r < S) for (let x = 0; x < Wd * 2; x += 2) img[r * Wd * 2 + x] = 200; }
  const pgm = file.replace(/\.png$/, '.pgm');
  writeFileSync(pgm, Buffer.concat([Buffer.from(`P5 ${Wd * 2} ${S} 255\n`), Buffer.from(img)]));
  execFileSync('convert', [pgm, file]); rmSync(pgm);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2), json = args.includes('--json');
  const opt = (k) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : null; };
  const controlDir = opt('--control'), silDir = opt('--sil');
  const games = args.filter((a, i) => !a.startsWith('--') && !['--control', '--sil'].includes(args[i - 1]));
  const rowsOut = [];
  const cast = [...Object.entries(CAST).map(([g, c]) => [g, c, '.glb']), ...Object.entries(CREEPS).map(([g, c]) => [g, c, '.cwchar'])];
  for (const [game, c, ext] of cast) {
    if (games.length && !games.includes(game)) continue;
    for (const [name, role] of Object.entries(c.figures)) {
      const file = controlDir ? `${controlDir}:${c.dir}/${name}${ext}` : join(ROOT, c.dir, name + ext);
      if (!controlDir && !existsSync(file)) { console.error(`missing ${file}`); process.exitCode = 1; continue; }
      const m = ext === '.glb' ? measure(file) : measureCreep(file.includes(':') && !existsSync(file) ? execFileSync('git', ['-C', ROOT, 'show', file], { maxBuffer: 1 << 30 }) : readFileSync(file), role);
      const { r, fails } = judge(m, role);
      if (silDir) { mkdirSync(silDir, { recursive: true }); m.allTris ??= loadFigure(file).tris; silhouette(m, join(silDir, `${game}-${name}.png`)); }
      rowsOut.push({ game, name, role: role.big ? 'big' : role.woman ? 'woman' : role.robe ? 'robe' : 'man', H: m.H, ...r, fails });
    }
  }
  const f = (x, d = 2) => x.toFixed(d);
  if (json) console.log(JSON.stringify(rowsOut, null, 1));
  else {
    console.log('game         figure       role   H     heads sh/wa sh/H  wa/H  waD/H hip/H uArm  thigh fArm/H calf/H foot/H hand/H  verdict');
    for (const x of rowsOut) console.log([x.game.padEnd(12), x.name.padEnd(12), x.role.padEnd(6), f(x.H), f(x.heads).padStart(5), f(x.shoulderWaist).padStart(5),
      f(x.shoulderH, 3), f(x.waistH, 3), f(x.waistDepthH, 3), f(x.hipH, 3), f(x.upperArm).padStart(5), f(x.thigh).padStart(5),
      f(x.forearmH, 3).padStart(6), f(x.calfH, 3).padStart(6), f(x.footH, 3).padStart(6), f(x.handH, 3).padStart(6), ' ' + (x.fails.length ? 'FAIL ' + x.fails.join(', ') : 'ok')].join(' '));
  }
  const failed = rowsOut.filter((x) => x.fails.length);
  if (controlDir) {
    if (failed.length < rowsOut.length) { console.error(`CONTROL: ${rowsOut.length - failed.length} old figure(s) pass; every old figure in the control set must fail`); process.exitCode = 1; }
    else console.log(`control: all ${rowsOut.length} old figures fail, as they must`);
  } else if (failed.length) process.exitCode = 1;
}
