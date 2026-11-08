/* towers rules: 1-4 players hold one map against shared waves (DESIGN.md).
 *
 *   creeps    every creep is an agent of the engine's crowd on the map's navmesh (its path tiles), steered by one
 *             flow field from the core along the path. Waves come out of the gates in groups, one after another on
 *             a timer (a player can call the next one early for gold), so late waves overlap into thousands.
 *   players   a cursor each that hops between the build slots of their own share of the map (the d-pad or stick);
 *             the four face buttons build the four towers on an empty slot (a diamond: north bolt, west frost, east
 *             beam, south mortar); on a tower of theirs south upgrades, north changes its aim, a held west sells it.
 *   towers    bolt (chain lightning), frost (a slowing, freezing pulse), beam (a ramping beam on one creep), mortar
 *             (a lobbed shell and a blast). Each has three levels. Kills pay the tower's owner; every wave pays all.
 * Everything here runs on the fixed tick with cartwheel math and cw_rng: the same inputs give the same game on every
 * host. */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/profile.h"
#include "game.h"

static float rf(sim *s) { return cw_rng_float(&s->rng); }
static void ev(sim *s, int kind, float x, float y, float z, float w, int a, int b) {
  if (s->ev_n >= MAXEV) return;
  s->ev[s->ev_n++] = (event){ (uint8_t)kind, (uint8_t)a, (uint8_t)b, x, y, z, w };
}

/* ── tables ─────────────────────────────────────────────────────────── */

static const char *const KIND_NAME[CK_KINDS] = { "skitter", "ghoul", "hound", "imp", "sporeling", "bloater", "shade", "ogre" };
const char *sim_kind_name(int k) { return k >= 0 && k < CK_KINDS ? KIND_NAME[k] : "?"; }
int sim_kind_by_name(const char *name) {
  for (int k = 0; k < CK_KINDS; k++)
    if (!strcmp(name, KIND_NAME[k])) return k;
  return -1;
}
static const char *const TOWER_NAME[TK_KINDS] = { "BOLT", "FROST", "BEAM", "MORTAR" };
const char *sim_tower_name(int k) { return TOWER_NAME[(unsigned)k % TK_KINDS]; }
static const char *const BRANCH_NAME[TK_KINDS][2] = { { "STORM", "ARC" }, { "SHATTER", "BLIZZARD" }, { "LANCE", "PRISM" }, { "CLUSTER", "NAPALM" } };
static const char *const BRANCH_DOES[TK_KINDS][2] = { { "eight leaps", "stuns all it strikes" }, { "every third pulse freezes", "wide, deep cold" },
                                                      { "pierces the line", "splits on to two more" }, { "four bomblets", "the path burns" } };
const char *sim_branch_name(int kind, int branch) { return branch == BR_B ? BRANCH_NAME[(unsigned)kind % TK_KINDS][1] : BRANCH_NAME[(unsigned)kind % TK_KINDS][0]; }
const char *sim_branch_does(int kind, int branch) { return branch == BR_B ? BRANCH_DOES[(unsigned)kind % TK_KINDS][1] : BRANCH_DOES[(unsigned)kind % TK_KINDS][0]; }
static const char *const AIM_NAME[AIM_KINDS] = { "first", "strongest", "last" };
const char *sim_aim_name(int a) { return AIM_NAME[(unsigned)a % AIM_KINDS]; }

/* cost to reach the level, range (m), damage (a hit; the beam: a second), period (ticks), extra (bolt: hops, frost:
 * slow in percent, beam: 1 pierces, mortar: bomblets) */
const tower_stats TOWER[TK_KINDS] = {
  { { 100, 130, 210 }, { 7.5f, 8.0f, 8.5f }, { 14, 26, 44 }, { 50, 44, 38 }, { 3, 5, 8 } },
  { { 80, 110, 170 }, { 5.5f, 6.0f, 6.5f }, { 2, 5, 10 }, { 80, 72, 64 }, { 40, 50, 60 } },
  { { 120, 160, 250 }, { 8.0f, 8.5f, 9.0f }, { 26, 48, 84 }, { 1, 1, 1 }, { 0, 0, 1 } },
  { { 150, 190, 290 }, { 10.0f, 11.0f, 12.0f }, { 46, 82, 140 }, { 150, 136, 120 }, { 0, 0, 4 } },
};
int sim_tower_cost(int kind, int level) { return TOWER[(unsigned)kind % TK_KINDS].cost[CW_CLAMP(level, 1, 3) - 1]; }
float sim_tower_range(int kind, int level) { return TOWER[(unsigned)kind % TK_KINDS].range[CW_CLAMP(level, 1, 3) - 1]; }
int sim_sell_value(const tower *t) { return t->spent * 7 / 10; }
static const float SPLASH[3] = { 2.2f, 2.6f, 3.0f };
#define MORTAR_MIN 2.5f
#define MORTAR_T 70
#define BOLT_HOP 3.6f
#define SELL_HOLD 36
/* the third level's B branches (DESIGN.md, towers): arc (3 leaps, harder strikes, each creep struck stands stunned),
 * blizzard (no freeze: a wider, deeper, longer cold), prism (the beam splits from its creep on to two more near it),
 * napalm (no bomblets: the blast leaves the path burning) */
#define ARC_LEAPS 3
#define ARC_DMG 52.0f
#define ARC_STUN 20
#define BLIZZARD_RANGE 9.0f
#define BLIZZARD_SLOW 68
#define BLIZZARD_DMG 7.0f
#define PRISM_MAIN 0.75f
#define PRISM_SPLIT 0.5f
#define PRISM_REACH 3.5f
#define NAPALM_R 2.6f
#define NAPALM_DPS 26.0f
#define NAPALM_T 180
/* synergies (DESIGN.md, towers): a mixed defence beats four of one kind. Blasts and fire hit a frozen or stunned creep
 * half again as hard (it shatters); a beam heats twice as fast on a slowed creep; lightning leaps farther off a chilled
 * one */
#define SHATTER_K 1.5f
#define CHILL_HOP 4.8f
float sim_tower_reach(const tower *t) { return t->kind == TK_FROST && t->level >= 3 && t->branch == BR_B ? BLIZZARD_RANGE : sim_tower_range(t->kind, t->level); }

/* health, speed (m/s), bounty, what a leak costs the core, armour (taken off every hit but the beam's), crowd radius */
typedef struct ckind { float hp, speed; int bounty, leak; float armor, radius; } ckind;
static const ckind CK[CK_KINDS] = {
  { 8, 3.4f, 1, 1, 0, 0.30f },     /* skitter: the swarm */
  { 22, 2.4f, 2, 1, 0, 0.38f },    /* ghoul */
  { 16, 3.9f, 2, 1, 0, 0.40f },    /* hound: quick */
  { 28, 2.7f, 3, 1, 3, 0.36f },    /* imp: armoured */
  { 40, 2.0f, 4, 1, 0, 0.42f },    /* sporeling: heals those round it */
  { 75, 1.5f, 5, 2, 0, 0.52f },    /* bloater: bursts into skitters */
  { 24, 3.1f, 3, 1, 0, 0.36f },    /* shade: shrugs off half the cold, never freezes */
  { 230, 1.3f, 15, 3, 6, 0.74f },  /* ogre: the heavy */
};
float sim_creep_radius(int kind) { return CK[CW_CLAMP(kind, 0, CK_KINDS - 1)].radius; }

