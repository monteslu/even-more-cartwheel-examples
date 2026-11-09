# Ember Throne's cast from CC0 bases (tools/chars/cc0: Quaternius' Universal Base Characters and Modular Character
# Outfits - Fantasy, CC0 1.0): each figure is an outfit, a head and its hair, posed from the source's T pose to the
# game's rest (arms hanging), sized, bulked per character (shoulders, chest, limbs), the head and hands a little larger
# for the third-person camera, dressed with generated pieces (plate, helmets, crowns, robes, capes: dress.py), and
# recoloured. The skin of the head and the hands is baked into one atlas. The result is dumped for
# tools/chars/make-characters.mjs, which writes the glb with the game's skeleton and clips:
#   out/cc0/<name>.json  joints (glTF axes), materials, the vertex streams' layout
#   out/cc0/<name>.bin   the streams
#   out/textures/<name>_<mat>_{c,n,orm,e}.png
# Run (absolute paths; Blender used as a tool, headless):
#   flatpak run --filesystem=home org.blender.Blender --background --factory-startup \
#     --python <abs>/tools/chars/blender/build_chars.py -- <abs>/tools/chars/cc0 <abs>/tools/chars/out [name ...]
import bpy, bmesh, sys, os, json, math
import numpy as np
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cast  # the figures (specs)
import dress  # generated pieces

argv = sys.argv[sys.argv.index('--') + 1:]
CC0, OUT = argv[0], argv[1]
ONLY = argv[2:]
MOUT = os.path.join(OUT, 'cc0')
TEX = os.path.join(OUT, 'textures')
os.makedirs(MOUT, exist_ok=True)
os.makedirs(TEX, exist_ok=True)

# the source rig's bones onto the game's (make-characters.mjs BONE_NAMES)
MAP = {'root': 'Hips', 'pelvis': 'Hips', 'spine_01': 'Spine', 'spine_02': 'Spine', 'spine_03': 'Chest', 'neck_01': 'Neck', 'Head': 'Head'}
for s, S in (('l', 'L'), ('r', 'R')):
    MAP.update({'clavicle_' + s: 'Chest', 'upperarm_' + s: 'UpperArm' + S, 'lowerarm_' + s: 'LowerArm' + S, 'hand_' + s: 'Hand' + S,
                'thumb_01_' + s: 'Hand' + S, 'thumb_02_' + s: 'Thumb' + S, 'thumb_03_' + s: 'Thumb' + S, 'thumb_04_leaf_' + s: 'Thumb' + S,
                'thigh_' + s: 'UpperLeg' + S, 'calf_' + s: 'LowerLeg' + S, 'foot_' + s: 'Foot' + S, 'ball_' + s: 'Foot' + S, 'ball_leaf_' + s: 'Foot' + S})
    for f in ('index', 'middle', 'ring', 'pinky'):
        MAP.update({f + '_01_' + s: 'Fing' + S + '1', f + '_02_' + s: 'Fing' + S + '2', f + '_03_' + s: 'Fing' + S + '2', f + '_04_leaf_' + s: 'Fing' + S + '2'})
GAME_BONES = ['Hips', 'Spine', 'Chest', 'Neck', 'Head', 'UpperArmL', 'LowerArmL', 'HandL', 'UpperArmR', 'LowerArmR', 'HandR',
              'UpperLegL', 'LowerLegL', 'FootL', 'UpperLegR', 'LowerLegR', 'FootR', 'FingL1', 'FingL2', 'ThumbL', 'FingR1', 'FingR2', 'ThumbR']
CHILD = {'Hips': 'Spine', 'Spine': 'Chest', 'Chest': 'Neck', 'Neck': 'Head', 'UpperArmL': 'LowerArmL', 'LowerArmL': 'HandL', 'HandL': 'FingL1',
         'UpperArmR': 'LowerArmR', 'LowerArmR': 'HandR', 'HandR': 'FingR1', 'UpperLegL': 'LowerLegL', 'LowerLegL': 'FootL', 'UpperLegR': 'LowerLegR', 'LowerLegR': 'FootR'}


def log(*a):
    print('[chars]', *a, flush=True)


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for im in list(bpy.data.images):
        bpy.data.images.remove(im)


def load(name):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(CC0, name + '.gltf'))
    new = [o for o in bpy.data.objects if o not in before]
    for o in list(new):
        if o.type == 'MESH' and o.name.startswith('Icosphere'):
            new.remove(o)
            bpy.data.objects.remove(o)
    return new


