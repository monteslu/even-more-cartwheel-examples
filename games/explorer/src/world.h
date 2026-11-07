/* explorer: the world. One large map of tiles cut into rooms on a grid of screen-sized cells, thirteen regions,
 * the entities placed in it (pickups, save shrines, grapple anchors, enemies, bosses, lamps, props) and the
 * doors between rooms. tools/worldgen builds it (worldgen.c) and writes assets/world.bin; the game, the checker
 * and the soak read that file.
 *
 * Units: one tile is one world unit; x grows right, y grows down. A cell is CELL_W x CELL_H tiles (one screen).
 * A room covers a rectangle of cells; its outer two tiles are wall except where a door opens. */
#ifndef EXPLORER_WORLD_H
#define EXPLORER_WORLD_H

#include <stdbool.h>
#include <stdint.h>

#define CELL_W 32
#define CELL_H 18
#define GW 60 /* cells */
#define GH 34
#define TW (GW * CELL_W)
#define TH (GH * CELL_H)
#define MAXROOMS 640
#define MAXENTS 8000
#define MAXDOORS 1400
#define NREGIONS 13

/* tiles */
enum {
  T_EMPTY,
  T_SOLID,
  T_ONEWAY,   /* stand on it from above, jump up through it, drop through with down + jump */
  T_SPIKE,    /* thorns: hurt, and back to the last safe ground */
  T_WATER,    /* deep water: swim with the tide charm, without it the cold pushes you back out */
  T_CRACKED,  /* solid until a ground pound lands on it */
  T_SECRET,   /* solid wall that a strike breaks (a hidden way) */
  T_BOSSDOOR, /* solid until the room's warden falls */
  T_SEAL,     /* the heart's seal: solid until five wardens have fallen */
  T_KINDS
};

/* abilities, in the order the world hands them out */
enum {
  AB_BOLT = 1 << 0,    /* the ranged spark (energy) */
  AB_DJUMP = 1 << 1,   /* a second jump in the air */
  AB_DASH = 1 << 2,    /* a burst along the ground or through the air */
  AB_CLIMB = 1 << 3,   /* cling to walls, climb them, jump off them */
  AB_POUND = 1 << 4,   /* plunge from the air: breaks cracked floors */
  AB_SWIM = 1 << 5,    /* the tide charm: swim deep water */
  AB_GRAPPLE = 1 << 6, /* a line to the anchors overhead: swing */
  AB_ALL = 0x7f
};
#define NABILITIES 7

/* regions */
enum { R_GATE, R_ROOTWAY, R_GLASS, R_FEN, R_CLOCK, R_TIDAL, R_STAR, R_SKY, R_ARCHIVE, R_FORGE, R_FROST, R_BLOOM, R_HEART };

/* entity kinds */
enum {
  EK_NONE,
  EK_PICKUP,  /* sub: PK_* */
  EK_SAVE,    /* a save shrine (also a waystone: fast travel between the ones lit) */
  EK_ANCHOR,  /* a grapple anchor */
  EK_ENEMY,   /* sub: enemy kind */
  EK_BOSS,    /* sub: boss index 0..5 */
  EK_LAMP,    /* a light: sub = style (lantern, crystal, brazier, glowshroom, ...) */
  EK_PROP,    /* decoration: sub = prop kind; flags say how it is held up */
  EK_START,
  EK_KINDS
};
enum { PK_ABILITY, PK_HEALTH, PK_ENERGY, PK_RELIC }; /* pickup kinds (sub), with ability in arg */
/* prop kinds (sub): 0..5 small floor things (grass, mushrooms, a rock, a fern, crystals, flowers), 6..8 hanging
 * (vines, roots, a glowing strand: arg is the length), then the regions' set pieces (arg: a variation seed) */
enum { PR_PIPE = 9, PR_MACHINE, PR_TREE, PR_CLUSTER, PR_KELP, PR_SHELF, PR_STATUE };
enum { EN_CRAWLER, EN_SPITTER, EN_FLYER, EN_HOPPER, EN_KNIGHT, EN_KINDS };
#define NBOSSES 6
/* how a prop or a lamp is held: standing on the tile below, hanging from the tile above, on a wall beside it */
enum { HOLD_FLOOR = 1, HOLD_CEIL = 2, HOLD_WALL_L = 4, HOLD_WALL_R = 8, HOLD_BACK = 16 /* on the back wall */ };

typedef struct ent {
  uint8_t kind, sub, arg, hold;
  uint16_t room, id;  /* id: pickups / bosses / saves numbered from 0 within their kind */
  float x, y;         /* feet (pickups and enemies: bottom centre), or the point (anchors, lamps) */
} ent;

enum { RK_PLAIN, RK_SHAFT, RK_HALL, RK_SAVE, RK_ITEM, RK_BOSS, RK_GATE, RK_SECRET, RK_START };

typedef struct room {
  uint8_t x, y, w, h; /* cells */
  uint8_t region, kind;
  uint8_t gate;       /* RK_GATE: the ability the obstacle needs (an AB_* bit index + 1), 0 none */
  uint8_t boss;       /* RK_BOSS: boss index + 1 */
  uint16_t first_door, ndoors;
} room;

typedef struct door {
  uint16_t a, b;      /* rooms */
  uint8_t vertical;   /* 0: in a wall between side by side rooms; 1: in a floor / ceiling */
  uint8_t pad;
  uint16_t x, y;      /* the opening's first tile (tiles) */
  uint8_t len;        /* tiles along the opening */
} door;

typedef struct region_info {
  const char *name;
  uint8_t x, y, w, h;  /* cells */
  uint32_t sky, far, mid, near, ground, top, accent; /* palette 0xRRGGBB */
} region_info;
extern const region_info REGIONS[NREGIONS];

typedef struct world {
  uint8_t t[TH][TW];
  uint16_t cell_room[GH][GW]; /* 0xffff: no room */
  room rooms[MAXROOMS];
  int nrooms;
  door doors[MAXDOORS];
  int ndoors;
  ent ents[MAXENTS];
  int nents;
  int npickups, nbosses, nsaves;
  float start_x, start_y;
  uint32_t seed, hash; /* hash of everything above: the route and the gates name the world they were made for */
} world;

static inline bool w_in(int x, int y) { return x >= 0 && y >= 0 && x < TW && y < TH; }
static inline int w_tile(const world *w, int x, int y) { return w_in(x, y) ? w->t[y][x] : T_SOLID; }
static inline bool t_solid(int t) { return t == T_SOLID || t == T_CRACKED || t == T_SECRET || t == T_BOSSDOOR || t == T_SEAL; }
int w_room_at(const world *w, float x, float y); /* room index or -1 */

bool world_load(world *w, const char *path); /* cw_asset or a file (native tools) */
bool world_save(const world *w, const char *path);
uint32_t world_hash(const world *w);
void world_generate(world *w, uint32_t seed); /* worldgen.c */

#endif