/* ── the map's grid ─────────────────────────────────────────────────── */

static int flow_cell(const sim *s, float x, float y) {
  int cx = (int)(x + (float)s->m.w + 1000.0f) - 1000, cy = (int)(y + (float)s->m.h + 1000.0f) - 1000;
  if (cx < 0 || cy < 0 || cx >= s->fw || cy >= s->fh) return -1;
  return cy * FGW + cx;
}
static void build_flow(sim *s) {
  memset(s->blocked, 1, sizeof s->blocked);
  for (int cy = 0; cy < s->fh; cy++)
    for (int cx = 0; cx < s->fw; cx++) s->blocked[cy * FGW + cx] = !map_walkable(&s->m, ((float)cx + 0.5f) * 0.5f, ((float)cy + 0.5f) * 0.5f); /* the floor's cells */
  static int queue[FGW * FGH];
  for (int i = 0; i < FGW * FGH; i++) s->flow[i] = 0xffff;
  int qh = 0, qt = 0;
  for (int k = 0; k < 4; k++) { /* the core's tile: its four cells */
    int c = flow_cell(s, s->core_x + (k & 1 ? 0.5f : -0.5f), s->core_y + (k & 2 ? 0.5f : -0.5f));
    if (c >= 0 && s->flow[c]) s->flow[c] = 0, queue[qt++] = c;
  }
  static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  while (qh < qt) {
    int c = queue[qh++], cx = c % FGW, cy = c / FGW;
    uint16_t d = s->flow[c];
    for (int k = 0; k < 8; k++) {
      int nx = cx + DX[k], ny = cy + DY[k];
      if (nx < 0 || ny < 0 || nx >= s->fw || ny >= s->fh) continue;
      int n = ny * FGW + nx;
      if (s->blocked[n] || s->flow[n] != 0xffff) continue;
      if (k >= 4 && (s->blocked[cy * FGW + nx] || s->blocked[ny * FGW + cx])) continue; /* no cutting corners */
      s->flow[n] = (uint16_t)(d + (k < 4 ? 2 : 3));
      queue[qt++] = n;
    }
  }
}
/* the downhill step of the flow field from (x, y): a unit direction (false when none) */
static bool flow_step(const sim *s, float x, float y, float *dx, float *dy) {
  int c = flow_cell(s, x, y);
  if (c < 0) return false;
  int cx = c % FGW, cy = c / FGW, bx = cx, by = cy;
  uint16_t best = s->flow[c];
  bool off = s->blocked[c]; /* pushed over the path's edge by the crowd: back to the nearest open cell */
  for (int r = 1; r <= (off ? 3 : 1) && bx == cx && by == cy; r++)
    for (int oy = -r; oy <= r; oy++)
      for (int ox = -r; ox <= r; ox++) {
        int nx = cx + ox, ny = cy + oy;
        if (nx < 0 || ny < 0 || nx >= s->fw || ny >= s->fh || (!ox && !oy) || (abs(ox) != r && abs(oy) != r)) continue;
        if (!off && ox && oy && (s->blocked[cy * FGW + nx] || s->blocked[ny * FGW + cx])) continue;
        uint16_t d = s->flow[ny * FGW + nx];
        if (d < best) best = d, bx = nx, by = ny;
      }
  if (bx == cx && by == cy) return false;
  float tx = (float)bx + 0.5f - (float)s->m.w - x, ty = (float)by + 0.5f - (float)s->m.h - y, l = sqrtf(tx * tx + ty * ty);
  if (l < 1e-4f) return false;
  *dx = tx / l, *dy = ty / l;
  return true;
}
static bool on_path(const sim *s, float x, float y) {
  int tx = mp_tx(&s->m, x), ty = mp_ty(&s->m, y);
  return mp_in(&s->m, tx, ty) && s->m.t[ty][tx] == T_PATH;
}

/* ── the navmesh: the floor of the map's path tiles (map_walkable), in sixths of a tile ───────────────── */

static void nav_build(sim *s) {
  if (!CW_HANDLE_IS_NULL(s->nav)) cw_navmesh_free(s->nav);
  cw_nav_geom g = { 0 };
  const tdmap *m = &s->m;
  enum { SUB = 6 };
  const float cs = MP_TILE / SUB;
  for (int j = 0; j < m->h * SUB; j++) {
    int i = 0;
    while (i < m->w * SUB) {
      if (!map_walkable(m, ((float)i + 0.5f) / SUB, ((float)j + 0.5f) / SUB)) { i++; continue; }
      int i0 = i;
      while (i < m->w * SUB && map_walkable(m, ((float)i + 0.5f) / SUB, ((float)j + 0.5f) / SUB)) i++;
      float cx = ((float)(i0 + i) * 0.5f / SUB - (float)m->w * 0.5f) * MP_TILE, cz = (((float)j + 0.5f) / SUB - (float)m->h * 0.5f) * MP_TILE;
      cw_nav_geom_add_box(&g, (t3_vec3){ cx, -0.25f, cz }, (t3_vec3){ (float)(i - i0) * cs * 0.5f, 0.25f, cs * 0.5f }, CW_NAV_AREA_WALK);
    }
  }
  cw_nav_config c = cw_nav_config_default();
  c.cell_size = 0.25f;
  c.cell_height = 0.2f;
  c.agent_radius = 0.25f;
  c.agent_height = 1.6f;
  c.tile_size = 64;
  c.region_min_size = 2;
  s->nav = cw_navmesh_build(&c, &g);
  cw_nav_geom_free(&g);
  s->crowd = cw_crowd_new(s->nav, MAXE);
  cw_crowd_set_plan_budget(s->crowd, 8, 0);
}

/* ── creeps ─────────────────────────────────────────────────────────── */

static int spawn_creep(sim *s, int kind, float hp, float x, float y, int bounty) {
  int slot = -1;
  for (int i = 0; i < MAXE; i++)
    if (!s->e[i].alive) { slot = i; break; }
  if (slot < 0 || !on_path(s, x, y)) return -1;
  cw_crowd_agent_params ap = cw_crowd_agent_params_default();
  ap.radius = CK[kind].radius;
  ap.height = 1.4f;
  float sp = CK[kind].speed * (0.92f + 0.16f * rf(s));
  ap.max_speed = sp * 1.5f;
  ap.max_accel = 12;
  ap.neighbor_dist = 2.0f;
  ap.max_neighbors = 5;
  ap.time_horizon = 0.5f;
  ap.avoidance_priority = kind == CK_OGRE ? 0.9f : 0.5f;
  cw_crowd_agent a = cw_crowd_add(s->crowd, (t3_vec3){ x, 0, y }, &ap);
  if (CW_HANDLE_IS_NULL(a)) return -1;
  creep *e = &s->e[slot];
  *e = (creep){ .alive = true, .kind = (uint8_t)kind, .hp = hp, .maxhp = hp, .x = x, .y = y, .agent = a, .speed = sp, .bounty = bounty, .leak = CK[kind].leak };
  e->face = rf(s) * 6.28f;
  e->heal_cd = 30 + (int)(rf(s) * 30);
  int c = flow_cell(s, x, y);
  e->prog = e->best = c >= 0 ? s->flow[c] : 0xffff;
  s->nenemies++;
  s->spawned_total++;
  return slot;
}
int sim_spawn(sim *s, int kind, float hp, float x, float y) { return spawn_creep(s, CW_CLAMP(kind, 0, CK_KINDS - 1), hp, x, y, 1); }

