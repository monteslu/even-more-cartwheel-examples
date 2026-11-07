/* explorer: the art made in code (art.h). Height fields per material become colour and a tangent-space normal map
 * (OpenGL convention), so the 2D lights shade every bevel, crack and grain. Render code: libm is fine here. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "world.h"

art A;

int region_material(int r) {
  switch (r) {
  case R_ROOTWAY: case R_FEN: case R_BLOOM: return MAT_EARTH;
  case R_GLASS: case R_FROST: case R_STAR: return MAT_CRYSTAL;
  case R_CLOCK: case R_FORGE: return MAT_BRASS;
  case R_TIDAL: return MAT_CORAL;
  case R_SKY: return MAT_CLOUD;
  default: return MAT_STONE;
  }
}

/* ── noise ── */
static float hash2(int x, int y, int s) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) & 0xffffff) / 16777215.0f;
}
static float vnoise(float x, float y, int s, int period) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = x - (float)x0, fy = y - (float)y0;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  int px = period > 0 ? period : 1 << 20;
  int ax = ((x0 % px) + px) % px, bx = ((x0 + 1) % px + px) % px;
  float a = hash2(ax, y0, s), b = hash2(bx, y0, s), c = hash2(ax, y0 + 1, s), d = hash2(bx, y0 + 1, s);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float x, float y, int s, int oct, int period) {
  float v = 0, a = 0.5f, f = 1;
  for (int i = 0; i < oct; i++) v += vnoise(x * f, y * f, s + i * 17, period > 0 ? period * (int)f : 0) * a, a *= 0.5f, f *= 2;
  return v;
}
static float sstep(float a, float b, float x) {
  float t = (x - a) / (b - a);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}
static uint8_t u8(float v) { return (uint8_t)(v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255 + 0.5f)); }

/* normals from a height field (wrap or clamp), strength k */
static void normals(const float *h, int w, int hh, float k, bool wrap, uint8_t *out) {
  for (int y = 0; y < hh; y++)
    for (int x = 0; x < w; x++) {
      int xl = x - 1, xr = x + 1, yu = y - 1, yd = y + 1;
      if (wrap) xl = (xl + w) % w, xr %= w, yu = (yu + hh) % hh, yd %= hh;
      else xl = xl < 0 ? 0 : xl, xr = xr >= w ? w - 1 : xr, yu = yu < 0 ? 0 : yu, yd = yd >= hh ? hh - 1 : yd;
      float dx = (h[y * w + xr] - h[y * w + xl]) * k, dy = (h[yd * w + x] - h[yu * w + x]) * k;
      float nx = -dx, ny = dy, nz = 1, l = sqrtf(nx * nx + ny * ny + nz * nz);
      uint8_t *o = out + 4 * (y * w + x);
      o[0] = u8(nx / l * 0.5f + 0.5f), o[1] = u8(ny / l * 0.5f + 0.5f), o[2] = u8(nz / l * 0.5f + 0.5f), o[3] = 255;
    }
}

