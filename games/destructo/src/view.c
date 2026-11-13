/* destructo's view: render only; nothing here feeds the simulation.
 *
 *   blocks     one instanced box per material (wood, stone, glass, steel) with a texture of its own made here
 *              (grain, speckle, brushed paint with rivets, a glass edge), every block an instance scaled to its
 *              size and tinted by the level's palette; crystals are glowing octahedra, each with a light while it
 *              stands
 *   roofs      the roofs' boards (wood, painted) drawn as an instanced box of their own with a slate-course texture
 *   field      the setting's ground with its colour wandering over the field, a worn yard and track blended into it,
 *              a ring of hills or mountains, trees of layered foliage swaying in the wind (trees.c)
 *   launcher   a wheeled carriage that turns and a barrel that raises with the aim
 *   aim path   the first stretch of the shot's flight as bright beads (the same flight the world will fly)
 *   camera     behind the launcher while aiming; after the shot as it flies; side-on to the collapse while it
 *              settles; the wide view while the player holds it; a slow orbit when the level is done */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/math.h"
#include "cartwheel/profile.h"
#include "cartwheel/render3d.h"
#include "../../common/coplanar.h"
#include "opts.h"
#include "trees.h"
#include "view.h"

#define NPATH 48
#define NVAR 4

bool g_view_no_path;

static struct {
  cw_node root, field, sky, sun, post;
  t3_instanced_mesh *poles, *flags, *beams;
  t3_instanced_mesh *blk[M_COUNT][NVAR]; /* a material's blocks drawn as NVAR variants of its face (a wall of blocks does not repeat one picture) */
  int nvar[M_COUNT];
  t3_instanced_mesh *water, *pool; /* the lake's parcels, and the pool they leave (water.c) */
  cw_node lake;             /* the lake's surface for this level, under the field (freed with it) */
  t3_instanced_mesh *path, *roof, *curtain; /* curtain: a tower's tinted glass (glass painted dark or blue): no lamps behind it, the sky in it */
  float wind;
  cw_node crystal_light[MAXTGT];
  cw_node launcher, yaw, barrel;
  cw_node shot;
  t3_material *shot_mat;
  int shot_kind;
  t3_vec3 eye, at;
  float fov, aspect;
  bool cam_init;
  int last_state;
  float orbit;
  float rp_orbit;    /* the replay camera's turn round the impact */
  float beam_w;      /* the beacons' width: narrower on a landmark (a tower's silhouette shows through) */
  float cliff;       /* the sea setting: the z of the headland's cliff, clear of the launcher's circle */
  float front;       /* the structure's near face toward the launcher (game_front), for the aim path */
  float front_orbit; /* ... at this orbit */
  int front_level;   /* the level (+1) front was found for */
  int built_level;
  uint8_t lay[MAXBLK][3]; /* each block's drawn layers along its own axes (block_layers) */
  int lay_born[MAXBLK]; /* the block (its born tick + 1) the layer was given to: 0 none */
  int lay_settled;      /* the tick the level's blocks are coloured again, settled (0: done) */

  float pal[M_COUNT][3];
  view_stats st;
} V;

static const t3_mat4 ZERO;

/* ── textures, made here ───────────────────────────────────────────── */

static float h2(int x, int y, int s) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) & 0xffffff) / 16777216.0f;
}
static float vnoise(float x, float y, int s) {
  int xi = (int)floorf(x), yi = (int)floorf(y);
  float fx = x - xi, fy = y - yi;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = h2(xi, yi, s), b = h2(xi + 1, yi, s), c = h2(xi, yi + 1, s), d = h2(xi + 1, yi + 1, s);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float x, float y, int s) {
  float v = 0, a = 0.5f;
  for (int i = 0; i < 5; i++) v += a * vnoise(x, y, s + i), x *= 2.03f, y *= 2.03f, a *= 0.5f;
  return v / 0.97f;
}
static uint8_t u8(float v) { return (uint8_t)(cw_clampf(v, 0, 1) * 255 + 0.5f); }

/* a block face: luminance detail (the instance colour gives the hue), darker at the edges, and a normal map made from the
 * same height field (grain, pits, hairline cracks, bedding, a chipped edge, brushed metal, wood grain), `seed` a variant of
 * the face so a wall of blocks does not repeat one picture */
static float ridge(float v, float w) { return 1 - cw_smoothstep(0, w, fabsf(v - 0.5f)); } /* 1 on a thin line where noise crosses its middle */
static void block_texture(int mat, int seed, t3_texture **albedo, t3_texture **normal) {
  enum { N = 256 };
  static uint8_t px[N * N * 4], nm[N * N * 4];
  static float H[N * N];
  static float AO[N * N];
  const int s = seed * 53;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
      float e = fminf(fminf(u, 1 - u), fminf(v, 1 - v));
      float edge = 0.62f + 0.38f * cw_smoothstep(0.0f, 0.07f, e);
      float r = 1, g = 1, b = 1, a = 1, h = 0, ao = 1;
      switch (mat) {
      case M_WOOD: {
        float n = fbm(u * 3, v * 24, 11 + s);
        float grain = 0.5f + 0.5f * sinf((u * 7 + n * 2.2f) * 6.2831853f * 2.5f);
        float k = 0.72f + 0.2f * grain + 0.12f * (fbm(u * 18, v * 3, 5 + s) - 0.5f);
        float kd = sqrtf((u - 0.63f) * (u - 0.63f) * 9 + (v - 0.35f) * (v - 0.35f));
        float knot = 1 - 0.35f * cw_smoothstep(0.06f, 0.0f, kd - 0.02f);
        float fine = vnoise(u * 8, v * 160, 61 + s); /* the pores, along the grain */
        k *= 0.94f + 0.1f * fine;
        h = 0.5f * grain + 0.25f * fine + 0.3f * knot + 0.1f * (fbm(u * 18, v * 3, 5 + s));
        r = g = b = k * knot * edge;
        ao = 0.8f + 0.2f * grain;
        break;
      }
      case M_STONE: {
        float n = fbm(u * 6, v * 6, 21 + s), sp = vnoise(u * 60, v * 60, 3 + s);
        float n2 = fbm(u * 18, v * 18, 9 + s), gr = vnoise(u * 150, v * 150, 71 + s), gr2 = vnoise(u * 90 + 5, v * 90, 73 + s);
        float pit = sp > 0.86f ? 0.78f : 1;
        float chip = 1 - 0.25f * cw_smoothstep(0.035f, 0.0f, e + (n2 - 0.5f) * 0.04f);
        float bed = 0.94f + 0.06f * sinf((v * 5 + fbm(u * 4, v * 2, 13 + s) * 1.5f) * 6.2831853f); /* the stone's bedding */
        float crack = ridge(fbm(u * 4.5f + 3, v * 3.2f, 33 + s), 0.018f) * cw_smoothstep(0.52f, 0.62f, fbm(u * 2, v * 2, 35 + s)); /* hairline cracks, only in some places */
        float streak = fbm(u * 2.2f, v * 0.7f, 37 + s); /* mineral streaks running down the face */
        float mica = gr > 0.9f ? 1.28f : gr2 < 0.07f ? 0.7f : 1; /* feldspar glints, dark hornblende flecks */
        float k = (0.74f + 0.3f * (n - 0.5f) * 2 + 0.22f * (streak - 0.5f)) * pit * chip * bed * mica * (1 - 0.55f * crack);
        h = 0.45f * n + 0.3f * n2 + 0.18f * gr + 0.1f * gr2 - (sp > 0.86f ? 0.3f : 0) - 0.5f * crack + 0.06f * sinf((v * 5 + fbm(u * 4, v * 2, 13 + s) * 1.5f) * 6.2831853f);
        h -= 1.2f * (1 - cw_smoothstep(0.0f, 0.07f, e + (n2 - 0.5f) * 0.03f)); /* the edge worn down */
        ao = 0.82f + 0.18f * n2;
        r = k * edge, g = k * edge, b = k * edge;
        r *= 1 + 0.06f * (streak - 0.5f), b *= 1 - 0.06f * (streak - 0.5f);
        break;
      }
      case M_STEEL: {
        float brush = 0.92f + 0.08f * vnoise(u * 3, v * 120, 31 + s);
        float k = brush * (0.9f + 0.1f * fbm(u * 8, v * 8, 4 + s));
        h = 0.4f * vnoise(u * 3, v * 160, 31 + s) + 0.2f * fbm(u * 8, v * 8, 4 + s);
        /* rivets near the corners and along the edges */
        for (int i = 0; i < 4; i++) {
          float cx = i & 1 ? 0.91f : 0.09f, cy = i & 2 ? 0.91f : 0.09f;
          float d = sqrtf((u - cx) * (u - cx) + (v - cy) * (v - cy));
          if (d < 0.035f) k *= 0.6f + 0.6f * (1 - d / 0.035f), h += 0.9f * (1 - d / 0.035f);
        }
        float seam = cw_smoothstep(0.006f, 0.0f, fabsf(u - 0.5f)); /* a panel seam down the face */
        k *= 1 - 0.25f * seam, h -= 0.8f * seam;
        float wear = fbm(u * 10, v * 10, 41 + s) > 0.72f ? 0.55f : 1; /* paint worn to the metal */
        h -= wear < 1 ? 0.25f : 0;
        h -= 1.0f * (1 - cw_smoothstep(0.0f, 0.04f, e));
        r = g = b = k * edge;
        if (wear < 1) r *= 0.75f, g *= 0.75f, b *= 0.8f;
        break;
      }
      case M_GLASS: {
        float frame = cw_smoothstep(0.05f, 0.03f, e);
        float streak = 0.08f * cw_smoothstep(0.02f, 0.0f, fabsf(u - v * 0.6f - 0.25f));
        r = g = b = 0.75f + 0.25f * frame + streak;
        a = 0.35f + 0.55f * frame;
        break;
      }
      default: r = g = b = edge; break;
      }
      uint8_t *p = &px[(y * N + x) * 4];
      p[0] = u8(r * ao), p[1] = u8(g * ao), p[2] = u8(b * ao), p[3] = u8(a);
      H[y * N + x] = h;
      AO[y * N + x] = ao;
    }
  t3_texture *t = t3_texture_new(N, N, px);
  t->color_space = T3_SRGB_COLOR_SPACE;
  t->anisotropy = 4;
  *albedo = t;
  *normal = NULL;
  if (mat == M_GLASS) return;
  const float strength = mat == M_STEEL ? 2.0f : mat == M_WOOD ? 3.0f : 4.5f; /* the slope of the height field in the normal */
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      int xl = x > 0 ? x - 1 : x, xr = x < N - 1 ? x + 1 : x, yu = y > 0 ? y - 1 : y, yd = y < N - 1 ? y + 1 : y;
      float dx = (H[y * N + xr] - H[y * N + xl]) * strength, dy = (H[yd * N + x] - H[yu * N + x]) * strength;
      float il = 1.0f / sqrtf(dx * dx + dy * dy + 1);
      uint8_t *p = &nm[(y * N + x) * 4];
      p[0] = u8(0.5f - 0.5f * dx * il), p[1] = u8(0.5f + 0.5f * dy * il), p[2] = u8(0.5f + 0.5f * il), p[3] = 255; /* v runs down the image, up the face's v */
    }
  t3_texture *nt = t3_texture_new(N, N, nm);
  nt->color_space = T3_LINEAR_SRGB_COLOR_SPACE;
  nt->anisotropy = 4;
  *normal = nt;
}

/* the roofs' boards: courses of slates or tiles across the face, each course shaded under the one above it */
static t3_texture *roof_texture(void) {
  enum { N = 128 };
  static uint8_t px[N * N * 4];
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
      float e = fminf(fminf(u, 1 - u), fminf(v, 1 - v));
      float cv = v * 2, f = cv - floorf(cv);
      float k = (0.62f + 0.38f * f) * (f < 0.06f ? 0.55f : 1) * (0.9f + 0.2f * fbm(u * 3, floorf(cv) * 7 + v, 17));
      k *= 0.7f + 0.3f * cw_smoothstep(0.0f, 0.05f, e);
      uint8_t *p = &px[(y * N + x) * 4];
      p[0] = p[1] = p[2] = u8(k), p[3] = 255;
    }
  t3_texture *t = t3_texture_new(N, N, px);
  t->color_space = T3_SRGB_COLOR_SPACE;
  t->anisotropy = 4;
  return t;
}

/* ground: tiling noise round a base colour (sRGB), blades or grit, green grounds a little yellow in places */
static t3_texture *ground_texture(const float *c, int seed) {
  enum { N = 512 };
  static uint8_t px[N * N * 4];
  bool dirt = seed != 0, green = c[1] > c[0] && c[1] > c[2];
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float u = (float)x / N, v = (float)y / N;
      /* tiling noise: sample on a torus of periods that divide the texture */
      float n = 0, a = 0.5f, f = 4;
      for (int o = 0; o < 5; o++) {
        float fx = u * f, fy = v * f;
        int p = (int)f;
        int xi = (int)floorf(fx), yi = (int)floorf(fy);
        float tx = fx - xi, ty = fy - yi;
        tx = tx * tx * (3 - 2 * tx), ty = ty * ty * (3 - 2 * ty);
        float aa = h2(xi % p, yi % p, o + seed * 10), bb = h2((xi + 1) % p, yi % p, o + seed * 10);
        float cc = h2(xi % p, (yi + 1) % p, o + seed * 10), dd = h2((xi + 1) % p, (yi + 1) % p, o + seed * 10);
        n += a * (aa + (bb - aa) * tx + (cc - aa) * ty + (aa - bb - cc + dd) * tx * ty);
        f *= 2, a *= 0.5f;
      }
      float blade = h2(x, y, 7);
      float k = 0.72f + 0.45f * (n - 0.5f) + (dirt ? 0.1f * (blade - 0.5f) : 0.22f * (blade - 0.5f));
      float yel = green ? cw_smoothstep(0.55f, 0.75f, n) * 0.25f : 0;
      uint8_t *p = &px[(y * N + x) * 4];
      p[0] = u8(c[0] * k + yel * 0.3f), p[1] = u8(c[1] * k + yel * 0.2f), p[2] = u8(c[2] * k), p[3] = 255;
    }
  t3_texture *t = t3_texture_new(N, N, px);
  t->color_space = T3_SRGB_COLOR_SPACE;
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->anisotropy = 8;
  return t;
}

/* ── helpers ───────────────────────────────────────────────────────── */

