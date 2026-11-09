/* towers newcomer: a seat played the way a first-time player plays it, for the difficulty check (tests/towers-newcomer.mjs,
 * towers_soak TOWERS_BOT=newcomer). Unlike towers_bot it reads only what the screen shows and acts at a person's pace:
 *   - what it sees: its gold (the HUD), the costs on the build diamond and the upgrade pill, its own pads (the cursor
 *     hops between them) and roughly how much path runs past each pad (judged by eye: the true reach, give or take
 *     a third, and never the reach rings' numbers)
 *   - how it chooses: a pad among those that look good, nearer ones first (a person does not cross the map for a few
 *     metres more path); a tower kind by taste, not by the table (the first is a bolt, the top of the diamond); an
 *     upgrade only once it has a few towers, on one of its towers picked at random; a branch at random
 *   - its pace: it looks for 1 to 2.5 s before it decides, hops one pad a press (held 4 to 7 ticks, let go 5 to 12),
 *     sometimes hops the wrong way first, reads the pill for a moment before it presses, and after a build watches
 *     the field for a few seconds
 *   - what it reads: the "next" line's word on a kind the map has not sent before (its tower: beams for the armoured,
 *     say) makes that tower its favourite from then on
 *   - what it learns: a creep that reaches the core is seen (the core's count drops, the leak flashes), and for the
 *     next while it builds nearer the core
 *   - what it never does: call a wave early, change a tower's aim, sell, or use the storm before it has seen a crowd
 *     near its cursor for a while (the HUD's storm pill is there, so it finds it, slowly)
 * It keeps its own state (a person remembers what they meant to do) and its own random numbers, so it never touches
 * the rules' generator. */
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

enum { NB_LOOK, NB_MOVE, NB_READ, NB_WATCH };
typedef struct nbot {
  bool init;
  uint32_t rng;
  uint32_t eye;
  int phase, wait, goal, act, press, gap, crowd_t, built, leaks_seen, worry, hint_read;
  float taste[TK_KINDS];
} nbot;
static nbot NB[MAXP];
uint32_t g_newcomer_seed = 1;

static uint32_t nr(nbot *b) {
  b->rng ^= b->rng << 13, b->rng ^= b->rng >> 17, b->rng ^= b->rng << 5;
  return b->rng;
}
static float nf(nbot *b) { return (float)(nr(b) >> 8) / 16777216.0f; }
static int ni(nbot *b, int lo, int hi) { return lo + (int)(nf(b) * (float)(hi - lo + 1)); }

void towers_newcomer_reset(void) { memset(NB, 0, sizeof NB); }

static float seen_cover(nbot *b, const sim *s, int k) {
  /* the same pad always looks the same to the same person: the error is fixed per pad */
  uint32_t h = ((uint32_t)k * 2654435761u ^ b->eye) * 2246822519u; h ^= h >> 15;
  float e = 0.67f + 0.66f * (float)(h % 1000u) / 1000.0f;
  return (float)s->cover[k] * e;
}

static int pick_kind(nbot *b, int gold) {
  if (b->built == 0) return TK_BOLT; /* the top of the diamond, first */
  float sum = 0;
  for (int k = 0; k < TK_KINDS; k++) if (gold >= sim_tower_cost(k, 1)) sum += b->taste[k];
  if (sum <= 0) return -1;
  float r = nf(b) * sum;
  for (int k = 0; k < TK_KINDS; k++)
    if (gold >= sim_tower_cost(k, 1)) { if ((r -= b->taste[k]) <= 0) return k; }
  return TK_BOLT;
}

static void decide(nbot *b, const sim *s, int i) {
  const player *p = &s->p[i];
  b->goal = -1;
  int mine = 0;
  for (int k = 0; k < s->m.nslot; k++) mine += s->tw[k].alive && s->tw[k].owner == i;
  /* the pads that look worth a tower: within the best few by eye, the nearer the likelier */
  float best = 0;
  for (int k = 0; k < s->m.nslot; k++)
    if (sim_slot_mine(s, i, k) && !s->tw[k].alive) best = fmaxf(best, seen_cover(b, s, k));
  int cand = -1;
  float cs = -1;
  const mp_slot *c = &s->m.slot[p->slot];
  for (int k = 0; k < s->m.nslot; k++) {
    if (!sim_slot_mine(s, i, k) || s->tw[k].alive) continue;
    float v = seen_cover(b, s, k);
    if (v < best * 0.6f || s->cover[k] <= 4) continue;
    float dx = (float)(s->m.slot[k].x - c->x), dy = (float)(s->m.slot[k].y - c->y);
    float sc = v / (1.0f + 0.04f * sqrtf(dx * dx + dy * dy)) * (0.75f + 0.5f * nf(b));
    if (b->worry > 0) { /* a leak was seen: the pads by the core look better */
      float qx = mp_wx(&s->m, s->m.slot[k].x) - s->core_x, qy = mp_wz(&s->m, s->m.slot[k].y) - s->core_y;
      sc *= 1.0f + 2.0f / (1.0f + 0.15f * sqrtf(qx * qx + qy * qy));
    }
    if (sc > cs) cs = sc, cand = k;
  }
  int kind = pick_kind(b, p->gold);
  /* saving up: a person wants the tower they like, not whatever is cheapest (a third of the time they wait) */
  bool build = cand >= 0 && kind >= 0 && (mine < 2 || nf(b) < 0.7f);
  /* an upgrade: once there are three towers, on one of them (the green upgrade pill is under every tower it visits), as
   * often as a build once there are four */
  int up = -1;
  if (mine >= 3 && (!build || nf(b) < (mine >= 4 ? 0.5f : 0.25f))) {
    int n = 0;
    for (int k = 0; k < s->m.nslot; k++) {
      const tower *t = &s->tw[k];
      if (t->alive && t->owner == i && t->level < 3 && p->gold >= sim_tower_cost(t->kind, t->level + 1) && ni(b, 0, n++) == 0) up = k;
    }
  }
  if (up >= 0) {
    b->goal = up;
    b->act = A_S;
    if (s->tw[up].level == 2 && nf(b) < 0.5f) b->act = A_E;
  } else if (build) {
    b->goal = cand;
    b->act = kind; /* the diamond: the TK_ order is the A_ order */
  }
}

