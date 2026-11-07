/* skate nav: where a bot can skate on its own wheels, for the bots' route planner (bot.c).
 *
 *   the grid   half-metre cells over the park. Each cell's floor is the highest ride surface straight down through its
 *              middle; a cell is skateable when that floor is flat or a slope a board can climb (a bank, not a
 *              ramp's transition).
 *   walls      everything that is not a skateable floor (walls, ramps' faces, rails, posts, a ledge's side) is
 *              sampled every 20 cm over its surface into the cells it passes through, as bands of 10 cm: a cell is
 *              blocked for a rider on a floor at h when something is in it between h + 4 cm and h + 1.7 m.
 *   moves      to the eight cells around: level (or along a slope), or a drop off an edge onto a floor up to 4.2 m
 *              below (one way: a rider rolls off a roof's edge, never up one). Up a slope only straight up its
 *              fall line, with a metre clear either side (a bank is climbed square, at a run).
 *   routes     Dijkstra from the rider's cell over those moves, a metre's cost a metre, plus a little for a cell
 *              beside a wall (the routes keep off the edges), a climb, a drop.
 *
 * Pure C on the park's triangles and cartwheel/math.h, like the rest of the simulation: every host plans the same route.
 * The grid of a park is built the first time a bot asks for it (two parks are kept: the check alternates). */
#include "game.h"

#define NAV_CS 0.5f
#define NAV_DROP 4.2f
#define NAV_BAND 0.1f
#define NAV_WORDS 10 /* 320 bands of 10 cm: 32 m of height */
#define NAV_INF 1e30f

bool g_nav_ignore_walls; /* the route check's control: a planner blind to the park (every move allowed) */

