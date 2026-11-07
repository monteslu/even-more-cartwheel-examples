/* skate: the board and the rider on it. A custom controller with swept contact against the park's triangles: the
 * engine's rigid bodies step a ball once per tick against flat facets, which at mega ramp speeds (20 m/s and more:
 * a third of a metre a tick) clips lips, hops at the foot of every transition and loses energy to the facets. Here:
 *
 *   rolling   substeps of at most 5 cm: gravity along the surface, the push, rolling resistance and air drag, the
 *             steer turning the velocity about the surface normal. Then the board is put back on the surface by a short
 *             cast along the normal, and the velocity turned with the normal (a rotation: speed is kept, so a quarter
 *             pipe gives back what it took and the apex is v^2 / 2g). The normal is the smooth one at the contact point,
 *             interpolated from the triangle's corners, so a curve is a curve. A convex break of more than 11 degrees,
 *             or no surface under the board, and it is in the air. A wall ahead (a cast at the trucks' height along the
 *             step) turns the velocity off it, or bails the rider when it is hit head on and hard.
 *   the air   ballistic, substepped the same way; the step from the last point to the next is cast against every
 *             surface, so nothing is tunnelled through at any speed. Off a vertical wall the flight keeps to the plane
 *             of the coping (vert air: the rider comes back down onto the ramp). A landing is judged: the board's
 *             heading against the run, its up against the surface, a flip or a grab still going, how hard it came down.
 *   grinds    a press of grind near a rail, a ledge's lip or a coping snaps the board onto it; it slides along the
 *             linked runs with gravity and friction, the rider keeping balance (a meter, the stick against it).
 *   manuals   a press on the ground (or held into a landing) rolls on two wheels: a balance meter, the stick against it.
 *
 * Tricks and the combo: a flip, a grab, a spin, a grind, a manual and a gap each add points; the combo's sum is
 * multiplied by its number of tricks and banked a moment after a landing that is not linked by a manual or a grind.
 * A bail loses it. Repeats in one combo are worth less each time. */
#include "game.h"

physparams g_phys;

/* ── the tricks (names are the game's own) ─────────────────────────────── */
/* stick direction: 0 none, 1 up, 2 down, 3 left, 4 right, 5 up-left, 6 up-right, 7 down-left, 8 down-right */
const trick_def FLIPS[] = {
  { "Twistflip", 150, 24, 1, 0, 0 },     { "Corkscrew", 260, 30, 1, 0.5f, 0 }, { "Shove Spin", 120, 22, 0, 0.5f, 0 },
  { "Heelroll", 150, 24, -1, 0, 0 },     { "Spinflip", 300, 30, 1, 1, 0 },     { "Double Twist", 380, 36, 2, 0, 0 },
  { "Tumbleflip", 320, 32, 0, 0, 1 },    { "Big Shove", 220, 28, 0, 1, 0 },    { "Double Heelroll", 380, 36, -2, 0, 0 },
};
const int NFLIPS = (int)(sizeof FLIPS / sizeof *FLIPS);
const trick_def GRABS[] = {
  { "Nose Hold", 200, 0, 0, 0, 0 }, { "Sky Reach", 250, 0, 0, 0, 0 }, { "Tail Hold", 200, 0, 0, 0, 0 },
  { "Heel Grab", 200, 0, 0, 0, 0 }, { "Toe Grab", 200, 0, 0, 0, 0 },  { "Crossbone", 300, 0, 0, 0, 0 },
  { "Star Grab", 300, 0, 0, 0, 0 }, { "Low Tuck", 250, 0, 0, 0, 0 },  { "Tail Kick", 260, 0, 0, 0, 0 },
};
const int NGRABS = (int)(sizeof GRABS / sizeof *GRABS);
const char *const GRIND_NAMES[] = { "Twin Truck", "Nose Run", "Tail Drag", "Board Glide", "Board Glide" };
const char *const MANUAL_NAMES[] = { "Wheelie", "Nose Wheelie" };

#define PUSH_ACC 5.0f
#define PUSH_MAX 9.0f
#define ROLL_FRIC 0.10f
#define DRAG 0.0032f
#define BRAKE 5.0f
#define MAX_SPEED 26.0f
#define SNAP_UP 0.40f
#define CONVEX_BREAK 0.20f
#define LINK_TICKS 12
#define BAIL_TICKS 96
#define CAPTURE_R 0.55f

static int dir_of(float sx, float sy) {
  bool u = sy > 0.45f, d = sy < -0.45f, l = sx < -0.45f, r = sx > 0.45f;
  if (u && l) return 5;
  if (u && r) return 6;
  if (d && l) return 7;
  if (d && r) return 8;
  if (u) return 1;
  if (d) return 2;
  if (l) return 3;
  if (r) return 4;
  return 0;
}
static uint32_t rng_next(uint32_t *s) {
  uint32_t x = *s ? *s : 0x9e3779b9u;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return *s = x;
}
static float rng_f(uint32_t *s) { return (float)(rng_next(s) >> 8) / 16777216.0f; }

const char *sk_state_name(int st) {
  static const char *const N[] = { "roll", "air", "grind", "bail" };
  return st >= 0 && st < 4 ? N[st] : "?";
}

/* ── the combo ─────────────────────────────────────────────────────────── */

