/* skate: procedural PBR texture sets for the parks and a small mesh builder (the noise and the texel pipeline after the
 * arena and derby examples: our own code, MIT).
 *
 * Every surface is a full PBR set made at load time (no image files): albedo (sRGB), a tangent-space normal map from a
 * height field, roughness and ambient occlusion. All tile without seams, are mipmapped with 8x anisotropy and are made
 * once, in bands of rows (the loading screen keeps drawing). The albedos are light and nearly neutral: the colour of a
 * ramp, a ledge or a wall is its vertex tint (parks.c), the texture gives it the grain: trowelled concrete with its
 * joints, plywood sheets with their screws, brushed steel, pavers, brick with windows, grass and leaves.
 * Integer hashing and cartwheel/math.h only, so every host makes the same texels. */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "view.h"

static uint32_t hash2(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float h01(int x, int y, uint32_t seed) { return (float)(hash2(x, y, seed) & 0xffffff) / 16777216.0f; }
static float smooth(float t) { return t * t * (3 - 2 * t); }
static int wrapi(int a, int p) { return ((a % p) + p) % p; }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float fractf(float x) { return x - floorf(x); }

static float vnoise(float x, float y, int p, uint32_t seed) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = smooth(x - (float)x0), fy = smooth(y - (float)y0);
  float a = h01(wrapi(x0, p), wrapi(y0, p), seed), b = h01(wrapi(x0 + 1, p), wrapi(y0, p), seed);
  float c = h01(wrapi(x0, p), wrapi(y0 + 1, p), seed), d = h01(wrapi(x0 + 1, p), wrapi(y0 + 1, p), seed);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float u, float v, int p, int oct, uint32_t seed) {
  float s = 0, amp = 0.5f, norm = 0;
  for (int o = 0; o < oct; o++) {
    s += amp * vnoise(u * (float)p, v * (float)p, p, seed + (uint32_t)o * 101u);
    norm += amp;
    amp *= 0.5f;
    p *= 2;
  }
  return s / norm;
}
static void cells(float u, float v, int p, uint32_t seed, float *d1, float *d2, uint32_t *id) {
  float x = u * (float)p, y = v * (float)p;
  int cx = (int)floorf(x), cy = (int)floorf(y);
  *d1 = *d2 = 9;
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
      int gx = cx + i, gy = cy + j;
      float px = (float)gx + h01(wrapi(gx, p), wrapi(gy, p), seed), py = (float)gy + h01(wrapi(gx, p), wrapi(gy, p), seed + 7);
      float d = sqrtf((px - x) * (px - x) + (py - y) * (py - y));
      if (d < *d1) {
        *d2 = *d1;
        *d1 = d;
        *id = hash2(wrapi(gx, p), wrapi(gy, p), seed + 13);
      } else if (d < *d2) *d2 = d;
    }
}

typedef struct texel { float r, g, b, a, h, rough, ao; } texel;
static void rgb(texel *t, float r, float g, float b) { t->r = r, t->g = g, t->b = b; }

/* ── generators: u, v in 0..1 over one tile ──────────────────────────────── */

