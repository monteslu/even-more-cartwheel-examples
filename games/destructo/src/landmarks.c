/* destructo's landmarks: famous buildings of the world, scaled to the field and built of the game's blocks.
 *
 * The sculptor. A landmark is a shape (a function: what fills a point, which material and paint) sampled on a grid of
 * cells, a course at a time, and laid as masonry: each course runs its bricks along x or z in turn (the courses lock
 * together as a running bond does), and a run of filled cells is split into bricks one to eight cells long by a small
 * search that keeps every brick's middle over something it stands on (a cell of the course below, or any block or the
 * ground under it). So a brick may reach a cell out past the course below (a corbel: a dome swelling, a saucer's
 * underside) or bridge a gap (a lintel over an arch, a platform between the legs of a tower), and a cell nothing can
 * hold is left out: whatever a shape asks for, everything laid stands on something.
 *
 * The shapes keep each landmark's silhouette and proportions first (the profile, the setbacks, the domes and spires),
 * then its key features (arches, windows, terraces, antennas) and its materials: stone that breaks into pieces,
 * steel lattice that bends rather than breaks, glass that shatters, brick. Thin things too slender to stand as
 * masonry (spires, masts, antennas, minarets' tips) are stacks of single blocks. Every level is checked
 * (tools/levelcheck.c): it stands still for a minute alone, nothing floats, and the bot clears it within par. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

#define GAP 0.004f
#define VGAP 0.0004f /* courses laid all but touching: a tall stack laid 4 mm a course apart drops a hand span at its top as it settles, and slams */
#define CELL(m, p) ((uint16_t)(((p) << 8) | ((m) + 1)))
#define LMAX 14

static inline float sq(float x) { return x * x; }
static inline t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }

typedef uint16_t (*shape_fn)(float x, float y, float z);

typedef struct sculpt {
  shape_fn fn;
  float x0, x1, z0, z1, y0, y1; /* the box sampled */
  float cell, course;           /* cell size across and course height */
  bool long_bricks;             /* a big mass: bricks of four cells cost no more than two (fewer, bigger blocks) */
} sculpt;

/* something to stand on just under (x, yb, z): a block or the ground */
static bool under(game *g, float x, float yb, float z, float r) {
  cw_qshape3d q = cw_qshape3d_box(v3(x, yb - 0.012f, z), v3(r, 0.008f, r), (t3_quat){ 0, 0, 0, 1 });
  cw_phys_filter f = cw_phys_filter_all();
  cw_shape3d hit[1];
  return cw_world3d_overlap(g->w, &q, &f, hit, 1) > 0;
}

static int sculpt_run(game *g, const sculpt *s) {
  int nx = (int)ceilf((s->x1 - s->x0) / s->cell), nz = (int)ceilf((s->z1 - s->z0) / s->cell);
  int ny = (int)((s->y1 - s->y0) / s->course + 0.02f); /* never past y1 (the next sculpt may start there) */
  if (nx < 1 || nz < 1 || ny < 1 || nx > 400 || nz > 400) return 0;
  uint16_t *code = malloc(sizeof(uint16_t) * (size_t)(nx * nz));
  uint8_t *sup = malloc((size_t)(nx * nz)), *prev = calloc((size_t)(nx * nz), 1), *now = calloc((size_t)(nx * nz), 1);
  int laid = 0;
  for (int j = 0; j < ny; j++) {
    float yb = s->y0 + j * s->course, yc = yb + s->course * 0.5f;
    for (int i = 0; i < nx; i++)
      for (int k = 0; k < nz; k++) {
        int c = i * nz + k;
        float x = s->x0 + (i + 0.5f) * s->cell, z = s->z0 + (k + 0.5f) * s->cell;
        code[c] = s->fn(x, yc, z);
        sup[c] = code[c] && (prev[c] || (j == 0 && s->y0 <= 0.001f) || under(g, x, yb, z, s->cell * 0.3f));
      }
    memset(now, 0, (size_t)(nx * nz));
    bool along_x = (j & 1) == 0;
    int nl = along_x ? nz : nx, n = along_x ? nx : nz;
    for (int l = 0; l < nl; l++) {
#define AT(t) (along_x ? (t) * nz + l : l * nz + (t))
      for (int a = 0; a < n;) {
        uint16_t cc = code[AT(a)];
        if (!cc) { a++; continue; }
        int b = a;
        while (b < n && code[AT(b)] == cc) b++;
        /* the run a..b: the cheapest split into bricks, each standing with its middle over support (or a cell dropped) */
        float best[401];
        int from[401];
        best[a] = 0;
        for (int p = a + 1; p <= b; p++) best[p] = 1e30f, from[p] = -1;
        for (int p = a; p < b; p++) {
          if (best[p] >= 1e29f) continue;
          if (best[p] + 30 < best[p + 1]) best[p + 1] = best[p] + 30, from[p + 1] = -(p + 1) - 1; /* drop cell p */
          int lo = -1, hi = -1;
          for (int q = p + 1; q <= b && q - p <= LMAX; q++) {
            if (sup[AT(q - 1)]) { if (lo < 0) lo = q - 1; hi = q - 1; }
            float mid = (p + q - 1) * 0.5f;
            if (lo < 0 || lo > mid || hi < mid) continue;
            int L = q - p;
            float cost = L == 2 ? 0 : L == 1 ? 1.6f : L == 3 ? 0.5f : L == 4 ? (s->long_bricks ? 0.0f : 1.0f) : 1.5f + 0.3f * (L - 5);
            if (q < b && ((q + l + j) & 1)) cost += 0.35f; /* joints staggered line to line */
            if (best[p] + cost < best[q]) best[q] = best[p] + cost, from[q] = p;
          }
        }
        /* lay the split, from the run's end back */
        for (int q = b; q > a;) {
          int p = from[q];
          if (p < -1) { q = -p - 2; continue; } /* a dropped cell */
          if (p < 0) break;
          int mat = (cc & 0xff) - 1, paint = cc >> 8;
          float c0 = (along_x ? s->x0 : s->z0) + p * s->cell, c1 = (along_x ? s->x0 : s->z0) + q * s->cell;
          float o = (along_x ? s->z0 : s->x0) + (l + 0.5f) * s->cell;
          t3_vec3 ctr = along_x ? v3((c0 + c1) * 0.5f, yc, o) : v3(o, yc, (c0 + c1) * 0.5f);
          float hl = (c1 - c0) * 0.5f - GAP, hw = s->cell * 0.5f - GAP, hh = s->course * 0.5f - VGAP;
          g->paint_now = (uint8_t)paint;
          if (game_add_block(g, mat, ctr, along_x ? v3(hl, hh, hw) : v3(hw, hh, hl), 0) >= 0) {
            laid++;
            for (int t = p; t < q; t++) now[AT(t)] = 1;
          }
          q = p;
        }
        a = b;
      }
#undef AT
    }
    if (getenv("LM_DEBUG")) {
      int f = 0, su = 0, pl = 0;
      for (int c = 0; c < nx * nz; c++) f += code[c] != 0, su += sup[c], pl += now[c];
      fprintf(stderr, "course %d y %.2f: %d filled, %d over support, %d laid\n", j, yb, f, su, pl);
    }
    uint8_t *t = prev;
    prev = now, now = t;
  }
  g->paint_now = P_NONE;
  free(code), free(sup), free(prev), free(now);
  return laid;
}