int sk_combo_value(const combo *c) {
  int base = 0;
  for (int i = 0; i < c->n; i++) base += c->it[i].points;
  return base * (c->n > 0 ? c->n : 0);
}
static int fnv_name(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
  return (int)(h & 0x7fffffff);
}
/* a trick into the combo: its points cut for each time it is already in */
static int combo_add(skater *s, const char *name, int points) {
  combo *c = &s->cb;
  int id = fnv_name(name), reps = 0;
  for (int i = 0; i < c->n; i++) reps += c->last_kind_id[i] == id;
  for (int r = 0; r < reps; r++) points /= 2;
  if (points < 10) points = 10;
  if (c->n == COMBO_MAX) { /* full: fold the oldest two */
    c->it[0].points += c->it[1].points;
    memmove(&c->it[1], &c->it[2], sizeof(combo_item) * (COMBO_MAX - 2));
    memmove(&c->last_kind_id[1], &c->last_kind_id[2], sizeof(int) * (COMBO_MAX - 2));
    c->n--;
  }
  snprintf(c->it[c->n].name, sizeof c->it[c->n].name, "%s", name);
  c->it[c->n].points = points;
  c->last_kind_id[c->n] = id;
  c->n++;
  s->tricks++;
  s->ev |= EV_TRICK;
  return c->n - 1;
}
static void combo_bank(skater *s) {
  int v = sk_combo_value(&s->cb);
  if (v > 0) {
    s->score += v;
    s->last_banked = v;
    s->last_banked_t = 150;
    if (v > s->best_combo) s->best_combo = v;
    s->ev |= EV_COMBO;
  }
  memset(&s->cb, 0, sizeof s->cb);
}
static void combo_lose(skater *s) {
  if (s->cb.n) s->ev |= EV_LOST;
  memset(&s->cb, 0, sizeof s->cb);
}

void sk_combo_text(const skater *s, char *out, size_t cap) {
  size_t u = 0;
  out[0] = 0;
  for (int i = 0; i < s->cb.n && u + 40 < cap; i++) u += (size_t)snprintf(out + u, cap - u, "%s%s", i ? " + " : "", s->cb.it[i].name);
}

/* ── placing ───────────────────────────────────────────────────────────── */

static float yaw_of(v3 f) { return cw_atan2(-f.x, -f.z); }
static v3 dir_yaw(float yaw) {
  float s, c;
  cw_sincos(yaw, &s, &c);
  return V(-s, 0, -c);
}

static void reset_air(skater *s) {
  s->flip = -1, s->flip_t = 0, s->grab = -1, s->grab_t = 0, s->spin = 0, s->spin_v = 0, s->vert = false, s->pitch = 0;
  s->flips_done = 0;
}

void sk_place(skater *s, const park *k, v3 p, float yaw, float speed) {
  phit h;
  v3 n = V(0, 1, 0);
  int tri = -1;
  if (park_ground(k, p, 1.5f, 6.0f, &h)) p = h.p, n = h.n, tri = h.tri;
  s->st = SK_ROLL;
  s->p = p;
  s->u = n;
  s->f = vnorm(vperp(dir_yaw(yaw), n));
  s->v = vmul(s->f, speed);
  s->fakie = false;
  s->tri = tri;
  s->crouch = 0;
  s->manual = false;
  s->edge = -1;
  s->bail_t = 0;
  s->link_t = 0;
  s->ground_t = 0;
  reset_air(s);
  s->safe_p = p, s->safe_yaw = yaw;
}

void sk_spawn(skater *s, const park *k, int i) {
  uint32_t keep_rng = s->rng;
  memset(s, 0, sizeof *s);
  s->rng = keep_rng ? keep_rng : 0x1234567u + (uint32_t)i * 977u;
  s->edge = -1;
  s->spawn_i = k->nspawn ? i % k->nspawn : 0;
  const pspawn *sp = k->nspawn ? &k->spawn[s->spawn_i] : NULL;
  sk_place(s, k, sp ? sp->p : V(0, 0, 0), sp ? sp->yaw : 0, 0);
}

/* ── bailing ───────────────────────────────────────────────────────────── */

static void bail(skater *s) {
  if (s->st == SK_BAIL) return;
  s->bail_v = s->v;
  s->st = SK_BAIL;
  s->bail_t = 0;
  s->bails++;
  s->manual = false;
  s->edge = -1;
  s->ev |= EV_BAIL;
  combo_lose(s);
  reset_air(s);
}

static void bail_step(skater *s, const park *k) {
  s->bail_t++;
  /* the body tumbles (rag.c shows it); the simulation's point slides to a stop on the ground under it */
  phit h;
  s->v = vmul(s->v, 0.94f);
  s->v.y -= GRAV * TICK_DT;
  v3 np = vmad(s->p, s->v, TICK_DT);
  if (park_cast(k, vadd(s->p, V(0, 0.3f, 0)), vsub(np, s->p), (1u << SURF_WALL) | (1u << SURF_RIDE), &h) && h.fn.y < 0.5f) {
    s->v = vsub(s->v, vmul(h.fn, 1.2f * vdot(s->v, h.fn)));
    np = s->p;
  }
  /* swept down onto whatever is under it (it can fall fast) */
  if (park_cast(k, vadd(s->p, V(0, 0.05f, 0)), vsub(np, s->p), 1u << SURF_RIDE, &h)) np = h.p, s->v.y = 0;
  if (park_ground(k, np, 0.3f, 0.5f, &h)) {
    np = h.p;
    if (s->v.y < 0) s->v.y = 0;
    s->v = vperp(s->v, h.n);
  }
  s->p = np;
  if (s->p.y < k->lo.y - 5) s->p = s->safe_p, s->v = V(0, 0, 0);
  if (s->bail_t >= BAIL_TICKS) {
    /* back on the board: here if the ground is flat enough, else where it last rolled on the flat */
    v3 at = s->safe_p;
    float yaw = s->safe_yaw;
    if (park_ground(k, s->p, 0.5f, 2.0f, &h) && h.n.y > 0.97f) {
      at = h.p;
      v3 fl = V(s->bail_v.x, 0, s->bail_v.z);
      if (vlen(fl) > 0.5f) yaw = yaw_of(vnorm(fl));
    }
    int keep_bails = s->bails;
    sk_place(s, k, at, yaw, 0);
    s->bails = keep_bails;
  }
}

