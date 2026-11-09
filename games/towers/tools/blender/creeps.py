# Towers' humanoid creeps (ghoul, imp, shade, ogre), headless:
#   flatpak run --filesystem=home org.blender.Blender --background --factory-startup --python \
#     <abs>/games/towers/tools/blender/creeps.py -- <abs out dir> [kind ...]
# kinds: ghoul imp shade ogre (default all four).
#
# Towers' own copy of crawler's creature scripts (more-cartwheel-examples, games/crawler/tools/blender: levelchars.py
# for the ghoul, imp and shade, hordechars.py for the ogre), rebuilt with athletic proportions: a narrower waist under
# defined shoulders, tapered limbs, hands and feet at a real size, a head about a seventh of the stature. The ogre is a
# muscular brute: big through the chest, shoulders, arms and thighs, with a waist. The other creeps
# (skitter, hound, sporeling, bloater) are crawler's files as they are.
#
# The same pipeline as crawler (cwchar_lib.py): primitives skinned to an armature with hand-set weights, posed per
# frame by procedural cycles, evaluated through Blender's armature modifier and written as .cwchar. Clips, in the
# order the game indexes them: 0 walk (6 frames), 1 attack (3: wind-up, strike, recovery). Each kind keeps crawler's
# silhouette from above (the camera looks almost straight down from far away) and its one accent colour: ghoul bone,
# imp ember wings, shade cyan mantle, ogre blood-red mantle. Glows are vertex colours above 1.5. No frame puts a foot
# under the ground or lifts a walker off it (settle, below). Every shape is generated here (no outside art).
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Vector
from cwchar_lib import *
from crawler_lib import mesh, mix, lathe

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if not argv:
    sys.exit("usage: -- <out dir> [kinds]")
OUT = os.path.abspath(argv[0])
kinds = argv[1:]
os.makedirs(OUT, exist_ok=True)
TAU = 2 * math.pi
WALK = 6

def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)

# ───────────────────────── rigs (crawler's) ───────────────────────────
# Characters face -Y with Z up (the writer turns that into the file's +Z forward, Y up). A bone that points straight
# down has local X = world X (rotating it +x swings its tip back), local Y along the bone and local Z = world +Y.

def humanoid_rig(M, hip_z, thigh, shin, torso, neck, arm_up, arm_lo, sx, hx):
    M.bone("hips", None, (0, 0, hip_z - 0.05), (0, 0, hip_z + 0.1))
    M.bone("spine", "hips", (0, 0, hip_z + 0.1), (0, 0, hip_z + torso * 0.5))
    M.bone("chest", "spine", (0, 0, hip_z + torso * 0.5), (0, 0, hip_z + torso))
    M.bone("head", "chest", (0, 0, hip_z + torso), (0, 0, hip_z + torso + neck))
    for s, side in ((1, "L"), (-1, "R")):
        sz = hip_z + torso - 0.08
        M.bone("uarm" + side, "chest", (s * sx, 0, sz), (s * sx, 0, sz - arm_up))
        M.bone("farm" + side, "uarm" + side, (s * sx, 0, sz - arm_up), (s * sx, 0, sz - arm_up - arm_lo))
        M.bone("thigh" + side, "hips", (s * hx, 0, hip_z), (s * hx, 0, hip_z - thigh))
        M.bone("shin" + side, "thigh" + side, (s * hx, 0, hip_z - thigh), (s * hx, 0, hip_z - thigh - shin))
        M.bone("foot" + side, "shin" + side, (s * hx, 0, hip_z - thigh - shin), (s * hx, -0.18, hip_z - thigh - shin))

def walk_pose(f, n, A_leg=0.7, K=0.9, arm=0.5, bounce=0.05, spine_lean=0.0, twist=0.15, arm_bend=0.4, arm_fwd=0.0, lean_extra=0.0):
    ph = TAU * f / n
    p = {}
    sL = math.sin(ph); sR = math.sin(ph + math.pi)
    def leg(side, s):
        fold = K * max(0.0, math.sin(ph + (0.9 if side == "L" else 0.9 + math.pi)))
        return {"thigh" + side: (-A_leg * s, 0, 0), "shin" + side: (fold + 0.1, 0, 0), "foot" + side: (-0.3 * fold + 0.25 * s, 0, 0)}
    p.update(leg("L", sL)); p.update(leg("R", sR))
    bob = bounce * abs(math.sin(ph))
    p["hips"] = (spine_lean * 0.4, 0, twist * 0.4 * math.sin(ph), 0, bob, 0)
    p["spine"] = (spine_lean * 0.4, 0, -twist * math.sin(ph) * 0.8)
    p["chest"] = (spine_lean * 0.2 + lean_extra, 0, -twist * 0.5 * math.sin(ph))
    p["head"] = (-spine_lean * 0.6 - lean_extra - 0.05, 0, twist * 1.1 * math.sin(ph))
    p["uarmL"] = (arm * sR - arm_fwd, 0.0, 0); p["farmL"] = (arm_bend + 0.2 * max(0, sR) * arm - arm_fwd * 0.3, 0, 0)
    p["uarmR"] = (arm * sL - arm_fwd, 0.0, 0); p["farmR"] = (arm_bend + 0.2 * max(0, sL) * arm - arm_fwd * 0.3, 0, 0)
    return p

