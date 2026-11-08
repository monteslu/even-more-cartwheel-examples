/* pinball's lights in motion: how every light on the table follows the rules (lights.c). Shared by the view, which
 * draws them, and by pinball_flashcheck, which measures them headless over whole games.
 *
 * The house rule: nothing strobes. A light swells and fades; it never blinks or flickers. Each light here is a glow
 * that eases toward what the rules want at a limited speed, and that may turn round (from rising to falling or back)
 * at most once every GLOW_TURN ticks: three turns in any second at the most, however fast the rules toggle it (a
 * player tapping the flippers moves the lit lanes a dozen times a second; a ball rattling in the pops hits them as
 * often). A lamp the rules pulse (lit for a shot, a running mode) is a smooth wave of at most 1.1 cycles a second.
 * Flashers rise over a few frames, hold and fade, and fire again only after 1.3 s. Deterministic: ticks only. */
#ifndef PINBALL_LIGHTS_H
#define PINBALL_LIGHTS_H
#include "game.h"

#define GLOW_TURN 26     /* ticks between two turns of one light (under 2.5 a second: never 3) */
#define NFLASH 4
#define NPOPL 4

typedef struct glow {
  float v;               /* drawn level 0..1 */
  int8_t dir;            /* +1 rising, -1 falling, 0 settled */
  int16_t since;         /* ticks since it last turned */
} glow;

typedef struct pb_lights {
  glow lamp[MAXLAMP];
  glow part[MAXEL];      /* a standup, a drop target, a pop's cap: lit by a hit */
  int part_hold[MAXEL];  /* ticks a hit keeps its part wanting light */
  glow pop[NPOPL];       /* each pop's light */
  int pop_hold[NPOPL];
  glow flash[NFLASH];    /* the flashers */
  float flash_want[NFLASH];
  int flash_hold[NFLASH], flash_cool[NFLASH];
  int nflash;
  glow gi;               /* the general illumination: 1 normal, dimmer in a drain or a tilt, a breath at a mode start */
  int gi_dip, gi_cool;   /* ticks of a mode start's dip left, ticks before another may start */
  int last_tick, frames;
} pb_lights;

void lights_reset(pb_lights *L, int nflash);
/* one tick: the rules' lamp states and this tick's events (g->fx) into the drawn levels */
void lights_step(pb_lights *L, const game *g);
/* a glow toward want, rising at most `up` and falling at most `down` a tick, turning at most once every GLOW_TURN */
void glow_step(glow *gl, float want, float up, float down);

#endif
