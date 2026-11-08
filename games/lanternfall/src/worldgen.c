/* lanternfall: the world generator. Deterministic from a seed; tools/worldgen runs it and writes assets/world.bin,
 * and the checker (check.c) proves the result.
 *
 * How the world is laid out:
 *   1. regions sit on the cell grid (regions.c); the links between them are listed below with the ability each
 *      link's gate needs. A gate is a two-cell room on the border, split in two halves by an obstacle (a wall too
 *      high for one jump, a thorn pit too long to jump, a sheer wall, a cracked floor, a flooded passage, a chasm
 *      under anchors). One half opens toward the region you come from, the other into the next one.
 *   2. each region places its set pieces (the warden's arena with the prize behind it, item rooms, save shrines,
 *      side branches behind a later gate or a hidden wall), then a spine of cells joins every doorway they need,
 *      branches grow off it, and the cells are cut into rooms of one to three cells joined by doors.
 *   3. each room is carved: walls, door openings, ledges under doors that sit above the floor, ladders of one-way
 *      ledges up to them, the floor's steps, thorn pits and pools, platforms, then enemies, lamps and props, each
 *      placed on what holds it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world.h"

static uint32_t rng;
static uint32_t rnd(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
static int rint_(int lo, int hi) { return hi <= lo ? lo : lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }
static bool chance(int pct) { return (int)(rnd() % 100u) < pct; }

static world *W;
static int8_t creg[GH][GW]; /* region of a cell (-1 none) */
static uint8_t used[GH][GW]; /* 1: chosen for rooms, 2: a set piece's cell */

/* ── links between regions ───────────────────────────────────────────── */

enum { G_NONE, G_DJUMP, G_DASH, G_CLIMB, G_POUND, G_SWIM, G_GRAPPLE, G_SEAL, G_BOSS };
/* the ability bit a gate needs */
static inline int gate_ability(int g) {
  static const int A[] = { 0, AB_DJUMP, AB_DASH, AB_CLIMB, AB_POUND, AB_SWIM, AB_GRAPPLE, 0, 0 };
  return A[g];
}
typedef struct link { int from, to, gate; } link;
/* from: the region you reach first (the gate's old side) */
static const link LINKS[] = {
  { R_GATE, R_ROOTWAY, G_NONE },  { R_ROOTWAY, R_GLASS, G_DJUMP }, { R_GLASS, R_FEN, G_DASH },
  { R_FEN, R_CLOCK, G_CLIMB },      { R_CLOCK, R_TIDAL, G_POUND },   { R_TIDAL, R_STAR, G_SWIM },
  { R_STAR, R_SKY, G_GRAPPLE },     { R_GATE, R_ARCHIVE, G_DASH }, { R_ARCHIVE, R_FORGE, G_POUND },
  { R_CLOCK, R_FORGE, G_CLIMB },    { R_CLOCK, R_BLOOM, G_SWIM },    { R_BLOOM, R_FROST, G_DJUMP },
  { R_FORGE, R_FROST, G_GRAPPLE },  { R_GLASS, R_STAR, G_GRAPPLE },  { R_ROOTWAY, R_SKY, G_GRAPPLE },
  { R_FEN, R_TIDAL, G_SWIM },       { R_GATE, R_HEART, G_SEAL },   { R_FORGE, R_HEART, G_SEAL },
};
#define NLINKS ((int)(sizeof LINKS / sizeof LINKS[0]))

/* what each region holds: its warden (boss index + 1), the ability its prize room gives, side branches */
typedef struct region_plan {
  int boss;      /* 1..6, 0 none */
  int prize;     /* AB_* in the prize room (behind the warden when there is one), 0 none */
  int saves;
  int target;    /* cells of rooms to grow (set pieces not counted) */
  int branch_gate[3]; /* side branches behind a gate of these abilities (G_*), each with an upgrade */
  int branch_pk[3];   /* PK_HEALTH / PK_ENERGY / PK_RELIC */
  int secrets;   /* hidden walls with a relic behind */
} region_plan;
static const region_plan PLAN[NREGIONS] = {
  [R_GATE] = { 0, AB_BOLT, 2, 18, { G_POUND, G_SWIM, 0 }, { PK_HEALTH, PK_RELIC, 0 }, 1 },
  [R_ROOTWAY] = { 1, AB_DJUMP, 2, 30, { G_DASH, G_CLIMB, 0 }, { PK_ENERGY, PK_HEALTH, 0 }, 1 },
  [R_GLASS] = { 2, AB_DASH, 2, 30, { G_CLIMB, G_SWIM, 0 }, { PK_HEALTH, PK_RELIC, 0 }, 1 },
  [R_FEN] = { 0, AB_CLIMB, 1, 22, { G_POUND, 0, 0 }, { PK_ENERGY, 0, 0 }, 1 },
  [R_CLOCK] = { 3, AB_POUND, 2, 30, { G_GRAPPLE, 0, 0 }, { PK_HEALTH, 0, 0 }, 1 },
  [R_TIDAL] = { 4, AB_SWIM, 2, 34, { G_GRAPPLE, 0, 0 }, { PK_RELIC, 0, 0 }, 1 },
  [R_STAR] = { 0, AB_GRAPPLE, 2, 30, { G_GRAPPLE, 0, 0 }, { PK_ENERGY, 0, 0 }, 1 },
  [R_SKY] = { 5, 0, 2, 28, { 0, 0, 0 }, { 0, 0, 0 }, 2 },
  [R_ARCHIVE] = { 0, 0, 1, 26, { G_CLIMB, G_GRAPPLE, 0 }, { PK_HEALTH, PK_ENERGY, 0 }, 2 },
  [R_FORGE] = { 0, 0, 1, 26, { G_SWIM, 0, 0 }, { PK_RELIC, 0, 0 }, 1 },
  [R_FROST] = { 0, 0, 2, 28, { G_GRAPPLE, 0, 0 }, { PK_HEALTH, 0, 0 }, 2 },
  [R_BLOOM] = { 0, 0, 2, 28, { G_GRAPPLE, 0, 0 }, { PK_ENERGY, 0, 0 }, 2 },
  [R_HEART] = { 6, 0, 0, 4, { 0, 0, 0 }, { 0, 0, 0 }, 0 },
};

/* ── rooms and doors ─────────────────────────────────────────────────── */

static int new_room(int x, int y, int w, int h, int region, int kind) {
  if (W->nrooms >= MAXROOMS) { fprintf(stderr, "worldgen: too many rooms\n"); exit(2); }
  int i = W->nrooms++;
  W->rooms[i] = (room){ .x = (uint8_t)x, .y = (uint8_t)y, .w = (uint8_t)w, .h = (uint8_t)h, .region = (uint8_t)region, .kind = (uint8_t)kind };
  for (int cy = y; cy < y + h; cy++)
    for (int cx = x; cx < x + w; cx++) W->cell_room[cy][cx] = (uint16_t)i;
  return i;
}

