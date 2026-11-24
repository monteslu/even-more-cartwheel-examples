# Ember Throne's motion: the clips every figure plays, on the source rig (Quaternius' Universal Animation Library 1
# and 2, CC0 1.0: motion capture cleaned by hand, on the same humanoid rig as the Universal Base Characters the cast
# is built from). The clips the game needs are picked, cut, joined, held and timed here; where the libraries have no
# clip (the strafes, the back walk, the spinning heavy, the back step, a boss's roar, the leans), one is made here
# from real motion (a walk turned at the hips with the chest kept on the target, a walk run backwards, a cut whipped
# round a full turn) or keyed in the rig's own space with the joints staggered (the hips lead, the chest, the arms
# and the head follow and overshoot, then settle). The result, tools/chars/out/anims.glb, is the source each
# character's .cwimport retargets at pack time (cartwheel's retargeting: the rotations relative to the rest poses,
# the hips scaled by leg length, the feet placed by IK, the planted feet of the cycles held still).
#   flatpak run --filesystem=home org.blender.Blender --background --factory-startup \
#     --python <abs>/tools/chars/blender/build_anims.py -- <ual1 AL_Standard.fbx> <ual2 UAL2_Standard.glb> \
#     <ual2 Mannequin_F.glb> <out anims.glb>
# (tools/chars/fetch-anims.sh downloads the two libraries and checks them.)
import bpy, sys, math, os
from mathutils import Vector, Quaternion, Matrix

argv = sys.argv[sys.argv.index('--') + 1:]
FBX1, GLB2, MAN, OUT = argv[:4]
FPS = 30


def log(*a):
    print('[anims]', *a, flush=True)


bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.fps = FPS
bpy.ops.import_scene.gltf(filepath=MAN)
ARM = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
for a in list(bpy.data.actions):
    bpy.data.actions.remove(a)
bpy.ops.import_scene.fbx(filepath=FBX1)
bpy.ops.import_scene.gltf(filepath=GLB2)
for o in list(bpy.data.objects):
    if o != ARM and o.parent != ARM:
        bpy.data.objects.remove(o)
SRC = {a.name.split('|')[-1]: a for a in bpy.data.actions}
BONES = [b.name for b in ARM.data.bones]
PB = ARM.pose.bones
for pb in PB:
    pb.rotation_mode = 'QUATERNION'
ARM.animation_data_create()


# ── a clip is a list of frames; a frame is {bone: (location Vector, rotation Quaternion)} in each bone's own (basis) space
def assign(act):
    ARM.animation_data.action = act
    try:
        ARM.animation_data.action_slot = act.slots[0]
    except Exception:
        pass


def sample(name, a=None, b=None):
    act = SRC[name]
    assign(act)
    f0, f1 = (int(round(x)) for x in act.frame_range)
    a = f0 if a is None else a
    b = f1 if b is None else b
    out = []
    for f in range(a, b + 1):
        sc.frame_set(f)
        out.append({n: (PB[n].location.copy(), PB[n].rotation_quaternion.copy()) for n in BONES})
    return out


def in_place(c):
    """the root bone's travel taken out (the game moves the body; the clip keeps the hips' own motion)"""
    return [{**f, 'root': (Vector((0, 0, 0)), f['root'][1].copy())} for f in c]


def cat(*cs):
    out = []
    for c in cs:
        out += c
    return out


def hold(c, n):
    return c + [dict(c[-1]) for _ in range(n)]


def rev(c):
    return list(reversed(c))


def lerp_frame(a, b, t):
    return {n: (a[n][0].lerp(b[n][0], t), a[n][1].slerp(b[n][1], t)) for n in BONES}


def blend_in(c, first, n):
    """the first n frames of c eased in from `first` (a join without a step)"""
    out = list(c)
    for k in range(min(n, len(c))):
        t = (k + 1) / (n + 1)
        t = t * t * (3 - 2 * t)
        out[k] = lerp_frame(first, c[k], t)
    return out


def loopify(c, n):
    """the last n frames eased toward the first frame, so the clip repeats without a step"""
    out = list(c)
    for k in range(n):
        i = len(c) - n + k
        t = (k + 1) / n
        t = t * t * (3 - 2 * t)
        out[i] = lerp_frame(c[i], c[0], t)
    return out


def retime(c, length_frames):
    """resampled to length_frames frames (slower or faster, the same motion)"""
    out = []
    for k in range(length_frames):
        x = k * (len(c) - 1) / max(1, length_frames - 1)
        i = min(int(x), len(c) - 2)
        out.append(lerp_frame(c[i], c[i + 1], x - i))
    return out


# ── the rig's own space: pose a frame, read every bone's matrix in the armature, change some, write the bases back
def put(f):
    for n in BONES:
        PB[n].location = f[n][0]
        PB[n].rotation_quaternion = f[n][1]
    bpy.context.view_layer.update()


def take():
    return {n: (PB[n].location.copy(), PB[n].rotation_quaternion.copy()) for n in BONES}


