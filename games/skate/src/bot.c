/* skate bots: skate the park's lines through the same input a pad gives (the stick, the four face buttons, the
 * shoulders, the back button), so the auto cart, the soak and the park check all exercise the real controls.
 *
 *   pick     the next line: a route planner over the park's lines (nav.c's grid: where a board rolls, where it can
 *            drop off an edge, never up one) floods the park from where the bot is; a line whose start it can
 *            reach is picked, the nearer the likelier, never the same one twice running. A start nothing reaches on
 *            wheels (the mega ramp's roll in, a higher roof, a ramp's deck) is walked to: back is pressed until the
 *            start point is the line's, as a person would carry the board up. The park check names its line and
 *            always goes there by back (it checks the line, not the way to it).
 *   go       skate to the line's start: follow the route a few metres ahead, push to a cruising speed, slow for a
 *            turn (a stop and a pivot for a turn about), take a run at a bank, and come into the start along the
 *            line's heading. A route that stops getting shorter is given up (back to the start, counted).
 *   run      push up to the line's speed along its heading, steering back onto it
 *   feature  quarter pipes: a grab or a flip at the top of the air and a half turn to come down forward; kickers and gaps:
 *            a flip, a grab or a spin over the gap, let go in time; rails and ledges: crouch, pop just before the run,
 *            grind held, balance against the meter, jump off near the end; pads: pop onto the pad and hold a manual
 *   link     every landing tries a manual into the next thing (the combo's link), then banks it
 *   done     a line counts when its feature was done and landed (a grind of half a second, the gap, an air landed) */
#include "game.h"

enum { PH_PICK, PH_RESET, PH_RUN, PH_LINK, PH_GO };

static uint32_t rnd(bot *b) {
  uint32_t x = b->rng ? b->rng : 0x2545f491u;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return b->rng = x;
}
static float rndf(bot *b) { return (float)(rnd(b) >> 8) / 16777216.0f; }

void bot_init(bot *b, uint32_t seed, int skill) {
  memset(b, 0, sizeof *b);
  b->rng = seed * 2654435761u + 12345u;
  b->line = -1;
  b->want_line = -1;
  b->last_line = -1;
  b->skill = skill;
}

static v3 hdir(float yaw) {
  float s, c;
  cw_sincos(yaw, &s, &c);
  return V(-s, 0, -c);
}

/* steer toward the horizontal direction d */
static float steer_to(const skater *s, v3 d) {
  v3 h = V(s->v.x, 0, s->v.z);
  if (vlen(h) < 0.8f) h = s->fakie ? vmul(V(s->f.x, 0, s->f.z), -1) : V(s->f.x, 0, s->f.z);
  h = vnorm(h);
  float ang = cw_atan2(vcross(h, d).y, vdot(h, d));
  return clampf(-ang * 2.2f, -1, 1);
}

/* the near end of a run, seen from p going along d: its distance ahead */
static float run_ahead(const park *k, int run, v3 p, v3 d, v3 *end) {
  float best = 1e9f;
  for (int i = 0; i < k->ne; i++) {
    const pedge *e = &k->e[i];
    if (run >= 0 && e->run != run) continue;
    { /* alongside it already (its ends either side of p along d): no distance to go */
      float aa = vdot(vsub(e->a, p), d), bb = vdot(vsub(e->b, p), d);
      if ((aa < 0.3f && bb > 1.5f) || (bb < 0.3f && aa > 1.5f)) { *end = p; best = 0; continue; }
    }
    for (int w = 0; w < 2; w++) {
      v3 q = w ? e->b : e->a;
      float a = vdot(vsub(q, p), d);
      if (a > -0.5f && a < best) best = a, *end = q;
    }
  }
  return best;
}


/* ── routes ───────────────────────────────────────────────────────────── */

/* where a route to line l ends: a few metres back from its start along its heading (so the bot comes in square),
   or the start itself when that is not on the start's floor; the cell, or -1 when the last flood did not reach it */