/* one block, its bottom at yb */
static int blk(game *g, int mat, int paint, float x, float yb, float z, float hx, float hy, float hz) {
  g->paint_now = (uint8_t)paint;
  int i = game_add_block(g, mat, v3(x, yb + hy, z), v3(hx - GAP, hy - VGAP, hz - GAP), 0);
  g->paint_now = P_NONE;
  return i;
}
/* a stack of blocks from y0 to y1, w across, seg tall each, tapering to w1 at the top; returns the top */
static float mast(game *g, int mat, int paint, float x, float z, float y0, float y1, float w0, float w1, float seg) {
  int n = (int)((y1 - y0) / seg + 0.5f);
  if (n < 1) n = 1;
  float h = (y1 - y0) / n;
  for (int i = 0; i < n; i++) {
    float w = w0 + (w1 - w0) * (float)i / (float)(n > 1 ? n - 1 : 1);
    blk(g, mat, paint, x, y0 + i * h, z, w * 0.5f, h * 0.5f, w * 0.5f);
  }
  return y0 + n * h;
}

/* the top of whatever stands highest round (x, z) (a few rays: one may fall between two blocks) */
static float top_at(game *g, float x, float z) {
  cw_phys_filter f = cw_phys_filter_layers(3u);
  float top = 0;
  for (int k = 0; k < 5; k++) {
    float dx = k == 0 ? 0 : (k & 1 ? 0.06f : -0.06f), dz = k == 0 ? 0 : (k & 2 ? 0.06f : -0.06f);
    cw_ray_hit3d h;
    if (cw_world3d_raycast(g->w, v3(x + dx, 80, z + dz), v3(0, -100, 0), &f, &h) && h.point.y + GAP > top) top = h.point.y + GAP;
  }
  return top;
}
/* would a block here touch another (1 cm in from its faces)? */
static bool clashes(game *g, t3_vec3 c, t3_vec3 half) {
  cw_qshape3d q = cw_qshape3d_box(c, v3(half.x - 0.01f, half.y - 0.01f, half.z - 0.01f), (t3_quat){ 0, 0, 0, 1 });
  cw_phys_filter f = cw_phys_filter_layers(2u);
  cw_shape3d hit[1];
  return cw_world3d_overlap(g->w, &q, &f, hit, 1) > 0;
}
/* the distance up from (x, y, z) to the underside of whatever is above, 100 when nothing is */
static float gap_above(game *g, float x, float y, float z) {
  cw_phys_filter f = cw_phys_filter_layers(2u);
  cw_ray_hit3d h;
  return cw_world3d_raycast(g->w, v3(x, y + 0.001f, z), v3(0, 100, 0), &f, &h) ? h.point.y - y : 100;
}

bool landmark_crystal(game *g, float x, float z) {
  cw_phys_filter f = cw_phys_filter_layers(3u); /* the ground and the blocks */
  for (int ring = 0; ring < 7; ring++)
    for (int a = 0; a < (ring ? 8 : 1); a++) {
      float px = x + (ring ? cw_cos(a * 0.785398f) * ring * 0.18f : 0), pz = z + (ring ? cw_sin(a * 0.785398f) * ring * 0.18f : 0);
      float top = -1e9f, low = 1e9f;
      for (int c = 0; c < 5; c++) {
        float dx = c == 0 ? 0 : (c & 1 ? 0.24f : -0.24f), dz = c == 0 ? 0 : (c & 2 ? 0.24f : -0.24f);
        cw_ray_hit3d h;
        if (!cw_world3d_raycast(g->w, v3(px + dx, 80, pz + dz), v3(0, -100, 0), &f, &h)) { low = -1e9f; break; }
        if (h.point.y > top) top = h.point.y;
        if (h.point.y < low) low = h.point.y;
      }
      if (top - low > 0.03f) continue; /* not flat under it */
      return game_add_target(g, v3(px, top + 0.32f + GAP, pz)) >= 0;
    }
  return false;
}

