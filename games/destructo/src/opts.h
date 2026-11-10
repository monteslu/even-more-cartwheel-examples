/* Destructo's run options: what a gate or a developer sets on a running game through the debug channel (carts read no
 * environment, and the gates run every check as a cart on romdev). The `dm` command:
 *   dm set NAME VALUE...   sets an option (the rest of the request is its value: dm set LEVEL_NAME Space Needle)
 *   dm unset NAME          clears one
 *   dm list                every option set, as JSON
 *   dm restart             the game starts its level again with the options as they are now (autopilot builds: the
 *                          level LEVEL or LEVEL_NAME names; the counters and the blast's timing window start over)
 * Names are the game's old development switches without their DESTRUCTO_ prefix: LEVEL, LEVEL_NAME, PLAYERS,
 * QUALITY, FREEZE, HOLD, ORBIT, OLD_RULES, REPLAY_PERTURB, SEAT_CONTROL, OVERVIEW, NO_HUD, LOOK_CONTROL, STROBE_CONTROL,
 * FX_LOG, TOD, NORTH, MIE, RAYLEIGH, SUN, SKY, EXPOSURE, GRADE, TONEMAP, NOFOG, NO_GRASS, NO_TREES, LAYER_LOG. */
#ifndef DESTRUCTO_OPTS_H
#define DESTRUCTO_OPTS_H

#include <stdbool.h>

/* the option's value, or NULL when it is not set */
const char *opt_get(const char *name);
bool opt_on(const char *name); /* set (to anything but "0") */
void opt_set(const char *name, const char *value);
/* registers the `dm` command; restart runs on `dm restart`, set_quality when QUALITY is set */
void opt_init(void (*restart)(void), void (*changed)(const char *name));

#endif
