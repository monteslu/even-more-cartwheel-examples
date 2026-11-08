/* Ember Throne: a third-person action adventure in one connected world. See DESIGN.md.
 *
 * sim.c    the rules: players, foes and bosses, combat, stamina, the camera (it steers the player, so it is
 *          simulation), progress (keys, hearts, the maul, shrines, doors), all on the fixed tick, deterministic
 * world.c  the world: the terrain's heights and paint, the regions, the buildings, stairs, gates, doors and props,
 *          their collision, the paging that keeps only the near regions live, the regions' colour
 * view.c   the characters (skinned models, clips, foot placement, weapons in the hand), the camera node, markers
 * fx.c     particles, flashes, sounds
 * bot.c    the autopilot: plays through the same input the pads give
 * main.c   the app, the shell, the HUD, the debug fields */
#ifndef ADV_GAME_H
#define ADV_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/core.h"
#include "cartwheel/math.h"
#include "cartwheel/physics3d.h"
#include "cartwheel/scene.h"
#include "three.h"

#define TPS 60
#define MAXP 2       /* one player, a second drops in */
#define MAXE 64      /* foes in the whole world (bosses included) */
#define MAXA (MAXP + MAXE)

/* ── the world ─────────────────────────────────────────────────────── */
#define WORLD_N 640  /* terrain quads a side, 1 m each */
enum { RG_MEADOW, RG_CANYON, RG_MARSH, RG_KEEP, RG_HEIGHTS, RG_UNDER, RG_SPIRE, RG_WOOD, RG_COUNT };
extern const char *const REGION_NAME[RG_COUNT];

/* structure materials */
enum { MT_STONE, MT_STONE_RED, MT_MARBLE, MT_FLAG, MT_WOOD, MT_IRON, MT_SLATE, MT_COUNT };

/* things the world holds that the rules touch */
enum { OB_SHRINE, OB_CHEST, OB_GATE, OB_DOOR, OB_CRACKED, OB_LEVER, OB_PICKUP, OB_KINDS };
enum { IT_NONE, IT_HEART, IT_CHARM, IT_KEY_SUN, IT_KEY_MOON, IT_MAUL, IT_TONIC_UP, IT_KINDS };
typedef struct wobj {
  int kind, region;
  float x, y, z, yaw;     /* ground position, facing */
  float sx, sy, sz;       /* size (gates, doors, walls) */
  int item;               /* chests and pickups: what is inside; gates: the keys it needs (bit 1 sun, 2 moon) */
  int flag;               /* the save flag that says it is done (opened, broken, taken, pulled) */
  int link;               /* a lever: the door it opens */
  int side;               /* a door: the side (+1/-1 along its facing) it can be opened from, 0 either */
} wobj;
#define MAXOBJ 48
extern wobj g_obj[MAXOBJ];
extern int g_nobj;

/* a foe placed in the world */
typedef struct wfoe { int kind, region, group; float x, z, yaw; int flag; } wfoe;
extern wfoe g_foe[MAXE];
extern int g_nfoe;

/* a route point for the autopilot */
typedef struct wroute { float x, z; int need_flag; int act; int obj; } wroute;
enum { RA_GO, RA_USE, RA_REST, RA_FIGHT, RA_HIT, RA_SWAP };
extern wroute g_route[256];
extern int g_nroute;

typedef struct shrine_pos { float x, y, z, yaw; int region; } shrine_pos;

void world_build(cw_tree *tree);            /* everything, once */
float world_height(float x, float z);        /* the terrain's surface (a ray against the terrain only) */
float world_ground(float x, float z, float from_y); /* the highest walkable surface below from_y (terrain, stairs, floors) */
int world_region_at(float x, float z);
void world_obj_state(int i, bool done);      /* show a gate open, a chest open, a wall broken */
void world_page(float x, float z, int region); /* paging: shows the near regions, hides the far */
void world_colour(float x, float z, float dt); /* blends the region's fog, ambient and sun toward where the camera is */
int world_shrine_obj(int shrine);            /* the object index of shrine n */
cw_world3d world_phys(void);
uint32_t world_hash(void);
extern int g_world_paged; /* regions live */

