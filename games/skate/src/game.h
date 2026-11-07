/* skate (working title): shared declarations. Units are metres and seconds, the simulation runs on fixed ticks (60 a
 * second, substepped inside), Y is up. Everything in the simulation (park.c, parks.c, skater.c, sim.c, bot.c) is pure C
 * on cartwheel/math.h's deterministic functions, so every host and the native soak play the same run. */
#ifndef SKATE_GAME_H
#define SKATE_GAME_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "three.h"

#define TPS 60
#define TICK_DT (1.0f / 60.0f)
#define MAXP 4
#define GRAV 9.81f
#define PI_F 3.14159265f

/* ── small vector helpers (deterministic: sqrt and cw_* only) ───────── */
typedef t3_vec3 v3;
static inline v3 V(float x, float y, float z) { return (v3){ x, y, z }; }
static inline v3 vadd(v3 a, v3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline v3 vsub(v3 a, v3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline v3 vmul(v3 a, float s) { return V(a.x * s, a.y * s, a.z * s); }
static inline v3 vmad(v3 a, v3 b, float s) { return V(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s); }
static inline float vdot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline v3 vcross(v3 a, v3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float vlen(v3 a) { return sqrtf(vdot(a, a)); }
static inline float vlen2(v3 a) { return vdot(a, a); }
static inline v3 vnorm(v3 a) {
  float l = vlen(a);
  return l > 1e-8f ? vmul(a, 1.0f / l) : V(0, 0, 0);
}
static inline v3 vlerp(v3 a, v3 b, float t) { return V(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
static inline float vhlen(v3 a) { return sqrtf(a.x * a.x + a.z * a.z); }
/* a rotated about the unit axis k by angle (Rodrigues) */
static inline v3 vrot(v3 a, v3 k, float ang) {
  float s, c;
  cw_sincos(ang, &s, &c);
  return vadd(vadd(vmul(a, c), vmul(vcross(k, a), s)), vmul(k, vdot(k, a) * (1 - c)));
}
/* the part of a perpendicular to the unit n */
static inline v3 vperp(v3 a, v3 n) { return vsub(a, vmul(n, vdot(a, n))); }
/* the rotation that takes unit a to unit b, applied to x (a and b close or far, never opposite in practice) */
static inline v3 vrot_between(v3 x, v3 a, v3 b) {
  v3 k = vcross(a, b);
  float s = vlen(k), c = vdot(a, b);
  if (s < 1e-7f) return x;
  k = vmul(k, 1.0f / s);
  float ang = cw_atan2(s, c);
  return vrot(x, k, ang);
}
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float signf(float x) { return x < 0 ? -1.0f : 1.0f; }
static inline float wrap_pi(float a) {
  while (a > PI_F) a -= 2 * PI_F;
  while (a < -PI_F) a += 2 * PI_F;
  return a;
}

/* ── the park (park.c): triangles to ride and hit, edges to grind ──── */

/* what a triangle is to the board */
enum { SURF_RIDE = 0, /* the board rolls on it */
       SURF_WALL = 1, /* it blocks: the board and the body bounce off it */
       SURF_NONE = 2  /* drawn only (rails' tubes, posts, trees' leaves) */ };
/* what a triangle is drawn with (view.c: a textured material each, tinted by the vertex colour) */
enum { MAT_CONCRETE, MAT_ASPHALT, MAT_WOOD, MAT_PAINT, MAT_METAL, MAT_GRASS, MAT_BRICK, MAT_ROOF, MAT_TILE, MAT_DIRT, MAT_LEAF, MAT_GLASS,
       MAT_KINDS };

typedef struct ptri {
  v3 a, b, c;      /* corners, counter clockwise seen from the face's side */
  v3 na, nb, nc;   /* smooth normals at the corners (a ramp's curve is exact between them) */
  v3 n;            /* the face normal */
  float col[3];    /* tint (sRGB 0..1) */
  float uvs[6];    /* texture coordinates in metres */
  uint8_t surf, mat;
  uint16_t piece;  /* the kit piece that made it */
} ptri;

/* a grindable edge: a straight run (coping, a rail, a ledge's lip). Runs that meet end to end are linked, so a grind
 * carries on round a bowl's coping or along a kinked rail */
enum { EDGE_RAIL, EDGE_LEDGE, EDGE_COPING };
typedef struct pedge {
  v3 a, b;
  v3 t;          /* unit a -> b */
  float len;
  v3 up;         /* which way the board stands on it */
  int prev, next; /* linked runs (-1 none) */
  uint8_t kind;
  uint16_t piece;
  uint16_t run;   /* edges of one rail / ledge / coping share a run id (grind goals) */
} pedge;

/* where a run starts: a spot on the ground, a heading */
typedef struct pspawn { v3 p; float yaw; char name[24]; } pspawn;

/* a gap: leaving the ground inside from and landing inside to (both boxes) scores it */
typedef struct pgap { v3 from_lo, from_hi, to_lo, to_hi; char name[28]; int points; } pgap;

/* a line: a start, a speed to push to, and what is there to do (the park check skates every one, the bot picks them) */
enum { LINE_QUARTER, LINE_KICKER, LINE_RAIL, LINE_LEDGE, LINE_GAP, LINE_BOWL, LINE_MEGA, LINE_MANUAL, LINE_KINDS };
typedef struct pline {
  v3 start;      /* on the ground */
  float yaw;     /* heading to ride */
  float speed;   /* push to this speed (m/s) */
  v3 at;         /* the feature: the rail's middle, the lip, the gap's landing */
  int kind;
  int edge_run;  /* LINE_RAIL / LINE_LEDGE: the run to grind (-1 none) */
  int gap;       /* LINE_GAP / LINE_MEGA: the gap it must clear (-1 none) */
  float pop_dist; /* LINE_RAIL / LINE_LEDGE / LINE_GAP / LINE_MANUAL: pop this far before the feature (0: ride it, the lip launches) */
  int spawn;      /* its start point (the reset button reaches it) */
  char name[28];
} pline;

#define PARK_MAXLINE 48
#define PARK_MAXSPAWN 12
#define PARK_MAXGAP 16
#define PARK_MAXTOKEN 5
#define PARK_NAME 32

typedef struct park {
  ptri *t;
  int nt, cap_t;
  pedge *e;
  int ne, cap_e;
  int nruns;
  pspawn spawn[PARK_MAXSPAWN];
  int nspawn;
  pgap gap[PARK_MAXGAP];
  int ngap;
  pline line[PARK_MAXLINE];
  int nline;
  v3 token[PARK_MAXTOKEN];
  int ntoken;
  v3 lo, hi;     /* bounds of everything */
  /* the triangles by grid cell (x, z) */
  float cell;
  int gx, gz;
  float ox, oz;
  int *cell_start, *cell_items;
  /* the look */
  int theme;
  char name[PARK_NAME];
  char blurb[64];
  int npieces;
  /* the pieces' footprints for the float check: the lowest points each piece claims to stand on */
  v3 *rest;
  uint16_t *rest_piece;
  int nrest, cap_rest;
  int goal_run;    /* the run the "grind it" goal names (-1 none) */
  float goal_grind_s;
  char goal_run_name[28];
  int goal_gap;    /* the gap a goal names */
  int ctl_block_line, ctl_short_line; /* the lines the check's controls break (PARKV_BLOCK, PARKV_SHORT; -1 none) */
} park;

typedef struct phit {
  float t;      /* 0..1 along the cast */
  v3 p, n;      /* point; smooth normal there */
  v3 fn;        /* the face normal */
  int tri;
} phit;

void park_free(park *k);
void park_finish(park *k); /* bounds and the grid; links edges */
/* the first triangle the segment from o to o + d crosses (from its front side) whose surf is in mask (1 << SURF_*) */
bool park_cast(const park *k, v3 o, v3 d, unsigned mask, phit *h);
/* the smooth normal of triangle i at the point p (barycentric) */
v3 park_normal_at(const park *k, int i, v3 p);
/* the ground under p: the highest ride surface within [p.y - below, p.y + above] straight down */
bool park_ground(const park *k, v3 p, float above, float below, phit *h);

/* ── the kit (park.c): pieces that write triangles, edges and rest points into a park ── */
typedef struct kit { park *k; int piece; float lift; /* every point raised this much (pieces on a roof) */ } kit;
void kit_begin(kit *b, park *k);
int kit_piece(kit *b); /* a new piece id */
void kit_tri(kit *b, v3 a, v3 c, v3 d, v3 na, v3 nc, v3 nd, int surf, int mat, const float col[3]);
void kit_quad(kit *b, v3 a, v3 c, v3 d, v3 e, int surf, int mat, const float col[3]); /* flat */
void kit_rest(kit *b, v3 p); /* the piece stands on the ground at p */
int kit_edge(kit *b, v3 a, v3 c, v3 up, int kind, int run); /* returns the edge index */
int kit_run(kit *b); /* a new run id */
/* a box with its bottom at y0: top rides, sides block (surf_side), corners at (x0..x1, z0..z1) turned yaw about (cx, cz) */
void kit_box(kit *b, float cx, float cz, float yaw, float w, float d, float y0, float y1, int mat_top, int mat_side, const float top[3], const float side[3]);
/* a floor slab top at y (ride), w x d, centred, turned */
void kit_floor(kit *b, float cx, float cz, float yaw, float w, float d, float y, int mat, const float col[3], float tile_m);

/* a ramp profile in the piece's own frame: u runs from the ride's foot (0) into the ramp, y up. Points with their normals */
#define PROF_MAX 48
typedef struct profile { float u[PROF_MAX], y[PROF_MAX], nu[PROF_MAX], ny[PROF_MAX]; int n; } profile;
void prof_quarter(profile *p, float radius, float height, int segs); /* a circular transition, vertical past the radius */
void prof_bank(profile *p, float len, float height, float foot_r); /* a flat slope with a rounded foot */
void prof_kicker(profile *p, float radius, float height, int segs); /* a launch: a circle arc, steep at the lip */
/* sweep a profile along a straight run: the foot at (x, z) along the line facing yaw (the rider rides toward yaw), width w,
 * with a deck of deck_d behind the top, closed sides and back (walls down to y0) */
typedef struct ramp_opts { float deck; float y0; int mat_face, mat_side; float face[3], side[3], deck_col[3]; bool coping; bool no_back; bool no_sides; int run; } ramp_opts;
void kit_ramp(kit *b, float x, float z, float yaw, float w, const profile *pr, const ramp_opts *o);
/* a bowl: a rounded rectangle (half sizes hx, hz, corner radius rc) sunk depth below y_top, its walls the profile (radius r),
 * coping round the rim; the floor at y_top - depth */
void kit_bowl(kit *b, float cx, float cz, float hx, float hz, float rc, float depth, float r, int mat, const float col[3], const float coping[3]);
/* a rail: a round bar from a to c (both at the bar's top) on posts down to ya and yc under its ends (posts at least
 * every 2.5 m) */
void kit_rail_on(kit *b, v3 a, v3 c, float ya, float yc, const float col[3]);
/* the deck round a bowl: from its rim out to a rectangle (half sizes ox, oz) at y 0 */
void kit_bowl_deck(kit *b, float cx, float cz, float hx, float hz, float rc, float ox, float oz, int mat, const float col[3]);
/* the ground: a rectangle at y 0 in 2 m cells, leaving out the cells inside any hole (x0, z0, x1, z1 each) */
void kit_ground(kit *b, float x0, float z0, float x1, float z1, const float *holes, int nholes, int mat, const float col[3]);
/* a low wall round a rectangle (the park's edge), h high */
void kit_fence(kit *b, float x0, float z0, float x1, float z1, float h, int mat, const float col[3]);
/* stairs down from (x, z) at height h along yaw, n steps of run x rise, width w; a handrail on one side when rail */
void kit_stairs(kit *b, float x, float z, float yaw, float w, float h, int n, float y0, int mat, const float col[3], int rail_side);

/* ── the parks (parks.c) ───────────────────────────────────────────── */
enum { PARK_SUNNY, PARK_PLAZA, PARK_MEGA, PARK_ROOFS, PARK_BARN, PARK_COUNT };
extern const char *const PARK_NAMES[PARK_COUNT];
/* build park n; variant 0 is the game's; the check's controls ask for broken ones (PARKV_*) */
enum { PARKV_GAME = 0, PARKV_FLOAT = 1, PARKV_BLOCK = 2, PARKV_SHORT = 3 };
bool park_build(park *k, int n, int variant);
/* the float and line checks (tools/parkcheck.c and the soak): problems written to out, count returned */
int park_check_floats(const park *k, char *out, size_t cap);
uint32_t park_geom_hash(const park *k); /* the drawn shape: the bake (lightmaps/park<n>.hash) is current while it matches */

/* ── input: the same for a pad, the keyboard and a bot ────────────── */
typedef struct input {
  float sx, sy;      /* steer / lean: x right, y up (stick or d-pad), -1..1 */
  bool ollie;        /* held: crouch; let go: pop */
  bool flip, grab, grind, manual; /* held */
  bool spin_l, spin_r; /* shoulders: spin in the air */
  bool reset;        /* back to a start point */
} input;

/* ── the skater (skater.c) ─────────────────────────────────────────── */
enum { SK_ROLL, SK_AIR, SK_GRIND, SK_BAIL };
enum { TR_NONE, TR_POP, TR_FLIP, TR_GRAB, TR_GRIND, TR_MANUAL, TR_SPIN, TR_GAP, TR_LIP, TR_KINDS };

#define COMBO_MAX 24
typedef struct combo_item { char name[32]; int points; } combo_item;
typedef struct combo {
  combo_item it[COMBO_MAX];
  int n, total, base, mult;
  int last_kind_id[COMBO_MAX]; /* the trick ids, for repeats */
} combo;

typedef struct trick_def { const char *name; int points; int len; float roll, yaw, pitch; } trick_def; /* flip: turns of the board */
extern const trick_def FLIPS[];
extern const int NFLIPS;
extern const trick_def GRABS[];
extern const int NGRABS;
extern const char *const GRIND_NAMES[];
extern const char *const MANUAL_NAMES[];

typedef struct skater {
  int st;
  v3 p, v;            /* the board's contact point (bottom middle) and velocity */
  v3 f, u;            /* the board's nose and up */
  bool fakie;         /* rolling tail first */
  int tri;            /* the ground triangle (SK_ROLL) */
  float crouch;       /* 0..1: holding to pop */
  int crouch_t;
  int ground_t, air_t;
  float push_t;       /* the push animation's phase */
  int push_k;         /* pushes made */
  /* the air */
  bool vert;
  v3 vert_n;          /* the wall's normal, flat */
  float spin;         /* turned about the board's up so far this air (radians, signed) */
  float spin_v;       /* spin speed */
  float pitch;        /* the board's pitch to the flight (non vert air) */
  bool land_seen;     /* a surface ahead it will come down on: the board pitches to meet it */
  float land_pitch;
  v3 takeoff;
  float peak_y, takeoff_y;
  int air_from_surf;
  /* a flip under way */
  int flip, flip_t, flip_len; /* FLIPS index or -1 */
  int flips_done;
  /* a grab */
  int grab, grab_t;   /* GRABS index or -1; ticks held */
  int grab_since;     /* ticks since it was let go (presentation) */
  /* a grind */
  int edge;
  float es, gv;       /* position along the edge from a, speed along it */
  float bal, bal_v, wob; /* balance -1..1; the wobble pushing it */
  int grind_kind, grind_t, grind_dir, grind_item;
  float grind_total_s; /* this grind's seconds (the grind goal) */
  int grind_run;
  bool ollie_held_grind, ollie_armed;
  /* a manual */
  bool manual;
  int manual_kind, manual_t, manual_item;
  float mbal, mbal_v, mwob;
  /* a bail */
  int bail_t;
  v3 bail_v;          /* the velocity it bailed at (the ragdoll's push) */
  int bails;
  const char *bail_why; /* the last bail's cause (tests, tuning) */
  /* the combo */
  combo cb;
  int link_t;         /* ticks left to link after landing */
  int land_t;         /* ticks since the last landing */
  int land_kind;      /* 0 clean, 1 sketchy */
  int last_banked;    /* the last combo's points (the HUD shows it) */
  int last_banked_t;
  int best_combo;
  int score;
  /* inputs remembered */
  bool prev_ollie, prev_flip, prev_grab, prev_grind, prev_manual, prev_reset;
  int buf_grind, buf_manual, buf_flip, buf_grab; /* ticks a press stays wanted */
  float buf_sx, buf_sy;
  int flip_dir, grab_dir;
  /* safety */
  v3 safe_p;
  float safe_yaw;
  int spawn_i;
  /* stats (debug fields, tests) */
  int tricks, landed, grinds, manuals, airs, gaps_hit, lost;
  float max_air, max_speed, max_grind_s;
  int lip_t;
  uint32_t rng;
  /* events this tick (sounds, effects) */
  uint32_t ev;
  /* the gap this air is over */
  int gap_from;
  int tokens; /* bits of the park's tokens taken */
  int best_spin; /* the biggest spin landed (degrees) */
} skater;

enum { EV_POP = 1, EV_LAND = 2, EV_BAIL = 4, EV_GRIND_ON = 8, EV_GRIND_OFF = 16, EV_TRICK = 32, EV_COMBO = 64, EV_TOKEN = 128, EV_GAP = 256, EV_PUSH = 512,
       EV_WALL = 1024, EV_MANUAL = 2048, EV_SKETCH = 4096, EV_LOST = 8192 };

typedef struct physparams { bool no_friction, no_substeps, no_smooth, no_sweep; } physparams;
extern physparams g_phys;

void sk_spawn(skater *s, const park *k, int spawn_i);
void sk_place(skater *s, const park *k, v3 p, float yaw, float speed);
void sk_step(skater *s, const park *k, const input *in);
void sk_force_bail(skater *s); /* development: a wipeout now (screenshots of the ragdoll) */
/* names */
const char *sk_state_name(int st);
/* the combo text ("Twistflip + 360 + Twin Truck") into out */
void sk_combo_text(const skater *s, char *out, size_t cap);
int sk_combo_value(const combo *c);

/* ── the run (sim.c): players, modes, the clock, goals ────────────── */
enum { MODE_FREE, MODE_ATTACK, MODE_BATTLE, MODE_COUNT };
extern const char *const MODE_NAMES[MODE_COUNT];
enum { RUN_PLAY, RUN_OVER };
#define RUN_TICKS (TPS * 120)
enum { GOAL_SCORE1, GOAL_SCORE2, GOAL_SCORE3, GOAL_TOKENS, GOAL_COMBO, GOAL_GAP, GOAL_GRIND, GOAL_COUNT };

typedef struct sim {
  park k;
  int park_id, mode, players;
  skater s[MAXP];
  int state, tick, clock; /* clock: ticks left (timed modes) */
  int over_t;
  uint32_t goals[MAXP];  /* bits */
  int goal_score[3];
  int winner;
  uint32_t hash;
  int skin[MAXP];        /* each player's skater */
} sim;

void sim_init(sim *S, int park_id, int mode, int players);
void sim_free(sim *S);
void sim_step(sim *S, const input in[MAXP]);
const char *sim_goal_text(const sim *S, int g, char *out, size_t cap);
uint32_t sim_hash(const sim *S);

/* ── the bot (bot.c): skates lines through the same input a pad gives ── */
typedef struct bot {
  int line;        /* the line it is on (-1 picking) */
  int phase, t, stuck;
  int trick_plan, grab_hold, spin_dir, flip_pick;
  uint32_t rng;
  v3 goal;
  int lines_done, lines_failed;
  int last_bails;
  int want_line; /* the park check: skate only this line (-1 any) */
  int skill;     /* 0 casual .. 2 sharp */
  int popped, crouching, link_pops, link_crouch;
  bool mistake;
} bot;
void bot_init(bot *b, uint32_t seed, int skill);
void bot_tick(bot *b, const sim *S, int i, input *in);

#endif
