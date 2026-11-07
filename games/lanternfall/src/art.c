/* lanternfall: the art made in code (art.h). Height fields per material become colour and a tangent-space normal map
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

/* ── each region's rock: a body that repeats every two tiles each way (so neighbours always join), in its own grain ── */
enum { RP = TILE_PX * 2 }; /* the body's period, pixels */
static float pnoise(float x, float y, int s, int n) { /* value noise with n cells across the period, wrapping both ways */
  float u = x * (float)n / RP, v = y * (float)n / RP;
  int x0 = (int)floorf(u), y0 = (int)floorf(v);
  float fx = u - (float)x0, fy = v - (float)y0;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  int ax = ((x0 % n) + n) % n, bx = (ax + 1) % n, ay = ((y0 % n) + n) % n, by = (ay + 1) % n;
  float a = hash2(ax, ay, s), b = hash2(bx, ay, s), c = hash2(ax, by, s), d = hash2(bx, by, s);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float pfbm(float x, float y, int s, int oct, int n) {
  float v = 0, a = 0.5f, t = 0;
  for (int i = 0; i < oct; i++) v += pnoise(x, y, s + i * 17, n << i) * a, t += a, a *= 0.5f;
  return v / t;
}
/* cells round scattered points, wrapping: f1, f2 the distances (pixels) to the nearest two */
static void pcells(float x, float y, int s, int n, float *f1, float *f2, float *id) {
  float c = (float)RP / (float)n, u = x / c, v = y / c;
  int cx = (int)floorf(u), cy = (int)floorf(v);
  *f1 = *f2 = 1e9f, *id = 0;
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
      int gx = cx + i, gy = cy + j, wx = ((gx % n) + n) % n, wy = ((gy % n) + n) % n;
      float px = ((float)gx + 0.15f + 0.7f * hash2(wx, wy, s)) * c, py = ((float)gy + 0.15f + 0.7f * hash2(wx, wy, s + 1)) * c;
      float d = sqrtf((x - px) * (x - px) + (y - py) * (y - py));
      if (d < *f1) *f2 = *f1, *f1 = d, *id = hash2(wx, wy, s + 2);
      else if (d < *f2) *f2 = d;
    }
}
static const float TAU = 6.2831853f;
/* the rock body at (x, y) of the two-tile period: height and lightness, by region */
static void rock(int r, float x, float y, float *h, float *l) {
  float n = pfbm(x, y, 300 + r * 11, 3, 8), fine = pfbm(x, y, 340 + r * 11, 2, 16);
  switch (r) {
  case R_GATE: { /* weathered sandstone: soft strata bent by the grain, pitted, a crack or two */
    float warp = pfbm(x, y, 401, 3, 4);
    float strata = 0.5f + 0.5f * sinf(TAU * (3 * y / RP) + warp * 7);
    float crack = sstep(0.05f, 0.0f, fabsf(pfbm(x, y, 403, 3, 6) - 0.5f));
    float pit = sstep(0.74f, 0.86f, pnoise(x, y, 404, 16));
    *h = 0.45f * strata + 0.4f * n + 0.15f * fine - 0.4f * crack - 0.25f * pit;
    *l = 0.66f + 0.14f * strata + 0.12f * (n - 0.5f) - 0.22f * crack - 0.12f * pit;
    break;
  }
  case R_ARCHIVE: { /* slate: thin sheets slanting down to the right, split along their seams */
    float warp = pfbm(x, y, 411, 2, 4);
    float s = TAU * (5 * y / RP + 2 * x / RP) + warp * 4;
    float sheet = 0.5f + 0.5f * sinf(s), seam = sstep(0.12f, 0.0f, fabsf(sinf(s * 0.5f)));
    *h = 0.5f * sheet + 0.3f * n + 0.2f * fine - 0.35f * seam;
    *l = 0.64f + 0.1f * sheet + 0.14f * (n - 0.5f) - 0.2f * seam;
    break;
  }
  case R_HEART: { /* old dark rock knotted with roots */
    float r1 = fabsf(pfbm(x, y, 421, 3, 6) - 0.5f), r2 = fabsf(pfbm(x, y, 422, 3, 4) - 0.5f);
    float root = fmaxf(sstep(0.06f, 0.0f, r1), sstep(0.05f, 0.0f, r2) * 0.8f);
    *h = 0.4f * n + 0.2f * fine + 0.5f * root;
    *l = 0.58f + 0.16f * (n - 0.5f) + 0.26f * root;
    break;
  }
  case R_ROOTWAY: case R_FEN: case R_BLOOM: { /* packed earth: roots, pebbles (the Fen mossy, the Gardens soft) */
    float r1 = fabsf(pfbm(x, y, 431 + r, 3, 6) - 0.5f);
    float root = sstep(0.045f, 0.0f, r1) * sstep(0.4f, 0.6f, pfbm(x, y, 432 + r, 2, 4));
    float peb = sstep(0.8f, 0.9f, pnoise(x, y, 433 + r, 16)) * (r == R_BLOOM ? 0.5f : 1);
    float moss = r == R_FEN ? sstep(0.55f, 0.7f, pfbm(x, y, 434, 3, 8)) : 0;
    *h = 0.45f + 0.3f * n + 0.35f * root + 0.3f * peb - 0.1f * moss;
    *l = 0.64f + 0.2f * (n - 0.5f) + 0.2f * root + 0.15f * peb + 0.12f * moss;
    break;
  }
  case R_GLASS: case R_STAR: case R_FROST: { /* faceted: flat planes between sharp creases (ice wider and smoother) */
    float f1, f2, plane;
    pcells(x, y, 441 + r, r == R_FROST ? 5 : r == R_STAR ? 7 : 6, &f1, &f2, &plane);
    float crease = sstep(3.5f, 0.0f, f2 - f1);
    *h = 0.3f + 0.4f * plane - 0.5f * crease + 0.04f * fine;
    *l = 0.66f + 0.14f * plane + 0.14f * (n - 0.5f) - 0.2f * crease;
    break;
  }
  case R_FORGE: { /* basalt: cooled columns split by dark seams, a little slag on the faces */
    float f1, f2, id;
    pcells(x, y, 451, 4, &f1, &f2, &id);
    float seam = sstep(3.0f, 0.3f, f2 - f1), dome = sstep(30, 0, f1);
    *h = 0.4f + 0.35f * dome + 0.15f * fine - 0.5f * seam;
    *l = 0.6f + 0.08f * dome + 0.14f * (id - 0.5f) + 0.14f * (n - 0.5f) - 0.16f * seam;
    break;
  }
  case R_CLOCK: { /* rough rock seamed with bright ore */
    float vein = sstep(0.04f, 0.0f, fabsf(pfbm(x, y, 461, 3, 6) - 0.5f));
    float fleck = sstep(0.86f, 0.93f, pnoise(x, y, 462, 32));
    *h = 0.45f + 0.35f * n + 0.15f * fine + 0.25f * vein + 0.2f * fleck;
    *l = 0.58f + 0.16f * (n - 0.5f) + 0.3f * vein + 0.3f * fleck;
    break;
  }
  case R_TIDAL: { /* porous coral and shells */
    float pore = sstep(0.7f, 0.85f, pnoise(x, y, 471, 16)), shell = sstep(0.82f, 0.9f, pnoise(x, y, 472, 8));
    *h = 0.5f + 0.4f * n - 0.4f * pore + 0.2f * shell;
    *l = 0.62f + 0.22f * (n - 0.5f) - 0.25f * pore + 0.18f * shell;
    break;
  }
  default: { /* cloudstone: soft rounded billows */
    float b = pfbm(x, y, 481, 4, 6);
    *h = sstep(0.3f, 0.7f, b);
    *l = 0.72f + 0.22f * (b - 0.5f);
    break;
  }
  }
}

