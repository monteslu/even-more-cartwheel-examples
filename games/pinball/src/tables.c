/* pinball's tables: each built in code, physics and drawing from the same data.
 *
 * Table frame: metres, x across (0 is the cabinet's middle), y up the table (0 at the bottom edge), z up off the
 * playfield. The playfield runs from x -0.255 to 0.255 and y 0 to 1.075 (a 0.51 x 1.07 m playfield), the shooter lane
 * on the right (x 0.222 to 0.255), the ball 27 mm across. Every table shares the lower third (flippers, slingshots,
 * inlanes, outlanes, the shooter lane, the top arc) and has its own upper playfield, theme and rules. */
#include <math.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

static table *T;
#define CX0 (-0.0165f) /* the middle of the playfield left of the shooter lane */

/* ── the builder ─────────────────────────────────────────────────────── */

static const float MAT_E[MAT_COUNT] = { 0.45f, 0.4f, 0.45f, 0.78f, 0.78f, 0.35f };
static const float MAT_MU[MAT_COUNT] = { 0.2f, 0.12f, 0.18f, 0.35f, 0.35f, 0.08f };

static int mk_wall(const float (*p)[2], int n, float r, float h, int mat, bool closed) {
  if (T->nwall >= MAXWALL || n < 2) return -1;
  wall *w = &T->walls[T->nwall];
  memset(w, 0, sizeof *w);
  w->n = n > 24 ? 24 : n;
  for (int i = 0; i < w->n; i++) w->p[i][0] = p[i][0], w->p[i][1] = p[i][1];
  w->r = r, w->h = h, w->mat = mat, w->closed = closed, w->col = -1;
  int segs = closed ? w->n : w->n - 1;
  for (int i = 0; i < segs; i++) {
    int j = (i + 1) % w->n;
    pb_add_seg(&T->W, w->p[i][0], w->p[i][1], w->p[j][0], w->p[j][1], r, MAT_E[mat], MAT_MU[mat], 0, -1);
  }
  return T->nwall++;
}
#define WALL(mat, r, h, ...) do { static const float pts_[][2] = { __VA_ARGS__ }; mk_wall(pts_, (int)(sizeof pts_ / sizeof pts_[0]), r, h, mat, false); } while (0)

/* an arc of wall: centre, radius, from a0 to a1 (degrees), segs pieces */
static int arc_wall(float cx, float cy, float rad, float a0, float a1, int segs, float r, float h, int mat) {
  float p[24][2];
  if (segs > 23) segs = 23;
  for (int i = 0; i <= segs; i++) {
    float a = (a0 + (a1 - a0) * (float)i / (float)segs) * CW_PI / 180.0f;
    p[i][0] = cx + rad * cw_cos(a), p[i][1] = cy + rad * cw_sin(a);
  }
  return mk_wall((const float (*)[2])p, segs + 1, r, h, mat, false);
}

static int mk_elem(int kind, float x, float y, float ang, float w, int col) {
  if (T->nel >= MAXEL) return -1;
  elem *e = &T->el[T->nel];
  memset(e, 0, sizeof *e);
  *e = (elem){ .kind = kind, .x = x, .y = y, .ang = ang, .w = w, .col = col, .shape = T->W.nsh, .nshape = 0, .lamp = -1, .group = -1, .path = -1, .shot = -1 };
  return T->nel++;
}
static void own(int el, int shape) { /* a shape the element just added */
  if (shape < 0) return;
  T->W.sh[shape].tag = (int16_t)el;
  T->el[el].nshape++;
}

static int mk_lamp(float x, float y, float size, float ang, int shape, int col) {
  if (T->nlamp >= MAXLAMP) return -1;
  T->lamps[T->nlamp] = (lamp){ .x = x, .y = y, .size = size, .ang = ang, .shape = shape, .col = col };
  return T->nlamp++;
}

static int post(float x, float y, float r, int mat) {
  int el = mk_elem(EK_POST, x, y, 0, r, mat == MAT_RUBBER_WHITE ? 1 : 0);
  own(el, pb_add_circle(&T->W, x, y, r, MAT_E[mat], MAT_MU[mat], 0, el));
  return el;
}

static int pop(float x, float y, int col) {
  int el = mk_elem(EK_POP, x, y, 0, 0.026f, col);
  int s = pb_add_circle(&T->W, x, y, 0.024f, 0.55f, 0.1f, SF_REPORT, el);
  T->W.sh[s].kick = 1.5f;
  T->W.sh[s].kick_min = 0.0001f; /* the skirt fires on any touch */
  own(el, s);
  if (T->npop < 4) T->pops[T->npop++] = el;
  return el;
}

/* a standup target: a face from (x, y) facing ang (degrees: the way a ball hitting it comes from) */
static int target(float x, float y, float ang, float w, int col) {
  float a = ang * CW_PI / 180.0f, tx = -cw_sin(a) * w * 0.5f, ty = cw_cos(a) * w * 0.5f;
  int el = mk_elem(EK_TARGET, x, y, ang, w, col);
  own(el, pb_add_seg(&T->W, x - tx, y - ty, x + tx, y + ty, 0.004f, 0.35f, 0.2f, SF_REPORT, el));
  T->el[el].lamp = mk_lamp(x + cw_cos(a) * 0.028f, y + cw_sin(a) * 0.028f, 0.009f, 0, LS_CIRCLE, col);
  return el;
}

