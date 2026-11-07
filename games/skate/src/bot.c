/* skate bots: skate the park's lines through the same input a pad gives (the stick, the four face buttons, the
 * shoulders, the back button), so the auto cart, the soak and the park check all exercise the real controls.
 *
 *   pick     a line (the park check names one); press back until the start point is the line's
 *   run      push up to the line's speed along its heading, steering back onto it
 *   feature  quarter pipes: a grab or a flip at the top of the air and a half turn to come down forward; kickers and gaps:
 *            a flip, a grab or a spin over the gap, let go in time; rails and ledges: crouch, pop just before the run,
 *            grind held, balance against the meter, jump off near the end; pads: pop onto the pad and hold a manual
 *   link     every landing tries a manual into the next thing (the combo's link), then banks it
 *   done     a line counts when its feature was done and landed (a grind of half a second, the gap, an air landed) */
#include "game.h"

enum { PH_PICK, PH_RESET, PH_RUN, PH_LINK, PH_DONE };

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

int bot_line_status(const bot *b) { return b->lines_done > 0 ? 1 : b->lines_failed > 0 ? -1 : 0; }

static void finish(bot *b, bool ok) {
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
    b->line = b->want_line >= 0 ? b->want_line % n : (int)(rnd(b) % (uint32_t)n);
    b->phase = PH_RESET;
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
