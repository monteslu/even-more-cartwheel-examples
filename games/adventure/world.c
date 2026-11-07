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

const char *const REGION_NAME[RG_COUNT] = { "THE GREEN HOLLOW", "THE AMBER CANYON", "THE TIDEMARSH", "THE EMBER KEEP" };

wobj g_obj[MAXOBJ];
int g_nobj;
wfoe g_foe[MAXE];
int g_nfoe;
wroute g_route[96];
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
static float land(float x, float z, int *paint, int *region) {
  float h = 31 + 9 * fbm(x / 55, z / 55, 7, 4); /* the highlands between the regions */
  float bd = fminf(fminf(x, WORLD_N - x), fminf(z, WORLD_N - z));
  h += 16 * (1 - smooth01(0, 28, bd)); /* mountains round the edge */
  int pt = 4, rg = -1;
  float best = 0;
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
    static const int HIGH[RG_COUNT] = { 0, 4, 0, 7 };
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
    if (w > 0.6f) pt = RAMPS[i].paint;
  }
  if (paint) *paint = pt;
  if (region) *region = rg;
  return h;
}
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
typedef struct sbox { float cx, cy, cz, hx, hy, hz, yaw; int rg, mt; } sbox;
static sbox *BOX;
static int nbox, capbox;
static void box_add(int rg, int mt, float cx, float cy, float cz, float hx, float hy, float hz, float yaw, bool solid) {
  if (nbox == capbox) capbox = capbox * 2 + 256, BOX = realloc(BOX, sizeof(sbox) * (size_t)capbox);
  BOX[nbox++] = (sbox){ cx, cy, cz, hx, hy, hz, yaw, rg, mt };
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
/* the props that also block (a tree trunk, a boulder, a pillar, a brazier): a box in the region's body */
static void prop_solid(int rg, const char *file, float x, float z, float yaw, float scale, float r, float sink, float hr, float h) {
  cw_node n = prop(rg, file, x, z, yaw, scale, r, sink, true);
  if (!n.id) return;
  t3_vec3 p = cw_node3d_position(n);
  col_box(rg, x, p.y + h / 2, z, hr, h / 2, hr, yaw);
}

/* ── the objects the rules touch ───────────────────────────────────── */
static cw_node OBJ_NODE[MAXOBJ], OBJ_PART[MAXOBJ];
static cw_body3d OBJ_BODY[MAXOBJ];
static int obj_add(wobj o) {
  if (g_nobj >= MAXOBJ) return -1;
  g_obj[g_nobj] = o;
  return g_nobj++;
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
  int *pt = malloc(sizeof(int) * (size_t)N * N);
  const cw_terrain_def *d = cw_terrain_get_def(W_terrain);
  for (int z = 0; z < N; z++)
    for (int x = 0; x < N; x++) h[z * N + x] = land((float)x, (float)z, &pt[z * N + x], NULL) - d->origin.y;
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
      if (base == 0) { over = 1, blend = (int)(cw_clampf((n - 0.45f) * 3.0f, 0, 1) * 255); }        /* flowers in drifts */
      if (base == 5) { over = 0, blend = (int)(cw_clampf((h[i] + d->origin.y - 8.6f) * 1.2f, 0, 1) * 200); } /* grass on the marsh's dry humps */
      if (base == 7) { over = 4, blend = (int)(cw_clampf((n - 0.5f) * 2.0f, 0, 1) * 160); }
      if (slope > 0.55f) { over = 4, blend = (int)(cw_clampf((slope - 0.55f) * 3.0f, 0, 1) * 255); }
      if (slope > 0.9f) base = 4, blend = 0;
      ctl[i] = cw_terrain_control(base, over, blend, (int)(hash01(x / 4, z / 4, 9) * 15), 0, false);
    }
  cw_terrain_set_control(W_terrain, 0, 0, N, N, ctl);
  cw_terrain_sync_physics(W_terrain);
  cw_terrain_scatter_invalidate(W_terrain);
  for (int i = 0; i < N * N; i += 97) W_hash = (W_hash ^ (uint32_t)(int)(h[i] * 1000)) * 16777619u;
  free(h), free(ctl), free(pt);
}

