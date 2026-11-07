/* explorer: the bot (bot.h). Simulation side: it reads the sim and fills an input; deterministic. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bot.h"

enum { B_POS, B_MOVE, B_RECOVER };

bool bot_load(bot *b, const void *bytes, size_t len, uint32_t hash) {
  memset(b, 0, sizeof *b);
  const uint32_t *h = bytes;
  if (!bytes || len < 12 || h[0] != 0x31545258u || h[1] != hash || len < 12 + (size_t)h[2] * sizeof(route_step)) return false;
  b->steps = (const route_step *)((const uint8_t *)bytes + 12);
  b->n = (int)h[2];
  b->last_tile = -1;
  b->seed = 99;
  return true;
}

static inline int fl(float v) { return (int)floorf(v); }
static bool standing(const player *p) { return p->on_ground && p->state == PS_NORMAL && fabsf(p->vx) < 0.05f; }
static int tile_of(const player *p) { return (int)roundf(p->y) * TW + fl(p->x); }
static bool node_ok(const sim *s, int x, int y) {
  if (!w_in(x, y) || y < 3) return false;
  int t = s->t[y][x];
  if (!(t_solid(t) || t == T_ONEWAY)) return false;
  for (int k = 1; k <= 2; k++) {
    int a = s->t[y - k][x];
    if (t_solid(a) || a == T_SPIKE) return false;
  }
  return true;
}

/* walk to x on the current surface: true when there and still */
static bool walk_to(const player *p, float tx, pinput *in) {
  float dx = tx - p->x;
  if (fabsf(dx) < 0.12f && fabsf(p->vx) < 0.05f) return true;
  float want = dx * 7;
  if (want > RUN) want = RUN;
  if (want < -RUN) want = -RUN;
  in->mx = (int8_t)(want > p->vx + 0.6f ? 1 : want < p->vx - 0.6f ? -1 : 0);
  return false;
}

/* a short search from where the bot stands for a way onto the route ahead (the live tiles, the same moves) */
static bool recover(bot *b, const sim *s, const player *p, int lo, int hi) {
  enum { MAXN = 700 };
  static int32_t q[MAXN], par[MAXN];
  static uint8_t pmv[MAXN];
  static int32_t seen_t[MAXN];
  int qn = 0;
  int start = tile_of(p);
  q[qn] = start, par[qn] = -1, pmv[qn] = 255, seen_t[qn] = start, qn++;
  /* targets: the from-tiles of the next steps */
  if (lo < 0) lo = 0;
  if (hi > b->n) hi = b->n;
  int sx = start % TW, sy = start / TW;
  for (int h = 0; h < qn && qn < MAXN - 48; h++) {
    int u = q[h], ux = u % TW, uy = u / TW;
    bool back = hi <= b->i;
    for (int kk = 0; kk < hi - lo; kk++) {
      int k = back ? hi - 1 - kk : lo + kk;
      if (b->steps[k].from == u) { /* found: the plan is the path to u, then the route from step k */
        int path[MAXN], n = 0;
        for (int j = h; j > 0; j = par[j]) path[n++] = j;
        b->np = 0;
        for (int j = n - 1; j >= 0 && b->np < 96; j--)
          b->plan[b->np++] = (route_step){ q[par[path[j]]], q[path[j]], pmv[path[j]], 0, 0, 0 };
        b->pi = 0;
        b->i = k;
        return true;
      }
    }
    /* walk neighbours and moves */
    for (int dx = -1; dx <= 1; dx += 2)
      if (node_ok(s, ux + dx, uy)) {
        int v = uy * TW + ux + dx;
        bool dup = false;
        for (int j = 0; j < qn && !dup; j++) dup = seen_t[j] == v;
        if (!dup && qn < MAXN) q[qn] = v, par[qn] = h, pmv[qn] = 255, seen_t[qn] = v, qn++;
      }
    for (int m = 0; m < NMOVES; m++) {
      if (MOVES[m].need & ~s->abilities) continue;
      player out;
      int broke;
      if (move_play(&MOVES[m], s->w, (const uint8_t(*)[TW])s->t, s->abilities, (float)ux + 0.5f, (float)uy, &out, &broke) < 0) continue;
      int v = (int)roundf(out.y) * TW + fl(out.x);
      if (v == u || !node_ok(s, v % TW, v / TW) || abs(v % TW - sx) > 70 || abs(v / TW - sy) > 40) continue;
      bool dup = false;
      for (int j = 0; j < qn && !dup; j++) dup = seen_t[j] == v;
      if (!dup && qn < MAXN) q[qn] = v, par[qn] = h, pmv[qn] = (uint8_t)m, seen_t[qn] = v, qn++;
    }
  }
  return false;
}

