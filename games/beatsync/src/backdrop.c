/* The backdrops (backdrop.h): scenes behind the play field, built in the tree under Level/Backdrop and faded by a
 * weight each.
 *
 * The camera looks down -Z from z 22 with a 40 degree field of view (main.cwscene), so a backdrop at depth z fills
 * a screen 0.364 (22 - z) high either side of the middle, and a floor seen from the camera's height runs to a horizon
 * across the middle of the screen. Everything here is behind the field (z < -1): the field's rivers, notes and land
 * are drawn over it, and a style showing a backdrop lays a dark road under each river (club.c), which writes depth, so
 * nothing of a backdrop shows inside a road.
 *
 * Fading: everything a backdrop draws for itself is light that adds (additive, its colour times the weight), the
 * engine's nodes take the weight in their own terms (a particle system's energy, a light's energy, a fog volume's
 * density, the water's colours), and a backdrop at weight 0 is hidden. Nothing here switches on or off while it shows,
 * and nothing blinks: every motion is a slow sweep or a drift on the beat's clock. */
#include "backdrop.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/particles.h"
#include "cartwheel/scene.h"

#define PI 3.14159265f

static struct {
  bool built;
  cw_node root, node[BD_COUNT];
  bool shown[BD_COUNT];
  float w_set[BD_COUNT];
  float w[BD_COUNT];
  /* fire */
  cw_node flames, core, embers, haze;
  t3_mesh *fire_wall, *fire_floor;
  /* water */
  cw_node water, moonlight;
  t3_mesh *moon, *moon_halo, *sea_sky;
  t3_instanced_mesh *harbour, *stars;
  /* grid */
  t3_instanced_mesh *grid_long, *grid_cross;
  t3_mesh *sun, *grid_sky, *grid_floor, *horizon;
  /* lasers */
  t3_instanced_mesh *rays, *heads;
  t3_mesh *laser_glow;
  cw_node spot[3], haze_vol;
} B;

static const char *const NAMES[BD_COUNT] = { "none", "fire", "water", "grid", "lasers" };
const char *backdrop_name(int bd) { return bd >= 0 && bd < BD_COUNT ? NAMES[bd] : "?"; }

/* ── building blocks ─────────────────────────────────────────────────── */

static cw_node add(cw_node parent, const char *type, const char *name) {
  cw_node n = cw_node_new(type, name);
  cw_node_add_child(parent, n);
  return n;
}
static void setf(cw_node n, const char *p, double v) { cw_node_set(n, p, cw_val_float(v)); }
static void setb(cw_node n, const char *p, bool v) { cw_node_set(n, p, cw_val_bool(v)); }
static void seti(cw_node n, const char *p, int64_t v) { cw_node_set(n, p, cw_val_int(v)); }
static void setv(cw_node n, const char *p, float x, float y, float z) { cw_node_set(n, p, cw_val_vec3(x, y, z)); }
static void setc(cw_node n, const char *p, float r, float g, float b) { cw_node_set(n, p, cw_val_color(r, g, b, 1)); }
static void sete(cw_node n, const char *p, const char *e) { cw_node_set(n, p, cw_val_enum(e)); }
static void sets(cw_node n, const char *p, const char *s) { cw_node_set(n, p, cw_val_string(s)); }

