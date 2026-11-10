/* Destructo: the simulation. Structures of wood, stone, glass and steel blocks on the engine's
 * Box3D world, knocked down with launched shot and charges. Everything here is deterministic (Box3D, cw_sin and
 * friends, integer hashes): the same inputs give the same bits on every host. No rendering. */
#ifndef DESTRUCTO_GAME_H
#define DESTRUCTO_GAME_H
#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/physics3d.h"

#define MAXP 4
#define MAXBLK 16384     /* block slots */
#define BODY_CAP 3000    /* bodies alive at once: past it a break leaves dust, not pieces (physcheck: 3000 awake bodies step in about 2 ms on the pool) */
#define BREAK_BUDGET 24  /* breaks a tick from the fracture queue */
#define BLAST_SHELLS 4   /* ticks a charge's blast takes to reach its radius */
#define AWAKE_SPLIT 1500 /* awake bodies past which a stone block breaks in two, not four */
#define MAXTGT 8
#define MAXEV 512
#define NAMMO 3
#define TICK (1.0f / 60.0f)
#define GRAV 9.81f

/* materials: how heavy, how tough, how they break */
enum { M_WOOD, M_STONE, M_GLASS, M_STEEL, M_TARGET, M_COUNT };
typedef struct material_info {
  const char *name;
  float density;     /* kg/m^3 */
  float friction, restitution;
  float toughness;   /* J per m^3 of block it takes to break one */
  float min_hit;     /* J: hits below this do no harm (a block settling on another) */
  int pieces;        /* how a block splits: 0 = to dust and shards only, 2 or 4 bodies */
  float points;      /* score per m^3 brought down */
} material_info;
extern const material_info MATS[M_COUNT];

/* ammunition */
enum { A_SHOT, A_HEAVY, A_CHARGE };
typedef struct ammo_info {
  const char *name;
  float radius, mass, vmin, vmax;
} ammo_info;
extern const ammo_info AMMO[NAMMO];

typedef struct block {
  cw_body3d body;
  t3_vec3 half;
  float hp;          /* J left before it breaks */
  float vol;
  uint8_t mat, gen, alive, tint;
  uint8_t paint;     /* 0: the material's colour in the level's palette; else a paint (view.c PAINT): roofs, trims, timber */
  int8_t target;     /* index into game.tgt, or -1 */
  uint16_t tag;      /* the structure's own: 1 a block placed by the level (not a fragment), 2 one of a landmark's core (the big blocks inside, under its skin of bricks: not counted in the share brought down) */
  int born;          /* tick */
  t3_vec3 home;      /* where it sat when the level settled (placed blocks) */
  bool down;         /* counted as brought down (score) */
  bool queued;       /* out of strength, waiting its turn to break (the fracture queue) */
  uint16_t blasted;  /* the charge (game.booms) that last pushed it */
  uint8_t keg;       /* a powder keg: when it breaks it goes off like a smaller charge (and may set off the next) */
} block;

typedef struct target_state {
  int blk;           /* block index, -1 when broken */
  t3_vec3 home;
  bool down;
  int down_tick, by_player;
} target_state;

/* paints the level builder gives blocks (the look only) */
enum { P_NONE, P_ROOF_RED, P_ROOF_SLATE, P_ROOF_COPPER, P_TRIM_CREAM, P_TIMBER, P_PLASTER, P_GOLD, P_TURQUOISE, P_BRICK, P_DARK_STONE,
       P_SAND_STONE, P_ROSE_STONE, P_WHITE_STONE, P_KEG,
       P_MARBLE, P_LIMESTONE, P_BRONZE, P_DARK_GLASS, P_TILE_WHITE, P_RED_BRICK, P_WHITE_PAINT, P_BLUE_GLASS, P_ROCK, P_SILVER,
       P_SARSEN, P_VERDIGRIS, P_CONCRETE, P_COUNT };
/* a banner on a pole, carried by a block (it goes where the block goes, and with it) */
#define MAXBANNER 24
typedef struct banner { int blk; t3_vec3 local; float h; uint8_t colour, side; } banner;

