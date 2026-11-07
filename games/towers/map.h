/* towers maps: the .cwtd level format, parsed for the rules (sim.c), the view (view.c, mapbuild.c), the bake tool
 * (tools/mapview) and the soak build. Text, one record a line, '#' starts a comment line:
 *
 *   towers-map 1                   the format and its version (first line)
 *   map <n>                        the map's number (1..)
 *   name <words>                   the name shown on the map screen
 *   theme <id>                     meadow autumn desert snow volcano crystal
 *   size <w> <h>                   tiles (2 m each), at most MP_MAX_W x MP_MAX_H
 *   seed <n>                       what made it (the generator's seed, or 0 for a map drawn by hand)
 *   waves <n>                      how many waves the map holds off (the last one is a boss wave)
 *   diff <f>                       the map's difficulty: a multiplier on the creeps' health and numbers
 *   kinds <name> ...               the creeps its waves are made of (sim_kind_by_name), in the order they arrive
 *   grid                           then h lines of w characters, then "end":
 *                                    '.' path (the creeps walk it, low)   'g' ground (raised; nothing walks it)
 *                                    'o' a build slot (raised ground with a pad)   '#' rock (high)   '~' water (low)
 *   spawn <x> <y>                  a gate the creeps come out of: the first of its two path tiles on the map's edge
 *                                  (down the edge on the west and east sides, across it on north and south), up to
 *                                  MP_MAXSPAWN
 *   core <x> <y>                   the core they walk to: the top-left tile of a 2 x 2 block of path
 *   slot <x> <y> <o2> <o3> <o4>    a build slot ('o' in the grid) and its owner when two, three or four play
 *                                  (0-based seats; one player owns every slot)
 *   prop <x> <y> <kind> <frame> <turn>   scenery on a ground tile: kind decor (the theme's kit, its frame) or lamp;
 *                                  turn in quarter turns (0..3)
 *   feature <rock|water> <x> <y> <r> <h>   a smooth rock outcrop (a dome h metres high) or a pool with banks: its
 *                                  middle and radius in tiles (fractions allowed); the tiles under it stay ground
 *   light <x> <y> <height> <r> <g> <b> <energy> <range>   a light in the bake only (lamps, glows): its tile centre
 *                                  and height in metres, colour 0..1, energy and range as OmniLight3D
 *   route <x> <y> <w> <x> <y> <w> ...   a path as the generator drew it: its waypoints (tile units, the grid's corner
 *                                  at 0 0) and its width there, joined by a Catmull-Rom curve. The ground's shape
 *                                  follows the routes (map_floor_d), the rules the tiles (and the floor's part of
 *                                  them); a map without routes draws its path from the tiles
 *
 * Tile (x, y) is column x, row y; row 0 is the far (north, top of the screen) side. Its centre in the world is
 * x = (x - w / 2 + 0.5) * 2, z = (y - h / 2 + 0.5) * 2 metres. */
#ifndef TOWERS_MAP_H
#define TOWERS_MAP_H

#include <stdbool.h>
#include <stdint.h>

#define MP_MAX_W 40
#define MP_MAX_H 24
#define MP_TILE 2.0f   /* metres a tile */
#define MP_GROUND_H 0.5f /* raised ground's height over the path */
#define MP_ROCK_H 1.6f   /* rock's */
enum { MP_MAXSPAWN = 4, MP_MAXSLOT = 64, MP_MAXPROP = 400, MP_MAXLIGHT = 96, MP_MAXKIND = 8, MP_MAXFEAT = 16, MP_MAXROUTE = 6, MP_MAXWP = 48 };
enum { MP_THEME_MEADOW, MP_THEME_AUTUMN, MP_THEME_DESERT, MP_THEME_SNOW, MP_THEME_VOLCANO, MP_THEME_CRYSTAL, MP_THEMES };
extern const char *const MP_THEME_ID[MP_THEMES];

/* tile kinds in the grid */
enum { T_PATH, T_GROUND, T_SLOT, T_ROCK, T_WATER, T_KINDS };

typedef struct mp_slot { uint8_t x, y, own[4]; } mp_slot; /* own[n - 1]: the owner when n play */
typedef struct mp_prop { uint8_t x, y, kind, frame, turn; } mp_prop; /* kind 0 decor, 1 lamp */
typedef struct mp_light { uint8_t x, y; float h, r, g, b, energy, range; } mp_light;
/* a smooth terrain feature over the tiles (they stay ground for the rules): a rock outcrop (a dome h metres over the
 * ground) or a pool (a bowl down under its water); x, y its middle and r its radius in tiles */
enum { FEAT_ROCK, FEAT_WATER };
typedef struct mp_feat { uint8_t kind; float x, y, r, h; } mp_feat;
typedef struct mp_route { int n; float wp[MP_MAXWP][3]; } mp_route; /* its waypoints: x, y, width (tiles) */

