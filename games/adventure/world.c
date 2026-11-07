/* adventure: the world. One terrain under four regions joined by stairs, ramps, a passage behind a cracked wall, a
 * shortcut door and a gate that wants two keys:
 *
 *   the meadow (south, the start)   rolling green, a ruined village, the first shrine, stairs up north to the canyon,
 *                                   a door east to the marsh (opened from the marsh side), the keep's gate west
 *   the canyon (north)              amber sandstone floor under violet cliffs, a terrace with the maul's chest, a
 *                                   ledge with a heart, the Thornwarden's ring of pillars up its north stairs, and a
 *                                   passage east to the marsh behind a cracked wall only the maul breaks
 *   the marsh (east)                teal pools and stilt trees, the Tidecaller's marble temple at its south end
 *   the keep (west)                 a red stone keep on an ember hill: a courtyard, a long walled ramp, the Ember
 *                                   King's throne top
 *
 * Everything is built here from tables at start-up, the same on every host: the terrain's heights and paint (a
 * function of x and z, cw_math only), the buildings as boxes (merged per material and region for drawing, one static
 * body per region for collision), the props as glTF instances seated on the ground they stand on (world_props lists
 * them for the grounding check), and the objects the rules touch (g_obj). Paging: the props, buildings and foes of a
 * region more than one step from the player's are hidden and still. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/physics_nodes.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/terrain.h"
#include "cartwheel/vfs.h"
#include "game.h"

const char *const REGION_NAME[RG_COUNT] = { "THE GREEN HOLLOW", "THE AMBER CANYON", "THE TIDEMARSH", "THE EMBER KEEP", "THE FROSTSPINE HEIGHTS", "THE UNDERWAY", "THE WINDWARD SPIRE", "THE GLOWCAP WOOD" };

wobj g_obj[MAXOBJ];
int g_nobj;
wfoe g_foe[MAXE];
int g_nfoe;
wroute g_route[256];
int g_nroute;
int g_world_paged;

static cw_tree *W_tree;
static cw_terrain *W_terrain;
static cw_world3d W_phys;
static cw_node W_root, W_env, W_sun, W_sky, W_post;
static cw_node W_region_node[RG_COUNT];
static uint32_t W_hash;

cw_world3d world_phys(void) { return W_phys; }
uint32_t world_hash(void) { return W_hash; }

/* ── noise (integer hashing and cw maths: the same bits everywhere) ── */
static float hash01(int x, int z, uint32_t s) {
  uint32_t h = (uint32_t)x * 0x85EBCA77u ^ (uint32_t)z * 0xC2B2AE3Du ^ s * 0x9E3779B1u;
  h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12, h *= 0x297A2D39u, h ^= h >> 15;
  return (float)(h >> 8) / 16777216.0f;
}
static float vnoise(float x, float z, uint32_t s) {
  float fx = floorf(x), fz = floorf(z);
  int ix = (int)fx, iz = (int)fz;
  float u = x - fx, v = z - fz;
  u = u * u * (3 - 2 * u), v = v * v * (3 - 2 * v);
  float a = hash01(ix, iz, s), b = hash01(ix + 1, iz, s), c = hash01(ix, iz + 1, s), d = hash01(ix + 1, iz + 1, s);
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
static float fbm(float x, float z, uint32_t s, int oct) {
  float sum = 0, amp = 0.5f, norm = 0;
  for (int o = 0; o < oct; o++) sum += vnoise(x, z, s + (uint32_t)o * 31u) * amp, norm += amp, x *= 2.03f, z *= 2.03f, amp *= 0.5f;
  return sum / norm;
}
static float smooth01(float e0, float e1, float x) { return cw_smoothstep(e0, e1, x); }

/* the flora's species (see flora at the end), and the trees the builders set by hand */
enum { SP_OAK, SP_BIRCH, SP_ASPEN, SP_WILLOW, SP_PINE, SP_FIR, SP_WINDSWEPT, SP_DEEPWOOD, SP_GLOWCAP, SP_COUNT };
typedef struct fixedtree { int rg, sp; float x, z, s; } fixedtree;
static fixedtree FIXED[64];
static int nfixed;
static void flora_fixed(int rg, int sp, float x, float z, float s) { if (nfixed < 64) FIXED[nfixed++] = (fixedtree){ rg, sp, x, z, s }; }

/* ── the shape of the land ─────────────────────────────────────────── */
typedef struct zone { int rect; float cx, cz, rx, rz, h, fall, noise; int region, paint; } zone;
/* paint: the terrain layer the zone's flat ground takes (0 grass, 1 meadow flowers, 2 path, 3 sand, 4 rock, 5 mud,
 * 6 cobble, 7 ash) */
static const zone ZONES[] = {
  { 0, 160, 252, 72, 62, 12, 26, 1.6f, RG_MEADOW, 0 },     /* the meadow bowl */
  { 1, 160, 132, 44, 46, 20, 7, 0.5f, RG_CANYON, 3 },      /* the canyon floor */
  { 0, 160, 70, 19, 19, 27, 4, 0.0f, RG_CANYON, 6 },       /* the Thornwarden's ring */
  { 1, 207, 104, 12, 11, 26, 3, 0.2f, RG_CANYON, 3 },      /* the terrace with the chest */
  { 1, 121, 112, 6, 8, 25, 3, 0.0f, RG_CANYON, 3 },        /* the west ledge (a heart) */
  { 0, 268, 212, 46, 88, 7.4f, 18, 1.5f, RG_MARSH, 5 },    /* the marsh basin */
  { 0, 249, 142, 6, 6, 9.2f, 4, 0.0f, RG_MARSH, 2 },       /* the marsh shrine's knoll */
  { 0, 300, 188, 6, 6, 9.6f, 4, 0.3f, RG_MARSH, 0 },       /* the charm's island */
  { 1, 276, 276, 19, 17, 11, 3, 0.0f, RG_MARSH, 6 },       /* the temple court */
  { 0, 60, 168, 46, 82, 19, 16, 1.0f, RG_KEEP, 7 },        /* the keep hill */
  { 1, 64, 206, 17, 13, 22, 3, 0.0f, RG_KEEP, 6 },         /* the courtyard */
  { 1, 55, 108, 16, 16, 30, 3, 0.0f, RG_KEEP, 6 },         /* the throne top */
  { 0, 432, 118, 62, 56, 46, 14, 2.0f, RG_HEIGHTS, 1 },    /* the frostspine heights: a snowy plateau east of the canyon */
  { 1, 270, 66, 7, 30, 20, 4, 0.4f, RG_HEIGHTS, 4 },       /* the gorge the cliff path bridges (its floor a way of its own) */
  { 1, 176, 360, 7, 36, 6, 3, 0.0f, RG_UNDER, 2 },         /* the underway: a cutting under the hollow's south hills, roofed */
  { 0, 195, 474, 72, 56, 16, 18, 1.2f, RG_SPIRE, 0 },      /* the windward spire's downs */
  { 1, 202, 470, 11, 11, 16, 3, 0.0f, RG_SPIRE, 6 },       /* the spire's court */
  { 0, 482, 336, 86, 96, 13, 20, 2.0f, RG_WOOD, 5 },       /* the glowcap wood */
};
#define NZONES ((int)(sizeof ZONES / sizeof ZONES[0]))
/* ramps: the ground along a segment, at a height running from one end to the other (stairs get a ramp 0.35 m under
 * their steps so the steps cover it) */
typedef struct ramp { float ax, az, ah, bx, bz, bh, hw, edge; int paint; } ramp;
static const ramp RAMPS[] = {
  { 160, 211, 12.2f, 160, 178, 19.65f, 3.2f, 3, 2 },       /* meadow to canyon (stairs) */
  { 160, 103, 19.65f, 160, 86, 26.65f, 3.2f, 3, 2 },       /* canyon to the ring (stairs) */
  { 180, 126, 20, 199, 108, 26, 3.2f, 3, 3 },              /* canyon to the terrace */
  { 133, 142, 19.65f, 124, 121, 24.65f, 2.2f, 2, 3 },      /* canyon to the west ledge (stairs) */
  { 198, 136, 20, 244, 141, 9.2f, 3.2f, 4, 2 },            /* canyon to the marsh (the passage) */
  { 272, 246, 7.6f, 274, 259, 11, 3.5f, 3, 2 },            /* marsh to the temple court */
  { 238, 216, 8.6f, 206, 222, 12.4f, 2.8f, 3, 2 },         /* marsh to the meadow (the shortcut) */
  { 120, 241, 12.2f, 97, 232, 16.5f, 3.2f, 4, 2 },         /* meadow to the keep's gate and up */
  { 97, 232, 16.5f, 80, 214, 22, 3.2f, 4, 6 },
  { 63, 192, 22, 58, 125, 30, 3.0f, 3, 6 },                /* the courtyard to the throne (the long ramp) */
  { 160, 300, 12.6f, 160, 252, 12.2f, 2.0f, 2, 2 },        /* the paths (painted, nearly flat) */
  { 160, 252, 12.2f, 160, 211, 12.2f, 2.0f, 2, 2 },
  { 160, 252, 12.2f, 122, 241, 12.2f, 1.8f, 2, 2 },
  { 160, 252, 12.2f, 205, 228, 12.4f, 1.8f, 2, 2 },
  { 160, 176, 20, 160, 103, 19.65f, 2.0f, 2, 2 },
  { 248, 144, 9.2f, 270, 238, 7.6f, 1.8f, 2, 2 },
  /* the switchbacks up the cliffs from the marsh to the heights */
  { 304, 168, 8.4f, 336, 150, 18, 2.6f, 3, 2 },
  { 336, 150, 18, 316, 126, 28, 2.6f, 3, 2 },
  { 316, 126, 28, 352, 108, 38, 2.6f, 3, 2 },
  { 352, 108, 38, 378, 118, 46, 2.6f, 3, 2 },
  /* the cliff path west from the heights, over the gorge (a bridge), down to the Thornwarden's ring */
  { 376, 98, 46, 300, 70, 44, 2.4f, 3, 4 },
  { 300, 70, 44, 282, 70, 44, 2.4f, 2, 4 },               /* the abutments either side of the bridge */
  { 258, 70, 43.2f, 240, 70, 42, 2.4f, 2, 4 },
  { 240, 70, 42, 214, 68, 38, 2.4f, 3, 4 },
  { 214, 68, 38, 181, 70, 27.2f, 2.6f, 3, 2 },
  /* the gorge floor: from the marsh shrine up under the bridge */
  { 252, 136, 9.4f, 270, 98, 20, 2.4f, 3, 2 },
  /* the underway: down out of the hollow, along the cutting, up onto the spire's downs */
  { 160, 300, 12.6f, 168, 326, 6, 3.0f, 2, 2 },
  { 184, 394, 6, 192, 420, 16, 3.0f, 2, 2 },
  { 192, 420, 16, 200, 458, 16, 1.8f, 2, 2 },
  /* the marsh east to the glowcap wood */
  { 306, 246, 8.2f, 360, 282, 14, 3.0f, 4, 2 },
  { 360, 282, 14, 404, 304, 13, 2.4f, 3, 2 },
};
#define NRAMPS ((int)(sizeof RAMPS / sizeof RAMPS[0]))

static float zone_w(const zone *z, float x, float y) {
  float dx = x - z->cx, dy = y - z->cz, out;
  if (z->rect) {
    float ex = fabsf(dx) - z->rx, ey = fabsf(dy) - z->rz;
    ex = ex > 0 ? ex : 0, ey = ey > 0 ? ey : 0;
    out = sqrtf(ex * ex + ey * ey);
  } else {
    float d = sqrtf((dx / z->rx) * (dx / z->rx) + (dy / z->rz) * (dy / z->rz));
    out = d > 1 ? (d - 1) * (z->rx < z->rz ? z->rx : z->rz) : 0;
  }
  return 1 - smooth01(0, z->fall, out);
}
static float ramp_w(const ramp *r, float x, float z, float *h, float *along) {
  float ex = r->bx - r->ax, ez = r->bz - r->az, l2 = ex * ex + ez * ez;
  float t = ((x - r->ax) * ex + (z - r->az) * ez) / l2;
  float tc = t < 0 ? 0 : t > 1 ? 1 : t;
  float qx = r->ax + ex * tc - x, qz = r->az + ez * tc - z, d = sqrtf(qx * qx + qz * qz);
  *h = r->ah + (r->bh - r->ah) * tc;
  if (along) *along = t;
  return 1 - smooth01(r->hw, r->hw + r->edge, d);
}
/* the analytic land (the terrain samples it at whole metres) */
static float land_ex(float x, float z, int *paint, int *region, int *over, int *blend) {
  float h = 31 + 9 * fbm(x / 55, z / 55, 7, 4); /* the highlands between the regions */
  float bd = fminf(fminf(x, WORLD_N - x), fminf(z, WORLD_N - z));
  h += 26 * (1 - smooth01(0, 60, bd)) + 10 * fbm(x / 23, z / 23, 9, 3) * (1 - smooth01(0, 90, bd)); /* mountains round the edge, craggy */
  int pt = 4, rg = -1, opt = -1;
  float best = 0, ob = 0;
  for (int i = 0; i < NZONES; i++) {
    const zone *zn = &ZONES[i];
    float w = zone_w(zn, x, z);
    if (w <= 0) continue;
    float target = zn->h + zn->noise * (fbm(x / 18, z / 18, 100 + (uint32_t)i, 3) - 0.5f) * 2;
    h = h + (target - h) * w;
    if (w > 0.5f) pt = zn->paint;
    if (w > best) best = w, rg = zn->region;
  }
  if (best < 0.5f) { /* the highlands take the colour of the nearest region: grass, sand, moss, ash */
    static const int HIGH[RG_COUNT] = { 0, 4, 0, 7, 1, 4, 0, 5 };
    float bd = 1e30f;
    int nr = 0;
    for (int i = 0; i < NZONES; i++) {
      float d = sqrtf(dist2(x, z, ZONES[i].cx, ZONES[i].cz)) - (ZONES[i].rx + ZONES[i].rz) * 0.5f;
      if (d < bd) bd = d, nr = ZONES[i].region;
    }
    pt = HIGH[nr];
  }
  for (int i = 0; i < NRAMPS; i++) {
    float rh;
    float w = ramp_w(&RAMPS[i], x, z, &rh, NULL);
    if (w <= 0) continue;
    h = h + (rh - h) * w;
    if (w > 0.7f) pt = RAMPS[i].paint, ob = 0;
    else if (w > 0.1f && (w - 0.1f) / 0.6f > ob) ob = (w - 0.1f) / 0.6f, opt = RAMPS[i].paint; /* a soft edge into what is beside it */
  }
  if (paint) *paint = pt;
  if (region) *region = rg;
  if (over) *over = opt == pt ? pt : opt;
  if (blend) *blend = opt == pt ? 0 : (int)(ob * 255);
  return h;
}
static float land(float x, float z, int *paint, int *region) { return land_ex(x, z, paint, region, NULL, NULL); }
float world_height(float x, float z) { return W_terrain ? cw_terrain_height_at(W_terrain, x, z) : land(x, z, NULL, NULL); }

int world_region_at(float x, float z) {
  /* the strongest zone; between them, the nearest zone centre */
  int rg = -1;
  land(x, z, NULL, &rg);
  if (rg >= 0) return rg;
  float bd = 1e30f;
  for (int i = 0; i < NZONES; i++) {
    float d = dist2(x, z, ZONES[i].cx, ZONES[i].cz);
    if (d < bd) bd = d, rg = ZONES[i].region;
  }
  return rg < 0 ? 0 : rg;
}

/* ── geometry: boxes merged per material and region ─────────────────── */
typedef struct mbuf { float *p, *n, *uv; uint32_t *ix; int nv, ni, cv, ci; } mbuf;
static mbuf MB[RG_COUNT][MT_COUNT];
static void mb_reserve(mbuf *b, int nv, int ni) {
  if (b->nv + nv > b->cv) {
    b->cv = (b->nv + nv) * 2 + 256;
    b->p = realloc(b->p, sizeof(float) * 3 * (size_t)b->cv), b->n = realloc(b->n, sizeof(float) * 3 * (size_t)b->cv), b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->cv);
  }
  if (b->ni + ni > b->ci) b->ci = (b->ni + ni) * 2 + 384, b->ix = realloc(b->ix, sizeof(uint32_t) * (size_t)b->ci);
}
/* a box: centre, half sizes, yaw; uv from world metres on each face (tile metres per repeat) */
static void mb_box(mbuf *b, float cx, float cy, float cz, float hx, float hy, float hz, float yaw, float tile) {
  float s = cw_sin(yaw), c = cw_cos(yaw);
  static const int F[6][4][3] = { { { 1, -1, 1 }, { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } }, { { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 } },
                                  { { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 }, { -1, 1, -1 } }, { { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 } },
                                  { { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } }, { { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } } };
  static const float N[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
  mb_reserve(b, 24, 36);
  for (int f = 0; f < 6; f++) {
    uint32_t base = (uint32_t)b->nv;
    for (int k = 0; k < 4; k++) {
      float lx = (float)F[f][k][0] * hx, ly = (float)F[f][k][1] * hy, lz = (float)F[f][k][2] * hz;
      float wx = cx + lx * c + lz * s, wz = cz - lx * s + lz * c, wy = cy + ly;
      float *p = b->p + 3 * b->nv, *n = b->n + 3 * b->nv, *uv = b->uv + 2 * b->nv;
      p[0] = wx, p[1] = wy, p[2] = wz;
      n[0] = N[f][0] * c + N[f][2] * s, n[1] = N[f][1], n[2] = -N[f][0] * s + N[f][2] * c;
      /* the face's two axes, in the box's frame (so a wall's courses stay level along it) */
      float u = f < 2 ? lz : lx, v = f == 2 || f == 3 ? lz : ly;
      if (f == 1 || f == 4) u = f == 1 ? lz : lx; /* (mirrored faces keep their direction) */
      float off = f < 2 ? cx * 0.37f + cz * 0.61f : cx * 0.53f + cz * 0.29f;
      uv[0] = (u + off) / tile, uv[1] = -(v + (f == 2 || f == 3 ? 0 : cy)) / tile;
      b->nv++;
    }
    uint32_t q[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
    memcpy(b->ix + b->ni, q, sizeof q), b->ni += 6;
  }
}

/* collision: one static body per region, every box a shape on it */
static cw_body3d W_body[RG_COUNT];
static void col_box(int rg, float cx, float cy, float cz, float hx, float hy, float hz, float yaw) {
  if (!cw_body3d_valid(W_body[rg])) {
    cw_body3d_def bd = cw_body3d_default_def();
    bd.type = CW_BODY_STATIC;
    bd.name = "region";
    W_body[rg] = cw_body3d_create(W_phys, &bd);
  }
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.material.friction = 0.6f;
  float s, c;
  cw_sincos(yaw * 0.5f, &s, &c);
  cw_shape3d_box(W_body[rg], &sd, (t3_vec3){ hx, hy, hz }, (cw_xform3){ .p = { cx, cy, cz }, .q = { 0, s, 0, c } });
  W_hash = (W_hash ^ (uint32_t)(int)(cx * 100 + cz * 37 + cy * 11)) * 16777619u;
}

/* the box catalogue: each box remembers its region and material; walkable tops are found by world_ground */
typedef struct sbox { float cx, cy, cz, hx, hy, hz, yaw; int rg, mt; bool span; } sbox;
static bool g_span; /* boxes added while set are spans (a bridge deck): they stand on their ends */
static sbox *BOX;
static int nbox, capbox;
static void box_add(int rg, int mt, float cx, float cy, float cz, float hx, float hy, float hz, float yaw, bool solid) {
  if (nbox == capbox) capbox = capbox * 2 + 256, BOX = realloc(BOX, sizeof(sbox) * (size_t)capbox);
  BOX[nbox++] = (sbox){ cx, cy, cz, hx, hy, hz, yaw, rg, mt, g_span };
  static const float TILE[MT_COUNT] = { 2.4f, 2.4f, 3.0f, 2.5f, 2.2f, 2.0f, 2.0f };
  mb_box(&MB[rg][mt], cx, cy, cz, hx, hy, hz, yaw, TILE[mt]);
  if (solid) col_box(rg, cx, cy, cz, hx, hy, hz, yaw);
}
/* the lowest terrain under a footprint (a box sinks to it so no edge hangs in the air) */
static float ground_min(float cx, float cz, float hx, float hz, float yaw) {
  float s = cw_sin(yaw), c = cw_cos(yaw), lo = 1e30f;
  for (int i = -2; i <= 2; i++)
    for (int k = -2; k <= 2; k++) {
      float lx = hx * (float)i / 2, lz = hz * (float)k / 2;
      float h = world_height(cx + lx * c + lz * s, cz - lx * s + lz * c);
      if (h < lo) lo = h;
    }
  return lo;
}
static float ground_max(float cx, float cz, float hx, float hz, float yaw) {
  float s = cw_sin(yaw), c = cw_cos(yaw), hi = -1e30f;
  for (int i = -2; i <= 2; i++)
    for (int k = -2; k <= 2; k++) {
      float lx = hx * (float)i / 2, lz = hz * (float)k / 2;
      float h = world_height(cx + lx * c + lz * s, cz - lx * s + lz * c);
      if (h > hi) hi = h;
    }
  return hi;
}
/* a wall from (x0, z0) to (x1, z1): its foot sunk to the lowest ground along it, its top `h` over the highest */
static void wall(int rg, int mt, float x0, float z0, float x1, float z1, float h, float thick) {
  float dx = x1 - x0, dz = z1 - z0, len = sqrtf(dx * dx + dz * dz), yaw = cw_atan2(dx, dz) - CW_PI / 2;
  float cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
  float lo = ground_min(cx, cz, len / 2, thick / 2, yaw) - 0.6f, hi = ground_max(cx, cz, len / 2, thick / 2, yaw) + h;
  box_add(rg, mt, cx, (lo + hi) / 2, cz, len / 2, (hi - lo) / 2, thick / 2, yaw, true);
  /* weathering: a broken course or two standing up from the top, a fallen block at the foot */
  uint32_t hs = (uint32_t)(int)(cx * 13 + cz * 7);
  float c = cw_cos(yaw), s = cw_sin(yaw);
  for (int k = 0; k < 2 && len > 3; k++) {
    float t = (hash01((int)hs, k, 801) - 0.5f) * (len - 1.2f), w = 0.4f + 0.5f * hash01((int)hs, k, 802), hh = 0.15f + 0.35f * hash01((int)hs, k, 803);
    box_add(rg, mt, cx + c * t, hi + hh / 2 - 0.02f, cz - s * t, w / 2, hh / 2 + 0.02f, thick / 2 - 0.04f, yaw, true);
  }
  if (h > 1.4f && hash01((int)hs, 3, 804) < 0.7f) {
    float t = (hash01((int)hs, 4, 805) - 0.5f) * (len - 1), side = hash01((int)hs, 5, 806) < 0.5f ? -1.f : 1.f, d = thick / 2 + 0.45f;
    float rx = cx + c * t + s * d * side, rz = cz - s * t + c * d * side, g = ground_min(rx, rz, 0.3f, 0.25f, yaw + 0.4f);
    box_add(rg, mt, rx, g + 0.12f, rz, 0.3f, 0.16f, 0.25f, yaw + 0.4f, true);
  }
}
/* a flight of stairs up from a to b (the steps are solid blocks down into the ground), with low side walls */
static void stairs(int rg, int mt, float ax, float az, float ay, float bx, float bz, float by, float width, bool sides) {
  float dx = bx - ax, dz = bz - az, run = sqrtf(dx * dx + dz * dz), yaw = cw_atan2(dx, dz);
  int n = (int)ceilf((by - ay) / 0.3f);
  float rise = (by - ay) / (float)n, depth = run / (float)n;
  for (int i = 0; i < n; i++) {
    float t = ((float)i + 0.5f) / (float)n, cx = ax + dx * t, cz = az + dz * t, top = ay + rise * (float)(i + 1);
    float lo = ground_min(cx, cz, width / 2, depth / 2, yaw) - 0.5f;
    if (lo > top - 0.3f) lo = top - 0.3f;
    box_add(rg, mt, cx, (lo + top) / 2, cz, width / 2, (top - lo) / 2, depth / 2 + 0.02f, yaw, true);
  }
  if (sides)
    for (int sd = -1; sd <= 1; sd += 2) {
      float ox = cw_cos(yaw) * (width / 2 + 0.3f) * (float)sd, oz = -cw_sin(yaw) * (width / 2 + 0.3f) * (float)sd;
      for (int i = 0; i < n; i += 3) {
        float t = ((float)i + 1.5f) / (float)n, cx = ax + dx * t + ox, cz = az + dz * t + oz, top = ay + rise * (float)(i + 2) + 0.9f;
        float lo = ground_min(cx, cz, 0.3f, depth * 1.5f, yaw) - 0.5f;
        box_add(rg, MT_STONE, cx, (lo + top) / 2, cz, 0.3f, (top - lo) / 2, depth * 1.5f + 0.01f, yaw, true);
      }
    }
}
/* a ruined house: four walls with a door gap toward `door` (0 south, 1 east, 2 north, 3 west), a broken top */
static void house(int rg, float cx, float cz, float w, float d, float h, int door, bool roofed) {
  float x0 = cx - w / 2, x1 = cx + w / 2, z0 = cz - d / 2, z1 = cz + d / 2;
  float gap = 1.6f;
  /* south (z1), east (x1), north (z0), west (x0); the door side in two pieces */
  float hs[4] = { h, h * 0.85f, h * 1.0f, h * 0.7f };
  if (door == 0) wall(rg, MT_STONE, x0, z1, cx - gap / 2, z1, hs[0], 0.5f), wall(rg, MT_STONE, cx + gap / 2, z1, x1, z1, hs[0] * 0.8f, 0.5f);
  else wall(rg, MT_STONE, x0, z1, x1, z1, hs[0], 0.5f);
  if (door == 1) wall(rg, MT_STONE, x1, z0, x1, cz - gap / 2, hs[1], 0.5f), wall(rg, MT_STONE, x1, cz + gap / 2, x1, z1, hs[1], 0.5f);
  else wall(rg, MT_STONE, x1, z0, x1, z1, hs[1], 0.5f);
  if (door == 2) wall(rg, MT_STONE, x0, z0, cx - gap / 2, z0, hs[2], 0.5f), wall(rg, MT_STONE, cx + gap / 2, z0, x1, z0, hs[2], 0.5f);
  else wall(rg, MT_STONE, x0, z0, x1, z0, hs[2], 0.5f);
  if (door == 3) wall(rg, MT_STONE, x0, z0, x0, cz - gap / 2, hs[3], 0.5f), wall(rg, MT_STONE, x0, cz + gap / 2, x0, z1, hs[3], 0.5f);
  else wall(rg, MT_STONE, x0, z0, x0, z1, hs[3], 0.5f);
  if (roofed) { /* a slate roof as two pitched slabs over the walls */
    float top = ground_max(cx, cz, w / 2, d / 2, 0) + h;
    for (int sd = -1; sd <= 1; sd += 2) {
      float pitch = 0.5f;
      float hz = d / 4 + 0.4f, cz2 = cz + (float)sd * d / 4;
      sbox b = { cx, top + d / 4 * 0.55f, cz2, w / 2 + 0.4f, 0.12f, hz / cw_cos(pitch) * 0.98f, 0, rg, MT_SLATE };
      /* a pitched slab is a box turned about x: drawn only (the walls carry it) */
      mbuf *m = &MB[rg][MT_SLATE];
      int v0 = m->nv;
      mb_box(m, b.cx, b.cy, b.cz, b.hx, b.hy, b.hz, 0, 2.0f);
      float s = cw_sin((float)sd * pitch), c = cw_cos((float)sd * pitch);
      for (int v = v0; v < m->nv; v++) { /* rotate about the slab's centre line */
        float *p = m->p + 3 * v, *n = m->n + 3 * v;
        float y = p[1] - b.cy, z = p[2] - b.cz;
        p[1] = b.cy + y * c + z * s, p[2] = b.cz - y * s + z * c;
        float ny = n[1], nz = n[2];
        n[1] = ny * c + nz * s, n[2] = -ny * s + nz * c;
      }
    }
  }
}

/* ── props: glTF instances seated on the ground ────────────────────── */
#define MAXPROP 512
static wprop PROP[MAXPROP];
static int nprop;
static cw_res PROP_RES[32];
static const char *PROP_FILE[32];
static int nprop_res;
int world_props(const wprop **out) { *out = PROP; return nprop; }
int world_box_count(void) { return nbox; }
static t3_object *prop_obj(const char *file) {
  int k = 0;
  for (; k < nprop_res; k++) if (!strcmp(PROP_FILE[k], file)) break;
  if (k == nprop_res) {
    char path[64];
    snprintf(path, sizeof path, "models/%s.glb", file);
    PROP_FILE[k] = file, PROP_RES[k] = cw_res_load(path, CW_RES_GLTF), nprop_res++;
  }
  return cw_gltf_instantiate(PROP_RES[k]);
}
static void shadows(t3_object *o, bool cast) {
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH || o->type == T3_INSTANCED_MESH) o->cast_shadow = cast, o->receive_shadow = true;
  for (int i = 0; i < o->child_count; i++) shadows(o->children[i], cast);
}
/* a prop at (x, z) on the ground under it (`r`: its footprint, the lowest ground under it so no edge floats; sink
 * pushes it in), facing yaw, scaled. Returns its node. */
static cw_node prop(int rg, const char *file, float x, float z, float yaw, float scale, float r, float sink, bool cast) {
  float y = (r > 0 ? ground_min(x, z, r, r, 0) : world_height(x, z)) - sink;
  /* stood on a building: the top of the box under it */
  float gy = world_ground(x, z, y + 3);
  if (gy > y + 0.02f) y = gy - sink;
  t3_object *o = prop_obj(file);
  if (!o) { cw_error("adventure: %s did not load", file); return (cw_node){ 0 }; }
  shadows(o, cast);
  char nm[32];
  snprintf(nm, sizeof nm, "P%d_%s", nprop, file);
  cw_node n = cw_node_new("Node3D", nm);
  cw_node_add_child(W_region_node[rg], n);
  cw_node3d_set_object(n, o);
  t3_release(o);
  cw_node3d_set_position(n, (t3_vec3){ x, y, z });
  cw_node3d_set_rotation(n, (t3_vec3){ 0, yaw, 0 });
  cw_node3d_set_scale(n, (t3_vec3){ scale, scale, scale });
  if (nprop == 7 && getenv("ADV_FLOAT_BUG")) y += 0.5f, cw_node3d_set_position(n, (t3_vec3){ x, y, z }); /* the grounding check's control: one tree lifted */
  if (nprop < MAXPROP) PROP[nprop++] = (wprop){ x, y + sink, z, r, file, rg };
  return n;
}
/* a tree's crown as a box the camera stops at (layer 3: only the camera's sweep sees it; bodies walk under it) */
static void crown(int rg, float x, float y, float z, float hr, float hy, float cy) {
  if (!cw_body3d_valid(W_body[rg])) col_box(rg, x, -50, z, 0.1f, 0.1f, 0.1f, 0); /* (makes the region's body) */
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.layer = 3;
  cw_shape3d_box(W_body[rg], &sd, (t3_vec3){ hr, hy, hr }, (cw_xform3){ .p = { x, y + cy, z }, .q = { 0, 0, 0, 1 } });
}
/* the props that also block (a tree trunk, a boulder, a pillar, a brazier): a box in the region's body */
static void prop_solid(int rg, const char *file, float x, float z, float yaw, float scale, float r, float sink, float hr, float h) {
  cw_node n = prop(rg, file, x, z, yaw, scale, r, sink, true);
  if (!n.id) return;
  t3_vec3 p = cw_node3d_position(n);
  col_box(rg, x, p.y + h / 2, z, hr, h / 2, hr, yaw);
  if (!strcmp(file, "tree")) crown(rg, x, p.y, z, 2.0f * scale, 1.4f * scale, 4.8f * scale);
  if (!strcmp(file, "tree_gold")) crown(rg, x, p.y, z, 1.6f * scale, 1.2f * scale, 3.5f * scale);
  if (!strcmp(file, "mangrove")) crown(rg, x, p.y, z, 2.0f * scale, 1.2f * scale, 3.8f * scale);
}

/* ── the objects the rules touch ───────────────────────────────────── */
static cw_node OBJ_NODE[MAXOBJ], OBJ_PART[MAXOBJ];
static cw_body3d OBJ_BODY[MAXOBJ];
static int obj_add(wobj o) {
  if (g_nobj >= MAXOBJ) return -1;
  g_obj[g_nobj] = o;
  return g_nobj++;
}
static void dress(int rg, float x, float z, int seed) {
  static const char *const KIND[5] = { "barrel", "crate", "sacks", "barrel", "cart" };
  for (int k = 0; k < 3; k++) {
    int kind = (int)(hash01(seed, k, 820) * 5);
    float a = hash01(seed, k, 821) * CW_TAU, r = 0.8f * (float)k;
    float px = x + cw_cos(a) * r, pz = z + cw_sin(a) * r;
    if (kind == 4 && k) continue; /* one cart at most, first */
    float fr = kind == 4 ? 1.3f : 0.4f;
    prop_solid(rg, KIND[kind], px, pz, a, 1.0f, fr, 0.02f, kind == 4 ? 1.0f : 0.35f, kind == 4 ? 1.1f : 0.8f);
  }
}
static void lamp_post(int rg, float x, float z, float yaw) {
  cw_node n = prop(rg, "lantern_post", x, z, yaw, 1.0f, 0.15f, 0.05f, true);
  if (!n.id) return;
  t3_vec3 p = cw_node3d_position(n);
  col_box(rg, x, p.y + 1.2f, z, 0.08f, 1.2f, 0.08f, 0);
  cw_node l = cw_node_new("OmniLight3D", "LampLight");
  cw_node_add_child(W_region_node[rg], l);
  cw_node_set(l, "color", cw_val_color(1, 0.7f, 0.38f, 1));
  cw_node_set(l, "energy", cw_val_float(10));
  cw_node_set(l, "range", cw_val_float(8));
  cw_node3d_set_position(l, (t3_vec3){ x + cw_sin(yaw) * 0.48f, p.y + 1.85f, z + cw_cos(yaw) * 0.48f });
}
static int shrine_obj[8], nshrine;
int world_shrine_obj(int s) { return s >= 0 && s < nshrine ? shrine_obj[s] : -1; }
static int shrine(int rg, float x, float z, float yaw) {
  float y = world_height(x, z);
  int i = obj_add((wobj){ OB_SHRINE, rg, x, y, z, yaw, 0, 0, 0, 0, -1, -1, 0 });
  cw_node n = prop(rg, "shrine", x, z, yaw, 1, 1.6f, 0.05f, true);
  OBJ_NODE[i] = n;
  /* the floating crystal, a cyan light */
  cw_node c = cw_node_new("Node3D", "Crystal");
  cw_node_add_child(W_region_node[rg], c);
  t3_object *o = prop_obj("crystal");
  cw_node3d_set_object(c, o), t3_release(o);
  cw_node3d_set_position(c, (t3_vec3){ x, y + 1.55f, z });
  cw_node3d_set_scale(c, (t3_vec3){ 0.6f, 0.6f, 0.6f });
  OBJ_PART[i] = c;
  cw_node l = cw_node_new("OmniLight3D", "ShrineLight");
  cw_node_add_child(W_region_node[rg], l);
  cw_node_set(l, "color", cw_val_color(0.45f, 0.95f, 1, 1));
  cw_node_set(l, "energy", cw_val_float(24));
  cw_node_set(l, "range", cw_val_float(9));
  cw_node3d_set_position(l, (t3_vec3){ x, y + 2.2f, z });
  col_box(rg, x, y + 0.24f, z, 1.1f, 0.24f, 1.1f, 0);
  float s = cw_sin(yaw), c2 = cw_cos(yaw);
  for (int sd = -1; sd <= 1; sd += 2) col_box(rg, x + c2 * 0.85f * (float)sd, y + 1.5f, z - s * 0.85f * (float)sd, 0.16f, 1.05f, 0.2f, yaw);
  shrine_obj[nshrine++] = i;
  return i;
}
static int chest(int rg, float x, float z, float yaw, int item, int flag) {
  float y = ground_min(x, z, 0.5f, 0.5f, yaw);
  int i = obj_add((wobj){ OB_CHEST, rg, x, y, z, yaw, 1, 0.6f, 0.62f, item, flag, -1, 0 });
  OBJ_NODE[i] = prop(rg, "chest", x, z, yaw, 1, 0.5f, 0.02f, true);
  cw_node lid = cw_node_new("Node3D", "Lid");
  cw_node_add_child(OBJ_NODE[i], lid);
  t3_object *o = prop_obj("chest_lid");
  shadows(o, true);
  cw_node3d_set_object(lid, o), t3_release(o);
  cw_node3d_set_position(lid, (t3_vec3){ 0, 0.6f, -0.31f });
  OBJ_PART[i] = lid;
  col_box(rg, x, y + 0.4f, z, 0.5f, 0.4f, 0.31f, yaw);
  return i;
}
static int pickup(int rg, float x, float z, int item, int flag) {
  float y = world_ground(x, z, world_height(x, z) + 6);
  int i = obj_add((wobj){ OB_PICKUP, rg, x, y, z, 0, 0, 0, 0, item, flag, -1, 0 });
  static const char *const FILE_OF[IT_KINDS] = { "", "heart", "charm", "key_sun", "key_moon", "maul", "tonic" };
  cw_node n = cw_node_new("Node3D", "Pickup");
  cw_node_add_child(W_region_node[rg], n);
  t3_object *o = prop_obj(FILE_OF[item]);
  shadows(o, true);
  cw_node3d_set_object(n, o), t3_release(o);
  cw_node3d_set_position(n, (t3_vec3){ x, y + 0.5f, z });
  OBJ_NODE[i] = n;
  cw_node l = cw_node_new("OmniLight3D", "Glow");
  cw_node_add_child(n, l);
  cw_node_set(l, "color", item == IT_HEART ? cw_val_color(1, 0.35f, 0.45f, 1) : item == IT_KEY_MOON ? cw_val_color(0.7f, 0.5f, 1, 1) : cw_val_color(1, 0.85f, 0.45f, 1));
  cw_node_set(l, "energy", cw_val_float(10));
  cw_node_set(l, "range", cw_val_float(5));
  cw_node3d_set_position(l, (t3_vec3){ 0, 0.6f, 0 });
  OBJ_PART[i] = l;
  return i;
}
/* a panel that blocks a way (a gate's bars, a door, a cracked wall): its own node and body */
static int barrier(int kind, int rg, int mt, float x, float z, float yaw, float w, float h, float thick, int item, int flag, int side) {
  float y = ground_min(x, z, w / 2, thick / 2, yaw) - 0.3f;
  int i = obj_add((wobj){ kind, rg, x, y, z, yaw, w, h, thick, item, flag, -1, side });
  /* drawn as its own mesh */
  mbuf m = { 0 };
  float top = ground_max(x, z, w / 2, thick / 2, yaw) + h;
  if (kind == OB_GATE) { /* iron bars and cross rails */
    int bars = (int)(w / 0.35f);
    for (int b = 0; b < bars; b++) mb_box(&m, ((float)b + 0.5f) / (float)bars * w - w / 2, (top - y) / 2, 0, 0.05f, (top - y) / 2, 0.05f, 0, 2);
    for (int r = 0; r < 3; r++) mb_box(&m, 0, 0.6f + (float)r * (top - y - 1) / 2, 0, w / 2, 0.08f, 0.07f, 0, 2);
  } else if (kind == OB_DOOR) {
    mb_box(&m, 0, (top - y) / 2, 0, w / 2, (top - y) / 2, thick / 2, 0, 2.2f);
    for (int r = 0; r < 2; r++) mb_box(&m, 0, 1.0f + (float)r * (top - y - 2) , thick / 2 + 0.02f, w / 2 - 0.1f, 0.1f, 0.03f, 0, 2);
  } else { /* the cracked wall: rough blocks */
    int cols = (int)(w / 0.9f), rows = (int)((top - y) / 0.6f);
    for (int r = 0; r < rows; r++)
      for (int cc = 0; cc < cols; cc++) {
        float j = hash01(cc, r, 77) * 0.12f;
        mb_box(&m, ((float)cc + 0.5f + ((r & 1) ? 0.25f : -0.25f)) / (float)cols * w - w / 2, ((float)r + 0.5f) / (float)rows * (top - y), j, w / (float)cols / 2 - 0.02f,
               (top - y) / (float)rows / 2 - 0.02f, thick / 2 - j, 0, 2.4f);
      }
  }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, m.p, m.nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, m.n, m.nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, m.uv, m.nv, 2));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, m.ix, m.ni, 1));
  free(m.p), free(m.n), free(m.uv), free(m.ix);
  extern t3_material *world_material(int mt, float tint);
  t3_mesh *mesh = t3_mesh_new(g, world_material(kind == OB_GATE ? MT_IRON : mt, kind == OB_CRACKED ? 0.85f : 1));
  mesh->base.cast_shadow = true, mesh->base.receive_shadow = true;
  char nm[24];
  snprintf(nm, sizeof nm, "Barrier%d", i);
  cw_node n = cw_node_new("MeshInstance3D", nm);
  cw_node_add_child(W_region_node[rg], n);
  cw_mesh3d_set_procedural(n, mesh);
  t3_release(mesh), t3_release(g);
  cw_node3d_set_position(n, (t3_vec3){ x, y, z });
  cw_node3d_set_rotation(n, (t3_vec3){ 0, yaw, 0 });
  OBJ_NODE[i] = n;
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  OBJ_BODY[i] = cw_body3d_create(W_phys, &bd);
  float s, c;
  cw_sincos(yaw * 0.5f, &s, &c);
  cw_shape3d_def sd = cw_shape3d_default_def();
  cw_shape3d_box(OBJ_BODY[i], &sd, (t3_vec3){ w / 2, (top - y) / 2, thick / 2 + 0.05f }, (cw_xform3){ .p = { x, (top + y) / 2, z }, .q = { 0, s, 0, c } });
  return i;
}
static int lever(int rg, float x, float z, float yaw, int door, int flag) {
  float y = world_height(x, z);
  int i = obj_add((wobj){ OB_LEVER, rg, x, y, z, yaw, 0.5f, 1, 0.5f, 0, flag, door, 0 });
  OBJ_NODE[i] = prop(rg, "lever", x, z, yaw, 1, 0.25f, 0.05f, true);
  cw_node arm = cw_node_new("Node3D", "Arm");
  cw_node_add_child(OBJ_NODE[i], arm);
  t3_object *o = prop_obj("lever_arm");
  cw_node3d_set_object(arm, o), t3_release(o);
  cw_node3d_set_position(arm, (t3_vec3){ 0, 1.0f, 0 });
  cw_node3d_set_rotation(arm, (t3_vec3){ -0.7f, 0, 0 });
  OBJ_PART[i] = arm;
  col_box(rg, x, y + 0.5f, z, 0.25f, 0.5f, 0.25f, yaw);
  return i;
}

