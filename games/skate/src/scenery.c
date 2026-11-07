/* skate scenery: each park's setting out to the horizon, and its mood. Presentation only: nothing here is ridden, hit
 * or read by the simulation (the parks' own triangles are park.c's).
 *
 *   Sunnyside    a beach town: the boardwalk along the park's east fence, the sand and the sea past it, huts,
 *                umbrellas and lifeguard towers on the beach, pastel low-rise streets to the west, hills behind them;
 *                golden hour, the sun low over the land, long shadows across the park.
 *   Civic Plaza  a city block: streets with lanes, crossings and parked cars round the plaza, sidewalks with trees and
 *                lamps, mid-rise facades with shopfronts and awnings, towers further out, a skyline to the horizon;
 *                a bright hazy morning, soft shadows.
 *   Big Sky      an event out in the open: golden grassland, a gravel car park of cars and vans, tents and bleachers by
 *                the mega ramp, mesas and mountains far off; clear noon light under a deep sky.
 *   Rooftops     downtown at sunset: the street runs on past both fences, taller buildings close in north and south
 *                with lit windows and neon blade signs, a skyline all round; the sun on the horizon, warm and long.
 *   Timber Yard  a lumber yard: a gravel yard of board stacks and log piles, a sawmill shed of corrugated sheet, pine
 *                forest on the hills round it, mountains behind; an early morning, gold light through cool air.
 *
 * How it is drawn. The near things are merged into one mesh per surface (facades, brick, glass, roofs, trim, paint,
 * steel, wood, sheet metal, lumber, car paint, the lights' glow): a few draws for a whole town, each a texture from
 * gfx.c with world-scaled coordinates, so a window is a window's size on every building. Forests are instanced. The
 * ground is one radial mesh round the park, flat near it and rising into hills and mountains far off. The far skyline
 * is an impostor: a ring of towers painted on a band (alpha cut) at 650 m, in the haze. Height fog with the sun's
 * scatter gives the distance its air. */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "view.h"

