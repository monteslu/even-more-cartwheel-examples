/* emberthrone: how the rules look. Each actor has a rig: its glTF instance (the geometry shared with every other of
 * its kind), an AnimationPlayer with the shared clip library, a FootPlacement3D that plants the feet on the slopes
 * and stairs it walks (the legs solved onto the physics ground, the pelvis dropped to the lower foot), its weapon
 * hung on the right hand bone, the tonic flask in the left while it drinks, a light that flares through a foe's
 * wind-up. The clip follows the rules' state each fixed tick (cross-faded, an attack's clip timed so its blow lands
 * on the rules' hit frame). The dead fall as ragdolls (cartwheel/ragdoll.h) on the world's own collision and lie
 * there; that is presentation only, the rules never read it. Rings on the ground warn of area blows and shockwaves
 * (they fade in and out). A blade leaves a ribbon through the arc where its blow is live (the rules' hit window), the
 * heroes' and the bosses'. A blow's hit-stop stops every clip with the world. A shield bearer carries its shield up in
 * front while it holds and drops it when it is knocked aside; a slinger's stones fly with a short streak; a marsh
 * lurker waits in a pool, head and shoulders above the water, and rises. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/anim.h"
#include "cartwheel/cloth.h"
#include "cartwheel/debug.h"
#include "cartwheel/ik.h"
#include "cartwheel/ragdoll.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/retarget.h"
#include "cartwheel/rim_light.h"
#include "cartwheel/scene_file.h"
#include "../common/coplanar.h"
#include "game.h"

bool g_hide_hud;

typedef struct rig {
  bool made;
  int kind, region; /* region: the foe's colours (tint_outfit) */
  cw_node root, model, anim, tree, feet, light, flask, trail, shield, look[3], cape;
  int role;
  t3_object *inst;
  const char *clip;
  int clip_tick;     /* the tick the current clip started */
  int last_state, last_atk, last_st_t;
  cw_pose pose;
  bool bound;
  cw_ragdoll *rd;
  int rd_t;
  float rd_yaw;
  bool still;        /* the dead, settled: the pose is kept and nothing more runs */
  int shadow;        /* casting the sun's shadow now (-1 unknown) */
  bool fresh;        /* made this tick: the tree waits a tick, so the skeleton modifiers bind on the bind pose (their rest;
                      * cartwheel-engine 0.71.0 takes the rest from the skin's bind pose whenever they bind, after which this is
                      * harmless) */
  int act;           /* the action playing over the locomotion (ACT index), -1 none */
  float lean_s, lean_f, last_speed, last_yaw, look_w, armed, guard, bx, by, bs, feet_w; /* bx, by, bs: the blend space's place, eased */ /* smoothed leans, the last tick's speed and facing, the look's weight */
  t3_vec3 cape_at;   /* where the cape's top edge hangs (model frame, the figure at rest) */
  t3_vec3 last_pos;  /* the last tick's place (a jump re-hangs the cape) */
  t3_vec3 look_at;   /* the point the look turns to (glides between faces) */
  bool adv_due;      /* the tree advances by adv_dt in the next tick's node phase (anim_hook), before the modifiers */
  float adv_dt;
  bool root_due;     /* and the root is placed there, at root_at turned root_yaw */
  t3_vec3 root_at;
  float root_yaw;
  t3_vec3 drawn_at;  /* where the root stands now (placed by anim_hook): what the recorders measure against */
  float drawn_yaw;
  float gv[5];       /* ground speed of walk, run, back, strafe left, strafe right (m/s, from the clips' planted feet) */
} rig;
static rig RIG[MAXA];
static void tree_make(rig *r, int kind);
static int act_index(const char *c);
static t3_vec3 (*mrec_cape_fn)(int actor, const cw_pose *p, int chest, bool *ok);
static float LEAN_FORCE[2];
static void cmd_framing(int argc, const char **argv, cw_reply *rp);
/* each kind's clips and their lengths (seconds), read from its model's library: an action is timed by its clip's own length */
#define MAXCLIPS 48
static struct { int n; char name[MAXCLIPS][16]; float len[MAXCLIPS]; } CLIPS[K_KINDS];
static float clip_len(int kind, const char *c) {
  for (int k = 0; k < CLIPS[kind].n; k++) if (!strcmp(CLIPS[kind].name[k], c)) return CLIPS[kind].len[k];
  return 1;
}
static cw_node V_world, V_ring[8], V_shock[4], V_stone[MAXSTONE], V_stone_trail[MAXSTONE];
static t3_material *RING_MAT[8], *SHOCK_MAT[4];
static t3_mesh *SHIELD_MESH, *STONE_MESH;
static cw_res RES_KIND[K_KINDS], RES_WEAP[8], RES_FLASK;
static view_stats VST;
static int rag_live;
view_stats view_get_stats(void) { return VST; }

static const cw_ragdoll_part PARTS[] = {
  { .bone = "Hips", .tip = "Spine", .radius = 0.14f, .density_scale = 1.2f },
  { .bone = "Spine", .tip = "Chest", .radius = 0.13f, .joint = CW_RAGDOLL_BALL, .swing = 0.45f, .lower = -0.35f, .upper = 0.35f },
  { .bone = "Chest", .tip = "Neck", .radius = 0.15f, .joint = CW_RAGDOLL_BALL, .swing = 0.35f, .lower = -0.3f, .upper = 0.3f },
  { .bone = "Head", .tip_offset = { 0, 0.2f, 0.02f }, .radius = 0.09f, .joint = CW_RAGDOLL_BALL, .swing = 0.75f, .lower = -0.7f, .upper = 0.7f },
  { .bone = "UpperArmL", .tip = "LowerArmL", .radius = 0.055f, .joint = CW_RAGDOLL_BALL, .swing = 1.7f, .lower = -0.7f, .upper = 0.7f },
  { .bone = "LowerArmL", .tip = "HandL", .radius = 0.048f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -2.4f, .upper = 0.05f },
  { .bone = "UpperArmR", .tip = "LowerArmR", .radius = 0.055f, .joint = CW_RAGDOLL_BALL, .swing = 1.7f, .lower = -0.7f, .upper = 0.7f },
  { .bone = "LowerArmR", .tip = "HandR", .radius = 0.048f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -2.4f, .upper = 0.05f },
  { .bone = "UpperLegL", .tip = "LowerLegL", .radius = 0.085f, .joint = CW_RAGDOLL_BALL, .swing = 1.25f, .lower = -0.45f, .upper = 0.45f },
  { .bone = "LowerLegL", .tip = "FootL", .radius = 0.065f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.05f, .upper = 2.4f },
  { .bone = "FootL", .tip_offset = { 0, -0.06f, 0.15f }, .radius = 0.045f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.6f, .upper = 0.7f },
  { .bone = "UpperLegR", .tip = "LowerLegR", .radius = 0.085f, .joint = CW_RAGDOLL_BALL, .swing = 1.25f, .lower = -0.45f, .upper = 0.45f },
  { .bone = "LowerLegR", .tip = "FootR", .radius = 0.065f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.05f, .upper = 2.4f },
  { .bone = "FootR", .tip_offset = { 0, -0.06f, 0.15f }, .radius = 0.045f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.6f, .upper = 0.7f },
};
#define NPARTS ((int)(sizeof PARTS / sizeof *PARTS))
#define RAG_MAX 8

/* the scale each model was generated at (tools/chars/make-characters.mjs SPECS scale): ragdoll radii follow it */
static const float GEN_SCALE[K_KINDS] = { 1.02f, 1.06f, 1.0f, 1.3f, 1.08f, 1.75f, 1.5f, 1.95f };
/* the ankle above the sole at that scale's 1 (tools/chars/out/cc0/<name>.json, FootL): the feet sit on the ground */
static const float ANKLE[K_KINDS] = { 0.068f, 0.086f, 0.085f, 0.086f, 0.088f, 0.086f, 0.068f, 0.086f };

static void shadows(t3_object *o) {
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH) o->cast_shadow = !(o->name && !strncmp(o->name, "Hair", 4)), o->receive_shadow = true;
  for (int i = 0; i < o->child_count; i++) shadows(o->children[i]);
}
static void cast_shadows(t3_object *o, bool on) {
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH) o->cast_shadow = on && !(o->name && !strncmp(o->name, "Hair", 4));
  for (int i = 0; i < o->child_count; i++) cast_shadows(o->children[i], on);
}
/* the foes wear their region's colours: the painted outfit tinted (one material per kind and region, cloned from the
 * file's), so a raider of the marsh is not a raider of the canyon */
static const t3_color REGION_TINT[RG_COUNT] = {
  [RG_MEADOW] = { 1.0f, 1.0f, 1.0f }, [RG_CANYON] = { 1.18f, 0.92f, 0.68f }, [RG_MARSH] = { 0.7f, 1.0f, 1.08f },
  [RG_KEEP] = { 1.22f, 0.72f, 0.66f }, [RG_HEIGHTS] = { 0.84f, 0.96f, 1.2f }, [RG_UNDER] = { 1.08f, 0.9f, 0.74f },
  [RG_SPIRE] = { 0.96f, 1.06f, 0.88f }, [RG_WOOD] = { 0.9f, 0.82f, 1.18f },
};
static t3_material *TINTED[K_KINDS][RG_COUNT];
static void tint_outfit(t3_object *o, int kind, int region) {
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH) {
    t3_mesh *m = (t3_mesh *)o;
    for (int k = 0; k < m->material_count; k++) {
      t3_material *mat = m->materials[k];
      if (!mat || !mat->name || strcmp(mat->name, "outfit_outfit")) continue;
      t3_material **t = &TINTED[kind][region];
      if (!*t) {
        *t = t3_material_clone(mat);
        t3_color c = REGION_TINT[region];
        (*t)->color = (t3_color){ mat->color.r * c.r, mat->color.g * c.g, mat->color.b * c.b };
      }
      t3_retain(*t);
      t3_release(m->materials[k]);
      m->materials[k] = *t;
    }
  }
  for (int i = 0; i < o->child_count; i++) tint_outfit(o->children[i], kind, region);
}
/* a thin rim of light round each figure (cartwheel/rim_light.h, on every preset): it separates them from busy ground
 * at play distance and fades at arm's length; warm on the heroes, a cold edge on the foes, the bosses' their own
 * colour. Per material: the file's materials are shared by every instance of a kind (and the foes' tinted clones),
 * so each gets its rim once. */
static void rim(t3_object *o, int kind) {
  static const t3_color C[K_KINDS] = { { 1, 0.82f, 0.55f }, { 1, 0.82f, 0.55f }, { 0.75f, 0.85f, 1 }, { 0.75f, 0.85f, 1 }, { 0.75f, 0.85f, 1 },
                                       { 0.6f, 0.85f, 0.45f }, { 0.75f, 0.6f, 1 }, { 1, 0.55f, 0.25f } };
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH) {
    t3_mesh *m = (t3_mesh *)o;
    for (int k = 0; k < m->material_count; k++) {
      t3_material *mat = m->materials[k];
      if (!mat || cw_rim_light_get(mat, NULL) || (mat->name && !strncmp(mat->name, "eye", 3))) continue;
      cw_rim_light_desc d = cw_rim_light_default();
      d.color = C[kind], d.strength = kind <= K_WARDEN ? 0.7f : kind >= K_THORN ? 0.45f : 0.55f, d.power = 3.2f, d.near = 4, d.far = 14, d.close = 0.2f;
      cw_rim_light_apply(mat, &d);
    }
  }
  for (int i = 0; i < o->child_count; i++) rim(o->children[i], kind);
}
static t3_object *weapon_obj(const char *name) {
  static const char *const W[] = { "sword", "maul", "cleaver", "spear", "bigaxe" };
  for (int k = 0; k < 5; k++)
    if (!strcmp(W[k], name)) {
      if (!RES_WEAP[k].id) {
        char p[48];
        snprintf(p, sizeof p, "models/%s.glb", W[k]);
        RES_WEAP[k] = cw_res_load(p, CW_RES_GLTF);
      }
      t3_object *o = cw_gltf_instantiate(RES_WEAP[k]);
      if (o) shadows(o);
      return o;
    }
  return NULL;
}

