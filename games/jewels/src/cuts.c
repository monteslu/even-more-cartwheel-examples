/* The cuts (cuts.h): facet planes from cutting angles, and light traced through them on the CPU. */
#include "cuts.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979f
#define RAD(d) ((d) * PI / 180.0f)

jw_cut jw_cuts[JW_NCUTS];
/* the key light: high on the upper left, a little toward the viewer */
const float jw_key_dir[3] = { -0.4472136f, 0.5366563f, 0.7155418f };

/* ── outlines and their support functions ───────────────────────────── */

#define OUT_N 720
typedef struct outline { int n; float x[OUT_N], y[OUT_N]; } outline;

/* how far the outline reaches toward direction psi */
static float support(const outline *o, float psi) {
  float cx = cosf(psi), cy = sinf(psi), h = -1e9f;
  for (int i = 0; i < o->n; i++) {
    float v = o->x[i] * cx + o->y[i] * cy;
    if (v > h) h = v;
  }
  return h;
}
static float outline_area(const outline *o) {
  float a = 0;
  for (int i = 0; i < o->n; i++) {
    int j = (i + 1) % o->n;
    a += o->x[i] * o->y[j] - o->x[j] * o->y[i];
  }
  return fabsf(a) * 0.5f;
}
static void outline_circle(outline *o) {
  o->n = OUT_N;
  for (int i = 0; i < OUT_N; i++) o->x[i] = cosf(2 * PI * i / OUT_N), o->y[i] = sinf(2 * PI * i / OUT_N);
}
/* a pear: a round bottom of radius r and two straight flanks meeting in a point at the top */
static void outline_pear(outline *o) {
  /* a teardrop: round below, gently convex shoulders up to the point at the top */
  o->n = OUT_N;
  for (int i = 0; i < OUT_N; i++) {
    float t = 2 * PI * (float)i / (float)OUT_N;
    o->x[i] = 0.82f * sinf(t) * powf(fabsf(sinf(t * 0.5f)), 0.85f), o->y[i] = cosf(t);
  }
}
/* a marquise (navette): the lens two circles make, pointed left and right */
static void outline_marquise(outline *o, float half_w, float half_h) {
  /* circles of radius R through (+-half_w, 0) and (0, +-half_h), centred on the y axis */
  float R = (half_w * half_w + half_h * half_h) / (2 * half_h), c = R - half_h;
  float a = asinf(half_w / R);
  int n = 0;
  for (int i = 0; i < OUT_N / 2; i++) { /* the upper arc, right to left */
    float t = -a + 2 * a * (float)i / (float)(OUT_N / 2 - 1);
    o->x[n] = R * sinf(-t), o->y[n] = -c + R * cosf(t), n++;
  }
  for (int i = 0; i < OUT_N / 2; i++) { /* the lower arc, left to right */
    float t = -a + 2 * a * (float)i / (float)(OUT_N / 2 - 1);
    o->x[n] = R * sinf(t), o->y[n] = c - R * cosf(t), n++;
  }
  o->n = n;
}
static void outline_poly(outline *o, const float *xy, int n) {
  o->n = n;
  for (int i = 0; i < n; i++) o->x[i] = xy[i * 2], o->y[i] = xy[i * 2 + 1];
}

/* ── planes ──────────────────────────────────────────────────────────── */

static void plane(jw_cut *c, float nx, float ny, float nz, float d) {
  if (c->nplanes >= JW_MAX_PLANES) return;
  float l = sqrtf(nx * nx + ny * ny + nz * nz);
  jw_plane *p = &c->p[c->nplanes++];
  p->n[0] = nx / l, p->n[1] = ny / l, p->n[2] = nz / l, p->d = d / l;
}
/* a facet sloping alpha from the girdle plane, facing psi, through the point at horizontal reach h and height z;
 * up: a crown facet (else the pavilion) */
static void facet(jw_cut *c, float alpha, float psi, float h, float z, bool up) {
  float s = sinf(alpha), k = cosf(alpha), nx = s * cosf(psi), ny = s * sinf(psi), nz = up ? k : -k;
  plane(c, nx, ny, nz, nx * h * cosf(psi) + ny * h * sinf(psi) + nz * z);
}

/* a brilliant on any convex outline: table, 8 stars, 8 bezels, 16 upper girdle halves, a faceted girdle, 8 pavilion
 * mains, 16 lower girdle halves and a small culet. Angles in degrees; rt: the table's share of the outline */