/* ── noise (presentation only: plain floats are fine, but keep to cw_ math so every host draws the same) ── */
static uint32_t sh(int x, int y, uint32_t s) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float sh01(int x, int y, uint32_t s) { return (float)(sh(x, y, s) & 0xffffff) / 16777216.0f; }
static float snoise(float x, float y, uint32_t s) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = x - (float)x0, fy = y - (float)y0;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = sh01(x0, y0, s), b = sh01(x0 + 1, y0, s), c = sh01(x0, y0 + 1, s), d = sh01(x0 + 1, y0 + 1, s);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float sfbm(float x, float y, int oct, uint32_t s) {
  float sum = 0, amp = 0.5f, norm = 0;
  for (int o = 0; o < oct; o++) sum += amp * snoise(x, y, s + (uint32_t)o * 31u), norm += amp, amp *= 0.5f, x *= 2.03f, y *= 2.03f;
  return sum / norm;
}
static float sstep(float a, float b, float x) {
  float t = clampf((x - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
}
static uint32_t RNG;
static float rnd(void) {
  RNG = RNG * 1664525u + 1013904223u;
  return (float)(RNG >> 8) / 16777216.0f;
}
static float rr(float a, float b) { return a + (b - a) * rnd(); }

/* ── the merged meshes ─────────────────────────────────────────────────── */
enum { SB_FACADE, SB_BRICK, SB_CURTAIN, SB_ROOF, SB_CONC, SB_TILE, SB_ASPH, SB_PAINT, SB_METAL, SB_WOOD, SB_CORR, SB_LUMBER, SB_CAR, SB_GLOW, SB_LEAF,
       SB_SHOP, SB_FOLIAGE, SB_COUNT };
static mesh_build SB[SB_COUNT];
static float TU[SB_COUNT] = { 32, 6, 12, 3, 4, 4.8f, 4, 2, 1, 2.4f, 2, 2.4f, 1, 1, 2, 12, 1 }; /* metres a texture tile spans across */
static float TV[SB_COUNT] = { 28, 6, 14, 3, 4, 4.8f, 4, 2, 1, 2.4f, 2, 2.4f, 1, 1, 2, 14, 1 }; /* and up */

static const scenery_mood *MOOD;

/* a box, turned by yaw about y, its sides mapped in metres along the wall (u) and up (v), its top in plan; uo offsets
 * the wall's u so neighbours do not repeat the same windows */
static void wbox(int sb, v3 c, float yaw, v3 h, const float col[3], float uo, bool top, bool bottom) {
  mesh_build *b = &SB[sb];
  float s, co;
  cw_sincos(yaw, &s, &co);
  v3 R = V(co, 0, -s), F = V(s, 0, co);
  t3_color l = lin(col[0], col[1], col[2]);
  float tu = TU[sb], tv = TV[sb];
  /* sides: +R, -R, +F, -F */
  for (int f = 0; f < 4; f++) {
    v3 n = f == 0 ? R : f == 1 ? vmul(R, -1) : f == 2 ? F : vmul(F, -1);
    v3 t = f == 0 ? F : f == 1 ? vmul(F, -1) : f == 2 ? vmul(R, -1) : R;
    float hn = f < 2 ? h.x : h.z, ht = f < 2 ? h.z : h.x;
    v3 o = vadd(c, vmul(n, hn));
    float u0 = uo + (f == 1 || f == 3 ? 40 : 0);
    v3 P[4] = { vadd(o, vadd(vmul(t, -ht), V(0, -h.y, 0))), vadd(o, vadd(vmul(t, ht), V(0, -h.y, 0))), vadd(o, vadd(vmul(t, ht), V(0, h.y, 0))),
                vadd(o, vadd(vmul(t, -ht), V(0, h.y, 0))) };
    int v[4];
    for (int i = 0; i < 4; i++) {
      float along = (i == 1 || i == 2) ? 2 * ht : 0, up = (i >= 2) ? P[i].y : P[0].y;
      v[i] = mb_vert(b, P[i], n, (u0 + along) / tu, up / tv);
      mb_color(b, v[i], l.r, l.g, l.b);
    }
    mb_quad(b, v[0], v[3], v[2], v[1]);
  }
  for (int f = 0; f < 2; f++) {
    if ((f == 0 && !top) || (f == 1 && !bottom)) continue;
    float y = f == 0 ? c.y + h.y : c.y - h.y;
    v3 n = V(0, f == 0 ? 1 : -1, 0);
    v3 P[4] = { vadd(V(c.x, y, c.z), vadd(vmul(R, -h.x), vmul(F, -h.z))), vadd(V(c.x, y, c.z), vadd(vmul(R, h.x), vmul(F, -h.z))),
                vadd(V(c.x, y, c.z), vadd(vmul(R, h.x), vmul(F, h.z))), vadd(V(c.x, y, c.z), vadd(vmul(R, -h.x), vmul(F, h.z))) };
    int v[4];
    for (int i = 0; i < 4; i++) v[i] = mb_vert(b, P[i], n, P[i].x / tu, P[i].z / tu), mb_color(b, v[i], l.r, l.g, l.b);
    if (f == 0) mb_quad(b, v[0], v[3], v[2], v[1]);
    else mb_quad(b, v[0], v[1], v[2], v[3]);
  }
}
/* a flat quad on the ground (lines, crossings, sidewalk slabs' tops): x0 z0 x1 z1 at height y */
static void flat(int sb, float x0, float z0, float x1, float z1, float y, const float col[3]) {
  mesh_build *b = &SB[sb];
  t3_color l = lin(col[0], col[1], col[2]);
  v3 n = V(0, 1, 0);
  int v[4] = { mb_vert(b, V(x0, y, z0), n, x0 / TU[sb], z0 / TU[sb]), mb_vert(b, V(x1, y, z0), n, x1 / TU[sb], z0 / TU[sb]), mb_vert(b, V(x1, y, z1), n, x1 / TU[sb], z1 / TU[sb]),
               mb_vert(b, V(x0, y, z1), n, x0 / TU[sb], z1 / TU[sb]) };
  for (int i = 0; i < 4; i++) mb_color(b, v[i], l.r, l.g, l.b);
  mb_quad(b, v[0], v[3], v[2], v[1]);
}
/* a cylinder (poles, tanks, logs): from a to c, radius r */
static void cyl(int sb, v3 a, v3 c, float r, int sides, const float col[3], bool caps) {
  mesh_build *b = &SB[sb];
  t3_color l = lin(col[0], col[1], col[2]);
  v3 ax = vnorm(vsub(c, a)), p = fabsf(ax.y) < 0.9f ? V(0, 1, 0) : V(1, 0, 0);
  v3 e1 = vnorm(vcross(ax, p)), e2 = vcross(ax, e1);
  float len = vlen(vsub(c, a));
  for (int i = 0; i < sides; i++) {
    float a0 = 2 * PI_F * (float)i / (float)sides, a1 = 2 * PI_F * (float)(i + 1) / (float)sides, s0, c0, s1, c1;
    cw_sincos(a0, &s0, &c0), cw_sincos(a1, &s1, &c1);
    v3 n0 = vadd(vmul(e1, c0), vmul(e2, s0)), n1 = vadd(vmul(e1, c1), vmul(e2, s1));
    float u0 = (float)i / (float)sides * 2 * PI_F * r / TU[sb], u1 = (float)(i + 1) / (float)sides * 2 * PI_F * r / TU[sb];
    int v[4] = { mb_vert(b, vmad(a, n0, r), n0, u0, 0), mb_vert(b, vmad(a, n1, r), n1, u1, 0), mb_vert(b, vmad(c, n1, r), n1, u1, len / TV[sb]),
                 mb_vert(b, vmad(c, n0, r), n0, u0, len / TV[sb]) };
    for (int k = 0; k < 4; k++) mb_color(b, v[k], l.r, l.g, l.b);
    mb_quad(b, v[0], v[1], v[2], v[3]);
    if (caps)
      for (int e = 0; e < 2; e++) {
        v3 o = e ? c : a, n = e ? ax : vmul(ax, -1);
        int m = mb_vert(b, o, n, 0.5f, 0.5f), q0 = mb_vert(b, vmad(o, n0, r), n, c0 * 0.5f + 0.5f, s0 * 0.5f + 0.5f), q1 = mb_vert(b, vmad(o, n1, r), n, c1 * 0.5f + 0.5f, s1 * 0.5f + 0.5f);
        mb_color(b, m, l.r * 0.9f, l.g * 0.9f, l.b * 0.9f), mb_color(b, q0, l.r, l.g, l.b), mb_color(b, q1, l.r, l.g, l.b);
        if (e) mb_tri(b, m, q0, q1);
        else mb_tri(b, m, q1, q0);
      }
  }
}
/* a cone (tent and umbrella tops, roofs of huts): apex over the centre of a ring of radius r at y */
static void cone(int sb, v3 base, float r, float hgt, int sides, const float col_a[3], const float col_b[3]) {
  mesh_build *b = &SB[sb];
  for (int i = 0; i < sides; i++) {
    const float *col = (i & 1) ? col_b : col_a;
    t3_color l = lin(col[0], col[1], col[2]);
    float a0 = 2 * PI_F * (float)i / (float)sides, a1 = 2 * PI_F * (float)(i + 1) / (float)sides, s0, c0, s1, c1;
    cw_sincos(a0, &s0, &c0), cw_sincos(a1, &s1, &c1);
    v3 p0 = V(base.x + c0 * r, base.y, base.z + s0 * r), p1 = V(base.x + c1 * r, base.y, base.z + s1 * r), ap = V(base.x, base.y + hgt, base.z);
    v3 n = vnorm(vcross(vsub(p1, ap), vsub(p0, ap)));
    int v0 = mb_vert(b, p0, n, 0, 0), v1 = mb_vert(b, p1, n, 1, 0), v2 = mb_vert(b, ap, n, 0.5f, 1);
    mb_color(b, v0, l.r, l.g, l.b), mb_color(b, v1, l.r, l.g, l.b), mb_color(b, v2, l.r, l.g, l.b);
    mb_tri(b, v0, v2, v1);
    /* the underside, darker */
    int w0 = mb_vert(b, p0, V(0, -1, 0), 0, 0), w1 = mb_vert(b, p1, V(0, -1, 0), 1, 0), w2 = mb_vert(b, V(base.x, base.y + hgt * 0.6f, base.z), V(0, -1, 0), 0.5f, 1);
    mb_color(b, w0, l.r * 0.5f, l.g * 0.5f, l.b * 0.5f), mb_color(b, w1, l.r * 0.5f, l.g * 0.5f, l.b * 0.5f), mb_color(b, w2, l.r * 0.5f, l.g * 0.5f, l.b * 0.5f);
    mb_tri(b, w0, w1, w2);
  }
}

/* ── colours (sRGB) ───────────────────────────────────────────────────── */
static const float WHITE[3] = { 0.95f, 0.95f, 0.93f }, OFFWHITE[3] = { 0.88f, 0.86f, 0.82f }, DARK[3] = { 0.12f, 0.12f, 0.13f };
static const float STEEL[3] = { 0.62f, 0.64f, 0.68f }, POLE[3] = { 0.24f, 0.26f, 0.3f }, ASPH[3] = { 0.36f, 0.36f, 0.38f };
static const float LINE_W[3] = { 0.95f, 0.95f, 0.92f }, LINE_Y[3] = { 1.0f, 0.78f, 0.12f }, CURB[3] = { 0.78f, 0.77f, 0.75f };
static const float PASTEL[8][3] = { { 1.0f, 0.78f, 0.62f }, { 0.62f, 0.86f, 0.9f }, { 1.0f, 0.92f, 0.62f }, { 0.96f, 0.68f, 0.72f },
                                    { 0.72f, 0.9f, 0.7f }, { 0.98f, 0.98f, 0.94f }, { 0.78f, 0.74f, 0.96f }, { 1.0f, 0.7f, 0.5f } };
static const float CITY[8][3] = { { 0.86f, 0.8f, 0.7f }, { 0.7f, 0.72f, 0.76f }, { 0.92f, 0.88f, 0.8f }, { 0.78f, 0.62f, 0.5f },
                                  { 0.6f, 0.66f, 0.72f }, { 0.94f, 0.92f, 0.88f }, { 0.82f, 0.72f, 0.62f }, { 0.68f, 0.74f, 0.7f } };
static const float BRICKS[4][3] = { { 0.78f, 0.42f, 0.3f }, { 0.86f, 0.62f, 0.44f }, { 0.62f, 0.34f, 0.26f }, { 0.9f, 0.8f, 0.64f } };
static const float CARS[10][3] = { { 0.9f, 0.12f, 0.1f }, { 0.12f, 0.35f, 0.85f }, { 0.95f, 0.95f, 0.95f }, { 0.12f, 0.12f, 0.14f }, { 0.75f, 0.76f, 0.78f },
                                   { 1.0f, 0.75f, 0.1f }, { 0.2f, 0.6f, 0.35f }, { 0.55f, 0.1f, 0.15f }, { 0.1f, 0.65f, 0.75f }, { 1.0f, 0.45f, 0.1f } };
static const float NEON[6][3] = { { 1.0f, 0.2f, 0.6f }, { 0.2f, 0.9f, 1.0f }, { 1.0f, 0.6f, 0.1f }, { 0.6f, 0.3f, 1.0f }, { 0.3f, 1.0f, 0.4f }, { 1.0f, 0.9f, 0.3f } };

/* ── pieces ───────────────────────────────────────────────────────────── */

/* a city building: facade (or brick, or glass) walls, a shopfront band with an awning, a cornice, a roof with its
 * parapet and its plant; yaw turns it, its front (shops) faces +F */
static void building(float x, float z, float yaw, float w, float d, float h, int style, const float col[3], bool shops, float uo) {
  int wall = style == 0 ? SB_FACADE : style == 1 ? SB_BRICK : SB_CURTAIN;
  float s, c;
  cw_sincos(yaw, &s, &c);
  v3 F = V(s, 0, c), R = V(c, 0, -s);
  float base = shops ? 4.0f : 0.0f;
  if (shops) {
    /* the ground floor: shop glass set back a little, piers between, an awning over each shop */
    wbox(SB_SHOP, V(x, 2.0f, z), yaw, V(w * 0.5f - 0.2f, 2.0f, d * 0.5f - 0.2f), WHITE, uo, false, false);
    int n = (int)(w / 7.0f);
    for (int i = 0; i <= n; i++) {
      float t = -w * 0.5f + w * (float)i / (float)(n > 0 ? n : 1);
      v3 p = vadd(V(x, 2.0f, z), vadd(vmul(R, t), vmul(F, d * 0.5f - 0.05f)));
      wbox(SB_CONC, p, yaw, V(0.35f, 2.0f, 0.3f), OFFWHITE, 0, false, false);
      if (i < n) {
        const float *ac = PASTEL[(int)(rnd() * 8) & 7];
        float aw = w / (float)n * 0.5f - 0.5f;
        v3 q = vadd(V(x, 3.55f, z), vadd(vmul(R, t + aw + 0.5f), vmul(F, d * 0.5f + 0.7f)));
        wbox(SB_PAINT, q, yaw, V(aw, 0.12f, 0.75f), ac, 0, true, true);
      }
    }
    wbox(SB_CONC, V(x, 4.1f, z), yaw, V(w * 0.5f + 0.15f, 0.12f, d * 0.5f + 0.15f), OFFWHITE, 0, false, true);
  }
  float hh = (h - base) * 0.5f;
  wbox(wall, V(x, base + hh, z), yaw, V(w * 0.5f, hh, d * 0.5f), col, uo, false, false);
  if (style == 0 && shops) {
    /* depth on the near facades: a sill course at every floor and piers between the bays, standing out of the wall */
    for (float y = 7.0f; y < h - 1.0f; y += 3.5f)
      for (int e = 0; e < 2; e++) {
        v3 n = e ? F : vmul(F, -1);
        wbox(SB_CONC, vadd(V(x, y + 0.12f, z), vmul(n, d * 0.5f + 0.06f)), yaw, V(w * 0.5f, 0.1f, 0.08f), OFFWHITE, 0, true, true);
      }
    for (float a = -w * 0.5f + 4.0f; a < w * 0.5f - 1.0f; a += 8.0f)
      for (int e = 0; e < 2; e++) {
        v3 n = e ? F : vmul(F, -1);
        wbox(SB_FACADE, vadd(V(x, base + hh, z), vadd(vmul(n, d * 0.5f + 0.1f), vmul(R, a))), yaw, V(0.35f, hh, 0.1f), col, uo + a, false, false);
      }
  }
  /* the cornice and the roof, its parapet and its plant */
  wbox(SB_CONC, V(x, h + 0.25f, z), yaw, V(w * 0.5f + 0.3f, 0.25f, d * 0.5f + 0.3f), OFFWHITE, 0, false, true);
  wbox(SB_ROOF, V(x, h + 0.45f, z), yaw, V(w * 0.5f, 0.02f, d * 0.5f), OFFWHITE, 0, true, false);
  for (int e = 0; e < 4; e++) {
    v3 n = e == 0 ? R : e == 1 ? vmul(R, -1) : e == 2 ? F : vmul(F, -1);
    float off = e < 2 ? w * 0.5f : d * 0.5f, len = e < 2 ? d * 0.5f : w * 0.5f;
    v3 p = vadd(V(x, h + 0.85f, z), vmul(n, off + 0.12f));
    wbox(SB_CONC, p, yaw + (e < 2 ? 0 : PI_F * 0.5f), V(0.15f, 0.4f, len + 0.25f), OFFWHITE, 0, true, false);
  }
  int units = 1 + (int)(rnd() * 3);
  for (int i = 0; i < units; i++) {
    v3 p = vadd(V(x, h + 1.2f, z), vadd(vmul(R, rr(-w * 0.3f, w * 0.3f)), vmul(F, rr(-d * 0.3f, d * 0.3f))));
    wbox(SB_METAL, p, yaw, V(rr(0.8f, 1.6f), 0.75f, rr(0.8f, 1.4f)), STEEL, 0, true, false);
  }
  if (style == 1 && rnd() < 0.5f) { /* a water tank on legs */
    v3 p = vadd(V(x, h + 0.45f, z), vadd(vmul(R, w * 0.2f), vmul(F, -d * 0.2f)));
    static const float tank[3] = { 0.55f, 0.4f, 0.28f };
    for (int l = 0; l < 4; l++) {
      float a = (float)l * PI_F * 0.5f + 0.78f, sa, ca;
      cw_sincos(a, &sa, &ca);
      cyl(SB_METAL, vadd(p, V(ca * 1.3f, 0, sa * 1.3f)), vadd(p, V(ca * 1.3f, 3.0f, sa * 1.3f)), 0.08f, 5, POLE, false);
    }
    cyl(SB_WOOD, vadd(p, V(0, 3.0f, 0)), vadd(p, V(0, 6.0f, 0)), 1.7f, 12, tank, true);
    cone(SB_METAL, vadd(p, V(0, 6.0f, 0)), 1.8f, 1.0f, 12, POLE, POLE);
  }
}
/* a convex side profile (points along the run f and up y, counter clockwise seen from +R) extruded across +-w about
 * (x, z), each edge its own colour (a windscreen dark, the bonnet painted) */
static void extrude(int sb, float x, float z, float yaw, const float (*P)[2], int n, float w, const float *const *ecol, const float cap[3]) {
  mesh_build *b = &SB[sb];
  float s, c;
  cw_sincos(yaw, &s, &c);
  v3 F = V(s, 0, c), R = V(c, 0, -s), O = V(x, 0, z);
#define PT(i, sd) vadd(vadd(O, vmul(F, P[i][0])), vadd(V(0, P[i][1], 0), vmul(R, (sd) * w)))
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    float df = P[j][0] - P[i][0], dy = P[j][1] - P[i][1], l = sqrtf(df * df + dy * dy);
    if (l < 1e-4f) continue;
    /* outward: the edge turned clockwise in the (f, y) plane */
    v3 nn = vnorm(vadd(vmul(F, dy / l), V(0, -df / l, 0)));
    t3_color cl = lin(ecol[i][0], ecol[i][1], ecol[i][2]);
    int v[4] = { mb_vert(b, PT(i, -1), nn, 0, 0), mb_vert(b, PT(j, -1), nn, 1, 0), mb_vert(b, PT(j, 1), nn, 1, 1), mb_vert(b, PT(i, 1), nn, 0, 1) };
    for (int k = 0; k < 4; k++) mb_color(b, v[k], cl.r, cl.g, cl.b);
    mb_quad(b, v[0], v[3], v[2], v[1]);
  }
  t3_color cc = lin(cap[0], cap[1], cap[2]);
  for (int sd = -1; sd <= 1; sd += 2) {
    v3 nn = vmul(R, (float)sd);
    int first = mb_vert(b, PT(0, sd), nn, 0, 0);
    mb_color(b, first, cc.r, cc.g, cc.b);
    int prev = -1;
    for (int i = 1; i < n; i++) {
      int v = mb_vert(b, PT(i, sd), nn, 0, 0);
      mb_color(b, v, cc.r, cc.g, cc.b);
      if (prev >= 0) {
        if (sd > 0) mb_tri(b, first, v, prev);
        else mb_tri(b, first, prev, v);
      }
      prev = v;
    }
  }
#undef PT
}
/* a car: a body with a sloped bonnet and boot, a glasshouse with raked windscreens and side windows, wheels in dark
 * arches, lamps; a van is taller and square at the back. Along the yaw. */
static void car(float x, float z, float yaw, const float col[3], bool van) {
  float s, c;
  cw_sincos(yaw, &s, &c);
  v3 F = V(s, 0, c), R = V(c, 0, -s);
  static const float glass[3] = { 0.07f, 0.09f, 0.12f }, tyre[3] = { 0.05f, 0.05f, 0.05f }, hub[3] = { 0.7f, 0.72f, 0.75f }, light[3] = { 1.0f, 0.95f, 0.8f },
                     tail[3] = { 0.7f, 0.05f, 0.05f }, trim[3] = { 0.12f, 0.12f, 0.13f };
  const float L = van ? 2.45f : 2.25f, W = 0.88f;
  /* the body: a lower profile, front at +L */
  const float body[6][2] = { { -L, 0.32f }, { L - 0.05f, 0.32f }, { L, 0.62f }, { L - 0.75f, 0.86f }, { -L + 0.45f, 0.9f }, { -L, 0.7f } };
  const float *bc[6] = { trim, trim, col, col, col, col };
  extrude(SB_CAR, x, z, yaw, body, 6, W, bc, col);
  if (van) {
    const float top[4][2] = { { -L + 0.05f, 0.88f }, { L - 0.8f, 0.88f }, { L - 1.3f, 1.95f }, { -L + 0.05f, 1.95f } };
    const float *tc[4] = { col, glass, col, col };
    extrude(SB_CAR, x, z, yaw, top, 4, W - 0.02f, tc, col);
    wbox(SB_CAR, vadd(V(x, 1.5f, z), vmul(F, 0.2f)), yaw, V(W - 0.01f, 0.28f, 1.0f), glass, 0, false, false); /* side windows */
  } else {
    const float cab[4][2] = { { -L + 0.55f, 0.86f }, { L - 0.95f, 0.86f }, { L - 1.6f, 1.38f }, { -L + 1.05f, 1.38f } };
    const float *cc[4] = { col, glass, col, glass };
    extrude(SB_CAR, x, z, yaw, cab, 4, W - 0.1f, cc, glass);
  }
  for (int i = -1; i <= 1; i += 2)
    for (int j = -1; j <= 1; j += 2) {
      v3 p = vadd(V(x, 0.31f, z), vadd(vmul(R, (float)i * (W - 0.1f)), vmul(F, (float)j * (L - 0.62f))));
      cyl(SB_CAR, vadd(p, vmul(R, -0.11f)), vadd(p, vmul(R, 0.11f)), 0.31f, 8, tyre, false);
      cyl(SB_CAR, vadd(p, vmul(R, (float)i * 0.1f)), vadd(p, vmul(R, (float)i * 0.115f)), 0.17f, 8, hub, true);
      wbox(SB_GLOW, vadd(V(x, 0.62f, z), vadd(vmul(R, (float)i * 0.6f), vmul(F, (float)j * (L + 0.005f)))), yaw, V(0.17f, 0.06f, 0.02f), j > 0 ? light : tail, 0, false, false);
    }
}
/* a street lamp: a pole, an arm, a head with a glowing lens */
static void street_lamp(float x, float z, float yaw) {
  float s, c;
  cw_sincos(yaw, &s, &c);
  v3 F = V(s, 0, c);
  cyl(SB_METAL, V(x, 0, z), V(x, 6.5f, z), 0.09f, 6, POLE, false);
  v3 head = vadd(V(x, 6.4f, z), vmul(F, 1.3f));
  cyl(SB_METAL, V(x, 6.4f, z), head, 0.05f, 5, POLE, false);
  wbox(SB_METAL, head, yaw, V(0.22f, 0.08f, 0.4f), POLE, 0, true, true);
  static const float lamp[3] = { 1.0f, 0.88f, 0.62f };
  wbox(SB_GLOW, vadd(head, V(0, -0.09f, 0)), yaw, V(0.17f, 0.01f, 0.33f), lamp, 0, false, true);
}
/* a palm: a leaning ringed trunk and drooping fronds */
static void palm(float x, float z, float h, float lean) {
  float s, c;
  cw_sincos(lean, &s, &c);
  static const float bark[3] = { 0.6f, 0.48f, 0.34f }, frond[3] = { 0.3f, 0.6f, 0.2f };
  v3 prev = V(x, 0, z);
  for (int i = 1; i <= 6; i++) {
    float t = (float)i / 6;
    v3 p = V(x + s * 1.4f * t * t, h * t, z + c * 1.4f * t * t);
    cyl(SB_WOOD, prev, p, 0.2f - 0.07f * t, 7, bark, false);
    prev = p;
  }
  mesh_build *b = &SB[SB_LEAF];
  t3_color l = lin(frond[0], frond[1], frond[2]);
  for (int f = 0; f < 10; f++) {
    float a = 2 * PI_F * (float)f / 10 + rr(0, 0.4f), sa, ca;
    cw_sincos(a, &sa, &ca);
    v3 d = V(ca, 0, sa), side = V(-sa, 0, ca);
    float len = rr(2.6f, 3.4f);
    v3 mid = vadd(prev, vadd(vmul(d, len * 0.5f), V(0, 0.4f, 0))), tip = vadd(prev, vadd(vmul(d, len), V(0, -rr(0.6f, 1.2f), 0)));
    v3 n = vnorm(vadd(V(0, 1, 0), vmul(d, 0.2f)));
    int q[5] = { mb_vert(b, prev, n, 0, 0.5f), mb_vert(b, vadd(mid, vmul(side, 0.6f)), n, 0.5f, 0), mb_vert(b, mid, n, 0.5f, 0.5f),
                 mb_vert(b, vadd(mid, vmul(side, -0.6f)), n, 0.5f, 1), mb_vert(b, tip, n, 1, 0.5f) };
    for (int k = 0; k < 5; k++) mb_color(b, q[k], l.r * rr(0.85f, 1.1f), l.g, l.b);
    mb_tri(b, q[0], q[2], q[1]), mb_tri(b, q[0], q[3], q[2]), mb_tri(b, q[1], q[2], q[4]), mb_tri(b, q[2], q[3], q[4]);
  }
}
/* a lumber stack: boards on bearers, lumber sides, sawn ends */
static void lumber_stack(float x, float z, float yaw, float w, float d, float h) {
  static const float bearer[3] = { 0.42f, 0.34f, 0.26f }, endc[3] = { 0.95f, 0.8f, 0.58f }, lum[3] = { 1, 1, 1 };
  wbox(SB_WOOD, V(x, 0.08f, z), yaw, V(w * 0.5f, 0.08f, 0.1f), bearer, 0, true, false);
  float s, c;
  cw_sincos(yaw, &s, &c);
  v3 F = V(s, 0, c);
  wbox(SB_WOOD, vadd(V(x, 0.08f, z), vmul(F, d * 0.4f)), yaw, V(w * 0.5f, 0.08f, 0.1f), bearer, 0, true, false);
  wbox(SB_WOOD, vadd(V(x, 0.08f, z), vmul(F, -d * 0.4f)), yaw, V(w * 0.5f, 0.08f, 0.1f), bearer, 0, true, false);
  wbox(SB_LUMBER, V(x, 0.16f + h * 0.5f, z), yaw, V(w * 0.5f, h * 0.5f, d * 0.5f), lum, rr(0, 10), true, false);
  (void)endc;
}

/* ── the ground ──────────────────────────────────────────────────────── */
typedef struct ground_fn {
  float flat;       /* metres past the park's bounds kept level */
  float hill, mtn;  /* heights of the near hills and the far mountains */
  float mtn_r;      /* where the mountains start */
} ground_fn;
static v3 G_LO, G_HI;
static int G_PARK;
static ground_fn GF;

static float rect_dist(float x, float z) {
  float dx = fmaxf(fmaxf(G_LO.x - x, x - G_HI.x), 0), dz = fmaxf(fmaxf(G_LO.z - z, z - G_HI.z), 0);
  return sqrtf(dx * dx + dz * dz);
}
static float ground_h(float x, float z) {
  float d = rect_dist(x, z);
  if (G_PARK == PARK_SUNNY && x > G_HI.x + 8) { /* the beach falls to the sea */
    float t = x - G_HI.x - 8;
    return -0.05f - 0.008f * t - (t > 60 ? (t - 60) * 0.04f : 0);
  }
  float h = -0.05f;
  float hills = GF.hill * sfbm(x / 260.0f, z / 260.0f, 4, 7u + (uint32_t)G_PARK);
  float m = sfbm(x / 520.0f, z / 520.0f, 5, 19u + (uint32_t)G_PARK);
  float mtn = GF.mtn * sstep(0.35f, 0.85f, m);
  h += hills * sstep(GF.flat, GF.flat + 180, d) + mtn * sstep(GF.mtn_r, GF.mtn_r + 400, d);
  if (G_PARK == PARK_SUNNY && x > G_HI.x - 30) h *= sstep(G_HI.x + 8, G_HI.x - 30, x); /* no hills on the beach side */
  return h;
}
/* what the ground is at (x, z): 0 grass and scrub, 1 sand, 2 street; its colour */
static int ground_zone(float x, float z, float h, float col[3]) {
  float d = rect_dist(x, z), n = sfbm(x / 40.0f, z / 40.0f, 3, 41u), far = sstep(150, 700, d);
  const scenery_mood *m = MOOD;
  int zone = 0;
  for (int i = 0; i < 3; i++) col[i] = m->ground_near[i] + (m->ground_far[i] - m->ground_near[i]) * far;
  for (int i = 0; i < 3; i++) col[i] *= 0.85f + 0.3f * n;
  if (h > 60) for (int i = 0; i < 3; i++) col[i] += (m->rock[i] - col[i]) * sstep(60, 220, h);
  if (G_PARK == PARK_SUNNY && x > G_HI.x + 6) {
    zone = 1;
    static const float dry[3] = { 0.98f, 0.88f, 0.66f }, wet[3] = { 0.78f, 0.66f, 0.48f };
    float t = sstep(G_HI.x + 50, G_HI.x + 64, x);
    for (int i = 0; i < 3; i++) col[i] = (dry[i] + (wet[i] - dry[i]) * t) * (0.95f + 0.1f * n);
  }
  if ((G_PARK == PARK_PLAZA || G_PARK == PARK_ROOFS) && d < 420) {
    zone = 2;
    for (int i = 0; i < 3; i++) col[i] = ASPH[i] * (0.9f + 0.2f * n);
  }
  if ((G_PARK == PARK_BARN || G_PARK == PARK_MEGA) && d < (G_PARK == PARK_BARN ? 90 : 40)) { /* the yard's gravel */
    static const float grav[3] = { 0.52f, 0.47f, 0.4f };
    float t = sstep(G_PARK == PARK_BARN ? 70 : 25, G_PARK == PARK_BARN ? 90 : 40, d);
    for (int i = 0; i < 3; i++) col[i] = grav[i] * (0.9f + 0.2f * n) * (1 - t) + col[i] * t;
  }
  return zone;
}
static void build_ground(cw_node parent, t3_material *mats[3]) {
  v3 c = vmul(vadd(G_LO, G_HI), 0.5f);
  enum { NR = 64, NA = 128 };
  const float RMAX = 1700;
  float r0 = fmaxf(G_HI.x - G_LO.x, G_HI.z - G_LO.z) * 0.75f + 10;
  /* a disc of radius r0 (the park and round it, flat) and rings out from it, spaced wider with the distance */
  static float rad[NR + 1];
  rad[0] = 0;
  for (int i = 1; i <= NR; i++) {
    float t = (float)(i - 1) / (float)(NR - 1);
    rad[i] = i == 1 ? r0 * 0.5f : r0 * 0.5f + (RMAX - r0 * 0.5f) * t * t;
  }
  mesh_build mb[3] = { { 0 }, { 0 }, { 0 } };
  for (int i = 0; i < NR; i++)
    for (int j = 0; j < NA; j++) {
      float a0 = 2 * PI_F * (float)j / NA, a1 = 2 * PI_F * (float)(j + 1) / NA, s0, c0, s1, c1;
      cw_sincos(a0, &s0, &c0), cw_sincos(a1, &s1, &c1);
      v3 P[4] = { V(c.x + c0 * rad[i], 0, c.z + s0 * rad[i]), V(c.x + c1 * rad[i], 0, c.z + s1 * rad[i]), V(c.x + c1 * rad[i + 1], 0, c.z + s1 * rad[i + 1]),
                  V(c.x + c0 * rad[i + 1], 0, c.z + s0 * rad[i + 1]) };
      float col[4][3];
      v3 N[4];
      for (int k = 0; k < 4; k++) {
        P[k].y = ground_h(P[k].x, P[k].z);
        float e = 2.0f;
        N[k] = vnorm(V(ground_h(P[k].x - e, P[k].z) - ground_h(P[k].x + e, P[k].z), 2 * e, ground_h(P[k].x, P[k].z - e) - ground_h(P[k].x, P[k].z + e)));
      }
      /* none under the park (its bowls and pits go below the ground): the park's own ground covers it */
      bool inside = true;
      for (int k = 0; k < 4; k++) inside = inside && P[k].x > G_LO.x + 1 && P[k].x < G_HI.x - 1 && P[k].z > G_LO.z + 1 && P[k].z < G_HI.z - 1;
      if (inside) continue;
      v3 mid = vmul(vadd(vadd(P[0], P[1]), vadd(P[2], P[3])), 0.25f);
      float mc[3];
      int zone = ground_zone(mid.x, mid.z, mid.y, mc);
      for (int k = 0; k < 4; k++) ground_zone(P[k].x, P[k].z, P[k].y, col[k]);
      mesh_build *b = &mb[zone];
      float tile = zone == 1 ? 4.0f : zone == 2 ? 4.0f : 8.0f;
      int v[4];
      for (int k = 0; k < 4; k++) {
        v[k] = mb_vert(b, P[k], N[k], P[k].x / tile, P[k].z / tile);
        t3_color l = lin(clampf(col[k][0], 0, 1), clampf(col[k][1], 0, 1), clampf(col[k][2], 0, 1));
        mb_color(b, v[k], l.r, l.g, l.b);
      }
      if (i == 0) mb_tri(b, v[0], v[2], v[3]);
      else mb_quad(b, v[0], v[1], v[2], v[3]);
    }
  for (int z = 0; z < 3; z++) {
    if (!mb[z].nv) { mb_free(&mb[z]); continue; }
    t3_geometry *g = mb_finish(&mb[z]);
    t3_mesh *me = t3_mesh_new(g, mats[z]);
    me->base.cast_shadow = false, me->base.receive_shadow = false; /* outside the sun's map: no shadow look-ups for it */
    cw_node n = cw_node_new("Node3D", "Ground");
    cw_node_add_child(parent, n);
    cw_node3d_set_object(n, &me->base);
    t3_release(me), t3_release(g);
  }
}

/* ── materials ───────────────────────────────────────────────────────── */
static t3_material *tex_mat(int kind, float rough, float metal, float ns, bool vcol) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->vertex_colors = vcol;
  m->roughness = rough, m->metalness = metal;
  /* the scenery is seen from tens of metres off: its colour map only (no normal, roughness or occlusion maps: three
     texture reads a pixel saved over a whole town, four times over in a four-way split) */
  (void)ns;
  t3_material_set_texture(m, T3_MAP, tex_get(kind)->map);
  return m;
}
static t3_material *sb_material(int sb) {
  t3_material *m;
  switch (sb) {
  case SB_FACADE:
    m = tex_mat(TEX_FACADE, 0.8f, 0, 0.8f, true);
    if (MOOD->windows_lit > 0) {
      t3_material_set_texture(m, T3_EMISSIVE_MAP, tex_get(TEX_FACADE_LIT)->map);
      m->emissive = (t3_color){ 1, 1, 1 };
      m->emissive_intensity = MOOD->windows_lit;
    }
    return m;
  case SB_BRICK:
    m = tex_mat(MAT_BRICK, 1, 0, 1.0f, true);
    return m;
  case SB_CURTAIN: return tex_mat(TEX_CURTAIN, 0.25f, 0.3f, 0.4f, true);
  case SB_SHOP:
    m = tex_mat(TEX_CURTAIN, 0.3f, 0.2f, 0.4f, true);
    m->emissive = lin(1.0f, 0.85f, 0.6f);
    m->emissive_intensity = MOOD->windows_lit > 0 ? 0.25f : 0.05f;
    return m;
  case SB_ROOF: return tex_mat(MAT_ROOF, 1, 0, 1.0f, true);
  case SB_CONC: return tex_mat(MAT_CONCRETE, 1, 0, 0.8f, true);
  case SB_TILE: return tex_mat(MAT_TILE, 1, 0, 0.8f, true);
  case SB_ASPH: return tex_mat(MAT_ASPHALT, 1, 0, 1.0f, true);
  case SB_PAINT: return tex_mat(MAT_PAINT, 1, 0, 0.5f, true);
  case SB_METAL: return tex_mat(MAT_METAL, 0.45f, 0.7f, 0.4f, true);
  case SB_WOOD: return tex_mat(MAT_WOOD, 1, 0, 0.8f, true);
  case SB_CORR: return tex_mat(TEX_CORRUGATED, 1, 0.5f, 1.0f, true);
  case SB_LUMBER: return tex_mat(TEX_LUMBER, 1, 0, 1.0f, true);
  case SB_LEAF:
    m = tex_mat(MAT_LEAF, 1, 0, 0.8f, true);
    m->side = T3_DOUBLE_SIDE;
    return m;
  case SB_FOLIAGE:
    m = t3_mesh_standard_material_new(0xffffff);
    m->vertex_colors = true, m->roughness = 0.85f, m->side = T3_DOUBLE_SIDE, m->alpha_test = 0.5f;
    t3_material_set_texture(m, T3_MAP, tex_get(TEX_FOLIAGE)->map);
    return m;
  case SB_CAR:
    m = t3_mesh_standard_material_new(0xffffff);
    m->vertex_colors = true, m->roughness = 0.28f, m->metalness = 0.35f; /* car paint: the one glossy thing out here */
    return m;
  default: /* SB_GLOW: lights, lenses, neon: unlit */
    m = t3_mesh_basic_material_new(0xffffff);
    m->vertex_colors = true;
    m->color = (t3_color){ MOOD->glow, MOOD->glow, MOOD->glow };
    return m;
  }
}