/* trowelled park concrete (a 4 m tile): soft mottling, fine pits, sawn joints every 2 m, tyre and wheel marks */
static void g_concrete(float u, float v, texel *t) {
  float n = fbm(u, v, 6, 5, 21), fine = fbm(u, v, 96, 2, 22), pit = h01((int)(u * 512), (int)(v * 512), 23) > 0.997f;
  float ju = fminf(fractf(u * 2), 1 - fractf(u * 2)), jv = fminf(fractf(v * 2), 1 - fractf(v * 2));
  float joint = 1 - clamp01((fminf(ju, jv) - 0.002f) * 500);
  float marks = clamp01((fbm(u * 3, v * 0.6f, 4, 3, 24) - 0.62f) * 4) * 0.5f;
  float c = 0.86f + 0.07f * (n - 0.5f) + 0.03f * (fine - 0.5f) - 0.06f * marks;
  rgb(t, c, c * 0.99f, c * 0.97f);
  t->r -= 0.1f * joint, t->g -= 0.1f * joint, t->b -= 0.1f * joint;
  t->h = 0.5f + 0.12f * n + 0.05f * fine - 0.25f * pit - 0.12f * joint;
  t->rough = 0.72f + 0.12f * fine - 0.1f * marks;
  t->ao = 1 - 0.12f * joint - 0.2f * pit;
}
/* asphalt (a 4 m tile): dark aggregate, patched, a little worn */
static void g_asphalt(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 160, 31, &d1, &d2, &id);
  float stone = clamp01((d2 - d1) * 4), tone = (float)(id & 255) / 255.0f;
  float patch = clamp01((fbm(u, v, 3, 3, 32) - 0.6f) * 6);
  float c = 0.62f + 0.22f * tone * stone + 0.08f * (fbm(u, v, 8, 3, 33) - 0.5f);
  c = mixf(c, 0.5f, patch);
  rgb(t, c, c, c * 1.02f);
  t->h = 0.3f + 0.4f * stone;
  t->rough = 0.88f - 0.1f * stone;
  t->ao = 0.75f + 0.25f * stone;
}
/* ramp plywood (a 2.4 m tile, sheets 1.2 x 2.4): grain, seams, screw rows, wheel polish down the middle */
static void g_wood(float u, float v, texel *t) {
  float su = fractf(u * 2), sv = v;
  int sheet = (int)floorf(u * 2);
  float seam = 1 - clamp01((fminf(fminf(su, 1 - su) * 1.2f, fminf(sv, 1 - sv) * 2.4f) - 0.004f) * 300);
  float warp = fbm(u, v, 4, 3, 41 + (uint32_t)sheet) * 0.3f;
  float grain = cw_pow(1 - fabsf(fractf(v * 46 + warp) - 0.5f) * 2, 6) * 0.5f;
  float tone = h01(sheet & 1, 0, 42);
  /* screws: a row every 30 cm along each sheet's edges */
  float sx = fminf(su, 1 - su) * 1.2f, sy = fractf(v * 8);
  float screw = (sx > 0.03f && sx < 0.05f && fabsf(sy - 0.5f) < 0.04f) ? 1 : 0;
  float polish = clamp01(1 - fabsf(su - 0.5f) * 3) * 0.6f;
  float c = 0.86f + 0.06f * tone - 0.08f * grain + 0.03f * (fbm(u, v, 32, 2, 43) - 0.5f);
  rgb(t, c, c * 0.97f, c * 0.93f);
  t->r -= 0.4f * seam + 0.35f * screw, t->g -= 0.4f * seam + 0.35f * screw, t->b -= 0.4f * seam + 0.35f * screw;
  t->h = 0.5f - 0.05f * grain - 0.4f * seam - 0.2f * screw;
  t->rough = 0.62f - 0.22f * polish + 0.08f * grain;
  t->ao = 1 - 0.4f * seam - 0.3f * screw;
}
/* painted sides (a 2 m tile): brushed paint, scuffs to the wood under it near the bottom */
static void g_paint(float u, float v, texel *t) {
  float brush = fbm(u * 6, v * 0.5f, 8, 3, 51), n = fbm(u, v, 6, 4, 52);
  float scuff = clamp01((fbm(u, v, 10, 4, 53) - 0.66f) * 5) * clamp01(1 - v * 1.6f);
  float c = 0.92f + 0.04f * (brush - 0.5f) + 0.03f * (n - 0.5f);
  rgb(t, mixf(c, 0.55f, scuff), mixf(c, 0.47f, scuff), mixf(c, 0.38f, scuff));
  t->h = 0.5f + 0.06f * brush - 0.2f * scuff;
  t->rough = 0.45f + 0.1f * brush + 0.35f * scuff;
  t->ao = 1 - 0.1f * scuff;
}
/* brushed steel (a 1 m tile): long brush lines, polished where the trucks run */
static void g_metal(float u, float v, texel *t) {
  float line = fbm(u * 0.5f, v * 64, 8, 2, 61), n = fbm(u, v, 8, 3, 62);
  float c = 0.86f + 0.08f * (line - 0.5f) + 0.04f * (n - 0.5f);
  rgb(t, c, c, c * 1.02f);
  t->h = 0.5f + 0.05f * line;
  t->rough = 0.3f + 0.15f * line;
  t->ao = 1;
}
/* summer grass (a 3 m tile) */
static void g_grass(float u, float v, texel *t) {
  float n1 = fbm(u, v, 4, 4, 71), n2 = fbm(u, v, 48, 3, 72), blade = h01((int)(u * 512), (int)(v * 512), 73);
  float k = 0.82f + 0.2f * (n1 - 0.5f) + 0.15f * (blade - 0.5f);
  rgb(t, k * 0.95f, k, k * 0.85f);
  t->h = 0.4f + 0.4f * blade * n2;
  t->rough = 0.9f;
  t->ao = 0.8f + 0.2f * blade;
}
/* brick with windows (a 6 m tile: 3 m storeys, 3 windows across, white frames, blue glass) */
static void g_brick(float u, float v, texel *t) {
  float x = u * 3, y = v * 2;
  float wu = fractf(x), wv = fractf(y);
  bool win = wu > 0.28f && wu < 0.72f && wv > 0.32f && wv < 0.82f;
  bool frame = wu > 0.25f && wu < 0.75f && wv > 0.28f && wv < 0.86f && !win;
  if (win || frame) {
    if (frame) {
      rgb(t, 0.95f, 0.95f, 0.93f);
      t->h = 0.7f, t->rough = 0.5f, t->ao = 1;
    } else {
      float refl = clamp01((wv - 0.32f) * 2.5f) * 0.3f + 0.3f * fbm(u, v, 6, 2, 81);
      bool bar = fabsf(wu - 0.5f) < 0.012f || fabsf(wv - 0.57f) < 0.012f;
      rgb(t, bar ? 0.9f : 0.25f + refl * 0.4f, bar ? 0.9f : 0.33f + refl * 0.5f, bar ? 0.88f : 0.45f + refl * 0.5f);
      t->h = 0.3f, t->rough = bar ? 0.5f : 0.08f, t->ao = 0.85f;
    }
    return;
  }
  float rowf = v * 40;
  int row = (int)floorf(rowf);
  float bx = u * 24 + (row & 1) * 0.5f;
  float fu = fractf(bx), fv = fractf(rowf);
  float mortar = 1 - clamp01((fminf(fminf(fu, 1 - fu) * 2, fminf(fv, 1 - fv)) - 0.06f) * 30);
  float tone = h01((int)floorf(bx) & 63, row & 63, 82);
  float c = 0.84f + 0.14f * (tone - 0.5f) + 0.05f * (fbm(u, v, 16, 3, 83) - 0.5f);
  rgb(t, mixf(c, 0.92f, mortar), mixf(c * 0.98f, 0.9f, mortar), mixf(c * 0.96f, 0.86f, mortar));
  t->h = 0.5f + 0.2f * (1 - mortar) - 0.3f * mortar;
  t->rough = 0.85f;
  t->ao = 1 - 0.3f * mortar;
}
/* a gravel roof (a 3 m tile) */
static void g_roof(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 96, 91, &d1, &d2, &id);
  float stone = clamp01((d2 - d1) * 5), tone = (float)(id & 255) / 255.0f;
  float c = 0.78f + 0.18f * tone * stone;
  rgb(t, c, c * 0.98f, c * 0.95f);
  t->h = stone * 0.7f;
  t->rough = 0.9f;
  t->ao = 0.6f + 0.4f * stone;
}
/* square pavers (a 4.8 m tile of 0.6 m slabs) */
static void g_tile(float u, float v, texel *t) {
  float tu = fractf(u * 8), tv = fractf(v * 8);
  int ix = (int)floorf(u * 8), iy = (int)floorf(v * 8);
  float e = fminf(fminf(tu, 1 - tu), fminf(tv, 1 - tv));
  float grout = 1 - clamp01((e - 0.02f) * 120);
  float id = h01(ix & 7, iy & 7, 101);
  float n = fbm(u, v, 16, 4, 102);
  float c = 0.9f + 0.1f * (id - 0.5f) + 0.06f * (n - 0.5f);
  rgb(t, mixf(c, 0.55f, grout), mixf(c * 0.98f, 0.54f, grout), mixf(c * 0.95f, 0.52f, grout));
  t->h = 0.35f + 0.5f * clamp01(e * 25) * (1 - grout);
  t->rough = mixf(0.6f + 0.2f * id, 0.95f, grout);
  t->ao = 1 - 0.45f * grout;
}
/* planter soil with grass tufts (a 2 m tile) */
static void g_dirt(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 4, 111), tuft = clamp01((fbm(u, v, 12, 3, 112) - 0.45f) * 3);
  rgb(t, mixf(0.62f, 0.95f, tuft), mixf(0.5f, 1.0f, tuft), mixf(0.38f, 0.82f, tuft));
  t->r *= 0.8f + 0.3f * n, t->g *= 0.8f + 0.3f * n, t->b *= 0.8f + 0.3f * n;
  t->h = 0.4f + 0.3f * n + 0.2f * tuft;
  t->rough = 0.95f;
  t->ao = 0.8f + 0.2f * n;
}
/* leaves (a 2 m tile): clumps of light and dark */
static void g_leaf(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 24, 121, &d1, &d2, &id);
  float leaf = clamp01(1 - d1 * 1.6f), tone = (float)(id & 255) / 255.0f;
  float c = 0.7f + 0.35f * leaf * (0.6f + 0.4f * tone);
  rgb(t, c * 0.95f, c, c * 0.85f);
  t->h = leaf;
  t->rough = 0.75f;
  t->ao = 0.55f + 0.45f * leaf;
}
/* water (a 4 m tile): ripples */
static void g_glass(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 4, 131);
  rgb(t, 0.9f, 0.95f, 1.0f);
  t->h = 0.5f + 0.4f * n;
  t->rough = 0.06f;
  t->ao = 1;
}

