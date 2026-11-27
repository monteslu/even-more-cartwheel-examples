// robe-front.mjs: an open robe's front panels on bone chains of their own, so they swing as cloth.
//
//   node games/emberthrone/tools/chars/robe-front.mjs assets/models/tidecaller.glb [...]   (in place; run again: same)
//
// The robe (tools/chars/blender: dress.py robe 'open', cast.py ROBE) hangs from five chains round its sides and back;
// its two front panels, between the side chains and the opening, were skinned to the thighs from the hips to the hem
// (follow 1). A thigh raised on a stair, or swung through a turn at a run, then carried the whole panel with it as one
// rigid board: its hem stood out level with the hips, a stiff sail. Here each front panel gets a chain of its own
// (RobeFL, RobeFR: four bones and an end marker under the hips, like the others), down the middle of the panel's
// leg-weighted cloth, the root moved 5 cm out and 6 cm up as the others' are; every weight the panel had on its thigh
// goes onto that chain's bones by height (each vertex shared between the two bones round it), and the same for the hem
// trim along it. The game's springs (view.c SWING) link the seven chains into one open cloth (skirt OPEN) that the leg
// capsules push aside: a raised knee lifts the panel, which falls from it and swings back.
// make-characters.mjs applies it to each figure it writes with a robe open down the front.
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const NC = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };
const CS = { 5121: 1, 5123: 2, 5125: 4, 5126: 4 };
function readGlb(buf) {
  const jl = buf.readUInt32LE(12), json = JSON.parse(buf.subarray(20, 20 + jl).toString('utf8'));
  const bl = buf.readUInt32LE(20 + jl);
  return { json, bin: Buffer.from(buf.subarray(28 + jl, 28 + jl + bl)) };
}
function writeGlb(json, bin) {
  const pad = (b, c) => { const p = (4 - (b.length % 4)) % 4; return p ? Buffer.concat([b, Buffer.alloc(p, c)]) : b; };
  bin = pad(bin, 0);
  json.buffers[0].byteLength = bin.length;
  const jb = pad(Buffer.from(JSON.stringify(json), 'utf8'), 0x20);
  const h = Buffer.alloc(12); h.writeUInt32LE(0x46546c67, 0); h.writeUInt32LE(2, 4); h.writeUInt32LE(12 + 8 + jb.length + 8 + bin.length, 8);
  const ch = (b, t) => { const c = Buffer.alloc(8); c.writeUInt32LE(b.length, 0); c.writeUInt32LE(t, 4); return Buffer.concat([c, b]); };
  return Buffer.concat([h, ch(jb, 0x4e4f534a), ch(bin, 0x004e4942)]);
}
// an accessor as a view onto the binary chunk: get(i, c), set(i, c, v)
function view(json, bin, ai) {
  const a = json.accessors[ai], bv = json.bufferViews[a.bufferView], n = NC[a.type], s = CS[a.componentType];
  const st = bv.byteStride || n * s, o = (bv.byteOffset || 0) + (a.byteOffset || 0);
  const rd = { 5121: (p) => bin.readUInt8(p), 5123: (p) => bin.readUInt16LE(p), 5125: (p) => bin.readUInt32LE(p), 5126: (p) => bin.readFloatLE(p) }[a.componentType];
  const wr = { 5121: (p, v) => bin.writeUInt8(v, p), 5123: (p, v) => bin.writeUInt16LE(v, p), 5125: (p, v) => bin.writeUInt32LE(v, p), 5126: (p, v) => bin.writeFloatLE(v, p) }[a.componentType];
  return { count: a.count, n, ct: a.componentType, get: (i, c) => rd(o + i * st + c * s), set: (i, c, v) => wr(o + i * st + c * s, v) };
}

export const FRONT = [{ name: 'RobeFL', side: 'L', of: 'RobeL', sx: 1 }, { name: 'RobeFR', side: 'R', of: 'RobeR', sx: -1 }];
const ROOT_OUT = 0.05, ROOT_LIFT = 0.06;