/* ── the forests (instanced) ─────────────────────────────────────────── */
/* a tree in two parts, each instanced: its wood (a plain material) and its foliage (alpha-cut cards of the foliage
 * texture, their normals pointed out from the crown's middle, so the crown shades like a volume, not like flat cards) */
typedef struct tree_geo { t3_geometry *wood, *leaves; int leaf_tex; } tree_geo;
static void tex_cyl(mesh_build *b, v3 a, v3 c, float r0, float r1, int sides, t3_color l) {
  for (int i = 0; i < sides; i++) {
    float a0 = 2 * PI_F * (float)i / (float)sides, a1 = 2 * PI_F * (float)(i + 1) / (float)sides, s0, c0, s1, c1;
    cw_sincos(a0, &s0, &c0), cw_sincos(a1, &s1, &c1);
    v3 ax = vnorm(vsub(c, a)), p = fabsf(ax.y) < 0.9f ? V(0, 1, 0) : V(1, 0, 0), e1 = vnorm(vcross(ax, p)), e2 = vcross(ax, e1);
    v3 n0 = vadd(vmul(e1, c0), vmul(e2, s0)), n1 = vadd(vmul(e1, c1), vmul(e2, s1));
    int v[4] = { mb_vert(b, vmad(a, n0, r0), n0, 0, 0), mb_vert(b, vmad(a, n1, r0), n1, 1, 0), mb_vert(b, vmad(c, n1, r1), n1, 1, 1), mb_vert(b, vmad(c, n0, r1), n0, 0, 1) };
    for (int k = 0; k < 4; k++) mb_color(b, v[k], l.r, l.g, l.b);
    mb_quad(b, v[0], v[1], v[2], v[3]);
  }
}
/* a card: centre c, its two half axes, its normal; the texture's whole square */
static void card(mesh_build *b, v3 c, v3 ax, v3 ay, v3 n, t3_color l, float shade_lo) {
  v3 P[4] = { vsub(vsub(c, ax), ay), vsub(vadd(c, ax), ay), vadd(vadd(c, ax), ay), vadd(vsub(c, ax), ay) };
  static const float UV[4][2] = { { 0.01f, 0.01f }, { 0.99f, 0.01f }, { 0.99f, 0.99f }, { 0.01f, 0.99f } };
  int v[4];
  for (int i = 0; i < 4; i++) {
    float k = i < 2 ? shade_lo : 1.0f;
    v[i] = mb_vert(b, P[i], n, UV[i][0], UV[i][1]);
    mb_color(b, v[i], l.r * k, l.g * k, l.b * k);
  }
  mb_quad(b, v[0], v[1], v[2], v[3]);
}
static tree_geo broadleaf_geo(void) {
  mesh_build w = { 0 }, f = { 0 };
  t3_color bark = lin(0.42f, 0.32f, 0.24f), leaf = lin(0.62f, 0.85f, 0.45f);
  tex_cyl(&w, V(0, 0, 0), V(0, 3.4f, 0), 0.24f, 0.15f, 6, bark);
  for (int i = 0; i < 3; i++) {
    float a = (float)i * 2.1f, sa, ca;
    cw_sincos(a, &sa, &ca);
    tex_cyl(&w, V(0, 2.6f, 0), V(ca * 1.3f, 4.2f, sa * 1.3f), 0.1f, 0.05f, 5, bark);
  }
  /* the crown: cards on an ellipsoid round (0, 4.7, 0), each crossed by a second at right angles */
  v3 cc = V(0, 4.7f, 0);
  uint32_t h = 71;
  for (int i = 0; i < 16; i++) {
    h = h * 1664525u + 1013904223u;
    float az = (float)(h >> 8) / 16777216.0f * 2 * PI_F;
    h = h * 1664525u + 1013904223u;
    float el = -0.5f + 1.4f * (float)(h >> 8) / 16777216.0f;
    float sa, ca, se, ce;
    cw_sincos(az, &sa, &ca), cw_sincos(el, &se, &ce);
    v3 dir = V(ca * ce, se, sa * ce);
    v3 c = vadd(cc, V(dir.x * 1.7f, dir.y * 1.2f, dir.z * 1.7f));
    v3 n = vnorm(vadd(dir, V(0, 0.3f, 0)));
    v3 t1 = vnorm(vcross(n, V(0, 1, 0)));
    if (vlen(vcross(n, V(0, 1, 0))) < 0.2f) t1 = V(1, 0, 0);
    v3 t2 = vnorm(vcross(t1, n));
    float s = 1.3f + 0.5f * (float)((h >> 4) & 255) / 255.0f;
    float shade = 0.55f + 0.35f * (dir.y * 0.5f + 0.5f);
    card(&f, c, vmul(t1, s), vmul(V(0, 1, 0), s * 0.85f), n, (t3_color){ leaf.r * shade, leaf.g * shade, leaf.b * shade }, 0.7f);
    card(&f, c, vmul(t2, s), vmul(V(0, 1, 0), s * 0.85f), n, (t3_color){ leaf.r * shade, leaf.g * shade, leaf.b * shade }, 0.7f);
  }
  return (tree_geo){ mb_finish(&w), mb_finish(&f), TEX_FOLIAGE };
}
static tree_geo pine_geo(void) {
  mesh_build w = { 0 }, f = { 0 };
  t3_color bark = lin(0.4f, 0.3f, 0.22f), nd = lin(0.55f, 0.8f, 0.55f);
  tex_cyl(&w, V(0, 0, 0), V(0, 12.5f, 0), 0.28f, 0.06f, 6, bark);
  /* tiers of branch cards out from the trunk, shorter up the tree, each tier turned from the last */
  for (int tier = 0; tier < 7; tier++) {
    float y = 2.0f + 1.5f * (float)tier, len = 3.0f - 0.36f * (float)tier;
    int nb = 7 - tier / 3;
    for (int k = 0; k < nb; k++) {
      float a = 2 * PI_F * (float)k / (float)nb + (float)tier * 0.7f, sa, ca;
      cw_sincos(a, &sa, &ca);
      v3 out = V(ca, -0.25f, sa), side = V(-sa, 0, ca);
      v3 c = vadd(V(0, y, 0), vmul(out, len * 0.5f));
      v3 n = vnorm(V(ca * 0.6f, 0.8f, sa * 0.6f));
      /* the card lies along the branch (u from the trunk), tilted up a little across it */
      v3 ax = vmul(vnorm(out), len * 0.5f), ay = vmul(vnorm(vadd(vmul(side, 0.45f), V(0, 1, 0))), len * 0.42f); /* steep: a card lying flat vanishes edge on from the ground */
      float shade = 0.6f + 0.06f * (float)tier;
      card(&f, c, ax, ay, n, (t3_color){ nd.r * shade, nd.g * shade, nd.b * shade }, 0.8f);
    }
  }
  /* the top: two crossed cards upright */
  card(&f, V(0, 12.6f, 0), V(0.7f, 0, 0), V(0, 1.3f, 0), V(0, 1, 0), nd, 0.8f);
  card(&f, V(0, 12.6f, 0), V(0, 0, 0.7f), V(0, 1.3f, 0), V(0, 1, 0), nd, 0.8f);
  return (tree_geo){ mb_finish(&w), mb_finish(&f), TEX_PINECARD };
}
static const float *FOREST_Y; /* the trees' own heights (a planter's top), else the ground's */
static void forest_part(cw_node parent, t3_geometry *g, t3_material *m, const float *xz, int n, uint32_t seed, float scale, bool shadow) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  im->mesh.base.cast_shadow = shadow, im->mesh.base.receive_shadow = true;
  for (int i = 0; i < n; i++) {
    float x = xz[i * 2], z = xz[i * 2 + 1], s = scale * (0.75f + 0.5f * sh01(i, 1, seed)), yaw = 6.28f * sh01(i, 2, seed);
    t3_mat4 mm;
    t3_quat q = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
    t3_mat4_compose(&mm, V(x, FOREST_Y ? FOREST_Y[i] : ground_h(x, z) - 0.2f, z), q, V(s, s * (0.9f + 0.25f * sh01(i, 3, seed)), s));
    t3_instanced_mesh_set_matrix_at(im, i, &mm);
    float k = 0.85f + 0.3f * sh01(i, 4, seed);
    t3_instanced_mesh_set_color_at(im, i, (t3_color){ k * (0.95f + 0.1f * sh01(i, 5, seed)), k, k * (0.9f + 0.15f * sh01(i, 6, seed)) });
  }
  t3_sphere sph = { { (G_LO.x + G_HI.x) * 0.5f, 0, (G_LO.z + G_HI.z) * 0.5f }, 2000 };
  t3_instanced_mesh_set_bounding_sphere(im, &sph);
  cw_node nd = cw_node_new("Node3D", "Forest");
  cw_node_add_child(parent, nd);
  cw_node3d_set_object(nd, &im->mesh.base);
  t3_release(im);
}
/* the far trees' stand-in: crossed cards of the whole crown (a pine: its branch card stood upright, wide at the foot,
 * a point at the top), 4 to 6 quads where the near tree has 30 */
