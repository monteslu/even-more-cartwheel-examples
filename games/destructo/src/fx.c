/* destructo's effects: what a break, a hit, a blast and a launch look like (render only: they read the
 * simulation's events and never feed it).
 *
 *   breaks     a puff of dust the colour of the material (warm for wood, pale for stone), chips of it thrown out
 *              (instanced boxes on the GPU that bounce off what the camera sees), glass in bright shards, steel
 *              in sparks
 *   hits       a smaller puff and a few chips where a block lands hard on another or the ground
 *   charges    a pooled Explosion3D (the engine's kit: fire and smoke flipbooks, sparks, debris, embers, heat
 *              haze, a ground shockwave, a flash light and a scorch mark); the explosion kit's settings follow
 *              hordeshooter's (more-cartwheel-examples). No camera shake: the shooter's pad rumbles instead
 *   crystals   a burst of sparkles and a swell of their light
 *   launch     a muzzle flash and a puff of smoke from the launcher
 *   collapse   a plume of dust that hangs over the pile, drifts downwind and thins over about ten seconds, and the
 *              ground marked where the structure came down (crushed stone and dust, splinters under wood)
 * No strobing (tests/destructo-flash.mjs): every light swells over a tenth of a second or more and fades smoothly,
 * never steps or flickers. The explosion kit's own flash light is off for that reason (it starts at its peak and
 * flickers at about 11 Hz); the blast's light here is one of the swelling lights.
 * Each kind is one system of burst slots (one simulation pass and one draw for the pool). A frame fires at most a
 * few bursts of each kind: a collapse of a thousand blocks makes a cloud, not a thousand clouds. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/explosions.h"
#include "cartwheel/input.h"
#include "cartwheel/particles.h"
#include "fx.h"

#define BOOMS 3
#define LIGHTS 6
#define MARKS 28

static struct {
  bool ready;
  cw_node root;
  cw_node boom[BOOMS];
  int nboom;
  cw_node dust_warm, dust_pale, dust_big, plume, chips[M_COUNT], sparks, glass, sparkle, muzzle, smoke;
  cw_node light[LIGHTS];
  float light_t[LIGHTS], light_peak[LIGHTS], light_rise[LIGHTS], light_fall[LIGHTS];
  int nl;
  /* ground marks: a pool of decals, the oldest reused */
  cw_node mark[MARKS];
  t3_vec3 mark_at[MARKS];
  float mark_t[MARKS], mark_size[MARKS], mark_a[MARKS];
  int nmark, plume_wait, plumes;
  bool frame_dirty;
  int per_tick[16];
  float rum_low, rum_high;
  int rum_ms, rum_player;
  fx_stats st;
} FX;

static cw_node pnode(cw_node parent, const char *name, const char *const *kv, int seed) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_add_child(parent, n);
  for (int i = 0; kv[i]; i += 2) {
    cw_value v;
    const cw_prop *p = cw_node_find_prop(n, kv[i], NULL);
    if (p && p->type == CW_T_ENUM) {
      cw_node_set(n, kv[i], cw_val_enum(kv[i + 1]));
      continue;
    }
    if (!p || !cw_value_parse(kv[i + 1], p->type, &v)) {
      cw_warn("destructo: bad particle setting %s = %s", kv[i], kv[i + 1]);
      continue;
    }
    cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "seed", cw_val_int(seed));
  cw_node3d_set_position(n, cw_node_is(parent, "Explosion3D") ? (t3_vec3){ 0, 0.3f, 0 } : (t3_vec3){ 0, -60, 0 });
  return n;
}
/* a pool of one-shots as one system of burst slots */
static cw_node bnode(const char *name, const char *const *kv, int seed, int slots) {
  cw_node n = pnode(FX.root, name, kv, seed);
  cw_value v;
  int per = cw_node_get_prop(n, "amount", &v) ? (int)v.i : 1;
  cw_node_set(n, "bursts", cw_val_int(slots));
  cw_node_set(n, "amount", cw_val_int(per * slots));
  cw_node3d_set_position(n, (t3_vec3){ 0, 0, 0 });
  return n;
}

/* ── the settings ───────────────────────────────────────────────────── */