export function addRobeFront(buf) {
  const { json, bin } = readGlb(buf);
  const nodes = json.nodes, skin = json.skins[0], names = skin.joints.map((k) => nodes[k].name);
  if (names.includes('RobeFL0')) return null; // (done)
  if (!names.includes('RobeL0') || !names.includes('RobeR0')) return null; // (no robe chains)
  const ibm = view(json, bin, skin.inverseBindMatrices);
  // a joint's place in the mesh's bind coordinates: the skeleton is translations only (make-characters.mjs), so the
  // inverse bind matrix is the translation to it, negated
  const at = (j) => [-ibm.get(j, 12), -ibm.get(j, 13), -ibm.get(j, 14)];
  const J = (n) => names.indexOf(n);
  for (let j = 0; j < names.length; j++)
    for (let c = 0; c < 12; c++) if (Math.abs(ibm.get(j, c) - (c % 5 === 0 ? 1 : 0)) > 1e-4) throw new Error(`robe-front: joint ${names[j]} is not a translation`);
  const hips = at(J('Hips'));
  const body = nodes.find((x) => x.name === 'Body');
  const prims = json.meshes[body.mesh].primitives.map((p) => ({ mat: json.materials[p.material].name, P: view(json, bin, p.attributes.POSITION), Jt: view(json, bin, p.attributes.JOINTS_0), W: view(json, bin, p.attributes.WEIGHTS_0) }));
  const cloth = prims.filter((p) => /robe|plate/.test(p.mat));
  // the front panels' cloth: below the waist, in front of the side chains, weighted to a thigh
  const panel = (side) => {
    const leg = J('UpperLeg' + side), out = [];
    for (const p of cloth)
      for (let v = 0; v < p.P.count; v++) {
        let wl = 0;
        for (let q = 0; q < 4; q++) if (p.Jt.get(v, q) === leg) wl += p.W.get(v, q);
        const x = p.P.get(v, 0), y = p.P.get(v, 1), z = p.P.get(v, 2), az = (Math.atan2(x - hips[0], z - hips[2]) * 180) / Math.PI;
        if (wl > 0 && y < hips[1] && Math.abs(az) < 95) out.push({ p, v, wl, x, y, z });
      }
    return out;
  };
  let added = 0;
  for (const f of FRONT) {
    const V = panel(f.side);
    if (!V.length) continue;
    // the levels: the side chain's joints' heights (its root, three joints, the end marker)
    const lv = [0, 1, 2, 3, '_end'].map((k) => at(J(f.of + k))[1]);
    const robeV = V.filter((u) => /robe/.test(u.p.mat));
    const joints = lv.map((y, k) => {
      // the panel's cloth at that height (the root: its top), weighted by how much of it the thigh carried
      const yy = k === 0 ? Math.max(...robeV.map((u) => u.y)) : y;
      let band = 0.02, m = [];
      while ((m = robeV.filter((u) => Math.abs(u.y - yy) < band)).length < 6) band *= 1.5;
      const s = m.reduce((a, u) => a + u.wl, 0), p = [0, 0, 0];
      for (const u of m) p[0] += (u.x * u.wl) / s, p[2] += (u.z * u.wl) / s;
      p[1] = y;
      if (k === 0) { // the seam: out from the hips' vertical and up, clear of the thigh's reach (the others' rule)
        const dx = p[0] - hips[0], dz = p[2] - hips[2], r = Math.hypot(dx, dz);
        p[0] += (dx / r) * ROOT_OUT, p[2] += (dz / r) * ROOT_OUT, p[1] = lv[0];
      }
      return p;
    });
    // mirrored exactly: the right chain is the left one's mirror (the robe is symmetric)
    if (f.side === 'R') {
      const L = FRONT[0]._joints;
      if (L) L.forEach((p, k) => (joints[k] = [2 * hips[0] - p[0], p[1], p[2]]));
    }
    f._joints = joints;
    // the nodes: under the hips, each a translation from its parent; the end marker too
    const hipsNode = skin.joints[J('Hips')];
    let parent = hipsNode, prevAt = hips;
    const newJ = [];
    for (let k = 0; k <= 4; k++) {
      const nm = k < 4 ? f.name + k : f.name + '_end';
      const ni = nodes.length;
      nodes.push({ name: nm, translation: [joints[k][0] - prevAt[0], joints[k][1] - prevAt[1], joints[k][2] - prevAt[2]] });
      (nodes[parent].children ||= []).push(ni);
      parent = ni, prevAt = joints[k];
      newJ.push(ni);
    }
    // the joints join the skin (the end marker too, as the side chains' do)
    const first = skin.joints.length;
    skin.joints.push(...newJ);
    names.push(...newJ.map((k) => nodes[k].name));
    f._first = first;
    added += newJ.length;
    // the panel's weights: the thigh's share onto the chain's bones by height (bone k from joint k to k + 1; a vertex
    // between two bones' middles shared between them; above the first bone's middle all on it, below the last's on it)
    const leg = J('UpperLeg' + f.side);
    for (const u of V) {
      let s = 0;
      for (let k = 0; k < 4; k++) if (u.y <= joints[k][1]) s = k + Math.min(1, (joints[k][1] - u.y) / (joints[k][1] - joints[k + 1][1]));
      if (u.y > joints[0][1]) s = 0;
      const m = Math.max(0.5, Math.min(3.5, s)) - 0.5, b0 = Math.floor(m), t = m - b0;
      const add = [[first + b0, u.wl * (1 - t)], [first + Math.min(3, b0 + 1), u.wl * t]];
      const w = [];
      for (let q = 0; q < 4; q++) { const ji = u.p.Jt.get(u.v, q), wq = u.p.W.get(u.v, q); if (wq > 0 && ji !== leg) w.push([ji, wq]); }
      for (const [ji, wq] of add) { const e = w.find((x) => x[0] === ji); if (e) e[1] += wq; else if (wq > 0) w.push([ji, wq]); }
      w.sort((a, b) => b[1] - a[1]);
      const keep = w.slice(0, 4), sum = keep.reduce((a, x) => a + x[1], 0);
      for (let q = 0; q < 4; q++) {
        u.p.Jt.set(u.v, q, q < keep.length ? keep[q][0] : 0);
        u.p.W.set(u.v, q, q < keep.length ? keep[q][1] / sum : 0);
      }
    }
  }
  if (!added) return null;
  for (const p of prims) if (p.Jt.ct === 5121 && skin.joints.length > 255) throw new Error('robe-front: over 255 joints in a byte index');
  // the inverse bind matrices, longer by the new joints: a new buffer view at the end of the binary
  const n = skin.joints.length, mats = Buffer.alloc(n * 64);
  for (let j = 0; j < n; j++) {
    const p = j < ibm.count ? at(j) : (() => { const f = FRONT.find((x) => j >= x._first && j < x._first + 5); return f._joints[j - f._first]; })();
    const m = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p[0], -p[1], -p[2], 1];
    m.forEach((v, c) => mats.writeFloatLE(v, j * 64 + c * 4));
  }
  const off = Math.ceil(bin.length / 4) * 4, out = Buffer.concat([bin, Buffer.alloc(off - bin.length), mats]);
  json.bufferViews.push({ buffer: 0, byteOffset: off, byteLength: mats.length });
  json.accessors.push({ bufferView: json.bufferViews.length - 1, componentType: 5126, count: n, type: 'MAT4' });
  skin.inverseBindMatrices = json.accessors.length - 1;
  return { glb: writeGlb(json, out), joints: n, added };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  for (const f of process.argv.slice(2)) {
    const r = addRobeFront(readFileSync(f));
    if (!r) { console.log(`--   ${f}: no open robe, or done`); continue; }
    writeFileSync(f, r.glb);
    console.log(`ok   ${f}: ${r.added} joints added (${r.joints})`);
  }
}
