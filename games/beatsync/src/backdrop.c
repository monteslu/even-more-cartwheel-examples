/* The backdrops (backdrop.h): each a Backdrop node of main.cwscene drawing its own world (backdrops/<name>.cwscene)
 * behind the field. The scene files hold what stands still (camera, environment, fog, floors, lights, the particle
 * systems); this file builds what is made by code (skies, grids, beams, the crowd, the hills) into each world and moves
 * it with the music.
 *
 * Fading: a Backdrop's opacity is its weight. Two at once (a style fading into the next): the one going out stays at
 * opacity 1 underneath and the one coming in is drawn over it at its weight, so the picture is a straight cross-fade
 * with no dip in the middle; the one going out drops to 0 (free: not drawn at all) when the fade ends. One alone fades
 * over the field's own black by its weight.
 *
 * Nothing here switches on or off while it shows, and nothing blinks: every motion is a slow sweep, a drift or a sway
 * on the beat's clock, and the light breathes with the music's loudness, which is smoothed over seconds. */
#include "backdrop.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/backdrop.h"
#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/noise.h"
#include "cartwheel/particles.h"
#include "cartwheel/rim_light.h"
#include "cartwheel/scene.h"

#include "band.h"
#include "bd_kit.h"
#include "worlds.h"


static const char *const NAMES[BD_COUNT] = { "none", "fire", "sea", "grid", "lasers", "stage", "clean", "maze", "aurora", "space", "city", "forest", "deep", "dunes", "lava", "metal" };
static const char *const NODES[BD_COUNT] = { "", "BdFire", "BdSea", "BdGrid", "BdLasers", "BdStage", "BdClean", "BdMaze", "BdAurora", "BdSpace", "BdCity", "BdForest", "BdDeep", "BdDunes", "BdLava", "BdMetal" };
const char *backdrop_name(int bd) { return bd >= 0 && bd < BD_COUNT ? NAMES[bd] : "?"; }

typedef struct crowd { t3_instanced_mesh *body, *arm; t3_material *mat; } crowd;

static struct {
  bool built;
  cw_node node[BD_COUNT], root[BD_COUNT];
  float op_set[BD_COUNT];
  int order_top, drawn;
  /* fire */
  t3_mesh *fire_sky, *fire_glow;
  cw_node fire_light[5];
  /* sea */
  cw_node water, moonlight;
  t3_mesh *moon, *moon_halo, *sea_sky;
  t3_instanced_mesh *harbour, *stars;
  /* grid */
  t3_instanced_mesh *grid_long, *grid_cross, *grid_stars, *peaks;
  t3_mesh *sun, *grid_sky, *grid_floor, *horizon, *haze_band;
  /* lasers */
  t3_instanced_mesh *rays, *ray_glow, *heads;
  crowd crowd_l, crowd_s;
  t3_mesh *booth_wall;
  cw_node head[3];
  /* stage */
  t3_instanced_mesh *wall_px;
  cw_node spot[3];
  /* clean */
  t3_mesh *hills, *range;
  t3_geometry *hills_g;
  float *hill_h;
  /* maze */
  t3_instanced_mesh *walls, *critters, *crit_glow, *trails;
  bool maze_old; /* the `maze old 1` request: the old bright critters (the readability gate's note-likeness control) */
  t3_object *maze_root;
} B;

static t3_object *obj(int bd) { return cw_node3d_object(B.root[bd]); }

static void tx_sun(float u, float v, float c[3]) { /* yellow to magenta, cut by bands that widen toward its foot */
  float x = (u - 0.5f) * 2, y = (v - 0.5f) * 2, d = sqrtf(x * x + y * y);
  float disc = 1 - smooth(0.97f, 1.0f, d);
  float t = v;
  float r = 1.0f, g = 0.85f - 0.75f * t, b = 0.2f + 0.45f * t;
  float cut = 1;
  if (t > 0.5f) {
    float k = (t - 0.5f) / 0.5f, band = fmodf(t * 18.0f, 1.0f);
    cut = band > k * 0.75f ? 1 : 0;
    cut = 1 - (1 - cut) * smooth(0.0f, 0.03f, fminf(band, k * 0.75f - band + 0.03f) + 0.03f);
  }
  float glow = 0.18f * expf(-fmaxf(0, d - 1) * 6) * (d > 1);
  c[0] = r * disc * cut + glow, c[1] = g * disc * cut + glow * 0.3f, c[2] = b * disc * cut + glow * 0.6f;
}
static void tx_sky_grid(float u, float v, float c[3]) { /* purple rising from the horizon (the bottom) into black */
  (void)u;
  float k = powf(v, 3.0f);
  c[0] = 0.55f * k, c[1] = 0.08f * k, c[2] = 0.6f * k;
}
/* the fire's sky: banks of smoke lit from below, brightest low down near the burning horizon, black overhead */
static cw_noise smoke_n, smoke_w;
static void tx_fire_sky(float u, float v, float c[3]) {
  float x = u * 900, y = v * 300;
  float wx = x, wy = y;
  cw_noise_warp_2d(&smoke_w, &wx, &wy);
  float n = 0.5f + 0.5f * cw_noise_2d(&smoke_n, wx, wy * 1.8f);           /* billows */
  float low = 0.03f + 0.97f * powf(v, 3.2f);                              /* lit from below: the horizon is v 1, dark overhead */
  float lit = low * (0.2f + 1.2f * n * n);
  float rim = smooth(0.55f, 0.85f, n) * low * 0.5f;                        /* the billows' lit undersides */
  c[0] = 0.9f * lit + rim, c[1] = 0.26f * lit + 0.35f * rim, c[2] = 0.06f * lit + 0.08f * rim;
}
/* the burnt ground: black crust split by glowing cracks (the cells' borders), the cracks the emissive map */
static cw_noise crack_n, crack_d;
static float crack_at(float u, float v) {
  float x = u * 256, y = v * 256;
  float d = cw_noise_2d(&crack_n, x, y); /* distance2 - distance: 0 on a border */
  float w = 0.5f + 0.5f * cw_noise_2d(&crack_d, x * 2, y * 2);
  return expf(-fmaxf(0, d + 1) * (22 + 30 * w)) * smooth(0.35f, 0.75f, w);
}
static float tx_crust(float u, float v) { return 0.35f + 0.25f * (0.5f + 0.5f * cw_noise_2d(&crack_d, u * 700, v * 700)) - 0.5f * crack_at(u, v); }
static float tx_glow_up(float u, float v) { (void)u; return powf(v, 3.0f); }

/* ── fire ────────────────────────────────────────────────────────────── */

static void build_fire(void) {
  t3_object *at = obj(BD_FIRE);
  smoke_n = cw_noise_default(), smoke_n.seed = 9, smoke_n.frequency = 0.012f, smoke_n.fractal = CW_NOISE_FRACTAL_FBM, smoke_n.octaves = 5;
  smoke_w = cw_noise_default(), smoke_w.seed = 10, smoke_w.frequency = 0.006f, smoke_w.warp_amp = 60;
  /* the sky: a vast card far behind the horizon fire, the smoke lit from below (it wraps: a slow drift sideways) */
  t3_texture *sky = texture_rgb(512, 256, tx_fire_sky);
  sky->wrap_s = sky->wrap_t = T3_REPEAT; /* (both: the generated programs take a map that repeats both ways or neither) */
  B.fire_sky = quad(at, 2600, 700, sky, (t3_vec3){ 0, 200, -700 });
  B.fire_sky->materials[0]->fog = false;
  /* the horizon's glow over the far fire */
  B.fire_glow = quad(at, 2600, 260, texture(4, 128, tx_glow_up), (t3_vec3){ 0, 40, -640 });
  B.fire_glow->materials[0]->fog = false;
  /* the ground: a vast plane of burnt crust split by glowing cracks, tiled */
  crack_n = cw_noise_default(), crack_n.seed = 11, crack_n.type = CW_NOISE_CELLULAR, crack_n.frequency = 0.03f;
  crack_n.cellular_return = CW_NOISE_CELL_DISTANCE2_SUB, crack_n.cellular_distance = CW_NOISE_DIST_EUCLIDEAN;
  crack_d = cw_noise_default(), crack_d.seed = 12, crack_d.frequency = 0.02f, crack_d.fractal = CW_NOISE_FRACTAL_FBM;
  {
    t3_texture *glow = texture(256, 256, crack_at), *crust = texture(256, 256, tx_crust);
    glow->wrap_s = glow->wrap_t = T3_REPEAT, crust->wrap_s = crust->wrap_t = T3_REPEAT;
    glow->repeat = crust->repeat = (t3_vec2){ 60, 50 };
    t3_material *m = t3_mesh_standard_material_new(0xffffff);
    m->color = (t3_color){ 0.05f, 0.035f, 0.03f };
    m->roughness = 0.8f;
    m->emissive = (t3_color){ 2.0f, 0.47f, 0.08f };
    t3_material_set_map(m, crust), t3_release(crust);
    t3_material_set_texture(m, T3_EMISSIVE_MAP, glow), t3_release(glow);
    t3_mesh *g = add_mesh(at, t3_plane_geometry_new(4000, 3000, 1, 1), m, (t3_vec3){ 0, 0, -1400 });
    T3_OBJ(g)->quaternion = about(1, 0, 0, -PI / 2);
  }
  static const char *const L[5] = { "LightL0", "LightR0", "LightL1", "LightR1", "LightFar" };
  for (int i = 0; i < 5; i++) B.fire_light[i] = cw_node_get(B.root[BD_FIRE], L[i]);
}

