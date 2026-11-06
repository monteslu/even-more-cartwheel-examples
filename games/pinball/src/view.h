/* pinball's view: the table in 3D (render only: nothing here feeds the rules) */
#ifndef PINBALL_VIEW_H
#define PINBALL_VIEW_H
#include "cartwheel/render3d.h"
#include "cartwheel/scene.h"
#include "game.h"

/* table frame (x across, y up the table, z off the playfield) to world (y up, the table's middle at the origin, the
 * player's end toward +z) */
#define TABLE_MID 0.545f
static inline t3_vec3 tw(float x, float y, float z) { return (t3_vec3){ x, z, TABLE_MID - y }; }

uint8_t *art_paint(const table *T, int *w, int *h); /* art.c: malloc'd RGBA */

void view_init(cw_node root);
void view_build(const game *g);          /* (re)build the table's meshes and lights for g's table */
void view_sync(const game *g, float alpha, float dt); /* every frame */
void view_camera(const game *g, cw_node camera, float aspect, float dt);
bool view_screen(float x, float y, float z, float *u, float *v); /* a table point on screen, 0..1 */
int view_built_table(void);

typedef struct view_stats { int lights, lamps_lit; float cam_y; } view_stats;
view_stats view_get_stats(void);
extern float g_cam_k; /* development: camera variants (debug field) */

#endif