static void remove_creep(sim *s, int i) {
  creep *e = &s->e[i];
  if (!e->alive) return;
  e->alive = false;
  cw_crowd_remove(s->crowd, e->agent);
  s->nenemies--;
}

/* a hit: the tower that dealt it (its slot) and whether armour counts */
static void damage(sim *s, int i, float amount, int slot, bool pierce_armor) {
  creep *e = &s->e[i];
  if (!e->alive || amount <= 0) return;
  if (!pierce_armor && CK[e->kind].armor > 0) amount = CW_MAX(amount - CK[e->kind].armor, amount * 0.25f);
  e->hp -= amount;
  if (e->flash < 3) e->flash = 3;
  tower *t = slot >= 0 && slot < MAXT ? &s->tw[slot] : NULL;
  if (t && t->alive) t->dmg += amount, s->p[t->owner].dmg += amount;
  if (e->hp > 0) return;
  /* dead: the owner is paid; a bloater bursts into skitters */
  int owner = t && t->alive ? t->owner : -1;
  s->pot += e->bounty, s->gold_total += e->bounty;
  if (owner >= 0) s->p[owner].kills++, t->kills++;
  s->kills++;
  ev(s, EV_KILL, e->x, e->y, e->face, e->boss ? 1.0f : 0.0f, e->kind, owner < 0 ? 255 : owner);
  float x = e->x, y = e->y, hp = e->maxhp;
  int kind = e->kind;
  remove_creep(s, i);
  if (kind == CK_BLOATER) {
    ev(s, EV_SPLIT, x, y, 0, 0, 0, 0);
    for (int k = 0; k < 4; k++) {
      float a = (float)k * 1.5708f + 0.4f, ox = cw_cos(a) * 0.5f, oy = cw_sin(a) * 0.5f;
      float px = on_path(s, x + ox, y + oy) ? x + ox : x, py = on_path(s, x + ox, y + oy) ? y + oy : y;
      int c = spawn_creep(s, CK_SKITTER, hp * CK[CK_SKITTER].hp / CK[CK_BLOATER].hp, px, py, 1);
      if (c >= 0) s->spawned_total--; /* (not a new arrival) */
    }
  }
}

/* ── the hit grid ───────────────────────────────────────────────────── */

static void build_grid(sim *s) {
  static int16_t cnt[MP_MAX_W * MP_MAX_H + 1];
  const int W = s->m.w, H = s->m.h;
  memset(cnt, 0, sizeof(int16_t) * (size_t)(W * H + 1));
  for (int i = 0; i < MAXE; i++) {
    if (!s->e[i].alive) continue;
    int cx = CW_CLAMP(mp_tx(&s->m, s->e[i].x), 0, W - 1), cy = CW_CLAMP(mp_ty(&s->m, s->e[i].y), 0, H - 1);
    cnt[cy * W + cx + 1]++;
  }
  for (int i = 1; i <= W * H; i++) cnt[i] = (int16_t)(cnt[i] + cnt[i - 1]);
  memcpy(s->cell_start, cnt, sizeof(int16_t) * (size_t)(W * H + 1));
  static int16_t fill[MP_MAX_W * MP_MAX_H];
  memcpy(fill, cnt, sizeof(int16_t) * (size_t)(W * H));
  for (int i = 0; i < MAXE; i++) {
    if (!s->e[i].alive) continue;
    int cx = CW_CLAMP(mp_tx(&s->m, s->e[i].x), 0, W - 1), cy = CW_CLAMP(mp_ty(&s->m, s->e[i].y), 0, H - 1);
    s->cell_idx[fill[cy * W + cx]++] = (int16_t)i;
  }
}
/* the creeps within r of (x, y), each once, in grid order: their slots into out (at most cap); returns the count */
static int creeps_near(const sim *s, float x, float y, float r, int *out, int cap) {
  const int W = s->m.w, H = s->m.h;
  int x0 = CW_CLAMP(mp_tx(&s->m, x - r - 1), 0, W - 1), x1 = CW_CLAMP(mp_tx(&s->m, x + r + 1), 0, W - 1);
  int y0 = CW_CLAMP(mp_ty(&s->m, y - r - 1), 0, H - 1), y1 = CW_CLAMP(mp_ty(&s->m, y + r + 1), 0, H - 1);
  int n = 0;
  for (int cy = y0; cy <= y1; cy++)
    for (int cx = x0; cx <= x1; cx++)
      for (int k = s->cell_start[cy * W + cx]; k < s->cell_start[cy * W + cx + 1] && n < cap; k++) {
        int i = s->cell_idx[k];
        const creep *e = &s->e[i];
        if (!e->alive) continue;
        float dx = e->x - x, dy = e->y - y, rr = r + CK[e->kind].radius * 0.5f;
        if (dx * dx + dy * dy <= rr * rr) out[n++] = i;
      }
  return n;
}
/* the creep a tower aims at in its range (-1 none) */
static int pick_target(const sim *s, const tower *t, float r, float rmin) {
  static int buf[MAXE];
  int n = creeps_near(s, t->x, t->y, r, buf, MAXE), best = -1;
  float bv = 0;
  for (int k = 0; k < n; k++) {
    const creep *e = &s->e[buf[k]];
    if (rmin > 0 && (e->x - t->x) * (e->x - t->x) + (e->y - t->y) * (e->y - t->y) < rmin * rmin) continue;
    float v = t->aim == AIM_STRONG ? e->hp + (e->boss ? 1e6f : 0) : t->aim == AIM_LAST ? (float)e->prog : -(float)e->prog;
    if (best < 0 || v > bv || (v == bv && buf[k] < best)) bv = v, best = buf[k];
  }
  return best;
}

/* ── towers ─────────────────────────────────────────────────────────── */

static void slow(creep *e, float k, int ticks) {
  if (e->kind == CK_SHADE) k *= 0.5f;
  if (k >= e->slow_k || e->slow_t <= 0) e->slow_k = k;
  e->slow_t = CW_MAX(e->slow_t, ticks);
}