static void update_fire(float beat, float t, float energy) {
  (void)beat;
  float e = 0.85f + 0.3f * energy;
  tint(B.fire_sky, 1, 1, 1, 1.1f * e);
  B.fire_sky->materials[0]->map->offset.x = fmodf(t * 0.004f, 1.0f); /* the smoke drifts on the wind */
  tint(B.fire_glow, 1.0f, 0.35f, 0.08f, 0.7f * e);
  /* the fires' light breathes slowly, each its own phase (never a flicker: periods of seconds) */
  static const float BASE[5] = { 550, 550, 1600, 1600, 9000 };
  for (int i = 0; i < 5; i++)
    if (B.fire_light[i].id) setf(B.fire_light[i], "energy", BASE[i] * e * (0.85f + 0.15f * sinf(t * 0.9f + (float)i * 1.7f)));
}

/* ── sea ─────────────────────────────────────────────────────────────── */

#define HARBOUR 40
#define STARS 160
static const t3_vec3 MOON = { -70, 34, -300 };

static void build_sea(void) {
  t3_object *at = obj(BD_SEA);
  B.sea_sky = quad(at, 1200, 300, texture_rgb(4, 128, tx_sky_grid), (t3_vec3){ 0, 90, -420 });
  B.moon_halo = quad(at, 90, 90, texture(128, 128, tx_halo), (t3_vec3){ MOON.x, MOON.y, MOON.z - 1 });
  B.moon = quad(at, 18, 18, texture(128, 128, tx_disc), MOON);
  B.stars = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(16, 16, tx_dot), STARS);
  B.harbour = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(16, 16, tx_dot), HARBOUR);
  cw_node r = B.root[BD_SEA];
  B.water = cw_node_get(r, "Water"); /* in backdrops/sea.cwscene: a Water3D added later is drawn without its passes */
  B.moonlight = cw_node_get(r, "Moonlight");
  cw_node3d_look_at(B.moonlight, (t3_vec3){ -MOON.x, -MOON.y, -MOON.z }, (t3_vec3){ 0, 1, 0 }); /* from the moon toward the camera */
  setf(B.moonlight, "energy", 1.6);
  for (int i = 0; i < STARS; i++) {
    float x = -500 + 1000 * rnd(11, (uint32_t)i), y = 20 + 200 * powf(rnd(12, (uint32_t)i), 0.7f), z = -400;
    float s = 0.8f + 1.4f * rnd(13, (uint32_t)i), b = (0.15f + 0.5f * rnd(14, (uint32_t)i));
    place(B.stars, i, (t3_vec3){ x, y, z }, about(0, 0, 1, 0), (t3_vec3){ s, s, 1 }, (t3_color){ b * 0.8f, b * 0.9f, b });
  }
  done(B.stars);
  static const float COLS[4][3] = { { 1, 0.55f, 0.2f }, { 1, 0.3f, 0.6f }, { 0.3f, 0.8f, 1 }, { 1, 0.85f, 0.5f } };
  for (int i = 0; i < HARBOUR; i++) {
    float x = -380 + 760 * ((float)i + rnd(21, (uint32_t)i)) / HARBOUR, z = -560;
    if (fabsf(x - MOON.x * 1.9f) < 50) x += 100;
    float y = -5.8f + 3.0f * rnd(22, (uint32_t)i), s = 2.2f + 2.0f * rnd(23, (uint32_t)i);
    const float *c = COLS[hsh(24, (uint32_t)i) % 4];
    float b = (0.9f + 0.6f * rnd(25, (uint32_t)i));
    place(B.harbour, i, (t3_vec3){ x, y, z }, about(0, 0, 1, 0), (t3_vec3){ s, s, 1 }, (t3_color){ c[0] * b, c[1] * b, c[2] * b });
  }
  done(B.harbour);
  tint(B.sea_sky, 0.25f, 0.5f, 1.0f, 0.5f);
  tint(B.moon, 1.0f, 0.97f, 0.9f, 3.2f);
  tint(B.moon_halo, 0.6f, 0.7f, 1.0f, 0.35f);
}

/* ── grid ────────────────────────────────────────────────────────────── */

#define GRID_LONG 61
#define GRID_CROSS 64
#define GRID_Y (-7.0f)
#define GRID_STEP 6.0f
#define GRID_STARS 220
#define PEAKS 34

static void build_grid(void) {
  t3_object *at = obj(BD_GRID);
  B.grid_sky = quad(at, 1600, 400, texture_rgb(4, 256, tx_sky_grid), (t3_vec3){ 0, 180 + GRID_Y, -520 });
  B.grid_stars = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(16, 16, tx_dot), GRID_STARS);
  for (int i = 0; i < GRID_STARS; i++) {
    float x = -800 + 1600 * rnd(31, (uint32_t)i), y = 70 + 300 * powf(rnd(32, (uint32_t)i), 0.8f);
    float s = 1.2f + 1.8f * rnd(33, (uint32_t)i), b = 0.25f + 0.6f * rnd(34, (uint32_t)i) * smooth(60, 200, y);
    place(B.grid_stars, i, (t3_vec3){ x, y + GRID_Y, -515 }, about(0, 0, 1, 0), (t3_vec3){ s, s, 1 }, (t3_color){ b * 0.9f, b * 0.8f, b });
  }
  done(B.grid_stars);
  B.sun = quad(at, 170, 170, texture_rgb(256, 256, tx_sun), (t3_vec3){ 0, 30 + GRID_Y, -500 });
  T3_OBJ(B.sun)->render_order = -3;
  /* the wire mountains: triangles of glowing line along the horizon either side of the sun, dark inside (a black
   * triangle in front of the sky, its two edges lit) */
  {
    t3_material *m = t3_mesh_basic_material_new(0x000000);
    m->color = (t3_color){ 0.02f, 0.0f, 0.035f };
    m->side = T3_DOUBLE_SIDE;
    float tri[9] = { -0.5f, 0, 0, 0.5f, 0, 0, 0, 1, 0 };
    t3_geometry *g = t3_geometry_new();
    t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, tri, 3, 3));
    B.peaks = many_m(at, g, m, PEAKS * 2);
    T3_OBJ(B.peaks)->render_order = -2;
  }
  {
    t3_geometry *g = t3_plane_geometry_new(1600, 700, 1, 1);
    t3_material *m = t3_mesh_basic_material_new(0x000000);
    m->transparent = true, m->depth_write = false, m->opacity = 0.94f;
    m->color = (t3_color){ 0.03f, 0.0f, 0.055f };
    B.grid_floor = add_mesh(at, g, m, (t3_vec3){ 0, GRID_Y - 0.05f, -340 });
    T3_OBJ(B.grid_floor)->quaternion = about(1, 0, 0, -PI / 2);
    T3_OBJ(B.grid_floor)->render_order = -1;
  }
  B.horizon = quad(at, 1600, 16, texture(4, 64, tx_horizon), (t3_vec3){ 0, GRID_Y + 0.2f, -680 });
  B.haze_band = quad(at, 1600, 70, texture(4, 128, tx_horizon), (t3_vec3){ 0, GRID_Y + 4, -660 });
  B.grid_long = many(at, t3_plane_geometry_new(1, 1, 1, 1), NULL, GRID_LONG);
  B.grid_cross = many(at, t3_plane_geometry_new(1, 1, 1, 1), NULL, GRID_CROSS);
  /* the mountains' outlines: two edge quads a peak (thin additive lines) */
  t3_instanced_mesh *edges = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(8, 4, tx_dot), PEAKS * 4);
  int e = 0;
  for (int i = 0; i < PEAKS * 2; i++) {
    int side = i < PEAKS ? -1 : 1, k = i % PEAKS;
    float x = (float)side * (95 + 520 * ((float)k + rnd(41, (uint32_t)i)) / PEAKS) * (1 + 0.15f * rnd(45, (uint32_t)i));
    float w = 70 + 110 * rnd(42, (uint32_t)i), h = (18 + 70 * rnd(43, (uint32_t)i)) * (0.5f + 0.5f * smooth(80, 300, fabsf(x)));
    float z = -600 + 40 * rnd(44, (uint32_t)i);
    place(B.peaks, i, (t3_vec3){ x, GRID_Y, z }, about(0, 0, 1, 0), (t3_vec3){ w, h, 1 }, (t3_color){ 1, 1, 1 });
    /* its two edges */
    for (int s2 = -1; s2 <= 1; s2 += 2) {
      float ex = x + (float)s2 * w * 0.25f, ey = GRID_Y + h * 0.5f, len = sqrtf(w * w * 0.25f + h * h);
      float ang = atan2f((float)s2 * w * 0.5f, h); /* the edge leans out from the peak */
      float b = 0.6f + 0.4f * rnd(46, (uint32_t)i);
      place(edges, e++, (t3_vec3){ ex, ey, z + 0.5f }, about(0, 0, 1, -ang), (t3_vec3){ 1.6f, len, 1 }, (t3_color){ b, 0.15f * b, 0.9f * b });
    }
  }
  edges->count = e;
  done(edges);
  done(B.peaks);
}