/* ── landings ──────────────────────────────────────────────────────────── */

static void add_spin(skater *s) {
  float deg = fabsf(s->spin) * (180.0f / PI_F);
  int n = (int)((deg + 50.0f) / 180.0f); /* within 50 degrees counts */
  if (n <= 0) return;
  static const int PTS[] = { 0, 100, 300, 600, 1000, 1500, 2200, 3000 };
  char b[24];
  snprintf(b, sizeof b, "%d", n * 180);
  combo_add(s, b, PTS[n < 7 ? n : 7]);
  if (n * 180 > s->best_spin) s->best_spin = n * 180;
}

static void gaps_on_land(skater *s, const park *k) {
  for (int g = 0; g < k->ngap; g++) {
    const pgap *G = &k->gap[g];
    v3 a = s->takeoff, b = s->p;
    bool in_from = a.x >= G->from_lo.x && a.x <= G->from_hi.x && a.y >= G->from_lo.y && a.y <= G->from_hi.y && a.z >= G->from_lo.z && a.z <= G->from_hi.z;
    bool in_to = b.x >= G->to_lo.x && b.x <= G->to_hi.x && b.y >= G->to_lo.y && b.y <= G->to_hi.y && b.z >= G->to_lo.z && b.z <= G->to_hi.z;
    if (in_from && in_to) {
      combo_add(s, G->name, G->points);
      s->gaps_hit |= 1 << g;
      s->ev |= EV_GAP;
    }
  }
}

/* true when the rider stays on: the board onto the surface at h */
static bool land(skater *s, const park *k, const phit *h) {
  v3 n = h->n;
  float vn = -vdot(s->v, n);
  v3 vt = vperp(s->v, n);
  float sp = vlen(vt), full = vlen(s->v);
  /* back into a transition from vert air, nearly along it: the knees take it, the speed is kept */
  if (s->vert && sp > 0.5f && vn < 0.5f * full) vt = vmul(vt, full / sp), sp = full, vn = 0;
  /* the board's heading against the run along the surface */
  v3 ft = vnorm(vperp(s->f, n));
  float err = 0;
  bool fk = false;
  if (sp > 1.5f) {
    float c = vdot(ft, vmul(vt, 1.0f / sp));
    fk = c < 0;
    err = cw_acos(fabsf(c));
  }
  float upc = vdot(s->u, n);
  const char *why = NULL;
  if (s->flip >= 0 && s->flip_t < FLIPS[s->flip].len - 3) why = "mid flip";
  else if (s->grab >= 0) why = "holding the board";
  else if (upc < 0.55f) why = "board not under feet";
  else if (vn > 13.5f) why = "came down too hard";
  else if (err > 0.85f) why = "landed sideways";
  if (why) s->bail_why = why;
  s->p = h->p;
  s->tri = h->tri;
  s->u = n;
  if (why) {
    bail(s);
    return false;
  }
  s->v = vt;
  if (sp > 1.5f) s->f = fk ? vmul(vnorm(vt), -1) : vnorm(vt);
  else s->f = ft;
  s->fakie = fk;
  s->st = SK_ROLL;
  s->land_kind = err > 0.45f ? 1 : 0;
  if (s->land_kind) s->v = vmul(s->v, 0.7f), s->ev |= EV_SKETCH;
  /* the air's tricks */
  add_spin(s);
  float h_air = s->peak_y - s->takeoff_y;
  if (s->vert && h_air > 0.5f && s->cb.n == 0) combo_add(s, "Lift", 50 + (int)(h_air * 40));
  else if (h_air > 2.5f) combo_add(s, "Big Air", (int)(h_air * 45));
  gaps_on_land(s, k);
  if (h_air > s->max_air) s->max_air = h_air;
  s->landed++;
  s->airs++;
  s->ev |= EV_LAND;
  s->land_t = 0;
  s->link_t = LINK_TICKS;
  reset_air(s);
  return true;
}

/* ── grinds ────────────────────────────────────────────────────────────── */

