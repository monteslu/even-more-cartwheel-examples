/* towers effects: render only; they read the simulation's events, the towers and the shells in flight, and never feed
 * it. Built on the engine's effect kit: Explosion3D (fire and smoke flipbooks blended along baked motion vectors,
 * sparks, debris with smoke sub-emitters through PARENT emission, a ground Shockwave3D, a pooled flash light and a
 * scorch mark that cools), burst slots (one system fires many one-shots at their own places), DISTORT heat haze,
 * clustered omni lights.
 *
 *   bolt     jagged lightning from the coil's orb to the first creep and on, hop by hop: one fork a strike that writhes
 *            smoothly from one jagged shape into another as it fades (drawn by view.c from the segments here), a
 *            blue-white glow swelling at the orb and sparks at each hit
 *   frost    a ring of cold racing out over the ground, ice motes thrown up, a freeze shatters into shards
 *   beam     sparks spraying off the creep it burns, heat haze and a light riding the hit (view.c draws the beam)
 *   mortar   a muzzle flash and a puff of smoke, a burning fuse trail on the shell, and an explosion where it lands
 *            (fire, sparks that leave smoke, a shockwave ring, debris, a cooling scorch)
 * No camera shake: the pads rumble instead. The effects preset thins particle counts and secondary lights; every
 * tower keeps its effect.
 * No strobing (tests/towers-flash.mjs): every light swells in over a few frames and fades out smoothly, nothing blinks
 * or flickers, no flash is red, and a light taken back from the pool is the dimmest one. The explosions' own flash
 * light (the engine's, which starts at its peak) is off; the blasts swell one of these instead. TOWERS_STROBE_CONTROL
 * (native, tests only) brings back the old flicker for the flash check's control. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/explosions.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "game.h"
#include "mapbuild.h"

#define BIGS 3
#define SMALLB 6
#define WAVES 12
#define LIGHTS 12
#define MAXBOLT 160
#define BOLT_SEGS 14
#define MAXCORPSE 400 /* (12 a frame for 30 frames: never one taken back mid-fall) */

/* a strike: its two jagged shapes (offsets of each inner point across and up), morphed from one to the other over its life */
typedef struct bolt { float p[BOLT_SEGS + 1][3], j[2][BOLT_SEGS + 1][2]; int n, life, life0, in; float a[3], b[3], w; } bolt;
#define BOLT_LIFE 16 /* frames: a strike swells in over 2 and fades over the rest */
#define FLASH_IN 4   /* frames a flash light takes to swell to its peak */
/* the storm (a player's power): its strikes and its light swell in slowly and die away slowly, a slow surge of light
 * rather than a crack, so a storm called on a busy map (four players, a storm each) keeps well under three flashes a
 * second in any part of the screen (tests/towers-flash.mjs: the storm scene) */
#define STORM_BOLT_LIFE 46 /* frames */
#define STORM_BOLT_IN 14
#define STORM_LIGHT_IN 16
#define STORM_LIGHT_LEN 80

static struct {
  bool ready;
  cw_node root;
  cw_node big[BIGS], small[SMALLB], wave[WAVES];
  int nb, ns, nw;
  cw_node coin, hit, spark, frost, shard, dust, sparkle, wisp, spiral, coins, gore, heal, trail, smoke, haze, flashp, lava_haze, napalm;
  cw_node light[LIGHTS];
  cw_node motes;
  int motes_theme;
  float light_e[LIGHTS], light_peak[LIGHTS]; /* each flash light's energy now and its peak */
  int light_age[LIGHTS], light_len[LIGHTS], light_in[LIGHTS]; /* frames since it began; frames it lasts; frames it swells */
  int nl;
  bool strobe; /* TOWERS_STROBE_CONTROL: the old flicker, for the flash check's control */
  bool old_storm; /* TOWERS_OLD_STORM: the storm's old quick strikes and light (at the three-a-second bound), the storm check's control */
  bolt bolts[MAXBOLT];
  int nbolt_next;
  float seg[MAXBOLT * BOLT_SEGS * 7];
  corpse dead[MAXCORPSE];
  int ndead_next;
  int nseg;
  uint32_t rng;
  float rum_k[MAXP];
  uint32_t rum_end[MAXP];
  float want_low[MAXP], want_high[MAXP];
  int want_ms[MAXP], rumbles;
  uint32_t tick, leak_next;
  int frame_hits, frame_kills, frame_booms, booms_total, bolts_total;
} FX;

int g_fx_level = 2;
bool g_strobe_control;

static float frand(void) {
  FX.rng = FX.rng * 1664525u + 1013904223u;
  return (float)(FX.rng >> 8) / 16777216.0f;
}

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
      cw_warn("towers: bad particle setting %s = %s", kv[i], kv[i + 1]);
      continue;
    }
    cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "seed", cw_val_int(seed));
  cw_node3d_set_position(n, cw_node_is(parent, "Explosion3D") ? (t3_vec3){ 0, 0.5f, 0 } : (t3_vec3){ 0, -60, 0 });
  return n;
}
static cw_node bnode(const char *name, const char *const *kv, int seed, int slots) {
  cw_node n = pnode(FX.root, name, kv, seed);
  cw_value v;
  int per = cw_node_get_prop(n, "amount", &v) ? (int)v.i : 1;
  cw_node_set(n, "bursts", cw_val_int(slots));
  cw_node_set(n, "amount", cw_val_int(per * slots));
  cw_node3d_set_position(n, (t3_vec3){ 0, 0, 0 });
  return n;
}
static void burst(cw_node n, float x, float y, float z, float dx, float dy, float dz) { cw_particles_burst(n, (t3_vec3){ x, y, z }, (t3_vec3){ dx, dy, dz }); }

/* ── the parts ──────────────────────────────────────────────────────── */

static const char *const HIT[] = { "amount", "10", "lifetime", "0.32", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.05", "spread", "80", "speed_min", "2", "speed_max", "6.5",
  "gravity", "vec3(0, -8, 0)", "draw_mode", "STRETCHED", "stretch", "0.04", "size", "0.05",
  "color_ramp", "\"0 1 0.92 0.6 0.6, 0.15 1 0.92 0.6 1, 1 1 0.4 0.1 0\"", "energy", "4", "blend", "ADD", NULL };
/* electric sparks: blue-white, quick, at each creep a bolt strikes */
static const char *const SPARK[] = { "amount", "9", "lifetime", "0.25", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.1", "spread", "180", "speed_min", "3", "speed_max", "8",
  "gravity", "vec3(0, -6, 0)", "draw_mode", "STRETCHED", "stretch", "0.05", "size", "0.045",
  "color_ramp", "\"0 0.9 0.95 1 1, 0.5 0.5 0.75 1 1, 1 0.3 0.5 1 0\"", "energy", "10", "blend", "ADD", NULL };