static void update_grid(float beat, float energy) {
  float e = 0.9f + 0.2f * energy;
  tint(B.grid_sky, 1, 1, 1, 0.9f);
  tint(B.sun, 1, 1, 1, 1.25f);
  tint(B.horizon, 1.0f, 0.3f, 0.8f, 0.8f * e);
  tint(B.haze_band, 0.9f, 0.2f, 0.7f, 0.22f * e);
  t3_quat flat = about(1, 0, 0, -PI / 2);
  const float far = 680;
  for (int i = 0; i < GRID_LONG; i++) {
    float x = ((float)i - (GRID_LONG - 1) * 0.5f) * GRID_STEP;
    float b = 0.6f * (0.6f + 0.4f * expf(-fabsf(x) / 60)) * e;
    place(B.grid_long, i, (t3_vec3){ x, GRID_Y, -far * 0.5f }, flat, (t3_vec3){ 0.16f, far, 1 }, (t3_color){ b, 0.12f * b, 0.9f * b });
  }
  done(B.grid_long);
  float off = fmodf(beat, 1.0f) * GRID_STEP;
  for (int i = 0; i < GRID_CROSS; i++) {
    float z = -2 - (float)i * GRID_STEP * powf(1.035f, (float)i) + off * powf(1.035f, (float)i);
    float dist = 22 - z, fade = expf(-dist / 260.0f) * smooth(far, far * 0.6f, -z);
    float b = 0.8f * fade * e, th = 0.14f + 0.004f * dist;
    place(B.grid_cross, i, (t3_vec3){ 0, GRID_Y, z }, flat, (t3_vec3){ 1600, th, 1 }, (t3_color){ b * 0.25f, 0.85f * b, b });
  }
  done(B.grid_cross);
}

/* ── the crowd: people as dark shapes against the light, rim-lit by the room's beams, swaying and dancing with the
 * music (shared by the club and the stage) ── */

#define CROWD 150
static crowd build_crowd(t3_object *at, t3_color rim) {
  /* one body: a torso (a lathe from the hips to the crown: shoulders, a neck, a head); two arms each, a tapered limb
   * hung from the shoulder, raised overhead by those who have their hands up */
  t3_material *m = t3_mesh_standard_material_new(0x000000);
  m->color = (t3_color){ 0.012f, 0.012f, 0.014f };
  m->roughness = 0.7f;
  t3_vec2 prof[12];
  for (int i = 0; i < 12; i++) {
    static const float P[12][2] = { { 0, 0 }, { 0.2f, 0.02f }, { 0.24f, 0.5f }, { 0.27f, 0.9f }, { 0.26f, 1.1f }, { 0.12f, 1.2f },
                                    { 0.08f, 1.26f }, { 0.12f, 1.32f }, { 0.13f, 1.42f }, { 0.11f, 1.52f }, { 0.06f, 1.58f }, { 0, 1.6f } };
    prof[i] = (t3_vec2){ P[i][0], P[i][1] };
  }
  /* the rim: the edges facing away from the camera catch the beams behind them, so each head and pair of shoulders
   * reads against the haze (cartwheel/rim_light.h; full strength at every distance: the crowd is all far off) */
  cw_rim_light_desc rd = cw_rim_light_default();
  rd.color = rim, rd.strength = 1.6f, rd.power = 2.6f, rd.near = 0, rd.far = 1, rd.close = 1;
  cw_rim_light_apply(m, &rd);
  crowd c;
  t3_retain(m);
  c.mat = m;
  c.body = many_m(at, t3_lathe_geometry_new(prof, 12, 10, 0, 2 * PI), m, CROWD);
  t3_retain(m);
  /* an arm: a limb 0.62 long, its top (the shoulder) at the origin */
  t3_geometry *ag = t3_cylinder_geometry_new(0.055f, 0.04f, 0.62f, 6, 1, false);
  t3_geometry_translate(ag, 0, -0.31f, 0);
  c.arm = many_m(at, ag, m, CROWD * 2);
  return c;
}
/* the crowd at the beat: each person bobs a little off the beat and sways over two bars; a share of them (more as the
 * music gets louder) have their hands up, pumping slowly on the beat; the rest have them down at their sides, swinging
 * a little. Every motion a sine (never a jump), each person in their own phase. */
static void update_crowd(crowd *c, float beat, float z0, float depth, float width, uint32_t seed, float y0, float energy) {
  float share = 0.18f + 0.5f * fmaxf(0, energy);
  for (int i = 0; i < CROWD; i++) {
    float r1 = rnd(seed, (uint32_t)i), r2 = rnd(seed + 1, (uint32_t)i), r3 = rnd(seed + 2, (uint32_t)i), r4 = rnd(seed + 3, (uint32_t)i);
    float z = z0 - depth * r2, x = (r1 - 0.5f) * width * (1 + 0.3f * (1 - r2));
    float s = (1.6f + 0.35f * r3) / 1.6f * 1.05f;
    float ph = 2 * PI * (beat + 0.15f * r3);
    float bob = 0.06f * (0.5f + 0.5f * sinf(ph)), sway = 0.07f * sinf(PI * beat * 0.5f + r1 * 6);
    t3_quat q = about(0, 0, 1, sway);
    place(c->body, i, (t3_vec3){ x, y0 + bob, z }, q, (t3_vec3){ 1.05f, s, 1.05f }, (t3_color){ 1, 1, 1 });
    /* hands up: a smooth share (a person lifts their arms over a couple of beats as the share passes them) */
    float up = smooth(r4 - 0.08f, r4 + 0.08f, share);
    for (int k = 0; k < 2; k++) {
      float side = k ? 1.0f : -1.0f;
      /* the shoulder, in the body's frame (sway turns it) */
      float sx = side * 0.25f * 1.05f, sy = 1.12f * s;
      float cs = cosf(sway), sn = sinf(sway);
      t3_vec3 sh = { x + sx * cs - sy * sn, y0 + bob + sx * sn + sy * cs, z };
      /* down: hanging, swinging a little; up: raised overhead and out, pumping on the beat */
      float pump = 0.18f * sinf(ph + (float)k * 0.6f);
      float down_a = side * (0.12f + 0.06f * sinf(ph * 0.5f + r1 * 4));
      float up_a = side * (2.65f + 0.25f * r3) - side * pump;
      float a2 = down_a + (up_a - down_a) * up + sway;
      place(c->arm, i * 2 + k, sh, about(0, 0, 1, a2), (t3_vec3){ 1, s, 1 }, (t3_color){ 1, 1, 1 });
    }
  }
  done(c->body);
  done(c->arm);
}
/* the rim's colour now (the beams' colour drifting) */
static void crowd_rim(crowd *c, t3_color col, float k) {
  cw_rim_light_desc rd;
  if (!cw_rim_light_get(c->mat, &rd)) return;
  rd.color = col, rd.strength = k;
  cw_rim_light_apply(c->mat, &rd);
}

