/* destructo's simulation: the world, the blocks and how they break, the launcher, the shot, charges, targets and
 * turns. Deterministic: Box3D (deterministic across hosts and worker counts), cartwheel's deterministic maths and
 * integer hashes. Nothing here reads a clock or the view.
 *
 * Breaking. Box3D does not fracture, so the game does: every block shape reports hit events (a contact that began
 * closing faster than the world's hit speed). A hit's energy is the reduced mass of the pair times the approach speed
 * squared over two; the part above the material's floor comes off the block's strength (its toughness times its
 * volume). A block out of strength is replaced by two or four pieces that tile it (each moving as that part of the
 * block moved, plus a little spread), or by dust and shards alone when it is small, glass, or a second-generation
 * piece. A shot that breaks what it hits keeps the speed the break did not use: it punches through instead of
 * stopping dead on the solver's contact.
 *
 * Charges push every block within their radius (an impulse falling off with the square of the distance) and harm
 * it the same way. */
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/profile.h"

static double profile_clock(void) { return (double)cw_profile_now_ns() * 1e-9; }
double (*g_sim_clock)(void) = profile_clock; /* the real monotonic clock, in carts too (cw_time is the host's, pinned in a replay) */
static double sim_clock(void) { return g_sim_clock(); }
#include "game.h"
#include "opts.h"
#include "box3d/box3d.h"

float g_phys_worst[10];

const material_info MATS[M_COUNT] = {
  [M_WOOD] = { "wood", 620, 0.65f, 0.05f, 52000, 160, 2, 10 },
  [M_STONE] = { "stone", 2300, 0.75f, 0.02f, 150000, 900, 4, 14 },
  [M_GLASS] = { "glass", 2500, 0.4f, 0.1f, 9000, 30, 0, 8 },
  [M_STEEL] = { "steel", 2600, 0.5f, 0.05f, 900000, 6000, 2, 20 }, /* girders: an I-section fills a third of its box */
  [M_TARGET] = { "crystal", 900, 0.5f, 0.1f, 6000, 60, 0, 0 },
};
/* Each ammunition fires at one fixed muzzle speed (no charging, no power): its energy is what it is. A shot is a 50 kg
 * iron ball at 62 m/s (96 kJ), a heavy ball 210 kg at 40 m/s (168 kJ: slower, heavier, it crushes more and toppled
 * towers), a charge a 30 kg bomb at 28 m/s (12 kJ of flight: a mortar, lobbed over a wall; it goes off where it
 * lands for 260 kJ falling off with the square of the distance, CHARGE_ENERGY). */
const ammo_info AMMO[NAMMO] = {
  [A_SHOT] = { "Shot", 0.22f, 50, 62, 62 },
  [A_HEAVY] = { "Heavy ball", 0.36f, 210, 40, 40 },
  [A_CHARGE] = { "Charge", 0.26f, 30, 28, 28 },
};

/* the launcher's power is fixed, by ammunition (no player sets it): a shot and a heavy ball leave at their full speed (the heavy ball is
 * the slower, heavier one), a charge is lobbed at 23 m/s so that a high arc (up to 79 degrees) lands it on a structure 20 to 54 m off */
const float AMMO_POWER[NAMMO] = { [A_SHOT] = 1.0f, [A_HEAVY] = 1.0f, [A_CHARGE] = 1.0f } /* (each ammunition has its one speed, AMMO.vmax) */;

#define USER_PROJ ((void *)(intptr_t)(MAXBLK + 7))
#define WELD_POP_SPEED 4.0f /* m/s: a welded member struck or landing this hard loses its welds */
#define CHARGE_R 4.6f
#define CHARGE_IMPULSE 5200.0f
#define CHARGE_ENERGY 260000.0f
#define KEG_R 3.4f        /* a keg's blast: radius, ... */
#define KEG_K 0.7f        /* ... and strength against a charge's */
#define KEG_DELAY 7       /* ticks between the blasts of a chain */
#define FRAC_SPEED 1.6f  /* m/s: a chunk landing this hard breaks in two */
#define FRAC_COOL 5      /* ticks before a piece breaks again */
#define LAYER_GROUND 0
#define LAYER_BLOCK 1
#define LAYER_PROJ 2

static void ev_push(game *g, int kind, int mat, t3_vec3 p, t3_vec3 n, float size, float speed, int a) {
  if (g->nev >= MAXEV) return;
  g->ev[g->nev++] = (sim_event){ (uint8_t)kind, (uint8_t)mat, p, n, size, speed, a };
}

void game_ev(game *g, int kind, int mat, t3_vec3 p, t3_vec3 n, float size, float speed, int a) { ev_push(g, kind, mat, p, n, size, speed, a); }

