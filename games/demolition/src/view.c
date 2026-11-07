/* demolition's view: render only; nothing here feeds the simulation.
 *
 *   blocks     one instanced box per material (wood, stone, glass, steel) with a texture of its own made here
 *              (grain, speckle, brushed paint with rivets, a glass edge), every block an instance scaled to its
 *              size and tinted by the level's palette; crystals are glowing octahedra, each with a light while it
 *              stands
 *   field      a grass plain with a worn dirt yard round the structure, a ring of hills, trees, fences
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
#include "cartwheel/render3d.h"
#include "view.h"

#define NPATH 48
#define NTREES 90

bool g_view_no_path;

static struct {
  cw_node root, field, sky, sun, post;
  t3_instanced_mesh *poles, *flags, *beams;
  t3_instanced_mesh *blk[M_COUNT];
  t3_instanced_mesh *path, *trees_trunk, *trees_top;
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
  int built_level;
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

/* a block face: luminance detail (the instance colour gives the hue), darker at the edges */
static t3_texture *block_texture(int mat) {
  enum { N = 256 };
  static uint8_t px[N * N * 4];
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
      float e = fminf(fminf(u, 1 - u), fminf(v, 1 - v));
      float edge = 0.62f + 0.38f * cw_smoothstep(0.0f, 0.07f, e);
      float r = 1, g = 1, b = 1, a = 1;
      switch (mat) {
      case M_WOOD: {
        float n = fbm(u * 3, v * 24, 11);
        float grain = 0.5f + 0.5f * sinf((u * 7 + n * 2.2f) * 6.2831853f * 2.5f);
        float k = 0.72f + 0.2f * grain + 0.12f * (fbm(u * 18, v * 3, 5) - 0.5f);
        float knot = 1 - 0.35f * cw_smoothstep(0.06f, 0.0f, sqrtf((u - 0.63f) * (u - 0.63f) * 9 + (v - 0.35f) * (v - 0.35f)) - 0.02f);
        r = g = b = k * knot * edge;
        break;
      }
      case M_STONE: {
        float n = fbm(u * 6, v * 6, 21), sp = vnoise(u * 60, v * 60, 3);
        float pit = sp > 0.86f ? 0.78f : 1;
        float chip = 1 - 0.25f * cw_smoothstep(0.035f, 0.0f, e + (fbm(u * 20, v * 20, 9) - 0.5f) * 0.04f);
        float k = (0.78f + 0.24f * (n - 0.5f) * 2) * pit * chip;
        r = k * edge, g = k * edge, b = k * edge;
        break;
      }
      case M_STEEL: {
        float brush = 0.92f + 0.08f * vnoise(u * 3, v * 120, 31);
        float k = brush * (0.9f + 0.1f * fbm(u * 8, v * 8, 4));
        /* rivets near the corners and along the edges */
        for (int i = 0; i < 4; i++) {
          float cx = i & 1 ? 0.91f : 0.09f, cy = i & 2 ? 0.91f : 0.09f;
          float d = sqrtf((u - cx) * (u - cx) + (v - cy) * (v - cy));
          if (d < 0.035f) k *= 0.6f + 0.6f * (1 - d / 0.035f);
        }
        float wear = fbm(u * 10, v * 10, 41) > 0.72f ? 0.55f : 1; /* paint worn to the metal */
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
      p[0] = u8(r), p[1] = u8(g), p[2] = u8(b), p[3] = u8(a);
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
    } else {
      static const float rough[M_COUNT] = { 0.78f, 0.88f, 0.06f, 0.42f, 0 };
      static const float metal[M_COUNT] = { 0, 0, 0, 0.55f, 0 };
      mt = std_mat(1, 1, 1, rough[m], metal[m]);
      t3_texture *tx = block_texture(m);
      t3_material_set_map(mt, tx);
      t3_release(tx);
      if (m == M_GLASS) {
        mt->transparent = true;
        mt->opacity = 1;
        mt->depth_write = false;
        mt->emissive = (t3_color){ 1.0f, 0.62f, 0.28f }; /* lit windows: a lamp's warm light inside */
        mt->emissive_intensity = 1.6f;
      }
      geo = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
    }
    V.blk[m] = imesh(world, MATS[m].name, geo, mt, MAXBLK, m != M_GLASS);
  }
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

/* ── the settings: each kit stands somewhere of its own ─────────────── */

enum { BD_HILLS, BD_MOUNTAINS, BD_MESAS, BD_SEA, BD_GORGE, BD_QUARRY };
enum { TR_NONE, TR_ROUND, TR_PINES, TR_AUTUMN };
typedef struct setting {
  float tod, north, sun_energy, sky_energy, exposure;
  float ground[3], yard[3];
  float fog[3], fog_density;
  int backdrop;
  float back_low[3], back_high[3];
  int trees;
  /* the materials' colours here: wood, stone, glass (lit windows), steel */
  float pal[4][3];
} setting;

static const setting SETTINGS[K_COUNT] = {
  /* tower: a highland valley under grey-blue mountains, green trees */
  [K_TOWER] = { 15.7f, 55, 4.6f, 1.6f, 1.7f, { 0.17f, 0.36f, 0.07f }, { 0.42f, 0.30f, 0.18f }, { 0.55f, 0.65f, 0.8f }, 0.0016f,
                BD_MOUNTAINS, { 0.1f, 0.16f, 0.12f }, { 1.0f, 1.0f, 1.0f }, TR_ROUND,
                { { 0.80f, 0.48f, 0.24f }, { 0.78f, 0.74f, 0.66f }, { 1.0f, 0.82f, 0.5f }, { 0.88f, 0.2f, 0.12f } } },
  /* wall: a golden meadow, low hills, autumn woods */
  [K_WALL] = { 16.7f, 35, 4.6f, 1.6f, 1.7f, { 0.30f, 0.42f, 0.08f }, { 0.46f, 0.32f, 0.17f }, { 0.75f, 0.65f, 0.5f }, 0.0016f,
               BD_HILLS, { 0.25f, 0.42f, 0.14f }, { 0.45f, 0.5f, 0.2f }, TR_AUTUMN,
               { { 0.82f, 0.5f, 0.25f }, { 0.92f, 0.78f, 0.55f }, { 1.0f, 0.8f, 0.45f }, { 0.2f, 0.4f, 0.9f } } },
  /* bridge: a river gorge between steep rock walls, pines */
  [K_BRIDGE] = { 15.2f, 70, 4.6f, 1.6f, 1.7f, { 0.15f, 0.34f, 0.10f }, { 0.40f, 0.33f, 0.25f }, { 0.55f, 0.68f, 0.75f }, 0.0018f,
                 BD_GORGE, { 0.42f, 0.34f, 0.28f }, { 0.62f, 0.55f, 0.46f }, TR_PINES,
                 { { 0.75f, 0.45f, 0.22f }, { 0.8f, 0.72f, 0.62f }, { 1.0f, 0.8f, 0.45f }, { 0.25f, 0.28f, 0.33f } } },
  /* castle: a headland over the sea, the cliffs falling away behind */
  [K_CASTLE] = { 15.9f, 50, 4.6f, 1.6f, 1.6f, { 0.2f, 0.42f, 0.1f }, { 0.5f, 0.42f, 0.3f }, { 0.65f, 0.78f, 0.92f }, 0.0014f,
                 BD_SEA, { 0.32f, 0.5f, 0.2f }, { 0.4f, 0.55f, 0.25f }, TR_ROUND,
                 { { 0.78f, 0.46f, 0.22f }, { 0.9f, 0.86f, 0.78f }, { 1.0f, 0.85f, 0.5f }, { 0.2f, 0.3f, 0.6f } } },
  /* scaffold: a quarry's floor among terraced orange walls */
  [K_SCAFFOLD] = { 15.4f, 65, 4.6f, 1.6f, 1.72f, { 0.38f, 0.34f, 0.3f }, { 0.3f, 0.26f, 0.22f }, { 0.75f, 0.68f, 0.6f }, 0.0018f,
                   BD_QUARRY, { 0.26f, 0.15f, 0.09f }, { 0.9f, 0.62f, 0.38f }, TR_NONE,
                   { { 0.82f, 0.55f, 0.28f }, { 0.75f, 0.72f, 0.68f }, { 0.55f, 0.9f, 1.0f }, { 1.0f, 0.72f, 0.08f } } },
  /* pyramid: desert sand under red mesas, a hot haze */
  [K_PYRAMID] = { 16.6f, 40, 4.6f, 1.6f, 1.5f, { 0.76f, 0.48f, 0.26f }, { 0.5f, 0.32f, 0.18f }, { 0.95f, 0.75f, 0.55f }, 0.0016f,
                  BD_MESAS, { 0.32f, 0.11f, 0.06f }, { 0.9f, 0.48f, 0.26f }, TR_NONE,
                  { { 0.75f, 0.45f, 0.2f }, { 0.95f, 0.8f, 0.55f }, { 0.4f, 0.95f, 0.9f }, { 0.75f, 0.2f, 0.12f } } },
  /* keep: a snowy pass between white peaks, dark pines, a cold light */
  [K_KEEP] = { 14.2f, 75, 4.6f, 1.6f, 1.06f, { 0.7f, 0.74f, 0.82f }, { 0.36f, 0.32f, 0.3f }, { 0.75f, 0.82f, 0.95f }, 0.0022f,
               BD_MOUNTAINS, { 0.09f, 0.11f, 0.15f }, { 1.0f, 1.0f, 1.0f }, TR_PINES,
               { { 0.72f, 0.42f, 0.2f }, { 0.55f, 0.56f, 0.6f }, { 1.0f, 0.75f, 0.35f }, { 0.75f, 0.15f, 0.1f } } },
};
static const setting *SET;

/* the paints (game.h P_*), linear colours */
static const float PAINT[P_COUNT][3] = {
  [P_ROOF_RED] = { 0.62f, 0.12f, 0.06f }, [P_ROOF_SLATE] = { 0.12f, 0.2f, 0.38f }, [P_ROOF_COPPER] = { 0.16f, 0.5f, 0.42f },
  [P_TRIM_CREAM] = { 0.98f, 0.88f, 0.62f }, [P_TIMBER] = { 0.22f, 0.12f, 0.06f }, [P_PLASTER] = { 0.96f, 0.93f, 0.85f },
  [P_GOLD] = { 1.0f, 0.68f, 0.12f }, [P_TURQUOISE] = { 0.05f, 0.55f, 0.6f }, [P_BRICK] = { 0.62f, 0.2f, 0.12f },
  [P_DARK_STONE] = { 0.26f, 0.26f, 0.3f }, [P_SAND_STONE] = { 0.95f, 0.72f, 0.42f }, [P_ROSE_STONE] = { 0.85f, 0.52f, 0.45f },
  [P_WHITE_STONE] = { 0.95f, 0.94f, 0.9f },
};
static const float BANNER[4][3] = { { 0.85f, 0.08f, 0.06f }, { 0.08f, 0.22f, 0.8f }, { 1.0f, 0.72f, 0.05f }, { 0.08f, 0.55f, 0.2f } };

/* the backdrop: a ring of land far out, its shape by kind, coloured from low to high (vertex colours) */
static float terrace(float h, float step) { float k = floorf(h / step); float f = h / step - k; return (k + cw_smoothstep(0.7f, 0.95f, f)) * step; }
static t3_geometry *backdrop_geo(const setting *S, uint32_t seed) {
  enum { NA = 160, NR = 16 };
  float r_in = S->backdrop == BD_QUARRY ? 60 : S->backdrop == BD_GORGE ? 70 : S->backdrop == BD_MOUNTAINS ? 150 : 110;
  float r_out = S->backdrop == BD_QUARRY ? 130 : S->backdrop == BD_MOUNTAINS ? 440 : 320;
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
      case BD_QUARRY: h = terrace(rise * (14 + n * 24), 6); top = 34; break;
      case BD_GORGE: h = (40 + n * 50) * cw_smoothstep(0.0f, 0.3f, t) * cw_smoothstep(30, 80, fabsf(x)); top = 80; break;
      case BD_SEA: h = z > -40 ? (n * 22 + 4) * rise : -60; top = 30; break; /* none over the sea */
      default: h = (n * 30 + 6) * rise * (1 - 0.3f * t); top = 35; break;
      }
      int i = r * NA + a;
      pos[i * 3] = x, pos[i * 3 + 1] = h - 1.5f, pos[i * 3 + 2] = z;
      uv[i * 2] = ang * 24, uv[i * 2 + 1] = rad * 0.04f + h * 0.05f; /* steep faces stretch the texture less */
      float k = cw_clampf(h / top, 0, 1);
      if (S->backdrop == BD_MOUNTAINS) k = cw_smoothstep(0.62f, 0.8f, k); /* snow on the peaks alone */
      float band = (S->backdrop == BD_QUARRY || S->backdrop == BD_MESAS) ? 0.82f + 0.18f * sinf(h * 0.9f) : 1; /* strata */
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

void view_level(const game *g) {
  if (cw_node_valid(V.field)) cw_node_free(V.field);
  V.field = cw_node_new("Node3D", "Field");
  cw_node_add_child(V.root, V.field);
  const setting *S = SET = &SETTINGS[g->L->kind];
  for (int m = 0; m < 4; m++) memcpy(V.pal[m], S->pal[m], sizeof V.pal[m]);
  V.pal[M_TARGET][0] = V.pal[M_TARGET][1] = V.pal[M_TARGET][2] = 1;
  /* the light: a strong low sun behind the launcher, a cooler sky */
  if (cw_node_valid(V.sky)) {
    float t = S->tod, n = S->north;
    if (getenv("DEMOLITION_TOD")) t = (float)atof(getenv("DEMOLITION_TOD")); /* development: trying the light */
    if (getenv("DEMOLITION_NORTH")) n = (float)atof(getenv("DEMOLITION_NORTH"));
    cw_node_set(V.sky, "time_of_day", cw_val_float(t));
    cw_node_set(V.sky, "north", cw_val_float(n));
    if (getenv("DEMOLITION_MIE")) cw_node_set(V.sky, "mie", cw_val_float((float)atof(getenv("DEMOLITION_MIE"))));
    if (getenv("DEMOLITION_RAYLEIGH")) cw_node_set(V.sky, "rayleigh", cw_val_float((float)atof(getenv("DEMOLITION_RAYLEIGH"))));
    cw_node_set(V.sky, "sun_energy", cw_val_float(getenv("DEMOLITION_SUN") ? (float)atof(getenv("DEMOLITION_SUN")) : S->sun_energy));
    cw_node_set(V.sky, "sky_energy", cw_val_float(getenv("DEMOLITION_SKY") ? (float)atof(getenv("DEMOLITION_SKY")) : S->sky_energy));
  }
  if (cw_node_valid(V.post)) {
    float e = getenv("DEMOLITION_EXPOSURE") ? (float)atof(getenv("DEMOLITION_EXPOSURE")) : S->exposure;
    if (getenv("DEMOLITION_LOOK_CONTROL")) e *= 3.0f; /* the look gate's control: a washed-out picture */
    cw_node_set(V.post, "exposure", cw_val_float(e));
    if (getenv("DEMOLITION_TONEMAP")) cw_node_set(V.post, "tonemap", cw_val_enum(getenv("DEMOLITION_TONEMAP")));
  }
  /* distance: height fog the colour of the setting's air (the structure stays clear: it starts 45 m out) */
  {
    cw_node f = cw_node_new("HeightFog", "Haze");
    cw_node_add_child(V.field, f);
    cw_node_set(f, "density", cw_val_float(getenv("DEMOLITION_NOFOG") ? 0.0f : S->fog_density));
    cw_node_set(f, "height_falloff", cw_val_float(0.035f));
    cw_node_set(f, "use_sky_color", cw_val_bool(false));
    cw_node_set(f, "color", cw_val_color(S->fog[0], S->fog[1], S->fog[2], 1));
    cw_node_set(f, "start_distance", cw_val_float(70));
    cw_node_set(f, "sky_distance", cw_val_float(0));
    cw_node_set(f, "max_opacity", cw_val_float(0.55f));
  }
  /* the ground */
  bool sea = S->backdrop == BD_SEA;
  flat("Ground", 640, sea ? 360 : 640, (t3_vec3){ 0, 0, sea ? 140 : 0 }, ground_mat(S->ground, 50, sea ? 28 : 50, 0, 0.95f));
  if (sea) { /* the headland ends at a cliff behind the castle; the sea far below to the horizon */
    t3_material *cm = std_mat(S->back_low[0] * 0.9f + 0.25f, S->back_low[1] * 0.6f + 0.22f, S->back_low[2] * 0.5f + 0.2f, 0.95f, 0);
    t3_geometry *cg = t3_plane_geometry_new(640, 40, 1, 1);
    xform_geo(cg, (t3_vec3){ 0, -20, -40 }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 });
    piece(V.field, "Cliff", cg, cm, false);
    t3_material *sm = std_mat(0.03f, 0.18f, 0.32f, 0.08f, 0);
    flat("Sea", 1400, 700, (t3_vec3){ 0, -38, -390 }, sm);
  }
  if (S->backdrop == BD_GORGE) { /* a river under the bridge's first span, running down the gorge */
    t3_material *wm = std_mat(0.04f, 0.2f, 0.3f, 0.06f, 0);
    flat("River", 4.0f, 600, (t3_vec3){ 4.8f, 0.05f, 0 }, wm);
    flat("Bank", 7.0f, 600, (t3_vec3){ 4.8f, 0.035f, 0 }, ground_mat(S->yard, 2, 120, 3, 0.9f));
  }
  /* the yard round the structure and the track to the launcher */
  {
    float r = g->extent + 5;
    t3_geometry *cg = t3_circle_geometry_new(r, 48);
    xform_geo(cg, (t3_vec3){ 0, 0.03f, 0 }, qaxis(1, 0, 0, -1.5707963f), (t3_vec3){ 1, 1, 1 });
    piece(V.field, "Yard", cg, ground_mat(S->yard, 6, 6, 5, 0.98f), false);
    float len = g->muzzle.z + 4 - r * 0.5f;
    flat("Track", 4.5f, len, (t3_vec3){ 0, 0.04f, r * 0.5f + len * 0.5f }, ground_mat(S->yard, 1, 8, 5, 0.98f));
  }
  /* the backdrop */
  {
    t3_material *m = std_mat(1, 1, 1, 0.92f, 0);
    m->vertex_colors = true;
    static const float grey[3] = { 0.85f, 0.85f, 0.85f };
    t3_texture *t = ground_texture(grey, 9);
    t3_material_set_map(m, t);
    t3_release(t);
    piece(V.field, "Backdrop", backdrop_geo(S, g->L->seed), m, false);
  }
  /* trees: round crowns, autumn crowns or pines, outside the field of fire */
  if (S->trees != TR_NONE) {
    bool pines = S->trees == TR_PINES;
    t3_material *tm = std_mat(0.32f, 0.2f, 0.11f, 0.9f, 0);
    t3_material *cm = std_mat(1, 1, 1, 0.85f, 0);
    t3_geometry *trunk = xform_geo(t3_cylinder_geometry_new(0.18f, 0.28f, 2.4f, 8, 1, false), (t3_vec3){ 0, 1.2f, 0 }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 });
    t3_geometry *crown = pines ? xform_geo(t3_cone_geometry_new(1.7f, 6.5f, 9, 1, false), (t3_vec3){ 0, 4.6f, 0 }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 })
                               : xform_geo(t3_icosahedron_geometry_new(1.6f, 1), (t3_vec3){ 0, 3.4f, 0 }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1.15f, 1 });
    V.trees_trunk = imesh(V.field, "Trunks", trunk, tm, NTREES, true);
    V.trees_top = imesh(V.field, "Crowns", crown, cm, NTREES, true);
    int n = 0;
    for (int i = 0; n < NTREES && i < NTREES * 4; i++) {
      float a = hashf(g->L->seed * 31u + (uint32_t)i * 7u) * 6.2831853f, r = 35 + hashf(g->L->seed * 17u + (uint32_t)i * 13u) * 70;
      float x = cosf(a) * r, z = sinf(a) * r;
      if (fabsf(x) < 18 && z > -10 && z < g->muzzle.z + 12) continue; /* the field of fire and the launcher's place */
      if (x * x + z * z < (g->extent + 12) * (g->extent + 12)) continue;
      if (sea && z < -36) continue;
      if (S->backdrop == BD_GORGE && fabsf(x - 4.8f) < 5) continue;
      float s = 0.75f + hashf((uint32_t)i * 101u) * 0.7f;
      t3_mat4 m;
      t3_mat4_compose(&m, (t3_vec3){ x, 0, z }, qaxis(0, 1, 0, a * 3), (t3_vec3){ s, s * (pines ? 1.1f : 1), s });
      t3_instanced_mesh_set_matrix_at(V.trees_trunk, n, &m);
      t3_instanced_mesh_set_matrix_at(V.trees_top, n, &m);
      float k = 0.75f + hashf((uint32_t)i * 59u) * 0.5f, hsel = hashf((uint32_t)i * 7u);
      t3_color c = pines ? (t3_color){ 0.06f * k, 0.22f * k, 0.12f * k } : (t3_color){ 0.16f * k, 0.42f * k, 0.1f * k };
      if (S->trees == TR_AUTUMN) c = hsel < 0.4f ? (t3_color){ 0.85f * k, 0.32f * k, 0.06f * k } : hsel < 0.7f ? (t3_color){ 0.9f * k, 0.62f * k, 0.1f * k } : c;
      t3_instanced_mesh_set_color_at(V.trees_top, n, c);
      t3_instanced_mesh_set_color_at(V.trees_trunk, n, (t3_color){ 1, 1, 1 });
      n++;
    }
    V.trees_trunk->count = V.trees_top->count = n;
    t3_sphere sph = { { 0, 4, 0 }, 130 };
    t3_instanced_mesh_set_bounding_sphere(V.trees_trunk, &sph);
    t3_instanced_mesh_set_bounding_sphere(V.trees_top, &sph);
  }
  build_launcher(g);
  V.built_level = g->level;
  V.cam_init = false;
}