def stance(crouch=0.0, spread=0.0):
    p = {}
    for side, s in (("L", 1), ("R", -1)):
        p["thigh" + side] = (-crouch, 0, s * spread)
        p["shin" + side] = (crouch * 1.8, 0, 0)
        p["foot" + side] = (-crouch * 0.8, 0, 0)
    p["hips"] = (0, 0, 0, 0, 0, 0)
    return p

def torso_weights(hz, a, b, c, d):
    return lambda p: {"hips": 1 - smoothstep(hz + a, hz + b, p.z),
                      "spine": smoothstep(hz + a, hz + b, p.z) * (1 - smoothstep(hz + c, hz + d, p.z)),
                      "chest": smoothstep(hz + c, hz + d, p.z)}

# ───────────────────────── the ghoul ──────────────────────────────────

def make_ghoul():
    """a hunched skeleton in a black rag cloak, its bone-yellow skull, arms and a row of vertebra spikes showing from
    above, bone-yellow eyes, a notched blade fused to the right arm. Crawler's, with the skull a little lower crown to
    jaw (the same from above) so the head is a seventh of the figure"""
    M = Model()
    hz = 0.98
    humanoid_rig(M, hz, 0.48, 0.48, 0.62, 0.2, 0.34, 0.34, 0.22, 0.11)
    bone_c = (0.86, 0.76, 0.44); old = (0.56, 0.46, 0.26); cloth = (0.2, 0.18, 0.17); cloth2 = (0.13, 0.12, 0.11)
    rust = (0.36, 0.2, 0.12); steel = (0.46, 0.44, 0.42); eye = (3.0, 2.5, 0.5); void = (0.03, 0.025, 0.02)
    tw = torso_weights(hz, 0.05, 0.25, 0.3, 0.5)
    M.limb("spine", (0, 0.02, hz + 0.02), (0, 0.04, hz + 0.4), 0.035, 0.035, old, seg=5, w=tw)          # the spine
    M.ell("hips", (0, 0.0, hz + 0.0), (0.14, 0.09, 0.07), old, seg=(7, 4))                              # pelvis
    for k in range(5):                                                                                  # the ribs
        z = hz + 0.3 + k * 0.055
        r = 0.15 - abs(k - 2) * 0.012
        M.ell("chest", (0, -0.02, z), (r, 0.1, 0.018), bone_c, seg=(8, 3))
    M.box("chest", (0, -0.1, hz + 0.42), (0.04, 0.03, 0.22), bone_c, bevel=0.01)                       # sternum
    M.ell("chest", (0, 0, hz + 0.6), (0.24, 0.1, 0.04), bone_c, seg=(8, 3))                             # collarbones
    M.ell("head", (0, -0.03, hz + 0.745), (0.13, 0.15, 0.1), bone_c, seg=(8, 6))                        # skull, bare to the sky
    M.ell("head", (0, -0.03, hz + 0.815), (0.07, 0.1, 0.035), mix(bone_c, (1, 1, 1), 0.2), seg=(6, 3))  # its pale crown
    M.box("head", (0, -0.11, hz + 0.665), (0.11, 0.07, 0.04), bone_c, bevel=0.012)                      # jaw
    for s in (1, -1):
        M.ell("head", (s * 0.052, -0.14, hz + 0.765), (0.045, 0.03, 0.03), void, seg=(5, 4))             # sockets
        M.ell("head", (s * 0.052, -0.155, hz + 0.772), (0.032, 0.02, 0.024), eye, seg=(5, 4), emissive=True)
    # the rag cloak: a cowl fallen behind the skull, a cape over the back, a rag of strips round the hips
    M.ell("chest", (0, 0.1, hz + 0.64), (0.2, 0.12, 0.1), cloth, seg=(8, 5), noise_amt=0.25)
    M.ell("chest", (0, 0.08, hz + 0.48), (0.27, 0.14, 0.17), cloth, seg=(8, 5), noise_amt=0.3)
    for k in range(5):                                                                                  # vertebra spikes through the rag
        z = hz + 0.62 - 0.1 * k
        M.spike("chest" if k < 3 else "spine", (0, 0.16 + 0.02 * k, z), (0, 0.3 + 0.02 * k, z + 0.08), 0.035, bone_c, seg=4)
    for k in range(6):
        a = TAU * k / 6 + 0.5
        top = (0.15 * math.cos(a), 0.03 + 0.12 * math.sin(a), hz + 0.1)
        bot = (0.2 * math.cos(a), 0.05 + 0.16 * math.sin(a), hz - 0.3 - 0.12 * ((k * 5) % 3))
        M.limb("hips", top, bot, 0.07, 0.025, cloth2 if k % 2 else cloth, seg=4, noise_amt=0.35,
               w={"hips": 0.5, "thighL": 0.5} if top[0] > 0 else {"hips": 0.5, "thighR": 0.5})
    for s, side in ((1, "L"), (-1, "R")):
        sz = hz + 0.62 - 0.08
        M.ell("chest", (s * 0.22, 0, sz + 0.03), (0.07, 0.07, 0.06), bone_c, seg=(6, 4))                   # shoulder knobs
        M.spike("chest", (s * 0.24, 0.02, sz + 0.06), (s * 0.36, 0.06, sz + 0.16), 0.03, bone_c, seg=4)
        M.limb("uarm" + side, (s * 0.22, 0, sz), (s * 0.22, 0, sz - 0.34), 0.035, 0.03, bone_c, seg=5)
        M.limb("farm" + side, (s * 0.22, 0, sz - 0.34), (s * 0.22, 0, sz - 0.68), 0.03, 0.025, bone_c, seg=5)
        M.limb("thigh" + side, (s * 0.11, 0, hz), (s * 0.11, 0, hz - 0.48), 0.035, 0.03, old, seg=5)
        M.ell("shin" + side, (s * 0.11, 0, hz - 0.48), (0.04, 0.04, 0.04), old, seg=(5, 4))
        M.limb("shin" + side, (s * 0.11, 0, hz - 0.48), (s * 0.11, 0, hz - 0.92), 0.03, 0.025, old, seg=5)
        M.box("foot" + side, (s * 0.11, -0.07, 0.03), (0.07, 0.2, 0.05), old, bevel=0.015)
    for k in (-1, 0, 1):                                                                                # claw fingers
        M.spike("farmL", (0.22 + k * 0.02, -0.02, hz + 0.54 - 0.68), (0.22 + k * 0.035, -0.06, hz + 0.54 - 0.82), 0.014, bone_c, seg=4)
    # the notched blade fused to the right forearm, pointing down past the hand
    M.box("farmR", (-0.22, -0.02, hz + 0.54 - 0.6), (0.07, 0.05, 0.1), rust, bevel=0.01)
    M.box("farmR", (-0.22, -0.02, hz + 0.54 - 0.98), (0.025, 0.13, 0.62), steel, bevel=0.005)
    M.spike("farmR", (-0.22, -0.02, hz + 0.54 - 1.28), (-0.22, -0.04, hz + 0.54 - 1.4), 0.05, steel, seg=4)
    return M