static bool try_grind(skater *s, const park *k, int dir, bool from_ground) {
  int best = -1;
  float bd = 1e9f, bs = 0;
  for (int i = 0; i < k->ne; i++) {
    const pedge *e = &k->e[i];
    v3 ap = vsub(s->p, e->a);
    float t = clampf(vdot(ap, e->t), 0, e->len);
    if (t <= 0.05f && e->prev < 0 && vdot(s->v, e->t) < 0) continue; /* at a dead end, leaving it */
    if (t >= e->len - 0.05f && e->next < 0 && vdot(s->v, e->t) > 0) continue;
    v3 c = vmad(e->a, e->t, t), d = vsub(s->p, c);
    float dy = vdot(d, e->up);
    v3 dl = vsub(d, vmul(e->up, dy));
    float dh = vlen(dl);
    if (dh > CAPTURE_R || dy < -0.3f || dy > 0.75f) continue;
    float along = fabsf(vdot(s->v, e->t)), sp = vlen(s->v);
    if (sp > 0.5f && along < 0.3f * sp && along < 1.2f) continue; /* across it, not along */
    if (!from_ground && s->v.y > 3.5f && dy < 0.1f) continue; /* rising past it fast */
    float score = dh + fabsf(dy) * 0.5f;
    if (score < bd) bd = score, best = i, bs = t;
  }
  if (best < 0) return false;
  const pedge *e = &k->e[best];
  if (from_ground && e->kind != EDGE_COPING) return false;
  s->st = SK_GRIND;
  s->edge = best;
  s->es = bs;
  float ev = vdot(s->v, e->t);
  if (fabsf(ev) < 2.2f) ev = ev < 0 ? -2.2f : 2.2f;
  s->gv = ev;
  s->p = vmad(e->a, e->t, bs);
  s->v = vmul(e->t, ev);
  s->bal = (rng_f(&s->rng) - 0.5f) * 0.2f;
  s->bal_v = 0;
  int kind = dir == 1 ? 1 : dir == 2 ? 2 : (dir == 3 || dir == 4 || dir >= 5) ? 3 : 0;
  s->grind_kind = kind;
  s->grind_t = 0;
  s->grind_total_s = 0;
  s->grind_run = e->run;
  /* the board along the edge (or across it for a board glide), standing on it */
  s->u = e->up;
  v3 along = vdot(s->f, e->t) >= 0 ? e->t : vmul(e->t, -1);
  s->f = kind == 3 ? vnorm(vcross(e->up, along)) : along;
  s->fakie = kind != 3 && vdot(s->f, s->v) < 0;
  /* a grind lands the air's tricks */
  add_spin(s);
  if (s->flip >= 0 && s->flip_t < FLIPS[s->flip].len - 3) { s->bail_why = "grind mid flip"; bail(s); return true; }
  reset_air(s);
  char b[32];
  snprintf(b, sizeof b, "%s%s", e->kind == EDGE_COPING && kind == 0 ? "Lip " : "", GRIND_NAMES[kind]);
  s->grind_item = combo_add(s, b, 100);
  s->grinds++;
  s->ev |= EV_GRIND_ON;
  return true;
}

static void grind_leave(skater *s, const park *k, v3 extra) {
  const pedge *e = &k->e[s->edge];
  s->st = SK_AIR;
  s->v = vadd(vmul(e->t, s->gv), extra);
  s->takeoff = s->p, s->takeoff_y = s->p.y, s->peak_y = s->p.y;
  s->air_t = 0;
  s->vert = false;
  s->edge = -1;
  s->ev |= EV_GRIND_OFF;
  s->link_t = 0;
  float yl = s->grind_total_s;
  if (yl > s->max_grind_s) s->max_grind_s = yl;
  /* the board turns back along the run (a board glide comes off straight) */
  v3 vf = vnorm(V(s->v.x, 0, s->v.z));
  if (vlen(vf) > 0.5f) s->f = s->fakie ? vmul(vf, -1) : vf;
  s->u = V(0, 1, 0);
  s->pitch = 0;
  s->gap_from = -1;
}

static void grind_step(skater *s, const park *k, const input *in, bool pop) {
  const pedge *e = &k->e[s->edge];
  s->grind_t++;
  s->grind_total_s += TICK_DT;
  /* along the edge: gravity, friction */
  float a = -GRAV * e->t.y - (g_phys.no_friction ? 0 : 0.6f) * signf(s->gv);
  s->gv += a * TICK_DT;
  s->es += s->gv * TICK_DT;
  /* balance: unstable, a wobble that grows, the stick against it */
  if ((s->grind_t % 18) == 1) s->wob = (rng_f(&s->rng) - 0.5f) * (1.2f + 0.02f * (float)s->grind_t);
  s->bal_v += (s->bal * 2.4f + s->wob - in->sx * 6.0f) * TICK_DT;
  s->bal_v *= 0.97f;
  s->bal += s->bal_v * TICK_DT;
  /* points as it goes */
  if (s->grind_item >= 0 && s->grind_item < s->cb.n && s->grind_t % 6 == 0) s->cb.it[s->grind_item].points += 10;
  if (fabsf(s->bal) >= 1.0f) {
    s->v = vadd(vmul(e->t, s->gv), V(0, -1, 0));
    s->bail_why = "lost the grind's balance";
    bail(s);
    return;
  }
  if (pop) { /* jump off: up, and a little to the side the stick says */
    v3 side = vnorm(vcross(e->t, e->up));
    grind_leave(s, k, vadd(V(0, 3.6f, 0), vmul(side, in->sx * 1.4f)));
    s->ev |= EV_POP;
    return;
  }
  /* along the run, onto the linked edge or off the end */
  while (s->es > e->len || s->es < 0) {
    int nx = s->es > e->len ? e->next : e->prev;
    if (nx < 0) {
      s->es = clampf(s->es, 0, e->len);
      s->p = vmad(e->a, e->t, s->es);
      grind_leave(s, k, V(0, 0.8f, 0));
      return;
    }
    float over = s->es > e->len ? s->es - e->len : -s->es;
    const pedge *n = &k->e[nx];
    bool fwd = s->es > e->len;
    float sgn = vdot(e->t, n->t) >= 0 ? 1.0f : -1.0f;
    s->gv *= sgn;
    if (!fwd) sgn = -sgn;
    s->edge = nx;
    e = n;
    s->es = fwd ? over : e->len - over;
    v3 along = vdot(s->f, e->t) >= 0 ? e->t : vmul(e->t, -1);
    s->f = s->grind_kind == 3 ? vnorm(vcross(e->up, along)) : along;
    (void)sgn;
  }
  if (fabsf(s->gv) < 0.25f && fabsf(e->t.y) < 0.05f) { /* stalled on a flat rail: off it */
    grind_leave(s, k, V(0, 0.5f, 0));
    return;
  }
  s->p = vmad(e->a, e->t, s->es);
  s->v = vmul(e->t, s->gv);
}

