/* pinball_physcheck: is the engine's physics good enough for a pinball ball, or does the game need its own solver?
 * The same shots on four solvers, at 60 ticks a second:
 *   game    the game's ball solver (src/phys.c: substeps, conservative advancement, swept flipper contact)
 *   box2d   the engine's 2D physics (Box2D v3.2): the ball a bullet circle, the wall a segment, the flipper a
 *           kinematic capsule turned by its angular velocity; 4 substeps, continuous collision on
 *   box3d   the engine's 3D physics (Box3D): the ball a bullet sphere, the wall a 1 mm box, the flipper a kinematic
 *           capsule; 4 substeps, continuous collision on
 *   box2d_480hz, box3d_480hz  the same worlds stepped 8 times a tick (what a game would try next)
 *   naive   the control that must fail: one discrete step a tick, overlaps pushed out after the move
 * Shots:
 *   wall     the ball at 3 to 40 m/s, seven angles, five offsets, at a zero-thickness wall (box3d: 1 mm): did its centre
 *            ever cross the wall's line inside the wall?
 *   flipper  the ball dropped at 3 to 10 m/s from nine directions onto a flipper that fires as it arrives (40 rad/s):
 *            did the ball ever reach the flipper's underside?
 *   cradle   the ball resting on the flipper at 1 to 7 cm from the pivot, the flipper fired: did it pass through, and
 *            how fast did it leave? A rigid flipper turning at w gives a ball at rest (1 + e) w d on its first contact (a
 *            second contact as the flipper catches up only adds to it): the shot's speed should
 *            follow where the ball sat, which is what lets a player aim.
 * Prints one JSON object; tests/pinball-physics.mjs judges it. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/phys.h"
#include "cartwheel/math.h"
#include "cartwheel/physics2d.h"
#include "cartwheel/physics3d.h"

#define DT (1.0f / 60.0f)
#define FL_LEN 0.07f
#define FL_R 0.009f
#define FL_REST (-0.5236f)
#define FL_UP (0.4363f)
#define FL_W 40.0f
#define E_REST 0.5f

enum { S_GAME, S_BOX2D, S_BOX3D, S_BOX2D_8, S_BOX3D_8, S_NAIVE, S_COUNT };
static const char *const NAME[S_COUNT] = { "game", "box2d", "box3d", "box2d_480hz", "box3d_480hz", "naive" };
static bool is2d(int k) { return k == S_BOX2D || k == S_BOX2D_8; }
static bool is3d(int k) { return k == S_BOX3D || k == S_BOX3D_8; }

/* a solver behind one interface: a ball, optionally a wall or a flipper */
typedef struct sim {
  int kind;
  pb_world pw;           /* game and naive */
  cw_world2d w2; cw_body2d b2, f2;
  cw_world3d w3; cw_body3d b3, f3;
  float fang;            /* the flipper's angle (all solvers turn it the same way) */
  bool has_flipper, firing;
  float bx, by, bvx, bvy;
} sim;

static float flip_profile_step(float ang) { /* the next tick's angle: constant FL_W until up */
  float a = ang + FL_W * DT;
  return a > FL_UP ? FL_UP : a;
}

