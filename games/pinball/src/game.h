/* pinball (working title): shared types. The simulation (phys.c, table.c, tables.c, rules.c, bot.c) never touches
 * the GPU, so the soak and the physics check build it without a window. */
#ifndef PINBALL_GAME_H
#define PINBALL_GAME_H
#include <stdbool.h>
#include <stdint.h>

#include "phys.h"

#define MAXP 4
#define MAXEL 160
#define MAXLAMP 128
#define MAXSHOT 10
#define MAXMODE 4
#define MAXDECOR 96
#define MAXWALL 220
#define NTABLES 3
#define BALLS_PER_GAME 3
#define TICK (1.0f / 60.0f)

/* ── what a table is made of ─────────────────────────────────────────── */

enum { MAT_WOOD, MAT_METAL, MAT_PLASTIC, MAT_RUBBER, MAT_RUBBER_WHITE, MAT_WIRE, MAT_COUNT };
/* elements: the parts the rules know */
enum {
  EK_POP, EK_SLING, EK_POST, EK_TARGET, EK_DROP, EK_SPINNER, EK_ROLLOVER, EK_RAMP, EK_HOLE, EK_KICKBACK,
  EK_GATE, EK_DRAIN, EK_LOOP, EK_PLUNGER, EK_TOY, EK_KINDS
};

typedef struct wall {      /* one drawn run of wall: the physics segments and the mesh come from it */
  float p[24][2];
  int n;
  float r, h;               /* half thickness, height */
  int mat;
  bool closed;
  int8_t col;               /* the theme colour of a plastic wall (-1: the material's own) */
} wall;

typedef struct elem {
  int kind;
  float x, y, ang, w;      /* where it is drawn: centre, facing, size */
  int col;                 /* theme colour index (0..3) */
  int shape, nshape;       /* its physics shapes */
  int lamp;                /* its insert (-1 none) */
  int group;               /* a drop bank, a lane set, a shot */
  int path;                /* a ramp's path */
  int dir;                 /* a sensor's counted direction (+1, -1, 0 either) */
  int shot;                /* the major shot it makes (-1 none) */
  /* the view's state (rules write it) */
  bool down;               /* a drop target down, a gate open */
  float flash;             /* 1 on a hit, fading */
  float spin, spin_v;      /* a spinner's plate */
  bool swept;              /* a target already hit in a target sweep mode */
} elem;

enum { LS_CIRCLE, LS_ARROW, LS_TRI, LS_STAR, LS_RECT, LS_SHIELD };
typedef struct lamp {
  float x, y, size, ang;
  int shape, col;
  int8_t state;            /* rules: 0 off, 1 on, 2 slow blink, 3 fast blink */
  float level;             /* the view's brightness 0..1 (follows state) */
} lamp;

/* a major shot (an orbit, a ramp, a scoop): the modes light them */
typedef struct shot {
  const char *name;
  int arrow;               /* its arrow insert */
  int made;                /* times made this ball */
} shot;

/* how a mode plays: each table's four are its own mix of these */
enum {
  MK_SHOTS,  /* every shot in the mask lit for the whole mode */
  MK_ALL,    /* every shot in the mask lit until it is made once */
  MK_ROVE,   /* one lit shot that moves to the next every few seconds; catch it */
  MK_HURRY,  /* the lit shots carry a value that counts down; a shot collects it and it starts again */
  MK_SWEEP,  /* every standup and drop target lit: hit each one */
  MK_TOY,    /* the table's toy: the creature to hit, the magnet's catches, the bell to ring */
};
typedef struct mode_def {
  const char *name, *goal; /* "TIDE POOL", "hit lit shots" */
  int shots_mask;          /* the shots lit for it */
  int need;                /* hits to finish */
  int seconds;
  int value;               /* each hit (a hurry-up: the value it falls to) */
  int kind;                /* MK_* */
  int add_balls;           /* balls the mode adds (a two-ball mode) */
} mode_def;

/* each table's toy */
enum { TOY_NONE, TOY_CREATURE, TOY_MAGNET, TOY_STRIKER };

typedef struct decor {     /* drawn only: plastics, flasher domes, signs */
  int kind;                /* DC_PLASTIC (a coloured shape over posts), DC_FLASHER, DC_POSTCAP, DC_LANEGUIDE */
  float x, y, z, w, h, ang;
  int col;
  float pts[8][2];
  int n;
} decor;
enum { DC_PLASTIC, DC_FLASHER, DC_SIGN };