/* the rock atlas of a region: the 16 edge variants (bit 1 top open, 2 right, 4 bottom, 8 left) in a 4 x 4 block, one
 * block for each of the four places a tile can sit in the two-tile period (x odd: right half, y odd: bottom half) */
static void make_rock(int r) {
  enum { W = TILE_PX * 8, H = TILE_PX * 8, PAD = TILE_PX + 2 };
  static uint8_t col[W * H * 4], nrm[W * H * 4];
  static float body_h[RP * RP], body_l[RP * RP], hf[PAD * PAD];
  static uint8_t cn[PAD * PAD * 4];
  for (int y = 0; y < RP; y++)
    for (int x = 0; x < RP; x++) rock(r, (float)x, (float)y, &body_h[y * RP + x], &body_l[y * RP + x]);
  for (int ph = 0; ph < 4; ph++)
    for (int v = 0; v < 16; v++) {
      int bx = (ph & 1) * TILE_PX, by = (ph >> 1) * TILE_PX;
      int ox = (ph & 1) * TILE_PX * 4 + (v % 4) * TILE_PX, oy = (ph >> 1) * TILE_PX * 4 + (v / 4) * TILE_PX;
      /* heights over the tile and a pixel round it (the body wraps), for normals that join their neighbours */
      for (int y = -1; y <= TILE_PX; y++)
        for (int x = -1; x <= TILE_PX; x++) {
          int sx = (bx + x + RP) % RP, sy = (by + y + RP) % RP;
          float h = body_h[sy * RP + sx], l = body_l[sy * RP + sx];
          int cx = x < 0 ? 0 : x >= TILE_PX ? TILE_PX - 1 : x, cy = y < 0 ? 0 : y >= TILE_PX ? TILE_PX - 1 : y;
          float d = 1e9f, edge_alpha = 1;
          if (v & 1) d = fminf(d, (float)cy);
          if (v & 2) d = fminf(d, (float)(TILE_PX - 1 - cx));
          if (v & 4) d = fminf(d, (float)(TILE_PX - 1 - cy));
          if (v & 8) d = fminf(d, (float)cx);
          { /* a corner open on both sides is rounded: rock masses read as boulders, not boxes */
            const float R = 30;
            float fx = (float)cx, fy = (float)cy, E = (float)(TILE_PX - 1);
            static const int CB[4][2] = { { 1, 8 }, { 1, 2 }, { 4, 2 }, { 4, 8 } };
            for (int c = 0; c < 4; c++) {
              if (!((v & CB[c][0]) && (v & CB[c][1]))) continue;
              float ccx = CB[c][1] == 8 ? R : E - R, ccy = CB[c][0] == 1 ? R : E - R;
              bool in = (CB[c][1] == 8 ? fx < ccx : fx > ccx) && (CB[c][0] == 1 ? fy < ccy : fy > ccy);
              if (in) d = fminf(d, R - sqrtf((fx - ccx) * (fx - ccx) + (fy - ccy) * (fy - ccy)));
            }
          }
          float bev = sstep(0, 9, d);
          if (d < 3) edge_alpha = sstep(0, 3, d + fbm((float)cx * 0.3f, (float)cy * 0.3f, 61 + v, 2, 0) * 2.5f); /* a ragged silhouette */
          h = h * (0.35f + 0.65f * bev);
          l = l * (0.55f + 0.45f * bev);
          hf[(y + 1) * PAD + x + 1] = h;
          if (x >= 0 && y >= 0 && x < TILE_PX && y < TILE_PX) {
            int i = (oy + y) * W + ox + x;
            col[4 * i + 0] = col[4 * i + 1] = col[4 * i + 2] = u8(l);
            col[4 * i + 3] = u8(edge_alpha);
          }
        }
      normals(hf, PAD, PAD, 2.5f, false, cn);
      for (int y = 0; y < TILE_PX; y++) memcpy(nrm + 4 * ((oy + y) * W + ox), cn + 4 * ((y + 1) * PAD + 1), 4 * TILE_PX);
    }
  A.rock[r] = cw_texture_from_pixels(W, H, col, NULL);
  cw_texture_set_normal_map(A.rock[r], nrm);
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
  A.wall[m] = cw_texture_from_pixels(S, S, col, NULL); /* (an atlas texture: it keeps its normal map) */
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
  for (int m = 0; m < NMAT; m++) make_lip(m), make_ledge(m), make_wall(m);
  for (int r = 0; r < NREGIONS; r++) make_rock(r);
  make_misc();
}