def cut_to_head(o, thr=0.35):
    """keep only the head and neck of a full body (the outfit is the body)"""
    gi = {g.index: g.name for g in o.vertex_groups}
    bm = bmesh.new()
    bm.from_mesh(o.data)
    dl = bm.verts.layers.deform.active
    kill = [v for v in bm.verts if sum(w for g, w in v[dl].items() if gi.get(g) in ('Head', 'neck_01')) < thr]
    bmesh.ops.delete(bm, geom=kill, context='VERTS')
    bm.to_mesh(o.data)
    bm.free()


def pose_arms_down(arm, out_deg):
    """turn each arm's chain to hang down, a little out from the body (the game's rest pose)"""
    bpy.context.view_layer.update()
    for s, sx in (('l', 1), ('r', -1)):
        a = math.radians(out_deg)
        want = Vector((sx * math.sin(a), 0, -math.cos(a)))
        for bn, cn in (('upperarm_' + s, 'lowerarm_' + s), ('lowerarm_' + s, 'hand_' + s), ('hand_' + s, 'middle_01_' + s)):
            pb, pc = arm.pose.bones[bn], arm.pose.bones[cn]
            h = arm.matrix_world @ pb.head
            c = arm.matrix_world @ pc.head
            d = (c - h)
            if bn.startswith('hand'):
                d = Vector((d.x, 0, d.z))
            q = d.normalized().rotation_difference(want)
            R = Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Translation(-h)
            pb.matrix = arm.matrix_world.inverted() @ R @ arm.matrix_world @ pb.matrix
            bpy.context.view_layer.update()


def bake_pose(objs):
    """apply every mesh's armature modifier (the posed shape becomes the mesh), keep the vertex groups"""
    for o in objs:
        if o.type != 'MESH':
            continue
        for m in list(o.modifiers):
            if m.type == 'ARMATURE':
                bpy.context.view_layer.objects.active = o
                with bpy.context.temp_override(object=o, active_object=o):
                    bpy.ops.object.modifier_apply(modifier=m.name)
        mw = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = mw
        o.data.transform(o.matrix_world)
        o.matrix_world = Matrix.Identity(4)


def joints_of(arm):
    """the game's joints (Blender axes) from the posed source rig"""
    P = lambda n: (arm.matrix_world @ arm.pose.bones[n].head).copy()
    J = {'Hips': P('pelvis'), 'Spine': P('spine_01'), 'Chest': P('spine_03'), 'Neck': P('neck_01'), 'Head': P('Head')}
    for s, S in (('l', 'L'), ('r', 'R')):
        J['UpperArm' + S] = P('upperarm_' + s)
        J['LowerArm' + S] = P('lowerarm_' + s)
        J['Hand' + S] = P('hand_' + s)
        J['UpperLeg' + S] = P('thigh_' + s)
        J['LowerLeg' + S] = P('calf_' + s)
        J['Foot' + S] = P('foot_' + s)
        J['Fing' + S + '1'] = sum((P(f + '_01_' + s) for f in ('index', 'middle', 'ring', 'pinky')), Vector()) / 4
        J['Fing' + S + '2'] = sum((P(f + '_02_' + s) for f in ('index', 'middle', 'ring', 'pinky')), Vector()) / 4
        J['Thumb' + S] = P('thumb_02_' + s)
    return J


# ── per-vertex work on the joined mesh, in numpy ──────────────────────────────────────────────
def game_weights(o):
    """per vertex: the game bones' weights (summed over the source bones that map to each), as an (n, len(GAME_BONES)) array"""
    n = len(o.data.vertices)
    W = np.zeros((n, len(GAME_BONES)), np.float32)
    gi = {g.index: g.name for g in o.vertex_groups}
    col = {b: i for i, b in enumerate(GAME_BONES)}
    for v in o.data.vertices:
        for g in v.groups:
            nm = gi.get(g.group)
            t = MAP.get(nm, nm if nm in col else None)
            if t in col:
                W[v.index, col[t]] += g.weight
    s = W.sum(1, keepdims=True)
    s[s == 0] = 1
    return W / s


def seg_closest(p, a, b):
    ab = b - a
    t = np.clip(((p - a) @ ab) / max(1e-9, ab @ ab), 0, 1)
    return a + t[:, None] * ab