/* ── manuals ───────────────────────────────────────────────────────────── */

static void manual_start(skater *s, int kind) {
  s->manual = true;
  s->manual_kind = kind;
  s->manual_t = 0;
  s->mbal = (rng_f(&s->rng) - 0.5f) * 0.2f;
  s->mbal_v = 0;
  s->manual_item = combo_add(s, MANUAL_NAMES[kind], 60);
  s->manuals++;
  s->ev |= EV_MANUAL;
}
static void manual_tick(skater *s, const input *in) {
  s->manual_t++;
  if ((s->manual_t % 20) == 1) s->mwob = (rng_f(&s->rng) - 0.5f) * (1.0f + 0.018f * (float)s->manual_t);
  s->mbal_v += (s->mbal * 2.2f + s->mwob - in->sy * 6.0f) * TICK_DT;
  s->mbal_v *= 0.97f;
  s->mbal += s->mbal_v * TICK_DT;
  if (s->manual_item >= 0 && s->manual_item < s->cb.n && s->manual_t % 6 == 0) s->cb.it[s->manual_item].points += 8;
}

/* ── the air ───────────────────────────────────────────────────────────── */

static void takeoff(skater *s, v3 n) {
  s->st = SK_AIR;
  s->air_t = 0;
  s->takeoff = s->p;
  s->takeoff_y = s->peak_y = s->p.y;
  s->vert = n.y < 0.3f;
  if (s->vert) {
    s->vert_n = vnorm(V(n.x, 0, n.z));
    s->u = s->vert_n;
    s->f = vnorm(vperp(s->f, s->u));
  } else {
    v3 h = vnorm(V(s->f.x, 0, s->f.z));
    s->pitch = cw_atan2(s->f.y, vlen(V(s->f.x, 0, s->f.z)));
    if (vlen(h) < 0.5f) s->pitch = 0;
  }
  s->manual = false;
  s->spin = 0;
  s->spin_v = 0;
  s->gap_from = -1;
}

static void pop(skater *s, v3 n) {
  float k = 3.2f + 1.5f * s->crouch;
  v3 d = n.y > 0.5f ? n : V(0, 1, 0);
  takeoff(s, n);
  s->v = vmad(s->v, d, k);
  s->crouch = 0;
  s->ev |= EV_POP;
  combo_add(s, "Pop", 25);
}

/* the board's orientation through the air */
static void air_attitude(skater *s, float dt, float spin_in) {
  float target = spin_in * 7.6f;
  float acc = 46.0f * dt;
  s->spin_v += clampf(target - s->spin_v, -acc, acc);
  float dspin = s->spin_v * dt;
  s->spin += dspin;
  if (s->vert) {
    s->f = vnorm(vrot(s->f, s->u, dspin));
    return;
  }
  v3 h = V(s->f.x, 0, s->f.z);
  if (vlen(h) < 0.2f) h = V(s->v.x, 0, s->v.z);
  h = vnorm(h);
  if (vlen(h) < 0.5f) h = V(0, 0, -1);
  h = vrot(h, V(0, 1, 0), dspin);
  v3 vh = V(s->v.x, 0, s->v.z);
  float vhl = vlen(vh);
  float al = vhl > 0.5f ? clampf(vdot(h, vmul(vh, 1.0f / vhl)), -1, 1) : 0;
  float flight = cw_atan2(s->v.y, vhl > 0.3f ? vhl : 0.3f);
  float want = clampf(flight * al, -0.9f, 0.9f);
  /* coming down onto something within a few tenths of a second: the board meets it (a transition, a slope, the flat) */
  if (s->land_seen) want = clampf(s->land_pitch, -1.1f, 1.1f);
  s->pitch += clampf(want - s->pitch, -3.5f * dt, 3.5f * dt);
  float sp, cp;
  cw_sincos(s->pitch, &sp, &cp);
  s->f = vnorm(vadd(vmul(h, cp), V(0, sp, 0)));
  s->u = vnorm(vadd(vmul(h, -sp), V(0, cp, 0)));
}