static t3_material *std_mat(float r, float g, float b, float rough, float metal) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = (t3_color){ r, g, b };
  m->roughness = rough, m->metalness = metal;
  return m;
}

static cw_node piece(cw_node parent, const char *name, t3_geometry *g, t3_material *m, bool cast) {
  cw_node n = cw_node_new("Node3D", name);
  cw_node_add_child(parent, n);
  t3_object *grp = t3_group_new();
  t3_mesh *mesh = t3_mesh_new(g, m);
  mesh->base.cast_shadow = cast, mesh->base.receive_shadow = true;
  t3_object_add(grp, mesh);
  cw_node3d_set_object(n, grp);
  t3_release(mesh), t3_release(grp), t3_release(g), t3_release(m);
  return n;
}

static t3_instanced_mesh *imesh(cw_node parent, const char *name, t3_geometry *g, t3_material *m, int n, bool cast) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  im->mesh.base.cast_shadow = cast, im->mesh.base.receive_shadow = true;
  for (int i = 0; i < n; i++) {
    t3_instanced_mesh_set_matrix_at(im, i, &ZERO);
    t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 });
  }
  im->instance_matrix->usage = T3_DYNAMIC_DRAW;
  im->instance_color->usage = T3_DYNAMIC_DRAW;
  t3_sphere sph = { { 0, 4, 0 }, 120 };
  t3_instanced_mesh_set_bounding_sphere(im, &sph);
  cw_node node = cw_node_new("Node3D", name);
  cw_node_add_child(parent, node);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, &im->mesh.base);
  cw_node3d_set_object(node, wrap);
  t3_release(wrap), t3_release(g), t3_release(m);
  im->count = 0;
  return im;
}

static cw_node omni(cw_node parent, t3_color c, float energy, float range) {
  cw_node l = cw_node_new("OmniLight3D", "Light");
  cw_node_add_child(parent, l);
  cw_node_set(l, "color", cw_val_color(c.r, c.g, c.b, 1));
  cw_node_set(l, "energy", cw_val_float(energy));
  cw_node_set(l, "range", cw_val_float(range));
  cw_node_set(l, "decay", cw_val_float(2));
  return l;
}

static t3_geometry *xform_geo(t3_geometry *g, t3_vec3 p, t3_quat q, t3_vec3 s) {
  t3_mat4 m;
  t3_mat4_compose(&m, p, q, s);
  t3_geometry_apply_matrix4(g, &m);
  return g;
}
static t3_quat qaxis(float x, float y, float z, float a) { return t3_quat_from_axis_angle((t3_vec3){ x, y, z }, a); }

/* ── building ──────────────────────────────────────────────────────── */

void view_init(cw_node root) {
  memset(&V, 0, sizeof V);
  V.root = root;
  V.built_level = -1;
  V.fov = 50;
  V.sky = cw_node_get(root, "Main/Sky");
  V.sun = cw_node_get(root, "Main/Sun");
  V.post = cw_node_get(root, "Main/Post");
  cw_node world = cw_node_new("Node3D", "Blocks");
  cw_node_add_child(root, world);
  for (int m = 0; m < M_COUNT; m++) {
    t3_material *mt;
    t3_geometry *geo;
    if (m == M_TARGET) {
      mt = std_mat(1, 0.3f, 0.85f, 0.15f, 0.1f);
      mt->emissive = (t3_color){ 1, 0.25f, 0.8f };
      mt->emissive_intensity = 4.5f; /* they glow past white into the bloom: they read through a structure */
      geo = t3_octahedron_geometry_new(0.5f, 0);
      xform_geo(geo, (t3_vec3){ 0, 0, 0 }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1.4f, 1.0f, 1.4f });
    }
    static const float rough[M_COUNT] = { 0.78f, 0.88f, 0.06f, 0.42f, 0 };
    static const float metal[M_COUNT] = { 0, 0, 0, 0.55f, 0 };
    if (m == M_TARGET) { V.nvar[m] = 1, V.blk[m][0] = imesh(world, MATS[m].name, geo, mt, MAXBLK, true); continue; }
    V.nvar[m] = m == M_STONE ? 4 : m == M_GLASS ? 1 : 2;
    for (int vi = 0; vi < V.nvar[m]; vi++) {
      t3_material *vm = std_mat(1, 1, 1, rough[m], metal[m]);
      t3_texture *tx, *nx;
      block_texture(m, vi, &tx, &nx);
      t3_material_set_map(vm, tx);
      t3_release(tx);
      if (nx) t3_material_set_texture(vm, T3_NORMAL_MAP, nx), t3_release(nx);
      if (m == M_GLASS) {
        vm->transparent = true;
        vm->opacity = 1;
        vm->depth_write = false;
        vm->emissive = (t3_color){ 1.0f, 0.62f, 0.28f }; /* lit windows: a lamp's warm light inside */
        vm->emissive_intensity = 1.6f;
      }
      V.blk[m][vi] = imesh(world, MATS[m].name, t3_box_geometry_new(1, 1, 1, 1, 1, 1), vm, MAXBLK, m != M_GLASS);
    }
  }
  {
    t3_material *rm = std_mat(1, 1, 1, 0.66f, 0); /* glazed tile and slate: the sky shows in them (they went black in flight) */
    t3_texture *tx = roof_texture();
    t3_material_set_map(rm, tx);
    t3_release(tx);
    V.roof = imesh(world, "Roofs", t3_box_geometry_new(1, 1, 1, 1, 1, 1), rm, MAXBLK / 4, true);
    t3_material *cm = std_mat(1, 1, 1, 0.1f, 0.65f);
    t3_texture *ct, *cn;
    block_texture(M_GLASS, 0, &ct, &cn);
    t3_material_set_map(cm, ct);
    t3_release(ct);
    V.curtain = imesh(world, "CurtainGlass", t3_box_geometry_new(1, 1, 1, 1, 1, 1), cm, MAXBLK / 2, true);
  }
  { /* the water's parcels: small glossy spheres, foam-white where they run fast */
    t3_material *wm = std_mat(1, 1, 1, 0.07f, 0);
    wm->emissive = (t3_color){ 0.05f, 0.16f, 0.3f };
    wm->emissive_intensity = 0.6f;
    V.water = imesh(world, "Water", t3_sphere_geometry_new(0.5f, 10, 6), wm, MAXWATER, false);
    t3_material *pm = std_mat(0.07f, 0.33f, 0.58f, 0.04f, 0);
    pm->emissive = (t3_color){ 0.02f, 0.1f, 0.2f };
    pm->emissive_intensity = 0.5f;
    V.pool = imesh(world, "Pool", t3_cylinder_geometry_new(0.5f, 0.5f, 1, 20, 1, false), pm, WATER_POOL + 8, false);
  }
  trees_init();
  for (int i = 0; i < MAXTGT; i++) {
    V.crystal_light[i] = omni(world, (t3_color){ 1, 0.3f, 0.85f }, 0, 6);
    cw_node3d_set_position(V.crystal_light[i], (t3_vec3){ 0, -50, 0 });
  }
  /* banners: a pole and a flag that waves, each carried by a block */
  {
    t3_material *pm = std_mat(0.85f, 0.7f, 0.35f, 0.35f, 0.8f);
    V.poles = imesh(world, "Poles", xform_geo(t3_cylinder_geometry_new(0.5f, 0.5f, 1, 6, 1, false), (t3_vec3){ 0, 0.5f, 0 }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 }), pm, MAXBANNER, true);
    t3_material *fm = std_mat(1, 1, 1, 0.9f, 0);
    fm->side = T3_DOUBLE_SIDE;
    fm->emissive = (t3_color){ 1, 1, 1 };
    fm->emissive_intensity = 0.12f; /* cloth with the sun through it */
    /* a flag: a plane from the pole out along +x, a swallowtail cut by two triangles */
    t3_geometry *fg = t3_plane_geometry_new(1, 1, 6, 1);
    xform_geo(fg, (t3_vec3){ 0.5f, 0, 0 }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 });
    V.flags = imesh(world, "Flags", fg, fm, MAXBANNER, true);
  }
  /* the crystals' beacons: a column of light above each standing crystal, seen over any wall */
  {
    t3_material *bm = t3_mesh_basic_material_new(0xffffff);
    bm->color = (t3_color){ 1, 0.3f, 0.85f };
    bm->transparent = true, bm->opacity = 0.28f, bm->depth_write = false, bm->blending = T3_ADDITIVE_BLENDING;
    bm->side = T3_DOUBLE_SIDE;
    t3_geometry *bg = xform_geo(t3_cylinder_geometry_new(0.25f, 0.6f, 1, 12, 1, true), (t3_vec3){ 0, 0.5f, 0 }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 });
    V.beams = imesh(world, "Beacons", bg, bm, MAXTGT, false);
  }
  /* the aim path's beads */
  {
    t3_material *pm = std_mat(1, 0.9f, 0.6f, 0.5f, 0);
    pm->emissive = (t3_color){ 1, 0.85f, 0.45f };
    pm->emissive_intensity = 3.0f;
    V.path = imesh(root, "AimPath", t3_sphere_geometry_new(1, 10, 6), pm, NPATH, false);
  }
  /* the shot */
  V.shot_mat = std_mat(0.12f, 0.12f, 0.14f, 0.35f, 0.85f);
  t3_retain(V.shot_mat);
  V.shot = piece(root, "Shot", t3_sphere_geometry_new(1, 24, 16), V.shot_mat, true);
  cw_node3d_set_position(V.shot, (t3_vec3){ 0, -50, 0 });
}

static void build_launcher(const game *g) {
  V.launcher = cw_node_new("Node3D", "Launcher");
  cw_node_add_child(V.field, V.launcher);
  cw_node3d_set_position(V.launcher, (t3_vec3){ g->muzzle.x, 0, g->muzzle.z });
  cw_node3d_set_rotation(V.launcher, (t3_vec3){ 0, g->orbit, 0 }); /* facing the structure's middle from its place on the circle */
  V.yaw = cw_node_new("Node3D", "Yaw");
  cw_node_add_child(V.launcher, V.yaw);
  /* a carriage: two cheeks on an axle, four spoked-looking wheels, a trail behind */
  piece(V.yaw, "Bed", xform_geo(t3_box_geometry_new(1.1f, 0.3f, 2.6f, 1, 1, 1), (t3_vec3){ 0, 0.55f, 0.5f }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 }), std_mat(0.55f, 0.3f, 0.12f, 0.7f, 0), true);
  for (int s = -1; s <= 1; s += 2) {
    piece(V.yaw, "Cheek", xform_geo(t3_box_geometry_new(0.16f, 0.9f, 1.4f, 1, 1, 1), (t3_vec3){ s * 0.48f, 1.1f, 0 }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 }), std_mat(0.62f, 0.34f, 0.14f, 0.7f, 0), true);
    for (int k = 0; k < 2; k++) {
      t3_geometry *wg = t3_cylinder_geometry_new(0.55f, 0.55f, 0.16f, 20, 1, false);
      piece(V.yaw, "Wheel", xform_geo(wg, (t3_vec3){ s * 0.68f, 0.55f, k ? 1.4f : -0.2f }, qaxis(0, 0, 1, 1.5707963f), (t3_vec3){ 1, 1, 1 }), std_mat(0.85f, 0.18f, 0.12f, 0.6f, 0.1f), true);
      t3_geometry *hub = t3_cylinder_geometry_new(0.14f, 0.14f, 0.22f, 12, 1, false);
      piece(V.yaw, "Hub", xform_geo(hub, (t3_vec3){ s * 0.7f, 0.55f, k ? 1.4f : -0.2f }, qaxis(0, 0, 1, 1.5707963f), (t3_vec3){ 1, 1, 1 }), std_mat(0.9f, 0.75f, 0.3f, 0.35f, 0.8f), true);
    }
  }
  V.barrel = cw_node_new("Node3D", "Barrel");
  cw_node_add_child(V.yaw, V.barrel);
  cw_node3d_set_position(V.barrel, (t3_vec3){ 0, g->muzzle.y, 0 });
  t3_geometry *bg = t3_cylinder_geometry_new(0.3f, 0.38f, 2.6f, 24, 1, false);
  piece(V.barrel, "Tube", xform_geo(bg, (t3_vec3){ 0, 0, -0.3f }, qaxis(1, 0, 0, -1.5707963f), (t3_vec3){ 1, 1, 1 }), std_mat(0.16f, 0.2f, 0.28f, 0.3f, 0.85f), true);
  t3_geometry *rg = t3_torus_geometry_new(0.33f, 0.07f, 10, 24, 6.2831853f);
  piece(V.barrel, "Muzzle", xform_geo(rg, (t3_vec3){ 0, 0, -1.6f }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 }), std_mat(0.95f, 0.72f, 0.25f, 0.3f, 0.9f), true);
  t3_geometry *bk = t3_sphere_geometry_new(0.42f, 20, 12);
  piece(V.barrel, "Breech", xform_geo(bk, (t3_vec3){ 0, 0, 1.0f }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 }), std_mat(0.16f, 0.2f, 0.28f, 0.3f, 0.85f), true);
}

/* ── the settings: each landmark stands somewhere of its own ─────────────── */

enum { BD_HILLS, BD_MOUNTAINS, BD_MESAS, BD_SEA, BD_CITY, BD_HARBOUR };
typedef struct setting {
  float tod, north, sun_energy, sky_energy, exposure;
  float ground[3], yard[3];
  float fog[3], fog_density;
  int backdrop;
  float back_low[3], back_high[3];
  /* the materials' colours here (linear): wood, stone, glass (lit windows), steel */
  float pal[4][3];
  float wander;      /* how far the ground's colour wanders over the field: lush and dry, drifts and hollows */
  float relief;      /* metres the ground rolls past the yard (the field of play stays flat) */
  int sp[2];         /* the trees: two species (trees.h SP_*), the second's share of them, how many */
  float sp_share;
  int ntrees;
  float leaf[2][3];  /* their foliage */
} setting;