typedef struct brilliant { float crown, pavilion, table, girdle, star_drop, upper_rise, lower_rise; int girdle_n; float phase; } brilliant;
static void build_brilliant(jw_cut *c, const outline *o, const brilliant *b) {
  float g = b->girdle * 0.5f;
  float hc = (1 - b->table) * tanf(RAD(b->crown)); /* crown height above the girdle */
  float zt = g + hc;
  plane(c, 0, 0, 1, zt); /* the table */
  for (int k = 0; k < 8; k++) {
    float psi = RAD(b->phase + 45.0f * k);
    /* bezels: through the girdle's top edge */
    facet(c, RAD(b->crown), psi, support(o, psi), g, true);
    /* upper girdle halves: steeper, through the girdle's top either side of the bezel */
    for (int s = -1; s <= 1; s += 2) {
      float q = psi + RAD(11.25f) * (float)s;
      facet(c, RAD(b->crown + b->upper_rise), q, support(o, q), g, true);
    }
    /* stars: shallower, meeting the table's edge between the bezels */
    float qs = psi + RAD(22.5f);
    facet(c, RAD(b->crown - b->star_drop), qs, support(o, qs) * b->table, zt, true);
    /* pavilion mains: through the girdle's bottom edge (on a circle they meet in the culet) */
    facet(c, RAD(b->pavilion), psi, support(o, psi), -g, false);
    for (int s = -1; s <= 1; s += 2) {
      float q = psi + RAD(11.25f) * (float)s;
      facet(c, RAD(b->pavilion + b->lower_rise), q, support(o, q), -g, false);
    }
  }
  for (int k = 0; k < b->girdle_n; k++) { /* the girdle */
    float psi = RAD(b->phase) + 2 * PI * (float)k / (float)b->girdle_n + PI / (float)b->girdle_n;
    plane(c, cosf(psi), sinf(psi), 0, support(o, psi));
  }
  /* the culet: a tiny flat at the bottom */
  float deep = 1e9f;
  for (int k = 0; k < 64; k++) {
    float psi = 2 * PI * k / 64.0f, hz = support(o, psi);
    float z = -g - hz * tanf(RAD(b->pavilion));
    if (z < deep) deep = z;
  }
  (void)deep;
  plane(c, 0, 0, -1, g + 1.0f * tanf(RAD(b->pavilion)) * 0.97f);
}

/* a step cut on a polygon outline: rows of facets parallel to each side, the crown in `nc` steps and the pavilion in
 * `np` steps (angle in degrees and the width each step takes), then a table and a keel */
typedef struct step { float angle, width; } step;
static void build_steps(jw_cut *c, const outline *o, float girdle, const step *cs, int nc, const step *ps, int np) {
  float g = girdle * 0.5f;
  /* the sides: each edge of the polygon */
  float psis[32], hs[32];
  int ns = 0;
  for (int i = 0; i < o->n && ns < 32; i++) {
    int j = (i + 1) % o->n;
    float ex = o->x[j] - o->x[i], ey = o->y[j] - o->y[i];
    float psi = atan2f(-ex, ey); /* the outward normal of a counter-clockwise edge */
    psis[ns] = psi, hs[ns] = o->x[i] * cosf(psi) + o->y[i] * sinf(psi), ns++;
  }
  for (int i = 0; i < ns; i++) plane(c, cosf(psis[i]), sinf(psis[i]), 0, hs[i]);
  float w = 0, z = g;
  for (int s = 0; s < nc; s++) {
    for (int i = 0; i < ns; i++) facet(c, RAD(cs[s].angle), psis[i], hs[i] - w, z, true);
    z += cs[s].width * tanf(RAD(cs[s].angle));
    w += cs[s].width;
  }
  plane(c, 0, 0, 1, z); /* the table */
  w = 0, z = -g;
  for (int s = 0; s < np; s++) {
    for (int i = 0; i < ns; i++) facet(c, RAD(ps[s].angle), psis[i], hs[i] - w, z, false);
    z -= ps[s].width * tanf(RAD(ps[s].angle));
    w += ps[s].width;
  }
}

/* ── the polyhedron: corners, live facets, extents ──────────────────── */

