/* demolition's effects (render only) */
#ifndef DEMOLITION_FX_H
#define DEMOLITION_FX_H
#include "cartwheel/scene.h"
#include "game.h"

typedef struct fx_stats { int dust, chips, sparks, shards, booms, rumbles; } fx_stats;
void fx_init(cw_node root);
void fx_events(const game *g, int shooter); /* after each tick's step */
void fx_update(float dt);                   /* every frame */
fx_stats fx_get_stats(void);
void fx_prime(t3_vec3 under); /* compile every effect's programs now (at a level's load) */

#endif
