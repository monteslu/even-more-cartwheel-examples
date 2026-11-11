/* destructo's landmarks: famous buildings of the world, scaled to the field and built of the game's blocks.
 *
 * The sculptor. A landmark is a shape (a function: what fills a point, which material and paint) sampled on a grid of
 * cells, a course at a time, and laid as masonry: each course runs its bricks along x or z in turn (the courses lock
 * together as a running bond does), and a run of filled cells is split into bricks one to fourteen cells long by a small
 * search that keeps every brick's middle over something it stands on (a cell of the course below, or any block or the
 * ground under it). So a brick may reach a cell out past the course below (a corbel: a dome swelling, a saucer's
 * underside) or bridge a gap (a lintel over an arch, a platform between the legs of a tower), and a cell nothing can
 * hold is left out: whatever a shape asks for, everything laid stands on something.
 *
 * The shapes. Most landmarks are cells made by tools/voxelize.mjs (src/lm_data.c): from an openly licensed 3D model
 * of the landmark (CREDITS names each), or from a shape drawn by hand in tools/landmark-procs.mjs where no good open
 * model exists. A solid landmark keeps a skin of small bricks (the detail the camera sees: windows, arches, strata,
 * domes) over a core of big blocks (k cells by k courses: few bodies, and the core is not counted in the share brought
 * down). Steel lattice, statues, shells and roofs on eaves are welded instead: their cells merged into members (the
 * longest box up each column, then across), each welded to the members it touches, so a frame stands that masonry
 * could not (an Eiffel Tower's legs, a statue's arms, an opera house's vaults); a weld goes when either member breaks.
 * Monoliths (Stonehenge) are single stones. Every level is checked (tools/levelcheck.c): it stands still for a minute
 * alone, nothing floats, and the bot clears it within par. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"
#include "lm_data.h"
#include "opts.h"
#include "box3d/box3d.h"

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
  bool weld;                    /* steel lattice: every cell laid (each brick is welded to the bricks it touches, weld_blocks), not only those standing on support */
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
        sup[c] = code[c] && (s->weld || prev[c] || (j == 0 && s->y0 <= 0.001f) || under(g, x, yb, z, s->cell * 0.3f));
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
bool landmark_crystal(game *g, float x, float z) {
  cw_phys_filter f = cw_phys_filter_layers(3u); /* the ground and the blocks */
  for (int ring = 0; ring < 16; ring++) /* out to 2.9 m: a landmark's flat tops are where its shape puts them */
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

/* ── a landmark made from a model (tools/voxelize.mjs, src/lm_data.c): its cells laid as masonry. The inside the
 * voxeliser marked as core goes down first as big blocks (k cells by k courses), and the shell of small bricks is
 * laid round and over it, each brick on support as always ── */
static const lm_vox *VX;
static uint8_t *VXG;
static float VX_dx, VX_dy, VX_dz;
static uint8_t vx_at(float x, float y, float z) {
  int i = (int)floorf((x - VX_dx - VX->x0) / VX->cell), j = (int)floorf((y - VX_dy) / VX->course), k = (int)floorf((z - VX_dz - VX->z0) / VX->cell);
  if (i < 0 || j < 0 || k < 0 || i >= VX->nx || j >= VX->ny || k >= VX->nz) return 0;
  return VXG[((size_t)j * VX->nx + i) * VX->nz + k];
}
static uint16_t sh_vox(float x, float y, float z) {
  uint8_t v = vx_at(x, y, z);
  return v == 0 || v >= 254 ? 0 : VX->pal[v - 1];
}
static uint16_t sh_vox_core(float x, float y, float z) { return vx_at(x, y, z) == 255 ? VX->pal[0] : 0; }
static uint16_t sh_vox_core2(float x, float y, float z) { return vx_at(x, y, z) == 254 ? VX->pal[0] : 0; }
/* steel lattice: each brick from..to is bonded to the others of from..to it touches: the bonded members are one rigid
 * chunk (chunks.c), held up by the support graph and by nothing else (no joint: a joint is a soft constraint). */
static void weld_blocks(game *g, int from, int to) {
  cw_phys_filter f = cw_phys_filter_layers(2u);
  for (int i = from; i < to; i++) {
    const block *a = &g->b[i];
    if (!a->alive) continue;
    cw_xform3 xa = game_block_xform(g, i);
    cw_qshape3d q = cw_qshape3d_box(xa.p, v3(a->half.x + 0.012f, a->half.y + 0.006f, a->half.z + 0.012f), xa.q);
    cw_shape3d hit[64];
    int n = cw_world3d_overlap(g->w, &q, &f, hit, 64);
    for (int h = 0; h < n; h++) {
      int j = game_block_of_shape(hit[h]);
      if (j <= i || j >= to || !g->b[j].alive) continue;
      game_weld(g, i, j);
    }
  }
}

/* a bond between blocks i and j (their members stay one chunk), recorded in game.weld */
void game_weld(game *g, int i, int j) {
  if (g->nweld == g->weld_cap) {
    g->weld_cap = g->weld_cap ? g->weld_cap * 2 : 4096;
    g->weld = realloc(g->weld, (size_t)g->weld_cap * sizeof *g->weld);
  }
  g->weld[g->nweld++] = (struct weld_pair){ (uint16_t)i, (uint16_t)j };
}

/* the blocks laid since `from` turned yaw about the vertical through (cx, cz): a landmark set on the field at the
 * angle its default view wants (the grid stays square to the model, so its walls are clean and its members long) */
static void turn_blocks(game *g, int from, float cx, float cz, float yaw) {
  if (yaw == 0) return;
  float s, c;
  cw_sincos(yaw, &s, &c);
  float hs, hc;
  cw_sincos(yaw * 0.5f, &hs, &hc);
  for (int i = from; i < g->nb; i++) {
    block *b = &g->b[i];
    if (!b->alive) continue;
    cw_xform3 x = game_block_xform(g, i);
    float px = x.p.x - cx, pz = x.p.z - cz;
    x.p.x = cx + px * c + pz * s, x.p.z = cz - px * s + pz * c;
    x.q = (t3_quat){ 0, hs, 0, hc };
    cw_body3d_set_transform(g->ch[b->chunk].body, x);
    b->home = x.p;
  }
}
/* the model's cells, the grid's middle at (dx, dz), its foot at dy, the whole turned yaw about that middle; returns the
 * blocks laid */
static int build_vox_turned(game *g, const lm_vox *v, float dx, float dy, float dz, float yaw) {
  size_t n = (size_t)v->nx * v->ny * v->nz, c = 0;
  int from0 = g->nb;
  VXG = malloc(n);
  for (int r = 0; r < v->nruns && c < n; r++) {
    uint8_t val = v->runs[2 * r], cnt = v->runs[2 * r + 1];
    for (int t = 0; t < cnt && c < n; t++) VXG[c++] = val;
  }
  VX = v, VX_dx = dx, VX_dy = dy, VX_dz = dz;
  float x0 = dx + v->x0, z0 = dz + v->z0, x1 = x0 + v->nx * v->cell, z1 = z0 + v->nz * v->cell, y0 = dy, y1 = dy + v->ny * v->course;
  int laid = 0;
  if (v->core > 1) {
    int cf = g->nb;
    sculpt k = { sh_vox_core, x0, x1, z0, z1, y0, y1, v->cell * v->core, v->course * v->core, true };
    laid += sculpt_run(g, &k);
    if (v->core >= 4) {
      sculpt k2 = { sh_vox_core2, x0, x1, z0, z1, y0, y1, v->cell * (v->core / 2), v->course * (v->core / 2), true };
      laid += sculpt_run(g, &k2);
    }
    for (int i = cf; i < g->nb; i++) g->b[i].tag = 2; /* the core: not counted in the share brought down */
  }
  if (v->weld) { /* a welded lattice: its cells as members, each the longest box of one paint up the column, then as wide
                  * as that holds (a member is a body, however many cells: fewer, longer joints keep the frame stiff) */
    int from = g->nb, NX = v->nx, NY = v->ny, NZ = v->nz;
#define VI(i, j, k) (((size_t)(j) * NX + (i)) * NZ + (k))
    uint8_t *used = calloc(n, 1);
    for (int j = 0; j < NY; j++)
      for (int i = 0; i < NX; i++)
        for (int k = 0; k < NZ; k++) {
          uint8_t c0 = VXG[VI(i, j, k)];
          if (!c0 || c0 >= 254 || used[VI(i, j, k)]) continue;
          int h = 1, w = 1, d = 1;
          while (j + h < NY && h < v->mh && VXG[VI(i, j + h, k)] == c0 && !used[VI(i, j + h, k)]) h++;
          for (bool grow = true; grow && i + w < NX && w < v->mw;) {
            for (int t = 0; t < h && grow; t++) grow = VXG[VI(i + w, j + t, k)] == c0 && !used[VI(i + w, j + t, k)];
            if (grow) w++;
          }
          for (bool grow = true; grow && k + d < NZ && d < v->mw;) {
            for (int t = 0; t < h && grow; t++)
              for (int s2 = 0; s2 < w && grow; s2++) grow = VXG[VI(i + s2, j + t, k + d)] == c0 && !used[VI(i + s2, j + t, k + d)];
            if (grow) d++;
          }
          for (int t = 0; t < h; t++)
            for (int s2 = 0; s2 < w; s2++)
              for (int u = 0; u < d; u++) used[VI(i + s2, j + t, k + u)] = 1;
          uint16_t cc = v->pal[c0 - 1];
          if (blk(g, (cc & 0xff) - 1, cc >> 8, x0 + (i + w * 0.5f) * v->cell, y0 + j * v->course, z0 + (k + d * 0.5f) * v->cell,
                  w * v->cell * 0.5f, h * v->course * 0.5f, d * v->cell * 0.5f) >= 0)
            laid++;
        }
#undef VI
    free(used);
    turn_blocks(g, from0, dx, dz, yaw);
    weld_blocks(g, from, g->nb);
  } else {
    sculpt s = { sh_vox, x0, x1, z0, z1, y0, y1, v->cell, v->course, false };
    laid += sculpt_run(g, &s);
    turn_blocks(g, from0, dx, dz, yaw);
  }
  free(VXG), VXG = NULL;
  return laid;
}
static int build_vox(game *g, const lm_vox *v, float dx, float dy, float dz) { return build_vox_turned(g, v, dx, dy, dz, 0); }

#define WT_T 0.78f /* Willis Tower: a tube's width (its antennas stand on the middle two) */

/* one block turned yaw about y, its bottom at yb */
static int blk_yaw(game *g, int mat, int paint, float x, float yb, float z, float hx, float hy, float hz, float yaw) {
  g->paint_now = (uint8_t)paint;
  int i = game_add_block(g, mat, v3(x, yb + hy, z), v3(hx - GAP, hy - VGAP, hz - GAP), yaw);
  g->paint_now = P_NONE;
  return i;
}

/* ── Stonehenge: the sarsen circle of uprights capped by a ring of lintels, part fallen; inside it the horseshoe of
 * five great trilithons rising to the back, the bluestones and the altar stone. Monoliths, as they are: each stone
 * one block (its upper part a second, a little narrower: the stones taper and are dressed at the top) ── */
#define SH_R 7.6f
static void sarsen(game *g, float a, float r, float h, float w, float t, int paint) {
  float x = cw_sin(a) * r, z = cw_cos(a) * r;
  float lo = h * 0.62f;
  blk_yaw(g, M_STONE, paint, x, 0, z, w * 0.5f, lo * 0.5f, t * 0.5f, a);
  blk_yaw(g, M_STONE, paint, x, lo, z, w * 0.44f, (h - lo) * 0.5f, t * 0.46f, a);
}
static void build_stonehenge(game *g, const level_def *L) {
  const float step = 6.2831853f / 30;
  /* the circle: the stones that still stand (stone 0 faces the launcher), and the runs of lintels on them */
  static const uint8_t stands[30] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1 };
  static const uint8_t lintel[30] = { 1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1 }; /* on i and i + 1 */
  const float H = 2.2f;
  for (int i = 0; i < 30; i++) {
    float a = i * step;
    uint32_t hsh = hash32((uint32_t)i * 977u + 13u);
    float wob = 0.9f + 0.2f * hashf(hsh);
    bool capped = lintel[i] || lintel[(i + 29) % 30]; /* an upright under a lintel is dressed to the ring's height (a lintel on two uprights of different heights tips off) */
    if (stands[i]) sarsen(g, a, SH_R, capped ? H : H * (0.97f + 0.06f * hashf(hsh >> 3)), 0.86f * wob, 0.5f, P_SARSEN);
    else if ((hsh & 3) == 0) { /* fallen: lying in the grass beside its socket */
      float x = cw_sin(a) * (SH_R + 0.9f), z = cw_cos(a) * (SH_R + 0.9f);
      blk_yaw(g, M_STONE, P_SARSEN, x, 0, z, 0.42f, 0.24f, 0.78f, a + 0.3f);
    }
  }
  for (int i = 0; i < 30; i++) {
    if (!lintel[i] || !stands[i] || !stands[(i + 1) % 30]) continue;
    float a = (i + 0.5f) * step, r = SH_R * cw_cos(step * 0.5f);
    blk_yaw(g, M_STONE, P_SARSEN, cw_sin(a) * r, H + 0.002f, cw_cos(a) * r, SH_R * cw_sin(step * 0.5f) - 0.01f, 0.2f, 0.27f, a + 1.5707963f);
  }
  /* the bluestone circle, small and many gone */
  for (int i = 0; i < 40; i++) {
    uint32_t hsh = hash32((uint32_t)i * 131u + 7u);
    if (hsh % 5 < 2) continue;
    float a = i * 6.2831853f / 40, h = 0.55f + 0.35f * hashf(hsh >> 4);
    blk_yaw(g, M_STONE, P_DARK_STONE, cw_sin(a) * 4.5f, 0, cw_cos(a) * 4.5f, 0.17f, h * 0.5f, 0.12f, a);
  }
  /* the horseshoe of trilithons, open toward the launcher, the great one at the back; [angle, height, standing] */
  static const float TRI[5][3] = { { 1.15f, 2.15f, 2 }, { -1.15f, 2.15f, 1 }, { 2.05f, 2.5f, 2 }, { -2.05f, 2.5f, 2 }, { 3.14159f, 2.95f, 0 } };
  for (int t = 0; t < 5; t++) {
    float a = TRI[t][0], h = TRI[t][1], r = 2.9f;
    float cx = cw_sin(a) * r, cz = cw_cos(a) * r, tx = cw_cos(a), tz = -cw_sin(a); /* along the trilithon */
    int st = (int)TRI[t][2];
    for (int k = 0; k < 2; k++) {
      float o = (k ? 0.55f : -0.55f);
      if (st == 1 && k) { blk_yaw(g, M_STONE, P_SARSEN, cx + tx * o * 2.2f, 0, cz + tz * o * 2.2f, 0.5f, 0.25f, 1.1f, a + 0.4f); continue; } /* one leg down */
      if (st == 0 && !k) continue;
      float x = cx + tx * o, z = cz + tz * o, lo = h * 0.62f;
      blk_yaw(g, M_STONE, P_SARSEN, x, 0, z, 0.45f, lo * 0.5f, 0.31f, a);
      blk_yaw(g, M_STONE, P_SARSEN, x, lo, z, 0.4f, (h - lo) * 0.5f, 0.29f, a);
    }
    if (st == 2) blk_yaw(g, M_STONE, P_SARSEN, cx, h + 0.002f, cz, 1.1f, 0.22f, 0.3f, a);
    if (st == 0) blk_yaw(g, M_STONE, P_SARSEN, cx + tx * 1.4f, 0, cz + tz * 1.4f - 0.6f, 1.15f, 0.22f, 0.32f, a + 1.0f); /* the great lintel, fallen */
  }
  /* the altar stone, flat in the middle */
  blk(g, M_STONE, P_SAND_STONE, 0, 0, 0.6f, 0.95f, 0.16f, 0.32f);
  static const float spot[][2] = { { 0, 0.6f }, { 0, -2.9f } };
  for (int t = 0; t < L->targets && t < 2; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
}

/* a landmark made from its model alone, and its crystals */
/* a model at the field's middle turned yaw, its crystals at the spots (given square to the model, turned with it) */
static void build_model_turned(game *g, const level_def *L, const lm_vox *v, const float (*spot)[2], int nspot, float yaw) {
  build_vox_turned(g, v, 0, 0, 0, yaw);
  float s, c;
  cw_sincos(yaw, &s, &c);
  for (int t = 0; t < L->targets && t < nspot; t++) landmark_crystal(g, spot[t][0] * c + spot[t][1] * s, -spot[t][0] * s + spot[t][1] * c);
}
static void build_model(game *g, const level_def *L, const lm_vox *v, const float (*spot)[2], int nspot) { build_model_turned(g, L, v, spot, nspot, 0); }
static uint16_t sh_box_fill(float x, float y, float z) { (void)x, (void)y, (void)z; return CELL(M_STONE, P_ROCK); }

/* ── Ahu Tongariki: fifteen moai in a row on their stone platform, their backs to the sea ── */
static void build_moai(game *g, const level_def *L) {
  const float AHU = 0.45f, LEN = 23.0f;
  sculpt p = { sh_box_fill, -LEN / 2, LEN / 2, -1.95f, 1.2f, 0, AHU, 0.3f, AHU / 2, true };
  sculpt_run(g, &p);
  for (int m = 0; m < 15; m++) {
    float x = -LEN / 2 + 0.75f + m * (LEN - 1.5f) / 14;
    build_vox(g, &LMV_moai, x, AHU, 0);
  }
  static const float spot[][2] = { { -3.8f, -1.45f }, { 3.1f, -1.45f }, { 9.2f, -1.45f } }; /* on the platform behind them */
  for (int t = 0; t < L->targets && t < 3; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
}

/* ── the Statue of Liberty on its pedestal, on the eleven-pointed star of Fort Wood ── */
static uint16_t sh_star_fort(float x, float y, float z) {
  (void)y;
  float a = atan2f(z, x), t = fmodf((a + 3.14159265f) * 11.0f / 6.2831853f, 1.0f);
  float r = 2.7f + 0.9f * (1 - fabsf(t - 0.5f) * 2); /* a point every eleventh of the way round */
  return sqrtf(x * x + z * z) < r ? CELL(M_STONE, P_WHITE_STONE) : 0;
}
static void build_liberty(game *g, const level_def *L) {
  const float FORT = 0.9f;
  sculpt f = { sh_star_fort, -3.7f, 3.7f, -3.7f, 3.7f, 0, FORT, 0.25f, 0.3f, true };
  sculpt_run(g, &f);
  build_vox(g, &LMV_liberty, 0, FORT, 0);
  static const float spot[][2] = { { 0, 2.1f }, { -1.8f, -1.1f } };
  for (int t = 0; t < L->targets && t < 2; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
}

/* ── Christ the Redeemer: the statue, arms out, on its plinth on the summit ── */
static void build_christ(game *g, const level_def *L) {
  const float PL = 2.4f;
  sculpt p = { sh_box_fill, -1.2f, 1.2f, -1.2f, 1.2f, 0, PL, 0.3f, 0.3f, true };
  sculpt_run(g, &p);
  build_vox(g, &LMV_christ_redeemer, 0, PL, 0);
  static const float spot[][2] = { { 0.9f, 0.9f } };
  for (int t = 0; t < L->targets && t < 1; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
}

void landmark_build(game *g, const level_def *L) {
  g->paint_now = P_NONE;
  switch (L->lm) {
  case LM_SPHINX: { static const float s[][2] = { { -3.6f, 0 }, { 1.4f, 0 }, { 5.5f, 0.75f } }; build_model(g, L, &LMV_sphinx, s, 3); break; }
  case LM_SPACE_NEEDLE: { static const float s[][2] = { { 0, 1.35f }, { -1.35f, 0 } }; build_model(g, L, &LMV_needle, s, 2); break; }
  case LM_GIZA: { static const float s[][2] = { { -5.8f, -6.7f }, { 3.4f, -0.7f }, { 10.2f, 5.3f } }; build_model(g, L, &LMV_giza, s, 3); break; }
  case LM_EIFFEL: { static const float s[][2] = { { 1.0f, 1.6f }, { -1.0f, -1.6f } }; build_model(g, L, &LMV_eiffel, s, 2); break; }
  case LM_EMPIRE_STATE: {
    build_vox(g, &LMV_empire, 0, 0, 0);
    mast(g, M_STEEL, P_SILVER, 0, 0, top_at(g, 0, 0), 17.0f, 0.14f, 0.08f, 0.9f);
    static const float spot[][2] = { { 1.75f, 0 }, { -2.05f, 0.75f } };
    for (int t = 0; t < L->targets && t < 2; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
    break;
  }
  case LM_OPERA_HOUSE: { /* turned 40 degrees on the field: the default view is the one from the quay, into the shells' glazed mouths */
    static const float s[][2] = { { 6.2f, 0 }, { -7.4f, 2.6f } }; /* the first spot well inside the podium's rounded harbour end (at 7.6 the crystal lay into its edge bricks) */
    build_vox_turned(g, &LMV_opera_podium, 0, 0, 0, 0.698f);
    build_model_turned(g, L, &LMV_opera_shells, s, 2, 0.698f);
    break;
  }
  case LM_TAJ_MAHAL: { static const float s[][2] = { { 0, 2.6f }, { 2.6f, 0 }, { -4.2f, 4.2f } }; build_model(g, L, &LMV_taj_mahal, s, 3); break; }
  case LM_WILLIS: {
    build_vox(g, &LMV_willis, 0, 0, 0);
    for (int k = 0; k < 2; k++) { float az = (k ? -1 : 0) * WT_T + (k ? 0.18f : -0.12f); mast(g, M_STEEL, P_WHITE_PAINT, 0, az, top_at(g, 0, az), 18.4f, 0.15f, 0.15f, 3.4f); }
    static const float spot[][2] = { { 0.75f, 0.78f }, { -0.78f, -0.78f } };
    for (int t = 0; t < L->targets && t < 2; t++) landmark_crystal(g, spot[t][0], spot[t][1]);
    break;
  }
  case LM_NEUSCHWANSTEIN: { /* turned 25 degrees: the default view is the three-quarter one, the gatehouse in front */
    static const float s[][2] = { { 7.3f, 0 }, { -3.0f, 0.6f }, { 2.8f, 0 } };
    build_vox_turned(g, &LMV_ns_rock, 0, 0, 0, -0.436f);
    build_vox_turned(g, &LMV_ns_gate, 0, 0, 0, -0.436f); /* the gatehouse in brick courses (welded members read as planks) */
    build_model_turned(g, L, &LMV_neuschwanstein, s, 3, -0.436f);
    break;
  }
  case LM_BURJ_KHALIFA: { static const float s[][2] = { { -0.95f, 0.9f }, { 0, -1.2f } }; build_model(g, L, &LMV_burj_khalifa, s, 2); break; }
  case LM_STONEHENGE: build_stonehenge(g, L); break;
  case LM_PETRONAS: { static const float s[][2] = { { 0, 0.75f }, { 0, -0.75f } }; build_vox(g, &LMV_pt_bridge, 0, 0, 0); build_model(g, L, &LMV_petronas, s, 2); break; }
  case LM_CN_TOWER: { static const float s[][2] = { { 0, 2.4f } }; /* clear of the base (a crystal at the shaft's foot lay into it) */ build_model(g, L, &LMV_cn_tower, s, 1); break; }
  case LM_ANGKOR_WAT: { static const float s[][2] = { { 5.2f, 0 }, { -5.2f, 0 }, { 0, 4.4f } }; build_model(g, L, &LMV_angkor_wat, s, 3); break; }
  case LM_ST_BASILS: { static const float s[][2] = { { 0, 5.4f }, { 5.4f, 0 }, { -5.4f, 0 } }; build_vox(g, &LMV_sb_base, 0, 0, 0); build_model(g, L, &LMV_st_basils, s, 3); break; }
  case LM_TOWER_BRIDGE: { static const float s[][2] = { { -4.0f, 0 }, { 4.0f, 0 } }; build_vox(g, &LMV_tb_steel, 0, 0, 0); build_model(g, L, &LMV_tower_bridge, s, 2); break; }
  case LM_MOAI: build_moai(g, L); break;
  case LM_CHRIST_REDEEMER: build_christ(g, L); break;
  case LM_COLOSSEUM: { static const float s[][2] = { { -5.6f, 3.2f }, { 5.6f, -3.2f } }; build_vox(g, &LMV_col_cavea, 0, 0, 0); build_model(g, L, &LMV_colosseum, s, 2); break; }
  case LM_BIG_BEN: { static const float s[][2] = { { 2.4f, 0 }, { -2.4f, 0 } }; build_model(g, L, &LMV_big_ben, s, 2); break; }
  case LM_PISA: { static const float s[][2] = { { -2.8f, 0 } }; build_model(g, L, &LMV_pisa, s, 1); break; }
  case LM_PARTHENON: { static const float s[][2] = { { -3.5f, 0 }, { 3.5f, 0 } }; build_vox(g, &LMV_pa_base, 0, 0, 0); build_model(g, L, &LMV_parthenon, s, 2); break; }
  case LM_PAGODA: { static const float s[][2] = { { 0, 2.3f } }; build_model(g, L, &LMV_pagoda, s, 1); break; }
  case LM_CHICHEN_ITZA: { static const float s[][2] = { { 0.9f, 0.9f }, { -0.9f, -0.9f } }; /* two spots, not one twice (two crystals in one place lay into each other) */ build_model(g, L, &LMV_chichen_itza, s, 2); break; }
  case LM_LIBERTY: build_liberty(g, L); break;
  case LM_RUSHMORE: { static const float s[][2] = { { -5.7f, -1.0f }, { 0.7f, -1.5f }, { 6.0f, -1.5f } }; build_model(g, L, &LMV_m_rushmore, s, 3); break; }
  case LM_KREMLIN: { static const float s[][2] = { { 6.0f, 1.6f }, { -6.0f, -1.8f } }; build_model(g, L, &LMV_kremlin, s, 2); break; }
  case LM_TAIPEI: { static const float s[][2] = { { 1.9f, 0 }, { -1.9f, 0 } }; build_model(g, L, &LMV_m_taipei, s, 2); break; }
  case LM_GOLDEN_GATE: { static const float s[][2] = { { 4.6f, 0 }, { -4.6f, 0 } }; build_model(g, L, &LMV_golden_gate, s, 2); break; }
  case LM_BROOKLYN: { static const float s[][2] = { { 3.0f, 0 }, { -3.0f, 0 }, { 12.5f, 0 } }; build_model(g, L, &LMV_brooklyn, s, 3); break; }
  case LM_CHRYSLER: { static const float s[][2] = { { 2.2f, 0 }, { -2.2f, 0 } }; build_model(g, L, &LMV_m_chrysler, s, 2); break; }
  case LM_BERLIN_TV: { static const float s[][2] = { { 1.7f, 0 }, { -1.7f, 0 } }; build_model(g, L, &LMV_m_berlin_tv, s, 2); break; }
  case LM_BURJ_ARAB: { static const float s[][2] = { { -0.35f, -1.2f }, { 0.9f, 2.0f } }; build_model(g, L, &LMV_m_burj_arab, s, 2); break; }
  case LM_GATEWAY: { static const float s[][2] = { { 6.5f, 0 }, { -6.5f, 0 } }; build_model(g, L, &LMV_gateway, s, 2); break; }
  case LM_ARC: { static const float s[][2] = { { -2.1f, -0.7f }, { 4.1f, 1.7f } }; build_model(g, L, &LMV_m_arc, s, 2); break; }
  case LM_BRANDENBURG: { static const float s[][2] = { { 4.2f, 0 }, { -4.2f, 0 } }; build_model(g, L, &LMV_brandenburg, s, 2); break; }
  case LM_PANTHEON: { static const float s[][2] = { { -2.6f, 1.4f }, { 2.6f, 2.5f } }; build_model(g, L, &LMV_m_pantheon, s, 2); break; }
  case LM_NOTRE_DAME: { static const float s[][2] = { { -0.2f, 6.7f }, { 4.5f, -3.0f } }; build_model(g, L, &LMV_m_notre_dame, s, 2); break; }
  case LM_TOKYO_TOWER: { static const float s[][2] = { { 0.8f, 0 }, { -0.8f, 0 } }; build_model(g, L, &LMV_tokyo_tower, s, 2); break; }
  case LM_ATOMIUM: { static const float s[][2] = { { 2.2f, 0 }, { -2.5f, 2.2f } }; build_model(g, L, &LMV_atomium, s, 2); break; }
  case LM_GREAT_WALL: { static const float s[][2] = { { -3.7f, -1.2f }, { 2.4f, 5.0f } }; build_model(g, L, &LMV_m_great_wall, s, 2); break; }
  case LM_HAGIA_SOPHIA: { static const float s[][2] = { { -1.0f, -3.2f }, { 3.2f, 1.0f } }; build_model(g, L, &LMV_m_hagia, s, 2); break; }
  case LM_BLUE_MOSQUE: { static const float s[][2] = { { -0.1f, 3.5f }, { 3.7f, -4.0f } }; build_model(g, L, &LMV_m_blue_mosque, s, 2); break; }
  case LM_MONT_ST_MICHEL: { static const float s[][2] = { { -3.0f, -0.3f }, { -3.0f, 0.8f } }; build_model(g, L, &LMV_m_msm, s, 2); break; }
  case LM_JAPANESE_CASTLE: { static const float s[][2] = { { 6.5f, 0 }, { -6.5f, 0 } }; build_model(g, L, &LMV_m_himeji, s, 2); break; }
  case LM_MACHU_PICCHU: { static const float s[][2] = { { 6.0f, 2.0f }, { -6.0f, 0 } }; build_model(g, L, &LMV_m_machu, s, 2); break; }
  case LM_PETRA: { static const float s[][2] = { { 2.6f, 2.4f }, { -2.6f, 2.4f } }; build_model(g, L, &LMV_petra, s, 2); break; }
  default: break;
  }
  g->paint_now = P_NONE;
}
