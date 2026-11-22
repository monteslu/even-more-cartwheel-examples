/* detexto's effects (render only) */
#ifndef DETEXTO_FX_H
#define DETEXTO_FX_H
#include "cartwheel/scene.h"
#include "game.h"

typedef struct fx_stats { int dust, chips, sparks, shards, booms, rumbles; } fx_stats;
void fx_init(cw_node root);
void fx_events(const game *g, int shooter); /* after each tick's step */
void fx_update(float dt);                   /* every frame */
fx_stats fx_get_stats(void);
void fx_prime(t3_vec3 under);
/* the dust, smoke and plume clouds' opacity (1 as made): the 1-bit looks draw them lighter, so the structure reads through a
 * cloud rather than behind a field of white dots (dither.h) */
void fx_set_cloud_opacity(float a);
void fx_clear(void);           /* a new level: the ground marks and lights go */ /* compile every effect's programs now (at a level's load) */

#endif