static bool solve3(const jw_plane *a, const jw_plane *b, const jw_plane *c, float out[3]) {
  const float *n1 = a->n, *n2 = b->n, *n3 = c->n;
  float cx[3] = { n2[1] * n3[2] - n2[2] * n3[1], n2[2] * n3[0] - n2[0] * n3[2], n2[0] * n3[1] - n2[1] * n3[0] };
  float det = n1[0] * cx[0] + n1[1] * cx[1] + n1[2] * cx[2];
  if (fabsf(det) < 1e-6f) return false;
  float c31[3] = { n3[1] * n1[2] - n3[2] * n1[1], n3[2] * n1[0] - n3[0] * n1[2], n3[0] * n1[1] - n3[1] * n1[0] };
  float c12[3] = { n1[1] * n2[2] - n1[2] * n2[1], n1[2] * n2[0] - n1[0] * n2[2], n1[0] * n2[1] - n1[1] * n2[0] };
  for (int i = 0; i < 3; i++) out[i] = (a->d * cx[i] + b->d * c31[i] + c->d * c12[i]) / det;
  return true;
}

static void finish(jw_cut *c) {
  /* drop duplicate planes */
  for (int i = 0; i < c->nplanes; i++)
    for (int j = c->nplanes - 1; j > i; j--) {
      const jw_plane *a = &c->p[i], *b = &c->p[j];
      if (a->n[0] * b->n[0] + a->n[1] * b->n[1] + a->n[2] * b->n[2] > 0.99999f && fabsf(a->d - b->d) < 1e-5f) {
        c->p[j] = c->p[--c->nplanes];
      }
    }
  /* the corners: every triple of planes meeting inside all the others */
  static float v[4096][3];
  int nv = 0;
  for (int i = 0; i < c->nplanes; i++)
    for (int j = i + 1; j < c->nplanes; j++)
      for (int k = j + 1; k < c->nplanes; k++) {
        float p[3];
        if (!solve3(&c->p[i], &c->p[j], &c->p[k], p)) continue;
        bool in = true;
        for (int m = 0; m < c->nplanes && in; m++) in = c->p[m].n[0] * p[0] + c->p[m].n[1] * p[1] + c->p[m].n[2] * p[2] <= c->p[m].d + 1e-5f;
        if (!in) continue;
        bool dup = false;
        for (int m = 0; m < nv && !dup; m++) dup = fabsf(v[m][0] - p[0]) + fabsf(v[m][1] - p[1]) + fabsf(v[m][2] - p[2]) < 1e-5f;
        if (!dup && nv < 4096) memcpy(v[nv++], p, sizeof p);
      }
  /* keep the planes that are faces (three corners or more on them) */
  int keep = 0;
  for (int i = 0; i < c->nplanes; i++) {
    int on = 0;
    for (int m = 0; m < nv; m++) on += fabsf(c->p[i].n[0] * v[m][0] + c->p[i].n[1] * v[m][1] + c->p[i].n[2] * v[m][2] - c->p[i].d) < 1e-4f;
    if (on >= 3) c->p[keep++] = c->p[i];
  }
  c->nplanes = keep;
  c->radius = 0, c->zmin = 1e9f, c->zmax = -1e9f, c->extent_x = c->extent_y = 0;
  c->nverts = 0;
  for (int m = 0; m < nv; m++) {
    float r = sqrtf(v[m][0] * v[m][0] + v[m][1] * v[m][1] + v[m][2] * v[m][2]);
    if (r > c->radius) c->radius = r;
    if (v[m][2] < c->zmin) c->zmin = v[m][2];
    if (v[m][2] > c->zmax) c->zmax = v[m][2];
    if (fabsf(v[m][0]) > c->extent_x) c->extent_x = fabsf(v[m][0]);
    if (fabsf(v[m][1]) > c->extent_y) c->extent_y = fabsf(v[m][1]);
    if (c->nverts < 256) memcpy(c->verts[c->nverts++], v[m], sizeof v[m]);
  }
}

/* the size on screen: every kind covers about the same area (a 100 px round), none reaches past 56 px from its
 * centre, so a triangle does not read smaller than a round */
static void size_for(jw_cut *c, float area) {
  float d = sqrtf(3.14159f * 50.0f * 50.0f / area);
  float reach = fmaxf(c->extent_x, c->extent_y);
  if (d * reach > 56.0f) d = 56.0f / reach;
  c->display = d;
}

static void optics(jw_cut *c, float ior, float disp, float fire, float sr, float sg, float sb, float br, float bg, float bb) {
  c->ior = ior, c->dispersion = disp, c->fire = fire;
  c->sigma[0] = sr, c->sigma[1] = sg, c->sigma[2] = sb;
  c->body[0] = br, c->body[1] = bg, c->body[2] = bb;
}