static void sim_make(sim *s, int kind, bool wall, bool flipper, float x, float y, float vx, float vy, float g) {
  memset(s, 0, sizeof *s);
  s->kind = kind, s->has_flipper = flipper, s->fang = FL_REST;
  if (kind == S_GAME || kind == S_NAIVE) {
    pb_init(&s->pw);
    s->pw.g = g;
    if (wall) pb_add_seg(&s->pw, 0, -0.1f, 0, 0.1f, 0, E_REST, 0.3f, 0, -1);
    if (flipper) {
      int f = pb_add_flipper(&s->pw, 0, 0, FL_LEN, FL_R, FL_R, FL_REST, FL_UP, 0);
      s->pw.fl[f].e = E_REST, s->pw.fl[f].mu = 0.3f;
      s->pw.fl[f].wmax = FL_W, s->pw.fl[f].alpha_up = 1e9f; /* the same constant-speed swing as the others */
    }
    pb_ball_add(&s->pw, x, y, vx, vy);
  } else if (is2d(kind)) {
    cw_world2d_def wd = cw_world2d_default_def();
    wd.gravity = (cw_vec2){ 0, -g };
    wd.restitution_threshold = 0.1f;
    s->w2 = cw_world2d_create(&wd);
    cw_shape2d_def sd = cw_shape2d_default_def();
    sd.material.restitution = E_REST, sd.material.friction = 0.3f;
    if (wall) {
      cw_body2d_def bd = cw_body2d_default_def();
      bd.type = CW_BODY_STATIC;
      cw_body2d wb = cw_body2d_create(s->w2, &bd);
      cw_shape2d_segment(wb, &sd, (cw_vec2){ 0, -0.1f }, (cw_vec2){ 0, 0.1f });
    }
    if (flipper) {
      cw_body2d_def bd = cw_body2d_default_def();
      bd.type = CW_BODY_KINEMATIC;
      bd.rotation = cw_rot2_from_angle(FL_REST);
      s->f2 = cw_body2d_create(s->w2, &bd);
      cw_shape2d_capsule(s->f2, &sd, (cw_vec2){ 0, 0 }, (cw_vec2){ FL_LEN, 0 }, FL_R);
    }
    cw_body2d_def bd = cw_body2d_default_def();
    bd.position = (cw_pos2){ x, y };
    bd.linear_velocity = (cw_vec2){ vx, vy };
    bd.bullet = true;
    bd.sleep = false;
    s->b2 = cw_body2d_create(s->w2, &bd);
    sd.density = 1;
    cw_shape2d_circle(s->b2, &sd, (cw_vec2){ 0, 0 }, PB_R);
  } else {
    cw_world3d_def wd = cw_world3d_default_def();
    wd.gravity = (t3_vec3){ 0, -g, 0 };
    wd.restitution_threshold = 0.1f;
    s->w3 = cw_world3d_create(&wd);
    cw_shape3d_def sd = cw_shape3d_default_def();
    sd.material.restitution = E_REST, sd.material.friction = 0.3f;
    if (wall) {
      cw_body3d_def bd = cw_body3d_default_def();
      bd.type = CW_BODY_STATIC;
      cw_body3d wb = cw_body3d_create(s->w3, &bd);
      cw_shape3d_box(wb, &sd, (t3_vec3){ 0.0005f, 0.1f, 0.05f }, cw_xform3_identity());
    }
    if (flipper) {
      cw_body3d_def bd = cw_body3d_default_def();
      bd.type = CW_BODY_KINEMATIC;
      bd.rotation = (t3_quat){ 0, 0, cw_sin(FL_REST * 0.5f), cw_cos(FL_REST * 0.5f) };
      s->f3 = cw_body3d_create(s->w3, &bd);
      cw_shape3d_capsule(s->f3, &sd, (t3_vec3){ 0, 0, 0 }, (t3_vec3){ FL_LEN, 0, 0 }, FL_R);
    }
    cw_body3d_def bd = cw_body3d_default_def();
    bd.position = (t3_vec3){ x, y, 0 };
    bd.linear_velocity = (t3_vec3){ vx, vy, 0 };
    bd.bullet = true;
    bd.sleep = false;
    s->b3 = cw_body3d_create(s->w3, &bd);
    sd.density = 1000;
    cw_shape3d_sphere(s->b3, &sd, (t3_vec3){ 0, 0, 0 }, PB_R);
  }
  s->bx = x, s->by = y, s->bvx = vx, s->bvy = vy;
}

static void sim_free(sim *s) {
  if (is2d(s->kind)) cw_world2d_destroy(s->w2);
  if (is3d(s->kind)) cw_world3d_destroy(s->w3);
}

/* the naive control: move, then push out of whatever it overlaps */
static void naive_step(pb_world *w, float dt) {
  pb_ball *b = &w->b[0];
  b->vy -= w->g * dt;
  for (int f = 0; f < w->nfl; f++) {
    pb_flipper *F = &w->fl[f];
    float a0 = F->ang;
    if (F->held) F->ang = flip_profile_step(F->ang);
    F->omega = (F->ang - a0) / dt;
  }
  b->x += b->vx * dt, b->y += b->vy * dt;
  for (int i = 0; i < w->nsh; i++) {
    float nx, ny, d = pb_dist_shape(&w->sh[i], b->x, b->y, &nx, &ny) - PB_R;
    if (d >= 0) continue;
    b->x += nx * -d, b->y += ny * -d;
    float vn = b->vx * nx + b->vy * ny;
    if (vn < 0) b->vx -= (1 + E_REST) * vn * nx, b->vy -= (1 + E_REST) * vn * ny;
  }
  for (int f = 0; f < w->nfl; f++) {
    pb_flipper *F = &w->fl[f];
    float nx, ny, d = pb_dist_flipper(F, F->ang, b->x, b->y, &nx, &ny) - PB_R;
    if (d >= 0) continue;
    b->x += nx * -d, b->y += ny * -d;
    float cx = b->x - nx * PB_R, cy = b->y - ny * PB_R, vsx = -F->omega * cy, vsy = F->omega * cx;
    float vn = (b->vx - vsx) * nx + (b->vy - vsy) * ny;
    if (vn < 0) b->vx -= (1 + E_REST) * vn * nx, b->vy -= (1 + E_REST) * vn * ny;
  }
}

