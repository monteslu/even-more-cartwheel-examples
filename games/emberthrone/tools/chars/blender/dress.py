# Generated pieces for build_chars.py: plate (cuirass, pauldrons), helmets and crowns, horns and thorns, robes, open coats,
# capes and cloth masks, made around the figure (its joints and the shape of its body) and skinned to the game's bones
# by name (vertex groups named as the game's bones). Every piece carries its colour, roughness and metalness as
# attributes; one atlas (UV map "Gen") is baked from them with ambient occlusion, a top light, worn bright edges and a
# hammered or woven grain: the hand-painted look of the CC0 outfits (bake_atlas). Materials: GEN_metal (plate, trim)
# and GEN_cloth (robes, capes, masks), both reading the same texture set.
import bpy, bmesh, math, os
import numpy as np
from mathutils import Vector, Matrix

V = Vector
SKIN_W = ('Head',)


def sstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# ── measuring the figure ──────────────────────────────────────────────────────────────────────
def body_points(o, bones, thr=0.8):
    """world points of the vertices weighted to these source or game bones (at least thr in total)"""
    import build_chars as bc
    W = bc.game_weights(o)
    col = {b: i for i, b in enumerate(bc.GAME_BONES)}
    w = sum(W[:, col[b]] for b in bones)
    n = len(o.data.vertices)
    P = np.empty(n * 3, np.float32)
    o.data.vertices.foreach_get('co', P)
    P = P.reshape(n, 3)
    return P[w >= thr]


_PROFILE = {}


def torso_profile(o, z, bones=('Hips', 'Spine', 'Chest'), thr=0.85):
    """the body's half width and its front and back at height z (from the vertices of these bones near z)"""
    key = (o.name, bones, thr)
    if key not in _PROFILE:
        _PROFILE[key] = body_points(o, bones, thr)
    P = _PROFILE[key]
    for win in (0.02, 0.05, 0.1, 0.2):
        s = P[np.abs(P[:, 2] - z) < win]
        if len(s) >= 6:
            return float(np.abs(s[:, 0]).max()), float(-s[:, 1].min()), float(s[:, 1].max())  # x, front (-Y), back (+Y)
    return 0.16, 0.12, 0.12


def head_box(o):
    """the head's box from its skin alone (hair and hoods are weighted to the head too)"""
    import build_chars as bc
    me = o.data
    skin = {i for i, m in enumerate(me.materials) if m.name.startswith('MI_Superhero')}
    vs = sorted({v for p in me.polygons if p.material_index in skin for v in p.vertices})
    W = bc.game_weights(o)
    hi_ = bc.GAME_BONES.index('Head')
    P = np.array([me.vertices[v].co[:] for v in vs if W[v, hi_] >= 0.9])
    _PROFILE[(o.name, 'skinhead')] = np.array([me.vertices[v].co[:] for v in vs if W[v, hi_] + W[v, hi_ - 1] >= 0.5])
    lo, hi = P.min(0), P.max(0)
    return V(lo), V(hi)


# ── building ──────────────────────────────────────────────────────────────────────────────────
class Piece:
    """a mesh being built: verts, faces, per-vertex bone weights, per-face colour/rough/metal/trim"""

    def __init__(self, name, color, rough=0.45, metal=1.0, mat='GEN_metal', grain='hammer'):
        self.name, self.color, self.rough, self.metal, self.mat, self.grain = name, color, rough, metal, mat, grain
        self.v, self.f, self.w = [], [], []

    def vert(self, p, w):
        self.v.append(V(p))
        self.w.append(w)
        return len(self.v) - 1

    def face(self, *ix):
        self.f.append(ix)


def loft(pc, rings, closed=True, cap_top=False, cap_bot=False):
    """rings: lists of (point, weights) of equal length; quads between consecutive rings"""
    ids = [[pc.vert(p, w) for p, w in r] for r in rings]
    n = len(rings[0])
    for a, b in zip(ids, ids[1:]):
        for i in range(n if closed else n - 1):
            j = (i + 1) % n
            pc.face(a[i], a[j], b[j], b[i])
    if cap_top:
        pc.face(*reversed(ids[-1]))
    if cap_bot:
        pc.face(*ids[0])
    return ids


def to_object(pc, o, thickness=0.0, subdiv=1, smooth=True):
    me = bpy.data.meshes.new(pc.name)
    me.from_pydata([tuple(v) for v in pc.v], [], [tuple(f) for f in pc.f])
    me.update()
    ob = bpy.data.objects.new(pc.name, me)
    bpy.context.scene.collection.objects.link(ob)
    groups = {}
    for i, w in enumerate(pc.w):
        tot = sum(w.values()) or 1
        for b, x in w.items():
            if x <= 0:
                continue
            g = groups.get(b) or ob.vertex_groups.new(name=b)
            groups[b] = g
            g.add([i], x / tot, 'REPLACE')
    for p in me.polygons:
        p.use_smooth = smooth
    if thickness:
        m = ob.modifiers.new('solid', 'SOLIDIFY')
        m.thickness = thickness
        m.offset = 1
        m.use_rim = True
    if subdiv:
        m = ob.modifiers.new('sub', 'SUBSURF')
        m.levels = subdiv
        m.render_levels = subdiv
    bpy.context.view_layer.objects.active = ob
    for m in list(ob.modifiers):
        with bpy.context.temp_override(object=ob, active_object=ob):
            bpy.ops.object.modifier_apply(modifier=m.name)
    # the piece's surface values, per corner (the atlas bake reads them)
    me = ob.data
    # stylised plate: not a mirror (the bright sky would wash it white): a little rough, three quarters metal
    for nm, val in (('gen_col', (*pc.color, 1)), ('gen_prm', (max(pc.rough, 0.42), pc.metal * 0.75, {'hammer': 0.0, 'weave': 0.5, 'smooth': 1.0}[pc.grain], 1))):
        a = me.color_attributes.new(nm, 'FLOAT_COLOR', 'CORNER')
        a.data.foreach_set('color', list(val) * len(me.loops))
    mat = bpy.data.materials.get(pc.mat) or gen_material(pc.mat)
    me.materials.append(mat)
    return ob