/* ── lasers ──────────────────────────────────────────────────────────── */

#define FANS 3
#define FAN_RAYS 7
#define LASER_Z (-36.0f)
static const float FAN_X[FANS] = { -16, 0, 16 };
static const float FAN_COL[FANS][3] = { { 0.15f, 1.0f, 0.3f }, { 1.0f, 0.15f, 0.7f }, { 0.2f, 0.7f, 1.0f } };

static void build_lasers(void) {
  t3_object *at = obj(BD_LASERS);
  B.rays = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(32, 256, tx_beam), FANS * FAN_RAYS);
  B.heads = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(32, 32, tx_dot), FANS);
  /* a glowing panel behind the booth */
  {
    t3_material *m = t3_mesh_basic_material_new(0x000000);
    B.booth_wall = add_mesh(at, t3_box_geometry_new(14, 1.2f, 0.3f, 1, 1, 1), m, (t3_vec3){ 0, 2.2f, -31 });
  }
  for (int k = 0; k < 3; k++) {
    static const char *const H[3] = { "HeadA", "HeadB", "HeadC" };
    B.head[k] = cw_node_get(B.root[BD_LASERS], H[k]);
  }
  B.crowd_l = build_crowd(at, (t3_color){ 0.8f, 0.3f, 1.0f });
}

static void update_lasers(float beat, float energy) {
  float e = 0.85f + 0.3f * energy;
  B.booth_wall->materials[0]->color = (t3_color){ 0.8f * e, 0.25f * e, 1.6f * e };
  const t3_vec3 eye = { 0, 3.6f, 18 };
  for (int k = 0; k < FANS; k++) {
    /* each fan reaches out over the crowd toward the room: a sheet of rays from its head, sweeping slowly side to side
     * (a whole sway over four bars) and rising and falling over eight, each fan out of step with the others */
    float sway = 22 * sinf(2 * PI * beat / 32.0f + (float)k * 2.1f);
    float open = 26 + 12 * sinf(2 * PI * beat / 32.0f + (float)k);
    float lift = 6 + 5 * sinf(2 * PI * beat / 24.0f + (float)k * 1.3f);
    t3_vec3 o = { FAN_X[k], 9.0f, LASER_Z };
    for (int r = 0; r < FAN_RAYS; r++) {
      float f = ((float)r / (FAN_RAYS - 1)) - 0.5f;
      t3_vec3 tg = { o.x + sway + open * f * 2, lift + 3 + 2.5f * f * f * 4, 26 };
      t3_vec3 d = { tg.x - o.x, tg.y - o.y, tg.z - o.z };
      float dl = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
      d = (t3_vec3){ d.x / dl, d.y / dl, d.z / dl };
      float b = 2.4f * e;
      /* each ray ends well before the camera (a beam passing by the lens would sweep a wedge of light over the screen) */
      float len = fminf(75, (2.0f - o.z) / fmaxf(d.z, 0.05f));
      place_beam(B.rays, k * FAN_RAYS + r, o, d, len, 1.5f, eye, (t3_color){ FAN_COL[k][0] * b, FAN_COL[k][1] * b, FAN_COL[k][2] * b });
    }
    float hb = 3.0f;
    place(B.heads, k, o, about(0, 0, 1, 0), (t3_vec3){ 1.6f, 1.6f, 1 }, (t3_color){ FAN_COL[k][0] * hb, FAN_COL[k][1] * hb, FAN_COL[k][2] * hb });
  }
  done(B.rays);
  done(B.heads);
  /* the moving heads: each sweeps across the crowd and back over eight bars, slowly, in its own phase; colours drift */
  for (int k = 0; k < 3; k++) {
    if (!B.head[k].id) continue;
    float ph = 2 * PI * beat / 32.0f + (float)k * 2.0f;
    t3_vec3 d = { 0.6f * sinf(ph) + (float)(k - 1) * 0.3f, -1.0f, 0.1f * cosf(ph * 0.7f) }; /* across the room, seen from the side */
    aim(B.head[k], d);
    setf(B.head[k], "energy", 3000 * e);
  }
  update_crowd(&B.crowd_l, beat, 6, 22, 40, 50, 0, energy);
  { /* the rim takes the fans' colours in turn, slowly (a whole turn over 32 bars) */
    float u = fmodf(beat / 128.0f, 1.0f) * FANS;
    int k0 = (int)u % FANS, k1 = (k0 + 1) % FANS;
    float f = smooth(0, 1, u - floorf(u));
    t3_color c = { FAN_COL[k0][0] + (FAN_COL[k1][0] - FAN_COL[k0][0]) * f, FAN_COL[k0][1] + (FAN_COL[k1][1] - FAN_COL[k0][1]) * f, FAN_COL[k0][2] + (FAN_COL[k1][2] - FAN_COL[k0][2]) * f };
    crowd_rim(&B.crowd_l, c, 1.8f * e);
  }
}

/* ── stage ───────────────────────────────────────────────────────────── */

#define WALL_W 46
#define WALL_H 18
static void build_stage(void) {
  t3_object *at = obj(BD_STAGE);
  /* the LED wall behind the band: a grid of soft pixels, a slow wave of colour across it */
  B.wall_px = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(16, 16, tx_dot), WALL_W * WALL_H);
  for (int k = 0; k < 3; k++) {
    static const char *const S[3] = { "SpotA", "SpotB", "SpotC" };
    B.spot[k] = cw_node_get(B.root[BD_STAGE], S[k]);
  }
  B.crowd_s = build_crowd(at, (t3_color){ 0.35f, 0.2f, 0.12f });
  B.crowd_s.mat->color = (t3_color){ 0.003f, 0.003f, 0.004f }, B.crowd_s.mat->roughness = 0.95f; /* silhouettes: the front key reaches them */
  band_build(B.root[BD_STAGE]);
}

static void update_stage(float beat, float t, float energy) {
  float e = 0.85f + 0.3f * energy;
  for (int y = 0; y < WALL_H; y++)
    for (int x = 0; x < WALL_W; x++) {
      float u = (float)x / (WALL_W - 1), v = (float)y / (WALL_H - 1);
      /* a slow plasma of two waves, warm to cold */
      float w = 0.5f + 0.5f * sinf(u * 5 + beat * 0.25f) * cosf(v * 4 - beat * 0.18f + u * 2);
      float b = (0.12f + 0.45f * w * w) * e;
      t3_color c = { b * (0.3f + 0.7f * w), b * 0.25f, b * (1.0f - 0.6f * w) };
      place(B.wall_px, y * WALL_W + x, (t3_vec3){ -11.5f + 23.0f * u, 3.2f + 8.5f * v, -13.7f }, about(0, 0, 1, 0), (t3_vec3){ 0.55f, 0.55f, 1 }, c);
    }
  done(B.wall_px);
  for (int k = 0; k < 3; k++) {
    if (!B.spot[k].id) continue;
    float ph = 2 * PI * beat / 32.0f + (float)k * 2.1f;
    t3_vec3 d = { 0.45f * sinf(ph) + (float)(k - 1) * 0.3f, -1.0f, -0.25f + 0.15f * cosf(ph * 0.8f) }; /* seen from the side: shafts, not glare */
    aim(B.spot[k], d);
    setf(B.spot[k], "energy", 2000 * e);
  }
  update_crowd(&B.crowd_s, beat, 2.6f, 3.2f, 18, 60, -0.45f, energy * 0.5f - 0.05f); /* close in front of the deck: heads and shoulders along the foot of the picture, fewer hands up */
  band_update(beat, t, energy);
}

/* ── clean ───────────────────────────────────────────────────────────── */