/* capes: cloth (cartwheel/cloth.h, a Cloth3D pinned along its top edge to the chest bone) that swings with the
 * body and settles when it stops, kept near the back it hangs on (skin_distance) so it never goes through the legs.
 * Matte wool: no sheen to speak of. Sizes in the figure's own metres (the kind's scale applied). */
static const struct { bool on; float w, h, back; t3_color c; } CAPE[K_KINDS] = {
  /* (the heroes wear none: the lock-on camera sits at their shoulder, where a cape would fill the view) */
  [K_EMBER] = { true, 0.62f, 1.12f, 0.2f, { 0.42f, 0.03f, 0.025f } },
};
static void cape_free(rig *r) {
  if (r->cape.id && cw_node_valid(r->cape)) cw_node_free(r->cape);
  r->cape = (cw_node){ 0 };
}
static bool CAPE_TEST; /* "motion cape 1": the first player wears the Ember King's cape, for tests/emberthrone-motion.mjs (the King never
                        * stands still in a fight, so his cape's settle is measured on the player's scripted stops) */
static void cape_make(rig *r) {
  const bool test = CAPE_TEST && r == &RIG[0];
  const int kind = r->kind, spec = test ? K_EMBER : kind;
  if (!CAPE[spec].on || r->cape.id) return;
  float g = GEN_SCALE[kind], w = CAPE[spec].w * g;
  r->cape = cw_node_new("Cloth3D", "Cape");
  cw_node_add_child(r->root, r->cape);
  cw_node3d_set_position(r->cape, (t3_vec3){ -w / 2, r->cape_at.y * g, (r->cape_at.z - CAPE[spec].back) * g });
  cw_node_set(r->cape, "width", cw_val_float(w));
  cw_node_set(r->cape, "height", cw_val_float(CAPE[spec].h * g));
  cw_node_set(r->cape, "columns", cw_val_int(8));
  cw_node_set(r->cape, "rows", cw_val_int(12));
  cw_node_set(r->cape, "attach_to", cw_val_string("../Model"));
  cw_node_set(r->cape, "bone", cw_val_string("Chest"));
  cw_node_set(r->cape, "skin_distance", cw_val_float(0.1f * g)); /* close to the back: a spin never throws it round the front */
  cw_node_set(r->cape, "mass", cw_val_float(0.8f * g));
  cw_node_set(r->cape, "bend_compliance", cw_val_float(0.04f));
  cw_node_set(r->cape, "damping", cw_val_float(2.5f));
  cw_node_set(r->cape, "thickness", cw_val_float(0.02f));
  cw_node_set(r->cape, "collision_mask", cw_val_int(0));
  cw_node_set(r->cape, "quality_scaled", cw_val_bool(true));
  cw_node_set(r->cape, "color", cw_val_color(CAPE[spec].c.r, CAPE[spec].c.g, CAPE[spec].c.b, 1));
  cw_node_set(r->cape, "roughness", cw_val_float(0.95f));
  cw_node_set(r->cape, "sheen", cw_val_float(0.15f));
}
/* the recorder's cape point: the middle of the cape's hem, in the chest's frame */
static t3_vec3 cape_point(int i, const cw_pose *p, int chest, bool *ok) {
  rig *r = &RIG[i];
  *ok = false;
  if (!r->cape.id || chest < 0) return (t3_vec3){ 0 };
  cw_cloth c = cw_cloth3d_get(r->cape);
  int n = 0;
  const t3_vec3 *x = c.id ? cw_cloth_positions(c, &n) : NULL;
  if (!x || n < 13 * 9) return (t3_vec3){ 0 };
  t3_vec3 w = x[4 + 12 * 9];
  const actor *a = &S.a[i];
  float dx = w.x - r->drawn_at.x, dy = w.y - r->drawn_at.y, dz = w.z - r->drawn_at.z, cy = cw_cos(-r->drawn_yaw), sy = cw_sin(-r->drawn_yaw);
  t3_vec3 m = { dx * cy + dz * sy, dy, -dx * sy + dz * cy }; /* the model frame (yaw only) */
  t3_vec3 o = cw_pose_model_position(p, chest);
  t3_quat q = cw_pose_model_rotation(p, chest);
  t3_vec3 d = { m.x - o.x, m.y - o.y, m.z - o.z };
  /* by the inverse of the chest's turn */
  t3_quat iq = { -q.x, -q.y, -q.z, q.w };
  float tx = 2 * (iq.y * d.z - iq.z * d.y), ty = 2 * (iq.z * d.x - iq.x * d.z), tz = 2 * (iq.x * d.y - iq.y * d.x);
  *ok = true;
  return (t3_vec3){ d.x + iq.w * tx + (iq.y * tz - iq.z * ty), d.y + iq.w * ty + (iq.z * tx - iq.x * tz), d.z + iq.w * tz + (iq.x * ty - iq.y * tx) };
}

static void rig_make(int i, int kind) {
  rig *r = &RIG[i];
  memset(r, 0, sizeof *r);
  r->kind = kind;
  r->region = S.a[i].region;
  if (!RES_KIND[kind].id) {
    char p[48];
    snprintf(p, sizeof p, "models/%s.glb", KIND[kind].model);
    RES_KIND[kind] = cw_res_load(p, CW_RES_GLTF);
  }
  const cw_gltf *g = cw_res_gltf(RES_KIND[kind]);
  if (!g) { cw_error("emberthrone: %s did not load", KIND[kind].model); return; }
  t3_object *inst = cw_gltf_instantiate(RES_KIND[kind]);
  if (!inst) return;
  shadows(inst);
  if (kind >= K_RAIDER && kind <= K_LANCER) tint_outfit(inst, kind, CW_CLAMP(S.a[i].region, 0, RG_COUNT - 1));
  rim(inst, kind);
  char nm[24];
  snprintf(nm, sizeof nm, "Rig%d", i);
  r->root = cw_node_new("Node3D", nm);
  cw_node_add_child(V_world, r->root);
  r->model = cw_node_new("Node3D", "Model");
  cw_node_add_child(r->root, r->model);
  cw_node3d_set_object(r->model, inst);
  r->inst = inst;
  r->anim = cw_node_new("AnimationPlayer", "Anim");
  cw_node_add_child(r->root, r->anim);
  for (int k = 0; k < g->gltf->animation_count; k++) {
    cw_anim a = cw_anim_from_t3_clip(g->gltf->animations[k], "Model");
    cw_anim_set_stored(a, false);
    if (act_index(g->gltf->animations[k]->name) >= 0) cw_anim_set_loop(a, CW_LOOP_NONE); /* an action plays once (glTF clips load looping) */
    if (CLIPS[kind].n < MAXCLIPS && k == CLIPS[kind].n) {
      snprintf(CLIPS[kind].name[k], sizeof CLIPS[kind].name[k], "%s", g->gltf->animations[k]->name);
      CLIPS[kind].len[k] = cw_anim_length(a), CLIPS[kind].n++;
    }
    cw_anim_player_add(r->anim, g->gltf->animations[k]->name, a);
    cw_anim_release(a);
  }
  tree_make(r, kind);
  { /* the cape hangs from the top of the back: the neck's height, the chest's depth (the figure at rest, unscaled) */
    cw_pose pz = { 0 };
    if (cw_pose_bind(&pz, inst)) {
      int nk = cw_pose_find(&pz, "Neck"), ch = cw_pose_find(&pz, "Chest");
      cw_pose_update_model(&pz);
      t3_vec3 pn = cw_pose_model_position(&pz, nk), pc = cw_pose_model_position(&pz, ch);
      r->cape_at = (t3_vec3){ 0, (pn.y - 0.06f * GEN_SCALE[kind]) / GEN_SCALE[kind], pc.z / GEN_SCALE[kind] };
      cw_pose_free(&pz);
    }
  }
  mrec_cape_fn = cape_point;
  /* feet on the ground: rays against the world's layer only (never a ragdoll) */
  r->feet = cw_node_new("FootPlacement3D", "Feet");
  cw_node_add_child(r->model, r->feet);
  cw_node_set(r->feet, "legs", cw_val_string("UpperLegL,LowerLegL,FootL;UpperLegR,LowerLegR,FootR"));
  cw_node_set(r->feet, "pelvis", cw_val_string("Hips"));
  cw_node_set(r->feet, "collision_mask", cw_val_int(1));
  cw_node_set(r->feet, "foot_height", cw_val_float(ANKLE[kind] * GEN_SCALE[kind]));
  cw_node_set(r->feet, "max_step_down", cw_val_float(0.55f * GEN_SCALE[kind]));
  cw_node_set(r->feet, "ray_up", cw_val_float(0.6f * GEN_SCALE[kind]));
  /* the chest, the neck and the head turn toward what it fights (after the feet; weights by state, view_fixed) */
  {
    static const char *const LB[3] = { "Chest", "Neck", "Head" };
    static const float LIM[3] = { 0.45f, 0.6f, 0.9f };
    for (int k = 0; k < 3; k++) {
      r->look[k] = cw_node_new("LookAtModifier3D", LB[k]);
      cw_node_add_child(r->model, r->look[k]);
      cw_node_set(r->look[k], "bone", cw_val_string(LB[k]));
      cw_node_set(r->look[k], "limit_angle", cw_val_float(LIM[k]));
      cw_node_set(r->look[k], "influence", cw_val_float(0));
    }
  }
  /* the weapon in the right fist */
  t3_object *hand = t3_object_get_by_name(inst, "HandR");
  t3_object *w = weapon_obj(KIND[kind].weapon);
  if (hand && w) {
    t3_object_set_position(w, 0, -0.075f, 0.015f);
    t3_object_add(hand, w);
  }
  if (w) t3_release(w);
  if (kind <= K_WARDEN) { /* the maul, hidden until it is carried; the flask in the left hand */
    t3_object *m = weapon_obj("maul");
    if (hand && m) { t3_object_set_position(m, 0, -0.075f, 0.015f); m->name = "Maul"; m->visible = false; t3_object_add(hand, m); }
    if (m) t3_release(m);
    if (!RES_FLASK.id) RES_FLASK = cw_res_load("models/tonic.glb", CW_RES_GLTF);
    t3_object *f = cw_gltf_instantiate(RES_FLASK);
    t3_object *lh = t3_object_get_by_name(inst, "HandL");
    if (f && lh) { t3_object_set_position(f, 0, -0.1f, 0.03f); f->name = "Flask"; f->visible = false; t3_object_add(lh, f); }
    if (f) t3_release(f);
  }
  /* a foe's tell: a warm light that flares through its wind-up */
  if (kind > K_WARDEN) {
    r->light = cw_node_new("OmniLight3D", "Tell");
    cw_node_add_child(r->root, r->light);
    cw_node_set(r->light, "color", cw_val_color(1, 0.45f, 0.15f, 1));
    cw_node_set(r->light, "energy", cw_val_float(0));
    cw_node_set(r->light, "range", cw_val_float(4 * GEN_SCALE[kind]));
    cw_node3d_set_position(r->light, (t3_vec3){ 0, 1.3f * GEN_SCALE[kind], 0.5f });
  }
  /* the blade's ribbon: the heroes and the bosses */
  if (kind <= K_WARDEN || IS_BOSS(kind)) {
    r->trail = cw_node_new("Trail3D", "Blade");
    cw_node_add_child(V_world, r->trail);
    bool boss = IS_BOSS(kind);
    cw_node_set(r->trail, "length", cw_val_int(boss ? 12 : 10));
    cw_node_set(r->trail, "width", cw_val_float(boss ? 0.6f : 0.3f)); /* (round the tip's path) */
    cw_node_set(r->trail, "color_ramp", cw_val_string(boss ? "0 1 0.62 0.3 0.75, 0.5 1 0.45 0.18 0.4, 1 0.9 0.3 0.1 0" : "0 1 0.95 0.82 0.8, 0.5 1 0.78 0.45 0.4, 1 1 0.55 0.25 0"));
    cw_node_set(r->trail, "energy", cw_val_float(boss ? 1.6f : 1.8f));
    cw_node_set(r->trail, "emitting", cw_val_bool(false));
  }
  /* a shield bearer's shield: held up in front of it */
  r->role = S.a[i].role;
  if (r->role == R_SHIELD && SHIELD_MESH) {
    r->shield = cw_node_new("MeshInstance3D", "Shield");
    cw_node_add_child(r->root, r->shield);
    cw_mesh3d_set_procedural(r->shield, SHIELD_MESH);
  }
  cop_separate(inst, 0.002f, NULL); /* the model's rigid parts laid flush on each other 2 mm apart (games/common/coplanar.h) */
  t3_release(inst);
  r->made = true, r->shadow = -1, r->fresh = true;
}

