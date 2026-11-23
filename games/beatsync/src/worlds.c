/* The newer backdrop worlds (worlds.h): what each world's scene file (backdrops/<name>.cwscene) cannot hold, built by
 * code into its root and moved with the music. The scene files hold the camera, the environment, the lights, the
 * floors and the particle systems. */
#include "worlds.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/noise.h"
#include "cartwheel/rim_light.h"
#include "cartwheel/scene.h"

#include "bd_kit.h"

static float clamp01(float x) { return fminf(1, fmaxf(0, x)); }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }

/* a heightfield strip of RX x RZ vertices from x0..x1, z0..z1, heights and colours from a function, lit standard */
typedef void (*field_fn)(float x, float z, float *h, float c[3]);
static t3_mesh *heightfield(t3_object *at, int RX, int RZ, float x0, float x1, float z0, float z1, field_fn f, float rough) {
  float *p = calloc((size_t)RX * RZ * 3, sizeof(float)), *c = calloc((size_t)RX * RZ * 3, sizeof(float));
  for (int z = 0; z < RZ; z++)
    for (int x = 0; x < RX; x++) {
      float X = x0 + (x1 - x0) * (float)x / (float)(RX - 1), Z = z0 + (z1 - z0) * (float)z / (float)(RZ - 1), H;
      f(X, Z, &H, c + (z * RX + x) * 3);
      float *pp = p + (z * RX + x) * 3;
      pp[0] = X, pp[1] = H, pp[2] = Z;
    }
  uint32_t *ix = malloc(sizeof(uint32_t) * (size_t)(RX - 1) * (RZ - 1) * 6);
  int n = 0;
  for (int z = 0; z < RZ - 1; z++)
    for (int x = 0; x < RX - 1; x++) {
      uint32_t a = (uint32_t)(z * RX + x), b = a + 1, cc = a + (uint32_t)RX, d = cc + 1;
      ix[n++] = a, ix[n++] = cc, ix[n++] = b, ix[n++] = b, ix[n++] = cc, ix[n++] = d;
    }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, p, RX * RZ, 3));
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, c, RX * RZ, 3));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, ix, n, 1));
  t3_geometry_compute_vertex_normals(g);
  free(p), free(c), free(ix);
  t3_material *m = rough < 0 ? t3_mesh_basic_material_new(0xffffff) : t3_mesh_standard_material_new(0xffffff); /* rough < 0: unlit */
  m->vertex_colors = true;
  if (rough >= 0) m->roughness = rough;
  return add_mesh(at, g, m, (t3_vec3){ 0, 0, 0 });
}

/* stars on a far card: n dots between y0 and y1 at distance z, brightness falling toward the horizon */
static t3_instanced_mesh *stars(t3_object *at, int n, float w, float y0, float y1, float z, float size, uint32_t seed) {
  t3_instanced_mesh *im = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(16, 16, tx_dot), n);
  for (int i = 0; i < n; i++) {
    float x = -w * 0.5f + w * rnd(seed, (uint32_t)i), y = y0 + (y1 - y0) * powf(rnd(seed + 1, (uint32_t)i), 0.8f);
    float s = size * (0.6f + 1.4f * powf(rnd(seed + 2, (uint32_t)i), 3.0f)), b = (0.12f + 0.6f * powf(rnd(seed + 3, (uint32_t)i), 2.0f)) * smooth(y0, y0 + (y1 - y0) * 0.25f, y);
    float warm = rnd(seed + 4, (uint32_t)i);
    place(im, i, (t3_vec3){ x, y, z }, about(0, 0, 1, 0), (t3_vec3){ s, s, 1 }, (t3_color){ b * (0.8f + 0.2f * warm), b * 0.9f, b * (1.0f - 0.15f * warm) });
  }
  done(im);
  return im;
}

/* a vertical gradient sky card: bottom colour at the horizon (v 1) rising into the top colour */
static float g_sky_lo[3], g_sky_hi[3], g_sky_pow;
static void tx_sky(float u, float v, float c[3]) {
  (void)u;
  float k = powf(v, g_sky_pow);
  for (int i = 0; i < 3; i++) c[i] = g_sky_hi[i] + (g_sky_lo[i] - g_sky_hi[i]) * k;
}
static t3_mesh *sky_card(t3_object *at, float w, float h, t3_vec3 pos, const float lo[3], const float hi[3], float pw) {
  memcpy(g_sky_lo, lo, sizeof g_sky_lo), memcpy(g_sky_hi, hi, sizeof g_sky_hi), g_sky_pow = pw;
  t3_mesh *q = quad(at, w, h, texture_rgb(4, 256, tx_sky), pos);
  q->materials[0]->fog = false;
  tint(q, 1, 1, 1, 1);
  T3_OBJ(q)->render_order = -10;
  return q;
}

static float tx_crescent(float u, float v);

/* ── aurora ──────────────────────────────────────────────────────────── */

#define AU_CURTAINS 3
#define AU_RAYS 220
#define AU_TREES 260
static struct {
  t3_instanced_mesh *rays, *trees;
  t3_mesh *sky, *glow;
} AU;

/* one ray of a curtain (v 0 the top): a sharp bright lower hem in green, the light rising and thinning into violet */
static void tx_aurora(float u, float v, float c[3]) {
  float x = (u - 0.5f) * 2, across = expf(-x * x * 3.0f);
  float y = 1 - v; /* 0 at the hem */
  float hem = smooth(0.0f, 0.04f, y) * expf(-y * 2.2f);
  float body = smooth(0.0f, 0.06f, y) * (1 - smooth(0.35f, 1.0f, y));
  float green = 1.25f * hem + 0.5f * body;
  float violet = smooth(0.35f, 0.75f, y) * (1 - smooth(0.75f, 1.0f, y)) * 0.55f;
  c[0] = (0.12f * green + 0.75f * violet) * across;
  c[1] = (1.0f * green + 0.08f * violet) * across;
  c[2] = (0.45f * green + 0.95f * violet) * across;
}
static void tx_snowglow(float u, float v, float c[3]) {
  float x = (u - 0.5f) * 2, y = (v - 0.5f) * 2, d = x * x + y * y;
  float k = expf(-d * 2.5f);
  c[0] = 0.1f * k, c[1] = 0.6f * k, c[2] = 0.4f * k;
}
static cw_noise au_n;
static void au_range(float x, float z, float *h, float c[3]) {
  float n = cw_noise_2d(&au_n, x, z) * 0.5f + 0.5f;
  float H = (20 + 230 * n * n) * smooth(-1000, -1080, z) * (0.55f + 0.45f * smooth(0, 700, fabsf(x)));
  *h = H;
  float sn = smooth(70, 130, H);
  float rock[3] = { 0.04f, 0.05f, 0.07f }, snow[3] = { 0.55f, 0.65f, 0.8f };
  for (int k = 0; k < 3; k++) c[k] = rock[k] + (snow[k] - rock[k]) * sn;
}

void world_aurora_build(cw_node root) {
  t3_object *at = cw_node3d_object(root);
  static const float lo[3] = { 0.02f, 0.06f, 0.07f }, hi[3] = { 0.0f, 0.0f, 0.01f };
  AU.sky = sky_card(at, 4000, 1400, (t3_vec3){ 0, 600, -1500 }, lo, hi, 2.5f);
  stars(at, 700, 4000, 30, 1250, -1480, 4.0f, 801);
  au_n = cw_noise_default(), au_n.seed = 81, au_n.frequency = 0.004f, au_n.fractal = CW_NOISE_FRACTAL_RIDGED, au_n.octaves = 5;
  heightfield(at, 240, 30, -2000, 2000, -1000, -1500, au_range, 0.8f);
  AU.rays = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture_rgb(32, 256, tx_aurora), AU_CURTAINS * AU_RAYS);
  AU.glow = quad(at, 1400, 500, texture_rgb(64, 64, tx_snowglow), (t3_vec3){ 0, 0.2f, -420 });
  T3_OBJ(AU.glow)->quaternion = about(1, 0, 0, -PI / 2);
  /* the pines: three stacked cones a tree, dark, their edges rimmed by the sky's green light */
  t3_material *m = t3_mesh_standard_material_new(0x000000);
  m->color = (t3_color){ 0.006f, 0.014f, 0.012f }, m->roughness = 0.9f;
  cw_rim_light_desc rd = cw_rim_light_default();
  rd.color = (t3_color){ 0.25f, 1.0f, 0.65f }, rd.strength = 0.18f, rd.power = 5.0f, rd.near = 0, rd.far = 1, rd.close = 1;
  cw_rim_light_apply(m, &rd);
  t3_geometry *cone = t3_cone_geometry_new(1, 1, 12, 1, false);
  t3_geometry_translate(cone, 0, 0.5f, 0);
  AU.trees = many_m(at, cone, m, AU_TREES * 5);
  int n = 0;
  for (int i = 0; i < AU_TREES; i++) {
    float side = (i & 1) ? 1.0f : -1.0f, r = rnd(82, (uint32_t)i);
    float z = 8 - 520 * powf(rnd(83, (uint32_t)i), 1.3f), x = side * (16 + 12 * r + 0.55f * (8 - z) * (0.4f + 0.8f * rnd(84, (uint32_t)i)));
    float h = 9 + 9 * rnd(85, (uint32_t)i), w = h * 0.3f;
    for (int k = 0; k < 5; k++) { /* tiers of drooping boughs, each narrower, the top one a spire */
      float kk = (float)k / 5, tw = w * (1 - 0.17f * (float)k) * (0.9f + 0.2f * rnd(86, (uint32_t)(i * 5 + k)));
      place(AU.trees, n++, (t3_vec3){ x, h * (0.06f + 0.66f * kk), z }, about(0, 1, 0, r * 6 + (float)k), (t3_vec3){ tw, h * (k == 4 ? 0.36f : 0.24f), tw }, (t3_color){ 1, 1, 1 });
    }
  }
  done(AU.trees);
}