#define HX 120           /* vertices across */
#define HZ 60            /* rows (each half a beat) */
#define HILL_W 900.0f    /* across */
#define ROW_DZ 8.0f      /* a row's depth */
static cw_noise hill_n;
static void hill_colour(float h, float slope, float *c) {
  /* low: meadow green; slopes: earth and rock; high: pale rock and snow */
  float grass[3] = { 0.07f, 0.16f, 0.035f }, rock[3] = { 0.2f, 0.17f, 0.14f }, snow[3] = { 0.85f, 0.86f, 0.9f };
  float r = smooth(0.35f, 0.7f, slope) * 0.9f + smooth(28, 48, h) * 0.6f;
  r = fminf(1, r);
  float s = smooth(56, 72, h) * (1 - smooth(0.55f, 0.85f, slope));
  for (int k = 0; k < 3; k++) c[k] = (grass[k] + (rock[k] - grass[k]) * r) * (1 - s) + snow[k] * s;
}
static void build_clean(void) {
  t3_object *at = obj(BD_CLEAN);
  hill_n = cw_noise_default(), hill_n.seed = 21, hill_n.frequency = 0.02f, hill_n.fractal = CW_NOISE_FRACTAL_FBM, hill_n.octaves = 4;
  /* the hills: a grid whose rows ride toward the camera with the beat, their heights the other track */
  int nv = HX * HZ;
  float *pos = calloc((size_t)nv * 3, sizeof(float)), *col = calloc((size_t)nv * 3, sizeof(float));
  uint32_t *idx = malloc(sizeof(uint32_t) * (size_t)(HX - 1) * (HZ - 1) * 6);
  int ni = 0;
  for (int z = 0; z < HZ - 1; z++)
    for (int x = 0; x < HX - 1; x++) {
      uint32_t a = (uint32_t)(z * HX + x), b = a + 1, c = a + HX, d = c + 1;
      idx[ni++] = a, idx[ni++] = c, idx[ni++] = b, idx[ni++] = b, idx[ni++] = c, idx[ni++] = d;
    }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, nv, 3));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, idx, ni, 1));
  free(pos), free(col), free(idx);
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->vertex_colors = true, m->roughness = 0.92f, m->metalness = 0;
  B.hills_g = g;
  t3_retain(g);
  B.hills = add_mesh(at, g, m, (t3_vec3){ 0, 0, 0 });
  T3_OBJ(B.hills)->cast_shadow = true, T3_OBJ(B.hills)->receive_shadow = true;
  B.hill_h = calloc((size_t)nv, sizeof(float));
  /* the far range: a massif along the horizon that the valley opens onto, its two great horns standing either side of the
   * rivers' far end (the low sun sets between them), sharp ridged rock with snow on its shoulders and in its gullies,
   * lower jagged ranges running off to the sides */
  {
    const int RX = 300, RZ = 44;
    float *p = calloc((size_t)RX * RZ * 3, sizeof(float)), *c = calloc((size_t)RX * RZ * 3, sizeof(float));
    cw_noise rn = cw_noise_default(), gn = cw_noise_default();
    rn.seed = 22, rn.frequency = 0.0035f, rn.fractal = CW_NOISE_FRACTAL_RIDGED, rn.octaves = 6;
    gn.seed = 23, gn.frequency = 0.02f, gn.fractal = CW_NOISE_FRACTAL_FBM, gn.octaves = 3;
    /* the horns: x, z, height, base radius */
    static const float HORN[4][4] = { { -330, -1180, 430, 260 }, { 300, -1260, 360, 240 }, { -760, -1300, 280, 300 }, { 820, -1220, 250, 280 } };
    for (int z = 0; z < RZ; z++)
      for (int x = 0; x < RX; x++) {
        float X = -1800 + 3600 * (float)x / (RX - 1), Z = -880 - 640 * (float)z / (RZ - 1);
        float n = cw_noise_2d(&rn, X, Z) * 0.5f + 0.5f;                     /* ridged: sharp crests */
        float H = (30 + 170 * n * n) * smooth(-880, -1060, Z);
        for (int k = 0; k < 4; k++) { /* a horn: a steep pyramid of ridges, its faces cut by the ridged noise */
          float dx = X - HORN[k][0], dz = (Z - HORN[k][1]) * 1.3f, d = sqrtf(dx * dx + dz * dz) / HORN[k][3];
          /* faces and aretes: the cone pinched along four ridges turning with the peak, and cut by the noise's gullies */
          float ang = atan2f(dz, dx) + (float)k * 0.7f, arete = 0.82f + 0.18f * fabsf(cosf(ang * 2.0f));
          float face = fmaxf(0, 1 - d / arete);
          H = fmaxf(H, HORN[k][2] * powf(face, 1.45f) * (0.7f + 0.45f * n));
        }
        float *pp = p + (z * RX + x) * 3;
        pp[0] = X, pp[1] = H, pp[2] = Z;
        float cc[3];
        hill_colour(fminf(H * 0.25f, 50), 0.6f, cc); /* (the range's own snow below) */
        /* snow: above a line that dips into the gullies (the noise), bare rock on the steepest crests */
        float g = cw_noise_2d(&gn, X, Z) * 0.5f + 0.5f;
        float sn = smooth(200 + 90 * g, 250 + 90 * g, H) * smooth(0.8f, 0.5f, n); /* the crests' rock stays bare */
        for (int k = 0; k < 3; k++) c[(z * RX + x) * 3 + k] = cc[k] * (1 - sn) + (k == 2 ? 0.95f : 0.9f) * sn;
      }
    uint32_t *ix = malloc(sizeof(uint32_t) * (size_t)(RX - 1) * (RZ - 1) * 6);
    int n2 = 0;
    for (int z = 0; z < RZ - 1; z++)
      for (int x = 0; x < RX - 1; x++) {
        uint32_t a = (uint32_t)(z * RX + x), b = a + 1, cc = a + RX, d = cc + 1;
        ix[n2++] = a, ix[n2++] = cc, ix[n2++] = b, ix[n2++] = b, ix[n2++] = cc, ix[n2++] = d;
      }
    t3_geometry *rg = t3_geometry_new();
    t3_geometry_set_attribute(rg, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, p, RX * RZ, 3));
    t3_geometry_set_attribute(rg, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, c, RX * RZ, 3));
    t3_geometry_set_index(rg, t3_attribute_new(T3_UINT32, ix, n2, 1));
    t3_geometry_compute_vertex_normals(rg);
    free(p), free(c), free(ix);
    t3_material *rm = t3_mesh_standard_material_new(0xffffff);
    rm->vertex_colors = true, rm->roughness = 0.95f;
    B.range = add_mesh(at, rg, rm, (t3_vec3){ 0, 0, 0 });
  }
}

/* the hills: row r of the grid is the music half a beat apart, rb its beat; the rows ride toward the camera at a
 * row a half beat, so the front row is about now and the far rows what is coming */
/* one row of the hills at beat rb: its heights into B.hill_h[r] */
static void hill_row(const song *s, int r, float rb) {
  float h = 0.4f, band[3] = { 0.5f, 0.5f, 0.5f };
  if (s) {
    float hue, br, ton;
    song_look_at(s, rb, &h, &hue, &br, &ton);
    /* smoothed over a beat either side, as the field's land is */
    float d3[3] = { 0, 0, 0 };
    for (int t = -2; t <= 2; t++) {
      float bt[3], wt = (float)(3 - abs(t)) / 9.0f;
      song_bands_at(s, rb + 0.5f * (float)t, bt);
      for (int k = 0; k < 3; k++) d3[k] += wt * bt[k];
    }
    for (int k = 0; k < 3; k++) band[k] = fminf(2, d3[k]);
  }
  for (int x = 0; x < HX; x++) {
    float X = -HILL_W * 0.5f + HILL_W * (float)x / (HX - 1), ax = fabsf(X);
    /* the valley the rivers run down: flat in the middle, the land rising from its banks; bass beside the valley,
     * mid further, treble far out */
    float u = smooth(20, 300, ax);
    float bv = u < 0.5f ? band[0] + (band[1] - band[0]) * (u / 0.5f) : band[1] + (band[2] - band[1]) * ((u - 0.5f) / 0.5f);
    float rise = smooth(9, 42, ax);
    float detail = cw_noise_2d(&hill_n, X, rb * ROW_DZ * 2);
    /* the land's own rolling shape (it rides with the rows), and the music raising it: the louder the other track, the
     * higher its hills */
    float land = 14 + 22 * (0.5f + 0.5f * detail) * (0.4f + 0.6f * u);
    B.hill_h[r * HX + x] = rise * (land + 34 * (0.3f + 0.7f * h) * powf(fmaxf(0, bv), 0.8f) * (0.5f + 0.5f * u)) - 1.5f * (1 - rise);
  }
}

