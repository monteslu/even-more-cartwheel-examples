/* lanternfall: reachability over the real movement code. A node is a tile a player can stand on; an edge is a "move"
 * (a short input program: a run-up, a jump held so long, a second jump at a tick, a dash, a climb, a plunge, a
 * swim, a swing on the grapple line) played through player_step from the node until the player stands still again.
 * The checker (tools/check.c) walks the whole world with them stage by stage, and the bot plays the same moves
 * through the same input struct the pad fills. */
#ifndef LANTERNFALL_REACH_H
#define LANTERNFALL_REACH_H

#include "sim.h"

typedef struct move {
  int8_t dir;          /* -1, 0, 1 */
  uint8_t run;         /* ticks of run-up before the jump */
  uint8_t hold;        /* ticks the jump is held (0: no jump: walk off, a ground dash, a drop) */
  uint8_t air;         /* ticks the direction is held after the jump (255 all the way) */
  uint8_t dj;          /* tick of the second jump after the first (0 none) */
  uint8_t dash;        /* tick of the dash after the jump (0 none; 255 a ground dash at once) */
  uint8_t grapple, release, chain; /* tick of the line, ticks until the jump off it (0 none), lines again after */
  uint8_t climb;       /* hold toward the wall and up after the jump */
  uint8_t walljump;    /* tick of a jump off the wall (with climb) */
  uint8_t cdash;       /* tick of a dash off the wall (with climb): climbing and the dash chain */
  uint8_t pound;       /* tick of the plunge after the jump */
  int8_t swim;         /* hold this vertical while swimming (with 2 = no swim) */
  uint8_t drop;        /* down + jump through a one-way ledge */
  uint8_t need;        /* abilities the move uses (AB_*) */
} move;

extern move MOVES[];
extern int NMOVES;
void moves_init(void);

/* the input of move m at tick t (t counts from the move's start); phase: 0 before the player left the ground,
 * 1 airborne, 2 landed again (input released). Returns false when the move is over. */
bool move_input(const move *m, int t, const player *p, int *phase, pinput *in);

#define MOVE_MAX_TICKS 300

/* play move m from a standing spot (feet at x, y) with these tiles and abilities; returns ticks taken and leaves
 * the player where it settled, or -1 when it ended badly (thorns, the cold, never settled). broke: a tile a plunge
 * landed on (cracked floors), or -1 */
int move_play(const move *m, const world *w, const uint8_t (*t)[TW], uint32_t abilities, float x, float y, player *out, int *broke);

#endif
