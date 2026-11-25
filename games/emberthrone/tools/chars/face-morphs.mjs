// face-morphs.mjs: expression morph targets on a figure's face, so its mouth moves with its voice and its brows and
// mouth show what it feels (the engine's cw_face, cartwheel/speech.h, drives them by name).
//
//   node games/emberthrone/tools/chars/face-morphs.mjs assets/models/wayfarer.glb [...]   (in place; run again: same)
//
// make-characters.mjs calls addFaceMorphs on each figure it writes; this script applies it to the figures as they are.
// The figure's face, eyes and brows move out of the Body mesh into a mesh of their own, "Face" (same skin, same
// materials): only that small mesh is morphed (a morphed mesh is drawn on its own and reads every target per vertex).
// The targets are made from the face's own landmarks, measured on the mesh in its bind pose (head joint, eyes, nose
// tip, chin), in units of the eye-to-chin height H, so every head gets them at its own size:
//   jawOpen      the jaw turns down about a hinge in front of the ears; the lower lip and chin go with it, the neck
//                stretches a little, the upper lip lifts a touch
//   mouthFunnel  the lips pushed forward and gathered (an "oo"); mouthWide: drawn out to the sides (an "ee")
//   smile, frown the corners of the mouth up and back, or down
//   sneer        the upper lip and the sides of the nose up
//   browUp, browDown, browInnerUp   the brows (skin and brow hair) raised, lowered and drawn in, or lifted in the middle
//   squint       the lower lids pushed up
// The eyes are spheres without lids: there is no blink target.
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export const FACE_TARGETS = ['jawOpen', 'mouthFunnel', 'mouthWide', 'smile', 'frown', 'sneer', 'browUp', 'browDown', 'browInnerUp', 'squint'];
const CT = { 5120: Int8Array, 5121: Uint8Array, 5122: Int16Array, 5123: Uint16Array, 5125: Uint32Array, 5126: Float32Array };
const NC = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };

function readGlb(buf) {
  if (buf.readUInt32LE(0) !== 0x46546c67) throw new Error('not a glb');
  const jl = buf.readUInt32LE(12);
  const json = JSON.parse(buf.slice(20, 20 + jl).toString('utf8'));
  const bl = buf.readUInt32LE(20 + jl);
  const bin = Buffer.from(buf.slice(28 + jl, 28 + jl + bl));
  return { json, bin };
}
function writeGlb(json, bin) {
  const pad = (b, c) => { const p = (4 - (b.length % 4)) % 4; return p ? Buffer.concat([b, Buffer.alloc(p, c)]) : b; };
  json.buffers[0].byteLength = bin.length;
  const jb = pad(Buffer.from(JSON.stringify(json), 'utf8'), 0x20), bb = pad(bin, 0);
  const h = Buffer.alloc(12); h.writeUInt32LE(0x46546c67, 0); h.writeUInt32LE(2, 4); h.writeUInt32LE(12 + 8 + jb.length + 8 + bb.length, 8);
  const ch = (b, t) => { const c = Buffer.alloc(8); c.writeUInt32LE(b.length, 0); c.writeUInt32LE(t, 4); return Buffer.concat([c, b]); };
  return Buffer.concat([h, ch(jb, 0x4e4f534a), ch(bb, 0x004e4942)]);
}
function readAcc(g, i) {
  const a = g.json.accessors[i], v = g.json.bufferViews[a.bufferView], T = CT[a.componentType], n = NC[a.type];
  const stride = v.byteStride || T.BYTES_PER_ELEMENT * n, off = (v.byteOffset || 0) + (a.byteOffset || 0), out = new T(a.count * n);
  for (let k = 0; k < a.count; k++) for (let c = 0; c < n; c++) {
    const p = off + k * stride + c * T.BYTES_PER_ELEMENT;
    out[k * n + c] = T === Float32Array ? g.bin.readFloatLE(p) : T === Uint16Array ? g.bin.readUInt16LE(p) : T === Uint32Array ? g.bin.readUInt32LE(p) : T === Uint8Array ? g.bin.readUInt8(p) : g.bin.readInt16LE(p);
  }
  if (a.normalized && T === Uint8Array) return Float32Array.from(out, (x) => x / 255);
  if (a.normalized && T === Uint16Array) return Float32Array.from(out, (x) => x / 65535);
  return out;
}
function addAcc(g, arr, type, ct, target, minmax) {
  const data = Buffer.from(arr.buffer, arr.byteOffset, arr.byteLength);
  const pad = (4 - (g.bin.length % 4)) % 4;
  if (pad) g.bin = Buffer.concat([g.bin, Buffer.alloc(pad)]);
  g.json.bufferViews.push({ buffer: 0, byteOffset: g.bin.length, byteLength: data.length, ...(target ? { target } : {}) });
  g.bin = Buffer.concat([g.bin, data]);
  const n = NC[type], a = { bufferView: g.json.bufferViews.length - 1, componentType: ct, count: arr.length / n, type };
  if (minmax) {
    a.min = Array(n).fill(Infinity); a.max = Array(n).fill(-Infinity);
    for (let k = 0; k < arr.length; k++) { a.min[k % n] = Math.min(a.min[k % n], arr[k]); a.max[k % n] = Math.max(a.max[k % n], arr[k]); }
  }
  g.json.accessors.push(a);
  return g.json.accessors.length - 1;
}
const clamp = (x, a = 0, b = 1) => (x < a ? a : x > b ? b : x);
const smooth = (x) => { x = clamp(x); return x * x * (3 - 2 * x); };

