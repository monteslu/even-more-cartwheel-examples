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
 * lurker waits under the water and rises. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/anim.h"
#include "cartwheel/debug.h"
#include "cartwheel/ik.h"
#include "cartwheel/ragdoll.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/scene_file.h"
#include "game.h"

bool g_hide_hud;

typedef struct rig {
  bool made;
  int kind;
  cw_node root, model, anim, feet, light, flask, trail, shield;
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
} rig;
static rig RIG[MAXA];
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

static void shadows(t3_object *o) {
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH) o->cast_shadow = !(o->name && !strncmp(o->name, "Hair", 4)), o->receive_shadow = true;
  for (int i = 0; i < o->child_count; i++) shadows(o->children[i]);
}
static void cast_shadows(t3_object *o, bool on) {
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH) o->cast_shadow = on && !(o->name && !strncmp(o->name, "Hair", 4));
  for (int i = 0; i < o->child_count; i++) cast_shadows(o->children[i], on);
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

static void rig_make(int i, int kind) {
  rig *r = &RIG[i];
  memset(r, 0, sizeof *r);
  r->kind = kind;
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
    cw_anim_player_add(r->anim, g->gltf->animations[k]->name, a);
    cw_anim_release(a);
  }
  /* feet on the ground: rays against the world's layer only (never a ragdoll) */
  r->feet = cw_node_new("FootPlacement3D", "Feet");
  cw_node_add_child(r->model, r->feet);
  cw_node_set(r->feet, "legs", cw_val_string("UpperLegL,LowerLegL,FootL;UpperLegR,LowerLegR,FootR"));
  cw_node_set(r->feet, "pelvis", cw_val_string("Hips"));
  cw_node_set(r->feet, "collision_mask", cw_val_int(1));
  cw_node_set(r->feet, "foot_height", cw_val_float(0.08f * GEN_SCALE[kind]));
  cw_node_set(r->feet, "max_step_down", cw_val_float(0.55f * GEN_SCALE[kind]));
  cw_node_set(r->feet, "ray_up", cw_val_float(0.6f * GEN_SCALE[kind]));
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
    cw_node_set(r->trail, "width", cw_val_float(boss ? 0.9f : 0.45f));
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
  t3_release(inst);
  r->made = true, r->shadow = -1;
}