/* events for the view and the sound (render only; the simulation never reads them back) */
enum { EV_HIT, EV_BREAK, EV_SHATTER, EV_BOOM, EV_LAUNCH, EV_TARGET, EV_LAND, EV_SPLASH, EV_STRIKE /* a ball crushing into the structure (its burst) */ };
typedef struct sim_event {
  uint8_t kind, mat;
  t3_vec3 p, n;
  float size, speed;
  int a;
} sim_event;

/* one tick of the controls (a person's pad or the bot's: the same buttons) */
typedef struct input {
  bool left, right, up, down;        /* aim: turn and raise */
  float turn_k, raise_k;             /* analog strength 0..1 of the held direction (1 for a button) */
  bool orbit_left, orbit_right;      /* move the launcher round the structure on its circle */
  float orbit_k;                     /* analog strength of the orbit (1 for a button) */
  bool power_up, power_down;
  bool fire, ammo_next, ammo_prev;
} input;

/* a level, as generated (levels.c) */
enum { K_TOWER, K_WALL, K_BRIDGE, K_CASTLE, K_SCAFFOLD, K_PYRAMID, K_KEEP, K_LANDMARK, K_COUNT };
/* the settings a structure stands in (view.c): each kit's own, in the kits' order, a city and a harbour */
enum { ST_HIGHLAND, ST_MEADOW, ST_GORGE, ST_HEADLAND, ST_QUARRY, ST_DESERT, ST_SNOW, ST_CITY, ST_HARBOUR, ST_COUNT };
typedef struct level_def {
  const char *name;
  int kind;
  uint32_t seed;
  int size;          /* the kit's scale: storeys, rings, spans */
  int targets;
  int par;           /* shots for three stars */
  int ammo[NAMMO];   /* what the level gives */
  float range;       /* launcher distance from the structure's middle */
  int theme;         /* ground and sky */
  int lm;            /* K_LANDMARK: which landmark (landmarks.c LM_*) */
  const char *place; /* K_LANDMARK: where it stands (shown under its name) */
  int setting;       /* the setting (ST_*) + 1; 0: the kit's own */
  float mark;        /* the share that counts as brought down; 0: the kit's (KIT_MARK) */
} level_def;
#define NLEVELS 55
int level_find(const char *name); /* a level's index by its name, -1 none */
int level_setting(const level_def *L);
/* how long the level settles before play: 3 s, and a landmark 5.5 s (a tall tower sways a few centimetres for four or
 * five seconds before it is still) */
int level_settle_ticks(const level_def *L);
extern const level_def LEVELS[NLEVELS];
const level_def *level_get(int i);

enum { GS_AIM, GS_FLIGHT, GS_SETTLE, GS_WON, GS_LOST };
/* party (two to four players): steady hands. Ammunition without end, a shot a turn; a turn must bring down at least
 * one block of the structure (a turn that brings none is a strike, two strikes and you are out), and whoever brings a
 * crystal down is out. The last one in wins. */
enum { MODE_COOP, MODE_VERSUS, MODE_PARTY };
#define PARTY_STRIKES 2

typedef struct player_state {
  int score, shots, targets;
  int strikes;       /* party: turns that brought nothing down */
  int8_t out;        /* party: 0 in, 1 out for a crystal, 2 out on strikes */
  int8_t out_order;  /* party: 1 the first out, 2 the next ... */
} player_state;