/* show an object's state: a gate up, a door open, a wall gone, a chest open, a pickup gone, a lever pulled */
static float OBJ_ANIM[MAXOBJ];
void world_obj_state(int i, bool done) {
  if (i < 0 || i >= g_nobj) return;
  wobj *o = &g_obj[i];
  OBJ_ANIM[i] = done ? 1 : 0;
  switch (o->kind) {
  case OB_GATE: case OB_DOOR: case OB_CRACKED:
    if (done && cw_body3d_valid(OBJ_BODY[i])) cw_body3d_destroy(OBJ_BODY[i]), OBJ_BODY[i] = (cw_body3d){ 0 };
    if (o->kind == OB_CRACKED) cw_node_set(OBJ_NODE[i], "visible", cw_val_bool(!done));
    else if (o->kind == OB_GATE) cw_node3d_set_position(OBJ_NODE[i], (t3_vec3){ o->x, o->y + (done ? o->sy + 0.4f : 0), o->z });
    else { /* a door swings open on its hinge (its left end) */
      float yaw = o->yaw + (done ? 1.75f : 0), s = cw_sin(o->yaw), c = cw_cos(o->yaw), hx = o->x - c * o->sx / 2, hz = o->z + s * o->sx / 2;
      float s2 = cw_sin(yaw), c2 = cw_cos(yaw);
      cw_node3d_set_position(OBJ_NODE[i], (t3_vec3){ hx + c2 * o->sx / 2, o->y, hz - s2 * o->sx / 2 });
      cw_node3d_set_rotation(OBJ_NODE[i], (t3_vec3){ 0, yaw, 0 });
    }
    break;
  case OB_CHEST: if (OBJ_PART[i].id) cw_node3d_set_rotation(OBJ_PART[i], (t3_vec3){ done ? -1.9f : 0, 0, 0 }); break;
  case OB_PICKUP: cw_node_set(OBJ_NODE[i], "visible", cw_val_bool(!done)); break;
  case OB_LEVER: if (OBJ_PART[i].id) cw_node3d_set_rotation(OBJ_PART[i], (t3_vec3){ done ? 0.7f : -0.7f, 0, 0 }); break;
  default: break;
  }
}