/* a door between the rooms of two adjacent cells; returns its index */
static int add_door(int ax, int ay, int bx, int by, uint8_t fill) {
  int a = W->cell_room[ay][ax], b = W->cell_room[by][bx];
  door d = { .a = (uint16_t)a, .b = (uint16_t)b };
  if (ay == by) { /* side by side: the opening in the wall between, four tiles tall at the cell's floor */
    int bxp = (ax < bx ? bx : ax) * CELL_W;
    d.vertical = 0, d.x = (uint16_t)(bxp - 2), d.y = (uint16_t)(ay * CELL_H + 12), d.len = 4;
  } else {
    int byp = (ay < by ? by : ay) * CELL_H;
    d.vertical = 1, d.x = (uint16_t)(ax * CELL_W + 13), d.y = (uint16_t)(byp - 2), d.len = 6;
  }
  d.pad = fill;
  for (int i = 0; i < W->ndoors; i++) /* one door per cell edge */
    if (W->doors[i].x == d.x && W->doors[i].y == d.y && W->doors[i].vertical == d.vertical) return i;
  if (W->ndoors >= MAXDOORS) { fprintf(stderr, "worldgen: too many doors\n"); exit(2); }
  W->doors[W->ndoors] = d;
  return W->ndoors++;
}

/* ── set pieces ──────────────────────────────────────────────────────── */

typedef struct port { int cx, cy, px, py; } port; /* a set piece's cell, and the region cell its door leads to */
static port ports[256];
static int nports;

static bool cell_free(int x, int y, int region) {
  return x >= 0 && y >= 0 && x < GW && y < GH && creg[y][x] == region && !used[y][x];
}

/* the room kinds a piece makes */
typedef struct piece_room { int dx, dy, w, h, kind; } piece_room;

/* the gate (or warden) room's halves: which cell of a two-cell room is the old side */
typedef struct gate_rec { int room, gate; int old_cx, old_cy; } gate_rec;
static gate_rec gates[128];
static int ngates;

/* place a two-cell gate room in region n touching region o (o's cell the old side's door leads to) */
static bool place_link_gate(int o, int n, int g) {
  int tries = 0;
  for (; tries < 4000; tries++) {
    const region_info *rn = &REGIONS[n];
    int x = rn->x + rint_(0, rn->w - 1), y = rn->y + rint_(0, rn->h - 1);
    if (!cell_free(x, y, n)) continue;
    static const int DX[4] = { -1, 1, 0, 0 }, DY[4] = { 0, 0, -1, 1 };
    int d = rint_(0, 3), ox = x + DX[d], oy = y + DY[d];
    if (ox < 0 || oy < 0 || ox >= GW || oy >= GH || creg[oy][ox] != o || used[oy][ox]) continue;
    /* the new half: beside (2x1) or below (1x2, a cracked floor needs the old side on top) */
    int nx, ny;
    if (g == G_POUND) {
      if (d == 3) continue; /* the old side below: a floor cannot be broken from under it */
      nx = x, ny = y + 1;
    } else if (d == 0 || d == 1) nx = x - DX[d], ny = y; /* away from the border */
    else nx = x + (chance(50) ? 1 : -1), ny = y;
    if (!cell_free(nx, ny, n)) continue;
    /* a free region cell beside the new half for its door (not the old cell, not across the border) */
    int pick = -1;
    for (int k = 0, s = rint_(0, 3); k < 4; k++) {
      int dd = (s + k) & 3, px = nx + DX[dd], py = ny + DY[dd];
      if (px == x && py == y) continue;
      if (!cell_free(px, py, n)) continue;
      if (g == G_POUND && dd == 2) continue;
      pick = dd;
      break;
    }
    if (pick < 0) continue;
    int rx = x < nx ? x : nx, ry = y < ny ? y : ny, rw = x != nx ? 2 : 1, rh = y != ny ? 2 : 1;
    int r = new_room(rx, ry, rw, rh, n, g == G_SEAL || g == G_NONE ? RK_PLAIN : RK_GATE);
    W->rooms[r].gate = (uint8_t)g;
    used[y][x] = used[ny][nx] = 2;
    gates[ngates++] = (gate_rec){ r, g, x, y };
    /* the old side's door leads to o's cell (a room there is grown later); the new side's to n's */
    ports[nports++] = (port){ x, y, ox, oy };
    ports[nports++] = (port){ nx, ny, nx + DX[pick], ny + DY[pick] };
    return true;
  }
  fprintf(stderr, "worldgen: no place for the %s - %s link\n", REGIONS[o].name, REGIONS[n].name);
  return false;
}

/* place a piece of rooms (list) at a free spot of region r whose entry cell (rooms[0] at (dx,dy)) has a free
 * region cell beside it; returns the first room's index or -1 */
static int place_piece(int r, const piece_room *pr, int n, int *room_out, int *entry_x, int *entry_y) {
  const region_info *ri = &REGIONS[r];
  for (int tries = 0; tries < 3000; tries++) {
    int x = ri->x + rint_(0, ri->w - 1), y = ri->y + rint_(0, ri->h - 1);
    bool ok = true;
    for (int k = 0; k < n && ok; k++)
      for (int cy = 0; cy < pr[k].h && ok; cy++)
        for (int cx = 0; cx < pr[k].w && ok; cx++) ok = cell_free(x + pr[k].dx + cx, y + pr[k].dy + cy, r);
    if (!ok) continue;
    /* the entry: a free region cell beside the first room's first cell, left or right (or above) */
    static const int DX[3] = { -1, 1, 0 }, DY[3] = { 0, 0, -1 };
    int pick = -1, s = rint_(0, 2);
    for (int k = 0; k < 3; k++) {
      int d = (s + k) % 3, ex = x + pr[0].dx + DX[d], ey = y + pr[0].dy + DY[d];
      if (d == 1) ex = x + pr[0].dx + pr[0].w; /* the right of a wide room */
      bool inside = false;
      for (int q = 0; q < n; q++)
        if (ex >= x + pr[q].dx && ex < x + pr[q].dx + pr[q].w && ey >= y + pr[q].dy && ey < y + pr[q].dy + pr[q].h) inside = true;
      if (inside || !cell_free(ex, ey, r)) continue;
      pick = d;
      *entry_x = ex, *entry_y = ey;
      break;
    }
    if (pick < 0) continue;
    for (int k = 0; k < n; k++) {
      room_out[k] = new_room(x + pr[k].dx, y + pr[k].dy, pr[k].w, pr[k].h, r, pr[k].kind);
      for (int cy = 0; cy < pr[k].h; cy++)
        for (int cx = 0; cx < pr[k].w; cx++) used[y + pr[k].dy + cy][x + pr[k].dx + cx] = 2;
    }
    int ecx = pick == 1 ? x + pr[0].dx + pr[0].w - 1 : x + pr[0].dx;
    ports[nports++] = (port){ ecx, y + pr[0].dy, *entry_x, *entry_y };
    return room_out[0];
  }
  return -1;
}

/* ── the spine and the rooms of a region ─────────────────────────────── */