typedef struct game {
  cw_world3d w;
  cw_body3d ground;
  const level_def *L;
  int level;
  block b[MAXBLK];
  int nb, nalive, free_head;
  int free_list[MAXBLK];
  int nfree;
  target_state tgt[MAXTGT];
  int ntgt, tgt_left;
  /* the launcher */
  t3_vec3 muzzle;    /* launch point */
  float yaw, pitch, power;  /* aim: yaw 0 = straight at the structure's middle, from wherever the launcher stands */
  float orbit;       /* where the launcher stands on its circle round the structure: 0 on +z (the start), growing to its right */
  float ring;        /* the circle's radius (the level's range) */
  int hold_turn, hold_raise, hold_orbit;
  int ammo_sel;
  int ammo[NAMMO];
  /* the shot in flight */
  cw_body3d proj;
  int proj_kind, proj_ticks, proj_hit_tick;
  bool proj_live, charge_armed;
  t3_vec3 proj_pos, proj_vel, impact;
  /* turns */
  int state, state_t;
  int players, mode, cur;
  player_state P[MAXP];
  int shots;         /* the team's (coop) or everyone's (versus) shots on this structure */
  int down_count, down_at_fire; /* placed blocks brought down so far, and when the turn's shot was fired (party) */
  int party_winner;  /* party: the seat still in at the end, -1 none */
  int tick;
  int awake_now;
  /* the fracture queue: blocks out of strength break a few dozen a tick (a charge breaks hundreds at once: making
   * them all in one tick is a dropped frame) */
  int bq[MAXBLK];
  t3_vec3 bq_dir[MAXBLK];
  int bq_head, bq_n;
  bool no_budget;    /* the hitch gate's control: every break and the whole blast in one tick, as it used to be */
  /* a charge's blast runs outward over a few ticks, a shell a tick */
  t3_vec3 blast_at;
  int blast_shell;   /* the next shell, 0 when no blast is running */
  float blast_r, blast_k; /* the running blast's radius and strength (a charge: CHARGE_R and 1; a keg: smaller) */
  /* kegs set off and waiting their blast (a chain goes off one after another, a few ticks apart) */
#define MAXPEND 16
  t3_vec3 pend_at[MAXPEND];
  uint8_t pend_charge[MAXPEND]; /* a charge that went off during a keg's blast (a charge's size, not a keg's) */
  int npend, pend_wait;
  int breaks_tick, made_tick; /* this tick's breaks and bodies made (stats) */
  int settle_quiet;
  /* stats */
  int broken, fragments, booms, hits, max_awake;
  float step_ms, step_ms_max;
  float tick_ms, world_ms, pre_ms, post_ms; /* wall time of the last game_step and its parts (profiling only: host-dependent, never fed back) */
  uint32_t hash;
  /* how much of the structure is down: the volume of every block the level placed (crystals not counted), and of
   * those the shot has broken or lost off the field (game_share adds the ones standing away from home) */
  float vol_total, vol_gone;
  /* the judgement, made once the world is still (GS_SETTLE ends when at most SETTLE_AWAKE bodies have been awake for
   * SETTLE_QUIET ticks): what it saw */
  int judged_awake;
  float judged_share;
  bool sandbox;      /* free fire: ammunition without end, the level never judged (the select screen's choice) */
  bool old_damage;   /* tests only: the cannon before the fix (no strike burst): the human-like gate's control (tools/humancheck.c) */
  bool proj_burst;   /* the shot in flight has struck the structure (its burst is spent) */
  bool old_rules;    /* tests only: the rules before the fix (the level judged 1.5 s after its last crystal fell, stars by shots alone): the result check's control */
  float extent;      /* the structure's radius (camera) */
  float top;         /* its height */
  float front;       /* how far its face nearest the launcher stands toward the launcher, at the start (game_front for now) */
  /* events this tick */
  sim_event ev[MAXEV];
  int nev;
  /* tuning for tests: a block forced to float (levelcheck control), slope etc. */
  uint32_t rng;
  uint8_t paint_now; /* the paint game_add_block gives (levels.c) */
  banner ban[MAXBANNER];
  int nban;
} game;

/* sim.c */
extern double (*g_sim_clock)(void); /* seconds; profiling only */
void game_new(game *g, int level, int players, int mode);
void game_free(game *g);
void game_step(game *g, const input *in);
void game_settle(game *g, int ticks);       /* step with no input (a level settling before play) */
void game_settle_finish(game *g);           /* game_settle's end: where everything rests is home (a settle stepped by hand) */
int game_awake(const game *g);
t3_vec3 game_aim_dir(const game *g);
float game_world_yaw(const game *g);       /* the aim's yaw in the world: the launcher's place on its circle plus its turn */
t3_vec3 game_out_dir(const game *g);       /* from the structure's middle out to the launcher, on the ground (unit) */
/* how far toward the launcher (along game_out_dir) the structure's nearest face stands, by the blocks' settled places:
 * the aim path is drawn that far */