def ghoul_poses():
    walk = []
    for f in range(WALK):
        p = walk_pose(f, WALK, A_leg=0.45, K=0.6, arm=0.15, bounce=0.035, spine_lean=0.45, twist=0.25, arm_bend=0.15, arm_fwd=0.15)
        ph = TAU * f / WALK
        p["head"] = (p["head"][0] + 0.25, 0.15 * math.sin(ph), p["head"][2])      # a stiff, lolling head
        p["uarmR"] = (0.25, 0, 0.15); p["farmR"] = (-0.25, 0, 0)               # the blade arm hangs, dragging
        walk.append(p)
    attack = []
    # a backhand slash: the blade arm thrown far back across the body (the tell), swept out across the front, then
    # hanging forward
    for twist, uarm, farm, lean, crouch in (
            (-0.7, (-0.3, 0, 1.2), (-0.6, 0, 0), 0.2, 0.15),
            (0.75, (-1.45, 0, -0.9), (-0.1, 0, 0), 0.5, 0.3),
            (0.35, (-0.9, 0, -0.4), (-0.3, 0, 0), 0.4, 0.2)):
        p = stance(crouch, 0.1)
        p["thighL"] = (-crouch - 0.3, 0, 0.1)
        p["hips"] = (lean * 0.3, 0, twist * 0.3, 0, -0.04, 0)
        p["spine"] = (lean * 0.4, 0, twist * 0.6)
        p["chest"] = (lean * 0.3, 0, twist * 0.4)
        p["head"] = (-lean * 0.5, 0, -twist * 0.4)
        p["uarmR"] = uarm; p["farmR"] = farm
        p["uarmL"] = (-0.5, 0, 0.3); p["farmL"] = (-0.4, 0, 0)
        attack.append(p)
    return [walk, attack]

# ───────────────────────── the imp ────────────────────────────────────