void jw_cuts_build(void) {
  static outline o;
  memset(jw_cuts, 0, sizeof jw_cuts);
  jw_cut *c;

  /* 1 ruby: a princess, set on its point (a square turned 45 degrees), stepped chevrons below */
  c = &jw_cuts[0];
  c->name = "princess", c->stone = "ruby";
  {
    float k = 0.06f; /* the corners, clipped */
    float sq[] = { 1 - k, 0, 1, k, 1, 1 - k, 1 - k, 1, k, 1, 0, 1 - k, 0, k, k, 0 };
    float xy[16];
    for (int i = 0; i < 8; i++) { /* centre it, turn it 45 degrees, half-diagonal 1 */
      float x = (sq[i * 2] - 0.5f) * 1.41421f, y = (sq[i * 2 + 1] - 0.5f) * 1.41421f;
      xy[i * 2] = (x - y) * 0.70711f, xy[i * 2 + 1] = (x + y) * 0.70711f;
    }
    outline_poly(&o, xy, 8);
    step crown[] = { { 40, 0.10f }, { 27, 0.13f } };
    step pav[] = { { 60, 0.07f }, { 54, 0.07f }, { 48, 0.08f }, { 44, 0.10f }, { 41, 0.70f } };
    build_steps(c, &o, 0.03f, crown, 2, pav, 5);
  }
  finish(c);
  size_for(c, outline_area(&o));
  optics(c, 1.762f, 0.018f, 1.6f, 0.10f, 5.0f, 4.0f, 0.62f, 0.02f, 0.05f);

  /* 2 topaz: a trillion, point up, clipped corners */
  c = &jw_cuts[1];
  c->name = "trillion", c->stone = "topaz";
  {
    float xy[12];
    for (int i = 0; i < 3; i++) {
      float a = PI * 0.5f + 2 * PI * i / 3.0f, a0 = a - 0.16f, a1 = a + 0.16f;
      float r = 1.0f, rc = 0.90f;
      (void)r;
      xy[i * 4] = rc * cosf(a0), xy[i * 4 + 1] = rc * sinf(a0);
      xy[i * 4 + 2] = rc * cosf(a1), xy[i * 4 + 3] = rc * sinf(a1);
    }
    outline_poly(&o, xy, 6);
    step crown[] = { { 38, 0.12f }, { 24, 0.10f } };
    step pav[] = { { 52, 0.10f }, { 44, 0.12f }, { 40, 0.6f } };
    build_steps(c, &o, 0.03f, crown, 2, pav, 3);
  }
  finish(c);
  size_for(c, outline_area(&o));
  optics(c, 1.62f, 0.014f, 1.5f, 0.04f, 0.45f, 3.0f, 0.95f, 0.45f, 0.04f);

  /* 3 emerald: the emerald cut, a tall rectangle with cut corners, three steps above and below: a hall of mirrors */
  c = &jw_cuts[2];
  c->name = "emerald cut", c->stone = "emerald";
  {
    float hw = 0.74f, hh = 1.0f, k = 0.2f;
    float xy[] = { hw, -hh + k, hw, hh - k, hw - k, hh, -hw + k, hh, -hw, hh - k, -hw, -hh + k, -hw + k, -hh, hw - k, -hh };
    outline_poly(&o, xy, 8);
    step crown[] = { { 48, 0.07f }, { 36, 0.08f }, { 24, 0.08f } };
    step pav[] = { { 60, 0.10f }, { 52, 0.12f }, { 46, 0.12f }, { 42, 0.6f } };
    build_steps(c, &o, 0.03f, crown, 3, pav, 4);
  }
  finish(c);
  size_for(c, outline_area(&o));
  optics(c, 1.577f, 0.014f, 1.4f, 3.4f, 0.75f, 1.7f, 0.02f, 0.5f, 0.12f);

  /* 4 sapphire: a pear, point up, brilliant-cut */
  c = &jw_cuts[3];
  c->name = "pear", c->stone = "sapphire";
  outline_pear(&o);
  build_brilliant(c, &o, &(brilliant){ .crown = 34, .pavilion = 41, .table = 0.55f, .girdle = 0.03f, .star_drop = 12, .upper_rise = 8,
                                       .lower_rise = 2.5f, .girdle_n = 24, .phase = 112.5f });
  finish(c);
  size_for(c, outline_area(&o));
  optics(c, 1.766f, 0.018f, 1.6f, 4.0f, 1.8f, 0.12f, 0.03f, 0.12f, 0.75f);

  /* 5 amethyst: a marquise, lying across */
  c = &jw_cuts[4];
  c->name = "marquise", c->stone = "amethyst";
  outline_marquise(&o, 1.0f, 0.52f);
  build_brilliant(c, &o, &(brilliant){ .crown = 33, .pavilion = 41, .table = 0.55f, .girdle = 0.03f, .star_drop = 12, .upper_rise = 8,
                                       .lower_rise = 2.5f, .girdle_n = 24, .phase = 0 });
  finish(c);
  size_for(c, outline_area(&o));
  optics(c, 1.544f, 0.013f, 1.5f, 0.9f, 3.2f, 0.35f, 0.36f, 0.06f, 0.62f);

  /* 6 diamond: the round brilliant, Tolkowsky's proportions (crown 34.5, pavilion 40.75, table 53%) */
  c = &jw_cuts[5];
  c->name = "round brilliant", c->stone = "diamond";
  outline_circle(&o);
  build_brilliant(c, &o, &(brilliant){ .crown = 34.5f, .pavilion = 40.75f, .table = 0.53f, .girdle = 0.03f, .star_drop = 13, .upper_rise = 7.5f,
                                       .lower_rise = 1.5f, .girdle_n = 16, .phase = 0 });
  finish(c);
  size_for(c, outline_area(&o));
  optics(c, 2.417f, 0.044f, 1.0f, 0.012f, 0.012f, 0.016f, 0.9f, 0.92f, 0.95f);

  /* 7 the shard: an irregular fragment for the bursts (unit size; the burst scales it) */
  c = &jw_cuts[JW_CUT_SHARD];
  c->name = "shard", c->stone = "";
  {
    static const float dirs[][4] = { { 0.9f, 0.3f, 0.5f, 0.55f }, { -0.7f, 0.6f, 0.4f, 0.6f }, { -0.3f, -0.9f, 0.4f, 0.5f }, { 0.5f, -0.6f, 0.7f, 0.6f },
                                     { 0.2f, 0.1f, 1.0f, 0.42f }, { 0.7f, 0.5f, -0.6f, 0.6f }, { -0.6f, -0.2f, -0.8f, 0.5f }, { 0.1f, -0.8f, -0.6f, 0.55f },
                                     { -0.9f, 0.4f, -0.3f, 0.58f }, { 0.95f, -0.35f, -0.1f, 0.52f } };
    for (int i = 0; i < 10; i++) plane(c, dirs[i][0], dirs[i][1], dirs[i][2], dirs[i][3] * sqrtf(dirs[i][0] * dirs[i][0] + dirs[i][1] * dirs[i][1] + dirs[i][2] * dirs[i][2]));
  }
  finish(c);
  c->display = 1;
  optics(c, 1.7f, 0.02f, 1.5f, 0.5f, 0.5f, 0.5f, 1, 1, 1);
}