/* a texture from a function of (u, v), v 0 at the top: white light whose brightness is the function, alpha 1 */
typedef float (*tex_fn)(float u, float v);
static t3_texture *texture(int w, int h, tex_fn f) {
  uint8_t *px = malloc((size_t)w * h * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float k = f(((float)x + 0.5f) / (float)w, ((float)y + 0.5f) / (float)h);
      uint8_t c = (uint8_t)lrintf(fminf(1, fmaxf(0, k)) * 255.0f);
      uint8_t *p = px + ((size_t)y * w + x) * 4;
      p[0] = p[1] = p[2] = c, p[3] = 255;
    }
  t3_texture *t = t3_texture_new(w, h, px); /* flipY: row 0 is the top of the quad */
  free(px);
  return t;
}
/* a texture of colour (r, g, b from a function) */
typedef void (*tex_rgb_fn)(float u, float v, float out[3]);
static t3_texture *texture_rgb(int w, int h, tex_rgb_fn f) {
  uint8_t *px = malloc((size_t)w * h * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float c[3];
      f(((float)x + 0.5f) / (float)w, ((float)y + 0.5f) / (float)h, c);
      uint8_t *p = px + ((size_t)y * w + x) * 4;
      for (int k = 0; k < 3; k++) p[k] = (uint8_t)lrintf(fminf(1, fmaxf(0, c[k])) * 255.0f);
      p[3] = 255;
    }
  t3_texture *t = t3_texture_new(w, h, px);
  free(px);
  return t;
}