/* the highest walkable top under (x, z) below from_y: the terrain, or a box (a step, a floor) */
float world_ground(float x, float z, float from_y) {
  float best = world_height(x, z);
  for (int i = 0; i < nbox; i++) {
    const sbox *b = &BOX[i];
    float top = b->cy + b->hy;
    if (top <= best || top > from_y) continue;
    float dx = x - b->cx, dz = z - b->cz, s = cw_sin(b->yaw), c = cw_cos(b->yaw);
    float lx = dx * c - dz * s, lz = dx * s + dz * c;
    if (fabsf(lx) <= b->hx && fabsf(lz) <= b->hz) best = top;
  }
  return best;
}

/* ── materials ─────────────────────────────────────────────────────── */
static t3_texture *load_tex(const char *path, bool srgb) {
  size_t len = 0;
  void *png = cw_vfs_exists(path) ? cw_vfs_read(path, &len) : NULL;
  t3_texture *tx = png ? t3_texture_load_memory(png, len) : NULL;
  free(png);
  if (!tx) { cw_error("adventure: the texture %s did not load", path); return NULL; }
  tx->wrap_s = tx->wrap_t = T3_REPEAT;
  tx->flip_y = false;
  tx->generate_mipmaps = true;
  tx->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  tx->mag_filter = T3_LINEAR;
  tx->anisotropy = 8;
  tx->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  return tx;
}
static t3_material *MAT[MT_COUNT];
static t3_material *MAT_TINT[MT_COUNT];
t3_material *world_material(int mt, float tint) {
  static const char *const SET[MT_COUNT] = { "stone", "stone_red", "marble", "flag", "wood", "iron", "slate" };
  if (!MAT[mt]) {
    char p[64];
    t3_material *m = t3_mesh_standard_material_new(0xffffff);
    snprintf(p, sizeof p, "textures/%s_c.png", SET[mt]);
    t3_texture *c = load_tex(p, true);
    snprintf(p, sizeof p, "textures/%s_n.png", SET[mt]);
    t3_texture *n = load_tex(p, false);
    snprintf(p, sizeof p, "textures/%s_orm.png", SET[mt]);
    t3_texture *o = load_tex(p, false);
    if (c) t3_material_set_texture(m, T3_MAP, c), t3_release(c);
    if (n) t3_material_set_texture(m, T3_NORMAL_MAP, n), t3_release(n);
    if (o) t3_material_set_texture(m, T3_ROUGHNESS_MAP, o), t3_material_set_texture(m, T3_AO_MAP, o), t3_release(o);
    m->roughness = 1, m->metalness = mt == MT_IRON ? 0.6f : 0;
    m->normal_scale = (t3_vec2){ 1, 1 };
    MAT[mt] = m;
  }
  if (tint >= 0.999f) return MAT[mt];
  if (!MAT_TINT[mt]) {
    MAT_TINT[mt] = t3_material_clone(MAT[mt]);
    MAT_TINT[mt]->color = (t3_color){ 0.82f, 0.78f, 0.72f };
  }
  return MAT_TINT[mt];
}