static void spine_path(int r, int sx, int sy, int tx, int ty) {
  /* Dijkstra on noisy cell costs over the region's free cells (cells already chosen are cheap) */
  static int dist[GH][GW], par[GH][GW];
  static uint8_t cost[GH][GW], closed[GH][GW];
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) dist[y][x] = 1 << 30, par[y][x] = -1, closed[y][x] = 0, cost[y][x] = (uint8_t)(1 + rnd() % 6);
  dist[sy][sx] = 0;
  static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
  for (;;) {
    int bx = -1, by = -1, bd = 1 << 30;
    for (int y = REGIONS[r].y; y < REGIONS[r].y + REGIONS[r].h; y++)
      for (int x = REGIONS[r].x; x < REGIONS[r].x + REGIONS[r].w; x++)
        if (!closed[y][x] && dist[y][x] < bd) bd = dist[y][x], bx = x, by = y;
    if (bx < 0) return; /* unreachable */
    closed[by][bx] = 1;
    if (bx == tx && by == ty) break;
    for (int d = 0; d < 4; d++) {
      int nx = bx + DX[d], ny = by + DY[d];
      if (nx < 0 || ny < 0 || nx >= GW || ny >= GH || creg[ny][nx] != r || used[ny][nx] == 2 || closed[ny][nx]) continue;
      int nd = bd + (used[ny][nx] == 1 ? 1 : cost[ny][nx]);
      if (nd < dist[ny][nx]) dist[ny][nx] = nd, par[ny][nx] = by * GW + bx;
    }
  }
  for (int c = ty * GW + tx; c >= 0; c = par[c / GW][c % GW])
    if (used[c / GW][c % GW] != 2) used[c / GW][c % GW] = 1;
}

static void grow_region(int r) {
  const region_info *ri = &REGIONS[r];
  /* the cells the spine must join: every port into this region */
  int need[128][2], nn = 0;
  for (int i = 0; i < nports; i++)
    if (creg[ports[i].py][ports[i].px] == r && nn < 128) need[nn][0] = ports[i].px, need[nn][1] = ports[i].py, nn++;
  if (!nn) return;
  used[need[0][1]][need[0][0]] = 1;
  for (int i = 1; i < nn; i++) spine_path(r, need[0][0], need[0][1], need[i][0], need[i][1]);
  /* branches: free cells beside chosen ones until the region has its target */
  int have = 0;
  for (int y = ri->y; y < ri->y + ri->h; y++)
    for (int x = ri->x; x < ri->x + ri->w; x++) have += used[y][x] == 1;
  for (int guard = 0; have < PLAN[r].target && guard < 20000; guard++) {
    int x = ri->x + rint_(0, ri->w - 1), y = ri->y + rint_(0, ri->h - 1);
    if (!cell_free(x, y, r)) continue;
    int nb = 0;
    if (x > 0 && used[y][x - 1] == 1) nb++;
    if (x < GW - 1 && used[y][x + 1] == 1) nb++;
    if (y > 0 && used[y - 1][x] == 1) nb++;
    if (y < GH - 1 && used[y + 1][x] == 1) nb++;
    if (nb == 0 || (nb > 1 && chance(70))) continue; /* mostly corridors, few blobs */
    used[y][x] = 1;
    have++;
  }
  /* cut the chosen cells into rooms: runs of up to three side by side, some two tall, the rest single */
  for (int y = ri->y; y < ri->y + ri->h; y++)
    for (int x = ri->x; x < ri->x + ri->w; x++) {
      if (used[y][x] != 1 || W->cell_room[y][x] != 0xffff) continue;
      int w = 1, h = 1;
      int want = chance(55) ? rint_(2, 3) : 1;
      while (w < want && x + w < GW && used[y][x + w] == 1 && W->cell_room[y][x + w] == 0xffff && creg[y][x + w] == r) w++;
      if (w == 1 && chance(35) && y + 1 < GH && used[y + 1][x] == 1 && W->cell_room[y + 1][x] == 0xffff && creg[y + 1][x] == r)
        h = 2 + (chance(30) && y + 2 < GH && used[y + 2][x] == 1 && W->cell_room[y + 2][x] == 0xffff && creg[y + 2][x] == r);
      else if (w == 2 && chance(25) && y + 1 < GH) {
        bool ok = true;
        for (int k = 0; k < 2; k++) ok = ok && used[y + 1][x + k] == 1 && W->cell_room[y + 1][x + k] == 0xffff && creg[y + 1][x + k] == r;
        if (ok) h = 2;
      }
      new_room(x, y, w, h, r, h > 1 && w == 1 ? RK_SHAFT : w * h >= 4 ? RK_HALL : RK_PLAIN);
    }
  /* doors: a spanning tree over the region's grown rooms, then a few loops */
  int first = -1;
  for (int i = 0; i < W->nrooms; i++)
    if (W->rooms[i].region == r && used[W->rooms[i].y][W->rooms[i].x] == 1) { first = i; break; }
  if (first < 0) return;
  static uint8_t in_tree[MAXROOMS];
  memset(in_tree, 0, sizeof in_tree);
  in_tree[first] = 1;
  for (bool grew = true; grew;) {
    grew = false;
    /* every cell edge between a tree room and a grown room outside it, in a shuffled order */
    int cand[2048][4], nc = 0;
    for (int y = ri->y; y < ri->y + ri->h; y++)
      for (int x = ri->x; x < ri->x + ri->w; x++) {
        if (used[y][x] != 1) continue;
        int a = W->cell_room[y][x];
        static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
        for (int d = 0; d < 4; d++) {
          int nx = x + DX[d], ny = y + DY[d];
          if (nx < 0 || ny < 0 || nx >= GW || ny >= GH || used[ny][nx] != 1 || creg[ny][nx] != r) continue;
          int b = W->cell_room[ny][nx];
          if (a == b || !in_tree[a] || in_tree[b] || nc >= 2048) continue;
          cand[nc][0] = x, cand[nc][1] = y, cand[nc][2] = nx, cand[nc][3] = ny, nc++;
        }
      }
    if (nc) {
      int k = rint_(0, nc - 1);
      add_door(cand[k][0], cand[k][1], cand[k][2], cand[k][3], 0);
      in_tree[W->cell_room[cand[k][3]][cand[k][2]]] = 1;
      grew = true;
    }
  }
  /* loops */
  for (int y = ri->y; y < ri->y + ri->h; y++)
    for (int x = ri->x; x < ri->x + ri->w; x++) {
      if (used[y][x] != 1) continue;
      if (x + 1 < GW && used[y][x + 1] == 1 && creg[y][x + 1] == r && W->cell_room[y][x] != W->cell_room[y][x + 1] && chance(18)) add_door(x, y, x + 1, y, 0);
      if (y + 1 < GH && used[y + 1][x] == 1 && creg[y + 1][x] == r && W->cell_room[y][x] != W->cell_room[y + 1][x] && chance(12)) add_door(x, y, x, y + 1, 0);
    }
}

/* ── carving ─────────────────────────────────────────────────────────── */

static void set(int x, int y, int t) {
  if (w_in(x, y)) W->t[y][x] = (uint8_t)t;
}
static void fill(int x0, int y0, int x1, int y1, int t) { /* inclusive */
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) set(x, y, t);
}
static int tile(int x, int y) { return w_tile(W, x, y); }

static int add_ent(int kind, int sub, int arg, int hold, int room, float x, float y) {
  if (W->nents >= MAXENTS) return -1;
  ent *e = &W->ents[W->nents];
  *e = (ent){ .kind = (uint8_t)kind, .sub = (uint8_t)sub, .arg = (uint8_t)arg, .hold = (uint8_t)hold, .room = (uint16_t)room, .x = x, .y = y };
  if (kind == EK_PICKUP) e->id = (uint16_t)W->npickups++;
  if (kind == EK_BOSS) e->id = (uint16_t)W->nbosses++;
  if (kind == EK_SAVE) e->id = (uint16_t)W->nsaves++;
  return W->nents++;
}