/* the body of a material at pixel (x, y) of a tile-sized patch: height and lightness */
static void material(int m, float x, float y, float *h, float *l) {
  switch (m) {
  case MAT_STONE: { /* big worn blocks, one course a half tile: mortar lines, chipped faces */
    int row = (int)floorf(y / 32), bx = (int)floorf((x + (float)(row & 1) * 29) / 64);
    float fx = fmodf(x + (float)(row & 1) * 29, 64), fy = fmodf(y, 32);
    float e = fminf(fminf(fx, 63 - fx), fminf(fy, 31 - fy)) + 1.5f * fbm(x * 0.2f, y * 0.2f, 4, 2, 0);
    float chip = fbm(x * 0.15f, y * 0.15f, 3, 3, 0);
    *h = sstep(0, 3.5f, e) * (0.7f + 0.3f * chip);
    *l = 0.62f + 0.22f * hash2(bx, row, 9) + 0.12f * (chip - 0.5f) - (e < 1.2f ? 0.25f : 0);
    break;
  }
  case MAT_EARTH: { /* packed earth with roots and pebbles */
    float n = fbm(x * 0.06f, y * 0.06f, 5, 4, 0), r = fabsf(sinf(x * 0.09f + fbm(x * 0.03f, y * 0.05f, 8, 3, 0) * 6));
    float root = sstep(0.12f, 0.0f, r) * sstep(0.45f, 0.6f, fbm(x * 0.02f, y * 0.02f, 11, 2, 0));
    float peb = sstep(0.82f, 0.9f, vnoise(x * 0.22f, y * 0.22f, 13, 0));
    *h = 0.45f + 0.3f * n + 0.35f * root + 0.3f * peb;
    *l = 0.6f + 0.2f * n + 0.18f * root + 0.15f * peb;
    break;
  }
  case MAT_CRYSTAL: { /* faceted: flat planes between sharp creases */
    float c = vnoise(x * 0.07f, y * 0.07f, 21, 0), d = vnoise(x * 0.07f + 3.1f, y * 0.07f - 1.7f, 22, 0);
    float facet = floorf((c * 0.6f + d * 0.4f) * 6) / 6;
    *h = facet + 0.05f * fbm(x * 0.2f, y * 0.2f, 23, 2, 0);
    *l = 0.6f + 0.35f * facet;
    break;
  }
  case MAT_BRASS: { /* riveted plates, one a tile, dented and scratched */
    float fx = fmodf(x, 64), fy = fmodf(y, 64);
    float e = fminf(fminf(fx, 63 - fx), fminf(fy, 63 - fy));
    float rv = 0;
    for (int k = 0; k < 4; k++) {
      float cx = (k & 1) ? 57 : 6, cy = (k & 2) ? 57 : 6, d = sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy));
      rv = fmaxf(rv, sstep(2.6f, 0.5f, d));
    }
    float scratch = fbm(x * 0.4f, y * 0.04f, 31, 2, 0);
    *h = sstep(0, 2.5f, e) * 0.6f + rv * 0.5f;
    *l = 0.58f + 0.2f * scratch + 0.25f * rv - (e < 1 ? 0.3f : 0);
    break;
  }
  case MAT_CORAL: { /* porous coral and shells */
    float n = fbm(x * 0.08f, y * 0.08f, 41, 4, 0), p = vnoise(x * 0.3f, y * 0.3f, 42, 0);
    float pore = sstep(0.7f, 0.85f, p);
    *h = 0.5f + 0.4f * n - 0.4f * pore;
    *l = 0.62f + 0.25f * n - 0.25f * pore;
    break;
  }
  default: { /* cloudstone: soft rounded billows */
    float n = fbm(x * 0.05f, y * 0.05f, 51, 4, 0);
    *h = sstep(0.3f, 0.7f, n);
    *l = 0.72f + 0.22f * n;
    break;
  }
  }
}