/* a bank of n drop targets from (x0, y0) to (x1, y1), facing the side the ball comes from (left of x0->x1) */
static int drop_bank(float x0, float y0, float x1, float y1, int n, int col, bool back_wall) {
  float dx = (x1 - x0) / (float)n, dy = (y1 - y0) / (float)n, l = sqrtf(dx * dx + dy * dy);
  float nx = -dy / l, ny = dx / l, ang = cw_atan2(ny, nx) * 180.0f / CW_PI;
  int first = T->nel;
  for (int i = 0; i < n; i++) {
    float ax = x0 + dx * (float)i + dx * 0.08f, ay = y0 + dy * (float)i + dy * 0.08f;
    float bx = x0 + dx * (float)(i + 1) - dx * 0.08f, by = y0 + dy * (float)(i + 1) - dy * 0.08f;
    int el = mk_elem(EK_DROP, (ax + bx) * 0.5f, (ay + by) * 0.5f, ang, l * 0.84f, col);
    own(el, pb_add_seg(&T->W, ax, ay, bx, by, 0.004f, 0.3f, 0.2f, SF_REPORT, el));
    T->el[el].group = T->ndrop_banks;
    T->el[el].lamp = mk_lamp((ax + bx) * 0.5f + nx * 0.03f, (ay + by) * 0.5f + ny * 0.03f, 0.008f, 0, LS_CIRCLE, col);
  }
  if (back_wall) { /* behind the bank: what a ball through a gap meets */
    float p[2][2] = { { x0 - nx * 0.022f, y0 - ny * 0.022f }, { x1 - nx * 0.022f, y1 - ny * 0.022f } };
    mk_wall((const float (*)[2])p, 2, 0.004f, 0.03f, MAT_METAL, false);
  }
  if (T->ndrop_banks < 3) T->drop_banks[T->ndrop_banks][0] = first, T->drop_banks[T->ndrop_banks][1] = n, T->ndrop_banks++;
  return first;
}

/* a sensor line across a lane (a rollover switch, a spinner, an orbit's count) from a to b; dir: the counted way
 * (+1: crossing toward the left of a->b) */
static int sensor(int kind, float ax, float ay, float bx, float by, int dir, int col) {
  int el = mk_elem(kind, (ax + bx) * 0.5f, (ay + by) * 0.5f, cw_atan2(by - ay, bx - ax) * 180.0f / CW_PI, sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay)), col);
  own(el, pb_add_seg(&T->W, ax, ay, bx, by, 0, 0, 0, SF_SENSOR, el));
  T->el[el].dir = dir;
  return el;
}

/* a hole that holds the ball (a scoop, a saucer) and kicks it out toward kick_ang (degrees) at kick_v */
static int hole(float x, float y, float kick_ang, float kick_v, float hold_speed, int col) {
  int el = mk_elem(EK_HOLE, x, y, kick_ang, kick_v, col);
  int s = pb_add_circle(&T->W, x, y, 0.012f, 0, 0, SF_SENSOR | SF_HOLE, el);
  T->W.sh[s].hold_speed = hold_speed;
  own(el, s);
  return el;
}

/* a smooth 3D path through control points (Catmull-Rom), n_out points */
static int ramp_path(const float (*cp)[3], int ncp, int tag) {
  float pts[PB_PATHPTS][3];
  int n = 0, per = (PB_PATHPTS - 1) / (ncp - 1);
  if (per > 8) per = 8;
  for (int i = 0; i < ncp - 1; i++)
    for (int k = 0; k < per; k++) {
      float t = (float)k / (float)per;
      const float *p0 = cp[i > 0 ? i - 1 : 0], *p1 = cp[i], *p2 = cp[i + 1], *p3 = cp[i + 2 < ncp ? i + 2 : ncp - 1];
      for (int c = 0; c < 3; c++) {
        float a = p1[c], b = p2[c];
        float m1 = (p2[c] - p0[c]) * 0.5f, m2 = (p3[c] - p1[c]) * 0.5f;
        float t2 = t * t, t3 = t2 * t;
        pts[n][c] = (2 * t3 - 3 * t2 + 1) * a + (t3 - 2 * t2 + t) * m1 + (-2 * t3 + 3 * t2) * b + (t3 - t2) * m2;
      }
      n++;
    }
  pts[n][0] = cp[ncp - 1][0], pts[n][1] = cp[ncp - 1][1], pts[n][2] = cp[ncp - 1][2];
  n++;
  return pb_add_path(&T->W, (const float (*)[3])pts, n, 0.25f, tag);
}

/* a ramp: its entrance line (a to b, the ball crossing it toward the left of a->b goes up) and its path (control
 * points from the entrance's middle) */
static int ramp(float ax, float ay, float bx, float by, const float (*cp)[3], int ncp, int col) {
  int el = mk_elem(EK_RAMP, (ax + bx) * 0.5f, (ay + by) * 0.5f, 0, sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay)), col);
  int p = ramp_path(cp, ncp, el);
  int s = pb_add_seg(&T->W, ax, ay, bx, by, 0, 0, 0, SF_SENSOR | SF_RAMP, el);
  T->W.sh[s].path = (int8_t)p;
  own(el, s);
  T->el[el].path = p;
  return el;
}

static int shot_add(const char *name, int arrow) {
  if (T->nshot >= MAXSHOT) return -1;
  T->shots[T->nshot] = (shot){ name, arrow, 0 };
  return T->nshot++;
}

/* a mode: how it plays (MK_*), the shots it lights, the hits it needs, its clock, its value, the balls it adds.
 * PINBALL_RULES_ONE_SET builds every table on one shared set of lit-shot modes with no toy (the rule set the tables
 * had before): pinball_rulecheck's control, which must fail. */
static void mode_add(const char *name, const char *goal, int kind, int mask, int need, int seconds, int value, int balls) {
  if (T->nmode >= MAXMODE) return;
#ifdef PINBALL_RULES_ONE_SET
  static const int K[4] = { MK_SHOTS, MK_SHOTS, MK_SHOTS, MK_ALL }, M[4] = { 0x11, 0x0a, 0x1f, 0x1f };
  kind = K[T->nmode], mask = M[T->nmode], balls = 0;
#endif
  T->modes[T->nmode++] = (mode_def){ name, goal, mask, need, seconds, value, kind, balls };
}