static void tower_bolt(sim *s, int slot, tower *t) {
  const tower_stats *ts = &TOWER[TK_BOLT];
  int L = t->level - 1;
  int first = pick_target(s, t, ts->range[L], 0);
  if (first < 0) return;
  t->cd = ts->period[L];
  t->fired++;
  s->zaps_total++;
  static int hit[16];
  bool arc = t->level >= 3 && t->branch == BR_B;
  int nh = 0, cur = first, leaps = arc ? ARC_LEAPS : ts->extra[L];
  float px = t->x, py = t->y, dmg = arc ? ARC_DMG : ts->dmg[L];
  for (int hop = 0; hop <= leaps && cur >= 0; hop++) {
    creep *e = &s->e[cur];
    float ex = e->x, ey = e->y, hop_r = e->slow_t > 0 ? CHILL_HOP : BOLT_HOP; /* (read before the strike: it may die) */
    ev(s, EV_ZAP, px, py, ex, ey, t->level, hop);
    hit[nh++] = cur;
    if (arc && !e->boss && e->kind != CK_SHADE) {
      if (e->stun <= 0) ev(s, EV_STUN, ex, ey, 0, 0, e->kind, 0);
      e->stun = ARC_STUN;
    }
    damage(s, cur, dmg * (e->kind == CK_SHADE ? 1.5f : 1.0f), slot, false);
    dmg *= 0.86f;
    px = ex, py = ey;
    /* the next: the nearest creep within a hop not struck yet */
    static int buf[512];
    int n = creeps_near(s, px, py, hop_r, buf, 512), nx = -1;
    float bd = 1e9f;
    for (int k = 0; k < n; k++) {
      bool done = false;
      for (int j = 0; j < nh; j++) done |= hit[j] == buf[k];
      if (done) continue;
      float dx = s->e[buf[k]].x - px, dy = s->e[buf[k]].y - py, d = dx * dx + dy * dy;
      if (d < bd) bd = d, nx = buf[k];
    }
    cur = nh < 16 ? nx : -1;
  }
}

static void tower_frost(sim *s, int slot, tower *t) {
  const tower_stats *ts = &TOWER[TK_FROST];
  int L = t->level - 1;
  bool blizzard = t->level >= 3 && t->branch == BR_B;
  float r = sim_tower_reach(t);
  static int buf[MAXE];
  int n = creeps_near(s, t->x, t->y, r, buf, MAXE);
  if (!n) return;
  t->cd = ts->period[L];
  t->fired++, t->pulses++;
  s->frost_total++;
  bool freeze = t->level >= 3 && !blizzard && t->pulses % 3 == 0;
  ev(s, EV_FROST, t->x, t->y, r, blizzard ? 1.0f : 0.0f, t->level, freeze);
  float k = (float)(blizzard ? BLIZZARD_SLOW : ts->extra[L]) / 100.0f;
  int dur = blizzard ? 170 : 120 + 30 * L;
  for (int j = 0; j < n; j++) {
    creep *e = &s->e[buf[j]];
    if (!e->alive) continue;
    slow(e, k, dur);
    if (freeze && e->kind != CK_SHADE && !e->boss) {
      if (!e->frozen) ev(s, EV_FREEZE, e->x, e->y, 0, 0, e->kind, 0);
      e->frozen = 45;
    }
    damage(s, buf[j], blizzard ? BLIZZARD_DMG : ts->dmg[L], slot, false);
  }
}

static void tower_beam(sim *s, int slot, tower *t) {
  const tower_stats *ts = &TOWER[TK_BEAM];
  int L = t->level - 1;
  float r = ts->range[L];
  if (t->tgt >= 0) {
    const creep *e = &s->e[t->tgt];
    float dx = e->x - t->x, dy = e->y - t->y;
    if (!e->alive || dx * dx + dy * dy > (r + 0.3f) * (r + 0.3f)) t->tgt = -1;
  }
  if (t->tgt < 0) {
    t->tgt = pick_target(s, t, r, 0);
    t->beam_t = 0, t->tgt2[0] = t->tgt2[1] = -1;
    if (t->tgt < 0) { t->beam_k = 1; return; }
    ev(s, EV_BEAM_ON, t->x, t->y, s->e[t->tgt].x, s->e[t->tgt].y, t->level, slot);
  }
  creep *e = &s->e[t->tgt];
  t->beam_t += e->slow_t > 0 ? 2 : 1;
  t->beam_k = 1.0f + 2.0f * CW_MIN((float)t->beam_t / 180.0f, 1.0f);
  float want = cw_atan2(e->y - t->y, e->x - t->x);
  t->yaw = want;
  float dps = ts->dmg[L] * t->beam_k / TICK;
  bool prism = t->level >= 3 && t->branch == BR_B;
  if (prism) { /* it splits at its creep on to two others within reach of it: each ray holds its creep while it can */
    for (int q = 0; q < 2; q++) {
      int o = t->tgt2[q];
      float dx = o >= 0 ? s->e[o].x - e->x : 0, dy = o >= 0 ? s->e[o].y - e->y : 0;
      if (o < 0 || o == t->tgt || !s->e[o].alive || dx * dx + dy * dy > PRISM_REACH * PRISM_REACH || (q == 1 && o == t->tgt2[0])) t->tgt2[q] = -1;
    }
    if (t->tgt2[0] < 0 || t->tgt2[1] < 0) {
      static int buf[256];
      int n = creeps_near(s, e->x, e->y, PRISM_REACH, buf, 256);
      for (int q = 0; q < 2; q++) {
        if (t->tgt2[q] >= 0) continue;
        float bd = 1e9f;
        for (int k = 0; k < n; k++) {
          if (buf[k] == t->tgt || buf[k] == t->tgt2[0] || buf[k] == t->tgt2[1]) continue;
          float dx = s->e[buf[k]].x - e->x, dy = s->e[buf[k]].y - e->y, d = dx * dx + dy * dy;
          if (d < bd) bd = d, t->tgt2[q] = buf[k];
        }
      }
    }
    for (int k = 0; k < 2; k++)
      if (t->tgt2[k] >= 0) damage(s, t->tgt2[k], dps * PRISM_SPLIT, slot, true);
    dps *= PRISM_MAIN;
    if (!e->alive) { t->tgt = -1; return; }
  } else if (ts->extra[L]) { /* it pierces: everything on the line to the edge of its range takes half */
    float dx = e->x - t->x, dy = e->y - t->y, l = sqrtf(dx * dx + dy * dy) + 1e-4f;
    dx /= l, dy /= l;
    static int buf[MAXE];
    int n = creeps_near(s, t->x + dx * r * 0.5f, t->y + dy * r * 0.5f, r * 0.5f + 0.5f, buf, MAXE);
    for (int k = 0; k < n; k++) {
      if (buf[k] == t->tgt) continue;
      const creep *o = &s->e[buf[k]];
      float ox = o->x - t->x, oy = o->y - t->y, along = ox * dx + oy * dy, perp = fabsf(ox * dy - oy * dx);
      if (along > 0 && along < r && perp < 0.55f + CK[o->kind].radius * 0.5f) damage(s, buf[k], dps * 0.5f, slot, true);
    }
  }
  int tg = t->tgt;
  damage(s, tg, dps, slot, true);
  if (!s->e[tg].alive) t->tgt = -1;
}

static void fire_shell(sim *s, int slot, tower *t, float x0, float y0, float tx, float ty, float dmg, float r, int T, int level) {
  for (int k = 0; k < MAXSH; k++)
    if (!s->sh[k].alive) {
      s->sh[k] = (shell){ true, (uint8_t)slot, t->owner, (uint8_t)level, level >= 3 ? t->branch : 0, x0, y0, tx, ty, dmg, r, 0, T };
      return;
    }
}