/* a ladder of one-way ledges from the floor at (x, floor_y) up to a surface at top_y around x */
static void ladder(int x0, int x1, int fx, int floor_y, int top_y, int target_lo, int target_hi) {
  int y = floor_y - 3, k = 0, cx = fx;
  while (y > top_y + 3) {
    int w = 5, px = cx + (k & 1 ? 2 : -6);
    if (px < x0) px = x0 + (k & 1);
    if (px + w - 1 > x1) px = x1 - w + 1;
    for (int i = 0; i < w; i++)
      if (tile(px + i, y) == T_EMPTY) set(px + i, y, T_ONEWAY);
    y -= 3, k++;
  }
  /* the last one under the target's edge, three below it at most */
  int lo = target_lo - 2, hi = target_hi + 2;
  if (lo < x0) lo = x0;
  if (hi > x1) hi = x1;
  if (hi - lo < 3) hi = lo + 3;
  for (int i = lo; i <= hi; i++)
    if (tile(i, y) == T_EMPTY) set(i, y, T_ONEWAY);
}

static int door_side(const room *rm, const door *d) {
  /* 0 left, 1 right, 2 top, 3 bottom of this room */
  int X0 = rm->x * CELL_W, Y0 = rm->y * CELL_H;
  if (!d->vertical) return d->x < X0 + 2 ? 0 : 1;
  return d->y < Y0 + 2 ? 2 : 3;
}

/* ── composition: the rock that gives a room its shape ───────────────────
 * Rounded masses hang from the ceiling, bulge from the walls (overhangs, shelves, the alcoves between them) and
 * float as islands with tapered undersides; the ceiling's line rolls. Each is laid only where everything round it
 * is open air (so no door, ledge, ladder or kept column is touched) and only over open tiles. */
static bool area_open(int x0, int y0, int x1, int y1) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      if (tile(x, y) != T_EMPTY) return false;
  return true;
}
static void mass(int cx, int cy, float rx, float ry, int bx0, int by0, int bx1, int by1) {
  for (int y = by0; y <= by1; y++)
    for (int x = bx0; x <= bx1; x++) {
      float dx = ((float)x - (float)cx) / (rx + 0.5f), dy = ((float)y - (float)cy) / (ry + 0.5f);
      if (dx * dx + dy * dy <= 1 && tile(x, y) == T_EMPTY) set(x, y, T_SOLID);
    }
}
static bool kept(const uint8_t *keep, int X0, int Wd, int x0, int x1) {
  for (int x = x0; x <= x1; x++)
    if (x >= X0 && x < X0 + Wd && keep[x - X0]) return true;
  return false;
}
/* no hole in the ceiling row within d columns of x0..x1 (a trapdoor up and its ladder) */
static bool ceiling_whole(int y, int x0, int x1, int d) {
  for (int x = x0 - d; x <= x1 + d; x++)
    if (!t_solid(tile(x, y))) return false;
  return true;
}
static void compose(int ri, const uint8_t *keep, bool light) {
  room *rm = &W->rooms[ri];
  int X0 = rm->x * CELL_W, Y0 = rm->y * CELL_H, Wd = rm->w * CELL_W, Ht = rm->h * CELL_H;
  int ix0 = X0 + 2, ix1 = X0 + Wd - 3, iy0 = Y0 + 2;
  (void)keep;
  /* the ceiling's line rolls: 0..3 rows of rock under it, away from the holes up */
  float ph = (float)rint_(0, 628) * 0.01f, fq = 0.2f + 0.12f * (float)rint_(0, 4) / 4.0f;
  for (int x = ix0 + 1; x <= ix1 - 1; x++) {
    int k = (int)lroundf(1.3f + 1.2f * sinf((float)x * fq + ph) + 0.7f * sinf((float)x * fq * 2.7f + ph * 1.7f));
    if (k <= 0 || !ceiling_whole(iy0 - 1, x, x, 4) || !area_open(x, iy0, x, iy0 + k + 5)) continue;
    fill(x, iy0, x, iy0 + k - 1, T_SOLID);
  }
  /* masses hanging from the ceiling: rounded lumps, one to three a cell */
  for (int c = 0; c < rm->w; c++)
    for (int want = rint_(1, light ? 2 : 3), tries = 0; want > 0 && tries < 12; tries++) {
      int rx = rint_(2, light ? 3 : 5), ry = rint_(2, light ? 3 : 4), cx = X0 + c * CELL_W + rint_(4, CELL_W - 5), cy = iy0 - 1;
      if (cx - rx < ix0 + 1 || cx + rx > ix1 - 1 || !ceiling_whole(iy0 - 1, cx - rx, cx + rx, 4)) continue;
      if (!area_open(cx - rx - 1, iy0 + 4, cx + rx + 1, cy + ry + 7)) continue;
      mass(cx, cy, (float)rx, (float)ry, cx - rx, iy0, cx + rx, cy + ry);
      if (!light && chance(50)) /* a drip of rock under it */
        fill(cx - (rx > 3), cy + ry + 1, cx, cy + ry + 1 + rint_(0, 1), T_SOLID);
      want--;
    }
  /* bulges from the side walls: shelves, overhangs, the alcoves between them; never in front of a door */
  for (int cr = 0; cr < rm->h; cr++)
    for (int side = 0; side < 2; side++) {
      int wx = side ? ix1 + 1 : ix0 - 1, cy0 = Y0 + cr * CELL_H;
      for (int want = rint_(1, light ? 1 : 2), tries = 0; want > 0 && tries < 14; tries++) {
        int rx = rint_(2, light ? 3 : 5), ry = rint_(1, 3), cy = cy0 + rint_(3, CELL_H - 5);
        int x0 = side ? wx - rx - 3 : wx + 1, x1 = side ? wx - 1 : wx + rx + 3;
        int y0 = cy - ry - 3 < iy0 ? iy0 : cy - ry - 3, y1 = cy + ry + 3;
        if (cy - ry < iy0 || y1 > Y0 + Ht - 3) continue;
        bool wall = true; /* the wall beside it is whole (no door behind the bulge) */
        for (int y = y0 - 1; y <= y1 + 1; y++) wall = wall && t_solid(tile(wx, y));
        if (!wall || !area_open(x0, y0, x1, y1)) continue;
        mass(wx, cy, (float)rx, (float)ry, side ? wx - rx : wx + 1, cy - ry, side ? wx - 1 : wx + rx, cy + ry);
        want--;
      }
    }
}

