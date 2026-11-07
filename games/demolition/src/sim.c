/* demolition's simulation: the world, the blocks and how they break, the launcher, the shot, charges, targets and
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
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

const material_info MATS[M_COUNT] = {
  [M_WOOD] = { "wood", 620, 0.65f, 0.05f, 52000, 160, 2, 10 },
  [M_STONE] = { "stone", 2300, 0.75f, 0.02f, 150000, 900, 4, 14 },
  [M_GLASS] = { "glass", 2500, 0.4f, 0.1f, 9000, 30, 0, 8 },
  [M_STEEL] = { "steel", 2600, 0.5f, 0.05f, 900000, 6000, 2, 20 }, /* girders: an I-section fills a third of its box */
  [M_TARGET] = { "crystal", 900, 0.5f, 0.1f, 6000, 60, 0, 0 },
};
const ammo_info AMMO[NAMMO] = {
  [A_SHOT] = { "Shot", 0.22f, 50, 18, 62 },
  [A_HEAVY] = { "Heavy ball", 0.36f, 210, 14, 40 },
  [A_CHARGE] = { "Charge", 0.26f, 30, 16, 52 },
};

#define USER_PROJ ((void *)(intptr_t)(MAXBLK + 7))
#define CHARGE_R 4.6f
#define CHARGE_IMPULSE 5200.0f
#define CHARGE_ENERGY 260000.0f
#define LAYER_GROUND 0
#define LAYER_BLOCK 1
#define LAYER_PROJ 2

static void ev_push(game *g, int kind, int mat, t3_vec3 p, t3_vec3 n, float size, float speed, int a) {
  if (g->nev >= MAXEV) return;
  g->ev[g->nev++] = (sim_event){ (uint8_t)kind, (uint8_t)mat, p, n, size, speed, a };
}

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

static int alloc_block(game *g) {
  if (g->nfree > 0) return g->free_list[--g->nfree];
  if (g->nb < MAXBLK) return g->nb++;
  return -1;
}

static int block_of(cw_body3d b) {
  if (!cw_body3d_valid(b)) return -1;
  intptr_t u = (intptr_t)cw_body3d_user(b);
  if (u <= 0 || u > MAXBLK) return -1;
  return (int)u - 1;
}

static int make_block(game *g, int mat, t3_vec3 c, t3_vec3 half, t3_quat q, t3_vec3 v, t3_vec3 w, int gen) {
  int i = alloc_block(g);
  if (i < 0) return -1;
  block *k = &g->b[i];
  memset(k, 0, sizeof *k);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.position = c, bd.rotation = q, bd.linear_velocity = v, bd.angular_velocity = w;
  bd.angular_damping = gen ? 0.4f : 0.05f, bd.linear_damping = gen ? 0.05f : 0.0f;
  bd.user = (void *)(intptr_t)(i + 1);
  bd.interpolate = true;
  k->body = cw_body3d_create(g->w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  const material_info *m = &MATS[mat];
  sd.density = m->density;
  sd.material.friction = m->friction, sd.material.restitution = m->restitution;
  sd.material.user_material = (uint64_t)mat;
  sd.layer = LAYER_BLOCK;
  sd.hit_events = true;
  sd.contact_events = false;
  cw_shape3d_box(k->body, &sd, half, cw_xform3_identity());
  k->half = half;
  k->vol = 8 * half.x * half.y * half.z;
  k->hp = m->toughness * k->vol * (gen ? 0.75f : 1.0f);
  k->mat = (uint8_t)mat, k->gen = (uint8_t)gen, k->alive = 1, k->target = -1;
  k->tint = (uint8_t)(hash32((uint32_t)i * 2654435761u + g->L->seed) & 0xff);
  k->born = g->tick;
  k->home = c;
  g->nalive++;
  return i;
}

int game_add_block(game *g, int mat, t3_vec3 c, t3_vec3 half, float yaw) {
  float s, co;
  cw_sincos(yaw * 0.5f, &s, &co);
  int i = make_block(g, mat, c, half, (t3_quat){ 0, s, 0, co }, v3(0, 0, 0), v3(0, 0, 0), 0);
  if (i >= 0) g->b[i].tag = 1;
  return i;
}

int game_add_target(game *g, t3_vec3 c) {
  if (g->ntgt >= MAXTGT) return -1;
  int i = make_block(g, M_TARGET, c, v3(0.22f, 0.32f, 0.22f), (t3_quat){ 0, 0, 0, 1 }, v3(0, 0, 0), v3(0, 0, 0), 0);
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
  g->tgt_left--;
  g->P[g->cur].targets++;
  g->P[g->cur].score += 1000;
  ev_push(g, EV_TARGET, M_TARGET, ts->blk >= 0 && g->b[ts->blk].alive ? cw_body3d_transform(g->b[ts->blk].body).p : ts->home, v3(0, 1, 0), 0.5f, 0, t);
}

static void kill_block(game *g, int i) {
  block *k = &g->b[i];
  if (!k->alive) return;
  if (k->target >= 0) {
    g->tgt[k->target].blk = -1;
    target_down(g, k->target);
  }
  if (k->tag && !k->down) { k->down = true; g->P[g->cur].score += (int)(k->vol * MATS[k->mat].points * 10 + 1); }
  cw_body3d_destroy(k->body);
  k->alive = 0;
  g->nalive--;
  g->free_list[g->nfree++] = i;
}

/* break block i: pieces (or dust) and the event the view and sound read; dir is the hit's direction (may be 0) */
static void break_block(game *g, int i, t3_vec3 dir) {
  block *k = &g->b[i];
  if (!k->alive) return;
  cw_xform3 x = cw_body3d_transform(k->body);
  t3_vec3 v = cw_body3d_velocity(k->body), w = cw_body3d_angular_velocity(k->body);
  t3_vec3 h = k->half;
  int mat = k->mat, gen = k->gen;
  float big = fmaxf(h.x, fmaxf(h.y, h.z));
  bool glass = MATS[mat].pieces == 0;
  ev_push(g, glass ? EV_SHATTER : EV_BREAK, mat, x.p, dir, big * 2, vlen(v), i);
  g->broken++;
  kill_block(g, i);
  if (glass || gen >= 2 || big < 0.13f || g->nalive >= BODY_CAP) return;
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
    int j = make_block(g, mat, vadd(x.p, r), half, x.q, pv, pw, gen + 1);
    if (j >= 0) g->fragments++;
  }
}

