/* pinball's ball solver: the table is a plane (x across, y up the table, metres) and the ball a circle on it.
 *
 * Why the game has its own (tests/pinball-physics.mjs measures it against the engine's Box2D and Box3D worlds): a
 * pinball ball is small (27 mm), fast (up to about 6 m/s off a flipper, more off a slingshot or a magnet) and hits
 * thin things (wire guides, 1 mm sheet metal, drop target faces) and a fast-turning flipper. This solver
 *   - substeps every tick so the ball moves at most half its radius per substep and a flipper turns at most
 *     0.02 rad,
 *   - finds each contact by conservative advancement along the substep (the distance to every shape is exact, so
 *     the ball stops at the surface whatever the speed: nothing tunnels),
 *   - takes a flipper's turning into the sweep (the ball's motion is followed in the flipper's own turning frame),
 *     and gives the ball the surface's velocity at the contact point (omega x r), so a shot's speed follows where it
 *     meets the flipper,
 *   - has per-surface restitution, friction and kick (bumpers, slingshots), one-way gates, sensors for switches,
 *     spinners and ramp entrances, and holes that hold the ball.
 * Ramps and wire forms are paths: on one the ball runs along a 3D curve under the table's gravity and rolls back
 * if it is too slow. Everything is + - * / sqrt and cartwheel/math.h: the same bits on every host. */
#ifndef PINBALL_PHYS_H
#define PINBALL_PHYS_H
#include <stdbool.h>
#include <stdint.h>

#define PB_R 0.0135f      /* ball radius */
#define PB_MAXSH 640
#define PB_MAXFL 4
#define PB_MAXB 6
#define PB_MAXEV 256
#define PB_MAXPATH 12
#define PB_PATHPTS 48

enum { SH_SEG, SH_CIRCLE };
enum {
  SF_SENSOR = 1,   /* no response: a crossing (segment) or an entry (circle) is an event */
  SF_ONEWAY = 2,   /* a segment solid only to a ball moving against its normal (left of a->b) */
  SF_OFF = 4,      /* not there now (a dropped target, an open gate) */
  SF_REPORT = 8,   /* hits are events */
  SF_HOLE = 16,    /* a circle sensor that holds a ball slower than hold_speed */
  SF_RAMP = 32,    /* a sensor that sends a ball crossing it forwards onto path `path` */
  SF_DRAIN = 64,   /* a sensor: the ball is gone */
  SF_MAGNET = 128, /* a circle sensor that pulls toward its centre while on */
};

typedef struct pb_shape {
  uint8_t kind, flags;
  int16_t tag;          /* the rules' element (-1 none) */
  int8_t path;          /* SF_RAMP: the path */
  float ax, ay, bx, by; /* segment a..b; circle centre a */
  float r;              /* segment half thickness (0 for a wire); circle radius */
  float e, mu;          /* restitution, friction */
  float kick;           /* speed added along the normal on a hit faster than kick_min (bumpers, slingshots) */
  float kick_min;       /* this shape's own threshold (0: the world's); a pop bumper fires on any touch */
  float hold_speed;     /* SF_HOLE */
  float minx, miny, maxx, maxy;
} pb_shape;

typedef struct pb_flipper {
  float px, py, len, rb, rt; /* pivot, length to the tip's centre, base and tip radius */
  float rest, up;            /* angles (radians, from +x, counter-clockwise) */
  float ang, omega;
  float wmax, alpha_up, alpha_down;
  float e, mu;
  bool held, on;
  int16_t tag;
  /* timing (the latency gate reads it): the time since the coil last switched on or off, and when the flipper got
   * where that switch sends it (-1 while it is still on its way) */
  bool coil_was;
  float coil_t, reach_t;
} pb_flipper;

enum { BS_OFF, BS_FREE, BS_PATH, BS_HELD };
typedef struct pb_ball {
  int state;
  float x, y, vx, vy;       /* on the table */
  float z;                  /* height over the playfield (paths) */
  float wx, wy, wz;         /* where it is drawn (table frame, z up): the table point or the path point */
  float pwx, pwy, pwz;      /* last tick's, for interpolation */
  int path; float s, sv;    /* BS_PATH: the path, the distance along it, the speed along it */
  int16_t held_tag; int held_ticks;
  float roll[4];            /* the drawn rolling (a quaternion, view only but cheap to keep here) */
} pb_ball;

typedef struct pb_path {
  int n;
  float p[PB_PATHPTS][3];  /* table frame, z up */
  float cum[PB_PATHPTS];   /* distance from p[0] */
  float len;
  float friction;          /* deceleration along the path, m/s^2 */
  int16_t tag;             /* the rules' element for "made it" */
  int16_t end_hole;        /* >= 0: the end drops into this hole shape (a tag) instead of the playfield */
  float exit_speed;        /* > 0: the end sets the speed onto the table (a wire form feeding a flipper) */
} pb_path;

enum { EV_HIT, EV_CROSS, EV_FLIP, EV_BALLS, EV_HOLE, EV_RAMP_IN, EV_RAMP_DONE, EV_RAMP_BACK, EV_DRAIN, EV_KICK };
typedef struct pb_event {
  uint8_t kind, ball;
  int16_t tag;
  float speed;  /* the approach speed of a hit, the crossing speed of a sensor */
  int8_t dir;   /* a crossing: +1 along the sensor's normal (left of a->b), -1 against */
} pb_event;

typedef struct pb_world {
  pb_shape sh[PB_MAXSH];
  int nsh;
  pb_flipper fl[PB_MAXFL];
  int nfl;
  pb_ball b[PB_MAXB];
  pb_path path[PB_MAXPATH];
  int npath;
  float g;               /* acceleration down the table (rolling), m/s^2 */
  float gz;              /* into the table (for paths), m/s^2 */
  float kick_min;        /* the approach speed a kicker needs */
  float max_speed;       /* a cap that keeps a bad frame from launching the ball off the world */
  pb_event ev[PB_MAXEV];
  int nev;
  /* stats of the last step (the physics test reads them) */
  int substeps, contacts;
  float max_pen;         /* deepest overlap found after a substep, metres */
} pb_world;

void pb_init(pb_world *w);
int pb_add_seg(pb_world *w, float ax, float ay, float bx, float by, float r, float e, float mu, int flags, int tag);
int pb_add_circle(pb_world *w, float x, float y, float r, float e, float mu, int flags, int tag);
int pb_add_flipper(pb_world *w, float px, float py, float len, float rb, float rt, float rest, float up, int tag);
int pb_add_path(pb_world *w, const float (*pts)[3], int n, float friction, int tag);
void pb_finish(pb_world *w); /* bounds of every shape (call after adding or moving shapes) */
void pb_shape_bounds(pb_shape *s);
int pb_ball_add(pb_world *w, float x, float y, float vx, float vy);
/* one fixed tick */
void pb_step(pb_world *w, float dt);
/* the drawn transform of a flipper: its angle */
float pb_flipper_angle(const pb_flipper *f);
/* signed distance from a point to a shape's surface (a flipper at its current angle) */
float pb_dist_shape(const pb_shape *s, float x, float y, float *nx, float *ny);
float pb_dist_flipper(const pb_flipper *f, float ang, float x, float y, float *nx, float *ny);
/* a point along a path (table frame) and its unit tangent */
void pb_path_at(const pb_path *p, float s, float out[3], float tan[3]);

#endif