static void tower_mortar(sim *s, int slot, tower *t) {
  const tower_stats *ts = &TOWER[TK_MORTAR];
  int L = t->level - 1;
  int tg = pick_target(s, t, ts->range[L], MORTAR_MIN);
  if (tg < 0) return;
  t->cd = ts->period[L];
  t->fired++;
  creep *e = &s->e[tg];
  /* lead the target: where its way takes it in the shell's flight */
  float vx = 0, vy = 0, sp = e->speed * (e->slow_t > 0 ? 1 - e->slow_k : 1) * (e->frozen ? 0 : 1);
  float tx = e->x, ty = e->y;
  if (flow_step(s, e->x, e->y, &vx, &vy)) {
    float lead = sp * (float)MORTAR_T / TICK * 0.85f;
    for (int k = 0; k < 4; k++) { /* along the flow in four steps, so a corner is followed */
      float nx, ny;
      if (!flow_step(s, tx, ty, &nx, &ny)) break;
      tx += nx * lead * 0.25f, ty += ny * lead * 0.25f;
    }
  }
  t->yaw = cw_atan2(ty - t->y, tx - t->x);
  fire_shell(s, slot, t, t->x, t->y, tx, ty, ts->dmg[L], SPLASH[L], MORTAR_T, t->level);
  ev(s, EV_MORTAR, t->x, t->y, tx, ty, t->level, slot);
}

static void shells_step(sim *s) {
  for (int k = 0; k < MAXSH; k++) {
    shell *sh = &s->sh[k];
    if (!sh->alive || ++sh->t < sh->T) continue;
    sh->alive = false;
    bool big = sh->level >= 2 && sh->r > 2.0f;
    ev(s, EV_BOOM, sh->tx, sh->ty, sh->r, 0, sh->level, big);
    s->booms_total++;
    static int buf[MAXE];
    int n = creeps_near(s, sh->tx, sh->ty, sh->r, buf, MAXE);
    for (int j = 0; j < n; j++) {
      const creep *e = &s->e[buf[j]];
      float d = sqrtf((e->x - sh->tx) * (e->x - sh->tx) + (e->y - sh->ty) * (e->y - sh->ty));
      float k = e->frozen > 0 || e->stun > 0 ? SHATTER_K : 1.0f;
      if (k > 1 && e->hp <= sh->dmg * k) ev(s, EV_FREEZE, e->x, e->y, 0, 0, e->kind, 1); /* (shards: it shatters) */
      damage(s, buf[j], k * sh->dmg * (1.0f - 0.5f * CW_MIN(d / sh->r, 1.0f)), sh->slot, false);
    }
    tower *t = &s->tw[sh->slot];
    if (sh->level >= 3 && sh->branch == BR_B) { /* napalm: the path burns where it lands */
      for (int k2 = 0; k2 < MAXFIRE; k2++)
        if (!s->fire[k2].alive) {
          s->fire[k2] = (fire){ true, sh->slot, sh->tx, sh->ty, NAPALM_R, NAPALM_DPS, 0, NAPALM_T };
          ev(s, EV_FIRE, sh->tx, sh->ty, NAPALM_R, (float)NAPALM_T, sh->level, sh->slot);
          break;
        }
    } else if (sh->r > 2.0f && t->alive && TOWER[TK_MORTAR].extra[CW_CLAMP(sh->level, 1, 3) - 1] > 0) { /* the cluster: bomblets round the blast */
      int nb = TOWER[TK_MORTAR].extra[sh->level - 1];
      for (int b = 0; b < nb; b++) {
        float a = (float)b * 6.2831853f / (float)nb + (float)(s->tick % 7) * 0.3f, d = sh->r * 0.9f;
        fire_shell(s, sh->slot, t, sh->tx, sh->ty, sh->tx + cw_cos(a) * d, sh->ty + cw_sin(a) * d, sh->dmg * 0.4f, 1.6f, 16 + b * 3, 1);
      }
    }
  }
}

/* napalm: each fire burns what stands in it (every 6 ticks, a tenth of a second's worth), then dies */
static void fires_step(sim *s) {
  for (int k = 0; k < MAXFIRE; k++) {
    fire *f = &s->fire[k];
    if (!f->alive) continue;
    if (++f->t >= f->T) { f->alive = false; continue; }
    if (f->t % 6) continue;
    static int buf[MAXE];
    int n = creeps_near(s, f->x, f->y, f->r, buf, MAXE);
    for (int j = 0; j < n; j++) damage(s, buf[j], f->dps * 6.0f / TICK * (s->e[buf[j]].frozen > 0 || s->e[buf[j]].stun > 0 ? SHATTER_K : 1.0f), f->slot, true);
  }
}

static void towers_step(sim *s) {
  for (int k = 0; k < s->m.nslot; k++) {
    tower *t = &s->tw[k];
    if (!t->alive) continue;
    t->age++;
    if (t->kind == TK_BEAM) { tower_beam(s, k, t); continue; }
    if (t->cd > 0) { t->cd--; continue; }
    if (t->kind == TK_BOLT) tower_bolt(s, k, t);
    else if (t->kind == TK_FROST) tower_frost(s, k, t);
    else tower_mortar(s, k, t);
  }
}

/* ── players ────────────────────────────────────────────────────────── */

int sim_slot_owner(const sim *s, int slot) { return mp_owner(&s->m.slot[slot], s->players); }
bool sim_slot_mine(const sim *s, int i, int slot) {
  if (slot < 0 || slot >= s->m.nslot) return false;
  const tower *t = &s->tw[slot];
  return t->alive ? t->owner == i : sim_slot_owner(s, slot) == i;
}
/* the player's slot nearest (x, y) */
static int nearest_mine(const sim *s, int i, float x, float y) {
  int best = -1;
  float bd = 1e9f;
  for (int k = 0; k < s->m.nslot; k++) {
    if (!sim_slot_mine(s, i, k)) continue;
    float dx = mp_wx(&s->m, s->m.slot[k].x) - x, dy = mp_wz(&s->m, s->m.slot[k].y) - y, d = dx * dx + dy * dy;
    if (d < bd) bd = d, best = k;
  }
  return best;
}
/* the cursor's hop: to the player's slot that lies most nearly the way held, within 60 degrees of it */
static void hop(sim *s, int i, int dx, int dy) {
  player *p = &s->p[i];
  if (p->slot < 0) return;
  float ux = (float)dx, uy = (float)dy, ul = sqrtf(ux * ux + uy * uy);
  ux /= ul, uy /= ul;
  const mp_slot *c = &s->m.slot[p->slot];
  int best = -1;
  float bs = 1e9f;
  for (int k = 0; k < s->m.nslot; k++) {
    if (k == p->slot || !sim_slot_mine(s, i, k)) continue;
    float vx = (float)s->m.slot[k].x - (float)c->x, vy = (float)s->m.slot[k].y - (float)c->y;
    float along = vx * ux + vy * uy, perp = fabsf(vx * uy - vy * ux);
    if (along < 0.5f || perp > along * 1.75f) continue;
    float sc = along + perp * 2.2f;
    if (sc < bs) bs = sc, best = k;
  }
  if (best >= 0) {
    p->slot = best;
    ev(s, EV_HOP, mp_wx(&s->m, s->m.slot[best].x), mp_wz(&s->m, s->m.slot[best].y), 0, 0, i, 0);
  }
}