def deform(o, J, sp):
    """size, bulk and the head and hands scale, on the mesh and its joints alike"""
    me = o.data
    n = len(me.vertices)
    P = np.empty(n * 3, np.float32)
    me.vertices.foreach_get('co', P)
    P = P.reshape(n, 3).astype(np.float64)
    W = game_weights(o)
    col = {b: i for i, b in enumerate(GAME_BONES)}
    Jn = {k: np.array(v[:], np.float64) for k, v in J.items()}
    # 1. uniform size: the top of the head (without hair) to sp height
    top = sp.get('height', 1.78)
    k = top / sp['_crown']
    P *= k
    for j in Jn:
        Jn[j] *= k
    # 1b. the upper arm lengthened (arm_len, a share of the height): the source rig's elbows sit high and its arms hang
    # short; the elbow, the forearm and the hand move down the upper arm's own line, the upper arm stretching to meet
    # them (each vertex by its arm weight and how far down the upper arm it lies)
    al = sp.get('arm_len', 0) * top
    if al:
        for S in 'LR':
            A, B = Jn['UpperArm' + S].copy(), Jn['LowerArm' + S].copy()
            ax = (B - A) / np.linalg.norm(B - A)
            below = sum(W[:, col[c]] for c in ('LowerArm' + S, 'Hand' + S, 'Fing' + S + '1', 'Fing' + S + '2', 'Thumb' + S))
            t = np.clip(((P - A) @ ax) / np.linalg.norm(B - A), 0, 1)
            s = below + W[:, col['UpperArm' + S]] * t
            P += s[:, None] * ax[None, :] * al
            for c in ('LowerArm' + S, 'Hand' + S, 'Fing' + S + '1', 'Fing' + S + '2', 'Thumb' + S):
                Jn[c] = Jn[c] + ax * al
    # 2. thickness: each bone's flesh pushed out from its segment. A number pushes it out evenly; a dict sets the
    # breadth (x, side to side) and the depth (y, front to back) apart, so a big frame widens across the chest and the
    # shoulders with a flat front below the chest, and 'end' tapers the push along the bone (a limb thick at the
    # shoulder or the hip and lean at the elbow or the knee)
    thick = sp.get('thick', {})
    if thick:
        D = np.zeros_like(P)
        for b, f in thick.items():
            bones = [b] if b in col else [x for x in GAME_BONES if x.startswith(b)]
            fx, fy = (f.get('x', 1), f.get('y', f.get('x', 1))) if isinstance(f, dict) else (f, f)
            end = f.get('end') if isinstance(f, dict) else None
            for bb in bones:
                if bb not in CHILD or (fx == 1 and fy == 1 and end is None):
                    continue
                a, bj = Jn[bb], Jn[CHILD[bb]]
                ab = bj - a
                t = np.clip(((P - a) @ ab) / max(1e-9, ab @ ab), 0, 1)
                c = a + t[:, None] * ab
                k = np.stack([np.full(len(P), fx - 1), np.full(len(P), fy - 1), np.full(len(P), (fx + fy) / 2 - 1)], 1)
                if end is not None:
                    k = k + t[:, None] * ((end - 1) - k)
                D += W[:, col[bb]][:, None] * (P - c) * k
        P += D
    # 3. shoulders: each arm chain moved out
    sh = sp.get('shoulder', 0)
    if sh:
        for S, sx in (('L', 1), ('R', -1)):
            chain = [x for x in GAME_BONES if x.endswith(S) and (x.startswith('UpperArm') or x.startswith('LowerArm') or x.startswith('Hand') or x.startswith('Fing') or x.startswith('Thumb'))]
            w = sum(W[:, col[c]] for c in chain)
            # the clavicle region (Chest weight beside the arm) moves half as far, so the shoulder stays one shape
            side = np.clip((P[:, 0] * sx - 0.05) / 0.12, 0, 1)
            w = np.maximum(w, 0.5 * W[:, col['Chest']] * side)
            P[:, 0] += sx * sh * w
            for c in chain:
                Jn[c][0] += sx * sh
    # 4. the head and the hands a little larger, about their joints
    hs = sp.get('head_scale', 1.0)
    if hs != 1:
        w = W[:, col['Head']] + 0.5 * W[:, col['Neck']]
        P = Jn['Head'] + (P - Jn['Head']) * (1 + (hs - 1) * w)[:, None]
    hd = sp.get('hand_scale', 1.0)
    if hd != 1:
        for S in 'LR':
            w = sum(W[:, col[c]] for c in ('Hand' + S, 'Fing' + S + '1', 'Fing' + S + '2', 'Thumb' + S))
            J0 = Jn['Hand' + S].copy()
            P = J0 + (P - J0) * (1 + (hd - 1) * w)[:, None]
            for c in ('Fing' + S + '1', 'Fing' + S + '2', 'Thumb' + S):
                Jn[c] = J0 + (Jn[c] - J0) * hd
    # 5. the feet (foot_scale): heel to toe and side to side about the ankle, the sole left where it stands (no float)
    fs = sp.get('foot_scale', 1.0)
    if fs != 1:
        for S in 'LR':
            A = Jn['Foot' + S]
            w = W[:, col['Foot' + S]] * np.clip((A[2] + 0.03 - P[:, 2]) / 0.04, 0, 1)
            k = 1 + (fs - 1) * w
            P[:, 0] = A[0] + (P[:, 0] - A[0]) * k
            P[:, 1] = A[1] + (P[:, 1] - A[1]) * k
    me.vertices.foreach_set('co', P.astype(np.float32).ravel())
    me.update()
    return {j: Vector(v) for j, v in Jn.items()}