static void carve_room(int ri) {
  room *rm = &W->rooms[ri];
  int X0 = rm->x * CELL_W, Y0 = rm->y * CELL_H, Wd = rm->w * CELL_W, Ht = rm->h * CELL_H;
  int ix0 = X0 + 2, ix1 = X0 + Wd - 3, iy0 = Y0 + 2, iy1 = Y0 + Ht - 3;
  fill(ix0, iy0, ix1, iy1, T_EMPTY);
  int floor_y = Y0 + Ht - 2; /* the floor's surface row (the first solid row) */
  /* columns the floor keeps level and clear: under doors and ladders */
  static uint8_t keep[TW];
  memset(keep + X0, 0, (size_t)Wd);
  for (int i = 0; i < W->ndoors; i++) {
    door *d = &W->doors[i];
    if (d->a != ri && d->b != ri) continue;
    int side = door_side(rm, d);
    if (side <= 1) {
      int wx0 = side == 0 ? X0 : X0 + Wd - 2;
      fill(wx0, d->y, wx0 + 1, d->y + 3, d->pad ? d->pad : T_EMPTY);
      int ledge_y = d->y + 4;
      if (ledge_y < floor_y) { /* a door above the floor: a ledge under it, a ladder up */
        int lx0 = side == 0 ? ix0 : ix1 - 5, lx1 = side == 0 ? ix0 + 5 : ix1;
        fill(lx0, ledge_y, lx1, ledge_y + 1, T_SOLID);
        int fx = side == 0 ? lx1 + 3 : lx0 - 3;
        ladder(ix0, ix1, fx, floor_y, ledge_y, side == 0 ? lx1 : lx0, side == 0 ? lx1 : lx0);
        for (int x = fx - 7; x <= fx + 7; x++)
          if (x >= X0 && x < X0 + Wd) keep[x - X0] = 1;
      }
      for (int x = side == 0 ? ix0 : ix1 - 4; x <= (side == 0 ? ix0 + 4 : ix1); x++) keep[x - X0] = 1;
    } else if (side == 2) { /* a hole in the ceiling: a ladder up to the room above's trapdoor */
      fill(d->x, Y0, d->x + d->len - 1, Y0 + 1, T_EMPTY);
      if (rm->kind == RK_GATE) { /* in a gate room the ladder keeps to the outer wall, far from the obstacle */
        bool left = d->x < X0 + CELL_W;
        int lx0 = left ? ix0 : X0 + Wd - 15, lx1 = left ? X0 + 14 : ix1;
        ladder(lx0, lx1, left ? X0 + 8 : X0 + Wd - 9, floor_y, Y0 - 2, d->x, d->x + d->len - 1);
      } else
        ladder(ix0, ix1, d->x + 2, floor_y, Y0 - 2, d->x, d->x + d->len - 1);
      for (int x = d->x - 8; x <= d->x + d->len + 8; x++)
        if (x >= X0 && x < X0 + Wd) keep[x - X0] = 1;
    } else { /* a trapdoor in the floor */
      fill(d->x, Y0 + Ht - 2, d->x + d->len - 1, Y0 + Ht - 2, d->pad ? d->pad : T_ONEWAY);
      fill(d->x, Y0 + Ht - 1, d->x + d->len - 1, Y0 + Ht - 1, T_EMPTY);
      for (int x = d->x - 2; x <= d->x + d->len + 1; x++)
        if (x >= X0 && x < X0 + Wd) keep[x - X0] = 1;
    }
  }
  if (rm->kind == RK_GATE || rm->kind == RK_BOSS) return;
  if (rm->kind == RK_SAVE || rm->kind == RK_ITEM || rm->kind == RK_START) { compose(ri, keep, true); return; }
  /* the floor: steps of one to three, thorn pits and pools between kept columns */
  int region = rm->region;
  bool flooded = (region == R_TIDAL || region == R_GLASS) && rm->w >= 2 && rm->h == 1 && rm->kind == RK_PLAIN && (ri % 3) == 0;
  int x = ix0 + 5;
  int level = 0; /* rows above the base floor */
  while (x < ix1 - 5) {
    int run = rint_(4, 9);
    bool kept = false;
    for (int k = -1; k <= run; k++)
      if (x + k >= X0 && x + k < X0 + Wd && keep[x + k - X0]) kept = true;
    if (kept) { level = 0; x += 2; continue; }
    int choice = (int)(rnd() % 100);
    if (flooded && level == 0 && choice >= 20) { /* a flooded hall: wading pools from wall to wall */
      if (run >= 4) fill(x + 1, floor_y, x + run - 2, floor_y, T_WATER);
      x += run;
      continue;
    }
    if (choice < 30) { /* a step */
      int nl = level + rint_(-2, 3);
      nl = nl < 0 ? 0 : nl > 3 ? 3 : nl;
      if (nl - level > 3) nl = level + 3;
      level = nl;
      if (level) fill(x, floor_y - level, x + run - 1, floor_y - 1, T_SOLID);
    } else if (choice < 48 && level == 0) { /* a thorn pit */
      int pw = rint_(2, 4);
      fill(x + 1, floor_y, x + pw, floor_y, T_SPIKE);
      run = pw + 2;
    } else if (choice < 58 && level == 0 && (region == R_FEN || region == R_TIDAL || region == R_GLASS || region == R_BLOOM)) {
      int pw = rint_(3, 6); /* a shallow pool */
      fill(x + 1, floor_y, x + pw, floor_y, T_WATER);
      run = pw + 2;
    } else if (level) {
      fill(x, floor_y - level, x + run - 1, floor_y - 1, T_SOLID);
    }
    x += run;
  }
  compose(ri, keep, false);
  /* platforms in the air: one-way ledges (some broken in two), and islands of rock with tapered undersides, never
   * under a door ledge */
  int n = rm->w * rm->h * 2 + rint_(0, 2);
  for (int k = 0; k < n; k++) {
    int pw = rint_(3, 7), px = rint_(ix0 + 3, ix1 - 3 - pw), py = floor_y - rint_(4, 7) - 3 * rint_(0, rm->h - 1) * 3 / 2;
    if (py < iy0 + 4) continue;
    bool clear = true;
    for (int yy = py - 3; yy <= py + 3 && clear; yy++)
      for (int xx = px - 1; xx <= px + pw; xx++)
        if (tile(xx, yy) != T_EMPTY) clear = false;
    if (!clear) continue;
    int kind = rint_(0, 9);
    if (kind < 5) fill(px, py, px + pw - 1, py, T_ONEWAY);
    else if (kind < 7 && pw >= 5) { /* a ledge broken in two */
      int gap = px + rint_(2, pw - 3);
      fill(px, py, gap - 1, py, T_ONEWAY);
      fill(gap + 1, py, px + pw - 1, py, T_ONEWAY);
    } else if (area_open(px - 1, py + 1, px + pw, py + 7)) { /* an island: flat top, the underside tapering to a point (room under it) */
      fill(px, py, px + pw - 1, py, T_SOLID);
      for (int r = 1, a = px, b = px + pw - 1; r <= 2 && b - a >= 2; r++) {
        a += 1 + (r == 1 && chance(50)), b -= 1 + (r == 1 && chance(50));
        if (b >= a) fill(a, py + r, b, py + r, T_SOLID);
      }
    }
  }
  /* columns down from the ceiling in tall rooms */
  if (rm->h == 1)
    for (int k = 0; k < rm->w; k++)
      if (chance(35)) {
        int cx = X0 + k * CELL_W + rint_(6, 24), cw = rint_(2, 4), len = rint_(2, 4);
        bool clear = true;
        for (int xx = cx - 1; xx <= cx + cw; xx++)
          for (int yy = iy0; yy <= iy0 + len + 6; yy++)
            if (tile(xx, yy) != T_EMPTY) clear = false;
        if (!clear) continue;
        fill(cx, iy0, cx + cw - 1, iy0 + len - 1, T_SOLID);
        if (cw >= 3) fill(cx + 1, iy0 + len, cx + cw - 2, iy0 + len, T_SOLID); /* a broken column's tapered end */
        /* its stump below, two high, on a stretch of level floor nobody needs kept */
        if (chance(45) && !kept(keep, X0, Wd, cx - 2, cx + cw + 1) && area_open(cx - 1, floor_y - 6, cx + cw, floor_y - 1)) {
          bool level = true;
          for (int xx = cx - 1; xx <= cx + cw; xx++) level = level && t_solid(tile(xx, floor_y));
          if (level) fill(cx, floor_y - 2, cx + cw - 1, floor_y - 1, T_SOLID);
        }
      }
}