void world_aurora_update(float beat, float t, float energy) {
  (void)beat;
  float e = 0.8f + 0.35f * energy;
  /* the curtains fold and ripple slowly: a fold travels along each over tens of seconds, the rays' heights rippling
   * along it (waves running sideways, as an aurora's do), never pulsing on the beat */
  int n = 0;
  float glow = 0;
  for (int c = 0; c < AU_CURTAINS; c++) {
    float zc = -620 - 160 * (float)c, yb = 150 + 60 * (float)c, ph = t * (0.05f + 0.02f * (float)c) + (float)c * 2.1f;
    for (int i = 0; i < AU_RAYS; i++) {
      float s = (float)i / (AU_RAYS - 1);
      float x = -900 + 1800 * s + 60 * sinf(s * 5 + ph * 0.7f);
      float z = zc + 140 * sinf(s * 3.4f + ph) + 60 * sinf(s * 9.0f - ph * 1.6f);
      float ripple = 0.5f + 0.5f * sinf(s * 38 - t * (0.6f + 0.15f * (float)c) + (float)c) * sinf(s * 13 + t * 0.21f);
      /* the curtain thins and breaks along its length (stretches of dark sky between its bright folds) */
      float body = smooth(0.25f, 0.6f, 0.5f + 0.5f * sinf(s * 9.0f + ph * 1.3f + (float)c * 1.7f));
      float bright = (0.1f + 0.9f * ripple * ripple) * body * smooth(0, 0.15f, s) * smooth(1, 0.85f, s) * (c == 0 ? 0.42f : 0.26f) * e;
      float h = 150 + 150 * ripple + 70 * sinf(s * 7 + ph);
      float y = yb + 40 * sinf(s * 4.3f + ph * 0.8f) + h * 0.5f;
      place(AU.rays, n++, (t3_vec3){ x, y, z }, about(0, 0, 1, 0.05f * sinf(s * 11 + ph)), (t3_vec3){ 13, h, 1 }, (t3_color){ bright, bright, bright });
      glow += bright;
    }
  }
  done(AU.rays);
  glow /= (float)(AU_CURTAINS * AU_RAYS);
  tint(AU.glow, 1, 1, 1, 2.0f * glow);
}

/* ── space ───────────────────────────────────────────────────────────── */

static struct {
  t3_mesh *neb[3], *planet, *ring;
  t3_instanced_mesh *mid, *glints;
} SP;
static cw_noise sp_n, sp_w;
static int sp_layer;
/* the nebula's gas: warped billows, each layer its own colours: violet and magenta, teal and blue, a warm core */
static void tx_nebula(float u, float v, float c[3]) {
  float x = u * 600, y = v * 340;
  cw_noise_warp_2d(&sp_w, &x, &y);
  float n = cw_noise_2d(&sp_n, x + (float)sp_layer * 300, y) * 0.5f + 0.5f;
  float edge = smooth(0, 0.25f, u) * smooth(1, 0.75f, u) * smooth(0, 0.3f, v) * smooth(1, 0.7f, v);
  float d = powf(clamp01(n * 1.35f - 0.25f), 2.2f) * edge;
  static const float COL[3][2][3] = { { { 0.55f, 0.08f, 0.75f }, { 1.0f, 0.3f, 0.6f } },
                                      { { 0.05f, 0.35f, 0.8f }, { 0.2f, 0.9f, 0.95f } },
                                      { { 0.9f, 0.4f, 0.12f }, { 1.0f, 0.85f, 0.6f } } };
  float hot = smooth(0.35f, 0.9f, d);
  for (int k = 0; k < 3; k++) c[k] = d * mixf(COL[sp_layer][0][k], COL[sp_layer][1][k], hot);
}
static void tx_planet(float u, float v, float c[3]) {
  float x = u * 400, y = v * 200;
  float w = cw_noise_2d(&sp_n, x * 0.6f, y * 3.0f);
  float band = 0.5f + 0.5f * sinf(v * 34 + w * 2.2f) * cosf(v * 11 + w);
  c[0] = 0.55f + 0.3f * band, c[1] = 0.4f + 0.25f * band, c[2] = 0.32f + 0.15f * band;
}
static void tx_ring(float u, float v, float c[4]) {
  float x = (u - 0.5f) * 2, y = (v - 0.5f) * 2, r = sqrtf(x * x + y * y);
  float in = smooth(0.62f, 0.66f, r) * (1 - smooth(0.97f, 1.0f, r));
  float bands = 0.55f + 0.45f * sinf(r * 90) * sinf(r * 23 + 1);
  float gap = 1 - 0.85f * expf(-powf((r - 0.8f) / 0.012f, 2));
  c[0] = 0.85f, c[1] = 0.78f, c[2] = 0.68f, c[3] = in * bands * gap * 0.85f;
}
static void tx_glint(float u, float v, float c[3]) { /* a star's four-point glint */
  float x = fabsf(u - 0.5f) * 2, y = fabsf(v - 0.5f) * 2;
  float k = expf(-x * 40) * powf(1 - y, 3) + expf(-y * 40) * powf(1 - x, 3) + expf(-(x * x + y * y) * 30);
  c[0] = c[1] = c[2] = k;
}

void world_space_build(cw_node root) {
  t3_object *at = cw_node3d_object(root);
  stars(at, 900, 5000, -1800, 1800, -2600, 3.0f, 901);
  SP.mid = stars(at, 260, 2600, -900, 900, -1400, 3.6f, 905);
  sp_n = cw_noise_default(), sp_n.seed = 91, sp_n.frequency = 0.008f, sp_n.fractal = CW_NOISE_FRACTAL_FBM, sp_n.octaves = 6;
  sp_w = cw_noise_default(), sp_w.seed = 92, sp_w.frequency = 0.004f, sp_w.warp_amp = 90;
  static const float NZ[3] = { -2300, -2000, -1700 }, NW[3] = { 3600, 2800, 2000 }, NX[3] = { 300, -500, 600 }, NY[3] = { 250, 380, 120 };
  for (int k = 0; k < 3; k++) {
    sp_layer = k;
    t3_texture *tx = texture_rgb(384, 216, tx_nebula);
    SP.neb[k] = quad(at, NW[k], NW[k] * 0.5625f, tx, (t3_vec3){ NX[k], NY[k], NZ[k] });
    SP.neb[k]->materials[0]->fog = false;
  }
  /* the bright stars' glints */
  SP.glints = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture_rgb(64, 64, tx_glint), 40);
  for (int i = 0; i < 40; i++) {
    float x = -1300 + 2600 * rnd(93, (uint32_t)i), y = -700 + 1400 * rnd(94, (uint32_t)i), s = 18 + 26 * rnd(95, (uint32_t)i), b = 0.5f + 0.9f * rnd(96, (uint32_t)i);
    if (fabsf(x) < 260 && y < 200) x += x < 0 ? -400 : 400; /* clear of the rivers' far end */
    place(SP.glints, i, (t3_vec3){ x, y, -1300 }, about(0, 0, 1, 0.3f), (t3_vec3){ s, s, 1 }, (t3_color){ b, b * 0.95f, b * 1.05f });
  }
  done(SP.glints);
  /* the planet: banded, lit from the side by the star, a thin blue atmosphere at its rim, rings at a tilt */
  t3_material *pm = t3_mesh_standard_material_new(0xffffff);
  pm->roughness = 0.85f;
  t3_material_set_map(pm, texture_rgb(256, 128, tx_planet));
  cw_rim_light_desc rd = cw_rim_light_default();
  rd.color = (t3_color){ 0.35f, 0.6f, 1.0f }, rd.strength = 1.4f, rd.power = 3.5f, rd.near = 0, rd.far = 1, rd.close = 1;
  cw_rim_light_apply(pm, &rd);
  SP.planet = add_mesh(at, t3_sphere_geometry_new(150, 64, 48), pm, (t3_vec3){ -740, 200, -1300 });
  T3_OBJ(SP.planet)->quaternion = about(0, 0, 1, 0.35f);
  t3_material *rm = t3_mesh_standard_material_new(0xffffff);
  rm->transparent = true, rm->depth_write = false, rm->side = T3_DOUBLE_SIDE, rm->roughness = 0.9f;
  t3_material_set_map(rm, texture_rgba(512, 512, tx_ring));
  SP.ring = add_mesh(at, t3_plane_geometry_new(620, 620, 1, 1), rm, (t3_vec3){ -740, 200, -1300 });
  T3_OBJ(SP.ring)->quaternion = qmul(about(0, 0, 1, 0.35f), about(1, 0, 0, -1.25f));
}