/* one substep of flight; true when it landed or bailed */
static bool air_sub(skater *s, const park *k, float dt) {
  s->v.y -= GRAV * dt;
  if (!g_phys.no_friction) s->v = vmul(s->v, 1.0f - DRAG * 0.5f * vlen(s->v) * dt);
  if (s->vert) { /* keep to the plane of the coping, 5 cm out over the ramp: it comes back down onto the vert */
    float out = vdot(s->v, s->vert_n), d = vdot(vsub(s->p, s->takeoff), s->vert_n);
    s->v = vmad(s->v, s->vert_n, clampf((0.05f - d) * 4.0f, -0.5f, 0.5f) - out);
  }
  v3 step = vmul(s->v, dt);
  phit h;
  unsigned mask = (1u << SURF_RIDE) | (1u << SURF_WALL);
  if (!g_phys.no_sweep) {
    /* the body: walls at the hips */
    if (park_cast(k, vadd(s->p, V(0, 0.9f, 0)), step, 1u << SURF_WALL, &h)) {
      float into = -vdot(s->v, h.fn);
      s->v = vsub(s->v, vmul(h.fn, 1.15f * vdot(s->v, h.fn)));
      s->ev |= EV_WALL;
      if (into > 9.0f) { s->bail_why = "flew into a wall"; bail(s); return true; }
      return false;
    }
    if (park_cast(k, s->p, step, mask, &h)) {
      const ptri *t = &k->t[h.tri];
      if (t->surf == SURF_WALL || h.fn.y < -0.2f) { /* a wall or the underside of something: off it */
        float into = -vdot(s->v, h.fn);
        s->p = vmad(h.p, h.fn, 0.02f);
        s->v = vsub(s->v, vmul(h.fn, 1.15f * vdot(s->v, h.fn)));
        s->ev |= EV_WALL;
        if (into > 9.0f) { s->bail_why = "flew into a wall"; bail(s); return true; }
        return false;
      }
      land(s, k, &h);
      return true;
    }
  } else {
    /* the control: no sweep, only where it ends up, tested from just above */
    v3 np = vadd(s->p, step);
    if (park_cast(k, vadd(np, V(0, 0.05f, 0)), V(0, -0.1f, 0), 1u << SURF_RIDE, &h)) {
      s->p = np;
      land(s, k, &h);
      return true;
    }
  }
  s->p = vadd(s->p, step);
  return false;
}

/* ── rolling ───────────────────────────────────────────────────────────── */

/* one substep on the ground; returns false when it left the ground (or bailed) */
static bool roll_sub(skater *s, const park *k, const input *in, float dt) {
  v3 n = s->tri >= 0 ? park_normal_at(k, s->tri, s->p) : V(0, 1, 0);
  float sp = vlen(s->v);
  v3 g = V(0, -GRAV, 0);
  v3 acc = vperp(g, n);
  v3 fw = vnorm(vperp(s->f, n));
  /* push (on the flat-ish), brake */
  bool flat = n.y > 0.85f;
  if (!s->manual && !s->crouch_t && in->sy > 0.3f && flat && sp < PUSH_MAX) acc = vmad(acc, s->fakie ? vmul(fw, -1) : fw, PUSH_ACC * in->sy);
  if (in->sy < -0.4f && !s->manual && sp > 0.2f) acc = vmad(acc, vmul(s->v, 1.0f / sp), -BRAKE * (-in->sy));
  if (!g_phys.no_friction && sp > 0.05f) {
    acc = vmad(acc, vmul(s->v, 1.0f / sp), -(ROLL_FRIC + DRAG * sp * sp));
  }
  s->v = vmad(s->v, acc, dt);
  s->v = vperp(s->v, n);
  /* steer: turn the velocity about the normal */
  sp = vlen(s->v);
  float turn = clampf(2.6f - 0.07f * sp, 1.3f, 2.6f) * (s->crouch > 0.2f ? 1.2f : 1.0f);
  float w = -in->sx * turn * dt;
  if (s->manual) w *= 0.6f;
  s->v = vrot(s->v, n, w);
  s->f = vrot(s->f, n, w);
  /* the board follows its run: nose or tail first, whichever is nearer */
  if (sp > 0.3f) {
    v3 vd = vmul(s->v, 1.0f / sp);
    s->f = vdot(vd, s->f) >= 0 ? vd : vmul(vd, -1);
  } else s->f = vnorm(vperp(s->f, n));
  s->fakie = sp > 0.3f && vdot(s->v, s->f) < 0;
  if (sp > MAX_SPEED) s->v = vmul(s->v, MAX_SPEED / sp);
  v3 step = vmul(s->v, dt);
  phit h;
  /* walls ahead at the trucks' height */
  if (!g_phys.no_sweep && park_cast(k, vadd(s->p, vmul(n, 0.3f)), step, (1u << SURF_WALL) | (1u << SURF_RIDE), &h) &&
      (k->t[h.tri].surf == SURF_WALL || vdot(h.fn, n) < 0.3f)) {
    float into = -vdot(s->v, h.fn);
    if (into > 7.5f && into > 0.7f * vlen(s->v)) { s->bail_why = "rode into a wall"; bail(s); return false; }
    v3 wn = vnorm(vperp(h.fn, n));
    if (vlen(wn) < 0.1f) wn = h.fn;
    s->v = vsub(s->v, vmul(wn, 1.1f * vdot(s->v, wn)));
    s->p = vmad(s->p, wn, 0.02f); /* off the wall a little, so the next step slides */
    s->ev |= EV_WALL;
    return true;
  }
  v3 np = vadd(s->p, step);
  float reach = SNAP_UP + 0.12f + vlen(step) * 0.5f;
  if (!park_cast(k, vmad(np, n, SNAP_UP), vmul(n, -reach), 1u << SURF_RIDE, &h)) {
    s->p = np;
    takeoff(s, n);
    return false;
  }
  v3 n2 = h.n;
  float c = clampf(vdot(n, n2), -1, 1);
  float ang = cw_acos(c);
  /* a crest: the normal tips forward, toward the run (a concave foot tips it back) */
  bool convex = sp > 2.5f && vdot(vsub(n2, n), vmul(s->v, 1.0f / sp)) > 0;
  if (convex && ang > CONVEX_BREAK) { /* the surface falls away: off the lip */
    s->p = np;
    takeoff(s, n);
    return false;
  }
  if (vdot(h.fn, n) < 0.2f) { /* a face at odds with the board (a box's side the cast reached): stop there */
    s->v = V(0, 0, 0);
    return true;
  }
  s->p = h.p;
  s->tri = h.tri;
  s->v = vrot_between(s->v, n, n2);
  s->f = vnorm(vperp(vrot_between(s->f, n, n2), n2));
  s->u = n2;
  return true;
}