static void make_tiles(int m) {
  enum { W = TILE_PX * 4, H = TILE_PX * 4 };
  static uint8_t col[W * H * 4], nrm[W * H * 4];
  static float hf[W * H];
  for (int v = 0; v < 16; v++) {
    int ox = (v % 4) * TILE_PX, oy = (v / 4) * TILE_PX;
    for (int y = 0; y < TILE_PX; y++)
      for (int x = 0; x < TILE_PX; x++) {
        /* world-continuous pattern: the same patch across neighbours */
        float h, l;
        material(m, (float)x + (float)v * 7.0f, (float)y, &h, &l);
        /* bevel toward open sides */
        float d = 1e9f;
        if (v & 1) d = fminf(d, (float)y);
        if (v & 2) d = fminf(d, (float)(TILE_PX - 1 - x));
        if (v & 4) d = fminf(d, (float)(TILE_PX - 1 - y));
        if (v & 8) d = fminf(d, (float)x);
        /* a corner open on both sides is rounded: rock masses read as boulders, not boxes */
        {
          const float R = 30;
          float fx = (float)x, fy = (float)y, E = (float)(TILE_PX - 1);
          static const int CB[4][2] = { { 1, 8 }, { 1, 2 }, { 4, 2 }, { 4, 8 } };
          for (int c = 0; c < 4; c++) {
            if (!((v & CB[c][0]) && (v & CB[c][1]))) continue;
            float cx = CB[c][1] == 8 ? R : E - R, cy = CB[c][0] == 1 ? R : E - R;
            bool in = (CB[c][1] == 8 ? fx < cx : fx > cx) && (CB[c][0] == 1 ? fy < cy : fy > cy);
            if (in) d = fminf(d, R - sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)));
          }
        }
        float bev = sstep(0, 9, d);
        float rough = fbm((float)x * 0.3f, (float)y * 0.3f, 61 + v, 2, 0);
        float edge_alpha = 1;
        if (d < 3) edge_alpha = sstep(0, 3, d + rough * 2.5f); /* a ragged silhouette */
        h = h * (0.35f + 0.65f * bev);
        l = l * (0.55f + 0.45f * bev);
        int i = (oy + y) * W + ox + x;
        hf[i] = h;
        col[4 * i + 0] = col[4 * i + 1] = col[4 * i + 2] = u8(l);
        col[4 * i + 3] = u8(edge_alpha);
      }
  }
  normals(hf, W, H, 2.5f, false, nrm);
  A.tiles[m] = cw_texture_from_pixels(W, H, col, NULL);
  cw_texture_set_normal_map(A.tiles[m], nrm);
}

/* the walkable top: grass, moss, frost, rust, weed or cloud trim, bright, hanging a little over the face */
static void make_lip(int m) {
  enum { W = TILE_PX * 4, H = 40 };
  static uint8_t col[W * H * 4], nrm[W * H * 4];
  static float hf[W * H];
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int v = x / TILE_PX;
      float fx = (float)x;
      float a = 0, h = 0, l = 1;
      float band = 10 + 6 * fbm(fx * 0.08f, (float)v, 70 + m, 3, TILE_PX * 4 / 8);
      switch (m) {
      case MAT_EARTH: case MAT_STONE: { /* grass blades over a mossy band */
        float blade = fabsf(sinf(fx * 0.9f + hash2(x, 0, 71) * 3)) * (6 + 10 * hash2(x / 2, v, 72));
        float top = 12 - blade;
        a = (float)y >= top && (float)y < 12 + band ? 1 : 0;
        if ((float)y >= 12 + band - 3 && (float)y < 12 + band) a = sstep(12 + band, 12 + band - 3, (float)y);
        h = (float)y < 12 ? 1 - (12 - (float)y) / 16 : 0.6f;
        l = 0.75f + 0.25f * ((float)y < 12 ? 1 : 0.5f) + 0.1f * hash2(x, y, 73);
        break;
      }
      case MAT_CRYSTAL: { /* frost rim with small spikes */
        float sp = fmaxf(0, 1 - fabsf(fmodf(fx, 11) - 5.5f) / 3) * (5 + 6 * hash2(x / 11, v, 74));
        a = (float)y >= 12 - sp && (float)y < 10 + band ? 1 : 0;
        h = 1 - (float)y / (float)H;
        l = 0.85f + 0.15f * hash2(x, y, 75);
        break;
      }
      case MAT_BRASS: { /* a polished rail with rivets */
        a = y >= 10 && y < 22 ? 1 : 0;
        h = y < 16 ? (float)(y - 10) / 6 : (float)(22 - y) / 6;
        l = 0.8f + 0.2f * h + (fmodf(fx, 21) < 3 && y > 13 && y < 19 ? 0.2f : 0);
        break;
      }
      case MAT_CORAL: { /* weed fronds */
        float f = fabsf(sinf(fx * 0.35f + hash2(x / 3, 0, 76) * 2)) * (8 + 8 * hash2(x / 5, v, 77));
        a = (float)y >= 14 - f && (float)y < 12 + band ? 1 : 0;
        h = 0.7f;
        l = 0.75f + 0.25f * hash2(x, y / 3, 78);
        break;
      }
      default: { /* cloud trim: puffs */
        float c = 0;
        for (int k = 0; k < 6; k++) {
          float cx = (float)(v * TILE_PX) + 6 + (float)k * 11, cy = 14, r = 7 + 3 * hash2(k, v, 79);
          c = fmaxf(c, 1 - sqrtf((fx - cx) * (fx - cx) + ((float)y - cy) * ((float)y - cy)) / r);
        }
        a = c > 0 ? 1 : (y >= 14 && (float)y < 12 + band ? 1 : 0);
        h = c;
        l = 0.92f + 0.08f * c;
        break;
      }
      }
      int i = y * W + x;
      hf[i] = h;
      col[4 * i] = col[4 * i + 1] = col[4 * i + 2] = u8(l);
      col[4 * i + 3] = u8(a);
    }
  normals(hf, W, H, 3, false, nrm);
  A.lip[m] = cw_texture_from_pixels(W, H, col, NULL);
  cw_texture_set_normal_map(A.lip[m], nrm);
}

