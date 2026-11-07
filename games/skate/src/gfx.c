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

/* trowelled park concrete (a 4 m tile): soft mottling, fine pits, sawn joints every 2 m, hairline cracks, water stains,
 * wheel marks worn darker and smoother */
static void g_concrete(float u, float v, texel *t) {
  float n = fbm(u, v, 6, 5, 21), fine = fbm(u, v, 96, 2, 22), pit = h01((int)(u * 512), (int)(v * 512), 23) > 0.997f;
  float ju = fminf(fractf(u * 2), 1 - fractf(u * 2)), jv = fminf(fractf(v * 2), 1 - fractf(v * 2));
  float joint = 1 - clamp01((fminf(ju, jv) - 0.002f) * 500);
  float marks = clamp01((fbm(u * 3, v * 0.6f, 4, 3, 24) - 0.58f) * 4) * 0.7f;
  /* cracks: the edges of big cells, only where a mask lets them run, wobbling */
  float d1, d2;
  uint32_t id;
  float wu = u + 0.02f * (fbm(u, v, 8, 2, 25) - 0.5f), wv = v + 0.02f * (fbm(u, v, 8, 2, 26) - 0.5f);
  cells(wu, wv, 5, 27, &d1, &d2, &id);
  float crack = (1 - clamp01((d2 - d1) * 110)) * clamp01((fbm(u, v, 3, 2, 28) - 0.62f) * 6);
  float stain = clamp01((fbm(u, v, 4, 4, 29) - 0.6f) * 3);
  float c = 0.6f + 0.08f * (n - 0.5f) + 0.035f * (fine - 0.5f) - 0.1f * marks - 0.07f * stain;
  rgb(t, c, c * 0.985f, c * 0.96f);
  float dk = 0.12f * joint + 0.22f * crack;
  t->r -= dk, t->g -= dk, t->b -= dk;
  t->h = 0.5f + 0.12f * n + 0.05f * fine - 0.25f * pit - 0.12f * joint - 0.2f * crack;
  t->rough = 0.74f + 0.12f * fine - 0.22f * marks;
  t->ao = 1 - 0.12f * joint - 0.2f * pit - 0.3f * crack;
}
/* asphalt (a 4 m tile): dark aggregate, patched and sealed cracks, a little worn */
static void g_asphalt(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 160, 31, &d1, &d2, &id);
  float stone = clamp01((d2 - d1) * 4), tone = (float)(id & 255) / 255.0f;
  float patch = clamp01((fbm(u, v, 3, 3, 32) - 0.6f) * 6);
  float e1, e2;
  uint32_t eid;
  cells(u + 0.015f * fbm(u, v, 8, 2, 34), v, 4, 35, &e1, &e2, &eid);
  float seal = (1 - clamp01((e2 - e1) * 40)) * clamp01((fbm(u, v, 3, 2, 36) - 0.45f) * 4);
  float c = 0.6f + 0.22f * tone * stone + 0.1f * (fbm(u, v, 8, 3, 33) - 0.5f);
  c = mixf(c, 0.48f, patch);
  c = mixf(c, 0.22f, seal);
  rgb(t, c, c, c * 1.02f);
  t->h = 0.3f + 0.4f * stone * (1 - seal);
  t->rough = 0.88f - 0.1f * stone - 0.3f * seal;
  t->ao = 0.75f + 0.25f * stone;
}
/* ramp plywood (a 2.4 m tile, sheets 1.2 x 2.4): grain, seams, screw rows, wheel polish down the middle */
static void g_wood(float u, float v, texel *t) {
  float su = fractf(u * 2), sv = v;
  int sheet = (int)floorf(u * 2);
  float seam = 1 - clamp01((fminf(fminf(su, 1 - su) * 1.2f, fminf(sv, 1 - sv) * 2.4f) - 0.004f) * 300);
  float warp = fbm(u, v, 4, 3, 41 + (uint32_t)sheet) * 0.3f;
  float grain = cw_pow(1 - fabsf(fractf(v * 46 + warp) - 0.5f) * 2, 6) * 0.5f;
  float tone = h01(sheet & 1, 0, 42) * 0.6f + 0.4f * fbm(u, v, 2, 2, 45); /* each sheet its own shade, mottled */
  /* screws: a row every 30 cm along each sheet's edges */
  float sx = fminf(su, 1 - su) * 1.2f, sy = fractf(v * 8);
  float screw = (sx > 0.03f && sx < 0.05f && fabsf(sy - 0.5f) < 0.04f) ? 1 : 0;
  float polish = clamp01(1 - fabsf(su - 0.5f) * 3) * 0.6f;
  /* scuffs: wheel and shoe marks across the sheets, darker grey */
  float scuff = clamp01((fbm(u, v * 4, 6, 3, 44) - 0.6f) * 3.5f) * (0.5f + polish);
  float c = 0.78f + 0.14f * (tone - 0.5f) - 0.1f * grain + 0.03f * (fbm(u, v, 32, 2, 43) - 0.5f) - 0.16f * scuff;
  rgb(t, c, c * 0.97f - 0.03f * scuff, c * 0.93f - 0.05f * scuff);
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
  /* chips: the paint knocked off to dark primer in small flakes; scratches along the run */
  float chip = clamp01((fbm(u, v, 24, 3, 63) - 0.66f) * 8);
  float scratch = (h01((int)(u * 256), (int)(v * 8), 64) > 0.985f) ? 0.6f : 0;
  float c = 0.86f + 0.08f * (line - 0.5f) + 0.04f * (n - 0.5f);
  c = mixf(c, 0.3f, chip);
  c = mixf(c, 1.0f, scratch);
  rgb(t, c, c, c * 1.02f);
  t->h = 0.5f + 0.05f * line - 0.15f * chip;
  t->rough = 0.3f + 0.15f * line + 0.4f * chip;
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
  float id = h01(ix & 7, iy & 7, 101), id2 = h01(ix & 7, iy & 7, 103);
  float n = fbm(u, v, 16, 4, 102), stain = clamp01((fbm(u, v, 3, 3, 104) - 0.55f) * 3);
  /* slabs of a few stones (some warmer, some greyer), worn and stained */
  float c = 0.47f + 0.08f * (id - 0.5f) + 0.07f * (n - 0.5f) - 0.12f * stain;
  float warm = id2 > 0.75f ? 0.03f : id2 < 0.2f ? -0.025f : 0;
  rgb(t, mixf(c + warm, 0.5f, grout), mixf(c * 0.98f, 0.49f, grout), mixf(c * 0.95f - warm, 0.47f, grout));
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

/* ── the scenery's surfaces (scenery.c) ─────────────────────────────────── */

/* beach sand (a 4 m tile): wind ripples, darker damp grains, a few shells */
static void g_sand(float u, float v, texel *t) {
  float w = fbm(u, v, 4, 3, 141);
  float rip = 0.5f + 0.5f * cw_sin((v * 22 + w * 3.0f + 0.3f * fbm(u, v, 12, 2, 142)) * 2 * PI_F);
  float grain = h01((int)(u * 512), (int)(v * 512), 143), n = fbm(u, v, 8, 4, 144);
  float c = 0.86f + 0.06f * (n - 0.5f) + 0.05f * (rip - 0.5f) + 0.06f * (grain - 0.5f);
  rgb(t, c, c * 0.95f, c * 0.84f);
  if (grain > 0.996f) rgb(t, 0.98f, 0.95f, 0.9f);
  t->h = 0.5f + 0.18f * rip + 0.05f * grain;
  t->rough = 0.95f;
  t->ao = 0.85f + 0.15f * rip;
}
/* a facade (a 32 x 28 m tile: 8 windows across, 8 storeys): plaster, slab bands, recessed windows with frames, sills,
 * blinds half down in some, rain streaks under the sills. v runs up the wall. */
static void win_cell(float u, float v, int *ix, int *iy, float *wu, float *wv) {
  float x = u * 8, y = v * 8;
  *ix = (int)floorf(x), *iy = (int)floorf(y);
  *wu = fractf(x), *wv = fractf(y);
}
static void g_facade(float u, float v, texel *t) {
  int ix, iy;
  float wu, wv;
  win_cell(u, v, &ix, &iy, &wu, &wv);
  float id = h01(ix & 7, iy & 7, 151);
  bool glass = wu > 0.22f && wu < 0.78f && wv > 0.24f && wv < 0.82f;
  bool frame = wu > 0.19f && wu < 0.81f && wv > 0.21f && wv < 0.85f && !glass;
  bool sill = wu > 0.17f && wu < 0.83f && wv > 0.17f && wv <= 0.21f;
  bool slab = wv < 0.06f;
  float n = fbm(u, v, 16, 4, 152), streak = clamp01((fbm(u * 4, v, 24, 2, 153) - 0.45f) * 2) * clamp01(1 - (0.2f - wv) * 0) * (wu > 0.2f && wu < 0.8f && wv < 0.2f ? 1 : 0.25f);
  if (glass) {
    float blind = id > 0.55f ? 0.24f + 0.5f * h01(ix & 7, iy & 7, 154) : 0.0f;
    bool shade = blind > 0 && wv > 0.82f - blind;
    float refl = clamp01((wv - 0.24f) * 1.6f) * 0.35f + 0.15f * fbm(u * 2, v * 2, 6, 2, 155);
    bool bar = fabsf(wu - 0.5f) < 0.012f;
    if (shade) rgb(t, 0.86f, 0.83f, 0.76f), t->rough = 0.8f, t->h = 0.35f;
    else rgb(t, 0.18f + refl * 0.5f, 0.24f + refl * 0.55f, 0.32f + refl * 0.6f), t->rough = 0.06f, t->h = 0.3f;
    if (bar) rgb(t, 0.82f, 0.82f, 0.8f), t->rough = 0.5f;
    t->ao = 0.8f;
    return;
  }
  if (frame) { rgb(t, 0.92f, 0.92f, 0.9f); t->h = 0.62f, t->rough = 0.5f, t->ao = 0.95f; return; }
  float c = 0.9f + 0.05f * (n - 0.5f) - 0.12f * streak;
  if (slab) c *= 0.86f;
  if (sill) c = 0.97f;
  rgb(t, c, c * 0.98f, c * 0.95f);
  t->h = 0.5f + 0.08f * n + (sill ? 0.25f : 0) + (slab ? 0.12f : 0);
  t->rough = 0.85f;
  t->ao = 1 - 0.1f * streak - (wv > 0.85f && wv < 0.9f && wu > 0.2f && wu < 0.8f ? 0.3f : 0);
}
/* the same windows lit at dusk (an emissive map): some warm, some cool, most dark */
static void g_facade_lit(float u, float v, texel *t) {
  int ix, iy;
  float wu, wv;
  win_cell(u, v, &ix, &iy, &wu, &wv);
  float id = h01(ix & 7, iy & 7, 156), tone = h01(ix & 7, iy & 7, 157);
  bool glass = wu > 0.22f && wu < 0.78f && wv > 0.24f && wv < 0.82f && fabsf(wu - 0.5f) >= 0.012f;
  rgb(t, 0, 0, 0);
  if (glass && id > 0.42f) {
    float k = 0.55f + 0.45f * (1 - clamp01((wv - 0.24f) * 1.2f));
    if (tone < 0.65f) rgb(t, 1.0f * k, 0.78f * k, 0.48f * k);
    else if (tone < 0.85f) rgb(t, 0.75f * k, 0.85f * k, 1.0f * k);
    else rgb(t, 1.0f * k, 0.55f * k, 0.7f * k);
  }
  t->h = 0.5f, t->rough = 1, t->ao = 1;
}
/* a glass curtain wall (a 12 x 14 m tile: 8 panels across, 4 storeys): mullions, spandrels, the sky in the glass */
static void g_curtain(float u, float v, texel *t) {
  float x = u * 8, y = v * 4, pu = fractf(x), pv = fractf(y);
  bool mull = pu < 0.03f || pu > 0.97f, span = pv < 0.18f;
  float sky = 0.35f + 0.45f * clamp01(pv * 1.2f) + 0.2f * (fbm(u * 0.5f, v * 2, 4, 3, 161) - 0.5f);
  float pane = h01((int)floorf(x) & 7, (int)floorf(y) & 3, 162) * 0.08f;
  if (mull) { rgb(t, 0.35f, 0.37f, 0.4f); t->h = 0.7f, t->rough = 0.4f, t->ao = 1; return; }
  if (span) { rgb(t, 0.28f, 0.32f, 0.36f); t->h = 0.5f, t->rough = 0.35f, t->ao = 0.9f; return; }
  rgb(t, 0.32f * sky + pane, 0.5f * sky + pane, 0.62f * sky + pane);
  t->h = 0.45f, t->rough = 0.05f, t->ao = 1;
}
/* corrugated sheet metal (a 2 m tile): ridges up the wall, rust runs from the fixings */
static void g_corrugated(float u, float v, texel *t) {
  float rib = 0.5f + 0.5f * cw_sin(u * 2 * PI_F * 24);
  float rust = clamp01((fbm(u * 2, v, 8, 3, 171) - 0.62f) * 4) * clamp01(1.2f - fractf(v * 2) * 1.5f);
  float n = fbm(u, v, 16, 3, 172);
  float c = 0.84f + 0.08f * (rib - 0.5f) + 0.04f * (n - 0.5f);
  rgb(t, mixf(c, 0.55f, rust), mixf(c, 0.36f, rust), mixf(c, 0.24f, rust));
  t->h = 0.3f + 0.5f * rib;
  t->rough = 0.45f + 0.4f * rust + 0.1f * n;
  t->ao = 0.8f + 0.2f * rib;
}
/* a stack of sawn boards seen from the side (a 2.4 m tile): 16 boards high, gaps, grain, spacer sticks */
static void g_lumber(float u, float v, texel *t) {
  float y = v * 16, bv = fractf(y);
  int row = (int)floorf(y);
  float gap = bv < 0.07f ? 1 : 0;
  float stick = fabsf(fractf(u * 2) - 0.5f) < 0.02f ? 1 : 0;
  float tone = h01(row & 15, (int)floorf(u * 2 + h01(row & 15, 0, 181)) & 3, 182);
  float grain = cw_pow(1 - fabsf(fractf(u * 30 + fbm(u, v, 4, 2, 183) * 2) - 0.5f) * 2, 5) * 0.4f;
  float c = 0.84f + 0.1f * (tone - 0.5f) - 0.08f * grain;
  rgb(t, c, c * 0.84f, c * 0.62f);
  if (gap) rgb(t, 0.12f, 0.09f, 0.06f);
  if (stick && !gap) rgb(t, 0.5f, 0.4f, 0.3f);
  t->h = gap ? 0.1f : 0.6f - 0.06f * grain;
  t->rough = 0.85f;
  t->ao = gap ? 0.3f : 1;
}
/* open ground (an 8 m tile, tinted per vertex): tufts, bare patches, small stones */
static void g_ground(float u, float v, texel *t) {
  float n1 = fbm(u, v, 4, 4, 191), n2 = fbm(u, v, 32, 3, 192), blade = h01((int)(u * 512), (int)(v * 512), 193);
  float bare = clamp01((fbm(u, v, 6, 3, 194) - 0.58f) * 4);
  float k = 0.8f + 0.18f * (n1 - 0.5f) + 0.14f * (blade - 0.5f) * (1 - bare);
  rgb(t, mixf(k * 0.92f, k * 1.05f, bare), mixf(k, k * 0.9f, bare), mixf(k * 0.8f, k * 0.75f, bare));
  t->h = 0.4f + 0.35f * blade * n2 * (1 - bare);
  t->rough = 0.95f;
  t->ao = 0.75f + 0.25f * blade;
}
/* open water (an 8 m tile): long swells and chop (whole periods, so it tiles) */
static void g_water(float u, float v, texel *t) {
  float h = 0.35f * cw_sin((u * 3 + v * 2) * 2 * PI_F) + 0.25f * cw_sin((u * -2 + v * 5) * 2 * PI_F) + 0.4f * fbm(u, v, 16, 3, 201);
  rgb(t, 0.9f, 0.95f, 1.0f);
  t->h = 0.5f + 0.25f * h;
  t->rough = 0.04f;
  t->ao = 1;
}
/* a far skyline (a band, tiled across): towers of many heights with window lights, clear above them (alpha) */
static void g_skyline(float u, float v, texel *t) {
  int col = (int)floorf(u * 48);
  float hgt = 0.18f + 0.55f * cw_pow(h01(col, 0, 211), 1.6f) + (h01(col / 3, 1, 212) > 0.85f ? 0.25f : 0);
  float fu = fractf(u * 48);
  bool gap = fu < 0.05f && h01(col, 2, 213) > 0.6f;
  if (v > hgt || gap) { rgb(t, 0, 0, 0); t->a = 0; t->h = 0.5f, t->rough = 1, t->ao = 1; return; }
  float win = (fractf(u * 48 * 5) > 0.45f && fractf(v * 90) > 0.4f) ? 1 : 0;
  float c = 0.7f + 0.12f * (h01(col, 3, 214) - 0.5f) + 0.08f * win;
  rgb(t, c * 0.95f, c, c * 1.05f);
  t->a = 1, t->h = 0.5f, t->rough = 0.8f, t->ao = 1;
}
/* decals (a 4 x 4 atlas of 128 texel cells): our own tags, stickers and stencils, wax and skid marks; alpha cut out */
static float sdbox(float x, float y, float hx, float hy, float r) {
  float dx = fabsf(x) - hx + r, dy = fabsf(y) - hy + r;
  float ox = dx > 0 ? dx : 0, oy = dy > 0 ? dy : 0;
  return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(dx, dy), 0) - r;
}
static void g_decals(float u, float v, texel *t) {
  int cx = (int)floorf(u * 4), cy = (int)floorf(v * 4), cell = cy * 4 + cx;
  float x = fractf(u * 4) * 2 - 1, y = fractf(v * 4) * 2 - 1; /* -1..1 in the cell */
  uint32_t s = 300u + (uint32_t)cell * 17u;
  static const float PAL[8][3] = { { 1.0f, 0.3f, 0.45f }, { 0.2f, 0.75f, 1.0f }, { 1.0f, 0.8f, 0.1f }, { 0.4f, 0.95f, 0.4f },
                                   { 0.7f, 0.35f, 1.0f }, { 1.0f, 0.5f, 0.1f }, { 0.1f, 0.9f, 0.85f }, { 1.0f, 1.0f, 1.0f } };
  const float *a = PAL[cell % 8], *b = PAL[(cell * 3 + 2) % 8];
  t->a = 0, t->h = 0.5f, t->rough = 0.6f, t->ao = 1;
  rgb(t, 0, 0, 0);
  int kind = cell < 6 ? 0 : cell < 10 ? 1 : cell < 12 ? 2 : cell < 14 ? 3 : 4;
  if (kind == 0) { /* a tag: fat round letter shapes (a chain of blobs), a dark outline, a highlight */
    float d = 9;
    for (int i = 0; i < 5; i++) {
      float bx = -0.72f + 0.36f * (float)i, by = 0.25f * (h01(i, cell, s) - 0.5f), r = 0.2f + 0.12f * h01(i, cell, s + 1);
      float dd = sqrtf((x - bx) * (x - bx) / 1.0f + (y - by) * (y - by) / 1.6f) - r;
      if ((int)(h01(i, cell, s + 2) * 3) == 0) dd = sdbox(x - bx, y - by, r * 0.8f, r * 1.2f, 0.06f);
      d = fminf(d, dd);
    }
    float drip = (fabsf(fractf(x * 3.3f) - 0.5f) < 0.06f && y < 0 && y > -0.2f - 0.5f * h01((int)floorf(x * 3.3f) + 9, cell, s + 3)) ? 1 : 0;
    if (d < 0.07f || drip) {
      float fill = d < 0 ? 1 : 0;
      t->a = 1;
      float hl = (fill && y > 0.1f && fractf((x + y) * 3) < 0.15f) ? 1 : 0;
      if (fill) rgb(t, mixf(a[0], b[0], (y + 1) * 0.5f), mixf(a[1], b[1], (y + 1) * 0.5f), mixf(a[2], b[2], (y + 1) * 0.5f));
      else rgb(t, 0.08f, 0.06f, 0.1f);
      if (hl) rgb(t, 1, 1, 1);
      if (drip && !fill && d >= 0.07f) rgb(t, a[0] * 0.8f, a[1] * 0.8f, a[2] * 0.8f);
      t->rough = 0.55f;
    }
  } else if (kind == 1) { /* a sticker: a rounded rectangle with a white border, stripes or a dot */
    float hx = 0.8f, hy = 0.45f + 0.3f * h01(cell, 1, s);
    float d = sdbox(x, y, hx, hy, 0.15f);
    if (d < 0) {
      t->a = 1, t->rough = 0.35f, t->h = 0.6f;
      if (d > -0.08f) rgb(t, 0.97f, 0.97f, 0.95f);
      else if (cell & 1) rgb(t, (fractf(x * 2.5f) < 0.5f) ? a[0] : b[0], (fractf(x * 2.5f) < 0.5f) ? a[1] : b[1], (fractf(x * 2.5f) < 0.5f) ? a[2] : b[2]);
      else { float r = sqrtf(x * x + y * y); bool dot = r < 0.32f; rgb(t, dot ? b[0] : a[0], dot ? b[1] : a[1], dot ? b[2] : a[2]); }
    }
  } else if (kind == 2) { /* a round sticker: a ring and a star */
    float r = sqrtf(x * x + y * y), ang = cw_atan2(y, x);
    float star = 0.45f + 0.18f * cw_cos(ang * 5);
    if (r < 0.92f) {
      t->a = 1, t->rough = 0.35f, t->h = 0.6f;
      if (r > 0.8f) rgb(t, 0.08f, 0.08f, 0.1f);
      else if (r < star) rgb(t, b[0], b[1], b[2]);
      else rgb(t, a[0], a[1], a[2]);
    }
  } else if (kind == 3) { /* wax: a dark glossy smear along the cell (laid along an edge) */
    float w = 0.35f + 0.2f * (fbm(u * 2, v, 8, 2, s) - 0.5f);
    if (fabsf(y) < w) {
      float k = clamp01((w - fabsf(y)) * 6) * (0.5f + 0.5f * fbm(u * 3, v * 3, 16, 2, s + 1));
      t->a = k * 0.85f;
      rgb(t, 0.1f, 0.09f, 0.08f);
      t->rough = 0.15f, t->h = 0.52f;
    }
  } else { /* a skid: two dark rubber streaks */
    float k = 0;
    for (int i = -1; i <= 1; i += 2) k = fmaxf(k, clamp01(1 - fabsf(y - 0.35f * (float)i + 0.05f * x * x) * 9) * clamp01(1 - fabsf(x) * 0.9f));
    k *= 0.5f + 0.5f * fbm(u * 4, v, 16, 2, s + 2);
    if (k > 0.02f) { t->a = k * 0.75f; rgb(t, 0.06f, 0.06f, 0.06f); t->rough = 0.6f; }
  }
}

typedef void (*gen_fn)(float u, float v, texel *t);
static const gen_fn GEN[TEX_KINDS] = { g_concrete, g_asphalt, g_wood, g_paint, g_metal, g_grass, g_brick, g_roof, g_tile, g_dirt, g_leaf, g_glass,
                                        g_sand, g_facade, g_facade_lit, g_curtain, g_corrugated, g_lumber, g_ground, g_water, g_skyline, g_decals };
static const int SIZE[TEX_KINDS] = { 512, 512, 512, 256, 256, 256, 512, 256, 512, 256, 256, 256, 256, 512, 512, 256, 256, 256, 256, 256, 512, 512 };
static const float NORMAL_STRENGTH[TEX_KINDS] = { 3, 4, 3, 2, 1.5f, 5, 4, 5, 4, 5, 6, 2, 3, 3, 0, 2, 4, 3, 5, 3, 1, 1 };
const float MAT_TILE_M[TEX_KINDS] = { 4, 4, 2.4f, 2, 1, 3, 6, 3, 4.8f, 2, 2, 4, 4, 32, 32, 12, 2, 2.4f, 8, 8, 1, 1 };

static tex_set SETS[TEX_KINDS];
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
