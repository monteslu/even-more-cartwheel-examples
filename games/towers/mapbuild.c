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
  return h;
}

/* ── the heightfield ─────────────────────────────────────────────────── */

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

#define RES 8 /* heightfield samples a tile (a quarter metre: smooth curves on the paths, outcrops and pools, a crisp curb) */
/* a tile's kind, with the margin round the map copying the nearest edge tile (a gate's path runs on out of it) */
static int tile_at(const tdmap *m, int x, int y) {
  int cx = CW_CLAMP(x, 0, m->w - 1), cy = CW_CLAMP(y, 0, m->h - 1);
  int t = m->t[cy][cx];
  if ((cx != x || cy != y) && t == T_SLOT) t = T_GROUND;
  if ((cx != x || cy != y) && t == T_PATH) { /* a path on the edge runs on out only straight across the edge */
    bool across = (x < 0 || x >= m->w) ? (y == cy) : (x == cx);
    if (!across || (x < 0 || x >= m->w) == (y < 0 || y >= m->h)) t = T_GROUND;
  }
  return t;
}
static float kind_h(int t) { return t == T_PATH ? 0.0f : t == T_WATER ? -0.4f : t == T_ROCK ? MP_ROCK_H : MP_GROUND_H; }
/* a heightfield vertex's height: the lowest of the tiles it touches (a bank runs down into the higher tile), rock
 * rough on top. i, j count samples from the margin's corner */
/* the features at a point in tile units (tiles span [k, k + 1]): the outcrops' height there (the highest dome) and the
 * pools' depth (0 .. 1, deepest) */