/* grounding: every prop the world placed, its base and footprint radius */
typedef struct wprop { float x, y, z, r; const char *what; int region; } wprop;
int world_props(const wprop **out);
int world_ground_check(float *worst);
int world_tree_check(float *worst);
void world_wind(float t);
void world_flora_lod(float x, float z);
struct cw_terrain *world_terrain(void);
void world_sky(t3_vec3 cam, float t);
int world_box_count(void);

/* ── the rules ─────────────────────────────────────────────────────── */
enum { K_WAYFARER, K_WARDEN, K_RAIDER, K_BRUTE, K_LANCER, K_THORN, K_TIDE, K_EMBER, K_KINDS };
#define IS_BOSS(k) ((k) >= K_THORN)
enum { ST_MOVE, ST_ATTACK, ST_DODGE, ST_HURT, ST_DOWN, ST_DEAD, ST_DRINK, ST_ACT, ST_REST, ST_ROAR, ST_IDLE_HOME };
enum { A_NONE, A_SLASH1, A_SLASH2, A_SLASH3, A_HEAVY, A_MAUL1, A_MAUL2, A_CLEAVE, A_CHOP, A_STAB, A_SLAM, A_HAY, A_SPIN, A_LARIAT,
       A_HAMMER, A_WAVE, A_COUNT };

typedef struct attack_def {
  const char *clip;
  int len, on, off;       /* ticks: the whole move, the hit window */
  int dmg, poise;         /* damage, how much it staggers */
  float reach, arc;       /* metres in front, the cos of half its arc (-1: all round) */
  float lunge;            /* m/s forward through the windup and the window */
  int stamina;            /* the player's cost */
  int combo_from, combo_to; /* the next press is taken in this window */
  bool knock;             /* knocks down */
  bool ring;              /* a ring on the ground warns of it (an area blow) */
  float ring_r;           /* the ring's radius */
  int shock;              /* a shockwave that runs out along the ground from it: its speed in cm/tick, 0 none */
} attack_def;
extern const attack_def ATK[A_COUNT];

typedef struct kind_def {
  const char *model, *weapon; float scale;
  int hp, poise; float speed, radius, height; int dmg_k; float aggro;
  const char *name;
} kind_def;
extern const kind_def KIND[K_KINDS];

typedef struct actor {
  bool used, active;      /* active: its region is live */
  int kind, idx;
  cw_mover3d mv;
  float yaw, yaw_prev;
  float vx, vz;           /* the move velocity it wants (m/s) */
  float speed;            /* how fast it is moving (for the clips) */
  int hp, hp_max, poise, poise_max;
  float stamina, stamina_max;
  int stam_wait;          /* ticks before stamina comes back */
  int state, st_t, st_len;
  int atk, combo; bool queued; int queue_atk;
  uint32_t hit_mask[3];   /* who this swing already hit */
  int iframes, flash;
  int target;             /* lock-on target (players) or who it hunts (foes); -1 none */
  /* foes */
  int foe, group, region, phase, think, token, strafe;
  float home_x, home_z, home_yaw;
  int cooldown;
  int off_t;              /* ticks it has been off its home ground (fell from a ledge): back home after a while */
  float home_y;
  bool aggro, boss;
  /* players */
  int weapon;             /* 0 the sword, 1 the maul */
  int tonics, tonic_max;
  int dodge_dir;          /* 0 a roll, 1 a backstep */
  float roll_x, roll_z;
  int down_t;             /* fallen: ticks until back (players) */
  int last_hit_by;
  float ground_y;
} actor;

typedef struct input {
  float mx, my;           /* movement stick, -1..1 (my down the screen is +1) */
  float cx, cy;           /* camera stick */
  bool attack, heavy, dodge, use, lock, tonic, swap;  /* just pressed */
  bool attack_held, heavy_held, dodge_held;
} input;