def rot_about(bone, axis, ang):
    """turn one bone (and with it everything below) by ang radians about an armature-space axis through its head"""
    pb = PB[bone]
    h = pb.head.copy()
    R = Matrix.Translation(h) @ Quaternion(axis, ang).to_matrix().to_4x4() @ Matrix.Translation(-h)
    pb.matrix = R @ pb.matrix
    bpy.context.view_layer.update()


UP, FWD, SIDE = Vector((0, 0, 1)), Vector((0, -1, 0)), Vector((1, 0, 0))  # the rig faces -Y, its left is +X


def map_frames(c, fn):
    out = []
    for i, f in enumerate(c):
        put(f)
        fn(i, len(c))
        out.append(take())
    return out


def turned_walk(c, ang):
    """a walk with the legs turned ang about the vertical at the hips and the trunk turned back up the spine (a
    third at each of the three spine joints), so the chest and the head keep facing forward: a sidestep made of a real
    walk (the leg action, the weight shift and the arm swing are the capture's)"""
    def fn(i, n):
        rot_about('pelvis', UP, ang)
        for b in ('spine_01', 'spine_02', 'spine_03'):
            rot_about(b, UP, -ang / 3)
    return map_frames(c, fn)


def spun(c, a, b, turns=1.0, lead=0.0):
    """the whole body turned a full circle (turns) about the vertical between frames a and b, easing in and out, the
    chest a few frames behind the hips (lead: the share of a turn the hips lead by at the peak)"""
    def ease(t):
        t = max(0.0, min(1.0, t))
        return t * t * (3 - 2 * t)

    def fn(i, n):
        t = (i - a) / max(1, b - a)
        th = 2 * math.pi * turns * ease(t)
        lag = 2 * math.pi * turns * (ease(t) - ease(t - 0.12)) * lead
        rot_about('pelvis', UP, th)
        if lag:
            rot_about('spine_02', UP, -lag * 0.5)
            rot_about('spine_03', UP, -lag * 0.5)
    return map_frames(c, fn)


# keyed poses in the rig's space: keys are (frame, {bone: [(axis, degrees), ...]}) on top of a base frame; each bone's
# own channel runs late by its delay (frames), so the hips lead and the hands and the head follow
def keyed(base, length, keys, delay=None):
    delay = delay or {}
    order = [n for n in BONES]

    def at(bone, f):
        ks = [(fr, v.get(bone)) for fr, v in keys]
        pts = [(fr, d) for fr, d in ks if d is not None]
        if not pts:
            return None
        f = f - delay.get(bone, 0)
        if f <= pts[0][0]:
            return pts[0][1]
        if f >= pts[-1][0]:
            return pts[-1][1]
        for k in range(len(pts) - 1):
            (f0, d0), (f1, d1) = pts[k], pts[k + 1]
            if f0 <= f <= f1:
                t = (f - f0) / max(1e-6, f1 - f0)
                s = t * t * (3 - 2 * t)  # C1 at every key; the follow-through comes from the staggered joints
                return [(ax, a0 + (a1 - a0) * s) for (ax, a0), (_, a1) in zip(d0, d1)]
        return pts[-1][1]
    frames = []
    for f in range(length):
        put(base)
        for bone in order:
            v = at(bone, f)
            if v:
                for ax, d in v:
                    rot_about(bone, ax, math.radians(d))
        frames.append(take())
    return frames


def write(name, c, loop=False):
    act = bpy.data.actions.new(name)
    assign(act)
    for i, f in enumerate(c):
        for n in BONES:
            PB[n].location = f[n][0]
            PB[n].rotation_quaternion = f[n][1]
            PB[n].keyframe_insert('location', frame=i)
            PB[n].keyframe_insert('rotation_quaternion', frame=i)
    act.use_fake_user = True
    log(name, len(c), 'frames', '(loop)' if loop else '')
    return act


# ── the clips ───────────────────────────────────────────────────────────────────────────────────────────────────
OUT_CLIPS = {}


def clip(name, c, loop=False):
    OUT_CLIPS[name] = (c, loop)