void view_reset(void) {
  for (int i = 0; i < MAXA; i++) {
    if (RIG[i].rd) cw_ragdoll_destroy(RIG[i].rd);
    cw_pose_free(&RIG[i].pose);
    if (RIG[i].made && RIG[i].trail.id && cw_node_valid(RIG[i].trail)) cw_node_free(RIG[i].trail); /* (the ribbons hang off the world, not the rig) */
    if (RIG[i].made && RIG[i].root.id && cw_node_valid(RIG[i].root)) cw_node_free(RIG[i].root); /* (a jump ahead keeps the world: no figure left standing) */
  }
  memset(RIG, 0, sizeof RIG);
  rag_live = 0;
}

static t3_mesh *ring_mesh(float inner, float outer, t3_color c, float e) {
  t3_geometry *g = t3_ring_geometry_new(inner, outer, 48, 1);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = (t3_color){ c.r * e, c.g * e, c.b * e };
  m->transparent = true, m->opacity = 0.75f, m->depth_write = false, m->blending = 2, m->side = T3_DOUBLE_SIDE;
  m->tone_mapped = false;
  t3_mesh *mesh = t3_mesh_new(g, m);
  t3_release(g), t3_release(m);
  return mesh;
}

static t3_mesh *ring_mesh(float inner, float outer, t3_color c, float e);
static int init_stage;
bool view_init_step(cw_node root, int *done, int *total) {
  *total = 3 + K_KINDS;
  if (init_stage == 0) {
    V_world = cw_node_get(root, "World");
    world_build(cw_scene_app_tree());
    init_stage++;
  } else if (init_stage <= K_KINDS) { /* load each model up front (the first fight does not stall on it) */
    int k = init_stage - 1;
    char p[48];
    snprintf(p, sizeof p, "models/%s.glb", KIND[k].model);
    if (!RES_KIND[k].id) RES_KIND[k] = cw_res_load(p, CW_RES_GLTF);
    init_stage++;
  } else if (init_stage == K_KINDS + 1) {
    for (int i = 0; i < 8; i++) {
      char nm[16];
      snprintf(nm, sizeof nm, "Ring%d", i);
      V_ring[i] = cw_node_new("MeshInstance3D", nm);
      cw_node_add_child(V_world, V_ring[i]);
      t3_mesh *m = ring_mesh(0.86f, 1.0f, (t3_color){ 1, 0.35f, 0.1f }, 2.2f);
      RING_MAT[i] = m->materials[0], t3_retain(RING_MAT[i]);
      cw_mesh3d_set_procedural(V_ring[i], m);
      t3_release(m);
      cw_node_set(V_ring[i], "visible", cw_val_bool(false));
    }
    for (int i = 0; i < 4; i++) {
      char nm[16];
      snprintf(nm, sizeof nm, "Shock%d", i);
      V_shock[i] = cw_node_new("MeshInstance3D", nm);
      cw_node_add_child(V_world, V_shock[i]);
      t3_mesh *m = ring_mesh(0.92f, 1.0f, (t3_color){ 0.5f, 0.9f, 1 }, 3.0f);
      SHOCK_MAT[i] = m->materials[0], t3_retain(SHOCK_MAT[i]);
      cw_mesh3d_set_procedural(V_shock[i], m);
      t3_release(m);
      cw_node_set(V_shock[i], "visible", cw_val_bool(false));
    }
    { /* the shield (a board of planks held up facing forward) and the slingers' stones */
      /* (the world's own wood and stone: the programs the gates and doors already draw with) */
      extern t3_material *world_material(int mt, float tint);
      /* boxes: the world's materials are normal mapped, and the box geometry carries what that needs (a cylinder drew
       * black) */
      t3_geometry *g = t3_box_geometry_new(0.66f, 0.07f, 0.74f, 1, 1, 1);
      SHIELD_MESH = t3_mesh_new(g, world_material(MT_WOOD, 1));
      t3_release(g);
      g = t3_box_geometry_new(0.15f, 0.13f, 0.14f, 1, 1, 1);
      STONE_MESH = t3_mesh_new(g, world_material(MT_STONE, 1));
      t3_release(g);
      for (int i = 0; i < MAXSTONE; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "Stone%d", i);
        V_stone[i] = cw_node_new("MeshInstance3D", nm);
        cw_node_add_child(V_world, V_stone[i]);
        cw_mesh3d_set_procedural(V_stone[i], STONE_MESH);
        cw_node_set(V_stone[i], "visible", cw_val_bool(false));
        V_stone_trail[i] = cw_node_new("Trail3D", "StoneTrail");
        cw_node_add_child(V_stone[i], V_stone_trail[i]);
        cw_node_set(V_stone_trail[i], "length", cw_val_int(6));
        cw_node_set(V_stone_trail[i], "width", cw_val_float(0.14f));
        cw_node_set(V_stone_trail[i], "color_ramp", cw_val_string("0 0.9 0.85 0.75 0.6, 1 0.7 0.65 0.55 0"));
        cw_node_set(V_stone_trail[i], "blend", cw_val_enum("ALPHA"));
        cw_node_set(V_stone_trail[i], "emitting", cw_val_bool(false));
      }
    }
    init_stage++;
  } else {
    *done = *total;
    return true;
  }
  *done = init_stage;
  return false;
}

/* ── the animation tree ─────────────────────────────────────────────── */
/* Each rig's AnimationTree (cartwheel/anim.h): the locomotion is a 2D blend space of the stand, walk, run, back walk
 * and the two sidesteps by the velocity in the body's frame (each clip placed at its own ground speed, so the feet
 * keep pace with the ground; synced, so a walk becomes a sidestep without a skipped step), or a 1D one of the
 * unarmed stand, walk and run for a foe that has not seen you; the rest at a shrine and the lying of the dead are
 * blended in by weight; two additive leans (into a turn, into a start or a stop) on the spine; and every action
 * (a cut, a roll, a hit, the flask...) a one-shot over all of it, faded in and out, timed by its own rate. An action
 * plays to its end after the rules let go (the follow-through and the settle of a cut while the body stands) unless
 * the body moves off or another action starts. The tree advances by hand each fixed tick, by nothing through a
 * blow's hit-stop. */
static const char *const ACTS[] = { "slash1", "slash2", "slash3", "heavy", "swing", "slam", "stab", "bash", "haymaker", "lariat",
                                    "roar", "roll", "backstep", "hit", "hit_hard", "knockdown", "getup", "kneel", "rise", "drink", "pickup", "push" };