static const char *const DUST_WARM[] = { "amount", "10", "lifetime", "2.6", "lifetime_random", "0.3", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.35", "direction", "vec3(0, 1, 0)", "spread", "110", "speed_min", "0.4", "speed_max", "2.0",
  "gravity", "vec3(0, 0.25, 0)", "damping", "1.6", "blend", "ALPHA", "soft_distance", "0.5", "size", "1.6", "size_random", "0.4",
  "size_curve", "\"0 0.35, 0.25 1, 1 1.6\"", "color_ramp", "\"0 0.82 0.66 0.48 0, 0.2 0.78 0.62 0.45 0.6, 1 0.7 0.6 0.5 0\"",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", "spin", "0.25", "turbulence", "0.5", "turbulence_scale", "0.4", NULL };
static const char *const DUST_PALE[] = { "amount", "10", "lifetime", "3.0", "lifetime_random", "0.3", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.4", "direction", "vec3(0, 1, 0)", "spread", "120", "speed_min", "0.4", "speed_max", "2.2",
  "gravity", "vec3(0, 0.2, 0)", "damping", "1.5", "blend", "ALPHA", "soft_distance", "0.5", "size", "1.9", "size_random", "0.4",
  "size_curve", "\"0 0.35, 0.25 1, 1 1.7\"", "color_ramp", "\"0 0.8 0.78 0.72 0, 0.2 0.76 0.74 0.7 0.55, 1 0.7 0.68 0.66 0\"",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", "spin", "0.2", "turbulence", "0.5", "turbulence_scale", "0.4", NULL };
/* the cloud a falling storey raises: few, big, slow */
static const char *const DUST_BIG[] = { "amount", "14", "lifetime", "4.5", "lifetime_random", "0.3", "explosiveness", "0.8", "one_shot", "true",
  "emission_shape", "BOX", "emission_extents", "vec3(1.6, 0.2, 1.6)", "direction", "vec3(0, 1, 0)", "spread", "80", "speed_min", "0.5", "speed_max", "2.5",
  "gravity", "vec3(0, 0.15, 0)", "damping", "1.0", "blend", "ALPHA", "soft_distance", "0.8", "size", "3.6", "size_random", "0.4",
  "size_curve", "\"0 0.4, 0.3 1, 1 1.5\"", "color_ramp", "\"0 0.66 0.6 0.52 0, 0.25 0.64 0.58 0.52 0.5, 1 0.6 0.56 0.52 0\"",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", "spin", "0.12", "turbulence", "0.6", "turbulence_scale", "0.3", "camera_fade", "4", NULL };
static const char *const CHIPS[] = { "amount", "14", "lifetime", "2.0", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.25", "direction", "vec3(0, 1, 0)", "spread", "100", "speed_min", "2", "speed_max", "7",
  "gravity", "vec3(0, -9.8, 0)", "draw_mode", "MESH", "mesh_primitive", "BOX", "blend", "OPAQUE", "size", "0.09", "size_random", "0.6",
  "spin", "9", "bounce", "0.3", "friction", "0.6", "collide_depth", "true", "collision_radius", "0.03", NULL };
static const char *const GLASS[] = { "amount", "26", "lifetime", "1.6", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "120", "speed_min", "1.5", "speed_max", "6",
  "gravity", "vec3(0, -9.8, 0)", "draw_mode", "MESH", "mesh_primitive", "BOX", "blend", "ADD", "size", "0.07", "size_random", "0.7",
  "color", "color(0.55, 0.9, 1.0, 1)", "energy", "2.2", "spin", "12", "bounce", "0.35", "collide_depth", "true", "collision_radius", "0.02",
  "color_ramp", "\"0 1 1 1 1, 0.7 0.8 0.95 1 0.8, 1 0.6 0.9 1 0\"", NULL };
static const char *const SPARKS[] = { "amount", "30", "lifetime", "0.7", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "180", "speed_min", "3", "speed_max", "10", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -12, 0)", "draw_mode", "STRETCHED", "stretch", "0.04", "size", "0.04",
  "color_ramp", "\"0 1 0.95 0.7 1, 0.5 1 0.55 0.15 1, 1 0.6 0.1 0.05 0\"", "energy", "7", "bounce", "0.4", "collide_depth", "true",
  "collision_radius", "0.02", "blend", "ADD", NULL };
static const char *const SPARKLE[] = { "amount", "60", "lifetime", "1.4", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "1.5", "speed_max", "5",
  "gravity", "vec3(0, -2, 0)", "damping", "1.5", "size", "0.11", "size_random", "0.5",
  "color_ramp", "\"0 1 0.8 1 1, 0.5 1 0.35 0.9 1, 1 0.6 0.2 1 0\"", "energy", "8", "blend", "ADD", NULL };
static const char *const MUZZLE[] = { "amount", "24", "lifetime", "0.14", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "14", "speed_min", "8", "speed_max", "18", "draw_mode", "STRETCHED", "stretch", "0.03",
  "size", "0.12", "color_ramp", "\"0 1 0.95 0.75 1, 1 1 0.5 0.1 0\"", "energy", "9", "blend", "ADD", NULL };
static const char *const SMOKE[] = { "amount", "8", "lifetime", "2.4", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 0, -1)", "spread", "30", "speed_min", "1", "speed_max", "4",
  "gravity", "vec3(0, 0.6, 0)", "damping", "1.8", "blend", "ALPHA", "soft_distance", "0.5", "size", "1.4", "size_curve", "\"0 0.4, 0.3 1, 1 1.6\"",
  "color_ramp", "\"0 0.9 0.88 0.85 0, 0.1 0.8 0.78 0.76 0.6, 1 0.7 0.7 0.7 0\"",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", "spin", "0.2", NULL };

/* the plume a collapse leaves: a few big soft puffs that rise a little, drift downwind and thin over 8 to 11 s */
static const char *const PLUME[] = { "amount", "5", "lifetime", "9.5", "lifetime_random", "0.15", "explosiveness", "0.6", "one_shot", "true",
  "emission_shape", "BOX", "emission_extents", "vec3(2.4, 0.6, 2.4)", "direction", "vec3(0, 1, 0)", "spread", "60", "speed_min", "0.3", "speed_max", "1.2",
  "gravity", "vec3(0.32, 0.12, 0.1)", "damping", "0.35", "blend", "ALPHA", "soft_distance", "1.5", "size", "6.5", "size_random", "0.3",
  "size_curve", "\"0 0.5, 0.25 1, 1 1.9\"", "color_ramp", "\"0 0.6 0.55 0.48 0, 0.12 0.58 0.54 0.48 0.36, 0.6 0.56 0.53 0.5 0.22, 1 0.55 0.53 0.5 0\"",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", "spin", "0.05", "turbulence", "0.35", "turbulence_scale", "0.2", "camera_fade", "6", NULL };

/* the explosion kit (Explosion3D's children), as hordeshooter tunes it */
static const char *const K_FIRE[] = { "amount", "12", "lifetime", "1.3", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.9", "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.4", "speed_max", "2.2",
  "gravity", "vec3(0, 1.6, 0)", "damping", "2.2", "size", "5.0", "size_random", "0.35", "size_curve", "\"0 0.5, 0.3 1, 1 1.2\"",
  "color_ramp", "\"0 1 0.9 0.75 0, 0.3 1 0.85 0.65 0.9, 0.75 1 0.75 0.6 0.75, 1 1 0.65 0.5 0\"", "energy", "1.0",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.25", "soft_distance", "0.8", NULL };
static const char *const K_SMOKE[] = { "amount", "20", "lifetime", "4.5", "lifetime_random", "0.2", "explosiveness", "0.75", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "1.0", "direction", "vec3(0, 1, 0)", "spread", "120", "speed_min", "1", "speed_max", "4",
  "gravity", "vec3(0, 0.9, 0)", "damping", "1.1", "turbulence", "1.1", "turbulence_scale", "0.3", "blend", "ALPHA",
  "soft_distance", "0.9", "size", "5.0", "size_random", "0.3", "size_curve", "\"0 0.45, 0.3 1, 1 1.55\"",
  "color_ramp", "\"0 1 0.9 0.8 0, 0.08 0.55 0.5 0.46 0.9, 0.45 0.36 0.34 0.32 0.7, 1 0.3 0.3 0.31 0\"",
  "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.18", NULL };
static const char *const K_SPARKS[] = { "amount", "160", "lifetime", "1.4", "lifetime_random", "0.4", "explosiveness", "0.95", "one_shot", "true",
  "emission_shape", "POINT", "spread", "180", "speed_min", "5", "speed_max", "16", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -12, 0)", "damping", "0.5", "draw_mode", "STRETCHED", "stretch", "0.045", "size", "0.05",
  "color_ramp", "\"0 1 0.95 0.7 1, 0.5 1 0.5 0.12 1, 1 0.6 0.1 0.05 0\"", "energy", "7", "bounce", "0.45",
  "collision_radius", "0.03", "collide_depth", "true", "blend", "ADD", NULL };
static const char *const K_EMBERS[] = { "amount", "60", "lifetime", "2.2", "lifetime_random", "0.5", "explosiveness", "0.8",
  "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "1.0", "spread", "180", "speed_min", "0.5", "speed_max", "3",
  "gravity", "vec3(0, 1.2, 0)", "damping", "1", "size", "0.07", "size_random", "0.5",
  "color_ramp", "\"0 1 0.7 0.3 1, 0.7 1 0.4 0.1 1, 1 0.6 0.1 0.02 0\"", "energy", "6", "blend", "ADD",
  "turbulence", "2", "turbulence_scale", "0.4", NULL };
static const char *const K_HAZE[] = { "amount", "24", "lifetime", "2.0", "explosiveness", "0.35", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "1.2", "direction", "vec3(0, 1, 0)", "spread", "50", "speed_min", "0.8", "speed_max", "2.2",
  "turbulence", "0.7", "blend", "DISTORT", "distort_strength", "0.12", "size", "4", "size_curve", "\"0 0.6, 1 1.6\"",
  "color_ramp", "\"0 1 1 1 0, 0.15 1 1 1 1, 1 1 1 1 0\"", NULL };

static cw_node boom_node(int seed) {
  cw_node e = cw_node_new("Explosion3D", "Blast");
  cw_node_add_child(FX.root, e);
  cw_node_set(e, "autostart", cw_val_bool(false));
  cw_node_set(e, "duration", cw_val_float(4.5f));
  cw_node_set(e, "flash_energy", cw_val_float(0)); /* the blast's light swells and fades here (light_swell): the kit's flickers */
  cw_node_set(e, "flash_color", cw_val_color(1, 0.65f, 0.3f, 1));
  cw_node_set(e, "flash_range", cw_val_float(18.0f));
  cw_node_set(e, "flash_duration", cw_val_float(0.55f));
  cw_node_set(e, "scorch_texture", cw_val_string("fx/scorch.png"));
  cw_node_set(e, "scorch_size", cw_val_float(6.0f));
  cw_node_set(e, "scorch_time", cw_val_float(6.0f));
  cw_node_set(e, "scorch_life", cw_val_float(60.0f));
  cw_node_set(e, "scorch_glow", cw_val_float(2.5f));
  cw_node_set(e, "shake_amount", cw_val_float(0)); /* never shake the camera: the pad rumbles */
  cw_node3d_set_position(e, (t3_vec3){ 0, -60, 0 });
  pnode(e, "Fire", K_FIRE, seed), pnode(e, "Smoke", K_SMOKE, seed + 1), pnode(e, "Sparks", K_SPARKS, seed + 2);
  pnode(e, "Embers", K_EMBERS, seed + 3), pnode(e, "Haze", K_HAZE, seed + 4);
  cw_node w = cw_node_new("Shockwave3D", "Wave");
  cw_node_add_child(e, w);
  cw_node_set(w, "ground", cw_val_bool(false));
  cw_node_set(w, "autostart", cw_val_bool(false));
  cw_node_set(w, "max_radius", cw_val_float(9.0f));
  cw_node_set(w, "duration", cw_val_float(0.7f));
  cw_node_set(w, "thickness", cw_val_float(1.4f));
  cw_node_set(w, "strength", cw_val_float(0.6f));
  return e;
}

void fx_init(cw_node root) {
  memset(&FX, 0, sizeof FX);
  FX.root = cw_node_new("Node3D", "FX");
  cw_node_add_child(root, FX.root);
  for (int i = 0; i < BOOMS; i++) FX.boom[i] = boom_node(100 + i * 10);
  FX.dust_warm = bnode("DustWarm", DUST_WARM, 300, 24);
  FX.dust_pale = bnode("DustPale", DUST_PALE, 330, 24);
  FX.dust_big = bnode("DustBig", DUST_BIG, 360, 10);
  FX.plume = bnode("Plume", PLUME, 380, 8);
  for (int i = 0; i < MARKS; i++) {
    cw_node d = FX.mark[i] = cw_node_new("Decal3D", "GroundMark");
    cw_node_add_child(FX.root, d);
    cw_node_set(d, "blend", cw_val_enum("MULTIPLY"));
    cw_node_set(d, "size", cw_val_vec3(1, 1, 0.8f));
    cw_node_set(d, "edge_fade", cw_val_float(0.3f));
    cw_node_set(d, "normal_fade", cw_val_float(0.5f));
    cw_node_set(d, "color", cw_val_color(1, 1, 1, 0));
    cw_node_set(d, "visible", cw_val_bool(false));
    cw_node3d_set_rotation(d, (t3_vec3){ -1.5707963f, 0, 0 });
    cw_node3d_set_position(d, (t3_vec3){ 0, -60, 0 });
  }
  static const float chip_col[M_COUNT][3] = { { 0.62f, 0.38f, 0.18f }, { 0.62f, 0.6f, 0.56f }, { 0.6f, 0.9f, 1 }, { 0.75f, 0.2f, 0.12f }, { 1, 0.3f, 0.85f } };
  for (int m = 0; m < M_COUNT; m++) {
    if (m == M_GLASS || m == M_TARGET) continue;
    FX.chips[m] = bnode("Chips", CHIPS, 400 + m * 10, 20);
    cw_node_set(FX.chips[m], "color", cw_val_color(chip_col[m][0], chip_col[m][1], chip_col[m][2], 1));
  }
  FX.glass = bnode("Shards", GLASS, 500, 16);
  FX.sparks = bnode("Sparks", SPARKS, 520, 20);
  FX.sparkle = bnode("Sparkle", SPARKLE, 540, 8);
  FX.muzzle = bnode("Muzzle", MUZZLE, 560, 4);
  FX.smoke = bnode("LaunchSmoke", SMOKE, 580, 4);
  for (int i = 0; i < LIGHTS; i++) {
    cw_node l = FX.light[i] = cw_node_new("OmniLight3D", "Flash");
    cw_node_add_child(FX.root, l);
    cw_node3d_set_position(l, (t3_vec3){ 0, -60, 0 });
    cw_node_set(l, "energy", cw_val_float(0));
    cw_node_set(l, "range", cw_val_float(10));
    cw_node_set(l, "decay", cw_val_float(2));
  }
  FX.ready = true;
}

static bool budget(int kind, int max) { return FX.per_tick[kind]++ < max; }

/* a light that swells to its peak over rise seconds and fades over fall seconds, both eased (no step, no flicker) */
static void light_swell(t3_vec3 at, float peak, float range, float rise, float fall, float r, float g, float b) {
  int i = FX.nl++ % LIGHTS;
  cw_node3d_set_position(FX.light[i], at);
  cw_node_set(FX.light[i], "color", cw_val_color(r, g, b, 1));
  cw_node_set(FX.light[i], "range", cw_val_float(range));
  FX.light_t[i] = 0, FX.light_peak[i] = peak, FX.light_rise[i] = rise, FX.light_fall[i] = fall;
}

/* a mark on the ground where the structure came down: merged into a near one (it grows and darkens), else the oldest
 * reused. Fades in over a second; tone: the material's dust (MULTIPLY: the grass under it goes the colour of it) */
static void ground_mark(t3_vec3 at, float size, int mat) {
  if (at.y > 1.2f) return;
  for (int i = 0; i < MARKS; i++) {
    if (FX.mark_size[i] <= 0) continue;
    float dx = FX.mark_at[i].x - at.x, dz = FX.mark_at[i].z - at.z;
    if (dx * dx + dz * dz < FX.mark_size[i] * FX.mark_size[i] * 0.16f) {
      if (FX.mark_size[i] < 5.5f) FX.mark_size[i] = fminf(5.5f, FX.mark_size[i] + size * 0.15f), FX.frame_dirty = true;
      FX.mark_a[i] = fminf(0.85f, FX.mark_a[i] + 0.04f);
      return;
    }
  }
  int i = FX.nmark++ % MARKS;
  FX.mark_at[i] = (t3_vec3){ at.x, 0, at.z };
  FX.mark_size[i] = size, FX.mark_t[i] = 0, FX.mark_a[i] = 0.55f;
  cw_node d = FX.mark[i];
  static const float tone[M_COUNT][3] = { { 0.86f, 0.74f, 0.58f }, { 0.9f, 0.86f, 0.78f }, { 0.92f, 0.94f, 0.95f }, { 0.7f, 0.66f, 0.62f }, { 0.9f, 0.86f, 0.78f } };
  int m = mat < M_COUNT ? mat : M_STONE;
  cw_node_set(d, "color", cw_val_color(tone[m][0], tone[m][1], tone[m][2], 0));
  cw_node_set(d, "visible", cw_val_bool(true));
  cw_node3d_set_position(d, (t3_vec3){ at.x, 0.2f, at.z });
  cw_node3d_set_rotation(d, (t3_vec3){ -1.5707963f, 0, hashf((uint32_t)FX.nmark * 977u) * 6.283f });
  FX.frame_dirty = true;
}

/* forget the marks (a new level) */
void fx_clear(void) {
  if (!FX.ready) return;
  for (int i = 0; i < MARKS; i++) {
    FX.mark_size[i] = 0;
    cw_node_set(FX.mark[i], "visible", cw_val_bool(false));
  }
  for (int i = 0; i < LIGHTS; i++) FX.light_peak[i] = 0, cw_node_set(FX.light[i], "energy", cw_val_float(0));
  FX.plumes = 0, FX.plume_wait = 0;
}

static void rumble(int player, float low, float high, int ms) {
  if (low + high <= FX.rum_low + FX.rum_high) return;
  FX.rum_low = low, FX.rum_high = high, FX.rum_ms = ms, FX.rum_player = player;
}

/* the tick's events; shooter: the seat whose pad feels them */
void fx_events(const game *g, int shooter) {
  if (!FX.ready) return;
  memset(FX.per_tick, 0, sizeof FX.per_tick);
  int big_breaks = 0;
  for (int k = 0; k < g->nev; k++) {
    const sim_event *e = &g->ev[k];
    t3_vec3 up = { 0, 1, 0 };
    switch (e->kind) {
    case EV_BREAK:
      big_breaks += e->size > 0.5f;
      if (e->size > 0.5f && e->p.y < 1.2f) ground_mark(e->p, 1.6f + e->size, e->mat);
      if (budget(0, 3)) cw_particles_burst(e->mat == M_WOOD ? FX.dust_warm : FX.dust_pale, e->p, up), FX.st.dust++;
      if (e->mat < M_COUNT && cw_node_valid(FX.chips[e->mat]) && budget(1, 4)) cw_particles_burst(FX.chips[e->mat], e->p, up), FX.st.chips++;
      if (e->mat == M_STEEL && budget(2, 3)) cw_particles_burst(FX.sparks, e->p, up), FX.st.sparks++;
      break;
    case EV_SHATTER:
      if (e->mat == M_TARGET) break;
      if (budget(3, 4)) cw_particles_burst(FX.glass, e->p, up), FX.st.shards++;
      break;
    case EV_HIT:
    case EV_LAND:
      if (e->mat == M_STEEL && e->speed > 4 && budget(2, 4)) cw_particles_burst(FX.sparks, e->p, e->n), FX.st.sparks++;
      else if (e->a > 3000 && budget(4, 2)) cw_particles_burst(e->mat == M_WOOD ? FX.dust_warm : FX.dust_pale, e->p, up), FX.st.dust++;
      if (e->kind == EV_LAND && e->a > 6000 && budget(5, 2)) ground_mark(e->p, 1.4f + fminf(1.5f, e->a / 40000.0f), e->mat);
      break;
    case EV_BOOM: {
      cw_node b = FX.boom[FX.nboom++ % BOOMS];
      cw_node3d_set_position(b, e->p);
      cw_node3d_global_transform(b);
      cw_explosion_fire(b);
      light_swell((t3_vec3){ e->p.x, e->p.y + 1.0f, e->p.z }, 70, 16, 0.4f, 1.3f, 1, 0.62f, 0.3f);
      FX.st.booms++;
      rumble(shooter, 1.0f, 0.45f, 420);
      break;
    }
    case EV_TARGET:
      cw_particles_burst(FX.sparkle, e->p, up);
      light_swell(e->p, 50, 10, 0.15f, 0.9f, 1, 0.35f, 0.9f);
      rumble(shooter, 0.3f, 0.7f, 200);
      break;
    case EV_LAUNCH:
      cw_particles_burst(FX.muzzle, e->p, e->n);
      cw_particles_burst(FX.smoke, e->p, e->n);
      light_swell(e->p, 22, 8, 0.1f, 0.4f, 1, 0.75f, 0.4f);
      rumble(shooter, 0.55f, 0.25f, 160);
      break;
    default: break;
    }
  }
  if (big_breaks >= 6) {
    /* a storey coming down: one big cloud at the middle of what broke */
    t3_vec3 c = { 0, 0, 0 };
    int n = 0;
    for (int k = 0; k < g->nev; k++)
      if (g->ev[k].kind == EV_BREAK) c.x += g->ev[k].p.x, c.y += g->ev[k].p.y, c.z += g->ev[k].p.z, n++;
    c.x /= n, c.y /= n, c.z /= n;
    cw_particles_burst(FX.dust_big, c, (t3_vec3){ 0, 1, 0 });
    rumble(shooter, 0.6f, 0.2f, 300);
    /* and the plume that hangs over the pile: one every half second while storeys keep falling, eight at most */
    if (FX.plume_wait <= 0 && FX.plumes < 8) {
      cw_particles_burst(FX.plume, (t3_vec3){ c.x, fmaxf(1.0f, c.y * 0.6f), c.z }, (t3_vec3){ 0, 1, 0 });
      FX.plume_wait = 30, FX.plumes++;
    }
  }
  if (FX.plume_wait > 0) FX.plume_wait--;
}

/* every frame: the flash lights die away; the strongest rumble of the frame goes to the pad */
void fx_update(float dt) {
  if (!FX.ready) return;
  for (int i = 0; i < LIGHTS; i++) {
    if (FX.light_peak[i] <= 0) continue;
    float t = FX.light_t[i] += dt, e;
    if (t < FX.light_rise[i]) {
      float u = t / FX.light_rise[i];
      e = u * u * (3 - 2 * u);
    } else {
      float u = (t - FX.light_rise[i]) / FX.light_fall[i];
      if (u >= 1) u = 1, FX.light_peak[i] = 0;
      e = (1 - u) * (1 - u) * (1 + 2 * u); /* 1 - smoothstep */
    }
    cw_node_set(FX.light[i], "energy", cw_val_float(FX.light_peak[i] > 0 ? FX.light_peak[i] * e : 0));
  }
  /* the ground marks fade in over a second */
  for (int i = 0; i < MARKS; i++) {
    if (FX.mark_size[i] <= 0) continue;
    if (FX.mark_t[i] < 1 || FX.frame_dirty) {
      FX.mark_t[i] = fminf(1, FX.mark_t[i] + dt);
      float k = FX.mark_t[i] * FX.mark_t[i] * (3 - 2 * FX.mark_t[i]);
      cw_value c;
      cw_node_get_prop(FX.mark[i], "color", &c);
      cw_node_set(FX.mark[i], "color", cw_val_color(c.v[0], c.v[1], c.v[2], FX.mark_a[i] * k));
      cw_node_set(FX.mark[i], "size", cw_val_vec3(FX.mark_size[i], FX.mark_size[i], 0.8f));
    }
  }
  FX.frame_dirty = false;
  if (FX.rum_low + FX.rum_high > 0) {
    cw_input_rumble(FX.rum_player, FX.rum_low, FX.rum_high, FX.rum_ms);
    FX.st.rumbles++;
  }
  FX.rum_low = FX.rum_high = 0;
}

fx_stats fx_get_stats(void) { return FX.st; }

/* Every effect fired once where nothing shows it (under the ground below the structure, in the camera's view so it
 * is drawn and hidden by depth): the GPU programs of the explosion kit and the bursts are compiled at load, not in
 * the frame the first charge goes off (a dropped frame at the moment that matters most). */
void fx_prime(t3_vec3 under) {
  if (!FX.ready) return;
  t3_vec3 up = { 0, 1, 0 };
  cw_node b = FX.boom[FX.nboom++ % BOOMS];
  cw_node3d_set_position(b, under);
  cw_node3d_global_transform(b);
  cw_explosion_fire(b);
  cw_particles_burst(FX.dust_warm, under, up), cw_particles_burst(FX.dust_pale, under, up), cw_particles_burst(FX.dust_big, under, up);
  cw_particles_burst(FX.plume, under, up);
  for (int m = 0; m < M_COUNT; m++) if (cw_node_valid(FX.chips[m])) cw_particles_burst(FX.chips[m], under, up);
  cw_particles_burst(FX.glass, under, up), cw_particles_burst(FX.sparks, under, up), cw_particles_burst(FX.sparkle, under, up);
  cw_particles_burst(FX.muzzle, under, up), cw_particles_burst(FX.smoke, under, up);
}