/** the figure's face into a "Face" mesh of its own with FACE_TARGETS (a figure that has one already is left alone) */
export function addFaceMorphs(glb) {
  const g = readGlb(glb);
  const J = g.json;
  if (J.meshes.some((m) => m.name?.endsWith('_face'))) return null;
  const skin = J.skins[0];
  const jointName = skin.joints.map((n) => J.nodes[n].name);
  const head = jointName.indexOf('Head');
  if (head < 0) throw new Error('no Head joint');
  const ibm = readAcc(g, skin.inverseBindMatrices);
  const bindPos = (j) => [-ibm[j * 16 + 12], -ibm[j * 16 + 13], -ibm[j * 16 + 14]];
  const hj = bindPos(head);
  const bodyNode = J.nodes.findIndex((n) => n.mesh !== undefined && n.skin !== undefined);
  const body = J.meshes[J.nodes[bodyNode].mesh];
  const prims = body.primitives.map((p) => ({ p, cls: J.materials[p.material].name.split('_')[0], pos: readAcc(g, p.attributes.POSITION), j: readAcc(g, p.attributes.JOINTS_0), w: readAcc(g, p.attributes.WEIGHTS_0), idx: readAcc(g, p.indices) }));
  const headW = (P, v) => { let s = 0; for (let k = 0; k < 4; k++) if (P.j[v * 4 + k] === head) s += P.w[v * 4 + k]; return s; };
  // the landmarks: the eyes (their mesh), the nose tip (the frontmost head skin), the chin (the lowest face point in front)
  const skinP = prims.find((x) => x.cls === 'skin'), eyeP = prims.find((x) => x.cls === 'eye');
  if (!skinP || !eyeP) throw new Error('no skin or eye primitive');
  let ey = 0, en = 0, eyeX = 0;
  for (let v = 0; v < eyeP.pos.length / 3; v++) { ey += eyeP.pos[v * 3 + 1]; eyeX = Math.max(eyeX, Math.abs(eyeP.pos[v * 3] - hj[0])); en++; }
  ey /= en;
  const hv = [];
  for (let v = 0; v < skinP.pos.length / 3; v++) if (headW(skinP, v) > 0.5) hv.push(v);
  let nose = null, zf = -1e9;
  for (const v of hv) { const x = skinP.pos[v * 3] - hj[0], y = skinP.pos[v * 3 + 1], z = skinP.pos[v * 3 + 2]; if (Math.abs(x) < 0.02 && y < ey && z > zf) zf = z, nose = [x, y, z]; }
  let chin = 1e9;
  for (const v of hv) { const x = skinP.pos[v * 3] - hj[0], y = skinP.pos[v * 3 + 1], z = skinP.pos[v * 3 + 2]; if (Math.abs(x) < 0.03 && z > zf - 0.06 && y < chin) chin = y; }
  const H = ey - chin;
  if (!(H > 0.05 && H < 0.4) || !nose) throw new Error(`face landmarks not found (H ${H})`);
  const ym = nose[1] - 0.42 * (nose[1] - chin); // the mouth line
  const mw = 0.3 * H;                            // half the mouth's width
  const hinge = [0, ym + 0.32 * H, hj[2] - 0.05 * H]; // the jaw's hinge: below the ears, a little behind the head joint
  // the offset each target gives a point (bind pose, metres, x about the head's middle)
  function offsets(x, y, z) {
    const o = {};
    const front = smooth((z - (zf - 0.55 * H)) / (0.3 * H)); // the face's front, not the back of the head
    // jawOpen: below the mouth line, turned about the hinge; the neck under the chin follows partly
    {
      const below = smooth((ym + 0.03 * H - y) / (0.08 * H)), neck = smooth((y - (chin - 0.45 * H)) / (0.45 * H));
      const side = smooth((0.75 * H - Math.abs(x)) / (0.3 * H)), back = smooth((z - (hinge[2] - 0.05 * H)) / (0.25 * H));
      const w = below * neck * side * back, a = 0.24 * w;
      const dy = y - hinge[1], dz = z - hinge[2];
      const ny = hinge[1] + dy * Math.cos(a) + dz * Math.sin(a), nz = hinge[2] - dy * Math.sin(a) + dz * Math.cos(a);
      const lip = smooth((y - ym) / (0.02 * H)) * smooth((ym + 0.18 * H - y) / (0.08 * H)) * front * smooth((mw * 1.2 - Math.abs(x)) / (0.2 * H));
      o.jawOpen = [0, ny - y + lip * 0.035 * H, nz - z];
    }
    // the mouth's region: near the mouth line and in front
    const rm = Math.hypot(x / (mw * 1.35), (y - ym) / (0.28 * H)), mouth = smooth(1.15 - rm) * front;
    o.mouthFunnel = [-x * 0.3 * mouth, 0, 0.05 * H * mouth * smooth(1 - Math.abs(x) / mw)];
    o.mouthWide = [x * 0.16 * mouth, -0.01 * H * mouth, -0.015 * H * mouth];
    const corner = smooth(1 - Math.hypot((Math.abs(x) - mw) / (0.32 * H), (y - ym) / (0.24 * H))) * front;
    const sx = Math.sign(x) || 1;
    o.smile = [sx * 0.035 * H * corner, 0.07 * H * corner, -0.04 * H * corner];
    o.frown = [sx * 0.01 * H * corner, -0.06 * H * corner, -0.01 * H * corner];
    const sneer = smooth(1 - Math.hypot((Math.abs(x) - 0.16 * H) / (0.2 * H), (y - (ym + 0.3 * H)) / (0.16 * H))) * front;
    o.sneer = [0, 0.045 * H * sneer, 0.008 * H * sneer];
    // the brows: a band over the eyes, across the forehead's front
    const brow = smooth(1 - Math.abs(y - (ey + 0.28 * H)) / (0.2 * H)) * front * smooth((eyeX * 1.9 - Math.abs(x)) / (0.25 * H));
    const inner = smooth((eyeX * 0.9 - Math.abs(x)) / (0.35 * H));
    o.browUp = [0, 0.075 * H * brow, 0];
    o.browDown = [-sx * 0.02 * H * brow * inner, -0.06 * H * brow * (0.55 + 0.45 * inner), 0.01 * H * brow];
    o.browInnerUp = [-sx * 0.01 * H * brow * inner, 0.08 * H * brow * inner, 0];
    // the lower lids: a band just under the eyes
    const lid = smooth(1 - Math.abs(y - (ey - 0.08 * H)) / (0.09 * H)) * front * smooth(1 - Math.abs(Math.abs(x) - eyeX) / (0.4 * H));
    o.squint = [0, 0.03 * H * lid, 0.004 * H * lid];
    return o;
  }
  // the face's triangles: skin all on the head, the eyes, the brow hair in front of the forehead
  const faceTri = (P, t) => {
    const vs = [P.idx[t * 3], P.idx[t * 3 + 1], P.idx[t * 3 + 2]];
    if (P.cls === 'eye') return true;
    if (P.cls === 'skin') return vs.every((v) => headW(P, v) > 0.5);
    if (P.cls === 'hair') return vs.every((v) => headW(P, v) > 0.5 && Math.abs(P.pos[v * 3 + 1] - (ey + 0.28 * H)) < 0.22 * H && P.pos[v * 3 + 2] > zf - 0.45 * H);
    return false;
  };
  const faceMesh = { name: (body.name || 'figure').replace(/_body$/, '') + '_face', primitives: [], weights: FACE_TARGETS.map(() => 0), extras: { targetNames: FACE_TARGETS } };
  let moved = 0;
  for (const P of prims) {
    if (!['skin', 'eye', 'hair'].includes(P.cls)) continue;
    const nt = P.idx.length / 3, keep = [], take = [];
    for (let t = 0; t < nt; t++) (faceTri(P, t) ? take : keep).push(t);
    if (!take.length) continue;
    // the face's own vertices, compacted
    const map = new Map(), order = [];
    const fidx = new Uint32Array(take.length * 3);
    take.forEach((t, k) => { for (let c = 0; c < 3; c++) { const v = P.idx[t * 3 + c]; if (!map.has(v)) map.set(v, order.push(v) - 1); fidx[k * 3 + c] = map.get(v); } });
    const attrs = {};
    for (const [name, ai] of Object.entries(P.p.attributes)) {
      const src = readAcc(g, ai), a = J.accessors[ai], n = NC[a.type], T = CT[a.componentType];
      const dst = new T(order.length * n);
      order.forEach((v, k) => { for (let c = 0; c < n; c++) dst[k * n + c] = a.normalized ? 0 : src[v * n + c]; });
      if (a.normalized) { // re-read raw normalized values (weights stored as normalized ints)
        const raw = new T(order.length * n), view = J.bufferViews[a.bufferView], stride = view.byteStride || T.BYTES_PER_ELEMENT * n, off = (view.byteOffset || 0) + (a.byteOffset || 0);
        order.forEach((v, k) => { for (let c = 0; c < n; c++) raw[k * n + c] = T === Uint8Array ? g.bin.readUInt8(off + v * stride + c) : g.bin.readUInt16LE(off + v * stride + c * 2); });
        attrs[name] = addAcc(g, raw, a.type, a.componentType, 34962, name === 'POSITION');
        J.accessors[attrs[name]].normalized = true;
      } else attrs[name] = addAcc(g, dst, a.type, a.componentType, 34962, name === 'POSITION');
    }
    const targets = FACE_TARGETS.map(() => new Float32Array(order.length * 3));
    order.forEach((v, k) => {
      const x = P.pos[v * 3] - hj[0], y = P.pos[v * 3 + 1], z = P.pos[v * 3 + 2];
      const o = offsets(x, y, z);
      FACE_TARGETS.forEach((name, ti) => { const d = o[name]; if (P.cls === 'eye' && name !== 'jawOpen') return; if (P.cls === 'eye') return; if (P.cls === 'hair' && !name.startsWith('brow')) return; targets[ti].set(d, k * 3); });
    });
    const prim = { attributes: attrs, indices: addAcc(g, order.length > 65535 ? fidx : Uint16Array.from(fidx), 'SCALAR', order.length > 65535 ? 5125 : 5123, 34963), material: P.p.material,
      targets: targets.map((d) => ({ POSITION: addAcc(g, d, 'VEC3', 5126, 34962, true) })) };
    faceMesh.primitives.push(prim);
    // the Body keeps the rest
    const kidx = new Uint32Array(keep.length * 3);
    keep.forEach((t, k) => { for (let c = 0; c < 3; c++) kidx[k * 3 + c] = P.idx[t * 3 + c]; });
    const big = P.pos.length / 3 > 65535;
    P.p.indices = addAcc(g, big ? kidx : Uint16Array.from(kidx), 'SCALAR', big ? 5125 : 5123, 34963);
    P.keepCount = keep.length;
    moved += take.length;
  }
  body.primitives = body.primitives.filter((p, i) => prims[i].keepCount !== 0);
  J.meshes.push(faceMesh);
  J.nodes.push({ name: 'Face', mesh: J.meshes.length - 1, skin: J.nodes[bodyNode].skin });
  const scene = J.scenes[J.scene || 0];
  scene.nodes.push(J.nodes.length - 1);
  return { glb: writeGlb(J, g.bin), H, moved, landmarks: { eye: ey, nose, chin, mouth: ym } };
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  for (const f of process.argv.slice(2)) {
    const r = addFaceMorphs(readFileSync(f));
    if (!r) { console.log(`${f}: has a face already`); continue; }
    writeFileSync(f, r.glb);
    console.log(`${f}: face of ${r.moved} triangles, H ${r.H.toFixed(3)} m, mouth at ${r.landmarks.mouth.toFixed(3)}, chin ${r.landmarks.chin.toFixed(3)}`);
  }
}
