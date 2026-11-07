/* pinball's mesh builder (geo.h) */
#include "geo.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void gb_free(gb *b) {
  free(b->pos), free(b->nrm), free(b->uv), free(b->idx);
  memset(b, 0, sizeof *b);
}

static void grow(gb *b, int nv, int ni) {
  if (b->nv + nv > b->cv) {
    int c = b->cv ? b->cv : 256;
    while (c < b->nv + nv) c *= 2;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)c);
    b->nrm = realloc(b->nrm, sizeof(float) * 3 * (size_t)c);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)c);
    b->cv = c;
  }
  if (b->ni + ni > b->ci) {
    int c = b->ci ? b->ci : 512;
    while (c < b->ni + ni) c *= 2;
    b->idx = realloc(b->idx, sizeof(unsigned) * (size_t)c);
    b->ci = c;
  }
}
static int vtx(gb *b, float x, float y, float z, float nx, float ny, float nz, float u, float v) {
  int i = b->nv++;
  b->pos[i * 3] = x, b->pos[i * 3 + 1] = y, b->pos[i * 3 + 2] = z;
  float l = sqrtf(nx * nx + ny * ny + nz * nz);
  if (l < 1e-9f) l = 1;
  b->nrm[i * 3] = nx / l, b->nrm[i * 3 + 1] = ny / l, b->nrm[i * 3 + 2] = nz / l;
  b->uv[i * 2] = u, b->uv[i * 2 + 1] = v;
  return i;
}
static void tri(gb *b, int a, int c, int d) { b->idx[b->ni++] = (unsigned)a, b->idx[b->ni++] = (unsigned)c, b->idx[b->ni++] = (unsigned)d; }
static void quad(gb *b, int a, int c, int d, int e) { tri(b, a, c, d), tri(b, a, d, e); }

void gb_box(gb *b, float cx, float cy, float cz, float hx, float hy, float hz, float yaw) {
  grow(b, 24, 36);
  float c = cosf(yaw), s = sinf(yaw);
  static const float F[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
  for (int f = 0; f < 6; f++) {
    float n[3] = { F[f][0], F[f][1], F[f][2] };
    /* two axes across the face */
    float u[3] = { n[1] != 0 ? 1.0f : 0.0f, n[1] != 0 ? 0.0f : 1.0f, 0 }, v[3];
    if (n[2] != 0) u[0] = 1, u[1] = 0, u[2] = 0;
    v[0] = n[1] * u[2] - n[2] * u[1], v[1] = n[2] * u[0] - n[0] * u[2], v[2] = n[0] * u[1] - n[1] * u[0];
    int base = b->nv;
    for (int k = 0; k < 4; k++) {
      float su = (k == 1 || k == 2) ? 1.0f : -1.0f, sv = (k >= 2) ? 1.0f : -1.0f;
      float lx = (n[0] + u[0] * su + v[0] * sv) * hx, ly = (n[1] + u[1] * su + v[1] * sv) * hy, lz = (n[2] + u[2] * su + v[2] * sv) * hz;
      float wx = c * lx + s * lz, wz = -s * lx + c * lz;
      float nx = c * n[0] + s * n[2], nz = -s * n[0] + c * n[2];
      vtx(b, cx + wx, cy + ly, cz + wz, nx, n[1], nz, su * 0.5f + 0.5f, sv * 0.5f + 0.5f);
    }
    quad(b, base, base + 1, base + 2, base + 3);
    /* face winding: outward */
    float *p0 = &b->pos[base * 3], *p1 = &b->pos[(base + 1) * 3], *p2 = &b->pos[(base + 2) * 3];
    float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] }, e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
    float cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
    float *nn = &b->nrm[base * 3];
    if (cr[0] * nn[0] + cr[1] * nn[1] + cr[2] * nn[2] < 0) {
      unsigned *q = &b->idx[b->ni - 6];
      unsigned t = q[1]; q[1] = q[2], q[2] = t;
      t = q[4], q[4] = q[5], q[5] = t;
    }
  }
}

