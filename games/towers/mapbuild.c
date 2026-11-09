/* towers map builder (mapbuild.h). */
#include "mapbuild.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/vfs.h"
#include "../common/coplanar.h"

t3_color mb_lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

/* ── .cwchar (tools/blender/cwchar_lib.py writes them) ─────────────────── */

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
bool mb_chr_load(mb_chr *c, const char *path) {
  memset(c, 0, sizeof *c);
  size_t len = 0;
  uint8_t *buf = cw_vfs_exists(path) ? cw_vfs_read(path, &len) : NULL;
  if (!buf || len < 16 || memcmp(buf, "CWCH", 4) != 0) {
    cw_error("towers: cannot read the model %s", path);
    free(buf);
    return false;
  }
  c->nclip = (int)rd32(buf + 8), c->ngroup = CW_MIN((int)rd32(buf + 12), MB_MAXG);
  const uint8_t *p = buf + 16;
  for (int k = 0; k < (int)rd32(buf + 8); k++, p += 4) {
    int n = (int)rd32(p);
    if (k < MB_MAXC) c->nf[k] = n, c->first[k] = c->total;
    c->total += n;
  }
  c->lo_y = 1e9f, c->hi_y = -1e9f;
  float r2 = 0;
  for (int g = 0; g < (int)rd32(buf + 12); g++) {
    int nv = (int)rd32(p), nt = (int)rd32(p + 4);
    p += 8;
    const uint8_t *uv = p, *col = p + 8 * nv, *idx = p + 20 * nv;
    p += 20 * (size_t)nv + (((size_t)nt * 6 + 3) & ~(size_t)3);
    float *cl = malloc(sizeof(float) * 3 * (size_t)CW_MAX(nv, 1));
    for (int v = 0; v < nv; v++) {
      float rgb[3];
      memcpy(rgb, col + 12 * v, 12);
      bool emi = rgb[0] > 1.5f || rgb[1] > 1.5f || rgb[2] > 1.5f;
      t3_color l = emi ? (t3_color){ rgb[0], rgb[1], rgb[2] } : mb_lin(rgb[0], rgb[1], rgb[2]);
      cl[v * 3] = l.r, cl[v * 3 + 1] = l.g, cl[v * 3 + 2] = l.b;
    }
    for (int f = 0; f < c->total; f++) {
      if (g < MB_MAXG && f < MB_MAXF && nv > 0 && nt > 0) {
        t3_geometry *geo = t3_geometry_new();
        t3_geometry_set_attribute(geo, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, p, nv, 3));
        t3_geometry_set_attribute(geo, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, p + 12 * (size_t)nv, nv, 3));
        t3_geometry_set_attribute(geo, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, nv, 2));
        t3_geometry_set_attribute(geo, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, cl, nv, 3));
        t3_geometry_set_index(geo, t3_attribute_new(T3_UINT16, idx, nt * 3, 1));
        cw_geometry_optimize_vertex_cache(geo, NULL, NULL);
        c->geo[g][f] = geo;
        const float *pp = (const float *)p;
        for (int v = 0; v < nv; v++) {
          float x, y, z;
          memcpy(&x, pp + v * 3, 4), memcpy(&y, pp + v * 3 + 1, 4), memcpy(&z, pp + v * 3 + 2, 4);
          if (g == 0) c->lo_y = CW_MIN(c->lo_y, y), c->hi_y = CW_MAX(c->hi_y, y);
          r2 = CW_MAX(r2, x * x + z * z);
        }
      }
      p += 24 * (size_t)nv;
    }
    free(cl);
  }
  c->rad = sqrtf(r2);
  c->total = CW_MIN(c->total, MB_MAXF);
  free(buf);
  return true;
}
/* a model's triangles drawn top first. The camera looks down on the map (and the creeps lean their up toward it), so
 * a body's upper faces are drawn before the lower ones they cover, and those fail the depth test before they are
 * shaded: the crowd's bodies and the towers drawn top first cost the 890M 0.13 to 0.2 ms less a bench frame, with the
 * same picture (only where two faces of one model sit at exactly the same depth can the other one win the pixel).
 * Ties keep a fixed order (by index), so every C library's qsort gives one order. TOWERS_OLD_ORDER=1 (development,
 * A/B timing) leaves the order as loaded */
static const float *g_sort_y;
static int tri_by_height(const void *a, const void *b) {
  const uint16_t *p = a, *q = b;
  float ya = g_sort_y[p[0] * 3 + 1] + g_sort_y[p[1] * 3 + 1] + g_sort_y[p[2] * 3 + 1];
  float yb = g_sort_y[q[0] * 3 + 1] + g_sort_y[q[1] * 3 + 1] + g_sort_y[q[2] * 3 + 1];
  if (ya != yb) return ya > yb ? -1 : 1;
  for (int i = 0; i < 3; i++)
    if (p[i] != q[i]) return p[i] < q[i] ? -1 : 1;
  return 0;
}
void mb_sort_tris(t3_geometry *g) {
  static int old = -1;
  if (old < 0) old = getenv("TOWERS_OLD_ORDER") && atoi(getenv("TOWERS_OLD_ORDER"));
  if (old || !g || !g->index || g->index->type != T3_UINT16 || !g->attributes[T3_ATTR_POSITION]) return;
  g_sort_y = t3_attribute_f32(g->attributes[T3_ATTR_POSITION]);
  qsort(g->index->array, (size_t)g->index->count / 3, sizeof(uint16_t) * 3, tri_by_height);
  g->index->version++;
}
/* fewer triangles for a model seen small: vertices clustered on a grid of `cell` metres (from the first frame, so every
 * frame keeps one topology), each cluster kept as its first vertex, the triangles that collapse dropped */