static void build(sim *s, int i, int slot, int kind) {
  player *p = &s->p[i];
  tower *t = &s->tw[slot];
  int cost = sim_tower_cost(kind, 1);
  float x = mp_wx(&s->m, s->m.slot[slot].x), y = mp_wz(&s->m, s->m.slot[slot].y);
  if (p->gold < cost) { ev(s, EV_NOGOLD, x, y, 0, 0, i, kind); return; }
  p->gold -= cost, p->spent += cost, p->built++;
  *t = (tower){ .alive = true, .kind = (uint8_t)kind, .level = 1, .owner = (uint8_t)i, .x = x, .y = y, .tgt = -1, .tgt2 = { -1, -1 }, .spent = cost, .beam_k = 1, .cd = 20 };
  t->yaw = cw_atan2(s->core_y - y, s->core_x - x);
  s->built_total++;
  ev(s, EV_BUILD, x, y, 0, 0, kind, i);
}

float sim_power_damage(int wave) { return 60.0f + 9.0f * (float)CW_MAX(wave - 1, 0); }
/* the storm: lightning down on everything round the cursor's pad (bosses take it but are never stunned) */
static void power(sim *s, int i) {
  player *p = &s->p[i];
  if (p->power_cd > 0 || p->slot < 0) return;
  float x = mp_wx(&s->m, s->m.slot[p->slot].x), y = mp_wz(&s->m, s->m.slot[p->slot].y);
  static int buf[MAXE];
  int n = creeps_near(s, x, y, POWER_R, buf, MAXE);
  p->power_cd = POWER_CD;
  ev(s, EV_POWER, x, y, POWER_R, 0, i, (int)CW_MIN(n, 255));
  float dmg = sim_power_damage(s->wave);
  int k0 = s->kills;
  for (int j = 0; j < n; j++) {
    creep *e = &s->e[buf[j]];
    if (!e->alive) continue;
    if (j < 14) ev(s, EV_ZAP, x, y, e->x, e->y, 3, 200); /* (b 200: a bolt from the sky) */
    if (!e->boss) e->stun = CW_MAX(e->stun, POWER_STUN);
    damage(s, buf[j], dmg, -1, false);
  }
  p->kills += s->kills - k0; /* (the storm's kills are the player's) */
}

static void player_step(sim *s, int i, const input *in) {
  player *p = &s->p[i];
  if (!p->active) return;
  if (p->power_cd > 0) p->power_cd--;
  if (in->power && s->state == ST_PLAY) power(s, i);
  if (!sim_slot_mine(s, i, p->slot)) p->slot = nearest_mine(s, i, p->slot >= 0 ? mp_wx(&s->m, s->m.slot[p->slot].x) : 0, p->slot >= 0 ? mp_wz(&s->m, s->m.slot[p->slot].y) : 0);
  if (p->slot < 0) return;
  /* the hop: on a press, then again every 9 ticks after 18 held */
  int dx = CW_CLAMP(in->dx, -1, 1), dy = CW_CLAMP(in->dy, -1, 1);
  if (dx != p->hx || dy != p->hy) p->hold = 0;
  else p->hold++;
  p->hx = (int8_t)dx, p->hy = (int8_t)dy;
  if ((dx || dy) && (p->hold == 0 || (p->hold >= 18 && (p->hold - 18) % 9 == 0))) hop(s, i, dx, dy);
  int slot = p->slot;
  tower *t = &s->tw[slot];
  if (!t->alive) {
    p->sell_t = 0;
    for (int a = 0; a < A_KINDS; a++)
      if (in->act[a]) { build(s, i, slot, a); break; } /* the diamond: A_N bolt, A_W frost, A_E beam, A_S mortar (the TK_ order) */
    return;
  }
  bool branch_b = in->act[A_E] && t->level == 2;
  if ((in->act[A_S] || branch_b) && t->level < 3) { /* upgrade (to the third level: south A, east B) */
    int cost = sim_tower_cost(t->kind, t->level + 1);
    if (p->gold >= cost) {
      p->gold -= cost, p->spent += cost, t->spent += cost, t->level++;
      if (t->level == 3) t->branch = branch_b ? BR_B : BR_A;
      s->upgrades_total++;
      ev(s, EV_UPGRADE, t->x, t->y, 0, 0, t->kind, t->level);
    } else
      ev(s, EV_NOGOLD, t->x, t->y, 0, 0, i, t->kind);
  }
  if (in->act[A_N]) t->aim = (uint8_t)((t->aim + 1) % AIM_KINDS), ev(s, EV_AIM, t->x, t->y, 0, 0, i, t->aim);
  if (in->sell_held) {
    if (++p->sell_t >= SELL_HOLD) {
      int v = sim_sell_value(t);
      p->gold += v;
      s->sold_total++;
      ev(s, EV_SELL, t->x, t->y, (float)v, 0, t->kind, i);
      t->alive = false;
      p->sell_t = 0;
    }
  } else
    p->sell_t = 0;
}

/* ── waves ──────────────────────────────────────────────────────────── */

