/* The match-three rules, with no drawing and no engine in them: a grid in, what happened out. tests/jewels-rules.mjs
 * holds them to the original Lua rules move for move.
 *
 *   a move swaps two orthogonal neighbours, and is legal only when it lines up three or more of a kind;
 *   every matched run clears at once, the jewels above fall, new ones drop in from the top;
 *   that repeats on its own until nothing matches (a cascade); a board with no legal move is reshuffled.
 *
 * Cells are grid[y][x], y 0..H-1 from the top, x 0..W-1 from the left; kinds 1..KINDS, 0 empty. */
#ifndef JEWELS_RULES_H
#define JEWELS_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define JW_W 10
#define JW_H 8
#define JW_KINDS 6
#define JW_MAX_RUNS 64
#define JW_MAX_MOVES (JW_W * JW_H * 2)

typedef struct jw_grid { uint8_t c[JW_H][JW_W]; } jw_grid;

/* xorshift32: the generator the rules draw from (tests/jewels-rules-driver.lua gives the Lua the same one) */
typedef struct jw_rng { uint32_t s; } jw_rng;
void jw_rng_seed(jw_rng *r, uint32_t seed);
uint32_t jw_rng_u32(jw_rng *r);
int jw_rng_int(jw_rng *r, int n); /* 1..n */
float jw_rng_float(jw_rng *r);    /* 0..1 */

typedef struct jw_run { uint8_t n, horiz, x, y; } jw_run; /* length, direction, first cell */
typedef struct jw_matches {
  uint8_t hit[JW_H][JW_W];
  int nhit;
  jw_run runs[JW_MAX_RUNS];
  int nruns;
} jw_matches;

typedef struct jw_move { int8_t x, y, dx, dy; } jw_move;

/* what a collapse did, for the view to animate */
typedef struct jw_fall { int8_t x, from_y, to_y; } jw_fall;
typedef struct jw_spawn { int8_t x, y, kind, from_y; } jw_spawn; /* from_y < 0: above the board */
typedef struct jw_collapse {
  jw_fall fell[JW_W * JW_H];
  int nfell;
  jw_spawn spawned[JW_W * JW_H];
  int nspawned;
} jw_collapse;

bool jw_inside(int x, int y);
void jw_find_matches(const jw_grid *g, jw_matches *m);
bool jw_any_match(const jw_grid *g);
void jw_swap(jw_grid *g, int x1, int y1, int x2, int y2);
bool jw_is_legal(jw_grid *g, int x1, int y1, int x2, int y2);
int jw_legal_moves(jw_grid *g, jw_move *out); /* out may be NULL; row order, right before down */
void jw_collapse_grid(jw_grid *g, const jw_matches *m, jw_rng *r, jw_collapse *out);
void jw_new_board(jw_grid *g, jw_rng *r);
bool jw_reshuffle(jw_grid *g, jw_rng *r); /* false when it fell back to a new board */
int64_t jw_run_score(int n, int cascade);
extern int jw_refill_kinds;

#endif