/* ── foes and route ────────────────────────────────────────────────── */
static void foe(int kind, int rg, int group, float x, float z, float yaw) {
  if (g_nfoe >= MAXE) return;
  g_foe[g_nfoe] = (wfoe){ kind, rg, group, x, z, yaw, IS_BOSS(kind) ? 100 + kind : -1 };
  g_nfoe++;
}
static void route(float x, float z, int act, int obj, int need) {
  if (g_nroute < (int)(sizeof g_route / sizeof g_route[0])) g_route[g_nroute++] = (wroute){ x, z, need, act, obj };
}

/* ── the build ─────────────────────────────────────────────────────── */
static void paint_and_shape(void) {
  const int N = WORLD_N + 1;
  float *h = malloc(sizeof(float) * (size_t)N * N);
  uint32_t *ctl = malloc(sizeof(uint32_t) * (size_t)N * N);
  int *pt = malloc(sizeof(int) * (size_t)N * N), *ov = malloc(sizeof(int) * (size_t)N * N), *bl = malloc(sizeof(int) * (size_t)N * N);
  const cw_terrain_def *d = cw_terrain_get_def(W_terrain);
  for (int z = 0; z < N; z++)
    for (int x = 0; x < N; x++) h[z * N + x] = land_ex((float)x, (float)z, &pt[z * N + x], NULL, &ov[z * N + x], &bl[z * N + x]) - d->origin.y;
  cw_terrain_set_heights_m(W_terrain, 0, 0, N, N, h);
  /* paint: the zone's layer on the flat, rock on the steep, grass and flowers blended in the meadow, a path's edge
   * softened into what is beside it */
  for (int z = 0; z < N; z++)
    for (int x = 0; x < N; x++) {
      int i = z * N + x;
      float hl = h[z * N + CW_MAX(x - 1, 0)], hr = h[z * N + CW_MIN(x + 1, N - 1)], hd = h[CW_MAX(z - 1, 0) * N + x], hu = h[CW_MIN(z + 1, N - 1) * N + x];
      float slope = sqrtf((hr - hl) * (hr - hl) + (hu - hd) * (hu - hd)) * 0.5f;
      int base = pt[i], over = base;
      int blend = 0;
      float n = fbm((float)x / 9, (float)z / 9, 501, 3);
      if (base == 0) { over = 5, blend = (int)(cw_clampf((n - 0.62f) * 2.0f, 0, 1) * 150); }        /* darker moss in patches */
      if (base == 1) { over = 4, blend = (int)(cw_clampf((n - 0.45f) * 3.0f, 0, 1) * 230); }        /* rock through the snow, in broad patches */
      if (base == 5) { over = 0, blend = (int)(cw_clampf((h[i] + d->origin.y - 8.6f) * 1.2f, 0, 1) * 200); } /* grass on the marsh's dry humps */
      if (base == 7) { over = 4, blend = (int)(cw_clampf((n - 0.5f) * 2.0f, 0, 1) * 160); }
      if (bl[i] > 0) over = ov[i], blend = bl[i]; /* a path's soft edge */
      if (slope > 0.55f) { over = 4, blend = (int)(cw_clampf((slope - 0.55f) * 3.0f, 0, 1) * 255); }
      if (slope > 0.9f) base = 4, blend = 0;
      ctl[i] = cw_terrain_control(base, over, blend, (int)(hash01(x / 4, z / 4, 9) * 15), 0, false);
    }
  cw_terrain_set_control(W_terrain, 0, 0, N, N, ctl);
  cw_terrain_sync_physics(W_terrain);
  cw_terrain_scatter_invalidate(W_terrain);
  for (int i = 0; i < N * N; i += 97) W_hash = (W_hash ^ (uint32_t)(int)(h[i] * 1000)) * 16777619u;
  free(h), free(ctl), free(pt), free(ov), free(bl);
}