/* ── the Great Sphinx: a lion lying with its paws out, the head of a king in a striped headdress; facing +x ── */
static uint16_t sh_sphinx(float x, float y, float z) {
  const uint16_t S = CELL(M_STONE, P_SAND_STONE);
  float az = fabsf(z);
  if (y < 0.3f) return x > -6.1f && x < 6.5f && az < 1.95f ? CELL(M_STONE, P_ROCK) : 0; /* the bedrock it is cut from */
  if (y < 0.75f && x > 2.4f && x < 6.0f && fabsf(az - 0.58f) < 0.34f) return S;  /* the paws, reaching out in front */
  /* the body: the haunch rounded at the back, the back long and low, rising a little to the shoulders */
  float top = x < -4.2f ? 0.3f + 1.25f * sqrtf(fmaxf(0, 1 - sq((x + 4.2f) / 1.0f))) : 1.55f + 0.3f * (x + 4.2f) / 6.0f;
  if (x > -5.2f && x < 2.9f && y < top) {
    float hw = 1.2f * sqrtf(fmaxf(0.25f, 1 - sq(fmaxf(0, y - 0.8f) / (top - 0.4f))));
    if (az < hw) return S;
  }
  if (x >= 1.8f && x < 3.0f && y < 2.0f && az < 1.0f) return S;                    /* the chest and shoulders */
  /* the headdress (the nemes): broad lappets falling to the shoulders behind the face, striped */
  float back = 1.55f + 0.35f * (y - 1.8f) / 1.6f;                                  /* its back edge, sloping forward */
  if (y >= 1.8f && y < 3.45f && x > back && x < 3.0f) {
    float w = y < 3.0f ? 0.98f - 0.42f * (y - 1.8f) / 1.2f : 0.56f * sqrtf(fmaxf(0, 1 - sq((y - 3.0f) / 0.47f)));
    if (az < w) return CELL(M_STONE, ((int)(y / 0.3f) & 1) ? P_LIMESTONE : P_SAND_STONE);
  }
  /* the face, its nose standing out */
  if (x >= 3.0f && x < 3.3f && y >= 2.15f && y < 3.2f && az < 0.42f) return S;
  if (x >= 3.3f && x < 3.5f && y >= 2.45f && y < 2.85f && az < 0.16f) return S;
  return 0;
}
static void build_sphinx(game *g, const level_def *L) {
  sculpt s = { sh_sphinx, -6.3f, 6.6f, -2.0f, 2.0f, 0, 3.6f, 0.25f, 0.3f, false };
  sculpt_run(g, &s);
  static const float spot[][2] = { { 2.45f, 0 }, { -3.0f, 0 }, { 4.6f, 0 } };
  for (int t = 0; t < L->targets && t < 3; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
}

/* ── the Pyramids of Giza: the Great Pyramid, the second with its cap of casing, the small third, on a diagonal ── */
static struct { float cx, cz, half, h, cap; } PYR;
static uint16_t sh_pyramid(float x, float y, float z) {
  float t = y / PYR.h;
  if (t >= 1) return 0;
  float hw = PYR.half * (1 - t);
  if (fabsf(x - PYR.cx) > hw || fabsf(z - PYR.cz) > hw) return 0;
  return CELL(M_STONE, t > PYR.cap ? P_LIMESTONE : P_SAND_STONE);
}
static void build_giza(game *g, const level_def *L) {
  static const float P[3][5] = { { -7.3f, -5.2f, 4.75f, 6.0f, 2 }, { 1.9f, 0.8f, 4.3f, 5.5f, 0.8f }, { 8.7f, 6.8f, 2.1f, 2.7f, 2 } };
  for (int i = 0; i < 3; i++) {
    PYR.cx = P[i][0], PYR.cz = P[i][1], PYR.half = P[i][2], PYR.h = P[i][3], PYR.cap = P[i][4];
    float c = i == 2 ? 0.42f : 0.6f;
    sculpt s = { sh_pyramid, PYR.cx - PYR.half, PYR.cx + PYR.half, PYR.cz - PYR.half, PYR.cz + PYR.half, 0, PYR.h, c, c * 0.58f, true };
    sculpt_run(g, &s);
  }
  for (int t = 0; t < L->targets && t < 3; t++) landmark_crystal(g, P[t][0], P[t][1]);
}

/* ── the Space Needle: three pairs of legs pinched at the waist round a core, the saucer on top, the spire ── */
#define SN_SAUCER 12.0f
static uint16_t sh_needle_top(float x, float y, float z) {
  float r = sqrtf(x * x + z * z), t = y - SN_SAUCER;
  /* the underside: a disc on the legs' heads and the core, sloping out to the rim; above it the saucer is a ring of
   * rooms round the core (light: a heavy saucer on a slender shaft rocks it over) */
  float under = 1.72f + t * 0.6f;
  bool ring = r > 1.3f || r < 0.6f;
  if (t < 0.48f) return r < fminf(under, 1.95f) ? CELL(M_STONE, P_WHITE_PAINT) : 0;
  if (t < 0.72f) return r < 1.95f && ring ? CELL(M_STONE, P_GOLD) : 0;  /* the halo */
  if (t < 1.08f) return r < 1.95f && ring ? CELL(M_GLASS, P_BLUE_GLASS) : 0; /* the windows round the restaurant */
  if (t < 1.2f) return r < 1.95f ? CELL(M_STONE, P_WHITE_PAINT) : 0;   /* the observation deck */
  if (t < 1.8f) return r < 1.35f - (t - 1.2f) * 1.4f ? CELL(M_STONE, P_WHITE_PAINT) : 0; /* the roof up to the spire */
  return 0;
}
/* the shaft: the core, and three pairs of legs as fins out from it, flaring wide at the foot, pinched at the waist
 * and flaring again up under the saucer (each pair: two white legs, the gap between them dark) */
static float needle_leg(float y) {
  if (y < 7.2f) return 2.3f - (2.3f - 0.72f) * sqrtf(fmaxf(0, y) / 7.2f);
  return 0.72f + (1.7f - 0.72f) * sq((y - 7.2f) / (SN_SAUCER - 7.2f));
}
static uint16_t sh_needle_shaft(float x, float y, float z) {
  if (y < 0.24f) return fabsf(x) < 2.7f && fabsf(z) < 2.7f ? CELL(M_STONE, P_LIMESTONE) : 0; /* the plaza */
  float r = sqrtf(x * x + z * z);
  if (r < 0.56f) return CELL(M_STONE, P_WHITE_PAINT);
  for (int p = 0; p < 6; p++) { /* three pairs of legs, the two of a pair a little apart */
    float a = 0.5235988f + (p >> 1) * 2.0943951f + ((p & 1) ? 0.2f : -0.2f), sa = cw_sin(a), ca = cw_cos(a);
    float along = x * sa + z * ca, across = fabsf(-x * ca + z * sa);
    if (along > 0 && along < needle_leg(y) && across < 0.17f) return CELL(M_STEEL, P_WHITE_PAINT);
  }
  return 0;
}
static void build_needle(game *g, const level_def *L) {
  sculpt p = { sh_needle_shaft, -2.7f, 2.7f, -2.7f, 2.7f, 0, 0.24f, 0.6f, 0.24f, true };
  sculpt_run(g, &p);
  sculpt s = { sh_needle_shaft, -2.7f, 2.7f, -2.7f, 2.7f, 0.24f, SN_SAUCER, 0.2f, (SN_SAUCER - 0.24f) / 20, false };
  sculpt_run(g, &s);
  sculpt t = { sh_needle_top, -2.0f, 2.0f, -2.0f, 2.0f, SN_SAUCER, SN_SAUCER + 1.8f, 0.25f, 0.12f, false };
  sculpt_run(g, &t);
  mast(g, M_STEEL, P_WHITE_PAINT, 0, 0, top_at(g, 0, 0), SN_SAUCER + 4.2f, 0.26f, 0.14f, 1.2f);
  for (int k = 0; k < L->targets && k < 2; k++) landmark_crystal(g, k ? -1.65f : 0, k ? 0 : 1.65f);
}

/* ── the Eiffel Tower: four legs sweeping in under arches to the first platform, the second, the tall shaft, the top ── */
#define EF_P1 3.1f
#define EF_P2 6.3f
#define EF_TOP 15.2f
#define EF_UPPER 9.0f /* the slender upper shaft: taller courses (every course is a joint that gives a little) */
static float eif_w(float y) { return fmaxf(3.4f * expf(-y / 5.5f), 0.36f); }
#define EF_S 0.82f /* the tower as designed below, scaled to the field */
static uint16_t sh_eiffel_unit(float x, float y, float z) {
  const uint16_t B = CELL(M_STEEL, P_BRONZE);
  float ax = fabsf(x), az = fabsf(z), w = eif_w(y);
  if (fabsf(y - EF_P1) < 0.3f) return ax < eif_w(EF_P1) + 0.3f && az < eif_w(EF_P1) + 0.3f ? B : 0; /* the platforms */
  if (fabsf(y - EF_P2) < 0.15f) return ax < eif_w(EF_P2) + 0.2f && az < eif_w(EF_P2) + 0.2f ? B : 0;
  if (ax > w || az > w) return 0;
  if (y > EF_TOP) return y < EF_TOP + 0.9f && ax < 0.3f && az < 0.3f ? B : 0; /* the top's cupola */
  bool face = ax > w - 0.3f || az > w - 0.3f, corner = ax > w - 0.3f && az > w - 0.3f;
  /* the lattice: holes through the faces between the corners' columns */
  int cy = (int)floorf(y / 0.3f), cs = (int)floorf((ax > az ? z : x) / 0.3f + 100);
  bool hole = face && !corner && (((cy + cs) & 3) != 0 && ((cy - cs) & 3) != 0); /* the lattice: crossed bracing, open between */
  if (y < EF_P2) {
    /* a leg's inner edge leans in less than its outer edge (its weight stays over its foot); above the first
     * platform the legs close in to meet at the second */
    float inner = y < EF_P1 ? 1.25f : 0;
    if (ax > inner && az > inner) { /* a leg: open lattice on its faces, solid at its edges */
      int fx = (ax > w - 0.3f) + (ax < inner + 0.3f), fz = (az > w - 0.3f) + (az < inner + 0.3f);
      int cl = (int)floorf((fx ? z : x) / 0.3f + 100);
      return (fx + fz == 1 && ((cy + cl) & 3) != 0 && ((cy - cl) & 3) != 0) ? CELL(M_STEEL, P_DARK_GLASS) : B; /* the lattice's openings: dark (a real hole would leave what is above it standing on nothing) */
    }
    if (y < EF_P1 && y > EF_P1 - 1.0f && face) {  /* an arch's spandrel on each face */
      float s = (ax > az ? az : ax) / inner;
      if (y > EF_P1 - 1.0f * sqrtf(fmaxf(0, 1 - s * s))) return B;
    }
    return 0;
  }
  return hole ? CELL(M_STEEL, P_DARK_GLASS) : B;
}
static uint16_t sh_eiffel(float x, float y, float z) { return sh_eiffel_unit(x / EF_S, y / EF_S, z / EF_S); }
static void build_eiffel(game *g, const level_def *L) {
  /* each leg's foot against a stone pier on its outer sides: the legs lean in, and their feet would slide out */
  for (int k = 0; k < 4; k++) {
    float sx = k & 1 ? 1 : -1, sz = k & 2 ? 1 : -1;
    float o = 3.5f * EF_S + 0.3f; /* just outside the leg's foot */
    blk(g, M_STONE, P_LIMESTONE, sx * o, 0, sz * 2.4f * EF_S, 0.27f, 0.3f, 1.25f * EF_S);
    blk(g, M_STONE, P_LIMESTONE, sx * 2.25f * EF_S, 0, sz * o, 1.0f * EF_S, 0.3f, 0.27f);
  }
  float S = EF_S, e = 3.6f * S;
  sculpt s = { sh_eiffel, -e, e, -e, e, 0, 7.5f, 0.2f, 0.3f, false };
  sculpt_run(g, &s);
  sculpt u = { sh_eiffel, -1.5f, 1.5f, -1.5f, 1.5f, 7.5f, 13.5f, 0.2f, 0.6f, false };
  sculpt_run(g, &u);
  mast(g, M_STEEL, P_BRONZE, 0, 0, top_at(g, 0, 0), (EF_TOP + 2.6f) * S, 0.16f, 0.16f, 3.0f);
  static const float spot[][2] = { { 1.0f * EF_S, 1.95f * EF_S }, { -1.0f * EF_S, -1.95f * EF_S } }; /* on the first platform, off the tower's middle (a beacon up through it veils it) */
  for (int t = 0; t < L->targets && t < 2; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
}

/* ── the Empire State Building: the podium, the setbacks, the long shaft of limestone piers and windows, the
 * stepped crown, the mast ── */
static uint16_t sh_empire(float x, float y, float z) {
  static const float SEC[][3] = { { 1.0f, 2.5f, 1.15f }, { 3.2f, 1.7f, 1.0f }, { 3.9f, 1.45f, 0.9f }, { 11.3f, 1.15f, 0.8f },
                                  { 12.6f, 0.9f, 0.65f }, { 13.3f, 0.7f, 0.5f }, { 13.65f, 0.55f, 0.42f } };
  float ax = fabsf(x), az = fabsf(z);
  int s = 0;
  while (s < 7 && y >= SEC[s][0]) s++;
  if (s >= 7) { /* the mast */
    float r = 0.36f - 0.1f * (y - 13.65f) / 1.6f;
    return y < 15.25f && ax < r && az < r ? CELL(M_STONE, P_SILVER) : 0;
  }
  float hx = SEC[s][1], hz = SEC[s][2];
  if (ax > hx || az > hz) return 0;
  /* hollow inside, floored at each setback, under the section above's own hollow (its walls stand on solid below) */
  float ix = s < 6 ? fminf(hx, SEC[s + 1][1]) : hx, iz = s < 6 ? fminf(hz, SEC[s + 1][2]) : hz;
  if (ax < ix - 0.5f && az < iz - 0.5f && y > 0.3f && y < SEC[s][0] - 0.3f) return 0;
  bool fx = ax > hx - 0.25f, fz = az > hz - 0.25f;
  if (s >= 1 && (fx || fz) && !(fx && fz)) { /* the faces: limestone piers between columns of windows */
    int col = (int)floorf((fx ? z : x) / 0.25f + 100);
    if ((col & 1) && fmodf(y, 0.56f) > 0.1f) return CELL(M_GLASS, P_NONE);
  }
  return CELL(M_STONE, P_LIMESTONE);
}
static void build_empire(game *g, const level_def *L) {
  sculpt s = { sh_empire, -2.5f, 2.5f, -1.25f, 1.25f, 0, 15.25f, 0.25f, 0.56f, false };
  sculpt_run(g, &s);
  mast(g, M_STEEL, P_SILVER, 0, 0, top_at(g, 0, 0), 17.0f, 0.14f, 0.08f, 0.9f);
  static const float spot[][2] = { { 1.75f, 0 }, { -2.05f, 0.75f } };
  for (int t = 0; t < L->targets && t < 2; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
}

/* ── the Sydney Opera House: two rows of white shells rising like sails over a granite podium by the water ── */
static const float SHELL[][5] = { /* x of the tip, height, z, half width at the foot, length */
  { -2.4f, 2.6f, 0.95f, 0.85f, 2.0f }, { -0.9f, 3.3f, 0.95f, 0.95f, 2.4f }, { 0.6f, 2.7f, 0.95f, 0.85f, 2.0f }, { 1.9f, 1.9f, 0.95f, 0.7f, 1.6f },
  { -2.2f, 2.2f, -1.05f, 0.75f, 1.8f }, { -0.8f, 2.7f, -1.05f, 0.85f, 2.0f }, { 0.5f, 2.2f, -1.05f, 0.75f, 1.7f }, { 1.6f, 1.6f, -1.05f, 0.6f, 1.4f },
  { -4.3f, 1.2f, 0, 0.6f, 1.2f }, { -3.65f, 1.0f, 0, 0.5f, 1.0f },
};
#define OP_DECK 0.6f
static uint16_t sh_opera(float x, float y, float z) {
  float h = y - OP_DECK;
  for (int i = 0; i < 10; i++) {
    const float *S = SHELL[i];
    float xt = S[0], xb = xt - S[4], top;
    if (x < xb || x > xt + 0.28f * S[4] * 0.5f) continue;
    if (x <= xt) top = S[1] * powf((x - xb) / (xt - xb), 0.55f);
    else top = S[1] * (1 - (x - xt) / (0.14f * S[4]));
    if (h > top) continue;
    float w = 1.2f * S[3] * powf(fmaxf(0, 1 - h / S[1]), 0.4f) * fminf(1, (x - xb) / (0.3f * S[4]) + 0.45f);
    if (fabsf(z - S[2]) < w) return CELL(M_STONE, ((int)floorf(h / 0.2f) & 1) ? P_TILE_WHITE : P_MARBLE); /* the tiles in bands */
  }
  return 0;
}
static uint16_t sh_opera_podium(float x, float y, float z) {
  if (fabsf(z) > 2.2f) return 0;
  if (x > -4.9f && x < 3.2f) return CELL(M_STONE, P_ROSE_STONE);
  if (x <= -4.9f && x > -6.1f && y < OP_DECK * (x + 6.1f) / 1.2f + 0.1f) return CELL(M_STONE, P_ROSE_STONE); /* the steps up from the city */
  return 0;
}
#define OP_S 1.35f /* the house as designed above, scaled to the field */
static uint16_t sh_opera_s(float x, float y, float z) { return sh_opera(x / OP_S + 1.4f, y / OP_S, z / OP_S); }
static uint16_t sh_opera_podium_s(float x, float y, float z) { return sh_opera_podium(x / OP_S + 1.4f, y / OP_S, z / OP_S); }
static void build_opera(game *g, const level_def *L) {
  const float S = OP_S, dx = -1.4f * OP_S; /* centred on the field */
  sculpt p = { sh_opera_podium_s, -6.1f * S - dx, 3.2f * S - dx, -2.2f * S, 2.2f * S, 0, OP_DECK * S, 0.5f, OP_DECK * S / 2, true };
  sculpt_run(g, &p);
  sculpt s = { sh_opera_s, -5.6f * S - dx, 2.4f * S - dx, -2.0f * S, 2.0f * S, OP_DECK * S, (OP_DECK + 3.4f) * S, 0.26f, 0.26f, false };
  sculpt_run(g, &s);
  static const float spot[][2] = { { 2.7f, 0 }, { -4.6f, 1.6f } };
  for (int t = 0; t < L->targets && t < 2; t++) landmark_crystal(g, (spot[t][0] - 1.4f) * S, spot[t][1] * S);
}

/* ── the Taj Mahal: the white marble tomb on its plinth, the great onion dome on its drum, four kiosks, the pointed
 * arches of its faces and four minarets at the plinth's corners ── */
#define TJ_PL 0.72f
#define TJ_ROOF 4.1f
static uint16_t sh_taj_plinth(float x, float y, float z) { return fabsf(x) < 5.0f && fabsf(z) < 5.0f ? CELL(M_STONE, P_MARBLE) : 0; }
static uint16_t sh_taj(float x, float y, float z) {
  const uint16_t M = CELL(M_STONE, P_MARBLE);
  float ax = fabsf(x), az = fabsf(z);
  if (y < TJ_ROOF) {
    if (ax > 2.9f || az > 2.9f || ax + az > 4.7f) return 0;
    float s = ax > az ? az : ax, d = ax > az ? 2.9f - ax : 2.9f - az; /* along the face, and in from it */
    if (ax + az > 4.1f && d > -1) { s = 0; d = 4.7f - (ax + az); } /* the chamfered corners */
    float arch = TJ_PL + 2.3f + 0.5f * (1 - fminf(1, s / 0.95f));
    if (s < 0.95f && d < 0.36f && y < arch) return 0;                  /* the great arch of each face (its iwan) */
    if (s > 1.45f && s < 2.15f && d < 0.24f && fmodf(y - TJ_PL, 1.55f) < 1.15f) return 0; /* two storeys of small arches */
    return M;
  }
  float r = sqrtf(x * x + z * z);
  if (y < 4.85f) return r < 1.45f ? M : 0;                             /* the drum */
  float t = (y - 4.85f) / 2.6f;
  if (t < 1) {                                                          /* the onion dome */
    float rd = t < 0.3f ? 1.45f + 0.4f * sinf(t / 0.3f * 1.5707963f) : 1.85f * powf(fmaxf(0, 1 - sq((t - 0.3f) / 0.7f)), 0.65f);
    if (r < rd) return M;
  }
  /* the kiosks at the roof's corners */
  for (int k = 0; k < 4; k++) {
    float cx = (k & 1 ? 1 : -1) * 2.15f, cz = (k & 2 ? 1 : -1) * 2.15f, rk = sqrtf(sq(x - cx) + sq(z - cz));
    if (y < 4.7f && rk < 0.42f && (rk > 0.2f || y > 4.55f)) return M;
    if (y >= 4.7f && y < 5.35f && rk < 0.5f * sqrtf(fmaxf(0, 1 - sq((y - 4.7f) / 0.65f)))) return M;
  }
  return 0;
}
static uint16_t sh_minaret(float x, float y, float z) {
  float r = sqrtf(x * x + z * z), h = y - TJ_PL;
  if (h < 4.0f) {
    float rr = 0.36f - 0.05f * h / 4.0f;
    if (fabsf(h - 1.4f) < 0.12f || fabsf(h - 2.7f) < 0.12f || fabsf(h - 3.85f) < 0.15f) rr += 0.12f; /* its balconies */
    return r < rr ? CELL(M_STONE, P_MARBLE) : 0;
  }
  return h < 4.55f && r < 0.36f * sqrtf(fmaxf(0, 1 - sq((h - 4.0f) / 0.55f))) ? CELL(M_STONE, P_MARBLE) : 0;
}
static float minaret_x, minaret_z;
static uint16_t sh_minaret_at(float x, float y, float z) { return sh_minaret(x - minaret_x, y, z - minaret_z); }
static void build_taj(game *g, const level_def *L) {
  sculpt p = { sh_taj_plinth, -5.0f, 5.0f, -5.0f, 5.0f, 0, TJ_PL, 0.8f, 0.36f, true };
  sculpt_run(g, &p);
  sculpt s = { sh_taj, -2.95f, 2.95f, -2.95f, 2.95f, TJ_PL, 7.5f, 0.3f, 0.34f, true };
  sculpt_run(g, &s);
  mast(g, M_STEEL, P_GOLD, 0, 0, top_at(g, 0, 0), 8.2f, 0.16f, 0.08f, 0.26f); /* the finial */
  for (int k = 0; k < 4; k++) {
    minaret_x = (k & 1 ? 1 : -1) * 4.45f, minaret_z = (k & 2 ? 1 : -1) * 4.45f;
    sculpt m = { sh_minaret_at, minaret_x - 0.5f, minaret_x + 0.5f, minaret_z - 0.5f, minaret_z + 0.5f, TJ_PL, TJ_PL + 4.55f, 0.2f, 0.25f, false };
    sculpt_run(g, &m);
  }
  static const float spot[][2] = { { 0, 2.45f }, { 2.45f, 0 }, { -3.6f, 4.3f } };
  for (int t = 0; t < L->targets && t < 3; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
}

/* ── Willis Tower: nine black square tubes bundled, ending at four heights, two white antennas on the tallest ── */
#define WT_T 0.78f
#define WT_FLOOR (15.0f / 108.0f)
static const int WT_H[3][3] = { { 66, 108, 90 }, { 90, 108, 90 }, { 50, 66, 50 } }; /* floors, by row (back to front) and column */
static uint16_t sh_willis(float x, float y, float z) {
  int i = (int)floorf(x / WT_T + 1.5f), j = (int)floorf(z / WT_T + 1.5f);
  if (i < 0 || i > 2 || j < 0 || j > 2) return 0;
  if (y > WT_H[j][i] * WT_FLOOR) return 0;
  bool face = fabsf(x) > 1.5f * WT_T - 0.27f || fabsf(z) > 1.5f * WT_T - 0.27f;
  /* every tube's face: dark glass between the black frame's bands */
  if (face && fmodf(y, 1.8f) > 0.6f) return CELL(M_GLASS, P_DARK_GLASS);
  return CELL(M_STONE, P_DARK_STONE);
}
static void build_willis(game *g, const level_def *L) {
  float h = 1.5f * WT_T;
  sculpt s = { sh_willis, -h, h, -h, h, 0, 108 * WT_FLOOR, 0.26f, 0.6f, false };
  sculpt_run(g, &s);
  for (int k = 0; k < 2; k++) { float az = (k ? -1 : 0) * WT_T + (k ? 0.18f : -0.12f); mast(g, M_STEEL, P_WHITE_PAINT, 0, az, top_at(g, 0, az), 18.4f, 0.15f, 0.15f, 3.4f); }
  static const float spot[][2] = { { 0.75f, 0.78f }, { -0.78f, -0.78f } };
  for (int t = 0; t < L->targets && t < 2; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
}

/* ── Neuschwanstein Castle: white walls and slender towers under slate cones on a rock ridge, the red gatehouse ── */
#define NS_ROCK 1.6f
static uint16_t sh_ns_rock(float x, float y, float z) {
  float e = sq(x / 6.6f) + sq(z / 2.9f);
  if (e > 1) return 0;
  float top = NS_ROCK;
  if (!(x > -5.4f && x < 4.1f && fabsf(z) < 1.35f)) top = NS_ROCK * (1.15f - 0.85f * e) + 0.25f * sinf(x * 2.1f + z * 3.3f);
  return y < top ? CELL(M_STONE, P_ROCK) : 0;
}
static bool cyl(float x, float z, float cx, float cz, float r) { return sq(x - cx) + sq(z - cz) < r * r; }
static uint16_t tower_at(float x, float y, float z, float cx, float cz, float r, float wall_top, float cone_h) {
  if (!cyl(x, z, cx, cz, r + 0.12f)) return 0;
  if (y < wall_top) {
    if (!cyl(x, z, cx, cz, r)) return 0;
    if (fmodf(y, 1.2f) > 0.7f && fmodf(y, 1.2f) < 1.0f && !cyl(x, z, cx, cz, r - 0.25f)) return CELL(M_GLASS, P_NONE);
    return CELL(M_STONE, P_WHITE_STONE);
  }
  float t = (y - wall_top) / cone_h;
  return t < 1 && cyl(x, z, cx, cz, (r + 0.12f) * (1 - t)) ? CELL(M_WOOD, P_ROOF_SLATE) : 0;
}
static uint16_t sh_ns(float x, float y, float z) {
  uint16_t c;
  /* the towers first (they stand above and through the rest) */
  if ((c = tower_at(x, y, z, -1.6f, -0.5f, 0.66f, 8.4f, 2.6f))) return c;  /* the tall stair tower */
  if ((c = tower_at(x, y, z, 3.55f, 1.0f, 0.42f, 6.2f, 1.9f))) return c;
  if ((c = tower_at(x, y, z, 3.55f, -1.0f, 0.42f, 6.2f, 1.9f))) return c;
  if ((c = tower_at(x, y, z, -1.25f, 1.1f, 0.36f, 7.0f, 1.6f))) return c;
  if ((c = tower_at(x, y, z, -2.7f, 1.0f, 0.3f, 4.9f, 0.9f))) return c;     /* the gatehouse's turrets */
  if ((c = tower_at(x, y, z, -2.7f, -1.0f, 0.3f, 4.9f, 0.9f))) return c;
  float az = fabsf(z);
  /* the palace (the palas): white walls, rows of windows, the steep slate roof */
  if (x > -1.2f && x < 3.4f) {
    if (y < 6.2f && az < 1.05f) {
      if (az < 0.5f && x > -0.6f && x < 2.8f && y < 5.9f) return 0; /* inside */
      bool face = az > 0.8f || x < -0.95f || x > 3.15f;
      float fy = fmodf(y - NS_ROCK, 1.12f);
      int col = (int)floorf(x / 0.28f + 100);
      if (face && fy > 0.5f && fy < 0.95f && (col % 3) == 1) return CELL(M_GLASS, P_NONE);
      return CELL(M_STONE, y < NS_ROCK + 0.3f ? P_LIMESTONE : P_WHITE_STONE);
    }
    if (y >= 6.2f && az < 1.15f * (1 - (y - 6.2f) / 1.7f)) return CELL(M_WOOD, P_ROOF_SLATE);
    return 0;
  }
  /* the wall to the gatehouse */
  if (x >= -2.6f && x <= -1.2f) return az < 0.55f && y < 3.7f ? CELL(M_STONE, P_WHITE_STONE) : 0;
  /* the gatehouse: red, its gate on the front, a flat roof behind a parapet */
  if (x > -5.2f && x < -2.6f && az < 1.0f) {
    if (y > 4.1f) return (az > 0.75f || x < -4.95f || x > -2.85f) && y < 4.4f ? CELL(M_STONE, P_RED_BRICK) : 0;
    if (z > 0.7f && fabsf(x + 3.9f) < 0.4f && y < NS_ROCK + 1.0f + 0.35f * (1 - fabsf(x + 3.9f) / 0.4f)) return 0; /* the gate */
    return CELL(M_STONE, P_RED_BRICK);
  }
  return 0;
}
static void build_neuschwanstein(game *g, const level_def *L) {
  sculpt r = { sh_ns_rock, -6.6f, 6.6f, -2.9f, 2.9f, 0, NS_ROCK + 0.4f, 0.6f, 0.4f, true };
  sculpt_run(g, &r);
  sculpt s = { sh_ns, -5.3f, 4.1f, -1.35f, 1.35f, NS_ROCK, 11.2f, 0.28f, 0.28f, false };
  sculpt_run(g, &s);
  static const float spot[][2] = { { -3.9f, 0 }, { -1.9f, 0 }, { 0.6f, 2.0f } };
  for (int t = 0; t < L->targets && t < 3; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
}

/* ── Burj Khalifa: three wings round a core in a Y, each stepping back at its own heights so the setbacks spiral
 * up to the core's top, then the long spire ── */
#define BK_CORE 16.0f
static uint16_t sh_burj(float x, float y, float z) {
  float r = sqrtf(x * x + z * z);
  float core = y < 13.0f ? 0.62f : 0.62f - 0.3f * (y - 13.0f) / (BK_CORE - 13.0f);
  bool in = r < core && y < BK_CORE;
  for (int w = 0; w < 3 && !in; w++) {
    float a = 0.5235988f + w * 2.0943951f, dx = cw_sin(a), dz = cw_cos(a);
    float along = x * dx + z * dz, across = fabsf(-x * dz + z * dx);
    int step = (int)floorf((y + w * 0.45f) / 1.35f);
    float reach = 1.85f * (1 - step / 11.0f);
    if (along > 0 && along < reach && across < 0.42f) in = true;
  }
  if (!in) return 0;
  /* silver glass between steel fins */
  if (fmodf(y, 0.72f) > 0.24f) return CELL(M_GLASS, P_BLUE_GLASS);
  return CELL(M_STONE, P_SILVER);
}
static void build_burj(game *g, const level_def *L) {
  sculpt s = { sh_burj, -2.0f, 2.0f, -2.0f, 2.0f, 0, BK_CORE, 0.2f, 0.48f, false };
  sculpt_run(g, &s);
  mast(g, M_STEEL, P_SILVER, 0, 0, top_at(g, 0, 0), 22.0f, 0.36f, 0.12f, 2.0f);
  static const float spot[][2] = { { -0.95f, 0.9f }, { 0, -1.2f } };
  for (int t = 0; t < L->targets && t < 2; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
}

void landmark_build(game *g, const level_def *L) {
  g->paint_now = P_NONE;
  switch (L->lm) {
  case LM_SPHINX: build_sphinx(g, L); break;
  case LM_SPACE_NEEDLE: build_needle(g, L); break;
  case LM_GIZA: build_giza(g, L); break;
  case LM_EIFFEL: build_eiffel(g, L); break;
  case LM_EMPIRE_STATE: build_empire(g, L); break;
  case LM_OPERA_HOUSE: build_opera(g, L); break;
  case LM_TAJ_MAHAL: build_taj(g, L); break;
  case LM_WILLIS: build_willis(g, L); break;
  case LM_NEUSCHWANSTEIN: build_neuschwanstein(g, L); break;
  case LM_BURJ_KHALIFA: build_burj(g, L); break;
  default: break;
  }
  g->paint_now = P_NONE;
}