/* the table's toy (game.h TOY_*): its element, and for the creature its body (a post, off until it rises), for the
 * magnet its field (a sensor that pulls a ball toward its middle while on) */
static void toy_add(int toy, float x, float y, int col, int lamp_shape, float lamp_size) {
#ifdef PINBALL_RULES_ONE_SET
  return;
#endif
  int el = mk_elem(EK_TOY, x, y, 90, 0, col);
  T->toy = toy, T->toy_el = el;
  if (toy == TOY_CREATURE) own(el, pb_add_circle(&T->W, x, y, 0.016f, 0.5f, 0.15f, SF_REPORT | SF_OFF, el));
  if (toy == TOY_MAGNET) {
    int s = pb_add_circle(&T->W, x, y, 0.065f, 0, 0, SF_SENSOR | SF_MAGNET | SF_OFF, el);
    T->W.sh[s].kick = 4.0f; /* its pull */
    own(el, s);
  }
  if (lamp_size > 0) T->el[el].lamp = mk_lamp(x, y, lamp_size, 0, lamp_shape, col);
}

static void plastic(int col, float z, const float (*p)[2], int n) {
  if (T->ndec >= MAXDECOR) return;
  decor *d = &T->dec[T->ndec++];
  memset(d, 0, sizeof *d);
  d->kind = DC_PLASTIC, d->col = col, d->z = z, d->n = n > 8 ? 8 : n;
  for (int i = 0; i < d->n; i++) d->pts[i][0] = p[i][0], d->pts[i][1] = p[i][1];
}
#define PLASTIC(col, z, ...) do { const float pp_[][2] = { __VA_ARGS__ }; plastic(col, z, pp_, (int)(sizeof pp_ / sizeof pp_[0])); } while (0)

static void flasher(float x, float y, float z, int col) {
  if (T->ndec >= MAXDECOR) return;
  T->dec[T->ndec++] = (decor){ .kind = DC_FLASHER, .x = x, .y = y, .z = z, .w = 0.018f, .col = col };
}

/* ── the shared lower playfield ──────────────────────────────────────── */

static void lower(int inlane_col, int outlane_col) {
  /* the cabinet: the outer walls, the shooter lane and the top arc */
  WALL(MAT_WOOD, 0.004f, 0.06f, { -0.258f, 0.20f }, { -0.258f, 0.82f });
  arc_wall(-0.0005f, 0.82f, 0.2575f, 180, 0, 22, 0.004f, 0.06f, MAT_WOOD);
  WALL(MAT_WOOD, 0.004f, 0.06f, { 0.257f, 0.82f }, { 0.257f, 0.0f });
  WALL(MAT_METAL, 0.003f, 0.035f, { 0.219f, 0.0f }, { 0.219f, 0.80f });
  /* the shooter lane's gate: up through it, never back down (a ball from the top arc slides off it onto the table) */
  int g = mk_elem(EK_GATE, 0.2375f, 0.83f, 0, 0.04f, 0);
  own(g, pb_add_seg(&T->W, 0.2185f, 0.80f, 0.2545f, 0.865f, 0.002f, 0.3f, 0.1f, SF_ONEWAY, g));
  /* the plunger's tip: the ball rests on it */
  T->plunge_x = 0.2375f, T->plunge_y = 0.0455f;
  int pl = mk_elem(EK_PLUNGER, 0.2375f, 0.03f, 90, 0.03f, 0);
  T->plunger_shape = pb_add_seg(&T->W, 0.2225f, 0.03f, 0.2525f, 0.03f, 0.002f, 0.1f, 0.2f, 0, pl);
  own(pl, T->plunger_shape);

  /* outlanes and inlanes, both sides (the right mirrored about the playfield's middle) */
  for (int side = 0; side < 2; side++) {
    float m = side ? -1.0f : 1.0f;
#define X(v) (side ? 2 * CX0 - (v) : (v))
    /* the outer wall's lower run into the drain */
    float ow[3][2] = { { -0.258f, 0.22f }, { -0.258f, 0.20f }, { X(-0.16f), 0.055f } };
    if (side) ow[1][0] = 0.219f, ow[0][0] = ow[1][0], ow[0][1] = ow[1][1] + 0.001f;
    mk_wall((const float (*)[2])ow, 3, side ? 0.003f : 0.004f, 0.04f, side ? MAT_METAL : MAT_WOOD, false);
    /* the divider between the outlane and the inlane, down to the flipper's base */
    float dv[3][2] = { { X(-0.215f), 0.36f }, { X(-0.215f), 0.24f }, { X(-0.126f), 0.152f } };
    mk_wall((const float (*)[2])dv, 3, 0.003f, 0.035f, MAT_METAL, false);
    /* the slingshot: a triangle of rubber, its long face kicks */
    float A[2] = { X(-0.171f), 0.332f }, B[2] = { X(-0.171f), 0.262f }, C[2] = { X(-0.126f), 0.224f };
    float sw[3][2] = { { A[0], A[1] }, { B[0], B[1] }, { C[0], C[1] } };
    mk_wall((const float (*)[2])sw, 3, 0.004f, 0.03f, MAT_PLASTIC, false);
    int sl = mk_elem(EK_SLING, (A[0] + C[0]) * 0.5f, (A[1] + C[1]) * 0.5f, 0, 0, side);
    float ax = A[0] + (C[0] - A[0]) * 0.08f, ay = A[1] + (C[1] - A[1]) * 0.08f, cx = A[0] + (C[0] - A[0]) * 0.92f, cy = A[1] + (C[1] - A[1]) * 0.92f;
    int s = pb_add_seg(&T->W, ax, ay, cx, cy, 0.005f, 0.6f, 0.3f, SF_REPORT, sl);
    T->W.sh[s].kick = 1.35f;
    own(sl, s);
    post(A[0], A[1], 0.005f, MAT_RUBBER);
    post(B[0], B[1], 0.005f, MAT_RUBBER);
    post(C[0], C[1], 0.005f, MAT_RUBBER);
    PLASTIC(side ? 1 : 0, 0.034f, { A[0] - m * 0.004f, A[1] + 0.008f }, { B[0] - m * 0.006f, B[1] - 0.004f }, { C[0] + m * 0.002f, C[1] - 0.008f });
    /* the lane switches: outlane and inlane */
    int out = sensor(EK_ROLLOVER, side ? 0.219f : -0.258f, 0.29f, X(-0.215f), 0.29f, 0, outlane_col);
    int in = sensor(EK_ROLLOVER, X(-0.215f), 0.29f, X(-0.171f), 0.29f, 0, inlane_col);
    T->el[out].lamp = mk_lamp(side ? 0.2005f : -0.2355f, 0.26f, 0.008f, 0, LS_CIRCLE, outlane_col);
    T->el[in].lamp = mk_lamp(X(-0.193f), 0.26f, 0.008f, 0, LS_CIRCLE, inlane_col);
    if (side) T->outlane_r = out, T->inlane_r = in;
    else T->outlane_l = out, T->inlane_l = in;
#undef X
  }
  /* the kickback: in the left outlane, a kicker that sends the ball back up while lit */
  T->kickback = mk_elem(EK_KICKBACK, -0.2355f, 0.215f, 90, 0, 3);
  own(T->kickback, pb_add_seg(&T->W, -0.258f, 0.215f, -0.215f, 0.215f, 0, 0, 0, SF_SENSOR, T->kickback));
  T->kickback_lamp = mk_lamp(-0.2355f, 0.235f, 0.007f, 90, LS_ARROW, 3);
  T->el[T->kickback].lamp = T->kickback_lamp;
  /* the flippers */
  float fy = 0.135f, fl = -0.1115f, fr = 0.0785f;
  T->flipper_cx = CX0;
  T->left_flipper = pb_add_flipper(&T->W, fl, fy, 0.075f, 0.012f, 0.0065f, -0.5236f, 0.4887f, -1);
  T->right_flipper = pb_add_flipper(&T->W, fr, fy, 0.075f, 0.012f, 0.0065f, CW_PI + 0.5236f, CW_PI - 0.4887f, -1);
  T->upper_flipper = -1;
  /* the drain */
  int d = mk_elem(EK_DRAIN, CX0, 0.02f, 0, 0.5f, 0);
  own(d, pb_add_seg(&T->W, -0.27f, 0.02f, 0.219f, 0.02f, 0, 0, 0, SF_SENSOR | SF_DRAIN, d));
  /* the apron's middle lamps: shoot again, bonus multipliers */
  T->shoot_again_lamp = mk_lamp(CX0, 0.075f, 0.012f, 0, LS_SHIELD, 2);
  for (int i = 0; i < 4; i++) T->bonus_x_lamp[i] = mk_lamp(CX0 + ((float)i - 1.5f) * 0.024f, 0.205f, 0.0085f, 0, LS_CIRCLE, 2);
  T->extra_ball_lamp = mk_lamp(CX0, 0.235f, 0.011f, 90, LS_ARROW, 1);
}