void gb_cyl(gb *b, float cx, float y0, float cz, float y1, float r, int segs, bool cap) {
  grow(b, (segs + 1) * 2 + (cap ? segs + 2 : 0), segs * 6 + (cap ? segs * 3 : 0));
  int base = b->nv;
  for (int i = 0; i <= segs; i++) {
    float a = 6.2831853f * (float)i / (float)segs, x = cosf(a), z = -sinf(a);
    vtx(b, cx + x * r, y0, cz + z * r, x, 0, z, (float)i / (float)segs, 0);
    vtx(b, cx + x * r, y1, cz + z * r, x, 0, z, (float)i / (float)segs, 1);
  }
  for (int i = 0; i < segs; i++) quad(b, base + i * 2, base + i * 2 + 2, base + i * 2 + 3, base + i * 2 + 1);
  if (cap) {
    int c = vtx(b, cx, y1, cz, 0, 1, 0, 0.5f, 0.5f), cb = b->nv;
    for (int i = 0; i <= segs; i++) {
      float a = 6.2831853f * (float)i / (float)segs;
      vtx(b, cx + cosf(a) * r, y1, cz - sinf(a) * r, 0, 1, 0, 0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a));
    }
    for (int i = 0; i < segs; i++) tri(b, c, cb + i, cb + i + 1);
  }
}

void gb_band(gb *b, float cx, float y0, float cz, float y1, float r_in, float r_out, int segs) {
  /* the outside bulges: three rings of vertices (bottom, middle out, top) */
  grow(b, (segs + 1) * 5, segs * 24);
  int base = b->nv;
  float ym = (y0 + y1) * 0.5f, rm = r_out, re = (r_in + r_out) * 0.5f;
  for (int i = 0; i <= segs; i++) {
    float a = 6.2831853f * (float)i / (float)segs, x = cosf(a), z = -sinf(a);
    vtx(b, cx + x * re, y0, cz + z * re, x * 0.5f, -1, z * 0.5f, 0, 0);
    vtx(b, cx + x * rm, ym, cz + z * rm, x, 0, z, 0, 0.5f);
    vtx(b, cx + x * re, y1, cz + z * re, x * 0.5f, 1, z * 0.5f, 0, 1);
    vtx(b, cx + x * r_in, y1, cz + z * r_in, 0, 1, 0, 0, 1);
    vtx(b, cx + x * r_in, y0, cz + z * r_in, -x, 0, -z, 0, 0);
  }
  for (int i = 0; i < segs; i++) {
    int a = base + i * 5, c = base + (i + 1) * 5;
    quad(b, a, c, c + 1, a + 1);
    quad(b, a + 1, c + 1, c + 2, a + 2);
    quad(b, a + 2, c + 2, c + 3, a + 3);
  }
}

void gb_tube(gb *b, const float (*p)[3], int n, float r, int segs) {
  if (n < 2) return;
  grow(b, n * (segs + 1), (n - 1) * segs * 6);
  int base = b->nv;
  float up0[3] = { 0, 1, 0 };
  for (int i = 0; i < n; i++) {
    int a = i > 0 ? i - 1 : 0, c = i < n - 1 ? i + 1 : n - 1;
    float t[3] = { p[c][0] - p[a][0], p[c][1] - p[a][1], p[c][2] - p[a][2] };
    float tl = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    if (tl < 1e-9f) tl = 1;
    t[0] /= tl, t[1] /= tl, t[2] /= tl;
    float *up = up0, alt[3] = { 1, 0, 0 };
    if (fabsf(t[1]) > 0.95f) up = alt;
    float s1[3] = { t[1] * up[2] - t[2] * up[1], t[2] * up[0] - t[0] * up[2], t[0] * up[1] - t[1] * up[0] };
    float sl = sqrtf(s1[0] * s1[0] + s1[1] * s1[1] + s1[2] * s1[2]);
    s1[0] /= sl, s1[1] /= sl, s1[2] /= sl;
    float s2[3] = { s1[1] * t[2] - s1[2] * t[1], s1[2] * t[0] - s1[0] * t[2], s1[0] * t[1] - s1[1] * t[0] };
    for (int k = 0; k <= segs; k++) {
      float an = 6.2831853f * (float)k / (float)segs, ca = cosf(an), sa = sinf(an);
      float nx = s1[0] * ca + s2[0] * sa, ny = s1[1] * ca + s2[1] * sa, nz = s1[2] * ca + s2[2] * sa;
      vtx(b, p[i][0] + nx * r, p[i][1] + ny * r, p[i][2] + nz * r, nx, ny, nz, (float)k / (float)segs, (float)i);
    }
  }
  for (int i = 0; i < n - 1; i++)
    for (int k = 0; k < segs; k++) {
      int a = base + i * (segs + 1) + k, c = a + segs + 1;
      quad(b, a, c, c + 1, a + 1);
    }
}