static t3_geometry *far_geo(int leaf_tex) {
  mesh_build f = { 0 };
  t3_color l = leaf_tex == TEX_PINECARD ? lin(0.5f, 0.75f, 0.5f) : lin(0.55f, 0.78f, 0.42f);
  for (int i = 0; i < 3; i++) {
    float a = (float)i * PI_F / 3, sa, ca;
    cw_sincos(a, &sa, &ca);
    if (leaf_tex == TEX_PINECARD) {
      if (i == 2) break;
      /* u up the tree, v across: the card's quad turned so its u axis is vertical */
      mesh_build *b = &f;
      v3 n = V(-sa, 0, ca), ax = V(ca, 0, sa);
      v3 P[4] = { vadd(V(0, 0.8f, 0), vmul(ax, -3.2f)), vadd(V(0, 0.8f, 0), vmul(ax, 3.2f)), vadd(V(0, 13.6f, 0), vmul(ax, 3.2f)), vadd(V(0, 13.6f, 0), vmul(ax, -3.2f)) };
      static const float UV[4][2] = { { 0.01f, 0.01f }, { 0.01f, 0.99f }, { 0.99f, 0.99f }, { 0.99f, 0.01f } };
      int v[4];
      for (int k = 0; k < 4; k++) v[k] = mb_vert(b, P[k], n, UV[k][0], UV[k][1]), mb_color(b, v[k], l.r * (k < 2 ? 0.7f : 1), l.g * (k < 2 ? 0.7f : 1), l.b * (k < 2 ? 0.7f : 1));
      mb_quad(b, v[0], v[1], v[2], v[3]);
    } else {
      card(&f, V(0, 4.6f, 0), V(ca * 2.6f, 0, sa * 2.6f), V(0, 2.3f, 0), V(0, 1, 0), l, 0.7f);
    }
  }
  return mb_finish(&f);
}
static void forest(cw_node parent, tree_geo tg, const float *xz, int n, uint32_t seed, float scale, bool shadow) {
  if (n > 0 && !getenv("SKATE_NO_FOREST")) {
    /* near trees whole (wood and foliage), far ones (past 220 m) as crossed cards */
    v3 c = vmul(vadd(G_LO, G_HI), 0.5f);
    float *nxz = malloc(sizeof(float) * 2 * (size_t)n), *fxz = malloc(sizeof(float) * 2 * (size_t)n);
    int nn = 0, nf = 0;
    for (int i = 0; i < n; i++) {
      float dx = xz[i * 2] - c.x, dz = xz[i * 2 + 1] - c.z;
      bool far = !FOREST_Y && dx * dx + dz * dz > 220.0f * 220.0f;
      float *o = far ? fxz + nf++ * 2 : nxz + nn++ * 2;
      o[0] = xz[i * 2], o[1] = xz[i * 2 + 1];
    }
    t3_material *ml = t3_mesh_standard_material_new(0xffffff);
    ml->vertex_colors = true, ml->roughness = 0.85f;
    ml->side = T3_DOUBLE_SIDE;
    ml->alpha_test = 0.5f;
    t3_material_set_texture(ml, T3_MAP, tex_get(tg.leaf_tex)->map);
    if (nn > 0) {
      t3_material *mw = t3_mesh_standard_material_new(0xffffff);
      mw->vertex_colors = true, mw->roughness = 0.9f;
      forest_part(parent, tg.wood, mw, nxz, nn, seed, scale, shadow);
      forest_part(parent, tg.leaves, ml, nxz, nn, seed, scale, shadow);
      t3_release(mw);
    }
    if (nf > 0) {
      t3_geometry *fg = far_geo(tg.leaf_tex);
      forest_part(parent, fg, ml, fxz, nf, seed + 1, scale, false);
      t3_release(fg);
    }
    t3_release(ml);
    free(nxz), free(fxz);
  }
  t3_release(tg.wood), t3_release(tg.leaves);
}
/* scatter n trees over ground within [r0, r1] of the park, not on steep or excluded ground */
static int scatter(float *xz, int cap, int n, float r0, float r1, float hmax, bool (*skip)(float x, float z)) {
  v3 c = vmul(vadd(G_LO, G_HI), 0.5f);
  int k = 0;
  for (int tries = 0; tries < n * 8 && k < n && k < cap; tries++) {
    float a = rr(0, 2 * PI_F), r = rr(0, 1);
    r = r0 + (r1 - r0) * r * r;
    float x = c.x + cw_cos(a) * r, z = c.z + cw_sin(a) * r;
    if (rect_dist(x, z) < r0 * 0.4f) continue;
    float h = ground_h(x, z);
    if (h > hmax || (skip && skip(x, z))) continue;
    /* clumps: a noise field thins the trees into groves */
    if (sfbm(x / 70.0f, z / 70.0f, 2, 77u) < 0.42f) continue;
    xz[k * 2] = x, xz[k * 2 + 1] = z, k++;
  }
  return k;
}