#define NACT ((int)(sizeof ACTS / sizeof *ACTS))
#define ACT_ATTACKS 10 /* the first ACT_ATTACKS are blows: they follow through while the body stands */
static cw_blend_id ACT_NODE[K_KINDS][NACT], ACT_CLIP[K_KINDS][NACT];
static int act_index(const char *c) {
  for (int k = 0; k < NACT; k++) if (!strcmp(ACTS[k], c)) return k;
  return -1;
}
/* a one-frame additive pose: turns about model axes on the spine (the rest rotations are identity) */
static cw_anim lean_clip(const char *name, t3_vec3 axis, float deg) {
  static const char *const B[] = { "Hips", "Spine", "Spine2", "Chest" };
  static const float SHARE[] = { 0.2f, 0.25f, 0.3f, 0.25f };
  cw_anim a = cw_anim_new(name, 1, CW_LOOP_LINEAR);
  cw_anim_set_stored(a, false);
  for (int b = 0; b < 4; b++) {
    char p[48];
    snprintf(p, sizeof p, "Model@%s:quaternion", B[b]);
    int k = cw_anim_add_value_track(a, p, CW_INTERP_LINEAR);
    float s, c;
    cw_sincos(deg * SHARE[b] * CW_PI / 360.0f, &s, &c);
    cw_value v = { CW_T_QUAT };
    v.v[0] = axis.x * s, v.v[1] = axis.y * s, v.v[2] = axis.z * s, v.v[3] = c;
    cw_anim_add_key(a, k, 0, v);
  }
  return a;
}
static void tree_make(rig *r, int kind) {
  /* the leans, one clip a way (an additive's weight is 0 to 1): into a turn to the left (+x) or the right, forward or back */
  static const struct { const char *n; t3_vec3 ax; float deg; } LEANS[4] = { { "lean_l", { 0, 0, -1 }, 12 }, { "lean_r", { 0, 0, 1 }, 12 },
                                                                            { "lean_fw", { 1, 0, 0 }, 9 }, { "lean_bk", { -1, 0, 0 }, 7 } };
  for (int k = 0; k < 4; k++) {
    cw_anim l = lean_clip(LEANS[k].n, LEANS[k].ax, LEANS[k].deg);
    cw_anim_player_add(r->anim, LEANS[k].n, l);
    cw_anim_release(l);
  }
  r->tree = cw_node_new("AnimationTree", "Tree");
  cw_node_add_child(r->root, r->tree);
  cw_node_set(r->tree, "player", cw_val_string("../Anim"));
  cw_node_set(r->tree, "process", cw_val_enum("MANUAL"));
  cw_node t = r->tree;
  /* the clips' own ground speeds (retargeted to this figure's legs at pack time) */
  static const char *const LOCO[] = { "walk_w", "run_w", "walk_back", "strafe_l", "strafe_r" };
  static const float FALLBACK[] = { 1.5f, 4.2f, 1.4f, 1.4f, 1.4f };
  cw_pose pz = { 0 };
  bool posed = r->inst && cw_pose_bind(&pz, r->inst);
  int lf = posed ? cw_pose_find(&pz, "FootL") : -1, rf = posed ? cw_pose_find(&pz, "FootR") : -1;
  for (int k = 0; k < 5; k++) {
    cw_anim a = cw_anim_player_get(r->anim, LOCO[k]);
    t3_vec3 v = posed && lf >= 0 && rf >= 0 && a.id ? cw_anim_ground_velocity(a, &pz, lf, rf, true) : (t3_vec3){ 0 };
    float g = sqrtf(v.x * v.x + v.z * v.z);
    r->gv[k] = g > 0.3f && g < 9 ? g : FALLBACK[k];
  }
  if (posed) cw_pose_free(&pz);
  cw_blend_id lw = cw_animtree_blend2d(t, "mx", "my");
  cw_animtree_blend2d_add(t, lw, cw_animtree_clip(t, "idle_w"), 0, 0);
  cw_animtree_blend2d_add(t, lw, cw_animtree_clip(t, "walk_w"), 0, r->gv[0]);
  cw_animtree_blend2d_add(t, lw, cw_animtree_clip(t, "run_w"), 0, r->gv[1]);
  cw_animtree_blend2d_add(t, lw, cw_animtree_clip(t, "walk_back"), 0, -r->gv[2]);
  cw_animtree_blend2d_add(t, lw, cw_animtree_clip(t, "strafe_l"), r->gv[3], 0);
  cw_animtree_blend2d_add(t, lw, cw_animtree_clip(t, "strafe_r"), -r->gv[4], 0);
  cw_animtree_set_sync(t, lw, true);
  cw_blend_id lu = cw_animtree_blend1d(t, "speed");
  cw_animtree_blend1d_add(t, lu, cw_animtree_clip(t, "idle"), 0);
  cw_animtree_blend1d_add(t, lu, cw_animtree_clip(t, "walk_w"), r->gv[0]);
  cw_animtree_blend1d_add(t, lu, cw_animtree_clip(t, "run_w"), r->gv[1]);
  cw_animtree_set_sync(t, lu, true);
  cw_blend_id loco = cw_animtree_time_scale(t, cw_animtree_blend2(t, lu, lw, "armed"), "rate");
  /* the guard: the sword stand's arms over the walks (the walks are the library's unarmed ones, arms swinging), so a figure
   * that fights keeps its weapon up as it moves and comes back to its guard out of a roll, a hit or a cut, not to a
   * stroll */
  cw_blend_id guard = cw_animtree_layer(t, loco, cw_animtree_clip(t, "idle_w"), "guard");
  {
    static const char *const ARMS[] = { "ShoulderR+", "ShoulderL+" };
    cw_animtree_set_filter(t, guard, ARMS, 2);
  }
  cw_blend_id base = cw_animtree_blend2(t, guard, cw_animtree_clip(t, "rest"), "resting");
  base = cw_animtree_blend2(t, base, cw_animtree_clip(t, "lying"), "lying");
  base = cw_animtree_add(t, base, cw_animtree_clip(t, "lean_l"), "lean_l");
  base = cw_animtree_add(t, base, cw_animtree_clip(t, "lean_r"), "lean_r");
  base = cw_animtree_add(t, base, cw_animtree_clip(t, "lean_fw"), "lean_fw");
  base = cw_animtree_add(t, base, cw_animtree_clip(t, "lean_bk"), "lean_bk");
  for (int k = 0; k < NACT; k++) {
    ACT_NODE[kind][k] = ACT_CLIP[kind][k] = -1;
    if (!cw_anim_player_get(r->anim, ACTS[k]).id) continue; /* (each kind carries only the clips it plays: the .cwimport lists them) */
    /* the big blows wind up from far off their first pose: a longer fade in (the rules' hit is at least half a second away) */
    bool big = k == act_index("heavy") || k == act_index("lariat") || k == act_index("slam") || k == act_index("haymaker");
    float in = big ? 0.2f : k < ACT_ATTACKS ? 0.12f : k == act_index("hit") || k == act_index("hit_hard") ? 0.1f : 0.15f;
    float out = k < ACT_ATTACKS ? 0.3f : 0.2f;
    cw_blend_id shot = ACT_CLIP[kind][k] = cw_animtree_clip(t, ACTS[k]); /* its rate set as it fires (act_play) */
    base = ACT_NODE[kind][k] = cw_animtree_oneshot(t, base, shot, ACTS[k], in, out);
  }
  cw_animtree_set_root(t, base);
  cw_animtree_set_param(t, "rate", 1);
  r->act = -1, r->armed = -1;
}
static void act_play(rig *r, const char *c, float rate) {
  int k = act_index(c);
  if (k < 0 || !r->tree.id || ACT_NODE[r->kind][k] < 0) return;
  if (r->act >= 0 && r->act != k) cw_animtree_set_param(r->tree, ACTS[r->act], -1); /* the last one fades out under it */
  cw_animtree_clip_set_speed(r->tree, ACT_CLIP[r->kind][k], rate);
  cw_animtree_set_param(r->tree, c, 1);
  r->act = k, r->clip = ACTS[k];
}
static void act_rate(rig *r, float rate) {
  if (r->act >= 0) cw_animtree_clip_set_speed(r->tree, ACT_CLIP[r->kind][r->act], rate);
}
static void act_stop(rig *r) {
  if (r->act >= 0) cw_animtree_set_param(r->tree, ACTS[r->act], -1);
  r->act = -1;
}
static bool act_on(const rig *r) { return r->act >= 0 && ACT_NODE[r->kind][r->act] >= 0 && cw_animtree_oneshot_active(r->tree, ACT_NODE[r->kind][r->act]); }

/* ── clips ─────────────────────────────────────────────────────────── */
/* where the blow lands in each clip (seconds): an attack's clip runs at the speed that puts this on the hit frame */
static float strike_time(const char *clip) {
  static const struct { const char *c; float t; } T[] = { { "slash1", 0.25f }, { "slash2", 0.27f }, { "slash3", 0.65f }, { "heavy", 0.27f }, { "swing", 0.15f },
                                                          { "slam", 0.38f }, { "stab", 0.3f }, { "haymaker", 0.27f }, { "lariat", 0.27f },
                                                          { "roar", 0.43f }, { "bash", 0.1f } };
  for (int i = 0; i < (int)(sizeof T / sizeof *T); i++) if (!strcmp(T[i].c, clip)) return T[i].t;
  return 0.2f;
}
/* an action by the rules' state: started on a new state (restart), else only its rate follows */
static void play(rig *r, const char *clip, float blend, float speed, bool restart) {
  (void)blend;
  if (!r->made) return;
  int k = act_index(clip);
  if (k < 0) return;
  if (restart || r->act != k) act_play(r, clip, speed);
  else act_rate(r, speed);
}

static t3_mat4 model_to_world(t3_vec3 at, float yaw) {
  t3_mat4 m;
  float s = cw_sin(yaw * 0.5f), c = cw_cos(yaw * 0.5f);
  t3_mat4_compose(&m, at, (t3_quat){ 0, s, 0, c }, (t3_vec3){ 1, 1, 1 });
  return m;
}

/* the fall of the dead: a ragdoll from the pose it had, pushed away from the blow */
static void rag_start(rig *r, const actor *a, float away) {
  if (rag_live >= RAG_MAX) return;
  if (!r->bound) {
    t3_object *o = cw_node3d_object(r->model);
    if (!o || !cw_pose_bind(&r->pose, o)) return;
    r->bound = true;
  }
  cw_pose_capture(&r->pose);
  t3_mat4 m = model_to_world(a->mv.position, a->yaw);
  cw_ragdoll_part parts[NPARTS];
  float k = GEN_SCALE[a->kind];
  for (int p = 0; p < NPARTS; p++) parts[p] = PARTS[p], parts[p].radius *= k;
  cw_ragdoll_def def = cw_ragdoll_default_def(parts, NPARTS);
  def.joint_friction = 3.0f * k * k;
  def.angular_damping = 0.8f;
  def.layer = 2, def.mask = 1u;
  r->rd = cw_ragdoll_create(world_phys(), &def, &r->pose, &m);
  if (!r->rd) return;
  rag_live++;
  r->rd_t = 0;
  t3_vec3 v = { cw_sin(away) * 2.5f, 1.5f, cw_cos(away) * 2.5f };
  cw_ragdoll_set_velocity(r->rd, v, (t3_vec3){ cw_cos(away) * 3, 0, -cw_sin(away) * 3 });
  cw_node_set(r->feet, "active", cw_val_bool(false));
}

/* where the blade is through the live part of a blow (from three ticks before the rules' hit window to one after):
 * when the ribbon draws (the rules' window, so it shows when the blow can land; view_fixed puts it on the tip of the
 * weapon in the hand), and a point on an arc round the body by the blow's shape (a sweep left or right, a full spin, an
 * overhead, a thrust). */
static bool blade_at(const actor *a, t3_vec3 *out) {
  if (a->state != ST_ATTACK || a->atk == A_THROW) return false;
  const attack_def *d = &ATK[a->atk];
  int t0 = d->on - 3, t1 = d->off + 1;
  if (a->st_t < t0 || a->st_t > t1) return false;
  float u = (float)(a->st_t - t0) / (float)(t1 - t0), g = GEN_SCALE[a->kind];
  float r = fmaxf(1.0f, d->reach * 0.75f) * g, h = 1.15f * g, th = 0;
  switch (a->atk) {
  case A_SLASH1: case A_CLEAVE: case A_HAY: th = lerpf(1.3f, -1.3f, u); break;
  case A_SLASH2: th = lerpf(-1.3f, 1.3f, u); break;
  case A_HEAVY: case A_SPIN: th = u * CW_TAU; break;
  case A_LARIAT: th = u * CW_TAU * 3; break;
  case A_STAB: case A_BASH: r *= lerpf(0.5f, 1.0f, u); h = 1.2f * g; break;
  case A_WAVE: return false;
  default: h = lerpf(2.2f, 0.35f, u) * g, r *= 0.7f + 0.3f * cw_sin(u * CW_PI); break; /* overheads and slams */
  }
  float yw = a->yaw + th;
  *out = (t3_vec3){ a->mv.position.x + cw_sin(yw) * r, a->mv.position.y + h, a->mv.position.z + cw_cos(yw) * r };
  return true;
}


/* ── the motion recorder (debug channel): what tests/emberthrone-motion.mjs measures ──────────────────
 * "motion rec <ticks> [actor]" records the next <ticks> fixed ticks of one actor (the player by default): its state,
 * clip, speed, facing, place and its lock target, every measured bone in the model frame (rotation and origin) and the
 * cape point (the lowest middle point of its cloth, else a point carried rigidly by the chest as a skinned cape is),
 * relative to the chest. "motion get <from> <n>" returns rows; "motion info" the bone list and the row count. */
static const char *const MREC_BONES[] = { "Hips", "Spine", "Chest", "Neck", "Head", "UpperArmR", "LowerArmR", "HandR", "UpperArmL", "LowerArmL", "HandL",
                                          "UpperLegL", "LowerLegL", "FootL", "UpperLegR", "LowerLegR", "FootR" };