void mb_chr_simplify(mb_chr *c, int g, float cell) {
  t3_geometry *g0 = c->geo[g][0];
  if (!g0 || !g0->index || g0->index->type != T3_UINT16) return;
  int nv = g0->attributes[T3_ATTR_POSITION]->count, ni = g0->index->count;
  const float *p0 = t3_attribute_f32(g0->attributes[T3_ATTR_POSITION]);
  int *rep = malloc(sizeof(int) * (size_t)nv), *newid = malloc(sizeof(int) * (size_t)nv);
  int nk = 0;
  for (int v = 0; v < nv; v++) {
    int kx = (int)floorf(p0[v * 3] / cell), ky = (int)floorf(p0[v * 3 + 1] / cell), kz = (int)floorf(p0[v * 3 + 2] / cell);
    rep[v] = v;
    for (int u = 0; u < v; u++) /* (small models: a linear search over the kept ones is fine at load) */
      if (rep[u] == u && (int)floorf(p0[u * 3] / cell) == kx && (int)floorf(p0[u * 3 + 1] / cell) == ky && (int)floorf(p0[u * 3 + 2] / cell) == kz) { rep[v] = u; break; }
    if (rep[v] == v) newid[v] = nk++;
  }
  const uint16_t *ix = (const uint16_t *)g0->index->array;
  uint16_t *out = malloc(sizeof(uint16_t) * (size_t)ni);
  int no = 0;
  for (int t = 0; t + 2 < ni; t += 3) {
    int a = newid[rep[ix[t]]], b = newid[rep[ix[t + 1]]], d = newid[rep[ix[t + 2]]];
    if (a == b || b == d || a == d) continue;
    out[no++] = (uint16_t)a, out[no++] = (uint16_t)b, out[no++] = (uint16_t)d;
  }
  for (int f = 0; f < c->total && f < MB_MAXF; f++) {
    t3_geometry *src = c->geo[g][f];
    if (!src) continue;
    t3_geometry *dst = t3_geometry_new();
    static const t3_attr_slot SL[4] = { T3_ATTR_POSITION, T3_ATTR_NORMAL, T3_ATTR_UV, T3_ATTR_COLOR };
    static const int DIM[4] = { 3, 3, 2, 3 };
    for (int s = 0; s < 4; s++) {
      t3_attribute *a = src->attributes[SL[s]];
      if (!a) continue;
      const float *in = t3_attribute_f32(a);
      float *o = malloc(sizeof(float) * (size_t)(DIM[s] * CW_MAX(nk, 1)));
      for (int v = 0; v < nv; v++)
        if (rep[v] == v) memcpy(o + newid[v] * DIM[s], in + v * DIM[s], sizeof(float) * (size_t)DIM[s]);
      t3_geometry_set_attribute(dst, SL[s], t3_attribute_new(T3_FLOAT32, o, nk, DIM[s]));
      free(o);
    }
    t3_geometry_set_index(dst, t3_attribute_new(T3_UINT16, out, no, 1));
    t3_release(src);
    c->geo[g][f] = dst;
  }
  free(rep), free(newid), free(out);
}
void mb_chr_free(mb_chr *c) {
  for (int g = 0; g < MB_MAXG; g++)
    for (int f = 0; f < MB_MAXF; f++)
      if (c->geo[g][f]) t3_release(c->geo[g][f]);
  memset(c, 0, sizeof *c);
}

void mapbuild_kit(int theme, char *out, int len) { snprintf(out, (size_t)len, "models/decor_%s.cwchar", MP_THEME_ID[CW_CLAMP(theme, 0, MP_THEMES - 1)]); }

