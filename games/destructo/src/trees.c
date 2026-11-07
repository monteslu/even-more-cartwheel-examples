/* destructo's trees: render only. Layered foliage, not lollipops: every tree is a wood mesh (a tapered trunk and its
 * limbs) and a foliage mesh of alpha-cut cards of a texture made here (broad leaves, needle sprays, palm fronds),
 * the cards' normals pointed out from the middle of the clump they belong to, so a crown shades like a volume and
 * lets the sky through at its edges. Five species, each setting picks its own: oaks, birches, spruces, snowy firs and
 * date palms. Each species is two instanced meshes; the wind sways them by shearing each tree's matrix (the crown
 * moves, the foot stays), a little out of step from tree to tree. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "trees.h"

#define PI_F 3.14159265f

/* ── a small mesh builder ──────────────────────────────────────────── */
typedef struct mb { float *p, *n, *uv, *c; unsigned *ix; int nv, ni, cv, ci; } mb;
static int mb_v(mb *b, t3_vec3 p, t3_vec3 n, float u, float v, t3_color c) {
  if (b->nv == b->cv) {
    b->cv = b->cv ? b->cv * 2 : 256;
    b->p = realloc(b->p, sizeof(float) * 3 * b->cv), b->n = realloc(b->n, sizeof(float) * 3 * b->cv);
    b->uv = realloc(b->uv, sizeof(float) * 2 * b->cv), b->c = realloc(b->c, sizeof(float) * 3 * b->cv);
  }
  int i = b->nv++;
  b->p[i * 3] = p.x, b->p[i * 3 + 1] = p.y, b->p[i * 3 + 2] = p.z;
  b->n[i * 3] = n.x, b->n[i * 3 + 1] = n.y, b->n[i * 3 + 2] = n.z;
  b->uv[i * 2] = u, b->uv[i * 2 + 1] = v;
  b->c[i * 3] = c.r, b->c[i * 3 + 1] = c.g, b->c[i * 3 + 2] = c.b;
  return i;
}
static void mb_tri(mb *b, int a, int c, int d) {
  if (b->ni + 3 > b->ci) b->ci = b->ci ? b->ci * 2 : 768, b->ix = realloc(b->ix, sizeof(unsigned) * b->ci);
  b->ix[b->ni++] = (unsigned)a, b->ix[b->ni++] = (unsigned)c, b->ix[b->ni++] = (unsigned)d;
}
static void mb_quad(mb *b, int a, int c, int d, int e) { mb_tri(b, a, c, d), mb_tri(b, a, d, e); }
static t3_geometry *mb_finish(mb *b) {
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, b->p, b->nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, b->n, b->nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2));
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, b->c, b->nv, 3));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, b->ix, b->ni, 1));
  t3_geometry_compute_bounding_sphere(g);
  t3_geometry_compute_bounding_box(g);
  free(b->p), free(b->n), free(b->uv), free(b->c), free(b->ix);
  memset(b, 0, sizeof *b);
  return g;
}