static float bump(float t) { return t >= 1 ? 0 : 0.5f + 0.5f * cw_cos(3.14159265f * t); }
static void feat_at(const tdmap *m, float x, float y, float *rock, float *water) {
  *rock = 0, *water = 0;
  for (int k = 0; k < m->nfeat; k++) {
    const mp_feat *f = &m->feat[k];
    float b = bump(sqrtf((x - f->x) * (x - f->x) + (y - f->y) * (y - f->y)) / f->r);
    if (f->kind == FEAT_ROCK) *rock = CW_MAX(*rock, f->h * b);
    else *water = CW_MAX(*water, b);
  }
}
static float vert_h(const tdmap *m, int i, int j) {
  int tx0 = (i - 1) / RES - MB_MARGIN - ((i - 1) < 0), tx1 = i / RES - MB_MARGIN;
  int ty0 = (j - 1) / RES - MB_MARGIN - ((j - 1) < 0), ty1 = j / RES - MB_MARGIN;
  if (i % RES) tx0 = tx1;
  if (j % RES) ty0 = ty1;
  float h = 1e9f;
  for (int y = ty0; y <= ty1; y++)
    for (int x = tx0; x <= tx1; x++) h = CW_MIN(h, kind_h(tile_at(m, x, y)));
  if (h > 0.01f) { /* a notch in a curve of path (path beside the tile on two sides and across their corner): the corner cut on
                    * the diagonal, so a curve drawn in tiles reads as a curve, not as steps */
    float fx = (float)i / RES - MB_MARGIN, fy = (float)j / RES - MB_MARGIN;
    for (int y = ty0; y <= ty1; y++)
      for (int x = tx0; x <= tx1; x++) {
        if (tile_at(m, x, y) != T_GROUND) continue;
        for (int c = 0; c < 4; c++) {
          int dx = c == 0 || c == 1 ? 1 : -1, dy = c == 1 || c == 2 ? 1 : -1;
          if (tile_at(m, x + dx, y) != T_PATH || tile_at(m, x, y + dy) != T_PATH || tile_at(m, x + dx, y + dy) != T_PATH) continue;
          float s = fabsf(fx - (float)x - (dx > 0 ? 1.0f : 0.0f)) + fabsf(fy - (float)y - (dy > 0 ? 1.0f : 0.0f));
          h = CW_MIN(h, MP_GROUND_H * CW_CLAMP((s - 0.5f) / 1.41421356f / 0.25f, 0.0f, 1.0f));
        }
      }
  }
  if (h >= MP_ROCK_H - 0.01f) h += 0.45f * fhash(i, j, 41) + 0.35f * vnoise((float)i * 0.4f, (float)j * 0.4f, 43);
  float rock, water;
  feat_at(m, (float)i / RES - MB_MARGIN, (float)j / RES - MB_MARGIN, &rock, &water);
  if (h >= MP_GROUND_H - 0.01f) h += rock;
  if (water > 0) h = CW_MIN(h, MP_GROUND_H - (MP_GROUND_H + 0.6f) * water);
  return h;
}
float mapbuild_height(const tdmap *m, float x, float z) {
  float fi = (x / MP_TILE + (float)m->w * 0.5f + (float)MB_MARGIN) * RES, fj = (z / MP_TILE + (float)m->h * 0.5f + (float)MB_MARGIN) * RES;
  int i = (int)floorf(fi), j = (int)floorf(fj);
  float u = fi - (float)i, v = fj - (float)j;
  float a = vert_h(m, i, j), b = vert_h(m, i + 1, j), c = vert_h(m, i, j + 1), d = vert_h(m, i + 1, j + 1);
  return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

/* the themes' palettes (screen colours): the ground, its accent in patches, the path, the banks and the rock; the
 * textures' tile size (m) */
typedef struct palette { float ground[3], accent[3], path[3], bank[3], rock[3], water[3], tile_g, tile_p; } palette;
/* each theme a value hierarchy with a complementary accent (DESIGN.md, the look): the path a clear step lighter or
 * darker than the ground and in the opposite temperature, a dark curb on the banks between them, the water cool where
 * the ground is warm */
static const palette PAL[MP_THEMES] = {
  { { 0.25f, 0.48f, 0.14f }, { 0.46f, 0.58f, 0.14f }, { 0.98f, 0.91f, 0.74f }, { 0.20f, 0.16f, 0.11f }, { 0.55f, 0.57f, 0.62f }, { 0.12f, 0.55f, 0.78f }, 4, 3 },
  { { 0.64f, 0.39f, 0.12f }, { 0.64f, 0.22f, 0.08f }, { 0.72f, 0.84f, 0.99f }, { 0.18f, 0.12f, 0.10f }, { 0.44f, 0.47f, 0.56f }, { 0.12f, 0.50f, 0.62f }, 4, 3 },
  { { 0.93f, 0.76f, 0.48f }, { 0.88f, 0.54f, 0.30f }, { 0.17f, 0.21f, 0.33f }, { 0.22f, 0.14f, 0.10f }, { 0.72f, 0.36f, 0.24f }, { 0.08f, 0.66f, 0.72f }, 5, 3 },
  { { 0.90f, 0.94f, 0.99f }, { 0.72f, 0.84f, 0.97f }, { 0.46f, 0.33f, 0.25f }, { 0.20f, 0.18f, 0.22f }, { 0.36f, 0.40f, 0.52f }, { 0.20f, 0.52f, 0.86f }, 5, 3 },
  { { 0.35f, 0.29f, 0.40f }, { 0.85f, 0.30f, 0.06f }, { 0.46f, 0.82f, 0.90f }, { 0.07f, 0.06f, 0.07f }, { 0.17f, 0.15f, 0.18f }, { 1.00f, 0.42f, 0.10f }, 4, 3 },
  { { 0.32f, 0.24f, 0.56f }, { 0.20f, 0.58f, 0.64f }, { 1.00f, 0.92f, 0.62f }, { 0.14f, 0.10f, 0.24f }, { 0.50f, 0.55f, 0.74f }, { 0.30f, 0.85f, 0.90f }, 4, 3 },
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

/* the ground and the path: one heightfield, its quads split between the two meshes (each with uv0 over the whole
 * field: the lightmap's layout) */
static void build_terrain(mb_built *b, const tdmap *m) {
  const int NI = (m->w + 2 * MB_MARGIN) * RES + 1, NJ = (m->h + 2 * MB_MARGIN) * RES + 1;
  const float W = (float)(m->w + 2 * MB_MARGIN) * MP_TILE, H = (float)(m->h + 2 * MB_MARGIN) * MP_TILE;
  const float x0 = -W * 0.5f, z0 = -H * 0.5f, step = MP_TILE / RES;
  const palette *pal = &PAL[b->theme];
  int nv = NI * NJ;
  float *hgt = malloc(sizeof(float) * (size_t)nv);
  float *wet = malloc(sizeof(float) * (size_t)nv); /* how far into a pool each vertex is */
  for (int j = 0; j < NJ; j++)
    for (int i = 0; i < NI; i++) {
      hgt[j * NI + i] = vert_h(m, i, j);
      float rk;
      feat_at(m, (float)i / RES - MB_MARGIN, (float)j / RES - MB_MARGIN, &rk, &wet[j * NI + i]);
    }
  for (int part = 0; part < 2; part++) { /* 0 the ground, 1 the path */
    float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *nrm = malloc(sizeof(float) * 3 * (size_t)nv), *col = malloc(sizeof(float) * 3 * (size_t)nv),
          *uv = malloc(sizeof(float) * 2 * (size_t)nv);
    uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (size_t)(NI - 1) * (size_t)(NJ - 1));
    for (int j = 0; j < NJ; j++)
      for (int i = 0; i < NI; i++) {
        int v = j * NI + i;
        float x = x0 + (float)i * step, z = z0 + (float)j * step, h = hgt[v];
        pos[v * 3] = x, pos[v * 3 + 1] = h, pos[v * 3 + 2] = z;
        float hl = hgt[j * NI + CW_MAX(i - 1, 0)], hr = hgt[j * NI + CW_MIN(i + 1, NI - 1)], hd = hgt[CW_MAX(j - 1, 0) * NI + i], hu = hgt[CW_MIN(j + 1, NJ - 1) * NI + i];
        float nx = (hl - hr) / (2 * step), nz = (hd - hu) / (2 * step), nl = sqrtf(nx * nx + 1 + nz * nz);
        nrm[v * 3] = nx / nl, nrm[v * 3 + 1] = 1 / nl, nrm[v * 3 + 2] = nz / nl;
        uv[v * 2] = (float)i / (float)(NI - 1), uv[v * 2 + 1] = (float)j / (float)(NJ - 1);
        /* colour: by what the vertex is (path, bank, ground, rock, water bed), mottled, the theme's accent in patches */
        float slope = sqrtf(nx * nx + nz * nz);
        float k = 0.85f + 0.42f * (vnoise(x / 6, z / 6, 11 + (uint32_t)m->num) - 0.5f) + 0.26f * (vnoise(x / 1.7f, z / 1.7f, 12) - 0.5f);
        float a = 0.55f * CW_CLAMP((vnoise(x / 5.0f, z / 5.0f, 31 + (uint32_t)m->num) - 0.52f) * 3.2f, 0.0f, 1.0f);
        float c[3];
        for (int q = 0; q < 3; q++) {
          float gq = pal->ground[q] + a * (pal->accent[q] - pal->ground[q]);
          if (wet[v] > 0.01f) c[q] = h < MP_GROUND_H - 0.1f ? pal->water[q] * 0.5f + pal->bank[q] * 0.5f : pal->bank[q] + (pal->ground[q] - pal->bank[q]) * 0.5f;
          else if (h < -0.05f) c[q] = pal->water[q] * 0.55f + pal->bank[q] * 0.45f;
          else if (h < 0.02f) c[q] = pal->path[q] * (0.92f + 0.16f * (vnoise(x / 2.5f, z / 2.5f, 17) - 0.5f) * 2);
          else if (h > MP_GROUND_H + 0.05f) c[q] = pal->rock[q] * (0.8f + 0.4f * fhash(i / 2, j / 2, 7)) * (0.45f + 0.75f * CW_MIN((h - MP_GROUND_H) / 1.4f, 1.0f)); /* modelled: dark at the foot, light on top */
          else if (slope > 0.3f) c[q] = pal->bank[q]; /* the curb: a dark band that draws the path's edge */
          else c[q] = gq;
          c[q] *= k;
        }
        t3_color l = mb_lin(CW_MIN(c[0], 1.0f), CW_MIN(c[1], 1.0f), CW_MIN(c[2], 1.0f));
        col[v * 3] = l.r, col[v * 3 + 1] = l.g, col[v * 3 + 2] = l.b;
      }
    int nq = 0;
    for (int j = 0; j < NJ - 1; j++)
      for (int i = 0; i < NI - 1; i++) {
        int a = j * NI + i, bb = a + 1, c = a + NI, d = c + 1;
        bool dry = wet[a] < 0.005f && wet[bb] < 0.005f && wet[c] < 0.005f && wet[d] < 0.005f;
        bool low = hgt[a] < 0.02f && hgt[bb] < 0.02f && hgt[c] < 0.02f && hgt[d] < 0.02f && hgt[a] > -0.05f && hgt[bb] > -0.05f && hgt[c] > -0.05f && hgt[d] > -0.05f && dry;
        if (low != (part == 1)) continue;
        uint32_t *q = idx + nq * 6;
        /* split along the diagonal that follows the shape (a bank's corner stays a corner) */
        if (fabsf(hgt[a] - hgt[d]) <= fabsf(hgt[bb] - hgt[c])) q[0] = (uint32_t)a, q[1] = (uint32_t)c, q[2] = (uint32_t)d, q[3] = (uint32_t)a, q[4] = (uint32_t)d, q[5] = (uint32_t)bb;
        else q[0] = (uint32_t)a, q[1] = (uint32_t)c, q[2] = (uint32_t)bb, q[3] = (uint32_t)bb, q[4] = (uint32_t)c, q[5] = (uint32_t)d;
        nq++;
      }
    t3_geometry *g = t3_geometry_new();
    t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, nv, 3));
    t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nrm, nv, 3));
    t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, nv, 3));
    t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, nv, 2));
    t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, idx, nq * 6, 1));
    free(pos), free(nrm), free(col), free(uv), free(idx);
    t3_material *gm = t3_mesh_standard_material_new(0xffffff);
    gm->vertex_colors = true;
    gm->roughness = 0.9f;
    char path[64];
    snprintf(path, sizeof path, "textures/%s_%s.png", MP_THEME_ID[b->theme], part ? "path" : "ground");
    t3_texture *tx = load_png(path);
    if (tx) {
      float tile = part ? pal->tile_p : pal->tile_g;
      tx->repeat = (t3_vec2){ W / tile, H / tile };
      t3_material_set_texture(gm, T3_MAP, tx);
      t3_release(tx);
    } else
      cw_error("towers: the texture %s did not load", path);
    t3_mesh *mesh = t3_mesh_new(g, gm);
    cw_node n = mesh_node(b->node, part ? "Path" : "Ground", mesh, true);
    cw_node_set(n, "lightmap_uv", cw_val_enum("UV0"));
    t3_release(mesh), t3_release(g), t3_release(gm);
  }
  free(hgt), free(wet);
}