/* the obstacle of a gate room, across its middle */
static void carve_gate(const gate_rec *g) {
  room *rm = &W->rooms[g->room];
  int X0 = rm->x * CELL_W, Y0 = rm->y * CELL_H;
  if (g->gate == G_POUND) { /* a cracked floor between the two cells, a ledge by the old side's doors */
    int fy = Y0 + CELL_H - 2 + 2; /* the boundary rows inside one room: top cell rows 16..17 and bottom 0..1 */
    fill(X0 + 2, Y0 + CELL_H - 2, X0 + CELL_W - 3, Y0 + CELL_H + 1, T_CRACKED);
    (void)fy;
    /* the ledges each side stay solid four tiles in, so the door into the top half has a floor once it breaks */
    fill(X0 + 2, Y0 + CELL_H - 2, X0 + 5, Y0 + CELL_H + 1, T_SOLID);
    fill(X0 + CELL_W - 6, Y0 + CELL_H - 2, X0 + CELL_W - 3, Y0 + CELL_H + 1, T_SOLID);
    /* a ladder up through the hole from the bottom floor (back the way you came, once it is open) */
    int floor_y = Y0 + 2 * CELL_H - 2;
    ladder(X0 + 2, X0 + CELL_W - 3, X0 + 12, floor_y, Y0 + CELL_H - 2, X0 + 6, X0 + 9);
    return;
  }
  if (g->gate == G_SEAL || g->gate == G_NONE) return;
  int floor_y = Y0 + CELL_H - 2, mid = X0 + CELL_W;
  switch (g->gate) {
  case G_DJUMP: /* a wall too high for one jump, in a tunnel so nothing reaches over it from above */
    fill(mid - 12, Y0 + 2, mid + 11, floor_y - 10, T_SOLID);
    fill(mid - 4, floor_y - 6, mid + 3, floor_y - 1, T_SOLID);
    break;
  case G_CLIMB: fill(mid - 3, Y0 + 5, mid + 2, floor_y - 1, T_SOLID); break;
  case G_DASH:
    fill(mid - 12, Y0 + 2, mid + 11, floor_y - 6, T_SOLID); /* a low tunnel over a long thorn pit */
    fill(mid - 5, floor_y, mid + 5, floor_y, T_SPIKE);
    break;
  case G_SWIM: fill(mid - 10, Y0 + 2, mid + 9, floor_y - 1, T_WATER); break;
  case G_GRAPPLE:
    fill(mid - 12, floor_y, mid + 11, floor_y, T_SPIKE);
    for (int k = 0; k < 4; k++) add_ent(EK_ANCHOR, 0, 0, HOLD_CEIL, g->room, (float)(mid - 9 + k * 6), (float)(Y0 + 4) + 0.5f);
    fill(mid - 12, Y0 + 2, mid + 11, Y0 + 3, T_SOLID); /* the beam the anchors hang from */
    break;
  case G_BOSS: break;
  }
}

/* ── placing things ──────────────────────────────────────────────────── */

/* a standing spot on the floor of a room near local x: feet y, or -1 */
static int surface_at(int ri, int x) {
  room *rm = &W->rooms[ri];
  int Y0 = rm->y * CELL_H, Ht = rm->h * CELL_H;
  for (int y = Y0 + Ht - 2; y > Y0 + 3; y--)
    if (t_solid(tile(x, y)) || tile(x, y) == T_ONEWAY) {
      if (tile(x, y - 1) == T_EMPTY && tile(x, y - 2) == T_EMPTY && tile(x, y - 3) == T_EMPTY) return y;
    }
  return -1;
}