/* strike what is close in front, break a hidden wall the route leans on */
static bool strike_near(const sim *s, const player *p, pinput *in) {
  /* threats first: a charge or a leap coming, an orb in the air: jump it */
  for (int i = 0; i < s->nen; i++) {
    const enemy *e = &s->en[i];
    if (!e->alive) continue;
    float dx = e->x - p->x, dy = (e->y - enemy_hh(e->kind)) - (p->y - 0.9f);
    bool toward = (dx > 0) == (bool)e->facing_left;
    if (e->kind == EN_CRAWLER && e->state == ES_ACT && toward && fabsf(dx) < 3.2f && fabsf(dy) < 1.5f && p->on_ground) {
      in->jump = in->jump_held = true;
      return true;
    }
  }
  for (int i = 0; i < MAXSHOTS; i++) {
    const shot *h = &s->sh[i];
    if (!h->alive || !h->enemy) continue;
    float sx = h->x - p->x, sy = h->y - (p->y - 0.9f);
    if (fabsf(sx) < 2.2f && fabsf(sy) < 1.6f && ((sx > 0 && h->vx < 0) || (sx < 0 && h->vx > 0)) && p->on_ground) {
      in->jump = in->jump_held = true;
      return true;
    }
  }
  /* a flyer aiming or swooping: face it, strike as it arrives (a strike breaks its swoop) */
  for (int i = 0; i < s->nen; i++) {
    const enemy *e = &s->en[i];
    if (!e->alive || e->kind != EN_FLYER || (e->state != ES_TELL && e->state != ES_ACT)) continue;
    float dx = e->x - p->x, dy = (e->y - enemy_hh(e->kind)) - (p->y - 0.9f), d = sqrtf(dx * dx + dy * dy);
    if (d > 10) continue;
    if ((dx < 0) != (bool)p->facing_left && fabsf(dx) > 0.3f) in->mx = (int8_t)(dx < 0 ? -1 : 1);
    else if (d < 2.6f && p->atk_cd == 0) { in->attack = true; if (dy < -1.4f) in->my = -1; }
    return true;
  }
  /* then strike what is close, or shoot what is in line further off */
  for (int i = 0; i < s->nen; i++) {
    const enemy *e = &s->en[i];
    if (!e->alive) continue;
    float dx = e->x - p->x, dy = (e->y - enemy_hh(e->kind)) - (p->y - 0.9f);
    if (fabsf(dx) < 2.5f && fabsf(dy) < 1.7f) {
      bool facing = (dx < 0) == (bool)p->facing_left;
      if (!facing) in->mx = (int8_t)(dx < 0 ? -1 : 1);
      else if (p->atk_cd == 0) in->attack = true;
      /* a knight's shield faces you: get round it with a jump */
      if (e->kind == EN_KNIGHT && ((p->x < e->x) == !e->facing_left) && p->on_ground && fabsf(dx) < 2) in->jump = in->jump_held = true, in->mx = (int8_t)(dx < 0 ? -1 : 1);
      return true;
    }
    if (e->kind == EN_FLYER && fabsf(dx) < 1.4f && dy <= -3.0f && dy > -9 && p->en > 0 && (s->abilities & AB_BOLT) && p->shot_cd == 0) {
      in->my = -1, in->shoot = true; /* straight up at a flyer overhead */
      return true;
    }
    if (e->kind == EN_FLYER && fabsf(dx) < 1.4f && dy <= -3.0f && dy > -6 && p->on_ground && e->state != ES_ACT) {
      in->jump = in->jump_held = true; /* jump to strike it */
      return true;
    }
    if (fabsf(dx) < 1.7f && dy < -1.0f && dy > -3.6f) { /* above */
      if (p->atk_cd == 0) in->my = -1, in->attack = true;
      return true;
    }
    if (fabsf(dx) < 9 && fabsf(dy) < 0.9f && p->en > 0 && (s->abilities & AB_BOLT) && p->shot_cd == 0) {
      if ((dx < 0) == (bool)p->facing_left) in->shoot = true;
      else in->mx = (int8_t)(dx < 0 ? -1 : 1);
      return true;
    }
    if (fabsf(dx) < 5 && fabsf(dy) < 2 && e->kind == EN_FLYER && e->state != ES_REST) { /* wait for it to come: hold still, facing it */
      if ((dx < 0) != (bool)p->facing_left) in->mx = (int8_t)(dx < 0 ? -1 : 1);
      return true;
    }
  }
  return false;
}

