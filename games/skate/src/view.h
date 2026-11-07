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
extern const float MAT_TILE_M[MAT_KINDS];
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
void view_rigs(const int skin[MAXP], int players);        /* each seat's rider and board */
/* the frame: rigs, boards, flips, cameras, the token rings; views: how many split views */
void view_frame(const sim *S, const skater prev[MAXP], float alpha, int players, float dt);
void view_title_camera(const park *k, float t);           /* behind the menus: a slow orbit of the park */
cw_node view_camera(int i);
bool view_world_to_screen(int view, v3 p, float *sx, float *sy); /* 0..1 in that view's rect */
typedef struct view_stats { int draws, tris; } view_stats;
extern int g_view_shadow_size;
/* what the frames show, measured (debug fields: the feel gate) */
typedef struct view_metrics { float cam_turn_max, cam_dist, foot_err_max; } view_metrics;
extern view_metrics g_view_metrics;
extern float g_cam_k, g_cam_side;

/* ── pose.c: the rider's body from the board's state ───────────────────── */
typedef struct rider_pose {
  float crouch, tuck, lean, twist, push_phase, push_w, manual, grind_w, bal, grab_w, flip_w, look;
  v3 hand_l, hand_r; /* grab targets (model space of the rider) */
  int grab;
  bool fakie, bail;
} rider_pose;
typedef struct rig {
  bool bound;
  cw_pose pose;
  int hips, spine, chest, neck, head, ua_l, la_l, h_l, ua_r, la_r, h_r, ul_l, ll_l, f_l, ul_r, ll_r, f_r;
  t3_vec3 foot_rest_l, foot_rest_r, hip_rest;
  float leg_len, last_yaw, foot_err;
  rider_pose s; /* smoothed */
} rig;
bool rig_bind(rig *r, t3_object *model);
void rig_pose(rig *r, const skater *k, float dt, float t);
/* where the board sits in the rider's model space for a grab (how high the knees pulled it) */
float rig_board_lift(const rig *r);

/* ── rag.c: wipeouts as ragdolls (presentation only) ───────────────────── */
void rag_park(const park *k);   /* the ragdoll world: the park's triangles as one static mesh */
void rag_free(void);
/* each tick: a bail starts a ragdoll from the rider's pose, a respawn ends it */
void rag_tick(int i, const skater *s, rig *r, cw_node model_node, const t3_mat4 *model_to_world);
bool rag_active(int i);
v3 rag_hips(int i);
int rag_live(void);

/* the deck's top over the ground (the rider stands on it) */
#define DECK_Y 0.105f
void rag_step(void); /* one step of the ragdolls' world (a tick) */

/* ── hud.c ─────────────────────────────────────────────────────────────── */
typedef struct ui_state { int mode, park, row, skin[MAXP], best; } ui_state;
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