static int line_goal(const park *k, const pline *l) {
  v3 d = hdir(l->yaw);
  static const float BACK[3] = { 3.0f, 1.5f, 0 };
  for (int i = 0; i < 3; i++) {
    v3 p = vmad(l->start, d, -BACK[i]);
    int c = nav_cell(k, p);
    if (c >= 0 && fabsf(nav_point(k, c).y - l->start.y) < 0.08f && nav_dist(k, c) < 1e29f) return c; /* (on the start's own floor, not a ramp's foot) */
  }
  return -1;
}

/* plan from where the bot is to its line; false when there is no way on wheels */
static bool plan(bot *b, const park *k, const skater *s) {
  int src = nav_cell(k, s->p);
  if (src < 0) return false;
  nav_flood(k, src);
  int g = line_goal(k, &k->line[b->line]);
  if (g < 0) return false;
  b->route_n = nav_route(k, g, b->route, BOT_ROUTE);
  b->route_i = 0;
  b->route_end = g;
  return b->route_n > 0;
}

/* the next line: a reachable start (nearer likelier, not the last line again), or one only back reaches */
static void pick_line(bot *b, const park *k, const skater *s) {
  int n = k->nline, src = nav_cell(k, s->p);
  float w[PARK_MAXLINE], sum = 0;
  int reach = 0, walk = 0;
  if (src >= 0) nav_flood(k, src);
  for (int i = 0; i < n; i++) {
    w[i] = 0;
    if (src < 0 || line_goal(k, &k->line[i]) < 0) { walk++; continue; }
    if (i == b->last_line && n > 1) continue;
    float d = nav_dist(k, line_goal(k, &k->line[i]));
    w[i] = 1.0f / (1.0f + d / 20.0f);
    sum += w[i], reach++;
  }
  /* now and then (after a couple of lines on the board) the walk up to a start only feet reach: the park's big line */
  bool hike = reach == 0 || (walk > 0 && b->since_hike >= 2 && rnd(b) % 3 == 0);
  if (hike) {
    int pick = (int)(rnd(b) % (uint32_t)(walk ? walk : n));
    for (int i = 0; i < n; i++)
      if (!walk || src < 0 || line_goal(k, &k->line[i]) < 0) {
        if (pick-- == 0) { b->line = i; break; }
      }
    b->phase = PH_RESET;
    b->hikes++;
    b->since_hike = 0;
    return;
  }
  float r = rndf(b) * sum;
  b->line = -1;
  for (int i = 0; i < n && b->line < 0; i++) {
    if (w[i] <= 0) continue;
    if ((r -= w[i]) <= 0) b->line = i;
  }
  if (b->line < 0)
    for (int i = n - 1; i >= 0 && b->line < 0; i--)
      if (w[i] > 0) b->line = i;
  b->phase = PH_GO;
  b->route_n = 0;
  b->route_best = 1e9f;
  b->route_best_t = 0;
}

/* how far along the route is still to go from index i (metres, to its end, then on to the start) */
static float route_left(const bot *b, const park *k, int i) {
  float d = 0;
  for (int j = i; j + 1 < b->route_n; j++) d += vhlen(vsub(nav_point(k, b->route[j + 1]), nav_point(k, b->route[j])));
  return d + vhlen(vsub(k->line[b->line].start, nav_point(k, b->route[b->route_n - 1])));
}