int sim_wave_count(const sim *s) { return s->m.waves; }
static float wave_hp(const sim *s, int w) {
  float k = (float)(w - 1);
  return s->m.diff * (1.0f + 0.08f * k + 0.002f * k * k);
}
int sim_wave_size(const sim *s, int w) {
  float n = 30.0f * cw_pow(1.19f, (float)(w - 1)) * cw_pow(s->m.diff, 0.5f);
  return CW_CLAMP((int)n, 12, 3200);
}
/* the kinds that have arrived by wave w: the map's list, one more every few waves */
static int kinds_by(const sim *s, int w) {
  int nk = s->m.nkind, waves = CW_MAX(s->m.waves, 1);
  return CW_CLAMP(1 + (w - 1) * nk / CW_MAX(waves * 2 / 3, 1), 1, nk);
}
static void add_group(sim *s, int kind, int count, int spawn, int every, float hp, bool boss) {
  for (int g = 0; g < MAXGRP; g++)
    if (!s->grp[g].live) {
      s->grp[g] = (wgroup){ true, boss, (uint8_t)kind, (uint8_t)(spawn % CW_MAX(s->m.nspawn, 1)), count, CW_MAX(every, 1), 1 + g % 3, hp };
      return;
    }
}
int sim_wave_kinds(const sim *s, int w, int out[2]) {
  if (w < 1 || w > s->m.waves) return 0;
  int avail = kinds_by(s, w);
  out[0] = s->m.kind[(w - 1) % avail], out[1] = s->m.kind[avail - 1];
  return out[0] == out[1] ? 1 : 2;
}
static void start_wave(sim *s) {
  if (s->wave >= s->m.waves) return;
  int w = ++s->wave;
  int total = sim_wave_size(s, w), avail = kinds_by(s, w), ns = CW_MAX(s->m.nspawn, 1);
  float hm = wave_hp(s, w);
  /* the wave's body: its newest kind and one before it; skitters come in swarms (three for one) */
  int main_k = s->m.kind[(w - 1) % avail], new_k = s->m.kind[avail - 1];
  float secs = 10.0f + 0.5f * (float)w;
  int share[2] = { total * 2 / 3, total - total * 2 / 3 };
  int kinds[2] = { main_k, new_k };
  if (main_k == new_k) share[0] = total, share[1] = 0;
  for (int j = 0; j < 2; j++) {
    if (share[j] <= 0) continue;
    int k = kinds[j];
    float weight = k == CK_OGRE ? 6.0f : k == CK_BLOATER ? 3.0f : k == CK_SKITTER ? 0.34f : k == CK_SPORELING ? 1.6f : 1.0f;
    int count = CW_MAX(1, (int)((float)share[j] / weight));
    for (int g = 0; g < ns; g++) { /* split over the gates */
      int c = count / ns + (g < count % ns);
      if (c <= 0) continue;
      add_group(s, k, c, g, (int)(secs * TICK / (float)c), CK[k].hp * hm, false);
    }
  }
  if (w % 10 == 0 || w == s->m.waves) /* a heavy at the tail: the last wave's boss */
    add_group(s, CK_OGRE, 1, w % ns, 1, CK[CK_OGRE].hp * hm * (w == s->m.waves ? 9.0f : 4.0f), true);
  /* every wave pays every player (a share each) */
  int n = 0;
  for (int i = 0; i < MAXP; i++) n += s->p[i].active;
  int pay = (30 + 5 * w) / CW_MAX(n, 1);
  for (int i = 0; i < MAXP; i++)
    if (s->p[i].active) s->p[i].gold += pay;
  s->next_t = w < s->m.waves ? (int)((22.0f + 0.3f * (float)w) * TICK) : -1;
  ev(s, EV_WAVE, 0, 0, 0, 0, (uint8_t)w, (uint8_t)s->m.waves);
}
static void waves_step(sim *s, const input in[MAXP]) {
  if (s->no_spawn) return;
  if (s->next_t > 0) {
    bool call = false;
    for (int i = 0; i < MAXP; i++) call |= s->p[i].active && in[i].wave;
    if (call && s->wave > 0) { /* called early: the time left, paid out */
      int bonus = s->next_t / 60;
      for (int i = 0; i < MAXP; i++)
        if (s->p[i].active) s->p[i].gold += bonus;
      ev(s, EV_CALL, 0, 0, (float)bonus, 0, 0, 0);
      s->next_t = 0;
    } else if (call)
      s->next_t = 0;
    else
      s->next_t--;
    if (s->next_t <= 0) start_wave(s);
  }
  for (int g = 0; g < MAXGRP; g++) {
    wgroup *gr = &s->grp[g];
    if (!gr->live || --gr->cd > 0) continue;
    gr->cd = gr->every;
    float x, y;
    mp_gate_pos(&s->m, gr->spawn, &x, &y);
    float j = (rf(s) - 0.5f) * 1.4f, k = (rf(s) - 0.5f) * 1.4f;
    int bounty = (int)((float)CK[gr->kind].bounty * (1.0f + (float)s->wave / 18.0f) + 0.5f) * (gr->boss ? 20 : 1);
    int c = spawn_creep(s, gr->kind, gr->hp, x + j, y + k, bounty);
    if (c < 0) c = spawn_creep(s, gr->kind, gr->hp, x, y, bounty);
    if (c < 0) continue; /* full: again next tick */
    if (gr->boss) s->e[c].boss = true, s->e[c].leak = 10, s->e[c].speed *= 1.5f;
    ev(s, EV_SPAWN, x, y, 0, 0, gr->kind, gr->spawn);
    if (--gr->left <= 0) gr->live = false;
  }
}

/* ── creeps a tick ──────────────────────────────────────────────────── */

static void creeps_step(sim *s) {
  for (int i = 0; i < MAXE; i++) {
    creep *e = &s->e[i];
    if (!e->alive) continue;
    e->age++;
    float vx = 0, vy = 0;
    if (!flow_step(s, e->x, e->y, &vx, &vy)) { /* at the bottom of the flow: straight for the core */
      float dx = s->core_x - e->x, dy = s->core_y - e->y, d = sqrtf(dx * dx + dy * dy);
      if (d > 0.01f) vx = dx / d, vy = dy / d;
    }
    float sp = e->speed;
    if (e->slow_t > 0) e->slow_t--, sp *= 1.0f - e->slow_k;
    if (e->frozen > 0) e->frozen--, sp = 0;
    if (e->stun > 0) e->stun--, sp = 0;
    if (e->age < 15) sp *= 0.6f;
    cw_crowd_set_velocity(s->crowd, e->agent, (t3_vec3){ vx * sp, 0, vy * sp });
    if (e->flash > 0) e->flash--;
  }
  { CW_PROFILE_SCOPE("towers.crowd"); cw_crowd_step(s->crowd, 1.0f / TICK); }
  s->crowd_ms = cw_crowd_get_stats(s->crowd).step_ms;
  for (int i = 0; i < MAXE; i++) {
    creep *e = &s->e[i];
    if (!e->alive) continue;
    t3_vec3 p = cw_crowd_position(s->crowd, e->agent);
    float dx = p.x - e->x, dy = p.z - e->y;
    if (dx * dx + dy * dy > 0.00002f) e->face = cw_atan2(dy, dx);
    e->x = p.x, e->y = p.z;
    int c = flow_cell(s, e->x, e->y);
    if (c >= 0 && s->flow[c] != 0xffff) e->prog = s->flow[c];
    /* a creep wedged where the crowd cannot move it on (a corner of the floor, a crush at a choke) for 6 s: set down on
     * the open cell nearest the core within a metre and a half */
    if (e->prog < e->best) e->best = e->prog, e->stall = 0;
    else if (!e->frozen && ++e->stall > 360 && c >= 0) {
      int cx = c % FGW, cy = c / FGW, bc = -1;
      uint16_t bd = 0xffff;
      for (int oy = -1; oy <= 1; oy++)
        for (int ox = -1; ox <= 1; ox++) {
          int nx = cx + ox, ny = cy + oy;
          if (nx < 0 || ny < 0 || nx >= s->fw || ny >= s->fh || s->blocked[ny * FGW + nx]) continue;
          if (s->flow[ny * FGW + nx] < bd) bd = s->flow[ny * FGW + nx], bc = ny * FGW + nx;
        }
      if (bc >= 0 && bd < e->prog) {
        float tx = (float)(bc % FGW) + 0.5f - (float)s->m.w, ty = (float)(bc / FGW) + 0.5f - (float)s->m.h;
        cw_crowd_teleport(s->crowd, e->agent, (t3_vec3){ tx, 0, ty });
        e->x = tx, e->y = ty;
      }
      e->stall = 0;
    }
    /* the core: a creep that reaches it is spent on it */
    float cx = e->x - s->core_x, cy = e->y - s->core_y;
    if (cx * cx + cy * cy < 1.5f * 1.5f) {
      if (!s->god) s->core_hp -= e->leak;
      s->leaks += e->leak;
      ev(s, EV_LEAK, e->x, e->y, (float)e->leak, 0, e->kind, e->boss);
      remove_creep(s, i);
    }
  }
  { CW_PROFILE_SCOPE("towers.grid"); build_grid(s); }
  /* sporelings heal those round them */
  for (int i = 0; i < MAXE; i++) {
    creep *e = &s->e[i];
    if (!e->alive || e->kind != CK_SPORELING || --e->heal_cd > 0) continue;
    e->heal_cd = 45;
    static int buf[256];
    int n = creeps_near(s, e->x, e->y, 2.5f, buf, 256), healed = 0;
    for (int k = 0; k < n; k++) {
      creep *o = &s->e[buf[k]];
      if (o->hp < o->maxhp && s->tick - o->healed >= 45) o->hp = CW_MIN(o->maxhp, o->hp + o->maxhp * 0.05f), o->healed = s->tick, healed++;
    }
    if (healed) ev(s, EV_HEAL, e->x, e->y, 2.5f, 0, (uint8_t)CW_MIN(healed, 255), 0);
  }
}