/* clear the way: walk up to an enemy on this floor and strike it, before playing a move it could interrupt */
static bool hunt(const sim *s, const player *p, pinput *in) {
  if (!p->on_ground || p->state != PS_NORMAL) return false;
  const enemy *best = NULL;
  float bd = 1e9f;
  for (int i = 0; i < s->nen; i++) {
    const enemy *e = &s->en[i];
    if (!e->alive) continue;
    float dx = e->x - p->x, dy = (e->y - enemy_hh(e->kind)) - (p->y - 0.9f);
    if (fabsf(dx) > 9 || fabsf((e->y) - p->y) > 0.6f) continue; /* on this floor only */
    if (e->kind == EN_FLYER) continue;
    (void)dy;
    if (fabsf(dx) < bd) bd = fabsf(dx), best = e;
  }
  if (!best) return false;
  float dir = best->x > p->x ? 1.0f : -1.0f, tx = best->x - dir * 1.9f;
  /* only along firm floor: never walk off a ledge or into thorns to reach it */
  int y = (int)roundf(p->y);
  for (float x = p->x; dir > 0 ? x < tx : x > tx; x += dir * 0.5f)
    if (!node_ok(s, (int)floorf(x), y)) return false;
  walk_to(p, tx, in);
  if (fabsf(best->x - p->x) < 2.6f && ((best->x < p->x) == (bool)p->facing_left) && p->atk_cd == 0) in->attack = true;
  return true;
}