#define MREC_NB ((int)(sizeof MREC_BONES / sizeof *MREC_BONES))
#define MREC_MAX 1800
typedef struct { int tick, state, atk, st_t, target; char clip[16]; float speed, yaw, x, y, z, tx, ty, tz, cape[3]; float q[MREC_NB][4], p[MREC_NB][3]; } mrec_row;
static mrec_row *MREC;
static int mrec_n, mrec_want, mrec_actor, mrec_tick;
static cw_pose MREC_POSE[2];
static bool MREC_BOUND[2];
static t3_vec3 (*mrec_cape_fn)(int actor, const cw_pose *p, int chest, bool *ok);
static void mrec_sample(int i) {
  if (!MREC || mrec_n >= mrec_want) return;
  if (mrec_actor == -2) { /* the first live boss */
    int b = -1;
    for (int k = 0; k < MAXA && b < 0; k++) if (S.a[k].used && S.a[k].active && S.a[k].boss) b = k;
    if (i != b) return;
  } else if (i != mrec_actor) return;
  const actor *a = &S.a[i];
  rig *r = &RIG[i];
  if (!r->made || !r->inst) return;
  /* bound again whenever the rig is made again (a freed model's memory can come back at the same address) */
  static uint32_t bound_root;
  cw_pose *p = &MREC_POSE[0];
  if (MREC_BOUND[0] && bound_root != r->root.id) cw_pose_free(p), MREC_BOUND[0] = false;
  if (!MREC_BOUND[0]) { if (!cw_pose_bind(p, r->inst)) return; MREC_BOUND[0] = true, bound_root = r->root.id; }
  cw_pose_capture(p);
  cw_pose_update_model(p);
  mrec_row *w = &MREC[mrec_n++];
  memset(w, 0, sizeof *w);
  /* the state that chose the pose drawn: the clips advance in the next tick's node phase (anim_hook), so the pose drawn now
   * is the one the last tick's state asked for */
  static struct { int state, atk, st_t, target; char clip[sizeof w->clip]; bool ok; } was;
  w->tick = mrec_tick, w->state = was.ok ? was.state : a->state, w->atk = was.ok ? was.atk : a->atk, w->st_t = was.ok ? was.st_t : a->st_t;
  w->target = a->target;
  snprintf(w->clip, sizeof w->clip, "%s", was.ok ? was.clip : r->clip ? r->clip : "");
  was.state = a->state, was.atk = a->atk, was.st_t = a->st_t, was.target = a->target, was.ok = mrec_n > 1;
  snprintf(was.clip, sizeof was.clip, "%s", r->clip ? r->clip : "");
  w->speed = a->speed, w->yaw = r->drawn_yaw, w->x = r->drawn_at.x, w->y = r->drawn_at.y, w->z = r->drawn_at.z; /* (where the pose drawn stands) */
  if (a->target >= 0) { const actor *t = &S.a[a->target]; w->tx = t->mv.position.x, w->ty = t->mv.position.y + 1.5f * GEN_SCALE[t->kind], w->tz = t->mv.position.z; }
  else { /* free: the nearest foe on the hunt within 9 m and 1.8 rad of its facing (the threat; the aim measure's free look) */
    float best = 81;
    for (int k = MAXP; k < MAXA; k++) {
      const actor *o = &S.a[k];
      if (!o->used || !o->active || o->state == ST_DEAD || !o->aggro) continue;
      float dx = o->mv.position.x - a->mv.position.x, dz = o->mv.position.z - a->mv.position.z, d = dx * dx + dz * dz;
      if (d < best && fabsf(wrapf(cw_atan2(dx, dz) - a->yaw)) <= 1.8f) best = d, w->tx = o->mv.position.x, w->ty = o->mv.position.y + 1.5f * GEN_SCALE[o->kind], w->tz = o->mv.position.z;
    }
  }
  for (int b = 0; b < MREC_NB; b++) {
    int k = cw_pose_find(p, MREC_BONES[b]);
    if (k < 0) { w->q[b][3] = 1; continue; }
    t3_quat q = cw_pose_model_rotation(p, k);
    t3_vec3 o = cw_pose_model_position(p, k);
    w->q[b][0] = q.x, w->q[b][1] = q.y, w->q[b][2] = q.z, w->q[b][3] = q.w;
    w->p[b][0] = o.x, w->p[b][1] = o.y, w->p[b][2] = o.z;
  }
  int chest = cw_pose_find(p, "Chest");
  bool ok = false;
  t3_vec3 c = { 0 };
  if (mrec_cape_fn) c = mrec_cape_fn(i, p, chest, &ok);
  if (!ok && chest >= 0) { /* a skinned cape: carried by the chest */
    c = (t3_vec3){ 0, -0.6f, -0.22f };
    w->cape[0] = c.x, w->cape[1] = c.y, w->cape[2] = c.z;
  } else w->cape[0] = c.x, w->cape[1] = c.y, w->cape[2] = c.z;
}
static float LEAN_FORCE[2] = { 99, 99 }; /* "motion lean <side> <fwd>": held leans for a test (99: off) */
static float LOOK_FORCE = 999; /* "motion look <deg>": the player looks at a point that far round from its facing, fully (999: off; a test of the look) */
static bool mrec_due[MAXA]; /* sampled in view_sync, after the tick's skeleton modifiers (foot placement, the look) */
static void cmd_motion(int argc, const char **argv, cw_reply *rp) {
  if (argc >= 3 && !strcmp(argv[1], "cape")) { CAPE_TEST = atoi(argv[2]) != 0; if (!CAPE_TEST) cape_free(&RIG[0]); cw_reply_printf(rp, "{\"cape\":%d}", CAPE_TEST); return; }
  if (argc >= 4 && !strcmp(argv[1], "lean")) { LEAN_FORCE[0] = (float)atof(argv[2]), LEAN_FORCE[1] = (float)atof(argv[3]); cw_reply_printf(rp, "{\"lean\":[%g,%g]}", LEAN_FORCE[0], LEAN_FORCE[1]); return; }
  if (argc >= 3 && !strcmp(argv[1], "look")) { LOOK_FORCE = (float)atof(argv[2]); cw_reply_printf(rp, "{\"look\":%g}", LOOK_FORCE); return; }
  if (argc >= 2 && !strcmp(argv[1], "rec")) {
    mrec_want = argc >= 3 ? CW_CLAMP(atoi(argv[2]), 1, MREC_MAX) : 600;
    mrec_actor = argc >= 4 ? (!strcmp(argv[3], "boss") ? -2 : atoi(argv[3])) : 0; /* "boss": the first boss in the fight */
    if (!MREC) MREC = (mrec_row *)calloc(MREC_MAX, sizeof *MREC);
    mrec_n = 0;
    cw_reply_printf(rp, "{\"recording\":%d,\"actor\":%d}", mrec_want, mrec_actor);
    return;
  }
  if (argc >= 2 && !strcmp(argv[1], "info")) {
    cw_reply_printf(rp, "{\"rows\":%d,\"want\":%d,\"bones\":[", mrec_n, mrec_want);
    for (int b = 0; b < MREC_NB; b++) cw_reply_printf(rp, "%s\"%s\"", b ? "," : "", MREC_BONES[b]);
    cw_reply_printf(rp, "]}");
    return;
  }
  if (argc >= 4 && !strcmp(argv[1], "get")) {
    int from = CW_CLAMP(atoi(argv[2]), 0, mrec_n), n = CW_CLAMP(atoi(argv[3]), 0, mrec_n - from);
    cw_reply_printf(rp, "[");
    for (int k = 0; k < n; k++) {
      const mrec_row *w = &MREC[from + k];
      cw_reply_printf(rp, "%s[%d,%d,%d,%d,%d,\"%s\",%.4f,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f", k ? "," : "", w->tick, w->state, w->atk, w->st_t, w->target, w->clip,
                      w->speed, w->yaw, w->x, w->y, w->z, w->tx, w->ty, w->tz, w->cape[0], w->cape[1], w->cape[2]);
      for (int b = 0; b < MREC_NB; b++) cw_reply_printf(rp, ",%.5f,%.5f,%.5f,%.5f,%.4f,%.4f,%.4f", w->q[b][0], w->q[b][1], w->q[b][2], w->q[b][3], w->p[b][0], w->p[b][1], w->p[b][2]);
      cw_reply_printf(rp, "]");
    }
    cw_reply_printf(rp, "]");
    return;
  }
  cw_reply_error(rp, "motion rec <ticks> [actor] | motion info | motion get <from> <n>");
}

/* ── the blade trail recorder (debug channel): what tests/emberthrone-trail.mjs measures ──────────────────
 * "trail rec <ticks>" checks, each fixed tick the lead player's ribbon is emitting, how far its head is from the tip of
 * the blade in the hand: the tip from the skeleton as posed (the right hand's model transform, the weapon where it hangs
 * in the hand, its length along the weapon), placed in the world by the figure's place and facing. "trail get" returns
 * the ticks, the mean and the largest distance (m). */
static struct { int want, n; double sum, max; uint32_t root; bool bound; cw_pose pose; } TRL;
static float weapon_tip(int kind, int weapon) { /* the blade's tip along the weapon's +z (models/<weapon>.glb), a little in */
  const char *w = kind <= K_WARDEN && weapon == 1 ? "maul" : KIND[kind].weapon;
  return !strcmp(w, "maul") ? 0.88f : !strcmp(w, "bigaxe") ? 1.2f : !strcmp(w, "spear") ? 1.75f : !strcmp(w, "cleaver") ? 0.6f : 0.95f;
}
static void trail_sample(int i, const actor *a, rig *r, bool on) {
  if (i != 0 || !on || TRL.n >= TRL.want || !r->inst) return;
  if (TRL.bound && TRL.root != r->root.id) cw_pose_free(&TRL.pose), TRL.bound = false;
  if (!TRL.bound) { if (!cw_pose_bind(&TRL.pose, r->inst)) return; TRL.bound = true, TRL.root = r->root.id; }
  cw_pose_capture(&TRL.pose);
  cw_pose_update_model(&TRL.pose);
  int h = cw_pose_find(&TRL.pose, "HandR");
  if (h < 0) return;
  t3_vec3 m = t3_vec3_apply_mat4((t3_vec3){ 0, -0.075f, 0.015f + weapon_tip(a->kind, a->weapon) }, &TRL.pose.model[h]);
  t3_mat4 w = model_to_world(r->drawn_at, r->drawn_yaw); /* (where the pose drawn stands) */
  t3_vec3 tip = t3_vec3_apply_mat4(m, &w), hd = cw_node3d_position(r->trail);
  float d = sqrtf((tip.x - hd.x) * (tip.x - hd.x) + (tip.y - hd.y) * (tip.y - hd.y) + (tip.z - hd.z) * (tip.z - hd.z));
  TRL.n++, TRL.sum += d, TRL.max = fmax(TRL.max, d);
}
static void cmd_trail(int argc, const char **argv, cw_reply *rp) {
  if (argc >= 2 && !strcmp(argv[1], "rec")) { TRL.want = argc >= 3 ? atoi(argv[2]) : 600, TRL.n = 0, TRL.sum = TRL.max = 0; cw_reply_printf(rp, "{\"recording\":%d}", TRL.want); return; }
  cw_reply_printf(rp, "{\"ticks\":%d,\"mean_m\":%.3f,\"max_m\":%.3f}", TRL.n, TRL.n ? TRL.sum / TRL.n : 0, TRL.max);
}