def make_imp():
    """a wiry black-stone devil: long shins on a lean frame, a narrow waist under a deep chest, ember-red bat wings
    spread over its back, a barbed tail, horns, glowing ember cracks and eyes"""
    M = Model()
    hz = 0.72
    humanoid_rig(M, hz, 0.36, 0.38, 0.46, 0.16, 0.28, 0.3, 0.17, 0.09)
    for s, side in ((1, "L"), (-1, "R")):
        M.bone("wing" + side, "chest", (s * 0.1, 0.12, hz + 0.4), (s * 0.7, 0.3, hz + 0.8))
    stone = (0.21, 0.19, 0.19); stone2 = (0.14, 0.12, 0.12); ember = (0.93, 0.24, 0.07); dember = (0.55, 0.12, 0.05)
    crack = (5.0, 1.4, 0.3); horn = (0.42, 0.38, 0.34)
    tw = torso_weights(hz, 0.02, 0.2, 0.22, 0.38)
    # the trunk, one turned profile: hips, a narrow waist, a deep chest
    lathe(M, "spine", [(0.0, -0.09), (0.085, -0.06), (0.105, 0.0), (0.1, 0.07), (0.088, 0.16), (0.1, 0.24), (0.145, 0.32), (0.165, 0.39),
                       (0.15, 0.45), (0.09, 0.5), (0.0, 0.52)], stone, seg=10, w=tw, c=(0, 0.0, hz), sy=0.72, noise_amt=0.35)
    for s in (1, -1):
        M.ell("chest", (s * 0.075, -0.085, hz + 0.37), (0.07, 0.04, 0.06), stone2, seg=(6, 4), noise_amt=0.3)  # its pectorals
        M.ell("chest", (s * 0.16, 0, hz + 0.41), (0.055, 0.055, 0.05), stone, seg=(6, 4), noise_amt=0.3)      # shoulder caps
    M.ell("chest", (0, 0.05, hz + 0.47), (0.04, 0.1, 0.025), crack, seg=(5, 3), emissive=True)          # a glowing crack down the back
    M.ell("chest", (0, -0.12, hz + 0.34), (0.045, 0.015, 0.07), crack, seg=(5, 3), emissive=True)
    M.ell("hips", (0, 0.02, hz - 0.01), (0.1, 0.07, 0.07), stone2, seg=(7, 5), noise_amt=0.35)                 # a dark loin
    M.ell("head", (0, -0.06, hz + 0.56), (0.09, 0.105, 0.09), stone, seg=(8, 5), noise_amt=0.3)
    M.box("head", (0, -0.15, hz + 0.51), (0.08, 0.06, 0.045), stone2, bevel=0.012)
    for s in (1, -1):
        M.ell("head", (s * 0.04, -0.145, hz + 0.585), (0.026, 0.018, 0.018), crack, seg=(4, 3), emissive=True)
        M.spike("head", (s * 0.05, -0.02, hz + 0.62), (s * 0.19, 0.1, hz + 0.8), 0.04, horn, seg=5)
    # the tail: a curl behind it ending in an ember barb
    tpts = [(0, 0.09, hz - 0.02), (0.04, 0.34, hz - 0.22), (0.0, 0.56, hz - 0.4), (-0.12, 0.7, hz - 0.46)]
    for k in range(3):
        M.limb("hips", tpts[k], tpts[k + 1], 0.035 - 0.008 * k, 0.027 - 0.008 * k, stone, seg=4)
    M.spike("hips", (-0.12, 0.7, hz - 0.46), (-0.24, 0.82, hz - 0.46), 0.06, ember, seg=3)
    for s, side in ((1, "L"), (-1, "R")):                                                                # bat wings, spread back over it
        r0 = Vector((s * 0.1, 0.12, hz + 0.4))
        tips = [Vector((s * (0.45 + 0.15 * k), 0.22 + 0.12 * k, hz + 0.75 - 0.25 * k)) for k in range(3)]
        for t in tips:
            M.limb("wing" + side, r0, t, 0.04, 0.01, stone2, seg=4, noise_amt=0.3)
        # the membrane between the fingers, both faces (the game culls back faces)
        pts = [r0] + tips + [Vector((s * 0.38, 0.28, hz + 0.2))]
        fan = [(0, 1, 2), (0, 2, 3), (0, 3, 4)]
        faces = [f if s > 0 else f[::-1] for f in fan]
        fn = (pts[1] - pts[0]).cross(pts[2] - pts[0]).normalized() * (1 if s > 0 else -1)
        mesh(M, "wing" + side, [tuple(p) for p in pts], faces, ember, noise_amt=0.2)
        mesh(M, "wing" + side, [tuple(p - fn * 0.015) for p in pts], [f[::-1] for f in faces], ember, noise_amt=0.2)
        M.ell("wing" + side, (s * 0.28, 0.22, hz + 0.5), (0.04, 0.07, 0.04), dember, seg=(4, 3))
        sz = hz + 0.46 - 0.08
        M.limb("uarm" + side, (s * 0.17, 0, sz), (s * 0.17, 0, sz - 0.28), 0.042, 0.03, stone, seg=6)
        M.ell("uarm" + side, (s * 0.17, -0.01, sz - 0.1), (0.04, 0.04, 0.08), stone, seg=(6, 4), noise_amt=0.3)   # biceps
        M.limb("farm" + side, (s * 0.17, 0, sz - 0.28), (s * 0.17, 0, sz - 0.56), 0.034, 0.024, stone, seg=6)
        M.ell("farm" + side, (s * 0.17, -0.005, sz - 0.6), (0.032, 0.03, 0.04), stone2, seg=(5, 4))              # the hand
        for c in (-1, 0, 1):
            M.spike("farm" + side, (s * 0.17 + c * 0.018, -0.01, sz - 0.62), (s * 0.17 + c * 0.03, -0.05, sz - 0.7), 0.012, horn, seg=4)
        M.limb("thigh" + side, (s * 0.09, 0, hz), (s * 0.09, -0.12, hz - 0.34), 0.06, 0.038, stone, seg=6)
        M.limb("shin" + side, (s * 0.09, -0.12, hz - 0.34), (s * 0.09, 0.04, 0.05), 0.04, 0.026, stone, seg=6)  # digitigrade
        M.box("foot" + side, (s * 0.09, -0.035, 0.025), (0.06, 0.15, 0.04), stone2, bevel=0.01)
    return M