void world_space_update(float beat, float t, float energy) {
  (void)beat;
  float e = 0.85f + 0.3f * energy;
  /* the gas breathes with the music's loudness (over seconds), each layer drifting at its own pace: depth */
  for (int k = 0; k < 3; k++) {
    tint(SP.neb[k], 1, 1, 1, (k == 2 ? 0.7f : 1.0f) * e);
    T3_OBJ(SP.neb[k])->position.x += 0; /* (still: a drift of the far gas read as the camera turning) */
    T3_OBJ(SP.neb[k])->quaternion = about(0, 0, 1, 0.02f * sinf(t * 0.01f + (float)k));
  }
  T3_OBJ(SP.mid)->position.x = 40 * sinf(t * 0.015f);
  T3_OBJ(SP.planet)->quaternion = qmul(about(0, 0, 1, 0.35f), about(0, 1, 0, t * 0.02f));
}

/* ── city ────────────────────────────────────────────────────────────── */

#define CT_HALF 9.0f       /* half the street's width, kerb to kerb */
#define CT_SIGN_KINDS 6
#define CT_SIGNS 6         /* of each kind */
#define CT_LAMPS 16
static struct {
  t3_instanced_mesh *sign[CT_SIGN_KINDS], *lamp_glow;
  t3_mesh *sky, *haze;
  t3_material *windows;
  cw_node street;
  float refl_set;
} CT;
/* windows: a grid of cells 2.2 m wide and 3.2 m tall in world units, each a lit or dark pane, warm or cold */
static float pane_at(float u, float v, int *cx, int *cy) {
  *cx = (int)(u * 16), *cy = (int)(v * 16);
  float fx = u * 16 - (float)*cx, fy = v * 16 - (float)*cy;
  /* tall narrow panes (a lit window in the backdrop never a square: a note is one) */
  return smooth(0.38f, 0.42f, fx) * smooth(0.62f, 0.58f, fx) * smooth(0.08f, 0.13f, fy) * smooth(0.94f, 0.89f, fy);
}
/* the facade: dark concrete with the panes darker glass and a ledge along each floor */
static void tx_facade(float u, float v, float c[3]) {
  int cx, cy;
  float pane = pane_at(u, v, &cx, &cy), fy = v * 16 - (float)cy;
  float ledge = smooth(0.06f, 0.0f, fy) + smooth(0.94f, 1.0f, fy);
  float k = 0.32f + 0.25f * ledge - 0.2f * pane;
  c[0] = k * 0.9f, c[1] = k * 0.9f, c[2] = k;
}
static void tx_windows(float u, float v, float c[3]) {
  int cx, cy;
  float pane = pane_at(u, v, &cx, &cy);
  uint32_t h = hsh((uint32_t)cx * 31u + 7u, (uint32_t)cy * 17u + 3u);
  float lit = (h & 255) < 80 ? 1.0f : ((h >> 8) & 255) < 60 ? 0.3f : 0.03f;
  float warm = ((h >> 16) & 3) ? 1 : 0;
  float b = lit * pane * (0.6f + 0.4f * (float)((h >> 20) & 15) / 15.0f);
  c[0] = b * (warm ? 1.0f : 0.55f), c[1] = b * (warm ? 0.72f : 0.75f), c[2] = b * (warm ? 0.42f : 1.0f);
}
/* the neon blade signs: tubes of light on a dark board, each kind a design of its own (no letters: shapes) */
static int ct_kind;
static float tube(float d, float w) { return expf(-d * d / (w * w)); }
static void tx_sign(float u, float v, float c[3]) {
  float x = (u - 0.5f) * 2, y = (v - 0.5f) * 2; /* tall: x -1..1 across the board, y -1..1 down it */
  float frame = tube(fmaxf(fabsf(x) - 0.82f, fabsf(y) - 0.92f) > 0 ? fmaxf(fabsf(x) - 0.82f, fabsf(y) - 0.92f) : fminf(0.82f - fabsf(x), 0.92f - fabsf(y)), 0.035f);
  float in = 0;
  switch (ct_kind) {
  case 0: /* stacked rings */
    for (int k = 0; k < 4; k++) { float cy = -0.66f + 0.44f * (float)k; in += tube(sqrtf(x * x + (y - cy) * (y - cy)) - 0.28f, 0.03f); }
    break;
  case 1: /* a zigzag down the board */
    { float zz = 0.5f * (2 * fabsf(fmodf(y * 2.5f + 10, 2) - 1) - 1); in = tube(x - zz, 0.04f) * smooth(0.85f, 0.75f, fabsf(y)); }
    break;
  case 2: /* bars of different lengths */
    for (int k = 0; k < 6; k++) { float cy = -0.72f + 0.29f * (float)k, len = 0.3f + 0.4f * rnd(97, (uint32_t)k); in += tube(y - cy, 0.035f) * smooth(len + 0.04f, len, fabsf(x)); }
    break;
  case 3: /* an arrow pointing down */
    { float sh = tube(x, 0.04f) * smooth(0.7f, 0.6f, fabsf(y + 0.1f)); float hd = tube(fabsf(x) - (0.55f - (y - 0.45f) * 0.0f) * 0 - fabsf(y - 0.75f) * 0.8f, 0.04f) * smooth(0.45f, 0.5f, y) * smooth(0.88f, 0.8f, y); in = sh + hd; }
    break;
  case 4: /* a diamond and dots */
    { float dm = fabsf(x) + fabsf(y * 0.8f + 0.2f); in = tube(dm - 0.45f, 0.035f); for (int k = 0; k < 3; k++) in += tube(sqrtf((x - 0.4f * (float)(k - 1)) * (x - 0.4f * (float)(k - 1)) + (y - 0.7f) * (y - 0.7f)), 0.07f); }
    break;
  default: /* a wave */
    in = tube(x - 0.45f * sinf(y * 7), 0.04f) * smooth(0.85f, 0.75f, fabsf(y));
    break;
  }
  float board = 0.03f;
  float k = fminf(1.4f, frame * 0.8f + in) + board;
  c[0] = c[1] = c[2] = k;
}
static void tx_haze(float u, float v, float c[3]) {
  (void)u;
  float k = expf(-fabsf(v - 0.75f) * 5.0f);
  c[0] = 0.5f * k, c[1] = 0.25f * k, c[2] = 0.7f * k;
}

/* a box's faces into the building geometry: the face toward the street (normal sx), the face toward the camera (+z)
 * and the top; uvs in window cells (2.2 m by 3.2 m, 16 a texture) */
