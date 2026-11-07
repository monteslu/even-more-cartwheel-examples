/* explorer: the simulation. Fixed 60 Hz ticks, + - * / and sqrt only (and the engine's deterministic cw_ math),
 * so every host plays the same game from the same input.
 *
 *   player.c   the character: running, jumping (coyote time, buffered presses, variable height), the second jump,
 *              the dash, wall climbing, the plunge, swimming, the grapple line; tile collision. The checker and the
 *              bot run the same code.
 *   sim.c      everything else: strikes and sparks, enemies and their tells, the wardens, pickups, shrines, the
 *              tiles that break or open, rooms explored, events for sound and effects. */
#ifndef EXPLORER_SIM_H
#define EXPLORER_SIM_H

#include "world.h"

#define DT (1.0f / 60.0f)
#define MAXP 2
#define MAXEN 48
#define MAXSHOTS 96
#define MAXEV 128

/* the player's body: feet at (x, y), PW half wide, PH tall */
#define PW 0.38f
#define PH 1.8f

/* tuning (tiles, seconds); tests/explorer-feel measures what these give */
#define RUN 9.5f
#define G_ACC 95.0f
#define G_DEC 125.0f
#define A_ACC 70.0f
#define A_DEC 45.0f
#define GRAV_UP 68.0f
#define GRAV_CUT 155.0f
#define GRAV_FALL 100.0f
#define APEX_K 0.55f
#define APEX_V 3.0f
#define MAX_FALL 26.0f
#define JUMP_V 25.0f
#define DJUMP_V 20.0f
#define COYOTE 6  /* ticks a jump still works after leaving a ledge */
#define JBUF 8    /* ticks a jump pressed early is kept for the landing */
#define DASH_V 27.0f
#define DASH_T 10
#define DASH_CD 20
#define CLIMB_V 6.0f
#define WALLJ_VX 10.0f
#define WALLJ_VY 20.0f
#define WALL_LOCK 10
#define POUND_V 34.0f
#define POUND_STALL 6
#define SWIM_V 7.0f
#define GRAPPLE_REACH 10.5f

/* what the controls ask for this tick (the pad, the keyboard, the bot all fill this) */
typedef struct pinput {
  int8_t mx, my;           /* -1, 0, 1 (my -1 = up) */
  bool jump, jump_held;    /* jump went down this tick / is held */
  bool attack, shoot, dash, grapple, interact;
} pinput;

enum { PS_NORMAL, PS_CLING, PS_DASH, PS_POUND, PS_SWIM, PS_GRAPPLE, PS_HURT, PS_DEAD };

typedef struct player {
  float x, y, vx, vy;
  uint8_t state, on_ground, facing_left, active;
  int16_t coyote, jbuf, air_jumps, dash_t, dash_cd, wall_lock, pound_t, cold, hurt_t, invuln, drop_t, ground_t;
  int8_t wall;             /* -1 wall on the left, 1 on the right, 0 none (touching while airborne) */
  int8_t dash_dir, lock_side;
  bool dash_ready, jump_rising, in_water, deep;
  int16_t anchor;          /* grapple: the anchor entity, -1 none */
  float rope;
  float safe_x, safe_y;    /* the last firm ground (thorns and the cold send you back here) */
  int16_t atk_t, atk_dir, atk_cd, shot_cd; /* strike: ticks left, 0 side 1 up 2 down */
  int16_t assist_hits;
  int16_t mend_t;          /* mending: ticks left of the channel (down + spark on the ground: 2 energy for a leaf) */
  int16_t hp, hp_max, en, en_max, en_frac;
  uint32_t ev;             /* PE_* this tick */
  float land_v;
} player;

enum { /* player events */
  PE_JUMP = 1 << 0, PE_DJUMP = 1 << 1, PE_WALLJ = 1 << 2, PE_DASH = 1 << 3, PE_LAND = 1 << 4, PE_POUND_START = 1 << 5,
  PE_POUND_LAND = 1 << 6, PE_SPLASH = 1 << 7, PE_STROKE = 1 << 8, PE_GRAPPLE = 1 << 9, PE_RELEASE = 1 << 10,
  PE_HAZARD = 1 << 11, PE_COLD = 1 << 12, PE_CLING = 1 << 13, PE_MANTLE = 1 << 14, PE_DROP = 1 << 15,
};

/* what the body moves through: the tiles (with the broken and opened ones) and the anchors */
typedef struct pworld {
  const uint8_t (*t)[TW];
  const ent *anchors[64];
  int nanchors;
  uint32_t abilities;
} pworld;

void player_spawn(player *p, float x, float y);
/* one tick of movement; returns the tile index (y * TW + x) a plunge landed on (for the caller to break), or -1 */
int player_step(player *p, const pinput *in, const pworld *pw);
bool player_box_hits(const pworld *pw, float x, float y, int tile_kind); /* the body at (x, y) overlaps that kind */
float player_water(const pworld *pw, float x, float y);                  /* share of the body under water */
/* anchors near a room for pw (the room's and its neighbours') */
void pworld_anchors(pworld *pw, const world *w, int room);