typedef struct tdmap {
  int num, theme, w, h, waves;
  uint32_t seed;
  float diff;
  char name[48];
  uint8_t t[MP_MAX_H][MP_MAX_W]; /* [y][x] tile kind */
  int nspawn, nslot, nprop, nlight, nkind, nfeat, nroute;
  uint8_t spawn[MP_MAXSPAWN][2];
  uint8_t core[2];
  uint8_t kind[MP_MAXKIND];
  mp_slot slot[MP_MAXSLOT];
  mp_prop prop[MP_MAXPROP];
  mp_light light[MP_MAXLIGHT];
  mp_feat feat[MP_MAXFEAT];
  mp_route route[MP_MAXROUTE];
} tdmap;

/* Parse a .cwtd from its text; false with a message in err when it is not a valid map. kind_by_name names the creeps
 * (sim_kind_by_name); NULL accepts any name as kind 0. */
bool map_parse(const char *text, tdmap *m, int (*kind_by_name)(const char *), char *err, int errlen);
/* Read assets/maps/<n>.cwtd (an asset path, cw_vfs) and parse it. */
bool map_load(int num, tdmap *m, int (*kind_by_name)(const char *));
/* the asset path of a map's level file and of its bake */
void map_path(int num, char *out, int len);
void map_bake_path(int num, char *out, int len);
/* the maps there are (assets/maps/count.txt's number; 0 when it cannot be read) */
int map_count(void);

static inline float mp_wx(const tdmap *m, float tx) { return (tx - (float)m->w * 0.5f + 0.5f) * MP_TILE; }
static inline float mp_wz(const tdmap *m, float ty) { return (ty - (float)m->h * 0.5f + 0.5f) * MP_TILE; }
static inline int mp_tx(const tdmap *m, float x) { return (int)((x / MP_TILE) + (float)m->w * 0.5f + 1000.0f) - 1000; }
static inline int mp_ty(const tdmap *m, float z) { return (int)((z / MP_TILE) + (float)m->h * 0.5f + 1000.0f) - 1000; }
static inline bool mp_in(const tdmap *m, int x, int y) { return x >= 0 && y >= 0 && x < m->w && y < m->h; }
/* a gate's mouth: two path tiles on the map's edge from its tile (down the edge on the west and east sides, across
 * it on the north and south); where creeps come out of it, and whether it opens sideways (west or east) */
static inline bool mp_gate_side(const tdmap *m, int k) { return m->spawn[k][0] == 0 || m->spawn[k][0] == m->w - 1; }
static inline void mp_gate_pos(const tdmap *m, int k, float *x, float *z) {
  *x = mp_wx(m, m->spawn[k][0]) + (mp_gate_side(m, k) ? 0 : MP_TILE * 0.5f), *z = mp_wz(m, m->spawn[k][1]) + (mp_gate_side(m, k) ? MP_TILE * 0.5f : 0);
}
/* the core stands in the middle of the 2 x 2 path block whose top-left tile its record names */
static inline void mp_core_pos(const tdmap *m, float *x, float *z) { *x = mp_wx(m, m->core[0]) + MP_TILE * 0.5f, *z = mp_wz(m, m->core[1]) + MP_TILE * 0.5f; }
/* the slot's owner when n play (0 when one plays) */
static inline int mp_owner(const mp_slot *s, int n) { return n <= 1 ? 0 : s->own[(n > 4 ? 4 : n) - 1]; }
/* ── the ground's shape (the view draws it, the rules walk its floor; tools/check-maps.mjs mirrors it) ──
 * Points in tile units over the grid (tile (x, y) spans [x, x + 1] x [y, y + 1]). */
#define MP_CURB 0.25f  /* tiles: the curb's width, the floor's edge up to the ground */
#define MP_INSET 0.15f /* the floor's half-width: the route's less this (its tiles reach a little past it) */
#define MP_PAD_R 0.5f  /* the level ground round a slot's middle */
/* the path's floor: the signed distance (tiles) from its edge, negative on the floor. The routes' curves and the
 * core's yard run together smoothly, pushed back round every slot so its pad stands on level ground. wide: the
 * route's width at the nearest point (NULL when not wanted). */
float map_floor_d(const tdmap *m, float x, float y, float *wide);
/* where the floor is paved as a plaza (negative): the wide stretches, a curb's width in from the floor's edge */
float map_plaza_d(const tdmap *m, float x, float y);
/* the outcrops' height (m) over the ground at a point (the highest dome) and the pools' depth there (0 .. 1) */
void map_feat_at(const tdmap *m, float x, float y, float *rock, float *water);
/* the ground's height (m): the floor at 0, the curb rising to MP_GROUND_H, outcrops on the ground, pools sunk in */
float map_ground_h(const tdmap *m, float x, float y);
/* a point the creeps may walk: on a path tile and on the floor */
bool map_walkable(const tdmap *m, float x, float y);
/* the hash of what a map's bake depends on: FNV-1a 64 over its text's theme, size, grid, feature, prop, light, slot and route lines (each trimmed, with a newline), in file order. The bake tool writes it beside the .cwlm (lightmaps/<n>.hash,
 * 16 hex digits); tools/check-maps.mjs computes the same from the text and compares. */
uint64_t map_bake_hash(const char *text);

#endif
