/* explorer_check: proves the world (native only).
 *
 *   explorer_check <world.bin> [--route <out>] [--break <n>] [--quiet]
 *
 * From the start, with no abilities, it walks every place the real movement code can reach (reach.h: every node
 * a player can stand on, every move from it, played through player_step from two spots on the tile that must both
 * land on the same tile). When the walk takes an ability pickup the walk goes on with it: the stages are the order
 * the abilities come in. Wardens are fought where their arena is reached (their sealed doors open, a beacon is
 * lit), hidden walls break where you can strike them, cracked floors break where a plunge lands, the heart's seal
 * opens at five beacons. Then the checks:
 *   order      the abilities come one stage at a time in the order the world was made for
 *   pickups    every pickup is reached
 *   softlock   at the end of every stage, every place reached can get back to the start
 *   gates      every gate room: its far half cannot be reached without the gate's ability (a control on the
 *              obstacle), and can with it
 *   grounded   every prop, lamp, pickup, shrine, anchor and ground enemy is held by the tile it says
 * Exit 0 when everything holds, 1 when a check fails.
 *
 * --break n breaks the world on purpose first (the controls, which must fail): 1 takes the anchors out of the
 * grapple gates, 2 swaps the second jump and the dash prizes, 3 digs a pit with no way out next to the start,
 * 4 lifts a lamp off its ceiling, 5 lowers a wall gate to one jump's height.
 * --route writes the bot's route: the moves from the start through every pickup and warden (route.bin). */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/reach.h"

static world W;
static uint8_t T[TH][TW];
static int32_t *node_at; /* tile index -> node, -1 */
static int32_t *node_xy; /* node -> tile index */
static int nnodes;
static int quiet;
static const char *only; /* --only gates|grounded: that check alone (the controls that need no walk) */

static bool standable(int x, int y) {
  int t = T[y][x];
  if (!(t_solid(t) || t == T_ONEWAY) || t == T_SECRET) return false; /* (a hidden floor breaks under you: nobody stands on it) */
  for (int k = 1; k <= 2; k++) {
    int a = w_tile(&W, x, y - k);
    if (t_solid(a) || a == T_SPIKE) return false;
  }
  if (w_tile(&W, x, y - 2) == T_WATER && T[y - 2][x] == T_WATER) return false;
  return true;
}

static void build_nodes(void) {
  nnodes = 0;
  for (int y = 3; y < TH; y++)
    for (int x = 1; x < TW - 1; x++) {
      int i = y * TW + x;
      node_at[i] = -1;
      if (standable(x, y)) node_at[i] = nnodes, node_xy[nnodes++] = i;
    }
}

/* ── edges ── */
#define MAXE 48
typedef struct edges { int32_t to[MAXE]; uint8_t mv[MAXE]; uint16_t ticks[MAXE]; int n; int32_t broke[4]; int nbroke; } edges;
static edges *E;          /* by tile index of the node (sparse: only expanded nodes) */
static int32_t *eslot;    /* tile index -> slot in E */
static int nslots;

static uint32_t AB;       /* abilities of the current stage */

static int landing_node(const player *p) {
  int x = (int)floorf(p->x), y = (int)roundf(p->y);
  if (!w_in(x, y)) return -1;
  if (node_at[y * TW + x] >= 0) return y * TW + x;
  /* straddling: the column under the side still on ground */
  for (int dx = -1; dx <= 1; dx += 2) {
    int xx = (int)floorf(p->x + (float)dx * (PW - 0.01f));
    if (w_in(xx, y) && node_at[y * TW + xx] >= 0) return y * TW + xx;
  }
  return -1;
}