/* ── optics ──────────────────────────────────────────────────────────── */

float jw_ior_at(const jw_cut *c, float nm) {
  /* Cauchy: n = A + B / l^2, B from the dispersion between the B (686.7) and G (430.8) Fraunhofer lines, scaled by
   * the game's fire */
  float B = c->dispersion * c->fire / (1.0f / (430.8f * 430.8f) - 1.0f / (686.7f * 686.7f));
  float A = c->ior - B / (589.3f * 589.3f);
  return A + B / (nm * nm);
}

static float lobe(float x, float mu, float s1, float s2) {
  float t = (x - mu) / (x < mu ? s1 : s2);
  return expf(-0.5f * t * t);
}
/* Wyman, Sloan and Shirley's multi-lobe fit of the CIE 1931 2-degree observer, to linear sRGB, clamped, scaled so a
 * flat spectrum over 400-700 nm is white */
static void spectrum_raw(float nm, float out[3]) {
  float X = 1.056f * lobe(nm, 599.8f, 37.9f, 31.0f) + 0.362f * lobe(nm, 442.0f, 16.0f, 26.7f) - 0.065f * lobe(nm, 501.1f, 20.4f, 26.2f);
  float Y = 0.821f * lobe(nm, 568.8f, 46.9f, 40.5f) + 0.286f * lobe(nm, 530.9f, 16.3f, 31.1f);
  float Z = 1.217f * lobe(nm, 437.0f, 11.8f, 36.0f) + 0.681f * lobe(nm, 459.0f, 26.0f, 13.8f);
  out[0] = fmaxf(0, 3.2406f * X - 1.5372f * Y - 0.4986f * Z);
  out[1] = fmaxf(0, -0.9689f * X + 1.8758f * Y + 0.0415f * Z);
  out[2] = fmaxf(0, 0.0557f * X - 0.2040f * Y + 1.0570f * Z);
}
static float spec_norm[3];
void jw_spectrum_rgb(float nm, float out[3]) {
  if (!spec_norm[0]) {
    float s[3] = { 0, 0, 0 }, t[3];
    int n = 0;
    for (float l = 400; l <= 700; l += 0.5f, n++) {
      spectrum_raw(l, t);
      for (int i = 0; i < 3; i++) s[i] += t[i];
    }
    for (int i = 0; i < 3; i++) spec_norm[i] = (float)n / s[i];
  }
  spectrum_raw(nm, out);
  for (int i = 0; i < 3; i++) out[i] *= spec_norm[i];
}

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* unpolarised Fresnel reflectance going from index n1 into n2 at cosine cosi; 1 past the critical angle */
static float fresnel(float cosi, float n1, float n2) {
  float sint = n1 / n2 * sqrtf(fmaxf(0, 1 - cosi * cosi));
  if (sint >= 1) return 1;
  float cost = sqrtf(1 - sint * sint);
  float rs = (n1 * cosi - n2 * cost) / (n1 * cosi + n2 * cost), rp = (n2 * cosi - n1 * cost) / (n2 * cosi + n1 * cost);
  return 0.5f * (rs * rs + rp * rp);
}
/* refract unit d through a surface of unit normal n (facing against d), eta = n1 / n2; false on total reflection */
static bool refract3(const float *d, const float *n, float eta, float *out) {
  float c = -dot3(n, d), k = 1 - eta * eta * (1 - c * c);
  if (k < 0) return false;
  float s = eta * c - sqrtf(k);
  for (int i = 0; i < 3; i++) out[i] = eta * d[i] + s * n[i];
  return true;
}