static void sim_tick(sim *s) {
  if (s->kind == S_GAME) {
    if (s->has_flipper) s->pw.fl[0].held = s->firing;
    pb_step(&s->pw, DT);
    s->fang = s->pw.fl[0].ang;
    s->bx = s->pw.b[0].x, s->by = s->pw.b[0].y, s->bvx = s->pw.b[0].vx, s->bvy = s->pw.b[0].vy;
    return;
  }
  if (s->kind == S_NAIVE) {
    if (s->has_flipper) s->pw.fl[0].held = s->firing;
    naive_step(&s->pw, DT);
    s->fang = s->pw.fl[0].ang;
    s->bx = s->pw.b[0].x, s->by = s->pw.b[0].y, s->bvx = s->pw.b[0].vx, s->bvy = s->pw.b[0].vy;
    return;
  }
  float next = s->has_flipper && s->firing ? flip_profile_step(s->fang) : s->fang, w = (next - s->fang) / DT;
  int sub = s->kind == S_BOX2D_8 || s->kind == S_BOX3D_8 ? 8 : 1; /* the engine's world stepped 8 times a tick */
  if (is2d(s->kind)) {
    if (s->has_flipper) cw_body2d_set_angular_velocity(s->f2, w);
    for (int i = 0; i < sub; i++) cw_world2d_step(s->w2, DT / (float)sub);
    cw_xform2 x = cw_body2d_transform(s->b2);
    cw_vec2 v = cw_body2d_velocity(s->b2);
    s->bx = x.p.x, s->by = x.p.y, s->bvx = v.x, s->bvy = v.y;
    if (s->has_flipper) s->fang = cw_rot2_angle(cw_body2d_transform(s->f2).q);
  } else {
    if (s->has_flipper) cw_body3d_set_angular_velocity(s->f3, (t3_vec3){ 0, 0, w });
    for (int i = 0; i < sub; i++) cw_world3d_step(s->w3, DT / (float)sub);
    cw_xform3 x = cw_body3d_transform(s->b3);
    t3_vec3 v = cw_body3d_velocity(s->b3);
    s->bx = x.p.x, s->by = x.p.y, s->bvx = v.x, s->bvy = v.y;
    if (s->has_flipper) {
      t3_quat q = cw_body3d_transform(s->f3).q;
      s->fang = 2 * cw_atan2(q.z, q.w);
    }
  }
}

/* the ball's centre in the flipper's frame: along, across */
static void flip_local(const sim *s, float *lx, float *ly) {
  float c = cw_cos(s->fang), sn = cw_sin(s->fang);
  *lx = c * s->bx + sn * s->by, *ly = -sn * s->bx + c * s->by;
}