void towers_newcomer(const sim *s, int i, input *in) {
  const player *p = &s->p[i];
  if (!p->active || p->slot < 0 || s->state != ST_PLAY) return;
  nbot *b = &NB[i];
  if (!b->init) {
    memset(b, 0, sizeof *b);
    b->init = true;
    b->rng = 0x9e3779b9u * (uint32_t)(i + 1) ^ g_newcomer_seed * 2246822519u;
    if (!b->rng) b->rng = 1;
    for (int k = 0; k < 8; k++) nr(b);
    for (int k = 0; k < TK_KINDS; k++) b->taste[k] = 0.5f + nf(b);
    b->eye = nr(b);
    b->taste[TK_BOLT] += 0.6f; /* the first one they met */
    b->wait = ni(b, 120, 300); /* reading the screen at the start */
  }
  /* the storm: a crowd round the cursor for a couple of seconds, then the pill is noticed */
  if (p->power_cd == 0) {
    float x = mp_wx(&s->m, s->m.slot[p->slot].x), y = mp_wz(&s->m, s->m.slot[p->slot].y);
    int near = 0;
    for (int k = 0; k < MAXE && near < 25; k++) {
      const creep *e = &s->e[k];
      if (!e->alive) continue;
      float dx = e->x - x, dy = e->y - y;
      near += dx * dx + dy * dy < POWER_R * POWER_R;
    }
    b->crowd_t = near >= 25 ? b->crowd_t + 1 : 0;
    if (b->crowd_t > 150 && nf(b) < 0.02f) in->power = true, b->crowd_t = 0;
  }
  { /* the "next" line, read once a wave while it shows */
    int nw = s->next_t > 0 ? sim_wave_new_kind(s, s->wave + 1) : -1;
    if (!g_old_difficulty && nw >= 0 && b->hint_read != s->wave + 1 && sim_kind_counter(nw) >= 0) b->hint_read = s->wave + 1, b->taste[sim_kind_counter(nw)] += 1.5f;
  }
  if (s->leaks > b->leaks_seen) b->leaks_seen = s->leaks, b->worry = 1800;
  else if (b->worry > 0) b->worry--;
  if (b->press > 0) { /* a hop's press still held */
    b->press--;
    in->dx = p->hx, in->dy = p->hy;
    return;
  }
  if (b->wait > 0) { b->wait--; return; }
  switch (b->phase) {
  case NB_LOOK:
    decide(b, s, i);
    if (b->goal < 0) { b->wait = ni(b, 60, 150); return; }
    b->phase = NB_MOVE;
    return;
  case NB_MOVE: {
    if (b->goal == p->slot) { b->phase = NB_READ, b->wait = ni(b, 15, 45); return; }
    float dx = (float)s->m.slot[b->goal].x - (float)s->m.slot[p->slot].x, dy = (float)s->m.slot[b->goal].y - (float)s->m.slot[p->slot].y;
    float ax = fabsf(dx), ay = fabsf(dy);
    int8_t hx = (int8_t)(ax > ay * 0.45f ? (dx > 0 ? 1 : -1) : 0), hy = (int8_t)(ay > ax * 0.45f ? (dy > 0 ? 1 : -1) : 0);
    if (nf(b) < 0.1f) hx = (int8_t)ni(b, -1, 1), hy = (int8_t)ni(b, -1, 1); /* the wrong way, now and then */
    if (!hx && !hy) hx = 1;
    in->dx = hx, in->dy = hy;
    b->press = ni(b, 3, 6);
    b->wait = ni(b, 5, 12);
    if (++b->gap > 40) b->phase = NB_LOOK, b->gap = 0; /* lost: look again */
    return;
  }
  case NB_READ: {
    b->gap = 0;
    const tower *t = &s->tw[p->slot];
    int cost = t->alive ? (t->level < 3 ? sim_tower_cost(t->kind, t->level + 1) : 1 << 30) : sim_tower_cost(b->act, 1);
    if (p->gold >= cost) {
      in->act[b->act] = true;
      if (!t->alive) b->built++;
    }
    b->phase = NB_WATCH;
    b->wait = ni(b, 90, 300); /* watch it work */
    return;
  }
  default:
    b->phase = NB_LOOK;
  }
}