static const setting SETTINGS[ST_COUNT] = {
  /* highland: a highland valley under grey-blue mountains; oaks and a few spruces */
  [ST_HIGHLAND] = { 15.7f, 55, 4.6f, 1.6f, 1.85f, { 0.18f, 0.37f, 0.08f }, { 0.36f, 0.27f, 0.17f }, { 0.55f, 0.65f, 0.8f }, 0.0012f,
                BD_MOUNTAINS, { 0.06f, 0.1f, 0.08f }, { 0.85f, 0.88f, 0.92f },
                { { 0.5f, 0.3f, 0.15f }, { 0.4f, 0.31f, 0.2f }, { 1.0f, 0.82f, 0.5f }, { 0.7f, 0.16f, 0.1f } },
                0.25f, 2.5f, { SP_OAK, SP_SPRUCE }, 0.3f, 90, { { 0.13f, 0.27f, 0.05f }, { 1, 1, 1 } } },
  /* meadow: a golden meadow, low hills, autumn oaks and birches */
  [ST_MEADOW] = { 16.7f, 35, 4.6f, 1.6f, 1.8f, { 0.33f, 0.43f, 0.1f }, { 0.4f, 0.29f, 0.16f }, { 0.75f, 0.65f, 0.5f }, 0.0016f,
               BD_HILLS, { 0.18f, 0.32f, 0.1f }, { 0.36f, 0.42f, 0.16f },
               { { 0.52f, 0.32f, 0.16f }, { 0.56f, 0.44f, 0.29f }, { 1.0f, 0.8f, 0.45f }, { 0.2f, 0.4f, 0.9f } },
               0.28f, 2.0f, { SP_OAK, SP_BIRCH }, 0.35f, 90, { { 0.55f, 0.22f, 0.03f }, { 0.55f, 0.42f, 0.05f } } },
  /* headland: a headland over the sea, the cliffs falling away behind; oaks bent by the wind, spruces */
  [ST_HEADLAND] = { 15.9f, 50, 4.6f, 1.6f, 1.5f, { 0.19f, 0.4f, 0.09f }, { 0.42f, 0.35f, 0.25f }, { 0.65f, 0.78f, 0.92f }, 0.0014f,
                 BD_SEA, { 0.32f, 0.5f, 0.2f }, { 0.4f, 0.55f, 0.25f },
                 { { 0.5f, 0.3f, 0.15f }, { 0.58f, 0.54f, 0.47f }, { 1.0f, 0.85f, 0.5f }, { 0.2f, 0.3f, 0.6f } },
                 0.3f, 0, { SP_OAK, SP_SPRUCE }, 0.25f, 90, { { 0.12f, 0.25f, 0.06f }, { 1, 1, 1 } } },
  /* desert: pale desert sand under red mesas, date palms in groves */
  [ST_DESERT] = { 16.6f, 40, 4.6f, 1.6f, 1.52f, { 0.84f, 0.74f, 0.58f }, { 0.66f, 0.54f, 0.4f }, { 0.8f, 0.72f, 0.62f }, 0.0007f,
                  BD_MESAS, { 0.09f, 0.028f, 0.014f }, { 0.3f, 0.12f, 0.055f },
                  { { 0.5f, 0.31f, 0.15f }, { 0.62f, 0.5f, 0.33f }, { 0.4f, 0.95f, 0.9f }, { 0.75f, 0.2f, 0.12f } },
                  0.08f, 3.5f, { SP_PALM, SP_PALM }, 0, 30, { { 0.2f, 0.32f, 0.07f }, { 0.2f, 0.32f, 0.07f } } },
  /* snow: a snowy pass between white peaks over dark rock, snow-laden firs, a cold light */
  [ST_SNOW] = { 14.2f, 75, 4.6f, 1.6f, 0.95f, { 0.7f, 0.74f, 0.82f }, { 0.58f, 0.56f, 0.56f }, { 0.75f, 0.82f, 0.95f }, 0.0012f,
               BD_MOUNTAINS, { 0.04f, 0.045f, 0.06f }, { 0.78f, 0.8f, 0.86f },
               { { 0.48f, 0.28f, 0.14f }, { 0.42f, 0.42f, 0.45f }, { 1.0f, 0.75f, 0.35f }, { 0.75f, 0.15f, 0.1f } },
               0.22f, 3.0f, { SP_FIR_SNOW, SP_FIR_SNOW }, 0, 72, { { 1, 1, 1 }, { 1, 1, 1 } } },
  /* city: a paved square in a park, plane trees, a skyline of towers all round in the haze */
  [ST_CITY] = { 15.6f, 60, 4.6f, 1.6f, 1.6f, { 0.2f, 0.36f, 0.1f }, { 0.6f, 0.57f, 0.52f }, { 0.68f, 0.72f, 0.8f }, 0.0015f,
                BD_CITY, { 0.16f, 0.17f, 0.2f }, { 0.42f, 0.45f, 0.5f },
                { { 0.5f, 0.31f, 0.16f }, { 0.55f, 0.52f, 0.47f }, { 1.0f, 0.82f, 0.5f }, { 0.35f, 0.37f, 0.4f } },
                0.2f, 0, { SP_OAK, SP_BIRCH }, 0.3f, 60, { { 0.16f, 0.3f, 0.07f }, { 0.22f, 0.36f, 0.08f } } },
  /* harbour: a paved point out in the water, the city's towers across the bay */
  [ST_HARBOUR] = { 15.8f, 50, 4.6f, 1.6f, 1.55f, { 0.2f, 0.36f, 0.1f }, { 0.58f, 0.55f, 0.5f }, { 0.68f, 0.76f, 0.86f }, 0.0012f,
                   BD_HARBOUR, { 0.16f, 0.17f, 0.2f }, { 0.42f, 0.45f, 0.5f },
                   { { 0.5f, 0.31f, 0.16f }, { 0.55f, 0.52f, 0.47f }, { 1.0f, 0.82f, 0.5f }, { 0.35f, 0.37f, 0.4f } },
                   0.15f, 0, { SP_PALM, SP_PALM }, 0, 0, { { 0.2f, 0.32f, 0.07f }, { 0.2f, 0.32f, 0.07f } } },
};
/* how much of the contrast grade (assets/grade/contrast.cube) each setting takes */
static const float GRADE[ST_COUNT] = { [ST_HIGHLAND] = 0.55f, [ST_MEADOW] = 0.55f, [ST_HEADLAND] = 0.65f,
                                       [ST_DESERT] = 1.0f, [ST_SNOW] = 0.95f, [ST_CITY] = 0.7f, [ST_HARBOUR] = 0.7f };
static const setting *SET;

/* the paints (game.h P_*), linear colours */
static const float PAINT[P_COUNT][3] = {
  [P_ROOF_RED] = { 0.42f, 0.1f, 0.045f }, [P_ROOF_SLATE] = { 0.075f, 0.09f, 0.125f }, [P_ROOF_COPPER] = { 0.1f, 0.36f, 0.3f },
  [P_TRIM_CREAM] = { 0.72f, 0.6f, 0.4f }, [P_TIMBER] = { 0.12f, 0.065f, 0.035f }, [P_PLASTER] = { 0.72f, 0.66f, 0.54f },
  [P_GOLD] = { 1.0f, 0.68f, 0.12f }, [P_TURQUOISE] = { 0.04f, 0.42f, 0.46f }, [P_BRICK] = { 0.45f, 0.15f, 0.08f },
  [P_DARK_STONE] = { 0.2f, 0.2f, 0.23f }, [P_SAND_STONE] = { 0.68f, 0.5f, 0.3f }, [P_ROSE_STONE] = { 0.62f, 0.38f, 0.32f },
  [P_WHITE_STONE] = { 0.66f, 0.63f, 0.56f },
  [P_MARBLE] = { 0.8f, 0.78f, 0.74f }, [P_LIMESTONE] = { 0.6f, 0.55f, 0.45f }, [P_BRONZE] = { 0.13f, 0.095f, 0.07f },
  [P_DARK_GLASS] = { 0.05f, 0.055f, 0.065f }, [P_TILE_WHITE] = { 0.84f, 0.82f, 0.77f }, [P_RED_BRICK] = { 0.52f, 0.15f, 0.09f },
  [P_WHITE_PAINT] = { 0.76f, 0.76f, 0.74f }, [P_BLUE_GLASS] = { 0.16f, 0.26f, 0.36f }, [P_ROCK] = { 0.27f, 0.25f, 0.23f },
  [P_SILVER] = { 0.5f, 0.53f, 0.57f }, [P_SARSEN] = { 0.46f, 0.47f, 0.42f }, [P_VERDIGRIS] = { 0.36f, 0.58f, 0.5f }, [P_CONCRETE] = { 0.58f, 0.57f, 0.54f },
  [P_SHELL_WHITE] = { 0.95f, 0.94f, 0.9f }, [P_PODIUM] = { 0.4f, 0.28f, 0.2f }, [P_IRON] = { 0.2f, 0.13f, 0.09f },
  [P_GRANITE] = { 0.13f, 0.125f, 0.115f }, [P_GRANITE_WARM] = { 0.17f, 0.135f, 0.1f }, [P_GRANITE_COOL] = { 0.11f, 0.115f, 0.125f }, /* mountain rock: the light is strong, a paint this dark shows mid grey */
};
static const float BANNER[4][3] = { { 0.85f, 0.08f, 0.06f }, { 0.08f, 0.22f, 0.8f }, { 1.0f, 0.72f, 0.05f }, { 0.08f, 0.55f, 0.2f } };

/* the backdrop: a ring of land far out, its shape by kind, coloured from low to high (vertex colours) */
static float terrace(float h, float step) { float k = floorf(h / step); float f = h / step - k; return (k + cw_smoothstep(0.7f, 0.95f, f)) * step; }
static t3_geometry *backdrop_geo(const setting *S, uint32_t seed) {
  enum { NA = 160, NR = 16 };
  float r_in = S->backdrop == BD_MOUNTAINS ? 150 : 110;
  float r_out = S->backdrop == BD_MOUNTAINS ? 440 : 320;
  float *pos = malloc(sizeof(float) * 3 * NA * NR), *col = malloc(sizeof(float) * 3 * NA * NR), *uv = malloc(sizeof(float) * 2 * NA * NR);
  unsigned *idx = malloc(sizeof(unsigned) * 6 * NA * (NR - 1));
  for (int r = 0; r < NR; r++)
    for (int a = 0; a < NA; a++) {
      float ang = a * 6.2831853f / (NA - 1), t = (float)r / (NR - 1);
      float rad = r_in + (r_out - r_in) * t * t;
      float x = cosf(ang) * rad, z = sinf(ang) * rad;
      float n = fbm(cosf(ang) * 3 + 10, sinf(ang) * 3 + 10 + t * 2, (int)seed);
      float rise = cw_smoothstep(0, 0.35f, t);
      float h = 0, top = 1;
      switch (S->backdrop) {
      case BD_MOUNTAINS: h = (powf(n, 1.5f) * 95 + 8) * rise * (0.3f + 0.7f * t); top = 70; break; /* low enough to leave the sky its share of the picture */
      case BD_MESAS: h = terrace(fmaxf(0, n - 0.42f) * 160, 14) * rise; top = 40; break;
      case BD_SEA: h = z > V.cliff ? (n * 22 + 4) * rise : -60; top = 30; break; /* none over the sea */
      default: h = (n * 30 + 6) * rise * (1 - 0.3f * t); top = 35; break;
      }
      int i = r * NA + a;
      pos[i * 3] = x, pos[i * 3 + 1] = h - 1.5f, pos[i * 3 + 2] = z;
      uv[i * 2] = ang * 24, uv[i * 2 + 1] = rad * 0.04f + h * 0.05f; /* steep faces stretch the texture less */
      float k = cw_clampf(h / top, 0, 1);
      if (S->backdrop == BD_MOUNTAINS) k = cw_smoothstep(0.5f, 0.66f, k + (fbm(x * 0.02f, z * 0.02f + h * 0.03f, (int)seed + 3) - 0.5f) * 0.45f); /* snow on the peaks and down the gullies, rock between */
      float band = S->backdrop == BD_MESAS ? 0.82f + 0.18f * sinf(h * 0.9f) : 1; /* strata */
      for (int c = 0; c < 3; c++) col[i * 3 + c] = (S->back_low[c] + (S->back_high[c] - S->back_low[c]) * k) * band;
    }
  int n = 0;
  for (int r = 0; r < NR - 1; r++)
    for (int a = 0; a < NA - 1; a++) {
      unsigned i0 = r * NA + a, i1 = i0 + 1, i2 = i0 + NA, i3 = i2 + 1;
      idx[n++] = i0, idx[n++] = i1, idx[n++] = i2, idx[n++] = i1, idx[n++] = i3, idx[n++] = i2; /* facing up */
    }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, NA * NR, 3));
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, NA * NR, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, NA * NR, 2));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, idx, n, 1));
  t3_geometry_compute_vertex_normals(g);
  t3_geometry_compute_bounding_sphere(g);
  t3_geometry_compute_bounding_box(g);
  free(pos), free(col), free(uv), free(idx);
  return g;
}

/* the skyline's walls: rows of windows between floors, a repeat 4 floors by 4 bays */
static t3_texture *windows_texture(void) {
  enum { N = 128 };
  static uint8_t px[N * N * 4];
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      int fx = x % 32, fy = y % 32;
      bool win = fx > 5 && fx < 27 && fy > 9 && fy < 26;
      float n = hashf((uint32_t)(x / 32) * 7u + (uint32_t)(y / 32) * 131u);
      float v = win ? 0.35f + 0.25f * n : 1.0f;
      uint8_t *p = &px[(y * N + x) * 4];
      p[0] = p[1] = (uint8_t)(v * 255), p[2] = (uint8_t)(fminf(1, v * 1.08f) * 255), p[3] = 255;
    }
  t3_texture *t = t3_texture_new(N, N, px);
  t->color_space = T3_SRGB_COLOR_SPACE;
  return t;
}

/* the city's skyline: towers in a ring far out, each its own height and stone, steel or glass, lighter at the top in
 * the haze; a few tall ones stand out */