/* the top lanes: n lanes between guides from x0, each w wide, at y; returns the first rollover */
static int top_lanes(float x0, float y, float w, int n, int col) {
  for (int i = 0; i <= n; i++) {
    float x = x0 + w * (float)i;
    float p[2][2] = { { x, y - 0.03f }, { x, y + 0.03f } };
    mk_wall((const float (*)[2])p, 2, 0.003f, 0.03f, MAT_METAL, false);
    post(x, y - 0.032f, 0.005f, MAT_RUBBER);
  }
  int first = T->nel;
  for (int i = 0; i < n; i++) {
    float x = x0 + w * ((float)i + 0.5f);
    int el = sensor(EK_ROLLOVER, x - w * 0.45f, y, x + w * 0.45f, y, 0, col);
    T->el[el].group = 100 + i;
    T->el[el].lamp = mk_lamp(x, y - 0.05f, 0.0085f, 0, LS_CIRCLE, col);
  }
  T->lane_group = first, T->lane_n = n;
  return first;
}

/* the left orbit: a lane up the left wall that runs into the top arc; its count switch and spinner */
static int left_orbit(int col, bool spinner_too) {
  WALL(MAT_METAL, 0.003f, 0.035f, { -0.215f, 0.56f }, { -0.215f, 0.82f });
  arc_wall(0, 0.82f, 0.215f, 180, 128, 8, 0.003f, 0.035f, MAT_METAL);
  post(-0.215f, 0.558f, 0.006f, MAT_RUBBER);
  /* under the lane: a guide that turns a ball coming back down off the outlane and into the inlane */
  WALL(MAT_METAL, 0.003f, 0.035f, { -0.258f, 0.50f }, { -0.2155f, 0.405f });
  int el = sensor(EK_LOOP, -0.215f, 0.63f, -0.258f, 0.63f, 1, col); /* up the lane: toward the left of a->b */
  if (spinner_too) {
    int sp = sensor(EK_SPINNER, -0.215f, 0.70f, -0.258f, 0.70f, 0, col);
    (void)sp;
  }
  return el;
}

/* the right orbit: up between the shooter lane's wall and an inner wall, into the arc */
static int right_orbit(int col) {
  WALL(MAT_METAL, 0.003f, 0.035f, { 0.178f, 0.56f }, { 0.178f, 0.78f });
  arc_wall(0, 0.82f, 0.178f, -12, 40, 5, 0.003f, 0.035f, MAT_METAL);
  post(0.178f, 0.558f, 0.006f, MAT_RUBBER);
  WALL(MAT_METAL, 0.003f, 0.035f, { 0.219f, 0.50f }, { 0.1825f, 0.405f });
  return sensor(EK_LOOP, 0.219f, 0.63f, 0.178f, 0.63f, 1, col);
}