def imp_poses():
    walk = []
    # a hop: crouch, spring (wings half open), glide, land, gathering again; the forearms work with it
    for crouch, rise, wing, lean, fa in ((0.5, 0.0, 0.0, 0.3, -0.5), (0.0, 0.35, -0.6, 0.45, -0.25), (-0.2, 0.7, -0.9, 0.55, -0.1),
                                         (-0.1, 0.6, -0.8, 0.5, -0.15), (0.1, 0.25, -0.4, 0.4, -0.3), (0.4, 0.02, -0.1, 0.3, -0.45)):
        p = stance(max(crouch, 0) * 0.9, 0.1)
        p["hips"] = (lean * 0.3, 0, 0, 0, rise - max(crouch, 0) * 0.15, 0)
        p["spine"] = (lean * 0.4, 0, 0); p["chest"] = (lean * 0.3, 0, 0); p["head"] = (-lean * 0.6, 0, 0)
        if crouch < 0: p["thighL"] = (0.6, 0, 0.1); p["thighR"] = (0.6, 0, -0.1); p["shinL"] = (0.3, 0, 0); p["shinR"] = (0.3, 0, 0)
        p["wingL"] = (0, 0, wing); p["wingR"] = (0, 0, -wing)
        p["uarmL"] = (-0.6, 0, 0.3); p["uarmR"] = (-0.6, 0, -0.3)
        p["farmL"] = (fa, 0, 0); p["farmR"] = (fa, 0, 0)
        walk.append(p)
    attack = []
    # the dive: at the top of a hop, wings flared wide (the tell), dropping claws first, crouched on landing
    for rise, wing, lean, arm, fa in ((0.8, -1.3, -0.2, -2.0, -0.2), (0.25, -0.3, 0.9, -1.4, 0.0), (0.0, -0.1, 0.4, -0.6, -0.4)):
        p = stance(0.3 if rise < 0.1 else 0.0, 0.1)
        p["hips"] = (lean * 0.3, 0, 0, 0, rise, 0)
        p["spine"] = (lean * 0.4, 0, 0); p["head"] = (-lean * 0.5, 0, 0)
        p["wingL"] = (0, 0, wing); p["wingR"] = (0, 0, -wing)
        p["uarmL"] = (arm, 0, 0.3); p["uarmR"] = (arm, 0, -0.3)
        p["farmL"] = (fa, 0, 0); p["farmR"] = (fa, 0, 0)
        attack.append(p)
    return [walk, attack]

# ───────────────────────── the shade ──────────────────────────────────

def make_shade():
    """a gliding black-robed wraith: a slim robe close at the waist and flaring to a hem of tatters, a pale cyan mantle
    over its shoulders, two cyan points of light under its hood, slender sleeves and clawed hands, a trailing tail of
    rags"""
    M = Model()
    hz = 1.0
    humanoid_rig(M, hz, 0.4, 0.4, 0.6, 0.18, 0.33, 0.33, 0.2, 0.12)
    wool = (0.17, 0.17, 0.2); wool2 = (0.11, 0.11, 0.14); cyan = (0.58, 0.95, 0.93); dcyan = (0.3, 0.55, 0.56)
    void = (0.015, 0.02, 0.025); eye = (1.2, 3.2, 3.0); hand = (0.4, 0.42, 0.44)
    tw = torso_weights(hz, 0.05, 0.25, 0.3, 0.5)
    # the robe: close at the waist, flaring to the hem (no legs: the thighs carry the tatters so they sway), matte wool
    M.limb("spine", (0, 0.02, hz + 0.42), (0, 0.03, hz + 0.18), 0.125, 0.13, wool, w=tw, seg=8, noise_amt=0.3)
    M.limb("spine", (0, 0.03, hz + 0.2), (0, 0.04, hz - 0.1), 0.13, 0.2, wool, w=tw, seg=8, noise_amt=0.3)
    for k in range(8):
        a = TAU * k / 8 + 0.2
        top = (0.18 * math.cos(a), 0.04 + 0.17 * math.sin(a), hz - 0.05)
        bot = (0.27 * math.cos(a), 0.06 + 0.26 * math.sin(a), 0.04 + 0.08 * ((k * 5) % 3))
        M.limb("hips", top, bot, 0.075, 0.015, wool2 if k % 2 else wool, seg=4, noise_amt=0.35,
               w={"thighL": 0.7, "hips": 0.3} if top[0] > 0 else {"thighR": 0.7, "hips": 0.3})
    for k, x in enumerate((-0.08, 0.08, 0.0)):                                                       # the trailing tail of rags
        M.limb("hips", (x, 0.17, hz - 0.1), (x * 1.8, 0.7 + 0.1 * k, 0.12 + 0.06 * k), 0.07, 0.012, wool2 if k % 2 else wool, seg=4, noise_amt=0.35,
               w={"hips": 0.6, "thighL": 0.4} if k % 2 else {"hips": 0.6, "thighR": 0.4})
    M.ell("chest", (0, 0.02, hz + 0.47), (0.19, 0.13, 0.15), wool, seg=(8, 5), noise_amt=0.3)        # the chest, narrowing to the waist
    # the cyan mantle over the shoulders: from above a pale collar round the dark hood
    M.ell("chest", (0, 0.04, hz + 0.6), (0.3, 0.22, 0.075), cyan, seg=(10, 4), noise_amt=0.2)
    for k in range(5):                                                                                # its ragged edge
        a = math.pi * (k + 0.5) / 5
        M.spike("chest", (0.28 * math.cos(a), 0.04 + 0.2 * math.sin(a), hz + 0.58), (0.35 * math.cos(a), 0.06 + 0.3 * math.sin(a), hz + 0.42), 0.05, dcyan, seg=3)
    # the hood, a black void in its mouth, two cold points of light
    M.ell("head", (0, 0.02, hz + 0.76), (0.14, 0.16, 0.135), wool2, seg=(9, 6), noise_amt=0.3)
    M.limb("head", (0, 0.08, hz + 0.86), (0, 0.24, hz + 0.93), 0.07, 0.008, wool2, seg=5)            # the hood's point
    M.ell("head", (0, -0.09, hz + 0.74), (0.085, 0.07, 0.09), void, seg=(7, 4))
    for s in (1, -1):
        M.ell("head", (s * 0.038, -0.14, hz + 0.775), (0.028, 0.016, 0.02), eye, seg=(4, 3), emissive=True)
    for s, side in ((1, "L"), (-1, "R")):
        sz = hz + 0.6 - 0.08
        M.limb("uarm" + side, (s * 0.2, 0, sz), (s * 0.21, 0, sz - 0.33), 0.06, 0.055, wool, seg=6, noise_amt=0.3)   # slender sleeves
        M.limb("farm" + side, (s * 0.21, 0, sz - 0.33), (s * 0.21, 0, sz - 0.6), 0.055, 0.085, wool2, seg=6, noise_amt=0.3)
        M.limb("farm" + side, (s * 0.21, 0, sz - 0.58), (s * 0.21, -0.01, sz - 0.68), 0.024, 0.018, hand, seg=5)
        for k in (-1, 0, 1):
            M.spike("farm" + side, (s * 0.21 + k * 0.016, -0.01, sz - 0.67), (s * 0.21 + k * 0.026, -0.04, sz - 0.77), 0.011, cyan, seg=4)
    return M