void view_reset(void) {
  for (int i = 0; i < MAXA; i++) {
    if (RIG[i].rd) cw_ragdoll_destroy(RIG[i].rd);
    cw_pose_free(&RIG[i].pose);
    if (RIG[i].made && RIG[i].trail.id && cw_node_valid(RIG[i].trail)) cw_node_free(RIG[i].trail); /* (the ribbons hang off the world, not the rig) */
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
    { /* the shield (a round of planks with an iron rim: a disc facing forward) and the slingers' stones */
      /* (the world's own wood and stone: the programs the gates and doors already draw with) */
      extern t3_material *world_material(int mt, float tint);
      t3_geometry *g = t3_cylinder_geometry_new(0.38f, 0.38f, 0.07f, 20, 1, false);
      SHIELD_MESH = t3_mesh_new(g, world_material(MT_WOOD, 1));
      t3_release(g);
      g = t3_icosahedron_geometry_new(0.09f, 1);
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

/* ── clips ─────────────────────────────────────────────────────────── */
/* where the blow lands in each clip (seconds): an attack's clip runs at the speed that puts this on the hit frame */
static float strike_time(const char *clip) {
  static const struct { const char *c; float t; } T[] = { { "slash1", 0.2f }, { "slash2", 0.19f }, { "slash3", 0.28f }, { "heavy", 0.52f }, { "swing", 0.27f },
                                                          { "slam", 0.64f }, { "stab", 0.2f }, { "haymaker", 0.5f }, { "spin", 0.2f }, { "lariat", 0.12f },
                                                          { "hammer", 0.37f }, { "roar", 0.3f } };
  for (int i = 0; i < (int)(sizeof T / sizeof *T); i++) if (!strcmp(T[i].c, clip)) return T[i].t;
  return 0.2f;
}
static void play(rig *r, const char *clip, float blend, float speed, bool restart) {
  if (!r->made) return;
  if (r->clip == clip && !restart) { cw_node_set(r->anim, "speed_scale", cw_val_float(speed)); return; }
  r->clip = clip;
  cw_anim_player_play(r->anim, clip, blend);
  cw_anim_player_seek(r->anim, 0, false);
  cw_node_set(r->anim, "speed_scale", cw_val_float(speed));
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
 * a point on an arc round the body by the blow's shape (a sweep left or right, a full spin, an overhead, a thrust).
 * It follows the rules, so the ribbon shows exactly where the blow can land. */
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

/* the rig follows its actor, once per fixed tick (main.c calls it after the rules) */
void view_fixed(void) {
  VST.rigs = VST.drawn = 0;
  for (int i = 0; i < MAXA; i++) {
    const actor *a = &S.a[i];
    rig *r = &RIG[i];
    if (!a->used || (!a->active && i >= MAXP)) {
      if (r->made && cw_node_valid(r->root)) cw_node_set(r->root, "visible", cw_val_bool(false));
      if (r->made && r->trail.id) cw_node_set(r->trail, "emitting", cw_val_bool(false));
      if (r->rd && (!a->used || !a->active)) cw_ragdoll_destroy(r->rd), r->rd = NULL, rag_live--;
      if (r->made && !a->used) r->still = false;
      continue;
    }
    if (!r->made || r->kind != a->kind || r->role != a->role) {
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
    if (lurk && a->state == ST_IDLE_HOME) near = false; /* under the water */
    cw_node_set(r->root, "visible", cw_val_bool(near));
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
      if (lurk && a->state == ST_ROAR) { float k = 1 - (float)a->st_t / (float)CW_MAX(1, a->st_len); at.y -= 2.1f * k * k; } /* rising out of the water */
      cw_node3d_set_position(r->root, at);
      cw_node3d_set_rotation(r->root, (t3_vec3){ 0, a->yaw, 0 });
    }
    float sc = 1;
    switch (a->state) {
    case ST_MOVE: case ST_IDLE_HOME: {
      const actor *t = a->target >= 0 && i < MAXP ? &S.a[a->target] : NULL;
      float sp = a->speed;
      if (sp < 0.35f) play(r, a->idx < MAXP || a->aggro ? "idle_w" : "idle", 0.25f, 1, false);
      else if (t || (i >= MAXP && a->aggro && !a->token)) {
        /* moving while facing something: forward, back or a sidestep by the way it moves against its facing */
        float mvy = cw_atan2(a->mv.velocity.x, a->mv.velocity.z), rel = wrapf(mvy - a->yaw);
        const char *c = fabsf(rel) < 0.8f ? (sp > 3.6f ? "run_w" : "walk_w") : fabsf(rel) > 2.3f ? "walk_back" : rel > 0 ? "strafe_l" : "strafe_r";
        play(r, c, 0.2f, cw_clampf(sp / 2.2f, 0.6f, 1.6f), false);
      } else if (sp < 2.6f) play(r, "walk_w", 0.22f, cw_clampf(sp / 1.6f, 0.6f, 1.5f), false);
      else play(r, "run_w", 0.2f, cw_clampf(sp / 5.0f, 0.75f, 1.35f), false);
      break;
    }
    case ST_ATTACK: {
      const attack_def *d = &ATK[a->atk];
      float st = strike_time(d->clip);
      sc = st / ((float)d->on / TPS);
      play(r, d->clip, 0.08f, sc, newst);
      break;
    }
    case ST_DODGE: play(r, a->dodge_dir ? "backstep" : "roll", 0.06f, a->dodge_dir ? 1.0f : 0.62f / (37.0f / TPS), newst); break;
    case ST_HURT: play(r, a->st_len > 24 ? "hit_hard" : "hit", 0.06f, 1, newst); break;
    case ST_DOWN: play(r, a->st_t < a->st_len - 54 ? "knockdown" : "getup", 0.12f, 1, newst); break;
    case ST_DRINK: play(r, "drink", 0.15f, 0.9f / (54.0f / TPS), newst); break;
    case ST_ACT: {
      const char *c = a->atk < 0 ? "rise" : g_obj[a->atk].kind == OB_CHEST ? "pickup" : "push";
      play(r, c, 0.15f, c[0] == 'p' && c[1] == 'i' ? 0.5f / ((float)a->st_len / TPS) : c[0] == 'p' ? 1.0f / ((float)a->st_len / TPS) : 1, newst);
      break;
    }
    case ST_REST: play(r, a->st_t < 36 ? "kneel" : "rest", 0.2f, 1, false); break;
    case ST_ROAR: if (lurk) play(r, "getup", 0.15f, 1, newst); else play(r, "roar", 0.15f, 1.2f / ((float)a->st_len / TPS), newst); break;
    case ST_DEAD:
      if (newst && !r->rd && !r->still) {
        rag_start(r, a, a->last_hit_by >= 0 ? cw_atan2(a->mv.position.x - S.a[a->last_hit_by].mv.position.x, a->mv.position.z - S.a[a->last_hit_by].mv.position.z) : a->yaw + CW_PI);
        if (!r->rd) play(r, "knockdown", 0.1f, 1, true);
      }
      break;
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
    cw_node_set(r->feet, "active", cw_val_bool(plant && !r->rd && !r->still));
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
        cw_node_set(r->anim, "speed_scale", cw_val_float(0));
      }
    } else if (r->still && r->bound) cw_pose_flush(&r->pose);
    /* the blade's ribbon through the live part of the blow */
    if (r->trail.id) {
      t3_vec3 bp;
      bool on = near && blade_at(a, &bp);
      if (on) cw_node3d_set_position(r->trail, bp);
      cw_node_set(r->trail, "emitting", cw_val_bool(on));
    }
    /* the shield: up in front while it holds, down at the side once it is knocked aside */
    if (r->shield.id) {
      bool up = a->guard > 0 && a->state != ST_DEAD;
      float g = GEN_SCALE[a->kind];
      cw_node3d_set_position(r->shield, up ? (t3_vec3){ -0.2f * g, 1.12f * g, 0.42f * g } : (t3_vec3){ -0.42f * g, 0.75f * g, 0.05f * g });
      cw_node3d_set_rotation(r->shield, up ? (t3_vec3){ CW_PI / 2, 0, 0 } : (t3_vec3){ CW_PI / 2, -CW_PI / 2 + 0.3f, 0.2f });
      cw_node_set(r->shield, "visible", cw_val_bool(a->state != ST_DEAD || !r->rd));
    }
    /* a blow's hit-stop holds every clip with the world */
    if ((g_held_now || S.hitstop > 0) && a->state != ST_DEAD) cw_node_set(r->anim, "speed_scale", cw_val_float(0));
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

void view_sync(float alpha) {
  (void)alpha;
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