typedef struct { float *p, *n, *uv; uint32_t *ix; int nv, ni; } geo;
static void geo_quad(geo *g, t3_vec3 a, t3_vec3 b, t3_vec3 c, t3_vec3 d, t3_vec3 nrm, float u0, float v0, float u1, float v1) {
  t3_vec3 q[4] = { a, b, c, d };
  float uvs[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
  for (int k = 0; k < 4; k++) {
    float *pp = g->p + g->nv * 3, *nn = g->n + g->nv * 3, *tt = g->uv + g->nv * 2;
    pp[0] = q[k].x, pp[1] = q[k].y, pp[2] = q[k].z, nn[0] = nrm.x, nn[1] = nrm.y, nn[2] = nrm.z, tt[0] = uvs[k][0], tt[1] = uvs[k][1];
    g->nv++;
  }
  uint32_t o = (uint32_t)g->nv - 4;
  g->ix[g->ni++] = o, g->ix[g->ni++] = o + 1, g->ix[g->ni++] = o + 2, g->ix[g->ni++] = o, g->ix[g->ni++] = o + 2, g->ix[g->ni++] = o + 3;
}

void world_city_build(cw_node root) {
  t3_object *at = cw_node3d_object(root);
  CT.street = cw_node_get(root, "Street"), CT.refl_set = -1;
  static const float lo[3] = { 0.09f, 0.04f, 0.12f }, hi[3] = { 0.004f, 0.004f, 0.014f };
  CT.sky = sky_card(at, 2400, 900, (t3_vec3){ 0, 300, -900 }, lo, hi, 3.0f);
  CT.haze = quad(at, 2400, 300, texture_rgb(4, 128, tx_haze), (t3_vec3){ 0, 60, -880 });
  CT.haze->materials[0]->fog = false;
  /* the towers: along both sides of the street from beside the camera to the far end, a skyline beyond */
  enum { MAXB = 160 };
  geo g = { calloc(MAXB * 12 * 3, 4), calloc(MAXB * 12 * 3, 4), calloc(MAXB * 12 * 2, 4), calloc(MAXB * 18, 4), 0, 0 };
  int nb = 0;
  for (int side = -1; side <= 1; side += 2) {
    float z = 14;
    for (int i = 0; z > -620 && nb < MAXB - 10; i++, nb++) {
      float len = 14 + 20 * rnd(1001 + (uint32_t)side, (uint32_t)i), depth = 18 + 16 * rnd(1003 + (uint32_t)side, (uint32_t)i);
      float hgt = 18 + 95 * powf(rnd(1005 + (uint32_t)side, (uint32_t)i), 1.5f) * (0.5f + 0.5f * smooth(10, -200, z));
      float x0 = (CT_HALF + 2.5f + 2 * rnd(1007 + (uint32_t)side, (uint32_t)i)) * (float)side, x1 = x0 + depth * (float)side;
      float z0 = z, z1 = z - len;
      float cu = len / 2.2f / 16.0f, cv = hgt / 3.2f / 16.0f, cd = depth / 2.2f / 16.0f;
      float off = rnd(1009 + (uint32_t)side, (uint32_t)i) * 0.5f;
      /* the street face */
      geo_quad(&g, (t3_vec3){ x0, 0, z0 }, (t3_vec3){ x0, 0, z1 }, (t3_vec3){ x0, hgt, z1 }, (t3_vec3){ x0, hgt, z0 }, (t3_vec3){ (float)-side, 0, 0 }, off, off + cv, off + cu, off);
      /* the face toward the camera */
      geo_quad(&g, (t3_vec3){ x0, 0, z0 }, (t3_vec3){ x1, 0, z0 }, (t3_vec3){ x1, hgt, z0 }, (t3_vec3){ x0, hgt, z0 }, (t3_vec3){ 0, 0, 1 }, off, off + cv, off + cd, off);
      /* the roof */
      geo_quad(&g, (t3_vec3){ x0, hgt, z0 }, (t3_vec3){ x1, hgt, z0 }, (t3_vec3){ x1, hgt, z1 }, (t3_vec3){ x0, hgt, z1 }, (t3_vec3){ 0, 1, 0 }, 0, 0, 0, 0);
      z = z1 - 1.5f - 3 * rnd(1011 + (uint32_t)side, (uint32_t)i);
    }
  }
  /* the skyline beyond the street's end */
  for (int i = 0; i < 9 && nb < MAXB; i++, nb++) {
    float x0 = -260 + 520 * (float)i / 8 + 20 * rnd(1013, (uint32_t)i), w = 40 + 40 * rnd(1014, (uint32_t)i), hgt = 60 + 170 * rnd(1015, (uint32_t)i), z0 = -640 - 60 * rnd(1016, (uint32_t)i);
    float cu = w / 2.2f / 16.0f, cv = hgt / 3.2f / 16.0f;
    geo_quad(&g, (t3_vec3){ x0, 0, z0 }, (t3_vec3){ x0 + w, 0, z0 }, (t3_vec3){ x0 + w, hgt, z0 }, (t3_vec3){ x0, hgt, z0 }, (t3_vec3){ 0, 0, 1 }, 0, cv, cu, 0);
  }
  t3_geometry *bg = t3_geometry_new();
  t3_geometry_set_attribute(bg, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, g.p, g.nv, 3));
  t3_geometry_set_attribute(bg, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, g.n, g.nv, 3));
  t3_geometry_set_attribute(bg, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, g.uv, g.nv, 2));
  t3_geometry_set_index(bg, t3_attribute_new(T3_UINT32, g.ix, g.ni, 1));
  free(g.p), free(g.n), free(g.uv), free(g.ix);
  t3_material *bm = t3_mesh_standard_material_new(0xffffff);
  bm->color = (t3_color){ 0.12f, 0.12f, 0.14f }, bm->roughness = 0.55f, bm->metalness = 0.2f;
  bm->emissive = (t3_color){ 0.5f, 0.5f, 0.5f }; /* (dim enough that no window reads as a note) */
  t3_texture *ft = texture_rgb(512, 512, tx_facade), *wt = texture_rgb(512, 512, tx_windows);
  ft->wrap_s = ft->wrap_t = T3_REPEAT, wt->wrap_s = wt->wrap_t = T3_REPEAT;
  t3_material_set_map(bm, ft), t3_release(ft);
  t3_material_set_texture(bm, T3_EMISSIVE_MAP, wt), t3_release(wt);
  CT.windows = bm;
  add_mesh(at, bg, bm, (t3_vec3){ 0, 0, 0 });
  /* the pavements */
  for (int side = -1; side <= 1; side += 2) {
    t3_material *pm = t3_mesh_standard_material_new(0xffffff);
    pm->color = (t3_color){ 0.03f, 0.03f, 0.035f }, pm->roughness = 0.35f;
    add_mesh(at, t3_box_geometry_new(3, 0.3f, 700, 1, 1, 1), pm, (t3_vec3){ (float)side * (CT_HALF + 1.5f), 0.15f, -320 });
  }
  /* the signs, blade signs off the street faces, facing the camera, each kind its own instanced set */
  for (int k = 0; k < CT_SIGN_KINDS; k++) {
    ct_kind = k;
    CT.sign[k] = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture_rgb(64, 160, tx_sign), CT_SIGNS);
  }
  static const float COL[5][3] = { { 1, 0.15f, 0.55f }, { 0.15f, 0.85f, 1 }, { 1, 0.6f, 0.12f }, { 0.6f, 0.3f, 1 }, { 0.3f, 1, 0.45f } };
  for (int k = 0; k < CT_SIGN_KINDS; k++)
    for (int i = 0; i < CT_SIGNS; i++) {
      int j = k * CT_SIGNS + i;
      float side = (j & 1) ? 1.0f : -1.0f, z = 6 - 380 * powf(((float)j + rnd(1021, (uint32_t)j)) / (CT_SIGN_KINDS * CT_SIGNS), 1.25f);
      float h = 5 + 4 * rnd(1022, (uint32_t)j), w = 1.6f + 0.8f * rnd(1023, (uint32_t)j), y = 4 + 14 * rnd(1024, (uint32_t)j);
      const float *c = COL[hsh(1025, (uint32_t)j) % 5];
      float b = 0.95f + 0.5f * rnd(1026, (uint32_t)j); /* (bright enough to glow, never as bright as a note) */
      place(CT.sign[k], i, (t3_vec3){ side * (CT_HALF + 2.0f + w * 0.6f), y, z }, about(0, 0, 1, 0), (t3_vec3){ w, h, 1 }, (t3_color){ c[0] * b, c[1] * b, c[2] * b });
    }
  for (int k = 0; k < CT_SIGN_KINDS; k++) done(CT.sign[k]);
  /* the street lamps' heads: warm glows along both kerbs */
  CT.lamp_glow = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(32, 32, tx_dot), CT_LAMPS);
  for (int i = 0; i < CT_LAMPS; i++) {
    float side = (i & 1) ? 1.0f : -1.0f, z = 4 - 34 * (float)(i / 2);
    place(CT.lamp_glow, i, (t3_vec3){ side * (CT_HALF - 0.5f), 7.5f, z }, about(0, 0, 1, 0), (t3_vec3){ 3.6f, 1.1f, 1 }, (t3_color){ 0.9f, 0.6f, 0.28f }); /* a wide low glow of a sodium lamp's long head: never a round bright dot (a note) */
  }
  done(CT.lamp_glow);
}

