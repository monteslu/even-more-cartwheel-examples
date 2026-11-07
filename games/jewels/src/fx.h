/* The effects: what the game's events look and feel like (no rules here, and nothing here reaches the board).
 *
 * Scale: a clear's power grows with the cells it takes, the length of its runs, crossings (an L, a T, a plus) and
 * its step in a cascade. Power sets the burst (shards and sparks), the flash that lights the stones round it, a
 * ring across the velvet from four cells up or from the second step, the pad's rumble, and the score's pop.
 * Cascades: a counter that builds a step at a time, a streak of light from one cleared group to the next, the room
 * warming step by step, and a finale (a sweep of light and a shower of sparks round the frame) when a cascade of
 * four steps or more comes to rest. Everything fades within about a second: what cleared and where the new jewels
 * fell stays readable. */
#ifndef JEWELS_FX_H
#define JEWELS_FX_H

#include "game.h"
#include "render.h"

#define JW_FX_BURSTS 96
#define JW_FX_POPS 16

typedef struct jw_pop { float x, y, t, power; int64_t value; int step; } jw_pop;

typedef struct jw_fx {
  float time;
  jw_burst_draw bursts[JW_FX_BURSTS];
  int nbursts;
  jw_ring_draw rings[JW_MAX_RINGS];
  int nrings;
  struct { float x, y, t, power, r, g, b; } flash[8];
  int nflash;
  jw_trail trails[JW_MAX_TRAILS];
  int ntrails;
  jw_pop pops[JW_FX_POPS];
  int npops;
  float last_cx, last_cy; int last_step;
  float warmth, warmth_target;
  float sweep_t;          /* <0 none */
  int combo, combo_shown; float combo_t, combo_hold;
  float finale_t; int finale_steps; int64_t finale_gain;
  float reshuffle;
  float hint_t;
  uint32_t seed;
  /* the record the effects test reads */
  float power_last, power_max;
  float step_power[8];    /* the strongest clear at each cascade step this move */
  int sparks_last, shards_last;
  int finales, rumbles;
  float rumble_last;
} jw_fx;

void jw_fx_init(jw_fx *f);
/* the game's events since the last call; dt seconds */
void jw_fx_events(jw_fx *f, const jw_game *g, float dt);
/* rumble requests: the strength of the strongest event this tick (0 none), read and cleared by the caller */
float jw_fx_take_rumble(jw_fx *f, int *ms);
/* fill the frame's effects */
void jw_fx_frame(const jw_fx *f, jw_frame *out, float alpha_dt);
/* a clear's power (exposed for the tests) */
float jw_fx_power(int cells, int step, int shape);

#endif
