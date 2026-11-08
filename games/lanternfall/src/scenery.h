/* lanternfall: features painted into the rock's body (scenery.c). Render code only. */
#ifndef LANTERNFALL_SCENERY_H
#define LANTERNFALL_SCENERY_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/render2d.h"

void scenery_init(void);
/* one feature of the region's material centred at (x, y) (world tiles), chosen and turned by the hash h, tinted by the
 * rock's body colour, at most `room` tiles across (the rock round it) */
void scenery_decal(int material, float x, float y, uint32_t h, cw_color body, float room);

/* a prop painted (grass, mushrooms, a boulder, a fern, crystals, flowers, a machine, a tree, a crystal cluster, a shelf, a
 * statue) at its feet (x, y): true when it drew it (a machine's furnace and pilot light are still view.c's) */
bool scenery_prop(int sub, int arg, float x, float y, int region, float t);
/* what holds up a one-way ledge running over tiles x0..x1 of row y: a bracket from a wall beside an end, else chains from the
 * ceiling ceil_* tiles up (0: none), else knotted roots */
void scenery_ledge(int x0, int x1, int y, bool wall_l, bool wall_r, int ceil_l, int ceil_r, int material, cw_color tint);

/* what holds a one-way ledge up, from a PR_SUPPORT prop at (x, y): hold HOLD_CEIL a chain down len tiles, HOLD_FLOOR a post up
 * len tiles, HOLD_WALL_L / HOLD_WALL_R a bracket from that wall */
void scenery_support(int hold, float x, float y, int len, cw_color tint);
/* a region's landmark (its beacon tower, in the region's own building) standing at (x, y); the lamp's place for its light */
bool scenery_landmark(int region, float x, float y, float t, float *lamp_x, float *lamp_y);
/* a map stone (map) or a tablet of the story standing at (x, y): carved stone with a glowing inlay */
bool scenery_tablet(bool map, float x, float y, float t);
/* the landmark of a region drawn h high with its foot at (x, y) (the map) */
bool scenery_landmark_icon(int region, float x, float y, float h, cw_color tint);
/* the map's chart: painted vellum over the canvas (w x h, screen units), a compass rose in its top right corner */
void scenery_map_paper(float w, float h, float a);

#endif