static void build_meadow(void) {
  const int R = RG_MEADOW;
  shrine(R, 160, 279, 0);
  house(R, 136, 252, 8, 6, 3.2f, 1, true);
  house(R, 186, 246, 7, 7, 2.6f, 3, false);
  house(R, 147, 226, 6, 5, 2.2f, 0, false);
  house(R, 178, 268, 6, 5, 3.0f, 2, true);
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
  for (int i = 0; i < (int)(sizeof TREES / sizeof TREES[0]); i++) prop_solid(R, "tree", TREES[i][0], TREES[i][1], TREES[i][2], 1.0f + 0.15f * (float)(i % 3), 0.45f, 0.3f, 0.35f, 3);
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
  for (int i = 0; i < 5; i++) prop_solid(R, "tree_gold", GOLD[i][0], GOLD[i][1], (float)i, 1.0f, 0.4f, 0.3f, 0.3f, 2.4f);
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
  for (int i = 0; i < 8; i++) prop_solid(R, "mangrove", MANG[i][0], MANG[i][1], (float)i * 0.8f, 1.0f, 0.6f, 0.2f, 0.4f, 3);
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
static void build_route(void) {
  /* the autopilot's way through the game, in order: rest, the canyon, the maul, the Thornwarden, the passage, the
   * marsh, the Tidecaller, the lever, the meadow's cellar, the gate, the keep, the Ember King */
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
  route(258, 214, RA_GO, -1, -1);
  route(231, 214, RA_USE, -1, 7);
  route(216, 220, RA_GO, -1, -1);
  route(190, 236, RA_GO, -1, -1);
  route(140, 240, RA_GO, -1, -1);
  route(129, 234, RA_HIT, -1, 1);
  route(123, 234, RA_GO, -1, 2);
  route(129.5f, 235, RA_GO, -1, -1);
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
  g_nobj = g_nfoe = g_nroute = nbox = nprop = nshrine = 0;
  build_meadow();
  build_canyon();
  build_marsh();
  build_keep();
  build_route();
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
  cw_info("adventure: probe land %.2f terrain %.2f ground %.2f", land(160, 281, NULL, NULL), world_height(160, 281), world_ground(160, 281, 100));
  cw_info("adventure: world built: %d boxes, %d props, %d objects, %d foes, %d route points, hash %08x", nbox, nprop, g_nobj, g_nfoe, g_nroute, W_hash);
}

/* ── paging: the player's region and its neighbours are live ───────── */
static const int NEAR[RG_COUNT] = { (1 << RG_MEADOW) | (1 << RG_CANYON) | (1 << RG_MARSH) | (1 << RG_KEEP), (1 << RG_MEADOW) | (1 << RG_CANYON) | (1 << RG_MARSH),
                                    (1 << RG_MEADOW) | (1 << RG_CANYON) | (1 << RG_MARSH), (1 << RG_MEADOW) | (1 << RG_KEEP) };
void world_page(float x, float z, int region) {
  (void)x, (void)z;
  int live = NEAR[CW_CLAMP(region, 0, RG_COUNT - 1)];
  if (live == g_world_paged) return;
  for (int r = 0; r < RG_COUNT; r++) cw_node_set(W_region_node[r], "visible", cw_val_bool((live >> r) & 1));
  g_world_paged = live;
}

/* ── colour: each region's light, blended as the camera moves between them ── */
typedef struct look { float fog[3], density, amb[3], amb_e, sky[3], gnd[3], hemi, sun[3], sun_e, expo; } look;
static const look LOOK[RG_COUNT] = {
  /* the meadow: a clear gold morning, blue-white haze */
  { { 0.6f, 0.76f, 0.98f }, 0.003f, { 1, 0.95f, 0.85f }, 0.35f, { 0.6f, 0.76f, 1 }, { 0.42f, 0.38f, 0.22f }, 0.75f, { 1, 0.93f, 0.8f }, 3.4f, 1.05f },
  /* the canyon: amber light, a warm rose haze */
  { { 1.0f, 0.72f, 0.52f }, 0.0032f, { 1, 0.85f, 0.7f }, 0.4f, { 0.7f, 0.68f, 0.95f }, { 0.6f, 0.36f, 0.2f }, 0.8f, { 1, 0.82f, 0.6f }, 3.6f, 1.0f },
  /* the marsh: teal and violet, a thicker cool mist */
  { { 0.42f, 0.7f, 0.72f }, 0.012f, { 0.7f, 0.95f, 1 }, 0.45f, { 0.55f, 0.6f, 1 }, { 0.2f, 0.4f, 0.32f }, 0.85f, { 0.85f, 0.95f, 1 }, 3.0f, 1.08f },
  /* the keep: a crimson sunset, ember haze */
  { { 0.85f, 0.45f, 0.36f }, 0.008f, { 1, 0.7f, 0.6f }, 0.4f, { 0.85f, 0.55f, 0.7f }, { 0.45f, 0.2f, 0.12f }, 0.8f, { 1, 0.62f, 0.42f }, 3.3f, 1.02f },
};
static look CUR;
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
  cw_node_set(W_env, "ambient_energy", cw_val_float(CUR.amb_e));
  cw_node_set(W_env, "sky_color", cw_val_color(CUR.sky[0], CUR.sky[1], CUR.sky[2], 1));
  cw_node_set(W_env, "ground_color", cw_val_color(CUR.gnd[0], CUR.gnd[1], CUR.gnd[2], 1));
  cw_node_set(W_env, "hemi_energy", cw_val_float(CUR.hemi));
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
    const sbox *b = &BOX[i];
    float s = cw_sin(b->yaw), c = cw_cos(b->yaw), bottom = b->cy - b->hy, hi = -1e30f;
    for (int k = 0; k < 4; k++) { /* the highest ground under its corners must reach its foot */
      float lx = (k & 1 ? 1 : -1) * b->hx * 0.95f, lz = (k & 2 ? 1 : -1) * b->hz * 0.95f;
      float g = world_ground(b->cx + lx * c + lz * s, b->cz - lx * s + lz * c, bottom + 0.02f);
      if (g > hi) hi = g;
    }
    float gap = bottom - hi;
    if (gap > 0.03f) { bad++; cw_warn("adventure: box %d floats %.3f m at (%.1f, %.1f)", i, gap, b->cx, b->cz); }
    if (gap > worst) worst = gap;
  }
  *worst_out = worst;
  return bad;
}