static void build_meadow(void) {
  const int R = RG_MEADOW;
  shrine(R, 160, 279, 0);
  house(R, 136, 252, 8, 6, 3.2f, 1, true);
  house(R, 186, 246, 7, 7, 2.6f, 3, false);
  house(R, 147, 226, 6, 5, 2.2f, 0, false);
  house(R, 178, 268, 6, 5, 3.0f, 2, true);
  dress(R, 141.5f, 256, 1), dress(R, 190.5f, 250.5f, 2), dress(R, 150, 222, 3), dress(R, 174, 272, 4);
  lamp_post(R, 156, 270, 0.4f), lamp_post(R, 165, 246, 2.8f), lamp_post(R, 155, 222, 0.2f);
  /* field walls along the path, broken in places */
  wall(R, MT_STONE, 150, 268, 150, 258, 0.9f, 0.6f);
  wall(R, MT_STONE, 170, 262, 170, 246, 0.9f, 0.6f);
  wall(R, MT_STONE, 150, 240, 150, 232, 0.8f, 0.6f);
  wall(R, MT_STONE, 196, 236, 206, 236, 1.1f, 0.6f);
  /* the old watch tower */
  box_add(R, MT_STONE, 204, ground_min(204, 262, 2, 2, 0) + 3, 262, 2, 3.6f, 2, 0, true);
  box_add(R, MT_STONE, 204, ground_min(204, 262, 2, 2, 0) + 6.8f, 262, 2.4f, 0.4f, 2.4f, 0, true);
  /* the cellar with a heart, walled up behind cracked stone */
  house(R, 124, 234, 5, 5, 2.4f, 1, true);
  barrier(OB_CRACKED, R, MT_STONE, 126.5f, 234, CW_PI / 2, 1.8f, 2.2f, 0.5f, 0, 1, 0);
  pickup(R, 123.5f, 234, IT_HEART, 2);
  /* the stairs up to the canyon, between buttresses */
  stairs(R, MT_STONE, 160, 211, 12.2f, 160, 178, 20.0f, 6.2f, true);
  /* the keep's gate: two towers and the bars (both keys) */
  for (int sd = -1; sd <= 1; sd += 2) box_add(RG_KEEP, MT_STONE_RED, 104, ground_min(104, 236 + (float)sd * 4.6f, 1.6f, 1.6f, 0) + 3.5f, 236 + (float)sd * 4.6f, 1.6f, 5.0f, 1.6f, 0, true);
  box_add(RG_KEEP, MT_STONE_RED, 104, ground_max(104, 236, 1, 3, 0) + 5.6f, 236, 1.4f, 0.6f, 6.2f, 0, true);
  barrier(OB_GATE, RG_KEEP, MT_IRON, 104, 236, CW_PI / 2, 6.2f, 4.6f, 0.3f, 3, 9, 0);
  /* trees and stones round the hollow */
  static const float TREES[][3] = { { 128, 272, 0.4f }, { 196, 280, 1.1f }, { 120, 255, 2.0f }, { 205, 248, 0.7f }, { 141, 213, 1.6f }, { 182, 216, 2.6f }, { 112, 266, 0.2f }, { 214, 268, 1.9f } };
  for (int i = 0; i < (int)(sizeof TREES / sizeof TREES[0]); i++) flora_fixed(R, i % 3 == 2 ? SP_BIRCH : SP_OAK, TREES[i][0], TREES[i][1], 1.0f + 0.15f * (float)(i % 3));
  static const float ROCKS[][3] = { { 172, 290, 0.3f }, { 145, 288, 1.2f }, { 192, 258, 2.1f }, { 130, 240, 0.8f } };
  for (int i = 0; i < (int)(sizeof ROCKS / sizeof ROCKS[0]); i++) prop_solid(R, "rock", ROCKS[i][0], ROCKS[i][1], ROCKS[i][2], 0.9f + 0.2f * (float)(i % 2), 0.9f, 0.25f, 0.8f, 1.0f);
  /* foes: raiders on the road and in the ruins */
  foe(K_RAIDER, R, 1, 156, 242, 3.0f);
  foe(K_RAIDER, R, 1, 165, 238, 3.4f);
  foe(K_RAIDER, R, 2, 188, 230, 2.4f);
  foe(K_RAIDER, R, 2, 194, 240, 2.0f);
  foe(K_LANCER, R, 2, 182, 236, 2.8f);
}
static void build_canyon(void) {
  const int R = RG_CANYON;
  shrine(R, 150, 168, 0.3f);
  /* sandstone outcrops and columns of rock */
  static const float SPIRES[][4] = { { 132, 160, 0.3f, 1.3f }, { 188, 160, 1.1f, 1.5f }, { 140, 104, 2.2f, 1.2f }, { 184, 98, 0.8f, 1.4f }, { 125, 128, 1.7f, 1.1f }, { 196, 150, 2.9f, 1.2f } };
  for (int i = 0; i < 6; i++) prop_solid(R, "rock_tall", SPIRES[i][0], SPIRES[i][1], SPIRES[i][2], SPIRES[i][3], 0.8f, 0.4f, 0.9f * SPIRES[i][3], 3.4f * SPIRES[i][3]);
  static const float GOLD[][2] = { { 145, 145 }, { 176, 168 }, { 168, 118 }, { 136, 120 }, { 205, 98 } };
  for (int i = 0; i < 5; i++) flora_fixed(R, SP_ASPEN, GOLD[i][0], GOLD[i][1], 1.1f);
  /* stairs: up to the ring (with buttresses), up to the west ledge */
  stairs(R, MT_STONE, 160, 103, 19.65f, 160, 86, 27.0f, 6.0f, true);
  stairs(R, MT_STONE, 133, 142, 19.8f, 124, 121, 25.0f, 3.6f, false);
  /* the ring: six pillars round the Thornwarden's floor */
  for (int k = 0; k < 6; k++) {
    float a = (float)k / 6 * CW_TAU + 0.26f, x = 160 + cw_cos(a) * 15, z = 70 + cw_sin(a) * 15;
    if (z > 80 && fabsf(x - 160) < 6) continue; /* the way in */
    prop_solid(R, "pillar", x, z, a, 1.0f, 0.55f, 0.05f, 0.45f, 4.6f);
  }
  /* the terrace: low walls and the maul's chest */
  wall(R, MT_STONE, 196, 96, 218, 96, 1.0f, 0.6f);
  wall(R, MT_STONE, 218, 96, 218, 112, 1.0f, 0.6f);
  chest(R, 210, 101, CW_PI, IT_MAUL, 3);
  pickup(R, 121, 108, IT_HEART, 4);
  /* the passage east, and the cracked wall across it */
  barrier(OB_CRACKED, R, MT_STONE, 214, 137.4f, CW_PI / 2 + 0.11f, 7.0f, 4.2f, 0.9f, 0, 5, 0);
  for (int sd = -1; sd <= 1; sd += 2) box_add(R, MT_STONE, 214, ground_min(214, 137.4f + (float)sd * 4.3f, 0.8f, 0.8f, 0) + 2.6f, 137.4f + (float)sd * 4.3f, 0.8f, 3.4f, 0.8f, 0.11f, true);
  /* foes */
  foe(K_RAIDER, R, 3, 152, 150, 3.1f);
  foe(K_RAIDER, R, 3, 160, 146, 3.3f);
  foe(K_BRUTE, R, 4, 172, 126, 3.0f);
  foe(K_RAIDER, R, 4, 178, 132, 2.6f);
  foe(K_BRUTE, R, 5, 206, 106, 3.3f);
  foe(K_RAIDER, R, 5, 200, 110, 3.6f);
  foe(K_LANCER, R, 6, 154, 108, 3.1f);
  foe(K_LANCER, R, 6, 166, 108, 3.2f);
  foe(K_RAIDER, R, 7, 124, 114, 0.4f);
  foe(K_THORN, R, 8, 160, 68, 0);
}
static void build_marsh(void) {
  const int R = RG_MARSH;
  shrine(R, 249, 143, -CW_PI / 2);
  /* stilt trees and reeds */
  static const float MANG[][2] = { { 262, 165 }, { 290, 172 }, { 252, 196 }, { 296, 214 }, { 258, 228 }, { 288, 240 }, { 246, 262 }, { 300, 258 } };
  for (int i = 0; i < 8; i++) flora_fixed(R, SP_WILLOW, MANG[i][0], MANG[i][1], 1.0f + 0.1f * (float)(i % 3));
  for (int i = 0; i < 18; i++) {
    float x = 240 + hash01(i, 1, 33) * 60, z = 150 + hash01(i, 2, 33) * 110;
    prop(R, "reeds", x, z, (float)i, 1.0f, 0, 0.05f, false);
  }
  /* the temple: a marble court, columns, steps */
  for (int k = 0; k < 10; k++) {
    float x = 262 + (float)(k % 5) * 7, z = k < 5 ? 263 : 289;
    prop_solid(R, "pillar", x, z, 0, 1.15f, 0.6f, 0.05f, 0.5f, 5.2f);
  }
  wall(R, MT_MARBLE, 257, 262, 257, 290, 1.2f, 0.8f);
  wall(R, MT_MARBLE, 295, 262, 295, 290, 1.2f, 0.8f);
  wall(R, MT_MARBLE, 257, 292, 295, 292, 2.4f, 0.8f);
  pickup(R, 300, 188, IT_CHARM, 6);
  /* the shortcut: a door in a wall across the passage west, its lever on the marsh side */
  for (int sd = -1; sd <= 1; sd += 2) box_add(R, MT_STONE, 224, ground_min(224, 219 + (float)sd * 3.0f, 0.7f, 0.7f, 0) + 2.5f, 219 + (float)sd * 3.0f, 0.7f, 3.2f, 0.7f, 0.19f, true);
  int door = barrier(OB_DOOR, R, MT_WOOD, 224, 219, CW_PI / 2 + 0.19f, 4.2f, 3.4f, 0.35f, 0, 7, 1);
  int lv = lever(R, 229, 214, -CW_PI / 2, door, 7);
  (void)lv;
  /* foes */
  foe(K_LANCER, R, 9, 256, 176, 3.0f);
  foe(K_LANCER, R, 9, 266, 182, 3.2f);
  foe(K_RAIDER, R, 10, 284, 204, 2.6f);
  foe(K_RAIDER, R, 10, 278, 212, 2.9f);
  foe(K_BRUTE, R, 11, 270, 238, 3.1f);
  foe(K_LANCER, R, 11, 278, 236, 3.4f);
  foe(K_TIDE, R, 12, 276, 278, 3.14f);
}
static void build_keep(void) {
  const int R = RG_KEEP;
  shrine(R, 58, 212, CW_PI / 2);
  /* the courtyard walls (open east to the ramp and north to the long ramp) */
  wall(R, MT_STONE_RED, 46, 192, 46, 220, 3.4f, 1.0f);
  wall(R, MT_STONE_RED, 46, 220, 82, 220, 3.4f, 1.0f);
  wall(R, MT_STONE_RED, 46, 192, 58, 192, 3.4f, 1.0f);
  wall(R, MT_STONE_RED, 68, 192, 82, 192, 3.4f, 1.0f);
  wall(R, MT_STONE_RED, 82, 192, 82, 202, 3.4f, 1.0f);
  dress(R, 50, 216, 8), dress(R, 78, 216, 9);
  /* the long ramp's walls, with a side door back down to the courtyard (opened from above) */
  wall(R, MT_STONE_RED, 57, 190, 52, 128, 2.2f, 0.8f);
  wall(R, MT_STONE_RED, 69, 190, 66, 168, 2.2f, 0.8f);
  wall(R, MT_STONE_RED, 66, 158, 64, 128, 2.2f, 0.8f);
  barrier(OB_DOOR, R, MT_WOOD, 65.6f, 163, CW_PI / 2 + 0.06f, 3.6f, 3.0f, 0.35f, 0, 8, -1);
  /* the throne top: braziers and banners */
  static const float BRAZ[][2] = { { 44, 98 }, { 66, 98 }, { 44, 118 }, { 66, 118 }, { 70, 205 }, { 52, 200 } };
  for (int i = 0; i < 6; i++) {
    prop_solid(R, "brazier", BRAZ[i][0], BRAZ[i][1], 0, 1.0f, 0.35f, 0.02f, 0.4f, 1.1f);
    cw_node l = cw_node_new("OmniLight3D", "Fire");
    cw_node_add_child(W_region_node[R], l);
    cw_node_set(l, "color", cw_val_color(1, 0.55f, 0.2f, 1));
    cw_node_set(l, "energy", cw_val_float(30));
    cw_node_set(l, "range", cw_val_float(10));
    cw_node3d_set_position(l, (t3_vec3){ BRAZ[i][0], world_height(BRAZ[i][0], BRAZ[i][1]) + 1.8f, BRAZ[i][1] });
  }
  static const float BAN[][3] = { { 42, 108, 0 }, { 68, 108, CW_PI }, { 48, 196, 0.5f }, { 76, 196, 2.6f } };
  for (int i = 0; i < 4; i++) prop_solid(R, "banner", BAN[i][0], BAN[i][1], BAN[i][2], 1.0f, 0.1f, 0.05f, 0.1f, 4);
  wall(R, MT_STONE_RED, 39, 92, 71, 92, 2.8f, 1.0f);
  /* parapets round the throne top (a fight there must not spill over its edge), open where the ramp arrives */
  wall(R, MT_STONE_RED, 39, 92, 39, 124, 1.3f, 0.8f);
  wall(R, MT_STONE_RED, 71, 92, 71, 124, 1.3f, 0.8f);
  wall(R, MT_STONE_RED, 39, 124, 53.5f, 124, 1.3f, 0.8f);
  wall(R, MT_STONE_RED, 63.5f, 124, 71, 124, 1.3f, 0.8f);
  /* foes */
  foe(K_RAIDER, R, 13, 92, 228, 2.2f);
  foe(K_RAIDER, R, 13, 88, 222, 2.0f);
  foe(K_BRUTE, R, 14, 66, 202, 1.6f);
  foe(K_LANCER, R, 14, 72, 210, 1.8f);
  foe(K_BRUTE, R, 15, 61, 172, 0);
  foe(K_LANCER, R, 15, 59, 150, 0);
  foe(K_RAIDER, R, 16, 58, 136, 0.2f);
  foe(K_EMBER, R, 17, 55, 104, 0);
}
/* the engine's scatter (TerrainScatter3D children, layers in scene order: grass, trees, pines, stilt trees, stones):
 * its placement is a pure function of the cell, so the trees get trunks to bump into and crowns the camera stops at,
 * and every instance joins the grounding check */
#define MAXSCAT 8192
static float SCAT[MAXSCAT][5];
static int SCAT_LAYER[MAXSCAT], nscat;
int world_scatter(const float (**pos)[5], const int **layer) { *pos = (const float (*)[5])SCAT; *layer = SCAT_LAYER; return nscat; }
static void scatter_bodies(void) {
  nscat = 0;
  for (int layer = 1; layer <= 1; layer++) /* (the stones: the trees are the game's own, see flora) */
    for (int cz = -1; cz <= WORLD_N / 32 + 1; cz++)
      for (int cx = -1; cx <= WORLD_N / 32 + 1; cx++) {
        float buf[5 * 256];
        int n = cw_terrain_scatter_cell(W_terrain, layer, cx, cz, buf, 256);
        for (int i = 0; i < n && nscat < MAXSCAT; i++) {
          const float *q = buf + 5 * i;
          bool dup = false;
          for (int k = nscat - 1; k >= 0 && k >= nscat - 512 && !dup; k--) dup = SCAT_LAYER[k] == layer && fabsf(SCAT[k][0] - q[0]) < 0.01f && fabsf(SCAT[k][2] - q[2]) < 0.01f;
          if (dup) continue;
          memcpy(SCAT[nscat], q, sizeof(float) * 5), SCAT_LAYER[nscat] = layer, nscat++;
          int rg = world_region_at(q[0], q[2]);
          float sc = q[3];
          if (layer == 4) { col_box(rg, q[0], q[1] + 0.25f * sc, q[2], 0.35f * sc, 0.25f * sc, 0.35f * sc, q[4]); continue; } /* a stone */
          col_box(rg, q[0], q[1] + 1.5f * sc, q[2], 0.3f * sc, 1.5f * sc, 0.3f * sc, 0); /* the trunk */
          if (layer == 1) crown(rg, q[0], q[1], q[2], 2.0f * sc, 1.4f * sc, 4.8f * sc);
          if (layer == 2) crown(rg, q[0], q[1], q[2], 1.3f * sc, 2.0f * sc, 3.4f * sc);
          if (layer == 3) crown(rg, q[0], q[1], q[2], 2.0f * sc, 1.2f * sc, 3.8f * sc);
        }
      }
  cw_info("adventure: %d scatter trees and stones given bodies", nscat);
}