void world_city_windows(float k) {
  if (!CT.windows) return;
  float e = 0.5f * k;
  if (fabsf(CT.windows->emissive.r - e) > 1e-4f) CT.windows->emissive = (t3_color){ e, e, e };
  /* and the street's mirror fainter (a sign's reflection printed solid read as a note) */
  float rf = 0.6f * fmaxf(0.05f, (k - 0.3f) / 0.7f);
  if (CT.street.id && fabsf(rf - CT.refl_set) > 1e-3f) cw_node_set(CT.street, "reflectivity", cw_val_float(rf)), CT.refl_set = rf;
}

void world_city_update(float beat, float t, float energy) {
  (void)beat;
  float e = 0.85f + 0.3f * energy;
  /* the signs breathe slowly, each its own phase (seconds, never a flicker) */
  for (int k = 0; k < CT_SIGN_KINDS; k++) tint(&CT.sign[k]->mesh, 1, 1, 1, e * (0.9f + 0.1f * sinf(t * 0.5f + (float)k * 1.3f)));
  tint(CT.haze, 1, 1, 1, 0.8f * e);
}

/* ── forest ──────────────────────────────────────────────────────────── */

#define FO_TREES 420
#define FO_SHAFTS 7
static struct {
  t3_instanced_mesh *trunks, *crowns, *shafts, *mist;
  t3_mesh *sky, *moon, *halo;
} FO;
static const t3_vec3 FO_MOON = { 160, 150, -700 };

void world_forest_build(cw_node root) {
  t3_object *at = cw_node3d_object(root);
  static const float lo[3] = { 0.03f, 0.05f, 0.08f }, hi[3] = { 0.002f, 0.004f, 0.012f };
  FO.sky = sky_card(at, 3000, 1200, (t3_vec3){ 0, 500, -1000 }, lo, hi, 2.0f);
  stars(at, 400, 3000, 60, 1000, -990, 3.0f, 1101);
  FO.halo = quad(at, 260, 260, texture(128, 128, tx_halo), (t3_vec3){ FO_MOON.x, FO_MOON.y, FO_MOON.z - 1 });
  FO.moon = quad(at, 40, 40, texture(128, 128, tx_crescent), FO_MOON); /* a crescent: a full disc read as a round note */
  tint(FO.moon, 1.0f, 0.97f, 0.9f, 2.6f);
  tint(FO.halo, 0.5f, 0.65f, 1.0f, 0.3f);
  /* the trees: tall straight trunks, crowns of stacked cones, a clearing round the rivers that widens with distance;
   * dark, rimmed by the moon behind them */
  t3_material *m = t3_mesh_standard_material_new(0x000000);
  m->color = (t3_color){ 0.012f, 0.012f, 0.01f }, m->roughness = 0.9f;
  cw_rim_light_desc rd = cw_rim_light_default();
  rd.color = (t3_color){ 0.45f, 0.6f, 1.0f }, rd.strength = 0.16f, rd.power = 5.0f, rd.near = 0, rd.far = 1, rd.close = 1;
  cw_rim_light_apply(m, &rd);
  t3_material *cm = t3_mesh_standard_material_new(0x000000);
  cm->color = (t3_color){ 0.006f, 0.014f, 0.01f }, cm->roughness = 0.95f;
  cw_rim_light_apply(cm, &rd);
  t3_geometry *trunk = t3_cylinder_geometry_new(0.6f, 1, 1, 7, 1, false);
  t3_geometry_translate(trunk, 0, 0.5f, 0);
  FO.trunks = many_m(at, trunk, m, FO_TREES);
  t3_geometry *cone = t3_cone_geometry_new(1, 1, 12, 1, false);
  t3_geometry_translate(cone, 0, 0.5f, 0);
  FO.crowns = many_m(at, cone, cm, FO_TREES * 5);
  int n = 0;
  for (int i = 0; i < FO_TREES; i++) {
    float side = (i & 1) ? 1.0f : -1.0f;
    float z = 6 - 380 * powf(rnd(1111, (uint32_t)i), 1.2f), clear = 13 + 0.14f * (6 - z);
    float x = side * (clear + 4 + 120 * powf(rnd(1112, (uint32_t)i), 1.6f));
    float h = 14 + 16 * rnd(1113, (uint32_t)i), r = 0.25f + 0.25f * rnd(1114, (uint32_t)i);
    place(FO.trunks, i, (t3_vec3){ x, 0, z }, about(0, 1, 0, 0), (t3_vec3){ r, h, r }, (t3_color){ 1, 1, 1 });
    float w = h * 0.2f;
    for (int k = 0; k < 5; k++)
      place(FO.crowns, n++, (t3_vec3){ x, h * (0.3f + 0.13f * (float)k), z }, about(0, 1, 0, rnd(1115, (uint32_t)i) * 6 + (float)k), (t3_vec3){ w * (1 - 0.16f * (float)k), h * (k == 4 ? 0.3f : 0.2f), w * (1 - 0.16f * (float)k) }, (t3_color){ 1, 1, 1 });
  }
  done(FO.trunks), done(FO.crowns);
  FO.shafts = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(32, 256, tx_beam), FO_SHAFTS);
  /* mist lying low between the trees: soft additive banks, wide and flat, drifting */
  FO.mist = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(64, 64, tx_dot), 40);
}

void world_forest_update(float beat, float t, float energy) {
  (void)beat;
  float e = 0.85f + 0.3f * energy;
  const t3_vec3 eye = { 0, 2.4f, 22 };
  for (int i = 0; i < FO_SHAFTS; i++) {
    /* shafts slanting down from the moon's side through the canopy, swaying very slowly */
    float side = (i & 1) ? 1.0f : -1.0f, x = side * (14 + 9 * (float)i) + 3 * sinf(t * 0.05f + (float)i);
    float z = -20 - 22 * (float)i;
    t3_vec3 from = { x + 14, 30, z }, d = { -0.42f, -0.9f, 0.05f };
    float dl = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    d = (t3_vec3){ d.x / dl, d.y / dl, d.z / dl };
    float b = (0.2f + 0.08f * sinf(t * 0.13f + (float)i * 2)) * e;
    place_beam(FO.shafts, i, from, d, 34, 5 + 2 * (float)(i % 3), eye, (t3_color){ b * 0.6f, b * 0.75f, b });
  }
  done(FO.shafts);
  for (int i = 0; i < 40; i++) {
    float side = (i & 1) ? 1.0f : -1.0f, z = 6 - 9 * (float)i, x = side * (12 + 30 * rnd(1121, (uint32_t)i)) + 6 * sinf(t * 0.04f + (float)i);
    float w = 30 + 20 * rnd(1122, (uint32_t)i), b = 0.09f * e * (0.7f + 0.3f * sinf(t * 0.07f + (float)i));
    place(FO.mist, i, (t3_vec3){ x, 1.0f, z }, about(0, 0, 1, 0), (t3_vec3){ w, 4.5f, 1 }, (t3_color){ b * 0.6f, b * 0.75f, b });
  }
  done(FO.mist);
}

/* ── deep ────────────────────────────────────────────────────────────── */

#define DP_KELP 70
#define DP_SEGS 10
#define DP_FISH 90
#define DP_SHAFTS 9
static struct {
  t3_instanced_mesh *kelp, *fish, *shafts;
  t3_mesh *floor_caus, *floor_caus2, *surface, *sky;
  t3_material *floor;
} DP;
static cw_noise dp_n;
/* caustics: the bright web where the cells' borders meet (a tiling cellular noise) */
static void tx_caustic(float u, float v, float c[3]) {
  float d = cw_noise_2d(&dp_n, u * 256, v * 256);
  float k = expf(-fmaxf(0, d + 1) * 14.0f);
  c[0] = 0.55f * k, c[1] = 0.9f * k, c[2] = 1.0f * k;
}
static void tx_sand(float u, float v, float c[3]) {
  float n = cw_noise_2d(&dp_n, u * 900, v * 900) * 0.5f + 0.5f;
  float k = 0.6f + 0.4f * n;
  c[0] = 0.75f * k, c[1] = 0.68f * k, c[2] = 0.5f * k;
}
static void tx_surface(float u, float v, float c[3]) {
  float d = cw_noise_2d(&dp_n, u * 200 + 50, v * 200);
  float k = 0.25f + 0.75f * expf(-fmaxf(0, d + 1) * 6.0f);
  c[0] = 0.3f * k, c[1] = 0.75f * k, c[2] = 0.9f * k;
}
static void tx_kelp(float u, float v, float c[4]) { /* a blade: a long leaf with a dark rib, ragged edges */
  float x = (u - 0.5f) * 2;
  float w = 0.75f + 0.25f * sinf(v * 31) * sinf(v * 7);
  float a = smooth(w, w - 0.12f, fabsf(x));
  c[0] = 0.18f + 0.12f * (1 - fabsf(x)), c[1] = 0.3f + 0.15f * (1 - fabsf(x)), c[2] = 0.08f, c[3] = a;
}
static void tx_fish(float u, float v, float c[4]) { /* a fish's silhouette from the side, its tail at u 0 */
  float x = u * 2 - 1, y = (v - 0.5f) * 2;
  float body = 1 - (x * x / 0.55f + y * y / 0.16f);
  float tail = (x < -0.6f) ? smooth(0.05f, 0, fabsf(y) - (-0.6f - x) * 0.9f) : 0;
  float a = clamp01(fmaxf(smooth(0, 0.08f, body), tail));
  c[0] = 0.5f, c[1] = 0.7f, c[2] = 0.8f, c[3] = a;
}