/* ── the far skyline: a band of towers at 650 m (alpha cut), in the haze ── */
static void skyline(cw_node parent, float height, const float tint[3]) {
  mesh_build b = { 0 };
  v3 c = vmul(vadd(G_LO, G_HI), 0.5f);
  const int S = 96;
  const float R = 650;
  t3_color l = lin(tint[0], tint[1], tint[2]);
  for (int i = 0; i <= S; i++) {
    float a = 2 * PI_F * (float)i / S, sa, ca;
    cw_sincos(a, &sa, &ca);
    v3 n = V(-ca, 0, -sa);
    int v0 = mb_vert(&b, V(c.x + ca * R, -2, c.z + sa * R), n, (float)i / S * 7, 0);
    int v1 = mb_vert(&b, V(c.x + ca * R, height, c.z + sa * R), n, (float)i / S * 7, 1);
    mb_color(&b, v0, l.r, l.g, l.b), mb_color(&b, v1, l.r, l.g, l.b);
    if (i > 0) mb_quad(&b, v0 - 2, v0, v1, v1 - 2);
  }
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  m->side = T3_DOUBLE_SIDE;
  m->alpha_test = 0.5f;
  m->fog = true;
  t3_material_set_texture(m, T3_MAP, tex_get(TEX_SKYLINE)->map);
  if (MOOD->windows_lit > 0) {
    m->color = (t3_color){ 1.15f, 1.1f, 1.2f };
  }
  t3_geometry *g = mb_finish(&b);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false, me->base.receive_shadow = false;
  me->base.frustum_culled = false;
  cw_node n = cw_node_new("Node3D", "Skyline");
  cw_node_add_child(parent, n);
  cw_node3d_set_object(n, &me->base);
  t3_release(me), t3_release(g), t3_release(m);
}

/* ── the sea ─────────────────────────────────────────────────────────── */
static void sea(cw_node parent, float x0, float shore) {
  mesh_build b = { 0 };
  v3 n = V(0, 1, 0);
  const float Y = -0.42f, X1 = 3000, Z = 3000;
  /* the near water in strips (it takes the sky's light as it goes), the far in one quad */
  int v[4] = { mb_vert(&b, V(x0, Y, -Z), n, x0 / 8, -Z / 8), mb_vert(&b, V(X1, Y, -Z), n, X1 / 8, -Z / 8), mb_vert(&b, V(X1, Y, Z), n, X1 / 8, Z / 8),
               mb_vert(&b, V(x0, Y, Z), n, x0 / 8, Z / 8) };
  mb_quad(&b, v[0], v[3], v[2], v[1]);
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = lin(MOOD->water[0], MOOD->water[1], MOOD->water[2]);
  m->roughness = 0.06f, m->metalness = 0.0f;
  t3_material_set_texture(m, T3_NORMAL_MAP, tex_get(TEX_WATER)->normal);
  m->normal_scale = (t3_vec2){ 0.5f, 0.5f };
  t3_geometry *g = mb_finish(&b);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false, me->base.receive_shadow = false;
  cw_node nd = cw_node_new("Node3D", "Sea");
  cw_node_add_child(parent, nd);
  cw_node3d_set_object(nd, &me->base);
  t3_release(me), t3_release(g), t3_release(m);
  /* surf: a white line along the shore, broken */
  static const float foam[3] = { 0.98f, 0.98f, 0.96f };
  for (int i = 0; i < 80; i++) {
    float z = -400 + (float)i * 10 + rr(0, 4), w = rr(3, 9);
    float xs = shore + rr(-1, 1.5f);
    flat(SB_PAINT, xs, z, xs + rr(0.6f, 1.6f), z + w, Y + 0.03f, foam);
  }
}

/* ── each park's setting ─────────────────────────────────────────────── */

/* a street grid of city blocks from radius r0 out to r1 round the park (the ring road at r0, its sidewalks), each
 * block filled with buildings; heights grow outward from hmin to hmax */