uint64_t mapbuild_bake_hash(const char *text, int theme) {
  uint64_t h = map_bake_hash(text);
  char kit[96];
  mapbuild_kit(theme, kit, sizeof kit);
  const char *files[2] = { kit, "models/lamp.cwchar" };
  for (int k = 0; k < 2; k++) {
    size_t len = 0;
    uint8_t *b = cw_vfs_exists(files[k]) ? cw_vfs_read(files[k], &len) : NULL;
    for (size_t i = 0; i < len; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    h = (h ^ (uint64_t)len) * 0x100000001b3ull;
    free(b);
  }
  for (const char *c = MB_TERRAIN; *c; c++) h = (h ^ (uint8_t)*c) * 0x100000001b3ull; /* how the terrain is built */
  return h;
}

/* ── the terrain ─────────────────────────────────────────────────────── */

static uint32_t thash(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float fhash(int x, int z, uint32_t seed) { return (float)(thash(x, z, seed) & 0xffffff) / 16777216.0f; }
static float vnoise(float x, float z, uint32_t seed) {
  int ix = (int)floorf(x), iz = (int)floorf(z);
  float fx = x - (float)ix, fz = z - (float)iz;
  fx = fx * fx * (3 - 2 * fx), fz = fz * fz * (3 - 2 * fz);
  float a = fhash(ix, iz, seed), b = fhash(ix + 1, iz, seed), c = fhash(ix, iz + 1, seed), d = fhash(ix + 1, iz + 1, seed);
  return (a * (1 - fx) + b * fx) * (1 - fz) + (c * (1 - fx) + d * fx) * fz;
}

float mapbuild_height(const tdmap *m, float x, float z) { return map_ground_h(m, x / MP_TILE + (float)m->w * 0.5f, z / MP_TILE + (float)m->h * 0.5f); }


/* the themes' palettes (screen colours): the ground, its accent in patches, the path's floor (a ground material:
 * packed earth, cobbles, flagstones, ash crust), the curb, the rock, the water; the textures' tile sizes (m) */
typedef struct palette { float ground[3], accent[3], path[3], bank[3], rock[3], water[3], tile_g, tile_p; } palette;
/* the pools (screen colours): the water at the shore and in the deep, the sky's tint on the surface, how much it
 * glows (a light of its own: x the bake, at the shore and in the deep; lava and the crystal light) and the wet bank's
 * colour; the outcrops' caps (moss, lichen, dust, snow, ash, glowing lichen) on their flat tops and how much of them */
typedef struct poolpal { float shallow[3], deep[3], sky[3], glow0, glow1, wet[3], cap[3], cap_k; } poolpal;
static const poolpal POOL[MP_THEMES] = {
  { { 0.12f, 0.27f, 0.20f }, { 0.02f, 0.14f, 0.31f }, { 0.62f, 0.78f, 0.92f }, 1, 1, { 0.12f, 0.20f, 0.07f }, { 0.30f, 0.46f, 0.12f }, 0.7f },
  { { 0.24f, 0.27f, 0.19f }, { 0.05f, 0.19f, 0.33f }, { 0.70f, 0.76f, 0.86f }, 1, 1, { 0.26f, 0.15f, 0.06f }, { 0.52f, 0.50f, 0.20f }, 0.55f },
  { { 0.30f, 0.50f, 0.42f }, { 0.02f, 0.27f, 0.42f }, { 0.70f, 0.86f, 0.95f }, 1, 1, { 0.56f, 0.38f, 0.20f }, { 0.94f, 0.80f, 0.52f }, 0.4f },
  { { 0.36f, 0.48f, 0.58f }, { 0.06f, 0.20f, 0.44f }, { 0.82f, 0.88f, 0.98f }, 1, 1, { 0.56f, 0.64f, 0.76f }, { 0.95f, 0.97f, 1.00f }, 0.95f },
  { { 0.80f, 0.22f, 0.04f }, { 1.00f, 0.60f, 0.16f }, { 0.50f, 0.25f, 0.20f }, 1.6f, 3.2f, { 0.07f, 0.05f, 0.05f }, { 0.60f, 0.58f, 0.58f }, 0.6f },
  { { 0.05f, 0.26f, 0.42f }, { 0.06f, 0.56f, 0.66f }, { 0.45f, 0.40f, 0.85f }, 0.8f, 1.35f, { 0.10f, 0.08f, 0.22f }, { 0.40f, 0.84f, 0.88f }, 0.45f },
};
/* each theme a value hierarchy (DESIGN.md, the look): the path a clear step lighter or darker than the ground in a
 * colour its material could have, a dark curb between them (red brick in the meadow, slate where the ground is warm), the water cool where the ground is warm */
static const palette PAL[MP_THEMES] = {
  { { 0.25f, 0.48f, 0.14f }, { 0.46f, 0.58f, 0.14f }, { 0.88f, 0.75f, 0.53f }, { 0.40f, 0.15f, 0.11f }, { 0.60f, 0.60f, 0.62f }, { 0.12f, 0.55f, 0.78f }, 4, 3 }, /* packed earth */
  { { 0.64f, 0.39f, 0.12f }, { 0.64f, 0.22f, 0.08f }, { 0.46f, 0.53f, 0.62f }, { 0.13f, 0.15f, 0.24f }, { 0.62f, 0.60f, 0.58f }, { 0.12f, 0.50f, 0.62f }, 4, 2.5f }, /* slate cobbles */
  { { 0.93f, 0.76f, 0.48f }, { 0.88f, 0.54f, 0.30f }, { 0.54f, 0.33f, 0.23f }, { 0.16f, 0.24f, 0.38f }, { 0.72f, 0.36f, 0.24f }, { 0.08f, 0.66f, 0.72f }, 5, 3 }, /* sandstone flags */
  { { 0.90f, 0.94f, 0.99f }, { 0.72f, 0.84f, 0.97f }, { 0.46f, 0.36f, 0.30f }, { 0.22f, 0.19f, 0.22f }, { 0.40f, 0.43f, 0.50f }, { 0.20f, 0.52f, 0.86f }, 5, 3 }, /* frozen earth and gravel */
  { { 0.35f, 0.29f, 0.40f }, { 0.85f, 0.30f, 0.06f }, { 0.47f, 0.61f, 0.79f }, { 0.08f, 0.07f, 0.08f }, { 0.26f, 0.52f, 0.46f }, { 1.00f, 0.42f, 0.10f }, 4, 3 }, /* ash crust; the outcrops crusted green with copper minerals */
  { { 0.32f, 0.24f, 0.56f }, { 0.20f, 0.58f, 0.64f }, { 0.86f, 0.80f, 0.68f }, { 0.14f, 0.10f, 0.24f }, { 0.64f, 0.62f, 0.84f }, { 0.30f, 0.85f, 0.90f }, 4, 3 }, /* pale flagstones */
};

static cw_node mesh_node(cw_node parent, const char *name, t3_mesh *mesh, bool baked) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  cw_node_set(n, "gi_mode", cw_val_enum(baked ? "STATIC" : "DYNAMIC"));
  cw_mesh3d_set_procedural(n, mesh);
  return n;
}

static t3_texture *load_png(const char *path) {
  size_t len = 0;
  void *png = cw_vfs_exists(path) ? cw_vfs_read(path, &len) : NULL;
  t3_texture *tx = png ? t3_texture_load_memory(png, len) : NULL;
  free(png);
  if (!tx) return NULL;
  tx->wrap_s = tx->wrap_t = T3_REPEAT;
  tx->flip_y = false;
  tx->generate_mipmaps = true;
  tx->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  tx->mag_filter = T3_LINEAR;
  tx->anisotropy = 8;
  tx->color_space = T3_SRGB_COLOR_SPACE;
  return tx;
}

/* The terrain is a grid of sixths of a tile, every triangle of it cut along the shape's own curves (the floor's edge,
 * the curb's middle and top, the plaza's edge, the outcrops' foot, the pools' water line), so every edge between two
 * materials is a smooth line through the triangles, never the grid's steps. Each piece lands in its region's mesh;
 * its vertices take their height, normal and colour from the shape at that point. */
#define RES 6
enum { R_GROUND, R_POOL, R_FLOOR, R_CURB_LO, R_CURB_HI, R_PLAZA, R_ROCK, R_N };
enum { M_GROUND, M_PATH, M_PLAZA, M_ROCK, M_POOL, M_N };
static const int REG_MESH[R_N] = { M_GROUND, M_POOL, M_PATH, M_PATH, M_PATH, M_PLAZA, M_ROCK };
enum { NF = 10 }; /* the fields cut along: the floor's edge, the curb's middle and top, the plaza, the rock's foot, the water
                     * line, and the outcrops' creases (map_rock_creases) */
#define POOL_LEVEL MB_POOL_LEVEL
#define WATER_LINE ((MP_GROUND_H - POOL_LEVEL) / (MP_GROUND_H + 0.6f)) /* the pools' depth (feature units) at the water's level */
float mapbuild_pool_deep(const tdmap *m, float x, float z) {
  float rock, water;
  map_feat_at(m, x / MP_TILE + (float)m->w * 0.5f, z / MP_TILE + (float)m->h * 0.5f, &rock, &water);
  return water <= 0 ? -1 : (water - WATER_LINE) / (1 - WATER_LINE);
}

typedef struct tpt { float x, y, f[NF], rock, water; } tpt;
static void tpt_eval(const tdmap *m, float x, float y, tpt *p) {
  float w, d = map_floor_d(m, x, y, &w);
  p->x = x, p->y = y;
  map_feat_at(m, x, y, &p->rock, &p->water);
  p->f[0] = d, p->f[1] = d - MP_CURB * 0.5f, p->f[2] = d - MP_CURB;
  p->f[3] = CW_MAX(d + MP_CURB + 0.1f, (4.4f - w) * 0.5f);
  p->f[4] = p->rock - 0.05f, p->f[5] = WATER_LINE - p->water;
  if (p->rock > 0) map_rock_creases(m, x, y, p->f + 6);
  else p->f[6] = p->f[7] = p->f[8] = p->f[9] = 1;
}
static float tpt_field(const tdmap *m, int k, float x, float y) {
  tpt p;
  tpt_eval(m, x, y, &p);
  return p.f[k];
}
static int region_of(const tpt *poly, int n) {
  float f[NF] = { 0 };
  for (int i = 0; i < n; i++)
    for (int k = 0; k < NF; k++) f[k] += poly[i].f[k];
  if (f[5] < 0) return R_POOL;
  if (f[0] <= 0) return f[3] < 0 ? R_PLAZA : R_FLOOR;
  if (f[1] <= 0) return R_CURB_LO;
  if (f[2] <= 0) return R_CURB_HI;
  if (f[4] > 0) return R_ROCK;
  return R_GROUND;
}

typedef struct tmesh { float *pos, *nrm, *col; int nv, capv; uint32_t *idx; int ni, capi; float x0, y0, x1, y1, area; } tmesh;
typedef struct tbuild {
  const tdmap *m;
  const palette *pal;
  tmesh mesh[M_N];
  uint64_t *hkey; /* the vertex hash: (x, y, region) to its index in its mesh */
  int32_t *hval;
  uint32_t hcap;
  bool mask;
} tbuild;

static void tb_colour(const tbuild *tb, int reg, const tpt *p, float h, const float n[3], float out[3]) {
  float ny = n[1], sun = 0.22f + 1.05f * CW_MAX(0.0f, (-0.45f * n[0] + 0.75f * n[1] - 0.5f * n[2]) / 1.0062f);
  const palette *pal = tb->pal;
  const tdmap *m = tb->m;
  const poolpal *pp = &POOL[CW_CLAMP(m->theme, 0, MP_THEMES - 1)];
  float x = p->x * MP_TILE, z = p->y * MP_TILE; /* metres */
  float k = 0.85f + 0.42f * (vnoise(x / 6, z / 6, 11 + (uint32_t)m->num) - 0.5f) + 0.26f * (vnoise(x / 1.7f, z / 1.7f, 12) - 0.5f);
  /* a pool: how deep at this point (0 at the shore, 1 in the deep, quickly off the shore); the bank: how wet (1 at the water) */
  float deep = CW_CLAMP((p->water - WATER_LINE) / (1 - WATER_LINE), 0.0f, 1.0f), wet = CW_CLAMP(p->water / WATER_LINE, 0.0f, 1.0f);
  deep = deep * (2 - deep) * 0.5f + deep * 0.5f, wet = CW_CLAMP((wet - 0.35f) / 0.65f, 0.0f, 1.0f), wet = wet * wet * (3 - 2 * wet);
  float glow = 1, c[3];
  for (int q = 0; q < 3; q++) {
    switch (reg) {
    case R_FLOOR:
    case R_PLAZA: { /* the floor: its material's colour, worn paler down the middle, a shade darker in the gutter by the curb */
      float wear = CW_CLAMP(-p->f[0] / 0.6f, 0.0f, 1.0f), gut = 1 - 0.18f * (1 - CW_CLAMP(-p->f[0] / 0.12f, 0.0f, 1.0f));
      c[q] = pal->path[q] * (0.9f + 0.1f * wear + (reg == R_PLAZA ? 0.06f : 0.0f)) * gut * (0.94f + 0.12f * vnoise(x / 2.2f, z / 2.2f, 17));
      break;
    }
    case R_CURB_LO: c[q] = pal->bank[q] * 0.75f; break;
    case R_CURB_HI: c[q] = pal->bank[q] * 1.25f; break; /* the curb's top a shade lighter: its stones catch the light */
    case R_POOL: { /* the water: pale over the shallows, dark in the deep (the line wanders a little), the sky's tint on it */
      float d = CW_CLAMP(deep + 0.3f * (vnoise(x / 2.5f, z / 2.5f, 41 + (uint32_t)m->num) - 0.5f), 0.0f, 1.0f);
      c[q] = pp->shallow[q] + (pp->deep[q] - pp->shallow[q]) * d;
      float sky = 0.08f + 0.14f * CW_CLAMP((vnoise(x / 3.0f, z / 7.0f, 43) - 0.4f) * 2.5f, 0.0f, 1.0f); /* the sky in soft streaks */
      c[q] += (pp->sky[q] - c[q]) * sky * (0.4f + 0.6f * d);
      glow = pp->glow0 + (pp->glow1 - pp->glow0) * d;
      break;
    }
    case R_ROCK: { /* rock in facets, each lit by the sun as it faces (north-west, high), its own shade, darker at the foot; caps on the flat tops */
      float lift = 0.72f + 0.38f * CW_MIN(p->rock / 1.4f, 1.0f);
      float facet = 0.88f + 0.24f * fhash((int)floorf(n[0] * 8 + 64), (int)floorf(n[2] * 8 + 64), 7);
      float cap = CW_CLAMP((ny - 0.9f) * 12, 0.0f, 1.0f) * CW_CLAMP((vnoise(x / 1.3f, z / 1.3f, 96) - 0.42f) * 4, 0.0f, 1.0f) * CW_MIN(p->rock / 0.4f, 1.0f) * pp->cap_k;
      c[q] = pal->rock[q] * lift * facet * (0.93f + 0.14f * vnoise(x * 1.4f, z * 1.4f, 97));
      c[q] = (c[q] + (pp->cap[q] - c[q]) * cap) * (0.2f + 0.82f * sun);
      break;
    }
    default: { /* the ground: the accent in patches; the bank down to a pool wet and dark */
      float a = 0.55f * CW_CLAMP((vnoise(x / 5.0f, z / 5.0f, 31 + (uint32_t)m->num) - 0.52f) * 3.2f, 0.0f, 1.0f);
      float gq = pal->ground[q] + a * (pal->accent[q] - pal->ground[q]);
      if (tb->m->theme == MP_THEME_VOLCANO) { /* copper-mineral crust in small patches between the hot ones */
        static const float CU[3] = { 0.16f, 0.56f, 0.44f };
        float cu = 0.7f * CW_CLAMP((vnoise(x / 2.6f, z / 2.6f, 53 + (uint32_t)m->num) - 0.67f) * 4.0f, 0.0f, 1.0f) * (1 - a / 0.55f);
        gq += cu * (CU[q] - gq);
      }
      c[q] = gq + (pp->wet[q] - gq) * wet * 0.6f;
      break;
    }
    }
    if (reg == R_GROUND) c[q] *= k;
    else if (reg != R_ROCK && reg != R_POOL) c[q] *= 0.97f + 0.25f * (k - 0.85f);
  }
  if (tb->mask) { /* the look check's mask (TOWERS_LOOK_MASK): the floor blue, the curb red, the rest green */
    bool fl = reg == R_FLOOR || reg == R_PLAZA, cu = reg == R_CURB_LO || reg == R_CURB_HI;
    c[0] = cu ? 1.0f : 0.0f, c[1] = fl || cu ? 0.0f : 1.0f, c[2] = fl ? 1.0f : 0.0f, glow = 1;
  }
  t3_color l = mb_lin(CW_CLAMP(c[0], 0.0f, 1.0f), CW_CLAMP(c[1], 0.0f, 1.0f), CW_CLAMP(c[2], 0.0f, 1.0f));
  out[0] = l.r * glow, out[1] = l.g * glow, out[2] = l.b * glow;
  if (!tb->mask && reg == R_GROUND && pp->glow1 > 1.2f) { /* the glowing pools' light on the last of their banks */
    float e = CW_CLAMP(p->water / WATER_LINE, 0.0f, 1.0f) * 0.35f;
    t3_color g = mb_lin(pp->shallow[0], pp->shallow[1], pp->shallow[2]);
    out[0] += g.r * e * e, out[1] += g.g * e * e, out[2] += g.b * e * e;
  }
}

static uint32_t tb_push(tmesh *ms, float wx, float h, float wz, const float n[3]) {
  if (ms->nv == ms->capv) {
    ms->capv = ms->capv ? ms->capv * 2 : 4096;
    ms->pos = realloc(ms->pos, sizeof(float) * 3 * (size_t)ms->capv), ms->nrm = realloc(ms->nrm, sizeof(float) * 3 * (size_t)ms->capv), ms->col = realloc(ms->col, sizeof(float) * 3 * (size_t)ms->capv);
  }
  int v = ms->nv++;
  ms->pos[v * 3] = wx, ms->pos[v * 3 + 1] = h, ms->pos[v * 3 + 2] = wz;
  memcpy(ms->nrm + v * 3, n, sizeof(float) * 3);
  if (v == 0) ms->x0 = ms->x1 = wx, ms->y0 = ms->y1 = wz;
  ms->x0 = CW_MIN(ms->x0, wx), ms->x1 = CW_MAX(ms->x1, wx), ms->y0 = CW_MIN(ms->y0, wz), ms->y1 = CW_MAX(ms->y1, wz);
  return (uint32_t)v;
}
static void tb_index(tmesh *ms, uint32_t a, uint32_t b, uint32_t c) {
  if (ms->ni + 3 > ms->capi) ms->capi = ms->capi ? ms->capi * 2 : 8192, ms->idx = realloc(ms->idx, sizeof(uint32_t) * (size_t)ms->capi);
  ms->idx[ms->ni++] = a, ms->idx[ms->ni++] = b, ms->idx[ms->ni++] = c;
  const float *pa = ms->pos + a * 3, *pb = ms->pos + b * 3, *pc = ms->pos + c * 3;
  ms->area += 0.5f * fabsf((pb[0] - pa[0]) * (pc[2] - pa[2]) - (pb[2] - pa[2]) * (pc[0] - pa[0]));
}

static uint32_t tb_vertex(tbuild *tb, int reg, const tpt *p) {
  int64_t qx = (int64_t)llroundf(p->x * 4096), qy = (int64_t)llroundf(p->y * 4096);
  uint64_t key = ((uint64_t)(qx + (1 << 20)) << 32) ^ ((uint64_t)(qy + (1 << 20)) << 4) ^ (uint64_t)reg;
  uint64_t hk = key * 0x9e3779b97f4a7c15ull;
  uint32_t s = (uint32_t)(hk >> 32) & (tb->hcap - 1);
  while (tb->hval[s] >= 0) {
    if (tb->hkey[s] == key) return (uint32_t)tb->hval[s];
    s = (s + 1) & (tb->hcap - 1);
  }
  tmesh *ms = &tb->mesh[REG_MESH[reg]];
  const tdmap *m = tb->m;
  float h = map_ground_h(m, p->x, p->y), n[3] = { 0, 1, 0 };
  if (reg == R_FLOOR || reg == R_PLAZA) h = 0;
  else if (reg == R_POOL) h = POOL_LEVEL; /* the water's still surface */
  else { /* the slope from the shape itself (a sixth of a tile's span is too coarse for the curb) */
    const float e = 0.03f;
    float hl = map_ground_h(m, p->x - e, p->y), hr = map_ground_h(m, p->x + e, p->y), hd = map_ground_h(m, p->x, p->y - e), hu = map_ground_h(m, p->x, p->y + e);
    float nx = (hl - hr) / (2 * e * MP_TILE), nz = (hd - hu) / (2 * e * MP_TILE), nl = sqrtf(nx * nx + 1 + nz * nz);
    n[0] = nx / nl, n[1] = 1 / nl, n[2] = nz / nl;
  }
  uint32_t v = tb_push(ms, (p->x - (float)m->w * 0.5f) * MP_TILE, h, (p->y - (float)m->h * 0.5f) * MP_TILE, n);
  tb_colour(tb, reg, p, h, n, ms->col + v * 3);
  tb->hkey[s] = key, tb->hval[s] = (int32_t)v;
  return v;
}

static void tb_emit(tbuild *tb, const tpt *poly, int n) {
  if (n < 3) return;
  int reg = region_of(poly, n);
  tmesh *ms = &tb->mesh[REG_MESH[reg]];
  if (reg == R_ROCK) { /* rock is faceted: the pieces are cut along its creases, so each is one plane of it, drawn flat and
                        * shaded by its own normal (S > 1 cuts each again, for finer colour) */
    enum { S = 1 };
    const tdmap *m = tb->m;
    for (int i = 1; i + 1 < n; i++) {
      tpt sp[(S + 1) * (S + 2) / 2];
      float P[(S + 1) * (S + 2) / 2][3];
      int k = 0;
      for (int r = 0; r <= S; r++)
        for (int c = 0; r + c <= S; c++, k++) { /* barycentric: r toward poly[i], c toward poly[i + 1] */
          float u = (float)r / S, v = (float)c / S;
          float x = poly[0].x + (poly[i].x - poly[0].x) * u + (poly[i + 1].x - poly[0].x) * v, y = poly[0].y + (poly[i].y - poly[0].y) * u + (poly[i + 1].y - poly[0].y) * v;
          tpt_eval(m, x, y, &sp[k]);
          P[k][0] = (x - (float)m->w * 0.5f) * MP_TILE, P[k][1] = map_ground_h(m, x, y), P[k][2] = (y - (float)m->h * 0.5f) * MP_TILE;
        }
#define SI(r, c) ((r) * (2 * S + 3 - (r)) / 2 + (c))
      for (int r = 0; r < S; r++)
        for (int c = 0; r + c < S; c++) {
          int t[2][3] = { { SI(r, c), SI(r + 1, c), SI(r, c + 1) }, { SI(r + 1, c), SI(r + 1, c + 1), SI(r, c + 1) } };
          for (int q = 0; q < (r + c + 1 < S ? 2 : 1); q++) {
            const float *A = P[t[q][0]], *B = P[t[q][1]], *C = P[t[q][2]];
            float ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2], vx = C[0] - A[0], vy = C[1] - A[1], vz = C[2] - A[2];
            float nr[3] = { uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx }, l = sqrtf(nr[0] * nr[0] + nr[1] * nr[1] + nr[2] * nr[2]);
            if (l < 1e-8f) continue;
            bool up = nr[1] > 0;
            if (!up) l = -l;
            nr[0] /= l, nr[1] /= l, nr[2] /= l;
            uint32_t id[3];
            for (int e = 0; e < 3; e++) {
              id[e] = tb_push(ms, P[t[q][e]][0], P[t[q][e]][1], P[t[q][e]][2], nr);
              tb_colour(tb, reg, &sp[t[q][e]], P[t[q][e]][1], nr, ms->col + id[e] * 3);
            }
            if (up) tb_index(ms, id[0], id[1], id[2]); /* its normal up, as the grid's */
            else tb_index(ms, id[0], id[2], id[1]);
          }
        }
#undef SI
    }
    return;
  }
  uint32_t id[16];
  for (int i = 0; i < n; i++) id[i] = tb_vertex(tb, reg, &poly[i]);
  for (int i = 1; i + 1 < n; i++) {
    if (id[0] == id[i] || id[i] == id[i + 1] || id[0] == id[i + 1]) continue;
    tb_index(ms, id[0], id[i], id[i + 1]);
  }
}