static t3_vec3 V3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 add(t3_vec3 a, t3_vec3 b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 sub(t3_vec3 a, t3_vec3 b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
static t3_vec3 mul(t3_vec3 a, float s) { return V3(a.x * s, a.y * s, a.z * s); }
static float len(t3_vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
static t3_vec3 norm(t3_vec3 a) { float l = len(a); return l > 1e-6f ? mul(a, 1 / l) : V3(0, 1, 0); }
static t3_vec3 cross(t3_vec3 a, t3_vec3 b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static t3_color C(float r, float g, float b) { return (t3_color){ r, g, b }; }
static t3_color cmul(t3_color c, float k) { return (t3_color){ c.r * k, c.g * k, c.b * k }; }
static t3_color clerp(t3_color a, t3_color b, float t) { return (t3_color){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }

static uint32_t rs;
static float rf(void) { rs = rs * 1664525u + 1013904223u; return (float)(rs >> 8) / 16777216.0f; }
static float rr(float a, float b) { return a + (b - a) * rf(); }

/* a tapered tube from a to c, radius r0 to r1 */
static void tube(mb *b, t3_vec3 a, t3_vec3 c, float r0, float r1, int sides, t3_color c0, t3_color c1) {
  t3_vec3 ax = norm(sub(c, a)), up = fabsf(ax.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0);
  t3_vec3 e1 = norm(cross(ax, up)), e2 = cross(ax, e1);
  for (int i = 0; i < sides; i++) {
    float a0 = 2 * PI_F * (float)i / (float)sides, a1 = 2 * PI_F * (float)(i + 1) / (float)sides;
    t3_vec3 n0 = add(mul(e1, cosf(a0)), mul(e2, sinf(a0))), n1 = add(mul(e1, cosf(a1)), mul(e2, sinf(a1)));
    float u0 = (float)i / (float)sides, u1 = (float)(i + 1) / (float)sides;
    int v0 = mb_v(b, add(a, mul(n0, r0)), n0, u0, 0, c0), v1 = mb_v(b, add(a, mul(n1, r0)), n1, u1, 0, c0);
    int v2 = mb_v(b, add(c, mul(n1, r1)), n1, u1, 1, c1), v3 = mb_v(b, add(c, mul(n0, r1)), n0, u0, 1, c1);
    mb_quad(b, v0, v1, v2, v3);
  }
}

/* a card: centre, half axes (ax along u, ay along v), its normal; the colour at the bottom (v 0) and the top */
static void card(mb *b, t3_vec3 c, t3_vec3 ax, t3_vec3 ay, t3_vec3 n, t3_color lo, t3_color hi) {
  int v0 = mb_v(b, sub(sub(c, ax), ay), n, 0.01f, 0.01f, lo), v1 = mb_v(b, sub(add(c, ax), ay), n, 0.99f, 0.01f, lo);
  int v2 = mb_v(b, add(add(c, ax), ay), n, 0.99f, 0.99f, hi), v3 = mb_v(b, add(sub(c, ax), ay), n, 0.01f, 0.99f, hi);
  mb_quad(b, v0, v1, v2, v3);
}

/* a clump of leaf cards round centre cc, radii rx (across) and ry (up), card half size s: each card faces out from
 * the clump's middle (its normal too), with a second crossed through it; the underside darker */
static void clump(mb *b, t3_vec3 cc, float rx, float ry, int n, float s, t3_color leaf) {
  for (int i = 0; i < n; i++) {
    float az = rf() * 2 * PI_F, el = -0.55f + 1.5f * rf();
    t3_vec3 dir = V3(cosf(az) * cosf(el), sinf(el), sinf(az) * cosf(el));
    t3_vec3 c = add(cc, V3(dir.x * rx * rr(0.55f, 1.0f), dir.y * ry * rr(0.55f, 1.0f), dir.z * rx * rr(0.55f, 1.0f)));
    t3_vec3 nn = norm(add(dir, V3(0, 0.35f, 0)));
    t3_vec3 t1 = cross(nn, V3(0, 1, 0));
    t1 = len(t1) < 0.2f ? V3(1, 0, 0) : norm(t1);
    t3_vec3 t2 = norm(cross(t1, nn));
    float k = s * rr(0.8f, 1.2f), shade = 0.5f + 0.5f * (dir.y * 0.5f + 0.5f);
    t3_color lo = cmul(leaf, shade * 0.72f), hi = cmul(leaf, shade);
    card(b, c, mul(t1, k), mul(V3(0, 1, 0), k * 0.9f), nn, lo, hi);
    card(b, c, mul(t2, k), mul(V3(0, 1, 0), k * 0.9f), nn, lo, hi);
  }
}

/* ── the species ───────────────────────────────────────────────────── */
static void oak(mb *w, mb *f) {
  t3_color bark = C(0.06f, 0.045f, 0.035f), leaf = C(0.95f, 0.95f, 0.95f); /* the leaves' hue is the instance's */
  tube(w, V3(0, -0.2f, 0), V3(0.1f, 2.6f, 0), 0.32f, 0.22f, 7, bark, bark);
  t3_vec3 limbs[4];
  for (int i = 0; i < 4; i++) {
    float a = (float)i * 1.65f + 0.3f;
    limbs[i] = V3(cosf(a) * 1.5f, 3.6f + 0.5f * (i & 1), sinf(a) * 1.5f);
    tube(w, V3(0.1f, 2.4f, 0), limbs[i], 0.16f, 0.07f, 5, bark, bark);
  }
  /* the crown: a clump on each limb and one over the middle, their cards in the clumps' own directions */
  for (int i = 0; i < 4; i++) clump(f, add(limbs[i], V3(0, 0.5f, 0)), 1.4f, 1.15f, 13, 1.05f, leaf);
  clump(f, V3(0, 5.0f, 0), 1.7f, 1.3f, 16, 1.1f, leaf);
  clump(f, V3(0.2f, 3.4f, 0.1f), 2.0f, 0.7f, 8, 1.0f, leaf); /* the lower skirt that hides the limbs */
}
static void birch(mb *w, mb *f) {
  t3_color bark = C(0.5f, 0.48f, 0.44f), scar = C(0.05f, 0.045f, 0.04f), leaf = C(0.95f, 0.95f, 0.95f);
  /* a slim pale trunk in bands, dark scars between some, leaning a little */
  float y = -0.2f;
  for (int i = 0; i < 6; i++) {
    float y1 = y + 1.25f;
    t3_color c0 = (i == 1 || i == 3) ? clerp(bark, scar, 0.6f) : bark;
    tube(w, V3(0.04f * i, y, 0), V3(0.04f * (i + 1), y1, 0), 0.16f - 0.02f * i, 0.14f - 0.02f * i, 6, c0, bark);
    y = y1;
  }
  /* small clumps up the top two thirds of the trunk: a narrow, airy crown */
  for (int i = 0; i < 9; i++) {
    float t = (float)i / 8.0f, a = (float)i * 2.4f, r = 1.0f * (1 - 0.6f * t);
    clump(f, V3(cosf(a) * r + 0.2f, 2.8f + 4.4f * t, sinf(a) * r), 1.0f, 0.85f, 11, 0.8f, leaf);
  }
}
/* a spruce: tiers of needle-spray cards out from the trunk, shorter up the tree, each tier turned from the last.
 * snow: the upper side of each spray white */
static void spruce(mb *w, mb *f, bool snow) {
  t3_color bark = C(0.06f, 0.04f, 0.03f), nd = C(0.05f, 0.13f, 0.07f), white = C(0.8f, 0.84f, 0.92f);
  tube(w, V3(0, -0.2f, 0), V3(0, 10.0f, 0), 0.26f, 0.05f, 6, bark, bark);
  for (int tier = 0; tier < 9; tier++) {
    float y = 1.0f + 1.0f * (float)tier, l = 3.0f - 0.3f * (float)tier;
    int nb = 8 - tier / 3;
    for (int k = 0; k < nb; k++) {
      float a = 2 * PI_F * (float)k / (float)nb + (float)tier * 0.7f + rr(-0.2f, 0.2f);
      t3_vec3 out = V3(cosf(a), -0.32f, sinf(a)), side = V3(-sinf(a), 0, cosf(a));
      t3_vec3 c = add(V3(0, y, 0), mul(out, l * 0.5f));
      t3_vec3 n = norm(V3(cosf(a) * 0.55f, 0.85f, sinf(a) * 0.55f));
      t3_vec3 ax = mul(norm(out), l * 0.5f), ay = mul(norm(add(mul(side, 0.5f), V3(0, 1, 0))), l * 0.4f);
      float sh = 0.62f + 0.05f * (float)tier;
      t3_color lo = cmul(nd, sh * 0.8f), hi = cmul(nd, sh * 1.15f);
      if (snow) hi = clerp(hi, white, 0.42f), lo = clerp(lo, white, 0.03f);
      card(f, c, ax, ay, n, lo, hi);
      /* and on every other branch one lying across it, the spray's breadth seen from above and below */
      if (k & 1) card(f, add(c, V3(0, 0.1f, 0)), ax, mul(norm(add(side, V3(0, -0.25f, 0))), l * 0.38f), n, lo, hi);
    }
  }
  t3_color tip = snow ? clerp(nd, white, 0.5f) : nd;
  /* the leader: sprays pointing up (u along the shoot) */
  card(f, V3(0, 10.2f, 0), V3(0, 0.6f, 0), V3(0.4f, 0, 0), V3(1, 0.3f, 0), nd, tip);
  card(f, V3(0, 10.2f, 0), V3(0, 0.6f, 0), V3(0, 0, 0.4f), V3(0, 0.3f, 1), nd, tip);
}
/* a date palm: a ringed trunk curving up, a crown of fronds that arch out and droop, each frond two segments of a
 * V of leaflets (the texture's two halves either side of the rib) */
static void palm(mb *w, mb *f) {
  t3_color bark = C(0.3f, 0.22f, 0.14f), ring = C(0.18f, 0.12f, 0.08f), leaf = C(0.95f, 0.95f, 0.95f);
  t3_vec3 prev = V3(0, -0.2f, 0);
  for (int i = 1; i <= 9; i++) {
    float t = (float)i / 9.0f;
    t3_vec3 p = V3(0.9f * t * t, 7.0f * t, 0.15f * t);
    tube(w, prev, p, 0.3f - 0.1f * t + 0.03f, 0.3f - 0.1f * t, 7, ring, bark);
    prev = p;
  }
  t3_vec3 top = prev;
  for (int k = 0; k < 11; k++) {
    float a = 2 * PI_F * (float)k / 11.0f + rr(-0.15f, 0.15f), rise = rr(0.25f, 0.65f);
    t3_vec3 out = V3(cosf(a), 0, sinf(a)), side = V3(-sinf(a), 0, cosf(a));
    t3_vec3 p0 = top;
    for (int s = 0; s < 3; s++) {
      float pitch = rise - 0.55f * (float)s, l = 1.25f - 0.12f * (float)s;
      t3_vec3 dir = norm(add(mul(out, cosf(pitch)), V3(0, sinf(pitch), 0)));
      t3_vec3 p1 = add(p0, mul(dir, l));
      float wd = 0.75f * (s == 2 ? 0.6f : 1.0f);
      t3_vec3 droop = V3(0, -0.35f * wd, 0);
      t3_vec3 up = norm(cross(side, dir));
      if (up.y < 0) up = mul(up, -1);
      float v0 = (float)s / 3.0f, v1 = (float)(s + 1) / 3.0f; /* u across the frond, v along it */
      t3_color lo = cmul(leaf, 0.65f), hi = cmul(leaf, 0.95f);
      for (int hside = -1; hside <= 1; hside += 2) {
        t3_vec3 e0 = add(add(p0, mul(side, wd * (float)hside)), droop), e1 = add(add(p1, mul(side, wd * (float)hside)), droop);
        float ue = hside < 0 ? 0.01f : 0.99f;
        int a0 = mb_v(f, p0, up, 0.5f, v0, hi), a1 = mb_v(f, p1, up, 0.5f, v1, hi);
        int b1 = mb_v(f, e1, up, ue, v1, lo), b0 = mb_v(f, e0, up, ue, v0, lo);
        mb_quad(f, a0, a1, b1, b0);
      }
      p0 = p1;
    }
  }
}

/* ── the textures ──────────────────────────────────────────────────── */
enum { TX_LEAF, TX_NEEDLE, TX_FROND, TX_COUNT };
static t3_texture *TX[TX_COUNT];
static uint8_t u8c(float v) { return (uint8_t)(cw_clampf(v, 0, 1) * 255 + 0.5f); }

/* a stroke from (x0, y0) to (x1, y1), width wd (pixels), luminance k, into an N x N RGBA image */
static void stroke(uint8_t *px, int N, float x0, float y0, float x1, float y1, float wd, float k) {
  int xa = (int)fmaxf(0, fminf(x0, x1) - wd - 1), xb = (int)fminf(N - 1, fmaxf(x0, x1) + wd + 1);
  int ya = (int)fmaxf(0, fminf(y0, y1) - wd - 1), yb = (int)fminf(N - 1, fmaxf(y0, y1) + wd + 1);
  float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy + 1e-6f;
  for (int y = ya; y <= yb; y++)
    for (int x = xa; x <= xb; x++) {
      float t = cw_clampf(((x + 0.5f - x0) * dx + (y + 0.5f - y0) * dy) / l2, 0, 1);
      float ex = x + 0.5f - (x0 + dx * t), ey = y + 0.5f - (y0 + dy * t);
      float w = wd * (1 - 0.6f * t); /* tapering to its tip */
      if (ex * ex + ey * ey < w * w) { uint8_t *p = &px[(y * N + x) * 4]; p[0] = p[1] = p[2] = u8c(k), p[3] = 255; }
    }
}
static t3_texture *finish_tex(uint8_t *px, int N) {
  /* the cut-out's fringe takes its neighbours' colour, so the mips do not darken the edges */
  for (int pass = 0; pass < 4; pass++)
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++) {
        uint8_t *p = &px[(y * N + x) * 4];
        if (p[3] || p[0]) continue;
        int s = 0, n = 0;
        for (int k = 0; k < 4; k++) {
          int xx = x + (k == 0) - (k == 1), yy = y + (k == 2) - (k == 3);
          if (xx < 0 || yy < 0 || xx >= N || yy >= N) continue;
          uint8_t *q = &px[(yy * N + xx) * 4];
          if (q[0]) s += q[0], n++;
        }
        if (n) p[0] = p[1] = p[2] = (uint8_t)(s / n);
      }
  t3_texture *t = t3_texture_new(N, N, px);
  t->color_space = T3_SRGB_COLOR_SPACE;
  t->anisotropy = 4;
  return t;
}
static void make_textures(void) {
  enum { N = 256 };
  static uint8_t px[N * N * 4];
  /* broad leaves: a round spray of pointed leaves, each its own shade, a darker vein down it, the lower leaves in
   * the shade of the upper */
  memset(px, 0, sizeof px);
  rs = 977;
  for (int i = 0; i < 9; i++) { /* twigs under the leaves */
    float a = rf() * 2 * PI_F;
    stroke(px, N, N * 0.5f, N * 0.62f, N * (0.5f + cosf(a) * 0.36f), N * (0.55f + sinf(a) * 0.36f), 1.6f, 0.3f);
  }
  for (int i = 0; i < 300; i++) {
    float r = 0.42f * sqrtf(rf()), a = rf() * 2 * PI_F;
    float cx = N * (0.5f + cosf(a) * r), cy = N * (0.52f + sinf(a) * r * 0.92f);
    float L = N * rr(0.035f, 0.055f), Wd = L * rr(0.4f, 0.55f), ang = rf() * 2 * PI_F, ca = cosf(ang), sa = sinf(ang);
    float k = rr(0.55f, 1.0f) * (0.75f + 0.25f * (cy / N));
    for (int y = (int)(cy - L - 1); y <= (int)(cy + L + 1); y++)
      for (int x = (int)(cx - L - 1); x <= (int)(cx + L + 1); x++) {
        if (x < 0 || y < 0 || x >= N || y >= N) continue;
        float lx = (x + 0.5f - cx) * ca + (y + 0.5f - cy) * sa, ly = -(x + 0.5f - cx) * sa + (y + 0.5f - cy) * ca;
        float t = lx / L;
        if (fabsf(t) > 1) continue;
        float half = Wd * (1 - t * t);
        if (fabsf(ly) > half) continue;
        float vein = fabsf(ly) < 0.8f ? 0.78f : 1.0f, rim = 0.85f + 0.15f * (1 - fabsf(ly) / (half + 1e-3f));
        uint8_t *p = &px[(y * N + x) * 4];
        p[0] = p[1] = p[2] = u8c(k * vein * rim), p[3] = 255;
      }
  }
  TX[TX_LEAF] = finish_tex(px, N);
  /* a needle spray: u along the branch (0 at the trunk), side shoots, needles out of every shoot toward its tip */
  memset(px, 0, sizeof px);
  rs = 4111;
  stroke(px, N, 2, N * 0.5f, N * 0.97f, N * 0.5f, 2.2f, 0.28f);
  for (int i = 0; i < 14; i++) {
    float x0 = N * (0.04f + 0.064f * i), l = N * 0.44f * (1 - 0.06f * i);
    for (int s = -1; s <= 1; s += 2) {
      float x1 = x0 + l * 0.75f, y1 = N * 0.5f + s * l * 0.62f;
      stroke(px, N, x0, N * 0.5f, x1, y1, 1.4f, 0.3f);
      for (int k = 0; k < 20; k++) {
        float t = (float)k / 20.0f, bx = x0 + (x1 - x0) * t, by = N * 0.5f + (y1 - N * 0.5f) * t, nl = N * 0.075f * (1 - 0.5f * t);
        for (int q = -1; q <= 1; q += 2) {
          float a = atan2f(y1 - N * 0.5f, x1 - x0) + q * rr(0.6f, 1.0f);
          stroke(px, N, bx, by, bx + cosf(a) * nl, by + sinf(a) * nl, 0.9f, rr(0.55f, 1.0f));
        }
      }
    }
    for (int k = 0; k < 8; k++) { /* needles off the main branch */
      float bx = x0 + N * 0.09f * (float)k / 8.0f;
      for (int q = -1; q <= 1; q += 2) stroke(px, N, bx, N * 0.5f, bx + N * 0.04f, N * 0.5f + q * N * 0.05f, 0.9f, rr(0.5f, 0.9f));
    }
  }
  TX[TX_NEEDLE] = finish_tex(px, N);
  /* a palm frond's half-V: v along the frond, the rib down the middle of u, leaflets out to both edges and on
   * toward the tip, shorter at both ends */
  memset(px, 0, sizeof px);
  rs = 6007;
  stroke(px, N, N * 0.5f, 0, N * 0.5f, N - 1, 2.0f, 0.45f);
  for (int i = 0; i < 70; i++) {
    float t = (float)i / 70.0f, y0 = N * t, l = N * 0.48f * (0.35f + 0.65f * sinf(PI_F * (0.15f + 0.85f * t)));
    for (int s = -1; s <= 1; s += 2) stroke(px, N, N * 0.5f, y0, N * 0.5f + s * l, y0 + l * 0.55f, 1.5f, rr(0.6f, 1.0f));
  }
  TX[TX_FROND] = finish_tex(px, N);
}

/* ── the forest ────────────────────────────────────────────────────── */
static struct {
  t3_instanced_mesh *wood[2], *leaf[2];
  int n[2];
  tree_spot spot[2][TREES_MAX];
  float amp[2];
} T;

void trees_init(void) {
  memset(&T, 0, sizeof T);
  make_textures();
}

static t3_instanced_mesh *forest_mesh(cw_node parent, const char *name, t3_geometry *g, t3_material *m, int n) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  im->mesh.base.cast_shadow = false, im->mesh.base.receive_shadow = false; /* past the sun's shadow map (30 m round the structure): casting or sampling it would only cost time */
  im->instance_matrix->usage = T3_DYNAMIC_DRAW;
  t3_sphere sph = { { 0, 5, 0 }, 160 };
  t3_instanced_mesh_set_bounding_sphere(im, &sph);
  cw_node node = cw_node_new("Node3D", name);
  cw_node_add_child(parent, node);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, &im->mesh.base);
  cw_node3d_set_object(node, wrap);
  t3_release(wrap), t3_release(g), t3_release(m);
  return im;
}

void trees_build(cw_node parent, const tree_spot *spots, int n) {
  memset(T.wood, 0, sizeof T.wood), memset(T.leaf, 0, sizeof T.leaf), memset(T.n, 0, sizeof T.n);
  int sp_of[2] = { -1, -1 };
  for (int i = 0; i < n; i++) {
    int s = spots[i].species, k = sp_of[0] == s ? 0 : sp_of[1] == s ? 1 : sp_of[0] < 0 ? 0 : sp_of[1] < 0 ? 1 : -1;
    if (k < 0 || T.n[k] >= TREES_MAX) continue;
    sp_of[k] = s;
    T.spot[k][T.n[k]++] = spots[i];
  }
  for (int k = 0; k < 2; k++) {
    if (!T.n[k]) continue;
    mb w = { 0 }, f = { 0 };
    int tx = TX_LEAF;
    rs = 1234u + (uint32_t)sp_of[k] * 77u;
    switch (sp_of[k]) {
    case SP_OAK: oak(&w, &f), T.amp[k] = 0.02f; break;
    case SP_BIRCH: birch(&w, &f), T.amp[k] = 0.028f; break;
    case SP_SPRUCE: spruce(&w, &f, false), tx = TX_NEEDLE, T.amp[k] = 0.012f; break;
    case SP_FIR_SNOW: spruce(&w, &f, true), tx = TX_NEEDLE, T.amp[k] = 0.008f; break;
    default: palm(&w, &f), tx = TX_FROND, T.amp[k] = 0.025f; break;
    }
    t3_material *mw = t3_mesh_standard_material_new(0xffffff);
    mw->vertex_colors = true, mw->roughness = 0.92f;
    t3_material *ml = t3_mesh_standard_material_new(0xffffff);
    ml->vertex_colors = true, ml->roughness = 0.8f, ml->side = T3_DOUBLE_SIDE, ml->alpha_test = 0.5f;
    t3_material_set_map(ml, TX[tx]);
    T.wood[k] = forest_mesh(parent, "TreeWood", mb_finish(&w), mw, T.n[k]);
    T.leaf[k] = forest_mesh(parent, "TreeLeaves", mb_finish(&f), ml, T.n[k]);
    for (int i = 0; i < T.n[k]; i++) {
      t3_instanced_mesh_set_color_at(T.wood[k], i, (t3_color){ 1, 1, 1 });
      t3_instanced_mesh_set_color_at(T.leaf[k], i, T.spot[k][i].leaf);
    }
  }
  trees_sway(0);
}

void trees_sway(float t) {
  for (int k = 0; k < 2; k++) {
    if (!T.wood[k]) continue;
    for (int i = 0; i < T.n[k]; i++) {
      const tree_spot *s = &T.spot[k][i];
      t3_mat4 m;
      t3_quat q = t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, s->yaw);
      t3_mat4_compose(&m, (t3_vec3){ s->x, s->y, s->z }, q, (t3_vec3){ s->scale, s->scale * s->stretch, s->scale });
      /* the wind: a shear along x and z that grows with height (the foot stays put), gusting, each tree a little
       * out of step with the next (its phase from where it stands) */
      float ph = s->x * 0.11f + s->z * 0.07f;
      float gust = 0.6f + 0.4f * sinf(t * 0.37f + ph * 0.5f);
      float kx = T.amp[k] * gust * (sinf(t * 1.3f + ph) + 0.35f * sinf(t * 3.1f + ph * 2.3f)) + T.amp[k] * 0.8f;
      float kz = T.amp[k] * gust * 0.5f * sinf(t * 1.05f + ph * 1.7f);
      m.e[0] += kx * m.e[1], m.e[4] += kx * m.e[5], m.e[8] += kx * m.e[9];
      m.e[2] += kz * m.e[1], m.e[6] += kz * m.e[5], m.e[10] += kz * m.e[9];
      t3_instanced_mesh_set_matrix_at(T.wood[k], i, &m);
      t3_instanced_mesh_set_matrix_at(T.leaf[k], i, &m);
    }
    T.wood[k]->count = T.leaf[k]->count = T.n[k];
    T.wood[k]->instance_matrix->version++, T.leaf[k]->instance_matrix->version++;
  }
}

void trees_clear(void) { memset(T.wood, 0, sizeof T.wood), memset(T.leaf, 0, sizeof T.leaf), memset(T.n, 0, sizeof T.n); }