/* the clips advance in the tree's node phase (a hook just before the skeleton modifiers, cartwheel/ik.h PIPELINE): the
 * look, the feet and the rest then adjust the pose the clips wrote and that is what is drawn. Advanced in the app's
 * fixed_update (after the modifiers) the clips wrote over every solve, and the head never turned to what it fought.
 * view_fixed decides each rig's step (its clips, the hit-stop's hold); the next tick's hook takes it. */
static void anim_hook(cw_tree *tree, double dt, void *u) {
  (void)tree, (void)dt, (void)u;
  for (int i = 0; i < MAXA; i++) {
    rig *r = &RIG[i];
    if (r->root_due && r->made && r->root.id && cw_node_valid(r->root) && !r->rd && !r->still)
      cw_node3d_set_position(r->root, r->root_at), cw_node3d_set_rotation(r->root, (t3_vec3){ 0, r->root_yaw, 0 }), r->drawn_at = r->root_at, r->drawn_yaw = r->root_yaw;
    r->root_due = false;
    if (!r->adv_due) continue;
    r->adv_due = false;
    if (r->made && r->tree.id && cw_node_valid(r->tree)) cw_animtree_advance(r->tree, r->adv_dt);
  }
}

/* the rig follows its actor, once per fixed tick (main.c calls it after the rules) */
void view_fixed(void) {
  static bool mrec_cmd;
  if (!mrec_cmd) cw_debug_command("motion", "motion rec <ticks> [actor] | info | get <from> <n>: the motion recorder (tests/emberthrone-motion.mjs)", cmd_motion), mrec_cmd = true;
  static bool frm_cmd;
  if (!frm_cmd) cw_debug_command("framing", "framing rec <frames> | get: the lock-on framing recorder (tests/emberthrone-lockon.mjs)", cmd_framing), frm_cmd = true;
  static bool hooked;
  if (!hooked) cw_tree_add_hook(cw_current_tree(), 0, 140, anim_hook, NULL), hooked = true; /* (after physics, before the modifiers at 150) */
  static bool trl_cmd;
  if (!trl_cmd) cw_debug_command("trail", "trail rec <ticks> | get: the blade trail recorder (tests/emberthrone-trail.mjs)", cmd_trail), trl_cmd = true;
  mrec_tick++;
  VST.rigs = VST.drawn = 0;
  for (int i = 0; i < MAXA; i++) {
    const actor *a = &S.a[i];
    rig *r = &RIG[i];
    if (!a->used || (!a->active && i >= MAXP)) {
      if (r->made && cw_node_valid(r->root)) cw_node_set(r->root, "visible", cw_val_bool(false));
      if (r->made) cape_free(r);
      if (r->made && r->trail.id) cw_node_set(r->trail, "emitting", cw_val_bool(false));
      if (r->rd && (!a->used || !a->active)) cw_ragdoll_destroy(r->rd), r->rd = NULL, rag_live--;
      if (r->made && !a->used) r->still = false;
      continue;
    }
    if (!r->made || r->kind != a->kind || r->role != a->role || (a->kind >= K_RAIDER && a->kind <= K_LANCER && r->region != a->region)) {
      if (r->made && cw_node_valid(r->root)) cw_node_free(r->root);
      if (r->made && r->trail.id && cw_node_valid(r->trail)) cw_node_free(r->trail);
      rig_make(i, a->kind);
      if (!r->made) continue;
    }
    VST.rigs++;
    static int nochars = -1;
    if (nochars < 0) nochars = getenv("ADV_PROBE_NOCHARS") != NULL;
    bool near = !nochars && (i < MAXP || dist2(a->mv.position.x, a->mv.position.z, S.cam_pos.x, S.cam_pos.z) < 45 * 45); /* farther than 45 m a foe is a few pixels in the haze */
    bool lurk = a->role == R_LURKER && (a->state == ST_IDLE_HOME || a->state == ST_ROAR);
    /* a lurker waiting in a pool: only its head and shoulders above the water, still (it cannot be locked or struck
     * until it rises: the rules say so; the player sees what waits there) */
    const float sunk = 1.35f * GEN_SCALE[a->kind];
    cw_node_set(r->root, "visible", cw_val_bool(near));
    { /* the cape only while it is seen, hung again after a jump (a respawn, a teleport) */
      t3_vec3 p = a->mv.position;
      float j = dist2(p.x, p.z, r->last_pos.x, r->last_pos.z);
      if (!near || j > 3 * 3) cape_free(r);
      if (near) cape_make(r);
      r->last_pos = p;
    }
    VST.drawn += near;
    { /* a foe casts the sun's shadow only near the camera (the cascades reach 40 m; a far caster redraws them for a
       * shadow a few pixels long) */
      int sh = i < MAXP || dist2(a->mv.position.x, a->mv.position.z, S.cam_pos.x, S.cam_pos.z) < 24 * 24;
      if (r->inst && sh != r->shadow) cast_shadows(r->inst, sh), r->shadow = sh;
    }
    bool newst = a->state != r->last_state || a->st_t < r->last_st_t || (a->state == ST_ATTACK && a->atk != r->last_atk);
    if (a->state != ST_DEAD && (r->rd || r->still)) { /* back up (a rest, a respawn) */
      if (r->rd) cw_ragdoll_destroy(r->rd), r->rd = NULL, rag_live--;
      r->still = false;
      cw_node_set(r->feet, "active", cw_val_bool(true));
    }
    if (!r->rd && !r->still) {
      t3_vec3 at = a->mv.position;
      if (lurk && a->state == ST_IDLE_HOME) at.y -= sunk;
      if (lurk && a->state == ST_ROAR) { float k = 1 - (float)a->st_t / (float)CW_MAX(1, a->st_len); at.y -= sunk * k * k; } /* rising out of the water */
      /* placed with its pose, in the next tick's node phase (anim_hook): the figure drawn is the tick before's as a whole, its
       * place and its pose together; a figure just made is placed now */
      /* the facing drawn turns at most half a radian a tick: the rules turn a body at once (a roll's way, a cut's foe), the
       * figure swings round over two or three ticks rather than in one */
      float dy = wrapf(a->yaw - r->drawn_yaw);
      r->root_at = at, r->root_yaw = wrapf(r->drawn_yaw + cw_clampf(dy, -0.5f, 0.5f)), r->root_due = true;
      if (r->fresh) cw_node3d_set_position(r->root, at), cw_node3d_set_rotation(r->root, (t3_vec3){ 0, a->yaw, 0 }), r->drawn_at = at, r->drawn_yaw = a->yaw;
    }
    float sc = 1;
    float sp = a->speed;
    bool resting = false, lying = false;
    switch (a->state) {
    case ST_MOVE: case ST_IDLE_HOME:
      /* a blow plays out (its follow-through and settle) while the body stands; moving off fades it */
      if (r->act >= 0 && (r->act >= ACT_ATTACKS || sp > 0.6f || !act_on(r)) && r->act != act_index("rise")) act_stop(r);
      if (r->act == act_index("rise") && sp > 0.6f) act_stop(r);
      break;
    case ST_ATTACK: {
      const attack_def *d = &ATK[a->atk];
      /* the shield bearer's shove has its own clip (the rules share the lunge's); the overheads share one (the slinger's
       * throw, the bosses' hammer), and the bosses' spin is the heavy */
      const char *c = a->atk == A_BASH ? "bash" : a->atk == A_THROW || !strcmp(d->clip, "hammer") ? "slam" : !strcmp(d->clip, "spin") ? "heavy" : d->clip;
      float st = strike_time(c);
      sc = st / ((float)d->on / TPS);
      play(r, c, 0.08f, sc, newst);
      break;
    }
    case ST_DODGE: { const char *c = a->dodge_dir ? "backstep" : "roll"; play(r, c, 0.06f, clip_len(a->kind, c) / ((float)CW_MAX(1, a->st_len) / TPS), newst); break; }
    case ST_HURT: play(r, a->st_len > 24 ? "hit_hard" : "hit", 0.06f, 1, newst); break;
    case ST_DOWN: {
      /* the fall fills the time before the get-up, the get-up its 54 ticks */
      int fall = CW_MAX(1, a->st_len - 54);
      bool up = a->st_t >= fall;
      play(r, up ? "getup" : "knockdown", 0.12f, clip_len(a->kind, up ? "getup" : "knockdown") / ((float)(up ? 54 : fall) / TPS), newst || (up && r->act != act_index("getup")));
      lying = !up;
      break;
    }
    case ST_DRINK: play(r, "drink", 0.15f, clip_len(a->kind, "drink") / (54.0f / TPS), newst); break;
    case ST_ACT: {
      const char *c = a->atk < 0 ? "rise" : g_obj[a->atk].kind == OB_CHEST ? "pickup" : "push";
      play(r, c, 0.15f, clip_len(a->kind, c) / ((float)CW_MAX(1, a->st_len) / TPS), newst);
      break;
    }
    case ST_REST:
      if (a->st_t < 36) play(r, "kneel", 0.2f, clip_len(a->kind, "kneel") / (36.0f / TPS), newst);
      resting = true;
      break;
    case ST_ROAR: if (lurk) play(r, "getup", 0.15f, 1, newst); else play(r, "roar", 0.15f, clip_len(a->kind, "roar") / ((float)CW_MAX(1, a->st_len) / TPS), newst); break;
    case ST_DEAD:
      if (newst && !r->rd && !r->still) {
        rag_start(r, a, a->last_hit_by >= 0 ? cw_atan2(a->mv.position.x - S.a[a->last_hit_by].mv.position.x, a->mv.position.z - S.a[a->last_hit_by].mv.position.z) : a->yaw + CW_PI);
        if (!r->rd) play(r, "knockdown", 0.1f, 1, true);
      }
      lying = !r->rd;
      break;
    }
    if (r->tree.id) {
      cw_node t = r->tree;
      /* the locomotion: the velocity in the body's frame (x its left), the stand-to-run speed, armed or not */
      float cy = cw_cos(a->yaw), sy = cw_sin(a->yaw), vx = a->mv.velocity.x, vz = a->mv.velocity.z;
      float lx = vx * cy - vz * sy, lz = vx * sy + vz * cy;
      float lsp = sqrtf(lx * lx + lz * lz);
      if (lsp > 1e-3f && sp > 0) lx *= sp / lsp, lz *= sp / lsp; else lx = lz = 0;
      if (a->state != ST_MOVE && a->state != ST_IDLE_HOME) lx = lz = 0, sp = 0;
      bool facing = (i < MAXP && a->target >= 0) || (i >= MAXP && a->aggro && !a->token);
      if (!facing) lz = sp, lx = 0; /* running free it faces the way it goes */
      /* the blend space's place eases toward the body's velocity (a tenth of a second or so): a start, a stop or a
       * turn moves through the blend instead of jumping to the new clip */
      /* a share of the way a tick, speeding up at most 4.8 m/s a second (a start runs through the walk), slowing at
       * most 12 (a stop is quick, the feet barely slide) */
      const float k = 0.2f, up = 0.08f, dn = 0.2f;
      float tx = (lx - r->bx) * k, ty = (lz - r->by) * k, ts = (sp - r->bs) * k;
      bool faster = fabsf(lx) + fabsf(lz) > fabsf(r->bx) + fabsf(r->by);
      r->bx += cw_clampf(tx, faster ? -up : -dn, faster ? up : dn), r->by += cw_clampf(ty, faster ? -up : -dn, faster ? up : dn);
      r->bs += cw_clampf(ts, -dn, up);
      cw_animtree_set_param(t, "mx", r->bx);
      cw_animtree_set_param(t, "my", r->by);
      cw_animtree_set_param(t, "speed", r->bs);
      /* past the run's own speed the cycle runs faster (no sliding feet) */
      cw_animtree_set_param(t, "rate", r->bs > r->gv[1] ? cw_clampf(r->bs / r->gv[1], 1, 1.6f) : 1);
      float armed = a->idx < MAXP || a->aggro ? 1.0f : 0.0f;
      r->armed = r->armed < 0 ? armed : r->armed + (armed - r->armed) * 0.08f;
      cw_animtree_set_param(t, "armed", r->armed);
      /* the guard over the walks while it fights (locked on, a foe on the hunt) or walks; running free swings the arms */
      float gw = r->armed * (facing ? 1 : cw_clampf((r->gv[1] - r->bs) / fmaxf(0.5f, r->gv[1] - r->gv[0]), 0, 1));
      r->guard += (gw - r->guard) * 0.1f;
      cw_animtree_set_param(t, "guard", r->guard);
      cw_animtree_set_param(t, "resting", resting ? 1 : 0);
      cw_animtree_set_param(t, "lying", lying ? 1 : 0);
      /* the leans: into a turn by its sideways pull (speed x turn rate), forward into a start and back into a stop */
      float yr = wrapf(a->yaw - r->last_yaw) * TPS, acc = (a->speed - r->last_speed) * TPS;
      bool moving = a->state == ST_MOVE && !newst;
      float ws = moving ? cw_clampf(yr * a->speed / 8.0f, -1, 1) : 0, wf = moving ? cw_clampf(acc / 16.0f, -1, 1) : 0;
      r->lean_s += (ws - r->lean_s) * 0.12f, r->lean_f += (wf - r->lean_f) * 0.1f;
      float ls = i == 0 && LEAN_FORCE[0] < 98 ? LEAN_FORCE[0] : r->lean_s, lf = i == 0 && LEAN_FORCE[1] < 98 ? LEAN_FORCE[1] : r->lean_f;
      cw_animtree_set_param(t, "lean_l", fmaxf(0, ls));
      cw_animtree_set_param(t, "lean_r", fmaxf(0, -ls));
      cw_animtree_set_param(t, "lean_fw", fmaxf(0, lf));
      cw_animtree_set_param(t, "lean_bk", fmaxf(0, -lf));
      r->last_yaw = a->yaw, r->last_speed = a->speed;
      /* the look: the chest, the neck and the head toward what it fights, eased in and out. A hero locked on looks at
       * its foe; free, at the threat it can see (a foe winding up a blow at it first, else the nearest foe on the hunt,
       * within 9 m and within the turn the chest, neck and head can make together); a foe at whom it hunts. Full in a
       * stand or a walk, most of the way through a hit or the flask, half through a blow (the cut's own twist leads),
       * none in a roll, a fall or a rest. The point looked at glides from one face to the next. */
      int tg = a->target;
      bool threat = false;
      if (i < MAXP && (tg < 0 || !S.a[tg].used || S.a[tg].state == ST_DEAD)) {
        tg = -1;
        float best = 1e9f;
        for (int k = MAXP; k < MAXA; k++) {
          const actor *o = &S.a[k];
          if (!o->used || !o->active || o->state == ST_DEAD || !o->aggro) continue;
          float dx = o->mv.position.x - a->mv.position.x, dz = o->mv.position.z - a->mv.position.z, d = sqrtf(dx * dx + dz * dz);
          if (d > 9 || fabsf(wrapf(cw_atan2(dx, dz) - a->yaw)) > 1.8f) continue;
          if (o->state == ST_ATTACK && o->target == i && o->st_t < ATK[o->atk].on) d -= 6; /* winding up at it: first */
          if (d < best) best = d, tg = k;
        }
        threat = tg >= 0;
      }
      float want = 0;
      if (tg >= 0 && tg < MAXA && S.a[tg].used && S.a[tg].state != ST_DEAD) {
        want = a->state == ST_MOVE || a->state == ST_IDLE_HOME ? 1 : a->state == ST_ATTACK ? 0.5f : a->state == ST_HURT || a->state == ST_DRINK ? 0.7f : 0;
        if (threat) want *= 0.85f;
        const actor *o = &S.a[tg];
        t3_vec3 hp = { o->mv.position.x, o->mv.position.y + KIND[o->kind].height * KIND[o->kind].scale * 0.9f, o->mv.position.z };
        if (r->look_w < 0.02f) r->look_at = hp; /* (from nothing: straight there) */
        float g = 0.25f;
        r->look_at = (t3_vec3){ r->look_at.x + (hp.x - r->look_at.x) * g, r->look_at.y + (hp.y - r->look_at.y) * g, r->look_at.z + (hp.z - r->look_at.z) * g };
        for (int k = 0; k < 3; k++) cw_node_set(r->look[k], "target_position", cw_val_vec3(r->look_at.x, r->look_at.y, r->look_at.z));
      }
      if (i == 0 && LOOK_FORCE < 998) { /* (the test of the look: a point 5 m off, that far round) */
        float yw = a->yaw + LOOK_FORCE * CW_PI / 180;
        r->look_at = (t3_vec3){ a->mv.position.x + cw_sin(yw) * 5, a->mv.position.y + 1.6f, a->mv.position.z + cw_cos(yw) * 5 };
        for (int k = 0; k < 3; k++) cw_node_set(r->look[k], "target_position", cw_val_vec3(r->look_at.x, r->look_at.y, r->look_at.z));
        want = 1;
      }
      r->look_w += (want - r->look_w) * 0.15f;
      if (a->state == ST_DODGE || a->state == ST_DOWN) r->look_w *= 0.6f; /* (a roll turns the body round: the head goes with it within a few ticks, not held on the foe past its limit) */
      static const float SHARE[3] = { 0.4f, 0.65f, 1.0f };
      for (int k = 0; k < 3; k++) cw_node_set(r->look[k], "influence", cw_val_float(r->look_w * SHARE[k]));
      /* the far ones sample their clips less often; a blow's hit-stop holds every clip with the world */
      float d2 = dist2(a->mv.position.x, a->mv.position.z, S.cam_pos.x, S.cam_pos.z);
      cw_animtree_set_update_divisor(t, i < MAXP ? 1 : cw_anim_lod_divisor(sqrtf(d2), 18));
      bool held = (g_held_now || S.hitstop > 0) && a->state != ST_DEAD;
      if (r->fresh) r->fresh = false; /* (the modifiers bind this tick, on the model as it was built) */
      else if (near && !r->rd && !r->still) r->adv_due = true, r->adv_dt = held ? 0 : 1.0f / TPS;
    }
    /* the weapon in hand, the flask while drinking */
    if (i < MAXP && r->inst) {
      t3_object *sw = NULL, *mu = t3_object_get_by_name(r->inst, "Maul"), *fl = t3_object_get_by_name(r->inst, "Flask");
      t3_object *hand = t3_object_get_by_name(r->inst, "HandR");
      if (hand) for (int c = 0; c < hand->child_count; c++) if (hand->children[c] != mu) sw = hand->children[c];
      if (sw) sw->visible = a->weapon == 0;
      if (mu) mu->visible = a->weapon == 1;
      if (fl) fl->visible = a->state == ST_DRINK;
    }
    /* the feet: planted while standing and walking; free in the air, the roll, a fall */
    bool plant = !lurk && a->mv.on_floor && (a->state == ST_MOVE || a->state == ST_IDLE_HOME || a->state == ST_ATTACK || a->state == ST_HURT || a->state == ST_DRINK || a->state == ST_ACT || a->state == ST_ROAR);
    /* eased in and out, so the legs never snap onto the ground or off it (a roll's end, a get-up) */
    /* and only at a stand or a walk: in a run the feet are off the ground most of the cycle and the solve would fight
     * the stride (a leg flipped round its knee from one tick to the next) */
    float stride = cw_clampf((3.2f - fmaxf(a->speed, r->bs)) / 1.6f, 0, 1); /* (bs: the clips' own pace, slowing after a stop) */
    r->feet_w += ((plant ? stride : 0.0f) - r->feet_w) * 0.2f;
    cw_node_set(r->feet, "active", cw_val_bool(!r->rd && !r->still && r->feet_w > 0.01f));
    cw_node_set(r->feet, "influence", cw_val_float(cw_clampf(r->feet_w, 0, 1)));
    if (plant && i == 0) {
      cw_foot_state fs;
      VST.feet = 0, VST.foot_err = 0;
      for (int l = 0; l < 2 && cw_foot_placement_leg(r->feet, l, &fs); l++) VST.feet += fs.hit;
    }
    /* the tell: the light swells through the wind-up and fades over the blow (never cut off) */
    if (r->light.id) {
      float e = 0;
      if (a->state == ST_ATTACK) {
        const attack_def *d = &ATK[a->atk];
        if (a->st_t < d->on) e = 18 * cw_clampf((float)a->st_t / (float)d->on, 0, 1);
        else { float k = cw_clampf(1 - (float)(a->st_t - d->on) / 12.0f, 0, 1); e = 18 * k * k; }
      }
      cw_node_set(r->light, "energy", cw_val_float(e * (a->boss ? 2 : 1)));
      cw_node_set(r->light, "visible", cw_val_bool(e > 0.01f)); /* an unlit light still costs the clusters */
    }
    /* the ragdoll: the pose follows the bodies; when it settles it is kept as it lies */
    if (r->rd) {
      cw_pose_capture(&r->pose);
      t3_mat4 m = model_to_world(a->mv.position, a->yaw);
      cw_node3d_set_position(r->root, a->mv.position);
      cw_ragdoll_blend_pose(r->rd, &r->pose, &m, 1);
      cw_pose_flush(&r->pose);
      if (cw_ragdoll_resting(r->rd, 0.1f) || ++r->rd_t > 200) {
        cw_ragdoll_destroy(r->rd), r->rd = NULL, rag_live--;
        r->still = true;
      }
    } else if (r->still && r->bound) cw_pose_flush(&r->pose);
    /* the blade's ribbon: from the tip of the weapon in the hand (as the clip swings it), through the live part of the
     * blow (the rules' hit window, a few ticks either side) */
    if (r->trail.id) {
      t3_vec3 bp;
      bool on = near && blade_at(a, &bp);
      t3_object *hand = on && r->inst ? t3_object_get_by_name(r->inst, "HandR") : NULL, *wp = NULL;
      if (hand) for (int c = 0; c < hand->child_count; c++) if (hand->children[c]->visible) wp = hand->children[c];
      if (wp) {
        t3_object_update_world_matrix(wp, true, false);
        bp = t3_vec3_apply_mat4((t3_vec3){ 0, 0, weapon_tip(a->kind, a->weapon) }, &wp->matrix_world);
      }
      on = on && wp;
      if (on) cw_node3d_set_position(r->trail, bp);
      cw_node_set(r->trail, "emitting", cw_val_bool(on));
      trail_sample(i, a, r, on);
    }
    /* the shield: up in front while it holds, down at the side once it is knocked aside */
    if (r->shield.id) {
      bool up = a->guard > 0 && a->state != ST_DEAD;
      float g = GEN_SCALE[a->kind];
      cw_node3d_set_position(r->shield, up ? (t3_vec3){ -0.2f * g, 1.12f * g, 0.42f * g } : (t3_vec3){ -0.42f * g, 0.75f * g, 0.05f * g });
      cw_node3d_set_rotation(r->shield, up ? (t3_vec3){ CW_PI / 2, 0, 0 } : (t3_vec3){ CW_PI / 2, -CW_PI / 2 + 0.3f, 0.2f });
      cw_node_set(r->shield, "visible", cw_val_bool(near && a->state != ST_DEAD)); /* (dropped with the body) */
    }
    mrec_due[i] = true;
    r->last_state = a->state, r->last_st_t = a->st_t, r->last_atk = a->atk;
  }
}