def gen_material(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = next(x for x in nt.nodes if x.type == 'BSDF_PRINCIPLED')
    a = nt.nodes.new('ShaderNodeAttribute')
    a.attribute_name = 'gen_col'
    nt.links.new(a.outputs['Color'], bsdf.inputs['Base Color'])
    return m


# ── the pieces ────────────────────────────────────────────────────────────────────────────────
def ring_pts(c, rx, rf, rb, n, a0=0.0, a1=2 * math.pi, closed=True):
    """an egg ring at centre c: half width rx, front (-Y) depth rf, back depth rb"""
    pts = []
    m = n if closed else n + 1
    for i in range(m):
        a = a0 + (a1 - a0) * i / n
        x, y = math.cos(a), math.sin(a)  # a = 0: +X (left), a = -pi/2: front (-Y)
        pts.append(c + V((x * rx, y * (rb if y > 0 else rf), 0)))
    return pts


def trunk_weights(z, J):
    zs, zc = J['Spine'].z, J['Chest'].z
    if z >= zc:
        return {'Chest': 1.0}
    if z >= zs:
        t = (z - zs) / (zc - zs)
        return {'Chest': sstep(0.3, 1.0, t), 'Spine': 1 - sstep(0.3, 1.0, t)}
    t = sstep(J['Hips'].z, zs, z)
    return {'Spine': t, 'Hips': 1 - t}


def polar_profile(o, z, n, bones=('Hips', 'Spine', 'Chest'), thr=0.85, cy=None):
    """the body's outline at height z as n radii round (0, cy): per sector the 90th percentile of the body's points near
    z (so a buckle does not set it), gaps filled from the neighbours, smoothed round the ring"""
    key = (o.name, bones, thr)
    if key not in _PROFILE:
        _PROFILE[key] = body_points(o, bones, thr)
    P = _PROFILE[key]
    for win in (0.015, 0.03, 0.06, 0.12):
        s = P[np.abs(P[:, 2] - z) < win]
        if len(s) >= 3 * n:
            break
    if cy is None:
        cy = float((s[:, 1].min() + s[:, 1].max()) / 2) if len(s) else 0.0
    a = np.arctan2(s[:, 1] - cy, s[:, 0]) % (2 * math.pi)
    r = np.hypot(s[:, 0], s[:, 1] - cy)
    sec = (a / (2 * math.pi) * n).astype(int) % n
    R = np.full(n, np.nan)
    for i in range(n):
        x = r[sec == i]
        if len(x):
            R[i] = np.percentile(x, 90)
    ok = ~np.isnan(R)
    if not ok.any():
        R[:] = 0.14
    else:
        idx = np.arange(n)
        R = np.interp(idx, np.concatenate([idx[ok] - n, idx[ok], idx[ok] + n]), np.concatenate([R[ok]] * 3))
    for _ in range(3):
        R = (np.roll(R, 1) + 2 * R + np.roll(R, -1)) / 4
    return R, cy


def cuirass(o, J, w, pieces):
    """a breastplate and backplate as one shell from the belt to under the arms, following the chest's own outline
    (measured ring by ring), a ridge down the front, rolled rims"""
    pc = Piece('cuirass', w['color'], w.get('rough', 0.48), 1.0)
    z0 = J['Spine'].z + w.get('above', 0.05)
    z1 = J['UpperArmL'].z + w.get('over_arm', 0.0)
    lift = w.get('pad', 0.014)
    N, M = 40, 12
    TB = ('Spine', 'Chest')
    key = (o.name, TB, 0.6)
    if key not in _PROFILE:
        _PROFILE[key] = body_points(o, TB, 0.6)
    P = _PROFILE[key]
    ext = []
    for k in range(M + 1):
        z = z0 + (z1 - z0) * k / M
        for win in (0.02, 0.04, 0.08):
            s = P[np.abs(P[:, 2] - z) < win]
            if len(s) > 20:
                break
        ext.append([np.percentile(np.abs(s[:, 0]), 96), -np.percentile(s[:, 1], 4), np.percentile(s[:, 1], 96)])
    ext = np.array(ext)
    for _ in range(3):
        ext[1:-1] = (ext[:-2] + 2 * ext[1:-1] + ext[2:]) / 4
    cy = float((ext[:, 2] - ext[:, 1]).mean() / 2)
    top = ext[int(M * 0.8)]
    rings = []
    ridge = w.get('ridge', 0.012)
    e = w.get('square', 2.4)
    for k in range(M + 1):
        t = k / M
        z = z0 + (z1 - z0) * t
        rx, rf, rb = np.minimum(ext[k], top * (0.86 + 0.14 * t))
        rf = (rf + cy) * (1 + 0.06 * math.sin(math.pi * t)) + lift  # the front about the centre, the chest standing out
        rb = (rb - cy) + lift + w.get("back_pad", 0.022)
        rx = rx + lift
        r = []
        for i in range(N):
            a = 2 * math.pi * i / N
            c, sn = math.cos(a), math.sin(a)
            x = math.copysign(abs(c) ** (2 / e), c) * rx
            y = math.copysign(abs(sn) ** (2 / e), sn) * (rb if sn > 0 else rf)
            front = math.exp(-((((a + math.pi / 2) + math.pi) % (2 * math.pi) - math.pi) / 0.3) ** 2)
            y -= ridge * front * math.sin(math.pi * min(1, t * 1.05))
            # the neckline rises front and back and dips at the armholes
            zz = z + t ** 2 * (0.03 * abs(sn) - 0.05 * abs(c))
            r.append((V((x, cy + y, zz)), trunk_weights(zz, J)))
        rings.append(r)
    ids = loft(pc, rings)
    pieces.append(to_object(pc, o, thickness=0.01, subdiv=0))
    # rolled rims at the top and the bottom: trim
    for k, nm in ((M, 'cuirass_top'), (0, 'cuirass_hem')):
        rim(o, pieces, nm, [p for p, _ in rings[k]], cy, w.get('trim', w['color']), trunk_weights(rings[k][0][0].z, J))


def rim(o, pieces, nm, ring, cy, color, wt, r=0.009):
    """a rolled tube along a closed ring of points (round the axis through (0, cy))"""
    pc = Piece(nm, color, 0.32, 1.0, grain='smooth')
    rings = []
    for k in range(6):
        a = 2 * math.pi * k / 6
        rr = []
        for p in ring:
            out = V((p.x, p.y - cy, 0)).normalized()
            rr.append((p + out * (r * math.cos(a) + 0.004) + V((0, 0, r * math.sin(a))), wt))
        rings.append(rr)
    rings.append(rings[0])
    loft(pc, rings)
    pieces.append(to_object(pc, o, subdiv=0))


def tube(o, pieces, nm, z, color, lift, J, r=0.011, n=36):
    pc = Piece(nm, color, 0.3, 1.0, grain='smooth')
    rx, rf, rb = torso_profile(o, z)
    rings = []
    for k in range(6):
        a = 2 * math.pi * k / 6
        dz, dr = r * math.sin(a), r * math.cos(a)
        rings.append([(p, trunk_weights(z, J)) for p in ring_pts(V((0, 0, z + dz)), rx + lift + dr, rf + lift + dr, rb + lift + dr, n)])
    rings.append(rings[0])
    loft(pc, rings)
    pieces.append(to_object(pc, o, subdiv=0))


def dome(pc, c, rx, ry, rz, n, m, z_cut=-0.2, weights=None, front_cut=None):
    """the upper part of an ellipsoid (z from z_cut to the top, in units of rz); front_cut(a, t) can raise the edge"""
    rings = []
    for j in range(m + 1):
        t = z_cut + (1 - z_cut) * j / m  # sin(latitude)
        lat = math.asin(max(-1, min(1, t)))
        r = []
        for i in range(n):
            a = 2 * math.pi * i / n
            p = c + V((math.cos(a) * math.cos(lat) * rx, math.sin(a) * math.cos(lat) * ry, math.sin(lat) * rz))
            r.append((p, weights or {'Head': 1.0}))
        rings.append(r)
    rings[-1] = [(c + V((0, 0, rz)), weights or {'Head': 1.0})] * n
    return loft(pc, rings)


def pauldron(o, J, w, pieces, side):
    """overlapping plate lames over the shoulder, each a shallow cap tipped out over the arm, the top one largest"""
    sx = 1 if side == 'L' else -1
    s = w.get('size', 1.0)
    sh = J['UpperArm' + side]
    c = sh + V((sx * 0.02 * s, 0.005, 0.045 * s))
    for k in range(w.get('layers', 3)):
        pc = Piece('pauldron%s%d' % (side, k), w['color'] if k == 0 else w.get('trim', w['color']) if k == w.get('layers', 3) - 1 else w['color'], w.get('rough', 0.48), 1.0)
        kk = 1 - 0.1 * k
        cc = c + V((sx * 0.03 * k * s, 0, -0.05 * k * s))
        wt = {'UpperArm' + side: 0.6, 'Chest': 0.4} if k == 0 else {'UpperArm' + side: 0.85, 'Chest': 0.15}
        dome(pc, cc, 0.12 * s * kk, 0.125 * s * kk, 0.075 * s * kk, 28, 7, z_cut=0.05, weights=wt)
        ob = to_object(pc, o, thickness=0.009, subdiv=0)
        R = Matrix.Translation(cc) @ Matrix.Rotation(-sx * math.radians(30 + 10 * k), 4, 'Y') @ Matrix.Translation(-cc)
        ob.data.transform(R)
        pieces.append(ob)
    for k in range(w.get('spikes', 0)):
        a = (k - (w['spikes'] - 1) / 2) * 0.5
        base = c + V((sx * (0.05 + 0.02 * abs(a)) * s, a * 0.07 * s, 0.07 * s))
        horn(o, pieces, 'spikeP%s%d' % (side, k), base, V((sx * 0.5, a * 0.3, 1)).normalized(), w.get('spike_len', 0.11) * s,
             0.024 * s, w.get('spike_color', w.get('trim', w['color'])), {'UpperArm' + side: 0.7, 'Chest': 0.3}, curl=V((sx * 0.4, 0, 0)))


def horn(o, pieces, nm, base, d, length, r0, color, wt, curl=V((0, 0, 0)), n=10, seg=7, rough=0.5, metal=0.0, grain='smooth'):
    """a tapering curved cone: thorns, horns, spikes"""
    pc = Piece(nm, color, rough, metal, grain=grain, mat='GEN_metal')
    d = d.normalized()
    up = V((0, 0, 1)) if abs(d.z) < 0.9 else V((1, 0, 0))
    u = d.cross(up).normalized()
    v = d.cross(u).normalized()
    rings = []
    p = base.copy()
    dd = d.copy()
    for k in range(seg + 1):
        t = k / seg
        r = r0 * (1 - t) ** 0.9 + 0.0015
        u = dd.cross(up).normalized()
        v = dd.cross(u).normalized()
        rings.append([(p + (u * math.cos(2 * math.pi * i / n) + v * math.sin(2 * math.pi * i / n)) * r, wt) for i in range(n)])
        dd = (dd + curl * (1.0 / seg)).normalized()
        p = p + dd * (length / seg)
    loft(pc, rings, cap_bot=True)
    pieces.append(to_object(pc, o, subdiv=0))


def helm(o, J, w, pieces):
    lo, hi = head_box(o)
    c = (lo + hi) / 2
    hr = (hi - lo) / 2
    s = w.get('size', 1.12)
    cc = V((c.x, c.y + 0.004, c.z + hr.z * 0.05))
    pc = Piece('helm', w['color'], w.get('rough', 0.36), 1.0)
    # the dome: down to the brow at the front, the nape behind
    if w.get('dome', True):
        dome(pc, cc, hr.x * s, hr.y * s * 1.02, hr.z * s * 1.02, 32, 12, z_cut=-0.05)
        pieces.append(to_object(pc, o, thickness=0.012, subdiv=0))
    # a brow band
    pc = Piece('helm_band', w.get('trim', w['color']), 0.3, 1.0, grain='smooth')
    rings = []
    for k in range(5):
        a = 2 * math.pi * k / 5
        z = cc.z - 0.05 * hr.z * s + 0.012 * math.sin(a)
        rr = 0.012 * math.cos(a) + 0.006
        rings.append([(p, {'Head': 1.0}) for p in ring_pts(V((cc.x, cc.y, z)), hr.x * s + rr, hr.y * s * 1.02 + rr, hr.y * s * 1.02 + rr, 28)])
    rings.append(rings[0])
    loft(pc, rings)
    pieces.append(to_object(pc, o, subdiv=1))
    top = V((cc.x, cc.y, cc.z + hr.z * s * 1.02))
    kind = w.get('style', 'dome')
    if kind == 'crest':  # a ridge of plates over the crown
        for k in range(5):
            t = (k - 2) / 2.5
            b = cc + V((0, t * hr.y * s * 0.8, hr.z * s * math.cos(t * 0.9) * 0.98))
            horn(o, pieces, 'crest%d' % k, b, V((0, t * 0.6, 1)), 0.07 * (1 - abs(t) * 0.4), 0.02, w.get('trim', w['color']), {'Head': 1.0}, metal=1.0, rough=0.35, n=6, seg=3)
    if kind == 'horns':  # two horns out and up, thorns along the crown
        for sx in (1, -1):
            b = cc + V((sx * hr.x * s * 0.8, 0.0, hr.z * s * 0.45))
            horn(o, pieces, 'horn%d' % sx, b, V((sx, 0.15, 0.35)), w.get('horn_len', 0.32), 0.045, w.get('horn_color', (0.75, 0.68, 0.5)), {'Head': 1.0},
                 curl=V((0, 0.2, 1.4)), n=12, seg=9, rough=0.55)
        for k in range(w.get('thorns', 7)):
            a = math.pi * (0.15 + 0.7 * k / max(1, w.get('thorns', 7) - 1))
            b = cc + V((math.cos(a) * hr.x * s * 0.55, math.sin(a) * hr.y * s * 0.55 * 0 + (k % 2) * 0.02, hr.z * s * 0.88))
            horn(o, pieces, 'thorn%d' % k, b, V((math.cos(a) * 0.4, (k % 2 - 0.5) * 0.4, 1)), 0.06 + 0.02 * (k % 2), 0.014, w.get('thorn_color', (0.3, 0.5, 0.15)), {'Head': 1.0}, n=6, seg=4)
    if kind == 'crown':  # a crown of points round the helm
        n = w.get('points', 9)
        ring_z = cc.z + hr.z * s * 0.42
        for k in range(n):
            a = 2 * math.pi * k / n
            r = 0.94
            b = V((cc.x + math.cos(a) * hr.x * s * r * 0.92, cc.y + math.sin(a) * hr.y * s * r * 0.95, ring_z))
            ln = (0.11 if k % 2 == 0 else 0.075) * w.get('crown_len', 1.0)
            horn(o, pieces, 'crown%d' % k, b, V((math.cos(a) * 0.25, math.sin(a) * 0.25, 1)), ln, 0.024, w.get('trim', w['color']), {'Head': 1.0}, metal=1.0, rough=0.28, n=6, seg=3)
        pc = Piece('crown_band', w.get('trim', w['color']), 0.28, 1.0, grain='smooth')
        rings = []
        for k in range(5):
            a = 2 * math.pi * k / 5
            rings.append([(p, {'Head': 1.0}) for p in ring_pts(V((cc.x, cc.y, ring_z + 0.016 * math.sin(a))), hr.x * s * 0.95 + 0.012 * math.cos(a) + 0.01, hr.y * s * 0.97 + 0.012 * math.cos(a) + 0.01, hr.y * s * 0.97 + 0.012 * math.cos(a) + 0.01, 28)])
        rings.append(rings[0])
        loft(pc, rings)
        pieces.append(to_object(pc, o, subdiv=1))
    if kind == 'fins':  # the tidecaller's shell crown: fins fanning up behind the head
        for k in range(w.get('fins', 5)):
            t = (k - (w.get('fins', 5) - 1) / 2) / 2.0
            b = cc + V((t * hr.x * s * 0.8, hr.y * s * 0.5, hr.z * s * 0.6))
            horn(o, pieces, 'fin%d' % k, b, V((t * 0.8, 0.5, 1)), 0.2 - 0.05 * abs(t), 0.035, w.get('trim', w['color']), {'Head': 1.0}, curl=V((0, 0.8, 0)), metal=1.0, rough=0.3, n=6, seg=6)


def skirt(o, J, w, pieces, kind):
    """a robe (to the floor, closed), an open coat (to the calf, open at the front) or a cape (behind, from the
    shoulders): rings lofted round the figure, each vertex weighted to the hips, the thighs (by side) and the trunk"""
    color = w['color']
    pc = Piece(kind, color, w.get('rough', 0.85), 0.0, mat='GEN_cloth', grain='weave')
    if kind == 'cape':
        z_top, z_bot = J['Neck'].z - 0.03, w.get('hem', 0.3)
    elif kind == 'coat':  # from under the arms: a long sleeveless coat over the outfit
        z_top, z_bot = J['UpperArmL'].z - w.get('under_arm', 0.06), w.get('hem', 0.04)
    else:
        z_top, z_bot = J['Hips'].z + w.get('top', 0.06), w.get('hem', 0.04)
    N = 32
    if kind == 'robe':
        a0, a1, closed = 0, 2 * math.pi, True
    elif kind == 'coat':
        op = w.get('open', 0.5)
        a0, a1, closed = -math.pi / 2 + op, 3 * math.pi / 2 - op, False
    else:
        a0, a1, closed = 0.15, math.pi - 0.15, False
    rings = []
    zh = J['Hips'].z
    M = 16 if kind == 'coat' else 12
    for k in range(M + 1):
        t = k / M
        z = z_top + (z_bot - z_top) * t
        tl = max(0.0, min(1.0, (zh - z) / max(0.01, zh - z_bot)))  # down the legs, 0 above the hips
        if kind == 'cape':
            rx0, rf0, rb0 = torso_profile(o, min(z_top, J['Chest'].z + 0.05))
            rx = rx0 + 0.03 + 0.1 * t * w.get('flare', 1)
            rb = rb0 + 0.04 + 0.12 * t
            rf = rb
        else:
            rx0, rf0, rb0 = torso_profile(o, max(z, zh - 0.12), ('Hips', 'Spine', 'Chest', 'UpperLegL', 'UpperLegR'), 0.6)
            fl = w.get('flare', 0.18) * tl ** 1.2
            rx, rf, rb = rx0 + 0.035 + fl, rf0 + 0.04 + fl * 0.8, rb0 + 0.04 + fl
        r = []
        for p in ring_pts(V((0, 0, z)), rx, rf, rb, N, a0, a1, closed):
            if kind == 'cape':
                if z > J['Chest'].z - 0.05:
                    wt = {'Chest': 1.0}
                else:
                    tt = sstep(J['Chest'].z - 0.05, zh, z)
                    wt = {'Chest': 1 - tt, 'Spine': tt * 0.6, 'Hips': tt * 0.4}
                    if z < zh:
                        u = sstep(zh, zh - 0.5, z)
                        side = sstep(-0.12, 0.12, p.x)
                        wt = {'Hips': 1 - 0.45 * u, 'UpperLegL': 0.45 * u * side, 'UpperLegR': 0.45 * u * (1 - side)}
            else:
                u = sstep(zh + 0.02, zh - 0.25, z)  # 0 at the waist, 1 down the thighs
                side = sstep(-0.1, 0.1, p.x)
                follow = w.get('follow', 0.75)
                legw = u * follow
                wt = {'Hips': 1 - legw, 'UpperLegL': legw * side, 'UpperLegR': legw * (1 - side)}
                if z > zh + 0.02:
                    wt = trunk_weights(z, J) if z > J['Spine'].z else {'Hips': 1.0}
            r.append((p, wt))
        rings.append(r)
    loft(pc, rings, closed=closed)
    pieces.append(to_object(pc, o, thickness=0.008, subdiv=0))
    if w.get('trim'):
        # a band along the hem
        pc = Piece(kind + '_hem', w['trim'], 0.5, w.get('trim_metal', 0.0), mat='GEN_cloth' if not w.get('trim_metal') else 'GEN_metal', grain='smooth')
        hem = rings[-1]
        up = rings[-2]
        band = [[(p.lerp(q, 0.0) + (p - V((0, 0, p.z))).normalized() * 0.006, wt) for (p, wt), (q, _) in zip(hem, up)],
                [(p.lerp(q, 0.35) + (p - V((0, 0, p.z))).normalized() * 0.006, wt) for (p, wt), (q, _) in zip(hem, up)]]
        loft(pc, band, closed=closed)
        pieces.append(to_object(pc, o, thickness=0.004, subdiv=1))


def mask(o, J, w, pieces):
    """a cloth over the lower face: the head's own skin below the nose, copied, smoothed (the lips and nostrils gone
    under it), lifted off the face and given a thickness; it hangs a little lower in front of the throat"""
    lo, hi = head_box(o)
    c = (lo + hi) / 2
    hr = (hi - lo) / 2
    z_nose = c.z - hr.z * w.get('top', 0.32)
    me = o.data
    skin = {i for i, m in enumerate(me.materials) if m.name.startswith('MI_Superhero')}
    bm = bmesh.new()
    bm.from_mesh(me)
    keep = set()
    for f in bm.faces:
        if f.material_index not in skin:
            continue
        cc = f.calc_center_median()
        # the outer surface only: the mouth's inside faces back or up and down
        if cc.z < z_nose and cc.y < c.y + hr.y * 0.25 and cc.z > lo.z - 0.06 and (f.normal.y < -0.15 or abs(f.normal.x) > 0.55):
            keep.add(f)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f not in keep], context='FACES')
    bmesh.ops.holes_fill(bm, edges=bm.edges[:], sides=48)
    bm.to_mesh(me2 := bpy.data.meshes.new('mask'))
    bm.free()
    ob = bpy.data.objects.new('mask', me2)
    bpy.context.scene.collection.objects.link(ob)
    for g in o.vertex_groups:
        ob.vertex_groups.new(name=g.name)
    for p in me2.polygons:
        p.use_smooth = True
    me2.materials.clear()
    bpy.context.view_layer.objects.active = ob
    for typ, kw in (('SMOOTH', dict(factor=1.0, iterations=14)), ('DISPLACE', dict(strength=w.get('lift', 0.011), mid_level=0.0)),
                    ('SOLIDIFY', dict(thickness=0.004, offset=1))):
        m = ob.modifiers.new(typ.lower(), typ)
        for k, v in kw.items():
            setattr(m, k, v)
    for m in list(ob.modifiers):
        with bpy.context.temp_override(object=ob, active_object=ob):
            bpy.ops.object.modifier_apply(modifier=m.name)
    pc = Piece('mask', w['color'], 0.9, 0.0, mat='GEN_cloth', grain='weave')
    for nm, val in (('gen_col', (*pc.color, 1)), ('gen_prm', (pc.rough, 0.0, 0.5, 1))):
        a = me2.color_attributes.new(nm, 'FLOAT_COLOR', 'CORNER')
        a.data.foreach_set('color', list(val) * len(me2.loops))
    me2.materials.append(bpy.data.materials.get('GEN_cloth') or gen_material('GEN_cloth'))
    pieces.append(ob)


