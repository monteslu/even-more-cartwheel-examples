/* The effects: what the game's events look and feel like (no rules here, and nothing here reaches the board).
 *
 * Scale: a clear's power grows with the cells it takes, the length of its runs, crossings (an L, a T, a plus) and
 * its step in a cascade. Power sets everything a clear does.
 *
 * One clear, beat by beat: the matched stones swell and glow (the anticipation, five ticks); then each breaks into
 * shards of itself (traced like the stone, with short trails), sparks and a dust of sparkles that lingers, and each
 * gives off a light of its own colour that swells over 90 ms and fades over a few hundred: that light lights the
 * velvet, the walnut and the gold round it, and the neighbouring stones see it as a lamp (their facets flash it back,
 * and its hot core splits into fire). Where it stood, an afterglow of its colour lies on the velvet and fades over
 * about a second. Bigger clears add a ring that bends the picture as it passes (a shockwave) and rays of light. The
 * chime, the shatter and the pad's rumble land on the same ticks.
 *
 * Cascades: a counter that builds a step at a time, a streak of light from one cleared group to the next, the room
 * warming and brightening step by step, and when a cascade of four steps or more comes to rest a climax: a slow swell
 * of warm light over the whole board, a fan of rays, a sweep of light, a shower round the frame and the pad's longest
 * rumble. Never screen shake.
 *
 * No strobe: every light swells and fades (a smoothstep rise, an exponential fall), so no region of the screen
 * alternates; tests/jewels-flash.mjs holds the worst case to the photosensitivity thresholds. */
#ifndef JEWELS_FX_H
#define JEWELS_FX_H

#include "game.h"
#include "render.h"

#define JW_FX_BURSTS 96
#define JW_FX_POPS 16
#define JW_FX_LIGHTS 64

typedef struct jw_pop { float x, y, t, power; int64_t value; int step; } jw_pop;
/* a light in flight: its peak colour, how it swells and fades */
typedef struct jw_fx_light { float x, y, z, r, g, b, reach, core, stones, t, rise, fall, delay; } jw_fx_light;

typedef struct jw_fx {
  float time;
  jw_burst_draw bursts[JW_FX_BURSTS];
  int nbursts;
  jw_ring_draw rings[JW_MAX_RINGS];
  int nrings;
  jw_fx_light lights[JW_FX_LIGHTS];
  int nlights;
  jw_rays rays[JW_MAX_RAYS];
  int nrays;
  jw_trail trails[JW_MAX_TRAILS];
  int ntrails;
  jw_pop pops[JW_FX_POPS];
  int npops;
  float last_cx, last_cy; int last_step;
  float warmth, warmth_target;
  float room, room_target;
  float sweep_t;          /* <0 none */
  int combo, combo_shown; float combo_t, combo_hold;
  float finale_t; int finale_steps; int64_t finale_gain;
  float reshuffle;
  float hint_t;
  float strobe_t;         /* the photosensitivity gate's control (JEWELS_STROBE): a deliberate strobe, <0 none */
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
/* a light's strength `t` seconds into it (0..1): a smooth swell, then a smooth fall */
float jw_fx_envelope(float t, float rise, float fall);

#endif