/* ── the tables ──────────────────────────────────────────────────────── */

static void lin3(float *o, uint32_t hex) {
  float c[3] = { (float)((hex >> 16) & 255) / 255.0f, (float)((hex >> 8) & 255) / 255.0f, (float)(hex & 255) / 255.0f };
  for (int i = 0; i < 3; i++) o[i] = c[i] <= 0.04045f ? c[i] / 12.92f : powf((c[i] + 0.055f) / 1.055f, 2.4f);
}

/* the shot arrow inserts: a chevron pointing up the shot, a little in front of it */
static int arrow(float x, float y, float ang, int col) { return mk_lamp(x, y, 0.013f, ang, LS_ARROW, col); }

static void coral_deep(void) {
  T->name = "CORAL DEEP";
  T->tagline = "down past the reef wall, where the old lights still burn";
  lin3(T->th.base, 0x0b4a5c), lin3(T->th.base2, 0x1f8f8c);
  lin3(T->th.col[0], 0xff6a3d), lin3(T->th.col[1], 0x37e0a8), lin3(T->th.col[2], 0xffc53a), lin3(T->th.col[3], 0xb06cff);
  lin3(T->th.wood, 0x5a3a22), lin3(T->th.dmd, 0xff7a2a), lin3(T->th.light, 0xe8f4ff);
  T->th.pattern = 0, T->th.music_root = 110.0f, T->th.music_style = 0;
  lower(1, 0);
  int lo = left_orbit(1, true);
  int ro = right_orbit(1);
  top_lanes(-0.115f, 0.925f, 0.05f, 3, 2);
  /* pops under the lanes */
  pop(-0.075f, 0.79f, 0);
  pop(0.035f, 0.80f, 3);
  pop(-0.02f, 0.705f, 1);
  /* the left ramp: up, a turn to the left and down the left side into the left inlane */
  WALL(MAT_METAL, 0.003f, 0.035f, { -0.178f, 0.43f }, { -0.178f, 0.52f });
  WALL(MAT_METAL, 0.003f, 0.035f, { -0.132f, 0.43f }, { -0.132f, 0.52f });
  WALL(MAT_METAL, 0.003f, 0.02f, { -0.178f, 0.52f }, { -0.155f, 0.545f }, { -0.132f, 0.52f });
  static const float LR[][3] = { { -0.155f, 0.50f, 0 }, { -0.152f, 0.62f, 0.035f }, { -0.15f, 0.76f, 0.07f }, { -0.185f, 0.835f, 0.08f },
                                 { -0.225f, 0.78f, 0.075f }, { -0.232f, 0.62f, 0.06f }, { -0.215f, 0.46f, 0.035f }, { -0.198f, 0.38f, 0.0f } };
  int lr = ramp(-0.178f, 0.50f, -0.132f, 0.50f, LR, 8, 0);
  T->W.path[T->el[lr].path].exit_speed = 0.6f;
  /* the drop bank and the scoop */
  drop_bank(-0.05f, 0.61f, -0.118f, 0.585f, 3, 2, true);
  WALL(MAT_METAL, 0.004f, 0.035f, { -0.045f, 0.585f }, { -0.045f, 0.64f }, { -0.01f, 0.678f }, { 0.025f, 0.64f }, { 0.025f, 0.585f });
  int scoop = hole(-0.01f, 0.635f, -100, 1.3f, 3.5f, 2);
  /* the right ramp: up and over to the right, down into the right inlane */
  WALL(MAT_METAL, 0.003f, 0.035f, { 0.085f, 0.46f }, { 0.085f, 0.54f });
  WALL(MAT_METAL, 0.003f, 0.035f, { 0.131f, 0.46f }, { 0.131f, 0.54f });
  WALL(MAT_METAL, 0.003f, 0.02f, { 0.085f, 0.54f }, { 0.108f, 0.565f }, { 0.131f, 0.54f });
  static const float RR[][3] = { { 0.108f, 0.52f, 0 }, { 0.11f, 0.65f, 0.04f }, { 0.12f, 0.78f, 0.075f }, { 0.165f, 0.84f, 0.085f },
                                 { 0.2f, 0.78f, 0.08f }, { 0.2f, 0.62f, 0.06f }, { 0.185f, 0.46f, 0.035f }, { 0.165f, 0.38f, 0.0f } };
  int rr = ramp(0.085f, 0.52f, 0.131f, 0.52f, RR, 8, 3);
  T->W.path[T->el[rr].path].exit_speed = 0.6f;
  /* standups by the ramps */
  target(-0.13f, 0.66f, -60, 0.022f, 1);
  /* the major shots */
  int a_lo = arrow(-0.235f, 0.47f, 90, 1), a_lr = arrow(-0.155f, 0.455f, 90, 0), a_sc = arrow(-0.01f, 0.56f, 90, 2), a_rr = arrow(0.108f, 0.475f, 90, 3), a_ro = arrow(0.201f, 0.49f, 90, 1);
  int s_lo = shot_add("LEFT ORBIT", a_lo), s_lr = shot_add("LEFT RAMP", a_lr), s_sc = shot_add("SCOOP", a_sc), s_rr = shot_add("RIGHT RAMP", a_rr), s_ro = shot_add("RIGHT ORBIT", a_ro);
  T->el[lo].shot = s_lo, T->el[lr].shot = s_lr, T->el[scoop].shot = s_sc, T->el[rr].shot = s_rr, T->el[ro].shot = s_ro;
  T->mode_hole = scoop, T->lock_hole = rr, T->jackpot_shot = s_lr;
  /* modes: started at the scoop */
  mode_add("TIDE POOL", "CATCH THE MOVING SHOT", MK_ROVE, 1 << s_lo | 1 << s_lr | 1 << s_rr | 1 << s_ro, 3, 40, 150000, 0);
  mode_add("PEARL DIVE", "RAMPS COLLECT THE PEARL", MK_HURRY, 1 << s_lr | 1 << s_rr, 2, 40, 200000, 0);
  mode_add("CURRENT", "TWO BALLS: ANY LIT SHOT", MK_SHOTS, 0x1f, 4, 40, 120000, 1);
  mode_add("LEVIATHAN", "HIT THE LEVIATHAN", MK_TOY, 0, 3, 50, 300000, 0);
  T->wizard_name = "THE ABYSS", T->wizard_goal = "EVERY SHOT AND THE BEAST";
  toy_add(TOY_CREATURE, -0.01f, 0.49f, 1, LS_CIRCLE, 0.012f);
  for (int i = 0; i < T->nmode; i++) T->mode_lamp[i] = mk_lamp(CX0 + ((float)i - 1.5f) * 0.03f, 0.43f, 0.0095f, 0, LS_STAR, i);
  for (int i = 0; i < 2; i++) T->lock_lamp[i] = mk_lamp(0.108f + ((float)i - 0.5f) * 0.022f, 0.44f, 0.008f, 0, LS_CIRCLE, 3);
  T->lock_lamp[2] = -1;
  T->mb_lamp = mk_lamp(CX0, 0.32f, 0.016f, 0, LS_SHIELD, 3);
  T->jackpot_lamp = mk_lamp(-0.155f, 0.425f, 0.011f, 90, LS_TRI, 2);
  T->el[scoop].lamp = mk_lamp(-0.01f, 0.535f, 0.009f, 0, LS_CIRCLE, 2);
  /* plastics over the slings' tops and the ramp entrances, the flashers */
  PLASTIC(0, 0.04f, { -0.2f, 0.53f }, { -0.11f, 0.53f }, { -0.11f, 0.56f }, { -0.2f, 0.56f });
  PLASTIC(3, 0.04f, { 0.075f, 0.55f }, { 0.14f, 0.55f }, { 0.14f, 0.58f }, { 0.075f, 0.58f });
  flasher(-0.24f, 0.9f, 0.05f, 0);
  flasher(0.21f, 0.9f, 0.05f, 3);
  flasher(-0.01f, 0.69f, 0.045f, 2);
}