static void make_ledge(int m) {
  enum { W = TILE_PX * 3, H = 28 };
  static uint8_t col[W * H * 4], nrm[W * H * 4];
  static float hf[W * H];
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int piece = x / TILE_PX, lx = x % TILE_PX;
      float e = (float)(y < H - 1 - y ? y : H - 1 - y);
      float ends = piece == 0 ? (float)lx : piece == 2 ? (float)(TILE_PX - 1 - lx) : 99;
      float a = sstep(0, 2, fminf(ends + 2, 99)) * (y < 22 ? 1 : sstep(28, 22, (float)y) * (fmodf((float)x, 16) < 5 ? 1 : 0.0f));
      float grain = fbm((float)x * 0.05f, (float)y * 0.5f, 81 + m, 3, 0);
      float h = sstep(0, 3, e) * (0.6f + 0.4f * grain);
      float l = 0.65f + 0.3f * grain - (y > 16 ? 0.15f : 0);
      if (m == MAT_BRASS || m == MAT_CRYSTAL) l = 0.7f + 0.25f * grain;
      int i = y * W + x;
      hf[i] = h;
      col[4 * i] = col[4 * i + 1] = col[4 * i + 2] = u8(l);
      col[4 * i + 3] = u8(a);
    }
  normals(hf, W, H, 3, false, nrm);
  A.ledge[m] = cw_texture_from_pixels(W, H, col, NULL);
  cw_texture_set_normal_map(A.ledge[m], nrm);
}

static void make_wall(int m) {
  enum { S = 128 };
  static uint8_t col[S * S * 4], nrm[S * S * 4];
  static float hf[S * S];
  /* rough cave rock, no courses or grid: tilted strata, a few cracks, lumps (each material its own grain) */
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++) {
      float fx = (float)x, fy = (float)y;
      float n = fbm(fx / 32, fy / 32, 91 + m, 3, 4), n2 = fbm(fx / 16, fy / 16, 97 + m, 2, 8);
      float strata = 0.5f + 0.5f * sinf(6.2832f * (fy * 4 + fx) / (float)S + n * 5.0f); /* (whole cycles across: it tiles) */
      float crack = sstep(0.06f, 0.0f, fabsf(fbm(fx / 25.6f, fy / 25.6f, 99 + m, 3, 5) - 0.5f));
      float lump = sstep(0.45f, 0.75f, n);
      float h = 0.35f * strata + 0.45f * lump + 0.2f * n2 - 0.35f * crack;
      if (m == MAT_CRYSTAL) h = floorf((n * 0.7f + n2 * 0.3f) * 5) / 5;
      int i = y * S + x;
      hf[i] = h;
      float v = 0.66f + 0.2f * (0.6f * h + 0.4f * n2) - 0.1f * crack;
      col[4 * i] = col[4 * i + 1] = col[4 * i + 2] = u8(v);
      col[4 * i + 3] = 255;
    }
  normals(hf, S, S, 1.5f, true, nrm);
  A.wall[m] = cw_texture_from_pixels(S, S, col, &(cw_texture_desc){ .repeat = true });
  cw_texture_set_normal_map(A.wall[m], nrm);
}