/* skate to the line's start; true on arriving there, square to its heading */
static bool go(bot *b, const park *k, const skater *s, input *in) {
  const pline *l = &k->line[b->line];
  v3 d = hdir(l->yaw), R = V(-d.z, 0, d.x);
  float sp = vlen(s->v);
  if (s->st == SK_AIR) return false; /* off an edge: ride it out */
  if (s->st == SK_GRIND) { in->ollie = (b->t & 7) < 4; return false; } /* (caught on something on the way): off it */
  if (s->manual) { in->manual = (b->t & 1) == 0; return false; }
  /* there: on the start's floor, beside it or a little past, going its way */
  v3 to = vsub(s->p, l->start);
  float along = vdot(to, d), lat = fabsf(vdot(to, R));
  v3 hv = sp > 0.6f ? vnorm(V(s->v.x, 0, s->v.z)) : vnorm(s->fakie ? V(-s->f.x, 0, -s->f.z) : V(s->f.x, 0, s->f.z));
  if (fabsf(to.y) < 0.4f && along > -1.5f && along < 2.5f && lat < 1.0f && vdot(hv, d) > 0.82f) return true;
  /* the route: planned now and then from where it is, and again when it is off it */
  int near_i = b->route_i;
  float near_d = 1e9f;
  for (int j = b->route_i; j < b->route_n && j < b->route_i + 14; j++) {
    float dd = vhlen(vsub(nav_point(k, b->route[j]), s->p));
    if (dd < near_d) near_d = dd, near_i = j;
  }
  if (b->route_n == 0 || near_d > 1.6f || b->t % 120 == 0) {
    if (s->st == SK_ROLL && !plan(b, k, s) && b->route_n == 0) return false;
    near_i = 0;
  }
  b->route_i = near_i;
  /* progress: a route that stops getting shorter is given up (the caller sends it by back) */
  float left = route_left(b, k, b->route_i);
  if (left < b->route_best - 0.5f) b->route_best = left, b->route_best_t = b->t;
  /* the point to steer at: a few metres on along the route, then the start and on past it along the line */
  float look = 1.6f + 0.35f * sp, acc = 0;
  v3 aim = vmad(l->start, d, 2.0f);
  int j = b->route_i;
  for (; j + 1 < b->route_n; j++) {
    acc += vhlen(vsub(nav_point(k, b->route[j + 1]), nav_point(k, b->route[j])));
    if (acc >= look) { aim = nav_point(k, b->route[j + 1]); break; }
  }
  /* the climb ahead (a bank up to a terrace): a run at it, straight at its top */
  float climb = 0;
  int top = -1, foot = -1;
  for (int q = b->route_i; q < b->route_n && q < b->route_i + 18; q++) {
    float c = nav_point(k, b->route[q]).y - s->p.y;
    if (c > 0.12f && foot < 0) foot = q;
    if (c > climb + 0.02f) climb = c, top = q;
  }
  /* (on it, or at its foot: the route until then, which comes to the foot square) */
  if (climb > 0.3f && top >= 0 && foot >= 0 && foot <= b->route_i + 2) aim = nav_point(k, b->route[top]);
  v3 want = V(aim.x - s->p.x, 0, aim.z - s->p.z);
  if (vlen(want) < 0.05f) want = d;
  want = vnorm(want);
  float ang = fabsf(cw_atan2(vcross(hv, want).y, vdot(hv, want)));
  float vd = 4.5f;
  if (left < 7.0f) vd = 3.0f;
  if (ang > 0.5f) vd = 2.6f;
  if (ang > 1.1f) vd = 1.4f;
  if (climb > 0.3f && ang < 0.5f) vd = fminf(6.5f, sqrtf(2 * GRAV * climb) + 1.0f);
  in->sx = steer_to(s, want);
  if (ang > 1.9f && sp > 1.0f) in->sy = -1; /* a turn about: stop, pivot, push off */
  else if (sp < vd - 0.2f && s->u.y > 0.85f && !(ang > 1.9f)) in->sy = 1;
  else if (sp > vd + 1.0f && s->u.y > 0.9f) in->sy = -0.7f;
  return false;
}

static void finish(bot *b, bool ok) {
  b->last_line = b->line;
  if (ok) b->lines_done++;
  else b->lines_failed++;
  b->phase = PH_LINK;
  b->t = 0;
}

/* did the line's feature happen this tick? */
static bool feature_done(const bot *b, const park *k, const pline *l, const skater *s) {
  switch (l->kind) {
  case LINE_QUARTER: case LINE_KICKER: case LINE_BOWL:
    return (s->ev & EV_LAND) && b->stuck > 8; /* (stuck counts air ticks in this phase) */
  case LINE_GAP: case LINE_MEGA: return l->gap >= 0 ? (s->gaps_hit & (1 << l->gap)) != 0 && (s->ev & EV_LAND) : (s->ev & EV_LAND) != 0;
  case LINE_RAIL: case LINE_LEDGE: return s->st == SK_GRIND && s->grind_total_s >= 0.4f && (l->edge_run < 0 || s->grind_run == l->edge_run);
  case LINE_MANUAL: return s->manual && s->manual_t >= 45;
  }
  (void)k;
  return false;
}

