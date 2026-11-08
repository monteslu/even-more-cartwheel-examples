/* destructo's trees (render only): layered foliage cards on instanced meshes, swaying in the wind */
#ifndef DESTRUCTO_TREES_H
#define DESTRUCTO_TREES_H
#include "cartwheel/scene.h"

enum { SP_OAK, SP_BIRCH, SP_SPRUCE, SP_FIR_SNOW, SP_PALM, SP_COUNT };
#define TREES_MAX 128
typedef struct tree_spot {
  float x, y, z, yaw, scale, stretch;
  int species;
  t3_color leaf; /* the foliage's colour (multiplies the species' own) */
} tree_spot;

void trees_init(void);                                   /* the leaf, needle and frond textures */
void trees_build(cw_node parent, const tree_spot *s, int n); /* at most two species; the meshes go under parent */
void trees_clear(void);                                  /* parent was freed */
void trees_sway(float t);                                /* every frame: the wind, t in seconds */
/* grass: tufts of alpha-cut blades (two crossed cards each) in one static mesh under parent */
typedef struct grass_tuft { float x, y, z, yaw, w, h; t3_color c; } grass_tuft;
void grass_build(cw_node parent, const grass_tuft *t, int n);
#endif