/* cut a convex piece along field k's zero (the crossing found on the shape itself), then on along the next field */
static void tb_clip(tbuild *tb, const tpt *poly, int n, int k) {
  if (k == NF) { tb_emit(tb, poly, n); return; }
  bool neg = false, pos = false;
  for (int i = 0; i < n; i++) neg |= poly[i].f[k] < 0, pos |= poly[i].f[k] > 0;
  if (!neg || !pos) { tb_clip(tb, poly, n, k + 1); return; }
  tpt a[16], b[16];
  int na = 0, nb = 0;
  for (int i = 0; i < n && na < 15 && nb < 15; i++) {
    const tpt *p = &poly[i], *q = &poly[(i + 1) % n];
    if (p->f[k] <= 0) a[na++] = *p;
    if (p->f[k] >= 0) b[nb++] = *p;
    if ((p->f[k] < 0 && q->f[k] > 0) || (p->f[k] > 0 && q->f[k] < 0)) {
      float t0 = 0, t1 = 1, s0 = p->f[k], s1 = q->f[k], t = s0 / (s0 - s1);
      for (int it = 0; it < 3; it++) { /* regula falsi on the true field */
        float s = tpt_field(tb->m, k, p->x + (q->x - p->x) * t, p->y + (q->y - p->y) * t);
        if ((s < 0) == (s0 < 0)) t0 = t, s0 = s;
        else t1 = t, s1 = s;
        t = s0 == s1 ? (t0 + t1) * 0.5f : t0 + (t1 - t0) * s0 / (s0 - s1);
      }
      tpt c;
      tpt_eval(tb->m, p->x + (q->x - p->x) * t, p->y + (q->y - p->y) * t, &c);
      c.f[k] = 0;
      a[na++] = c, b[nb++] = c;
    }
  }
  tb_clip(tb, a, na, k + 1);
  tb_clip(tb, b, nb, k + 1);
}