static void harm(game *g, int i, float e, t3_vec3 dir) {
  block *k = &g->b[i];
  if (!k->alive) return;
  float f = MATS[k->mat].min_hit;
  if (e <= f) return;
  k->hp -= e - f;
  if (k->hp <= 0) break_block(g, i, dir);
}

/* ── the world ───────────────────────────────────────────────────────── */

void game_free(game *g) {
  if (cw_world3d_valid(g->w)) cw_world3d_destroy(g->w);
  g->w = (cw_world3d){ 0 };
}

void game_new(game *g, int level, int players, int mode) {
  game_free(g);
  memset(g, 0, sizeof *g);
  g->level = level;
  g->L = level_get(level);
  g->players = players < 1 ? 1 : players > MAXP ? MAXP : players;
  g->mode = mode;
  cw_world3d_def wd = cw_world3d_default_def();
  wd.gravity = v3(0, -GRAV, 0);
  wd.substeps = 4;
  wd.hit_speed = 1.2f;
  wd.restitution_threshold = 2.0f;
  wd.layers[LAYER_GROUND] = "ground", wd.layers[LAYER_BLOCK] = "block", wd.layers[LAYER_PROJ] = "shot";
  g->w = cw_world3d_create(&wd);
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
  level_build(g, g->L);
  for (int a = 0; a < NAMMO; a++) g->ammo[a] = g->L->ammo[a];
  g->muzzle = v3(0, 1.7f, g->L->range);
  g->yaw = 0, g->pitch = 0.12f, g->power = 0.75f;
  g->ammo_sel = A_SHOT;
  /* the structure's size (the camera frames it) */
  float ext = 3, top = 2;
  for (int i = 0; i < g->nb; i++) {
    if (!g->b[i].alive) continue;
    t3_vec3 p = g->b[i].home;
    float r = sqrtf(p.x * p.x + p.z * p.z) + vlen(g->b[i].half);
    if (r > ext) ext = r;
    if (p.y + g->b[i].half.y > top) top = p.y + g->b[i].half.y;
  }
  g->extent = ext, g->top = top;
  g->state = GS_AIM;
}

void game_settle(game *g, int ticks) {
  input none = { 0 };
  int st = g->state;
  g->state = GS_SETTLE;
  for (int t = 0; t < ticks; t++) cw_world3d_step(g->w, TICK);
  (void)none;
  g->state = st;
  /* where everything rests now is home */
  for (int i = 0; i < g->nb; i++)
    if (g->b[i].alive) g->b[i].home = cw_body3d_transform(g->b[i].body).p;
  for (int t = 0; t < g->ntgt; t++) if (g->tgt[t].blk >= 0) g->tgt[t].home = g->b[g->tgt[t].blk].home;
  g->nev = 0;
}

int game_awake(const game *g) { return cw_world3d_stats(g->w).awake_bodies; }