# ── textures ──────────────────────────────────────────────────────────────────────────────────
def img_np(path):
    im = bpy.data.images.load(path, check_existing=False)
    w, h = im.size
    a = np.empty(w * h * 4, np.float32)
    im.pixels.foreach_get(a)
    return a.reshape(h, w, 4), im


def save_np(arr, path, colorspace='sRGB'):
    h, w = arr.shape[:2]
    im = bpy.data.images.new(os.path.basename(path), w, h, alpha=arr.shape[2] == 4)
    im.colorspace_settings.name = colorspace if colorspace != 'Non-Color' else 'Non-Color'
    a = arr if arr.shape[2] == 4 else np.concatenate([arr, np.ones((h, w, 1), np.float32)], 2)
    im.pixels.foreach_set(a.astype(np.float32).ravel())
    im.filepath_raw = path
    im.file_format = 'PNG'
    im.save()
    bpy.data.images.remove(im)


def resize(arr, size):
    h, w = arr.shape[:2]
    if w == size:
        return arr
    if w % size == 0:  # box filter
        f = w // size
        return arr.reshape(size, f, size, f, arr.shape[2]).mean((1, 3))
    yi = (np.arange(size) * h / size).astype(int)
    xi = (np.arange(size) * w / size).astype(int)
    return arr[yi][:, xi]


def rgb2hsv(c):
    r, g, b = c[..., 0], c[..., 1], c[..., 2]
    mx, mn = c[..., :3].max(-1), c[..., :3].min(-1)
    d = mx - mn + 1e-9
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) / 6
    s = np.where(mx > 0, (mx - mn) / (mx + 1e-9), 0)
    return h, s, mx


def hsv2rgb(h, s, v):
    i = np.floor(h * 6) % 6
    f = h * 6 - np.floor(h * 6)
    p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
    out = np.stack([np.choose(i.astype(int), [v, q, p, p, t, v]), np.choose(i.astype(int), [t, v, v, q, p, p]), np.choose(i.astype(int), [p, p, t, v, v, q])], -1)
    return out


def hue_dist(h, c):
    d = np.abs(h - c)
    return np.minimum(d, 1 - d)


def recolor(rgb, rules):
    """rules: dicts with hue (centre, degrees), width (degrees), minsat, and to (hue degrees) or shift, sat, val:
    pixels near the hue move to the new one (soft edges); sat and val multiply. The value is kept, so the painted
    shading stays."""
    h, s, v = rgb2hsv(rgb)
    for r in rules:
        c, wd = r['hue'] / 360, r.get('width', 30) / 360
        m = np.clip(1 - (hue_dist(h, c) - wd) / (wd * 0.5 + 1e-6), 0, 1)
        m *= np.clip((s - r.get('minsat', 0.15)) / 0.1, 0, 1)
        if 'maxval' in r:
            m *= np.clip((r['maxval'] - v) / 0.1, 0, 1)
        if 'to' in r:
            nh = (r['to'] / 360) % 1
        else:
            nh = (h + r.get('shift', 0) / 360) % 1
        ns = np.clip(s * r.get('sat', 1), 0, 1)
        nv = np.clip(v * r.get('val', 1), 0, 1)
        # blend in hue along the short way
        dh = ((nh - h + 0.5) % 1) - 0.5
        h = (h + m * dh) % 1
        s = s + m * (ns - s)
        v = v + m * (nv - v)
    return hsv2rgb(h, s, v)