void gb_prism(gb *b, const float (*p)[2], int n, float y0, float y1) {
  if (n < 3) return;
  grow(b, n * 4 + 2 + n * 2, n * 6 + n * 6);
  float cx = 0, cz = 0;
  for (int i = 0; i < n; i++) cx += p[i][0], cz += p[i][1];
  cx /= (float)n, cz /= (float)n;
  /* the outline's turn: counter-clockwise seen from above (x right, z toward the viewer: area sign flips) */
  float area = 0;
  for (int i = 0; i < n; i++) { int j = (i + 1) % n; area += p[i][0] * p[j][1] - p[j][0] * p[i][1]; }
  bool ccw = area < 0;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    float ex = p[j][0] - p[i][0], ez = p[j][1] - p[i][1];
    float nx = ccw ? -ez : ez, nz = ccw ? ex : -ex; /* outward */
    int a = vtx(b, p[i][0], y0, p[i][1], nx, 0, nz, 0, 0), c = vtx(b, p[j][0], y0, p[j][1], nx, 0, nz, 1, 0);
    int d = vtx(b, p[j][0], y1, p[j][1], nx, 0, nz, 1, 1), e = vtx(b, p[i][0], y1, p[i][1], nx, 0, nz, 0, 1);
    if (ccw) quad(b, a, e, d, c); else quad(b, a, c, d, e);
  }
  int top = vtx(b, cx, y1, cz, 0, 1, 0, 0.5f, 0.5f), tb = b->nv;
  for (int i = 0; i < n; i++) vtx(b, p[i][0], y1, p[i][1], 0, 1, 0, 0, 0);
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    if (ccw) tri(b, top, tb + j, tb + i); else tri(b, top, tb + i, tb + j);
  }
}

void gb_trough(gb *b, const float (*p)[3], int n, float w, float h) {
  if (n < 2) return;
  grow(b, n * 8, (n - 1) * 36);
  int base = b->nv;
  for (int i = 0; i < n; i++) {
    int a = i > 0 ? i - 1 : 0, c = i < n - 1 ? i + 1 : n - 1;
    float tx = p[c][0] - p[a][0], tz = p[c][2] - p[a][2], tl = sqrtf(tx * tx + tz * tz);
    if (tl < 1e-9f) tl = 1;
    float sx = -tz / tl * w * 0.5f, sz = tx / tl * w * 0.5f; /* across, level */
    float x = p[i][0], y = p[i][1], z = p[i][2];
    /* floor (two vertices, up), left wall (inside), right wall (inside), and the outsides */
    vtx(b, x - sx, y, z - sz, 0, 1, 0, 0, (float)i);
    vtx(b, x + sx, y, z + sz, 0, 1, 0, 1, (float)i);
    vtx(b, x - sx, y + h, z - sz, sx, 0, sz, 0, (float)i);
    vtx(b, x - sx, y, z - sz, sx, 0, sz, 0, (float)i);
    vtx(b, x + sx, y, z + sz, -sx, 0, -sz, 1, (float)i);
    vtx(b, x + sx, y + h, z + sz, -sx, 0, -sz, 1, (float)i);
    vtx(b, x - sx, y - 0.002f, z - sz, 0, -1, 0, 0, (float)i);
    vtx(b, x + sx, y - 0.002f, z + sz, 0, -1, 0, 1, (float)i);
  }
  for (int i = 0; i < n - 1; i++) {
    int a = base + i * 8, c = a + 8;
    quad(b, a, a + 1, c + 1, c);         /* floor */
    quad(b, a + 2, a + 3, c + 3, c + 2); /* left wall */
    quad(b, a + 4, a + 5, c + 5, c + 4); /* right wall */
    quad(b, a + 6, c + 6, c + 7, a + 7); /* underside */
  }
}

void gb_dome(gb *b, float cx, float cy, float cz, float r, int segs) {
  int rings = segs / 2;
  grow(b, (rings + 1) * (segs + 1), rings * segs * 6);
  int base = b->nv;
  for (int j = 0; j <= rings; j++) {
    float ph = 1.5707963f * (float)j / (float)rings, y = sinf(ph), rr = cosf(ph);
    for (int i = 0; i <= segs; i++) {
      float a = 6.2831853f * (float)i / (float)segs, x = cosf(a) * rr, z = -sinf(a) * rr;
      vtx(b, cx + x * r, cy + y * r, cz + z * r, x, y, z, (float)i / (float)segs, (float)j / (float)rings);
    }
  }
  for (int j = 0; j < rings; j++)
    for (int i = 0; i < segs; i++) {
      int a = base + j * (segs + 1) + i, c = a + segs + 1;
      quad(b, a, a + 1, c + 1, c);
    }
}

t3_geometry *gb_geometry(gb *b) {
  t3_geometry *g = t3_geometry_new();
  if (b->nv == 0) { gb_free(b); return g; }
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, b->nrm, b->nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, b->idx, b->ni, 1));
  t3_geometry_compute_bounding_sphere(g);
  t3_geometry_compute_bounding_box(g);
  gb_free(b);
  return g;
}