def bracer(o, J, w, pieces, side):
    """a plate over the forearm"""
    pc = Piece('bracer' + side, w['color'], w.get('rough', 0.36), 1.0)
    a, b = J['LowerArm' + side], J['Hand' + side]
    P = body_points(o, ('LowerArm' + side,), 0.8)
    rings = []
    for k in range(5):
        t = 0.25 + 0.65 * k / 4
        c = a.lerp(b, t)
        s = P[np.abs(P[:, 2] - c.z) < 0.015]
        r = float(np.hypot(s[:, 0] - c.x, s[:, 1] - c.y).max()) if len(s) else 0.045
        r += 0.012 + 0.006 * (k == 4)
        rings.append([(c + V((math.cos(2 * math.pi * i / 16) * r, math.sin(2 * math.pi * i / 16) * r, 0)), {'LowerArm' + side: 1.0}) for i in range(16)])
    loft(pc, rings)
    pieces.append(to_object(pc, o, thickness=0.006, subdiv=0))


def mirror_part(o, name_part):
    """the outfit's one-sided piece (a pauldron) copied to the other side (its vertex groups swapped)"""
    return name_part


def dress(o, J, sp):
    pieces = []
    for w in sp.get('wear', []):
        k = w['kind']
        if k == 'cuirass':
            cuirass(o, J, w, pieces)
        elif k == 'pauldrons':
            for s in w.get('sides', 'LR'):
                pauldron(o, J, w, pieces, s)
        elif k == 'helm':
            helm(o, J, w, pieces)
        elif k in ('robe', 'coat', 'cape'):
            skirt(o, J, w, pieces, k)
        elif k == 'mask':
            mask(o, J, w, pieces)
        elif k == 'bracers':
            for s in 'LR':
                bracer(o, J, w, pieces, s)
        else:
            raise RuntimeError('unknown piece ' + k)
    if not pieces:
        return
    # the pieces join the figure (their groups merge by name with the game bones)
    bpy.ops.object.select_all(action='DESELECT')
    for p in pieces:
        p.select_set(True)
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.join()