static void star_foundry(void) {
  T->name = "STAR FOUNDRY";
  T->tagline = "the forge at the edge of the map, pouring new suns";
  lin3(T->th.base, 0x1a0f3d), lin3(T->th.base2, 0x4b1d6e);
  lin3(T->th.col[0], 0xff8a1f), lin3(T->th.col[1], 0x4fd2ff), lin3(T->th.col[2], 0xffe14d), lin3(T->th.col[3], 0xff3d8b);
  lin3(T->th.wood, 0x2e2a33), lin3(T->th.dmd, 0x52d8ff), lin3(T->th.light, 0xfff0e0);
  T->th.pattern = 1, T->th.music_root = 98.0f, T->th.music_style = 1;
  lower(0, 3);
  int lo = left_orbit(1, false);
  int ro = right_orbit(0);
  top_lanes(-0.14f, 0.93f, 0.045f, 4, 1);
  /* four pops in a diamond, upper middle */
  pop(-0.095f, 0.795f, 0);
  pop(0.035f, 0.80f, 3);
  pop(-0.03f, 0.855f, 1);
  pop(-0.03f, 0.735f, 2);
  /* the upper flipper on the right wall, aiming across at the left ramp */
  T->upper_flipper = pb_add_flipper(&T->W, 0.168f, 0.66f, 0.06f, 0.0105f, 0.006f, CW_PI + 0.5236f, CW_PI - 0.3491f, -1);
  WALL(MAT_METAL, 0.003f, 0.035f, { 0.18f, 0.64f }, { 0.168f, 0.676f });
  /* the centre ramp: straight up the middle, a loop round the top left and down to the left inlane */
  WALL(MAT_METAL, 0.003f, 0.035f, { -0.06f, 0.48f }, { -0.06f, 0.57f });
  WALL(MAT_METAL, 0.003f, 0.035f, { -0.014f, 0.48f }, { -0.014f, 0.57f });
  WALL(MAT_METAL, 0.003f, 0.02f, { -0.06f, 0.57f }, { -0.037f, 0.595f }, { -0.014f, 0.57f });
  static const float CR[][3] = { { -0.037f, 0.55f, 0 }, { -0.04f, 0.66f, 0.045f }, { -0.07f, 0.88f, 0.09f }, { -0.15f, 0.93f, 0.095f },
                                 { -0.205f, 0.85f, 0.085f }, { -0.205f, 0.66f, 0.06f }, { -0.2f, 0.47f, 0.03f }, { -0.198f, 0.38f, 0 } };
  int cr = ramp(-0.06f, 0.55f, -0.014f, 0.55f, CR, 8, 1);
  T->W.path[T->el[cr].path].exit_speed = 0.6f;
  /* two drop banks */
  drop_bank(-0.14f, 0.53f, -0.175f, 0.45f, 3, 0, false);
  drop_bank(0.165f, 0.45f, 0.135f, 0.53f, 3, 3, false);
  /* a saucer top left and a scoop right of the ramp */
  int sau = hole(-0.17f, 0.72f, -60, 1.4f, 1.1f, 2);
  WALL(MAT_METAL, 0.004f, 0.03f, { 0.02f, 0.53f }, { 0.02f, 0.59f }, { 0.055f, 0.628f }, { 0.09f, 0.59f }, { 0.09f, 0.53f });
  int scoop = hole(0.055f, 0.585f, -95, 1.7f, 3.5f, 2);
  /* a spinner in the lane under the pops */
  int sp = sensor(EK_SPINNER, -0.11f, 0.68f, -0.06f, 0.68f, 0, 1);
  (void)sp;
  target(-0.12f, 0.62f, -40, 0.02f, 1);
  target(0.10f, 0.67f, -150, 0.02f, 1);
  int a_lo = arrow(-0.235f, 0.47f, 90, 1), a_cr = arrow(-0.037f, 0.505f, 90, 1), a_sc = arrow(0.055f, 0.50f, 90, 2), a_ro = arrow(0.201f, 0.49f, 90, 0), a_sa = arrow(-0.15f, 0.66f, 120, 2);
  int s_lo = shot_add("LEFT ORBIT", a_lo), s_cr = shot_add("CENTRE RAMP", a_cr), s_sc = shot_add("SCOOP", a_sc), s_ro = shot_add("RIGHT ORBIT", a_ro), s_sa = shot_add("SAUCER", a_sa);
  T->el[lo].shot = s_lo, T->el[cr].shot = s_cr, T->el[scoop].shot = s_sc, T->el[ro].shot = s_ro, T->el[sau].shot = s_sa;
  T->mode_hole = scoop, T->lock_hole = sau, T->jackpot_shot = s_cr;
  mode_add("COLD START", "HIT EVERY TARGET", MK_SWEEP, 0, 6, 45, 120000, 0);
  mode_add("SUNPOUR", "RAMP AND SAUCER POUR", MK_HURRY, 1 << s_cr | 1 << s_sa, 2, 40, 250000, 0);
  mode_add("SHAPING", "THE MAGNET AND THE ORBITS", MK_TOY, 1 << s_lo | 1 << s_ro, 4, 45, 160000, 0);
  mode_add("NOVA", "TWO BALLS: ALL FIVE SHOTS", MK_ALL, 0x1f, 5, 60, 320000, 1);
  T->wizard_name = "SUPERNOVA", T->wizard_goal = "EVERY SHOT AND THE MAGNET";
  toy_add(TOY_MAGNET, -0.025f, 0.665f, 2, LS_STAR, 0.011f);
  for (int i = 0; i < T->nmode; i++) T->mode_lamp[i] = mk_lamp(CX0 + ((float)i - 1.5f) * 0.03f, 0.43f, 0.0095f, 0, LS_STAR, i);
  for (int i = 0; i < 3; i++) T->lock_lamp[i] = mk_lamp(-0.15f + (float)i * 0.016f, 0.69f + (float)i * 0.012f, 0.007f, 0, LS_CIRCLE, 2);
  T->mb_lamp = mk_lamp(CX0, 0.32f, 0.016f, 0, LS_SHIELD, 3);
  T->jackpot_lamp = mk_lamp(-0.037f, 0.47f, 0.011f, 90, LS_TRI, 2);
  T->el[scoop].lamp = mk_lamp(0.055f, 0.47f, 0.009f, 0, LS_CIRCLE, 2);
  PLASTIC(1, 0.04f, { -0.075f, 0.58f }, { 0.0f, 0.58f }, { 0.0f, 0.61f }, { -0.075f, 0.61f });
  flasher(-0.24f, 0.92f, 0.05f, 0);
  flasher(0.2f, 0.93f, 0.05f, 3);
  flasher(0.1f, 0.62f, 0.045f, 1);
}

