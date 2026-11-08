/* towers bots: play a seat through the same input a pad gives (hops of the cursor and the four face buttons), so the
 * auto cart, the soak and the map check's clear run all exercise the real controls. A bot works out its goal from
 * the rules' state alone each tick (no memory of its own): the empty slot of its share that reaches the most path,
 * built in a fixed mix of the four towers, or the tower of its that an upgrade does most for, whichever the gold
 * buys more of; then it hops its cursor there (one press every other tick) and presses the button (to the third
 * level: south for the A branch, east for B, as g_bot_branch says). */
#include <stdlib.h>

#include "cartwheel/math.h"
#include "game.h"

int g_bot_branch; /* 0: the A branch (the south button) for every third level, 1: B (east), 2: A and B by slot in turn */

/* the mix a bot builds in, by how many towers it holds */
static const int MIX[8] = { TK_BOLT, TK_FROST, TK_MORTAR, TK_BOLT, TK_BEAM, TK_MORTAR, TK_FROST, TK_BEAM };

void towers_bot(const sim *s, int i, input *in) {
  const player *p = &s->p[i];
  if (!p->active || p->slot < 0 || s->state != ST_PLAY) return;
  int mine = 0;
  for (int k = 0; k < s->m.nslot; k++) mine += s->tw[k].alive && s->tw[k].owner == i;
  /* the best empty slot of the share and the best upgrade, each as reach for the gold */
  int bs = -1, bu = -1;
  float vs = 0, vu = 0;
  int kind = MIX[mine % 8];
  for (int k = 0; k < s->m.nslot; k++) {
    if (!sim_slot_mine(s, i, k)) continue;
    const tower *t = &s->tw[k];
    if (!t->alive) {
      float v = (float)s->cover[k] / (float)sim_tower_cost(kind, 1);
      if (s->cover[k] > 8 && (bs < 0 || v > vs)) vs = v, bs = k;
    } else if (t->level < 3) {
      float v = (float)s->cover[k] * (t->level == 1 ? 1.35f : 1.1f) / (float)sim_tower_cost(t->kind, t->level + 1);
      if (bu < 0 || v > vu) vu = v, bu = k;
    }
  }
  int goal = -1, act = -1;
  bool can_build = bs >= 0 && p->gold >= sim_tower_cost(kind, 1);
  bool can_up = bu >= 0 && p->gold >= sim_tower_cost(s->tw[bu].kind, s->tw[bu].level + 1);
  if (can_build && (!can_up || vs >= vu * (mine < 3 ? 0.5f : 1.0f))) goal = bs, act = kind;
  else if (can_up) {
    goal = bu, act = A_S;
    if (s->tw[bu].level == 2 && (g_bot_branch == 1 || (g_bot_branch == 2 && bu % 2))) act = A_E;
  }
  /* the next wave early when the field is quiet and nothing is to be bought (the first bot calls) */
  if (i == 0 && s->wave >= 2 && s->next_t > 600 && s->nenemies == 0 && s->core_hp == CORE_HP && goal < 0) in->wave = (s->tick / 30) % 2 == 0;
  if (goal < 0) return;
  if (goal == p->slot) {
    if (s->tick % 2 == 0) in->act[act] = true;
    return;
  }
  /* a press toward the goal every other tick (each a fresh hop) */
  if (s->tick % 2) return;
  float dx = (float)s->m.slot[goal].x - (float)s->m.slot[p->slot].x, dy = (float)s->m.slot[goal].y - (float)s->m.slot[p->slot].y;
  float ax = fabsf(dx), ay = fabsf(dy);
  in->dx = (int8_t)(ax > ay * 0.45f ? (dx > 0 ? 1 : -1) : 0);
  in->dy = (int8_t)(ay > ax * 0.45f ? (dy > 0 ? 1 : -1) : 0);
}