/* ── per frame ─────────────────────────────────────────────────────── */
static t3_vec3 CAM_P, CAM_F;
static float CAM_FOV = 60, CAM_ASPECT = 16.0f / 9;
void view_camera(cw_node cam, float alpha, float aspect) {
  t3_vec3 p = { lerpf(S.cam_pos_prev.x, S.cam_pos.x, alpha), lerpf(S.cam_pos_prev.y, S.cam_pos.y, alpha), lerpf(S.cam_pos_prev.z, S.cam_pos.z, alpha) };
  t3_vec3 t = { lerpf(S.cam_target_prev.x, S.cam_target.x, alpha), lerpf(S.cam_target_prev.y, S.cam_target.y, alpha), lerpf(S.cam_target_prev.z, S.cam_target.z, alpha) };
  cw_node3d_set_position(cam, p);
  cw_node3d_look_at(cam, t, (t3_vec3){ 0, 1, 0 });
  float dx = t.x - p.x, dy = t.y - p.y, dz = t.z - p.z, l = sqrtf(dx * dx + dy * dy + dz * dz) + 1e-6f;
  CAM_P = p, CAM_F = (t3_vec3){ dx / l, dy / l, dz / l };
  CAM_ASPECT = aspect;
  cw_value v;
  CAM_FOV = cw_node_get_prop(cam, "fov", &v) && v.f > 1 ? (float)v.f : 60;
}
/* the figures' level of detail: cwpack simplifies each mesh (half and a quarter of the triangles); a figure draws the
 * coarsest level whose error stays under a pixel and a half on the screen (a foe across a courtyard, a boss from the
 * far side of its ring) */