/* the water: a still sheet over every water tile and over every pool (a disc the banks rise through) */
static void build_water(mb_built *b, const tdmap *m) {
  enum { SEG = 40 };
  int nt = 0, nf = 0;
  for (int y = 0; y < m->h; y++)
    for (int x = 0; x < m->w; x++) nt += m->t[y][x] == T_WATER;
  for (int k = 0; k < m->nfeat; k++) nf += m->feat[k].kind == FEAT_WATER;
  if (!nt && !nf) return;
  int nv = nt * 4 + nf * (SEG + 1), ni = nt * 6 + nf * SEG * 3;
  float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *nrm = malloc(sizeof(float) * 3 * (size_t)nv);
  uint32_t *idx = malloc(sizeof(uint32_t) * (size_t)ni);
  int v = 0, i = 0;
  const float LEVEL = -0.12f;
  for (int y = 0; y < m->h; y++)
    for (int x = 0; x < m->w; x++) {
      if (m->t[y][x] != T_WATER) continue;
      float cx = mp_wx(m, (float)x), cz = mp_wz(m, (float)y), hs = MP_TILE * 0.5f;
      static const float QX[4] = { -1, 1, 1, -1 }, QZ[4] = { -1, -1, 1, 1 };
      uint32_t o = (uint32_t)v;
      for (int c = 0; c < 4; c++, v++) pos[v * 3] = cx + QX[c] * hs, pos[v * 3 + 1] = LEVEL, pos[v * 3 + 2] = cz + QZ[c] * hs;
      uint32_t q[6] = { o, o + 2, o + 1, o, o + 3, o + 2 };
      memcpy(idx + i, q, sizeof q), i += 6;
    }
  for (int k = 0; k < m->nfeat; k++) {
    const mp_feat *f = &m->feat[k];
    if (f->kind != FEAT_WATER) continue;
    float cx = (f->x - (float)m->w * 0.5f) * MP_TILE, cz = (f->y - (float)m->h * 0.5f) * MP_TILE, r = f->r * MP_TILE;
    uint32_t o = (uint32_t)v;
    pos[v * 3] = cx, pos[v * 3 + 1] = LEVEL, pos[v * 3 + 2] = cz, v++;
    for (int s = 0; s < SEG; s++, v++) {
      float a = (float)s * 6.2831853f / SEG;
      pos[v * 3] = cx + cw_cos(a) * r, pos[v * 3 + 1] = LEVEL, pos[v * 3 + 2] = cz + cw_sin(a) * r;
    }
    for (int s = 0; s < SEG; s++) idx[i++] = o, idx[i++] = o + 1 + (uint32_t)((s + 1) % SEG), idx[i++] = o + 1 + (uint32_t)s;
  }
  for (int c = 0; c < v; c++) nrm[c * 3] = 0, nrm[c * 3 + 1] = 1, nrm[c * 3 + 2] = 0;
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, v, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nrm, v, 3));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, idx, i, 1));
  free(pos), free(nrm), free(idx);
  t3_material *mat = t3_mesh_standard_material_new(0xffffff);
  const float *wc = PAL[b->theme].water;
  mat->color = mb_lin(wc[0], wc[1], wc[2]);
  mat->roughness = b->theme == MP_THEME_SNOW ? 0.35f : 0.12f;
  mat->metalness = 0.0f;
  mat->transparent = true;
  mat->opacity = b->theme == MP_THEME_SNOW ? 0.88f : 0.75f;
  if (b->theme == MP_THEME_VOLCANO) mat->emissive = (t3_color){ 0.75f, 0.2f, 0.03f }, mat->color = mb_lin(0.55f, 0.12f, 0.03f), mat->opacity = 0.95f; /* lava */
  if (b->theme == MP_THEME_CRYSTAL) mat->emissive = (t3_color){ 0.15f, 0.55f, 0.6f };
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
    cw_node3d_set_position(n, (t3_vec3){ x, base + l->h, z });
    cw_node_set(n, "color", cw_val_color(l->r, l->g, l->b, 1));
    cw_node_set(n, "energy", cw_val_float(l->energy));
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
  build_water(b, m);
  build_props(b, m);
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