static void expand(int tile, edges *out) {
  out->n = 0, out->nbroke = 0;
  int x = tile % TW, y = tile / TW;
  /* walking to the neighbours on the same surface */
  for (int dx = -1; dx <= 1; dx += 2)
    if (node_at[y * TW + x + dx] >= 0 && out->n < MAXE) {
      int t2 = T[y][x + dx];
      (void)t2;
      out->to[out->n] = y * TW + x + dx, out->mv[out->n] = 255, out->ticks[out->n] = 3, out->n++;
    }
  for (int m = 0; m < NMOVES; m++) {
    if (MOVES[m].need & ~AB) continue;
    player a, b;
    int ba, bb;
    int ta = move_play(&MOVES[m], &W, (const uint8_t(*)[TW])T, AB, (float)x + 0.3f, (float)y, &a, &ba);
    if (ta < 0) continue;
    int la = landing_node(&a);
    if (ba >= 0 && out->nbroke < 4) out->broke[out->nbroke++] = ba;
    if (la < 0 || la == tile) continue;
    int tb = move_play(&MOVES[m], &W, (const uint8_t(*)[TW])T, AB, (float)x + 0.7f, (float)y, &b, &bb);
    if (tb < 0 || landing_node(&b) != la) continue;
    int tc = move_play(&MOVES[m], &W, (const uint8_t(*)[TW])T, AB, (float)x + 0.5f, (float)y, &b, &bb); /* and the middle, where the bot stands */
    if (tc < 0 || landing_node(&b) != la) continue;
    bool dup = false;
    for (int k = 0; k < out->n; k++)
      if (out->to[k] == la) {
        dup = true;
        if (out->ticks[k] > ta) out->ticks[k] = (uint16_t)ta, out->mv[k] = (uint8_t)m;
      }
    if (!dup && out->n < MAXE) out->to[out->n] = la, out->mv[out->n] = (uint8_t)m, out->ticks[out->n] = (uint16_t)ta, out->n++;
  }
}