int main(int argc, char **argv) {
  (void)argc, (void)argv;
  static const float SPEEDS[] = { 3, 6, 10, 20, 40 };
  int wall_n[S_COUNT] = { 0 }, wall_t[S_COUNT] = { 0 }, flip_n[S_COUNT] = { 0 }, flip_t[S_COUNT] = { 0 }, cr_t[S_COUNT] = { 0 };
  float cr_speed[S_COUNT][7], cr_expect[7];
  float game_pen = 0;
  for (int k = 0; k < S_COUNT; k++) {
    /* the wall */
    for (int si = 0; si < 5; si++)
      for (int ai = -3; ai <= 3; ai++)
        for (int yi = -2; yi <= 2; yi++) {
          float sp = SPEEDS[si], a = (float)ai * 15.0f * CW_PI / 180.0f, y0 = (float)yi * 0.02f;
          sim s;
          sim_make(&s, k, true, false, -0.06f, y0, sp * cw_cos(a), sp * cw_sin(a), 0);
          bool tunnel = false;
          float px = s.bx, py = s.by;
          for (int t = 0; t < 30 && !tunnel; t++) {
            sim_tick(&s);
            if (k == S_GAME) game_pen = fmaxf(game_pen, s.pw.max_pen);
            /* the centre's path this tick crossed x = 0 inside the wall? */
            if ((px < 0) != (s.bx < 0)) {
              float u = px / (px - s.bx), yc = py + (s.by - py) * u;
              if (fabsf(yc) < 0.08f) tunnel = true;
            }
            px = s.bx, py = s.by;
          }
          wall_n[k]++, wall_t[k] += tunnel;
          sim_free(&s);
        }
    /* the flipper, fired as a fast ball arrives */
    for (int si = 0; si < 3; si++)
      for (int ai = 0; ai < 9; ai++)
        for (int di = 0; di < 3; di++) {
          float sp = SPEEDS[si];
          float d = 0.015f + 0.02f * (float)di;                     /* where on the flipper it aims */
          float tx = d * cw_cos(-0.15f), ty = d * cw_sin(-0.15f);    /* a point on the flipper's line, mid-swing */
          float dir = (-60.0f + 15.0f * (float)ai) * CW_PI / 180.0f;  /* from above, -60 to +60 degrees off straight down */
          float vx = sp * cw_sin(dir), vy = -sp * cw_cos(dir);
          float lead = 0.75f * DT * sp;                              /* it arrives about as the flipper sweeps through */
          float x = tx - vx / sp * (lead + 0.03f), y = ty - vy / sp * (lead + 0.03f);
          sim s;
          sim_make(&s, k, false, true, x, y, vx, vy, 1.25f);
          s.firing = true;
          bool tunnel = false;
          for (int t = 0; t < 20 && !tunnel; t++) {
            sim_tick(&s);
            if (k == S_GAME) game_pen = fmaxf(game_pen, s.pw.max_pen);
            float lx, ly;
            flip_local(&s, &lx, &ly);
            if (lx > 0 && lx < FL_LEN && ly < 0) tunnel = true; /* on the underside, inside the flipper's length */
          }
          flip_n[k]++, flip_t[k] += tunnel;
          sim_free(&s);
        }
    /* the cradle: resting on the flipper at d, fired */
    for (int di = 0; di < 7; di++) {
      float d = 0.01f * (float)(di + 1);
      float c = cw_cos(FL_REST), sn = cw_sin(FL_REST), off = FL_R + PB_R + 0.0002f;
      float x = c * d - sn * off, y = sn * d + c * off;
      sim s;
      sim_make(&s, k, false, true, x, y, 0, 0, 0); /* no gravity: the shot alone */
      s.firing = true;
      bool tunnel = false;
      for (int t = 0; t < 8; t++) {
        sim_tick(&s);
        float lx, ly;
        flip_local(&s, &lx, &ly);
        if (lx > 0 && lx < FL_LEN && ly < 0) tunnel = true;
      }
      cr_t[k] += tunnel;
      cr_speed[k][di] = sqrtf(s.bvx * s.bvx + s.bvy * s.bvy);
      cr_expect[di] = (1 + E_REST) * FL_W * d; /* the first contact alone: the normal part of the surface speed under the ball */
      sim_free(&s);
    }
  }
  printf("{\"dt\":%.6f,\"flipper_w\":%.1f,\"restitution\":%.2f,\"game_max_overlap_mm\":%.4f,\"solvers\":{", DT, FL_W, E_REST, game_pen * 1000);
  for (int k = 0; k < S_COUNT; k++) {
    printf("%s\"%s\":{\"wall\":[%d,%d],\"flipper\":[%d,%d],\"cradle_tunnels\":%d,\"cradle_speed\":[", k ? "," : "", NAME[k], wall_t[k], wall_n[k], flip_t[k], flip_n[k], cr_t[k]);
    for (int d = 0; d < 7; d++) printf("%s%.3f", d ? "," : "", cr_speed[k][d]);
    printf("]}");
  }
  printf("},\"cradle_expect\":[");
  for (int d = 0; d < 7; d++) printf("%s%.3f", d ? "," : "", cr_expect[d]);
  printf("]}\n");
  return 0;
}