static const char *const FROST[] = { "amount", "40", "lifetime", "0.9", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "RING", "emission_radius", "1.0", "emission_inner_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "70",
  "speed_min", "2", "speed_max", "6", "gravity", "vec3(0, -2.5, 0)", "damping", "1.6", "size", "0.12", "size_random", "0.6",
  "color_ramp", "\"0 0.9 1 1 1, 0.5 0.55 0.85 1 0.9, 1 0.4 0.7 1 0\"", "energy", "5", "blend", "ADD",
  "turbulence", "1.2", "turbulence_scale", "0.5", NULL };
static const char *const SHARD[] = { "amount", "10", "lifetime", "0.6", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.2", "direction", "vec3(0, 1, 0)", "spread", "80", "speed_min", "2", "speed_max", "6",
  "gravity", "vec3(0, -14, 0)", "draw_mode", "MESH", "mesh_primitive", "BOX", "blend", "OPAQUE", "size", "0.12", "size_random", "0.5",
  "color", "color(0.75, 0.92, 1.0, 1)", "spin", "8", "bounce", "0.3", "collide_depth", "true", NULL };
static const char *const DUST[] = { "amount", "10", "lifetime", "1.1", "lifetime_random", "0.3", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.6", "direction", "vec3(0, 1, 0)", "spread", "80", "speed_min", "1", "speed_max", "3.2",
  "gravity", "vec3(0, 0.4, 0)", "damping", "2.2", "blend", "ALPHA", "soft_distance", "0.4", "size", "1.4", "size_curve", "\"0 0.4, 0.4 1, 1 1.5\"",
  "color_ramp", "\"0 0.8 0.74 0.62 0, 0.12 0.66 0.6 0.5 0.55, 1 0.5 0.47 0.42 0\"",
  "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.3", NULL };
static const char *const SPARKLE[] = { "amount", "22", "lifetime", "0.9", "lifetime_random", "0.4", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.45", "direction", "vec3(0, 1, 0)", "spread", "50", "speed_min", "1.5", "speed_max", "4.5",
  "gravity", "vec3(0, -4, 0)", "damping", "1.2", "size", "0.1", "size_random", "0.6",
  "color_ramp", "\"0 1 0.95 0.6 1, 0.6 1 0.8 0.3 1, 1 1 0.6 0.2 0\"", "energy", "8", "blend", "ADD", NULL };
static const char *const WISP[] = { "amount", "5", "lifetime", "0.7", "lifetime_random", "0.3", "explosiveness", "0.8", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "25", "speed_min", "2", "speed_max", "4",
  "gravity", "vec3(0, 1.5, 0)", "damping", "1.5", "size", "0.45", "size_curve", "\"0 0.4, 0.3 1, 1 0\"",
  "color_ramp", "\"0 0.7 0.85 1 1, 0.5 0.45 0.6 1 0.8, 1 0.3 0.3 1 0\"", "energy", "5", "blend", "ADD",
  "turbulence", "1.2", "turbulence_scale", "0.6", NULL };
static const char *const SPIRAL[] = { "amount", "40", "lifetime", "0.9", "explosiveness", "0.6", "one_shot", "true",
  "emission_shape", "RING", "emission_radius", "1.0", "direction", "vec3(0, 1, 0)", "spread", "8", "speed_min", "2.5", "speed_max", "4.5",
  "emission_inner_radius", "0.8", "damping", "0.8", "trail_length", "8", "trail_width", "0.05", "size", "0.08", "size_random", "0.5",
  "color_ramp", "\"0 1 1 1 1, 0.5 1 1 1 1, 1 1 1 1 0\"", "energy", "9", "blend", "ADD", NULL };
