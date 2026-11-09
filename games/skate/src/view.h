/* skate: the presentation's shared declarations (view.c, pose.c, rag.c, hud.c, audio.c, gfx.c, main.c). Nothing here
 * feeds the simulation. */
#ifndef SKATE_VIEW_H
#define SKATE_VIEW_H

#include "game.h"
#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/ik.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene.h"

/* ── gfx.c ─────────────────────────────────────────────────────────────── */
typedef struct tex_set { t3_texture *map, *normal, *rough, *ao; } tex_set;
bool tex_warm(int kind); /* a band of rows of texture set `kind`; true when it is made */
const tex_set *tex_get(int kind);
void tex_apply(t3_material *m, int kind, float normal_scale, float ao_intensity);
#define DITHER_K 0.03f /* the sky dither's depth: its mean is 1 - DITHER_K, its spread twice that (a pixel blends a few texels: about half that) */
t3_texture *tex_dither(void); /* the sky's dither noise (gfx.c) */
/* the scenery's own texture sets, after the park's (scenery.c) */
enum { TEX_SAND = MAT_KINDS, TEX_FACADE, TEX_FACADE_LIT, TEX_CURTAIN, TEX_CORRUGATED, TEX_LUMBER, TEX_GROUND, TEX_WATER, TEX_SKYLINE, TEX_DECALS, TEX_FOLIAGE, TEX_PINECARD, TEX_CLOUD, TEX_KINDS };
extern const float MAT_TILE_M[TEX_KINDS];
extern uint32_t tex_hash;
t3_color lin(float r, float g, float b);
typedef struct mesh_build {
  float *pos, *nor, *uv, *col;
  uint32_t *idx;
  int nv, ni, cap_v, cap_i;
  bool colors;
} mesh_build;
int mb_vert(mesh_build *b, t3_vec3 p, t3_vec3 n, float u, float v);
void mb_color(mesh_build *b, int v, float r, float g, float bl);
void mb_tri(mesh_build *b, int a, int c, int d);
void mb_quad(mesh_build *b, int a, int c, int d, int e);
void mb_free(mesh_build *b);
t3_geometry *mb_finish(mesh_build *b);

/* ── the skaters' looks ────────────────────────────────────────────────── */
#define NSKINS 4
extern const char *const SKIN_NAMES[NSKINS];
extern const char *const SKIN_FILES[NSKINS];
extern const float PLAYER_COL[MAXP][3]; /* each seat's colour (sRGB): its HUD and its board's graphic */

/* a skater's presentation state, smoothed between ticks (main.c keeps the previous tick) */
typedef struct skview {
  v3 p, f, u;         /* interpolated board contact point, nose, up */
  float speed;
} skview;

/* ── view.c ────────────────────────────────────────────────────────────── */
bool view_init_step(cw_node root, int *done, int *total); /* builds the scene a step at a time (loading screen) */
void view_park(const park *k);                            /* the park's meshes, its tokens (frees the last park's) */
bool view_bake(const char *dir, int park_index);              /* native development: SKATE_BAKE */
void view_rigs(const int skin[MAXP], int players);        /* each seat's rider and board */
/* the frame: rigs, boards, flips, cameras, the token rings; views: how many split views */
void view_frame(const sim *S, const skater prev[MAXP], float alpha, int players, float dt);
void view_title_camera(const park *k, float t);           /* behind the menus: a slow orbit of the park */
cw_node view_camera(int i);
extern int g_view_shadow_size;
/* what the frames show, measured (debug fields: the feel gate) */
typedef struct view_metrics { float cam_turn_max, cam_dist, foot_err_max, foot_place_max; int32_t foot_err_state, foot_place_state; } view_metrics;
extern view_metrics g_view_metrics;
extern float g_cam_k, g_cam_side;

/* ── scenery.c: each park's setting to the horizon, and its light ──────── */
typedef struct scenery_mood {
  float time, north, sun_energy, sky_energy, mie, rayleigh, exposure, hemi; /* the ProceduralSky, the exposure, the hemisphere fill */
  float zenith[3], horizon[3], cloud[3], fog[3]; /* the sky dome's colours, the clouds', the haze's (sRGB) */
  float fog_density;
  float ground_near[3], ground_far[3], rock[3], water[3];
  float shadow_soft; /* 1: a hazy day's softer, lighter shadows */
  float glow, windows_lit, lm_gain; /* lm_gain: the bake's light, scaled (a low sun's sky is dim) */

} scenery_mood;
const scenery_mood *scenery_mood_of(int park);
void scenery_build(cw_node parent, const park *k, int park_index);