static void build_terrain(mb_built *b, const tdmap *m) {
  const int NI = (m->w + 2 * MB_MARGIN) * RES + 1, NJ = (m->h + 2 * MB_MARGIN) * RES + 1;
  tbuild tb = { .m = m, .pal = &PAL[b->theme], .mask = getenv("TOWERS_LOOK_MASK") != NULL };
  tb.hcap = 1;
  while (tb.hcap < (uint32_t)(NI * NJ) * 8) tb.hcap <<= 1;
  tb.hkey = malloc(sizeof(uint64_t) * tb.hcap), tb.hval = malloc(sizeof(int32_t) * tb.hcap);
  memset(tb.hval, 0xff, sizeof(int32_t) * tb.hcap);
  tpt *grid = malloc(sizeof(tpt) * (size_t)NI * (size_t)NJ);
  for (int j = 0; j < NJ; j++)
    for (int i = 0; i < NI; i++) tpt_eval(m, (float)i / RES - MB_MARGIN, (float)j / RES - MB_MARGIN, &grid[j * NI + i]);
  for (int j = 0; j < NJ - 1; j++)
    for (int i = 0; i < NI - 1; i++) {
      const tpt *a = &grid[j * NI + i], *bb = a + 1, *c = a + NI, *d = c + 1;
      tpt t1[3] = { *a, *c, *d }, t2[3] = { *a, *d, *bb };
      tb_clip(&tb, t1, 3, 0);
      tb_clip(&tb, t2, 3, 0);
    }
  free(grid), free(tb.hkey), free(tb.hval);
  static const char *const NAME[M_N] = { "Ground", "Path", "Plaza", "Rock", "Pool" }, *const TEX[M_N] = { "ground", "path", "plaza", "rock", "water" };
  for (int k = 0; k < M_N; k++) {
    tmesh *ms = &tb.mesh[k];
    if (ms->ni >= 3) {
      /* uv0 over the mesh's own bounds (the lightmap's layout), its lightmap as fine as the whole field's */
      float bw = CW_MAX(ms->x1 - ms->x0, 0.1f), bh = CW_MAX(ms->y1 - ms->y0, 0.1f);
      float *uv = malloc(sizeof(float) * 2 * (size_t)ms->nv);
      for (int v = 0; v < ms->nv; v++) uv[v * 2] = (ms->pos[v * 3] - ms->x0) / bw, uv[v * 2 + 1] = (ms->pos[v * 3 + 2] - ms->y0) / bh;
      t3_geometry *g = t3_geometry_new();
      t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, ms->pos, ms->nv, 3));
      t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, ms->nrm, ms->nv, 3));
      t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, ms->col, ms->nv, 3));
      t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, ms->nv, 2));
      t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, ms->idx, ms->ni, 1));
      free(uv);
      t3_material *gm = t3_mesh_standard_material_new(0xffffff);
      gm->vertex_colors = true;
      gm->roughness = 0.9f;
      char path[64];
      snprintf(path, sizeof path, "textures/%s_%s.png", MP_THEME_ID[b->theme], TEX[k]);
      t3_texture *tx = load_png(path);
      if (tx) {
        float tile = k == M_GROUND ? tb.pal->tile_g : k == M_PATH ? tb.pal->tile_p : k == M_PLAZA ? 4.0f : k == M_POOL ? 5.0f : 3.0f;
        tx->repeat = (t3_vec2){ bw / tile, bh / tile };
        t3_material_set_texture(gm, T3_MAP, tx);
        t3_release(tx);
      } else
        cw_error("towers: the texture %s did not load", path);
      t3_mesh *mesh = t3_mesh_new(g, gm);
      cw_node n = mesh_node(b->node, NAME[k], mesh, true);
      cw_node_set(n, "lightmap_uv", cw_val_enum("UV0"));
      cw_node_set(n, "lightmap_scale", cw_val_float(CW_CLAMP(sqrtf(bw * bh / CW_MAX(ms->area, 1.0f)), 1.0f, 6.0f)));
      t3_release(mesh), t3_release(g), t3_release(gm);
    }
    free(ms->pos), free(ms->nrm), free(ms->col), free(ms->idx);
  }
}