void world_deep_build(cw_node root) {
  t3_object *at = cw_node3d_object(root);
  dp_n = cw_noise_default(), dp_n.seed = 121, dp_n.type = CW_NOISE_CELLULAR, dp_n.frequency = 0.04f;
  dp_n.cellular_return = CW_NOISE_CELL_DISTANCE2_SUB, dp_n.cellular_distance = CW_NOISE_DIST_EUCLIDEAN;
  static const float lo[3] = { 0.0f, 0.0f, 0.0f }, hi[3] = { 0.04f, 0.14f, 0.14f }; /* added over the water's colour (the background): brighter overhead */ /* its foot the water's colour far off (deep.cwscene Water) */
  DP.sky = sky_card(at, 2400, 900, (t3_vec3){ 0, 440, -600 }, lo, hi, 1.5f);
  /* the floor: sand lit by the water, the caustics as two webs scrolling across each other (their crossing is what
   * makes them dance) */
  t3_material *fm = t3_mesh_standard_material_new(0xffffff);
  fm->roughness = 0.9f;
  t3_texture *sand = texture_rgb(256, 256, tx_sand);
  sand->wrap_s = sand->wrap_t = T3_REPEAT, sand->repeat = (t3_vec2){ 40, 40 };
  t3_material_set_map(fm, sand), t3_release(sand);
  DP.floor = fm;
  t3_mesh *fl = add_mesh(at, t3_plane_geometry_new(1200, 800, 1, 1), fm, (t3_vec3){ 0, 0, -380 });
  T3_OBJ(fl)->quaternion = about(1, 0, 0, -PI / 2);
  dp_n.frequency = 0.03f;
  for (int k = 0; k < 2; k++) {
    t3_texture *ct = texture_rgb(256, 256, tx_caustic);
    ct->wrap_s = ct->wrap_t = T3_REPEAT, ct->repeat = k ? (t3_vec2){ 46, 34 } : (t3_vec2){ 60, 44 };
    t3_mesh *q = quad(at, 1200, 800, ct, (t3_vec3){ 0, 0.05f + 0.02f * (float)k, -380 });
    T3_OBJ(q)->quaternion = about(1, 0, 0, -PI / 2);
    if (k) DP.floor_caus2 = q; else DP.floor_caus = q;
  }
  /* the surface overhead, seen from below: bright ripples of light */
  dp_n.frequency = 0.05f;
  t3_texture *st = texture_rgb(256, 256, tx_surface);
  st->wrap_s = st->wrap_t = T3_REPEAT, st->repeat = (t3_vec2){ 30, 20 };
  DP.surface = quad(at, 1600, 1000, st, (t3_vec3){ 0, 42, -300 });
  T3_OBJ(DP.surface)->quaternion = about(1, 0, 0, PI / 2);
  DP.shafts = many(at, t3_plane_geometry_new(1, 1, 1, 1), texture(32, 256, tx_beam), DP_SHAFTS);
  /* kelp: strands of blades rising from the floor either side, each a chain of segments that sways */
  t3_material *km = t3_mesh_standard_material_new(0xffffff);
  km->transparent = false, km->alpha_test = 0.5f, km->side = T3_DOUBLE_SIDE, km->roughness = 0.8f;
  km->emissive = (t3_color){ 0.02f, 0.05f, 0.03f };
  t3_material_set_map(km, texture_rgba(32, 128, tx_kelp));
  t3_geometry *blade = t3_plane_geometry_new(1, 1, 1, 1);
  t3_geometry_translate(blade, 0, 0.5f, 0);
  DP.kelp = many_m(at, blade, km, DP_KELP * DP_SEGS);
  t3_material *fsh = t3_mesh_basic_material_new(0xffffff);
  fsh->transparent = true, fsh->depth_write = false, fsh->side = T3_DOUBLE_SIDE;
  t3_material_set_map(fsh, texture_rgba(64, 32, tx_fish));
  DP.fish = many_m(at, t3_plane_geometry_new(1, 1, 1, 1), fsh, DP_FISH);
}

void world_deep_update(float beat, float t, float energy) {
  (void)beat;
  float e = 0.85f + 0.3f * energy;
  DP.floor_caus->materials[0]->map->offset = (t3_vec2){ fmodf(t * 0.011f, 1), fmodf(t * 0.007f, 1) };
  DP.floor_caus2->materials[0]->map->offset = (t3_vec2){ fmodf(-t * 0.008f, 1), fmodf(t * 0.012f, 1) };
  tint(DP.floor_caus, 1, 1, 1, 0.5f * e);
  tint(DP.floor_caus2, 1, 1, 1, 0.4f * e);
  DP.surface->materials[0]->map->offset = (t3_vec2){ fmodf(t * 0.006f, 1), fmodf(t * 0.004f, 1) };
  tint(DP.surface, 1, 1, 1, 0.45f);
  const t3_vec3 eye = { 0, 3, 22 };
  for (int i = 0; i < DP_SHAFTS; i++) {
    float x = -120 + 30 * (float)i + 10 * sinf(t * 0.07f + (float)i), z = -40 - 30 * (float)(i % 4);
    t3_vec3 from = { x, 42, z }, d = { 0.18f, -1, 0.1f };
    float dl = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    d = (t3_vec3){ d.x / dl, d.y / dl, d.z / dl };
    float b = (0.16f + 0.08f * sinf(t * 0.2f + (float)i * 1.7f)) * e;
    place_beam(DP.shafts, i, from, d, 50, 6 + 4 * (float)(i % 3), eye, (t3_color){ b * 0.5f, b * 0.9f, b });
  }
  done(DP.shafts);
  /* kelp: each strand bends more toward its tip, the current swaying it slowly, each strand in its own phase */
  int n = 0;
  for (int k = 0; k < DP_KELP; k++) {
    float side = (k & 1) ? 1.0f : -1.0f, z = 10 - 300 * powf(rnd(1201, (uint32_t)k), 1.3f), x = side * (12 + 70 * powf(rnd(1202, (uint32_t)k), 1.4f) + 0.05f * (10 - z));
    float segl = 2.2f + 1.4f * rnd(1203, (uint32_t)k), w = 1.2f + 0.6f * rnd(1204, (uint32_t)k), ph = rnd(1205, (uint32_t)k) * 6.28f;
    t3_vec3 p = { x, 0, z };
    float ang = 0;
    for (int s = 0; s < DP_SEGS; s++) {
      float f = (float)s / DP_SEGS;
      ang += 0.06f * sinf(t * 0.5f + ph - f * 2.4f) * (0.4f + f);
      float tw = sinf(t * 0.3f + ph + f * 3);
      place(DP.kelp, n++, p, qmul(about(0, 1, 0, tw * 0.8f + ph), about(0, 0, 1, ang)), (t3_vec3){ w * (1 - 0.3f * f), segl, 1 }, (t3_color){ 1, 1, 1 });
      p.x += -sinf(ang) * segl, p.y += cosf(ang) * segl;
    }
  }
  done(DP.kelp);
  /* the school: fish circling a slow loop far off, each a little off the others, turning together */
  for (int i = 0; i < DP_FISH; i++) {
    float a = t * 0.08f + (float)i * 0.035f + 0.3f * rnd(1211, (uint32_t)i);
    float rr = 70 + 18 * rnd(1212, (uint32_t)i);
    float x = 60 + rr * cosf(a), z = -260 + rr * 0.6f * sinf(a), y = 16 + 10 * rnd(1213, (uint32_t)i) + 3 * sinf(a * 3 + (float)i);
    float yaw = -a + (float)PI; /* along the loop */
    float s = 2.2f + 0.8f * rnd(1214, (uint32_t)i), b = 0.55f;
    place(DP.fish, i, (t3_vec3){ x, y, z }, about(0, 1, 0, yaw), (t3_vec3){ s, s * 0.45f, 1 }, (t3_color){ b * 0.6f, b * 0.8f, b });
  }
  done(DP.fish);
}

