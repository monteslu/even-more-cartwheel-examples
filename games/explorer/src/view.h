/* explorer: drawing the world (view.c) and its effects. Render code only: nothing here feeds the simulation. */
#ifndef EXPLORER_VIEW_H
#define EXPLORER_VIEW_H

#include "cartwheel/render2d.h"
#include "sim.h"

/* the 2D renderer of cartwheel-engine v0.37 (HDR post chain, emission, soft shadows, GI, water, distortion): used when
 * the engine has it, else the game draws as before */
#if defined(CW_R2D_FEATURES) && CW_R2D_FEATURES >= 2
#define EXPLORER_R2D_V2 1
#endif

enum { L_SKY, L_WALL = 5, L_TILES, L_ACTORS, L_FX, L_WATER, L_FRONT, L_HUD };

typedef struct view_stats { int sprites, lights, particles, enemies; float submit_ms, end_ms; } view_stats;

void view_init(void);
/* camera follows the players inside the room; snap: no easing (a teleport) */
void view_camera(const sim *s, float dt, bool snap);
void view_draw(const sim *s, float t);
void view_events(const sim *s);           /* particles and flashes from the tick's events */
void view_tick(const sim *s, float dt);   /* particles move */
view_stats view_get_stats(void);
cw_vec2 view_world_to_screen(cw_vec2 w); /* canvas units */
float view_px_per_tile(void);
extern cw_camera2d g_cam;

#endif