# ── the atlas of the generated pieces ─────────────────────────────────────────────────────────
def gen_uv(o):
    """UV map 'Gen': the generated faces unwrapped (smart projection) and packed; the rest collapsed to a corner"""
    me = o.data
    if 'Gen' not in me.uv_layers:
        me.uv_layers.new(name='Gen')
    me.uv_layers.active = me.uv_layers['Gen']
    gi = {i for i, m in enumerate(me.materials) if m.name.startswith('GEN_')}
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.mode_set(mode='EDIT')
    bm = bmesh.from_edit_mesh(me)
    for f in bm.faces:
        f.select = f.material_index in gi
    bmesh.update_edit_mesh(me)
    bpy.ops.uv.smart_project(angle_limit=math.radians(55), island_margin=0.004, scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')


def bake_atlas(o, sp, tex, size=1024):
    """colour (the piece's colour, occlusion, a top light, bright worn edges, grain), normal (rounded edges, grain),
    and occlusion-roughness-metal for the generated faces, through UV map 'Gen'"""
    import build_chars as bc
    me = o.data
    gen = [m for m in me.materials if m.name.startswith('GEN_')]
    if not gen:
        return
    gen_uv(o)
    name = sp['name']
    # the bake shader: an emission of what we want, per channel
    def shader(m, mode):
        nt = m.node_tree
        for n in list(nt.nodes):
            if n.type not in ('OUTPUT_MATERIAL',):
                nt.nodes.remove(n)
        out = next(x for x in nt.nodes if x.type == 'OUTPUT_MATERIAL')
        col = nt.nodes.new('ShaderNodeAttribute'); col.attribute_name = 'gen_col'
        prm = nt.nodes.new('ShaderNodeAttribute'); prm.attribute_name = 'gen_prm'
        sep = nt.nodes.new('ShaderNodeSeparateColor'); nt.links.new(prm.outputs['Color'], sep.inputs[0])
        em = nt.nodes.new('ShaderNodeEmission')
        nt.links.new(em.outputs[0], out.inputs['Surface'])
        # grain: hammered (voronoi dents) or woven (fine noise), a little
        tc = nt.nodes.new('ShaderNodeTexCoord')
        vor = nt.nodes.new('ShaderNodeTexVoronoi'); vor.inputs['Scale'].default_value = 55
        nt.links.new(tc.outputs['Object'], vor.inputs['Vector'])
        noi = nt.nodes.new('ShaderNodeTexNoise'); noi.inputs['Scale'].default_value = 140; noi.inputs['Detail'].default_value = 6
        nt.links.new(tc.outputs['Object'], noi.inputs['Vector'])
        big = nt.nodes.new('ShaderNodeTexNoise'); big.inputs['Scale'].default_value = 9; big.inputs['Detail'].default_value = 4
        nt.links.new(tc.outputs['Object'], big.inputs['Vector'])
        if mode == 'COLOR':
            ao = nt.nodes.new('ShaderNodeAmbientOcclusion'); ao.samples = 16; ao.inputs['Distance'].default_value = 0.08
            bev = nt.nodes.new('ShaderNodeBevel'); bev.inputs['Radius'].default_value = 0.008
            geo = nt.nodes.new('ShaderNodeNewGeometry')
            # the edge: where the bevelled normal leaves the true one
            dot = nt.nodes.new('ShaderNodeVectorMath'); dot.operation = 'DOT_PRODUCT'
            nt.links.new(bev.outputs[0], dot.inputs[0]); nt.links.new(geo.outputs['Normal'], dot.inputs[1])
            edge = nt.nodes.new('ShaderNodeMapRange'); edge.inputs[1].default_value = 0.995; edge.inputs[2].default_value = 0.93
            nt.links.new(dot.outputs['Value'], edge.inputs[0])
            # the top light: brighter facing up
            sepn = nt.nodes.new('ShaderNodeSeparateXYZ'); nt.links.new(geo.outputs['Normal'], sepn.inputs[0])
            top = nt.nodes.new('ShaderNodeMapRange'); top.inputs[1].default_value = -1; top.inputs[2].default_value = 1; top.inputs[3].default_value = 0.72; top.inputs[4].default_value = 1.12
            nt.links.new(sepn.outputs['Z'], top.inputs[0])
            # colour = base x occlusion x top light x grain, then the worn edge lightened toward a pale highlight
            m1 = nt.nodes.new('ShaderNodeMix'); m1.data_type = 'RGBA'; m1.blend_type = 'MULTIPLY'; m1.inputs['Factor'].default_value = 1
            nt.links.new(col.outputs['Color'], m1.inputs[6]); nt.links.new(ao.outputs['AO'], m1.inputs[7])
            aor = nt.nodes.new('ShaderNodeMapRange'); aor.inputs[3].default_value = 0.45; aor.inputs[4].default_value = 1.0
            nt.links.new(ao.outputs['AO'], aor.inputs[0])
            m1b = nt.nodes.new('ShaderNodeMix'); m1b.data_type = 'RGBA'; m1b.blend_type = 'MULTIPLY'; m1b.inputs['Factor'].default_value = 1
            nt.links.new(col.outputs['Color'], m1b.inputs[6]); nt.links.new(aor.outputs[0], m1b.inputs[7])
            m2 = nt.nodes.new('ShaderNodeMix'); m2.data_type = 'RGBA'; m2.blend_type = 'MULTIPLY'; m2.inputs['Factor'].default_value = 1
            nt.links.new(m1b.outputs[2], m2.inputs[6]); nt.links.new(top.outputs[0], m2.inputs[7])
            gr = nt.nodes.new('ShaderNodeMapRange'); gr.inputs[3].default_value = 0.86; gr.inputs[4].default_value = 1.08
            nt.links.new(big.outputs['Fac'], gr.inputs[0])
            m3 = nt.nodes.new('ShaderNodeMix'); m3.data_type = 'RGBA'; m3.blend_type = 'MULTIPLY'; m3.inputs['Factor'].default_value = 1
            nt.links.new(m2.outputs[2], m3.inputs[6]); nt.links.new(gr.outputs[0], m3.inputs[7])
            hi = nt.nodes.new('ShaderNodeMix'); hi.data_type = 'RGBA'; hi.blend_type = 'SCREEN'
            nt.links.new(edge.outputs[0], hi.inputs['Factor'])
            nt.links.new(m3.outputs[2], hi.inputs[6]); hi.inputs[7].default_value = (0.5, 0.48, 0.42, 1)
            # edges only shine on metal
            em_f = nt.nodes.new('ShaderNodeMath'); em_f.operation = 'MULTIPLY'
            nt.links.new(edge.outputs[0], em_f.inputs[0]); nt.links.new(sep.outputs['Green'], em_f.inputs[1])
            nt.links.new(em_f.outputs[0], hi.inputs['Factor'])
            nt.links.new(hi.outputs[2], em.inputs['Color'])
        elif mode == 'ORM':
            ao = nt.nodes.new('ShaderNodeAmbientOcclusion'); ao.samples = 16; ao.inputs['Distance'].default_value = 0.08
            comb = nt.nodes.new('ShaderNodeCombineColor')
            aor = nt.nodes.new('ShaderNodeMapRange'); aor.inputs[3].default_value = 0.5; aor.inputs[4].default_value = 1.0
            nt.links.new(ao.outputs['AO'], aor.inputs[0]); nt.links.new(aor.outputs[0], comb.inputs[0])
            rr = nt.nodes.new('ShaderNodeMapRange'); rr.inputs[3].default_value = -0.08; rr.inputs[4].default_value = 0.1
            nt.links.new(big.outputs['Fac'], rr.inputs[0])
            ad = nt.nodes.new('ShaderNodeMath'); ad.operation = 'ADD'; ad.use_clamp = True
            nt.links.new(sep.outputs['Red'], ad.inputs[0]); nt.links.new(rr.outputs[0], ad.inputs[1])
            nt.links.new(ad.outputs[0], comb.inputs[1]); nt.links.new(sep.outputs['Green'], comb.inputs[2])
            nt.links.new(comb.outputs[0], em.inputs['Color'])
        else:  # NORMAL: the principled normal from a bevel and the grain as a bump
            bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
            nt.links.new(bsdf.outputs[0], out.inputs['Surface'])
            bev = nt.nodes.new('ShaderNodeBevel'); bev.inputs['Radius'].default_value = 0.008
            bump = nt.nodes.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.12; bump.inputs['Distance'].default_value = 0.002
            # hammered metal: the voronoi distance; cloth: the fine noise
            mixg = nt.nodes.new('ShaderNodeMix'); mixg.data_type = 'FLOAT'
            nt.links.new(sep.outputs['Blue'], mixg.inputs['Factor'])
            nt.links.new(vor.outputs['Distance'], mixg.inputs[2]); nt.links.new(noi.outputs['Fac'], mixg.inputs[3])
            nt.links.new(mixg.outputs[0], bump.inputs['Height']); nt.links.new(bev.outputs[0], bump.inputs['Normal'])
            nt.links.new(bump.outputs[0], bsdf.inputs['Normal'])
    res = {}
    for mode in ('COLOR', 'ORM', 'NORMAL'):
        for m in gen:
            shader(m, mode)
        img = bpy.data.images.new('gen_' + mode, size, size)
        img.colorspace_settings.name = 'sRGB' if mode == 'COLOR' else 'Non-Color'
        dummy = bpy.data.images.new('dummy', 8, 8)
        added = []
        for m in me.materials:
            n = m.node_tree.nodes.new('ShaderNodeTexImage')
            n.image = img if m in gen else dummy
            m.node_tree.nodes.active = n
            added.append((m, n))
        for l in me.uv_layers:
            l.active_render = (l.name == 'Gen')
        me.uv_layers.active = me.uv_layers['Gen']
        bpy.context.scene.cycles.samples = 16
        if mode == 'NORMAL':
            bpy.ops.object.bake(type='NORMAL', normal_space='TANGENT', uv_layer='Gen')
        else:
            bpy.ops.object.bake(type='EMIT', uv_layer='Gen')
        a = np.empty(size * size * 4, np.float32)
        img.pixels.foreach_get(a)
        res[mode] = a.reshape(size, size, 4)[..., :3]
        for m, n in added:
            m.node_tree.nodes.remove(n)
        bpy.data.images.remove(img)
        bpy.data.images.remove(dummy)
    for l in me.uv_layers:
        l.active_render = (l.name == 'UVMap')
    bc.save_np(np.clip(res['COLOR'], 0, 1), os.path.join(tex, f'{name}_gen_c.png'))
    bc.save_np(res['NORMAL'], os.path.join(tex, f'{name}_gen_n.png'), 'Non-Color')
    bc.save_np(np.clip(res['ORM'], 0, 1), os.path.join(tex, f'{name}_gen_orm.png'), 'Non-Color')


def material_info(m, sp, tex):
    base = m.name.split('.')[0]
    return {'key': base.lower(), 'cls': 'plate' if base == 'GEN_metal' else 'robe', 'set': f"{sp['name']}_gen", 'uv': 'Gen', 'double': base == 'GEN_cloth'}