static void city(float ring, float block, float street, float r1, float hmin, float hmax, bool lights, bool neon) {
  float x0 = G_LO.x - ring, x1 = G_HI.x + ring, z0 = G_LO.z - ring, z1 = G_HI.z + ring;
  /* the ring road's lines (the asphalt is the ground's): a centre line, edge lines, crossings at the corners */
  float mx0 = G_LO.x - ring * 0.5f, mx1 = G_HI.x + ring * 0.5f, mz0 = G_LO.z - ring * 0.5f, mz1 = G_HI.z + ring * 0.5f;
  for (float x = mx0 + 8; x < mx1 - 8; x += 6) flat(SB_PAINT, x, mz0 - 0.08f, x + 3, mz0 + 0.08f, 0.012f, LINE_W), flat(SB_PAINT, x, mz1 - 0.08f, x + 3, mz1 + 0.08f, 0.012f, LINE_W);
  for (float z = mz0 + 8; z < mz1 - 8; z += 6) flat(SB_PAINT, mx0 - 0.08f, z, mx0 + 0.08f, z + 3, 0.012f, LINE_W), flat(SB_PAINT, mx1 - 0.08f, z, mx1 + 0.08f, z + 3, 0.012f, LINE_W);
  for (int cnr = 0; cnr < 4; cnr++) {
    float cx = (cnr & 1) ? mx1 : mx0, cz = (cnr & 2) ? mz1 : mz0;
    for (int s = 0; s < 7; s++) {
      float o = -ring * 0.35f + (float)s * ring * 0.11f;
      flat(SB_PAINT, cx + o, cz + ((cnr & 2) ? -ring * 0.5f - 3.5f : ring * 0.5f + 0.5f), cx + o + ring * 0.06f, cz + ((cnr & 2) ? -ring * 0.5f - 0.5f : ring * 0.5f + 3.5f), 0.012f, LINE_W);
    }
  }
  /* the sidewalk round the fence (the park's side of the street) */
  float sw = 4.0f;
  wbox(SB_TILE, V((G_LO.x + G_HI.x) * 0.5f, 0.07f, G_LO.z - sw * 0.5f - 0.2f), 0, V((x1 - x0) * 0.5f - ring + sw, 0.08f, sw * 0.5f), CURB, 0, true, false);
  wbox(SB_TILE, V((G_LO.x + G_HI.x) * 0.5f, 0.07f, G_HI.z + sw * 0.5f + 0.2f), 0, V((x1 - x0) * 0.5f - ring + sw, 0.08f, sw * 0.5f), CURB, 0, true, false);
  wbox(SB_TILE, V(G_LO.x - sw * 0.5f - 0.2f, 0.07f, (G_LO.z + G_HI.z) * 0.5f), 0, V(sw * 0.5f, 0.08f, (z1 - z0) * 0.5f - ring), CURB, 0, true, false);
  wbox(SB_TILE, V(G_HI.x + sw * 0.5f + 0.2f, 0.07f, (G_LO.z + G_HI.z) * 0.5f), 0, V(sw * 0.5f, 0.08f, (z1 - z0) * 0.5f - ring), CURB, 0, true, false);
  /* parked cars along the far curb of the ring road, lamps along the park's sidewalk */
  for (float x = x0 + 10; x < x1 - 10; x += rr(5.6f, 9)) {
    if (rnd() < 0.8f) car(x, z0 + 1.6f, PI_F * 0.5f + (rnd() < 0.5f ? PI_F : 0), CARS[(int)(rnd() * 10) % 10], rnd() < 0.15f);
    if (rnd() < 0.8f) car(x + 2, z1 - 1.6f, PI_F * 0.5f + (rnd() < 0.5f ? PI_F : 0), CARS[(int)(rnd() * 10) % 10], rnd() < 0.15f);
  }
  for (float z = z0 + 10; z < z1 - 10; z += rr(5.6f, 9)) {
    if (rnd() < 0.8f) car(x0 + 1.6f, z, rnd() < 0.5f ? 0 : PI_F, CARS[(int)(rnd() * 10) % 10], rnd() < 0.15f);
    if (rnd() < 0.8f) car(x1 - 1.6f, z + 3, rnd() < 0.5f ? 0 : PI_F, CARS[(int)(rnd() * 10) % 10], rnd() < 0.15f);
  }
  for (float x = G_LO.x + 6; x < G_HI.x; x += 18) street_lamp(x, G_LO.z - 3.4f, PI_F), street_lamp(x + 9, G_HI.z + 3.4f, 0);
  for (float z = G_LO.z + 6; z < G_HI.z; z += 18) street_lamp(G_LO.x - 3.4f, z, -PI_F * 0.5f), street_lamp(G_HI.x + 3.4f, z + 9, PI_F * 0.5f);
  (void)lights;
  /* the blocks: a grid out from the ring road (the blocks in line with the park span its width) */
  float pitch = block + street;
  int nb = (int)((r1 - ring) / pitch) + 1;
  v3 pc = vmul(vadd(G_LO, G_HI), 0.5f);
  for (int bi = -nb; bi <= nb; bi++)
    for (int bj = -nb; bj <= nb; bj++) {
      if (bi == 0 && bj == 0) continue;
      float ax0, ax1, az0, az1;
      if (bi == 0) ax0 = x0 + street * 0.5f, ax1 = x1 - street * 0.5f;
      else if (bi > 0) ax0 = x1 + street * 0.5f + (float)(bi - 1) * pitch, ax1 = ax0 + block;
      else ax1 = x0 - street * 0.5f - (float)(-bi - 1) * pitch, ax0 = ax1 - block;
      if (bj == 0) az0 = z0 + street * 0.5f, az1 = z1 - street * 0.5f;
      else if (bj > 0) az0 = z1 + street * 0.5f + (float)(bj - 1) * pitch, az1 = az0 + block;
      else az1 = z0 - street * 0.5f - (float)(-bj - 1) * pitch, az0 = az1 - block;
      float cx = (ax0 + ax1) * 0.5f, cz = (az0 + az1) * 0.5f, wx = ax1 - ax0, wz = az1 - az0;
      float dist = sqrtf((cx - pc.x) * (cx - pc.x) + (cz - pc.z) * (cz - pc.z));
      if (dist > r1) continue;
      wbox(SB_TILE, V(cx, 0.08f, cz), 0, V(wx * 0.5f, 0.08f, wz * 0.5f), CURB, 0, true, false);
      float near = 1 - sstep(80, r1, dist);
      for (int side = 0; side < 4; side++) {
        /* a block in line with the park keeps only its outward side's row (the side toward the park too, facing it) */
        float len = side < 2 ? wx : wz;
        int n = (int)(len / 22.0f);
        if (n < 1) n = 1;
        if (n > 6) n = 6;
        float seg = (len - 6) / (float)n;
        float across = side < 2 ? wz : wx;
        float dep = fminf(18, across * 0.5f - 3);
        if (dep < 6) continue;
        for (int i = 0; i < n; i++) {
          float t = -len * 0.5f + 3 + seg * ((float)i + 0.5f);
          float yaw = side == 0 ? PI_F : side == 1 ? 0 : side == 2 ? -PI_F * 0.5f : PI_F * 0.5f;
          float px = side < 2 ? cx + t : cx + (side == 2 ? -1 : 1) * (wx * 0.5f - 2.5f - dep * 0.5f);
          float pz = side < 2 ? cz + (side == 0 ? -1 : 1) * (wz * 0.5f - 2.5f - dep * 0.5f) : cz + t;
          /* the rows facing along x stop short of the corners the rows facing along z hold */
          if (side >= 2 && fabsf(t) > len * 0.5f - dep - 3) continue;
          float h = hmin + (hmax - hmin) * (1 - near) * rnd() + rr(0, 10) + (rnd() < 0.15f ? rr(10, 30) * (1 - near) : 0);
          if (neon && near > 0.5f) h = fmaxf(h, rr(14, 26));
          int style = h > 45 ? 2 : (rnd() < 0.45f ? 1 : 0);
          const float *col = style == 1 ? BRICKS[(int)(rnd() * 4) & 3] : CITY[(int)(rnd() * 8) & 7];
          building(px, pz, yaw, seg - 1.0f, dep, h, style, col, near > 0.3f, rr(0, 64));
          if (neon && near > 0.4f && rnd() < 0.7f) {
            /* a blade sign out from the facade, lit */
            float s, c;
            cw_sincos(yaw, &s, &c);
            v3 F = V(s, 0, c), R = V(c, 0, -s);
            v3 p = vadd(V(px, rr(6, 11), pz), vadd(vmul(F, dep * 0.5f + 0.8f), vmul(R, rr(-seg * 0.3f, seg * 0.3f))));
            const float *nc = NEON[(int)(rnd() * 6) % 6];
            wbox(SB_METAL, p, yaw + PI_F * 0.5f, V(0.85f, rr(1.6f, 3.0f), 0.12f), DARK, 0, true, true);
            wbox(SB_GLOW, p, yaw + PI_F * 0.5f, V(0.7f, 0.12f, 0.14f), nc, 0, true, true);
            wbox(SB_GLOW, vadd(p, V(0, 0.9f, 0)), yaw + PI_F * 0.5f, V(0.55f, 0.1f, 0.14f), NEON[((int)(rnd() * 6) + 1) % 6], 0, true, true);
            wbox(SB_GLOW, vadd(p, V(0, -0.9f, 0)), yaw + PI_F * 0.5f, V(0.6f, 0.1f, 0.14f), nc, 0, true, true);
          }
        }
      }
      if (near > 0.5f && rnd() < 0.7f) street_lamp(ax0 + 0.8f, az0 + 0.8f, PI_F * 0.25f);
    }
}

static bool beach_skip(float x, float z) { (void)z; return x > G_HI.x - 2; }
static bool city_skip(float x, float z) { return rect_dist(x, z) < 420; }

static void set_sunny(cw_node parent) {
  /* the boardwalk along the east fence */
  static const float boards[3] = { 0.86f, 0.7f, 0.52f }, rail[3] = { 0.95f, 0.95f, 0.92f };
  float bx = G_HI.x + 1.0f;
  wbox(SB_WOOD, V(bx + 4, 0.25f, 0), 0, V(4, 0.25f, 180), boards, 0, true, false);
  for (float z = -178; z < 178; z += 3) cyl(SB_WOOD, V(bx + 8.1f, 0, z), V(bx + 8.1f, 1.2f, z), 0.06f, 5, rail, false);
  wbox(SB_WOOD, V(bx + 8.1f, 1.15f, 0), 0, V(0.07f, 0.05f, 178), rail, 0, true, true);
  for (float z = -170; z < 170; z += 12) palm(bx + rr(-0.5f, 0.5f) - 0.4f, z + rr(-2, 2), rr(7, 10), rr(0, 6.28f));
  /* a pier out over the water */
  for (float x = bx + 8; x < bx + 150; x += 4) {
    cyl(SB_WOOD, V(x, -3, -20), V(x, 1.4f, -20), 0.18f, 6, boards, false), cyl(SB_WOOD, V(x, -3, -12), V(x, 1.4f, -12), 0.18f, 6, boards, false);
  }
  wbox(SB_WOOD, V(bx + 79, 1.5f, -16), 0, V(71, 0.12f, 4.5f), boards, 0, true, true);
  for (float x = bx + 10; x < bx + 150; x += 3) cyl(SB_WOOD, V(x, 1.6f, -20.3f), V(x, 2.6f, -20.3f), 0.05f, 4, rail, false), cyl(SB_WOOD, V(x, 1.6f, -11.7f), V(x, 2.6f, -11.7f), 0.05f, 4, rail, false);
  /* huts, umbrellas, lifeguard towers on the sand */
  for (int i = 0; i < 9; i++) {
    float z = -150 + (float)i * 36 + rr(-4, 4), x = bx + 16 + rr(0, 4);
    const float *c = PASTEL[i & 7];
    wbox(SB_PAINT, V(x, 1.4f, z), 0, V(1.6f, 1.4f, 1.8f), c, rr(0, 9), false, false);
    wbox(SB_PAINT, V(x - 1.62f, 1.2f, z), 0, V(0.02f, 0.9f, 0.6f), WHITE, 0, false, false);
    cone(SB_PAINT, V(x, 2.8f, z), 2.6f, 1.3f, 4, WHITE, c);
  }
  for (int i = 0; i < 60; i++) {
    float x = bx + rr(22, 52), z = rr(-170, 170);
    if (z > -24 && z < -8) continue;
    const float *a = PASTEL[(int)(rnd() * 8) & 7], *b = WHITE;
    cyl(SB_METAL, V(x, 0, z), V(x, 2.2f, z), 0.03f, 4, WHITE, false);
    cone(SB_PAINT, V(x, 1.95f, z), rr(1.0f, 1.4f), 0.45f, 10, a, b);
    if (rnd() < 0.6f) wbox(SB_PAINT, V(x + 1.2f, ground_h(x + 1.2f, z) + 0.02f, z), rr(0, 3), V(0.45f, 0.02f, 0.95f), PASTEL[(int)(rnd() * 8) & 7], 0, true, false);
  }
  for (int i = 0; i < 3; i++) {
    float x = bx + 44, z = -110 + (float)i * 110, y = ground_h(x, z);
    for (int l = 0; l < 4; l++) cyl(SB_WOOD, V(x + ((l & 1) ? 1.2f : -1.2f), y, z + ((l & 2) ? 1.2f : -1.2f)), V(x + ((l & 1) ? 0.8f : -0.8f), y + 2.6f, z + ((l & 2) ? 0.8f : -0.8f)), 0.08f, 5, WHITE, false);
    static const float red[3] = { 0.9f, 0.2f, 0.15f };
    wbox(SB_PAINT, V(x, y + 3.6f, z), 0, V(1.1f, 1.0f, 1.1f), red, 0, true, true);
    cone(SB_PAINT, V(x, y + 4.6f, z), 1.6f, 0.7f, 4, WHITE, WHITE);
  }
  /* the town to the west: two-storey pastel fronts along a street, palms, cars */
  float sx = G_LO.x - 9;
  for (float z = -170; z < 170; z += rr(6, 9)) if (rnd() < 0.65f) car(sx + 3.3f, z, rnd() < 0.5f ? 0 : PI_F, CARS[(int)(rnd() * 10) % 10], rnd() < 0.2f);
  for (float z = -160; z < 160; z += 18) street_lamp(G_LO.x - 3.5f, z, -PI_F * 0.5f);
  for (int row = 0; row < 3; row++)
    for (float z = -170; z < 170;) {
      float w = rr(9, 16), d = rr(10, 14), h = row == 0 ? rr(6.5f, 10) : rr(7, 14);
      float x = sx - 8 - d * 0.5f - (float)row * 34;
      building(x, z + w * 0.5f, -PI_F * 0.5f, w, d, h, rnd() < 0.2f ? 1 : 0, PASTEL[(int)(rnd() * 8) & 7], row == 0, rr(0, 60));
      if (row == 0 && rnd() < 0.5f) palm(x + d * 0.5f + 3, z + w + 0.5f, rr(6, 9), rr(0, 6));
      z += w + rr(0.5f, 3);
    }
  /* hills behind the town: scrub and trees */
  static float xz[2 * 900];
  RNG = 4401;
  int n = scatter(xz, 900, 420, 200, 900, 400, beach_skip);
  forest(parent, broadleaf_geo(), xz, n, 11, 1.6f, false);
}