/* the hills: row r is the music at beat first + (r - 2) / 2; the rows ride toward the camera a row each half beat (the
 * mesh moves; its rows are rebuilt only when a new half beat comes in: one new row's heights, the rest shifted) */
static void update_clean(const song *s, float beat) {
  static float built_first = -1e9f;
  static const song *built_s;
  float first = floorf(beat * 2) / 2, frac = (beat - first) * 2;
  T3_OBJ(B.hills)->position = (t3_vec3){ 0, 0, ROW_DZ * frac };
  if (first == built_first && s == built_s) return;
  if (s == built_s && first == built_first + 0.5f) { /* one half beat on: shift the rows down one, make the far one */
    memmove(B.hill_h, B.hill_h + HX, sizeof(float) * HX * (HZ - 1));
    hill_row(s, HZ - 1, first + 0.5f * (float)(HZ - 1 - 2));
  } else
    for (int r = 0; r < HZ; r++) hill_row(s, r, first + 0.5f * (float)(r - 2));
  built_first = first, built_s = s;
  t3_attribute *pa = B.hills_g->attributes[T3_ATTR_POSITION], *ca = B.hills_g->attributes[T3_ATTR_COLOR];
  float *P = (float *)pa->array, *Cc = (float *)ca->array;
  const float z_front = 40;
  for (int r = 0; r < HZ; r++)
    for (int x = 0; x < HX; x++) {
      int i = r * HX + x;
      float *p = P + i * 3;
      p[0] = -HILL_W * 0.5f + HILL_W * (float)x / (HX - 1), p[1] = B.hill_h[i], p[2] = z_front - ROW_DZ * (float)(r - 2);
      /* colour by height and slope */
      float hx = B.hill_h[r * HX + (x < HX - 1 ? x + 1 : x)] - B.hill_h[r * HX + (x > 0 ? x - 1 : x)];
      float hz = B.hill_h[(r < HZ - 1 ? r + 1 : r) * HX + x] - B.hill_h[(r > 0 ? r - 1 : r) * HX + x];
      float slope = sqrtf(hx * hx / (4 * 22.0f) + hz * hz / (4 * 64.0f)) * 0.35f;
      hill_colour(B.hill_h[i], slope, Cc + i * 3);
      if (B.hill_h[i] < 0.5f) { /* the valley floor: a dark meadow */
        float k = smooth(0.5f, -1.0f, B.hill_h[i]);
        Cc[i * 3] = Cc[i * 3] * (1 - k) + 0.04f * k, Cc[i * 3 + 1] = Cc[i * 3 + 1] * (1 - k) + 0.09f * k, Cc[i * 3 + 2] = Cc[i * 3 + 2] * (1 - k) + 0.03f * k;
      }
    }
  pa->version++, ca->version++;
  t3_geometry_compute_vertex_normals(B.hills_g);
}

static void cmd_bdcam(int argc, const char **argv, cw_reply *r);
static void cmd_maze(int argc, const char **argv, cw_reply *r);
static void cmd_bdset(int argc, const char **argv, cw_reply *r);

static float tx_trail(float u, float v) { float x = (u - 0.5f) * 2; return expf(-x * x * 4) * powf(1 - v, 1.6f); }

/* ── maze ────────────────────────────────────────────────────────────── */

#define MZ_W 17          /* cells across */
#define MZ_H 128         /* cells along (the maze repeats) */
#define MZ_SHOW 30       /* rows drawn */
#define MZ_CELL 10.0f
#define MZ_WALLS (MZ_W * MZ_SHOW * 2 * 2 + 64)
#define CRITTERS 8
#define TRAVELLERS 10
static uint8_t mz[MZ_H][MZ_W]; /* bit 0: a wall on the cell's near side (+z), bit 1: on its left (-x) */
static int mz_built_row = -1000000;
static struct { int cx, cy, px, py; float t0; } crit[CRITTERS];

static void build_maze(void) {
  t3_object *at = obj(BD_MAZE);
  /* the maze: a depth-first carve over a band that wraps along its length (the seam row opened), so it rides forever */
  static uint8_t seen[MZ_H][MZ_W];
  static int16_t stack[MZ_H * MZ_W][2];
  for (int y = 0; y < MZ_H; y++)
    for (int x = 0; x < MZ_W; x++) mz[y][x] = 3, seen[y][x] = 0;
  uint32_t s = 0x2545f491u;
  int sp = 0;
  stack[sp][0] = MZ_W / 2, stack[sp][1] = 0, sp++, seen[0][MZ_W / 2] = 1;
  while (sp > 0) {
    int x = stack[sp - 1][0], y = stack[sp - 1][1], opts[4], n = 0;
    static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    for (int k = 0; k < 4; k++) {
      int nx = x + DX[k], ny = (y + DY[k] + MZ_H) % MZ_H;
      if (nx >= 0 && nx < MZ_W && !seen[ny][nx]) opts[n++] = k;
    }
    if (!n) { sp--; continue; }
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    int k = opts[s % (uint32_t)n], nx = x + DX[k], ny = (y + DY[k] + MZ_H) % MZ_H;
    if (k == 0) mz[y][nx] &= (uint8_t)~2;          /* right: the next cell's left wall */
    else if (k == 1) mz[y][x] &= (uint8_t)~2;      /* left: this cell's left wall */
    else if (k == 2) mz[y][x] &= (uint8_t)~1;      /* on: this cell's near wall (rows count away from the camera) */
    else mz[ny][x] &= (uint8_t)~1;                 /* back: the next cell's near wall */
    seen[ny][nx] = 1;
    stack[sp][0] = (int16_t)nx, stack[sp][1] = (int16_t)ny, sp++;
  }
  /* loops: knock out a few more walls, as an arcade maze has no dead ends to speak of */
  for (int y = 0; y < MZ_H; y++)
    for (int x = 1; x < MZ_W; x++) {
      s ^= s << 13, s ^= s >> 17, s ^= s << 5;
      if ((s & 15) < 3) mz[y][x] &= (uint8_t)~((s >> 4) & 1 ? 1 : 2);
    }
  B.maze_root = (t3_object *)t3_group_new();
  t3_object_add(at, B.maze_root);
  t3_release(B.maze_root);
  /* the walls: each a pair of thin glowing lines lying on the floor, deep blue (the bloom gives them their glow) */
  B.walls = many(B.maze_root, t3_plane_geometry_new(1, 1, 1, 1), texture(4, 16, tx_horizon), MZ_WALLS);
  B.critters = many(at, t3_circle_geometry_new(1, 4), NULL, CRITTERS * 2);
  B.critters->count = 0;
  /* the travellers: trails of faint light running the halls, a long thin streak fading back from its head (a line along
   * the corridor: no note is a line; dim violet-grey, a colour no river uses) */
  B.trails = many(B.maze_root, t3_plane_geometry_new(1, 1, 1, 1), texture(8, 64, tx_trail), TRAVELLERS * 3);
  for (int i = 0; i < CRITTERS; i++) crit[i].cx = crit[i].px = 2 + (i * 7) % (MZ_W - 4), crit[i].cy = crit[i].py = 6 + i * 4, crit[i].t0 = 0;
}

static float mz_x(int x) { return ((float)x - (MZ_W - 1) * 0.5f) * MZ_CELL; }
static float mz_z(int y) { return 40.0f - (float)y * MZ_CELL; } /* row y's centre, row 0 nearest */