typedef void (*gen_fn)(float u, float v, texel *t);
static const gen_fn GEN[MAT_KINDS] = { g_concrete, g_asphalt, g_wood, g_paint, g_metal, g_grass, g_brick, g_roof, g_tile, g_dirt, g_leaf, g_glass };
static const int SIZE[MAT_KINDS] = { 512, 512, 512, 256, 256, 256, 512, 256, 512, 256, 256, 256 };
static const float NORMAL_STRENGTH[MAT_KINDS] = { 3, 4, 3, 2, 1.5f, 5, 4, 5, 4, 5, 6, 2 };
const float MAT_TILE_M[MAT_KINDS] = { 4, 4, 2.4f, 2, 1, 3, 6, 3, 4.8f, 2, 2, 4 };

static tex_set SETS[MAT_KINDS];
uint32_t tex_hash = 2166136261u;

static t3_texture *finish(const uint8_t *px, int n, bool srgb) {
  t3_texture *t = t3_texture_new(n, n, px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  return t;
}
static uint8_t b8(float x) { return (uint8_t)(clamp01(x) * 255.0f + 0.5f); }
static uint32_t fnv(const uint8_t *p, size_t n, uint32_t h) {
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

static texel *WARM_TX;
static int warm_kind = -1, warm_row;
#define WARM_ROWS 64

bool tex_warm(int kind) {
  tex_set *s = &SETS[kind];
  if (s->map) return true;
  int N = SIZE[kind];
  if (warm_kind != kind) {
    if (!WARM_TX) WARM_TX = malloc(sizeof *WARM_TX * 512 * 512);
    warm_kind = kind, warm_row = 0;
  }
  texel *tx = WARM_TX;
  int y1 = warm_row + WARM_ROWS < N ? warm_row + WARM_ROWS : N;
  for (int y = warm_row; y < y1; y++)
    for (int x = 0; x < N; x++) {
      texel *t = &tx[y * N + x];
      memset(t, 0, sizeof *t);
      t->a = 1;
      GEN[kind](((float)x + 0.5f) / (float)N, ((float)y + 0.5f) / (float)N, t);
    }
  warm_row = y1;
  if (warm_row < N) return false;
  warm_kind = -1;
  uint8_t *a = malloc((size_t)N * N * 4), *nm = malloc((size_t)N * N * 4), *ro = malloc((size_t)N * N * 4), *ao = malloc((size_t)N * N * 4);
  float st = NORMAL_STRENGTH[kind] * (float)N / 256.0f;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      const texel *t = &tx[y * N + x];
      int i = (y * N + x) * 4;
      a[i] = b8(t->r), a[i + 1] = b8(t->g), a[i + 2] = b8(t->b), a[i + 3] = b8(t->a);
      float hx = tx[y * N + (x + 1) % N].h - tx[y * N + (x + N - 1) % N].h;
      float hy = tx[((y + 1) % N) * N + x].h - tx[((y + N - 1) % N) * N + x].h;
      float nx = -hx * st, ny = -hy * st, l = sqrtf(nx * nx + ny * ny + 1);
      nm[i] = b8(nx / l * 0.5f + 0.5f), nm[i + 1] = b8(ny / l * 0.5f + 0.5f), nm[i + 2] = b8(1 / l * 0.5f + 0.5f), nm[i + 3] = 255;
      ro[i] = ro[i + 1] = ro[i + 2] = b8(t->rough), ro[i + 3] = 255;
      ao[i] = ao[i + 1] = ao[i + 2] = b8(t->ao), ao[i + 3] = 255;
    }
  tex_hash = fnv(a, (size_t)N * N * 4, tex_hash);
  tex_hash = fnv(nm, (size_t)N * N * 4, tex_hash);
  s->map = finish(a, N, true);
  s->normal = finish(nm, N, false);
  s->rough = finish(ro, N, false);
  s->ao = finish(ao, N, false);
  free(a), free(nm), free(ro), free(ao);
  return true;
}

const tex_set *tex_get(int kind) {
  while (!tex_warm(kind)) {}
  return &SETS[kind];
}

void tex_apply(t3_material *m, int kind, float normal_scale, float ao_intensity) {
  const tex_set *s = tex_get(kind);
  t3_material_set_texture(m, T3_MAP, s->map);
  t3_material_set_texture(m, T3_NORMAL_MAP, s->normal);
  t3_material_set_texture(m, T3_ROUGHNESS_MAP, s->rough);
  t3_material_set_texture(m, T3_AO_MAP, s->ao);
  m->normal_scale = (t3_vec2){ normal_scale, normal_scale };
  m->ao_map_intensity = ao_intensity;
}

t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

/* ── the mesh builder ────────────────────────────────────────────────── */

int mb_vert(mesh_build *b, t3_vec3 p, t3_vec3 n, float u, float v) {
  if (b->nv == b->cap_v) {
    b->cap_v = b->cap_v ? b->cap_v * 2 : 1024;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)b->cap_v);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)b->cap_v);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->cap_v);
    b->col = realloc(b->col, sizeof(float) * 3 * (size_t)b->cap_v);
  }
  float *P = b->pos + b->nv * 3, *N = b->nor + b->nv * 3, *U = b->uv + b->nv * 2, *C = b->col + b->nv * 3;
  P[0] = p.x, P[1] = p.y, P[2] = p.z;
  N[0] = n.x, N[1] = n.y, N[2] = n.z;
  U[0] = u, U[1] = v;
  C[0] = C[1] = C[2] = 1;
  return b->nv++;
}
void mb_color(mesh_build *b, int v, float r, float g, float bl) {
  b->colors = true;
  float *C = b->col + v * 3;
  C[0] = r, C[1] = g, C[2] = bl;
}
void mb_tri(mesh_build *b, int a, int c, int d) {
  if (b->ni + 3 > b->cap_i) {
    b->cap_i = b->cap_i ? b->cap_i * 2 : 3072;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)b->cap_i);
  }
  b->idx[b->ni++] = (uint32_t)a, b->idx[b->ni++] = (uint32_t)c, b->idx[b->ni++] = (uint32_t)d;
}
void mb_quad(mesh_build *b, int a, int c, int d, int e) {
  mb_tri(b, a, c, d);
  mb_tri(b, a, d, e);
}
void mb_free(mesh_build *b) {
  free(b->pos), free(b->nor), free(b->uv), free(b->col), free(b->idx);
  memset(b, 0, sizeof *b);
}
t3_geometry *mb_finish(mesh_build *b) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->nor, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, a);
  t3_release(a);
  if (b->colors) {
    a = t3_attribute_new(T3_FLOAT32, b->col, b->nv, 3);
    t3_geometry_set_attribute(g, T3_ATTR_COLOR, a);
    t3_release(a);
  }
  a = t3_attribute_new(T3_UINT32, b->idx, b->ni, 1);
  t3_geometry_set_index(g, a);
  t3_release(a);
  mb_free(b);
  return g;
}