/* additive light: its colour is set every frame (times the weight) */
static t3_material *light_mat(t3_texture *tex) {
  t3_material *m = t3_mesh_basic_material_new(0x000000);
  m->blending = T3_ADDITIVE_BLENDING, m->transparent = true, m->depth_write = false;
  if (tex) t3_material_set_map(m, tex), t3_release(tex);
  return m;
}
static t3_mesh *quad(cw_node at, float w, float h, t3_texture *tex, t3_vec3 pos) {
  t3_geometry *g = t3_plane_geometry_new(w, h, 1, 1);
  t3_material *m = light_mat(tex);
  t3_mesh *q = t3_mesh_new(g, m);
  t3_release(g), t3_release(m);
  T3_OBJ(q)->position = pos;
  T3_OBJ(q)->frustum_culled = false;
  t3_object_add(cw_node3d_object(at), q);
  t3_release(q);
  return q;
}
static void tint(t3_mesh *q, float r, float g, float b, float k) {
  t3_material *m = q->materials[0];
  m->color = (t3_color){ r * k, g * k, b * k };
}
static t3_instanced_mesh *many(cw_node at, t3_geometry *g, t3_texture *tex, int n) {
  t3_material *m = light_mat(tex);
  m->color = (t3_color){ 1, 1, 1 };
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  t3_release(g), t3_release(m);
  T3_OBJ(im)->frustum_culled = false;
  t3_object_add(cw_node3d_object(at), im);
  t3_release(im);
  return im;
}
static void place(t3_instanced_mesh *im, int i, t3_vec3 p, t3_quat q, t3_vec3 s, t3_color c) {
  t3_mat4 m;
  t3_mat4_compose(&m, p, q, s);
  t3_instanced_mesh_set_matrix_at(im, i, &m);
  t3_instanced_mesh_set_color_at(im, i, c);
}
static void done(t3_instanced_mesh *im) {
  im->instance_matrix->version++;
  if (im->instance_color) im->instance_color->version++;
}
static t3_quat about(float x, float y, float z, float a) { return t3_quat_from_axis_angle((t3_vec3){ x, y, z }, a); }
static uint32_t hsh(uint32_t a, uint32_t b) {
  uint32_t h = a * 374761393u + b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float rnd(uint32_t a, uint32_t b) { return (float)(hsh(a, b) & 0xffffff) / 16777216.0f; }
static float smooth(float e0, float e1, float x) {
  float t = fminf(1, fmaxf(0, (x - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
}

/* the textures */
static float tx_up(float u, float v) { (void)u; return powf(v, 2.2f); }                      /* bright at the bottom */
static float tx_horizon(float u, float v) { (void)u; return expf(-fabsf(v - 0.5f) * 9.0f); } /* a band across the middle */
static float tx_soft_line(float u, float v) {                                                /* a beam: soft across, fading along */
  float a = expf(-powf((u - 0.5f) * 5.0f, 2)), core = expf(-powf((u - 0.5f) * 16.0f, 2));
  return (0.45f * a + 0.75f * core) * powf(1 - v, 0.6f) * smooth(0, 0.04f, v);
}
static float tx_dot(float u, float v) {
  float d = sqrtf((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2;
  return expf(-d * d * 5.0f);
}
static float tx_disc(float u, float v) { /* a moon: a disc with a soft limb and a little texture of maria */
  float x = (u - 0.5f) * 2, y = (v - 0.5f) * 2, d = sqrtf(x * x + y * y);
  float edge = 1 - smooth(0.92f, 1.0f, d);
  float maria = 0.86f + 0.14f * sinf(x * 7.1f + 1.3f) * sinf(y * 5.3f - 0.4f) - 0.1f * expf(-((x + 0.3f) * (x + 0.3f) + (y + 0.2f) * (y + 0.2f)) * 9);
  return edge * maria * (0.8f + 0.2f * sqrtf(fmaxf(0, 1 - d * d)));
}
static float tx_halo(float u, float v) {
  float d = sqrtf((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2;
  return d >= 1 ? 0 : powf(1 - d, 3.0f);
}
/* the sun of the grid: yellow at the top to magenta at the bottom, cut by bands that widen toward its foot */
static void tx_sun(float u, float v, float c[3]) {
  float x = (u - 0.5f) * 2, y = (v - 0.5f) * 2, d = sqrtf(x * x + y * y);
  float disc = 1 - smooth(0.97f, 1.0f, d);
  float t = v; /* 0 top, 1 bottom */
  float r = 1.0f, g = 0.85f - 0.75f * t, b = 0.2f + 0.45f * t;
  float cut = 1;
  if (t > 0.5f) { /* the bands: gaps that grow toward the bottom */
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

/* ── fire ────────────────────────────────────────────────────────────── */

static void build_fire(cw_node at) {
  /* the wall of heat behind the flames, red rising from the bottom */
  B.fire_wall = quad(at, 220, 80, texture(4, 128, tx_up), (t3_vec3){ 0, -6, -60 });
  B.fire_floor = quad(at, 220, 22, texture(4, 128, tx_horizon), (t3_vec3){ 0, -22, -40 });
  /* the flames: big flipbook billboards (the engine's own baked fire, CC0) rising from a long bed at the foot of the
   * screen, swaying in a slow curl of wind */
  B.flames = add(at, "GPUParticles3D", "Flames");
  setv(B.flames, "position", 0, -19, -26);
  seti(B.flames, "amount", 340);
  setf(B.flames, "lifetime", 2.8);
  setf(B.flames, "lifetime_random", 0.3);
  sete(B.flames, "emission_shape", "BOX");
  setv(B.flames, "emission_extents", 44, 0.6, 4);
  setv(B.flames, "direction", 0, 1, 0);
  setf(B.flames, "spread", 10);
  setf(B.flames, "speed_min", 4);
  setf(B.flames, "speed_max", 7.5);
  setv(B.flames, "gravity", 0, 2.6, 0);
  setf(B.flames, "damping", 0.4);
  setf(B.flames, "turbulence", 1.2);
  setf(B.flames, "turbulence_scale", 0.08);
  setf(B.flames, "turbulence_speed", 0.15);
  setf(B.flames, "size", 11);
  setf(B.flames, "size_random", 0.4);
  sets(B.flames, "size_curve", "0 0.5, 0.3 1, 1 0.7");
  sets(B.flames, "color_ramp", "0 1 0.75 0.4 0, 0.1 1 0.6 0.25 1, 0.6 0.9 0.3 0.1 0.8, 1 0.4 0.08 0.04 0");
  sets(B.flames, "texture", "fx/fire.png");
  sets(B.flames, "motion_texture", "fx/fire_mv.png");
  seti(B.flames, "hframes", 8);
  seti(B.flames, "vframes", 8);
  setf(B.flames, "spin", 0.15);
  setf(B.flames, "preprocess", 3);
  seti(B.flames, "seed", 71);
  /* the hot core: smaller, brighter, low */
  B.core = add(at, "GPUParticles3D", "Core");
  setv(B.core, "position", 0, -17, -20);
  seti(B.core, "amount", 140);
  setf(B.core, "lifetime", 1.3);
  sete(B.core, "emission_shape", "BOX");
  setv(B.core, "emission_extents", 36, 0.4, 2);
  setv(B.core, "direction", 0, 1, 0);
  setf(B.core, "spread", 8);
  setf(B.core, "speed_min", 2);
  setf(B.core, "speed_max", 3.5);
  setv(B.core, "gravity", 0, 2.5, 0);
  setf(B.core, "turbulence", 0.8);
  setf(B.core, "turbulence_scale", 0.12);
  setf(B.core, "size", 5);
  setf(B.core, "size_random", 0.3);
  sets(B.core, "size_curve", "0 0.6, 0.4 1, 1 0.5");
  sets(B.core, "color_ramp", "0 1 0.9 0.6 0, 0.12 1 0.8 0.45 1, 1 1 0.4 0.12 0");
  sets(B.core, "texture", "fx/fire.png");
  sets(B.core, "motion_texture", "fx/fire_mv.png");
  seti(B.core, "hframes", 8);
  seti(B.core, "vframes", 8);
  setf(B.core, "preprocess", 2);
  seti(B.core, "seed", 72);
  /* embers: sparks rising on the heat, stretched along their flight, drifting in the curl */
  B.embers = add(at, "GPUParticles3D", "Embers");
  setv(B.embers, "position", 0, -14, -16);
  seti(B.embers, "amount", 220);
  setf(B.embers, "lifetime", 4.0);
  setf(B.embers, "lifetime_random", 0.5);
  sete(B.embers, "emission_shape", "BOX");
  setv(B.embers, "emission_extents", 34, 2, 8);
  setv(B.embers, "direction", 0, 1, 0);
  setf(B.embers, "spread", 25);
  setf(B.embers, "speed_min", 2.5);
  setf(B.embers, "speed_max", 6);
  setv(B.embers, "gravity", 0, 0.6, 0);
  setf(B.embers, "turbulence", 2.5);
  setf(B.embers, "turbulence_scale", 0.15);
  setf(B.embers, "turbulence_speed", 0.3);
  sete(B.embers, "draw_mode", "STRETCHED");
  setf(B.embers, "stretch", 0.06);
  setf(B.embers, "size", 0.16);
  setf(B.embers, "size_random", 0.5);
  sets(B.embers, "color_ramp", "0 1 0.8 0.4 0, 0.1 1 0.6 0.2 1, 0.7 1 0.35 0.08 0.8, 1 0.6 0.1 0.02 0");
  setf(B.embers, "preprocess", 4);
  seti(B.embers, "seed", 73);
  /* the heat haze over the flames: big slow patches that shift the picture behind them a little */
  B.haze = add(at, "GPUParticles3D", "Haze");
  setv(B.haze, "position", 0, -12, -18);
  seti(B.haze, "amount", 48);
  setf(B.haze, "lifetime", 3);
  sete(B.haze, "emission_shape", "BOX");
  setv(B.haze, "emission_extents", 34, 1, 2);
  setv(B.haze, "direction", 0, 1, 0);
  setf(B.haze, "spread", 10);
  setf(B.haze, "speed_min", 1.5);
  setf(B.haze, "speed_max", 2.5);
  setv(B.haze, "gravity", 0, 0, 0);
  setf(B.haze, "turbulence", 0.6);
  setf(B.haze, "turbulence_scale", 0.2);
  sete(B.haze, "blend", "DISTORT");
  setf(B.haze, "size", 9);
  sets(B.haze, "size_curve", "0 0.6, 1 1.4");
  sets(B.haze, "color_ramp", "0 1 1 1 0, 0.3 1 1 1 1, 1 1 1 1 0");
  setf(B.haze, "preprocess", 3);
  seti(B.haze, "seed", 74);
}

static void update_fire(float w, float beat, float energy) {
  (void)beat;
  float e = 0.85f + 0.3f * energy; /* the fire breathes with the music's loudness (slowly: energy is smoothed) */
  tint(B.fire_wall, 0.9f, 0.22f, 0.04f, 0.8f * w * e);
  tint(B.fire_floor, 1.0f, 0.35f, 0.08f, 0.35f * w * e);
  setf(B.flames, "energy", 1.25 * w * e);
  setf(B.core, "energy", 1.1 * w * e);
  setf(B.embers, "energy", 3.0 * w);
  setf(B.haze, "distort_strength", 0.07 * w);
}

/* ── water ───────────────────────────────────────────────────────────── */

#define HARBOUR 40
#define STARS 160
static const t3_vec3 MOON = { -70, 34, -300 };

static void build_water(cw_node at) {
  B.sea_sky = quad(at, 1200, 300, texture_rgb(4, 128, tx_sky_grid), (t3_vec3){ 0, 90, -420 });
  B.moon_halo = quad(at, 90, 90, texture(128, 128, tx_halo), (t3_vec3){ MOON.x, MOON.y, MOON.z - 1 });
  B.moon = quad(at, 18, 18, texture(128, 128, tx_disc), MOON);
  /* the stars: fixed, faint, never twinkling */
  B.stars = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(16, 16, tx_dot), STARS);
  /* the harbour: lights along the horizon, which the swell draws out into streaks */
  B.harbour = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(16, 16, tx_dot), HARBOUR);
  B.water = add(at, "Water3D", "Sea");
  setv(B.water, "position", 0, -6.5f, -160);
  cw_node_set(B.water, "size", cw_val_vec2(500, 320));
  setf(B.water, "horizon_distance", 1400);
  seti(B.water, "subdivisions", 200);
  cw_node_set(B.water, "wave0", (cw_value){ .type = CW_T_VEC4, .v = { 80, 22, 0.55f, 0.4f } });
  cw_node_set(B.water, "wave1", (cw_value){ .type = CW_T_VEC4, .v = { 110, 9, 0.22f, 0.5f } });
  cw_node_set(B.water, "wave2", (cw_value){ .type = CW_T_VEC4, .v = { 60, 4, 0.08f, 0.4f } });
  setf(B.water, "wave_speed", 0.6);
  setb(B.water, "refraction", false);
  seti(B.water, "floor_map_size", 0);
  seti(B.water, "ripple_cells", 0);
  setf(B.water, "caustics", 0);
  setb(B.water, "underwater", false);
  setf(B.water, "shore_foam", 0);
  setf(B.water, "crest_foam", 0);
  setf(B.water, "scatter", 0);
  setf(B.water, "reflectivity", 0.12);
  setf(B.water, "distortion", 0.06);
  setf(B.water, "roughness", 0.08);
  setf(B.water, "glint", 1.4);
  /* the moon's light: low, from behind the sea toward the camera, so its path runs down the water to the field */
  B.moonlight = add(at, "DirectionalLight3D", "Moonlight");
  t3_vec3 d = { -MOON.x, -MOON.y, 22 - MOON.z }; /* from the moon toward the camera */
  float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
  d.x /= len, d.y /= len, d.z /= len;
  /* a node shines along its -Z: yaw then pitch to point -Z along d */
  float yaw = atan2f(-d.x, -d.z), pitch = asinf(d.y);
  cw_node3d_set_rotation(B.moonlight, (t3_vec3){ pitch, yaw, 0 });
  setc(B.moonlight, "color", 0.75f, 0.85f, 1.0f);
  setf(B.moonlight, "fog_energy", 0);
}

static void update_water(float w, float beat) {
  (void)beat;
  tint(B.sea_sky, 0.25f, 0.5f, 1.0f, 0.5f * w); /* a deep blue night over the horizon */
  tint(B.moon, 1.0f, 0.97f, 0.9f, 1.6f * w);
  tint(B.moon_halo, 0.6f, 0.7f, 1.0f, 0.35f * w);
  setf(B.moonlight, "energy", 3.0 * w);
  setc(B.water, "color", 0.1f * w, 0.25f * w, 0.4f * w);
  setc(B.water, "deep_color", 0.004f * w, 0.02f * w, 0.05f * w);
  for (int i = 0; i < STARS; i++) {
    float x = -500 + 1000 * rnd(11, (uint32_t)i), y = 20 + 200 * powf(rnd(12, (uint32_t)i), 0.7f), z = -400;
    float s = 0.8f + 1.4f * rnd(13, (uint32_t)i), b = (0.15f + 0.5f * rnd(14, (uint32_t)i)) * w;
    place(B.stars, i, (t3_vec3){ x, y, z }, about(0, 0, 1, 0), (t3_vec3){ s, s, 1 }, (t3_color){ b * 0.8f, b * 0.9f, b });
  }
  done(B.stars);
  static const float COLS[4][3] = { { 1, 0.55f, 0.2f }, { 1, 0.3f, 0.6f }, { 0.3f, 0.8f, 1 }, { 1, 0.85f, 0.5f } };
  for (int i = 0; i < HARBOUR; i++) {
    float x = -380 + 760 * ((float)i + rnd(21, (uint32_t)i)) / HARBOUR, z = -560;
    if (fabsf(x - MOON.x * 1.9f) < 50) x += 100; /* keep the moon's path clear */
    float y = -5.8f + 3.0f * rnd(22, (uint32_t)i), s = 2.2f + 2.0f * rnd(23, (uint32_t)i);
    const float *c = COLS[hsh(24, (uint32_t)i) % 4];
    float b = (0.9f + 0.6f * rnd(25, (uint32_t)i)) * w;
    place(B.harbour, i, (t3_vec3){ x, y, z }, about(0, 0, 1, 0), (t3_vec3){ s, s, 1 }, (t3_color){ c[0] * b, c[1] * b, c[2] * b });
  }
  done(B.harbour);
}

/* ── grid ────────────────────────────────────────────────────────────── */

#define GRID_LONG 61
#define GRID_CROSS 64
#define GRID_Y (-7.0f)
#define GRID_STEP 6.0f
static void build_grid(cw_node at) {
  B.grid_sky = quad(at, 1600, 400, texture_rgb(4, 256, tx_sky_grid), (t3_vec3){ 0, 180 + GRID_Y, -520 });
  B.sun = quad(at, 150, 150, texture_rgb(256, 256, tx_sun), (t3_vec3){ 0, 48 + GRID_Y, -500 });
  T3_OBJ(B.sun)->render_order = -3;
  /* the floor under the grid: dark violet glass over everything behind it (it hides the sun's foot below the horizon) */
  {
    t3_geometry *g = t3_plane_geometry_new(1600, 700, 1, 1);
    t3_material *m = t3_mesh_basic_material_new(0x000000);
    m->transparent = true, m->depth_write = false, m->opacity = 0;
    m->color = (t3_color){ 0.035f, 0.0f, 0.06f };
    B.grid_floor = t3_mesh_new(g, m);
    t3_release(g), t3_release(m);
    T3_OBJ(B.grid_floor)->position = (t3_vec3){ 0, GRID_Y - 0.05f, -340 };
    T3_OBJ(B.grid_floor)->quaternion = about(1, 0, 0, -PI / 2);
    T3_OBJ(B.grid_floor)->render_order = -2;
    T3_OBJ(B.grid_floor)->frustum_culled = false;
    t3_object_add(cw_node3d_object(at), B.grid_floor);
    t3_release(B.grid_floor);
  }
  B.horizon = quad(at, 1600, 16, texture(4, 64, tx_horizon), (t3_vec3){ 0, GRID_Y + 0.2f, -680 });
  B.grid_long = many(at, t3_plane_geometry_new(1, 1, 1, 1), NULL, GRID_LONG);
  B.grid_cross = many(at, t3_plane_geometry_new(1, 1, 1, 1), NULL, GRID_CROSS);
}

static void update_grid(float w, float beat) {
  tint(B.grid_sky, 1, 1, 1, 0.9f * w);
  tint(B.sun, 1, 1, 1, 1.25f * w);
  B.grid_floor->materials[0]->opacity = 0.92f * w, B.grid_floor->materials[0]->version++;
  tint(B.horizon, 1.0f, 0.3f, 0.8f, 0.7f * w);
  t3_quat flat = about(1, 0, 0, -PI / 2);
  const float far = 680;
  for (int i = 0; i < GRID_LONG; i++) { /* lines running away to the horizon */
    float x = ((float)i - (GRID_LONG - 1) * 0.5f) * GRID_STEP;
    float b = w * 0.55f * (0.6f + 0.4f * expf(-fabsf(x) / 60));
    place(B.grid_long, i, (t3_vec3){ x, GRID_Y, -far * 0.5f }, flat, (t3_vec3){ 0.16f, far, 1 }, (t3_color){ b, 0.12f * b, 0.9f * b });
  }
  done(B.grid_long);
  /* lines across, riding toward the camera one step a beat; each fades in far off (into the horizon's glow) and
   * gets thinner on screen with distance on its own, so nothing pops or shimmers */
  float off = fmodf(beat, 1.0f) * GRID_STEP;
  for (int i = 0; i < GRID_CROSS; i++) {
    float z = -2 - (float)i * GRID_STEP * powf(1.035f, (float)i) + off * powf(1.035f, (float)i);
    float dist = 22 - z, fade = expf(-dist / 260.0f) * smooth(far, far * 0.6f, -z);
    float b = w * 0.75f * fade, th = 0.14f + 0.004f * dist;
    place(B.grid_cross, i, (t3_vec3){ 0, GRID_Y, z }, flat, (t3_vec3){ 1600, th, 1 }, (t3_color){ b * 0.25f, 0.85f * b, b });
  }
  done(B.grid_cross);
}

/* ── lasers ──────────────────────────────────────────────────────────── */

#define FANS 3
#define FAN_RAYS 7
#define LASER_Z (-38.0f)
static const float FAN_X[FANS] = { -34, 0, 34 };
static const float FAN_COL[FANS][3] = { { 0.2f, 1.0f, 0.35f }, { 1.0f, 0.2f, 0.75f }, { 0.25f, 0.75f, 1.0f } };

static void build_lasers(cw_node at) {
  B.laser_glow = quad(at, 400, 80, texture(4, 128, tx_horizon), (t3_vec3){ 0, 14, LASER_Z - 2 });
  B.rays = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(32, 128, tx_soft_line), FANS * FAN_RAYS);
  B.heads = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(32, 32, tx_dot), FANS);
  /* haze behind the field for the spot lights' beams (volumetric fog: only lights with fog_energy show in it) */
  B.haze_vol = add(at, "FogVolume3D", "LaserHaze");
  setv(B.haze_vol, "position", 0, 5, -40);
  setv(B.haze_vol, "size", 170, 90, 50);
  setc(B.haze_vol, "albedo", 1, 1, 1);
  setf(B.haze_vol, "edge_fade", 0.2);
  setf(B.haze_vol, "noise", 0.5);
  setf(B.haze_vol, "noise_scale", 0.05);
  setv(B.haze_vol, "noise_wind", 0.6f, 0.2f, 0);
  for (int k = 0; k < FANS; k++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Spot%d", k);
    B.spot[k] = add(at, "SpotLight3D", nm);
    setv(B.spot[k], "position", FAN_X[k], 20, LASER_Z);
    setc(B.spot[k], "color", FAN_COL[k][0], FAN_COL[k][1], FAN_COL[k][2]);
    setf(B.spot[k], "angle", 0.05); /* radians: a narrow beam */
    setf(B.spot[k], "penumbra", 0.3);
    setf(B.spot[k], "range", 120);
    setf(B.spot[k], "decay", 0);
  }
}

static void update_lasers(float w, float beat) {
  tint(B.laser_glow, 0.5f, 0.2f, 0.9f, 0.12f * w);
  for (int k = 0; k < FANS; k++) {
    /* each fan sweeps slowly, a whole sway over four bars, the fans out of step with each other */
    float sway = 0.38f * sinf(2 * PI * beat / 16.0f + (float)k * 2.1f);
    float open = 0.55f + 0.25f * sinf(2 * PI * beat / 32.0f + (float)k);
    t3_vec3 o = { FAN_X[k], 13, LASER_Z };
    for (int r = 0; r < FAN_RAYS; r++) {
      float a = PI + sway + open * (((float)r / (FAN_RAYS - 1)) - 0.5f) * 2; /* pointing down, fanned */
      float len = 70;
      /* the quad's top (v 0) at the head: its centre half a length along the ray */
      t3_vec3 c = { o.x - sinf(a) * len * 0.5f, o.y + cosf(a) * len * 0.5f, o.z };
      float b = w * 1.4f;
      place(B.rays, k * FAN_RAYS + r, c, about(0, 0, 1, a), (t3_vec3){ 1.4f, len, 1 },
            (t3_color){ FAN_COL[k][0] * b, FAN_COL[k][1] * b, FAN_COL[k][2] * b });
    }
    float hb = w * 1.6f;
    place(B.heads, k, o, about(0, 0, 1, 0), (t3_vec3){ 3.5f, 3.5f, 1 }, (t3_color){ FAN_COL[k][0] * hb, FAN_COL[k][1] * hb, FAN_COL[k][2] * hb });
    /* the spot light rides the fan's middle, its cone lit in the haze */
    float a = PI + sway;
    t3_vec3 d = { -sinf(a), cosf(a), 0.25f };
    float yaw = atan2f(-d.x, -d.z), pitch = asinf(d.y / sqrtf(d.x * d.x + d.y * d.y + d.z * d.z));
    cw_node3d_set_rotation(B.spot[k], (t3_vec3){ pitch, yaw, 0 });
    setf(B.spot[k], "energy", 30.0 * w);
    setf(B.spot[k], "fog_energy", 1.5 * w);
  }
  done(B.rays);
  done(B.heads);
  setf(B.haze_vol, "density", 0.02 * w);
}

/* ── the backdrops ───────────────────────────────────────────────────── */

void backdrop_build(cw_node level) {
  if (B.built) return;
  B.built = true;
  B.root = add(level, "Node3D", "Backdrop");
  for (int i = 1; i < BD_COUNT; i++) {
    B.node[i] = add(B.root, "Node3D", NAMES[i]);
    setb(B.node[i], "visible", false);
  }
  build_fire(B.node[BD_FIRE]);
  build_water(B.node[BD_WATER]);
  build_grid(B.node[BD_GRID]);
  build_lasers(B.node[BD_LASERS]);
  for (int i = 1; i < BD_COUNT; i++) B.w_set[i] = -1; /* every backdrop set to nothing on the first update */
}

void backdrop_update(const float weight[BD_COUNT], float beat, float t, float energy) {
  (void)t;
  if (!B.built) return;
  for (int i = 1; i < BD_COUNT; i++) {
    float w = fminf(1, fmaxf(0, weight[i]));
    bool show = w > 0.001f;
    if (show != B.shown[i]) {
      B.shown[i] = show;
      setb(B.node[i], "visible", show);
      /* a fire coming back starts already burning (its particles simulated ahead), never from nothing */
      if (show && i == BD_FIRE) cw_particles_restart(B.flames), cw_particles_restart(B.core), cw_particles_restart(B.embers), cw_particles_restart(B.haze);
    }
    B.w[i] = w;
    if (!show && B.w_set[i] == 0) continue;
    B.w_set[i] = w; /* a backdrop going out is set to 0 once (its particles, lights and fog draw nothing) */
    switch (i) {
    case BD_FIRE: update_fire(w, beat, energy); break;
    case BD_WATER: update_water(w, beat); break;
    case BD_GRID: update_grid(w, beat); break;
    case BD_LASERS: update_lasers(w, beat); break;
    }
  }
}