def shade_poses():
    walk = []
    for f in range(WALK):
        ph = TAU * f / WALK
        # it glides a hand's width off the floor, swaying; the tatters swing with the "legs"; the hands drift
        p = {"hips": (0.05 * math.sin(ph), 0, 0.12 * math.sin(ph), 0, 0.18 + 0.05 * math.sin(ph * 2), 0),
             "spine": (0.15, 0, -0.08 * math.sin(ph)), "chest": (0.1, 0, 0), "head": (-0.1, 0, 0.1 * math.sin(ph)),
             "thighL": (0.3 + 0.15 * math.sin(ph), 0, 0), "thighR": (0.3 - 0.15 * math.sin(ph), 0, 0),
             "uarmL": (-0.15 + 0.1 * math.sin(ph), 0, 0.15), "uarmR": (-0.15 - 0.1 * math.sin(ph), 0, -0.15),
             "farmL": (-0.3 - 0.12 * math.sin(ph), 0, 0), "farmR": (-0.3 + 0.12 * math.sin(ph), 0, 0)}
        walk.append(p)
    attack = []
    # the lunge: drawn back and risen with the hands up (the tell), rushing forward with the hands out, settling
    for rise, lean, arm in ((0.6, -0.35, -2.4), (0.2, 0.7, -1.6), (0.3, 0.3, -0.8)):
        p = {"hips": (lean * 0.3, 0, 0, 0, rise, 0), "spine": (lean * 0.4, 0, 0), "chest": (lean * 0.3, 0, 0), "head": (-lean * 0.5, 0, 0),
             "thighL": (0.6 * max(lean, 0) + 0.2, 0, 0), "thighR": (0.6 * max(lean, 0) + 0.2, 0, 0),
             "uarmL": (arm, 0, 0.25), "uarmR": (arm, 0, -0.25), "farmL": (-0.2, 0, 0), "farmR": (-0.2, 0, 0)}
        attack.append(p)
    return [walk, attack]

# ───────────────────────── the ogre ───────────────────────────────────