static void neon_fair(void) {
  T->name = "NEON FAIR";
  T->tagline = "one more ride before the lights go out";
  lin3(T->th.base, 0x2a0a1e), lin3(T->th.base2, 0x7a1238);
  lin3(T->th.col[0], 0xff2d6f), lin3(T->th.col[1], 0x2dffd2), lin3(T->th.col[2], 0xffd23a), lin3(T->th.col[3], 0x5a8cff);
  lin3(T->th.wood, 0x6b2a1a), lin3(T->th.dmd, 0xff4fa0), lin3(T->th.light, 0xfff2e6);
  T->th.pattern = 2, T->th.music_root = 130.81f, T->th.music_style = 2;
  lower(2, 0);
  int lo = left_orbit(2, true);
  int ro = right_orbit(2);
  top_lanes(0.0f, 0.90f, 0.05f, 3, 2);
  /* three pops top right */
  pop(0.06f, 0.79f, 0);
  pop(0.14f, 0.80f, 3);
  pop(0.10f, 0.715f, 1);
  /* crossing ramps: the left one lands in the right inlane, the right one in the left */
  WALL(MAT_METAL, 0.003f, 0.035f, { -0.17f, 0.45f }, { -0.17f, 0.53f });
  WALL(MAT_METAL, 0.003f, 0.035f, { -0.124f, 0.45f }, { -0.124f, 0.53f });
  WALL(MAT_METAL, 0.003f, 0.02f, { -0.17f, 0.53f }, { -0.147f, 0.555f }, { -0.124f, 0.53f });
  static const float LR[][3] = { { -0.147f, 0.51f, 0 }, { -0.14f, 0.64f, 0.045f }, { -0.1f, 0.80f, 0.085f }, { 0.0f, 0.86f, 0.1f },
                                 { 0.12f, 0.88f, 0.1f }, { 0.2f, 0.80f, 0.085f }, { 0.195f, 0.55f, 0.05f }, { 0.165f, 0.38f, 0 } };
  int lr = ramp(-0.17f, 0.51f, -0.124f, 0.51f, LR, 8, 0);
  T->W.path[T->el[lr].path].exit_speed = 0.6f;
  WALL(MAT_METAL, 0.003f, 0.035f, { 0.04f, 0.47f }, { 0.04f, 0.55f });
  WALL(MAT_METAL, 0.003f, 0.035f, { 0.086f, 0.47f }, { 0.086f, 0.55f });
  WALL(MAT_METAL, 0.003f, 0.02f, { 0.04f, 0.55f }, { 0.063f, 0.575f }, { 0.086f, 0.55f });
  static const float RR[][3] = { { 0.063f, 0.53f, 0 }, { 0.055f, 0.66f, 0.05f }, { 0.0f, 0.76f, 0.09f }, { -0.11f, 0.84f, 0.11f },
                                 { -0.2f, 0.80f, 0.09f }, { -0.235f, 0.66f, 0.07f }, { -0.215f, 0.48f, 0.035f }, { -0.198f, 0.38f, 0 } };
  int rr = ramp(0.04f, 0.53f, 0.086f, 0.53f, RR, 8, 3);
  T->W.path[T->el[rr].path].exit_speed = 0.6f;
  /* the shooting gallery: five standups in a row */
  for (int i = 0; i < 5; i++) target(-0.095f + (float)i * 0.024f, 0.66f + (float)(i == 2) * 0.006f, -90, 0.018f, i & 1 ? 1 : 0);
  WALL(MAT_PLASTIC, 0.003f, 0.03f, { -0.108f, 0.676f }, { -0.047f, 0.708f }, { 0.014f, 0.676f }); /* the gallery's roof: nothing rests on it */
  /* a scoop centre left */
  WALL(MAT_METAL, 0.004f, 0.03f, { -0.085f, 0.53f }, { -0.085f, 0.58f }, { -0.05f, 0.618f }, { -0.015f, 0.58f }, { -0.015f, 0.53f });
  int scoop = hole(-0.05f, 0.575f, -95, 1.4f, 3.5f, 2);
  drop_bank(0.125f, 0.45f, 0.165f, 0.52f, 2, 3, false);
  int a_lo = arrow(-0.235f, 0.47f, 90, 2), a_lr = arrow(-0.147f, 0.465f, 90, 0), a_sc = arrow(-0.05f, 0.49f, 90, 2), a_rr = arrow(0.063f, 0.485f, 90, 3), a_ro = arrow(0.201f, 0.49f, 90, 2);
  int s_lo = shot_add("LEFT ORBIT", a_lo), s_lr = shot_add("LEFT RAMP", a_lr), s_sc = shot_add("SCOOP", a_sc), s_rr = shot_add("RIGHT RAMP", a_rr), s_ro = shot_add("RIGHT ORBIT", a_ro);
  T->el[lo].shot = s_lo, T->el[lr].shot = s_lr, T->el[scoop].shot = s_sc, T->el[rr].shot = s_rr, T->el[ro].shot = s_ro;
  T->mode_hole = scoop, T->lock_hole = lr, T->jackpot_shot = s_rr;
  mode_add("TICKET RUSH", "CATCH THE MOVING SHOT", MK_ROVE, 1 << s_lo | 1 << s_lr | 1 << s_rr | 1 << s_ro, 3, 40, 150000, 0);
  mode_add("HIGH STRIKER", "RAMPS: RING THE BELL", MK_TOY, 1 << s_lr | 1 << s_rr, 3, 45, 220000, 0);
  mode_add("GALLERY", "HIT EVERY TARGET", MK_SWEEP, 0, 5, 40, 120000, 0);
  mode_add("LAST RIDE", "TWO BALLS: ALL FIVE SHOTS", MK_ALL, 0x1f, 5, 60, 300000, 1);
  T->wizard_name = "GRAND FINALE", T->wizard_goal = "EVERY SHOT, RING THE BELL";
  toy_add(TOY_STRIKER, -0.165f, 1.0f, 2, LS_CIRCLE, 0);
  for (int i = 0; i < T->nmode; i++) T->mode_lamp[i] = mk_lamp(CX0 + ((float)i - 1.5f) * 0.03f, 0.43f, 0.0095f, 0, LS_STAR, i);
  for (int i = 0; i < 2; i++) T->lock_lamp[i] = mk_lamp(-0.147f + ((float)i - 0.5f) * 0.022f, 0.43f, 0.008f, 0, LS_CIRCLE, 0);
  T->lock_lamp[2] = -1;
  T->mb_lamp = mk_lamp(CX0, 0.32f, 0.016f, 0, LS_SHIELD, 0);
  T->jackpot_lamp = mk_lamp(0.063f, 0.445f, 0.011f, 90, LS_TRI, 2);
  T->el[scoop].lamp = mk_lamp(-0.05f, 0.52f, 0.009f, 0, LS_CIRCLE, 2);
  PLASTIC(0, 0.04f, { -0.19f, 0.54f }, { -0.11f, 0.54f }, { -0.11f, 0.57f }, { -0.19f, 0.57f });
  PLASTIC(1, 0.04f, { 0.03f, 0.56f }, { 0.1f, 0.56f }, { 0.1f, 0.59f }, { 0.03f, 0.59f });
  flasher(-0.24f, 0.9f, 0.05f, 0);
  flasher(0.21f, 0.92f, 0.05f, 1);
  flasher(-0.0f, 0.70f, 0.045f, 3);
}

static const char *const NAMES[NTABLES] = { "CORAL DEEP", "STAR FOUNDRY", "NEON FAIR" };
const char *table_name(int which) { return NAMES[which < 0 || which >= NTABLES ? 0 : which]; }

void table_build(table *t, int which) {
  memset(t, 0, sizeof *t);
  T = t;
  pb_init(&t->W);
  t->lock_lamp[2] = -1;
  t->kickback = -1;
  t->toy_el = -1;
  t->wizard_name = "WIZARD", t->wizard_goal = "EVERY SHOT";
  switch (which) {
  case 1: star_foundry(); break;
  case 2: neon_fair(); break;
  default: coral_deep(); break;
  }
  pb_finish(&t->W);
  T = NULL;
}