static t3_geometry *city_geo(const setting *S, uint32_t seed, int part, int parts) {
  enum { NB = 87 }; /* towers a part, six parts: 522 (games/common/coplanar.c bounds its grid; at the old bound this skyline trapped the cart) */
  float *pos = malloc(sizeof(float) * 3 * 20 * NB), *nrm = malloc(sizeof(float) * 3 * 20 * NB), *col = malloc(sizeof(float) * 3 * 20 * NB), *uv = malloc(sizeof(float) * 2 * 20 * NB);
  unsigned *idx = malloc(sizeof(unsigned) * 30 * NB);
  int nv = 0, ni = 0;
  for (int b = 0; b < NB; b++) {
    float a = (part + hashf(seed * 7u + (uint32_t)b * 2654435761u)) * 6.2831853f / parts, r = 210 + powf(hashf(seed * 13u + (uint32_t)b * 40503u), 0.8f) * 220;
    float cx = cosf(a) * r, cz = sinf(a) * r, w = 16 + hashf((uint32_t)b * 977u) * 30, d = 16 + hashf((uint32_t)b * 613u) * 30;
    float hh = hashf((uint32_t)b * 331u), h = 10 + hh * hh * 32 + (hashf((uint32_t)b * 71u) < 0.06f ? 30 : 0);
    float tone = hashf((uint32_t)b * 151u), rot = hashf((uint32_t)b * 29u) * 1.57f, cr = cosf(rot), sr = sinf(rot);
    float base[3];
    for (int c = 0; c < 3; c++) base[c] = (S->back_low[c] + (S->back_high[c] - S->back_low[c]) * (0.25f + 0.6f * tone)) * 0.45f;
    if (tone > 0.8f) base[0] *= 0.75f, base[1] *= 0.85f; /* blue glass */
    /* four walls and a roof */
    static const float F[5][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, 1, 0 } };
    for (int f = 0; f < 5; f++) {
      float nx = F[f][0] * cr - F[f][2] * sr, nz = F[f][0] * sr + F[f][2] * cr;
      float q[4][3];
      for (int k = 0; k < 4; k++) {
        float u = (k == 1 || k == 2) ? 1 : -1, v = k >= 2 ? 1 : -1, lx, ly, lz;
        if (f < 2) lx = F[f][0] * w * 0.5f, lz = u * d * 0.5f * F[f][0], ly = v > 0 ? h : 0;
        else if (f < 4) lz = F[f][2] * d * 0.5f, lx = -u * w * 0.5f * F[f][2], ly = v > 0 ? h : 0;
        else lx = u * w * 0.5f, lz = -v * d * 0.5f, ly = h;
        q[k][0] = cx + lx * cr - lz * sr, q[k][1] = ly - 1.5f, q[k][2] = cz + lx * sr + lz * cr;
      }
      for (int k = 0; k < 4; k++) {
        int i = nv + k;
        pos[i * 3] = q[k][0], pos[i * 3 + 1] = q[k][1], pos[i * 3 + 2] = q[k][2];
        nrm[i * 3] = f == 4 ? 0 : nx, nrm[i * 3 + 1] = f == 4 ? 1 : 0, nrm[i * 3 + 2] = f == 4 ? 0 : nz;
        float up = cw_clampf(q[k][1] / 90, 0, 1), sh = f == 4 ? 1.1f : 0.85f + 0.15f * (f & 1);
        for (int c = 0; c < 3; c++) col[i * 3 + c] = base[c] * sh * (0.85f + 0.3f * up);
        uv[i * 2] = (k == 1 || k == 2) ? (f < 2 ? d : w) / 28.0f : 0, uv[i * 2 + 1] = f == 4 ? 0.25f : q[k][1] / 28.0f; /* a repeat: 4 floors of 3.5 m, 4 bays */
      }
      /* wound to face out (f < 4) or up: the corners' turn checked against the face's normal */
      unsigned i0 = (unsigned)nv;
      float ax = q[1][0] - q[0][0], ay = q[1][1] - q[0][1], az = q[1][2] - q[0][2], bx = q[2][0] - q[0][0], by = q[2][1] - q[0][1], bz = q[2][2] - q[0][2];
      float cxn = ay * bz - az * by, cyn = az * bx - ax * bz, czn = ax * by - ay * bx;
      bool ccw = (f == 4 ? cyn : cxn * nx + czn * nz) > 0;
      if (ccw) idx[ni++] = i0, idx[ni++] = i0 + 1, idx[ni++] = i0 + 2, idx[ni++] = i0, idx[ni++] = i0 + 2, idx[ni++] = i0 + 3;
      else idx[ni++] = i0, idx[ni++] = i0 + 2, idx[ni++] = i0 + 1, idx[ni++] = i0, idx[ni++] = i0 + 3, idx[ni++] = i0 + 2;
      nv += 4;
    }
  }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nrm, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, nv, 2));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, idx, ni, 1));
  t3_geometry_compute_bounding_sphere(g);
  t3_geometry_compute_bounding_box(g);
  free(pos), free(nrm), free(col), free(uv), free(idx);
  return g;
}

/* a flat strip of ground texture (the yard, the track, the river, the sea) */
static void flat(const char *name, float w, float l, t3_vec3 at, t3_material *m) {
  t3_geometry *pg = t3_plane_geometry_new(w, l, 1, 1);
  xform_geo(pg, at, qaxis(1, 0, 0, -1.5707963f), (t3_vec3){ 1, 1, 1 });
  piece(V.field, name, pg, m, false);
}
static t3_material *ground_mat(const float *c, float repeat_u, float repeat_v, int seed, float rough) {
  t3_material *m = std_mat(1, 1, 1, rough, 0);
  t3_texture *t = ground_texture(c, seed);
  t->repeat = (t3_vec2){ repeat_u, repeat_v };
  t3_material_set_map(m, t);
  t3_release(t);
  return m;
}

/* the ground's colour wanders over the field (a multiplier of its texture): green ground in lush and dry patches,
 * sand and snow in drifts and hollows */
#define GROUND_TILE 12.8f /* metres a repeat of the ground texture covers */
static void wander(const setting *S, float x, float z, uint32_t seed, float *c) {
  float n = fbm(x / 48.0f + 31, z / 48.0f + 17, (int)(seed & 1023)) - 0.5f, m = fbm(x / 13.0f + 5, z / 13.0f + 3, (int)(seed & 1023) + 7) - 0.5f;
  float k = 1 + S->wander * (n * 1.1f + m * 0.3f);
  for (int i = 0; i < 3; i++) c[i] = powf(S->ground[i], 2.2f) * k; /* the texture is grey detail: the colour is here */
  if (S->ground[1] > S->ground[0] * 1.3f) { /* grass: the dry patches go straw-coloured */
    float dry = cw_smoothstep(0.0f, 0.3f, n) * S->wander * 2.0f;
    c[0] *= 1 + 0.8f * dry, c[2] *= 1 - 0.35f * dry;
  }
}
/* the ground's height: flat over the field of play (the yard, the track, the launcher's place, a river), rolling
 * past it (sand in dunes); always at or above the physics ground, so nothing that flies out there floats */
static float relief_h(const setting *S, const game *g, float x, float z) {
  if (S->relief <= 0) return 0;
  /* the field of play: the structure's yard and the launcher's circle round it, and all between (it fires from anywhere
   * on the circle) */
  float d = sqrtf(x * x + z * z) - g->ring - 4;
  float rise = cw_smoothstep(6.0f, 36.0f, d), n = fbm(x / 38.0f + 3, z / 38.0f + 11, 71);
  if (S->backdrop == BD_MESAS) n = 0.55f * n + 0.45f * (0.5f + 0.5f * sinf(x * 0.08f + z * 0.035f + n * 5)); /* dunes */
  return S->relief * rise * n * 1.6f;
}

/* a grid of ground from (x0, z0), w x l metres, cells of about cell metres, its texture laid in world space (so two
 * grids over one another match). decal: the yard round the structure and the track to the launcher, the setting's
 * worn earth or trodden snow fading into the ground at a ragged edge */
static t3_geometry *ground_grid(const setting *S, const game *g, float x0, float z0, float w, float l, float cell, bool decal, const float *hole) {
  int nx = (int)(w / cell + 0.5f) + 1, nz = (int)(l / cell + 0.5f) + 1;
  float dx = w / (float)(nx - 1), dz = l / (float)(nz - 1);
  int nv = nx * nz;
  float *pos = malloc(sizeof(float) * 3 * nv), *nrm = malloc(sizeof(float) * 3 * nv), *uv = malloc(sizeof(float) * 2 * nv), *col = malloc(sizeof(float) * 3 * nv);
  unsigned *idx = malloc(sizeof(unsigned) * 6 * (nx - 1) * (nz - 1));
  float yard[3];
  for (int c = 0; c < 3; c++) yard[c] = powf(S->yard[c], 2.2f);
  float r = g->extent + 5;
  for (int j = 0; j < nz; j++)
    for (int i = 0; i < nx; i++) {
      int v = j * nx + i;
      float x = x0 + dx * i, z = z0 + dz * j;
      pos[v * 3] = x, pos[v * 3 + 1] = relief_h(S, g, x, z) + (decal ? 0.03f : 0), pos[v * 3 + 2] = z;
      nrm[v * 3] = 0, nrm[v * 3 + 1] = 1, nrm[v * 3 + 2] = 0;
      uv[v * 2] = x / GROUND_TILE, uv[v * 2 + 1] = -z / GROUND_TILE;
      float c[3];
      wander(S, x, z, g->L->seed, c);
      if (decal) {
        float rad = sqrtf(x * x + z * z), d1 = rad - r, d2 = fabsf(rad - g->ring) - 2.4f; /* the yard, and the track the launcher rolls round on */
        float d = fminf(d1, d2) + (fbm(x * 0.35f + 40, z * 0.35f + 40, 23) - 0.5f) * 2.4f;
        float f = cw_smoothstep(-1.4f, 0.9f, d), rut = fabsf(fabsf(rad - g->ring) - 0.75f) < 0.22f ? 0.82f : 1.0f; /* the launcher's wheel ruts */
        float w = 0.9f + 0.2f * fbm(x * 0.6f, z * 0.6f, 29); /* the earth patchy too */
        for (int k = 0; k < 3; k++) c[k] = yard[k] * w * rut + (c[k] - yard[k] * w * rut) * f;
      }
      col[v * 3] = c[0], col[v * 3 + 1] = c[1], col[v * 3 + 2] = c[2];
    }
  int n = 0;
  for (int j = 0; j < nz - 1; j++)
    for (int i = 0; i < nx - 1; i++) {
      unsigned a = (unsigned)(j * nx + i), b = a + (unsigned)nx, c = a + 1, d = b + 1;
      /* a hole where the decal lies over the ground (the yard drawn once, not twice) */
      if (hole && x0 + dx * i >= hole[0] && x0 + dx * (i + 1) <= hole[1] && z0 + dz * j >= hole[2] && z0 + dz * (j + 1) <= hole[3]) continue;
      idx[n++] = a, idx[n++] = b, idx[n++] = c, idx[n++] = c, idx[n++] = b, idx[n++] = d; /* facing up */
    }
  t3_geometry *geo = t3_geometry_new();
  t3_geometry_set_attribute(geo, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, nv, 3));
  t3_geometry_set_attribute(geo, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nrm, nv, 3));
  t3_geometry_set_attribute(geo, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, nv, 2));
  t3_geometry_set_attribute(geo, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, nv, 3));
  t3_geometry_set_index(geo, t3_attribute_new(T3_UINT32, idx, n, 1));
  t3_geometry_compute_vertex_normals(geo);
  t3_geometry_compute_bounding_sphere(geo);
  t3_geometry_compute_bounding_box(geo);
  free(pos), free(nrm), free(uv), free(col), free(idx);
  return geo;
}

/* No two grass cards share a plane (what the coplanar scan used to move apart, at a second a level): a tuft whose card lies
 * in the plane of one already set (the same direction within 20 mrad, the same offset within 2 cm) is turned a little,
 * until it clears. The planes seen are kept in a hash of their direction and offset bins. */
static void grass_apart(grass_tuft *t, int n) {
  enum { SLOTS = 1 << 16 };
  static int32_t key[SLOTS][2];
  static uint8_t used[SLOTS];
  memset(used, 0, sizeof used);
  const float PI_ = 3.14159265f;
  for (int i = 0; i < n; i++)
    for (int tries = 0; tries < 24; tries++) {
      bool clash = false;
      int32_t kk[2][2];
      for (int c = 0; c < 2 && !clash; c++) {
        float a = t[i].yaw + c * PI_ * 0.5f, th = fmodf(a, PI_);
        if (th < 0) th += PI_;
        float s = -t[i].x * sinf(a) + t[i].z * cosf(a);
        kk[c][0] = (int32_t)floorf(th / 0.02f), kk[c][1] = (int32_t)floorf(s / 0.02f);
        for (int dx = -1; dx <= 1 && !clash; dx++)
          for (int dy = -1; dy <= 1 && !clash; dy++) {
            uint32_t h = ((uint32_t)(kk[c][0] + dx) * 73856093u ^ (uint32_t)(kk[c][1] + dy) * 19349663u) & (SLOTS - 1);
            for (int p = 0; p < 8 && used[(h + (uint32_t)p) & (SLOTS - 1)]; p++) {
              int q = (int)((h + (uint32_t)p) & (SLOTS - 1));
              if (key[q][0] == kk[c][0] + dx && key[q][1] == kk[c][1] + dy) { clash = true; break; }
            }
          }
      }
      if (!clash || tries == 23) {
        for (int c = 0; c < 2; c++) {
          uint32_t h = ((uint32_t)kk[c][0] * 73856093u ^ (uint32_t)kk[c][1] * 19349663u) & (SLOTS - 1);
          for (int p = 0; p < 8; p++) {
            int q = (int)((h + (uint32_t)p) & (SLOTS - 1));
            if (!used[q]) { used[q] = 1, key[q][0] = kk[c][0], key[q][1] = kk[c][1]; break; }
          }
        }
        break;
      }
      t[i].yaw += 0.07f;
    }
}

float g_view_ms[8]; /* the last view_level's parts, ms: sky and fog, ground, sea and river, backdrop, grass, trees, coplanar separation (profiling only) */
static double vl_t;
#define VL_MARK(i) do { double n_ = (double)cw_profile_now_ns() * 1e-6; g_view_ms[i] = (float)(n_ - vl_t); vl_t = n_; } while (0)
/* the level's render side, built a step at a time (view_level_step: each returns the next step, -1 when done) so the loading
 * screen's frames each do a slice (a landmark's took 140 ms in one) */