static void lantern(int rg, float x, float y, float z, float r, float g, float b, float e) {
  cw_node l = cw_node_new("OmniLight3D", "Lantern");
  cw_node_add_child(W_region_node[rg], l);
  cw_node_set(l, "color", cw_val_color(r, g, b, 1));
  cw_node_set(l, "energy", cw_val_float(e));
  cw_node_set(l, "range", cw_val_float(9));
  cw_node3d_set_position(l, (t3_vec3){ x, y, z });
}
static void build_heights(void) {
  const int R = RG_HEIGHTS;
  shrine(R, 420, 128, CW_PI);
  /* a broken watchtower at the top of the switchbacks, frozen ruins on the plateau */
  box_add(R, MT_SLATE, 384, ground_min(384, 108, 2, 2, 0) + 3.5f, 108, 2, 4.2f, 2, 0.2f, true);
  house(R, 450, 140, 7, 6, 2.4f, 2, false);
  house(R, 404, 92, 6, 5, 1.8f, 1, false);
  dress(R, 455, 146, 7);
  wall(R, MT_STONE, 436, 150, 462, 150, 1.0f, 0.6f);
  for (int k = 0; k < 7; k++) { float a = (float)k / 7 * CW_TAU, x = 432 + cw_cos(a) * 11, z = 112 + cw_sin(a) * 9; prop_solid(R, "rock_tall", x, z, a, 1.0f, 0.8f, 0.4f, 0.9f, 3.4f); }
  chest(R, 455, 100, CW_PI / 2, IT_HEART, 10);
  /* the bridge over the gorge on the cliff path: a timber deck on stone piers, rails both sides */
  {
    float x0 = 283, x1 = 257, y0 = 44.0f, y1 = 43.2f;
    g_span = true;
    for (int i = 0; i < 8; i++) {
      float t = ((float)i + 0.5f) / 8, x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t, len = (x0 - x1) / 8;
      box_add(R, MT_WOOD, x, y - 0.2f, 70, len / 2 + 0.02f, 0.2f, 1.7f, 0, true);
      g_span = false;
      for (int sd = -1; sd <= 1; sd += 2) box_add(R, MT_WOOD, x, y + 0.45f, 70 + (float)sd * 1.6f, len / 2, 0.45f, 0.08f, 0, true);
      g_span = true;
    }
    g_span = false;
    for (int k = 0; k < 2; k++) {
      float x = k ? 274.5f : 265.5f, top = y0 + (y1 - y0) * (x0 - x) / (x0 - x1) - 0.4f, lo = ground_min(x, 70, 1, 1.4f, 0) - 0.5f;
      box_add(R, MT_STONE, x, (top + lo) / 2, 70, 1.0f, (top - lo) / 2, 1.4f, 0, true);
    }
  }
  pickup(R, 270, 46, IT_HEART, 15); /* the end of the gorge floor, under the bridge */
  foe(K_BRUTE, R, 18, 430, 142, 3.0f);
  foe(K_LANCER, R, 18, 405, 116, 2.0f);
  foe(K_LANCER, R, 19, 446, 122, 3.6f);
  foe(K_RAIDER, R, 19, 396, 100, 1.0f);
  foe(K_RAIDER, R, 20, 272, 60, 3.1f);
}
static void build_under(void) {
  const int R = RG_UNDER;
  /* the roof over the cutting, on pillars along its sides, lanterns between */
  for (int k = 0; k < 10; k++) {
    float z = 331 + (float)k * 6.0f, x = 168 + (z - 326) * (16.0f / 68) + 6.0f, floor = 6;
    box_add(R, MT_STONE, x, floor + 7.6f, z, 8.0f, 0.4f, 3.05f, 0, true);
    for (int sd = -1; sd <= 1; sd += 2) box_add(R, MT_STONE, x + (float)sd * 5.6f, floor + 3.6f, z, 0.45f, 4.1f, 0.45f, 0, true);
    if (k % 2 == 0) lantern(R, x, floor + 4.6f, z, 1, 0.62f, 0.3f, 26);
  }
  foe(K_RAIDER, R, 21, 175, 352, 3.1f);
  foe(K_RAIDER, R, 21, 178, 372, 0.1f);
}
/* the spire: a stone tower with a stair winding round it to the top */
#define SPIRE_X 202.0f
#define SPIRE_Z 472.0f
#define SPIRE_STEP 0.28f
#define SPIRE_TURN 0.2618f
static int spire_steps;
static void build_spire(void) {
  const int R = RG_SPIRE;
  shrine(R, 190, 448, 0);
  float base = world_height(SPIRE_X, SPIRE_Z), top = base + 30;
  box_add(R, MT_STONE, SPIRE_X, (top + base - 1) / 2, SPIRE_Z, 3.0f, (top - base + 1) / 2, 3.0f, 0, true);
  spire_steps = (int)((top - base) / SPIRE_STEP);
  for (int i = 0; i < spire_steps; i++) {
    float a = (float)i * SPIRE_TURN, y = base + (float)(i + 1) * SPIRE_STEP;
    float cx = SPIRE_X + cw_cos(a) * 4.25f, cz = SPIRE_Z + cw_sin(a) * 4.25f, yaw = -a;
    box_add(R, MT_STONE, cx, y - 0.18f, cz, 1.35f, 0.18f, 0.62f, yaw, true);
    if (i % 2 == 0) box_add(R, MT_STONE, SPIRE_X + cw_cos(a) * 5.45f, y + 0.5f, SPIRE_Z + cw_sin(a) * 5.45f, 0.1f, 0.6f, 0.62f, yaw, true);
  }
  /* the top: a parapet on three sides (the stair arrives on the fourth) */
  float la = (float)(spire_steps - 1) * SPIRE_TURN, ca = cw_cos(la), sa = cw_sin(la);
  for (int sd = 0; sd < 4; sd++) {
    float nx = sd == 0 ? 1.f : sd == 1 ? -1.f : 0, nz = sd == 2 ? 1.f : sd == 3 ? -1.f : 0;
    if (nx * ca + nz * sa > 0.7f) continue;
    box_add(R, MT_STONE, SPIRE_X + nx * 2.85f, top + 0.5f, SPIRE_Z + nz * 2.85f, nz != 0 ? 3.0f : 0.15f, 0.5f, nx != 0 ? 3.0f : 0.15f, 0, true);
  }
  chest(R, SPIRE_X - 1.2f * ca, SPIRE_Z - 1.2f * sa, la + CW_PI / 2, IT_HEART, 12);
  lantern(R, SPIRE_X, top + 2.2f, SPIRE_Z, 1, 0.8f, 0.5f, 20);
  house(R, 176, 488, 7, 5, 2.4f, 1, false);
  house(R, 226, 455, 6, 6, 2.0f, 3, true);
  dress(R, 172, 484, 5), dress(R, 230.5f, 459, 6);
  lamp_post(R, 196, 448, 0.0f);
  foe(K_RAIDER, R, 22, 188, 460, 0.2f);
  foe(K_RAIDER, R, 22, 214, 486, 3.4f);
  foe(K_LANCER, R, 22, 182, 478, 1.4f);
  foe(K_BRUTE, R, 23, 212, 456, 2.2f);
}
static void build_wood(void) {
  const int R = RG_WOOD;
  shrine(R, 448, 322, -CW_PI / 2);
  for (int k = 0; k < 9; k++) { float a = (float)k / 9 * CW_TAU, x = 492 + cw_cos(a) * 9, z = 348 + cw_sin(a) * 9; prop_solid(R, "rock_tall", x, z, a, 0.8f, 0.7f, 0.4f, 0.7f, 2.6f); }
  chest(R, 510, 362, CW_PI, IT_CHARM, 11);
  for (int k = 0; k < 5; k++) lantern(R, 460 + (float)k * 14, world_height(460 + (float)k * 14, 340) + 1.5f, 340 + (float)(k % 2) * 12, 0.3f, 0.9f, 1, 18);
  foe(K_RAIDER, R, 24, 470, 330, 1.0f);
  foe(K_RAIDER, R, 24, 482, 346, 2.0f);
  foe(K_RAIDER, R, 25, 500, 320, 3.0f);
  foe(K_RAIDER, R, 25, 462, 362, 4.0f);
  foe(K_LANCER, R, 26, 520, 350, 2.4f);
  foe(K_LANCER, R, 26, 506, 376, 2.0f);
  foe(K_BRUTE, R, 27, 530, 334, 3.4f);
}
static void route_spire(bool up) {
  /* the stair round the spire, a point every second step's turn (30 degrees), at the stair's outer half */
  for (int k = 0; k <= spire_steps / 2; k++) {
    int i = up ? k * 2 : spire_steps - 1 - k * 2;
    if (i < 0) i = 0;
    float a = (float)i * SPIRE_TURN;
    route(SPIRE_X + cw_cos(a) * 4.6f, SPIRE_Z + cw_sin(a) * 4.6f, RA_GO, -1, -1);
  }
}
static void build_route(void) {
  /* the autopilot's way through the game, in order: rest, the canyon, the maul, the Thornwarden, the passage, the
   * marsh, the Tidecaller, the charm, the glowcap wood, the switchbacks to the heights, the cliff path and its bridge
   * back to the canyon, the hollow's cellar, the underway, the spire's stair, back, the gate, the keep, the Ember King */
  route(160, 282, RA_REST, world_shrine_obj(0), -1);
  route(160, 252, RA_GO, -1, -1);
  route(160, 214, RA_GO, -1, -1);
  route(160, 176, RA_GO, -1, -1);
  route(150, 165, RA_REST, world_shrine_obj(1), -1);
  route(165, 140, RA_GO, -1, -1);
  route(181, 125, RA_GO, -1, -1);
  route(200, 108, RA_GO, -1, -1);
  route(209, 103, RA_USE, -1, 3);
  route(181, 125, RA_GO, -1, -1);
  route(160, 108, RA_GO, -1, -1);
  route(160, 86, RA_GO, -1, -1);
  route(160, 72, RA_FIGHT, -1, 100 + K_THORN);
  route(160, 72, RA_GO, -1, -1);
  route(160, 104, RA_GO, -1, -1);
  route(190, 135, RA_GO, -1, -1);
  route(212.2f, 137.4f, RA_HIT, -1, 5);
  route(228, 139, RA_GO, -1, -1);
  route(248, 145, RA_REST, world_shrine_obj(2), -1);
  route(256, 175, RA_GO, -1, -1);
  route(268, 225, RA_GO, -1, -1);
  route(272, 248, RA_GO, -1, -1);
  route(276, 270, RA_FIGHT, -1, 100 + K_TIDE);
  route(276, 270, RA_GO, -1, -1);
  route(300, 188, RA_GO, -1, 6);
  /* the glowcap wood */
  route(306, 246, RA_GO, -1, -1);
  route(360, 282, RA_GO, -1, -1);
  route(404, 304, RA_GO, -1, -1);
  route(445, 322, RA_REST, world_shrine_obj(6), -1);
  route(508, 360, RA_USE, -1, 11);
  route(404, 304, RA_GO, -1, -1);
  route(360, 282, RA_GO, -1, -1);
  route(306, 246, RA_GO, -1, -1);
  /* up the switchbacks to the heights */
  route(304, 168, RA_GO, -1, -1);
  route(336, 150, RA_GO, -1, -1);
  route(316, 126, RA_GO, -1, -1);
  route(352, 108, RA_GO, -1, -1);
  route(378, 118, RA_GO, -1, -1);
  route(418, 131, RA_REST, world_shrine_obj(4), -1);
  route(453, 102, RA_USE, -1, 10);
  /* the cliff path, the bridge, down to the Thornwarden's ring */
  route(376, 98, RA_GO, -1, -1);
  route(300, 70, RA_GO, -1, -1);
  route(270, 70, RA_GO, -1, -1);
  route(240, 70, RA_GO, -1, -1);
  route(214, 68, RA_GO, -1, -1);
  route(181, 70, RA_GO, -1, -1);
  route(160, 104, RA_GO, -1, -1);
  route(160, 176, RA_GO, -1, -1);
  route(160, 214, RA_GO, -1, -1);
  route(160, 252, RA_GO, -1, -1);
  /* the hollow's cellar */
  route(140, 240, RA_GO, -1, -1);
  route(129, 234, RA_HIT, -1, 1);
  route(123, 234, RA_GO, -1, 2);
  route(129.5f, 235, RA_GO, -1, -1);
  /* the underway and the spire */
  route(160, 262, RA_GO, -1, -1);
  route(160, 300, RA_GO, -1, -1);
  route(168, 326, RA_GO, -1, -1);
  route(176, 360, RA_GO, -1, -1);
  route(184, 394, RA_GO, -1, -1);
  route(192, 420, RA_GO, -1, -1);
  route(190, 451, RA_REST, world_shrine_obj(5), -1);
  route_spire(true);
  route(SPIRE_X, SPIRE_Z, RA_USE, -1, 12);
  route_spire(false);
  route(192, 420, RA_GO, -1, -1);
  route(184, 394, RA_GO, -1, -1);
  route(168, 326, RA_GO, -1, -1);
  route(160, 300, RA_GO, -1, -1);
  route(150, 262, RA_GO, -1, -1);
  /* the gate, the keep */
  route(116, 239, RA_GO, -1, -1);
  route(108, 237, RA_USE, -1, 9);
  route(96, 232, RA_GO, -1, -1);
  route(80, 214, RA_GO, -1, -1);
  route(60, 209, RA_REST, world_shrine_obj(3), -1);
  route(63, 190, RA_GO, -1, -1);
  route(60, 160, RA_GO, -1, -1);
  route(58, 128, RA_GO, -1, -1);
  route(55, 108, RA_FIGHT, -1, 100 + K_EMBER);
}

static void build_flora(void);
static void build_sky(cw_node world);
void world_build(cw_tree *tree) {
  W_tree = tree;
  W_root = cw_tree_root(tree);
  W_env = cw_node_get(W_root, "World/Env"), W_sun = cw_node_get(W_root, "World/Sun"), W_sky = cw_node_get(W_root, "World/Sky");
  W_post = cw_node_get(W_root, "World/Post");
  W_terrain = cw_terrain_of(cw_node_get(W_root, "World/Terrain"));
  W_phys = cw_physics3d_world(tree);
  W_hash = 2166136261u;
  if (!W_terrain) { cw_error("adventure: no terrain"); return; }
  paint_and_shape();
  cw_node world = cw_node_get(W_root, "World");
  for (int r = 0; r < RG_COUNT; r++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Region%d", r);
    W_region_node[r] = cw_node_new("Node3D", nm);
    cw_node_add_child(world, W_region_node[r]);
  }
  g_nobj = g_nfoe = g_nroute = nbox = nprop = nshrine = nfixed = 0;
  build_meadow();
  build_canyon();
  build_marsh();
  build_keep();
  build_heights();
  build_under();
  build_spire();
  build_wood();
  build_route();
  build_flora();
  build_sky(world);
  scatter_bodies();
  /* the merged buildings: one mesh per region and material */
  for (int r = 0; r < RG_COUNT; r++)
    for (int m = 0; m < MT_COUNT; m++) {
      mbuf *b = &MB[r][m];
      if (!b->ni) continue;
      t3_geometry *g = t3_geometry_new();
      t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, b->p, b->nv, 3));
      t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, b->n, b->nv, 3));
      t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2));
      t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, b->ix, b->ni, 1));
      t3_mesh *mesh = t3_mesh_new(g, world_material(m, 1));
      mesh->base.cast_shadow = true, mesh->base.receive_shadow = true;
      char nm[24];
      snprintf(nm, sizeof nm, "Build%d_%d", r, m);
      cw_node n = cw_node_new("MeshInstance3D", nm);
      cw_node_add_child(W_region_node[r], n);
      cw_mesh3d_set_procedural(n, mesh);
      t3_release(mesh), t3_release(g);
      free(b->p), free(b->n), free(b->uv), free(b->ix);
      memset(b, 0, sizeof *b);
    }
  for (int i = 0; i < g_nobj; i++) world_obj_state(i, false);
  g_world_paged = (1 << RG_COUNT) - 1;
  /* perf probes (development, native): what each part of the picture costs */
  if (getenv("ADV_PROBE_NOGRASS")) cw_node_free(cw_node_get(W_root, "World/Terrain/Grass"));
  if (getenv("ADV_PROBE_NOSCATTER")) { const char *n[4] = { "Trees", "Pines", "Mangroves", "Stones" }; for (int i = 0; i < 4; i++) { char p[48]; snprintf(p, sizeof p, "World/Terrain/%s", n[i]); cw_node_free(cw_node_get(W_root, p)); } }
  if (getenv("ADV_PROBE_NOSHADOW")) cw_node_set(W_sun, "shadow", cw_val_bool(false));
  if (getenv("ADV_PROBE_TERRAINQ")) cw_terrain_set_quality(W_terrain, atoi(getenv("ADV_PROBE_TERRAINQ")));
  if (getenv("ADV_PROBE_NOBUILD")) for (int r = 0; r < RG_COUNT; r++) cw_node_set(W_region_node[r], "visible", cw_val_bool(false));
  if (getenv("ADV_PROBE_NOBLOOM")) cw_node_set(W_post, "bloom", cw_val_bool(false)), cw_node_set(W_post, "vignette", cw_val_bool(false));
  if (getenv("ADV_PROBE_NOWATER")) cw_node_free(cw_node_get(W_root, "World/Water"));
  if (getenv("ADV_PROBE_NOTERRAIN")) cw_node_set(cw_node_get(W_root, "World/Terrain"), "visible", cw_val_bool(false));
  if (getenv("ADV_PROBE_NOTERRAINSHADOW")) cw_node_set(cw_node_get(W_root, "World/Terrain"), "cast_shadow", cw_val_bool(false));
  if (getenv("ADV_PROBE_SHADOWSMALL")) cw_node_set(W_sun, "shadow_size", cw_val_int(1024));
  if (getenv("ADV_PROBE_BLOOMLV")) cw_node_set(W_post, "bloom_levels", cw_val_int(3));
  if (getenv("ADV_PROBE_PREPASS")) cw_node_set(W_post, "depth_prepass", cw_val_bool(true));
  if (getenv("ADV_PROBE_NOAA")) cw_node_set(W_post, "aa", cw_val_enum("NONE"));
  if (getenv("ADV_PROBE_FORWARD")) cw_node_set(cw_node_get(W_root, "World/Lighting"), "mode", cw_val_enum("FORWARD"));
  cw_info("adventure: probe land %.2f terrain %.2f ground %.2f", land(160, 281, NULL, NULL), world_height(160, 281), world_ground(160, 281, 100));
  cw_info("adventure: world built: %d boxes, %d props, %d objects, %d foes, %d route points, hash %08x", nbox, nprop, g_nobj, g_nfoe, g_nroute, W_hash);
}