t3_vec3 game_aim_dir(const game *g) {
  float sy, cy, sp, cp;
  cw_sincos(g->yaw, &sy, &cy);
  cw_sincos(g->pitch, &sp, &cp);
  return v3(-sy * cp, sp, -cy * cp);
}
float game_speed(const game *g) {
  const ammo_info *a = &AMMO[g->ammo_sel];
  return a->vmin + (a->vmax - a->vmin) * g->power;
}
t3_vec3 game_block_pos(const game *g, int i) { return cw_body3d_transform(g->b[i].body).p; }

void game_fire(game *g) {
  int a = g->ammo_sel;
  if (g->ammo[a] <= 0 || g->proj_live) return;
  g->ammo[a]--;
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
  g->proj_kind = a, g->proj_ticks = 0, g->proj_hit_tick = -1, g->proj_live = true, g->charge_armed = a == A_CHARGE;
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

static void detonate(game *g, t3_vec3 at) {
  ev_push(g, EV_BOOM, 0, at, v3(0, 1, 0), CHARGE_R, 0, 0);
  g->booms++;
  proj_end(g);
  cw_qshape3d q = cw_qshape3d_sphere(at, CHARGE_R);
  static cw_shape3d hit[1024];
  cw_phys_filter f = cw_phys_filter_layers(1u << LAYER_BLOCK);
  int n = cw_world3d_overlap(g->w, &q, &f, hit, 1024);
  /* pushes first (pieces of what breaks fly with them), then the harm, both in the overlap's sorted order */
  static int idx[1024];
  static float kk[1024];
  static t3_vec3 dirs[1024];
  int m = 0;
  for (int s = 0; s < n; s++) {
    int i = block_of(cw_shape3d_body(hit[s]));
    if (i < 0 || !g->b[i].alive) continue;
    t3_vec3 p = cw_body3d_transform(g->b[i].body).p;
    t3_vec3 d = vsub(p, at);
    d.y += 0.6f; /* blasts lift */
    float l = vlen(d);
    float dist = vlen(vsub(p, at));
    float k = 1 - dist / CHARGE_R;
    if (k <= 0) continue;
    k *= k;
    t3_vec3 dn = vscale(d, 1.0f / (l + 1e-4f));
    float mass = cw_body3d_mass(g->b[i].body);
    float imp = CHARGE_IMPULSE * k;
    if (imp > mass * 22) imp = mass * 22;
    cw_body3d_apply_impulse(g->b[i].body, vscale(dn, imp), NULL);
    idx[m] = i, kk[m] = k, dirs[m] = dn, m++;
  }
  for (int s = 0; s < m; s++) harm(g, idx[s], CHARGE_ENERGY * kk[s], dirs[s]);
}

/* the hits of the last step */
static void read_hits(game *g, t3_vec3 proj_v_before) {
  int n;
  const cw_phys_event3d *ev = cw_world3d_events(g->w, &n);
  bool proj_broke = false;
  float proj_used = 0;
  for (int e = 0; e < n; e++) {
    const cw_phys_event3d *h = &ev[e];
    if (h->type != CW_PHYS_HIT) continue;
    g->hits++;
    bool pa = g->proj_live && h->body_a.id == g->proj.id, pb = g->proj_live && h->body_b.id == g->proj.id;
    int ia = block_of(h->body_a), ib = block_of(h->body_b);
    float ma = cw_body3d_valid(h->body_a) ? cw_body3d_mass(h->body_a) : 0;
    float mb = cw_body3d_valid(h->body_b) ? cw_body3d_mass(h->body_b) : 0;
    float mu = ma > 0 && mb > 0 ? ma * mb / (ma + mb) : ma > 0 ? ma : mb;
    float en = 0.5f * mu * h->speed * h->speed;
    if (pa || pb) {
      if (g->proj_hit_tick < 0) g->proj_hit_tick = g->tick, g->impact = h->point;
      if (g->proj_kind == A_CHARGE && g->charge_armed && g->proj_ticks > 2) {
        detonate(g, h->point);
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
      harm(g, ib, en, dir);
      if (pa && was && !g->b[ib].alive) proj_broke = true, proj_used += MATS[g->b[ib].mat].toughness * g->b[ib].vol;
    }
    if (ia >= 0) {
      bool was = g->b[ia].alive;
      harm(g, ia, en, vscale(dir, -1));
      if (pb && was && !g->b[ia].alive) proj_broke = true, proj_used += MATS[g->b[ia].mat].toughness * g->b[ia].vol;
    }
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
  int dt = (in->right ? 1 : 0) - (in->left ? 1 : 0), dr = (in->up ? 1 : 0) - (in->down ? 1 : 0);
  g->hold_turn = dt ? g->hold_turn + 1 : 0;
  g->hold_raise = dr ? g->hold_raise + 1 : 0;
  /* a tap moves a hair; held, it speeds up */
  float kt = in->turn_k > 0 ? in->turn_k : 1, kr = in->raise_k > 0 ? in->raise_k : 1;
  float rt = (0.0015f + 0.009f * fminf(1, g->hold_turn / 45.0f)) * kt, rr = (0.0015f + 0.007f * fminf(1, g->hold_raise / 45.0f)) * kr;
  g->yaw = cw_clampf(g->yaw - dt * rt, -0.75f, 0.75f);
  g->pitch = cw_clampf(g->pitch + dr * rr, -0.08f, 1.15f);
  if (in->power_up) g->power = cw_clampf(g->power + 0.008f, 0, 1);
  if (in->power_down) g->power = cw_clampf(g->power - 0.008f, 0, 1);
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
static void tally(game *g) {
  for (int i = 0; i < g->nb; i++) {
    block *k = &g->b[i];
    if (!k->alive || !k->tag || k->down || k->target >= 0) continue;
    t3_vec3 p = cw_body3d_transform(k->body).p;
    float lim = fmaxf(0.6f, fmaxf(k->half.x, fmaxf(k->half.y, k->half.z)));
    if (vlen(vsub(p, k->home)) > lim) { k->down = true; g->P[g->cur].score += (int)(k->vol * MATS[k->mat].points * 10 + 1); }
  }
}

bool game_over(const game *g) { return g->state == GS_WON || g->state == GS_LOST; }

int game_stars(const game *g) {
  if (g->state != GS_WON) return 0;
  return g->shots <= g->L->par ? 3 : g->shots <= g->L->par + 2 ? 2 : 1;
}

void game_step(game *g, const input *in) {
  g->nev = 0;
  g->tick++, g->state_t++;
  if (g->state == GS_AIM) {
    aim(g, in);
    if (in->fire && g->ammo[g->ammo_sel] > 0) game_fire(g);
  }
  t3_vec3 pv = g->proj_live ? cw_body3d_velocity(g->proj) : v3(0, 0, 0);
  cw_world3d_step(g->w, TICK);
  cw_phys_stats st = cw_world3d_stats(g->w);
  g->step_ms = st.step_ms;
  if (st.step_ms > g->step_ms_max) g->step_ms_max = st.step_ms;
  if (st.awake_bodies > g->max_awake) g->max_awake = st.awake_bodies;
  g->awake_now = st.awake_bodies;
  read_hits(g, pv);
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
      if (g->b[i].alive && cw_body3d_transform(g->b[i].body).p.y < -8) kill_block(g, i);
  /* targets: broken (above), knocked off their place, or fallen */
  for (int t = 0; t < g->ntgt; t++) {
    target_state *ts = &g->tgt[t];
    if (ts->down || ts->blk < 0) continue;
    t3_vec3 p = cw_body3d_transform(g->b[ts->blk].body).p;
    if (vlen(vsub(p, ts->home)) > 0.9f || p.y < ts->home.y - 0.5f) target_down(g, t);
  }
  /* turns */
  if (g->state == GS_FLIGHT) {
    if (!g->proj_live || (g->proj_hit_tick >= 0 && g->tick - g->proj_hit_tick > 40) || g->state_t > 60 * 6) g->state = GS_SETTLE, g->state_t = 0, g->settle_quiet = 0;
  } else if (g->state == GS_SETTLE) {
    int awake = st.awake_bodies - (g->proj_live ? 1 : 0);
    g->settle_quiet = awake <= 2 ? g->settle_quiet + 1 : 0;
    if (g->tgt_left == 0 && g->state_t > 90) g->settle_quiet = 999;
    if (g->settle_quiet > 20 || g->state_t > 60 * 9) {
      proj_end(g);
      tally(g);
      if (g->tgt_left == 0) g->state = GS_WON;
      else if (ammo_left(g) == 0) g->state = GS_LOST;
      else {
        g->state = GS_AIM;
        if (g->players > 1) g->cur = (g->cur + 1) % g->players;
      }
      g->state_t = 0;
    }
  }
  /* the state hash: the world's (every moved transform and event) with the game's own counters */
  uint32_t h = st.hash;
  h = hash32(h ^ (uint32_t)g->tgt_left * 0x9e3779b9u);
  h = hash32(h ^ (uint32_t)g->shots * 0x85ebca6bu ^ (uint32_t)g->broken * 0xc2b2ae35u);
  h = hash32(h ^ (uint32_t)g->nalive);
  g->hash = h;
}