/* ── pose.c: the rider's body from the board's state ───────────────────── */
typedef struct rider_pose {
  /* eased toward what the board does */
  float knee, hx, hz, open, pitch, chest, look, head_pitch;     /* the body: knee bend, the hips, the turn toward the nose, the lean */
  float front_x, front_z, front_yaw, back_x, back_z, back_yaw;  /* the feet on the deck, in the board's frame */
  float arm_out, arm_fwd, arm_back, elbow;
  float push_phase, push_w, manual, grind_w, bal, grab_w, flip_w, lean, tuck, air_w, drop_air, support;
  float board_pitch, tail_drop;                                 /* the ollie's pop, on the board */
  int grab;
} rider_pose;
#define RIG_MAXB 96
typedef struct rig {
  bool bound;
  cw_pose pose;
  int hips, spine, chest, neck, head, ua_l, la_l, h_l, ua_r, la_r, h_r, ul_l, ll_l, f_l, ul_r, ll_r, f_r;
  t3_vec3 foot_rest_l, foot_rest_r, hip_rest;
  t3_quat foot_rot_l, foot_rot_r; /* the feet's rest rotations (model space) */
  float leg_len, thigh, shin, ankle_h, last_yaw, foot_err;
  t3_vec3 last_u, air_v;
  int last_st;
  bool popped, from_grind;
  float air_time, land_time, impact;
  float free_l, free_r; /* a foot lifted off the deck on purpose (a flip's flick) */
  /* the board under the rider (root space): as drawn (with a flip's turn), and the deck the feet stand on */
  t3_quat board_q, deck_q;
  t3_vec3 board_p, deck_p;
  rider_pose s; /* smoothed */
  /* goofy: the right foot forward. The body is posed as a regular rider and then mirrored (pose.c rig_mirror): each
     bone takes its partner's turn from rest reflected left to right, and the whole turned to face the heel edge */
  bool goofy;
  int nb;
  int mate[RIG_MAXB];         /* each bone's partner on the other side (itself on the middle line) */
  t3_quat rest_mrot[RIG_MAXB]; /* the rest pose, model space */
  t3_vec3 rest_mpos[RIG_MAXB];
  /* a run-out (a near miss landed on foot): the stride's phase */
  float run_ph;
} rig;
bool rig_bind(rig *r, t3_object *model);
/* the body, and the board's placement under it (board_q, board_p), from the board's state */
void rig_pose(rig *r, const skater *k, float dt, float t);
/* how far a foot's sole is outside where a skater's foot goes on the deck (metres, 0 inside); *which 1 front, 2 back */
float rig_foot_place(const rig *r, int *which);
float deck_top(float x); /* the deck's top over the trucks' plane at x along it */
/* the deck's kicktails: how far its middle line rises at x along it (metres; 0 between the trucks), and the slope there */
float deck_rise(float x, float *slope);

/* ── rag.c: wipeouts as ragdolls (presentation only) ───────────────────── */
void rag_park(const park *k);   /* the ragdoll world: the park's triangles as one static mesh */
void rag_free(void);
/* each tick: a bail starts a ragdoll from the rider's pose, a respawn ends it */
void rag_tick(int i, const skater *s, rig *r, cw_node model_node, const t3_mat4 *model_to_world);
bool rag_active(int i);
/* where a live ragdoll's hips are (the camera follows the body, not the point the rules slide to a stop) */
bool rag_hips(int i, v3 *out);
int rag_live(void);

/* the deck's top over the ground (the rider stands on it), and the board's measures (view.c builds it, pose.c stands
   on it): a street deck 81 cm long and 20.3 cm wide, the axles 42 cm apart, wheels 53 mm across */
#define DECK_Y 0.105f
#define BOARD_AXLE_X 0.21f
#define BOARD_WHEEL_R 0.0265f
void rag_step(void); /* one step of the ragdolls' world (a tick) */

/* ── hud.c ─────────────────────────────────────────────────────────────── */
/* the setup's rows: the mode, the park, the first player's skater and stance (the others pick theirs on their own pads) */
enum { ROW_MODE, ROW_PARK, ROW_SKATER, ROW_STANCE, ROWS };
typedef struct ui_state { int mode, park, row, skin[MAXP], best; bool goofy[MAXP]; } ui_state;
extern bool g_view_goofy[MAXP]; /* each seat's stance as drawn: true right foot forward (main.c sets it) */
void hud_setup(const ui_state *ui, int players);
void hud_results(const sim *S, int players, int best, bool new_best);
void audio_music(bool on);
void hud_init(cw_font font);
void hud_draw(const sim *S, int players, int phase, float t);
void hud_popup(int player, const char *text, int points, int kind); /* a landed trick's line */

/* ── audio.c ───────────────────────────────────────────────────────────── */
void audio_init(void);
void audio_events(const sim *S, int players);
void audio_roll(const sim *S, int players, bool playing); /* the wheels' rumble and the grind's scrape */
void audio_menu(int which); /* 0 move, 1 select, 2 back */

#endif