static const char *const GORE[] = { "amount", "5", "lifetime", "0.55", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.2", "spread", "70", "direction", "vec3(0, 1, 0)", "speed_min", "1.5", "speed_max", "3.5",
  "gravity", "vec3(0, -12, 0)", "size", "0.35", "size_curve", "\"0 0.5, 0.4 1, 1 1.2\"",
  "color_ramp", "\"0 0.5 0.45 0.4 0.8, 1 0.32 0.3 0.28 0\"", "blend", "ALPHA", "soft_distance", "0.3",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", NULL };
static const char *const HEAL[] = { "amount", "8", "lifetime", "0.9", "explosiveness", "0.7", "one_shot", "true",
  "emission_shape", "RING", "emission_radius", "2.0", "emission_inner_radius", "1.6", "direction", "vec3(0, 1, 0)", "spread", "10",
  "speed_min", "0.8", "speed_max", "1.8", "size", "0.1", "color_ramp", "\"0 0.5 1 0.4 1, 1 0.3 1 0.3 0\"", "energy", "5", "blend", "ADD", NULL };
/* the mortar shell's fuse trail */
static const char *const TRAIL[] = { "amount", "2", "lifetime", "0.4", "lifetime_random", "0.3", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.08", "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.1", "speed_max", "0.5",
  "gravity", "vec3(0, 1.2, 0)", "damping", "2.5", "size", "0.55", "size_random", "0.35", "size_curve", "\"0 1, 0.5 0.8, 1 0.2\"",
  "color_ramp", "\"0 1 0.95 0.8 1, 0.25 1 0.6 0.2 0.9, 1 0.5 0.1 0.04 0\"", "energy", "3.2", "blend", "ADD",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "1.2", NULL };
static const char *const SMOKE[] = { "amount", "5", "lifetime", "1.4", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.25", "direction", "vec3(0, 1, 0)", "spread", "40", "speed_min", "1", "speed_max", "2.6",
  "gravity", "vec3(0, 0.8, 0)", "damping", "1.6", "blend", "ALPHA", "soft_distance", "0.5", "size", "1.1", "size_curve", "\"0 0.4, 0.3 1, 1 1.5\"",
  "color_ramp", "\"0 0.9 0.85 0.75 0, 0.1 0.6 0.57 0.54 0.6, 1 0.4 0.4 0.42 0\"",
  "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.2", NULL };
static const char *const HAZE[] = { "amount", "1", "lifetime", "0.45", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "30", "speed_min", "0.3", "speed_max", "0.8", "direction", "vec3(0, 1, 0)",
  "blend", "DISTORT", "distort_strength", "0.08", "size", "1.2", "size_curve", "\"0 0.5, 1 1.4\"",
  "color_ramp", "\"0 1 1 1 0, 0.2 1 1 1 1, 1 1 1 1 0\"", "texture", "\"fx/haze.png\"", NULL };
/* the air shimmering over the lava pools: slow, wide, rising */
static const char *const LAVA_HAZE[] = { "amount", "1", "lifetime", "1.6", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.4", "spread", "15", "speed_min", "0.5", "speed_max", "0.9", "direction", "vec3(0, 1, 0)",
  "blend", "DISTORT", "distort_strength", "0.05", "size", "2.4", "size_curve", "\"0 0.6, 1 1.6\"",
  "color_ramp", "\"0 1 1 1 0, 0.3 1 1 1 1, 1 1 1 1 0\"", "texture", "\"fx/haze.png\"", NULL };
/* the glow at a coil's orb or a mortar's mouth as it fires: it swells in and fades (never a one-frame pop) */
static const char *const FLASHP[] = { "amount", "1", "lifetime", "0.26", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "0", "speed_min", "0", "speed_max", "0", "size", "1.1", "size_curve", "\"0 0.6, 0.25 1, 1 0.5\"",
  "color_ramp", "\"0 1 1 1 0, 0.2 1 1 1 1, 1 1 1 1 0\"", "energy", "7", "blend", "ADD", NULL };
static const char *const FLASHP_STROBE[] = { "amount", "1", "lifetime", "0.09", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "0", "speed_min", "0", "speed_max", "0", "size", "1.1", "size_curve", "\"0 1, 1 0.4\"",
  "color_ramp", "\"0 1 1 1 1, 1 1 1 1 0\"", "energy", "12", "blend", "ADD", NULL };
/* napalm: flames licking up off the burning path */
static const char *const NAPALM[] = { "amount", "2", "lifetime", "0.8", "lifetime_random", "0.3", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "20", "speed_min", "0.6", "speed_max", "1.4",
  "gravity", "vec3(0, 1.0, 0)", "damping", "1.5", "size", "0.9", "size_random", "0.35", "size_curve", "\"0 0.3, 0.3 1, 1 0.4\"",
  "color_ramp", "\"0 1 0.9 0.6 0, 0.15 1 0.7 0.3 0.9, 0.6 1 0.4 0.1 0.6, 1 0.4 0.1 0.04 0\"", "energy", "2.4", "blend", "ADD",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.8", NULL };
/* a coin popping up off a kill: one gold glint rising and fading */
static const char *const COIN[] = { "amount", "1", "lifetime", "0.7", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "direction", "vec3(0, 1, 0)", "spread", "8", "speed_min", "2.4", "speed_max", "2.8", "gravity", "vec3(0, -2.5, 0)",
  "size", "0.26", "size_curve", "\"0 0.5, 0.2 1, 1 0.8\"", "color_ramp", "\"0 1 0.85 0.3 0, 0.15 1 0.85 0.3 1, 0.7 1 0.8 0.3 0.9, 1 1 0.8 0.3 0\"",
  "energy", "3", "blend", "ADD", NULL };
static const char *const MOTES[] = { "amount", "130", "lifetime", "8", "lifetime_random", "0.4", "one_shot", "false",
  "emission_shape", "BOX", "emission_extents", "vec3(38, 1.2, 22)", "direction", "vec3(0, 1, 0)", "spread", "90", "speed_min", "0.1", "speed_max", "0.5",
  "gravity", "vec3(0.25, 0.05, 0.1)", "turbulence", "0.6", "turbulence_scale", "0.25", "size", "0.13", "size_random", "0.6",
  "color_ramp", "\"0 1 1 1 0, 0.2 1 1 1 1, 0.8 1 1 1 1, 1 1 1 1 0\"", "energy", "3", "blend", "ADD", NULL };

/* the explosion kit (crawler's): fire, smoke, sparks that leave smoke, debris that trails smoke, embers, haze */
static const char *const K_FIRE[] = { "amount", "10", "lifetime", "1.0", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.6", "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.4", "speed_max", "2",
  "gravity", "vec3(0, 1.6, 0)", "damping", "2.2", "size", "3.2", "size_random", "0.35", "size_curve", "\"0 0.5, 0.3 1, 1 1.2\"",
  "color_ramp", "\"0 1 1 1 0, 0.06 1 1 1 1, 0.75 1 0.85 0.8 0.85, 1 1 0.7 0.6 0\"", "energy", "1.8",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.25", "soft_distance", "0.8", NULL };
static const char *const K_SMOKE[] = { "amount", "12", "lifetime", "2.6", "lifetime_random", "0.2", "explosiveness", "0.75", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.7", "direction", "vec3(0, 1, 0)", "spread", "120", "speed_min", "1", "speed_max", "3.2",
  "gravity", "vec3(0, 0.9, 0)", "damping", "1.1", "turbulence", "1.1", "turbulence_scale", "0.3", "blend", "ALPHA", "sort", "true",
  "soft_distance", "0.9", "size", "3.2", "size_random", "0.3", "size_curve", "\"0 0.45, 0.3 1, 1 1.55\"",
  "color_ramp", "\"0 1 0.9 0.8 0, 0.08 0.6 0.57 0.53 0.8, 0.45 0.42 0.41 0.4 0.55, 1 0.35 0.35 0.36 0\"",
  "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.18", NULL };
static const char *const K_SPARKS[] = { "amount", "100", "lifetime", "1.0", "lifetime_random", "0.4", "explosiveness", "0.95", "one_shot", "true",
  "emission_shape", "POINT", "spread", "180", "speed_min", "4", "speed_max", "12", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -12, 0)", "damping", "0.5", "draw_mode", "STRETCHED", "stretch", "0.045", "size", "0.05",
  "color_ramp", "\"0 1 0.95 0.7 1, 0.5 1 0.5 0.12 1, 1 0.6 0.1 0.05 0\"", "energy", "8", "bounce", "0.45",
  "collision_radius", "0.03", "collide_depth", "true", "blend", "ADD", NULL };
static const char *const K_SPARKSMOKE[] = { "amount", "100", "lifetime", "0.8", "emission_shape", "PARENT", "parent", "\"../Sparks\"",
  "one_shot", "true", "inherit_velocity", "0.02", "spread", "180", "speed_min", "0", "speed_max", "0.3", "gravity", "vec3(0, 0.5, 0)",
  "blend", "ALPHA", "soft_distance", "0.3", "size", "0.35", "size_curve", "\"0 0.3, 1 1.3\"",
  "color_ramp", "\"0 0.7 0.65 0.6 0, 0.2 0.5 0.48 0.46 0.35, 1 0.35 0.35 0.35 0\"",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", NULL };
static const char *const K_DEBRIS[] = { "amount", "12", "lifetime", "1.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "65", "speed_min", "4", "speed_max", "9",
  "gravity", "vec3(0, -12, 0)", "draw_mode", "MESH", "mesh_primitive", "BOX", "blend", "OPAQUE", "size", "0.15", "size_random", "0.6",
  "color", "color(0.3, 0.26, 0.22, 1)", "spin", "6", "bounce", "0.3", "collide_depth", "true", NULL };
static const char *const K_TRAIL[] = { "amount", "120", "lifetime", "0.9", "emission_shape", "PARENT", "parent", "\"../Debris\"",
  "one_shot", "true", "inherit_velocity", "0.05", "spread", "180", "speed_min", "0.1", "speed_max", "0.5", "gravity", "vec3(0, 0.6, 0)",
  "blend", "ALPHA", "soft_distance", "0.4", "size", "0.55", "size_curve", "\"0 0.4, 1 1.4\"",
  "color_ramp", "\"0 0.85 0.8 0.75 0, 0.15 0.6 0.57 0.55 0.5, 1 0.4 0.4 0.4 0\"",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", "spin", "0.4", NULL };
static const char *const K_EMBERS[] = { "amount", "40", "lifetime", "1.6", "lifetime_random", "0.5", "explosiveness", "0.8",
  "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "0.8", "spread", "180", "speed_min", "0.5", "speed_max", "3",
  "gravity", "vec3(0, 1.2, 0)", "damping", "1", "size", "0.06", "size_random", "0.5",
  "color_ramp", "\"0 1 0.7 0.3 1, 0.7 1 0.4 0.1 1, 1 0.6 0.1 0.02 0\"", "energy", "7", "blend", "ADD",
  "turbulence", "2", "turbulence_scale", "0.4", NULL };
static const char *const K_HAZE[] = { "amount", "14", "lifetime", "1.4", "explosiveness", "0.35", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.9", "direction", "vec3(0, 1, 0)", "spread", "50", "speed_min", "0.8", "speed_max", "2.0",
  "turbulence", "0.7", "blend", "DISTORT", "distort_strength", "0.12", "size", "3", "size_curve", "\"0 0.6, 1 1.6\"",
  "color_ramp", "\"0 1 1 1 0, 0.15 1 1 1 1, 1 1 1 1 0\"", NULL };
static const char *const S_FIRE[] = { "amount", "5", "lifetime", "0.55", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.25", "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.3", "speed_max", "1.6",
  "gravity", "vec3(0, 1.4, 0)", "damping", "2", "size", "1.6", "size_random", "0.3", "size_curve", "\"0 0.5, 0.3 1, 1 1.1\"",
  "color_ramp", "\"0 1 1 1 0, 0.08 1 1 1 1, 1 1 0.75 0.6 0\"", "energy", "2.4",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.4", "soft_distance", "0.5", NULL };
static const char *const S_SPARKS[] = { "amount", "30", "lifetime", "0.7", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "180", "speed_min", "3", "speed_max", "8", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -12, 0)", "draw_mode", "STRETCHED", "stretch", "0.04", "size", "0.04", "bounce", "0.4", "collide_depth", "true",
  "color_ramp", "\"0 1 0.9 0.6 1, 1 1 0.4 0.1 0\"", "energy", "7", "blend", "ADD", NULL };
static const char *const S_SMOKE[] = { "amount", "4", "lifetime", "1.4", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "90", "speed_min", "0.4", "speed_max", "1.4",
  "gravity", "vec3(0, 0.8, 0)", "damping", "1.2", "blend", "ALPHA", "soft_distance", "0.5", "size", "1.4", "size_curve", "\"0 0.4, 0.3 1, 1 1.4\"",
  "color_ramp", "\"0 0.85 0.75 0.65 0, 0.12 0.55 0.52 0.5 0.6, 1 0.38 0.38 0.4 0\"",
  "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.2", NULL };

static cw_node boom_node(const char *name, bool big, int seed) {
  cw_node e = cw_node_new("Explosion3D", name);
  cw_node_add_child(FX.root, e);
  cw_node_set(e, "autostart", cw_val_bool(false));
  cw_node_set(e, "duration", cw_val_float(big ? 3.0f : 2.0f));
  cw_node_set(e, "flash_energy", cw_val_float(FX.strobe ? (big ? 140.0f : 55.0f) : 0)); /* (blast() swells a pooled light instead) */
  cw_node_set(e, "flash_range", cw_val_float(big ? 12.0f : 7.0f));
  cw_node_set(e, "flash_duration", cw_val_float(big ? 0.4f : 0.25f));
  cw_node_set(e, "scorch_texture", cw_val_string("fx/scorch.png"));
  cw_node_set(e, "scorch_size", cw_val_float(big ? 3.2f : 1.9f)); /* a scar under the blast, no bigger than it */
  cw_node_set(e, "scorch_time", cw_val_float(big ? 5.0f : 2.5f));
  cw_node_set(e, "scorch_life", cw_val_float(big ? 20.0f : 10.0f));
  cw_node_set(e, "scorch_glow", cw_val_float(big ? 3.0f : 2.0f));
  cw_node_set(e, "shake_amount", cw_val_float(0)); /* never: the pads rumble instead */
  cw_node3d_set_position(e, (t3_vec3){ 0, -60, 0 });
  if (big) {
    pnode(e, "Fire", K_FIRE, seed), pnode(e, "Smoke", K_SMOKE, seed + 2);
    pnode(e, "Sparks", K_SPARKS, seed + 3), pnode(e, "SparkSmoke", K_SPARKSMOKE, seed + 4);
    pnode(e, "Debris", K_DEBRIS, seed + 5), pnode(e, "DebrisTrail", K_TRAIL, seed + 6);
    pnode(e, "Embers", K_EMBERS, seed + 7), pnode(e, "Haze", K_HAZE, seed + 8);
  } else {
    pnode(e, "Fire", S_FIRE, seed), pnode(e, "Sparks", S_SPARKS, seed + 1), pnode(e, "SparkSmoke", K_SPARKSMOKE, seed + 2);
    pnode(e, "Smoke", S_SMOKE, seed + 3), pnode(e, "Debris", K_DEBRIS, seed + 4);
  }
  cw_node w = cw_node_new("Shockwave3D", "Wave");
  cw_node_add_child(e, w);
  cw_node_set(w, "ground", cw_val_bool(true));
  cw_node_set(w, "autostart", cw_val_bool(false));
  cw_node_set(w, "max_radius", cw_val_float(big ? 6.0f : 3.0f));
  cw_node_set(w, "duration", cw_val_float(big ? 0.6f : 0.4f));
  cw_node_set(w, "thickness", cw_val_float(big ? 1.1f : 0.6f));
  cw_node_set(w, "strength", cw_val_float(big ? 0.6f : 0.35f));
  cw_node3d_set_position(w, (t3_vec3){ 0, 0.3f, 0 });
  return e;
}
static cw_node wave_node(void) {
  cw_node w = cw_node_new("Shockwave3D", "Ring");
  cw_node_add_child(FX.root, w);
  cw_node_set(w, "ground", cw_val_bool(true));
  cw_node_set(w, "autostart", cw_val_bool(false));
  cw_node_set(w, "max_radius", cw_val_float(3.0f));
  cw_node_set(w, "duration", cw_val_float(0.5f));
  cw_node_set(w, "thickness", cw_val_float(0.8f));
  cw_node_set(w, "strength", cw_val_float(0.3f));
  cw_node3d_set_position(w, (t3_vec3){ 0, -60, 0 });
  return w;
}
static cw_node omni(const char *name, float range) {
  cw_node l = cw_node_new("OmniLight3D", name);
  cw_node_add_child(FX.root, l);
  cw_node3d_set_position(l, (t3_vec3){ 0, -60, 0 });
  cw_node_set(l, "energy", cw_val_float(0));
  cw_node_set(l, "range", cw_val_float(range));
  cw_node_set(l, "decay", cw_val_float(2));
  cw_node_set(l, "fog_energy", cw_val_float(0));
  return l;
}

void fx_free(void) { memset(&FX, 0, sizeof FX); }

void fx_init(cw_node root) {
  fx_free();
  FX.rng = 12345;
  FX.strobe = g_strobe_control = getenv("TOWERS_STROBE_CONTROL") && atoi(getenv("TOWERS_STROBE_CONTROL"));
  FX.old_storm = FX.strobe || getenv("TOWERS_OLD_STORM");
  FX.root = cw_node_new("Node3D", "FX");
  cw_node_add_child(root, FX.root);
  cw_node ls = cw_node_new("LightingSettings", "Lighting");
  cw_node_add_child(FX.root, ls);
  cw_node_set(ls, "mode", cw_val_enum("CLUSTERED"));
  cw_node_set(ls, "cluster_far", cw_val_float(400));
  for (int i = 0; i < BIGS; i++) FX.big[i] = boom_node("BigBlast", true, 100 + i * 10);
  for (int i = 0; i < SMALLB; i++) FX.small[i] = boom_node("SmallBlast", false, 200 + i * 10);
  for (int i = 0; i < WAVES; i++) FX.wave[i] = wave_node();
  FX.hit = bnode("Hit", HIT, 360, 48);
  FX.spark = bnode("Spark", SPARK, 365, 96);
  FX.frost = bnode("Frost", FROST, 440, 24);
  FX.shard = bnode("Shard", SHARD, 445, 48);
  FX.dust = bnode("Dust", DUST, 380, 16);
  FX.sparkle = bnode("Sparkle", SPARKLE, 390, 24);
  FX.wisp = bnode("Wisp", WISP, 400, 96);
  FX.spiral = bnode("Spiral", SPIRAL, 410, 12);
  FX.coins = bnode("Coins", SPARKLE, 420, 12);
  FX.gore = bnode("Gore", GORE, 430, 96);
  FX.heal = bnode("Heal", HEAL, 450, 24);
  FX.trail = bnode("ShellTrail", TRAIL, 300, 160);
  FX.smoke = bnode("MuzzleSmoke", SMOKE, 310, 24);
  FX.haze = bnode("BeamHaze", HAZE, 320, 32);
  FX.lava_haze = bnode("LavaHaze", LAVA_HAZE, 330, 24);
  FX.flashp = bnode("Flash", FX.strobe ? FLASHP_STROBE : FLASHP, 330, 48);
  FX.napalm = bnode("Napalm", NAPALM, 480, 160);
  FX.coin = bnode("Coin", COIN, 490, 64);
  for (int i = 0; i < MAXCORPSE; i++) FX.dead[i].age = CORPSE_LIFE;
  FX.motes = pnode(FX.root, "Motes", MOTES, 470);
  cw_node_set(FX.motes, "emitting", cw_val_bool(true));
  cw_node3d_set_position(FX.motes, (t3_vec3){ 0, 1.5f, 0 });
  FX.motes_theme = -1;
  for (int i = 0; i < LIGHTS; i++) FX.light[i] = omni("Flash", 10);
  FX.ready = true;
}

/* ── rumble ─────────────────────────────────────────────────────────── */

static void rumble(int p, float low, float high, int ms) {
  if (p < 0 || p >= MAXP || low + high <= FX.want_low[p] + FX.want_high[p]) return;
  FX.want_low[p] = low, FX.want_high[p] = high, FX.want_ms[p] = ms;
}
static void rumble_all(const sim *s, float low, float high, int ms) {
  for (int i = 0; i < MAXP; i++)
    if (s->p[i].active) rumble(i, low, high, ms);
}
static void rumble_send(void) {
  for (int i = 0; i < MAXP; i++) {
    float k = FX.want_low[i] + FX.want_high[i];
    if (k > 0 && (FX.tick >= FX.rum_end[i] || k >= FX.rum_k[i])) {
      cw_input_rumble(i, FX.want_low[i], FX.want_high[i], FX.want_ms[i]);
      FX.rum_k[i] = k, FX.rum_end[i] = FX.tick + (uint32_t)(FX.want_ms[i] * 60 / 1000), FX.rumbles++;
    }
    FX.want_low[i] = FX.want_high[i] = 0, FX.want_ms[i] = 0;
  }
}

fx_stats fx_get_stats(void) { return (fx_stats){ FX.booms_total, FX.rumbles, FX.bolts_total }; }

/* a light that swells to e over `in` frames (FLASH_IN for most) and fades out over `len` (smooth both ways: never a step). It takes the
 * dimmest light of the pool, and gives way when even that one is still brighter than a quarter of e (so a light is never
 * yanked away while bright) */
static void flash_in(float x, float y, float z, float e, float r, float g, float b, int len, int in) {
  if (g_fx_level <= 0 && e < 40) return;
  int i = FX.nl++ % LIGHTS;
  if (!FX.strobe) {
    for (int k = 0; k < LIGHTS; k++)
      if (FX.light_e[k] < FX.light_e[i]) i = k;
    if (FX.light_e[i] > e * 0.25f) return;
  }
  cw_node3d_set_position(FX.light[i], (t3_vec3){ x, y, z });
  cw_node_set(FX.light[i], "color", cw_val_color(r, g, b, 1));
  FX.light_peak[i] = e, FX.light_age[i] = 0, FX.light_len[i] = len, FX.light_in[i] = in;
  FX.light_e[i] = FX.strobe ? e : CW_MAX(FX.light_e[i], 0.01f);
}
static void flash_len(float x, float y, float z, float e, float r, float g, float b, int len) { flash_in(x, y, z, e, r, g, b, len, FLASH_IN); }
static void flash(float x, float y, float z, float e, float r, float g, float b) { flash_len(x, y, z, e, r, g, b, 14 + (int)(e * 0.08f)); }
static void blast(float x, float z, float r, bool big) {
  FX.booms_total++;
  if (FX.frame_booms++ > (big ? 3 : 6)) return;
  if (!FX.strobe) flash_len(x, 1.6f, z, big ? 80.0f : 35.0f, 1.0f, 0.62f, 0.28f, big ? 34 : 24);
  cw_node e = big ? FX.big[FX.nb++ % BIGS] : FX.small[FX.ns++ % SMALLB];
  cw_node3d_set_position(e, (t3_vec3){ x, 0, z });
  cw_node_set(cw_node_get(e, "Wave"), "max_radius", cw_val_float(CW_MAX(r * 1.5f, 2.5f)));
  cw_node3d_global_transform(e);
  cw_explosion_fire(e);
}
static void ring(float x, float z, float r, float dur) {
  cw_node w = FX.wave[FX.nw++ % WAVES];
  cw_node3d_set_position(w, (t3_vec3){ x, 0.25f, z });
  cw_node_set(w, "max_radius", cw_val_float(r));
  cw_node_set(w, "duration", cw_val_float(dur));
  cw_node3d_global_transform(w);
  cw_shockwave_fire(w);
}

/* a bolt of lightning from a to b: two jagged shapes made now, with its life in frames */
static void bolt_jag(bolt *bl, int s) {
  float dx = bl->b[0] - bl->a[0], dy = bl->b[1] - bl->a[1], dz = bl->b[2] - bl->a[2], l = sqrtf(dx * dx + dy * dy + dz * dz);
  for (int i = 0; i <= BOLT_SEGS; i++) {
    bool end = i == 0 || i == bl->n;
    bl->j[s][i][0] = end ? 0 : (frand() - 0.5f) * CW_MIN(l * 0.22f, 1.1f), bl->j[s][i][1] = end ? 0 : (frand() - 0.5f) * 0.5f;
  }
}
static void bolt_new(const float a[3], const float b[3], float w, int life) {
  bolt *bl = &FX.bolts[FX.nbolt_next++ % MAXBOLT];
  memcpy(bl->a, a, sizeof bl->a), memcpy(bl->b, b, sizeof bl->b);
  float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
  bl->w = w, bl->life = bl->life0 = life, bl->in = 2;
  bl->n = CW_CLAMP((int)(sqrtf(dx * dx + dy * dy + dz * dz) / 0.55f), 3, BOLT_SEGS);
  bolt_jag(bl, 0), bolt_jag(bl, 1);
  FX.bolts_total++;
}
/* the fork's points at k (0 its first shape .. 1 its second): smoothly between the two, so it writhes and never jumps */
static void bolt_shape(bolt *bl, float k) {
  float dx = bl->b[0] - bl->a[0], dy = bl->b[1] - bl->a[1], dz = bl->b[2] - bl->a[2], l = sqrtf(dx * dx + dy * dy + dz * dz);
  float px = -dz / (l + 1e-4f), pz = dx / (l + 1e-4f), s = k * k * (3 - 2 * k);
  int n = bl->n;
  for (int i = 0; i <= n; i++) {
    float t = (float)i / (float)n, j = bl->j[0][i][0] + (bl->j[1][i][0] - bl->j[0][i][0]) * s, jy = bl->j[0][i][1] + (bl->j[1][i][1] - bl->j[0][i][1]) * s;
    bl->p[i][0] = bl->a[0] + dx * t + px * j, bl->p[i][1] = bl->a[1] + dy * t + jy + 0.6f * t * (1 - t) * l * 0.15f, bl->p[i][2] = bl->a[2] + dz * t + pz * j;
  }
}

static const float KCOL[CK_KINDS][3] = { { 0.1f, 1.0f, 0.85f }, { 1.0f, 0.9f, 0.5f }, { 1.0f, 0.45f, 0.05f }, { 1.0f, 0.3f, 0.08f },
                                         { 0.35f, 1.0f, 0.45f }, { 0.55f, 1.0f, 0.15f }, { 0.55f, 0.95f, 1.0f }, { 1.0f, 0.12f, 0.1f } };

void fx_events(sim *s) {
  if (!FX.ready) { s->ev_n = 0; return; }
  FX.frame_hits = FX.frame_kills = FX.frame_booms = 0;
  int lvl = g_fx_level;
  static const float TOP[3] = { 1.0f, 1.25f, 1.5f };
  for (int i = 0; i < s->ev_n; i++) {
    const event *e = &s->ev[i];
    float x = e->x, z = e->y;
    switch (e->kind) {
    case EV_ZAP: {
      float a[3], b[3] = { e->z, 0.8f, e->w };
      if (e->b == 0) { /* from the coil's orb */
        int slot = s->slot_at[CW_CLAMP(mp_ty(&s->m, z), 0, MP_MAX_H - 1)][CW_CLAMP(mp_tx(&s->m, x), 0, MP_MAX_W - 1)] - 1;
        t3_vec3 m = slot >= 0 && s->tw[slot].alive ? view_muzzle(s, slot) : (t3_vec3){ x, MP_GROUND_H + TOP[CW_CLAMP(e->a, 1, 3) - 1] + 0.8f, z };
        a[0] = m.x, a[1] = m.y, a[2] = m.z;
        flash(m.x, m.y, m.z, 30 + 10 * (float)e->a, 0.55f, 0.75f, 1.0f);
        burst(FX.flashp, m.x, m.y, m.z, 0, 0, 0);
      } else if (e->b == 200) /* the storm: down from the sky */
        a[0] = e->z + (frand() - 0.5f) * 3, a[1] = 13, a[2] = e->w - 2 + (frand() - 0.5f) * 3;
      else
        a[0] = x, a[1] = 0.8f, a[2] = z;
      float w = e->b == 200 ? 1.1f : (1.0f - 0.08f * (float)e->b) * (0.8f + 0.2f * (float)e->a);
      bool storm = e->b == 200 && !FX.old_storm;
      bolt_new(a, b, w, FX.strobe ? 9 : storm ? STORM_BOLT_LIFE : BOLT_LIFE);
      if (storm) FX.bolts[(FX.nbolt_next - 1) % MAXBOLT].in = STORM_BOLT_IN;
      if (FX.frame_hits++ < (lvl >= 2 ? 40 : 20)) burst(FX.spark, e->z, 0.8f, e->w, 0, 1, 0);
      break;
    }
    case EV_FROST: {
      float r = e->z;
      ring(x, z, r, 0.55f);
      burst(FX.frost, x, MP_GROUND_H + 0.6f, z, 0, 1, 0);
      if (lvl >= 1) burst(FX.frost, x, 0.3f, z, 0, 1, 0);
      flash(x, MP_GROUND_H + 2.0f, z, e->b ? 70 : 30, 0.5f, 0.8f, 1.0f);
      if (e->b) ring(x, z, r * 0.6f, 0.35f);
      break;
    }
    case EV_FREEZE: if (FX.frame_hits++ < 30) burst(FX.shard, x, 0.6f, z, 0, 1, 0); break;
    case EV_MORTAR: {
      int slot = e->b;
      t3_vec3 m = slot < MAXT && s->tw[slot].alive ? view_muzzle(s, slot) : (t3_vec3){ x, MP_GROUND_H + 2, z };
      flash(m.x, m.y, m.z, 50, 1.0f, 0.6f, 0.25f);
      burst(FX.flashp, m.x, m.y, m.z, 0, 0, 0);
      burst(FX.smoke, m.x, m.y, m.z, 0, 1, 0);
      break;
    }
    case EV_BOOM:
      blast(x, z, e->z, e->b);
      if (e->b) for (int p = 0; p < MAXP; p++) if (s->p[p].active) rumble(p, 0.25f, 0.1f, 90);
      break;
    case EV_KILL:
      if (FX.frame_kills++ > (lvl >= 2 ? 12 : lvl >= 1 ? 8 : 5)) break;
      if (e->a != CK_SHADE) { /* the body falls (a shade just fades into its wisp) */
        corpse *c = &FX.dead[FX.ndead_next++ % MAXCORPSE];
        *c = (corpse){ x, z, e->z, 1, e->a, e->w > 0.5f, 0 };
      }
      if (lvl >= 1 && (FX.frame_kills & 1)) burst(FX.coin, x, 1.0f, z, 0, 1, 0); /* a coin up off it: the bounty into the pot */
      if (e->a == CK_SHADE) burst(FX.wisp, x, 0.8f, z, 0, 1, 0);
      else burst(FX.gore, x, 0.5f, z, 0, 1, 0);
      if (e->w > 0.5f) { blast(x, z, 3.0f, true); flash(x, 2, z, 150, 1, 0.5f, 0.3f); rumble_all(s, 0.8f, 0.4f, 300); }
      break;
    case EV_SPLIT: burst(FX.gore, x, 0.6f, z, 0, 1, 0), burst(FX.heal, x, 0.3f, z, 0, 1, 0); break;
    case EV_LEAK: {
      /* a leak: the core's ring, an amber light and sparks, at most one a second however many pour in (a
       * stream of leaks would otherwise pulse the core several times a second); the pads rumble for each */
      if (FX.strobe || FX.tick >= FX.leak_next) {
        FX.leak_next = FX.tick + 60;
        ring(x, z, 5.0f, 0.5f);
        flash(s->core_x, 3, s->core_y, 120, 1.0f, FX.strobe ? 0.3f : 0.62f, FX.strobe ? 0.25f : 0.35f); /* amber: never a red flash */
        burst(FX.sparkle, s->core_x, 1.5f, s->core_y, 0, 1, 0);
      }
      float k = CW_MIN(0.3f + 0.15f * e->z, 1.0f);
      rumble_all(s, k, 0.6f * k, 160 + 40 * (int)e->z);
      break;
    }
    case EV_BUILD: burst(FX.dust, x, MP_GROUND_H + 0.2f, z, 0, 1, 0), burst(FX.spiral, x, MP_GROUND_H + 0.1f, z, 0, 1, 0), flash(x, 2.5f, z, 50, TCOL[e->a][0], TCOL[e->a][1], TCOL[e->a][2]); if (e->b < MAXP) rumble(e->b, 0.3f, 0.2f, 90); break;
    case EV_UPGRADE: burst(FX.spiral, x, MP_GROUND_H + 0.1f, z, 0, 1, 0), burst(FX.sparkle, x, MP_GROUND_H + 1.5f, z, 0, 1, 0), flash(x, 3, z, 80, TCOL[e->a][0], TCOL[e->a][1], TCOL[e->a][2]), ring(x, z, 3.0f, 0.4f); break;
    case EV_SELL: burst(FX.coins, x, MP_GROUND_H + 1.0f, z, 0, 1, 0), burst(FX.dust, x, MP_GROUND_H + 0.2f, z, 0, 1, 0); break;
    case EV_WAVE:
      for (int g = 0; g < s->m.nspawn; g++) {
        float gx, gz;
        mp_gate_pos(&s->m, g, &gx, &gz);
        ring(gx, gz, 6.0f, 0.7f), flash(gx, 2.5f, gz, 90, 1.0f, 0.3f, 0.85f), burst(FX.wisp, gx, 1.2f, gz, 0, 1, 0);
      }
      rumble_all(s, 0.2f, 0.2f, 120);
      break;
    case EV_WON: for (int k = 0; k < 4; k++) burst(FX.spiral, s->core_x, 0.2f + (float)k, s->core_y, 0, 1, 0); flash(s->core_x, 4, s->core_y, 200, 0.7f, 0.9f, 1.0f); break;
    case EV_LOST: blast(s->core_x, s->core_y, 5.0f, true); rumble_all(s, 1.0f, 0.8f, 600); break;
    case EV_HEAL: if (lvl >= 1 && FX.frame_hits++ < 24) burst(FX.heal, x, 0.4f, z, 0, 1, 0); break;
    case EV_SPAWN: if (lvl >= 2 && (FX.tick & 3) == 0) burst(FX.wisp, x, 0.8f, z, 0, 1, 0); break;
    case EV_HOP: break;
    case EV_POWER: /* the storm: a ring of charge over the ground, a cold-white light swelling over it, the pad rumbles */
      ring(x, z, e->z, 0.6f), burst(FX.wisp, x, 1.0f, z, 0, 1, 0);
      if (FX.old_storm) flash_len(x, 4, z, 70, 0.7f, 0.8f, 1.0f, 40);
      else flash_in(x, 4, z, 70, 0.7f, 0.8f, 1.0f, STORM_LIGHT_LEN, STORM_LIGHT_IN);
      if (e->a < MAXP) rumble(e->a, 0.5f, 0.3f, 220);
      break;
    case EV_STUN: if (FX.frame_hits++ < 30) burst(FX.spark, x, 0.9f, z, 0, 1, 0); break;
    case EV_FIRE: burst(FX.dust, x, 0.2f, z, 0, 1, 0); break;
    default: break;
    }
  }
  s->ev_n = 0;
}

int fx_corpses(const corpse **out) {
  *out = FX.dead;
  return MAXCORPSE;
}

int fx_bolt_segments(const float **seg) {
  *seg = FX.seg;
  return FX.nseg;
}

/* every frame: the bolts flickering out, the beams' sparks, the shells' trails, the flashes dying away, the pads */
void fx_tick(const sim *s, float time) {
  if (!FX.ready) return;
  FX.tick++;
  int lvl = g_fx_level;
  { /* the air over the map, in the theme's colour: pollen, falling leaves' glints, sand, snow, embers, glow-spores */
    static const float MC[MP_THEMES][3] = { { 1.0f, 0.95f, 0.6f }, { 1.0f, 0.6f, 0.25f }, { 1.0f, 0.85f, 0.6f }, { 0.85f, 0.95f, 1.0f }, { 1.0f, 0.45f, 0.1f }, { 0.4f, 1.0f, 0.9f } };
    int th = CW_CLAMP(s->m.theme, 0, MP_THEMES - 1);
    if (th != FX.motes_theme) FX.motes_theme = th, cw_node_set(FX.motes, "color", cw_val_color(MC[th][0], MC[th][1], MC[th][2], 1));
    static int last_amount = -1;
    int amount = lvl >= 2 ? 130 : lvl >= 1 ? 100 : 60;
    if (amount != last_amount) last_amount = amount, cw_node_set(FX.motes, "amount", cw_val_int(amount));
  }
  /* the bolts: each fork writhes from its first shape into its second, swelling in over 2 frames and fading out
   * smoothly (the control: a new shape every other frame, at full width at once) */
  FX.nseg = 0;
  for (int i = 0; i < MAXBOLT; i++) {
    bolt *bl = &FX.bolts[i];
    if (bl->life <= 0) continue;
    int age = bl->life0 - bl->life;
    float k = (float)bl->life / (float)bl->life0, w;
    if (FX.strobe) {
      if (age == 0 || (FX.tick & 1)) bolt_jag(bl, 0);
      bolt_shape(bl, 0);
      w = bl->w * (0.35f + 0.65f * k) * (bl->life > bl->life0 - 2 ? 1.4f : 1.0f);
    } else {
      bolt_shape(bl, (float)age / (float)(bl->life0 - 1));
      float in = CW_MIN((float)(age + 1) / (float)bl->in, 1.0f), out = k * k * (3 - 2 * k);
      in = in * in * (3 - 2 * in);
      w = bl->w * 1.3f * in * out;
    }
    for (int j = 0; j < bl->n && FX.nseg < MAXBOLT * BOLT_SEGS; j++) {
      float *q = FX.seg + FX.nseg * 7;
      q[0] = bl->p[j][0], q[1] = bl->p[j][1], q[2] = bl->p[j][2], q[3] = bl->p[j + 1][0], q[4] = bl->p[j + 1][1], q[5] = bl->p[j + 1][2], q[6] = w;
      FX.nseg++;
    }
    bl->life--;
  }
  /* the beams: sparks off what they burn, haze over it */
  for (int k = 0; k < s->m.nslot; k++) {
    const tower *t = &s->tw[k];
    if (!t->alive || t->kind != TK_BEAM || t->tgt < 0 || !s->e[t->tgt].alive) continue;
    const creep *e = &s->e[t->tgt];
    if ((FX.tick + (uint32_t)k) % (lvl >= 2 ? 7u : 10u) == 0) burst(FX.hit, e->x, 0.7f, e->y, t->x - e->x, 0.6f, t->y - e->y); /* (a spray, not a sparkle: a burst every 20 Hz at one spot read as a flicker) */
    if (lvl >= 1 && (FX.tick + (uint32_t)k) % 7 == 0) burst(FX.haze, e->x, 0.8f, e->y, 0, 1, 0);
  }
  /* heat shimmering over the lava: now and then over a point of a pool off its shallows */
  if (s->m.theme == MP_THEME_VOLCANO && lvl >= 1 && FX.tick % (lvl >= 2 ? 4u : 8u) == 0 && s->m.nfeat > 0) {
    uint32_t h = (FX.tick * 2654435761u) ^ 0x9e3779b9u;
    const mp_feat *f = &s->m.feat[(h >> 8) % (uint32_t)s->m.nfeat];
    float a = (float)(h & 255) * 0.0245f, r = (float)((h >> 16) & 255) / 255.0f * 0.45f * f->r * MP_TILE;
    float x = mp_wx(&s->m, f->x - 0.5f) + cw_cos(a) * r, z = mp_wz(&s->m, f->y - 0.5f) + cw_sin(a) * r;
    if (f->kind == FEAT_WATER && mapbuild_pool_deep(&s->m, x, z) > 0.15f) burst(FX.lava_haze, x, MB_POOL_LEVEL + 0.4f, z, 0, 1, 0);
  }
  /* napalm: flames up off each burning patch, a few a frame, fewer as it dies down */
  for (int k = 0; k < MAXFIRE; k++) {
    const fire *f = &s->fire[k];
    if (!f->alive || f->t > f->T - 20 || (FX.tick + (uint32_t)k) % (lvl >= 2 ? 3u : 6u)) continue;
    float a = frand() * 6.2831853f, r = sqrtf(frand()) * f->r * 0.85f;
    burst(FX.napalm, f->x + cw_cos(a) * r, 0.25f, f->y + cw_sin(a) * r, 0, 1, 0);
  }
  /* the shells' fuses */
  for (int k = 0; k < MAXSH; k++) {
    const shell *sh = &s->sh[k];
    if (!sh->alive || sh->T < 30 || ((FX.tick + (uint32_t)k) & 1)) continue;
    float t = (float)sh->t / (float)sh->T, x = sh->x0 + (sh->tx - sh->x0) * t, z = sh->y0 + (sh->ty - sh->y0) * t;
    float dist = sqrtf((sh->tx - sh->x0) * (sh->tx - sh->x0) + (sh->ty - sh->y0) * (sh->ty - sh->y0));
    float y = (MP_GROUND_H + 1.8f) * (1 - t) + 4.0f * (3.0f + dist * 0.45f) * t * (1 - t) + 0.4f * t;
    burst(FX.trail, x, y, z, 0, 0.3f, 0);
  }
  (void)time;
  for (int i = 0; i < LIGHTS; i++) {
    if (FX.light_e[i] <= 0.0f) continue;
    if (FX.strobe) { /* the old flash: at its peak at once, then down by a fifth a frame */
      FX.light_e[i] *= 0.8f;
      if (FX.light_e[i] <= 0.5f) FX.light_e[i] = 0;
    } else { /* in over FLASH_IN frames, out over the rest (smoothstep both ways) */
      int a = ++FX.light_age[i], fin = CW_MAX(FX.light_in[i], 1), len = CW_MAX(FX.light_len[i], fin + 1);
      float u = a <= fin ? (float)a / (float)fin : 1.0f - (float)(a - fin) / (float)(len - fin);
      u = CW_CLAMP(u, 0.0f, 1.0f);
      float target = FX.light_peak[i] * u * u * (3 - 2 * u);
      /* (a light taken while still dim never drops first: it rises from where it was) */
      FX.light_e[i] = a <= fin ? CW_MAX(target, FX.light_e[i]) : target;
      if (a >= len) FX.light_e[i] = 0;
    }
    cw_node_set(FX.light[i], "energy", cw_val_float(FX.light_e[i]));
  }
  for (int i = 0; i < MAXCORPSE; i++)
    if (FX.dead[i].age < CORPSE_LIFE) FX.dead[i].age++;
  rumble_send();
}
