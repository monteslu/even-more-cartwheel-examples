/* explorer: the bot (bot.c). It plays through the same input struct the pad fills: it follows the checker's route
 * (route.bin: the moves from the start through every pickup and warden), standing on each step's tile and playing
 * its move tick by tick; when something knocks it off the route it searches nearby with the same moves for a way
 * back; in a warden's arena it fights by reading the tells. */
#ifndef EXPLORER_BOT_H
#define EXPLORER_BOT_H

#include "reach.h"

typedef struct route_step { int32_t from, to; uint8_t move, stage, goal, pad; } route_step;

typedef struct bot {
  const route_step *steps;
  int n, i;
  int mode, mv, t, phase;
  route_step plan[96];
  int np, pi;
  int stuck, idle, recoveries, fails, waited;
  int last_tile;
  int wiggle;
  uint32_t seed;
} bot;

bool bot_load(bot *b, const void *bytes, size_t len, uint32_t world_hash);
void bot_input(bot *b, const sim *s, int pi, pinput *out);
/* for player 2 in the auto build: keep near player 1, strike what comes close */
void bot_follow(const sim *s, int pi, pinput *out);

#endif