int view_level_step(const game *g, int step) {
  vl_t = (double)cw_profile_now_ns() * 1e-6;
  const setting *S = SET = &SETTINGS[level_setting(g->L)];
  bool sea = S->backdrop == BD_SEA, harbour = S->backdrop == BD_HARBOUR;
  switch (step) {
  case 0: {
  trees_clear();
  if (cw_node_valid(V.field)) cw_node_free(V.field);
  V.field = cw_node_new("Node3D", "Field");
  cw_node_add_child(V.root, V.field);
  for (int m = 0; m < 4; m++) memcpy(V.pal[m], S->pal[m], sizeof V.pal[m]);
  V.pal[M_TARGET][0] = V.pal[M_TARGET][1] = V.pal[M_TARGET][2] = 1;
  /* the light: a strong low sun behind the launcher, a cooler sky */
  if (cw_node_valid(V.sky)) {
    float t = S->tod, n = S->north;
    if (opt_get("TOD")) t = (float)atof(opt_get("TOD")); /* development: trying the light */
    if (opt_get("NORTH")) n = (float)atof(opt_get("NORTH"));
    cw_node_set(V.sky, "time_of_day", cw_val_float(t));
    cw_node_set(V.sky, "north", cw_val_float(n));
    if (opt_get("MIE")) cw_node_set(V.sky, "mie", cw_val_float((float)atof(opt_get("MIE"))));
    if (opt_get("RAYLEIGH")) cw_node_set(V.sky, "rayleigh", cw_val_float((float)atof(opt_get("RAYLEIGH"))));
    cw_node_set(V.sky, "sun_energy", cw_val_float(opt_get("SUN") ? (float)atof(opt_get("SUN")) : S->sun_energy));
    cw_node_set(V.sky, "sky_energy", cw_val_float(opt_get("SKY") ? (float)atof(opt_get("SKY")) : S->sky_energy));
  }
  if (cw_node_valid(V.post)) {
    float e = opt_get("EXPOSURE") ? (float)atof(opt_get("EXPOSURE")) : S->exposure;
    if (opt_get("LOOK_CONTROL")) e *= 3.0f; /* the look gate's control: a washed-out picture */
    cw_node_set(V.post, "exposure", cw_val_float(e));
    cw_node_set(V.post, "grade_intensity", cw_val_float(opt_get("GRADE") ? (float)atof(opt_get("GRADE")) : GRADE[SET - SETTINGS]));
    if (opt_get("TONEMAP")) cw_node_set(V.post, "tonemap", cw_val_enum(opt_get("TONEMAP")));
  }
  /* distance: height fog the colour of the setting's air (the structure stays clear: it starts 45 m out) */
  {
    cw_node f = cw_node_new("HeightFog", "Haze");
    cw_node_add_child(V.field, f);
    cw_node_set(f, "density", cw_val_float(opt_get("NOFOG") ? 0.0f : S->fog_density));
    cw_node_set(f, "height_falloff", cw_val_float(0.035f));
    cw_node_set(f, "use_sky_color", cw_val_bool(false));
    cw_node_set(f, "color", cw_val_color(S->fog[0], S->fog[1], S->fog[2], 1));
    cw_node_set(f, "start_distance", cw_val_float(70));
    cw_node_set(f, "sky_distance", cw_val_float(0));
    cw_node_set(f, "max_opacity", cw_val_float(0.55f));
  }
  VL_MARK(0);
  return 1;
  }
  case 1: {
  /* the ground, and the yard and track worn into it */
  V.cliff = -(g->ring + 10); /* the headland reaches past the launcher's circle */
  V.beam_w = 0.45f;
  {
    static const float grey[3] = { 1, 1, 1 };
    t3_material *gm = ground_mat(grey, 1, 1, S->ground[1] > S->ground[0] * 1.3f ? 0 : 4, 0.95f); /* blades on grass, grit elsewhere */
    gm->vertex_colors = true;
    t3_retain(gm);
    float zt = fmaxf(g->extent + 5, g->ring + 3) + 5; /* the yard and the launcher's track, all round */
    const float hole[4] = { -zt, zt, -zt, zt };
    if (!harbour) piece(V.field, "Ground", ground_grid(S, g, -320, sea ? V.cliff : -320, 640, sea ? 320 - V.cliff : 640, 5, false, hole), gm, false);
    else t3_release(gm);
    piece(V.field, "Yard", ground_grid(S, g, -zt, -zt, 2 * zt, 2 * zt, 0.5f, true, NULL), gm, false);
    if (harbour) { /* the point's quay: stone walls down into the water on its four sides */
      t3_material *qm = std_mat(0.5f, 0.47f, 0.42f, 0.9f, 0);
      t3_retain(qm), t3_retain(qm), t3_retain(qm);
      for (int k = 0; k < 4; k++) {
        t3_geometry *qg = t3_plane_geometry_new(2 * zt, 3, 1, 1);
        float a = k * 1.5707963f;
        xform_geo(qg, (t3_vec3){ sinf(a) * zt, -1.5f, cosf(a) * zt }, qaxis(0, 1, 0, a), (t3_vec3){ 1, 1, 1 });
        piece(V.field, "Quay", qg, qm, false);
      }
    }
  }
  VL_MARK(1);
  return 2;
  }
  case 2: {
  if (sea || harbour) { /* the headland ends at a cliff behind the castle; the sea far below to the horizon (the harbour: all round, at the quay's foot) */
    if (sea) {
      t3_material *cm = std_mat(S->back_low[0] * 0.9f + 0.25f, S->back_low[1] * 0.6f + 0.22f, S->back_low[2] * 0.5f + 0.2f, 0.95f, 0);
      t3_geometry *cg = t3_plane_geometry_new(640, 40, 1, 1);
      xform_geo(cg, (t3_vec3){ 0, -20, V.cliff }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 });
      piece(V.field, "Cliff", cg, cm, false);
    }
    /* the sea: the engine's water (long swells, glints of the sun, foam on the crests), rings round the camera */
    cw_node w = cw_node_new("Water3D", "Sea");
    cw_node_add_child(V.field, w);
    cw_node3d_set_position(w, harbour ? (t3_vec3){ 0, -0.7f, 0 } : (t3_vec3){ 0, -38, V.cliff - 350 });
    static const char *const BAY[] = { "size", "vec2(1400, 1400)", "grid", "VIEW", "horizon_distance", "900", "subdivisions", "48",
      "wave_count", "3", "wave0", "vec4(250, 14, 0.12, 0.3)", "wave1", "vec4(230, 7, 0.06, 0.3)", "wave2", "vec4(270, 4, 0.03, 0.3)",
      "color", "color(0.08, 0.3, 0.4, 1)", "deep_color", "color(0.02, 0.12, 0.2, 1)", "detail", "0.6", "roughness", "0.1",
      "reflection", "false", "refraction", "false", "crest_foam", "0.1", "floor_map_size", "0", "normal_scale", "0.05", "flow_speed", "0.02", NULL };
    static const char *const SEA[] = { "size", "vec2(1400, 700)", "grid", "VIEW", "horizon_distance", "900", "subdivisions", "48",
      "wave_count", "3", "wave0", "vec4(250, 38, 0.5, 0.35)", "wave1", "vec4(230, 17, 0.22, 0.4)", "wave2", "vec4(270, 9, 0.1, 0.4)",
      "color", "color(0.1, 0.32, 0.42, 1)", "deep_color", "color(0.02, 0.12, 0.2, 1)", "detail", "0.6", "roughness", "0.12",
      "reflection", "false", "refraction", "false", "crest_foam", "0.25", "floor_map_size", "0", "normal_scale", "0.05", "flow_speed", "0.02", NULL };
    const char *const *W = harbour ? BAY : SEA;
    for (int i = 0; W[i]; i += 2) {
      const cw_prop *p = cw_node_find_prop(w, W[i], NULL);
      cw_value v;
      if (p && p->type == CW_T_ENUM) cw_node_set(w, W[i], cw_val_enum(W[i + 1]));
      else if (p && cw_value_parse(W[i + 1], p->type, &v)) cw_node_set(w, W[i], v);
      else cw_warn("destructo: bad sea setting %s", W[i]);
    }
  }
  VL_MARK(2);
  return 3;
  }
  case 3: {
  /* the backdrop */
  {
    t3_material *m = std_mat(1, 1, 1, 0.92f, 0);
    m->vertex_colors = true;
    static const float grey[3] = { 0.85f, 0.85f, 0.85f };
    t3_texture *t = ground_texture(grey, 9);
    t3_material_set_map(m, t);
    t3_release(t);
    if (S->backdrop == BD_CITY || harbour) {
      t3_retain(m); /* the hills under the towers take it too (piece() lets one go) */
      t3_material *cm = std_mat(1, 1, 1, 0.75f, 0);
      cm->vertex_colors = true;
      t3_texture *wt = windows_texture();
      wt->repeat = (t3_vec2){ 2, 2 };
      wt->wrap_s = wt->wrap_t = T3_REPEAT;
      t3_material_set_map(cm, wt);
      t3_release(wt);
      cw_node sky = cw_node_new("Node3D", "Skyline");
      cw_node_add_child(V.field, sky);
      for (int p = 0; p < 6; p++) {
        t3_retain(cm);
        piece(sky, "Skyline", city_geo(S, g->L->seed + (uint32_t)p * 7919u, p, 6), cm, false);
      }
      /* the towers overlap and share roof and wall planes: they alone are layered apart (a few thousand faces, not the field's hundred thousand) */
      cop_separate(cw_node3d_object(sky), 0.005f, NULL);
      t3_release(cm);
      if (!harbour) piece(V.field, "Backdrop", backdrop_geo(&SETTINGS[ST_MEADOW], g->L->seed), m, false); /* low hills under the towers */
      else t3_release(m);
    } else piece(V.field, "Backdrop", backdrop_geo(S, g->L->seed), m, false);
  }
  VL_MARK(3);
  return 4;
  }
  case 4: {
  /* grass on green ground: tufts in the field round the yard, thick near it and thinning out to 45 m, off the yard,
   * the track and the river; the setting's green, lighter and drier in patches */
  if (S->ground[1] > S->ground[0] * 1.25f && S->ground[1] > S->ground[2] * 2 && !opt_get("NO_GRASS")) {
    enum { NG = 9000 };
    static grass_tuft tufts[NG];
    int n = 0;
    float r = g->extent + 5;
    t3_color base = { powf(S->ground[0], 2.2f) * 2.6f, powf(S->ground[1], 2.2f) * 2.6f, powf(S->ground[2], 2.2f) * 2.6f };
    for (int i = 0; i < NG * 4 && n < NG; i++) {
      float a = hashf(g->L->seed * 7u + (uint32_t)i * 2654435761u) * 6.2831853f;
      float rr = r + 2 + powf(hashf((uint32_t)i * 40503u + 17u), 1.8f) * 32;
      float x = cosf(a) * rr, z = sinf(a) * rr;
      float rad = sqrtf(x * x + z * z), d = fminf(rad - r, fabsf(rad - g->ring) - 4.0f);
          float clump = fbm(x / 9.0f + 5, z / 9.0f + 2, 33);
      if (d < 1.5f + clump * 3 || clump < 0.3f) continue;
      if (sea && z < V.cliff + 2) continue;
      if (harbour && (fabsf(x) > fmaxf(g->extent + 5, g->ring + 3) + 4.5f || fabsf(z) > fmaxf(g->extent + 5, g->ring + 3) + 4.5f)) continue;
      float dry = cw_smoothstep(0.55f, 0.8f, fbm(x / 25.0f + 1, z / 25.0f + 8, 44)), v = 0.8f + 0.4f * hashf((uint32_t)i * 977u);
      grass_tuft *t = &tufts[n++];
      t->x = x, t->z = z, t->y = relief_h(S, g, x, z);
      t->yaw = hashf((uint32_t)i * 313u) * 3.1415927f;
      t->w = 0.35f + 0.25f * hashf((uint32_t)i * 71u), t->h = (0.14f + 0.14f * hashf((uint32_t)i * 53u)) * (0.75f + 0.5f * clump);
      t->c = (t3_color){ (base.r + dry * 0.12f) * v, (base.g + dry * 0.05f) * v, base.b * v };
    }
    grass_apart(tufts, n);
    grass_build(V.field, tufts, n);
  }
  VL_MARK(4);
  return 5;
  }
  case 5: {
  /* trees: the setting's two species, in groves, outside the field of fire */
  if (S->ntrees > 0 && !opt_get("NO_TREES")) {
    static tree_spot spots[TREES_MAX];
    int n = 0;
    bool autumn = level_setting(g->L) == ST_MEADOW, groves = S->sp[0] == SP_PALM;
    for (int i = 0; n < S->ntrees && n < TREES_MAX && i < S->ntrees * 12; i++) {
      float a = hashf(g->L->seed * 31u + (uint32_t)i * 7u) * 6.2831853f, r = g->ring + 12 + hashf(g->L->seed * 17u + (uint32_t)i * 13u) * 70;
      float x = cosf(a) * r, z = sinf(a) * r;
      if (x * x + z * z < (g->ring + 12) * (g->ring + 12)) continue; /* the field of fire: the launcher fires from anywhere on its circle */
      if (x * x + z * z < (g->extent + 12) * (g->extent + 12)) continue;
      if (sea && z < V.cliff + 4) continue;
      if (fbm(x / 34.0f + 9, z / 34.0f + 9, 61) < (groves ? 0.52f : 0.38f)) continue; /* groves and clearings */
      tree_spot *t = &spots[n];
      float h = hashf((uint32_t)i * 59u + 3u), sel = hashf((uint32_t)i * 7u + 11u);
      int k = sel < S->sp_share ? 1 : 0;
      t->x = x, t->y = relief_h(S, g, x, z) - 0.15f, t->z = z, t->yaw = a * 3 + h * 2, t->species = S->sp[k];
      t->scale = (t->species == SP_PALM ? 1.1f : 0.75f) + hashf((uint32_t)i * 101u) * 0.5f;
      t->stretch = 0.9f + 0.25f * hashf((uint32_t)i * 37u);
      float v = 0.8f + 0.4f * h;
      const float *L = S->leaf[k];
      t3_color c = { L[0] * v, L[1] * v, L[2] * v };
      if (autumn && k == 0) { /* an oak's turn: copper, rust or still green */
        float q = hashf((uint32_t)i * 19u + 5u);
        c = q < 0.4f ? (t3_color){ 0.6f * v, 0.2f * v, 0.03f * v } : q < 0.75f ? (t3_color){ 0.48f * v, 0.27f * v, 0.04f * v } : (t3_color){ 0.16f * v, 0.27f * v, 0.05f * v };
      }
      t->leaf = c;
      n++;
    }
    trees_build(V.field, spots, n);
  }
  VL_MARK(5);
  return 6;
  }
  default: {
  /* (the field's faces laid flush on others, scanned and layered apart with cop_separate (games/common/coplanar.h), found 0 to 8 vertices to move
   * in a field of 100,000 faces and cost 0.2 to 2.5 s a level: the field is made so that none lie flush, and tests/destructo-coplanar.mjs scans every
   * level for pairs. dm set OLD_LOAD 1 (the load gate's control) scans as before.) */
  if (opt_on("OLD_LOAD")) {
    int left = 0, moved = cop_separate(cw_node3d_object(V.field), 0.005f, &left);
    cw_info("destructo: level %d: %d vertices moved off shared planes in the field in %.0f ms, %d pairs left", g->level, moved, cop_last_ms, left);
  }
  VL_MARK(6);
  build_launcher(g);
  VL_MARK(7);
  memset(V.lay_born, 0, sizeof V.lay_born);
  V.built_level = g->level;
  V.cam_init = false;
  return -1;
  }
  }
}
void view_level(const game *g) {
  for (int s = 0; s >= 0;) s = view_level_step(g, s);
}