/* the save: progress only (the shell keeps it) */
typedef struct save_data {
  uint32_t version;
  uint32_t flags[4];      /* world flags: chests, walls, doors, bosses, pickups */
  int32_t shrine;         /* where to wake */
  int32_t hearts, charms, tonic_max;
  int32_t keys, maul;
  int32_t deaths, rests;
  int32_t play_ticks;
} save_data;

enum { EV_HIT, EV_BLOCKED, EV_KILL, EV_SWING, EV_DODGE, EV_STEP, EV_PICK, EV_OPEN, EV_REST, EV_BOSS, EV_ROAR, EV_SHOCK, EV_DOWN, EV_DRINK,
       EV_BREAK, EV_LEVER, EV_GATE, EV_FALL, EV_TELL, EV_WIN, EV_REGION, EV_LAND };
typedef struct event { int kind, a, b; float x, y, z, v; } event;

enum { GS_PLAY, GS_FALLEN, GS_WON };

typedef struct shock { bool on; int from; float x, y, z, r, speed; int dmg; uint32_t hit; } shock;

typedef struct sim {
  actor a[MAXA];
  int players;
  uint32_t tick;
  int state, state_t;
  save_data sv;           /* the live progress (copied to the shell's save on a rest) */
  int region, region_prev, region_t;
  int boss;               /* the boss actor in a fight, -1 none */
  int boss_flag;
  /* the camera (it steers the player): orbit around the lead player */
  float cam_yaw, cam_pitch, cam_dist, cam_dist_want;
  t3_vec3 cam_pos, cam_target, cam_pos_prev, cam_target_prev;
  int cam_idle;           /* ticks since the camera stick moved */
  int cam_clip_t;         /* ticks the camera was pulled in by a wall */
  float cam_clip;         /* how far it is pulled in (debug) */
  int cam_blocked;        /* ticks something solid stood between the player and the camera (the check: 0) */
  int interact;           /* the object the lead player can use now, -1 none */
  shock sh[4];
  event ev[128];
  int ev_n;
  uint32_t hash;
  int kills, hits_taken, hits_landed, dodges, dodged, tells, stagger;
  int deaths;
  int heal_ticks;
  int banner_t, banner_kind, banner_item;
  int fall_t;
} sim;

extern sim S;
void sim_init(int players, const save_data *sv);
void sim_step(const input in[MAXP]);
void sim_add_player(int i);
int sim_alive_players(void);
int sim_find_target(int p, int prefer_dir);
bool sim_has_flag(int f);
void sim_set_flag(int f);
const char *sim_item_name(int item);
void sim_respawn_foes(void);
float sim_cam_yaw(void);
void sim_teleport_party(float x, float z);

/* ── the view ──────────────────────────────────────────────────────── */
bool view_init_step(cw_node root, int *done, int *total);
void view_sync(float alpha);
void view_camera(cw_node cam, float alpha, float aspect);
void view_lod(cw_node cam, int height); /* each figure's level of detail from its size on the screen */
bool view_screen(t3_vec3 p, float *u, float *v);
void view_reset(void);
extern bool g_hide_hud;
typedef struct view_stats { int rigs, drawn, feet; float foot_err; } view_stats;
view_stats view_get_stats(void);

/* ── fx ────────────────────────────────────────────────────────────── */
void fx_init(cw_node root);
void fx_events(void);
void fx_tick(float t);
extern int g_fx_level;

/* ── the bot ───────────────────────────────────────────────────────── */
void bot_input(int p, input *in);
int bot_route_index(void);
void bot_reset(void);
void bot_jump(int route_index);
extern int g_bot_stuck;

/* ── maths ─────────────────────────────────────────────────────────── */
static inline float wrapf(float a) { return cw_wrap_angle(a); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float dist2(float ax, float az, float bx, float bz) { float dx = ax - bx, dz = az - bz; return dx * dx + dz * dz; }

#endif