void view_lod(cw_node cam, int height) {
  const t3_camera *c = cw_camera3d_t3(cam);
  if (!c) return;
  for (int i = 0; i < MAXA; i++)
    if (RIG[i].made && RIG[i].inst) cw_gltf_instance_auto_lod(RIG[i].inst, c, (float)height, 1.5f);
}
bool view_screen(t3_vec3 w, float *u, float *v) {
  t3_vec3 f = CAM_F, up = { 0, 1, 0 };
  t3_vec3 rt = { f.y * up.z - f.z * up.y, f.z * up.x - f.x * up.z, f.x * up.y - f.y * up.x };
  float rl = sqrtf(rt.x * rt.x + rt.y * rt.y + rt.z * rt.z) + 1e-6f;
  rt.x /= rl, rt.y /= rl, rt.z /= rl;
  t3_vec3 uu = { rt.y * f.z - rt.z * f.y, rt.z * f.x - rt.x * f.z, rt.x * f.y - rt.y * f.x };
  float dx = w.x - CAM_P.x, dy = w.y - CAM_P.y, dz = w.z - CAM_P.z;
  float z = dx * f.x + dy * f.y + dz * f.z;
  if (z < 0.2f) return false;
  float th = tanf(CAM_FOV * 0.5f * CW_PI / 180);
  float x = (dx * rt.x + dy * rt.y + dz * rt.z) / (z * th * CAM_ASPECT), y = (dx * uu.x + dy * uu.y + dz * uu.z) / (z * th);
  *u = 0.5f + x * 0.5f, *v = 0.5f - y * 0.5f;
  return *u > -0.1f && *u < 1.1f && *v > -0.1f && *v < 1.1f;
}

/* ── the lock-on framing recorder (debug channel): what tests/emberthrone-lockon.mjs measures ──────────────────
 * "framing rec <frames>" samples each drawn frame while the lead player is locked on: the hero's and the target's
 * screen boxes (an upright box round each body: its feet to the top of its head, its shoulders' width), projected by
 * the camera as drawn. "framing get" returns the counts: samples, the hero's mean and largest height (share of the
 * frame), the target's smallest share inside the frame, the largest share of the target's box the hero covers, and how
 * many samples had the target cut by an edge or covered more than a quarter by the hero. */
static struct { int want, n, cut, hidden; double hero_h, hero_hmax, tgt_h, vis_min, occ_max, occ_sum, dist_sum; } FRM;
static bool body_box(const actor *a, float b[4]) {
  float r = fmaxf(0.32f, KIND[a->kind].radius) * KIND[a->kind].scale, top = KIND[a->kind].height * KIND[a->kind].scale;
  b[0] = b[1] = 1e9f, b[2] = b[3] = -1e9f;
  for (int k = 0; k < 8; k++) {
    t3_vec3 w = { a->mv.position.x + (k & 1 ? r : -r), a->mv.position.y + (k & 2 ? top : 0), a->mv.position.z + (k & 4 ? r : -r) };
    float u, v;
    t3_vec3 f = CAM_F;
    float z = (w.x - CAM_P.x) * f.x + (w.y - CAM_P.y) * f.y + (w.z - CAM_P.z) * f.z;
    if (z < 0.2f) return false;
    view_screen(w, &u, &v);
    b[0] = fminf(b[0], u), b[1] = fminf(b[1], v), b[2] = fmaxf(b[2], u), b[3] = fmaxf(b[3], v);
  }
  return true;
}
static void framing_sample(void) {
  if (FRM.n >= FRM.want) return;
  const actor *p = &S.a[0];
  if (!p->used || p->target < 0) return;
  const actor *t = &S.a[p->target];
  if (!t->used || t->state == ST_DEAD) return;
  float hb[4], tb[4];
  bool hv = body_box(p, hb), tv = body_box(t, tb);
  float ta = (tb[2] - tb[0]) * (tb[3] - tb[1]);
  float vis = 0, occ = 0;
  if (tv && ta > 1e-6f) {
    float ix = fmaxf(0, fminf(tb[2], 1) - fmaxf(tb[0], 0)), iy = fmaxf(0, fminf(tb[3], 1) - fmaxf(tb[1], 0));
    vis = ix * iy / ta;
    if (hv) {
      float ox = fmaxf(0, fminf(tb[2], hb[2]) - fmaxf(tb[0], hb[0])), oy = fmaxf(0, fminf(tb[3], hb[3]) - fmaxf(tb[1], hb[1]));
      occ = ox * oy / ta;
    }
  }
  float hh = hv ? hb[3] - hb[1] : 1;
  FRM.n++;
  FRM.hero_h += hh, FRM.hero_hmax = fmax(FRM.hero_hmax, hh), FRM.tgt_h += tv ? tb[3] - tb[1] : 0;
  FRM.vis_min = fmin(FRM.vis_min, vis), FRM.occ_max = fmax(FRM.occ_max, occ), FRM.occ_sum += occ;
  FRM.cut += vis < 0.98f, FRM.hidden += occ > 0.25f;
  FRM.dist_sum += sqrtf(dist2(p->mv.position.x, p->mv.position.z, t->mv.position.x, t->mv.position.z));
}
static void cmd_framing(int argc, const char **argv, cw_reply *rp) {
  if (argc >= 2 && !strcmp(argv[1], "rec")) {
    memset(&FRM, 0, sizeof FRM);
    FRM.want = argc >= 3 ? atoi(argv[2]) : 600, FRM.vis_min = 1;
    cw_reply_printf(rp, "{\"recording\":%d}", FRM.want);
    return;
  }
  double n = FRM.n ? FRM.n : 1;
  cw_reply_printf(rp, "{\"samples\":%d,\"hero_h\":%.3f,\"hero_h_max\":%.3f,\"target_h\":%.3f,\"target_vis_min\":%.3f,\"occ_mean\":%.3f,\"occ_max\":%.3f,\"cut\":%d,\"hidden\":%d,\"dist\":%.2f}",
                  FRM.n, FRM.hero_h / n, FRM.hero_hmax, FRM.tgt_h / n, FRM.vis_min, FRM.occ_sum / n, FRM.occ_max, FRM.cut, FRM.hidden, FRM.dist_sum / n);
}

void view_sync(float alpha) {
  (void)alpha;
  framing_sample();
  for (int i = 0; i < MAXA; i++) if (mrec_due[i]) mrec_due[i] = false, mrec_sample(i);
  /* warning rings under the area blows, through their wind-up */
  int n = 0;
  for (int i = MAXP; i < MAXA && n < 8; i++) {
    const actor *a = &S.a[i];
    if (!a->used || !a->active || a->state != ST_ATTACK || !ATK[a->atk].ring) continue;
    const attack_def *d = &ATK[a->atk];
    if (a->st_t > d->off + 10) continue;
    /* in over 8 ticks, out over 10 after the blow: no ring pops */
    float op = cw_clampf((float)a->st_t / 8.0f, 0, 1) * cw_clampf(1 - (float)(a->st_t - d->off) / 10.0f, 0, 1);
    RING_MAT[n]->opacity = 0.75f * op;
    float sc = KIND[a->kind].scale, r = d->ring_r * sc * 1.0f;
    float fwd = d->arc > -1 ? 1.4f * sc : 0;
    float x = a->mv.position.x + cw_sin(a->yaw) * fwd, z = a->mv.position.z + cw_cos(a->yaw) * fwd;
    float k = cw_clampf((float)a->st_t / (float)d->on, 0, 1);
    cw_node_set(V_ring[n], "visible", cw_val_bool(true));
    cw_node3d_set_position(V_ring[n], (t3_vec3){ x, world_ground(x, z, a->mv.position.y + 1.5f) + 0.06f, z });
    cw_node3d_set_rotation(V_ring[n], (t3_vec3){ -CW_PI / 2, 0, 0 });
    float s = r * (0.35f + 0.65f * k);
    cw_node3d_set_scale(V_ring[n], (t3_vec3){ s, s, s });
    n++;
  }
  for (; n < 8; n++) cw_node_set(V_ring[n], "visible", cw_val_bool(false));
  for (int k = 0; k < 4; k++) {
    const shock *s = &S.sh[k];
    cw_node_set(V_shock[k], "visible", cw_val_bool(s->on));
    if (!s->on) continue;
    SHOCK_MAT[k]->opacity = 0.75f * cw_clampf(s->r / 1.5f, 0, 1) * cw_clampf((18 - s->r) / 4.0f, 0, 1);
    cw_node3d_set_position(V_shock[k], (t3_vec3){ s->x, s->y + 0.15f, s->z });
    cw_node3d_set_rotation(V_shock[k], (t3_vec3){ -CW_PI / 2, 0, 0 });
    cw_node3d_set_scale(V_shock[k], (t3_vec3){ s->r, s->r, s->r });
  }
  for (int k = 0; k < MAXSTONE; k++) { /* the stones in flight */
    const stone *st = &S.st[k];
    cw_node_set(V_stone[k], "visible", cw_val_bool(st->on));
    cw_node_set(V_stone_trail[k], "emitting", cw_val_bool(st->on));
    if (st->on) cw_node3d_set_position(V_stone[k], (t3_vec3){ st->x, st->y, st->z });
  }
}