/* ── the frontier, expanded on every core ── */
static int32_t *work;
static int nwork;
static atomic_int next_work;
static edges *results;
static void *worker(void *u) {
  (void)u;
  for (;;) {
    int i = atomic_fetch_add(&next_work, 1);
    if (i >= nwork) return NULL;
    expand(work[i], &results[i]);
  }
}
static int nthreads = 8;
static void expand_all(void) {
  atomic_store(&next_work, 0);
  pthread_t th[64];
  for (int i = 0; i < nthreads; i++) pthread_create(&th[i], NULL, worker, NULL);
  for (int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
}

/* ── world state as the walk goes ── */
static uint8_t *vis;       /* tile index -> visited */
static int32_t *queue;
static int qn;
static uint32_t pending;   /* abilities picked up, taken at the stage's end */
static uint8_t got_pk[MAXENTS], boss_done[NBOSSES + 1];
static int beacons;
static int pk_stage[MAXENTS], pk_tile[MAXENTS];
static int changed_x[64], changed_y[64], nchanged;

static void flood_break(int x, int y, int kind) {
  int stackn = 0;
  static int32_t st[TW * 8];
  st[stackn++] = y * TW + x;
  while (stackn) {
    int i = st[--stackn], cx = i % TW, cy = i / TW;
    if (!w_in(cx, cy) || T[cy][cx] != kind) continue;
    T[cy][cx] = T_EMPTY;
    if (stackn < TW * 8 - 4) st[stackn++] = i + 1, st[stackn++] = i - 1, st[stackn++] = i + TW, st[stackn++] = i - TW;
  }
  if (nchanged < 64) changed_x[nchanged] = x, changed_y[nchanged] = y, nchanged++;
}

static void open_room_kind(int room, int kind) {
  const struct room *r = &W.rooms[room];
  for (int y = r->y * CELL_H - 2; y < (r->y + r->h) * CELL_H + 2; y++)
    for (int x = r->x * CELL_W - 2; x < (r->x + r->w) * CELL_W + 2; x++)
      if (w_in(x, y) && T[y][x] == kind) flood_break(x, y, kind);
}

static int stage;
static void visit_effects(int tile) {
  int x = tile % TW, y = tile / TW;
  int room = w_room_at(&W, (float)x + 0.5f, (float)y - 0.5f);
  /* pickups on this spot */
  for (int i = 0; i < W.nents; i++) {
    const ent *e = &W.ents[i];
    if (e->kind == EK_PICKUP && !got_pk[e->id] && (int)floorf(e->x) - x >= -1 && (int)floorf(e->x) - x <= 0 && (int)roundf(e->y) == y) {
      got_pk[e->id] = 1, pk_stage[e->id] = stage, pk_tile[e->id] = tile;
      if (e->sub == PK_ABILITY) pending |= e->arg;
    }
  }
  /* a warden's arena: the fight is won (the bot fights it), its doors open */
  if (room >= 0 && W.rooms[room].kind == RK_BOSS && !boss_done[W.rooms[room].boss]) {
    boss_done[W.rooms[room].boss] = 1;
    if (W.rooms[room].boss <= 5) beacons++;
    open_room_kind(room, T_BOSSDOOR);
    if (W.rooms[room].x + W.rooms[room].w < GW) { /* the prize room's door is the arena's far wall */
      int nb = W.cell_room[W.rooms[room].y][W.rooms[room].x + W.rooms[room].w];
      if (nb != 0xffff) open_room_kind(nb, T_BOSSDOOR);
    }
    if (beacons >= 5)
      for (int yy = 0; yy < TH; yy++)
        for (int xx = 0; xx < TW; xx++)
          if (T[yy][xx] == T_SEAL) flood_break(xx, yy, T_SEAL);
  }
  /* a hidden wall within a strike */
  for (int dy = 0; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++)
      if (w_in(x + dx, y - dy) && T[y - dy][x + dx] == T_SECRET) flood_break(x + dx, y - dy, T_SECRET);
}

static int32_t *par; /* tile index -> the tile it was first reached from */
static int cur_from = -1;
static void push(int tile) {
  if (vis[tile]) return;
  vis[tile] = 1;
  par[tile] = cur_from;
  queue[qn++] = tile;
  visit_effects(tile);
}
/* the rooms on the way the walk first came to a tile */
static void print_path(int tile) {
  int last = -2, n = 0, rooms[400];
  for (int t = tile; t >= 0 && n < 400; t = par[t]) {
    int r = w_room_at(&W, (float)(t % TW) + 0.5f, (float)(t / TW) - 0.5f);
    if (r != last) rooms[n++] = r, last = r;
  }
  printf("     the way there:");
  for (int i = n - 1; i >= 0; i--) printf(" %d%s", rooms[i], rooms[i] >= 0 && W.rooms[rooms[i]].kind == RK_GATE ? "(gate)" : "");
  printf("\n");
}

/* walk until nothing new: returns nodes visited */
static int walk(void) {
  int head = 0;
  while (head < qn) {
    nwork = qn - head;
    work = queue + head;
    results = realloc(results, sizeof(edges) * (size_t)nwork);
    expand_all();
    int end = qn;
    for (int i = 0; i < nwork; i++) {
      int tile = work[i];
      if (eslot[tile] < 0) eslot[tile] = nslots++;
      E[eslot[tile]] = results[i];
    }
    for (int i = 0; i < nwork; i++) {
      edges *r = &results[i];
      for (int k = 0; k < r->nbroke; k++) {
        int bx = r->broke[k] % TW, by = r->broke[k] / TW;
        if (T[by][bx] == T_CRACKED) flood_break(bx, by, T_CRACKED);
      }
      cur_from = work[i];
      for (int k = 0; k < r->n; k++) push(r->to[k]);
      cur_from = -1;
    }
    head = end;
    if (nchanged) { /* the tiles changed: the nodes round them again */
      build_nodes();
      int n0 = nchanged;
      nchanged = 0;
      for (int i = 0; i < qn; i++) {
        int x = queue[i] % TW, y = queue[i] / TW;
        bool near = false;
        for (int c = 0; c < n0 && !near; c++) near = abs(x - changed_x[c]) < 70 && abs(y - changed_y[c]) < 40;
        if (near && i < head) queue[qn++] = queue[i]; /* expand again (it is already visited) */
      }
      /* (nodes on tiles that broke are gone: their edges stay in E but nothing reaches them) */
    }
  }
  return qn;
}

/* every visited node reaches the start (reverse walk over the stored edges) */
static int softlocks(int start, int *example) {
  static int32_t *rev_head, *rev_next, *rev_from;
  static int cap;
  int ne = 0;
  for (int i = 0; i < TW * TH; i++)
    if (vis[i] && eslot[i] >= 0) ne += E[eslot[i]].n;
  if (ne > cap) {
    cap = ne;
    rev_next = realloc(rev_next, sizeof(int32_t) * (size_t)cap);
    rev_from = realloc(rev_from, sizeof(int32_t) * (size_t)cap);
  }
  if (!rev_head) rev_head = malloc(sizeof(int32_t) * TW * TH);
  for (int i = 0; i < TW * TH; i++) rev_head[i] = -1;
  int k = 0;
  for (int i = 0; i < TW * TH; i++) {
    if (!vis[i] || eslot[i] < 0) continue;
    edges *e = &E[eslot[i]];
    for (int j = 0; j < e->n; j++) {
      int to = e->to[j];
      rev_from[k] = i, rev_next[k] = rev_head[to], rev_head[to] = k, k++;
    }
  }
  static uint8_t *back;
  if (!back) back = malloc(TW * TH);
  memset(back, 0, TW * TH);
  static int32_t *q;
  if (!q) q = malloc(sizeof(int32_t) * TW * TH);
  int qh = 0, qt = 0;
  q[qt++] = start, back[start] = 1;
  while (qh < qt) {
    int n = q[qh++];
    for (int j = rev_head[n]; j >= 0; j = rev_next[j]) {
      int f = rev_from[j];
      if (!back[f]) back[f] = 1, q[qt++] = f;
    }
  }
  int bad = 0;
  *example = -1;
  for (int i = 0; i < TW * TH; i++)
    if (vis[i] && node_at[i] >= 0 && !back[i]) {
      bad++;
      if (*example < 0) *example = i;
    }
  return bad;
}

/* ── the gates' own check: inside the gate room only ── */
/* which half of a gate room a node is in: 0 or 1, or -1 near the obstacle (the obstacle's own top belongs to
 * neither side) */
static int gate_half(const struct room *r, int x, int y) {
  int X0 = r->x * CELL_W, Y0 = r->y * CELL_H;
  if (r->w == 2) {
    int mid = X0 + CELL_W;
    if (x < mid - 14) return 0;
    if (x >= mid + 14) return 1;
    return -1;
  }
  return y < Y0 + CELL_H ? 0 : 1;
}

static bool local_reach(int room, uint32_t ab, int from, bool *reached_other_half) {
  const struct room *r = &W.rooms[room];
  int X0 = r->x * CELL_W, Y0 = r->y * CELL_H, X1 = X0 + r->w * CELL_W, Y1 = Y0 + r->h * CELL_H;
  static uint8_t *lv;
  if (!lv) lv = malloc(TW * TH);
  static int32_t lq[CELL_W * CELL_H * 4];
  int qh = 0, qt = 0;
  static uint8_t keep[TH][TW];
  memcpy(keep, T, sizeof T); /* the room's cracked floors break as the walk plunges on them: put them back after */
  for (int y = Y0; y < Y1; y++)
    for (int x = X0; x < X1; x++) lv[y * TW + x] = 0;
  for (int y = Y0; y < Y1; y++)
    for (int x = X0; x < X1; x++)
      if (node_at[y * TW + x] >= 0 && gate_half(r, x, y) == from && qt < (int)(sizeof lq / sizeof lq[0])) lv[y * TW + x] = 1, lq[qt++] = y * TW + x;
  uint32_t save = AB;
  AB = ab;
  *reached_other_half = false;
  while (qh < qt && !*reached_other_half) {
    int n = lq[qh++];
    edges e;
    expand(n, &e);
    for (int k = 0; k < e.nbroke; k++) {
      int bx = e.broke[k] % TW, by = e.broke[k] / TW;
      if (T[by][bx] == T_CRACKED) {
        flood_break(bx, by, T_CRACKED);
        nchanged = 0;
        build_nodes();
        qh = 0; /* everything again over the opened floor */
      }
    }
    for (int k = 0; k < e.n; k++) {
      int to = e.to[k], tx = to % TW, ty = to / TW;
      if (tx < X0 || tx >= X1 || ty < Y0 || ty >= Y1 || lv[to]) continue;
      lv[to] = 1;
      int h = gate_half(r, tx, ty);
      if (h >= 0 && h != from) *reached_other_half = true;
      if (qt < (int)(sizeof lq / sizeof lq[0])) lq[qt++] = to;
    }
  }
  AB = save;
  if (memcmp(keep, T, sizeof T)) memcpy(T, keep, sizeof T), build_nodes();
  return true;
}

/* ── the controls: break the world on purpose ── */
static void break_world(int n) {
  if (n == 1) {
    for (int i = 0; i < W.nents; i++)
      if (W.ents[i].kind == EK_ANCHOR) W.ents[i].kind = EK_PROP, W.ents[i].sub = 6;
  } else if (n == 2) {
    for (int i = 0; i < W.nents; i++)
      if (W.ents[i].kind == EK_PICKUP && W.ents[i].sub == PK_ABILITY) {
        if (W.ents[i].arg == AB_DJUMP) W.ents[i].arg = AB_DASH;
        else if (W.ents[i].arg == AB_DASH) W.ents[i].arg = AB_DJUMP;
      }
  } else if (n == 3) {
    /* a pit by the start whose walls are too high to jump out of: dug through the floor into rock below */
    int sx = (int)W.start_x + 6, sy = (int)W.start_y;
    for (int y = sy; y < sy + 10 && y < TH - 1; y++)
      for (int x = sx; x < sx + 3; x++) W.t[y][x] = T_EMPTY;
    for (int i = 0; i < W.nents; i++)
      if (W.ents[i].x >= sx - 1 && W.ents[i].x <= sx + 4 && W.ents[i].y >= sy - 2 && W.ents[i].y <= sy + 11) W.ents[i].kind = EK_NONE;
    if (W.cell_room[(sy + 10) / CELL_H][sx / CELL_W] != 0xffff) printf("(the pit reaches another room)\n");
  } else if (n == 4) {
    for (int i = 0; i < W.nents; i++)
      if (W.ents[i].kind == EK_LAMP && W.ents[i].hold == HOLD_CEIL) { W.ents[i].y += 3; break; }
  } else if (n == 5) {
    for (int i = 0; i < W.nrooms; i++) {
      const struct room *r = &W.rooms[i];
      if (r->kind != RK_GATE || r->gate != 1 /* G_DJUMP */) continue;
      int mid = r->x * CELL_W + CELL_W, fy = r->y * CELL_H + CELL_H - 2;
      for (int y = fy - 6; y < fy - 2; y++)
        for (int x = mid - 4; x <= mid + 3; x++) W.t[y][x] = T_EMPTY;
    }
  }
}

static bool grounded(const ent *e) {
  int x = (int)floorf(e->x), y = (int)roundf(e->y);
  if (e->kind == EK_ANCHOR) return t_solid(w_tile(&W, x, (int)floorf(e->y) - 1)) || t_solid(w_tile(&W, x, (int)floorf(e->y) - 2));
  if (e->kind == EK_ENEMY && e->sub == EN_FLYER) return true;
  if (e->kind == EK_START) return t_solid(w_tile(&W, x, y));
  if (e->hold & HOLD_FLOOR) return t_solid(w_tile(&W, x, y)) && !t_solid(w_tile(&W, x, y - 1));
  if (e->hold & HOLD_CEIL) return t_solid(w_tile(&W, x, y - 1)) && !t_solid(w_tile(&W, x, y));
  if (e->hold & HOLD_WALL_L) return t_solid(w_tile(&W, x - 1, (int)e->y));
  if (e->hold & HOLD_WALL_R) return t_solid(w_tile(&W, x + 1, (int)e->y));
  return e->hold & HOLD_BACK;
}

/* ── the bot's route ── */
static edges *stage_E[12];
static uint32_t stage_ab[12];
static int nstages;

typedef struct route_step { int32_t from, to; uint8_t move, stage, goal, pad; } route_step;

/* the cheapest way over a stage's edges from `from` to the nearest tile of `goal_mask` (1 = a goal): steps
 * appended, the goal reached returned (-1 none) */
static int plan_leg(int st, int from, const uint8_t *goal_mask, route_step *out, int *nout, int cap) {
  static int32_t *dist, *prev, *prevmv;
  static uint8_t *done;
  if (!dist) dist = malloc(sizeof(int32_t) * TW * TH), prev = malloc(sizeof(int32_t) * TW * TH), prevmv = malloc(sizeof(int32_t) * TW * TH), done = malloc(TW * TH);
  /* only the tiles with edges matter: a binary heap over them */
  static int32_t *heap;
  static int hn;
  if (!heap) heap = malloc(sizeof(int32_t) * TW * TH * 4);
  for (int i = 0; i < TW * TH; i++) dist[i] = 1 << 30;
  memset(done, 0, TW * TH);
  hn = 0;
  dist[from] = 0, prev[from] = -1;
#define HPUSH(t)                                                                              \
  do {                                                                                        \
    int c_ = hn++;                                                                            \
    heap[c_] = (t);                                                                           \
    while (c_ > 0 && dist[heap[(c_ - 1) / 2]] > dist[heap[c_]]) {                             \
      int p_ = (c_ - 1) / 2, x_ = heap[p_];                                                   \
      heap[p_] = heap[c_], heap[c_] = x_, c_ = p_;                                            \
    }                                                                                         \
  } while (0)
  HPUSH(from);
  int found = -1;
  while (hn) {
    int u = heap[0];
    heap[0] = heap[--hn];
    for (int c = 0;;) {
      int l = 2 * c + 1, r = l + 1, m = c;
      if (l < hn && dist[heap[l]] < dist[heap[m]]) m = l;
      if (r < hn && dist[heap[r]] < dist[heap[m]]) m = r;
      if (m == c) break;
      int x = heap[m];
      heap[m] = heap[c], heap[c] = x, c = m;
    }
    if (done[u]) continue;
    done[u] = 1;
    if (goal_mask[u] && u != from) { found = u; break; }
    if (eslot[u] < 0 || eslot[u] >= nslots) continue;
    const edges *e = &stage_E[st][eslot[u]];
    for (int k = 0; k < e->n; k++) {
      int v = e->to[k], nd = dist[u] + e->ticks[k] + 4;
      if (nd < dist[v]) {
        dist[v] = nd, prev[v] = u, prevmv[v] = e->mv[k];
        HPUSH(v);
      }
    }
  }
#undef HPUSH
  if (found < 0) return -1;
  int path[20000], n = 0;
  for (int t = found; t != from && n < 20000; t = prev[t]) path[n++] = t;
  for (int i = n - 1; i >= 0 && *nout < cap; i--) {
    int to = path[i], fr = i == n - 1 ? from : path[i + 1];
    out[(*nout)++] = (route_step){ fr, to, (uint8_t)prevmv[to], (uint8_t)st, 0, 0 };
  }
  out[*nout - 1].goal = 1;
  return found;
}

static int write_route(const char *path, int start) {
  static uint8_t goal[TW * TH];
  static route_step steps[60000];
  int n = 0;
  memset(goal, 0, sizeof goal);
  int ngoals = 0, last = -1;
  for (int i = 0; i < W.nents; i++) {
    const ent *e = &W.ents[i];
    int t = -1;
    if (e->kind == EK_PICKUP) t = (int)roundf(e->y) * TW + (int)floorf(e->x);
    if (e->kind == EK_BOSS || e->kind == EK_SAVE) t = (int)roundf(e->y) * TW + (int)floorf(e->x);
    if (t >= 0 && t < TW * TH && node_at[t] < 0) /* the node beside it */
      for (int dx = -2; dx <= 2; dx++)
        if (node_at[t + dx] >= 0) { t += dx; break; }
    if (t < 0 || t >= TW * TH || node_at[t] < 0) continue;
    if (e->kind == EK_BOSS && e->sub == NBOSSES - 1) last = t; /* the heart: last of all, once everything else is found */
    else goal[t] = 1;
    ngoals++;
  }
  int cur = start, legs = 0;
  for (int st = 0; st < nstages; st++)
    for (;;) {
      int g = plan_leg(st, cur, goal, steps, &n, 60000);
      if (g < 0) break;
      goal[g] = 0, cur = g, legs++;
    }
  if (last >= 0) {
    goal[last] = 1;
    int g = plan_leg(nstages - 1, cur, goal, steps, &n, 60000);
    if (g >= 0) goal[g] = 0, legs++;
  }
  int left = 0;
  for (int i = 0; i < TW * TH; i++) left += goal[i];
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  uint32_t hdr[3] = { 0x31545258u /* "XRT1" */, W.hash, (uint32_t)n };
  fwrite(hdr, sizeof hdr, 1, f);
  fwrite(steps, sizeof(route_step), (size_t)n, f);
  fclose(f);
  printf("%s route: %d steps, %d goals reached in %d legs, %d left\n", left ? "FAIL" : "ok  ", n, legs, legs, left);
  return left;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: explorer_check <world.bin> [--route out] [--break n] [--quiet]\n");
    return 2;
  }
  const char *route = NULL;
  int brk = 0;
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "--route") && i + 1 < argc) route = argv[++i];
    else if (!strcmp(argv[i], "--break") && i + 1 < argc) brk = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--quiet")) quiet = 1;
    else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
    else if (!strcmp(argv[i], "--threads") && i + 1 < argc) nthreads = atoi(argv[++i]);
  }
  if (!world_load(&W, argv[1])) {
    fprintf(stderr, "explorer_check: cannot read %s\n", argv[1]);
    return 2;
  }
  if (brk) break_world(brk);
  moves_init();
  memcpy(T, W.t, sizeof T);
  node_at = malloc(sizeof(int32_t) * TW * TH);
  node_xy = malloc(sizeof(int32_t) * TW * TH);
  eslot = malloc(sizeof(int32_t) * TW * TH);
  vis = calloc(TW * TH, 1);
  par = malloc(sizeof(int32_t) * TW * TH);
  queue = malloc(sizeof(int32_t) * TW * TH * 2);
  for (int i = 0; i < TW * TH; i++) eslot[i] = -1;
  build_nodes();
  E = malloc(sizeof(edges) * (size_t)(nnodes + 20000));
  for (int i = 0; i < MAXENTS; i++) pk_stage[i] = -1;
  int fails = 0;
  double t0 = (double)clock() / CLOCKS_PER_SEC;
  struct timespec ts0;
  clock_gettime(CLOCK_MONOTONIC, &ts0);
  printf("world %08x: %d nodes, %d moves, %d threads\n", W.hash, nnodes, NMOVES, nthreads);
  int start = (int)roundf(W.start_y) * TW + (int)floorf(W.start_x);
  if (node_at[start] < 0) { printf("FAIL the start is not a standing spot\n"); return 1; }
  if (!only) {
  /* stages */
  AB = 0;
  static const uint32_t ORDER[NABILITIES] = { AB_BOLT | AB_DJUMP, AB_DASH, AB_CLIMB, AB_POUND, AB_SWIM, AB_GRAPPLE, 0 };
  int order_bad = 0;
  push(start);
  for (stage = 0; stage < 12; stage++) {
    walk();
    if (stage < 12) { /* this stage's edges, for the route */
      stage_E[stage] = malloc(sizeof(edges) * (size_t)nslots);
      memcpy(stage_E[stage], E, sizeof(edges) * (size_t)nslots);
      stage_ab[stage] = AB;
      nstages = stage + 1;
    }
    int ex;
    int locks = softlocks(start, &ex);
    if (getenv("CHECK_PROBE")) { int px = atoi(getenv("CHECK_PROBE")), py = atoi(strchr(getenv("CHECK_PROBE"), ',') + 1); printf("probe %d,%d node %d vis %d\n", px, py, node_at[py * TW + px], vis[py * TW + px]); }
    if (getenv("CHECK_STUCK")) { /* (development) the doors the walk reached one side of and not the other */
      static uint8_t rseen[MAXROOMS];
      memset(rseen, 0, sizeof rseen);
      for (int i = 0; i < TW * TH; i++)
        if (vis[i]) { int r = w_room_at(&W, (float)(i % TW) + 0.5f, (float)(i / TW) - 0.5f); if (r >= 0) rseen[r] = 1; }
      for (int i = 0; i < W.ndoors; i++)
        if (rseen[W.doors[i].a] != rseen[W.doors[i].b])
          printf("stuck: door %d at %d,%d (%s) between room %d (%s) and room %d (%s)\n", i, W.doors[i].x, W.doors[i].y, W.doors[i].vertical ? "floor" : "wall",
                 W.doors[i].a, rseen[W.doors[i].a] ? "reached" : "not", W.doors[i].b, rseen[W.doors[i].b] ? "reached" : "not");
    }
    int n = 0;
    for (int i = 0; i < TW * TH; i++) n += vis[i];
    printf("stage %d (abilities %02x): %d places reached, %d beacons, pickups +%02x\n", stage, AB, n, beacons, pending & ~AB);
    if (locks) {
      fails++;
      printf("FAIL softlock: %d places reached at stage %d cannot get back to the start (first at tile %d,%d, room %d)\n", locks, stage,
             ex % TW, ex / TW, w_room_at(&W, (float)(ex % TW), (float)(ex / TW) - 0.5f));
    }
    uint32_t got = pending & ~AB;
    if (!got) break;
    if (stage < NABILITIES && got != ORDER[stage]) {
      order_bad++;
      printf("FAIL order: stage %d gave %02x, the world was made to give %02x\n", stage, got, ORDER[stage]);
      for (int i = 0; i < W.nents; i++)
        if (W.ents[i].kind == EK_PICKUP && W.ents[i].sub == PK_ABILITY && (W.ents[i].arg & got & ~ORDER[stage])) {
          printf("     ability %02x in room %d (%s)\n", W.ents[i].arg, W.ents[i].room, REGIONS[W.rooms[W.ents[i].room].region].name);
          print_path(pk_tile[W.ents[i].id]);
        }
    }
    AB |= got;
    /* walk() expands the whole queue again from its start: every place, with the new abilities */
  }
  fails += order_bad;
  if (route && write_route(route, start)) fails++;
  if (AB != AB_ALL) { fails++; printf("FAIL abilities: only %02x were reached (want %02x)\n", AB, AB_ALL); }
  /* pickups */
  int missing = 0;
  for (int i = 0; i < W.nents; i++)
    if (W.ents[i].kind == EK_PICKUP && !got_pk[W.ents[i].id]) {
      missing++;
      if (missing <= 8) printf("FAIL pickup %d (kind %d) in room %d (%s) is never reached\n", W.ents[i].id, W.ents[i].sub, W.ents[i].room, REGIONS[W.rooms[W.ents[i].room].region].name);
    }
  if (missing) fails++, printf("FAIL pickups: %d of %d never reached\n", missing, W.npickups);
  else printf("ok   pickups: all %d reached\n", W.npickups);
  int bosses = 0;
  for (int i = 1; i <= NBOSSES; i++) bosses += boss_done[i];
  if (bosses != NBOSSES) fails++, printf("FAIL wardens: %d of %d arenas reached\n", bosses, NBOSSES);
  else printf("ok   wardens: all %d arenas reached\n", NBOSSES);
  }
  if (!only || !strcmp(only, "gates")) {
  /* the gates */
  int gate_bad = 0, ngate = 0;
  static const uint32_t GATE_AB[] = { 0, AB_DJUMP, AB_DASH, AB_CLIMB, AB_POUND, AB_SWIM, AB_GRAPPLE };
  memcpy(T, W.t, sizeof T); /* the world as made: cracked floors whole */
  build_nodes();
  for (int i = 0; i < W.nrooms; i++) {
    const struct room *r = &W.rooms[i];
    if (r->kind != RK_GATE || r->gate < 1 || r->gate > 6) continue;
    ngate++;
    /* which half is the old one: the half whose cell has a door out of the room that was reached first is not
     * known here; both directions are tried: a gate must stop the crossing at least one way without its ability,
     * and allow it with every ability (POUND only from the top) */
    bool a_to_b, b_to_a, a_to_b_all, b_to_a_all;
    uint32_t before = GATE_AB[r->gate] - 1; /* every ability that comes before this one (the bits are in order) */
    local_reach(i, before, 0, &a_to_b);
    local_reach(i, before, 1, &b_to_a);
    local_reach(i, AB_ALL, 0, &a_to_b_all);
    local_reach(i, AB_ALL, 1, &b_to_a_all);
    bool stops = r->gate == 4 ? !a_to_b : (!a_to_b && !b_to_a);
    bool opens = r->gate == 4 ? a_to_b_all : (a_to_b_all && b_to_a_all);
    if (!stops || !opens) {
      gate_bad++;
      if (gate_bad <= 10)
        printf("FAIL gate room %d (%s, needs %02x): with only the abilities before it %s/%s, with every ability %s/%s\n", i, REGIONS[r->region].name, GATE_AB[r->gate],
               a_to_b ? "crossed" : "held", b_to_a ? "crossed" : "held", a_to_b_all ? "crossed" : "HELD", b_to_a_all ? "crossed" : "HELD");
    }
  }
  if (gate_bad) fails++, printf("FAIL gates: %d of %d gate rooms\n", gate_bad, ngate);
  else printf("ok   gates: all %d gate rooms hold without their ability and open with it\n", ngate);
  }
  if (!only || !strcmp(only, "grounded")) {
  /* grounded */
  int floating = 0;
  for (int i = 0; i < W.nents; i++)
    if (W.ents[i].kind != EK_NONE && !grounded(&W.ents[i])) {
      floating++;
      if (floating <= 5) printf("FAIL floating: entity %d kind %d sub %d at %.1f,%.1f\n", i, W.ents[i].kind, W.ents[i].sub, W.ents[i].x, W.ents[i].y);
    }
  if (floating) fails++, printf("FAIL grounded: %d things float\n", floating);
  else printf("ok   grounded: all %d things are held by their tiles\n", W.nents);
  }
  struct timespec ts1;
  clock_gettime(CLOCK_MONOTONIC, &ts1);
  printf("%s in %.1f s\n", fails ? "check FAIL" : "check ok", (double)(ts1.tv_sec - ts0.tv_sec) + (double)(ts1.tv_nsec - ts0.tv_nsec) * 1e-9);
  (void)t0;
  return fails ? 1 : 0;
}
