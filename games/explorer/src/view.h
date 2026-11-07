/* explorer: drawing the world (view.c) and its effects. Render code only: nothing here feeds the simulation. */
#ifndef EXPLORER_VIEW_H
#define EXPLORER_VIEW_H

#include "cartwheel/platform.h"
#include "cartwheel/render2d.h"
#include "sim.h"

/* the 2D renderer of cartwheel-engine v0.37 (HDR post chain, emission, soft shadows, GI, water, distortion): used when
 * the engine has it, else the game draws as before */
#if defined(CW_R2D_FEATURES) && CW_R2D_FEATURES >= 2
#define EXPLORER_R2D_V2 1
#endif

/* canvas layers: the sky and every parallax depth in one unlit layer (their haze baked into their colour), the lit
 * run (back wall, tiles, actors), effects, water, the framing silhouettes in front of everything, distortion */
enum { L_SKY, L_WALL, L_TILES, L_ACTORS, L_FX, L_WATER, L_FRAME, L_FRONT, L_HUD };

typedef struct view_stats { int sprites, lights, particles, enemies; float submit_ms, end_ms; } view_stats;

void view_init(void);
/* camera follows the players inside the room; snap: no easing (a teleport) */
void view_camera(const sim *s, float dt, bool snap);
void view_draw(const sim *s, float t);
void view_events(const sim *s);           /* particles and flashes from the tick's events */
void view_tick(const sim *s, float dt);   /* particles move */
view_stats view_get_stats(void);
cw_vec2 view_world_to_screen(cw_vec2 w); /* canvas units */
cw_vec2 view_canvas(void); /* the window pass's canvas size, valid before the first pass too */
float view_px_per_tile(void);
extern cw_camera2d g_cam;

#endif