static inline t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static inline t3_vec3 vadd(t3_vec3 a, t3_vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline t3_vec3 vsub(t3_vec3 a, t3_vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline t3_vec3 vscale(t3_vec3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static inline float vdot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float vlen(t3_vec3 a) { return sqrtf(vdot(a, a)); }
static inline t3_vec3 vcross(t3_vec3 a, t3_vec3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
/* q * v (deterministic: + - * only) */
static t3_vec3 qrot(t3_quat q, t3_vec3 v) {
  t3_vec3 u = v3(q.x, q.y, q.z);
  t3_vec3 t = vscale(vcross(u, v), 2);
  return vadd(vadd(v, vscale(t, q.w)), vcross(u, t));
}

/* ── blocks ──────────────────────────────────────────────────────────── */

/* the launcher stands on its circle round the structure, at its orbit */
static void place_launcher(game *g) {
  float s, c;
  cw_sincos(g->orbit, &s, &c);
  g->muzzle = v3(g->ring * s, 1.7f, g->ring * c);
}

static int alloc_block(game *g) {
  if (g->nfree > 0) return g->free_list[--g->nfree];
  if (g->nb < MAXBLK) return g->nb++;
  return -1;
}

/* the block a body belongs to when it is a chunk of one (the shot has its own user mark) */
int game_block_of_shape(cw_shape3d s) {
  if (!cw_shape3d_valid(s)) return -1;
  intptr_t u = (intptr_t)cw_shape3d_user(s);
  if (u <= 0 || u > MAXBLK) return -1;
  return (int)u - 1;
}

cw_shape3d block_shape(game *g, int i, cw_body3d body, cw_xform3 local) {
  block *k = &g->b[i];
  cw_shape3d_def sd = cw_shape3d_default_def();
  const material_info *m = &MATS[k->mat];
  sd.density = m->density;
  sd.material.friction = m->friction, sd.material.restitution = m->restitution;
  sd.material.user_material = (uint64_t)k->mat;
  sd.layer = LAYER_BLOCK;
  sd.hit_events = true;
  sd.contact_events = false;
  sd.user = (void *)(intptr_t)(i + 1);
  return cw_shape3d_box(body, &sd, k->half, local);
}

static inline cw_xform3 xmul(cw_xform3 a, cw_xform3 b) {
  t3_quat p = a.q, q = b.q;
  return (cw_xform3){ vadd(a.p, qrot(a.q, b.p)), (t3_quat){ p.w * q.x + p.x * q.w + p.y * q.z - p.z * q.y, p.w * q.y - p.x * q.z + p.y * q.w + p.z * q.x, p.w * q.z + p.x * q.y - p.y * q.x + p.z * q.w, p.w * q.w - p.x * q.x - p.y * q.y - p.z * q.z } };
}
cw_xform3 game_block_xform(const game *g, int i) {
  const block *k = &g->b[i];
  const chunk *c = &g->ch[k->chunk];
  if (!c->dyn) return k->wxf;
  return xmul(cw_body3d_transform(c->body), k->local);
}
cw_xform3 game_block_interp(const game *g, int i, float alpha) {
  const block *k = &g->b[i];
  const chunk *c = &g->ch[k->chunk];
  if (!c->dyn) return k->wxf;
  return xmul(cw_body3d_interpolated(c->body, alpha), k->local);
}
t3_vec3 game_block_vel(const game *g, int i) {
  const chunk *c = &g->ch[g->b[i].chunk];
  if (!c->dyn) return v3(0, 0, 0);
  t3_vec3 p = game_block_xform(g, i).p;
  b3Vec3 v = b3Body_GetWorldPointVelocity(cw_body3d_raw(c->body), (b3Pos){ p.x, p.y, p.z });
  return v3(v.x, v.y, v.z);
}
t3_vec3 game_block_pos(const game *g, int i) { return game_block_xform(g, i).p; }

static int make_block(game *g, int mat, t3_vec3 c, t3_vec3 half, t3_quat q, t3_vec3 v, t3_vec3 w, int gen, int paint) {
  int i = alloc_block(g);
  if (i < 0) return -1;
  block *k = &g->b[i];
  memset(k, 0, sizeof *k);
  const material_info *m = &MATS[mat];
  k->half = half, k->mat = (uint8_t)mat;
  cw_body3d_def bd = cw_body3d_default_def();
  bd.position = c, bd.rotation = q, bd.linear_velocity = v, bd.angular_velocity = w;
  bd.angular_damping = gen ? 0.4f : 0.05f, bd.linear_damping = gen ? 0.05f : 0.0f;
  bd.interpolate = true;
  cw_body3d body = cw_body3d_create(g->w, &bd);
  if (gen) b3Body_SetSleepThreshold(cw_body3d_raw(body), 0.12f); /* rubble sleeps sooner (Box3D: 0.05 m/s): a pile still awake keeps its whole island stepping */
  k->shape = block_shape(g, i, body, cw_xform3_identity());
  k->chunk = chunk_new(g, body, true, false);
  k->local = cw_xform3_identity();
  chunk_add_member(g, k->chunk, i);
  k->vol = 8 * half.x * half.y * half.z;
  k->hp = m->toughness * k->vol * (gen ? 0.75f : 1.0f);
  k->gen = (uint8_t)gen, k->alive = 1, k->target = -1;
  k->tint = (uint8_t)(hash32((uint32_t)i * 2654435761u + g->L->seed) & 0xff);
  k->born = g->tick;
  k->home = c;
  k->paint = (uint8_t)paint;
  g->nalive++;
  return i;
}

int game_add_block(game *g, int mat, t3_vec3 c, t3_vec3 half, float yaw) {
  float s, co;
  cw_sincos(yaw * 0.5f, &s, &co);
  int i = make_block(g, mat, c, half, (t3_quat){ 0, s, 0, co }, v3(0, 0, 0), v3(0, 0, 0), 0, g->paint_now);
  if (i >= 0) g->b[i].tag = 1;
  return i;
}

void game_add_banner(game *g, int blk, t3_vec3 local, float h, int colour) {
  if (blk < 0 || g->nban >= MAXBANNER) return;
  g->ban[g->nban++] = (banner){ blk, local, h, (uint8_t)colour, (uint8_t)(g->nban & 1) };
}

int game_add_keg(game *g, float x, float z) {
  uint8_t was = g->paint_now;
  g->paint_now = P_KEG;
  int i = game_add_block(g, M_WOOD, v3(x, KEG_HALF + 0.004f, z), v3(KEG_HALF * 0.85f, KEG_HALF, KEG_HALF * 0.85f), 0);
  g->paint_now = was;
  if (i >= 0) g->b[i].keg = 1, g->b[i].hp *= 0.5f; /* a keg goes off at half a crate's knock */
  return i;
}

int game_add_target(game *g, t3_vec3 c) {
  if (g->ntgt >= MAXTGT) return -1;
  int i = make_block(g, M_TARGET, c, v3(0.22f, 0.32f, 0.22f), (t3_quat){ 0, 0, 0, 1 }, v3(0, 0, 0), v3(0, 0, 0), 0, 0);
  if (i < 0) return -1;
  g->b[i].tag = 1;
  g->b[i].target = (int8_t)g->ntgt;
  g->tgt[g->ntgt] = (target_state){ .blk = i, .home = c, .down = false, .by_player = -1 };
  g->ntgt++, g->tgt_left++;
  return g->ntgt - 1;
}

static void target_down(game *g, int t) {
  target_state *ts = &g->tgt[t];
  if (ts->down) return;
  ts->down = true, ts->down_tick = g->tick, ts->by_player = g->cur;
  if (g->mode == MODE_PARTY && !g->P[g->cur].out) { /* party: the one who brought a crystal down is out */
    int n = 0;
    for (int p = 0; p < g->players; p++) n += g->P[p].out != 0;
    g->P[g->cur].out = 1, g->P[g->cur].out_order = (int8_t)(n + 1);
  }
  g->tgt_left--;
  if (g->tgt_left == 0) g->win_tick = g->tick;
  g->P[g->cur].targets++;
  g->P[g->cur].score += 1000;
  ev_push(g, EV_TARGET, M_TARGET, ts->blk >= 0 && g->b[ts->blk].alive ? game_block_xform(g, ts->blk).p : ts->home, v3(0, 1, 0), 0.5f, 0, t);
}

static void kill_block(game *g, int i) {
  block *k = &g->b[i];
  if (!k->alive) return;
  if (k->target >= 0) {
    g->tgt[k->target].blk = -1;
    target_down(g, k->target);
  }
  if (k->tag && !k->down) { k->down = true; g->P[g->cur].score += (int)(k->vol * MATS[k->mat].points * 10 + 1); if (k->target < 0) g->down_count++; }
  if (k->tag == 1 && k->target < 0) g->vol_gone += k->vol;
  chunk_remove_member(g, i);
  k->alive = 0;
  g->nalive--;
  g->free_list[g->nfree++] = i;
}

/* break block i: pieces (or dust) and the event the view and sound read; dir is the hit's direction (may be 0) */
static void break_block(game *g, int i, t3_vec3 dir) {
  block *k = &g->b[i];
  if (!k->alive) return;
  cw_xform3 x = game_block_xform(g, i);
  t3_vec3 v = game_block_vel(g, i), w = g->ch[k->chunk].dyn ? cw_body3d_angular_velocity(g->ch[k->chunk].body) : v3(0, 0, 0);
  t3_vec3 h = k->half;
  int mat = k->mat, gen = k->gen, paint = k->paint;
  float big = fmaxf(h.x, fmaxf(h.y, h.z));
  if (k->keg) { /* a keg goes off: its blast waits its turn (a chain runs a blast at a time) */
    if (g->npend < MAXPEND) g->pend_charge[g->npend] = 0, g->pend_at[g->npend++] = x.p;
    if (g->blast_shell == 0 && g->pend_wait <= 0) g->pend_wait = 2;
    g->broken++;
    kill_block(g, i);
    return;
  }
  bool glass = MATS[mat].pieces == 0;
  ev_push(g, glass ? EV_SHATTER : EV_BREAK, mat, x.p, dir, big * 2, vlen(v), i);
  g->broken++;
  kill_block(g, i);
  if (glass || gen >= 1 || big < 0.13f || g->nalive >= BODY_CAP) return; /* a piece breaks to dust */
  /* split along the longest axis, and the next longest too for stone (4 pieces) when it is big enough */
  float hh[3] = { h.x, h.y, h.z };
  int a0 = hh[0] >= hh[1] && hh[0] >= hh[2] ? 0 : hh[1] >= hh[2] ? 1 : 2;
  int a1 = -1;
  if (MATS[mat].pieces >= 4 && g->awake_now < AWAKE_SPLIT) {
    float best = 0;
    for (int a = 0; a < 3; a++) if (a != a0 && hh[a] > best && hh[a] > 0.1f) best = hh[a], a1 = a;
  }
  int n = a1 >= 0 ? 4 : 2;
  for (int p = 0; p < n; p++) {
    if (g->nalive >= BODY_CAP) break;
    float ph[3] = { hh[0], hh[1], hh[2] }, off[3] = { 0, 0, 0 };
    ph[a0] *= 0.5f, off[a0] = (p & 1 ? 1 : -1) * ph[a0];
    if (a1 >= 0) ph[a1] *= 0.5f, off[a1] = (p & 2 ? 1 : -1) * ph[a1];
    /* uneven pieces: a split is never quite in the middle */
    float u = (hashf((uint32_t)i * 31u + (uint32_t)g->tick * 7u + (uint32_t)p) - 0.5f) * 0.3f;
    if (p & 1) ph[a0] *= 1 - u, off[a0] += ph[a0] * u; else ph[a0] *= 1 + u, off[a0] += ph[a0] * u;
    t3_vec3 half = v3(ph[0] - 0.003f, ph[1] - 0.003f, ph[2] - 0.003f);
    if (fmaxf(half.x, fmaxf(half.y, half.z)) < 0.07f) continue;
    t3_vec3 r = qrot(x.q, v3(off[0], off[1], off[2]));
    t3_vec3 rn = vscale(r, 1.0f / (vlen(r) + 1e-6f));
    t3_vec3 pv = vadd(vadd(v, vcross(w, r)), vadd(vscale(rn, 1.2f), vscale(dir, 0.8f)));
    t3_vec3 pw = vadd(w, v3((hashf((uint32_t)i * 5u + (uint32_t)p) - 0.5f) * 4, (hashf((uint32_t)i * 7u + (uint32_t)p) - 0.5f) * 4, (hashf((uint32_t)i * 11u + (uint32_t)p) - 0.5f) * 4));
    int j = make_block(g, mat, vadd(x.p, r), half, x.q, pv, pw, gen + 1, paint);
    if (j >= 0) g->fragments++;
  }
}

/* urgent: break now (what the shot itself hits, so it goes on through); the rest wait in the fracture queue */
static void harm(game *g, int i, float e, t3_vec3 dir, bool urgent) {
  block *k = &g->b[i];
  if (!k->alive || k->queued) return;
  float f = MATS[k->mat].min_hit;
  if (e <= f) return;
  k->hp -= e - f;
  if (k->hp > 0) return;
  if (urgent || g->no_budget || g->bq_n >= MAXBLK) { break_block(g, i, dir); return; }
  k->queued = true;
  int slot = (g->bq_head + g->bq_n) % MAXBLK;
  g->bq[slot] = i, g->bq_dir[slot] = dir, g->bq_n++;
}

/* the queue's turn this tick: the oldest breaks first */
static void break_queued(game *g) {
  for (int n = 0; n < BREAK_BUDGET && g->bq_n > 0; ) {
    int i = g->bq[g->bq_head];
    t3_vec3 d = g->bq_dir[g->bq_head];
    g->bq_head = (g->bq_head + 1) % MAXBLK, g->bq_n--;
    if (!g->b[i].alive || !g->b[i].queued) continue; /* gone already, or its slot is another block's now */
    g->b[i].queued = false;
    break_block(g, i, d);
    n++;
  }
}

/* ── the world ───────────────────────────────────────────────────────── */

/* worlds are made and destroyed one at a time: a level is built on a background job (loader.c) while the main thread may
 * free another world, and Box3D keeps its worlds in one table */
static atomic_flag WORLD_LOCK = ATOMIC_FLAG_INIT;
static void world_lock(void) { while (atomic_flag_test_and_set_explicit(&WORLD_LOCK, memory_order_acquire)) {} }
static void world_unlock(void) { atomic_flag_clear_explicit(&WORLD_LOCK, memory_order_release); }

void game_free(game *g) {
  if (cw_world3d_valid(g->w)) world_lock(), cw_world3d_destroy(g->w), world_unlock();
  g->w = (cw_world3d){ 0 };
  free(g->weld), g->weld = NULL, g->nweld = g->weld_cap = 0;
  free(g->ch), g->ch = NULL, g->nch = g->ch_cap = 0;
  free(g->cfree), g->cfree = NULL, g->ncfree = 0;
  free(g->sl_off), free(g->sl_to), free(g->sl_rect), free(g->bnd_off), free(g->bnd_to);
  g->sl_off = g->sl_to = g->bnd_off = g->bnd_to = NULL, g->sl_rect = NULL, g->nsl = 0;
}

/* load timing (profiling only, host-dependent, never fed back): ms of the last game_new's parts */
float g_ld_world_ms, g_ld_build_ms, g_ld_prestress_ms, g_ld_sleep_ms;
volatile int32_t g_ld_stage; /* how far the build got (a trap in the job names its place) */
static double ld_now(void) { return (double)cw_profile_now_ns() * 1e-6; }

void game_new(game *g, int level, int players, int mode) {
  double ld0 = ld_now();
  g_ld_stage = 1;
  game_free(g);
  memset(g, 0, sizeof *g);
  g->level = level;
  g->L = level_get(level);
  g->players = players < 1 ? 1 : players > MAXP ? MAXP : players;
  g->mode = mode;
  g->old_welds = opt_on("OLD_WELDS");
  cw_world3d_def wd = cw_world3d_default_def();
  wd.gravity = v3(0, -GRAV, 0);
  wd.substeps = opt_get("SUBSTEPS") ? atoi(opt_get("SUBSTEPS")) : 4;
  wd.hit_speed = 1.2f;
  wd.restitution_threshold = 2.0f;
  wd.layers[LAYER_GROUND] = "ground", wd.layers[LAYER_BLOCK] = "block", wd.layers[LAYER_PROJ] = "shot";
  /* the physics on at most 12 of the pool's threads: past about 12 a step stops getting faster (tests/destructo-physics.mjs:
   * 6000 bodies 5.8 ms on 12 threads, 5.4 on 16 and 24), and on a loaded machine all 24 made the blast's steps slower
   * (Great Keep's collapse on romdev, load about 13: a step 6.2 to 7.1 ms on 24, 4.0 to 4.2 on 12). The same bits on any
   * count. dm set WORKERS n measures another */
  wd.workers = opt_get("WORKERS") ? atoi(opt_get("WORKERS")) : 12;
  world_lock();
  g->w = cw_world3d_create(&wd);
  world_unlock();
  /* the ground: a slab under the whole field */
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  bd.position = v3(0, -1, 0);
  cw_body3d ground = g->ground = cw_body3d_create(g->w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.material.friction = 0.8f;
  sd.layer = LAYER_GROUND;
  sd.hit_events = false;
  cw_shape3d_box(ground, &sd, v3(250, 1, 250), cw_xform3_identity());
  g->rng = g->L->seed;
  double ld1 = ld_now();
  g_ld_stage = 2;
  level_build(g, g->L);
  double ld2 = ld_now();
  g_ld_stage = 3;
  if (!opt_on("NO_CHUNKS")) chunks_build(g); /* NO_CHUNKS (tests): the blocks stay loose bodies, the checks' control */
  double ld3 = ld_now();
  g_ld_stage = 4;
  g_ld_world_ms = (float)(ld1 - ld0), g_ld_build_ms = (float)(ld2 - ld1), g_ld_prestress_ms = (float)(ld3 - ld2); /* (the chunks' build) */
  for (int a = 0; a < NAMMO; a++) g->ammo[a] = g->L->ammo[a];
  for (int i = 0; i < g->nb; i++) if (g->b[i].alive && g->b[i].tag == 1 && g->b[i].target < 0) g->vol_total += g->b[i].vol;
  g->ring = g->L->range, g->orbit = g->L->orbit0;
  place_launcher(g);
  g->yaw = 0, g->pitch = 0.12f, g->power = 1.0f; /* fixed: no player sets it */
  g->ammo_sel = A_SHOT;
  /* the structure's size (the camera frames it) */
  float ext = 3, top = 2, front = 0;
  for (int i = 0; i < g->nb; i++) {
    if (!g->b[i].alive) continue;
    t3_vec3 p = g->b[i].home;
    if (!g->b[i].keg && p.z + fmaxf(g->b[i].half.x, g->b[i].half.z) > front) front = p.z + fmaxf(g->b[i].half.x, g->b[i].half.z);
    float r = sqrtf(p.x * p.x + p.z * p.z) + vlen(g->b[i].half);
    if (r > ext) ext = r;
    if (p.y + g->b[i].half.y > top) top = p.y + g->b[i].half.y;
  }
  g->extent = ext, g->top = top, g->front = front;
  game_sleep_all(g); /* what is dynamic (the crystals, the kegs) lies still from the first frame; the chunks stand */
  g_ld_sleep_ms = (float)(ld_now() - ld3);
  g_ld_stage = 5;
  g->state = GS_AIM;
}

float game_world_yaw(const game *g) { return g->yaw + g->orbit; }
t3_vec3 game_out_dir(const game *g) {
  float s, c;
  cw_sincos(g->orbit, &s, &c);
  return v3(s, 0, c);
}
float game_yaw_from_world(const game *g, float world_yaw) {
  float y = world_yaw - g->orbit;
  while (y > 3.14159265f) y -= 6.2831853f;
  while (y < -3.14159265f) y += 6.2831853f;
  return y;
}
float game_front(const game *g) {
  t3_vec3 u = game_out_dir(g);
  float front = 0;
  for (int i = 0; i < g->nb; i++) {
    const block *k = &g->b[i];
    if (!k->alive || !k->tag || k->keg) continue;
    float f = k->home.x * u.x + k->home.z * u.z + fmaxf(k->half.x, k->half.z);
    if (f > front) front = f;
  }
  return front;
}

void game_settle(game *g, int ticks) {
  input none = { 0 };
  int st = g->state;
  g->state = GS_SETTLE;
  for (int t = 0; t < ticks; t++) cw_world3d_step(g->w, TICK);
  (void)none;
  g->state = st;
  game_settle_finish(g);
}

void game_settle_finish(game *g) {
  /* where everything rests now is home */
  for (int i = 0; i < g->nb; i++)
    if (g->b[i].alive) g->b[i].home = game_block_xform(g, i).p;
  for (int t = 0; t < g->ntgt; t++) if (g->tgt[t].blk >= 0) g->tgt[t].home = g->b[g->tgt[t].blk].home;
  g->nev = 0;
}

int game_awake(const game *g) { return cw_world3d_stats(g->w).awake_bodies; }

t3_vec3 game_aim_dir(const game *g) {
  float sy, cy, sp, cp;
  cw_sincos(game_world_yaw(g), &sy, &cy);
  cw_sincos(g->pitch, &sp, &cp);
  return v3(-sy * cp, sp, -cy * cp);
}
void game_set_orbit(game *g, float orbit) {
  g->orbit = orbit;
  while (g->orbit > 3.14159265f) g->orbit -= 6.2831853f;
  while (g->orbit < -3.14159265f) g->orbit += 6.2831853f;
  place_launcher(g);
}
float game_speed(const game *g) {
  if (g->ammo_sel == A_CHARGE && opt_get("CHARGE_V")) return (float)atof(opt_get("CHARGE_V")); /* tests */
  return AMMO[g->ammo_sel].vmax;
}

void game_fire(game *g) {
  int a = g->ammo_sel;
  if (g->ammo[a] <= 0 || g->proj_live) return;
  if (!g->sandbox && g->mode != MODE_PARTY) g->ammo[a]--;
  g->down_at_fire = g->down_count;
  t3_vec3 d = game_aim_dir(g);
  float sp = game_speed(g);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.position = vadd(g->muzzle, vscale(d, 1.4f));
  bd.linear_velocity = vscale(d, sp);
  bd.bullet = true; /* physcheck: without it a fast shot passes through thin dynamic blocks */
  bd.user = USER_PROJ;
  bd.angular_damping = 0.3f;
  g->proj = cw_body3d_create(g->w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  const ammo_info *ai = &AMMO[a];
  sd.density = ai->mass / (4.18879f * ai->radius * ai->radius * ai->radius);
  sd.material.friction = 0.5f, sd.material.restitution = 0.15f;
  sd.material.rolling_resistance = 0.08f;
  sd.layer = LAYER_PROJ;
  sd.hit_events = true;
  cw_shape3d_sphere(g->proj, &sd, v3(0, 0, 0), ai->radius);
  g->proj_kind = a, g->proj_ticks = 0, g->proj_hit_tick = -1, g->proj_live = true, g->charge_armed = a == A_CHARGE, g->proj_burst = false;
  g->proj_pos = bd.position, g->proj_vel = bd.linear_velocity;
  g->shots++;
  g->P[g->cur].shots++;
  g->state = GS_FLIGHT, g->state_t = 0;
  ev_push(g, EV_LAUNCH, 0, bd.position, d, ai->radius, sp, a);
}

static void proj_end(game *g) {
  if (!g->proj_live) return;
  cw_body3d_destroy(g->proj);
  g->proj = (cw_body3d){ 0 };
  g->proj_live = false;
}

/* one shell of a blast: the blocks between the last shell's radius and this one's are pushed and harmed */
static void blast_shell(game *g, int shell) {
  t3_vec3 at = g->blast_at;
  float R = g->blast_r > 0 ? g->blast_r : CHARGE_R, K = g->blast_k > 0 ? g->blast_k : 1;
  float r0 = R * (float)(shell - 1) / BLAST_SHELLS, r1 = R * (float)shell / BLAST_SHELLS;
  if (g->no_budget) r0 = 0, r1 = R;
  cw_qshape3d q = cw_qshape3d_sphere(at, r1);
  static cw_shape3d hit[1024];
  cw_phys_filter f = cw_phys_filter_layers(1u << LAYER_BLOCK);
  int n = cw_world3d_overlap(g->w, &q, &f, hit, 1024);
  /* pushes first (pieces of what breaks fly with them), then the harm, both in the overlap's sorted order */
  static int idx[1024];
  static float kk[1024];
  static t3_vec3 dirs[1024];
  int m = 0;
  for (int s = 0; s < n; s++) {
    int i = game_block_of_shape(hit[s]);
    if (i < 0 || !g->b[i].alive) continue;
    t3_vec3 p = game_block_xform(g, i).p;
    t3_vec3 d = vsub(p, at);
    d.y += 0.6f; /* blasts lift */
    float l = vlen(d);
    float dist = vlen(vsub(p, at));
    if (g->b[i].blasted == (uint16_t)g->booms || (dist <= r0 && shell > 1)) continue; /* an earlier shell had it */
    g->b[i].blasted = (uint16_t)g->booms;
    float k = 1 - dist / R;
    if (k <= 0) continue;
    k *= k * K;
    t3_vec3 dn = vscale(d, 1.0f / (l + 1e-4f));
    float mass = MATS[g->b[i].mat].density * g->b[i].vol;
    float imp = CHARGE_IMPULSE * k;
    if (imp > mass * 22) imp = mass * 22;
    chunk_impulse(g, i, vscale(dn, imp), p);
    idx[m] = i, kk[m] = k, dirs[m] = dn, m++;
  }
  chunks_flush(g);
  for (int s = 0; s < m; s++) harm(g, idx[s], CHARGE_ENERGY * kk[s], dirs[s], false);
}

static void blast_begin(game *g, t3_vec3 at, float r, float k) {
  ev_push(g, EV_BOOM, 0, at, v3(0, 1, 0), r, 0, 0);
  g->booms++;
  g->blast_at = at, g->blast_r = r, g->blast_k = k;
  blast_shell(g, 1);
  g->blast_shell = g->no_budget ? 0 : 2;
}

/* the next keg of a chain, once the blast before has run out */
static void kegs_go(game *g) {
  if (g->npend == 0 || g->blast_shell != 0) return;
  if (g->pend_wait > 0) { g->pend_wait--; return; }
  t3_vec3 at = g->pend_at[0];
  bool charge = g->pend_charge[0];
  for (int i = 1; i < g->npend; i++) g->pend_at[i - 1] = g->pend_at[i], g->pend_charge[i - 1] = g->pend_charge[i];
  g->npend--;
  blast_begin(g, at, charge ? CHARGE_R : KEG_R, charge ? 1 : KEG_K);
  g->pend_wait = KEG_DELAY;
}

void game_hit_block(game *g, int i, float e) {
  if (i < 0 || i >= g->nb || !g->b[i].alive) return;
  harm(g, i, e, v3(0, 1, 0), true);
}

static void detonate(game *g, t3_vec3 at) {
  proj_end(g);
  if (g->blast_shell != 0) { /* a keg's blast is running: the charge goes next */
    if (g->npend < MAXPEND) {
      for (int i = g->npend; i > 0; i--) g->pend_at[i] = g->pend_at[i - 1], g->pend_charge[i] = g->pend_charge[i - 1];
      g->pend_at[0] = at, g->pend_charge[0] = 1, g->npend++, g->pend_wait = 0;
    }
    return;
  }
  ev_push(g, EV_BOOM, 0, at, v3(0, 1, 0), CHARGE_R, 0, 0);
  g->booms++;
  g->blast_at = at, g->blast_r = CHARGE_R, g->blast_k = 1;
  blast_shell(g, 1);
  g->blast_shell = g->no_budget ? 0 : 2;
}

/* A ball's strike on the structure. Box3D hands a hit to the one block the ball touches, and the game used to stop
 * there: a 50 kg shot at 51 m/s (65 kJ) broke the 124 kg brick it met (8.1 kJ of strength) and flew on, so a shot took
 * 2 blocks out of a structure of hundreds (median 0.1% of it down, a heavy ball 0.6%) and nothing standing moved. A
 * real ball crushes the masonry round where it lands and drives what is behind it back; the burst does that, once a
 * shot, where the ball first meets a block it is moving fast into:
 *   crush  blocks within crush_r take crush_k of the ball's energy, falling off linearly with the distance (a shot at
 *          the power a level starts with breaks a stone brick, 8.1 kJ + 0.9 kJ, out to 0.86 of crush_r)
 *   drive  blocks within drive_r are pushed along the flight (and a little outward and up): drive_k of the ball's
 *          momentum, falling off with the square of the distance, each at most drive_v m/s (a 124 kg brick at the
 *          point takes the cap, one 1.7 m off about 6 m/s from a shot)
 * Measured on the thirty levels' front faces (tools/humancheck.c --shot), a shot now breaks 35 blocks on average and
 * moves 200 (median 5.5% of a structure down, the top of a small tower off, a breach in a wall) and a heavy ball
 * breaks 92 (median 24%: it topples a tower); a charge is unchanged (median 40%). The ball keeps BURST_KEEP of its
 * speed and goes on through what broke. */
typedef struct burst_info { float crush_r, crush_k, drive_r, drive_k, drive_v; } burst_info;
static const burst_info BURST[NAMMO] = {
  [A_SHOT] = { 1.6f, 1.0f, 3.4f, 1.2f, 10.0f },
  [A_HEAVY] = { 2.2f, 1.0f, 4.6f, 0.9f, 9.0f },
  [A_CHARGE] = { 0, 0, 0, 0, 0 }, /* a charge goes off on its own */
};
#define BURST_MIN_SPEED 8.0f
#define BURST_KEEP 0.5f

/* true when it burst (the strike is spent on it: no single-block harm and punch-through for that hit) */
static bool strike_burst(game *g, t3_vec3 at, t3_vec3 v, int mat_at) {
  const burst_info *bi = &BURST[g->proj_kind];
  float sp = vlen(v);
  g->proj_burst = true;
  if (g->old_damage || bi->drive_r <= 0 || sp < BURST_MIN_SPEED) return false;
  float m = AMMO[g->proj_kind].mass, ke = 0.5f * m * sp * sp, mom = m * sp;
  t3_vec3 dn = vscale(v, 1.0f / sp);
  cw_qshape3d q = cw_qshape3d_sphere(at, bi->drive_r);
  static cw_shape3d hit[1024];
  static int idx[1024];
  static float dist[1024];
  static t3_vec3 dirs[1024];
  cw_phys_filter f = cw_phys_filter_layers(1u << LAYER_BLOCK);
  int n = cw_world3d_overlap(g->w, &q, &f, hit, 1024), k = 0;
  for (int s = 0; s < n; s++) { /* pushes first (pieces of what breaks fly with them), then the harm, in the overlap's order */
    int i = game_block_of_shape(hit[s]);
    if (i < 0 || !g->b[i].alive) continue;
    t3_vec3 p = game_block_xform(g, i).p, r = vsub(p, at);
    float d = vlen(r), w = 1 - d / bi->drive_r;
    if (w <= 0) continue;
    t3_vec3 out = vscale(r, 1.0f / (d + 1e-4f));
    t3_vec3 dir = vadd(dn, vadd(vscale(out, 0.35f), v3(0, 0.15f, 0)));
    dir = vscale(dir, 1.0f / (vlen(dir) + 1e-6f));
    float mass = MATS[g->b[i].mat].density * g->b[i].vol, imp = mom * bi->drive_k * w * w;
    if (imp > mass * bi->drive_v) imp = mass * bi->drive_v;
    chunk_impulse(g, i, vscale(dir, imp), p);
    idx[k] = i, dist[k] = d, dirs[k] = dir, k++;
  }
  chunks_flush(g);
  for (int s = 0; s < k; s++)
    if (dist[s] < bi->crush_r) harm(g, idx[s], ke * bi->crush_k * (1 - dist[s] / bi->crush_r), dirs[s], false);
  if (g->proj_live) cw_body3d_set_velocity(g->proj, vscale(v, BURST_KEEP));
  ev_push(g, EV_STRIKE, mat_at, at, vscale(dn, -1), bi->crush_r, sp, (int)fminf(ke, 2e9f)); /* dust, chips, the thump and the pad's kick where it struck */
  return true;
}

/* the hits of the last step */
static void read_hits(game *g, t3_vec3 proj_v_before) {
  int n;
  const cw_phys_event3d *ev = cw_world3d_events(g->w, &n);
  bool proj_broke = false;
  float proj_used = 0;
  int fq[64], nfq = 0;
  float fqe[64];
  t3_vec3 fqp[64];
  for (int e = 0; e < n; e++) {
    const cw_phys_event3d *h = &ev[e];
    if (h->type != CW_PHYS_HIT) continue;
    g->hits++;
    bool pa = g->proj_live && h->body_a.id == g->proj.id, pb = g->proj_live && h->body_b.id == g->proj.id;
    int ia = game_block_of_shape(h->shape_a), ib = game_block_of_shape(h->shape_b);
    bool da = ia >= 0 && g->ch[g->b[ia].chunk].dyn, db = ib >= 0 && g->ch[g->b[ib].chunk].dyn;
    /* the energy of a blow is the reduced mass of the two blocks that meet (a standing block, like the ground, does not give) */
    float ma = pa ? cw_body3d_mass(h->body_a) : da ? MATS[g->b[ia].mat].density * g->b[ia].vol : 0;
    float mb = pb ? cw_body3d_mass(h->body_b) : db ? MATS[g->b[ib].mat].density * g->b[ib].vol : 0;
    float mu = ma > 0 && mb > 0 ? ma * mb / (ma + mb) : ma > 0 ? ma : mb;
    float en = 0.5f * mu * h->speed * h->speed;
    /* a whole chunk landing on a standing one gives it the momentum it lost, and breaks where it lands */
    if (!pa && !pb && (da || db) && ia >= 0 && ib >= 0) {
      int ca = g->b[ia].chunk, cb = g->b[ib].chunk;
      if (da != db) { /* one falls on one that stands: the standing chunk is pushed (and goes if that moves it), by the reduced mass of the two chunks */
        int cd = da ? ca : cb, cs = da ? cb : ca, bs = da ? ib : ia;
        float md = cw_body3d_mass(g->ch[cd].body), ms = g->ch[g->ch[cs].leader].mass;
        float mc = md * ms / (md + ms + 1e-6f);
        chunk_impulse(g, bs, vscale(h->normal, (da ? 1.0f : -1.0f) * mc * h->speed), h->point);
      }
      if (h->speed >= FRAC_SPEED) {
        if (da && g->ch[ca].n > 1 && g->tick - g->ch[ca].frac_tick > FRAC_COOL && nfq < 64) fqe[nfq] = 0.5f * cw_body3d_mass(g->ch[ca].body) * h->speed * h->speed, fqp[nfq] = h->point, fq[nfq++] = ca;
        if (db && g->ch[cb].n > 1 && g->tick - g->ch[cb].frac_tick > FRAC_COOL && nfq < 64) fqe[nfq] = 0.5f * cw_body3d_mass(g->ch[cb].body) * h->speed * h->speed, fqp[nfq] = h->point, fq[nfq++] = cb;
      }
    } else if ((da || db) && h->speed >= FRAC_SPEED && nfq < 64) { /* ... onto the ground, or struck by the shot */
      int c = da ? g->b[ia].chunk : g->b[ib].chunk;
      if (g->ch[c].n > 1 && g->tick - g->ch[c].frac_tick > FRAC_COOL) fqe[nfq] = 0.5f * cw_body3d_mass(g->ch[c].body) * h->speed * h->speed, fqp[nfq] = h->point, fq[nfq++] = c;
    }
    if (pa || pb) {
      if (g->proj_hit_tick < 0) g->proj_hit_tick = g->tick, g->impact = h->point;
      if (g->proj_kind == A_CHARGE && g->charge_armed && g->proj_ticks > 2) {
        detonate(g, h->point);
        continue;
      }
      if (!g->proj_burst && g->proj_kind != A_CHARGE && (pa ? ib : ia) >= 0 && strike_burst(g, h->point, proj_v_before, g->b[pa ? ib : ia].mat)) {
        proj_broke = false; /* the first block it strikes: the burst, and no punch-through sum after it */
        continue;
      }
    }
    /* sounds and dust for anything that lands hard enough to hear */
    if (en > 400) {
      int mat = ia >= 0 ? g->b[ia].mat : ib >= 0 ? g->b[ib].mat : M_STONE;
      if (pa || pb) mat = ia >= 0 ? g->b[ia].mat : ib >= 0 ? g->b[ib].mat : M_STONE;
      ev_push(g, ia < 0 && ib < 0 ? EV_LAND : EV_HIT, mat, h->point, h->normal, 0, h->speed, (int)fminf(en, 2e9f));
    }
    t3_vec3 dir = h->normal;
    if (ib >= 0) {
      bool was = g->b[ib].alive;
      harm(g, ib, en, dir, pa);
      if (pa && was && !g->b[ib].alive) proj_broke = true, proj_used += MATS[g->b[ib].mat].toughness * g->b[ib].vol;
    }
    if (ia >= 0) {
      bool was = g->b[ia].alive;
      harm(g, ia, en, vscale(dir, -1), pb);
      if (pb && was && !g->b[ia].alive) proj_broke = true, proj_used += MATS[g->b[ia].mat].toughness * g->b[ia].vol;
    }
  }
  chunks_flush(g);
  for (int q = 0; q < nfq; q++) /* a chunk that landed hard breaks (once however many of its blocks met the ground) */
    if (g->ch[fq[q]].alive && g->ch[fq[q]].dyn && g->ch[fq[q]].n > 1 && g->tick - g->ch[fq[q]].frac_tick > FRAC_COOL) {
      int mat = g->b[g->ch[fq[q]].head].mat;
      chunk_fracture(g, fq[q]);
      if (fqe[q] > 3000) ev_push(g, EV_CRUMBLE, mat, fqp[q], v3(0, 1, 0), 0, 0, (int)fminf(fqe[q], 2e9f));
    }
  /* a shot that broke what it hit keeps the speed the break did not use */
  if (proj_broke && g->proj_live) {
    float m = AMMO[g->proj_kind].mass, v2 = vdot(proj_v_before, proj_v_before);
    float ke = 0.5f * m * v2;
    float keep = ke > 0 ? 1 - proj_used / ke : 0;
    if (keep < 0.15f) keep = 0.15f;
    if (keep > 0.92f) keep = 0.92f;
    cw_body3d_set_velocity(g->proj, vscale(proj_v_before, sqrtf(keep)));
  }
}

static void aim(game *g, const input *in) {
  g->power = AMMO_POWER[g->ammo_sel]; /* (shown, never set by a player) */
  int dt = (in->right ? 1 : 0) - (in->left ? 1 : 0), dr = (in->up ? 1 : 0) - (in->down ? 1 : 0);
  g->hold_turn = dt ? g->hold_turn + 1 : 0;
  g->hold_raise = dr ? g->hold_raise + 1 : 0;
  /* a tap moves a hair; held, it speeds up */
  float kt = in->turn_k > 0 ? in->turn_k : 1, kr = in->raise_k > 0 ? in->raise_k : 1;
  float rt = (0.0015f + 0.009f * fminf(1, g->hold_turn / 45.0f)) * kt, rr = (0.0015f + 0.007f * fminf(1, g->hold_raise / 45.0f)) * kr;
  g->yaw = cw_clampf(g->yaw - dt * rt, -0.75f, 0.75f);
  g->pitch = cw_clampf(g->pitch + dr * rr, -0.08f, 1.5f);
  g->yaw = cw_clampf(g->yaw - in->aim_dx, -0.75f, 0.75f);
  g->pitch = cw_clampf(g->pitch - in->aim_dy, -0.08f, 1.5f);
  if (in->ammo_set > 0 && in->ammo_set <= NAMMO && g->ammo[in->ammo_set - 1] > 0) g->ammo_sel = in->ammo_set - 1;
  /* the launcher's place on its circle: held, it rolls faster (a full turn round the structure in about 9 s); the aim
   * keeps its turn and raise, so it still points at the middle from the new place */
  int dorb = (in->orbit_right ? 1 : 0) - (in->orbit_left ? 1 : 0);
  g->hold_orbit = dorb ? g->hold_orbit + 1 : 0;
  if (dorb) {
    float ko = in->orbit_k > 0 ? in->orbit_k : 1;
    g->orbit += dorb * (0.002f + 0.010f * fminf(1, g->hold_orbit / 40.0f)) * ko;
    if (g->orbit > 3.14159265f) g->orbit -= 6.2831853f;
    if (g->orbit < -3.14159265f) g->orbit += 6.2831853f;
    place_launcher(g);
  }
  if (in->ammo_next || in->ammo_prev) {
    int d = in->ammo_next ? 1 : NAMMO - 1;
    for (int k = 0; k < NAMMO; k++) {
      g->ammo_sel = (g->ammo_sel + d) % NAMMO;
      if (g->ammo[g->ammo_sel] > 0) break;
    }
  }
  if (g->ammo[g->ammo_sel] <= 0)
    for (int k = 0; k < NAMMO; k++) if (g->ammo[k] > 0) { g->ammo_sel = k; break; }
}

static int ammo_left(const game *g) {
  int n = 0;
  for (int a = 0; a < NAMMO; a++) n += g->ammo[a];
  return n;
}

/* points for blocks the turn brought down: a placed block that left its home by more than its own size */
/* the level ends the instant its last target is down: the rubble goes on falling, nothing waits for it (old_settle: the control waits for stillness) */
static bool win_now(const game *g) { return !g->old_settle && !g->old_rules && !g->sandbox && g->tgt_left == 0 && g->ntgt > 0; }

static void tally(game *g) {
  for (int i = 0; i < g->nb; i++) {
    block *k = &g->b[i];
    if (!k->alive || !k->tag || k->down || k->target >= 0) continue;
    t3_vec3 p = game_block_xform(g, i).p;
    float lim = fmaxf(0.6f, fmaxf(k->half.x, fmaxf(k->half.y, k->half.z)));
    if (vlen(vsub(p, k->home)) > lim) { k->down = true; g->P[g->cur].score += (int)(k->vol * MATS[k->mat].points * 10 + 1); g->down_count++; }
  }
}

/* the state itself, apart from the world it lives in (the world's own hash folds in body ids, which carry the
 * world's index, so two worlds stepping the same never hash alike): every block slot, its pose's bits, the counters */
uint32_t game_state_hash(const game *g) {
  uint32_t h = hash32((uint32_t)g->tick * 0x9e3779b9u ^ (uint32_t)g->nb);
  for (int i = 0; i < g->nb; i++) {
    const block *k = &g->b[i];
    h = hash32(h ^ (uint32_t)k->alive ^ ((uint32_t)k->mat << 8) ^ ((uint32_t)k->gen << 16));
    if (!k->alive) continue;
    cw_xform3 x = game_block_xform(g, i);
    float f[7] = { x.p.x, x.p.y, x.p.z, x.q.x, x.q.y, x.q.z, x.q.w };
    uint32_t u[7];
    memcpy(u, f, sizeof u);
    for (int j = 0; j < 7; j++) h = hash32(h ^ u[j]);
  }
  h = hash32(h ^ (uint32_t)g->shots ^ ((uint32_t)g->broken << 12) ^ ((uint32_t)g->tgt_left << 24));
  for (int p = 0; p < g->players; p++) h = hash32(h ^ (uint32_t)g->P[p].score);
  return h;
}

bool game_over(const game *g) { return g->state == GS_WON || g->state == GS_LOST; }

/* a placed block standing away from home: more than its own size, and at least 0.6 m (the tally's rule) */
static bool block_moved(const game *g, const block *k) {
  t3_vec3 p = game_block_xform(g, (int)(k - g->b)).p;
  float lim = fmaxf(0.6f, fmaxf(k->half.x, fmaxf(k->half.y, k->half.z)));
  return vlen(vsub(p, k->home)) > lim;
}

float game_mark(const game *g) { return g->L->mark > 0 ? g->L->mark : KIT_MARK[g->L->kind]; }

float game_share(const game *g) {
  if (g->vol_total <= 0) return 0;
  float v = g->vol_gone;
  for (int i = 0; i < g->nb; i++) {
    const block *k = &g->b[i];
    if (k->alive && k->tag == 1 && k->target < 0 && block_moved(g, k)) v += k->vol;
  }
  return fminf(1, v / g->vol_total);
}

/* The stars, given once the world is still (game_over is only true then):
 *   1  every crystal is down
 *   2  ... within par
 *   3  ... and the structure is brought down (the level's mark of it, game_mark)
 * The control (old_rules) is the rule before the fix: three for any clear within par, two within par + 2. */
int game_stars(const game *g) {
  if (g->state != GS_WON || g->mode == MODE_PARTY) return 0;
  if (g->old_rules) return g->shots <= g->L->par ? 3 : g->shots <= g->L->par + 2 ? 2 : 1;
  if (g->shots > g->L->par) return 1;
  return g->judged_share >= game_mark(g) ? 3 : 2;
}

int game_result(const game *g) {
  if (g->state == GS_LOST) return RES_LOST;
  if (g->state != GS_WON) return RES_NONE;
  if (g->mode == MODE_PARTY) return RES_PARTY;
  if (g->old_rules) return RES_BROUGHT_DOWN;
  return g->judged_share >= game_mark(g) ? RES_BROUGHT_DOWN : RES_CLEARED;
}

void game_step(game *g, const input *in) {
  double t_start = sim_clock();
  g->nev = 0;
  g->tick++, g->state_t++;
  if (g->state == GS_AIM) {
    aim(g, in);
    if (in->fire && g->ammo[g->ammo_sel] > 0) game_fire(g);
  }
  t3_vec3 pv = g->proj_live ? cw_body3d_velocity(g->proj) : v3(0, 0, 0);
  double t_pre = sim_clock();
  cw_world3d_step(g->w, TICK);
  double t_world = sim_clock();
  cw_phys_stats st = cw_world3d_stats(g->w);
  g->step_ms = st.step_ms;
  if (st.step_ms > g->step_ms_max) {
    g->step_ms_max = st.step_ms;
    /* measuring: Box3D's own split of the slowest step (phys.worst: step, pairs, collide, solve, split islands, refit,
     * bullets, sleep islands, transforms, hit events) */
    b3Profile pr = b3World_GetProfile(b3LoadWorldId((uint32_t)g->w.id));
    const float v[10] = { pr.step, pr.pairs, pr.collide, pr.solve, pr.splitIslands, pr.refit, pr.bullets, pr.sleepIslands, pr.transforms, pr.hitEvents };
    memcpy(g_phys_worst, v, sizeof v);
  }
  if (st.awake_bodies > g->max_awake) g->max_awake = st.awake_bodies;
  g->awake_now = st.awake_bodies;
  read_hits(g, pv);
  if (g->blast_shell > 0) { /* the blast goes on outward */
    blast_shell(g, g->blast_shell);
    g->blast_shell = g->blast_shell >= BLAST_SHELLS ? 0 : g->blast_shell + 1;
  }
  kegs_go(g);
  int broke0 = g->broken;
  break_queued(g);
  chunks_step(g);
  g->breaks_tick = g->broken - broke0;
  /* the shot */
  if (g->proj_live) {
    g->proj_ticks++;
    cw_xform3 x = cw_body3d_transform(g->proj);
    g->proj_pos = x.p, g->proj_vel = cw_body3d_velocity(g->proj);
    if (g->proj_kind == A_CHARGE && g->charge_armed && g->proj_ticks > 2) {
      cw_contact3d c[4];
      if (cw_body3d_contacts(g->proj, c, 4) > 0 || x.p.y < AMMO[A_CHARGE].radius + 0.05f) detonate(g, x.p);
    }
    if (g->proj_live) {
      float sp = vlen(g->proj_vel);
      bool gone = x.p.y < -5 || fabsf(x.p.x) > 200 || fabsf(x.p.z) > 200 || g->proj_ticks > 60 * 14;
      bool still = !cw_body3d_awake(g->proj) || (sp < 0.25f && g->proj_ticks > 60);
      if (x.p.y < AMMO[g->proj_kind].radius + 0.02f && g->proj_hit_tick < 0) g->proj_hit_tick = g->tick, g->impact = x.p;
      if (gone || still) proj_end(g);
    }
  }
  /* blocks that left the field */
  if (g->tick % 30 == 0)
    for (int i = 0; i < g->nb; i++)
      if (g->b[i].alive && game_block_xform(g, i).p.y < -8) kill_block(g, i);
  /* targets: broken (above), knocked off their place, or fallen */
  for (int t = 0; t < g->ntgt; t++) {
    target_state *ts = &g->tgt[t];
    if (ts->down || ts->blk < 0) continue;
    t3_vec3 p = game_block_xform(g, ts->blk).p;
    if (vlen(vsub(p, ts->home)) > 0.9f || p.y < ts->home.y - 0.5f) target_down(g, t);
  }
  /* turns */
  if (g->state == GS_FLIGHT) {
    if (!g->proj_live || (g->proj_hit_tick >= 0 && g->tick - g->proj_hit_tick > 40) || g->state_t > 60 * 6 || win_now(g)) g->state = GS_SETTLE, g->state_t = 0, g->settle_quiet = 0;
  } else if (g->state == GS_SETTLE) {
    int awake = st.awake_bodies - (g->proj_live ? 1 : 0);
    g->settle_quiet = awake <= SETTLE_AWAKE && g->npend == 0 && g->blast_shell == 0 ? g->settle_quiet + 1 : 0;
    if (g->old_rules && g->tgt_left == 0 && g->state_t > 90) g->settle_quiet = 999; /* the control: judged while it falls */
    if (g->settle_quiet > SETTLE_QUIET || g->state_t > SETTLE_MAX || win_now(g)) {
      g->judge_tick = g->tick;
      g->rating_t = win_now(g) ? 1 : 0; /* judged at once: the rating goes on reading the rubble while the card shows */
      proj_end(g);
      tally(g);
      g->judged_awake = awake;
      g->judged_share = game_share(g);
      int in = 0;
      if (g->mode == MODE_PARTY) { /* the turn: a strike for bringing nothing down; who is still in */
        player_state *ps = &g->P[g->cur];
        if (!ps->out && g->down_count == g->down_at_fire && ++ps->strikes >= PARTY_STRIKES) {
          int n = 0;
          for (int p = 0; p < g->players; p++) n += g->P[p].out != 0;
          ps->out = 2, ps->out_order = (int8_t)(n + 1);
        }
        g->party_winner = -1;
        for (int p = 0; p < g->players; p++) if (!g->P[p].out) in++, g->party_winner = p;
        if (in != 1) g->party_winner = -1;
      }
      if (g->mode == MODE_PARTY && (in <= 1 || g->tgt_left == 0)) g->state = GS_WON; /* the party is over */
      else if (g->mode == MODE_PARTY) {
        g->state = GS_AIM;
        do g->cur = (g->cur + 1) % g->players; while (g->P[g->cur].out);
      } else if (g->sandbox) g->state = GS_AIM; /* free fire: never judged */
      else if (g->tgt_left == 0) g->state = GS_WON;
      else if (ammo_left(g) == 0) g->state = GS_LOST;
      else {
        g->state = GS_AIM;
        if (g->players > 1) g->cur = (g->cur + 1) % g->players;
      }
      g->state_t = 0;
    }
  }
  /* the card is up and the rubble still falls: the share it shows only grows, until the world is still (at most 8 s) */
  if (g->rating_t > 0 && game_over(g)) {
    int aw = st.awake_bodies;
    g->judged_share = fmaxf(g->judged_share, game_share(g));
    g->rating_quiet = aw <= SETTLE_AWAKE && g->npend == 0 && g->blast_shell == 0 ? g->rating_quiet + 1 : 0;
    if (g->rating_quiet > SETTLE_QUIET || ++g->rating_t > 480) g->rating_t = 0;
  }
  /* where the collapse is (the camera follows): the middle of the chunks in motion, and their spread */
  if (g->tick % 3 == 0) {
    t3_vec3 s = v3(0, 0, 0);
    int n = 0;
    for (int c = 0; c < g->nch; c++) {
      const chunk *k = &g->ch[c];
      if (!k->alive || !k->dyn || !cw_body3d_awake(k->body)) continue;
      if (k->n < 2 && g->awake_now > 40) continue; /* with a heap of rubble moving, its pieces and bigger */
      s = vadd(s, game_block_xform(g, k->head).p), n++;
    }
    if (n > 0) {
      t3_vec3 m = vscale(s, 1.0f / (float)n);
      float r = 0;
      for (int c = 0; c < g->nch; c++) {
        const chunk *k = &g->ch[c];
        if (!k->alive || !k->dyn || !cw_body3d_awake(k->body) || (k->n < 2 && g->awake_now > 40)) continue;
        t3_vec3 p = game_block_xform(g, k->head).p;
        float d = vlen(vsub(p, m));
        if (d > r) r = d;
      }
      g->hot_c = m, g->hot_r = r, g->hot_n = n;
    } else g->hot_n = 0;
  }
  /* the state hash: the world's (every moved transform and event) with the game's own counters */
  uint32_t h = st.hash;
  h = hash32(h ^ (uint32_t)g->tgt_left * 0x9e3779b9u);
  h = hash32(h ^ (uint32_t)g->shots * 0x85ebca6bu ^ (uint32_t)g->broken * 0xc2b2ae35u);
  h = hash32(h ^ (uint32_t)g->nalive);
  g->hash = h;
  double t_end = sim_clock();
  g->tick_ms = (float)((t_end - t_start) * 1000), g->world_ms = (float)((t_world - t_pre) * 1000);
  g->pre_ms = (float)((t_pre - t_start) * 1000), g->post_ms = (float)((t_end - t_world) * 1000);
}

/* a structure at rest starts asleep: contacts are made with gravity off for two steps (islands form), then every body is put to sleep, so nothing drifts before a hit */
void game_sleep_all(game *g) {
  static cw_xform3 D[MAXBLK];
  for (int c = 0; c < g->nch; c++) D[c] = g->ch[c].alive && g->ch[c].dyn ? cw_body3d_transform(g->ch[c].body) : (cw_xform3){ { 0, 0, 0 }, { 0, 0, 0, 1 } };
  for (int t = 0; t < 2; t++) cw_world3d_step(g->w, TICK); /* contacts are made and the islands form */
  for (int c = 0; c < g->nch; c++) {
    if (!g->ch[c].alive || !g->ch[c].dyn) continue;
    b3BodyId rb = cw_body3d_raw(g->ch[c].body);
    cw_body3d_set_transform(g->ch[c].body, D[c]);
    cw_body3d_set_velocity(g->ch[c].body, v3(0, 0, 0)), cw_body3d_set_angular_velocity(g->ch[c].body, v3(0, 0, 0));
    if (b3Body_IsAwake(rb)) b3Body_SetAwake(rb, false);
  }
  g->nev = 0;
}

void game_charge_at(game *g, t3_vec3 at) { detonate(g, at); }

/* a leg of the structure taken out (the checks, and the look sheets: dm set SCENE legout): the placed blocks of a third of its footprint round the foot
 * furthest from its middle, up to 2.2 m, break; with charge, a charge at the foot of the middle instead.
 * Returns the blocks broken. */
int game_legout(game *g, bool charge) {
  float cx = 0, cz = 0;
  int cn = 0, foot = -1, n = 0;
  float fd = -1;
  for (int i = 0; i < g->nb; i++) {
    const block *k = &g->b[i];
    if (k->alive && k->tag == 1 && k->target < 0 && k->home.y < 1.0f) cx += k->home.x, cz += k->home.z, cn++;
  }
  if (cn) cx /= (float)cn, cz /= (float)cn;
  for (int i = 0; i < g->nb; i++) {
    const block *k = &g->b[i];
    if (!k->alive || k->tag != 1 || k->target >= 0 || k->home.y >= 1.0f) continue;
    float d = (k->home.x - cx) * (k->home.x - cx) + (k->home.z - cz) * (k->home.z - cz);
    if (d > fd) fd = d, foot = i;
  }
  t3_vec3 at = foot >= 0 ? g->b[foot].home : v3(cx, 0.5f, cz);
  if (charge) { detonate(g, v3(at.x * 0.9f + cx * 0.1f, 0.5f, at.z * 0.9f + cz * 0.1f)); return 0; } /* at the foot, on the face */
  /* a leg: the third of the footprint (a wedge of 120 degrees) round the foot furthest out, from 0.35 of its reach outward, up to 2.2 m */
  float R = sqrtf(fd), fx = at.x - cx, fz = at.z - cz;
  for (int i = 0; i < g->nb; i++) {
    const block *k = &g->b[i];
    if (!k->alive || k->tag != 1 || k->target >= 0 || k->home.y > 2.2f) continue;
    float px = k->home.x - cx, pz = k->home.z - cz, r = sqrtf(px * px + pz * pz);
    if (r < 0.35f * R || (px * fx + pz * fz) < 0.5f * r * R) continue;
    harm(g, i, 1e9f, v3(0, 1, 0), true), n++;
  }
  return n;
}
