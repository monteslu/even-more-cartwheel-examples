/* skate: wipeouts as ragdolls (cartwheel/ragdoll.h), after the brawler's (more-cartwheel-examples, games/brawler).
 *
 * PRESENTATION ONLY. The simulation (skater.c) decides the bail, slides its point to a stop and puts the rider back on
 * the board after a second and a half. Here a bail turns the rider's skeleton into a Box3D ragdoll (a capsule per
 * bone, cone and twist limits, hinged knees and elbows) seeded from the pose it had and carried by the speed it bailed
 * at: it tumbles down a ramp, slides across the flat, folds over a ledge, in a world of its own made from the park's
 * triangles (one static mesh), never read back by the rules. The ragdoll ends when the simulation puts the rider back
 * up. Steps at the fixed tick (the frame runs one per tick in every gate), so every host shows the same tumble. */
#include <string.h>

#include "cartwheel/physics3d.h"
#include "cartwheel/ragdoll.h"
#include "view.h"

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
#define P_CHEST 2

static cw_world3d world;
static cw_mesh3d mesh;
static cw_ragdoll *RD[MAXP];
static int live;

void rag_step(void) {
  if (world.id && live > 0) cw_world3d_step(world, TICK_DT);
}

void rag_free(void) {
  for (int i = 0; i < MAXP; i++) if (RD[i]) cw_ragdoll_destroy(RD[i]), RD[i] = NULL;
  if (world.id) cw_world3d_destroy(world);
  world = (cw_world3d){ 0 };
  if (mesh.id) cw_mesh3d_destroy(mesh);
  mesh = (cw_mesh3d){ 0 };
  live = 0;
}

void rag_park(const park *k) {
  rag_free();
  cw_world3d_def wd = cw_world3d_default_def();
  wd.gravity = (t3_vec3){ 0, -GRAV, 0 };
  wd.workers = 1;
  wd.substeps = 4;
  world = cw_world3d_create(&wd);
  /* the park's ride and wall triangles, one static mesh */
  int n = 0;
  for (int i = 0; i < k->nt; i++) n += k->t[i].surf != SURF_NONE;
  t3_vec3 *vs = malloc(sizeof *vs * (size_t)n * 3);
  int32_t *ix = malloc(sizeof *ix * (size_t)n * 3);
  int m = 0;
  for (int i = 0; i < k->nt; i++) {
    const ptri *t = &k->t[i];
    if (t->surf == SURF_NONE) continue;
    vs[m * 3] = t->a, vs[m * 3 + 1] = t->b, vs[m * 3 + 2] = t->c;
    ix[m * 3] = m * 3, ix[m * 3 + 1] = m * 3 + 1, ix[m * 3 + 2] = m * 3 + 2;
    m++;
  }
  mesh = cw_mesh3d_create(vs, m * 3, ix, m);
  free(vs), free(ix);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  cw_body3d ground = cw_body3d_create(world, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.material.friction = 0.75f;
  cw_shape3d_mesh(ground, &sd, mesh, (t3_vec3){ 1, 1, 1 });
}

bool rag_active(int i) { return RD[i] != NULL; }
bool rag_hips(int i, v3 *out) {
  if (!RD[i]) return false;
  *out = cw_ragdoll_part_transform(RD[i], 0).p;
  return true;
}
int rag_live(void) { return live; }

void rag_tick(int i, const skater *s, rig *r, cw_node model_node, const t3_mat4 *m) {
  (void)model_node;
  if (!world.id || !r->bound) return;
  bool down = s->st == SK_BAIL;
  if (down && !RD[i]) {
    cw_pose_update_model(&r->pose);
    cw_ragdoll_def def = cw_ragdoll_default_def(PARTS, NPARTS);
    def.joint_friction = 3.0f;
    def.angular_damping = 0.8f;
    RD[i] = cw_ragdoll_create(world, &def, &r->pose, m);
    if (getenv("SKATE_BAIL_AT")) fprintf(stderr, "bench: ragdoll %s for seat %d\n", RD[i] ? "made" : "NOT made", i);
    if (RD[i]) {
      live++;
      /* the speed it bailed at carries the body; a tip forward over the nose */
      v3 v = s->bail_v;
      cw_ragdoll_set_velocity(RD[i], vmul(v, 0.9f), vcross(V(0, 1, 0), vmul(vnorm(V(v.x, 0, v.z)), 3.0f)));
      cw_xform3 c = cw_ragdoll_part_transform(RD[i], P_CHEST);
      cw_ragdoll_apply_impulse(RD[i], P_CHEST, vadd(vmul(vnorm(V(v.x, 0, v.z)), 12.0f), V(0, 4, 0)), c.p);
    }
  }
  if (!down && RD[i]) {
    cw_ragdoll_destroy(RD[i]);
    RD[i] = NULL;
    live--;
  }
  if (RD[i]) {
    cw_ragdoll_blend_pose(RD[i], &r->pose, m, 1.0f);
    cw_pose_flush(&r->pose);
  }
}
