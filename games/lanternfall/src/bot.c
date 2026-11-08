/* lanternfall: the bot (bot.h). Simulation side: it reads the sim and fills an input; deterministic. */
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
  b->trial_room = -1;
  return true;
}

static inline int fl(float v) { return (int)floorf(v); }
static bool standing(const player *p) { return p->on_ground && p->state == PS_NORMAL && fabsf(p->vx) < 0.05f; }
static int tile_of(const player *p) { return (int)roundf(p->y) * TW + fl(p->x); }
static bool node_ok(const sim *s, int x, int y) {
  if (!w_in(x, y) || y < 3) return false;
  int t = s->t[y][x];
  if (!(t_solid(t) || t == T_ONEWAY) || t == T_SECRET) return false; /* (a hidden floor breaks under you: nobody stands on it) */
  for (int k = 1; k <= 2; k++) {
    int a = s->t[y - k][x];
    if (t_solid(a) || a == T_SPIKE) return false;
  }
  return true;
}

/* firm to walk over (a hidden floor holds until it is struck) */
static bool walk_ok(const sim *s, int x, int y) {
  if (node_ok(s, x, y)) return true;
  return w_in(x, y) && y >= 3 && s->t[y][x] == T_SECRET && !t_solid(s->t[y - 1][x]) && !t_solid(s->t[y - 2][x]);
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
  /* never past a goal not yet reached (a pickup, a shrine, a warden): the search would happily skip it */
  for (int k = b->i; k < hi; k++)
    if (b->steps[k].goal) { hi = k + 1; break; }
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

/* a hidden wall or floor at hand (the route may run through it: the checker breaks every one it can strike) */
static bool secret_near(const sim *s, const player *p) {
  for (int dx = -2; dx <= 2; dx++)
    for (int dy = 0; dy <= 2; dy++) { /* (the floor under the feet, the wall beside the body) */
      int tx = (int)floorf(p->x) + dx, ty = (int)roundf(p->y) - dy;
      if (w_in(tx, ty) && s->t[ty][tx] == T_SECRET) return true;
    }
  return false;
}

/* what the bot can fight with an ordinary blow: not a burrower under the floor, not a puff (only from above), not a
 * dropper on its ceiling */
static bool plain_target(const enemy *e) {
  if (!e->alive || e->kind == EN_PUFF) return false;
  if (e->kind == EN_BURROWER && !(e->aux & 1)) return false;
  if (e->kind == EN_DROPPER && (e->state == ES_IDLE || e->state == ES_TELL)) return false;
  return true;
}

/* the newer threats: a burrower's ripple under you, a dropper shivering overhead, an eye about to burn, a puff in the way */
static bool dodge_new(const sim *s, const player *p, pinput *in) {
  for (int i = 0; i < s->nen; i++) {
    const enemy *e = &s->en[i];
    if (!e->alive) continue;
    float dx = e->x - p->x, dy = (e->y - enemy_hh(e->kind)) - (p->y - 0.9f);
    if (e->kind == EN_BURROWER && e->state == ES_TELL && fabsf(dx) < 2.6f && fabsf(e->y - p->y) < 0.6f && p->on_ground) {
      float dir = dx > 0 ? -1.0f : 1.0f; /* step off the ripple, along firm floor */
      int y = (int)roundf(p->y);
      if (!node_ok(s, (int)floorf(p->x + dir * 1.5f), y)) dir = -dir;
      in->mx = (int8_t)dir;
      return true;
    }
    if (e->kind == EN_DROPPER && e->state == ES_TELL && fabsf(dx) < 1.9f && p->on_ground) {
      float dir = dx > 0 ? -1.0f : 1.0f;
      if (!node_ok(s, (int)floorf(p->x + dir * 2.0f), (int)roundf(p->y))) dir = -dir;
      in->mx = (int8_t)dir;
      return true;
    }
    if (e->kind == EN_EYE && e->state == ES_TELL && e->t >= enemy_tell_ticks(EN_EYE) - 8 && fabsf(dy) < 1.4f && (dx < 0) == !e->facing_left && fabsf(dx) < 16 && p->on_ground) {
      in->jump = in->jump_held = true; /* over the beam */
      return true;
    }
    if (e->kind == EN_PUFF && fabsf(dx) < 1.3f && dy > 0.2f && dy < 2.4f && !p->on_ground && p->atk_cd == 0 && p->vy > -4) {
      in->my = 1, in->attack = true; /* down on it: it bounces you */
      return true;
    }
  }
  return false;
}

/* strike what is close in front, break a hidden wall the route leans on */
static bool strike_near(const sim *s, const player *p, pinput *in) {
  if (dodge_new(s, p, in)) return true;
  /* a hidden wall at hand: strike it open (the route may run through it) */
  for (int dx = -2; dx <= 2; dx++)
    for (int dy = 0; dy <= 2; dy++) { /* (the floor under the feet, the wall beside the body) */
      int tx = (int)floorf(p->x) + dx, ty = (int)roundf(p->y) - dy;
      if (!w_in(tx, ty) || s->t[ty][tx] != T_SECRET) continue;
      if (dy <= 0) { /* in the floor: jump, and strike down on the way back */
        if (p->on_ground) in->jump = true; /* (a tapped hop) */
        else if (p->atk_cd == 0 && p->vy > 0 && (float)ty - p->y < 1.4f) in->my = 1, in->attack = true;
        in->mx = (int8_t)(tx + 0.5f > p->x + 0.3f ? 1 : tx + 0.5f < p->x - 0.3f ? -1 : 0);
        return true;
      }
      if (dx && (dx < 0) != (bool)p->facing_left) in->mx = (int8_t)(dx < 0 ? -1 : 1);
      else if (p->atk_cd == 0) in->attack = true;
      return true;
    }
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
    bool incoming = (sx > 0 && h->vx < 0) || (sx < 0 && h->vx > 0);
    if (fabsf(sx) < 2.6f && fabsf(sy) < 1.8f && incoming && p->atk_cd == 0) { /* cut it out of the air */
      if ((sx < 0) != (bool)p->facing_left) in->mx = (int8_t)(sx < 0 ? -1 : 1);
      in->attack = true;
      return true;
    }
    if (fabsf(sx) < 3.6f && fabsf(sy) < 2.5f && incoming) { /* face it and wait for it */
      if ((sx < 0) != (bool)p->facing_left) in->mx = (int8_t)(sx < 0 ? -1 : 1);
      return true;
    }
    if (fabsf(sx) < 2.2f && fabsf(sy) < 1.6f && incoming && p->on_ground) {
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
    /* it swoops to the spot you stood on when its wings flared: off that spot (on firm ground), then strike it as it
     * comes past or rests */
    float aim = fabsf(e->vx - p->x);
    if (p->on_ground && aim < 1.6f && p->hp <= 2) {
      int y = (int)roundf(p->y);
      for (int k = 0; k < 2; k++) {
        float dir = (k == 0) == (dx > 0) ? -1.0f : 1.0f, tx = e->vx + dir * 2.6f;
        bool firm = true;
        for (float x = p->x; dir > 0 ? x < tx : x > tx; x += dir * 0.4f) firm = firm && node_ok(s, (int)floorf(x), y);
        if (firm && node_ok(s, (int)floorf(tx), y)) { in->mx = (int8_t)dir; return true; }
      }
    }
    if ((dx < 0) != (bool)p->facing_left && fabsf(dx) > 0.3f) in->mx = (int8_t)(dx < 0 ? -1 : 1);
    else if (d < (e->state == ES_ACT ? 2.0f : 2.4f) && p->atk_cd == 0) { in->attack = true; if (dy < -1.4f) in->my = -1; }
    return true;
  }
  /* then strike what is close, or shoot what is in line further off */
  for (int i = 0; i < s->nen; i++) {
    const enemy *e = &s->en[i];
    if (!plain_target(e)) continue;
    float dx = e->x - p->x, dy = (e->y - enemy_hh(e->kind)) - (p->y - 0.9f);
    if (fabsf(dx) < 2.5f && fabsf(dy) < 1.7f) {
      bool facing = (dx < 0) == (bool)p->facing_left;
      if (!facing) in->mx = (int8_t)(dx < 0 ? -1 : 1);
      else if (e->kind == EN_KNIGHT && e->state != ES_ACT && ((p->x < e->x) == !e->facing_left) && p->en > 0 && (s->abilities & AB_BOLT)) {
        if (p->shot_cd == 0) in->shoot = true; /* the shield turns strikes, not sparks */
        return true;
      } else if (p->atk_cd == 0) in->attack = true;
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
    if (fabsf(dx) < 5.0f && dy < -1.0f && dy > -3.6f && e->kind != EN_FLYER && p->on_ground) { /* on a ledge just above: under its edge, strike up */
      float tx = e->x - (dx > 0 ? 1.0f : -1.0f);
      bool firm = true;
      int y = (int)roundf(p->y);
      for (float x = p->x; dx > 0 ? x < tx : x > tx; x += dx > 0 ? 0.4f : -0.4f) firm = firm && node_ok(s, (int)floorf(x), y);
      if (firm && node_ok(s, (int)floorf(tx), y)) {
        if (fabsf(p->x - tx) > 0.35f) in->mx = (int8_t)(tx > p->x ? 1 : -1);
        else if (p->atk_cd == 0) in->my = -1, in->attack = true;
        return true;
      }
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
    if (!plain_target(e)) continue;
    float dx = e->x - p->x, dy = (e->y - enemy_hh(e->kind)) - (p->y - 0.9f);
    if (fabsf(dx) > (e->kind == EN_SPITTER ? 14 : 9) || fabsf((e->y) - p->y) > 0.6f) continue; /* on this floor only */
    if (e->kind == EN_FLYER) continue;
    (void)dy;
    if (fabsf(dx) < bd) bd = fabsf(dx), best = e;
  }
  if (!best) return false;
  float dir = best->x > p->x ? 1.0f : -1.0f, tx = best->x - dir * 1.9f;
  /* only along firm floor: never walk off a ledge or into thorns to reach it */
  int y = (int)roundf(p->y);
  for (float x = p->x; dir > 0 ? x < tx : x > tx; x += dir * 0.5f)
    if (!walk_ok(s, (int)floorf(x), y)) return false;
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
    if ((h->kind == SH_ORB || h->kind == SH_SPORE || h->kind == SH_FEATHER) && fabsf(sx) < 2.6f && fabsf(sy) < 1.5f && coming && p->atk_cd == 0) {
      in->attack = true; /* cut it out of the air */
      if ((sx < 0) != (bool)p->facing_left) in->mx = (int8_t)(sx < 0 ? -1 : 1);
      danger = true;
      continue;
    }
    if ((h->kind == SH_ORB || h->kind == SH_SPORE || h->kind == SH_FEATHER) && fabsf(sx) < 3.5f && fabsf(sy) < 2 && coming) {
      if (p->on_ground) in->jump = in->jump_held = true;
      else if (s->abilities & AB_DASH) in->dash = true, in->mx = (int8_t)(h->vx > 0 ? 1 : -1);
      danger = true;
    }
    if (h->kind == SH_SHARD && fabsf(sx) < 1.3f) want = p->x + (sx > 0 ? -2.5f : 2.5f), danger = true;
  }
  if (w->state == ES_TELL && w->move != BM_SWEEP_HIGH && w->move != BM_SUMMON) danger = true; /* never stand in reach while it winds up */
  if (w->state == ES_TELL || w->state == ES_ACT) {
    int tell = boss_tell_ticks(w->move);
    if (w->phase > 0) tell = tell * 4 / 5 < 26 ? 26 : tell * 4 / 5; /* (the second phase tells faster, as the sim does) */
    switch (w->move) {
    case BM_SWEEP_LOW:
      if (w->state == ES_TELL && w->t >= tell - 3 && p->on_ground) in->jump = true;
      if (w->state == ES_ACT || w->t >= tell - 3) in->jump_held = true, danger = true;
      if (w->state == ES_ACT && w->t > 12 && w->t < 16) in->jump = true; /* the second jump keeps it in the air */
      break;
    case BM_SWEEP_HIGH: danger = true; break; /* stay down */
    case BM_CHARGE: {
      /* over it as it comes: never run from it (it is faster), jump toward it a few tiles off and keep going over */
      bool coming = (dx > 0) == (bool)w->facing_left;
      if (w->state == ES_TELL) { want = w->x + side * 7.5f; if (want < w->ax0 + 9) want = w->ax0 + 9; if (want > w->ax1 - 9) want = w->ax1 - 9; }
      danger = true;
      if (w->state == ES_ACT && coming) {
        /* the ticks a held jump takes to clear its top, and how far it closes in that time (running at it) */
        static const float RISE[22] = { 0, 0.4f, 0.75f, 1.14f, 1.45f, 1.8f, 2.1f, 2.4f, 2.65f, 2.9f, 3.1f, 3.3f, 3.5f, 3.65f, 3.8f, 3.98f, 4.07f, 4.17f, 4.27f, 4.33f, 4.37f, 4.4f };
        float top = 2 * boss_hh(w->idx) + 0.25f;
        int k = 0;
        while (k < 15 && RISE[k] < top) k++;
        float lead = hw + 0.5f + (float)k * (13.0f + RUN) / 60.0f;
        in->jump_held = true;
        if (adx < lead + 1) in->mx = (int8_t)(dx > 0 ? 1 : -1);
        if (adx < lead && p->on_ground) in->jump = true;
        if (!p->on_ground && p->vy > -3 && adx < lead) in->jump = true; /* (the second jump, when it has one) */
      } else if (w->state == ES_ACT) {
        in->jump_held = !p->on_ground && p->vy < 0;
        want = p->x, danger = false; /* gone past: let it go, and deal with the rest */
      }
      break;
    }
    case BM_LEAP: want = w->x + side * 7; danger = true; break;
    case BM_VOLLEY:
      if (w->idx != 0) break;
      /* (the moss warden lobs at marks: like rain) */
      // fall through
    case BM_RAIN: case BM_PILLARS: {
      /* the clear spot furthest from every mark on this side of it (never through its body), unless already clear */
      float here = 99;
      for (int k = 0; k < w->nmarks; k++) here = fminf(here, fabsf(p->x - w->marks[k]));
      float best = want, bd = -1;
      for (float x = w->ax0 + 1.5f; x < w->ax1 - 1.5f; x += 1) {
        if (fabsf(x - w->x) < hw + 1.4f || (x < w->x) != (p->x < w->x)) continue;
        float d = 99;
        for (int k = 0; k < w->nmarks; k++) d = fminf(d, fabsf(x - w->marks[k]));
        d -= fabsf(x - p->x) * 0.05f;
        if (d > bd) bd = d, best = x;
      }
      want = best;
      danger = here < 2.2f || w->state == ES_ACT;
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
  if (!danger && p->on_ground) { /* the helpers it calls first: they crowd the floor and charge */
    pinput t2;
    memset(&t2, 0, sizeof t2);
    if (strike_near(s, p, &t2) || hunt(s, p, &t2)) { *in = t2; b->mode = B_POS, b->np = 0; return; }
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
  if (s->hitstop > 0) return;
  const player *p = &s->p[pi], *lead = &s->p[0];
  float dx = lead->x - p->x;
  if (strike_near(s, p, out)) return;
  if (fabsf(dx) > 2.5f) out->mx = (int8_t)(dx > 0 ? 1 : -1);
  if (p->on_ground && (lead->y < p->y - 2 || (out->mx && p->wall))) out->jump = out->jump_held = true;
  if (!p->on_ground && p->vy < 0) out->jump_held = true;
}

void bot_input(bot *b, const sim *s, int pi, pinput *in) {
  memset(in, 0, sizeof *in);
  if (s->hitstop > 0) return; /* (the world holds still: nothing to do, and the move's ticks must not run on) */
  const player *p = &s->p[pi];
  /* a trial's room: remember the step it was entered at; once the guardians fall, go back for the find */
  if (s->room >= 0 && b->trial_room != s->room && sim_trial_room(s, s->room)) b->trial_room = s->room, b->trial_i = b->i;
  if (b->trial_room >= 0 && s->trial_room < 0 && !sim_trial_room(s, b->trial_room)) {
    bool left = false;
    for (int k = 0; k < s->w->nents; k++) {
      const ent *e = &s->w->ents[k];
      if (e->kind == EK_PICKUP && e->room == b->trial_room && !sim_got(s, e->id)) left = true;
    }
    if (left && s->room == b->trial_room) b->i = b->trial_i, b->mode = B_RECOVER, b->np = 0;
    b->trial_room = -1;
  }
  if (s->trial_room >= 0) b->ignore = 0, b->prog_t = 0; /* (a trial is fought out) */
  if (!b->steps || b->i >= b->n) {
    if (s->b.active) fight(b, s, p, in);
    return;
  }
  /* a watchdog: standing in one spot for four seconds, the bot stops minding the enemies for a while */
  if (fabsf(p->x - b->lx) < 0.05f && fabsf(p->y - b->ly) < 0.05f) b->still++;
  else b->still = 0, b->lx = p->x, b->ly = p->y;
  if (b->still > 240) b->ignore = 360, b->still = 0, b->mode = B_RECOVER, b->waited = 600;
  /* and a slower one: twenty seconds with no step of the route gained (a fight that goes round in circles), it
   * stops minding the enemies for a while and plays on */
  if (b->i != b->prog_i) b->prog_i = b->i, b->prog_t = 0;
  else if (++b->prog_t > 1200) b->ignore = 420, b->waited = 600, b->prog_t = 0;
  if (b->ignore > 0) b->ignore--;
  if (s->b.active) {
    fight(b, s, p, in);
    return;
  }
  if (p->state == PS_CLING && b->mode != B_MOVE) { /* off a wall it was left clinging to */
    in->mx = (int8_t)-p->wall;
    b->mode = B_RECOVER;
    return;
  }
  if (p->state == PS_HURT || p->state == PS_DEAD) {
    b->mode = B_RECOVER;
    return;
  }
  /* deaths: remember where on the route */
  if (s->deaths != b->last_deaths) {
    if (b->death_step / 50 < 128 && b->died_at[b->death_step / 50] < 255) b->died_at[b->death_step / 50]++;
    b->last_deaths = s->deaths;
  }
  b->death_step = b->i > b->death_step ? b->i : b->death_step;
  if (s->deaths != b->last_deaths) b->death_step = b->i;
  bool careful = b->i / 50 < 128 && (b->died_at[b->i / 50] >= 2 || (b->i / 50 + 1 < 128 && b->died_at[b->i / 50 + 1] >= 2));
  /* mend when hurt and nothing is near (all the way up where it died before) */
  if (careful && standing(p) && p->hp < p->hp_max && p->en >= 2) {
    bool near = false;
    for (int i = 0; i < s->nen; i++) near |= s->en[i].alive && fabsf(s->en[i].x - p->x) < 11 && fabsf(s->en[i].y - p->y) < 8;
    if (!near) { in->my = 1, in->shoot = true; return; }
  }
  if (p->mend_t > 0) return;
  if (standing(p) && p->hp <= p->hp_max - 2 && p->en >= 2) {
    bool near = false;
    for (int i = 0; i < s->nen; i++) near |= s->en[i].alive && fabsf(s->en[i].x - p->x) < 11 && fabsf(s->en[i].y - p->y) < 8;
    if (!near) { in->my = 1, in->shoot = true; return; }
  }
  /* rest at a shrine the first time it is passed (the save point moves along with the bot) */
  for (int k = 0; k < s->w->nents && standing(p); k++) {
    const ent *e = &s->w->ents[k];
    if (e->kind == EK_SAVE && fabsf(e->x - p->x) < 1.5f && fabsf(e->y - p->y) < 1 && !((s->saves_lit[e->id >> 3] >> (e->id & 7)) & 1)) {
      in->interact = true;
      return;
    }
  }
  if (b->mode == B_MOVE && p->on_ground && p->state == PS_NORMAL && secret_near(s, p)) b->mode = B_POS; /* (break it first, then go again) */
  if (b->mode == B_MOVE) {
    const route_step *st = b->np ? &b->plan[b->pi] : &b->steps[b->i];
    if (!move_input(&MOVES[b->mv], b->t, p, &b->phase, in)) {
      int here = tile_of(p);
      b->mode = B_POS;
      if (here == st->to || (here / TW == st->to / TW && abs(here - st->to) <= 1 && p->on_ground)) { /* (a tile off is on it: the narrow pedestals) */
        if (b->np) {
          if (++b->pi >= b->np) b->np = 0;
        } else b->i++;
        b->fails = 0;
      } else b->mode = B_RECOVER;
      memset(in, 0, sizeof *in);
      return;
    }
    /* falling onto a hidden floor the route drops through (the checker broke it): strike it open on the way down */
    if (!p->on_ground && p->vy > 0 && p->atk_cd == 0 && st->to / TW > (int)roundf(p->y) + 1)
      for (int k = 0; k < 6; k++) {
        int tx = fl(p->x) + k % 3 - 1, ty = (int)floorf(p->y) + 1 + k / 3;
        if (w_in(tx, ty) && s->t[ty][tx] == T_SECRET && (float)ty - p->y < 1.4f) { in->my = 1, in->attack = true; break; }
      }
    b->t++;
    return;
  }
  if (b->mode == B_RECOVER) {
    if (!standing(p)) return;
    /* the next few steps first, then further on, then back along the route (after a fall or a return to a shrine) */
    if (recover(b, s, p, b->i, b->i + 12) || recover(b, s, p, b->i, b->i + 160) || recover(b, s, p, b->i - 3000, b->i)) {
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
  if (!p->on_ground && p->state == PS_NORMAL) { /* in the air between moves: strike up at a flyer close above, down at a hidden floor */
    for (int dx = -1; dx <= 1; dx++)
      for (int dy = 0; dy <= 3; dy++) {
        int tx = (int)floorf(p->x) + dx, ty = (int)floorf(p->y) + dy;
        if (w_in(tx, ty) && s->t[ty][tx] == T_SECRET && p->vy > 0 && p->atk_cd == 0 && (float)ty - p->y < 1.4f) { in->my = 1, in->attack = true; return; }
      }
    in->jump_held = p->vy < 0;
    for (int i = 0; i < s->nen; i++) {
      const enemy *e = &s->en[i];
      float dx = e->x - p->x, dy = (e->y - enemy_hh(e->kind)) - (p->y - 0.9f);
      if (e->alive && e->kind != EN_PUFF && fabsf(dx) < 1.6f && dy < -0.5f && dy > -3.4f && p->atk_cd == 0) in->my = -1, in->attack = true;
    }
    return;
  }
  if (p->on_ground && p->state == PS_NORMAL && secret_near(s, p) && strike_near(s, p, in)) return;
  { /* a cracked floor the route goes down through (the checker broke it with a plunge somewhere): plunge onto it */
    const route_step *nx = &b->steps[b->i < b->n ? b->i : b->n - 1];
    int fy = (int)roundf(p->y);
    bool cracked = false;
    for (int dx = -1; dx <= 1; dx++) cracked |= w_in(fl(p->x) + dx, fy) && s->t[fy][fl(p->x) + dx] == T_CRACKED;
    if (cracked && (s->abilities & AB_POUND) && nx->to / TW > fy + 2 && p->on_ground && p->state == PS_NORMAL) {
      b->mode = B_MOVE, b->mv = 80, b->t = 0, b->phase = 0, b->idle = 0; /* (the plunge straight down; the step after it recovers) */
      move_input(&MOVES[b->mv], b->t, p, &b->phase, in);
      b->t++;
      return;
    }
  }
  if (!b->ignore && p->on_ground && p->state == PS_NORMAL && (strike_near(s, p, in) || hunt(s, p, in))) return;
  if (s->trial_room >= 0 && p->on_ground && p->state == PS_NORMAL) { /* a trial: go to the guardians (the doors are shut) */
    const enemy *best = NULL;
    for (int i = 0; i < s->nen; i++)
      if (plain_target(&s->en[i]) && (!best || fabsf(s->en[i].x - p->x) < fabsf(best->x - p->x))) best = &s->en[i];
    if (best) {
      float tx = best->x + (best->x > p->x ? -1.8f : 1.8f);
      int y = (int)roundf(p->y);
      bool firm = node_ok(s, (int)floorf(p->x + (tx > p->x ? 1.0f : -1.0f)), y);
      if (firm) walk_to(p, tx, in);
      else if (fabsf(best->x - p->x) < 2.4f && p->atk_cd == 0) in->attack = true, in->my = best->y - enemy_hh(best->kind) < p->y - 2.6f ? -1 : 0;
      return;
    }
  }
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
  if (b->waited < 600 && !b->ignore)
    for (int i = 0; i < s->nen; i++) {
      const enemy *e = &s->en[i];
      if (e->alive && e->kind == EN_PUFF && fabsf(e->x - p->x) < 8 && fabsf(e->y - p->y) < 8 && (int)(s->tick % PUFF_CYCLE) > 30) {
        b->waited++; /* a puff near the way: go just after it goes soft (it stays soft a good while) */
        return;
      }
      if (!plain_target(e) || e->kind == EN_EYE) continue; /* (an eye stays put: it does not block a move) */
      bool spit_soon = e->kind == EN_SPITTER && (e->state == ES_TELL || e->t > 45) && fabsf(e->x - p->x) < 13 && fabsf(e->y - p->y) < 13;
      bool fly_near = careful && e->kind == EN_FLYER && fabsf(e->x - p->x) < 10 && fabsf(e->y - p->y) < 14 && e->state != ES_REST;
      if (e->alive && (spit_soon || fly_near || (e->kind != EN_SPITTER && fabsf(e->x - p->x) < 7 && fabsf(e->y - p->y) < 6))) {
        b->waited++;
        if ((e->kind == EN_FLYER || e->kind == EN_SPITTER) && (e->x < p->x) != (bool)p->facing_left) in->mx = (int8_t)(e->x < p->x ? -1 : 1);
        return;
      }
    }

  /* a move: stand on its tile first, then play it */
  int fx = st->from % TW, fy = st->from / TW;
  if (p->on_ground && (int)roundf(p->y) != fy) { if (standing(p)) b->mode = B_RECOVER; return; }
  if (abs(here % TW - fx) > 3 && standing(p)) { b->mode = B_RECOVER; return; }
  if (walk_to(p, (float)fx + 0.5f, in) && standing(p)) {
    b->mode = B_MOVE, b->mv = st->move, b->t = 0, b->phase = 0, b->idle = 0, b->waited = 0;
    move_input(&MOVES[b->mv], b->t, p, &b->phase, in);
    b->t++;
  }
  if (++b->idle > 400) b->idle = 0, b->mode = B_RECOVER;
}