/* ── the rest of the game ───────────────────────────────────────────── */

enum { SH_BOLT, SH_ORB, SH_WAVE, SH_SHARD, SH_SPORE, SH_FEATHER, SH_PILLAR, SH_BEAM, SH_KINDS };
typedef struct shot {
  uint8_t kind, alive, enemy, pad;
  float x, y, vx, vy, w, h;
  int16_t t, life, dmg;
} shot;

enum { ES_IDLE, ES_TELL, ES_ACT, ES_REST, ES_HURT };
typedef struct enemy {
  uint8_t kind, alive, state, facing_left;
  int16_t hp, hp_max, t, flash;
  float x, y, vx, vy, hx, hy; /* hx, hy: home */
  int16_t ent;                /* the world entity it came from */
  int16_t aux;
} enemy;

enum { BM_CHARGE, BM_LEAP, BM_VOLLEY, BM_RAIN, BM_SWEEP_LOW, BM_SWEEP_HIGH, BM_DIVE, BM_SUMMON, BM_PILLARS, BM_KINDS };
typedef struct boss {
  bool active, alive;
  uint8_t idx, state, move, phase, facing_left;
  int16_t hp, hp_max, t, flash, moves_done;
  float x, y, vx, vy, tx, ty;  /* tx, ty: the move's aim */
  float marks[6];
  int nmarks;
  int room, ent;
  float ax0, ax1, ay0, ay1;    /* the arena's floor span and height (tiles) */
  float floor_y;
} boss;

enum { /* game events, for sound and effects */
  EV_STRIKE, EV_HIT, EV_KILL, EV_BOLT, EV_HURT, EV_DIE, EV_PICKUP, EV_SAVE, EV_ROOM, EV_BREAK, EV_SECRET, EV_BOSS_START,
  EV_BOSS_TELL, EV_BOSS_ACT, EV_BOSS_HIT, EV_BOSS_DOWN, EV_SHOT, EV_SHOT_HIT, EV_DOOR, EV_SEAL, EV_ENEMY_TELL, EV_BOUNCE,
  EV_MEND, EV_KINDS
};
typedef struct event { uint8_t kind, a, b, c; float x, y; } event;

typedef struct sim {
  const world *w;
  uint8_t t[TH][TW];         /* the live tiles */
  uint8_t broken[TH * TW / 8]; /* tiles broken or opened, for the save */
  player p[MAXP];
  int players;
  uint32_t abilities;
  uint8_t got[MAXENTS / 8];   /* pickups taken (by pickup id) */
  uint8_t seen[MAXROOMS / 8]; /* rooms entered */
  uint8_t bosses_down;        /* bit per boss */
  uint8_t saves_lit[MAXENTS / 8]; /* save shrines touched (fast travel) */
  int relics, deaths, save_ent;
  int room, prev_room;        /* the room the camera shows (player 1's) */
  enemy en[MAXEN];
  int nen;
  shot sh[MAXSHOTS];
  boss b;
  event ev[MAXEV];
  int nev;
  uint32_t tick, hash;
  int boss_ticks;             /* ticks since the warden fight began (0: none) */
  bool won, god;              /* won: the heart has fallen; god: tests and the bench take no damage */
  int assist;                 /* hits taken per leaf lost (1 normally; the bot plays with 3: an assist, not a god mode) */
  int door_lock[8][3];        /* boss fight: door openings closed (x, y, len | vertical << 8) */
  int ndoor_lock;
  pworld pw;
} sim;

void sim_init(sim *s, const world *w, int players);
void sim_step(sim *s, const pinput in[MAXP]);
void sim_teleport(sim *s, float x, float y); /* every player there (fast travel, respawn) */
bool sim_got(const sim *s, int pickup_id);
int sim_beacons(const sim *s);
void sim_enter_room(sim *s, int room);       /* spawns the room's enemies, starts a warden fight */
uint32_t sim_hash(const sim *s);
int sim_pickups_total(const world *w, int pk);
int sim_pickups_got(const sim *s, int pk);

/* the save: what persists */
typedef struct save_state {
  uint32_t world_hash;
  uint32_t abilities;
  int16_t hp_max, en_max, save_ent, relics, deaths, bosses_down;
  uint8_t got[MAXENTS / 8 > 760 ? 760 : MAXENTS / 8];
  uint8_t seen[MAXROOMS / 8];
  uint8_t lit[96];
  uint16_t broken_n;
  uint32_t broken[256];       /* broken / opened tile regions, by a tile index each (flood-filled on load) */
  uint32_t play_ticks;
} save_state;
void sim_save(const sim *s, save_state *out);
void sim_restore(sim *s, const save_state *in);
/* sizes for drawing */
float enemy_hw(int kind);
float enemy_hh(int kind);
float boss_hw(int idx);
float boss_hh(int idx);
int boss_tell_ticks(int move);
extern const char *const ABILITY_NAME[NABILITIES];
extern const char *const BOSS_NAME[NBOSSES];

#endif
