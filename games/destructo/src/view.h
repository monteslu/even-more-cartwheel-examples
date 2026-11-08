/* destructo's view: the field, the structure's blocks, the launcher, the shot and the camera (render only) */
#ifndef DESTRUCTO_VIEW_H
#define DESTRUCTO_VIEW_H
#include "cartwheel/scene.h"
#include "game.h"

void view_init(cw_node root);
void view_level(const game *g);                 /* (re)build the scenery for g's level */
void view_sync(const game *g, float alpha);      /* every frame: blocks, launcher, shot, aim path */
/* every frame; overview: the player holds the wide view */
void view_camera(const game *g, cw_node camera, float aspect, float dt, bool overview, float alpha);
/* every frame of a replay: the shot followed, then a slow low turn round the impact (slow: the slow motion; cut: its first frame) */
void view_camera_replay(const game *g, cw_node camera, float aspect, float dt, bool slow, bool cut, float alpha);
bool view_screen(t3_vec3 p, float *u, float *v); /* a world point on screen (0..1), false behind the camera */

typedef struct view_stats { int instances, lights; float cam_dist; } view_stats;
view_stats view_get_stats(void);
extern bool g_view_no_path; /* key-art captures: no aim path */

#endif