walk = sample('Walk_Loop')
idle_w = sample('Sword_Idle')
clip('idle', sample('Idle_Loop'), True)
clip('idle_w', idle_w, True)
clip('walk_w', walk, True)
clip('run_w', sample('Jog_Fwd_Loop'), True)
clip('walk_back', rev(walk), True)
clip('strafe_l', turned_walk(walk, math.radians(62)), True)
clip('strafe_r', turned_walk(walk, math.radians(-62)), True)
# the sword: the combo is the library's three-cut combo, each cut with its own recovery (the game cuts a recovery
# short when the next cut comes); in place
A, Ar = in_place(sample('Sword_Regular_A')), in_place(sample('Sword_Regular_A_Rec'))
B, Br = in_place(sample('Sword_Regular_B')), in_place(sample('Sword_Regular_B_Rec'))
C = in_place(sample('Sword_Regular_C'))
clip('slash1', cat(A, Ar))
clip('slash2', cat(B, Br))
clip('slash3', C)
# the heavy and the bosses' spin: the wide cut (B) whipped round a full turn from its wind-up to past its blow, the
# hips leading the chest
spin = spun(cat(B, Br), 2, 16, 1.0, lead=0.35)
clip('heavy', spin)  # (the bosses' spin plays it too)
# a boss's lariat: three turns
clip('lariat', spun(cat(B, B[4:], B[4:], Br), 2, 40, 3.0, lead=0.25))
# the foes: a chop (cleaver, maul) and an overhead throw-like slam; the slinger's throw; the lancer's lunge; the shield
# bearer's shove; the boss's hook
clip('swing', sample('TreeChopping_Loop'))
clip('slam', sample('OverhandThrow'))  # (the bosses' hammer and the slinger's throw play it too)
clip('stab', in_place(sample('Sword_Dash_RM')))
clip('bash', in_place(sample('Shield_Dash_RM')))
clip('haymaker', cat(sample('Melee_Hook'), sample('Melee_Hook_Rec')))
# moving out of the way: the dive roll (trimmed to the rules' dodge), a back step keyed below
clip('roll', sample('Roll', 2, 36))
# hit, hard hit, knocked down (the fall; the game holds the last frame as 'lying'), up again
clip('hit', sample('Hit_Chest'))
clip('hit_hard', sample('Hit_Head'))
knock = sample('Hit_Knockback')
clip('knockdown', knock[:18])
clip('lying', [knock[-1], knock[-1]], True)
clip('getup', sample('LayToIdle'))
# the shrine: kneel, rest (looped), rise; the flask; a chest; a door or a lever
kneeling = sample('Fixing_Kneeling')
clip('kneel', kneeling[:24])
clip('rest', loopify(kneeling[24:131], 12), True)
clip('rise', kneeling[130:])
clip('drink', sample('Consume'))
clip('pickup', sample('Chest_Open'))
clip('push', sample('Push_Loop'))

# the back step: the library's hop (the crouch and the push off, then the landing taken in the knees), quick; the game
# carries the body back
g0 = idle_w[0]
clip('backstep', retime(cat(sample('Jump_Start', 1, 6), sample('Jump_Land', 1, 24)), 16))
# a boss's roar: it gathers (chest down, arms in), then throws the chest up and the arms wide and back, the head back,
# holds it with the whole body straining, and comes back to the guard; the arms and the head trail the chest
Z = [(SIDE, 0)]
Z2 = [(FWD, 0), (SIDE, 0)]
clip('roar', keyed(g0, 36, [
    (0, {'pelvis': Z, 'spine_01': Z, 'spine_03': Z, 'Head': Z, 'clavicle_l': [(FWD, 0)], 'clavicle_r': [(FWD, 0)], 'upperarm_l': Z2, 'upperarm_r': Z2}),
    (7, {'pelvis': [(SIDE, 4)], 'spine_01': [(SIDE, 10)], 'spine_03': [(SIDE, 14)], 'Head': [(SIDE, 8)], 'clavicle_l': [(FWD, -4)], 'clavicle_r': [(FWD, 4)],
         'upperarm_l': [(FWD, -15), (SIDE, -15)], 'upperarm_r': [(FWD, 15), (SIDE, -15)]}),
    (13, {'pelvis': [(SIDE, -4)], 'spine_01': [(SIDE, -14)], 'spine_03': [(SIDE, -26)], 'Head': [(SIDE, -32)], 'clavicle_l': [(FWD, 12)], 'clavicle_r': [(FWD, -12)],
          'upperarm_l': [(FWD, 75), (SIDE, 30)], 'upperarm_r': [(FWD, -75), (SIDE, 30)]}),
    (26, {'pelvis': [(SIDE, -5)], 'spine_01': [(SIDE, -16)], 'spine_03': [(SIDE, -28)], 'Head': [(SIDE, -36)], 'clavicle_l': [(FWD, 14)], 'clavicle_r': [(FWD, -14)],
          'upperarm_l': [(FWD, 80), (SIDE, 34)], 'upperarm_r': [(FWD, -80), (SIDE, 34)]}),
    (35, {'pelvis': Z, 'spine_01': Z, 'spine_03': Z, 'Head': Z, 'clavicle_l': [(FWD, 0)], 'clavicle_r': [(FWD, 0)], 'upperarm_l': Z2, 'upperarm_r': Z2}),
], delay={'spine_03': 1, 'clavicle_l': 2, 'clavicle_r': 2, 'upperarm_l': 3, 'upperarm_r': 3, 'Head': 3}))

# ── out: the mannequin with every clip ──────────────────────────────────────────────────────────────────────────────
for a in list(bpy.data.actions):
    bpy.data.actions.remove(a)
for name, (c, loop) in OUT_CLIPS.items():
    write(name, c, loop)
ARM.animation_data.action = None
put(idle_w[0])
for pb in PB:
    pb.location = Vector()
    pb.rotation_quaternion = Quaternion()
os.makedirs(os.path.dirname(OUT), exist_ok=True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', export_animations=True, export_animation_mode='ACTIONS',
                          export_force_sampling=True, export_frame_step=1, export_optimize_animation_size=False,
                          export_materials='NONE', export_yup=True)
log('wrote', OUT, len(OUT_CLIPS), 'clips')