/* ── a warden's fight ── */
static void fight(bot *b, const sim *s, const player *p, pinput *in) {
  const boss *w = &s->b;
  float hw = boss_hw(w->idx), dx = w->x - p->x, adx = fabsf(dx);
  float side = dx > 0 ? -1.0f : 1.0f; /* away from the warden */
  float want = w->x + side * 6.5f;
  if (want < w->ax0 + 2) want = w->ax0 + 2;
  if (want > w->ax1 - 2) want = w->ax1 - 2;
  bool danger = false;
  /* dodge: shots coming, the sweep's line, the charge, the rain and pillars' marks */
  for (int i = 0; i < MAXSHOTS; i++) {
    const shot *h = &s->sh[i];
    if (!h->alive || !h->enemy) continue;
    float sx = h->x - p->x, sy = h->y - (p->y - 0.9f);
    bool coming = (sx > 0 && h->vx < 0) || (sx < 0 && h->vx > 0) || fabsf(h->vx) < 0.1f;
    if (h->kind == SH_WAVE && fabsf(sx) < 3.2f && coming && p->on_ground) in->jump = in->jump_held = true, danger = true;
    if ((h->kind == SH_ORB || h->kind == SH_SPORE || h->kind == SH_FEATHER) && fabsf(sx) < 3.5f && fabsf(sy) < 2 && coming) {
      if (p->on_ground) in->jump = in->jump_held = true;
      else if (s->abilities & AB_DASH) in->dash = true, in->mx = (int8_t)(h->vx > 0 ? 1 : -1);
      danger = true;
    }
    if (h->kind == SH_SHARD && fabsf(sx) < 1.3f) want = p->x + (sx > 0 ? -2.5f : 2.5f), danger = true;
  }
  if (w->state == ES_TELL && w->move != BM_SWEEP_HIGH) danger = true; /* never stand in reach while it winds up */
  if (w->state == ES_TELL || w->state == ES_ACT) {
    int tell = boss_tell_ticks(w->move);
    switch (w->move) {
    case BM_SWEEP_LOW:
      if (w->state == ES_TELL && w->t >= tell - 3 && p->on_ground) in->jump = true;
      if (w->state == ES_ACT || w->t >= tell - 3) in->jump_held = true, danger = true;
      if (w->state == ES_ACT && w->t > 12 && w->t < 16) in->jump = true; /* the second jump keeps it in the air */
      break;
    case BM_SWEEP_HIGH: danger = true; break; /* stay down */
    case BM_CHARGE:
      if (w->state == ES_TELL && w->t >= tell - 5 && adx < 5.5f && p->on_ground) in->jump = in->jump_held = true;
      if (w->state == ES_TELL) danger = true;
      if (w->state == ES_ACT && adx < 5.5f && p->on_ground) in->jump = in->jump_held = true;
      if (w->state == ES_ACT) in->jump_held = true, danger = true;
      if (w->state == ES_ACT && !p->on_ground && p->vy > -2 && adx < 3) in->jump = true;
      break;
    case BM_LEAP: want = w->x + side * 7; danger = true; break;
    case BM_RAIN: case BM_PILLARS: {
      float best = want, bd = -1;
      for (float x = w->ax0 + 1.5f; x < w->ax1 - 1.5f; x += 1) {
        float d = 99;
        for (int k = 0; k < w->nmarks; k++) d = fminf(d, fabsf(x - w->marks[k]));
        d -= fabsf(x - p->x) * 0.05f;
        if (d > bd) bd = d, best = x;
      }
      want = best, danger = true;
      break;
    }
    case BM_DIVE:
      if (w->state == ES_ACT) {
        float tx = w->tx - p->x;
        if (fabsf(tx) < 3) want = p->x + (tx > 0 ? -5.0f : 5.0f);
        if (fabsf(tx) < 2.5f && (s->abilities & AB_DASH)) in->dash = true, in->mx = (int8_t)(tx > 0 ? -1 : 1);
      }
      danger = true;
      break;
    default: break;
    }
  }
  if (!danger) { /* in its rest: close in and strike, or shoot from range */
    bool flying = w->y < w->floor_y - 2;
    want = w->x + side * (hw + 0.9f);
    if (flying) want = w->x;
    if (adx < hw + 2.2f && p->atk_cd == 0) {
      in->attack = true;
      if (flying || w->y - boss_hh(w->idx) < p->y - 2.6f) in->my = -1;
      if (((dx < 0) != (bool)p->facing_left)) in->mx = (int8_t)(dx < 0 ? -1 : 1);
    } else if (p->en > 0 && (s->abilities & AB_BOLT) && fabsf((w->y - boss_hh(w->idx)) - (p->y - 1.1f)) < 1.6f && ((dx < 0) == (bool)p->facing_left))
      in->shoot = true;
    else if (flying && adx < 2 && p->en > 0 && (s->abilities & AB_BOLT))
      in->shoot = true, in->my = -1;
  }
  if (!in->mx && !in->dash) {
    pinput tmp = *in;
    walk_to(p, want, &tmp);
    in->mx = tmp.mx;
  }
  if (in->mx == 0 && p->on_ground && !danger && adx > 1 && ((dx < 0) != (bool)p->facing_left)) in->mx = (int8_t)(dx < 0 ? -1 : 1);
  b->mode = B_POS, b->np = 0;
}

void bot_follow(const sim *s, int pi, pinput *out) {
  memset(out, 0, sizeof *out);
  const player *p = &s->p[pi], *lead = &s->p[0];
  float dx = lead->x - p->x;
  if (strike_near(s, p, out)) return;
  if (fabsf(dx) > 2.5f) out->mx = (int8_t)(dx > 0 ? 1 : -1);
  if (p->on_ground && (lead->y < p->y - 2 || (out->mx && p->wall))) out->jump = out->jump_held = true;
  if (!p->on_ground && p->vy < 0) out->jump_held = true;
}