/* ── every frame ───────────────────────────────────────────────────── */

/* Blocks laid side by side in a course, or stacked flush, have faces in one plane, and once the structure settles they
 * press a few millimetres into each other: those strips of two faces have no depth order, and the depth test would
 * pick between the two blocks' faces pixel by pixel, differently as the camera moves. Each block is drawn a little
 * proud along each of its axes by that axis's layer (LAY_STEP a layer, both ends): along each world axis the blocks
 * are coloured so that two whose faces across it lie within LAY_NEAR of one plane, and that touch, are drawn at least
 * LAY_CLEAR apart there (a settled block sits a few millimetres off its neighbour and leans a few milliradians: the
 * layers must clear what settling left, not only differ). An axis is coloured on its own, so a wall's face takes its
 * layers in its depth alone and the joints keep their lines. The level's blocks are coloured when they appear and
 * again once settled (LAY_SETTLE); a fragment takes layers from its slot. Drawing only: the bodies are as they were. */
#define LAY_STEP 0.002f
#define LAY_N 8
#define LAY_NEAR 0.02f
#define LAY_TOUCH 0.03f /* (the level lays its blocks 8 mm apart, and they settle) */
#define LAY_CLEAR 0.0018f
#define LAY_SETTLE 120 /* ticks: a level's blocks have settled onto each other */
static bool getenv_layers_off(void) { /* COPLANAR_SEPARATE=0: the blocks drawn at their bodies' size, the sweep's control */
  return opt_get("COPLANAR_SEPARATE") && !strcmp(opt_get("COPLANAR_SEPARATE"), "0");
}
/* a block's world box and, for each world axis, the block's own axis that lies along it */
static void block_box(const game *g, int bi, float lo[3], float hi[3], uint8_t own[3]) {
  const block *k = &g->b[bi];
  cw_xform3 x = game_block_xform(g, bi);
  t3_mat4 r;
  t3_mat4_compose(&r, (t3_vec3){ 0, 0, 0 }, x.q, (t3_vec3){ 1, 1, 1 });
  const float h[3] = { k->half.x, k->half.y, k->half.z }, c[3] = { x.p.x, x.p.y, x.p.z };
  for (int a = 0; a < 3; a++) {
    float e = fabsf(r.e[a]) * h[0] + fabsf(r.e[4 + a]) * h[1] + fabsf(r.e[8 + a]) * h[2];
    lo[a] = c[a] - e, hi[a] = c[a] + e;
    int best = 0;
    for (int j = 1; j < 3; j++)
      if (fabsf(r.e[j * 4 + a]) > fabsf(r.e[best * 4 + a])) best = j;
    own[a] = (uint8_t)best;
  }
}
static void color_blocks(const game *g) {
  static float lo[MAXBLK][3], hi[MAXBLK][3];
  static uint8_t own[MAXBLK][3];
  static int stamp[MAXBLK], near[MAXBLK];
  float glo[3] = { 1e9f, 1e9f, 1e9f }, ghi[3] = { -1e9f, -1e9f, -1e9f };
  for (int i = 0; i < g->nb; i++)
    if (g->b[i].alive && !g->b[i].gen) {
      block_box(g, i, lo[i], hi[i], own[i]);
      for (int a = 0; a < 3; a++) glo[a] = fminf(glo[a], lo[i][a]), ghi[a] = fmaxf(ghi[a], hi[i][a]);
    }
  /* only blocks whose boxes come within LAY_TOUCH of each other can need apart (touching across the other two axes,
   * and a face within LAY_NEAR of a plane along this one means they overlap along it too): a grid of buckets finds
   * them (thousands of bricks: every pair would take seconds) */
  float cs = 0.6f;
  int gn[3];
  for (int a = 0; a < 3; a++) {
    if (ghi[a] < glo[a]) glo[a] = ghi[a] = 0;
    while ((ghi[a] - glo[a]) / cs > 96) cs *= 1.5f;
  }
  for (int a = 0; a < 3; a++) gn[a] = (int)((ghi[a] - glo[a]) / cs) + 1;
  int ncell = gn[0] * gn[1] * gn[2];
  int *start = calloc((size_t)ncell + 1, sizeof(int)), *fillp = calloc((size_t)ncell, sizeof(int));
#define GCELL(a, v) CW_CLAMP((int)(((v) - glo[a]) / cs), 0, gn[a] - 1)
#define FOR_CELLS(i, pad, ...) \
  for (int cx = GCELL(0, lo[i][0] - (pad)); cx <= GCELL(0, hi[i][0] + (pad)); cx++) \
    for (int cy = GCELL(1, lo[i][1] - (pad)); cy <= GCELL(1, hi[i][1] + (pad)); cy++) \
      for (int cz = GCELL(2, lo[i][2] - (pad)); cz <= GCELL(2, hi[i][2] + (pad)); cz++) { int cell = (cx * gn[1] + cy) * gn[2] + cz; __VA_ARGS__ }
  for (int i = 0; i < g->nb; i++)
    if (g->b[i].alive && !g->b[i].gen) FOR_CELLS(i, 0, start[cell + 1]++;)
  for (int c = 0; c < ncell; c++) start[c + 1] += start[c];
  int *list = malloc(sizeof(int) * (size_t)(start[ncell] + 1));
  for (int i = 0; i < g->nb; i++)
    if (g->b[i].alive && !g->b[i].gen) FOR_CELLS(i, 0, list[start[cell] + fillp[cell]++] = i;)
  memset(stamp, 0, sizeof stamp);
  for (int a = 0; a < 3; a++) /* each world axis on its own */
    for (int i = 0; i < g->nb; i++) {
      const block *k = &g->b[i];
      if (!k->alive || k->gen) continue;
      /* the earlier blocks near it */
      int nn = 0, tag = a * MAXBLK + i + 1;
      FOR_CELLS(i, LAY_TOUCH, for (int t = start[cell]; t < start[cell + 1]; t++) {
        int j = list[t];
        if (j < i && stamp[j] != tag) stamp[j] = tag, near[nn++] = j;
      })
      int c = 0;
      for (; c < LAY_N - 1; c++) { /* the lowest layer that clears every earlier neighbour across this axis */
        bool clear = true;
        for (int q = 0; q < nn && clear; q++) {
          int j = near[q];
          float dh = hi[i][a] - hi[j][a], dl = lo[i][a] - lo[j][a];
          if (fabsf(dh) >= LAY_NEAR && fabsf(dl) >= LAY_NEAR) continue;
          bool touch = true;
          for (int b = 0; b < 3 && touch; b++)
            if (b != a) touch = lo[i][b] <= hi[j][b] + LAY_TOUCH && lo[j][b] <= hi[i][b] + LAY_TOUCH;
          if (!touch) continue;
          float e = LAY_STEP * (float)(c - V.lay[j][own[j][a]]);
          if (fabsf(dh) < LAY_NEAR && fabsf(dh + e) < LAY_CLEAR) clear = false;
          if (fabsf(dl) < LAY_NEAR && fabsf(dl - e) < LAY_CLEAR) clear = false;
        }
        if (clear) break;
      }
      V.lay[i][own[i][a]] = (uint8_t)c;
    }
#undef FOR_CELLS
#undef GCELL
  free(start), free(fillp), free(list);
  for (int i = 0; i < g->nb; i++)
    if (g->b[i].alive && !g->b[i].gen) V.lay_born[i] = g->b[i].born + 1;
  if (opt_get("LAYER_LOG")) {
    int hist[LAY_N] = { 0 };
    for (int i = 0; i < g->nb; i++)
      if (g->b[i].alive && !g->b[i].gen)
        for (int a = 0; a < 3; a++) hist[V.lay[i][a]]++;
    fprintf(stderr, "destructo layers (tick %d):", g->tick);
    for (int c = 0; c < LAY_N; c++) fprintf(stderr, " %d", hist[c]);
    fprintf(stderr, "\n");
  }
}
static void block_layers(const game *g) {
  bool fresh = false;
  for (int i = 0; i < g->nb && !fresh; i++) fresh = g->b[i].alive && !g->b[i].gen && V.lay_born[i] != g->b[i].born + 1;
  if (fresh) color_blocks(g), V.lay_settled = g->tick + LAY_SETTLE;
  else if (V.lay_settled && g->tick >= V.lay_settled) color_blocks(g), V.lay_settled = 0; /* again once the structure has settled */
  for (int i = 0; i < g->nb; i++) /* a fragment: layers from its slot and birth */
    if (g->b[i].alive && g->b[i].gen && V.lay_born[i] != g->b[i].born + 1) {
      uint32_t h = (uint32_t)i * 2654435761u + (uint32_t)g->b[i].born * 40503u;
      for (int a = 0; a < 3; a++) V.lay[i][a] = (uint8_t)((h >> (8 * a)) & 3);
      V.lay_born[i] = g->b[i].born + 1;
    }
}

static void lake_view(const game *g, float alpha);

/* ── the lake behind a dam, and its water (water.c) ─────────────────────────────────────────────────── */
/* the lake: a surface over the canyon, from the dam's curved face back to the far cliff, at the level the sim says (it falls as the dam spills) */
static void lake_view(const game *g, float alpha) {
  const water_state *w = &g->wt;
  if (!w->on) {
    if (cw_node_valid(V.lake)) cw_node3d_set_scale(V.lake, (t3_vec3){ 0, 0, 0 }); /* (no lake in this level) */
    V.water->count = 0, V.pool->count = 0;
    return;
  }
  if (!cw_node_valid(V.lake)) {
    /* a block of water one metre tall (the node's y scale is the level): its top, and the face it shows the dam, from the dam's curved upstream
     * face back to the far ridge, as wide as the canyon at the surface (flaring upstream) */
    enum { NXS = 40, NZS = 24, NT = (NXS + 1) * (NZS + 1), NV = NT + 2 * (NXS + 1) };
    const float Zb = -46.2f;
    static float pos[NV * 3], uv[NV * 2];
    static unsigned idx[(NXS * NZS + NXS) * 6];
    for (int j = 0; j <= NZS; j++)
      for (int i = 0; i <= NXS; i++) {
        float zf = water_dam_zc(w, 0) - 0.1f, z = Zb + (zf - Zb) * (float)j / NZS;
        float lv = w->level0 / 1.4f, half = 1.4f * (5.5f + 0.2f * fminf(lv, 10.0f) + 0.08f * fmaxf(0, lv - 10.0f) + fminf(7.0f, 0.45f * fmaxf(0, -z / 1.4f - 2.2f))) + 0.3f, x = -half + 2 * half * (float)i / NXS;
        zf = water_dam_zc(w, x) - 0.1f, z = Zb + (zf - Zb) * (float)j / NZS; /* the far edge follows the arch */
        int v = j * (NXS + 1) + i;
        pos[v * 3] = x, pos[v * 3 + 1] = 1, pos[v * 3 + 2] = z;
        uv[v * 2] = x * 0.25f, uv[v * 2 + 1] = z * 0.25f;
      }
    for (int i = 0; i <= NXS; i++) { /* the face toward the dam: the top row's x and z, a metre down */
      int top = NZS * (NXS + 1) + i;
      for (int k = 0; k < 2; k++) {
        int v = NT + k * (NXS + 1) + i;
        pos[v * 3] = pos[top * 3], pos[v * 3 + 1] = k ? 1.0f : 0.0f, pos[v * 3 + 2] = pos[top * 3 + 2];
        uv[v * 2] = pos[top * 3] * 0.25f, uv[v * 2 + 1] = k * 0.25f;
      }
    }
    int n = 0;
    for (int j = 0; j < NZS; j++)
      for (int i = 0; i < NXS; i++) {
        unsigned a = (unsigned)(j * (NXS + 1) + i), b = a + NXS + 1, c = a + 1, d = b + 1;
        idx[n++] = a, idx[n++] = b, idx[n++] = c, idx[n++] = c, idx[n++] = b, idx[n++] = d;
      }
    for (int i = 0; i < NXS; i++) {
      unsigned a = (unsigned)(NT + i), b = a + NXS + 1, c = a + 1, d = b + 1; /* a, c: the foot; b, d: the surface */
      idx[n++] = a, idx[n++] = c, idx[n++] = b, idx[n++] = b, idx[n++] = c, idx[n++] = d;
    }
    t3_geometry *geo = t3_geometry_new();
    t3_geometry_set_attribute(geo, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, NV, 3));
    t3_geometry_set_attribute(geo, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, NV, 2));
    t3_geometry_set_index(geo, t3_attribute_new(T3_UINT32, idx, n, 1));
    t3_geometry_compute_vertex_normals(geo);
    t3_geometry_compute_bounding_sphere(geo);
    t3_geometry_compute_bounding_box(geo);
    t3_material *m = std_mat(0.06f, 0.3f, 0.52f, 0.05f, 0.0f);
    m->emissive = (t3_color){ 0.02f, 0.1f, 0.2f };
    m->emissive_intensity = 0.5f;
    V.lake = piece(V.field, "Lake", geo, m, false);
  }
  cw_node3d_set_position(V.lake, (t3_vec3){ 0, 0, 0 });
  cw_node3d_set_scale(V.lake, (t3_vec3){ 1, fmaxf(0.01f, w->level), 1 });
  /* the parcels */
  int n = 0;
  for (int i = 0; i < w->n; i++) {
    if (!w->live[i]) continue;
    cw_xform3 x = cw_body3d_interpolated(w->body[i], alpha);
    t3_vec3 vel = cw_body3d_velocity(w->body[i]);
    float sp = sqrtf(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z), f = cw_smoothstep(2.5f, 6.0f, sp);
    t3_mat4 mx;
    t3_mat4_compose(&mx, x.p, x.q, (t3_vec3){ 0.9f, 0.9f, 0.9f }); /* (a parcel draws bigger than its body: a few hundred must read as a stream) */
    t3_instanced_mesh_set_matrix_at(V.water, n, &mx);
    float tint = 0.9f + 0.2f * (float)((i * 37) & 15) / 15.0f;
    t3_instanced_mesh_set_color_at(V.water, n, (t3_color){ (0.22f + 0.7f * f) * tint, (0.52f + 0.4f * f) * tint, (0.95f + 0.05f * f) * tint });
    n++;
  }
  V.water->count = n;
  V.water->instance_matrix->version++;
  if (V.water->instance_color) V.water->instance_color->version++;
  /* the pool: a disc where each parcel lay, a quarter metre deep (a parcel is 0.22 m^3 over its disc) */
  int np = w->npool < WATER_POOL ? w->npool : WATER_POOL;
  for (int i = 0; i < np; i++) {
    t3_mat4 mx;
    t3_mat4_compose(&mx, (t3_vec3){ w->pool[i].x, w->pool[i].y + 0.07f + 0.0007f * (float)i, w->pool[i].z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1.8f, 0.16f, 1.8f });
    t3_instanced_mesh_set_matrix_at(V.pool, i, &mx);
    t3_instanced_mesh_set_color_at(V.pool, i, (t3_color){ 1, 1, 1 });
  }
  /* the flood: the water that has spilled runs out over the valley floor below the dam as a wave front that lengthens with it: a broad sheet, and over it
   * bands of foam that run downstream, brighter toward the front (each band a squashed disc, its place sliding with the tick) */
  float flood = fminf(60.0f, w->vol_out * 0.16f);
  if (flood > 0.5f) {
    float z0 = water_dam_zc(w, 0) + 3.0f, ph = (float)g->tick * 0.05f;
    for (int k = 0; k < 6 && np < WATER_POOL + 8; k++) {
      float f = k == 0 ? 1.0f : 1.0f - 0.14f * (float)k + 0.07f * sinf(ph * 2.0f + (float)k * 1.7f), len = flood * f;
      float spread = 1.0f - 0.1f * (float)k;
      t3_mat4 mx;
      t3_mat4_compose(&mx, (t3_vec3){ 0.6f * sinf(ph + (float)k), 0.16f + 0.03f * (float)k, z0 + len * 0.5f }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ (17.0f + flood * 0.35f) * spread, 0.22f, len });
      t3_instanced_mesh_set_matrix_at(V.pool, np, &mx);
      float b = k == 0 ? 1.0f : 1.25f + 0.12f * (float)k;
      t3_instanced_mesh_set_color_at(V.pool, np, k == 0 ? (t3_color){ 0.9f, 1.25f, 1.5f } : (t3_color){ b, b * 1.1f, b * 1.2f });
      np++;
    }
  }
  V.pool->count = np;
  V.pool->instance_matrix->version++;
  if (V.pool->instance_color) V.pool->instance_color->version++;
}