static void update_maze(float beat, float energy) {
  /* a cell every two beats toward the camera (a wall's two lines passing a point at a beat a cell is a regular
   * flicker at 4 Hz): the walls are rebuilt when a new row comes in, the group slides between */
  float cells = beat * 0.5f;
  int row = (int)floorf(cells);
  float frac = cells - floorf(cells);
  B.maze_root->position = (t3_vec3){ 0, 0, frac * MZ_CELL };
  if (row != mz_built_row) {
    mz_built_row = row;
    int n = 0;
    t3_quat flat = about(1, 0, 0, -PI / 2), side = qmul(about(1, 0, 0, -PI / 2), about(0, 0, 1, PI / 2));
    for (int r = 0; r < MZ_SHOW; r++) {
      int y = ((row + r) % MZ_H + MZ_H) % MZ_H;
      float fade = smooth((float)MZ_SHOW, (float)MZ_SHOW * 0.55f, (float)r);
      for (int x = 0; x <= MZ_W; x++) {
        uint8_t w = x < MZ_W ? mz[y][x] : 2;
        float cx = mz_x(x), cz = mz_z(r), b = 1.3f * fade;
        t3_color c = { 0.05f * b, 0.12f * b, 1.0f * b };
        for (int k = -1; k <= 1; k += 2) {
          if ((w & 1) && x < MZ_W && n < MZ_WALLS) /* the near wall: two lines across */
            place(B.walls, n++, (t3_vec3){ cx, 0, cz + MZ_CELL * 0.5f + (float)k * 1.3f }, flat, (t3_vec3){ MZ_CELL + 2.6f, 0.45f, 1 }, c);
          if ((w & 2) && n < MZ_WALLS) /* the left wall: two lines along */
            place(B.walls, n++, (t3_vec3){ cx - MZ_CELL * 0.5f + (float)k * 1.3f, 0, cz }, side, (t3_vec3){ MZ_CELL + 2.6f, 0.45f, 1 }, c);
        }
      }
    }
    B.walls->count = n;
    done(B.walls);
  }
  if (B.maze_old) {
    B.critters->count = CRITTERS * 2;
    /* the critters: four-pointed stars hopping cell to cell along open halls, a cell every half beat, eased (never a jump) */
    static const float CC[4][3] = { { 1, 0.35f, 0.6f }, { 0.4f, 1, 0.6f }, { 1, 0.7f, 0.25f }, { 0.5f, 0.8f, 1 } };
    for (int i = 0; i < CRITTERS; i++) {
      float step = beat * 2 + (float)i * 0.37f;
      int k = (int)floorf(step);
      if (k != (int)crit[i].t0) {
        crit[i].t0 = (float)k, crit[i].px = crit[i].cx, crit[i].py = crit[i].cy;
        int cx = crit[i].cx, cy = crit[i].cy;
        int yy = ((cy) % MZ_H + MZ_H) % MZ_H;
        int opts[4][2], no = 0;
        if (cx + 1 < MZ_W && !(mz[yy][cx + 1] & 2)) opts[no][0] = cx + 1, opts[no++][1] = cy;
        if (cx > 0 && !(mz[yy][cx] & 2)) opts[no][0] = cx - 1, opts[no++][1] = cy;
        if (!(mz[yy][cx] & 1)) opts[no][0] = cx, opts[no++][1] = cy - 1;
        if (!(mz[(yy + 1) % MZ_H][cx] & 1)) opts[no][0] = cx, opts[no++][1] = cy + 1;
        if (no) { int p = (int)(hsh((uint32_t)k, (uint32_t)i) % (uint32_t)no); crit[i].cx = opts[p][0], crit[i].cy = opts[p][1]; }
      }
      float u = step - floorf(step);
      u = u * u * (3 - 2 * u);
      float gx = mz_x(crit[i].px) + (mz_x(crit[i].cx) - mz_x(crit[i].px)) * u;
      float gy = (float)crit[i].py + (float)(crit[i].cy - crit[i].py) * u;
      /* stay on screen: the maze rides toward the camera, so a critter that falls behind is carried to the far rows */
      float rr = gy - (float)row - frac;
      if (rr < 1 || rr > MZ_SHOW - 6) { crit[i].cy = crit[i].py = row + 12 + (int)(hsh(i, (uint32_t)row) % 20u); continue; }
      float z = 40.0f - rr * MZ_CELL, e2 = 0.75f + 0.2f * energy;
      const float *c = CC[i % 4];
      float spin = beat * 1.5f + (float)i;
      place(B.critters, i * 2, (t3_vec3){ gx, 2.4f, z }, qmul(about(1, 0, 0, -0.78f), about(0, 0, 1, spin)), (t3_vec3){ 2.6f, 2.6f, 1 }, (t3_color){ c[0] * e2, c[1] * e2, c[2] * e2 });
      place(B.critters, i * 2 + 1, (t3_vec3){ gx, 2.45f, z + 0.1f }, qmul(about(1, 0, 0, -0.78f), about(0, 0, 1, -spin * 0.7f)), (t3_vec3){ 1.1f, 1.1f, 1 }, (t3_color){ 0.05f, 0.05f, 0.08f });
    }

    done(B.critters);
  } else if (B.critters->count) B.critters->count = 0, done(B.critters);
  /* the travellers: each runs the halls a cell a beat, choosing a way at each junction (never straight back unless it
   * must), drawn as a streak of light from where it was a cell and a half ago to where it is now, eased between cells so
   * it glides; the maze rides toward the camera and a traveller rides with it (it lives in the maze's own frame) */
  if (!B.maze_old) {
    static struct { int cx, cy, px, py, ppx, ppy; int k; } tr[TRAVELLERS];
    static bool tr_init;
    if (!tr_init) {
      tr_init = true;
      for (int i = 0; i < TRAVELLERS; i++) { int x = 1 + (i * 5) % (MZ_W - 2), y = row + 4 + i * 2; tr[i] = (typeof(tr[0])){ x, y, x, y, x, y, -1 }; }
    }
    int n = 0;
    t3_quat flat = about(1, 0, 0, -PI / 2);
    for (int i = 0; i < TRAVELLERS; i++) {
      float step = beat + (float)i * 0.31f;
      int k = (int)floorf(step);
      if (k != tr[i].k) {
        tr[i].k = k;
        tr[i].ppx = tr[i].px, tr[i].ppy = tr[i].py, tr[i].px = tr[i].cx, tr[i].py = tr[i].cy;
        int cx = tr[i].cx, cy = tr[i].cy, yy = ((cy % MZ_H) + MZ_H) % MZ_H;
        int opts[4][2], no = 0;
        if (cx + 1 < MZ_W && !(mz[yy][cx + 1] & 2)) opts[no][0] = cx + 1, opts[no++][1] = cy;
        if (cx > 0 && !(mz[yy][cx] & 2)) opts[no][0] = cx - 1, opts[no++][1] = cy;
        if (!(mz[yy][cx] & 1)) opts[no][0] = cx, opts[no++][1] = cy - 1;
        if (!(mz[(yy + 1) % MZ_H][cx] & 1)) opts[no][0] = cx, opts[no++][1] = cy + 1;
        int fwd[4][2], nf = 0;
        for (int o = 0; o < no; o++) if (opts[o][0] != tr[i].ppx || opts[o][1] != tr[i].ppy) fwd[nf][0] = opts[o][0], fwd[nf++][1] = opts[o][1];
        if (nf) { int p = (int)(hsh((uint32_t)k, (uint32_t)i + 99) % (uint32_t)nf); tr[i].cx = fwd[p][0], tr[i].cy = fwd[p][1]; }
        else if (no) tr[i].cx = opts[0][0], tr[i].cy = opts[0][1];
        /* kept on screen: one fallen behind the near rows is carried to the far ones */
        float rr = (float)tr[i].cy - (float)row;
        if (rr < 2 || rr > MZ_SHOW - 6) { int y = row + 10 + (int)(hsh((uint32_t)i, (uint32_t)row) % 14u); tr[i].cy = tr[i].py = tr[i].ppy = y, tr[i].px = tr[i].ppx = tr[i].cx; }
      }
      float u = step - floorf(step);
      u = u * u * (3 - 2 * u);
      /* the head between the last cell and the next; the tail back through the cell before */
      float hx = mz_x(tr[i].px) + (mz_x(tr[i].cx) - mz_x(tr[i].px)) * u, hz = mz_z(tr[i].py - row) + (mz_z(tr[i].cy - row) - mz_z(tr[i].py - row)) * u;
      float pts[3][2] = { { hx, hz }, { mz_x(tr[i].px), mz_z(tr[i].py - row) }, { mz_x(tr[i].ppx), mz_z(tr[i].ppy - row) } };
      float fade = 1;
      for (int sgi = 0; sgi < 2; sgi++) {
        float ax = pts[sgi][0], az = pts[sgi][1], bx = pts[sgi + 1][0], bz = pts[sgi + 1][1];
        float dx = bx - ax, dz = bz - az, len = sqrtf(dx * dx + dz * dz);
        if (len < 0.01f) continue;
        float b = (sgi == 0 ? 0.7f : 0.32f) * fade * (0.8f + 0.2f * energy);
        /* lying on the floor along the hall, its head (v 0) at a */
        t3_quat q = qmul(about(0, 1, 0, atan2f(-dx, -dz)), flat);
        place(B.trails, n++, (t3_vec3){ (ax + bx) * 0.5f, 0.05f, (az + bz) * 0.5f }, q, (t3_vec3){ 1.3f, len, 1 }, (t3_color){ b * 0.55f, b * 0.5f, b * 0.85f });
        fade *= 0.5f;
      }
    }
    B.trails->count = n;
    done(B.trails);
  } else if (B.trails->count) B.trails->count = 0, done(B.trails);
}

