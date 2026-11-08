/* lanternfall: features painted into the rock's body (scenery.c). Render code only. */
#ifndef LANTERNFALL_SCENERY_H
#define LANTERNFALL_SCENERY_H

#include <stdint.h>

#include "cartwheel/render2d.h"

void scenery_init(void);
/* one feature of the region's material centred at (x, y) (world tiles), chosen and turned by the hash h, tinted by the
 * rock's body colour, at most `room` tiles across (the rock round it) */
void scenery_decal(int material, float x, float y, uint32_t h, cw_color body, float room);

#endif