void view_sync(const game *g, float alpha) {
  block_layers(g);
  lake_view(g, alpha);
  int cnt[M_COUNT][NVAR] = { { 0 } }, nroof = 0, ncurt = 0;
  for (int i = 0; i < g->nb; i++) {
    const block *k = &g->b[i];
    if (!k->alive) continue;
    int m = k->mat;
    bool roofing = k->paint == P_ROOF_RED || k->paint == P_ROOF_SLATE || k->paint == P_ROOF_COPPER;
    if (roofing && nroof >= MAXBLK / 4) roofing = false;
    bool curtain = m == M_GLASS && (k->paint == P_DARK_GLASS || k->paint == P_BLUE_GLASS) && ncurt < MAXBLK / 2;
    int var = V.nvar[m] > 1 ? (k->tint >> 2) % V.nvar[m] : 0;
    t3_instanced_mesh *im = roofing ? V.roof : curtain ? V.curtain : V.blk[m][var];
    int s = roofing ? nroof++ : curtain ? ncurt++ : cnt[m][var]++;
    cw_xform3 x = game_block_interp(g, i, alpha);
    t3_mat4 mx;
    const float proud = m == M_TARGET || getenv_layers_off() ? 0 : 2.0f * LAY_STEP; /* a layer (both ends) */
    t3_vec3 sc = { k->half.x * 2 + proud * V.lay[i][0], k->half.y * 2 + proud * V.lay[i][1], k->half.z * 2 + proud * V.lay[i][2] };
    t3_mat4_compose(&mx, x.p, x.q, sc);
    t3_instanced_mesh_set_matrix_at(im, s, &mx);
    float v = (k->baked ? 0.88f + 0.24f * (float)k->tint / 255.0f : 0.74f + 0.48f * (float)k->tint / 255.0f) * (1.0f + (float)k->shade / 127.0f * 0.9f), hue = ((k->tint * 37) & 255) / 255.0f - 0.5f + (float)k->tone / 127.0f * 0.8f; /* each block its own shade, a little warmer or cooler; a landmark's bricks carry the model's relief and the rock's streaks */
    const float *c = V.pal[m];
    float dmg = 1;
    if (m != M_TARGET) {
      float full = MATS[m].toughness * k->vol * (k->gen ? 0.75f : 1.0f);
      float f = full > 0 ? k->hp / full : 1;
      dmg = 0.72f + 0.28f * cw_clampf(f, 0, 1); /* cracked blocks darken */
    }
    /* painted wood: one plank in eight takes a bright colour */
    t3_color col = { c[0] * v * dmg * (1 + hue * 0.2f), c[1] * v * dmg, c[2] * v * dmg * (1 - hue * 0.24f) };
    if (k->paint) { /* the builder's paint: roofs, trims, timber, gold */
      const float *p = PAINT[k->paint];
      float pv = (k->baked ? 0.92f + 0.16f * (float)k->tint / 255.0f : 0.82f + 0.32f * (float)k->tint / 255.0f) * (1.0f + (float)k->shade / 127.0f * 0.9f);
      float wm = (float)k->tone / 127.0f * 0.24f; /* warmer or cooler */
      col = (t3_color){ p[0] * pv * dmg * (1 + wm), p[1] * pv * dmg, p[2] * pv * dmg * (1 - wm) };
    } else if (m == M_WOOD && (k->tint & 7) == 0) { /* painted wood: one plank in eight takes a bright colour */
      static const float paint[4][3] = { { 0.85f, 0.2f, 0.15f }, { 0.15f, 0.45f, 0.85f }, { 0.95f, 0.75f, 0.15f }, { 0.2f, 0.65f, 0.45f } };
      const float *p = paint[(k->tint >> 3) & 3];
      col = (t3_color){ p[0] * dmg, p[1] * dmg, p[2] * dmg };
    }
    /* a piece of a broken block shows its inside: fresh pale stone, raw splintered wood */
    if (k->gen && (m == M_WOOD || m == M_STONE)) {
      static const float raw[2][3] = { { 0.6f, 0.45f, 0.27f }, { 0.6f, 0.57f, 0.51f } };
      const float *r = raw[m == M_STONE];
      col = (t3_color){ col.r + (r[0] - col.r) * 0.4f, col.g + (r[1] - col.g) * 0.4f, col.b + (r[2] - col.b) * 0.4f };
    }
    if (m == M_STEEL && k->tone > 0) { /* rust, from the weather baked into the member */
      float t = (float)k->tone / 127.0f * 0.75f, lum = 0.5f * (col.r + col.g + col.b);
      col = (t3_color){ col.r + (lum * 1.7f + 0.03f - col.r) * t, col.g + (lum * 0.7f + 0.01f - col.g) * t, col.b + (lum * 0.3f - col.b) * t };
    }
    t3_instanced_mesh_set_color_at(im, s, col);
  }
  int inst = 0;
  for (int m = 0; m < M_COUNT; m++) {
    for (int vi = 0; vi < V.nvar[m]; vi++) {
      V.blk[m][vi]->count = cnt[m][vi];
      V.blk[m][vi]->instance_matrix->version++;
      if (V.blk[m][vi]->instance_color) V.blk[m][vi]->instance_color->version++;
      inst += cnt[m][vi];
    }
  }
  V.curtain->count = ncurt;
  V.curtain->instance_matrix->version++;
  if (V.curtain->instance_color) V.curtain->instance_color->version++;
  inst += ncurt;
  V.roof->count = nroof;
  V.roof->instance_matrix->version++;
  if (V.roof->instance_color) V.roof->instance_color->version++;
  inst += nroof;
  V.st.instances = inst;
  /* the crystals' lights */
  int lights = 0;
  for (int t = 0; t < MAXTGT; t++) {
    bool on = t < g->ntgt && !g->tgt[t].down && g->tgt[t].blk >= 0;
    if (on) {
      cw_xform3 x = game_block_interp(g, g->tgt[t].blk, alpha);
      cw_node3d_set_position(V.crystal_light[t], (t3_vec3){ x.p.x, x.p.y + 0.6f, x.p.z });
      float pulse = 0.85f + 0.15f * sinf((float)g->tick * 0.08f + t);
      cw_node_set(V.crystal_light[t], "energy", cw_val_float(9 * pulse));
      lights++;
    } else cw_node_set(V.crystal_light[t], "energy", cw_val_float(0));
  }
  V.st.lights = lights;
  /* the beacons */
  {
    int nb = 0;
    for (int t = 0; t < g->ntgt && t < MAXTGT; t++) {
      if (g->tgt[t].down || g->tgt[t].blk < 0) continue;
      cw_xform3 x = game_block_interp(g, g->tgt[t].blk, alpha);
      float pulse = 1 + 0.08f * sinf((float)g->tick * 0.05f + t * 1.7f);
      t3_mat4 mx;
      t3_mat4_compose(&mx, (t3_vec3){ x.p.x, x.p.y + 0.3f, x.p.z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ pulse * V.beam_w, 18, pulse * V.beam_w });
      t3_instanced_mesh_set_matrix_at(V.beams, nb++, &mx);
    }
    V.beams->count = nb;
    V.beams->instance_matrix->version++;
  }
  /* the banners: on their blocks, the flags waving */
  {
    int nb = 0;
    for (int i = 0; i < g->nban; i++) {
      const banner *bn = &g->ban[i];
      const block *k = &g->b[bn->blk];
      if (!k->alive) continue;
      cw_xform3 x = game_block_interp(g, bn->blk, alpha);
      t3_vec3 foot = t3_vec3_apply_quat(bn->local, x.q);
      foot = (t3_vec3){ x.p.x + foot.x, x.p.y + foot.y, x.p.z + foot.z };
      t3_mat4 pm, fm;
      t3_mat4_compose(&pm, foot, x.q, (t3_vec3){ 0.07f, bn->h, 0.07f });
      t3_instanced_mesh_set_matrix_at(V.poles, nb, &pm);
      t3_instanced_mesh_set_color_at(V.poles, nb, (t3_color){ 1, 1, 1 });
      /* the flag at the pole's top, turned by the wind (it blows from the launcher's left) and waving */
      float wave = sinf((float)g->tick * 0.09f + i * 1.3f) * 0.18f;
      t3_quat q = t3_quat_multiply(x.q, t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, 0.6f + wave));
      float fw = 0.45f + bn->h * 0.35f, fh = fw * 0.6f;
      t3_vec3 top = t3_vec3_apply_quat((t3_vec3){ 0, bn->h - fh * 0.5f - 0.05f, 0 }, x.q);
      t3_mat4_compose(&fm, (t3_vec3){ foot.x + top.x, foot.y + top.y, foot.z + top.z }, q, (t3_vec3){ fw, fh, 1 });
      t3_instanced_mesh_set_matrix_at(V.flags, nb, &fm);
      const float *c = BANNER[bn->colour & 3];
      t3_instanced_mesh_set_color_at(V.flags, nb, (t3_color){ c[0], c[1], c[2] });
      nb++;
    }
    V.poles->count = V.flags->count = nb;
    V.poles->instance_matrix->version++, V.flags->instance_matrix->version++;
    if (V.poles->instance_color) V.poles->instance_color->version++;
    if (V.flags->instance_color) V.flags->instance_color->version++;
  }
  /* the launcher follows the aim; a shot kicks the barrel back and it runs out again, smoothly */
  static int kick_shots = -1;
  static float kick;
  if (kick_shots != g->shots) { if (kick_shots >= 0 && g->shots > kick_shots) kick = 1; kick_shots = g->shots; }
  kick *= 0.9f;
  if (cw_node_valid(V.yaw)) {
    cw_node3d_set_position(V.barrel, (t3_vec3){ 0, g->muzzle.y, 0.42f * kick });
    cw_node3d_set_position(V.launcher, (t3_vec3){ g->muzzle.x, 0, g->muzzle.z });
    cw_node3d_set_rotation(V.launcher, (t3_vec3){ 0, g->orbit, 0 });
    cw_node3d_set_rotation(V.yaw, (t3_vec3){ 0, g->yaw, 0 });
    cw_node3d_set_rotation(V.barrel, (t3_vec3){ g->pitch, 0, 0 });
  }
  /* the shot */
  if (g->proj_live) {
    cw_xform3 x = cw_body3d_interpolated(g->proj, alpha);
    float r = AMMO[g->proj_kind].radius;
    cw_node3d_set_position(V.shot, x.p);
    cw_node3d_set_quaternion(V.shot, x.q);
    cw_node3d_set_scale(V.shot, (t3_vec3){ r, r, r });
    if (g->proj_kind != V.shot_kind) {
      V.shot_kind = g->proj_kind;
      if (g->proj_kind == A_CHARGE) V.shot_mat->color = (t3_color){ 0.85f, 0.12f, 0.08f }, V.shot_mat->emissive = (t3_color){ 1, 0.3f, 0.05f }, V.shot_mat->metalness = 0.2f;
      else V.shot_mat->color = (t3_color){ 0.12f, 0.12f, 0.14f }, V.shot_mat->emissive = (t3_color){ 0, 0, 0 }, V.shot_mat->metalness = 0.85f;
    }
    if (g->proj_kind == A_CHARGE) V.shot_mat->emissive_intensity = 1.5f + 1.5f * sinf((float)g->tick * 0.6f);
  } else cw_node3d_set_position(V.shot, (t3_vec3){ 0, -50, 0 });
  /* the aim path: the flight as the world will fly it, as far as the structure's near face (how high it arrives there
   * shows; where it lands behind does not). It used to stop at 42% of the way, short of the structure, and a low wall
   * took shot after shot over its top from a path that seemed to run into it */
  int np = 0;
  if (game_aiming(g) && !g_view_no_path) {
    if (V.front_level != g->level + 1 || V.front_orbit != g->orbit) V.front = game_front(g), V.front_orbit = g->orbit, V.front_level = g->level + 1;
    t3_vec3 out = game_out_dir(g);
    t3_vec3 d = game_aim_dir(g);
    float sp = game_speed(g);
    t3_vec3 p = { g->muzzle.x + d.x * 1.4f, g->muzzle.y + d.y * 1.4f, g->muzzle.z + d.z * 1.4f };
    t3_vec3 v = { d.x * sp, d.y * sp, d.z * sp };
    float h = TICK / 4, along = 0, next = 1.2f;
    for (int i = 0; i < 4 * 60 * 6 && np < NPATH; i++) {
      v.y -= GRAV * h;
      t3_vec3 q = { p.x + v.x * h, p.y + v.y * h, p.z + v.z * h };
      along += sqrtf((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y) + (q.z - p.z) * (q.z - p.z));
      p = q;
      if (p.y < 0) break;
      if (along >= next) {
        float f = 1 - (float)np / NPATH;
        float cd = sqrtf((p.x - V.eye.x) * (p.x - V.eye.x) + (p.y - V.eye.y) * (p.y - V.eye.y) + (p.z - V.eye.z) * (p.z - V.eye.z));
        float r = (0.07f + 0.05f * f) * fmaxf(1.0f, cd / 14.0f); /* a bead keeps its size on the screen from a high camera (a dam seen from 36 m) */
        t3_mat4 m;
        t3_mat4_compose(&m, p, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ r, r, r });
        t3_instanced_mesh_set_matrix_at(V.path, np, &m);
        float b = 0.35f + 0.65f * f;
        t3_instanced_mesh_set_color_at(V.path, np, (t3_color){ b, b, b });
        np++;
        next += 1.1f;
        if (p.x * out.x + p.z * out.z <= V.front) { /* the last bead, where the shot reaches the structure: a big bright mark */
          t3_mat4 m2;
          float R = r * 4.0f;
          t3_mat4_compose(&m2, p, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ R, R, R });
          t3_instanced_mesh_set_matrix_at(V.path, np - 1, &m2);
          t3_instanced_mesh_set_color_at(V.path, np - 1, (t3_color){ 1.0f, 0.85f, 0.2f });
          break;
        }
      }
    }
  }
  V.path->count = np;
  V.path->instance_matrix->version++;
  if (V.path->instance_color) V.path->instance_color->version++;
}