typedef struct theme {
  float base[3], base2[3]; /* the playfield's paint (linear) */
  float col[4][3];         /* four accent colours: inserts, plastics, bumper caps */
  float wood[3];
  float dmd[3];            /* the dot display's colour */
  float light[3];          /* the general illumination's colour */
  int pattern;             /* the playfield art */
  float music_root;        /* the music's key (Hz) */
  int music_style;
} theme;

typedef struct table {
  const char *name, *tagline;
  theme th;
  pb_world W;              /* the physics as built (copied for each ball in play) */
  wall walls[MAXWALL];
  int nwall;
  elem el[MAXEL];
  int nel;
  lamp lamps[MAXLAMP];
  int nlamp;
  shot shots[MAXSHOT];
  int nshot;
  mode_def modes[MAXMODE];
  int nmode;
  decor dec[MAXDECOR];
  int ndec;
  /* the table's rules: which parts do what */
  int lock_hole;           /* the element that locks balls for multiball */
  int mode_hole;           /* the element that starts a mode when lit */
  int jackpot_shot;        /* the shot a multiball's jackpot is on */
  int kickback;            /* the left outlane's kickback element (-1) */
  int lane_group, lane_n;  /* the top lanes: elements lane_group .. +n (rollovers), their lamps */
  int inlane_l, inlane_r, outlane_l, outlane_r;
  int drop_banks[3][2];    /* first element, count */
  int ndrop_banks;
  int bonus_x_lamp[4];     /* 2x 3x 4x 5x */
  int lock_lamp[3], extra_ball_lamp, shoot_again_lamp, kickback_lamp, mode_lamp[MAXMODE], mb_lamp, jackpot_lamp;
  int skill_lane;          /* the top lane that is the skill shot (rotates) */
  float plunge_x, plunge_y; /* where a new ball sits on the plunger */
  int plunger_shape;       /* the plunger's tip (a segment that stops the ball) */
  int left_flipper, right_flipper, upper_flipper; /* flipper indices in W (upper -1) */
  float flipper_cx;        /* the middle of the two lower flippers (x) */
  int pops[4], npop;
  int toy, toy_el;         /* TOY_*, its element (EK_TOY) */
  const char *wizard_name, *wizard_goal; /* the mode all four modes played lights */
} table;

void table_build(table *t, int which); /* tables.c: 0 .. NTABLES-1 */
const char *table_name(int which);

/* ── the game (rules.c) ──────────────────────────────────────────────── */

enum { GS_IDLE, GS_PLUNGE, GS_PLAY, GS_DRAINED, GS_BONUS, GS_OVER };

typedef struct input {
  bool left, right, upper; /* flipper buttons held (upper: the third flipper; the same as right on a table with one) */
  bool launch;             /* the plunger button held (released fires) */
  bool both;               /* both flippers held: the launch button's face once no ball waits on the plunger (a press
                            * that began while one waited, or pulled the plunger, never flips) */
  bool nudge_l, nudge_r, nudge_up; /* pressed this tick */
  bool start;              /* add a player / start (pressed this tick) */
} input;

enum { MSG_NONE, MSG_TEXT, MSG_BIG, MSG_JACKPOT, MSG_MULTIBALL, MSG_MODE, MSG_TILT, MSG_BALLSAVE, MSG_EXTRA, MSG_SKILL, MSG_BONUS, MSG_WARN, MSG_LOCK, MSG_REPLAY, MSG_WIZARD };

typedef struct player_state {
  int64_t score;
  int ball;                /* 1-based ball in play for this player */
  int extra_balls;
  int bonus, bonus_x;
  int lanes;               /* top lane bits */
  int locks;               /* balls locked for multiball */
  int mode_next, modes_done, modes_played;
  bool wizard_lit;
  int wizards;
  int lane_sets;
  int ramp_count, loop_count, pop_count, spin_count, drop_sets, target_count;
  bool kickback_lit, mode_lit, lock_lit, extra_lit;
  int mb_count;
} player_state;