def make_ogre():
    """a huge dark-hided muscular brute: a deep chest and broad back over a waist, broad shoulders, strong arms
    and thighs that taper to the wrists and knees, a head a seventh of its height; a blood-red hide
    mantle over its shoulders and back, iron pauldrons, bone horns, a spiked club in the right fist, red eyes"""
    M = Model()
    hz = 1.0
    humanoid_rig(M, hz, 0.5, 0.48, 0.78, 0.2, 0.44, 0.44, 0.44, 0.17)
    hide = (0.3, 0.26, 0.23); hide2 = (0.22, 0.19, 0.17); plate = (0.3, 0.3, 0.32); dplate = (0.17, 0.17, 0.19)
    blood = (0.62, 0.04, 0.05); dblood = (0.38, 0.02, 0.03); bone_c = (0.8, 0.74, 0.6); eye = (5.0, 0.6, 0.4)
    dark = (0.12, 0.09, 0.08); wood = (0.3, 0.2, 0.12); strap = (0.14, 0.1, 0.08)
    torso_w = lambda p: {"hips": 1 - smoothstep(hz + 0.05, hz + 0.3, p.z), "spine": smoothstep(hz + 0.05, hz + 0.3, p.z) * (1 - smoothstep(hz + 0.35, hz + 0.6, p.z)), "chest": smoothstep(hz + 0.35, hz + 0.6, p.z)}
    # the trunk: a waist the width of the hips, flaring through the lats into a deep chest
    lathe(M, "spine", [(0.0, -0.17), (0.16, -0.13), (0.21, -0.03), (0.21, 0.07), (0.19, 0.18), (0.195, 0.27), (0.25, 0.38), (0.34, 0.5),
                       (0.41, 0.6), (0.4, 0.7), (0.3, 0.78), (0.15, 0.84), (0.0, 0.86)], hide, seg=12, w=torso_w, c=(0, 0.01, hz), sy=0.66)
    for k in range(3):                                                                                         # the abdomen's rows
        for s in (1, -1):
            M.ell("spine", (s * 0.055, -0.12, hz + 0.17 + 0.1 * k), (0.05, 0.03, 0.045), hide2, w=torso_w, seg=(6, 4))
    for s in (1, -1):
        M.ell("chest", (s * 0.15, -0.16, hz + 0.62), (0.16, 0.08, 0.11), hide2, seg=(8, 5))                    # pectorals
    M.ell("chest", (0, 0.08, hz + 0.78), (0.26, 0.15, 0.09), hide, seg=(8, 5))                                 # trapezius
    M.box("chest", (0.0, 0.0, hz + 0.6), (0.82, 0.5, 0.07), strap, rot=(0, 0.6, 0), bevel=0.02)               # a strap across it
    # the blood-red mantle: a hide thrown over the shoulders, hanging down the back in a ragged edge
    M.ell("chest", (0, 0.1, hz + 0.82), (0.42, 0.26, 0.1), blood, seg=(10, 5), noise_amt=0.25)
    for k in range(6):
        x = -0.34 + 0.136 * k
        M.spike("chest", (x, 0.27, hz + 0.76), (x * 1.1, 0.36, hz + 0.36 + 0.06 * (k % 2)), 0.09, dblood if k % 2 else blood, seg=3)
    M.ell("hips", (0, 0, hz - 0.04), (0.205, 0.15, 0.12), dark, seg=(8, 6))                                    # the hips in a dark wrap
    M.box("hips", (0, 0, hz + 0.08), (0.46, 0.36, 0.08), strap, bevel=0.025)                                   # the belt
    hc = hz + 0.9
    M.ell("head", (0, -0.07, hc), (0.13, 0.15, 0.13), hide2, seg=(9, 6))
    M.box("head", (0, -0.18, hc - 0.07), (0.2, 0.09, 0.09), plate, bevel=0.025)                                # jaw plate
    M.ell("head", (0, -0.06, hc + 0.11), (0.05, 0.11, 0.04), blood, seg=(6, 4))                                # a red crest of hide
    for s in (1, -1):
        M.ell("head", (s * 0.055, -0.19, hc + 0.035), (0.035, 0.025, 0.025), eye, seg=(5, 4), emissive=True)
        M.spike("head", (s * 0.1, -0.03, hc + 0.08), (s * 0.32, -0.12, hc + 0.34), 0.06, bone_c, seg=6)
    for s, side in ((1, "L"), (-1, "R")):
        sz = hz + 0.78 - 0.08
        M.ell("chest", (s * 0.42, 0, sz + 0.03), (0.15, 0.15, 0.14), hide, seg=(8, 6))                         # deltoid
        M.ell("chest", (s * 0.44, 0, sz + 0.09), (0.15, 0.15, 0.08), plate, seg=(8, 6))                        # pauldron
        M.ell("chest", (s * 0.44, 0, sz + 0.05), (0.16, 0.16, 0.03), dplate, seg=(8, 3))                       # its rim
        M.spike("chest", (s * 0.46, 0, sz + 0.15), (s * 0.56, 0.02, sz + 0.33), 0.05, bone_c, seg=5)
        M.limb("uarm" + side, (s * 0.44, 0, sz), (s * 0.44, 0, sz - 0.44), 0.105, 0.075, hide, seg=8)
        M.ell("uarm" + side, (s * 0.44, -0.02, sz - 0.17), (0.1, 0.09, 0.16), hide, seg=(8, 6))               # biceps
        M.ell("uarm" + side, (s * 0.44, 0.025, sz - 0.2), (0.08, 0.075, 0.14), hide2, seg=(8, 6))               # triceps
        M.ell("farm" + side, (s * 0.44, 0, sz - 0.44), (0.08, 0.08, 0.08), hide, seg=(7, 5))                    # elbow
        M.limb("farm" + side, (s * 0.44, 0, sz - 0.44), (s * 0.44, 0, sz - 0.84), 0.085, 0.06, hide, seg=8)
        M.ell("farm" + side, (s * 0.44, -0.01, sz - 0.55), (0.1, 0.1, 0.13), hide, seg=(8, 5))                # the forearm's swell
        M.limb("farm" + side, (s * 0.44, 0, sz - 0.68), (s * 0.44, 0, sz - 0.82), 0.075, 0.068, strap, seg=8)  # wrist wraps
        M.ell("farm" + side, (s * 0.44, -0.015, sz - 0.92), (0.075, 0.085, 0.095), hide2, seg=(8, 6))          # fist
        M.limb("thigh" + side, (s * 0.17, 0, hz), (s * 0.17, 0, hz - 0.5), 0.13, 0.085, hide, seg=8)
        M.ell("thigh" + side, (s * 0.17, -0.03, hz - 0.2), (0.13, 0.13, 0.21), hide, seg=(8, 6))               # quadriceps
        M.ell("shin" + side, (s * 0.17, 0, hz - 0.5), (0.085, 0.085, 0.085), hide, seg=(7, 5))                 # knee
        M.limb("shin" + side, (s * 0.17, 0, hz - 0.5), (s * 0.17, 0, hz - 0.92), 0.085, 0.06, hide, seg=8)
        M.ell("shin" + side, (s * 0.17, 0.035, hz - 0.64), (0.095, 0.1, 0.14), hide, seg=(8, 5))              # calf
        M.box("foot" + side, (s * 0.17, -0.08, 0.045), (0.16, 0.29, 0.09), dark, bevel=0.025)
    # the club, held forward in the right fist: a knotted log with iron bands and bone spikes
    fz = hz + 0.78 - 0.08 - 0.92
    a = Vector((-0.44, 0.12, fz)); b = Vector((-0.48, -0.92, fz + 0.05))
    M.limb("farmR", a, b, 0.055, 0.13, wood, seg=7, noise_amt=0.3)
    for t in (0.55, 0.85):
        p = a.lerp(b, t)
        M.limb("farmR", p - Vector((0, 0.03, 0)), p + Vector((0, 0.03, 0)), 0.055 + 0.09 * t, 0.055 + 0.09 * t, plate, seg=7)
    for k in range(5):
        p = a.lerp(b, 0.62 + 0.07 * k)
        ang = 1.3 * k
        d = Vector((math.cos(ang), 0, math.sin(ang)))
        M.spike("farmR", p + d * 0.09, p + d * 0.22, 0.032, bone_c, seg=4)
    return M

