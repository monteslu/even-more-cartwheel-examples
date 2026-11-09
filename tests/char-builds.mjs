// Character builds: every human and humanoid figure the 3D games ship reads as a fit, athletic person; a heavy role
// reads as muscle, not fat. Each figure is measured in its rest pose from its front and side silhouettes (its
// triangles drawn flat, clothes included, as the camera sees them):
//
//   H                  stature: the soles to the crown of the head (hair, hats and helmets left out)
//   heads              H over the head's height, chin to crown
//   shoulder / waist   the breadth over the deltoids over the narrowest breadth of the trunk between the hips and ribs
//   waist / H          that breadth over the stature
//   upper arm, thigh   the limb's greatest thickness (front or side) across the middle half of its length, over that
//                      length; armour plates and bracers left out, sleeves and trouser legs kept
//   foot / H           the foot's length heel to toe, shoe and all
//   hand / H           wrist to fingertip
//
// The limits, from an athletic adult (front breadths, clothed): shoulder over waist at least 1.4; the waist at most
// 0.19 H (0.205 H for a heavy role); the upper arm at most 0.42 of its length thick (0.5 heavy), the thigh at most
// 0.5 (0.58 heavy); the foot at most 0.17 H shod, the hand at most 0.12 H; 6.8 to 8.2 heads.
// A heavy role (a brute) is built big on purpose and keeps a waist and tapered limbs all the same.
// Creatures bloated by design (towers' bloater) are not humanoid and are not listed.
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
    brute: { heavy: true }, thornwarden: { heavy: true }, emberking: { heavy: true } } },
};
export const LIMITS = {
  shoulderWaist: { min: 1.4 }, waistH: { max: 0.19, heavy: 0.205 }, upperArm: { max: 0.42, heavy: 0.5 },
  thigh: { max: 0.5, heavy: 0.58 }, footH: { max: 0.17 }, handH: { max: 0.12 }, heads: { min: 6.8, max: 8.2 },
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
  const sd = 'L';
  const arm = body.filter((t) => t.chain === 'arm' + sd && !PLATE.test(t.mat)), leg = body.filter((t) => t.chain === 'leg' + sd && !PLATE.test(t.mat));
  out.ua = limb(arm, J['UpperArm' + sd], J['LowerArm' + sd]); out.fa = limb(arm, J['LowerArm' + sd], J['Hand' + sd]);
  out.th = limb(leg, J['UpperLeg' + sd], J['LowerLeg' + sd]); out.ca = limb(leg, J['LowerLeg' + sd], J['Foot' + sd]);
  const fp = leg.filter((t) => t.bones.some((b) => b === 'Foot' + sd)).flatMap((t) => t.v).filter((p) => p[1] < J['Foot' + sd][1] + 0.03);
  out.foot = Math.max(...fp.map((p) => p[2])) - Math.min(...fp.map((p) => p[2]));
  const hj = J['Hand' + sd], hv = body.filter((t) => t.chain === 'arm' + sd && t.bones.some((b) => /^(Hand|Fing|Thumb)/.test(b))).flatMap((t) => t.v);
  out.hand = Math.max(...hv.map((p) => Math.hypot(p[0] - hj[0], p[1] - hj[1], p[2] - hj[2])));
  out.J = J; out.body = body;
  return out;
}

export function judge(m, role = {}) {
  const heavy = !!role.heavy, r = {
    heads: m.heads, shoulderWaist: m.shoulders / m.waist, waistH: m.waist / m.H, waistDepthH: m.waistD / m.H, hipH: m.hips / m.H, shoulderH: m.shoulders / m.H,
    upperArm: Math.max(m.ua.front, m.ua.side) / m.ua.len, thigh: Math.max(m.th.front, m.th.side) / m.th.len,
    forearmH: Math.max(m.fa.front, m.fa.side) / m.H, calfH: Math.max(m.ca.front, m.ca.side) / m.H, footH: m.foot / m.H, handH: m.hand / m.H,
  };
  const fails = [];
  if (r.shoulderWaist < LIMITS.shoulderWaist.min) fails.push('shoulder/waist');
  if (r.waistH > (heavy ? LIMITS.waistH.heavy : LIMITS.waistH.max)) fails.push('waist');
  if (r.upperArm > (heavy ? LIMITS.upperArm.heavy : LIMITS.upperArm.max)) fails.push('upper arm');
  if (r.thigh > (heavy ? LIMITS.thigh.heavy : LIMITS.thigh.max)) fails.push('thigh');
  if (r.footH > LIMITS.footH.max) fails.push('foot');
  if (r.handH > LIMITS.handH.max) fails.push('hand');
  if (r.heads < LIMITS.heads.min || r.heads > LIMITS.heads.max) fails.push('heads');
  return { r, fails };
}

/** front and side silhouettes (body dark, hair and helmets grey) with the measured lines: crown, chin, shoulders, waist */
function silhouette(m, file) {
  const S = 520, top = m.crown + 0.12, bot = -0.02, k = S / (top - bot), Wd = Math.round(0.9 * k);
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
  for (const [game, c] of Object.entries(CAST)) {
    if (games.length && !games.includes(game)) continue;
    for (const [name, role] of Object.entries(c.figures)) {
      const file = controlDir ? `${controlDir}:${c.dir}/${name}.glb` : join(ROOT, c.dir, name + '.glb');
      if (!controlDir && !existsSync(file)) { console.error(`missing ${file}`); process.exitCode = 1; continue; }
      const m = measure(file), { r, fails } = judge(m, role);
      if (silDir) { mkdirSync(silDir, { recursive: true }); m.allTris = loadFigure(file).tris; silhouette(m, join(silDir, `${game}-${name}.png`)); }
      rowsOut.push({ game, name, role: role.heavy ? 'heavy' : role.woman ? 'woman' : 'man', H: m.H, ...r, fails });
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