/* ── every frame ───────────────────────────────────────────────────── */

void view_sync(const game *g, float alpha) {
  int cnt[M_COUNT] = { 0 };
  for (int i = 0; i < g->nb; i++) {
    const block *k = &g->b[i];
    if (!k->alive) continue;
    int m = k->mat;
    t3_instanced_mesh *im = V.blk[m];
    int s = cnt[m]++;
    cw_xform3 x = cw_body3d_interpolated(k->body, alpha);
    t3_mat4 mx;
    t3_vec3 sc = m == M_TARGET ? (t3_vec3){ k->half.x * 2, k->half.y * 2, k->half.z * 2 } : (t3_vec3){ k->half.x * 2, k->half.y * 2, k->half.z * 2 };
    t3_mat4_compose(&mx, x.p, x.q, sc);
    t3_instanced_mesh_set_matrix_at(im, s, &mx);
    float v = 0.8f + 0.4f * (float)k->tint / 255.0f, hue = ((k->tint * 37) & 255) / 255.0f - 0.5f; /* each block its own shade, a little warmer or cooler */
    const float *c = V.pal[m];
    float dmg = 1;
    if (m != M_TARGET) {
      float full = MATS[m].toughness * k->vol * (k->gen ? 0.75f : 1.0f);
      float f = full > 0 ? k->hp / full : 1;
      dmg = 0.72f + 0.28f * cw_clampf(f, 0, 1); /* cracked blocks darken */
    }
    /* painted wood: one plank in eight takes a bright colour */
    t3_color col = { c[0] * v * dmg * (1 + hue * 0.12f), c[1] * v * dmg, c[2] * v * dmg * (1 - hue * 0.12f) };
    if (k->paint) { /* the builder's paint: roofs, trims, timber, gold */
      const float *p = PAINT[k->paint];
      float pv = 0.9f + 0.2f * (float)k->tint / 255.0f;
      col = (t3_color){ p[0] * pv * dmg, p[1] * pv * dmg, p[2] * pv * dmg };
    } else if (m == M_WOOD && (k->tint & 7) == 0) { /* painted wood: one plank in eight takes a bright colour */
      static const float paint[4][3] = { { 0.85f, 0.2f, 0.15f }, { 0.15f, 0.45f, 0.85f }, { 0.95f, 0.75f, 0.15f }, { 0.2f, 0.65f, 0.45f } };
      const float *p = paint[(k->tint >> 3) & 3];
      col = (t3_color){ p[0] * dmg, p[1] * dmg, p[2] * dmg };
    }
    t3_instanced_mesh_set_color_at(im, s, col);
  }
  int inst = 0;
  for (int m = 0; m < M_COUNT; m++) {
    V.blk[m]->count = cnt[m];
    V.blk[m]->instance_matrix->version++;
    if (V.blk[m]->instance_color) V.blk[m]->instance_color->version++;
    inst += cnt[m];
  }
  V.st.instances = inst;
  /* the crystals' lights */
  int lights = 0;
  for (int t = 0; t < MAXTGT; t++) {
    bool on = t < g->ntgt && !g->tgt[t].down && g->tgt[t].blk >= 0;
    if (on) {
      cw_xform3 x = cw_body3d_interpolated(g->b[g->tgt[t].blk].body, alpha);
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
      cw_xform3 x = cw_body3d_interpolated(g->b[g->tgt[t].blk].body, alpha);
      float pulse = 1 + 0.08f * sinf((float)g->tick * 0.05f + t * 1.7f);
      t3_mat4 mx;
      t3_mat4_compose(&mx, (t3_vec3){ x.p.x, x.p.y + 0.3f, x.p.z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ pulse, 18, pulse });
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
      cw_xform3 x = cw_body3d_interpolated(k->body, alpha);
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
  /* the launcher follows the aim */
  if (cw_node_valid(V.yaw)) {
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
  /* the aim path: the first stretch of the flight, as the world will fly it */
  int np = 0;
  if (g->state == GS_AIM && !g_view_no_path) {
    t3_vec3 d = game_aim_dir(g);
    float sp = game_speed(g);
    t3_vec3 p = { g->muzzle.x + d.x * 1.4f, g->muzzle.y + d.y * 1.4f, g->muzzle.z + d.z * 1.4f };
    t3_vec3 v = { d.x * sp, d.y * sp, d.z * sp };
    float h = TICK / 4, along = 0, next = 1.2f;
    float reach = g->muzzle.z * 0.42f; /* the path shows the first part of the way */
    for (int i = 0; i < 4 * 60 * 6 && np < NPATH; i++) {
      v.y -= GRAV * h;
      t3_vec3 q = { p.x + v.x * h, p.y + v.y * h, p.z + v.z * h };
      along += sqrtf((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y) + (q.z - p.z) * (q.z - p.z));
      p = q;
      if (p.y < 0) break;
      if (along >= next) {
        float f = 1 - (float)np / NPATH;
        float r = 0.07f + 0.05f * f;
        t3_mat4 m;
        t3_mat4_compose(&m, p, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ r, r, r });
        t3_instanced_mesh_set_matrix_at(V.path, np, &m);
        float b = 0.35f + 0.65f * f;
        t3_instanced_mesh_set_color_at(V.path, np, (t3_color){ b, b, b });
        np++;
        next += 1.1f;
        if (along > reach) break;
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
  float sy = sinf(g->yaw), cy = cosf(g->yaw);
  t3_vec3 fwd = { -sy, 0, -cy }, right = { cy, 0, -sy };
  float ext = g->extent, top = g->top;
  t3_vec3 mid = { 0, top * 0.42f, 0 };
  if (overview) {
    eye = (t3_vec3){ ext * 1.6f + 10, top * 0.9f + 8, g->muzzle.z * 0.55f };
    at = mid;
    k = 4;
  } else if (g->state == GS_AIM) {
    /* behind the launcher, a little to the right, looking down the aim over the structure */
    t3_vec3 m = g->muzzle;
    eye = (t3_vec3){ m.x - fwd.x * 8.5f + right.x * 2.6f, m.y + 3.6f, m.z - fwd.z * 8.5f + right.z * 2.6f };
    float pitch_look = cw_clampf(g->pitch, -0.05f, 0.5f);
    at = (t3_vec3){ m.x + fwd.x * m.z, m.y + top * 0.25f + pitch_look * 8 - 1.0f, m.z + fwd.z * m.z };
    k = g->state == V.last_state ? 4 : 2;
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
    eye = (t3_vec3){ sinf(V.orbit + 0.7f) * r, top * 0.6f + 6, cosf(V.orbit + 0.7f) * r };
    at = mid;
    k = 1.5f;
  } else {
    /* side-on to the collapse: off the shot's line, far enough for the whole structure */
    t3_vec3 c = g->proj_hit_tick >= 0 ? g->impact : mid;
    c = lerp3(c, mid, 0.5f);
    float r = ext * 1.7f + 9;
    float a = 0.75f + (float)g->state_t * 0.0012f;
    eye = (t3_vec3){ c.x + sinf(a) * r, top * 0.55f + 4.5f, c.z + cosf(a) * r };
    at = (t3_vec3){ c.x, fmaxf(1.5f, c.y * 0.7f), c.z };
    k = 1.8f;
  }
  if (!V.cam_init) V.eye = eye, V.at = at, V.cam_init = true;
  float t = 1 - expf(-k * dt);
  V.eye = lerp3(V.eye, eye, t);
  V.at = lerp3(V.at, at, t);
  if (V.eye.y < 0.6f) V.eye.y = 0.6f;
  V.last_state = g->state;
  V.aspect = aspect;
  cw_node3d_set_position(camera, V.eye);
  cw_node3d_look_at(camera, V.at, (t3_vec3){ 0, 1, 0 });
  float want_fov = g->state == GS_AIM && !overview ? 32.0f : 50.0f; /* aiming looks long down the field */
  V.fov += (want_fov - V.fov) * t;
  cw_node_set(camera, "fov", cw_val_float(V.fov));
  cw_node_set(camera, "near", cw_val_float(0.2f));
  cw_node_set(camera, "far", cw_val_float(600));
  float dx = V.eye.x - V.at.x, dy = V.eye.y - V.at.y, dz = V.eye.z - V.at.z;
  V.st.cam_dist = sqrtf(dx * dx + dy * dy + dz * dz);
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

view_stats view_get_stats(void) { return V.st; }
