/* skate: a run. One to four skaters in one park, free skate (no clock), a score attack (two minutes, the park's goals,
 * the best score kept) or a score battle (two minutes, the most points wins). Fixed ticks from the input snapshot. */
#include "game.h"

const char *const MODE_NAMES[MODE_COUNT] = { "FREE SKATE", "SCORE ATTACK", "SCORE BATTLE" };

static const int GOAL_SCORES[PARK_COUNT][3] = {
  { 20000, 60000, 140000 }, { 20000, 55000, 120000 }, { 25000, 70000, 150000 }, { 20000, 60000, 130000 }, { 20000, 55000, 120000 },
};

void sim_init(sim *S, int park_id, int mode, int players) {
  int skin[MAXP];
  memcpy(skin, S->skin, sizeof skin);
  park_free(&S->k);
  memset(S, 0, sizeof *S);
  memcpy(S->skin, skin, sizeof skin);
  S->park_id = park_id;
  S->mode = mode;
  S->players = players < 1 ? 1 : players > MAXP ? MAXP : players;
  park_build(&S->k, park_id, PARKV_GAME);
  for (int i = 0; i < 3; i++) S->goal_score[i] = GOAL_SCORES[park_id % PARK_COUNT][i];
  for (int i = 0; i < S->players; i++) {
    S->s[i].rng = 0x51ed2701u + (uint32_t)i * 0x9e3779b9u;
    /* each player at their own start: the first general start, then the lines' */
    sk_spawn(&S->s[i], &S->k, i == 0 ? 0 : i);
  }
  S->clock = mode == MODE_FREE ? 0 : RUN_TICKS;
  S->state = RUN_PLAY;
  S->winner = -1;
}

void sim_free(sim *S) { park_free(&S->k); }

const char *sim_goal_text(const sim *S, int g, char *out, size_t cap) {
  switch (g) {
  case GOAL_SCORE1: case GOAL_SCORE2: case GOAL_SCORE3: snprintf(out, cap, "Score %d", S->goal_score[g - GOAL_SCORE1]); break;
  case GOAL_TOKENS: snprintf(out, cap, "Take the %d tokens", S->k.ntoken); break;
  case GOAL_COMBO: snprintf(out, cap, "Land a 10,000 combo"); break;
  case GOAL_GAP: if (S->k.goal_gap >= 0) snprintf(out, cap, "Clear the %s", S->k.gap[S->k.goal_gap].name); else snprintf(out, cap, "Land a 720"); break;
  case GOAL_GRIND: snprintf(out, cap, "Grind %s for %.1f s", S->k.goal_run_name, (double)S->k.goal_grind_s); break;
  default: out[0] = 0;
  }
  return out;
}

static void goals(sim *S, int i) {
  skater *s = &S->s[i];
  uint32_t g = S->goals[i];
  for (int k = 0; k < 3; k++) if (s->score >= S->goal_score[k]) g |= 1u << (GOAL_SCORE1 + k);
  if (S->k.ntoken && s->tokens == (1 << S->k.ntoken) - 1) g |= 1u << GOAL_TOKENS;
  if (s->best_combo >= 10000) g |= 1u << GOAL_COMBO;
  if (S->k.goal_gap >= 0 ? (s->gaps_hit & (1 << S->k.goal_gap)) : s->best_spin >= 720) g |= 1u << GOAL_GAP;
  if (s->st == SK_GRIND && s->grind_run == S->k.goal_run && s->grind_total_s >= S->k.goal_grind_s) g |= 1u << GOAL_GRIND;
  S->goals[i] = g;
}

uint32_t sim_hash(const sim *S) {
  uint32_t h = 2166136261u;
#define MIX(x) do { uint32_t v_; memcpy(&v_, &(x), 4); h = (h ^ v_) * 16777619u; } while (0)
  for (int i = 0; i < S->players; i++) {
    const skater *s = &S->s[i];
    MIX(s->p.x); MIX(s->p.y); MIX(s->p.z); MIX(s->v.x); MIX(s->v.y); MIX(s->v.z); MIX(s->f.x); MIX(s->f.z);
    MIX(s->score); MIX(s->st); MIX(s->cb.n);
  }
  MIX(S->tick);
#undef MIX
  return h;
}

void sim_step(sim *S, const input in[MAXP]) {
  if (S->state == RUN_OVER) {
    S->over_t++;
    return;
  }
  S->tick++;
  for (int i = 0; i < S->players; i++) {
    sk_step(&S->s[i], &S->k, &in[i]);
    goals(S, i);
  }
  if (S->mode != MODE_FREE && S->clock > 0) {
    S->clock--;
    if (S->clock == 0) {
      /* time: combos still going count if they are not bailed (they bank now) */
      for (int i = 0; i < S->players; i++) {
        skater *s = &S->s[i];
        if (s->st != SK_BAIL && s->cb.n) {
          int v = sk_combo_value(&s->cb);
          s->score += v;
          if (v > s->best_combo) s->best_combo = v;
          memset(&s->cb, 0, sizeof s->cb);
        }
        goals(S, i);
      }
      int best = -1, bs = -1;
      for (int i = 0; i < S->players; i++) if (S->s[i].score > bs) bs = S->s[i].score, best = i;
      S->winner = best;
      S->state = RUN_OVER;
      S->over_t = 0;
    }
  }
  S->hash = sim_hash(S);
}
