/* lanternfall: drawing the world (view.c) and its effects. Render code only: nothing here feeds the simulation. */
#ifndef LANTERNFALL_VIEW_H
#define LANTERNFALL_VIEW_H

#include "cartwheel/platform.h"
#include "cartwheel/debug.h"
#include "cartwheel/render2d.h"
#include "sim.h"

/* the 2D renderer of cartwheel-engine v0.37 (HDR post chain, emission, soft shadows, GI, water, distortion): used when
 * the engine has it, else the game draws as before */
#if defined(CW_R2D_FEATURES) && CW_R2D_FEATURES >= 2
#define LANTERNFALL_R2D_V2 1
#endif

/* canvas layers: the sky and every parallax depth in one unlit layer (their haze baked into their colour), the drops
 * through floors (unlit: their dusk keeps its colour), the lit run (back wall, tiles, actors), effects, water, the framing silhouettes in front of everything, distortion */
enum { L_SKY, L_DROP, L_WALL, L_TILES, L_ACTORS, L_FX, L_WATER, L_FRAME, L_FRONT, L_HUD };

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

/* the grounding gate's views (tests/lanternfall-grounding.mjs; main.c's lf_* debug commands): the camera held at a place
 * (clamped to the room as always), the players out of the frame, the sky layer drawn alone, and the backdrop's
 * geometry with every painted tree's, spire's, pier's and stack's foot in canvas pixels */
void view_lock_camera(bool lock, float x, float y);
void view_sky_only(bool on);
void view_par_only(int k); /* -2 every depth, -1 none, else that depth alone */
void view_hide_players(bool on);
void view_par_report(const sim *s, cw_reply *r);

#endif