/* ── the map ────────────────────────────────────────────────────────── */

void sim_free(sim *s) {
  if (!CW_HANDLE_IS_NULL(s->nav)) cw_navmesh_free(s->nav);
  s->nav = (cw_navmesh){ 0 };
  s->crowd = (cw_crowd){ 0 };
}

void sim_init(sim *s, int players, uint32_t seed) {
  sim_free(s);
  memset(s, 0, sizeof *s);
  cw_rng_seed(&s->rng, seed);
  s->players = CW_CLAMP(players, 1, MAXP);
  for (int i = 0; i < s->players; i++) s->p[i].active = true;
}

static int start_gold(const sim *s) {
  int n = 0;
  for (int i = 0; i < MAXP; i++) n += s->p[i].active;
  return CW_MAX(110, 420 / CW_MAX(n, 1));
}

void sim_map_start(sim *s, const tdmap *m) {
  s->m = *m;
  memset(s->tw, 0, sizeof s->tw);
  memset(s->e, 0, sizeof s->e);
  memset(s->sh, 0, sizeof s->sh);
  memset(s->fire, 0, sizeof s->fire);
  memset(s->grp, 0, sizeof s->grp);
  memset(s->slot_at, 0, sizeof s->slot_at);
  s->nenemies = 0, s->kills = 0, s->leaks = 0, s->wave = 0, s->core_hp = CORE_HP, s->pot = 0;
  s->state = ST_PLAY, s->state_ticks = 0, s->map_ticks = 0;
  s->fw = m->w * 2, s->fh = m->h * 2;
  mp_core_pos(m, &s->core_x, &s->core_y);
  for (int k = 0; k < m->nslot; k++) s->slot_at[m->slot[k].y][m->slot[k].x] = (int16_t)(k + 1);
  nav_build(s);
  build_flow(s);
  /* each slot's reach: the path cells within 7 m */
  for (int k = 0; k < m->nslot; k++) {
    float x = mp_wx(m, m->slot[k].x), y = mp_wz(m, m->slot[k].y);
    int n = 0;
    for (int cy = 0; cy < s->fh; cy++)
      for (int cx = 0; cx < s->fw; cx++) {
        if (s->blocked[cy * FGW + cx]) continue;
        float dx = (float)cx + 0.5f - (float)m->w - x, dy = (float)cy + 0.5f - (float)m->h - y;
        n += dx * dx + dy * dy < 49.0f;
      }
    s->cover[k] = n;
  }
  for (int i = 0; i < MAXP; i++) {
    player *p = &s->p[i];
    if (!p->active) continue;
    p->gold = start_gold(s), p->spent = 0, p->kills = 0, p->built = 0, p->dmg = 0, p->sell_t = 0, p->hold = 0;
    /* the cursor starts on the player's best slot */
    p->slot = -1;
    for (int k = 0; k < m->nslot; k++)
      if (sim_slot_mine(s, i, k) && (p->slot < 0 || s->cover[k] > s->cover[p->slot])) p->slot = k;
  }
  s->next_t = (int)(24 * TICK);
}

/* a player joins during play: a share of the slots (the empty ones re-dealt for the new count) and a start */
void sim_add_player(sim *s, int i) {
  if (i < 0 || i >= MAXP || s->p[i].active) return;
  player *p = &s->p[i];
  *p = (player){ .active = true, .slot = -1 };
  if (i >= s->players) s->players = i + 1;
  p->gold = start_gold(s) / 2 + 20 * s->wave;
  for (int k = 0; k < s->m.nslot; k++)
    if (sim_slot_mine(s, i, k) && (p->slot < 0 || s->cover[k] > s->cover[p->slot])) p->slot = k;
}

static void hash_state(sim *s) {
  uint32_t h = 2166136261u;
#define HMIX(v) do { uint32_t _v; float _f = (float)(v); memcpy(&_v, &_f, 4); h = (h ^ _v) * 16777619u; } while (0)
  for (int i = 0; i < MAXP; i++) { HMIX(s->p[i].gold); HMIX(s->p[i].slot); }
  for (int k = 0; k < s->m.nslot; k++) if (s->tw[k].alive) { HMIX(s->tw[k].kind); HMIX(s->tw[k].level); HMIX(s->tw[k].dmg); if (s->tw[k].branch == BR_B) HMIX(BR_B); }
  /* (a B branch mixes in; A plays exactly as the third level always has, so runs pinned before the branches stay pinned) */
  HMIX(s->nenemies); HMIX(s->kills); HMIX(s->core_hp); HMIX(s->wave); HMIX(s->tick);
  for (int i = 0; i < MAXE; i += 7) if (s->e[i].alive) { HMIX(s->e[i].x); HMIX(s->e[i].y); HMIX(s->e[i].hp); }
  s->hash = h;
}

void sim_step(sim *s, const input in[MAXP]) {
  if (s->ev_n > MAXEV - 1024) s->ev_n = 0; /* nobody is reading them (no view): start over */
  s->ev_start = s->ev_n;
  s->tick++;
  s->state_ticks++;
  if (s->state != ST_PLAY) {
    for (int i = 0; i < MAXP; i++) s->p[i].hx = s->p[i].hy = 0;
    hash_state(s);
    return;
  }
  s->map_ticks++;
  { CW_PROFILE_SCOPE("towers.players"); for (int i = 0; i < MAXP; i++) player_step(s, i, &in[i]); }
  waves_step(s, in);
  { CW_PROFILE_SCOPE("towers.creeps"); creeps_step(s); }
  { CW_PROFILE_SCOPE("towers.towers"); towers_step(s); }
  shells_step(s);
  fires_step(s);
  if (s->nenemies > s->most_alive) s->most_alive = s->nenemies;
  { /* the bounties, shared: each player an equal part, the remainder kept for the next */
    int n = 0;
    for (int i = 0; i < MAXP; i++) n += s->p[i].active;
    if (n > 0 && s->pot >= n) {
      int each = s->pot / n;
      for (int i = 0; i < MAXP; i++)
        if (s->p[i].active) s->p[i].gold += each;
      s->pot -= each * n;
    }
  }
  if (s->core_hp <= 0) {
    s->core_hp = 0, s->state = ST_LOST, s->state_ticks = 0;
    ev(s, EV_LOST, s->core_x, s->core_y, 0, 0, 0, 0);
  } else if (s->wave >= s->m.waves && s->nenemies == 0) {
    bool live = false;
    for (int g = 0; g < MAXGRP; g++) live |= s->grp[g].live;
    if (!live) s->state = ST_WON, s->state_ticks = 0, ev(s, EV_WON, s->core_x, s->core_y, 0, 0, 0, 0);
  }
  hash_state(s);
}
