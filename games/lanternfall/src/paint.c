/* lanternfall: the painter (paint.h). */
#include "paint.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/jobs.h"

/* ── noise ── */
float pn_hash(int x, int y, uint32_t s) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) & 0xffffff) / 16777215.0f;
}
float pn_noise(float x, float y, uint32_t s) {
  float fx0 = floorf(x), fy0 = floorf(y);
  int x0 = (int)fx0, y0 = (int)fy0;
  float fx = x - fx0, fy = y - fy0;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = pn_hash(x0, y0, s), b = pn_hash(x0 + 1, y0, s), c = pn_hash(x0, y0 + 1, s), d = pn_hash(x0 + 1, y0 + 1, s);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
float pn_fbm(float x, float y, uint32_t s, int oct) {
  float v = 0, a = 0.5f, t = 0;
  for (int i = 0; i < oct; i++) v += pn_noise(x, y, s + (uint32_t)i * 31u) * a, t += a, a *= 0.5f, x *= 2.03f, y *= 2.03f;
  return v / t;
}
static float cl(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float ss(float a, float b, float x) {
  float t = cl((x - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
}
static float fractf(float x) { return x - floorf(x); }
/* nearest two scattered points (cells of size 1): f1, f2 and the nearest's id */
static void cells(float x, float y, uint32_t s, float *f1, float *f2, float *id) {
  int cx = (int)floorf(x), cy = (int)floorf(y);
  *f1 = *f2 = 9, *id = 0;
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
      float px = (float)(cx + i) + 0.15f + 0.7f * pn_hash(cx + i, cy + j, s), py = (float)(cy + j) + 0.15f + 0.7f * pn_hash(cx + i, cy + j, s + 1);
      float d = sqrtf((x - px) * (x - px) + (y - py) * (y - py));
      if (d < *f1) *f2 = *f1, *f1 = d, *id = pn_hash(cx + i, cy + j, s + 2);
      else if (d < *f2) *f2 = d;
    }
}

/* ── canvases ── */
pcan *pc_new(int w, int h) {
  pcan *p = calloc(1, sizeof *p);
  p->w = w, p->h = h;
  p->c = calloc((size_t)w * h * 4, sizeof(float));
  p->ht = calloc((size_t)w * h, sizeof(float));
  p->em = calloc((size_t)w * h, sizeof(float));
  p->sp = calloc((size_t)w * h, sizeof(float));
  p->gl = calloc((size_t)w * h, sizeof(float));
  return p;
}
void pc_free(pcan *p) {
  if (!p) return;
  free(p->c), free(p->ht), free(p->em), free(p->sp), free(p->gl), free(p);
}

/* ── shapes: a signed distance (pixels, negative inside) and the form's local thickness (for its dome) ── */
enum { SH_ELLIPSE, SH_CAPSULE, SH_POLY, SH_STROKE, SH_LEAF };
#define MAXPTS 64
typedef struct shape {
  int kind, n;
  float a[8];
  float pts[MAXPTS * 2], rad[MAXPTS];
  float thick, round;
} shape;

static float sd_ucapsule(float px, float py, float ax, float ay, float bx, float by, float ra, float rb, float *thick) {
  float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy + 1e-6f;
  float t = cl(((px - ax) * dx + (py - ay) * dy) / l2, 0, 1);
  float r = ra + (rb - ra) * t;
  float qx = px - ax - dx * t, qy = py - ay - dy * t;
  *thick = r;
  return sqrtf(qx * qx + qy * qy) - r;
}

static float sdf(const shape *s, float x, float y, float *thick) {
  switch (s->kind) {
  case SH_ELLIPSE: {
    float cs = s->a[5], sn = s->a[6], dx = x - s->a[0], dy = y - s->a[1];
    float lx = dx * cs + dy * sn, ly = -dx * sn + dy * cs, rx = s->a[2], ry = s->a[3];
    float k0 = sqrtf((lx / rx) * (lx / rx) + (ly / ry) * (ly / ry)), k1 = sqrtf((lx / (rx * rx)) * (lx / (rx * rx)) + (ly / (ry * ry)) * (ly / (ry * ry)));
    *thick = s->thick;
    return k1 > 1e-6f ? k0 * (k0 - 1) / k1 : -fminf(rx, ry);
  }
  case SH_CAPSULE: return sd_ucapsule(x, y, s->a[0], s->a[1], s->a[2], s->a[3], s->a[4], s->a[5], thick);
  case SH_STROKE: {
    float best = 1e9f, bt = 1;
    for (int i = 0; i + 1 < s->n; i++) {
      float th, d = sd_ucapsule(x, y, s->pts[i * 2], s->pts[i * 2 + 1], s->pts[i * 2 + 2], s->pts[i * 2 + 3], s->rad[i], s->rad[i + 1], &th);
      if (d < best) best = d, bt = th;
    }
    *thick = bt;
    return best;
  }
  case SH_LEAF: {
    float ax = s->a[0], ay = s->a[1], bx = s->a[2], by = s->a[3], w = s->a[4], bend = s->a[5];
    float dx = bx - ax, dy = by - ay, L = sqrtf(dx * dx + dy * dy) + 1e-6f, ux = dx / L, uy = dy / L;
    float u = (x - ax) * ux + (y - ay) * uy, v = -(x - ax) * uy + (y - ay) * ux;
    float t = u / L;
    float tc = cl(t, 0, 1), sp = sinf(3.14159f * tc);
    float hw = w * powf(sp, 0.75f) + 0.5f, off = bend * sp;
    *thick = fmaxf(hw, 1);
    float d = fabsf(v - off) - hw;
    if (t < 0) d = fmaxf(d, -u);
    if (t > 1) d = fmaxf(d, u - L);
    return d * 0.85f;
  }
  default: { /* polygon (iq): distance to the outline, sign by winding */
    const float *v = s->pts;
    int n = s->n;
    float d = (x - v[0]) * (x - v[0]) + (y - v[1]) * (y - v[1]), sg = 1;
    for (int i = 0, j = n - 1; i < n; j = i, i++) {
      float ex = v[j * 2] - v[i * 2], ey = v[j * 2 + 1] - v[i * 2 + 1], wx = x - v[i * 2], wy = y - v[i * 2 + 1];
      float t = cl((wx * ex + wy * ey) / (ex * ex + ey * ey + 1e-9f), 0, 1);
      float bx = wx - ex * t, by = wy - ey * t;
      d = fminf(d, bx * bx + by * by);
      bool c1 = y >= v[i * 2 + 1], c2 = y < v[j * 2 + 1], c3 = ex * wy > ey * wx;
      if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) sg = -sg;
    }
    *thick = s->thick;
    return sg * sqrtf(d) - s->round;
  }
  }
}

/* a material texture at (x, y): height and lightness offsets, extra glow */
static void texture(const pstyle *st, float x, float y, float depth, float *th, float *tl, float *te) {
  float s = st->tex_scale > 0 ? st->tex_scale : 8, cs = cosf(st->tex_dir), sn = sinf(st->tex_dir);
  float u = (x * cs + y * sn) / s, v = (-x * sn + y * cs) / s; /* v runs along the grain */
  uint32_t sd = st->seed * 977u + 13u;
  *th = *tl = *te = 0;
  switch (st->tex) {
  case TX_FUR: {
    float strand = pn_noise(u * 1.6f, v * 0.22f, sd), fine = pn_noise(u * 4.2f, v * 0.5f, sd + 7);
    float clump = pn_fbm(x / (s * 5), y / (s * 5), sd + 9, 2);
    *tl = (strand - 0.5f) * 0.9f + (fine - 0.5f) * 0.45f + (clump - 0.5f) * 0.5f;
    *th = strand * 0.7f + clump * 0.6f;
    break;
  }
  case TX_CHITIN: {
    float n = pn_fbm(x / s, y / s, sd, 3), growth = 0.5f + 0.5f * sinf(depth * 26 + n * 3);
    float pit = ss(0.78f, 0.9f, pn_noise(x / (s * 0.5f), y / (s * 0.5f), sd + 3));
    *tl = (n - 0.5f) * 0.5f + (growth - 0.5f) * 0.18f - pit * 0.25f;
    *th = n * 0.5f + growth * 0.2f - pit * 0.3f;
    break;
  }
  case TX_SCALES: {
    float row = floorf(v), xx = u + (((int)row) & 1 ? 0.5f : 0), cx = floorf(xx) + 0.5f;
    float dx = (xx - cx) * 1.1f, dy = (v - row) - 0.15f, d = sqrtf(dx * dx + dy * dy) * 1.25f;
    float body = 1 - cl(d, 0, 1), edge = ss(0.7f, 0.98f, d);
    float var = pn_hash((int)cx, (int)row, sd);
    *tl = body * 0.35f - edge * 0.45f + (var - 0.5f) * 0.25f;
    *th = body * 0.9f;
    break;
  }
  case TX_WARTS: {
    float f1, f2, id;
    cells(x / s, y / s, sd, &f1, &f2, &id);
    float bump = ss(0.55f, 0.0f, f1) * (0.4f + 0.6f * id), mott = pn_fbm(x / (s * 3), y / (s * 3), sd + 5, 2);
    *tl = bump * 0.4f - 0.08f + (mott - 0.5f) * 0.5f;
    *th = bump;
    break;
  }
  case TX_VEINS: {
    float n = fabsf(pn_fbm(x / (s * 4), y / (s * 4), sd, 3) - 0.5f), line = ss(0.035f, 0.0f, n);
    float mott = pn_fbm(x / s, y / s, sd + 4, 2);
    *tl = line * 0.35f + (mott - 0.5f) * 0.35f;
    *th = line * 0.6f + mott * 0.2f;
    *te = line;
    break;
  }
  case TX_FEATHER: {
    float col = floorf(u * 0.5f), rowv = v * 0.33f + pn_hash((int)col, 0, sd) * 0.7f, tip = fractf(rowv);
    float barb = fabsf(fractf(u * 2.5f + tip * 1.5f) - 0.5f);
    *tl = (barb - 0.25f) * 0.35f - ss(0.82f, 1.0f, tip) * 0.45f + (pn_hash((int)col, (int)floorf(rowv), sd) - 0.5f) * 0.2f;
    *th = tip * 0.8f;
    break;
  }
  case TX_METAL: {
    float brush = pn_noise(u * 0.12f, v * 3.0f, sd), scr = ss(0.03f, 0.0f, fabsf(pn_noise(x / (s * 3), y / (s * 3), sd + 2) - 0.5f));
    float tarn = pn_fbm(x / (s * 4), y / (s * 4), sd + 4, 3);
    *tl = (brush - 0.5f) * 0.25f + scr * 0.35f + (tarn - 0.5f) * 0.45f;
    *th = brush * 0.15f - scr * 0.2f;
    break;
  }
  case TX_CLOTH: {
    float weave = sinf(x / s * 6.2832f) * sinf(y / s * 6.2832f);
    float fold = pn_fbm(u * 0.25f, v * 0.06f, sd, 3);
    *tl = weave * 0.06f + (fold - 0.5f) * 0.7f;
    *th = fold * 1.0f + weave * 0.05f;
    break;
  }
  case TX_GLASS: {
    float f1, f2, id;
    cells(x / s, y / s, sd, &f1, &f2, &id);
    float crease = ss(0.09f, 0.0f, f2 - f1);
    *tl = (id - 0.5f) * 0.45f + crease * 0.55f;
    *th = id * 0.6f - crease * 0.3f;
    *te = crease * 0.6f;
    break;
  }
  case TX_BARK: {
    float n = pn_fbm(u * 0.6f, v * 0.12f, sd, 3), crack = ss(0.05f, 0.0f, fabsf(n - 0.5f));
    *tl = (n - 0.5f) * 0.5f - crack * 0.55f;
    *th = n * 0.8f - crack * 0.6f;
    break;
  }
  case TX_SKIN: {
    float m = pn_fbm(x / (s * 2), y / (s * 2), sd, 3), spot = ss(0.72f, 0.82f, pn_noise(x / s, y / s, sd + 3));
    *tl = (m - 0.5f) * 0.4f - spot * 0.25f;
    *th = m * 0.4f;
    break;
  }
  case TX_MOSS: {
    float f = pn_fbm(x / s, y / s, sd, 4), tuft = pn_noise(x / (s * 0.35f), y / (s * 0.35f), sd + 6);
    *tl = (f - 0.5f) * 0.9f + (tuft - 0.5f) * 0.35f;
    *th = f * 0.9f + tuft * 0.3f;
    break;
  }
  case TX_MEMBRANE: {
    float vein = ss(0.05f, 0.0f, fabsf(fractf(u * 0.18f + pn_noise(u * 0.1f, v * 0.08f, sd) * 0.6f) - 0.5f));
    float mott = pn_fbm(x / (s * 2), y / (s * 2), sd + 2, 3);
    *tl = (mott - 0.5f) * 0.4f - vein * 0.3f;
    *th = vein * 0.4f;
    break;
  }
  case TX_STONE: {
    float n = pn_fbm(x / s, y / s, sd, 4), crack = ss(0.04f, 0.0f, fabsf(pn_fbm(x / (s * 3), y / (s * 3), sd + 3, 3) - 0.5f));
    *tl = (n - 0.5f) * 0.55f - crack * 0.5f;
    *th = n - crack * 0.6f;
    break;
  }
  default: break;
  }
}

typedef struct draw_ctx { pcan *p; const shape *sh; const pstyle *st; int x0, x1, y0; cw_color base, shade, hi, ink; float shr; } draw_ctx;

static void draw_rows(int begin, int end, int worker, void *vc) {
  (void)worker;
  const draw_ctx *c = vc;
  pcan *p = c->p;
  const pstyle *st = c->st;
  const float soft = st->soft > 0 ? st->soft : 1.2f, alpha = st->alpha > 0 ? st->alpha : 1, shr = c->shr, shoff = shr * 0.35f;
  for (int y = c->y0 + begin; y < c->y0 + end; y++)
    for (int x = c->x0; x < c->x1; x++) {
      float fx = (float)x + 0.5f, fy = (float)y + 0.5f, thick;
      float d = sdf(c->sh, fx, fy, &thick);
      size_t i = (size_t)y * p->w + x;
      float *C = p->c + 4 * i;
      /* the contact shadow on what is under: cast a little downward */
      if (st->shadow > 0 && d > -soft && C[3] > 0) {
        float t2, ds = sdf(c->sh, fx, fy - shoff, &t2);
        if (ds < shr) {
          float k = st->shadow * (1 - ss(-soft, shr, ds));
          C[0] *= 1 - k, C[1] *= 1 - k, C[2] *= 1 - k;
        }
      }
      if (d > soft) continue;
      float a = cl(0.5f - d / (soft * 2), 0, 1) * alpha;
      if (a <= 0) continue;
      float depth = cl(-d / fmaxf(thick, 1), 0, 1), prof = sqrtf(1 - (1 - depth) * (1 - depth));
      /* the edge's facing (for the key light from above, a little left) */
      float t3, gx = sdf(c->sh, fx + 1, fy, &t3) - sdf(c->sh, fx - 1, fy, &t3), gy = sdf(c->sh, fx, fy + 1, &t3) - sdf(c->sh, fx, fy - 1, &t3);
      float gl = sqrtf(gx * gx + gy * gy) + 1e-6f, key = (-gx * 0.42f - gy * 0.91f) / gl * (1 - prof);
      cw_color col = pc_mix(c->shade, c->base, ss(0.0f, 0.75f, depth));
      if (key > 0) col = pc_mix(col, c->hi, cl(key * 0.7f, 0, 1));
      else col = pc_mix(col, c->shade, cl(-key * 0.6f, 0, 1));
      col = pc_mix(col, c->hi, prof * prof * 0.18f);
      float th = 0, tl = 0, te = 0;
      if (st->tex) {
        texture(st, fx, fy, depth, &th, &tl, &te);
        float k = 1 + tl * st->tex_amt;
        col.r *= k, col.g *= k, col.b *= k;
      }
      if (st->ink > 0) col = pc_mix(col, c->ink, ss(st->ink + 0.6f, st->ink - 0.6f, -d) * 0.85f);
      if (st->rim > 0 && key > 0) col = pc_mix(col, c->hi, cl(st->rim * key * 2 * (1 - cl(-d / 3, 0, 1)), 0, 1));
      float h = st->lift + st->dome * prof + th * st->tex_amt * 0.35f;
      C[0] = col.r * a + C[0] * (1 - a), C[1] = col.g * a + C[1] * (1 - a), C[2] = col.b * a + C[2] * (1 - a), C[3] = a + C[3] * (1 - a);
      p->ht[i] += (h - p->ht[i]) * a;
      float em = st->em > 0 ? cl(st->em * (0.75f + 0.5f * te), 0, 1) : 0;
      p->em[i] += (em - p->em[i]) * a, p->sp[i] += (st->spec - p->sp[i]) * a, p->gl[i] += (st->gloss - p->gl[i]) * a;
    }
}

static void draw(pcan *p, const shape *sh, const pstyle *st, float bx0, float by0, float bx1, float by1) {
  float shr = st->shadow > 0 ? (st->shadow_r > 0 ? st->shadow_r : 10) : 0, m = shr + 3 + (st->soft > 0 ? st->soft : 1.2f);
  int x0 = (int)floorf(bx0 - m), y0 = (int)floorf(by0 - m), x1 = (int)ceilf(bx1 + m), y1 = (int)ceilf(by1 + m + shr * 0.35f);
  x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0, x1 = x1 > p->w ? p->w : x1, y1 = y1 > p->h ? p->h : y1;
  if (x1 <= x0 || y1 <= y0) return;
  cw_color base = st->base;
  cw_color shade = st->shade.a > 0 ? st->shade : pc_scale(base, 0.55f), hi = st->hi.a > 0 ? st->hi : pc_mix(base, (cw_color){ 1, 0.97f, 0.9f, 1 }, 0.3f);
  draw_ctx c = { p, sh, st, x0, x1, y0, base, shade, hi, pc_scale(shade, 0.3f), shr };
  cw_parallel_for(y1 - y0, 8, draw_rows, &c);
}

void pc_ellipse(pcan *p, float cx, float cy, float rx, float ry, float rot, const pstyle *s) {
  shape sh = { .kind = SH_ELLIPSE, .a = { cx, cy, rx, ry, rot, cosf(rot), sinf(rot) }, .thick = fminf(rx, ry) };
  float r = fmaxf(rx, ry);
  draw(p, &sh, s, cx - r, cy - r, cx + r, cy + r);
}
void pc_capsule(pcan *p, float ax, float ay, float bx, float by, float ra, float rb, const pstyle *s) {
  shape sh = { .kind = SH_CAPSULE, .a = { ax, ay, bx, by, ra, rb } };
  float r = fmaxf(ra, rb);
  draw(p, &sh, s, fminf(ax, bx) - r, fminf(ay, by) - r, fmaxf(ax, bx) + r, fmaxf(ay, by) + r);
}
void pc_poly(pcan *p, const float *xy, int n, float round, const pstyle *s) {
  if (n > MAXPTS) n = MAXPTS;
  shape sh = { .kind = SH_POLY, .n = n, .round = round };
  memcpy(sh.pts, xy, sizeof(float) * 2 * (size_t)n);
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, cx = 0, cy = 0;
  for (int i = 0; i < n; i++) x0 = fminf(x0, xy[i * 2]), x1 = fmaxf(x1, xy[i * 2]), y0 = fminf(y0, xy[i * 2 + 1]), y1 = fmaxf(y1, xy[i * 2 + 1]), cx += xy[i * 2], cy += xy[i * 2 + 1];
  /* the dome's thickness: the deepest point inside (sampled) */
  float th = 1, t;
  sh.thick = 1;
  for (float yy = y0; yy <= y1; yy += fmaxf(1, (y1 - y0) / 24))
    for (float xx = x0; xx <= x1; xx += fmaxf(1, (x1 - x0) / 24)) th = fmaxf(th, -sdf(&sh, xx, yy, &t));
  sh.thick = th;
  (void)cx, (void)cy;
  draw(p, &sh, s, x0 - round, y0 - round, x1 + round, y1 + round);
}
void pc_stroke(pcan *p, const float *xy, int n, float r0, float r1, const pstyle *s) {
  shape sh = { .kind = SH_STROKE };
  /* Catmull-Rom through the points, 6 steps a span */
  int m = 0;
  for (int i = 0; i + 1 < n && m < MAXPTS - 1; i++) {
    int i0 = i > 0 ? i - 1 : i, i2 = i + 1, i3 = i + 2 < n ? i + 2 : i + 1;
    for (int k = 0; k < 6 && m < MAXPTS - 1; k++) {
      float t = (float)k / 6, t2 = t * t, t3 = t2 * t;
      for (int c = 0; c < 2; c++) {
        float P0 = xy[i0 * 2 + c], P1 = xy[i * 2 + c], P2 = xy[i2 * 2 + c], P3 = xy[i3 * 2 + c];
        sh.pts[m * 2 + c] = 0.5f * ((2 * P1) + (-P0 + P2) * t + (2 * P0 - 5 * P1 + 4 * P2 - P3) * t2 + (-P0 + 3 * P1 - 3 * P2 + P3) * t3);
      }
      m++;
    }
  }
  sh.pts[m * 2] = xy[(n - 1) * 2], sh.pts[m * 2 + 1] = xy[(n - 1) * 2 + 1], m++;
  sh.n = m;
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (int i = 0; i < m; i++) {
    sh.rad[i] = r0 + (r1 - r0) * (float)i / (float)(m - 1);
    x0 = fminf(x0, sh.pts[i * 2]), x1 = fmaxf(x1, sh.pts[i * 2]), y0 = fminf(y0, sh.pts[i * 2 + 1]), y1 = fmaxf(y1, sh.pts[i * 2 + 1]);
  }
  float r = fmaxf(r0, r1);
  draw(p, &sh, s, x0 - r, y0 - r, x1 + r, y1 + r);
}
void pc_leaf(pcan *p, float ax, float ay, float bx, float by, float w, float bend, const pstyle *s) {
  shape sh = { .kind = SH_LEAF, .a = { ax, ay, bx, by, w, bend } };
  float r = w + fabsf(bend);
  draw(p, &sh, s, fminf(ax, bx) - r, fminf(ay, by) - r, fmaxf(ax, bx) + r, fmaxf(ay, by) + r);
}

/* ── the finished textures ── */
static uint8_t u8(float v) { return (uint8_t)(v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255 + 0.5f)); }

ptex pc_finish(pcan *p, float k) {
  int w = p->w, h = p->h;
  ptex out = { .w = w, .h = h };
  uint8_t *col = malloc((size_t)w * h * 4), *nrm = malloc((size_t)w * h * 4), *mat = malloc((size_t)w * h * 4);
  for (int side = 0; side < 2; side++) {
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        int sx = side ? w - 1 - x : x;
        size_t i = (size_t)y * w + sx;
        const float *C = p->c + 4 * i;
        float a = C[3];
        uint8_t *o = col + 4 * ((size_t)y * w + x);
        o[0] = u8(a > 1e-4f ? C[0] / a : 0), o[1] = u8(a > 1e-4f ? C[1] / a : 0), o[2] = u8(a > 1e-4f ? C[2] / a : 0), o[3] = u8(a);
        int xl = sx > 0 ? sx - 1 : sx, xr = sx < w - 1 ? sx + 1 : sx, yu = y > 0 ? y - 1 : y, yd = y < h - 1 ? y + 1 : y;
        float dx = (p->ht[(size_t)y * w + xr] - p->ht[(size_t)y * w + xl]) * k, dy = (p->ht[(size_t)yd * w + sx] - p->ht[(size_t)yu * w + sx]) * k;
        float nx = -dx, ny = dy, nz = 1, l = sqrtf(nx * nx + ny * ny + nz * nz);
        if (side) nx = -nx;
        uint8_t *n = nrm + 4 * ((size_t)y * w + x);
        n[0] = u8(nx / l * 0.5f + 0.5f), n[1] = u8(ny / l * 0.5f + 0.5f), n[2] = u8(nz / l * 0.5f + 0.5f), n[3] = 255;
        uint8_t *m = mat + 4 * ((size_t)y * w + x);
        m[0] = u8(p->em[i]), m[1] = u8(p->sp[i]), m[2] = u8(p->gl[i]), m[3] = 255;
      }
    out.t[side] = cw_texture_from_pixels(w, h, col, &(cw_texture_desc){ .mipmaps = true });
    cw_texture_set_normal_map(out.t[side], nrm);
    cw_texture_set_material_map(out.t[side], mat);
  }
  free(col), free(nrm), free(mat);
  return out;
}
