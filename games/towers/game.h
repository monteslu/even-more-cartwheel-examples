/* towers: shared declarations. sim.c is the rules (deterministic, fixed tick, cartwheel math); view.c draws them
 * (render only); fx.c is the effects (render only); map.c reads the maps (DESIGN.md has the design). */
#ifndef TOWERS_GAME_H
#define TOWERS_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/nav.h"
#include "map.h"

#define MAXP 4
#define MAXE 4096     /* creeps alive at once, at most */
#define MAXT MP_MAXSLOT /* towers: one a slot */
#define MAXSH 384     /* mortar shells in flight */
#define MAXEV 8192    /* events for the effects */
#define MAXGRP 48     /* wave groups still spawning */
#define FGW (MP_MAX_W * 2) /* flow field cells a side (1 m) */
#define FGH (MP_MAX_H * 2)
#define TICK 60.0f
#define CORE_HP 20

enum { CK_SKITTER, CK_GHOUL, CK_HOUND, CK_IMP, CK_SPORELING, CK_BLOATER, CK_SHADE, CK_OGRE, CK_KINDS };
const char *sim_kind_name(int kind);
int sim_kind_by_name(const char *name); /* -1 when not a kind */

/* towers, in the build diamond's order: north, west, east, south */
enum { TK_BOLT, TK_FROST, TK_BEAM, TK_MORTAR, TK_KINDS };
const char *sim_tower_name(int k);
/* the four face buttons as the build diamond lays them out */
enum { A_N, A_W, A_E, A_S, A_KINDS };
enum { AIM_FIRST, AIM_STRONG, AIM_LAST, AIM_KINDS };
const char *sim_aim_name(int a);

enum { EV_BUILD, EV_UPGRADE, EV_SELL, EV_NOGOLD, EV_HOP, EV_AIM, EV_ZAP, EV_FROST, EV_FREEZE, EV_MORTAR, EV_BOOM, EV_HIT, EV_KILL, EV_LEAK, EV_SPLIT,
       EV_WAVE, EV_CALL, EV_WON, EV_LOST, EV_HEAL, EV_SPAWN, EV_BEAM_ON };
/* event fields: a, b small ints, x y where, z w a direction or a second point (each emitter says) */
typedef struct event { uint8_t kind, a, b; float x, y, z, w; } event;

/* a player's input this tick: the held direction (-1, 0, 1 each way), the four face buttons pressed this tick (A_*),
 * the west button held (selling takes a hold), and the call-the-next-wave button pressed */
typedef struct input { int8_t dx, dy; bool act[A_KINDS]; bool sell_held; bool wave; } input;

typedef struct player {
  bool active;
  int gold, slot;           /* the slot the cursor is on */
  int8_t hx, hy;            /* the direction held last tick */
  int hold, sell_t;         /* ticks the direction has been held; ticks the sell button has been held */
  int kills, spent, built;
  float dmg;
} player;

typedef struct tower {
  bool alive;
  uint8_t kind, level, owner, aim; /* level 1..3 */
  float x, y, yaw;
  int cd, tgt, beam_t, age, kills, spent, fired, pulses;
  float dmg, beam_k; /* damage done; the beam's ramp (1 .. 3) */
} tower;

typedef struct creep {
  bool alive, boss;
  uint8_t kind, spawn;
  float hp, maxhp, x, y, face, speed, slow_k;
  int slow_t, frozen, flash, age, heal_cd, bounty, leak, healed; /* healed: the tick it was last healed (heals never stack) */
  uint16_t prog; /* steps to the core (the flow field), for aiming at the first */
  cw_crowd_agent agent;
} creep;

typedef struct shell { bool alive; uint8_t slot, owner, level; float x0, y0, tx, ty, dmg, r; int t, T; } shell;

/* a group of a wave still coming out: count creeps of one kind from one gate, one every `every` ticks */
typedef struct wgroup { bool live, boss; uint8_t kind, spawn; int left, every, cd; float hp; } wgroup;

enum { ST_PLAY, ST_WON, ST_LOST };