/* ── dunes ───────────────────────────────────────────────────────────── */

static struct {
  t3_mesh *sky, *moon, *halo, *milky;
} DU;
static cw_noise du_n, du_w;
static void du_field(float x, float z, float *h, float c[3]) {
  /* dunes: long crests across the view, their slip faces steep (a skewed wave), warped and broken by noise; the
   * valley the rivers run down kept low */
  float wx = x, wz = z;
  cw_noise_warp_2d(&du_w, &wx, &wz);
  float ph = wz * 0.022f + wx * 0.006f;
  float s = ph - floorf(ph);
  float wave = s < 0.72f ? smooth(0, 0.72f, s) : 1 - smooth(0.72f, 1.0f, s) * 1.0f;
  float n = cw_noise_2d(&du_n, x, z) * 0.5f + 0.5f;
  float big = 14 + 24 * n;
  float val = smooth(14, 60, fabsf(x));
  *h = wave * big * (0.25f + 0.75f * val) + 30 * n * smooth(-200, -700, z);
  c[0] = 0.95f, c[1] = 0.8f, c[2] = 0.6f;
}
static float tx_crescent(float u, float v) {
  float a = tx_disc(u, v), x = (u - 0.5f) * 2 - 0.3f, y = (v - 0.5f) * 2 + 0.1f;
  return a * smooth(0.78f, 0.9f, sqrtf(x * x + y * y));
}
static void tx_milky(float u, float v, float c[3]) { /* a band of faint light across the sky, dust lanes in it */
  float y = (v - 0.5f) * 2 + 0.25f * sinf(u * 4);
  float band = expf(-y * y * 6);
  float n = cw_noise_2d(&du_n, u * 900, v * 250) * 0.5f + 0.5f;
  float k = band * (0.4f + 0.6f * n) * (1 - 0.6f * smooth(0.55f, 0.8f, n) * expf(-y * y * 30));
  c[0] = 0.28f * k, c[1] = 0.26f * k, c[2] = 0.3f * k;
}

void world_dunes_build(cw_node root) {
  t3_object *at = cw_node3d_object(root);
  static const float lo[3] = { 0.02f, 0.025f, 0.05f }, hi[3] = { 0.002f, 0.002f, 0.008f };
  DU.sky = sky_card(at, 4000, 1400, (t3_vec3){ 0, 600, -1500 }, lo, hi, 2.5f);
  stars(at, 900, 4000, 20, 1300, -1490, 4.0f, 1301);
  du_n = cw_noise_default(), du_n.seed = 131, du_n.frequency = 0.006f, du_n.fractal = CW_NOISE_FRACTAL_FBM, du_n.octaves = 4;
  du_w = cw_noise_default(), du_w.seed = 132, du_w.frequency = 0.004f, du_w.warp_amp = 40;
  DU.milky = quad(at, 3600, 900, texture_rgb(512, 128, tx_milky), (t3_vec3){ 200, 520, -1470 });
  T3_OBJ(DU.milky)->quaternion = about(0, 0, 1, -0.45f);
  DU.milky->materials[0]->fog = false;
  heightfield(at, 220, 140, -900, 900, 30, -1100, du_field, 0.9f);
  DU.halo = quad(at, 200, 200, texture(128, 128, tx_halo), (t3_vec3){ -420, 90, -1399 });
  DU.moon = quad(at, 24, 24, texture(128, 128, tx_crescent), (t3_vec3){ -420, 90, -1400 }); /* a crescent: a full disc read as a round note */
  tint(DU.moon, 1.0f, 0.96f, 0.88f, 1.8f);
  tint(DU.halo, 0.7f, 0.75f, 1.0f, 0.3f);
}
void world_dunes_update(float beat, float t, float energy) { (void)beat, (void)t, (void)energy; }

/* ── lava ────────────────────────────────────────────────────────────── */

static struct {
  t3_mesh *ground, *flows, *crater, *sky;
} LV;
static cw_noise lv_n, lv_w;
/* the lava rivers: bright channels that wander toward the camera, crusting at their edges (tiles along its length) */
static void tx_lava(float u, float v, float c[3]) {
  float x = u * 512, y = v * 512;
  cw_noise_warp_2d(&lv_w, &x, &y);
  float n = cw_noise_2d(&lv_n, x, y * 0.35f) * 0.5f + 0.5f;
  float ch = smooth(0.62f, 0.72f, n) * (1 - smooth(0.8f, 0.9f, n) * 0.5f);
  float crust = cw_noise_2d(&lv_n, u * 2000, v * 2000) * 0.5f + 0.5f;
  float heat = ch * (0.7f + 0.3f * crust);
  c[0] = 1.0f * heat, c[1] = 0.35f * heat * heat, c[2] = 0.05f * heat * heat * heat;
}
static void tx_basalt(float u, float v, float c[3]) {
  float n = cw_noise_2d(&lv_n, u * 1500, v * 1500) * 0.5f + 0.5f;
  c[0] = 0.06f + 0.05f * n, c[1] = 0.05f + 0.04f * n, c[2] = 0.05f + 0.035f * n;
}
static void lv_volcano(float x, float z, float *h, float c[3]) {
  float dx = x - 330, dz = (z + 900) * 1.2f, d = sqrtf(dx * dx + dz * dz);
  float cone = 230 * powf(fmaxf(0, 1 - d / 520), 1.4f);
  float crater = 30 * expf(-powf(d / 45, 2));
  float n = cw_noise_2d(&lv_n, x * 3, z * 3);
  float range = (20 + 60 * (0.5f + 0.5f * n)) * smooth(-700, -850, z);
  *h = fmaxf(cone - crater, range) + 6 * n;
  /* lava down its flanks: bright streaks along the steepest lines (by angle round the cone) */
  float ang = atan2f(dz, dx);
  float streak = powf(fmaxf(0, cosf(ang * 5 + 1.3f + 0.6f * n)), 18) * smooth(520, 60, d) * (d > 40 ? 1 : 0);
  float glow = streak + 2.0f * expf(-powf(d / 55, 2));
  c[0] = 0.025f + 1.6f * glow, c[1] = 0.012f + 0.45f * glow, c[2] = 0.01f + 0.06f * glow;
}

void world_lava_build(cw_node root) {
  t3_object *at = cw_node3d_object(root);
  lv_n = cw_noise_default(), lv_n.seed = 141, lv_n.frequency = 0.006f, lv_n.fractal = CW_NOISE_FRACTAL_FBM, lv_n.octaves = 4;
  lv_w = cw_noise_default(), lv_w.seed = 142, lv_w.frequency = 0.004f, lv_w.warp_amp = 60;
  static const float lo[3] = { 0.25f, 0.06f, 0.02f }, hi[3] = { 0.01f, 0.004f, 0.004f };
  LV.sky = sky_card(at, 4000, 1400, (t3_vec3){ 0, 600, -1500 }, lo, hi, 4.0f);
  /* the plain: black basalt, its lava rivers an emissive map scrolling toward the camera */
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->roughness = 0.8f;
  t3_texture *b = texture_rgb(256, 256, tx_basalt), *l = texture_rgb(256, 256, tx_lava);
  b->wrap_s = b->wrap_t = T3_REPEAT, l->wrap_s = l->wrap_t = T3_REPEAT;
  b->repeat = (t3_vec2){ 30, 30 }, l->repeat = (t3_vec2){ 8, 6 };
  t3_material_set_map(m, b), t3_release(b);
  m->emissive = (t3_color){ 2.2f, 2.2f, 2.2f };
  t3_material_set_texture(m, T3_EMISSIVE_MAP, l), t3_release(l);
  LV.ground = add_mesh(at, t3_plane_geometry_new(1600, 1200, 1, 1), m, (t3_vec3){ 0, 0, -560 });
  T3_OBJ(LV.ground)->quaternion = about(1, 0, 0, -PI / 2);
  /* the volcano and the ranges behind, its flanks streaked with lava (vertex colours as emissive light) */
  heightfield(at, 200, 80, -900, 900, -500, -1400, lv_volcano, -1); /* unlit: the rock's own dim red, the streaks glowing */
  (void)LV.flows, (void)LV.crater;
}
void world_lava_update(float beat, float t, float energy) {
  (void)energy;
  /* the lava flows toward the camera at a steady creep (a tile every 16 beats: slow) */
  t3_texture *em = LV.ground->materials[0]->emissive_map;
  if (em) em->offset.y = fmodf(beat / 64.0f, 1.0f) * -1.0f;
  (void)t;
}