static t3_vec3 lerp3(t3_vec3 a, t3_vec3 b, float t) { return (t3_vec3){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }

void view_camera(const game *g, cw_node camera, float aspect, float dt, bool overview, float alpha) {
  t3_vec3 eye, at;
  float k = 3.0f; /* how fast the camera follows */
  float wy = game_world_yaw(g), sy = sinf(wy), cy = cosf(wy);
  t3_vec3 fwd = { -sy, 0, -cy }, right = { cy, 0, -sy };
  float so = sinf(g->orbit), co = cosf(g->orbit); /* the launcher's place: out from the middle, and its right */
  float ext = g->extent, top = g->top;
  t3_vec3 mid = { 0, top * 0.42f, 0 };
  bool aimv = false; /* the camera is in the aim view */
  if (overview) {
    float ox = ext * 1.6f + 10, oz = g->ring * 0.55f; /* to the launcher's right and out toward it */
    eye = (t3_vec3){ ox * co + oz * so, top * 0.9f + 8, -ox * so + oz * co };
    at = mid;
    k = 4;
  } else if (g->state == GS_AIM || (g->ready && g->ready_t > 24)) {
    aimv = g->state == GS_AIM || V.last_state == g->state;
    /* (the aim view is back a little after the shot lands, while the rubble still settles) behind the launcher, a little to the right, looking down the aim over the structure */
    t3_vec3 m = g->muzzle;
    float tall = fmaxf(0, top - 12); /* a tall landmark: the camera rises and looks up its height */
    float lcv[6];
    memcpy(lcv, g->L->cam, sizeof lcv);
    if (opt_get("CAM")) sscanf(opt_get("CAM"), "%f,%f,%f,%f,%f,%f", &lcv[0], &lcv[1], &lcv[2], &lcv[3], &lcv[4], &lcv[5]); /* development: the level's aim view tuned by eye (dm set CAM back,up,side,short,aty,fov) */
    const float *lc = lcv; /* a level's own aim view (level_def.cam) */
    if (lc[5] > 0) {
      eye = (t3_vec3){ m.x - fwd.x * lc[0] + right.x * lc[2], m.y + lc[1], m.z - fwd.z * lc[0] + right.z * lc[2] };
      at = (t3_vec3){ m.x + fwd.x * (g->ring - lc[3]), lc[4], m.z + fwd.z * (g->ring - lc[3]) };
    } else {
      eye = (t3_vec3){ m.x - fwd.x * (8.5f + tall * 0.4f) + right.x * 2.6f, m.y + 3.6f + tall * 0.15f, m.z - fwd.z * (8.5f + tall * 0.4f) + right.z * 2.6f };
      float pitch_look = cw_clampf(g->pitch, -0.05f, 0.5f);
      at = (t3_vec3){ m.x + fwd.x * g->ring, m.y + top * 0.25f + tall * 0.3f + pitch_look * 8 - 1.0f, m.z + fwd.z * g->ring };
    }
    k = g->state == V.last_state || g->ready ? 4 : 2;
  } else if (g->state == GS_FLIGHT && g->proj_live) {
    cw_xform3 x = cw_body3d_interpolated(g->proj, alpha);
    t3_vec3 v = g->proj_vel;
    float vl = sqrtf(v.x * v.x + v.z * v.z) + 1e-3f;
    t3_vec3 vd = { v.x / vl, 0, v.z / vl };
    /* the shot and the structure both in the picture: the camera starts behind the launcher and closes in behind
     * the shot as it nears, looking between the two */
    float d0 = sqrtf(g->muzzle.x * g->muzzle.x + g->muzzle.z * g->muzzle.z) + 1e-3f, d1 = sqrtf(x.p.x * x.p.x + x.p.z * x.p.z);
    float near = cw_clampf(1 - d1 / d0, 0, 1) * 0.75f;
    t3_vec3 e0 = { g->muzzle.x - fwd.x * 9 + right.x * 3, g->muzzle.y + 5, g->muzzle.z - fwd.z * 9 + right.z * 3 };
    t3_vec3 e1 = { x.p.x - vd.x * 9 + vd.z * 4.0f, fmaxf(x.p.y, top * 0.6f) + 3.0f, x.p.z - vd.z * 9 - vd.x * 4.0f };
    eye = lerp3(e0, e1, near);
    at = lerp3(x.p, mid, 0.55f);
    k = 4;
  } else if (g->state == GS_WON || g->state == GS_LOST) {
    V.orbit += dt * 0.12f;
    float r = ext * 2.2f + 12;
    eye = (t3_vec3){ sinf(V.orbit + g->orbit + 0.7f) * r, top * 0.6f + 6, cosf(V.orbit + g->orbit + 0.7f) * r };
    at = mid;
    k = 1.5f;
  } else {
    /* side-on to the collapse: off the shot's line, far enough for the whole structure */
    t3_vec3 c = g->proj_hit_tick >= 0 ? g->impact : mid;
    c = lerp3(c, mid, 0.5f);
    float r = ext * 1.7f + 9;
    if (g->hot_n > 0) { /* follow the collapse: the middle of what is in motion, far enough back for all of it */
      c = g->hot_c;
      r = fmaxf(ext * 1.1f + 7, g->hot_r * 2.2f + 8);
    }
    float a = g->orbit + 0.75f + (float)g->state_t * 0.0012f; /* off to the side of where the shot came from */
    eye = (t3_vec3){ c.x + sinf(a) * r, fmaxf(3.0f, c.y * 0.8f + r * 0.28f), c.z + cosf(a) * r };
    at = (t3_vec3){ c.x, fmaxf(1.5f, c.y * 0.8f), c.z };
    k = 1.8f;
  }
  V.wind += dt;
  trees_sway(V.wind);
  if (!V.cam_init) V.eye = eye, V.at = at, V.cam_init = true;
  float t = 1 - expf(-k * dt);
  V.eye = lerp3(V.eye, eye, t);
  V.at = lerp3(V.at, at, t);
  if (V.eye.y < 0.6f) V.eye.y = 0.6f;
  V.last_state = g->state;
  V.aspect = aspect;
  cw_node3d_set_position(camera, V.eye);
  cw_node3d_look_at(camera, V.at, (t3_vec3){ 0, 1, 0 });
  float want_fov = aimv && !overview ? (g->L->cam[5] > 0 ? (opt_get("CAM") ? (float)atof(strrchr(opt_get("CAM"), ',') + 1) : g->L->cam[5]) : 32.0f) : 50.0f; /* aiming looks long down the field */
  V.fov += (want_fov - V.fov) * t;
  cw_node_set(camera, "fov", cw_val_float(V.fov));
  cw_node_set(camera, "near", cw_val_float(0.2f));
  cw_node_set(camera, "far", cw_val_float(600));
  float dx = V.eye.x - V.at.x, dy = V.eye.y - V.at.y, dz = V.eye.z - V.at.z;
  V.st.cam_dist = sqrtf(dx * dx + dy * dy + dz * dz);
}

/* the replay's camera: close behind and beside the shot in flight, then a low, slow turn round where it hit on a
 * longer lens while the slow motion runs, widening as the structure settles. cut: the replay's first frame */
void view_camera_replay(const game *g, cw_node camera, float aspect, float dt, bool slow, bool cut, float alpha) {
  t3_vec3 eye, at;
  float ext = g->extent, top = g->top, k = 3;
  t3_vec3 mid = { 0, top * 0.42f, 0 };
  if (cut) V.rp_orbit = 0;
  if (g->proj_hit_tick < 0 && g->proj_live) {
    cw_xform3 x = cw_body3d_interpolated(g->proj, alpha);
    t3_vec3 v = g->proj_vel;
    float vl = sqrtf(v.x * v.x + v.z * v.z) + 1e-3f;
    t3_vec3 vd = { v.x / vl, 0, v.z / vl };
    eye = (t3_vec3){ x.p.x - vd.x * 6 - vd.z * 3.2f, x.p.y + 1.6f, x.p.z - vd.z * 6 + vd.x * 3.2f };
    at = (t3_vec3){ x.p.x + vd.x * 5, x.p.y, x.p.z + vd.z * 5 };
    k = 8;
  } else {
    t3_vec3 c = g->proj_hit_tick >= 0 ? g->impact : mid;
    c = lerp3(c, mid, 0.35f);
    V.rp_orbit += dt * (slow ? 0.05f : 0.14f);
    float settle = cw_clampf((float)(g->tick - g->proj_hit_tick) / 400.0f, 0, 1);
    float r = (ext * 1.05f + 6) * (1 + 0.45f * settle);
    float a = g->orbit - 0.9f + V.rp_orbit;
    eye = (t3_vec3){ c.x + sinf(a) * r, fmaxf(1.4f, top * (0.25f + 0.25f * settle) + 1.5f), c.z + cosf(a) * r };
    at = (t3_vec3){ c.x, fmaxf(1.2f, c.y * 0.75f), c.z };
    k = 2.2f;
  }
  V.wind += dt;
  trees_sway(V.wind);
  if (cut || !V.cam_init) V.eye = eye, V.at = at, V.cam_init = true;
  float t = 1 - expf(-k * dt);
  V.eye = lerp3(V.eye, eye, t);
  V.at = lerp3(V.at, at, t);
  if (V.eye.y < 0.6f) V.eye.y = 0.6f;
  V.aspect = aspect;
  cw_node3d_set_position(camera, V.eye);
  cw_node3d_look_at(camera, V.at, (t3_vec3){ 0, 1, 0 });
  float want_fov = slow ? 36.0f : 46.0f;
  if (cut) V.fov = want_fov;
  V.fov += (want_fov - V.fov) * (1 - expf(-2 * dt));
  cw_node_set(camera, "fov", cw_val_float(V.fov));
  V.last_state = -1; /* back in play the camera eases from here */
}

bool view_screen(t3_vec3 p, float *u, float *v) {
  if (V.aspect <= 0) return false;
  t3_vec3 f = { V.at.x - V.eye.x, V.at.y - V.eye.y, V.at.z - V.eye.z };
  float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
  f.x /= fl, f.y /= fl, f.z /= fl;
  t3_vec3 r = { -f.z, 0, f.x };
  float rl = sqrtf(r.x * r.x + r.z * r.z);
  r.x /= rl, r.z /= rl;
  t3_vec3 up = { r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };
  t3_vec3 d = { p.x - V.eye.x, p.y - V.eye.y, p.z - V.eye.z };
  float zz = d.x * f.x + d.y * f.y + d.z * f.z;
  if (zz <= 0.1f) return false;
  float th = tanf(V.fov * 3.14159265f / 360.0f);
  float sx = (d.x * r.x + d.y * r.y + d.z * r.z) / (zz * th * V.aspect), sy = (d.x * up.x + d.y * up.y + d.z * up.z) / (zz * th);
  *u = 0.5f + sx * 0.5f, *v = 0.5f - sy * 0.5f;
  return true;
}

bool view_hidden(const game *g, int t) {
  if (t < 0 || t >= g->ntgt || g->tgt[t].blk < 0) return false;
  t3_vec3 c = game_block_xform(g, g->tgt[t].blk).p;
  float so = sinf(g->orbit), co = cosf(g->orbit);
  t3_vec3 eye = { g->muzzle.x + so * 8.5f + co * 2.6f, g->muzzle.y + 3.6f, g->muzzle.z + co * 8.5f - so * 2.6f };
  cw_phys_filter f = cw_phys_filter_layers(1u << 1);
  cw_ray_hit3d hit;
  if (!cw_world3d_raycast(g->w, eye, (t3_vec3){ c.x - eye.x, c.y - eye.y, c.z - eye.z }, &f, &hit)) return false;
  return game_block_of_shape(hit.shape) != g->tgt[t].blk;
}

view_stats view_get_stats(void) { return V.st; }