/* a shrub in a planter: crossed foliage cards over a box of soil */
static void shrub(float x, float y, float z, float r) {
  t3_color l = lin(0.55f, 0.8f, 0.42f);
  for (int i = 0; i < 3; i++) {
    float a = (float)i * 1.05f + r, sa, ca;
    cw_sincos(a, &sa, &ca);
    card(&SB[SB_FOLIAGE], V(x, y + r * 0.8f, z), V(ca * r, 0, sa * r), V(0, r * 0.8f, 0), V(0, 1, 0), l, 0.6f);
  }
  card(&SB[SB_FOLIAGE], V(x, y + r * 1.1f, z), V(r, 0, 0), V(0, 0, r), V(0, 1, 0), l, 1.0f);
}
/* the plaza dressed: granite bands across its paving, and on the sidewalk round it planters, benches, bins and
 * bollards (outside the fence: nothing a line runs through) */
static void plaza_dress(void) {
  static const float granite[3] = { 0.28f, 0.27f, 0.29f }, soil[3] = { 0.3f, 0.22f, 0.16f }, slat[3] = { 0.62f, 0.44f, 0.28f }, iron[3] = { 0.16f, 0.17f, 0.2f };
  for (float x = -36; x <= 36; x += 12) flat(SB_TILE, x - 0.2f, -19.5f, x + 0.2f, 41.5f, 0.008f, granite);
  for (float z = -12; z <= 40; z += 12) flat(SB_TILE, -41.5f, z - 0.2f, 41.5f, z + 0.2f, 0.009f, granite);
  for (int side = 0; side < 4; side++)
    for (float t = -36; t <= 36; t += 9) {
      float x = side < 2 ? t : (side == 2 ? G_LO.x - 1.6f : G_HI.x + 1.6f), z = side < 2 ? (side == 0 ? G_LO.z - 1.6f : G_HI.z + 1.6f) : t;
      float yaw = side == 0 ? PI_F : side == 1 ? 0 : side == 2 ? -PI_F * 0.5f : PI_F * 0.5f;
      int k = ((int)(t + 40) / 9 + side) % 4;
      if (k == 0) { /* a planter */
        wbox(SB_CONC, V(x, 0.3f, z), yaw, V(1.1f, 0.3f, 0.55f), OFFWHITE, 0, true, false);
        wbox(SB_PAINT, V(x, 0.58f, z), yaw, V(1.0f, 0.02f, 0.45f), soil, 0, true, false);
        shrub(x - 0.45f * (side < 2), 0.6f, z - 0.45f * (side >= 2), 0.55f), shrub(x + 0.45f * (side < 2), 0.6f, z + 0.45f * (side >= 2), 0.5f);
      } else if (k == 1 || k == 3) { /* a bench: slats on iron legs */
        float s, c;
        cw_sincos(yaw, &s, &c);
        v3 R = V(c, 0, -s);
        for (int e = -1; e <= 1; e += 2) wbox(SB_METAL, vadd(V(x, 0.22f, z), vmul(R, 0.75f * (float)e)), yaw, V(0.04f, 0.22f, 0.22f), iron, 0, true, false);
        for (int sl = 0; sl < 3; sl++) wbox(SB_WOOD, vadd(V(x, 0.46f, z), vmul(V(s, 0, c), -0.15f + 0.15f * (float)sl)), yaw, V(0.9f, 0.025f, 0.06f), slat, 0, true, true);
        wbox(SB_WOOD, vadd(V(x, 0.75f, z), vmul(V(s, 0, c), -0.26f)), yaw, V(0.9f, 0.12f, 0.025f), slat, 0, true, true);
      } else { /* a bin and two bollards */
        cyl(SB_METAL, V(x, 0, z), V(x, 0.85f, z), 0.28f, 10, iron, true);
        cyl(SB_METAL, V(x + 1.4f * (side < 2), 0, z + 1.4f * (side >= 2)), V(x + 1.4f * (side < 2), 0.9f, z + 1.4f * (side >= 2)), 0.1f, 8, STEEL, true);
        cyl(SB_METAL, V(x - 1.4f * (side < 2), 0, z - 1.4f * (side >= 2)), V(x - 1.4f * (side < 2), 0.9f, z - 1.4f * (side >= 2)), 0.1f, 8, STEEL, true);
      }
    }
}

static void set_plaza(cw_node parent) {
  city(18, 46, 14, 380, 14, 70, false, false);
  plaza_dress();
  /* street trees along the park's sidewalk */
  static float xz[2 * 120];
  int n = 0;
  for (float x = G_LO.x + 10; x < G_HI.x; x += 15) xz[n * 2] = x, xz[n * 2 + 1] = G_LO.z - 3.2f, n++, xz[n * 2] = x + 7, xz[n * 2 + 1] = G_HI.z + 3.2f, n++;
  for (float z = G_LO.z + 10; z < G_HI.z; z += 15) xz[n * 2] = G_LO.x - 3.2f, xz[n * 2 + 1] = z, n++, xz[n * 2] = G_HI.x + 3.2f, xz[n * 2 + 1] = z + 7, n++;
  forest(parent, broadleaf_geo(), xz, n, 21, 1.2f, true);
  /* the plaza's own trees, on its planters (parks.c: their trunks and planters are the park's, solid) */
  {
    static const float PT[7][3] = { { -27, 2.2f, -33 }, { 27, 2.2f, -33 }, { 38.5f, 0.6f, -8 }, { 38.5f, 0.6f, 6 }, { 38.5f, 0.6f, 20 }, { -38.5f, 0.6f, 18 }, { -38.5f, 0.6f, 30 } };
    static float pxz[14], py[7];
    for (int i = 0; i < 7; i++) pxz[i * 2] = PT[i][0], pxz[i * 2 + 1] = PT[i][2], py[i] = PT[i][1];
    FOREST_Y = py;
    forest(parent, broadleaf_geo(), pxz, 7, 23, 0.95f, true);
    FOREST_Y = NULL;
  }
  static float far[2 * 600];
  RNG = 991;
  int m = scatter(far, 600, 300, 420, 1100, 500, city_skip);
  forest(parent, broadleaf_geo(), far, m, 22, 1.8f, false);
  static const float tint[3] = { 0.62f, 0.68f, 0.78f };
  skyline(parent, 160, tint);
}

static void set_mega(cw_node parent) {
  /* the gravel car park east of the fence: rows of cars and vans */
  float px = G_HI.x + 12;
  for (int row = 0; row < 3; row++)
    for (float z = -60; z < 60; z += 3.0f)
      if (rnd() < 0.7f) car(px + (float)row * 9 + (row & 1) * 2, z, (row & 1) ? PI_F * 0.5f : -PI_F * 0.5f, CARS[(int)(rnd() * 10) % 10], rnd() < 0.3f);
  /* tents along the west fence: pyramid tops in bright colours on white walls */
  for (int i = 0; i < 7; i++) {
    float x = G_LO.x - 10, z = -70 + (float)i * 18;
    const float *c = PASTEL[(i * 3) & 7];
    for (int l = 0; l < 4; l++) cyl(SB_METAL, V(x + ((l & 1) ? 3 : -3), 0, z + ((l & 2) ? 3 : -3)), V(x + ((l & 1) ? 3 : -3), 2.5f, z + ((l & 2) ? 3 : -3)), 0.05f, 4, WHITE, false);
    cone(SB_PAINT, V(x, 2.5f, z), 4.4f, 2.0f, 4, c, WHITE);
    wbox(SB_PAINT, V(x, 0.45f, z), 0, V(2.2f, 0.45f, 0.6f), WHITE, 0, true, false);
  }
  /* bleachers along the landing: steps of planks on steel */
  for (int s = 0; s < 6; s++) {
    float x = G_LO.x - 22 - (float)s * 0.9f, y = 0.4f + (float)s * 0.45f;
    wbox(SB_WOOD, V(x, y, 12), 0, V(0.42f, 0.05f, 22), WHITE, 0, true, true);
    for (float z = -9; z <= 33; z += 6) cyl(SB_METAL, V(x, 0, z), V(x, y, z), 0.05f, 4, STEEL, false);
  }
  /* banners on poles round the run */
  for (int i = 0; i < 10; i++) {
    float z = G_LO.z + 10 + (float)i * 14, x = G_LO.x - 3;
    cyl(SB_METAL, V(x, 0, z), V(x, 7, z), 0.06f, 5, WHITE, false);
    wbox(SB_PAINT, V(x, 5.6f, z + 0.7f), 0, V(0.02f, 1.2f, 0.6f), NEON[i % 6], 0, false, false);
  }
  static float xz[2 * 1600];
  RNG = 5150;
  int n = scatter(xz, 1600, 480, 160, 1300, 300, NULL);
  forest(parent, broadleaf_geo(), xz, n, 31, 1.4f, false);
}

static void set_roofs(cw_node parent) {
  city(14, 40, 12, 330, 20, 80, true, true);
  static const float tint[3] = { 0.5f, 0.52f, 0.66f };
  skyline(parent, 190, tint);
  (void)parent;
}