void bot_tick(bot *b, const sim *S, int i, input *in) {
  const skater *s = &S->s[i];
  const park *k = &S->k;
  memset(in, 0, sizeof *in);
  b->t++;
  if (!k->nline) return;
  if (s->st == SK_BAIL) {
    if (b->phase == PH_RUN) finish(b, false);
    b->phase = PH_LINK;
    b->t = 0;
    return;
  }
  /* balance, whatever the phase: a grind against its meter, a manual against its own */
  float lag = b->skill == 0 ? 0.6f : b->skill == 1 ? 0.85f : 1.0f;
  if (s->st == SK_GRIND) in->sx = clampf((s->bal * 1.6f + s->bal_v * 0.5f + s->wob / 6.0f) * lag, -1, 1);
  if (s->manual) in->sy = clampf((s->mbal * 1.6f + s->mbal_v * 0.5f + s->mwob / 6.0f) * lag, -1, 1);

  switch (b->phase) {
  case PH_PICK: {
    int n = k->nline;
    if (b->want_line >= 0) b->line = b->want_line % n, b->phase = PH_RESET;
    else {
      /* rolling on a floor the planner knows (not up a ramp's face, not in the air): plan from here */
      if ((s->st != SK_ROLL || nav_cell(k, s->p) < 0) && b->t < TPS * 4) {
        if (s->st == SK_ROLL && vlen(s->v) > 2.0f && s->u.y > 0.9f) in->sy = -0.6f;
        return;
      }
      pick_line(b, k, s);
    }
    b->t = 0;
    b->stuck = 0;
    b->trick_plan = (int)(rnd(b) % 4);
    b->flip_pick = (int)(rnd(b) % (uint32_t)NFLIPS);
    b->grab_hold = 10 + (int)(rnd(b) % 18);
    b->spin_dir = rnd(b) & 1 ? 1 : -1;
    b->mistake = b->skill == 0 && rnd(b) % 5 == 0; /* a casual skater gets one wrong now and then: a flip too late */
    return;
  }
  case PH_RESET: {
    const pline *l = &k->line[b->line];
    if (s->spawn_i == l->spawn && s->st == SK_ROLL && vlen(vsub(s->p, l->start)) < 1.0f) {
      if (b->t > 8) b->phase = PH_RUN, b->t = 0, b->stuck = 0, b->popped = 0;
      return;
    }
    in->reset = (b->t & 3) == 1; /* press, let go */
    if (b->t > 400) b->phase = PH_PICK;
    return;
  }
  case PH_GO: {
    if (go(b, k, s, in)) {
      b->rides++, b->since_hike++;
      b->phase = PH_RUN, b->t = 0, b->stuck = 0, b->popped = 0;
      return;
    }
    /* a route that has not got shorter for five seconds (or a minute gone): given up, back to the start */
    if (b->t - b->route_best_t > TPS * 5 || b->t > TPS * 60) {
      b->route_fails++;
      b->phase = PH_RESET, b->t = 0;
    }
    return;
  }
  case PH_RUN: {
    const pline *l = &k->line[b->line];
    v3 d = hdir(l->yaw), R = V(-d.z, 0, d.x);
    float sp = vlen(s->v);
    if (feature_done(b, k, l, s)) { finish(b, true); return; }
    if (s->st == SK_AIR) b->stuck++;
    if (b->t > TPS * 16 || (s->st == SK_ROLL && sp < 0.2f && b->t > TPS * 4)) { finish(b, false); return; }
    if (s->st == SK_ROLL) {
      /* keep on the line: steer to its heading, back toward it when off to the side */
      float e = vdot(vsub(s->p, l->start), R);
      v3 want = vnorm(vsub(d, vmul(R, clampf(e * 0.35f, -0.6f, 0.6f))));
      if (s->u.y < 0.9f && (l->kind == LINE_QUARTER || l->kind == LINE_BOWL)) want = d; /* up the wall: square to it */
      in->sx = s->manual ? in->sx : steer_to(s, want);
      if (sp < fmaxf(l->speed, 3.0f) && s->u.y > 0.85f && !s->manual) in->sy = 1;
      if (sp > l->speed + 1.5f && s->u.y > 0.95f && !s->manual) in->sy = -0.6f;
      /* pops */
      float lead = sp * 0.11f;
      if (l->kind == LINE_RAIL || l->kind == LINE_LEDGE) {
        v3 end;
        float ahead = run_ahead(k, l->edge_run, s->p, d, &end);
        if (ahead < l->pop_dist + lead + 1.2f && sp > l->speed * 0.75f) in->ollie = true; /* crouch */
        if (ahead < l->pop_dist + lead && b->popped == 0) in->ollie = false, b->popped = 1;
      } else if (l->kind == LINE_MANUAL) {
        float ahead = vdot(vsub(l->at, s->p), d);
        if (ahead < l->pop_dist + lead + 1.0f && !b->popped) in->ollie = true;
        if (ahead < l->pop_dist + lead && !b->popped) in->ollie = false, b->popped = 1;
      } else if ((l->kind == LINE_GAP || l->kind == LINE_MEGA) && l->pop_dist > 0) {
        /* an edge ahead: where the ground drops away; crouch coming up to it, pop at it */
        float edge = 99;
        for (float x = 0.25f; x < 4.0f; x += 0.25f) {
          phit h;
          if (!park_ground(k, vmad(s->p, d, x), 0.5f, 0.6f, &h)) { edge = x; break; }
        }
        if (!b->popped && edge < l->pop_dist + lead + 1.2f) in->ollie = true;
        if (!b->popped && edge < l->pop_dist + lead) in->ollie = false, b->popped = 1;
      }
      if (b->popped == 1) b->popped = 2;
      /* hold the grind button from the pop (the press is buffered) */
      if ((l->kind == LINE_RAIL || l->kind == LINE_LEDGE) && b->popped) in->grind = true;
      if (l->kind == LINE_MANUAL && b->popped) in->manual = true; /* held: one press */
    } else if (s->st == SK_AIR) {
      if (l->kind == LINE_RAIL || l->kind == LINE_LEDGE) {
        in->grind = (s->air_t & 1) == 1;
        in->sx = 0;
      } else if (l->kind == LINE_MANUAL) {
        in->manual = s->v.y < 0; /* held into the landing */
      } else {
        /* a trick: a grab, a flip, or a spin; let go before coming down */
        float fall_t = s->v.y < 0 ? (s->p.y - (s->vert ? s->takeoff_y : l->at.y)) / fmaxf(1.0f, -s->v.y) : 9;
        int plan = b->trick_plan;
        /* the air left: up and down to the ground under it (or the takeoff's height, up a wall); no flip it cannot finish */
        float below = s->takeoff_y;
        phit gh;
        if (!s->vert && park_ground(k, s->p, 0.2f, 30, &gh)) below = gh.p.y;
        float drop = fmaxf(0, s->p.y - below), left = (s->v.y + sqrtf(s->v.y * s->v.y + 2 * GRAV * drop)) / GRAV;
        bool flip_fits = left * TPS > 38;
        if (!flip_fits && (plan == 0 || plan == 2 || plan == 3)) plan = left * TPS > 22 ? 1 : -1;
        if (b->mistake && left * TPS < 14 && left * TPS > 4 && s->flip < 0) { in->flip = true; b->mistake = false; }
        if (s->vert && plan == 1 && !flip_fits) plan = 0;
        if (s->vert) {
          /* half a turn to land forward, a grab at the top */
          float want = PI_F * (float)b->spin_dir;
          if (fabsf(s->spin) < fabsf(want) - 0.25f && s->air_t > 3) in->sx = (float)b->spin_dir;
          if (plan != 1 && s->air_t > 4 && s->v.y > -3.0f && fall_t > 0.35f) in->grab = true, in->sx = fabsf(s->spin) < PI_F - 0.25f ? (float)b->spin_dir : 0;
          if (plan == 1 && s->air_t == 4) in->flip = true;
        } else {
          if (plan == 0 && s->air_t == 3) in->flip = true, in->sx = 0;
          if (plan == 1 && s->air_t >= 4 && s->grab_t < b->grab_hold && fall_t > 0.3f) in->grab = true;
          if (plan == 2 && s->air_t == 3) { in->flip = true; in->sy = 1; }
          if (plan == 3 && s->air_t > 2 && fabsf(s->spin) < PI_F - 0.3f) in->sx = (float)b->spin_dir;
        }
        /* into a manual on the way down */
        if (!s->vert && s->v.y < -1.0f && fall_t < 0.15f && b->skill > 0) in->manual = true;
      }
    } else if (s->st == SK_GRIND) {
      /* off near the end: a pop and maybe a flip */
      const pedge *e = &k->e[s->edge];
      float left = s->gv > 0 ? e->len - s->es : s->es;
      if (e->next < 0 && s->gv > 0 && left < fabsf(s->gv) * 0.25f && s->grind_total_s > 0.6f) in->ollie = true;
      if (e->prev < 0 && s->gv < 0 && left < fabsf(s->gv) * 0.25f && s->grind_total_s > 0.6f) in->ollie = true;
    }
    return;
  }
  case PH_LINK: {
    /* after the feature: into a manual to link it; a sharp bot pops out of the manual with a flip and manuals again,
       a link or two, before it lets the combo bank and picks again */
    if (b->t == 1) b->link_pops = 0, b->link_crouch = 0;
    bool flat = s->u.y > 0.9f;
    /* an edge ahead (a roof's, a deck's): brake before it */
    bool edge = false;
    if (s->st == SK_ROLL) {
      float sp = vlen(s->v);
      v3 dv = sp > 0.3f ? vnorm(V(s->v.x, 0, s->v.z)) : V(0, 0, 0);
      for (float x = 0.5f; x < sp * 0.9f + 2.0f && sp > 0.3f; x += 0.5f) {
        phit h;
        if (!park_ground(k, vmad(s->p, dv, x), 0.4f, 1.0f, &h)) { edge = true; break; }
      }
    }
    if (edge && !s->manual) in->sy = -1;
    if (edge && s->manual && s->manual_t > 10) { in->manual = (b->t & 1) == 0; return; } /* off two wheels to brake */
    if (s->st == SK_ROLL && !s->manual && flat && !edge && b->t < 6 && b->skill > 0) in->manual = true;
    if (s->manual && s->manual_t > 26) {
      /* only where the flat runs on ahead at the same height (no edge to drop off, no wall to meet) */
      bool clear = true;
      v3 dv = vnorm(V(s->v.x, 0, s->v.z));
      for (float x = 1.0f; x <= 5.0f && clear; x += 1.0f) {
        phit h;
        if (!park_ground(k, vmad(s->p, dv, x), 0.4f, 0.4f, &h) || h.n.y < 0.97f || fabsf(h.p.y - s->p.y) > 0.15f) clear = false;
        else if (park_cast(k, vadd(s->p, V(0, 0.5f, 0)), vmul(dv, x), 1u << SURF_WALL, &h)) clear = false;
      }
      if (b->skill == 2 && b->link_pops < 2 && s->cb.n < 8 && (clear || b->link_crouch > 0)) {
        b->link_crouch++;
        in->ollie = b->link_crouch < 7; /* crouch, then let go: pop out of the manual */
        if (b->link_crouch >= 7) b->link_pops++, b->link_crouch = 0, b->t = 6;
      } else in->manual = (b->t & 1) == 0; /* a press: off two wheels */
    }
    if (s->st == SK_AIR) {
      in->grab = false;
      if (s->air_t == 2 && b->link_pops > 0) in->flip = true, in->sx = 0;
      if (s->v.y < -0.5f && b->link_pops > 0) in->manual = true; /* held into the landing */
    }
    if (b->t > 70 && s->st != SK_GRIND && s->st != SK_AIR && !s->manual) b->phase = PH_PICK;
    if (b->t > 400) b->phase = PH_PICK;
    return;
  }
  }
}