static void populate(int ri) {
  room *rm = &W->rooms[ri];
  int X0 = rm->x * CELL_W, Y0 = rm->y * CELL_H, Wd = rm->w * CELL_W, Ht = rm->h * CELL_H;
  int region = rm->region;
  /* lamps: hanging from the ceiling, on walls, glowing things on the floor */
  int nl = rm->w * rm->h * 2 + 1;
  for (int k = 0; k < nl; k++) {
    int x = rint_(X0 + 3, X0 + Wd - 4);
    int style = (region * 3 + k) % 4;
    if (chance(50)) { /* hanging: below the first solid going up from the middle */
      int y = Y0 + rint_(3, Ht / 2);
      if (tile(x, y) != T_EMPTY) continue;
      while (y > Y0 && tile(x, y - 1) == T_EMPTY) y--;
      if (!t_solid(tile(x, y - 1))) continue;
      add_ent(EK_LAMP, style, rint_(2, 4), HOLD_CEIL, ri, (float)x + 0.5f, (float)y);
    } else {
      int y = surface_at(ri, x);
      if (y < 0 || tile(x, y) == T_ONEWAY) continue;
      add_ent(EK_LAMP, 4 + (region + k) % 3, 0, HOLD_FLOOR, ri, (float)x + 0.5f, (float)y);
    }
  }
  /* the region's set piece: big things standing on level floor (machines, trees, crystal clusters, kelp, shelves,
   * statues), three tiles of it or more, spaced out */
  static const uint8_t BIG[NREGIONS] = { [R_GATE] = PR_STATUE, [R_ROOTWAY] = PR_TREE, [R_GLASS] = PR_CLUSTER, [R_FEN] = PR_TREE,
                                         [R_CLOCK] = PR_MACHINE, [R_TIDAL] = PR_KELP, [R_STAR] = PR_CLUSTER, [R_SKY] = PR_STATUE,
                                         [R_ARCHIVE] = PR_SHELF, [R_FORGE] = PR_MACHINE, [R_FROST] = PR_CLUSTER, [R_BLOOM] = PR_TREE,
                                         [R_HEART] = PR_TREE };
  int nbig = rm->kind == RK_PLAIN || rm->kind == RK_HALL || rm->kind == RK_SHAFT ? rm->w * (region == R_FORGE || region == R_BLOOM ? 3 : 2) : 1;
  for (int k = 0, last = -99; k < nbig * 3 && nbig > 0; k++) {
    int x = rint_(X0 + 4, X0 + Wd - 5), y = surface_at(ri, x);
    if (y < 0 || abs(x - last) < 6 || !t_solid(tile(x, y))) continue;
    bool level = true;
    for (int dx = -1; dx <= 1; dx++) level = level && surface_at(ri, x + dx) == y && t_solid(tile(x + dx, y)) && tile(x + dx, y - 4) == T_EMPTY;
    if (!level) continue;
    add_ent(EK_PROP, BIG[region], (uint8_t)rint_(0, 255), HOLD_FLOOR, ri, (float)x + 0.5f, (float)y);
    last = x, nbig--;
  }
  /* each region's complementary accent on the floor: small blue pilot flames among the Forge's machines, warm
   * lantern posts in the cool regions */
  bool warm = region == R_FORGE || region == R_CLOCK, cool = region == R_GLASS || region == R_TIDAL || region == R_FROST ||
              region == R_STAR || region == R_SKY || region == R_GATE || region == R_ARCHIVE || region == R_FEN;
  if (warm || cool)
    for (int k = 0; k < rm->w * 2; k++) {
      int x = rint_(X0 + 3, X0 + Wd - 4), y = surface_at(ri, x);
      if (y > 0 && t_solid(tile(x, y)) && tile(x, y - 3) == T_EMPTY) add_ent(EK_LAMP, warm ? 8 : 9, 0, HOLD_FLOOR, ri, (float)x + 0.5f, (float)y);
    }
  /* pipes and chains along the ceiling in the Forge and the Clockwork Deep */
  if (region == R_FORGE || region == R_CLOCK || region == R_ARCHIVE)
    for (int x = X0 + 3; x < X0 + Wd - 3; x++)
      for (int yy = Y0 + 2; yy < Y0 + Ht - 4; yy++)
        if (tile(x, yy) == T_EMPTY && t_solid(tile(x, yy - 1))) {
          if (chance(7)) add_ent(EK_PROP, PR_PIPE, (uint8_t)rint_(1, 4), HOLD_CEIL, ri, (float)x + 0.5f, (float)yy);
          break;
        }
  /* props on the floor and the ceiling: gardens grow thick */
  int dense = region == R_BLOOM || region == R_FEN || region == R_ROOTWAY ? 34 : 22;
  for (int x = X0 + 2; x < X0 + Wd - 2; x++) {
    int y = surface_at(ri, x);
    if (y > 0 && t_solid(tile(x, y)) && chance(dense)) add_ent(EK_PROP, (uint8_t)rint_(0, 5), 0, HOLD_FLOOR, ri, (float)x + 0.5f, (float)y);
    for (int yy = Y0 + 2; yy < Y0 + Ht - 4; yy++)
      if (tile(x, yy) == T_EMPTY && t_solid(tile(x, yy - 1)) && chance(6)) {
        add_ent(EK_PROP, (uint8_t)(6 + rint_(0, 2)), (uint8_t)rint_(2, 6), HOLD_CEIL, ri, (float)x + 0.5f, (float)yy);
        break;
      }
  }
  if (rm->kind != RK_PLAIN && rm->kind != RK_SHAFT && rm->kind != RK_HALL) return;
  /* enemies: each region has its own cast (four kinds, the first two commonest), so a new region means new fights:
   * ground walkers on long floors, spitters and eyes on ledges, moths and puffs in the air, droppers on ceilings */
  static const uint8_t CAST[NREGIONS][4] = {
    [R_GATE] = { EN_CRAWLER, EN_FLYER, EN_SPITTER, EN_PUFF },     [R_ROOTWAY] = { EN_CRAWLER, EN_PUFF, EN_SPITTER, EN_FLYER },
    [R_GLASS] = { EN_DROPPER, EN_HOPPER, EN_FLYER, EN_EYE },      [R_FEN] = { EN_BURROWER, EN_SPITTER, EN_PUFF, EN_HOPPER },
    [R_CLOCK] = { EN_EYE, EN_KNIGHT, EN_DROPPER, EN_CRAWLER },    [R_TIDAL] = { EN_PUFF, EN_BURROWER, EN_SPITTER, EN_FLYER },
    [R_STAR] = { EN_EYE, EN_FLYER, EN_BURROWER, EN_KNIGHT },      [R_SKY] = { EN_FLYER, EN_PUFF, EN_KNIGHT, EN_EYE },
    [R_ARCHIVE] = { EN_KNIGHT, EN_DROPPER, EN_EYE, EN_FLYER },    [R_FORGE] = { EN_CRAWLER, EN_EYE, EN_BURROWER, EN_KNIGHT },
    [R_FROST] = { EN_DROPPER, EN_HOPPER, EN_KNIGHT, EN_BURROWER }, [R_BLOOM] = { EN_HOPPER, EN_PUFF, EN_BURROWER, EN_SPITTER },
    [R_HEART] = { EN_KNIGHT, EN_BURROWER, EN_EYE, EN_FLYER },
  };
  int ne = rm->w * rm->h + rint_(0, 1) + (region >= R_CLOCK);
  for (int k = 0; k < ne; k++) {
    int x = rint_(X0 + 8, X0 + Wd - 9), y = surface_at(ri, x);
    if (y < 0) continue;
    uint32_t r4 = rnd() % 10u;
    if (region == R_GATE || region == R_ROOTWAY) (void)rnd(); /* (the draw the old cast took here: the rest of the world stays as it was) */
    int kind = CAST[region][r4 < 4 ? 0 : r4 < 7 ? 1 : r4 < 9 ? 2 : 3];
    if (kind == EN_PUFF) { /* a puff drifts in open air, clear of anywhere to stand (its thorny breath must not sit on a ledge) */
      int hy = y - 6;
      bool open = true;
      for (int yy = hy - 3; yy <= hy + 4 && open; yy++)
        for (int xx = x - 5; xx <= x + 5 && open; xx++) open = tile(xx, yy) == T_EMPTY;
      if (!open) kind = EN_FLYER;
      else { add_ent(EK_ENEMY, EN_PUFF, 0, 0, ri, (float)x + 0.5f, (float)hy); continue; }
    }
    if (kind == EN_FLYER) add_ent(EK_ENEMY, (uint8_t)kind, 0, 0, ri, (float)x + 0.5f, (float)(y - 5));
    else if (kind == EN_DROPPER) { /* on the ceiling over the floor, with room to fall */
      int cy = y - 4;
      while (cy > Y0 + 1 && !t_solid(tile(x, cy - 1))) cy--;
      if (y - cy >= 5 && t_solid(tile(x, cy - 1))) add_ent(EK_ENEMY, EN_DROPPER, 0, HOLD_CEIL, ri, (float)x + 0.5f, (float)cy + 1.0f);
      else if (t_solid(tile(x, y))) add_ent(EK_ENEMY, EN_CRAWLER, 0, HOLD_FLOOR, ri, (float)x + 0.5f, (float)y);
    } else if (t_solid(tile(x, y))) add_ent(EK_ENEMY, (uint8_t)kind, 0, HOLD_FLOOR, ri, (float)x + 0.5f, (float)y);
  }
}

/* a pickup on a pedestal in the middle of an item room */
static void item_room(int ri, int pk, int arg) {
  room *rm = &W->rooms[ri];
  int cx = rm->x * CELL_W + CELL_W / 2, fy = rm->y * CELL_H + CELL_H - 2;
  /* (its pedestal is laid after carving: pedestals() below) */
  add_ent(EK_PICKUP, (uint8_t)pk, (uint8_t)arg, HOLD_FLOOR, ri, (float)cx, (float)(fy - 1));
  add_ent(EK_LAMP, 7, 3, HOLD_FLOOR, ri, (float)cx - 4.5f, (float)fy);
  add_ent(EK_LAMP, 7, 3, HOLD_FLOOR, ri, (float)cx + 4.5f, (float)fy);
}