/* ── the backdrops ───────────────────────────────────────────────────── */

void backdrop_build(cw_node level) {
  if (B.built) return;
  B.built = true;
  for (int i = 1; i < BD_COUNT; i++) {
    B.node[i] = cw_node_get(level, NODES[i]);
    if (!B.node[i].id) { cw_error("main.cwscene: Level/%s (Backdrop) is missing", NODES[i]); continue; }
    B.root[i] = cw_backdrop_root(B.node[i]);
    if (!B.root[i].id) cw_error("backdrop %s: its scene did not load", NAMES[i]);
    cw_backdrop_set_opacity(B.node[i], 0);
    if (B.root[i].id) cw_node_set(B.root[i], "visible", cw_val_bool(false));
    B.op_set[i] = 0;
  }
  cw_debug_command("bdset", "bdset <backdrop> <node path> <property> <value>: set a property in a backdrop's world (looking at its cost and its parts)", cmd_bdset);
  cw_debug_command("maze", "maze old 1|0: the arcade maze's old bright critters (the readability gate's note-likeness control)", cmd_maze);
  cw_debug_command("bdcam", "bdcam <backdrop> [x y z pitch yaw]: a backdrop's camera, read or moved (a closer look at its world)", cmd_bdcam);
  if (B.root[BD_FIRE].id) build_fire();
  if (B.root[BD_SEA].id) build_sea();
  if (B.root[BD_GRID].id) build_grid();
  if (B.root[BD_LASERS].id) build_lasers();
  if (B.root[BD_STAGE].id) build_stage();
  if (B.root[BD_CLEAN].id) build_clean();
  if (B.root[BD_MAZE].id) build_maze();
  if (B.root[BD_AURORA].id) world_aurora_build(B.root[BD_AURORA]);
  if (B.root[BD_SPACE].id) world_space_build(B.root[BD_SPACE]);
  if (B.root[BD_CITY].id) world_city_build(B.root[BD_CITY]);
  if (B.root[BD_FOREST].id) world_forest_build(B.root[BD_FOREST]);
  if (B.root[BD_DEEP].id) world_deep_build(B.root[BD_DEEP]);
  if (B.root[BD_DUNES].id) world_dunes_build(B.root[BD_DUNES]);
  if (B.root[BD_LAVA].id) world_lava_build(B.root[BD_LAVA]);
  if (B.root[BD_METAL].id) world_metal_build(B.root[BD_METAL]);
}

int backdrop_drawn(void) { return B.drawn; }

/* `bdcam <name> x y z pitch yaw`: move a backdrop's camera (a closer look at its world); `bdcam <name>` reads it */
static void cmd_bdcam(int argc, const char **argv, cw_reply *r) {
  int i = 1;
  while (i < BD_COUNT && (argc < 2 || strcmp(argv[1], NAMES[i]))) i++;
  if (i >= BD_COUNT || !B.root[i].id) { cw_reply_error(r, "bdcam <backdrop> [x y z pitch yaw]"); return; }
  cw_node cam = cw_node_get(B.root[i], "Camera");
  if (argc >= 7) {
    cw_node3d_set_position(cam, (t3_vec3){ (float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4]) });
    cw_node3d_set_rotation(cam, (t3_vec3){ (float)atof(argv[5]), (float)atof(argv[6]), 0 });
  }
  t3_vec3 p = cw_node3d_position(cam);
  cw_reply_printf(r, "{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}", p.x, p.y, p.z);
}

void backdrop_update(const float weight[BD_COUNT], int top, const song *s, float beat, float t, float energy) {
  if (!B.built) return;
  /* the one coming in over the one going out at full: a straight cross-fade */
  int lit[2], n = 0;
  for (int i = 1; i < BD_COUNT; i++)
    if (weight[i] > 0.0005f && n < 2) lit[n++] = i;
  float op[BD_COUNT] = { 0 };
  if (n == 1) op[lit[0]] = fminf(1, weight[lit[0]]);
  else if (n == 2) {
    int up = lit[0] == top ? lit[0] : lit[1], under = up == lit[0] ? lit[1] : lit[0];
    op[under] = 1, op[up] = fminf(1, weight[up]);
    if (up != B.order_top) {
      cw_backdrop_set_order(B.node[under], 0);
      cw_backdrop_set_order(B.node[up], 1);
      B.order_top = up;
    }
  }
  B.drawn = 0;
  for (int i = 1; i < BD_COUNT; i++) {
    if (!B.node[i].id) continue;
    if (op[i] != B.op_set[i]) {
      /* (no restart of a world's particles when it comes back: a hidden world pauses, so a fire comes back burning where it
       * left off; a restart simulated each system's preprocess seconds in one frame, a hitch of 85 to 350 ms) */
      cw_backdrop_set_opacity(B.node[i], op[i]);
      /* a world out of sight is hidden as well (cartwheel-engine 0.64.1): opacity 0 already draws nothing, hidden it does
       * not tick either (its particles pause, its water and lights stop); shown again the moment it fades in */
      if ((op[i] > 0) != (B.op_set[i] > 0)) cw_node_set(B.root[i], "visible", cw_val_bool(op[i] > 0));
      B.op_set[i] = op[i];
    }
    if (op[i] <= 0) continue;
    B.drawn++;
    switch (i) {
    case BD_FIRE: update_fire(beat, t, energy); break;
    case BD_SEA: break;
    case BD_GRID: update_grid(beat, energy); break;
    case BD_LASERS: update_lasers(beat, energy); break;
    case BD_STAGE: update_stage(beat, t, energy); break;
    case BD_CLEAN: update_clean(s, beat); break;
    case BD_MAZE: update_maze(beat, energy); break;
    case BD_AURORA: world_aurora_update(beat, t, energy); break;
    case BD_SPACE: world_space_update(beat, t, energy); break;
    case BD_CITY: world_city_update(beat, t, energy); break;
    case BD_FOREST: world_forest_update(beat, t, energy); break;
    case BD_DEEP: world_deep_update(beat, t, energy); break;
    case BD_DUNES: world_dunes_update(beat, t, energy); break;
    case BD_LAVA: world_lava_update(beat, t, energy); break;
    case BD_METAL: world_metal_update(beat, t, energy); break;
    }
  }
}

static void cmd_maze(int argc, const char **argv, cw_reply *r) {
  if (argc >= 3 && !strcmp(argv[1], "old")) B.maze_old = atoi(argv[2]) != 0;
  cw_reply_printf(r, "{\"old\":%d}", B.maze_old);
}

/* `bdset <backdrop> <node> <prop> <value>`: a property of a node in a backdrop's world, set (its text as the scene file writes it) */
static void cmd_bdset(int argc, const char **argv, cw_reply *r) {
  int i = 1;
  while (i < BD_COUNT && (argc < 2 || strcmp(argv[1], NAMES[i]))) i++;
  if (i < BD_COUNT && B.root[i].id && argc == 4) { /* `bdset <backdrop> <node> <prop>`: read it */
    cw_node n = cw_node_get(B.root[i], argv[2]);
    cw_value v;
    if (n.id && cw_node_get_prop(n, argv[3], &v)) { char t[128]; cw_value_format(&v, t, sizeof t); cw_reply_printf(r, "{\"value\":\"%s\"}", t); return; }
  }
  if (i >= BD_COUNT || !B.root[i].id || argc < 5) { cw_reply_error(r, "bdset <backdrop> <node path> <property> [value]"); return; }
  cw_node n = cw_node_get(B.root[i], argv[2]);
  const cw_prop *p = n.id ? cw_node_find_prop(n, argv[3], NULL) : NULL;
  cw_value v;
  if (!p || !cw_value_parse(argv[4], p->type, &v)) { cw_reply_error(r, "bdset: no such node or property, or a value it cannot take"); return; }
  cw_node_set(n, argv[3], v);
  cw_reply_printf(r, "{\"ok\":1}");
}