void bot_input(bot *b, const sim *s, int pi, pinput *in) {
  memset(in, 0, sizeof *in);
  const player *p = &s->p[pi];
  if (!b->steps || b->i >= b->n) {
    if (s->b.active) fight(b, s, p, in);
    return;
  }
  if (s->b.active) {
    fight(b, s, p, in);
    return;
  }
  if (p->state == PS_HURT || p->state == PS_DEAD) {
    b->mode = B_RECOVER;
    return;
  }
  /* rest at a shrine the first time it is passed (the save point moves along with the bot) */
  for (int k = 0; k < s->w->nents && standing(p); k++) {
    const ent *e = &s->w->ents[k];
    if (e->kind == EK_SAVE && fabsf(e->x - p->x) < 1.5f && fabsf(e->y - p->y) < 1 && !((s->saves_lit[e->id >> 3] >> (e->id & 7)) & 1)) {
      in->interact = true;
      return;
    }
  }
  if (b->mode == B_MOVE) {
    const route_step *st = b->np ? &b->plan[b->pi] : &b->steps[b->i];
    if (!move_input(&MOVES[b->mv], b->t, p, &b->phase, in)) {
      int here = tile_of(p);
      b->mode = B_POS;
      if (here == st->to) {
        if (b->np) {
          if (++b->pi >= b->np) b->np = 0;
        } else b->i++;
        b->fails = 0;
      } else b->mode = B_RECOVER;
      memset(in, 0, sizeof *in);
      return;
    }
    b->t++;
    return;
  }
  if (b->mode == B_RECOVER) {
    if (!standing(p)) return;
    /* the next few steps first, then further on, then back along the route (after a fall or a return to a shrine) */
    if (recover(b, s, p, b->i, b->i + 12) || recover(b, s, p, b->i, b->i + 160) || recover(b, s, p, b->i - 600, b->i)) {
      b->mode = B_POS;
      b->recoveries++;
    } else { /* wander a little and try again */
      b->fails++;
      b->stuck++;
      b->wiggle = 30;
      b->mode = B_POS;
      b->np = 0;
      if (b->fails > 6 && b->i + 1 < b->n) b->i++, b->fails = 0; /* (skip a step the search cannot find) */
    }
    return;
  }
  if (b->wiggle > 0) {
    b->wiggle--;
    b->seed = b->seed * 1103515245u + 12345u;
    in->mx = (int8_t)((b->seed >> 16) & 1 ? 1 : -1);
    if (b->wiggle == 0) b->mode = B_RECOVER;
    return;
  }
  if (!p->on_ground && p->state == PS_NORMAL) { /* in the air between moves: strike up at a flyer close above */
    in->jump_held = p->vy < 0;
    for (int i = 0; i < s->nen; i++) {
      const enemy *e = &s->en[i];
      float dx = e->x - p->x, dy = (e->y - enemy_hh(e->kind)) - (p->y - 0.9f);
      if (e->alive && fabsf(dx) < 1.6f && dy < -0.5f && dy > -3.4f && p->atk_cd == 0) in->my = -1, in->attack = true;
    }
    return;
  }
  if (p->on_ground && p->state == PS_NORMAL && (strike_near(s, p, in) || hunt(s, p, in))) return;
  const route_step *st = b->np ? &b->plan[b->pi] : &b->steps[b->i];
  int here = tile_of(p);
  if (st->move == 255) { /* walking: run on along every walking step on this row */
    int j = b->np ? b->pi : b->i;
    const route_step *list = b->np ? b->plan : b->steps;
    int lim = b->np ? b->np : b->n;
    int last = j;
    while (last + 1 < lim && list[last + 1].move == 255 && list[last + 1].to / TW == list[j].to / TW) last++;
    for (int k = j; k <= last; k++)
      if (here == list[k].to) {
        if (b->np) b->pi = k + 1;
        else b->i = k + 1;
        if (b->np && b->pi >= b->np) b->np = 0;
        return;
      }
    if (here / TW != list[j].to / TW && p->on_ground && standing(p)) { b->mode = B_RECOVER; return; }
    walk_to(p, (float)(list[last].to % TW) + 0.5f, in);
    if (++b->idle > 400) b->idle = 0, b->mode = B_RECOVER;
    return;
  }
  /* a move: only with the way clear (an enemy close by would break it off), then stand on its tile and play it */
  if (b->waited < 600)
    for (int i = 0; i < s->nen; i++) {
      const enemy *e = &s->en[i];
      if (e->alive && e->kind != EN_SPITTER && fabsf(e->x - p->x) < 7 && fabsf(e->y - p->y) < 6) {
        b->waited++;
        if (e->kind == EN_FLYER && (e->x < p->x) != (bool)p->facing_left) in->mx = (int8_t)(e->x < p->x ? -1 : 1);
        return;
      }
    }
  b->waited = 0;
  /* a move: stand on its tile first, then play it */
  int fx = st->from % TW, fy = st->from / TW;
  if (p->on_ground && (int)roundf(p->y) != fy) { if (standing(p)) b->mode = B_RECOVER; return; }
  if (abs(here % TW - fx) > 3 && standing(p)) { b->mode = B_RECOVER; return; }
  if (walk_to(p, (float)fx + 0.5f, in) && standing(p)) {
    b->mode = B_MOVE, b->mv = st->move, b->t = 0, b->phase = 0, b->idle = 0;
    move_input(&MOVES[b->mv], b->t, p, &b->phase, in);
    b->t++;
  }
  if (++b->idle > 400) b->idle = 0, b->mode = B_RECOVER;
}