/* the water tiles ('~' in the grid): a still sheet over each. The pools (features) are drawn with the terrain (Pool) */
static void build_water(mb_built *b, const tdmap *m) {
  int nt = 0;
  for (int y = 0; y < m->h; y++)
    for (int x = 0; x < m->w; x++) nt += m->t[y][x] == T_WATER;
  if (!nt) return;
  float *pos = malloc(sizeof(float) * 12 * (size_t)nt), *nrm = malloc(sizeof(float) * 12 * (size_t)nt);
  uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (size_t)nt);
  int v = 0, i = 0;
  for (int y = 0; y < m->h; y++)
    for (int x = 0; x < m->w; x++) {
      if (m->t[y][x] != T_WATER) continue;
      float cx = mp_wx(m, (float)x), cz = mp_wz(m, (float)y), hs = MP_TILE * 0.5f;
      static const float QX[4] = { -1, 1, 1, -1 }, QZ[4] = { -1, -1, 1, 1 };
      uint32_t o = (uint32_t)v;
      for (int c = 0; c < 4; c++, v++) pos[v * 3] = cx + QX[c] * hs, pos[v * 3 + 1] = POOL_LEVEL, pos[v * 3 + 2] = cz + QZ[c] * hs;
      uint32_t q[6] = { o, o + 2, o + 1, o, o + 3, o + 2 };
      memcpy(idx + i, q, sizeof q), i += 6;
    }
  for (int c = 0; c < v; c++) nrm[c * 3] = 0, nrm[c * 3 + 1] = 1, nrm[c * 3 + 2] = 0;
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, v, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nrm, v, 3));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, idx, i, 1));
  free(pos), free(nrm), free(idx);
  t3_material *mat = t3_mesh_standard_material_new(0xffffff);
  const float *wc = POOL[b->theme].deep;
  mat->color = mb_lin(wc[0], wc[1], wc[2]);
  mat->roughness = 0.12f;
  mat->transparent = true;
  mat->opacity = 0.8f;
  t3_mesh *mesh = t3_mesh_new(g, mat);
  mesh_node(b->node, "Water", mesh, false);
  t3_release(mesh), t3_release(g), t3_release(mat);
}