typedef struct sim {
  int tick, state, state_ticks, players, map_ticks;
  bool no_spawn;  /* tests: no waves */
  bool god;       /* tests and the perf bench: the core never falls */
  cw_rng rng;
  tdmap m;                      /* the map as loaded */
  player p[MAXP];
  tower tw[MAXT];               /* one a slot (tw[k] stands on m.slot[k]) */
  creep e[MAXE];
  shell sh[MAXSH];
  wgroup grp[MAXGRP];
  int wave, next_t, core_hp, nenemies, kills, leaks, gold_total;
  int pot; /* bounties not yet shared out: every kill pays the whole team alike (DESIGN.md, the economy) */
  int16_t slot_at[MP_MAX_H][MP_MAX_W]; /* the slot on a tile + 1 (0 none) */
  int cover[MAXT];              /* path cells within a middling range of each slot (the bots' and the check's measure) */
  uint32_t hash;
  /* stats (not state) */
  int spawned_total, booms_total, zaps_total, frost_total, built_total, upgrades_total, sold_total, most_alive;
  float crowd_ms;
  /* nav and flow */
  cw_navmesh nav;
  cw_crowd crowd;
  int fw, fh;                   /* flow grid size (cells of 1 m) */
  uint16_t flow[FGW * FGH];     /* steps to the core */
  uint8_t blocked[FGW * FGH];
  float core_x, core_y;
  /* the hit grid: creeps in cells of 2 m (one a tile) */
  int16_t cell_start[MP_MAX_W * MP_MAX_H + 1];
  int16_t cell_idx[MAXE];
  event ev[MAXEV];
  int ev_n, ev_start; /* the events not yet consumed (fx_events empties them); this tick's from ev_start */
} sim;

void sim_init(sim *s, int players, uint32_t seed);
/* start a map (the players' gold starts again; the towers, creeps and waves are cleared) */
void sim_map_start(sim *s, const tdmap *m);
void sim_free(sim *s);
void sim_step(sim *s, const input in[MAXP]);
void sim_add_player(sim *s, int i); /* a player joins during play */
int sim_spawn(sim *s, int kind, float hp, float x, float y); /* tests: one creep there; its slot or -1 */
bool sim_slot_mine(const sim *s, int i, int slot); /* player i may act on this slot (an empty slot of their share or a tower of theirs) */
int sim_slot_owner(const sim *s, int slot);        /* who builds on an empty slot now */
int sim_tower_cost(int kind, int level);           /* the gold to reach `level` (1: build) */
int sim_sell_value(const tower *t);
float sim_tower_range(int kind, int level);
int sim_wave_count(const sim *s);                 /* the map's waves */
int sim_wave_size(const sim *s, int w);           /* how many creeps wave w (1..) sends */
float sim_creep_radius(int kind);
void towers_bot(const sim *s, int i, input *in);   /* bot.c */

/* the tower table (DESIGN.md): per kind and level 1..3 */
typedef struct tower_stats { int cost[3]; float range[3], dmg[3]; int period[3], extra[3]; } tower_stats;
extern const tower_stats TOWER[TK_KINDS];

#endif

/* ── render side ─────────────────────────────────────────────────────── */
#ifdef TOWERS_RENDER
#include "cartwheel/scene.h"
#include "three.h"
t3_color lin(float r, float g, float b); /* screen colour -> linear */
extern const float PCOL[MAXP][3];        /* the players' colours */
extern const float TCOL[TK_KINDS][3];    /* each tower's colour */
/* view.c */
bool view_init_step(cw_node root, int *done, int *total);
void view_free(void);
void view_map(const sim *s);                /* build the map the sim is on (call after sim_map_start) */
bool view_lightmap(cw_tree *t, const char *bake_out); /* a frame after view_map: load (or bake, native) its lightmap */
void view_sync(const sim *s, float time, const bool show_cursor[MAXP]);
void view_camera(const sim *s, cw_node camera, float aspect);
bool view_screen(t3_vec3 p, float *u, float *v); /* a world point on screen (0..1, y down) */
float view_px_per_m(float canvas_h);            /* how many pixels a metre is at this canvas height */
typedef struct view_stats { int creeps_drawn, towers, shells, zaps; } view_stats;
view_stats view_get_stats(void);
t3_color view_light_at(float x, float z);
t3_vec3 view_muzzle(const sim *s, int slot);    /* where a tower's shot leaves, world */
extern float g_cam_k, g_cam_x, g_cam_z; /* development and key art: the camera's distance as a multiple of the fit, and its offset */
extern float g_tilt, g_lean;
extern bool g_hide_cursors;
/* fx.c */
void fx_init(cw_node root);
void fx_free(void);
void fx_events(sim *s); /* consumes the events */
void fx_tick(const sim *s, float time);
typedef struct fx_stats { int booms, rumbles, bolts; } fx_stats;
fx_stats fx_get_stats(void);
extern int g_fx_level;
/* lightning: fx.c makes the jagged bolts, view.c draws them (a pool of segments) */
int fx_bolt_segments(const float **seg); /* each segment 7 floats: x0 y0 z0 x1 y1 z1 width; returns the count */
#endif