def ogre_attack():
    # an overhead smash: both fists high over the head, leaning back; then slammed down in front with the whole body
    # folded over bent knees; then the fists hauled back up
    out = []
    for lean, arm, farm, crouch, drop in (
            (-0.16, -2.95, -0.5, 0.15, 0.03),  # wind-up
            (0.75, -0.75, -0.1, 0.65, -0.16),  # strike
            (0.35, -1.3, -0.4, 0.35, -0.07)):  # recovery
        p = stance(crouch, 0.18)
        p["hips"] = (lean * 0.35, 0, 0, 0, drop, 0)
        p["spine"] = (lean * 0.45, 0, 0)
        p["chest"] = (lean * 0.35, 0, 0)
        p["head"] = (-lean * 0.6, 0, 0)
        for side, s in (("L", 1), ("R", -1)):
            p["uarm" + side] = (arm, 0, -s * 0.25); p["farm" + side] = (farm, 0, 0)
        out.append(p)
    return out

def ogre_poses():
    walk = [walk_pose(f, WALK, A_leg=0.5, K=0.7, arm=0.35, bounce=0.07, spine_lean=0.18, twist=0.3, arm_bend=0.4) for f in range(WALK)]
    return [walk, ogre_attack()]

CHARS = {"ghoul": (make_ghoul, ghoul_poses), "imp": (make_imp, imp_poses), "shade": (make_shade, shade_poses), "ogre": (make_ogre, ogre_poses)}
AO_DEEP = (0.92, 1.5, 0.12)   # crevices between limbs, under arms and heads go dark (bake_ao)

# Grounding: a walker (the ghoul, the ogre) has a foot on the floor in every frame, so each frame is moved until its
# lowest point is on the floor; the imp's hop is in the air on purpose and the shade glides, so a frame of theirs is
# only lifted when it would put a point under the floor.
PLANTED = {"ghoul", "ogre"}

def settle(clip, planted):
    for (pos, nrm, lv) in clip:
        mz = min(p.z for p in pos)
        if mz < 0 or planted:
            for p in pos:
                p.z -= mz

for name in (kinds or list(CHARS)):
    mk, pf = CHARS[name]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    M = mk()
    ob, ao = build_object(M, name)
    pose_frame(ao, {})
    ao_col = bake_ao(ob, M, 48, *AO_DEEP)
    clips = [bake_clip(ob, M, ao, c) for c in pf()]
    for c in clips:
        settle(c, name in PLANTED)
    size = write_cwchar(os.path.join(OUT, name + ".cwchar"), M, ao_col, clips, ob)
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    print("CREEP %s: %d verts, %d tris, clips %s, %d bytes" % (name, len(M.v), tris, [len(c) for c in clips], size))