#include "art_par.h"

static void make_misc(void) {
  static uint8_t px[1024 * 512 * 4];
  /* soft glow: gaussian-ish, premultiplied white */
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++) {
      float dx = ((float)x - 31.5f) / 32, dy = ((float)y - 31.5f) / 32, d = dx * dx + dy * dy;
      float a = expf(-d * 4.5f) * (1 - sstep(0.8f, 1.0f, d));
      uint8_t *o = px + 4 * (y * 64 + x);
      o[0] = o[1] = o[2] = 255, o[3] = u8(a);
    }
  A.soft = cw_texture_from_pixels(64, 64, px, NULL);
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++) {
      float dx = ((float)x - 15.5f) / 16, dy = ((float)y - 15.5f) / 16, d = sqrtf(dx * dx + dy * dy);
      float a = fmaxf(sstep(1, 0.0f, d) * 0.5f, sstep(0.35f, 0.1f, d));
      uint8_t *o = px + 4 * (y * 32 + x);
      o[0] = o[1] = o[2] = 255, o[3] = u8(a);
    }
  A.spark = cw_texture_from_pixels(32, 32, px, NULL);
  /* a leaf / petal / shard */
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++) {
      float u = ((float)x - 15.5f) / 15, v = ((float)y - 15.5f) / 7;
      float a = sstep(1.0f, 0.85f, u * u + v * v);
      uint8_t *o = px + 4 * (y * 32 + x);
      float vein = fabsf(v) < 0.12f ? 0.75f : 1;
      o[0] = o[1] = o[2] = u8(vein), o[3] = u8(a);
    }
  A.leaf = cw_texture_from_pixels(32, 32, px, NULL);
  /* thorns: curved spikes in a row */
  for (int y = 0; y < 40; y++)
    for (int x = 0; x < 64; x++) {
      float a = 0, l = 0.8f;
      for (int k = 0; k < 4; k++) {
        float cx = 8 + (float)k * 16, hgt = 30 + 8 * hash2(k, 1, 95), lean = (hash2(k, 2, 96) - 0.5f) * 6;
        float t = (40 - (float)y) / hgt;
        if (t < 0 || t > 1) continue;
        float w = 7 * (1 - t), px2 = cx + lean * t * t;
        float d = fabsf((float)x - px2);
        if (d < w) a = 1, l = 0.55f + 0.45f * (1 - d / (w + 0.01f)) * (0.6f + 0.4f * t);
      }
      uint8_t *o = px + 4 * (y * 64 + x);
      o[0] = o[1] = o[2] = u8(l), o[3] = u8(a);
    }
  A.thorns = cw_texture_from_pixels(64, 40, px, NULL);
  /* a molten channel: dark slag crusting a bright flowing surface (tiles across) */
  for (int y = 0; y < 40; y++)
    for (int x = 0; x < 64; x++) {
      float fx = (float)x, fy = (float)y;
      float crust = sstep(0.55f, 0.63f, fbm(fx / 16, fy / 10, 131, 3, 4));
      float a = fy > 18 ? 1 : sstep(12, 18, fy + 3 * vnoise(fx / 8, 0, 132, 8));
      float l = 1.0f - 0.7f * crust - 0.2f * sstep(24, 40, fy);
      uint8_t *o = px + 4 * (y * 64 + x);
      o[0] = o[1] = o[2] = u8(l), o[3] = u8(a);
    }
  A.molten = cw_texture_from_pixels(64, 40, px, &(cw_texture_desc){ .repeat = true });
  make_parallax(px);
}

void art_init(void) {
  for (int m = 0; m < NMAT; m++) make_tiles(m), make_lip(m), make_ledge(m), make_wall(m);
  make_misc();
}