static t3_instanced_mesh *static_im(cw_node parent, const char *name, t3_geometry *g, t3_material *mat, int n, bool baked) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, mat, CW_MAX(n, 1));
  im->mesh.base.cast_shadow = false;
  im->mesh.base.receive_shadow = false;
  for (int i = 0; i < CW_MAX(n, 1); i++) t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 });
  im->instance_matrix->usage = T3_DYNAMIC_DRAW;
  im->instance_color->usage = T3_DYNAMIC_DRAW;
  t3_sphere sph = { { 0, 0, 0 }, 200 };
  t3_instanced_mesh_set_bounding_sphere(im, &sph);
  mesh_node(parent, name, &im->mesh, baked);
  im->count = n;
  t3_release(g), t3_release(mat);
  t3_release(im);
  return im;
}

/* where a prop stands: its tile's middle (a lamp out toward the path beside it), turned and sized by its record and
 * the tile's hash */
static void prop_place(const tdmap *m, const mp_prop *p, float *x, float *z, float *yaw, float *sc) {
  *x = mp_wx(m, p->x), *z = mp_wz(m, p->y);
  *yaw = (float)p->turn * 1.5707963f + fhash(p->x, p->y, 5) * 0.5f;
  *sc = 0.9f + 0.2f * fhash(p->x, p->y, 6);
  if (p->kind == 1) { /* a lamp: at the bank's top on the path's side */
    static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };
    for (int d = 0; d < 4; d++) {
      int nx = p->x + DX[(d + p->turn) & 3], ny = p->y + DY[(d + p->turn) & 3];
      if (mp_in(m, nx, ny) && m->t[ny][nx] == T_PATH) { *x += (float)DX[(d + p->turn) & 3] * 0.25f, *z += (float)DY[(d + p->turn) & 3] * 0.25f; break; }
    }
    *sc = 1.0f;
  }
}

/* the decor recedes: darker than the play and cooled against a warm theme (warmed against a cold one), so the trees
 * and rocks frame the paths and pads instead of competing with them */
static const float RECEDE[MP_THEMES][3] = { { 0.34f, 0.4f, 0.54f }, { 0.36f, 0.36f, 0.5f }, { 0.42f, 0.42f, 0.54f }, { 0.52f, 0.46f, 0.42f }, { 0.5f, 0.45f, 0.55f }, { 0.5f, 0.48f, 0.5f } };
static void build_props(mb_built *b, const tdmap *m) {
  for (int kind = 0; kind < 2; kind++) {
    int n = 0;
    for (int i = 0; i < m->nprop; i++) n += m->prop[i].kind == kind;
    if (!n) continue;
    char path[96];
    if (kind) snprintf(path, sizeof path, "models/lamp.cwchar");
    else mapbuild_kit(b->theme, path, sizeof path);
    mb_chr c;
    if (!mb_chr_load(&c, path)) continue;
    int frames = kind ? 1 : CW_MIN(c.total, 8);
    for (int f = 0; f < frames; f++) { /* each prop's own faces laid flush on each other 5 mm proud (in its own space) */
      t3_geometry *parts[MB_MAXG];
      for (int g = 0; g < MB_MAXG; g++) parts[g] = g < c.ngroup ? c.geo[g][f] : NULL;
      cop_separate_model(parts, MB_MAXG, 0.005f);
    }
    for (int f = 0; f < frames; f++) {
      int nf = 0;
      for (int i = 0; i < m->nprop; i++) nf += m->prop[i].kind == kind && (kind || m->prop[i].frame % frames == f);
      if (!nf) continue;
      for (int g = 0; g < c.ngroup && g < 2; g++) {
        if (!c.geo[g][f]) continue;
        char nm[24];
        snprintf(nm, sizeof nm, "%s%d_%d", kind ? "Lamp" : "Decor", f, g);
        t3_material *mat;
        if (g == 0) {
          mat = t3_mesh_standard_material_new(0xffffff);
          mat->vertex_colors = true;
          mat->roughness = 0.8f;
        } else {
          mat = t3_mesh_basic_material_new(0xffffff);
          mat->vertex_colors = true;
          static const float GLOW[MP_THEMES][3] = { { 2.0f, 1.6f, 0.8f }, { 2.2f, 1.4f, 0.6f }, { 2.0f, 1.5f, 0.8f }, { 1.2f, 1.8f, 2.6f }, { 3.2f, 1.1f, 0.25f }, { 0.7f, 2.4f, 2.6f } };
          const float *gc = kind ? GLOW[1] : GLOW[b->theme];
          mat->color = (t3_color){ gc[0], gc[1], gc[2] };
        }
        t3_instanced_mesh *im = static_im(b->node, nm, t3_retain(c.geo[g][f]), mat, nf, g == 0);
        int k = 0;
        for (int i = 0; i < m->nprop; i++) {
          const mp_prop *p = &m->prop[i];
          if (p->kind != kind || (!kind && p->frame % frames != f)) continue;
          float x, z, yaw, sc;
          prop_place(m, p, &x, &z, &yaw, &sc);
          t3_mat4 mm;
          t3_quat q = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
          t3_mat4_compose(&mm, (t3_vec3){ x, MP_GROUND_H, z }, q, (t3_vec3){ sc, sc, sc });
          if (!kind && g == 0) t3_instanced_mesh_set_color_at(im, k, (t3_color){ RECEDE[b->theme][0], RECEDE[b->theme][1], RECEDE[b->theme][2] });
          t3_instanced_mesh_set_matrix_at(im, k++, &mm);
        }
        im->instance_matrix->version++;
        im->instance_color->version++;
      }
    }
    mb_chr_free(&c);
  }
}