/* ── the tick ──────────────────────────────────────────────────────────── */

static int substeps(const skater *s) {
  if (g_phys.no_substeps) return 1;
  int n = (int)(vlen(s->v) * TICK_DT / 0.05f) + 1;
  return n < 2 ? 2 : n > 16 ? 16 : n;
}

static void tokens(skater *s, const park *k) {
  v3 c = vadd(s->p, vmul(s->u, 0.9f));
  for (int i = 0; i < k->ntoken; i++)
    if (!(s->tokens & (1 << i)) && vlen(vsub(c, k->token[i])) < 1.25f) {
      s->tokens |= 1 << i;
      s->ev |= EV_TOKEN;
    }
}

void sk_step(skater *s, const park *k, const input *in) {
  s->ev = 0;
  if (s->last_banked_t > 0) s->last_banked_t--;
  bool ollie_rel = s->prev_ollie && !in->ollie;
  bool flip_p = in->flip && !s->prev_flip, grab_p = in->grab && !s->prev_grab, grind_p = in->grind && !s->prev_grind;
  bool manual_p = in->manual && !s->prev_manual, reset_p = in->reset && !s->prev_reset;
  s->prev_ollie = in->ollie, s->prev_flip = in->flip, s->prev_grab = in->grab, s->prev_grind = in->grind, s->prev_manual = in->manual, s->prev_reset = in->reset;
  int dir = dir_of(in->sx, in->sy);
  if (grind_p) s->buf_grind = 10, s->grind_dir = dir;
  else if (s->buf_grind > 0) s->buf_grind--;
  if (manual_p) s->buf_manual = 8;
  else if (s->buf_manual > 0) s->buf_manual--;
  if (flip_p) s->buf_flip = 5, s->flip_dir = dir;
  else if (s->buf_flip > 0) s->buf_flip--;
  if (grab_p) s->buf_grab = 6, s->grab_dir = dir;
  else if (s->buf_grab > 0) s->buf_grab--;

  if (reset_p && s->st != SK_BAIL) { /* back to a start point (the next one each press) */
    combo_lose(s);
    s->spawn_i = k->nspawn ? (s->spawn_i + 1) % k->nspawn : 0;
    const pspawn *sp = &k->spawn[s->spawn_i];
    sk_place(s, k, sp->p, sp->yaw, 0);
    return;
  }

  switch (s->st) {
  case SK_BAIL: bail_step(s, k); return;

  case SK_GRIND: {
    bool popn = in->ollie && !s->ollie_held_grind;
    if (!in->ollie) s->ollie_held_grind = false;
    grind_step(s, k, in, popn);
    if (popn) s->ollie_held_grind = true;
    break;
  }

  case SK_ROLL: {
    s->ground_t++;
    /* crouch and pop */
    if (in->ollie) { s->crouch = fminf(1, s->crouch + TICK_DT / 0.3f); s->crouch_t++; }
    v3 n = s->tri >= 0 ? park_normal_at(k, s->tri, s->p) : V(0, 1, 0);
    bool want_pop = (ollie_rel && s->crouch_t > 0) || (s->buf_flip > 0 && s->ground_t > 2);
    if (!in->ollie) s->crouch_t = 0;
    if (want_pop) {
      if (s->buf_flip > 0) s->crouch = fmaxf(s->crouch, 0.55f);
      if (s->manual) s->manual = false;
      pop(s, n);
      s->link_t = 0;
      break;
    }
    if (!in->ollie && s->crouch > 0) s->crouch = fmaxf(0, s->crouch - TICK_DT / 0.15f);
    /* grind from a ramp's top onto its coping */
    if (s->buf_grind > 0 && n.y < 0.75f && try_grind(s, k, s->grind_dir, true)) {
      s->buf_grind = 0;
      break;
    }
    /* manual */
    float sp = vlen(s->v);
    if (s->manual) {
      manual_tick(s, in);
      if (fabsf(s->mbal) >= 1.0f || sp < 0.6f) {
        if (fabsf(s->mbal) >= 1.0f) { s->bail_why = "lost the manual's balance"; bail(s); return; }
        s->manual = false;
        s->link_t = LINK_TICKS / 2;
      } else if (manual_p && s->manual_t > 10) { /* the press that ends it is spent */
        s->buf_manual = 0;
        s->manual = false;
        s->link_t = LINK_TICKS / 2;
      }
    } else if (s->buf_manual > 0 && sp > 1.0f && s->crouch < 0.5f && n.y > 0.85f) { /* on two wheels: on the flat or a gentle slope, not up a wall */
      manual_start(s, in->sy < -0.4f ? 1 : 0);
      s->buf_manual = 0;
      s->link_t = 0;
    }
    /* the combo banks unless linked */
    if (!s->manual && s->cb.n) {
      if (s->link_t > 0) s->link_t--;
      if (s->link_t <= 0) combo_bank(s);
    }
    /* pushes (the animation's beat, a sound) */
    if (in->sy > 0.3f && !s->manual && sp < PUSH_MAX && n.y > 0.85f) {
      s->push_t += TICK_DT;
      if (s->push_t > 0.55f) s->push_t = 0, s->push_k++, s->ev |= EV_PUSH;
    } else s->push_t = 0;
    int ns = substeps(s);
    float dt = TICK_DT / (float)ns;
    for (int i = 0; i < ns && s->st == SK_ROLL; i++)
      if (!roll_sub(s, k, in, dt)) break;
    if (s->st == SK_AIR) { /* rolled off a lip: the air from here, the rest of the tick in it */
      s->link_t = s->manual ? 0 : s->link_t;
      break;
    }
    if (s->st == SK_ROLL) {
      n = s->tri >= 0 ? park_normal_at(k, s->tri, s->p) : V(0, 1, 0);
      s->u = n;
      if (n.y > 0.97f && !s->manual && s->crouch < 0.5f) s->safe_p = s->p, s->safe_yaw = yaw_of(s->fakie ? vmul(s->f, -1) : s->f);
    }
    break;
  }

  case SK_AIR: {
    s->air_t++;
    /* tricks */
    if (s->buf_flip > 0 && (s->flip < 0 || s->flip_t >= FLIPS[s->flip].len) && s->grab < 0 && s->air_t >= 2) {
      s->flip = s->flip_dir;
      s->flip_t = 0;
      s->buf_flip = 0;
      s->flips_done++;
    }
    if (s->flip >= 0) {
      s->flip_t++;
      if (s->flip_t == FLIPS[s->flip].len) combo_add(s, FLIPS[s->flip].name, FLIPS[s->flip].points);
    }
    bool flipping = s->flip >= 0 && s->flip_t < FLIPS[s->flip].len;
    if (s->grab < 0 && in->grab && (s->buf_grab > 0 || s->grab_since > 4) && !flipping && s->air_t >= 2) {
      s->grab = s->buf_grab > 0 ? s->grab_dir : dir;
      s->grab_t = 0;
      s->buf_grab = 0;
    }
    if (s->grab >= 0) {
      s->grab_t++;
      if (!in->grab) { /* let go: scored by how long it was held */
        int pts = GRABS[s->grab].points + (s->grab_t > 12 ? (s->grab_t - 12) * 8 : 0);
        combo_add(s, GRABS[s->grab].name, pts);
        s->grab = -1;
        s->grab_since = 0;
      }
    } else s->grab_since++;
    /* grind capture */
    if (s->buf_grind > 0 && s->air_t >= 3 && try_grind(s, k, s->grind_dir, false)) {
      s->buf_grind = 0;
      break;
    }
    /* where it will come down: along the flight for 0.4 s (falling under gravity), the surface it meets; the board
       pitches to meet it */
    if (!s->vert) {
      phit lh;
      const float T = 0.4f;
      v3 end = vadd(vmad(s->p, s->v, T), V(0, -0.5f * GRAV * T * T, 0));
      s->land_seen = s->v.y < 0.5f && park_cast(k, s->p, vsub(end, s->p), 1u << SURF_RIDE, &lh);
      if (s->land_seen) {
        v3 h = vnorm(V(s->f.x, 0, s->f.z));
        if (vlen(h) < 0.5f) h = vnorm(V(s->v.x, 0, s->v.z));
        s->land_pitch = cw_atan2(-vdot(lh.n, h), lh.n.y);
      }
    } else s->land_seen = false;
    float spin_in = in->spin_l ? -1.0f : in->spin_r ? 1.0f : 0;
    if (spin_in == 0 && fabsf(in->sx) > 0.3f && !flipping) spin_in = in->sx;
    int ns = substeps(s);
    float dt = TICK_DT / (float)ns;
    for (int i = 0; i < ns && s->st == SK_AIR; i++) {
      air_attitude(s, dt, -spin_in);
      if (air_sub(s, k, dt)) break;
      if (s->p.y > s->peak_y) s->peak_y = s->p.y;
    }
    if (s->st == SK_ROLL && (s->buf_manual > 0 || in->manual) && s->u.y > 0.85f) { /* held (or pressed just before) into a landing on the flat: straight into a manual */
      manual_start(s, in->sy < -0.4f ? 1 : 0);
      s->buf_manual = 0;
      s->link_t = 0;
    } else if (s->st == SK_ROLL && s->buf_grind > 0) {
      v3 n = s->tri >= 0 ? park_normal_at(k, s->tri, s->p) : V(0, 1, 0);
      if (n.y < 0.75f && try_grind(s, k, s->grind_dir, true)) s->buf_grind = 0;
    }
    if (s->p.y < k->lo.y - 8) { /* out of the world: should not happen; back to safety */
      sk_place(s, k, s->safe_p, s->safe_yaw, 0);
      s->lost++;
    }
    break;
  }
  }
  float spd = vlen(s->v);
  if (spd > s->max_speed) s->max_speed = spd;
  if (s->st != SK_BAIL) tokens(s, k);
}