/* ── paging: the player's region and its neighbours are live ───────── */
#define B(r) (1 << (r))
static const int NEAR[RG_COUNT] = {
  [RG_MEADOW] = B(RG_MEADOW) | B(RG_CANYON) | B(RG_MARSH) | B(RG_KEEP) | B(RG_UNDER),
  [RG_CANYON] = B(RG_MEADOW) | B(RG_CANYON) | B(RG_MARSH) | B(RG_HEIGHTS),
  [RG_MARSH] = B(RG_MEADOW) | B(RG_CANYON) | B(RG_MARSH) | B(RG_HEIGHTS) | B(RG_WOOD),
  [RG_KEEP] = B(RG_MEADOW) | B(RG_KEEP),
  [RG_HEIGHTS] = B(RG_CANYON) | B(RG_MARSH) | B(RG_HEIGHTS),
  [RG_UNDER] = B(RG_MEADOW) | B(RG_UNDER) | B(RG_SPIRE),
  [RG_SPIRE] = B(RG_UNDER) | B(RG_SPIRE),
  [RG_WOOD] = B(RG_MARSH) | B(RG_WOOD),
};
void world_page(float x, float z, int region) {
  (void)z;
  /* the marsh water only where it can be seen: from the marsh and the ways into it (its refraction copies the frame) */
  static int water = -1;
  int want = x > 196 || region == RG_MARSH;
  if (want != water) { cw_node w = cw_node_get(W_root, "World/Water"); if (w.id) cw_node_set(w, "visible", cw_val_bool(want)), cw_node_set(w, "refraction", cw_val_bool(want)); water = want; }
  int live = NEAR[CW_CLAMP(region, 0, RG_COUNT - 1)];
  if (live == g_world_paged) return;
  for (int r = 0; r < RG_COUNT; r++) cw_node_set(W_region_node[r], "visible", cw_val_bool((live >> r) & 1));
  g_world_paged = live;
}

/* ── colour: each region's light, blended as the camera moves between them ── */
typedef struct look { float fog[3], density, amb[3], amb_e, sky[3], gnd[3], hemi, sun[3], sun_e, expo; } look;
static const look LOOK[RG_COUNT] = {
  /* the meadow: a clear gold morning, blue-white haze */
  { { 0.6f, 0.76f, 0.98f }, 0.003f, { 1, 0.95f, 0.85f }, 0.35f, { 0.6f, 0.76f, 1 }, { 0.42f, 0.38f, 0.22f }, 0.75f, { 1, 0.93f, 0.8f }, 3.4f, 1.3f },
  /* the canyon: amber light, a cool lavender haze and blue shadows against the orange rock */
  { { 0.7f, 0.66f, 0.86f }, 0.0032f, { 0.55f, 0.65f, 1 }, 0.5f, { 0.7f, 0.68f, 0.95f }, { 0.6f, 0.36f, 0.2f }, 0.8f, { 1, 0.82f, 0.6f }, 3.4f, 0.98f },
  /* the marsh: teal and violet, a thicker cool mist */
  { { 0.3f, 0.56f, 0.6f }, 0.0055f, { 0.7f, 0.95f, 1 }, 0.45f, { 0.55f, 0.6f, 1 }, { 0.2f, 0.4f, 0.32f }, 0.85f, { 0.85f, 0.95f, 1 }, 3.0f, 1.2f },
  /* the keep: a crimson sunset, ember haze */
  { { 0.8f, 0.42f, 0.34f }, 0.005f, { 1, 0.7f, 0.6f }, 0.4f, { 0.85f, 0.55f, 0.7f }, { 0.45f, 0.2f, 0.12f }, 0.8f, { 1, 0.62f, 0.42f }, 3.3f, 0.94f },
  /* the heights: cold bright air, blue shadows on the snow */
  { { 0.66f, 0.78f, 0.95f }, 0.0035f, { 0.85f, 0.92f, 1 }, 0.35f, { 0.6f, 0.75f, 1 }, { 0.5f, 0.52f, 0.6f }, 0.7f, { 1, 0.96f, 0.9f }, 3.2f, 0.62f },
  /* the underway: lantern light under the roof */
  { { 0.42f, 0.34f, 0.26f }, 0.006f, { 1, 0.8f, 0.6f }, 1.3f, { 0.75f, 0.65f, 0.55f }, { 0.4f, 0.28f, 0.16f }, 1.6f, { 1, 0.9f, 0.75f }, 3.4f, 1.8f },
  /* the spire's downs: wind, a high clear sky */
  { { 0.58f, 0.74f, 0.96f }, 0.003f, { 1, 0.95f, 0.88f }, 0.35f, { 0.55f, 0.72f, 1 }, { 0.4f, 0.4f, 0.25f }, 0.75f, { 1, 0.92f, 0.8f }, 3.5f, 1.2f },
  /* the glowcap wood: deep green under the canopy, teal glow */
  { { 0.22f, 0.42f, 0.36f }, 0.006f, { 0.7f, 0.75f, 1 }, 0.45f, { 0.6f, 0.7f, 0.85f }, { 0.25f, 0.22f, 0.3f }, 0.85f, { 1, 0.92f, 0.8f }, 3.0f, 1.45f },
};
static look CUR;
static const float g_amb_k = 0.26f; /* the fill under the sun kept low, so shadows have depth */
static bool CUR_SET;
void world_colour(float x, float z, float dt) {
  /* weights: each zone's pull at the point (the strongest zone of each region), softened */
  float w[RG_COUNT] = { 0 };
  for (int i = 0; i < NZONES; i++) {
    float d = sqrtf(dist2(x, z, ZONES[i].cx, ZONES[i].cz)) - (ZONES[i].rx + ZONES[i].rz) * 0.5f;
    float k = 1 - smooth01(-10, 30, d);
    if (k > w[ZONES[i].region]) w[ZONES[i].region] = k;
  }
  float sum = 0;
  for (int r = 0; r < RG_COUNT; r++) sum += w[r];
  if (sum < 1e-3f) w[RG_MEADOW] = 1, sum = 1;
  look t = { 0 };
  float *tf = (float *)&t;
  for (int r = 0; r < RG_COUNT; r++) {
    const float *lf = (const float *)&LOOK[r];
    for (int k = 0; k < (int)(sizeof(look) / sizeof(float)); k++) tf[k] += lf[k] * w[r] / sum;
  }
  float *cf = (float *)&CUR;
  float e = CUR_SET ? 1 - cw_exp(-dt * 1.5f) : 1;
  CUR_SET = true;
  for (int k = 0; k < (int)(sizeof(look) / sizeof(float)); k++) cf[k] += (tf[k] - cf[k]) * e;
  cw_node_set(W_env, "fog_color", cw_val_color(CUR.fog[0], CUR.fog[1], CUR.fog[2], 1));
  cw_node_set(W_env, "fog_density", cw_val_float(CUR.density));
  cw_node_set(W_env, "ambient_color", cw_val_color(CUR.amb[0], CUR.amb[1], CUR.amb[2], 1));
  cw_node_set(W_env, "ambient_energy", cw_val_float(CUR.amb_e * g_amb_k));
  cw_node_set(W_env, "sky_color", cw_val_color(CUR.sky[0], CUR.sky[1], CUR.sky[2], 1));
  cw_node_set(W_env, "ground_color", cw_val_color(CUR.gnd[0], CUR.gnd[1], CUR.gnd[2], 1));
  cw_node_set(W_env, "hemi_energy", cw_val_float(CUR.hemi * g_amb_k));
  cw_node_set(W_sun, "color", cw_val_color(CUR.sun[0], CUR.sun[1], CUR.sun[2], 1));
  cw_node_set(W_sun, "energy", cw_val_float(CUR.sun_e));
  cw_node_set(W_post, "exposure", cw_val_float(CUR.expo));
}

/* the grounding check: every prop's base against the walkable ground under it (terrain and buildings), every
 * building box's foot against what is under it. Returns the number off the ground by more than 3 cm. */
int world_ground_check(float *worst_out) {
  int bad = 0;
  float worst = 0;
  for (int i = 0; i < nprop; i++) {
    const wprop *p = &PROP[i];
    float g = world_ground(p->x, p->z, p->y + 0.02f);
    float gap = p->y - g;
    if (gap > 0.03f) { bad++; cw_warn("adventure: prop %d (%s) floats %.3f m at (%.1f, %.1f)", i, p->what, gap, p->x, p->z); }
    if (gap > worst) worst = gap;
  }
  for (int i = 0; i < nbox; i++) {
    /* a box stands when something holds it up under its middle or a corner: the ground, a top it sits on, or a box
     * it is set into (a lintel let into its towers) */
    const sbox *b = &BOX[i];
    float s = cw_sin(b->yaw), c = cw_cos(b->yaw), bottom = b->cy - b->hy, hi = -1e30f;
    int ends = 0;
    for (int k = 0; k < 5; k++) {
      float lx = k == 4 ? 0 : (k & 1 ? 1 : -1) * b->hx * 0.95f, lz = k == 4 ? 0 : (k & 2 ? 1 : -1) * b->hz * 0.95f;
      if (b->span) { if (k > 1) break; lx = (k ? 1 : -1) * (b->hx + 0.03f), lz = 0; }
      float x = b->cx + lx * c + lz * s, z = b->cz - lx * s + lz * c;
      float g = world_ground(x, z, bottom + 0.02f);
      for (int j = 0; j < nbox; j++) { /* set into another box */
        const sbox *o = &BOX[j];
        if (j == i || o->cy - o->hy > bottom + (b->span ? 0.15f : 0) || o->cy + o->hy < bottom) continue;
        float dx = x - o->cx, dz = z - o->cz, so = cw_sin(o->yaw), co = cw_cos(o->yaw);
        if (fabsf(dx * co - dz * so) <= o->hx && fabsf(dx * so + dz * co) <= o->hz) g = bottom;
      }
      if (b->span) ends += g >= bottom - 0.03f;
      if (g > hi) hi = g;
    }
    if (b->span) hi = ends == 2 ? bottom : -1e30f; /* a span holds when both its ends rest on something */
    float gap = bottom - hi;
    if (gap > 0.03f) { bad++; cw_warn("adventure: box %d floats %.3f m at (%.1f, %.1f)", i, gap, b->cx, b->cz); }
    if (gap > worst) worst = gap;
  }
  for (int i = 0; i < nscat; i++) { /* the engine's scatter: each instance's base on the terrain under it */
    float gap = SCAT[i][1] - world_height(SCAT[i][0], SCAT[i][2]);
    if (gap > 0.03f) { bad++; cw_warn("adventure: scatter %d (layer %d) floats %.3f m at (%.1f, %.1f)", i, SCAT_LAYER[i], gap, SCAT[i][0], SCAT[i][2]); }
    if (gap > worst) worst = gap;
  }
  *worst_out = worst;
  return bad;
}

/* ── flora: trees placed per region, instanced per species, their crowns swaying in the wind ─────────────────────
 * Each species glb has a Trunk and a Crown (leaf cards, alpha-tested). A tree stands where its region's ground is
 * the kind it grows on, not on a path, a building, a prop, an object or the bot's way; it gets a trunk to bump
 * into and a crown the camera stops at. The crowns turn a little about the top of the trunk every frame. */
static const char *const SP_FILE[SP_COUNT] = { "flora_oak", "flora_birch", "flora_aspen", "flora_willow", "flora_pine", "flora_fir", "flora_windswept", "flora_deepwood", "flora_glowcap" };
static const float SP_PIVOT[SP_COUNT] = { 3.4f, 4.0f, 3.0f, 3.0f, 1.0f, 1.0f, 2.2f, 6.0f, 3.4f }; /* where the crown hinges */
static const float SP_SWAY[SP_COUNT] = { 0.03f, 0.04f, 0.045f, 0.035f, 0.02f, 0.02f, 0.035f, 0.02f, 0.008f };
typedef struct tree { float x, y, z, yaw, s, phase; int sp, rg; } tree;
#define MAXTREE 4096
static tree TREE[MAXTREE];
static int ntree;
typedef struct fbatch { int sp, rg, first, n; t3_instanced_mesh *trunk, *crown; cw_node node; } fbatch;
static fbatch FB[RG_COUNT * SP_COUNT];
static int nfb;
int world_trees(const float (**unused)[5]) { (void)unused; return ntree; }

/* what grows where: per region, a cell size, the share of cells with a tree, the ground it needs (paint bits), two
 * species and how often the second */