static cw_node build_lights(mb_built *b, const tdmap *m) {
  cw_node group = cw_node_new("Node3D", "BakeLights");
  cw_node_add_child(b->node, group);
  for (int i = 0; i < m->nlight; i++) {
    const mp_light *l = &m->light[i];
    cw_node n = cw_node_new("OmniLight3D", "Omni");
    cw_node_add_child(group, n);
    float x = mp_wx(m, l->x), z = mp_wz(m, l->y), base = mapbuild_height(m, x, z);
    for (int k = 0; k < m->nprop; k++) /* a lamp's light sits in its lantern */
      if (m->prop[k].kind == 1 && m->prop[k].x == l->x && m->prop[k].y == l->y) {
        float yaw, sc;
        prop_place(m, &m->prop[k], &x, &z, &yaw, &sc);
        base = MP_GROUND_H;
        break;
      }
    float lh = l->h, le = l->energy;
    for (int k = 0; k < m->nfeat; k++) /* a pool's own glow: lifted, so it lights the pool and its banks evenly, not in one hot spot */
      if (m->feat[k].kind == FEAT_WATER && CW_CLAMP((int)floorf(m->feat[k].x), 0, m->w - 1) == l->x && CW_CLAMP((int)floorf(m->feat[k].y), 0, m->h - 1) == l->y) {
        lh = 2.4f, base = POOL_LEVEL, le *= b->theme == MP_THEME_CRYSTAL ? 0.5f : 0.8f;
        break;
      }
    cw_node3d_set_position(n, (t3_vec3){ x, base + lh, z });
    cw_node_set(n, "color", cw_val_color(l->r, l->g, l->b, 1));
    cw_node_set(n, "energy", cw_val_float(le));
    cw_node_set(n, "range", cw_val_float(l->range));
    cw_node_set(n, "decay", cw_val_float(2));
    cw_node_set(n, "bake_direct", cw_val_bool(true));
    cw_node_set(n, "realtime", cw_val_bool(false));
  }
  return group;
}

bool mapbuild_build(cw_node parent, const tdmap *m, mb_built *b) {
  memset(b, 0, sizeof *b);
  b->m = m;
  b->theme = CW_CLAMP(m->theme, 0, MP_THEMES - 1);
  b->node = cw_node_new("Node3D", "Level");
  cw_node_add_child(parent, b->node);
  char scene[64];
  snprintf(scene, sizeof scene, "scenes/th_%s.cwscene", MP_THEME_ID[b->theme]);
  cw_node light = cw_scene_instance_path(scene);
  if (cw_node_valid(light)) {
    cw_node_set_name(light, "Light");
    cw_node_add_child(b->node, light);
  } else
    cw_error("towers: the light scene %s did not load", scene);
  build_terrain(b, m);
  if (!getenv("TOWERS_LOOK_MASK")) build_water(b, m), build_props(b, m);
  return true;
}

bool mapbuild_lightmap(cw_tree *t, mb_built *b, const char *bake_path, const char *text) {
  if (b->lm) return true;
  if (bake_path && *bake_path) {
    cw_lightmap_settings s = cw_lightmap_settings_default();
    s.texel_density = 6.5f;
    s.max_size = 1024;
    s.samples = 32;
    s.bounces = 1;
    s.probe_spacing = 4.0f;
    s.probe_samples = 48;
    s.denoise = 1;
    s.direct_samples = 8;
    s.shadow_softness = 0.03f;
    s.reflection_size = 0;
    double t0 = cw_time();
    cw_node lights = build_lights(b, b->m);
    cw_tree_pre_render(t, 0);
    b->lm = cw_lightmap_bake(t, b->node, &s);
    cw_node_free(lights);
    if (!b->lm) return false;
    if (!cw_lightmap_save(b->lm, bake_path)) cw_error("towers: could not write the bake to %s", bake_path);
    char hp[512];
    snprintf(hp, sizeof hp, "%.*s.hash", (int)(strlen(bake_path) > 5 ? strlen(bake_path) - 5 : strlen(bake_path)), bake_path);
    FILE *f = text ? fopen(hp, "wb") : NULL;
    if (f) fprintf(f, "%016llx\n", (unsigned long long)mapbuild_bake_hash(text, b->theme)), fclose(f);
    cw_lightmap_info info;
    cw_lightmap_get_info(b->lm, &info);
    size_t len = 0;
    cw_lightmap_bytes(b->lm, &len);
    fprintf(stderr, "towers: baked map %d: %d meshes, %d texels, %d probes, %.1f s, %zu bytes\n", b->m->num, info.meshes, info.texels, info.probes, cw_time() - t0, len);
  } else {
    char path[64];
    map_bake_path(b->m->num, path, sizeof path);
    b->lm = cw_vfs_exists(path) ? cw_lightmap_load(path) : NULL;
    if (!b->lm) {
      cw_error("towers: map %d has no bake (%s): it draws unbaked", b->m->num, path);
      return false;
    }
  }
  static const float GAIN[MP_THEMES] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
  cw_lightmap_apply_shaded(t, b->node, b->lm, GAIN[b->theme], CW_LIGHTMAP_BAKED);
  return true;
}

t3_color mapbuild_light_at(const mb_built *b, float x, float z) {
  if (!b->lm) return (t3_color){ 1, 1, 1 };
  t3_color e = { 0, 0, 0 }, f = { 0, 0, 0 };
  float y = mapbuild_height(b->m, x, z);
  bool hp = cw_lightmap_probe_irradiance(b->lm, (t3_vec3){ x, y + 1.2f, z }, (t3_vec3){ 0, 0.88f, 0.47f }, &e);
  bool hf = cw_lightmap_irradiance_below(b->lm, (t3_vec3){ x, y + 1.0f, z }, &f);
  if (!hp && !hf) return (t3_color){ 1, 1, 1 };
  const float base = 0.45f, gain = 0.25f;
  t3_color c = { base + gain * 0.5f * (e.r + f.r), base + gain * 0.5f * (e.g + f.g), base + gain * 0.5f * (e.b + f.b) };
  c.r = CW_MIN(c.r, 2.0f), c.g = CW_MIN(c.g, 2.0f), c.b = CW_MIN(c.b, 2.0f);
  return c;
}

void mapbuild_free(mb_built *b) {
  if (cw_node_valid(b->node)) cw_node_free(b->node);
  memset(b, 0, sizeof *b);
}