/* ── the whole world ─────────────────────────────────────────────────── */

void world_generate(world *w, uint32_t seed) {
  W = w;
  memset(w, 0, sizeof *w);
  w->seed = seed;
  rng = seed ? seed : 1;
  for (int i = 0; i < 8; i++) rnd();
  memset(w->t, T_SOLID, sizeof w->t);
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) w->cell_room[y][x] = 0xffff, creg[y][x] = -1;
  memset(used, 0, sizeof used);
  for (int r = 0; r < NREGIONS; r++)
    for (int y = REGIONS[r].y; y < REGIONS[r].y + REGIONS[r].h; y++)
      for (int x = REGIONS[r].x; x < REGIONS[r].x + REGIONS[r].w; x++) creg[y][x] = (int8_t)r;
  nports = ngates = 0;
  /* links */
  for (int i = 0; i < NLINKS; i++) place_link_gate(LINKS[i].from, LINKS[i].to, LINKS[i].gate);
  /* set pieces */
  int start_room = -1;
  for (int r = 0; r < NREGIONS; r++) {
    const region_plan *p = &PLAN[r];
    int rooms[4], ex, ey;
    if (r == R_GATE) {
      piece_room s[1] = { { 0, 0, 2, 1, RK_START } };
      start_room = place_piece(r, s, 1, rooms, &ex, &ey);
    }
    if (p->boss) { /* the warden's arena, and the prize room behind its sealed far door */
      piece_room b[2] = { { 0, 0, 2, 1, RK_BOSS }, { 2, 0, 1, 1, RK_ITEM } };
      bool has_prize = p->prize != 0;
      if (place_piece(r, b, has_prize ? 2 : 1, rooms, &ex, &ey) >= 0) {
        W->rooms[rooms[0]].boss = (uint8_t)p->boss;
        if (has_prize) {
          int cx = W->rooms[rooms[0]].x + 1, cy = W->rooms[rooms[0]].y;
          add_door(cx, cy, cx + 1, cy, T_BOSSDOOR);
          item_room(rooms[1], PK_ABILITY, p->prize);
        }
        int bx = W->rooms[rooms[0]].x * CELL_W + CELL_W, by = W->rooms[rooms[0]].y * CELL_H + CELL_H - 2;
        add_ent(EK_BOSS, (uint8_t)(p->boss - 1), 0, HOLD_FLOOR, rooms[0], (float)bx, (float)by);
      } else fprintf(stderr, "worldgen: no room for the %s warden\n", REGIONS[r].name);
    } else if (p->prize) {
      piece_room it[1] = { { 0, 0, 1, 1, RK_ITEM } };
      if (place_piece(r, it, 1, rooms, &ex, &ey) >= 0) item_room(rooms[0], PK_ABILITY, p->prize);
    }
    for (int s = 0; s < p->saves; s++) {
      piece_room sv[1] = { { 0, 0, 1, 1, RK_SAVE } };
      if (place_piece(r, sv, 1, rooms, &ex, &ey) >= 0) {
        room *rm = &W->rooms[rooms[0]];
        add_ent(EK_SAVE, 0, 0, HOLD_FLOOR, rooms[0], (float)(rm->x * CELL_W + CELL_W / 2), (float)(rm->y * CELL_H + CELL_H - 2));
      }
    }
    for (int b = 0; b < 3 && p->branch_gate[b]; b++) { /* a gate, and an upgrade behind it */
      piece_room br[2] = { { 0, 0, 2, 1, RK_GATE }, { 2, 0, 1, 1, RK_ITEM } };
      if (p->branch_gate[b] == G_POUND) br[0] = (piece_room){ 0, 0, 1, 2, RK_GATE }, br[1] = (piece_room){ 1, 1, 1, 1, RK_ITEM };
      if (place_piece(r, br, 2, rooms, &ex, &ey) >= 0) {
        W->rooms[rooms[0]].gate = (uint8_t)p->branch_gate[b];
        room *g = &W->rooms[rooms[0]];
        /* the entry door is on the first cell's side (place_piece chose it): that cell is the old half */
        gates[ngates++] = (gate_rec){ rooms[0], p->branch_gate[b], g->x, g->y };
        if (p->branch_gate[b] == G_POUND) add_door(g->x, g->y + 1, g->x + 1, g->y + 1, 0);
        else add_door(g->x + 1, g->y, g->x + 2, g->y, 0);
        item_room(rooms[1], p->branch_pk[b], 0);
      }
    }
    for (int s = 0; s < p->secrets; s++) { /* a hidden wall, and a relic behind it */
      piece_room it[1] = { { 0, 0, 1, 1, RK_SECRET } };
      if (place_piece(r, it, 1, rooms, &ex, &ey) >= 0) {
        item_room(rooms[0], PK_RELIC, 0);
        W->rooms[rooms[0]].kind = RK_SECRET;
      }
    }
  }
  /* grow each region round its ports, then cut it into rooms */
  for (int r = 0; r < NREGIONS; r++) grow_region(r);
  /* the ports' doors: set pieces to the rooms grown next to them */
  for (int i = 0; i < nports; i++) {
    port *p = &ports[i];
    if (W->cell_room[p->py][p->px] == 0xffff) { fprintf(stderr, "worldgen: port at %d,%d has no room\n", p->px, p->py); continue; }
    int a = W->cell_room[p->cy][p->cx];
    uint8_t fillt = 0;
    if (W->rooms[a].kind == RK_SECRET) fillt = T_SECRET;
    if (W->rooms[a].gate == G_SEAL) fillt = T_SEAL;
    add_door(p->cx, p->cy, p->px, p->py, fillt);
  }
  /* doors listed by room */
  for (int i = 0; i < W->nrooms; i++) {
    W->rooms[i].first_door = 0, W->rooms[i].ndoors = 0;
  }
  /* carve */
  for (int i = 0; i < W->nrooms; i++) carve_room(i);
  for (int i = 0; i < ngates; i++) carve_gate(&gates[i]);
  /* boss arenas: a flat floor and two ledges */
  for (int i = 0; i < W->nrooms; i++) {
    room *rm = &W->rooms[i];
    if (rm->kind != RK_BOSS) continue;
    int X0 = rm->x * CELL_W, fy = rm->y * CELL_H + CELL_H - 2;
    fill(X0 + 12, fy - 5, X0 + 19, fy - 5, T_ONEWAY);
    fill(X0 + 44, fy - 5, X0 + 51, fy - 5, T_ONEWAY);
  }
  for (int i = 0; i < W->nents; i++) /* pedestals under the pickups */
    if (W->ents[i].kind == EK_PICKUP) fill((int)W->ents[i].x - 1, (int)W->ents[i].y, (int)W->ents[i].x, (int)W->ents[i].y, T_SOLID);
  for (int i = 0; i < W->nrooms; i++) populate(i);
  if (start_room >= 0) {
    room *rm = &W->rooms[start_room];
    w->start_x = (float)(rm->x * CELL_W + 12) + 0.5f;
    w->start_y = (float)(rm->y * CELL_H + CELL_H - 2);
    add_ent(EK_START, 0, 0, HOLD_FLOOR, start_room, w->start_x, w->start_y);
  }
  w->hash = world_hash(w);
}