typedef struct grove { float cell, chance; uint32_t paints; int a, b; float b_share; } grove;
static const grove GROVE[RG_COUNT] = {
  [RG_MEADOW] = { 11, 0.55f, 1u << 0, SP_OAK, SP_BIRCH, 0.35f },
  [RG_CANYON] = { 20, 0.35f, (1u << 3), SP_ASPEN, SP_ASPEN, 0 },
  [RG_MARSH] = { 12, 0.45f, (1u << 5) | (1u << 0), SP_WILLOW, SP_BIRCH, 0.15f },
  [RG_KEEP] = { 10, 0.5f, (1u << 7) | (1u << 0), SP_PINE, SP_PINE, 0 },
  [RG_HEIGHTS] = { 10, 0.5f, (1u << 1), SP_FIR, SP_PINE, 0.25f },
  [RG_UNDER] = { 1000, 0, 0, SP_OAK, SP_OAK, 0 },
  [RG_SPIRE] = { 15, 0.45f, 1u << 0, SP_WINDSWEPT, SP_BIRCH, 0.4f },
  [RG_WOOD] = { 8, 0.6f, (1u << 5) | (1u << 0), SP_DEEPWOOD, SP_GLOWCAP, 0.4f },
};
static bool clear_of_things(float x, float z) {
  for (int i = 0; i < nbox; i++) {
    const sbox *b = &BOX[i];
    float r = fmaxf(b->hx, b->hz) + 2.5f;
    if (dist2(x, z, b->cx, b->cz) < r * r) return false;
  }
  for (int i = 0; i < nprop; i++) if (dist2(x, z, PROP[i].x, PROP[i].z) < (PROP[i].r + 3) * (PROP[i].r + 3)) return false;
  for (int i = 0; i < g_nobj; i++) if (dist2(x, z, g_obj[i].x, g_obj[i].z) < 5 * 5) return false;
  for (int i = 0; i < g_nfoe; i++) if (dist2(x, z, g_foe[i].x, g_foe[i].z) < 3 * 3) return false;
  for (int i = 0; i + 1 < g_nroute; i++) { /* the bot's way: a segment between route points kept clear */
    float ax = g_route[i].x, az = g_route[i].z, ex = g_route[i + 1].x - ax, ez = g_route[i + 1].z - az, l2 = ex * ex + ez * ez + 1e-6f;
    float t = cw_clampf(((x - ax) * ex + (z - az) * ez) / l2, 0, 1), qx = ax + ex * t - x, qz = az + ez * t - z;
    if (qx * qx + qz * qz < 3.5f * 3.5f) return false;
  }
  return true;
}
static void add_tree(int rg, int sp, float x, float z, float s, float yaw, float phase) {
  if (ntree >= MAXTREE) return;
  if (sp == SP_DEEPWOOD) s *= 1.25f;
  tree *t = &TREE[ntree++];
  *t = (tree){ x, world_height(x, z) - 0.25f * s, z, yaw, s, phase, sp, rg };
  col_box(rg, x, t->y + 1.5f * s, z, 0.32f * s, 1.5f * s, 0.32f * s, 0);
  float cr = sp == SP_PINE || sp == SP_FIR ? 1.4f : sp == SP_GLOWCAP ? 1.9f : sp == SP_DEEPWOOD ? 3.0f : 2.0f;
  float cy = sp == SP_PINE || sp == SP_FIR ? 3.6f : sp == SP_DEEPWOOD ? 8.0f : sp == SP_GLOWCAP ? 4.1f : SP_PIVOT[sp] + 1.4f;
  crown(rg, x, t->y, z, cr * s, 1.3f * s, cy * s);
}
static void place_trees(void) {
  ntree = 0;
  for (int i = 0; i < nfixed; i++) add_tree(FIXED[i].rg, FIXED[i].sp, FIXED[i].x, FIXED[i].z, FIXED[i].s, hash01(i, 1, 770) * CW_TAU, hash01(i, 2, 770) * CW_TAU);
  for (int rg = 0; rg < RG_COUNT; rg++) {
    const grove *g = &GROVE[rg];
    if (g->chance <= 0) continue;
    int nc = (int)((float)WORLD_N / g->cell);
    for (int cz = 0; cz < nc; cz++)
      for (int cx = 0; cx < nc; cx++) {
        if (hash01(cx, cz, 700 + (uint32_t)rg) > g->chance || ntree >= MAXTREE) continue;
        float x = ((float)cx + 0.15f + 0.7f * hash01(cx, cz, 710 + (uint32_t)rg)) * g->cell, z = ((float)cz + 0.15f + 0.7f * hash01(cx, cz, 720 + (uint32_t)rg)) * g->cell;
        if (x < 30 || z < 30 || x > WORLD_N - 30 || z > WORLD_N - 30) continue;
        int paint = 0, over = 0, blend = 0, zr = -1;
        land_ex(x, z, &paint, &zr, &over, &blend);
        if (zr != rg || !((g->paints >> paint) & 1) || blend > 40) continue;
        float h0 = world_height(x, z), dx = world_height(x + 1.5f, z) - world_height(x - 1.5f, z), dz = world_height(x, z + 1.5f) - world_height(x, z - 1.5f);
        if (sqrtf(dx * dx + dz * dz) / 3 > 0.45f) continue;
        if (rg == RG_MARSH && h0 < 7.9f) continue; /* not out in the deep water */
        if (!clear_of_things(x, z)) continue;
        int sp = hash01(cx, cz, 730 + (uint32_t)rg) < g->b_share ? g->b : g->a;
        add_tree(rg, sp, x, z, 0.8f + 0.45f * hash01(cx, cz, 740 + (uint32_t)rg), hash01(cx, cz, 750) * CW_TAU, hash01(cx, cz, 760) * CW_TAU);
      }
  }
}
static t3_mesh *first_mesh(t3_object *o) {
  if (!o) return NULL;
  if (o->type == T3_MESH) return (t3_mesh *)o;
  for (int i = 0; i < o->child_count; i++) { t3_mesh *m = first_mesh(o->children[i]); if (m) return m; }
  return NULL;
}
static void qmul(const float *a, const float *b, float *o) {
  float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
  o[0] = x, o[1] = y, o[2] = z, o[3] = w;
}
static void qrot(const float *q, const float *v, float *o) {
  float p[4] = { v[0], v[1], v[2], 0 }, c[4] = { -q[0], -q[1], -q[2], q[3] }, t[4], r[4];
  qmul(q, p, t), qmul(t, c, r);
  o[0] = r[0], o[1] = r[1], o[2] = r[2];
}
/* a tree's crown matrix: its trunk's transform, turned by the sway about the top of the trunk */
static void crown_matrix(const tree *t, float sway_x, float sway_z, t3_mat4 *m) {
  float sy = cw_sin(t->yaw * 0.5f), cy = cw_cos(t->yaw * 0.5f);
  float qy[4] = { 0, sy, 0, cy };
  float ax = sway_x, az = sway_z, ang = sqrtf(ax * ax + az * az);
  float qs[4] = { 0, 0, 0, 1 };
  if (ang > 1e-6f) { float s = cw_sin(ang * 0.5f) / ang; qs[0] = ax * s, qs[2] = az * s, qs[3] = cw_cos(ang * 0.5f); }
  /* sway in world axes: q = qs * qy */
  float q[4];
  qmul(qs, qy, q);
  float piv[3] = { 0, SP_PIVOT[t->sp] * t->s, 0 }, rp[3];
  qrot(qs, piv, rp);
  t3_vec3 pos = { t->x + piv[0] - rp[0], t->y + piv[1] - rp[1], t->z + piv[2] - rp[2] };
  t3_mat4_compose(m, pos, (t3_quat){ q[0], q[1], q[2], q[3] }, (t3_vec3){ t->s, t->s, t->s });
}
static void build_flora(void) {
  place_trees();
  /* the trees in batch order (region, then species; placement order inside a batch) */
  static tree SORTED[MAXTREE];
  int k = 0;
  nfb = 0;
  for (int rg = 0; rg < RG_COUNT; rg++)
    for (int sp = 0; sp < SP_COUNT; sp++) {
      int first = k;
      for (int i = 0; i < ntree; i++) if (TREE[i].rg == rg && TREE[i].sp == sp) SORTED[k++] = TREE[i];
      if (k > first) FB[nfb++] = (fbatch){ sp, rg, first, k - first, NULL, NULL, { 0 } };
    }
  memcpy(TREE, SORTED, sizeof(tree) * (size_t)k);
  for (int f = 0; f < nfb; f++) {
    fbatch *b = &FB[f];
    t3_object *o = prop_obj(SP_FILE[b->sp]);
    t3_mesh *tm = first_mesh(o ? t3_object_get_by_name(o, "Trunk") : NULL), *cm = first_mesh(o ? t3_object_get_by_name(o, "Crown") : NULL);
    char nm[32];
    snprintf(nm, sizeof nm, "Flora%d_%d", b->rg, b->sp);
    b->node = cw_node_new("Node3D", nm);
    cw_node_add_child(W_region_node[b->rg], b->node);
    for (int part = 0; part < 2; part++) {
      t3_mesh *src = part ? cm : tm;
      if (!src) continue;
      t3_instanced_mesh *im = t3_instanced_mesh_new(src->geometry, src->materials[0], b->n);
      im->mesh.base.cast_shadow = !(part && getenv("ADV_PROBE_NOCROWNSHADOW")), im->mesh.base.receive_shadow = true;
      im->instance_matrix->usage = part ? T3_DYNAMIC_DRAW : T3_STATIC_DRAW;
      for (int i = 0; i < b->n; i++) {
        t3_mat4 m;
        crown_matrix(&TREE[b->first + i], 0, 0, &m);
        t3_instanced_mesh_set_matrix_at(im, i, &m);
      }
      cw_node mn = cw_node_new("MeshInstance3D", part ? "Crowns" : "Trunks");
      cw_node_add_child(b->node, mn);
      cw_mesh3d_set_procedural(mn, &im->mesh);
      if (part) b->crown = im; else b->trunk = im;
      t3_release(im);
    }
    if (o) t3_release(o);
  }
  if (getenv("ADV_PROBE_NOTREES")) for (int f = 0; f < nfb; f++) cw_node_set(FB[f].node, "visible", cw_val_bool(false));
  cw_info("adventure: %d trees in %d batches", ntree, nfb);
}
/* the wind, every frame (render only): the crowns of the live regions turn about the tops of their trunks */
void world_wind(float t) {
  static float last = -1;
  static int still = -1;
  if (still < 0) still = getenv("ADV_PROBE_NOWIND") != NULL;
  if (still) return;
  if (t - last < 1.0f / 40) return; /* (40 updates a second is smooth for a sway) */
  last = t;
  for (int f = 0; f < nfb; f++) {
    fbatch *b = &FB[f];
    if (!b->crown || !((g_world_paged >> b->rg) & 1)) continue;
    float amp = SP_SWAY[b->sp];
    for (int i = 0; i < b->n; i++) {
      const tree *tr = &TREE[b->first + i];
      float ph = tr->phase + tr->x * 0.05f;
      float gust = 0.6f + 0.4f * cw_sin(t * 0.37f + tr->z * 0.02f);
      float sx = amp * gust * (cw_sin(t * 1.3f + ph) + 0.35f * cw_sin(t * 2.9f + ph * 1.7f)), sz = amp * 0.4f * cw_sin(t * 1.1f + ph * 0.6f);
      t3_mat4 m;
      crown_matrix(tr, sz, sx, &m);
      t3_instanced_mesh_set_matrix_at(b->crown, i, &m);
    }
    b->crown->instance_matrix->version++;
  }
}
int world_tree_check(float *worst) {
  int bad = 0;
  for (int i = 0; i < ntree; i++) {
    float gap = TREE[i].y - world_height(TREE[i].x, TREE[i].z);
    if (gap > 0.03f) { bad++; cw_warn("adventure: tree %d floats %.3f m at (%.1f, %.1f)", i, gap, TREE[i].x, TREE[i].z); }
    if (gap > *worst) *worst = gap;
  }
  return bad;
}

/* ── the sky's clouds: a dome round the camera, the cloud sheet drifting across it ── */
static cw_node SKY_DOME;
static t3_texture *CLOUD_TEX;
static void build_sky(cw_node world) {
  enum { RINGS = 14, SEG = 48 };
  const float R = 650;
  int nv = (RINGS + 1) * (SEG + 1), ni = RINGS * SEG * 6;
  float *p = malloc(sizeof(float) * 3 * (size_t)nv), *uv = malloc(sizeof(float) * 2 * (size_t)nv), *n = malloc(sizeof(float) * 3 * (size_t)nv);
  uint32_t *ix = malloc(sizeof(uint32_t) * (size_t)ni);
  int v = 0, k = 0;
  for (int r = 0; r <= RINGS; r++) {
    float el = 0.04f + 1.45f * (float)r / RINGS; /* from just over the horizon up to near the zenith */
    float ce = cw_cos(el), se = cw_sin(el);
    for (int s = 0; s <= SEG; s++, v++) {
      float az = (float)s / SEG * CW_TAU, ca = cw_cos(az), sa = cw_sin(az);
      float dx = ce * ca, dy = se, dz = ce * sa;
      p[v * 3] = dx * R, p[v * 3 + 1] = dy * R - 60, p[v * 3 + 2] = dz * R;
      n[v * 3] = -dx, n[v * 3 + 1] = -dy, n[v * 3 + 2] = -dz;
      float k2 = 1.0f / (dy + 0.12f); /* a flat cloud deck seen in perspective */
      uv[v * 2] = dx * k2 * 0.35f, uv[v * 2 + 1] = dz * k2 * 0.35f;
    }
  }
  for (int r = 0; r < RINGS; r++)
    for (int s = 0; s < SEG; s++) {
      uint32_t a = (uint32_t)(r * (SEG + 1) + s), b = a + 1, c = a + SEG + 1, d = c + 1;
      ix[k++] = a, ix[k++] = c, ix[k++] = b, ix[k++] = b, ix[k++] = c, ix[k++] = d;
    }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, p, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, n, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, nv, 2));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, ix, ni, 1));
  free(p), free(uv), free(n), free(ix);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  CLOUD_TEX = load_tex("textures/clouds_c.png", true);
  if (CLOUD_TEX) t3_material_set_texture(m, T3_MAP, CLOUD_TEX);
  m->transparent = true, m->depth_write = false, m->fog = false, m->side = T3_DOUBLE_SIDE;
  m->color = (t3_color){ 1.25f, 1.25f, 1.3f };
  t3_mesh *mesh = t3_mesh_new(g, m);
  mesh->base.cast_shadow = false, mesh->base.receive_shadow = false, mesh->base.frustum_culled = false;
  mesh->base.render_order = -100;
  SKY_DOME = cw_node_new("MeshInstance3D", "Clouds");
  cw_node_add_child(world, SKY_DOME);
  cw_mesh3d_set_procedural(SKY_DOME, mesh);
  t3_release(mesh), t3_release(g), t3_release(m);
  if (getenv("ADV_PROBE_NOCLOUDS")) cw_node_set(SKY_DOME, "visible", cw_val_bool(false));
}
void world_sky(t3_vec3 cam, float t) {
  if (!SKY_DOME.id) return;
  cw_node3d_set_position(SKY_DOME, (t3_vec3){ cam.x, cam.y, cam.z });
  if (CLOUD_TEX) CLOUD_TEX->offset = (t3_vec2){ t * 0.0016f, t * 0.0007f };
}
