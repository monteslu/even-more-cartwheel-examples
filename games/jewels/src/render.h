/* The picture: the tray, the stones and the effects, drawn by the game's own passes in the engine's render pipeline
 * (linear HDR; the engine's bloom, tone curve and grade follow), on GL and WebGPU alike.
 *
 *   "jw_tray"   OPAQUE 0   the backdrop, the walnut and gold frames, the velvet cushions, the panel and its button
 *                          (cached), with the lights of the moment (added up at a quarter of the canvas) over it
 *   "jw_gems"   OPAQUE 10  each stone's shadow and caustic on the velvet, then the stones, ray traced per pixel
 *                          through their facet planes (the lights of the moment among their lamps), then the shards
 *                          of the bursts, the shockwaves, the trails, sparks, dust, glows, rings, rays and links, the
 *                          cursor, the held jewel and the hint, then the star glints where facets flash a hot lamp
 *
 * The world: the tray is the plane z = 0, x right and y up, in canvas pixels about the canvas centre (a 1920 x 1080
 * canvas: a point on the tray at canvas (px, py) is world (px - 960, 540 - py, 0)). The camera looks down from
 * high above the board's centre, so the stones are seen nearly straight on, as a long lens shows them. */
#ifndef JEWELS_RENDER_H
#define JEWELS_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#define JW_MAX_GEMS 128
#define JW_MAX_BURSTS 96
#define JW_MAX_RINGS 24
#define JW_MAX_LIGHTS 48
#define JW_MAX_RAYS 8
#define JW_MAX_TRAILS 16

typedef struct jw_gem_draw {
  int kind;               /* 0..5 */
  float x, y;             /* canvas pixels: the stone's centre on the tray */
  float lift;             /* world units above its rest */
  float scale, opacity, glow;
  float spin, tilt_x, tilt_y; /* radians */
} jw_gem_draw;

typedef struct jw_burst_draw {
  int kind;
  float x, y, t;          /* where, and seconds since it broke */
  float power;            /* 1: a plain three; more for big groups and deep cascades */
  uint32_t seed;
  bool shower;            /* one of the climax's shower round the frame (it leaves no dust) */
} jw_burst_draw;

typedef struct jw_ring_draw { float x, y, t, power, r, g, b; } jw_ring_draw;
/* a light of the moment over the tray: a burst's, an afterglow's, the climax's. x, y canvas pixels, z its height
 * over the velvet; r, g, b its strength now (the envelope applied); reach the distance its light halves over; core the
 * radius of its hot centre (what the stones see as a small lamp: fire); stones 1 when the stones see it too (an
 * afterglow lies on the velvet and lights only the velvet: 0), 2 the stones only (the climax's sweep) */
typedef struct jw_light { float x, y, z, r, g, b, reach, core, stones; } jw_light;
/* a fan of light rays from a big clear: where, seconds since, power, colour */
typedef struct jw_rays { float x, y, t, power, r, g, b; uint32_t seed; } jw_rays;
typedef struct jw_trail { float x0, y0, x1, y1, t, r, g, b; } jw_trail; /* a streak of light linking two groups */

typedef struct jw_frame {
  double time;
  int ngems;
  jw_gem_draw gems[JW_MAX_GEMS];
  int nbursts;
  jw_burst_draw bursts[JW_MAX_BURSTS];
  int nrings;
  jw_ring_draw rings[JW_MAX_RINGS];
  int nlights;
  jw_light lights[JW_MAX_LIGHTS];
  int nrays;
  jw_rays rays[JW_MAX_RAYS];
  int ntrails;
  jw_trail trails[JW_MAX_TRAILS];
  bool cursor_on, sel_on, hint_on;
  float cursor_x, cursor_y, sel_x, sel_y; /* canvas pixels (cell centres) */
  int hint_x, hint_y, hint_dx, hint_dy;
  float hint_t;           /* seconds the hint has shown */
  float warmth;           /* 0..1: the room warms as a cascade goes on */
  float room;             /* 1: the room's light; it brightens a little with each step of a cascade */
  float sweep;            /* -1: none; else 0..1 across the board (the finale of a long cascade) */
  float button_glow, button_press; /* the on-screen hint button */
  float reshuffle;        /* 1 just after a reshuffle, fading */
  bool board_only;        /* the title and menus: the tray without the cursor */
} jw_frame;

/* the passes ask for the frame when they first need it (with the frame's interpolation) */
typedef void (*jw_frame_fn)(jw_frame *f, double alpha, void *user);

typedef struct jw_render_stats {
  int quality;            /* the effects detail the stones are drawn at (cw_quality) */
  int subsamples, bounces, wavelengths;
  int gems_drawn, shards_drawn, sparks_drawn, tray_draws;
  int lights, gem_lights, shocks; /* this frame: lights over the tray, light picks the stones took, rings that bend the picture */
  float gpu_tray, gpu_gems, gpu_glint; /* ms, -1 unknown */
} jw_render_stats;

void jw_render_init(jw_frame_fn fn, void *user);
const jw_render_stats *jw_render_get_stats(void);
/* the caustic and shadow each stone throws on the velvet: baked on the CPU in steps (call until it returns true) */
bool jw_render_bake_step(void);
/* the world camera, for the effects: canvas position of a world point */
void jw_world_to_canvas(float x, float y, float z, float *px, float *py);

#endif