enum { NC_PROBED = 1, NC_WALK = 2, NC_FLAT = 4 };
static const int DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, DZ[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

typedef struct nav {
  const ptri *key_t;
  int key_nt, key_ne;
  bool key_blind;
  float ox, oz, y0;
  int gx, gz;
  float *h, *ny, *nx, *nz;
  uint8_t *fl;
  uint32_t *band; /* NAV_WORDS per cell */
  uint8_t *eseen, *eok, *drop;
  float *dist;
  int *from, *heap, nheap, cap_heap;
} nav;

static nav NV[2];
static int nv_next;

static void nav_free(nav *n) {
  free(n->h), free(n->ny), free(n->nx), free(n->nz), free(n->fl), free(n->band), free(n->eseen), free(n->eok), free(n->drop), free(n->dist), free(n->from), free(n->heap);
  memset(n, 0, sizeof *n);
}

static void mark(nav *n, v3 p) {
  int x = (int)floorf((p.x - n->ox) / NAV_CS), z = (int)floorf((p.z - n->oz) / NAV_CS);
  if (x < 0 || z < 0 || x >= n->gx || z >= n->gz) return;
  int b = (int)floorf((p.y - n->y0) / NAV_BAND);
  if (b < 0 || b >= NAV_WORDS * 32) return;
  n->band[(z * n->gx + x) * NAV_WORDS + (b >> 5)] |= 1u << (b & 31);
}

static void nav_build(nav *n, const park *k) {
  nav_free(n);
  n->key_t = k->t, n->key_nt = k->nt, n->key_ne = k->ne, n->key_blind = g_nav_ignore_walls;
  n->ox = k->lo.x - 1, n->oz = k->lo.z - 1, n->y0 = k->lo.y - 1;
  n->gx = (int)((k->hi.x - n->ox) / NAV_CS) + 2, n->gz = (int)((k->hi.z - n->oz) / NAV_CS) + 2;
  size_t nc = (size_t)n->gx * (size_t)n->gz;
  n->h = calloc(nc, sizeof *n->h), n->ny = calloc(nc, sizeof *n->ny), n->nx = calloc(nc, sizeof *n->nx), n->nz = calloc(nc, sizeof *n->nz), n->fl = calloc(nc, 1), n->band = calloc(nc * NAV_WORDS, sizeof *n->band);
  n->eseen = calloc(nc, 1), n->eok = calloc(nc, 1), n->drop = calloc(nc, 1);
  n->dist = calloc(nc, sizeof *n->dist), n->from = calloc(nc, sizeof *n->from);
  n->cap_heap = (int)nc * 2;
  n->heap = calloc((size_t)n->cap_heap, sizeof *n->heap);
  /* what is in the way: every triangle that is not a skateable floor, sampled over its surface */
  for (int i = 0; i < k->nt; i++) {
    const ptri *t = &k->t[i];
    if (t->surf == SURF_RIDE && t->n.y >= 0.85f) continue;
    if (t->surf == SURF_NONE && t->mat == MAT_PAINT) continue; /* paint on the ground */
    float l = fmaxf(vlen(vsub(t->b, t->a)), fmaxf(vlen(vsub(t->c, t->a)), vlen(vsub(t->c, t->b))));
    int st = (int)(l / 0.2f) + 1;
    if (st > 400) st = 400;
    for (int a = 0; a <= st; a++)
      for (int b = 0; b <= st - a; b++) mark(n, vadd(t->a, vadd(vmul(vsub(t->b, t->a), (float)a / st), vmul(vsub(t->c, t->a), (float)b / st))));
  }
  /* rails (their tubes are only drawn): the bar along its run */
  for (int i = 0; i < k->ne; i++) {
    const pedge *e = &k->e[i];
    if (e->kind != EDGE_RAIL) continue;
    int st = (int)(e->len / 0.2f) + 1;
    for (int a = 0; a <= st; a++) mark(n, vmad(e->a, e->t, e->len * (float)a / st));
  }
}

static nav *nav_get(const park *k) {
  for (int i = 0; i < 2; i++)
    if (NV[i].key_t == k->t && NV[i].key_nt == k->nt && NV[i].key_ne == k->ne && NV[i].key_blind == g_nav_ignore_walls && NV[i].h) return &NV[i];
  nav *n = &NV[nv_next];
  nv_next ^= 1;
  nav_build(n, k);
  return n;
}

static v3 cell_mid(const nav *n, int c) { return V(n->ox + ((float)(c % n->gx) + 0.5f) * NAV_CS, n->h[c], n->oz + ((float)(c / n->gx) + 0.5f) * NAV_CS); }

static void probe(nav *n, const park *k, int c) {
  if (n->fl[c] & NC_PROBED) return;
  n->fl[c] |= NC_PROBED;
  v3 p = cell_mid(n, c);
  phit h;
  if (!park_ground(k, V(p.x, k->hi.y + 1, p.z), 0, k->hi.y - k->lo.y + 3, &h)) return;
  n->h[c] = h.p.y, n->ny[c] = h.n.y, n->nx[c] = h.n.x, n->nz[c] = h.n.z;
  if (h.n.y >= 0.85f) n->fl[c] |= NC_WALK;
  if (h.n.y >= 0.985f) n->fl[c] |= NC_FLAT;
}

/* something in cell c between y_lo and y_hi */
static bool blocked(const nav *n, int c, float y_lo, float y_hi) {
  int b0 = (int)ceilf((y_lo - n->y0) / NAV_BAND), b1 = (int)floorf((y_hi - n->y0) / NAV_BAND);
  if (b0 < 0) b0 = 0;
  if (b1 >= NAV_WORDS * 32) b1 = NAV_WORDS * 32 - 1;
  const uint32_t *w = &n->band[c * NAV_WORDS];
  for (int b = b0; b <= b1; b++)
    if (w[b >> 5] & (1u << (b & 31))) return true;
  return false;
}

static int neighbour(const nav *n, int c, int d) {
  int x = c % n->gx + DX[d], z = c / n->gx + DZ[d];
  return x < 0 || z < 0 || x >= n->gx || z >= n->gz ? -1 : z * n->gx + x;
}

/* the floor from c on to its neighbour d: 0 none (a step, a wall), 1 a roll, 2 a drop */
static int roll(nav *n, const park *k, int c, int d) {
  int m = neighbour(n, c, d);
  if (m < 0) return 0;
  probe(n, k, c), probe(n, k, m);
  if (!(n->fl[c] & NC_WALK) || !(n->fl[m] & NC_WALK)) return 0;
  float dist = (d & 1) ? NAV_CS * 1.4142f : NAV_CS, dh = n->h[m] - n->h[c];
  float ny = fminf(n->ny[c], n->ny[m]), tn = sqrtf(fmaxf(0, 1 - ny * ny)) / ny;
  bool drop = false;
  if (fabsf(dh) > 0.06f + dist * tn * 1.15f) {
    if (dh < 0 && -dh <= NAV_DROP && (n->fl[c] & NC_FLAT)) drop = true;
    else return 0;
  }
  float base = drop ? n->h[c] : fmaxf(n->h[c], n->h[m]);
  if (!g_nav_ignore_walls && (blocked(n, c, base + 0.04f, base + 1.7f) || blocked(n, m, base + 0.04f, base + 1.7f))) return 0;
  /* a diagonal squeezes past its two corners */
  if ((d & 1) && !g_nav_ignore_walls) {
    int a = neighbour(n, c, (d + 7) & 7), b = neighbour(n, c, (d + 1) & 7);
    if (a < 0 || b < 0 || blocked(n, a, base + 0.04f, base + 1.7f) || blocked(n, b, base + 0.04f, base + 1.7f)) return 0;
  }
  return drop ? 2 : 1;
}

/* the move from c to its neighbour d (cached): a roll or a drop, and up a slope only straight up its fall line, away
   from its sides (a bank is climbed square, with room either side, the way a run at it goes) */
static int edge(nav *n, const park *k, int c, int d) {
  if (n->eseen[c] & (1 << d)) return (n->eok[c] >> d & 1) ? (n->drop[c] >> d & 1) + 1 : 0;
  n->eseen[c] |= (uint8_t)(1 << d);
  int r = roll(n, k, c, d), m = neighbour(n, c, d);
  if (g_nav_ignore_walls && m >= 0) r = 1; /* (the control: a straight line through whatever is there) */
  if (!r) return 0;
  if (r == 1 && !(n->fl[m] & NC_FLAT) && n->h[m] > n->h[c] + 0.03f) {
    float ux = -n->nx[m], uz = -n->nz[m], ul = sqrtf(ux * ux + uz * uz), ml = (d & 1) ? 1.4142f : 1.0f;
    if (ul < 1e-4f || (ux * (float)DX[d] + uz * (float)DZ[d]) / (ul * ml) < 0.8f) return 0;
    for (int q = 0; q < 8; q += 2) {
      int m2 = neighbour(n, m, q);
      if (m2 < 0 || roll(n, k, m, q) != 1 || roll(n, k, m2, q) != 1) return 0;
    }
  }
  n->eok[c] |= (uint8_t)(1 << d);
  if (r == 2) n->drop[c] |= (uint8_t)(1 << d);
  return r;
}

/* how close c is to something: a cost to keep the routes off the edges */
static float edge_cost(nav *n, const park *k, int c) {
  float closed = 0;
  for (int d = 0; d < 8; d++) closed += roll(n, k, c, d) != 1 ? ((d & 1) ? 0.25f : 0.5f) : 0;
  return closed;
}

/* ── the heap (on dist) ───────────────────────────────────────────────── */
static void hpush(nav *n, int c) {
  if (n->nheap >= n->cap_heap) {
    n->cap_heap *= 2;
    n->heap = realloc(n->heap, (size_t)n->cap_heap * sizeof *n->heap);
  }
  int i = n->nheap++;
  while (i > 0) {
    int p = (i - 1) / 2;
    if (n->dist[n->heap[p]] <= n->dist[c]) break;
    n->heap[i] = n->heap[p], i = p;
  }
  n->heap[i] = c;
}
static int hpop(nav *n) {
  int top = n->heap[0], last = n->heap[--n->nheap], i = 0;
  for (;;) {
    int l = i * 2 + 1, r = l + 1, m = i;
    float dm = n->dist[last];
    if (l < n->nheap && n->dist[n->heap[l]] < dm) m = l, dm = n->dist[n->heap[l]];
    if (r < n->nheap && n->dist[n->heap[r]] < dm) m = r;
    if (m == i) break;
    n->heap[i] = n->heap[m], i = m;
  }
  if (n->nheap) n->heap[i] = last;
  return top;
}

/* ── the bots' interface ──────────────────────────────────────────────── */

/* the skateable cell a rider at p stands in (the nearest one at its height within a metre), -1 none */
int nav_cell(const park *k, v3 p) {
  nav *n = nav_get(k);
  int x0 = (int)floorf((p.x - n->ox) / NAV_CS), z0 = (int)floorf((p.z - n->oz) / NAV_CS), best = -1;
  float bd = 1e9f;
  for (int dz = -2; dz <= 2; dz++)
    for (int dx = -2; dx <= 2; dx++) {
      int x = x0 + dx, z = z0 + dz;
      if (x < 0 || z < 0 || x >= n->gx || z >= n->gz) continue;
      int c = z * n->gx + x;
      probe(n, k, c);
      if (!(n->fl[c] & NC_WALK) || fabsf(n->h[c] - p.y) > 0.35f) continue;
      /* a cell with a way out (not one walled in on every side) */
      bool out = false;
      for (int d = 0; d < 8 && !out; d++) out = edge(n, k, c, d) != 0;
      if (!out) continue;
      v3 m = cell_mid(n, c);
      float dd = (m.x - p.x) * (m.x - p.x) + (m.z - p.z) * (m.z - p.z);
      if (dd < bd) bd = dd, best = c;
    }
  return best;
}

/* every cell's cost from cell src (nav_dist reads it); one flood serves until the next */
void nav_flood(const park *k, int src) {
  nav *n = nav_get(k);
  int nc = n->gx * n->gz;
  for (int i = 0; i < nc; i++) n->dist[i] = NAV_INF, n->from[i] = -1;
  n->nheap = 0;
  if (src < 0) return;
  n->dist[src] = 0;
  hpush(n, src);
  while (n->nheap) {
    int c = hpop(n);
    float dc = n->dist[c];
    for (int d = 0; d < 8; d++) {
      int e = edge(n, k, c, d);
      if (!e) continue;
      int m = neighbour(n, c, d);
      float len = (d & 1) ? NAV_CS * 1.4142f : NAV_CS;
      float cost = len * (1 + edge_cost(n, k, m)) + (e == 2 ? 3.0f : 0) + fmaxf(0, n->h[m] - n->h[c]) * 4.0f;
      if (dc + cost < n->dist[m]) {
        n->dist[m] = dc + cost, n->from[m] = c;
        hpush(n, m);
      }
    }
  }
}

float nav_dist(const park *k, int c) { return c < 0 ? NAV_INF : nav_get(k)->dist[c]; }

/* the last flood's route to cell dst: its cells from the source's side, at most cap; the count */
int nav_route(const park *k, int dst, int *out, int cap) {
  nav *n = nav_get(k);
  if (dst < 0 || n->dist[dst] >= NAV_INF) return 0;
  int len = 0;
  for (int c = dst; c >= 0; c = n->from[c]) len++;
  int skip = len > cap ? len - cap : 0, i = len - skip;
  for (int c = dst; c >= 0; c = n->from[c]) {
    if (skip > 0) { skip--; continue; }
    out[--i] = c;
  }
  return len > cap ? cap : len;
}

v3 nav_point(const park *k, int c) { return cell_mid(nav_get(k), c); }
bool nav_is_drop(const park *k, int a, int b) {
  nav *n = nav_get(k);
  for (int d = 0; d < 8; d++)
    if (neighbour(n, a, d) == b) return edge(n, k, a, d) == 2;
  return false;
}