static void set_barn(cw_node parent) {
  /* stacks of boards in rows, log piles, a sawmill shed and its office */
  for (int row = 0; row < 3; row++)
    for (int i = 0; i < 6; i++) {
      float x = G_HI.x + 14 + (float)row * 11, z = -48 + (float)i * 16;
      lumber_stack(x, z, PI_F * 0.5f * (float)(row & 1), rr(5, 7), rr(2.4f, 3.2f), rr(1.4f, 2.6f));
    }
  for (int i = 0; i < 8; i++) {
    float x = G_LO.x + 6 + (float)i * 10, z = G_HI.z + 16;
    lumber_stack(x, z, 0, rr(5, 7), rr(2.4f, 3), rr(1.2f, 2.4f));
  }
  static const float logc[3] = { 0.55f, 0.4f, 0.28f };
  for (int pile = 0; pile < 3; pile++) {
    float px = G_LO.x - 20, pz = -30 + (float)pile * 28;
    for (int layer = 0; layer < 4; layer++)
      for (int i = 0; i < 6 - layer; i++) {
        float y = 0.35f + (float)layer * 0.6f, z = pz - 2.0f + (float)i * 0.72f + (float)layer * 0.36f;
        cyl(SB_WOOD, V(px - 4, y, z), V(px + 4, y, z), 0.34f, 8, logc, true);
      }
  }
  /* the sawmill: a long shed of corrugated sheet on a concrete plinth, a pitched roof, big doors */
  static const float shed[3] = { 0.62f, 0.72f, 0.82f }, roofc[3] = { 0.78f, 0.3f, 0.22f }, door[3] = { 0.9f, 0.85f, 0.7f };
  float sz = G_LO.z - 34;
  wbox(SB_CONC, V(0, 0.3f, sz), 0, V(36, 0.3f, 13), OFFWHITE, 0, true, false);
  wbox(SB_CORR, V(0, 4.6f, sz), 0, V(35, 4.0f, 12), shed, 0, false, false);
  for (int sd = -1; sd <= 1; sd += 2) {
    /* the roof's two slopes */
    mesh_build *b = &SB[SB_CORR];
    t3_color l = lin(roofc[0], roofc[1], roofc[2]);
    v3 n = vnorm(V(0, 1, (float)sd * 0.5f));
    int v[4] = { mb_vert(b, V(-36, 8.6f, sz + (float)sd * 13), n, -18, 0), mb_vert(b, V(36, 8.6f, sz + (float)sd * 13), n, 18, 0), mb_vert(b, V(36, 13.6f, sz), n, 18, 5),
                 mb_vert(b, V(-36, 13.6f, sz), n, -18, 5) };
    for (int k = 0; k < 4; k++) mb_color(b, v[k], l.r, l.g, l.b);
    if (sd > 0) mb_quad(b, v[0], v[1], v[2], v[3]);
    else mb_quad(b, v[0], v[3], v[2], v[1]);
  }
  for (int i = -1; i <= 1; i++) wbox(SB_PAINT, V((float)i * 18, 3.2f, sz + 12.05f), 0, V(3.5f, 3.0f, 0.05f), door, 0, false, false);
  /* gables */
  for (int e = -1; e <= 1; e += 2) {
    mesh_build *b = &SB[SB_CORR];
    t3_color l = lin(shed[0], shed[1], shed[2]);
    v3 n = V((float)e, 0, 0);
    int v[3] = { mb_vert(b, V(35.0f * (float)e, 8.6f, sz - 12), n, 0, 0), mb_vert(b, V(35.0f * (float)e, 8.6f, sz + 12), n, 12, 0), mb_vert(b, V(35.0f * (float)e, 13.6f, sz), n, 6, 2.5f) };
    for (int k = 0; k < 3; k++) mb_color(b, v[k], l.r, l.g, l.b);
    if (e > 0) mb_tri(b, v[0], v[2], v[1]);
    else mb_tri(b, v[0], v[1], v[2]);
  }
  building(G_LO.x - 18, G_LO.z - 18, PI_F * 0.5f, 10, 8, 6, 0, PASTEL[5], false, 4);
  /* trucks */
  for (int i = 0; i < 4; i++) car(G_HI.x + 50, -30 + (float)i * 9, 0, CARS[(i * 3) % 10], true);
  /* the forest on the hills */
  static float xz[2 * 3200];
  RNG = 777;
  int n = scatter(xz, 3200, 1800, 110, 1300, 260, NULL);
  forest(parent, pine_geo(), xz, n, 41, 1.25f, false);
  static float near[2 * 200];
  int m = 0;
  for (int i = 0; i < 200 && m < 200; i++) {
    float a = rr(0, 6.28f), r = rr(95, 140);
    float x = (G_LO.x + G_HI.x) * 0.5f + cw_cos(a) * r, z = (G_LO.z + G_HI.z) * 0.5f + cw_sin(a) * r;
    if (z < G_LO.z - 10 && fabsf(x) < 45) continue;
    near[m * 2] = x, near[m * 2 + 1] = z, m++;
  }
  forest(parent, pine_geo(), near, m, 42, 1.3f, true);
}

/* ── the moods ───────────────────────────────────────────────────────── */
static const scenery_mood MOODS[PARK_COUNT] = {
  /* Sunnyside: golden hour, the sea a deep teal */
  { .time = 17.6f, .north = 210, .sun_energy = 5.2f, .sky_energy = 1.6f, .mie = 0.55f, .rayleigh = 1.1f, .exposure = -0.4f, .hemi = 0.08f, .lm_gain = 0.6f,
    .zenith = { 0.16f, 0.42f, 0.95f }, .horizon = { 0.78f, 0.86f, 0.98f }, .cloud = { 1.0f, 0.86f, 0.72f }, .fog = { 0.56f, 0.68f, 0.9f }, .fog_density = 0.0010f,
    .ground_near = { 0.42f, 0.66f, 0.28f }, .ground_far = { 0.36f, 0.56f, 0.26f }, .rock = { 0.66f, 0.56f, 0.46f }, .water = { 0.04f, 0.42f, 0.55f },
    .shadow_soft = 0.0f, .glow = 1.4f },
  /* Civic Plaza: a bright hazy morning */
  { .time = 9.0f, .north = 140, .sun_energy = 6.6f, .sky_energy = 2.0f, .mie = 0.6f, .rayleigh = 1.2f, .exposure = -0.05f, .hemi = 0.05f, .lm_gain = 0.36f,
    .zenith = { 0.22f, 0.46f, 0.92f }, .horizon = { 0.86f, 0.92f, 1.0f }, .cloud = { 1.0f, 1.0f, 1.0f }, .fog = { 0.58f, 0.7f, 0.9f }, .fog_density = 0.0012f,
    .ground_near = { 0.45f, 0.62f, 0.32f }, .ground_far = { 0.5f, 0.58f, 0.42f }, .rock = { 0.6f, 0.58f, 0.55f }, .water = { 0.1f, 0.3f, 0.4f },
    .shadow_soft = 1.0f, .glow = 1.2f },
  /* Big Sky: clear noon, a deep blue */
  { .time = 13.0f, .north = 30, .sun_energy = 4.4f, .sky_energy = 1.8f, .mie = 0.25f, .rayleigh = 1.4f, .exposure = -0.9f, .hemi = 0.08f, .lm_gain = 0.6f,
    .zenith = { 0.08f, 0.3f, 0.82f }, .horizon = { 0.66f, 0.8f, 0.98f }, .cloud = { 1.0f, 1.0f, 1.0f }, .fog = { 0.55f, 0.7f, 0.95f }, .fog_density = 0.0008f,
    .ground_near = { 0.78f, 0.68f, 0.4f }, .ground_far = { 0.72f, 0.58f, 0.4f }, .rock = { 0.8f, 0.5f, 0.34f }, .water = { 0.1f, 0.3f, 0.4f },
    .shadow_soft = 0.0f, .glow = 1.2f },
  /* Rooftops: sunset, the windows and the signs coming on */
  { .time = 18.75f, .north = 100, .sun_energy = 5.5f, .sky_energy = 1.7f, .mie = 0.7f, .rayleigh = 1.3f, .exposure = 2.75f, .hemi = 0.24f, .lm_gain = 2.0f,
    .zenith = { 0.2f, 0.22f, 0.55f }, .horizon = { 1.0f, 0.6f, 0.42f }, .cloud = { 1.0f, 0.62f, 0.56f }, .fog = { 0.78f, 0.52f, 0.55f }, .fog_density = 0.0012f,
    .ground_near = { 0.4f, 0.5f, 0.3f }, .ground_far = { 0.45f, 0.45f, 0.4f }, .rock = { 0.5f, 0.45f, 0.42f }, .water = { 0.1f, 0.3f, 0.4f },
    .shadow_soft = 0.0f, .glow = 2.6f, .windows_lit = 1.6f },
  /* Timber Yard: early morning, gold light through cool air */
  { .time = 8.1f, .north = 300, .sun_energy = 4.8f, .sky_energy = 1.6f, .mie = 0.6f, .rayleigh = 1.2f, .exposure = -0.68f, .hemi = 0.08f, .lm_gain = 0.6f,
    .zenith = { 0.2f, 0.42f, 0.82f }, .horizon = { 0.95f, 0.86f, 0.72f }, .cloud = { 1.0f, 0.92f, 0.82f }, .fog = { 0.52f, 0.66f, 0.84f }, .fog_density = 0.0013f,
    .ground_near = { 0.4f, 0.58f, 0.28f }, .ground_far = { 0.3f, 0.46f, 0.26f }, .rock = { 0.56f, 0.56f, 0.6f }, .water = { 0.1f, 0.3f, 0.4f },
    .shadow_soft = 0.0f, .glow = 1.2f },
};
const scenery_mood *scenery_mood_of(int park) { return &MOODS[park % PARK_COUNT]; }

void scenery_build(cw_node parent, const park *k, int park_index) {
  G_PARK = park_index % PARK_COUNT;
  MOOD = &MOODS[G_PARK];
  /* the fence round each park (park.c's kit_fence in parks.c): the setting starts at it */
  static const float FENCE[PARK_COUNT][4] = { { -42, -42, 42, 42 }, { -42, -42, 42, 42 }, { -30, -80, 40, 70 }, { -64, -34, 84, 34 }, { -40, -40, 40, 40 } };
  G_LO = V(FENCE[G_PARK][0], 0, FENCE[G_PARK][1]), G_HI = V(FENCE[G_PARK][2], 0, FENCE[G_PARK][3]);
  (void)k;
  static const ground_fn GFS[PARK_COUNT] = { { 140, 60, 140, 500 }, { 420, 25, 60, 700 }, { 60, 40, 320, 450 }, { 380, 25, 60, 700 }, { 70, 120, 380, 500 } };
  GF = GFS[G_PARK];
  RNG = 12345u + (uint32_t)G_PARK * 7919u;
  memset(SB, 0, sizeof SB);
  t3_material *gm[3];
  gm[0] = tex_mat(TEX_GROUND, 1, 0, 1.0f, true);
  gm[1] = tex_mat(TEX_SAND, 1, 0, 0.8f, true);
  gm[2] = tex_mat(MAT_ASPHALT, 1, 0, 1.0f, true);
  build_ground(parent, gm);
  for (int i = 0; i < 3; i++) t3_release(gm[i]);
  switch (G_PARK) {
  case PARK_SUNNY: set_sunny(parent); sea(parent, G_HI.x + 50, G_HI.x + 54); break;
  case PARK_PLAZA: set_plaza(parent); break;
  case PARK_MEGA: set_mega(parent); break;
  case PARK_ROOFS: set_roofs(parent); break;
  default: set_barn(parent); break;
  }
  for (int s = 0; s < SB_COUNT; s++) {
    if (!SB[s].nv) { mb_free(&SB[s]); continue; }
    t3_geometry *g = mb_finish(&SB[s]);
    t3_material *m = sb_material(s);
    t3_mesh *me = t3_mesh_new(g, m);
    /* no scenery in the sun's map: it lies outside the park the map covers, and a town in the shadow pass costs more than it shows */
    me->base.cast_shadow = false, me->base.receive_shadow = false;
    cw_node n = cw_node_new("Node3D", "Scenery");
    cw_node_add_child(parent, n);
    cw_node3d_set_object(n, &me->base);
    t3_release(me), t3_release(g), t3_release(m);
  }
}