bool jw_trace_cpu(const jw_cut *c, const float o[3], const float d[3], float nm, int bounces, jw_exit_fn exit, void *user) {
  /* the entry: the last plane the ray enters through, before the first it leaves through */
  float tn = -1e30f, tf = 1e30f;
  int in = -1;
  for (int i = 0; i < c->nplanes; i++) {
    float den = dot3(c->p[i].n, d), s = dot3(c->p[i].n, o) - c->p[i].d;
    if (fabsf(den) < 1e-9f) {
      if (s > 0) return false;
      continue;
    }
    float t = -s / den;
    if (den < 0) {
      if (t > tn) tn = t, in = i;
    } else if (t < tf) tf = t;
  }
  if (in < 0 || tn > tf || tf < 0) return false;
  float n = jw_ior_at(c, nm);
  float p[3] = { o[0] + d[0] * tn, o[1] + d[1] * tn, o[2] + d[2] * tn };
  const float *nn = c->p[in].n;
  float cosi = -dot3(nn, d), F = fresnel(cosi, 1, n);
  float r[3] = { d[0] + 2 * cosi * nn[0], d[1] + 2 * cosi * nn[1], d[2] + 2 * cosi * nn[2] };
  float w[3] = { F, F, F };
  if (exit) exit(p, r, w, user);
  float dir[3];
  if (!refract3(d, nn, 1 / n, dir)) return true;
  float thr = 1 - F, path = 0;
  for (int b = 0; b < bounces && thr > 0.002f; b++) {
    float te = 1e30f;
    int ie = -1;
    for (int i = 0; i < c->nplanes; i++) {
      float den = dot3(c->p[i].n, dir);
      if (den <= 1e-7f) continue;
      float t = (c->p[i].d - dot3(c->p[i].n, p)) / den;
      if (t < te) te = t, ie = i;
    }
    if (ie < 0) break;
    te = fmaxf(te, 0);
    path += te;
    for (int i = 0; i < 3; i++) p[i] += dir[i] * te;
    const float *en = c->p[ie].n;
    float ce = dot3(en, dir);
    float Fe = fresnel(ce, n, 1);
    if (Fe < 1) {
      float neg[3] = { -en[0], -en[1], -en[2] }, out[3];
      if (refract3(dir, neg, n, out)) {
        float ww[3];
        for (int k = 0; k < 3; k++) ww[k] = thr * (1 - Fe) * expf(-c->sigma[k] * path);
        if (exit) exit(p, out, ww, user);
      }
    }
    thr *= Fe;
    for (int i = 0; i < 3; i++) dir[i] -= 2 * ce * en[i];
  }
  return true;
}