/* ── metal ───────────────────────────────────────────────────────────── */

#define MT_COLS 24
#define MT_BEAMS 10
#define MT_FLUTES 60
static struct {
  t3_texture *env;
  cw_node sweep[2], key;
} MT;
/* the hall's environment for the reflections: a dark gunmetal room with long soft light strips overhead (cool), a band
 * of warm gold light low round the walls, darker below: what the metal mirrors */
static void mt_face(int f, int x, int y, int n, uint8_t *out) {
  float a = ((float)x + 0.5f) / (float)n * 2 - 1, b = ((float)y + 0.5f) / (float)n * 2 - 1;
  float d[3];
  switch (f) { /* +x, -x, +y, -y, +z, -z (the cube map faces) */
  case 0: d[0] = 1, d[1] = -b, d[2] = -a; break;
  case 1: d[0] = -1, d[1] = -b, d[2] = a; break;
  case 2: d[0] = a, d[1] = 1, d[2] = b; break;
  case 3: d[0] = a, d[1] = -1, d[2] = -b; break;
  case 4: d[0] = a, d[1] = -b, d[2] = 1; break;
  default: d[0] = -a, d[1] = -b, d[2] = -1; break;
  }
  float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  float ux = d[0] / l, uy = d[1] / l, uz = d[2] / l;
  float strips = uy > 0.5f ? powf(fmaxf(0, cosf(atan2f(ux, uz) * 6)), 24) * smooth(0.5f, 0.8f, uy) : 0;
  float gold = 0.45f * expf(-powf((uy - 0.08f) / 0.05f, 2)) * (0.6f + 0.4f * cosf(atan2f(ux, uz) * 3));
  float base = 0.05f + 0.06f * smooth(-0.6f, 0.6f, uy);
  float r = base + 1.1f * strips + 0.9f * gold, g = base + 1.1f * strips + 0.62f * gold, bb = base * 1.15f + 1.2f * strips + 0.25f * gold;
  out[0] = (uint8_t)lrintf(fminf(1, r) * 255), out[1] = (uint8_t)lrintf(fminf(1, g) * 255), out[2] = (uint8_t)lrintf(fminf(1, bb) * 255), out[3] = 255;
}
/* brushed steel plates: fine streaks along the plate, a dark seam round each */
static void tx_plate(float u, float v, float c[3]) {
  float su = u * 4 - floorf(u * 4), sv = v * 4 - floorf(v * 4);
  float seam = 1 - 0.75f * (smooth(0.03f, 0.0f, su) + smooth(0.97f, 1.0f, su) + smooth(0.03f, 0.0f, sv) + smooth(0.97f, 1.0f, sv));
  float streak = 0.88f + 0.12f * (0.5f + 0.5f * sinf(v * 900 + 3 * sinf(u * 40)));
  float k = fmaxf(0, seam) * streak;
  c[0] = 0.62f * k, c[1] = 0.63f * k, c[2] = 0.67f * k;
}
static t3_material *metal_mat(float r, float g, float b, float rough, float aniso) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = (t3_color){ r, g, b }, m->metalness = 1, m->roughness = rough;
  m->anisotropy = aniso;
  t3_material_set_texture(m, T3_ENV_MAP, MT.env);
  m->env_map_intensity = 1.0f;
  return m;
}

void world_metal_build(cw_node root) {
  t3_object *at = cw_node3d_object(root);
  enum { N = 64 };
  uint8_t *faces[6];
  for (int f = 0; f < 6; f++) {
    faces[f] = malloc(N * N * 4);
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++) mt_face(f, x, y, N, faces[f] + (y * N + x) * 4);
  }
  MT.env = t3_cube_texture_new(N, (const uint8_t *const *)faces);
  for (int f = 0; f < 6; f++) free(faces[f]);
  /* the floor: brushed steel plates, the brushing along the hall */
  t3_material *fm = metal_mat(0.5f, 0.52f, 0.56f, 0.14f, 0.8f);
  fm->env_map_intensity = 1.6f;
  t3_texture *pt = texture_rgb(256, 256, tx_plate);
  pt->wrap_s = pt->wrap_t = T3_REPEAT, pt->repeat = (t3_vec2){ 60, 60 };
  t3_material_set_map(fm, pt), t3_release(pt);
  t3_mesh *fl = add_mesh(at, t3_plane_geometry_new(600, 600, 1, 1), fm, (t3_vec3){ 0, 0, -250 });
  T3_OBJ(fl)->quaternion = about(1, 0, 0, -PI / 2);
  /* the columns: polished chrome down both sides, gold rings at their feet and heads */
  t3_material *chrome = metal_mat(0.9f, 0.9f, 0.92f, 0.07f, 0), *gold = metal_mat(1.0f, 0.78f, 0.38f, 0.18f, 0), *gun = metal_mat(0.22f, 0.23f, 0.25f, 0.5f, 0.5f);
  gun->env_map_intensity = 0.55f;
  t3_retain(chrome), t3_retain(gold), t3_retain(gun);
  t3_instanced_mesh *cols = many_m(at, t3_cylinder_geometry_new(1.1f, 1.1f, 1, 24, 1, false), chrome, MT_COLS);
  t3_instanced_mesh *rings = many_m(at, t3_cylinder_geometry_new(1.35f, 1.35f, 1, 24, 1, false), gold, MT_COLS * 2);
  for (int i = 0; i < MT_COLS; i++) {
    float side = (i & 1) ? 1.0f : -1.0f, z = 6 - 32 * (float)(i / 2), x = side * 15, h = 34;
    place(cols, i, (t3_vec3){ x, h * 0.5f, z }, about(0, 1, 0, 0), (t3_vec3){ 1, h, 1 }, (t3_color){ 1, 1, 1 });
    place(rings, i * 2, (t3_vec3){ x, 0.4f, z }, about(0, 1, 0, 0), (t3_vec3){ 1, 0.8f, 1 }, (t3_color){ 1, 1, 1 });
    place(rings, i * 2 + 1, (t3_vec3){ x, h - 0.6f, z }, about(0, 1, 0, 0), (t3_vec3){ 1, 1.2f, 1 }, (t3_color){ 1, 1, 1 });
  }
  done(cols), done(rings);
  /* the beams overhead, gunmetal, column to column across the hall */
  t3_instanced_mesh *beams = many_m(at, t3_box_geometry_new(1, 1, 1, 1, 1, 1), gun, MT_BEAMS);
  for (int i = 0; i < MT_BEAMS; i++) place(beams, i, (t3_vec3){ 0, 33, 6 - 64 * (float)i }, about(0, 1, 0, 0), (t3_vec3){ 32, 1.6f, 1.6f }, (t3_color){ 1, 1, 1 });
  done(beams);
  /* the far wall: fluted gunmetal, its flutes catching the sweeps */
  t3_instanced_mesh *fl2 = many_m(at, t3_box_geometry_new(1, 1, 1, 1, 1, 1), gun, MT_FLUTES);
  for (int i = 0; i < MT_FLUTES; i++) place(fl2, i, (t3_vec3){ -150 + 300 * (float)i / (MT_FLUTES - 1), 40, -440 }, about(0, 1, 0, 0.6f), (t3_vec3){ 3.4f, 80, 3.4f }, (t3_color){ 1, 1, 1 });
  done(fl2);
  t3_release(chrome), t3_release(gold), t3_release(gun);
  MT.sweep[0] = cw_node_get(root, "SweepA"), MT.sweep[1] = cw_node_get(root, "SweepB"), MT.key = cw_node_get(root, "Key");
}

void world_metal_update(float beat, float t, float energy) {
  (void)t;
  float e = 0.8f + 0.35f * energy;
  /* each light crosses the hall over eight bars and back, swelling as it passes the middle (never a flash: seconds) */
  for (int k = 0; k < 2; k++) {
    if (!MT.sweep[k].id) continue;
    float ph = 2 * PI * beat / 32.0f + (float)k * PI;
    float x = 24 * sinf(ph), z = -40 - 50 * (float)k + 20 * cosf(ph * 0.5f);
    cw_node3d_set_position(MT.sweep[k], (t3_vec3){ x, 30, z });
    t3_vec3 d = { -x * 0.4f, -30, 10 };
    float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z), yaw = atan2f(-d.x, -d.z), pitch = asinf(d.y / len);
    cw_node3d_set_rotation(MT.sweep[k], (t3_vec3){ pitch, yaw, 0 });
    cw_node_set(MT.sweep[k], "energy", cw_val_float(1500 * e * (0.75f + 0.25f * cosf(ph))));
  }
}