def grade(rgb, g):
    """the figure's whole-texture grade: sat, val, a tint multiply"""
    if not g:
        return rgb
    h, s, v = rgb2hsv(rgb)
    s = np.clip(s * g.get('sat', 1), 0, 1)
    v = np.clip(v * g.get('val', 1), 0, 1)
    out = hsv2rgb(h, s, v)
    if 'tint' in g:
        out = out * np.array(g['tint'])
    return out


def srgb_tex(path):
    a, im = img_np(path)
    bpy.data.images.remove(im)
    return a


# ── baking (Cycles, CPU) ──────────────────────────────────────────────────────────────────────
def setup_cycles(samples=8):
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = samples
    sc.render.bake.margin = 8


def bake_to(o, mats, uvname, size, kind, path, colorspace='sRGB'):
    """bake the faces of mats on o into a new image through UV map uvname. kind: 'COLOR' (base colour via
    emission), 'NORMAL', 'ROUGHNESS'. Faces of other materials bake into a throwaway image."""
    # the source textures read through their own UV map (the bake target's map is the render-active one)
    for m in o.data.materials:
        nt = m.node_tree
        for n in list(nt.nodes):
            if n.type == 'TEX_IMAGE' and not n.inputs['Vector'].links:
                uvn = nt.nodes.new('ShaderNodeUVMap')
                uvn.uv_map = 'UVMap'
                nt.links.new(uvn.outputs['UV'], n.inputs['Vector'])
            if n.type == 'NORMAL_MAP':
                n.uv_map = 'UVMap'
    img =bpy.data.images.new('bake_' + kind, size, size, alpha=False, float_buffer=False)
    img.colorspace_settings.name = colorspace
    dummy = bpy.data.images.new('dummy', 8, 8)
    saved = []
    for m in o.data.materials:
        nt = m.node_tree
        n = nt.nodes.new('ShaderNodeTexImage')
        n.image = img if m.name in mats else dummy
        nt.nodes.active = n
        n.select = True
        saved.append((m, n))
        if kind == 'COLOR' and m.name in mats:
            bsdf = next(x for x in nt.nodes if x.type == 'BSDF_PRINCIPLED')
            out = next(x for x in nt.nodes if x.type == 'OUTPUT_MATERIAL')
            em = nt.nodes.new('ShaderNodeEmission')
            src = bsdf.inputs['Base Color']
            if src.links:
                nt.links.new(src.links[0].from_socket, em.inputs['Color'])
            else:
                em.inputs['Color'].default_value = src.default_value
            old = out.inputs['Surface'].links[0].from_socket if out.inputs['Surface'].links else None
            nt.links.new(em.outputs[0], out.inputs['Surface'])
            saved.append((m, em, old, out))
    o.data.uv_layers.active = o.data.uv_layers[uvname]
    for l in o.data.uv_layers:
        l.active_render = (l.name == uvname)
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    if kind == 'COLOR':
        bpy.ops.object.bake(type='EMIT')
    elif kind == 'NORMAL':
        bpy.ops.object.bake(type='NORMAL', normal_space='TANGENT')
    else:
        bpy.ops.object.bake(type='ROUGHNESS')
    for s in saved:
        if len(s) == 4:
            m, em, old, out = s
            if old:
                m.node_tree.links.new(old, out.inputs['Surface'])
            m.node_tree.nodes.remove(em)
    for s in saved:
        if len(s) == 2:
            s[0].node_tree.nodes.remove(s[1])
    a = np.empty(size * size * 4, np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    bpy.data.images.remove(dummy)
    for l in o.data.uv_layers:
        l.active_render = (l.name == 'UVMap')
    return a.reshape(size, size, 4)


HEAD_UV = 2.2


def skin_atlas(o, skin_mats):
    """a UV map 'Skin' holding only the skin faces' islands, packed to fill the square"""
    me = o.data
    src = me.uv_layers['UVMap']
    if 'Skin' not in me.uv_layers:
        me.uv_layers.new(name='Skin')
    dst = me.uv_layers['Skin']
    idx = [i for i, m in enumerate(me.materials) if m.name in skin_mats]
    uv = np.empty(len(me.loops) * 2, np.float32)
    src.data.foreach_get('uv', uv)
    dst.data.foreach_set('uv', uv)
    me.uv_layers.active = dst
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.mode_set(mode='EDIT')
    bm = bmesh.from_edit_mesh(me)
    uvl = bm.loops.layers.uv['Skin']
    for f in bm.faces:
        f.select = f.material_index in idx
        for l in f.loops:
            if not f.select:
                l[uvl].uv = (0.999, 0.999)
        # the face gets more of the atlas than the hands (it is what the close camera reads)
        if f.select and me.materials[f.material_index].name.startswith("MI_Superhero"):
            for l in f.loops:
                l[uvl].uv = l[uvl].uv * HEAD_UV
    bmesh.update_edit_mesh(me)
    bpy.context.scene.tool_settings.use_uv_select_sync = True
    bpy.ops.uv.pack_islands(rotate=True, margin=0.004)
    bpy.ops.object.mode_set(mode='OBJECT')


# ── the dump ──────────────────────────────────────────────────────────────────────────────────
def dump(o, J, sp, mats_info, path_json, path_bin):
    me = o.data
    me.calc_loop_triangles()
    W = game_weights(o)
    order = np.argsort(-W, 1)[:, :4]
    top = np.take_along_axis(W, order, 1)
    top /= np.maximum(top.sum(1, keepdims=True), 1e-9)
    cn = me.corner_normals
    uv_main = me.uv_layers['UVMap'].data
    uv_skin = me.uv_layers['Skin'].data if 'Skin' in me.uv_layers else None
    groups = {}
    for t in me.loop_triangles:
        mi = mats_info[t.material_index]
        g = groups.setdefault(mi['key'], {'info': mi, 'map': {}, 'pos': [], 'nrm': [], 'uv': [], 'j': [], 'w': [], 'idx': []})
        for li, vi in zip(t.loops, t.vertices):
            uvd = me.uv_layers[mi['uv']].data
            u, v = uvd[li].uv
            nx, ny, nz = cn[li].vector
            key = (vi, round(u, 5), round(v, 5), round(nx, 3), round(ny, 3), round(nz, 3))
            k = g['map'].get(key)
            if k is None:
                k = len(g['pos']) // 3
                g['map'][key] = k
                co = me.vertices[vi].co
                g['pos'] += [co.x, co.z, -co.y]
                g['nrm'] += [nx, nz, -ny]
                g['uv'] += [u, 1 - v]
                g['j'] += [int(x) for x in order[vi]]
                g['w'] += [float(x) for x in top[vi]]
            g['idx'].append(k)
    blobs, meta, off = [], [], 0
    for key, g in groups.items():
        rec = {'key': key, **{k: v for k, v in g['info'].items() if k != 'key'}}
        for nm, arr, dt in (('pos', g['pos'], np.float32), ('nrm', g['nrm'], np.float32), ('uv', g['uv'], np.float32), ('joints', g['j'], np.uint8), ('weights', g['w'], np.float32), ('idx', g['idx'], np.uint32)):
            b = np.asarray(arr, dt).tobytes()
            pad = (-len(b)) % 4
            rec[nm] = [off, len(arr)]
            blobs.append(b + b'\0' * pad)
            off += len(b) + pad
        meta.append(rec)
    with open(path_bin, 'wb') as f:
        for b in blobs:
            f.write(b)
    G = lambda v: [v.x, v.z, -v.y]
    js = {'name': sp['name'], 'bones': GAME_BONES, 'joints': {k: G(v) for k, v in J.items()}, 'groups': meta,
          'tris': sum(len(g['idx']) // 3 for g in groups.values()), 'source': sp.get('source', [])}
    with open(path_json, 'w') as f:
        json.dump(js, f, indent=1)
    log(sp['name'], 'tris', js['tris'], 'groups', [(m['key'], len(groups[m['key']]['idx']) // 3) for m in meta])


# ── one figure ────────────────────────────────────────────────────────────────────────────────
def build(sp):
    clear()
    setup_cycles()
    parts = []
    outfit = load(sp['outfit'])
    drop = sp.get('drop', [])
    for o in list(outfit):
        if o.type == 'MESH' and any(d in o.name for d in drop):
            outfit.remove(o)
            bpy.data.objects.remove(o)
    # the heavy outfits (the ranger's boots and belts are 9000 triangles) thinned: the shapes are smooth, the detail is
    # in the normal maps
    if sp.get('decimate'):
        for o in outfit:
            if o.type == 'MESH' and len(o.data.polygons) > sp.get('decimate_min', 1500):
                m = o.modifiers.new('dec', 'DECIMATE')
                m.ratio = sp['decimate']
                m.use_collapse_triangulate = True
                bpy.context.view_layer.objects.active = o
                with bpy.context.temp_override(object=o, active_object=o):
                    bpy.ops.object.modifier_move_to_index(modifier='dec', index=0)
                    bpy.ops.object.modifier_apply(modifier='dec')
    parts += outfit
    head = load(sp['head'])
    for o in head:
        if o.type == 'MESH' and o.data.materials and o.data.materials[0].name.startswith('MI_Superhero'):
            cut_to_head(o)
        if o.type == 'MESH' and sp.get('no_brows') and o.data.materials and o.data.materials[0].name.startswith('MI_Hair'):
            bpy.data.objects.remove(o)
    parts += [o for o in head if o.name in bpy.data.objects]
    for h in sp.get('hair', []):
        parts += load(h)
    arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    for a in arms:
        pose_arms_down(a, sp.get('arm_out', 6))
    J = joints_of(arms[0])
    assert J['UpperArmL'].x > 0, 'the source rig must have its left arm at +X'
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    bake_pose(meshes)
    for a in arms:
        bpy.data.objects.remove(a)
    # one mesh, the materials merged by name
    bpy.ops.object.select_all(action='DESELECT')
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active
    o.name = sp['name']
    # the crown: the highest skin vertex (the head without hair or hood)
    zs = [v.co.z for v in o.data.vertices if o.data.materials[0] and True]
    me = o.data
    skin_idx = {i for i, m in enumerate(me.materials) if m.name.startswith('MI_Superhero')}
    crown = max(me.vertices[l].co.z for p in me.polygons if p.material_index in skin_idx for l in p.vertices)
    sp['_crown'] = crown
    J = deform(o, J, sp)
    # generated pieces (weighted to the game's bones by name: vertex groups named as GAME_BONES)
    dress.dress(o, J, sp)
    mats_info = textures(o, sp)
    dump(o, J, sp, mats_info, os.path.join(MOUT, sp['name'] + '.json'), os.path.join(MOUT, sp['name'] + '.bin'))


def tex_path(m, kind):
    """the source texture of material m feeding socket kind ('Base Color', 'Normal', 'Roughness')"""
    nt = m.node_tree
    bsdf = next(x for x in nt.nodes if x.type == 'BSDF_PRINCIPLED')
    s = bsdf.inputs[kind]

    def up(sock):
        if not sock.links:
            return None
        n = sock.links[0].from_node
        if n.type == 'TEX_IMAGE':
            if n.image is None:
                raise RuntimeError(f'{m.name}: an image node without an image')
            return bpy.path.abspath(n.image.filepath)
        for i in n.inputs:
            r = up(i)
            if r:
                return r
        return None
    return up(s)


def textures(o, sp):
    """the figure's materials: what each source material becomes, and its texture set written to TEX"""
    me = o.data
    name = sp['name']
    info = []
    done = {}
    skin_mats = [m.name for m in me.materials if m.name.startswith('MI_Superhero') or m.name.startswith('MI_Regular')]
    if skin_mats:
        skin_atlas(o, skin_mats)
        S = sp.get('skin_size', 1024)
        col = bake_to(o, skin_mats, 'Skin', S, 'COLOR', None)
        nrm = bake_to(o, skin_mats, 'Skin', S, 'NORMAL', None, 'Non-Color')
        rough = bake_to(o, skin_mats, 'Skin', S, 'ROUGHNESS', None, 'Non-Color')
        rgb = col[..., :3]
        if 'skin_tint' in sp:
            # the painted skin moved to the figure's tone: the mean of the face's colour to the wanted one
            m = rgb.reshape(-1, 3)
            lum = m.mean(1)
            sel = m[(lum > 0.08) & (lum < 0.95)]
            mean = sel.mean(0)
            rgb = rgb * (np.array(sp['skin_tint']) / np.maximum(mean, 1e-3))
        rgb = grade(np.clip(rgb, 0, 1), sp.get('skin_grade'))
        save_np(np.clip(rgb, 0, 1), os.path.join(TEX, f'{name}_skin_c.png'))
        save_np(nrm[..., :3], os.path.join(TEX, f'{name}_skin_n.png'), 'Non-Color')
        orm = np.stack([np.ones_like(rough[..., 0]), np.clip(rough[..., 0] * sp.get('skin_rough', 1.0), 0, 1), np.zeros_like(rough[..., 0])], -1)
        save_np(orm, os.path.join(TEX, f'{name}_skin_orm.png'), 'Non-Color')
    dress.bake_atlas(o, sp, TEX)
    for i, m in enumerate(me.materials):
        mn = m.name.split('.')[0]
        if m.name in skin_mats:
            info.append({'key': 'skin', 'cls': 'skin', 'set': f'{name}_skin', 'uv': 'Skin'})
            continue
        if mn.startswith('MI_Ranger') or mn.startswith('MI_Peasant'):
            key, cls = 'outfit', 'outfit'
            rules, size, g = sp.get('recolor', []), 1024, sp.get('outfit_grade')
        elif mn.startswith('MI_Hair'):
            key, cls = 'hair_' + mn[-1], 'hair'
            rules, size, g = [], 1024, None
        elif mn.startswith('MI_Eyes'):
            key, cls = 'eye', 'eye'
            rules, size, g = [], 512, None
        elif mn.startswith('GEN_'):
            info.append(dress.material_info(m, sp, TEX))
            continue
        else:
            raise RuntimeError('unknown material ' + mn)
        info.append({'key': key, 'cls': cls, 'set': f'{name}_{key}', 'uv': 'UVMap', **({'glow': sp['glow_eyes']} if cls == 'eye' and sp.get('glow_eyes') else {})})
        if key in done:
            continue
        done[key] = True
        c = srgb_tex(tex_path(m, 'Base Color'))
        rgb = c[..., :3]
        if cls == 'hair':
            # the source hair is painted grey: its value kept, coloured to the figure's hair
            v = rgb.mean(-1, keepdims=True)
            hc = np.array(sp.get('hair_color', (0.3, 0.18, 0.1)))
            rgb = np.clip(hc * (v / 0.5) ** 1.1, 0, 1)
        elif cls == 'eye':
            rgb = eye_color(rgb, sp)
        else:
            rgb = grade(recolor(rgb, rules), g)
        rgb = resize(rgb, size)
        save_np(np.clip(rgb, 0, 1), os.path.join(TEX, f'{name}_{key}_c.png'))
        n = srgb_tex(tex_path(m, 'Normal'))
        save_np(resize(n[..., :3], size), os.path.join(TEX, f'{name}_{key}_n.png'), 'Non-Color')
        rp = tex_path(m, 'Roughness')
        if rp and 'ORM' in rp:
            orm = srgb_tex(rp)[..., :3]
        else:
            rough = srgb_tex(rp)[..., 0] if rp else np.full(c.shape[:2], 0.8 if cls == 'hair' else 0.35, np.float32)
            orm = np.stack([np.ones_like(rough), rough, np.zeros_like(rough)], -1)
        if cls == 'outfit' and sp.get('outfit_rough'):
            orm[..., 1] = np.clip(orm[..., 1] * sp['outfit_rough'], 0.05, 1)
        save_np(resize(orm, size), os.path.join(TEX, f'{name}_{key}_orm.png'), 'Non-Color')
        if cls == 'eye' and sp.get('glow_eyes'):
            e = eye_glow(c[..., :3], sp)
            save_np(resize(e, size), os.path.join(TEX, f'{name}_{key}_e.png'))
    return info


def iris_mask(rgb):
    """the iris of the source eye texture: the coloured disc (the sclera is pale, the pupil black)"""
    h, w = rgb.shape[:2]
    y, x = np.mgrid[0:h, 0:w]
    d = np.hypot(x / w - 0.497, y / h - 0.503)
    return np.clip((0.104 - d) / 0.008, 0, 1)


def eye_color(rgb, sp):
    m = iris_mask(rgb)[..., None]
    ic = np.array(sp.get('iris', (0.25, 0.35, 0.2)))
    v = rgb.mean(-1, keepdims=True)
    tinted = np.clip(ic * (v / max(1e-3, float(v[m[..., 0] > 0.5].mean()) if (m > 0.5).any() else 0.4)), 0, 1)
    return rgb * (1 - m) + tinted * m


def eye_glow(rgb, sp):
    m = iris_mask(rgb)[..., None]
    return np.clip(m * np.array(sp['glow_eyes']), 0, 1)


if __name__ == '__main__':
    for sp in cast.SPECS:
        if ONLY and sp['name'] not in ONLY:
            continue
        log('building', sp['name'])
        build(sp)
    log('done')