typedef struct game {
  table T;
  int table_id;
  pb_world W;
  int players, cur;
  player_state P[MAXP];
  int state, state_t;
  int tick;
  int balls_in_play;       /* balls on the table now */
  int ball_save_t;         /* ticks of ball save left */
  float plunge;            /* the plunger's pull 0..1 */
  bool launch_was, pull_armed; /* a release fires only a pull that began in play (not the press that started the game) */
  bool both_ok;            /* the both-flippers button was let go since it last pulled the plunger */
  int tilt_warn;           /* warnings this ball */
  float tilt_meter;
  bool tilted;
  int nudge_t; float nudge_x, nudge_y; /* the cabinet's shove (the view moves the table) */
  /* modes and multiball */
  int mode;                /* running mode (-1 none) */
  int mode_t, mode_hits;
  bool multiball;
  int mb_t;                /* ticks since multiball began */
  int jackpots, jackpot_value;
  int skill_lane, skill_t;
  int combo_t, combo_n;
  int drop_wait[3];        /* ticks since a drop bank went all down (it resets after a moment) */
  int kick_hold;           /* ticks a held ball has left before the hole kicks it */
  int64_t hurry;           /* a hurry-up's value now */
  int rove_shot, rove_t;   /* a roving mode's lit shot, ticks until it moves */
  int serve_left, serve_t; /* balls a mode or the wizard mode still has to send up the shooter lane */
  /* the wizard mode: every shot lit, a growing award, four balls */
  bool wizard;
  int wizard_hits;
  int64_t wizard_value, wizard_total;
  /* the toys (rules side; the view draws them from these) */
  bool toy_up;             /* the creature stands (its shape is solid) */
  float toy_h;             /* the creature's height 0..1 (drawn) */
  int toy_rest;            /* ticks before a hit creature may rise again */
  int toy_flinch;          /* ticks of a hit creature's flinch (drawn) */
  int magnet_ball, magnet_t, magnet_cool; /* the ball the magnet holds (-1), ticks it holds it, ticks before the next */
  float magnet_spin, magnet_spin_v;       /* the forge ring's turn (drawn) */
  float puck_h, puck_v;    /* the high striker's puck (0 the base, 1 the bell) */
  int bell_t;              /* ticks since the bell rang (drawn) */
  int toy_hits, magnet_grabs, bells, wizards_total, toy_events;
  bool practice;           /* practice: the ball always comes back (no score kept) */
  /* messages for the dot display */
  int msg, msg_t, msg_len;
  char msg_a[32], msg_b[32];
  int64_t msg_value;
  /* events the view and the sound follow */
  struct { uint8_t kind; int16_t a; float v; } fx[64];
  int nfx;
  /* totals (debug fields, gates) */
  int flips, hits, drains, ramps_made, modes_started, modes_won, mbs, jackpots_total, tilts, launches, saves, ball_ticks;
  uint32_t hash;
} game;

/* sound and light cues (game.fx) */
enum {
  FX_FLIP, FX_FLIP_DOWN, FX_POP, FX_SLING, FX_TARGET, FX_DROP, FX_DROP_RESET, FX_SPIN, FX_ROLLOVER, FX_RAMP_IN, FX_RAMP_DONE,
  FX_HOLE, FX_KICKOUT, FX_LAUNCH, FX_DRAIN, FX_WALL, FX_BALLS, FX_JACKPOT, FX_MULTIBALL, FX_MODE_START, FX_MODE_SHOT, FX_MODE_DONE,
  FX_MODE_FAIL, FX_EXTRA, FX_TILT, FX_WARN, FX_SAVE, FX_KICKBACK, FX_LOCK, FX_LANES, FX_BONUS_TICK, FX_GAME_OVER, FX_SKILL,
  FX_NUDGE, FX_FLASH, FX_START, FX_PLUNGER_PULL, FX_WIZARD, FX_WIZARD_SHOT, FX_WIZARD_END, FX_TOY, FX_TOY_HIT, FX_HURRY
};

void game_new(game *g, int table_id, int players, uint32_t seed);
void game_step(game *g, const input *in);
bool game_over(const game *g);
void game_launch_ball(game *g); /* the machine launches one more ball (the bench) */
float game_flash(const game *g, int lamp); /* a lamp's drawn level now */
const char *fmt_score(int64_t v, char *buf, int cap); /* 1,234,560 */
/* tests (pinball_rulecheck, the soak): a switch event into the rules as the solver would report it, and a mode or the
 * wizard mode started at once */
void game_test_event(game *g, int ev_kind, int tag, float speed);
void game_test_start_mode(game *g, int m);
void game_test_light_wizard(game *g);
bool game_shot_lit(const game *g, int s); /* is this shot lit for the running mode or the wizard mode? */

/* bot.c: the self-playing input (the auto cart, the soak) */
void bot_input(const game *g, input *in, uint32_t *rng);

#endif