float game_front(const game *g);
/* a world yaw as the launcher's own turn from where it stands (wrapped to -pi..pi) */
float game_yaw_from_world(const game *g, float world_yaw);
float game_speed(const game *g);
t3_vec3 game_block_pos(const game *g, int i);
bool game_over(const game *g);
uint32_t game_state_hash(const game *g); /* the blocks' poses and the counters, apart from the world they live in (replays) */
int game_stars(const game *g);
/* the share of the structure (by volume, crystals not counted) brought down: broken, lost off the field, or standing
 * more than its own size (and at least 0.6 m) from where it settled before play. Live: the HUD and the judgement */
float game_share(const game *g);
/* the level's mark: the share that counts as "brought down" (and the third star needs), by kit (levels.c): half a
 * tower or a scaffold, less of a wall or a castle, a sixth of a solid stepped pyramid */
extern const float KIT_MARK[K_COUNT];
float game_mark(const game *g);
#define SETTLE_AWAKE 2    /* the world is still: this many bodies awake at most ... */
#define SETTLE_QUIET 20   /* ... for this many ticks */
#define SETTLE_MAX (60 * 15) /* a turn's settle ends after 15 s whatever moves (jitter must not hang the game) */
/* the level's headline once judged: won and brought down, won with the structure mostly standing, or lost */
enum { RES_NONE, RES_BROUGHT_DOWN, RES_CLEARED, RES_LOST, RES_PARTY };
int game_result(const game *g);
/* place a block (levels.c builds with it): bottom-centre at p, half sizes, turned yaw about y */
int game_add_block(game *g, int mat, t3_vec3 centre, t3_vec3 half, float yaw);
int game_add_target(game *g, t3_vec3 centre);
/* a powder keg standing on the ground at (x, z) (levels.c places them against the structure) */
int game_add_keg(game *g, float x, float z);
#define KEG_HALF 0.32f
/* harm block i by energy e (J) the way a hit does (tests: set off a keg) */
void game_hit_block(game *g, int i, float e);
/* a banner on block blk, its pole foot at local (block frame), h tall */
void game_add_banner(game *g, int blk, t3_vec3 local, float h, int colour);
void game_fire(game *g);

/* levels.c */
void level_build(game *g, const level_def *L);
/* landmarks.c */
enum { LM_SPHINX, LM_SPACE_NEEDLE, LM_GIZA, LM_EIFFEL, LM_EMPIRE_STATE, LM_OPERA_HOUSE, LM_TAJ_MAHAL, LM_WILLIS, LM_NEUSCHWANSTEIN, LM_BURJ_KHALIFA,
       LM_COLOSSEUM, LM_BIG_BEN, LM_PISA, LM_PARTHENON, LM_STONEHENGE, LM_TOWER_BRIDGE, LM_ST_BASILS, LM_PETRONAS, LM_PAGODA, LM_ANGKOR_WAT,
       LM_CHICHEN_ITZA, LM_LIBERTY, LM_CHRIST_REDEEMER, LM_CN_TOWER, LM_MOAI, LM_COUNT };
void landmark_build(game *g, const level_def *L);
/* a crystal standing on whatever is highest at (x, z) (a flat top wide enough for it); false: no such place near */
bool landmark_crystal(game *g, float x, float z);

/* bot.c: the bot's buttons this tick (it aims through the same controls a person holds) */
typedef struct bot_state {
  int plan_tgt, attempt[MAXTGT], wait;
  float want_yaw, want_pitch, want_power;
  int want_ammo;
  bool planned, lob, solved;
  t3_vec3 aim_at;
  int last_shots;
  float want_orbit;  /* where on its circle the launcher is being rolled to (orbiting) */
  bool orbiting;
  bool orbited[MAXTGT]; /* it has looked round for a clear view of this crystal */
  bool no_orbit;     /* tests: the bot that never moves the launcher (the orbit check's control) */
  int yaw_jitter_control; /* tests: aim this many milliradians off (a bot that cannot solve) */
} bot_state;
void bot_reset(bot_state *b);
void bot_input(game *g, bot_state *b, input *in);

static inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return x;
}
static inline float hashf(uint32_t x) { return (float)(hash32(x) >> 8) / 16777216.0f; }

#endif
