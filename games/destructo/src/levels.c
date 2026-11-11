/* destructo's levels: structure kits built from blocks, a seed choosing the details.
 *
 *   tower     a square stone tower of storeys: running-bond brick walls (each course hands the corners to the other
 *             pair of walls, so the walls lock together), glass bricks for windows, plank floors that the next
 *             storey stands on, battlements on top; crystals on the floors
 *   wall      a long fortified wall with towers at its ends; crystals behind it on stacked crates
 *   bridge    stone piers, steel girders across them, a plank deck with posts; crystals on the deck
 *   castle    four corner towers joined by curtain walls round a keep
 *   scaffold  steel posts and beams in storeys, plank floors, glass cladding; crystals on the floors
 *   pyramid   a stepped pyramid of stone with a wooden shrine on top and a chamber inside
 *   keep      a big tower of every material
 * Every level is checked (tools/levelcheck.c): it stands still for a minute when left alone, nothing in it floats,
 * and the bot clears it within par. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

/* name, kind, seed, size, targets, par, ammo {shot, heavy, charge}, range, theme; landmarks: which, where, setting + 1, mark, orbit.
 * A landmark whose name or image is claimed goes by a nickname with its city (the shape is the real one) */
/* orbit: where the launcher (and the camera) starts round the landmark, radians from +z: the view that names it */
/* a landmark built but held out of the game until its gate is green (tests/destructo-landmarks.mjs: standing as built,
 * still in the settle, no overlaps); its row stays here so it comes back by changing one word */
#define LANDMARK_HELD(...) /* (no comma after a held row) */
#define LANDMARK(name, lm, targets, par, s, h, c, range, place, setting, mark, orbit) { name, K_LANDMARK, 1000 + lm, 0, targets, par, { s, h, c }, range, 0, lm, place, setting + 1, mark, orbit }
const level_def LEVELS[NLEVELS] = {
  /* the tutorials: four kit structures that teach the shot, the heavy ball, the lob over a wall and the charge for a hidden crystal */
  { "First Stones", K_TOWER, 11, 2, 1, 2, { 4, 0, 0 }, 26, 0 },
  { "Lookout", K_TOWER, 23, 3, 1, 3, { 4, 1, 0 }, 28, 0 },
  { "Garden Wall", K_WALL, 31, 3, 2, 3, { 5, 1, 0 }, 28, 0 },
  { "Step Shrine", K_PYRAMID, 61, 4, 1, 2, { 4, 1, 1 }, 30, 2 },
  /* the real landmarks, by difficulty and world region */
  LANDMARK("Leaning Tower of Pisa", LM_PISA, 1, 2, 4, 1, 0, 28, "Pisa, Italy", ST_MEADOW, 0.01f, 0.0f),
  LANDMARK("The Redeemer", LM_CHRIST_REDEEMER, 1, 2, 5, 1, 0, 30, "Rio de Janeiro, Brazil", ST_HIGHLAND, 0.35f, 0.0f),
  LANDMARK("Great Sphinx", LM_SPHINX, 1, 2, 4, 0, 0, 22, "Giza, Egypt", ST_DESERT, 0.03f, 0.3f),
  LANDMARK("The Needle", LM_SPACE_NEEDLE, 1, 2, 4, 1, 0, 28, "Seattle, United States", ST_CITY, 0.4f, 0.0f),
  LANDMARK("Iron Lady", LM_EIFFEL, 2, 2, 5, 1, 1, 32, "Paris, France", ST_CITY, 0.05f, 0.35f),
  LANDMARK("The Tower", LM_CN_TOWER, 1, 2, 5, 1, 1, 34, "Toronto, Canada", ST_HARBOUR, 0.01f, 0.0f),
  LANDMARK("The Great Clock", LM_BIG_BEN, 2, 2, 5, 1, 1, 30, "London, United Kingdom", ST_HARBOUR, 0.01f, 0.4f),
  LANDMARK("Statue of Liberty", LM_LIBERTY, 2, 8, 8, 2, 6, 32, "New York, United States", ST_HARBOUR, 0.09f, 0.3f),
  LANDMARK("Taj Mahal", LM_TAJ_MAHAL, 3, 3, 5, 2, 1, 34, "Agra, India", ST_MEADOW, 0.2f, 0.0f),
  LANDMARK("Harbour Sails", LM_OPERA_HOUSE, 2, 2, 5, 1, 1, 27, "Sydney, Australia", ST_HARBOUR, 0.07f, -0.15f),
  LANDMARK("The Empire", LM_EMPIRE_STATE, 2, 2, 5, 1, 1, 32, "New York, United States", ST_CITY, 0.17f, 0.45f),
  LANDMARK("Parthenon", LM_PARTHENON, 2, 3, 6, 2, 1, 32, "Athens, Greece", ST_HEADLAND, 0.3f, 0.5f),
  LANDMARK("Pyramids of Giza", LM_GIZA, 3, 3, 6, 2, 1, 38, "Giza, Egypt", ST_DESERT, 0.01f, 0.3f), /* the core of each pyramid is not counted (only its skin of bricks): an eighth of the skin */
  LANDMARK("Chichen Itza", LM_CHICHEN_ITZA, 2, 3, 6, 2, 1, 34, "Yucatan, Mexico", ST_MEADOW, 0.01f, 0.6f),
  LANDMARK("Angkor Wat", LM_ANGKOR_WAT, 3, 5, 6, 2, 3, 36, "Siem Reap, Cambodia", ST_MEADOW, 0.25f, 0.55f),
  LANDMARK("Moai of Ahu Tongariki", LM_MOAI, 3, 4, 6, 1, 0, 30, "Rapa Nui, Chile", ST_HEADLAND, 0.26f, -0.35f),
  LANDMARK("The Spire", LM_BURJ_KHALIFA, 2, 3, 6, 2, 2, 40, "Dubai, United Arab Emirates", ST_DESERT, 0.27f, 0.3f),
  LANDMARK("The Black Tower", LM_WILLIS, 2, 5, 5, 2, 1, 32, "Chicago, United States", ST_CITY, 0.35f, 0.5f),
  LANDMARK("Tower Bridge", LM_TOWER_BRIDGE, 2, 8, 6, 2, 3, 36, "London, United Kingdom", ST_HARBOUR, 0.3f, 0.25f),
  LANDMARK_HELD("Mount Rushmore", LM_RUSHMORE, 3, 4, 6, 2, 2, 30, "South Dakota, United States", ST_HIGHLAND, 0.1f, 0.3f)
  LANDMARK_HELD("The Kremlin", LM_KREMLIN, 2, 3, 6, 2, 2, 32, "Moscow, Russia", ST_SNOW, 0.3f, 0.25f)
  LANDMARK("Stacked Tower", LM_TAIPEI, 2, 3, 6, 2, 2, 36, "Taipei, Taiwan", ST_CITY, 0.3f, 0.3f),
  LANDMARK("Golden Span", LM_GOLDEN_GATE, 2, 3, 6, 2, 2, 46, "San Francisco, United States", ST_HARBOUR, 0.06f, 0.35f),
  LANDMARK_HELD("Brooklyn Bridge", LM_BROOKLYN, 3, 4, 6, 2, 2, 46, "New York, United States", ST_HARBOUR, 0.3f, 0.3f)
  LANDMARK("Crown Tower", LM_CHRYSLER, 2, 3, 6, 2, 1, 34, "New York, United States", ST_CITY, 0.3f, 0.4f),
  LANDMARK_HELD("The Gateway", LM_GATEWAY, 2, 3, 6, 2, 1, 34, "St. Louis, United States", ST_MEADOW, 0.3f, 0.25f)
  LANDMARK("Arc de Triomphe", LM_ARC, 2, 3, 6, 2, 1, 28, "Paris, France", ST_CITY, 0.07f, 0.3f),
  LANDMARK_HELD("Brandenburg Gate", LM_BRANDENBURG, 2, 3, 6, 2, 1, 30, "Berlin, Germany", ST_CITY, 0.3f, 0.3f)
  LANDMARK("Pantheon", LM_PANTHEON, 2, 3, 6, 2, 1, 30, "Rome, Italy", ST_CITY, 0.3f, 0.5f),
  LANDMARK("Notre-Dame", LM_NOTRE_DAME, 2, 3, 6, 2, 1, 32, "Paris, France", ST_CITY, 0.3f, -0.45f),
  LANDMARK_HELD("Red Tower", LM_TOKYO_TOWER, 2, 3, 6, 2, 1, 34, "Tokyo, Japan", ST_CITY, 0.3f, 0.3f)
  LANDMARK_HELD("The Atoms", LM_ATOMIUM, 2, 3, 6, 2, 1, 30, "Brussels, Belgium", ST_CITY, 0.3f, 0.3f)
  LANDMARK_HELD("Great Wall of China", LM_GREAT_WALL, 2, 3, 6, 2, 1, 44, "Beijing, China", ST_HIGHLAND, 0.3f, 0.2f)
  LANDMARK_HELD("Petra Treasury", LM_PETRA, 2, 3, 6, 2, 1, 26, "Petra, Jordan", ST_DESERT, 0.3f, 0.0f)
  LANDMARK("Hagia Sophia", LM_HAGIA_SOPHIA, 2, 3, 6, 2, 1, 34, "Istanbul, Turkey", ST_CITY, 0.3f, 0.5f),
  LANDMARK("Blue Mosque", LM_BLUE_MOSQUE, 1, 3, 6, 2, 1, 34, "Istanbul, Turkey", ST_CITY, 0.3f, 0.5f),
  LANDMARK_HELD("Mont Saint-Michel", LM_MONT_ST_MICHEL, 2, 3, 6, 2, 1, 36, "Normandy, France", ST_HEADLAND, 0.3f, 0.5f)
  LANDMARK("Japanese Castle", LM_JAPANESE_CASTLE, 2, 3, 6, 2, 1, 32, "Japan", ST_MEADOW, 0.03f, 0.4f),
  LANDMARK_HELD("Machu Picchu", LM_MACHU_PICCHU, 2, 3, 6, 2, 1, 34, "Cusco Region, Peru", ST_HIGHLAND, 0.3f, 0.4f)
  LANDMARK("Television Tower", LM_BERLIN_TV, 1, 2, 5, 1, 1, 34, "Berlin, Germany", ST_CITY, 0.3f, 0.3f),
  LANDMARK("Sail Hotel", LM_BURJ_ARAB, 2, 3, 6, 2, 1, 34, "Dubai, United Arab Emirates", ST_HEADLAND, 0.3f, 1.3f),
  LANDMARK_HELD("Neuschwanstein Castle", LM_NEUSCHWANSTEIN, 3, 3, 6, 2, 2, 34, "Bavaria, Germany", ST_HIGHLAND, 0.15f, 0.2f)
  /* LANDMARKS_END */
  /* kit structures held out of the level select (NSHOWN levels are shown): the gates still build and check them, and the bench uses Great Keep */
  { "Two Floors", K_TOWER, 47, 3, 2, 3, { 5, 1, 0 }, 30, 1 },
  { "Glass House", K_SCAFFOLD, 53, 2, 2, 2, { 5, 1, 1 }, 28, 1 },
  { "Mill Bridge", K_BRIDGE, 71, 2, 2, 2, { 5, 1, 1 }, 30, 0 },
  { "The Gatehouse", K_CASTLE, 83, 2, 2, 3, { 5, 1, 1 }, 34, 1 },
  { "Watch Pair", K_WALL, 97, 4, 3, 3, { 5, 2, 1 }, 32, 2 },
  { "Tall Order", K_TOWER, 101, 5, 2, 5, { 6, 1, 1 }, 34, 0 },
  { "Iron Frame", K_SCAFFOLD, 113, 3, 3, 2, { 5, 1, 2 }, 32, 1 },
  { "Sun Steps", K_PYRAMID, 127, 6, 2, 2, { 5, 1, 2 }, 34, 2 },
  { "Old Keep", K_KEEP, 131, 3, 2, 2, { 5, 2, 1 }, 34, 0 },
  { "River Span", K_BRIDGE, 149, 3, 3, 3, { 6, 1, 2 }, 34, 1 },
  { "Hill Fort", K_CASTLE, 151, 3, 3, 2, { 6, 2, 2 }, 38, 2 },
  { "Lantern Tower", K_TOWER, 163, 6, 3, 9, { 6, 2, 2 }, 36, 1 },
  { "Workyard", K_SCAFFOLD, 173, 4, 3, 2, { 6, 2, 2 }, 34, 0 },
  { "Long Rampart", K_WALL, 181, 5, 4, 4, { 7, 2, 2 }, 36, 2 },
  { "Tall Keep", K_KEEP, 191, 4, 3, 3, { 6, 2, 2 }, 38, 1 },
  { "High Steps", K_PYRAMID, 199, 7, 3, 5, { 6, 2, 2 }, 36, 0 },
  { "Viaduct", K_BRIDGE, 211, 4, 4, 4, { 7, 2, 2 }, 38, 2 },
  { "Twin Gates", K_CASTLE, 223, 4, 4, 4, { 7, 2, 2 }, 40, 0 },
  { "Sky Frame", K_SCAFFOLD, 227, 5, 4, 3, { 7, 2, 2 }, 36, 1 },
  { "Stone Giant", K_TOWER, 239, 6, 3, 5, { 7, 2, 2 }, 40, 2 },
  { "Great Keep", K_KEEP, 241, 5, 4, 4, { 7, 2, 3 }, 40, 0 },
  { "Citadel", K_CASTLE, 251, 5, 5, 6, { 8, 2, 4 }, 44, 1 },
  { "Grand Steps", K_PYRAMID, 263, 8, 4, 7, { 7, 2, 3 }, 40, 2 },
  { "Iron Bridge", K_BRIDGE, 271, 5, 5, 4, { 8, 2, 3 }, 42, 0 },
  { "Glass Works", K_SCAFFOLD, 277, 6, 5, 4, { 8, 2, 3 }, 40, 1 },
  { "The Last Fort", K_CASTLE, 283, 6, 6, 3, { 9, 3, 3 }, 46, 2 },
};

/* the share of the structure that counts as brought down (game_mark), by kit: each kit has a level the bot (which
 * aims at the crystals, not at a collapse) brings down this far within par (tests/destructo-levels.mjs) */
const float KIT_MARK[K_COUNT] = { [K_TOWER] = 0.22f, [K_WALL] = 0.35f, [K_BRIDGE] = 0.2f, [K_CASTLE] = 0.25f, [K_SCAFFOLD] = 0.5f, [K_PYRAMID] = 0.15f, [K_KEEP] = 0.4f,
                                  [K_LANDMARK] = 0.3f /* each landmark has its own (level_def.mark) */ };

int level_find(const char *name) {
  for (int i = 0; i < NLEVELS; i++) if (!strcmp(LEVELS[i].name, name)) return i;
  return -1;
}
int level_setting(const level_def *L) { return L->setting ? L->setting - 1 : L->kind; }
int level_settle_ticks(const level_def *L) { return L->kind == K_LANDMARK ? 330 : 180; }

const level_def *level_get(int i) {
  if (i < 0) i = 0;
  if (i >= NLEVELS) i = NLEVELS - 1;
  return &LEVELS[i];
}

#define BH 0.30f  /* a brick's height */
#define BD 0.30f  /* its depth (the wall's thickness) */
#define BL 0.60f  /* its usual length */
#define GAP 0.004f
#define PLANK_T 0.10f
#define PLANK_W 0.40f

static uint32_t rnd_state;
static float rnd(void) { rnd_state = rnd_state * 1664525u + 1013904223u; return (float)(rnd_state >> 8) / 16777216.0f; }
static int rndi(int n) { return (int)(rnd() * (float)n) % n; }

static t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }

/* one course of a straight wall along x (or z when along_z) from a to b at the other coordinate c, its bottom at y.
 * window: glass bricks in the middle of the course */
static void course(game *g, int mat, bool along_z, float a, float b, float c, float y, float depth, bool window, int win_every) {
  float len = b - a;
  int n = (int)(len / BL + 0.5f);
  if (n < 1) n = 1;
  float bl = len / (float)n;
  for (int i = 0; i < n; i++) {
    float m = a + bl * (i + 0.5f);
    int mm = mat;
    if (window && i > 0 && i < n - 1 && win_every > 0 && (i % win_every) == win_every / 2) mm = M_GLASS;
    t3_vec3 h = along_z ? v3(depth * 0.5f - GAP, BH * 0.5f - GAP * 0.5f, bl * 0.5f - GAP) : v3(bl * 0.5f - GAP, BH * 0.5f - GAP * 0.5f, depth * 0.5f - GAP);
    t3_vec3 p = along_z ? v3(c, y + BH * 0.5f, m) : v3(m, y + BH * 0.5f, c);
    game_add_block(g, mm, p, h, 0);
  }
}

/* a course with a gap from g0 to g1 (a gate): the two pieces either side */
static void course_gap(game *g, int mat, bool along_z, float a, float b, float c, float y, float g0, float g1) {
  if (g0 - a > 0.25f) course(g, mat, along_z, a, g0, c, y, BD, false, 0);
  if (b - g1 > 0.25f) course(g, mat, along_z, g1, b, c, y, BD, false, 0);
}

/* a ring of four walls round (cx, cz), outer size w x d, courses from y; returns the top. Each course gives the
 * corners to the x walls or the z walls in turn, so the corners interlock. windows: courses with glass */
static float room_from(game *g, int mat, float cx, float cz, float w, float d, float y, int courses, int win_lo, int win_hi, int win_every, int c0) {
  for (int c = 0; c < courses; c++) {
    bool xo = ((c + c0) & 1) == 0, win = c >= win_lo && c < win_hi;
    float yy = y + c * BH;
    if (xo) {
      course(g, mat, false, cx - w * 0.5f, cx + w * 0.5f, cz - d * 0.5f + BD * 0.5f, yy, BD, win, win_every);
      course(g, mat, false, cx - w * 0.5f, cx + w * 0.5f, cz + d * 0.5f - BD * 0.5f, yy, BD, win, win_every);
      course(g, mat, true, cz - d * 0.5f + BD, cz + d * 0.5f - BD, cx - w * 0.5f + BD * 0.5f, yy, BD, win, win_every);
      course(g, mat, true, cz - d * 0.5f + BD, cz + d * 0.5f - BD, cx + w * 0.5f - BD * 0.5f, yy, BD, win, win_every);
    } else {
      course(g, mat, true, cz - d * 0.5f, cz + d * 0.5f, cx - w * 0.5f + BD * 0.5f, yy, BD, win, win_every);
      course(g, mat, true, cz - d * 0.5f, cz + d * 0.5f, cx + w * 0.5f - BD * 0.5f, yy, BD, win, win_every);
      course(g, mat, false, cx - w * 0.5f + BD, cx + w * 0.5f - BD, cz - d * 0.5f + BD * 0.5f, yy, BD, win, win_every);
      course(g, mat, false, cx - w * 0.5f + BD, cx + w * 0.5f - BD, cz + d * 0.5f - BD * 0.5f, yy, BD, win, win_every);
    }
  }
  return y + courses * BH;
}
static float room(game *g, int mat, float cx, float cz, float w, float d, float y, int courses, int win_lo, int win_hi, int win_every) {
  return room_from(g, mat, cx, cz, w, d, y, courses, win_lo, win_hi, win_every, 0);
}

/* planks along x covering w x d round (cx, cz), their bottom at y; returns the top */
static float floor_planks(game *g, int mat, float cx, float cz, float w, float d, float y) {
  int n = (int)(d / PLANK_W + 0.5f);
  if (n < 1) n = 1;
  float pw = d / (float)n;
  for (int i = 0; i < n; i++)
    game_add_block(g, mat, v3(cx, y + PLANK_T * 0.5f, cz - d * 0.5f + pw * (i + 0.5f)), v3(w * 0.5f - GAP, PLANK_T * 0.5f - GAP * 0.5f, pw * 0.5f - GAP), 0);
  return y + PLANK_T;
}

/* battlements: every other brick of one course round the top */
static void battlements(game *g, int mat, float cx, float cz, float w, float d, float y) {
  int nx = (int)(w / BL + 0.5f), nz = (int)(d / BL + 0.5f);
  float bx = w / nx, bz = (d - 2 * BD) / (nz - 2 > 0 ? nz - 2 : 1);
  for (int i = 0; i < nx; i += 2) {
    float m = cx - w * 0.5f + bx * (i + 0.5f);
    game_add_block(g, mat, v3(m, y + BH * 0.5f, cz - d * 0.5f + BD * 0.5f), v3(bx * 0.5f - GAP, BH * 0.5f - GAP, BD * 0.5f - GAP), 0);
    game_add_block(g, mat, v3(m, y + BH * 0.5f, cz + d * 0.5f - BD * 0.5f), v3(bx * 0.5f - GAP, BH * 0.5f - GAP, BD * 0.5f - GAP), 0);
  }
  int kz = nz - 2 > 0 ? nz - 2 : 1;
  for (int i = 1; i < kz; i += 2) {
    float m = cz - d * 0.5f + BD + bz * (i + 0.5f);
    game_add_block(g, mat, v3(cx - w * 0.5f + BD * 0.5f, y + BH * 0.5f, m), v3(BD * 0.5f - GAP, BH * 0.5f - GAP, bz * 0.5f - GAP), 0);
    game_add_block(g, mat, v3(cx + w * 0.5f - BD * 0.5f, y + BH * 0.5f, m), v3(BD * 0.5f - GAP, BH * 0.5f - GAP, bz * 0.5f - GAP), 0);
  }
}

/* a crate: a wooden box made of one block (the crystals stand on stacks of them) */
static float crate(game *g, float x, float z, float y, float s) {
  game_add_block(g, M_WOOD, v3(x, y + s * 0.5f, z), v3(s * 0.5f - GAP, s * 0.5f - GAP, s * 0.5f - GAP), 0);
  return y + s;
}

static void target_on(game *g, float x, float z, float y) { game_add_target(g, v3(x, y + 0.32f + GAP, z)); }

static void paint(game *g, int p) { g->paint_now = (uint8_t)p; }

/* the index of the block made last (a banner's carrier) */
static int last_block(const game *g) {
  int best = -1, born = -1;
  for (int i = 0; i < g->nb; i++) if (g->b[i].alive && g->b[i].born >= born) best = i, born = g->b[i].born;
  return best;
}

/* a stepped roof from width w up to a point, a gilded finial on top; returns the finial's block. eaves: how far the
 * first step reaches past the walls. A wide step is a ring of four boards, rim wide (each course hands the corners
 * to the other pair, as the walls do), so the roof weighs a third of solid slabs and a tall tower can carry one; the
 * small steps at the top are solid */
#define ROOF_RIM 0.5f
static int roof(game *g, float cx, float cz, float w, float y, float step_in, float slab_h, int roof_paint, float eaves) {
  paint(g, roof_paint);
  float ww = w + eaves * 2;
  if (step_in > ROOF_RIM * 0.36f) step_in = ROOF_RIM * 0.36f; /* each board's middle stays over the ring below it */
  int c = 0;
  while (ww > 0.55f) {
    float hy = slab_h * 0.5f - GAP * 0.5f, yc = y + slab_h * 0.5f, hw = ww * 0.5f;
    if (ww < ROOF_RIM * 2 + 0.5f) game_add_block(g, M_WOOD, v3(cx, yc, cz), v3(hw - GAP, hy, hw - GAP), 0);
    else {
      float r = ROOF_RIM * 0.5f;
      bool xo = (c & 1) == 0; /* this ring's x boards run the full width, the z boards fit between them */
      float lx = xo ? hw : hw - ROOF_RIM, lz = xo ? hw - ROOF_RIM : hw;
      for (int k = -1; k <= 1; k += 2) {
        game_add_block(g, M_WOOD, v3(cx, yc, cz + k * (hw - r)), v3(lx - GAP, hy, r - GAP), 0);
        game_add_block(g, M_WOOD, v3(cx + k * (hw - r), yc, cz), v3(r - GAP, hy, lz - GAP), 0);
      }
    }
    y += slab_h;
    ww -= step_in * 2;
    c++;
  }
  paint(g, P_GOLD);
  game_add_block(g, M_WOOD, v3(cx, y + 0.2f, cz), v3(0.12f, 0.2f - GAP, 0.12f), 0);
  int fin = last_block(g);
  paint(g, P_NONE);
  return fin;
}

/* a banner on the block made last, its pole standing on the block's top */
static void banner_on_last(game *g, float h, int colour) {
  int b = last_block(g);
  if (b >= 0) game_add_banner(g, b, v3(0, g->b[b].half.y, 0), h, colour);
}

/* a storeyed square tower; returns the top. targets: put on the top floors first */
/* the look a structure's towers share (each kit sets it) */
static struct { int wall, trim, base, roof, banner; bool timber, tall_roof; } style;

static float tower(game *g, float cx, float cz, float w, int storeys, int courses, int *targets_left, int mat, bool crown) {
  float y = 0;
  float tops[16];
  int nt = 0;
  bool roofed = crown && rnd() < 0.6f && storeys <= 4; /* a heavy roof on a tall tower sways for ever: tall ones stay crenellated */
  if (crown && style.tall_roof && storeys > 4) roofed = true; /* the lone tower kit's tall ones carry a ring roof on their top floor */
  for (int s = 0; s < storeys; s++) {
    bool glassy = s > 0 && rnd() < 0.8f;
    bool timber = style.timber && s == storeys - 1 && storeys > 2;
    for (int c = 0; c < courses; c++) {
      /* the base course dark, the course under each floor a trim; a timber-framed top storey: plaster between
       * timber bands */
      int m = mat, pt = style.wall;
      if (s == 0 && c == 0) pt = style.base;
      if (c == courses - 1) pt = style.trim;
      if (timber) pt = (c & 1) ? P_PLASTER : P_TIMBER; /* the look only: a wall of mixed weights rocks */
      paint(g, pt);
      bool win = glassy && (c == 2 || c == 3);
      room_from(g, m, cx, cz, w, w, y + c * BH, 1, win ? 0 : 99, 1, 3 + rndi(2), c);
    }
    y += courses * BH;
    paint(g, P_NONE);
    y = floor_planks(g, M_WOOD, cx, cz, w, w, y);
    if (nt < 16) tops[nt++] = y;
  }
  if (crown && !roofed) {
    paint(g, style.trim);
    battlements(g, mat, cx, cz, w, w, y);
    banner_on_last(g, 1.6f + w * 0.2f, style.banner);
    paint(g, P_NONE);
  } else if (roofed) {
    /* an attic of three courses (the top floor's crystal stands under it), then the roof; a tall tower's roof stands
     * on its top floor (every course more on a six-storey tower is a lean that creeps for a minute: levelcheck) */
    int attic = storeys > 4 ? 0 : 3;
    for (int c = 0; c < attic; c++) {
      paint(g, c == 2 ? style.trim : style.wall);
      room_from(g, mat, cx, cz, w, w, y + c * BH, 1, c == 1 ? 0 : 99, 1, 2, c);
    }
    y += attic * BH;
    paint(g, P_NONE);
    int fin = roof(g, cx, cz, w, y, 0.18f + rnd() * 0.12f, 0.22f, style.roof, 0.12f);
    if (fin >= 0) game_add_banner(g, fin, v3(0, g->b[fin].half.y, 0), 1.2f, style.banner);
  }
  /* crystals: the highest floors first, then lower ones */
  for (int k = nt - 1; k >= 0 && *targets_left > 0; k -= (nt > 2 ? 2 : 1)) {
    float off = (w * 0.5f - BD - 0.4f) * (rnd() - 0.5f);
    target_on(g, cx + off, cz + (rnd() - 0.5f) * 0.4f, tops[k]);
    (*targets_left)--;
  }
  return y;
}

static void build_tower(game *g, const level_def *L) {
  int t = L->targets;
  style.wall = P_NONE, style.trim = P_TRIM_CREAM, style.base = P_DARK_STONE, style.roof = rnd() < 0.5f ? P_ROOF_RED : P_ROOF_SLATE;
  style.banner = rndi(4), style.timber = true, style.tall_roof = true;
  float w = 3.0f + (L->size > 4 ? 1.2f : 0.6f);
  tower(g, 0, 0, w, L->size, 6, &t, M_STONE, true);
  /* any crystals left over stand on crates beside it */
  for (int i = 0; t > 0; i++, t--) {
    float x = (i & 1 ? 1 : -1) * (w * 0.5f + 1.4f), z = 0.5f;
    float y = crate(g, x, z, 0, 0.9f);
    target_on(g, x, z, y);
  }
}

static void build_wall(game *g, const level_def *L) {
  int t = L->targets;
  style.wall = P_NONE, style.trim = P_TRIM_CREAM, style.base = P_DARK_STONE, style.roof = P_ROOF_RED, style.banner = rndi(4), style.timber = false;
  float len = 6.0f + L->size * 1.8f;
  int courses = 4 + L->size;
  /* the wall, two bricks thick so it stands on its own */
  for (int c = 0; c < courses; c++) {
    float y = c * BH, sh = (c & 1) ? BL * 0.5f : 0;
    paint(g, c == 0 ? P_DARK_STONE : c == courses - 1 ? P_TRIM_CREAM : P_NONE);
    course(g, M_STONE, false, -len * 0.5f + sh, len * 0.5f + sh - BL * ((c & 1) ? 1 : 0), -BD * 0.5f, y, BD, c == courses - 2, 4);
    course(g, M_STONE, false, -len * 0.5f + BL * 0.5f - sh, len * 0.5f - sh - BL * 0.5f + ((c & 1) ? BL * 0.5f : 0), BD * 0.5f, y, BD, false, 0);
  }
  paint(g, P_NONE);
  /* towers at the ends */
  int none = 0;
  float tw = 2.4f;
  tower(g, -len * 0.5f - tw * 0.5f - 0.1f, 0, tw, 2 + L->size / 3, 5, &none, M_STONE, true);
  tower(g, len * 0.5f + tw * 0.5f + 0.1f, 0, tw, 2 + L->size / 3, 5, &none, M_STONE, true);
  /* crystals behind the wall on crates, the crates in stacks */
  for (int i = 0; i < t; i++) {
    float x = -len * 0.35f + (len * 0.7f) * (t > 1 ? (float)i / (float)(t - 1) : 0.5f);
    float z = -1.6f - rnd() * 1.2f;
    float y = 0;
    int h = 1 + rndi(2) + (L->size > 3);
    for (int k = 0; k < h; k++) y = crate(g, x, z, y, 0.8f - k * 0.08f);
    target_on(g, x, z, y);
  }
}

static void build_bridge(game *g, const level_def *L) {
  int t = L->targets;
  style.banner = rndi(4);
  int spans = 1 + L->size / 2;
  float span = 4.0f, pier = 1.2f, ph = 1.8f + 0.3f * L->size;
  float total = spans * span + (spans + 1) * pier;
  float x0 = -total * 0.5f;
  int courses = (int)(ph / BH);
  /* piers: square stacks of big stone blocks, two per course turned in turn */
  for (int p = 0; p <= spans; p++) {
    float cx = x0 + pier * 0.5f + p * (span + pier);
    for (int c = 0; c < courses; c++) {
      float y = c * BH;
      paint(g, c == courses - 1 ? P_TRIM_CREAM : (c & 1) ? P_BRICK : P_ROSE_STONE);
      for (int k = 0; k < 2; k++) {
        if (c & 1) game_add_block(g, M_STONE, v3(cx - pier * 0.25f + k * pier * 0.5f, y + BH * 0.5f, 0), v3(pier * 0.25f - GAP, BH * 0.5f - GAP * 0.5f, 1.3f - GAP), 0);
        else game_add_block(g, M_STONE, v3(cx, y + BH * 0.5f, -0.65f + k * 1.3f), v3(pier * 0.5f - GAP, BH * 0.5f - GAP * 0.5f, 0.65f - GAP), 0);
      }
    }
  }
  paint(g, P_NONE);
  float y = courses * BH;
  /* girders across the whole length (steel), two lines of them, one per span plus the piers' tops */
  for (int s = 0; s < spans; s++) {
    /* each girder runs from pier middle to pier middle; the end ones out to the end piers' outer faces */
    float a = s == 0 ? x0 : x0 + pier * 0.5f + s * (span + pier);
    float b = s == spans - 1 ? x0 + total : x0 + pier * 0.5f + (s + 1) * (span + pier);
    for (int k = 0; k < 2; k++) game_add_block(g, M_STEEL, v3((a + b) * 0.5f, y + 0.12f, -0.9f + k * 1.8f), v3((b - a) * 0.5f - 0.01f, 0.12f - GAP, 0.1f - GAP), 0);
  }
  y += 0.24f;
  /* the deck: planks across (along z) */
  int np = (int)(total / PLANK_W);
  float pw = total / np;
  for (int i = 0; i < np; i++)
    game_add_block(g, M_WOOD, v3(x0 + pw * (i + 0.5f), y + PLANK_T * 0.5f, 0), v3(pw * 0.5f - GAP, PLANK_T * 0.5f - GAP * 0.5f, 1.25f), 0);
  y += PLANK_T;
  /* posts along the edges with glass panes between some */
  for (int i = 0; i < np; i += 3) {
    float x = x0 + pw * (i + 0.5f);
    for (int k = 0; k < 2; k++) {
      paint(g, P_TIMBER);
      game_add_block(g, M_WOOD, v3(x, y + 0.4f, -1.1f + k * 2.2f), v3(0.08f, 0.4f - GAP, 0.08f), 0);
      if (i == 0 || i + 3 >= np) banner_on_last(g, 1.4f, style.banner);
    }
    paint(g, P_NONE);
  }
  /* crystals on the deck, spread along it */
  for (int i = 0; i < t; i++) {
    float x = x0 + total * (i + 0.5f) / (float)t + (rnd() - 0.5f) * 0.6f;
    float z = (rnd() - 0.5f) * 0.8f;
    float yy = y;
    if (rnd() < 0.5f) yy = crate(g, x, z, yy, 0.7f);
    target_on(g, x, z, yy);
  }
}

static void build_castle(game *g, const level_def *L) {
  int t = L->targets;
  style.wall = P_WHITE_STONE, style.trim = P_TRIM_CREAM, style.base = P_DARK_STONE, style.roof = rnd() < 0.5f ? P_ROOF_SLATE : P_ROOF_COPPER;
  style.banner = rndi(4), style.timber = false;
  float side = 7.0f + L->size * 1.2f, tw = 2.4f;
  int st = 2 + (L->size > 3);
  int none = 0;
  for (int k = 0; k < 4; k++) {
    float x = (k & 1 ? 1 : -1) * side * 0.5f, z = (k & 2 ? 1 : -1) * side * 0.5f;
    tower(g, x, z, tw, st, 5, &none, M_STONE, true);
  }
  /* curtain walls between the towers: single courses, ends tucked against the towers */
  int courses = 6 + L->size;
  float a = -side * 0.5f + tw * 0.5f + 0.02f, b = side * 0.5f - tw * 0.5f - 0.02f;
  int gate_h = 5; /* the gate: five courses high, two bricks wide, a steel lintel over it */
  for (int c = 0; c < courses; c++) {
    float y = c * BH;
    bool win = c == courses - 3;
    paint(g, c == 0 ? P_DARK_STONE : c == courses - 1 ? P_TRIM_CREAM : P_WHITE_STONE);
    course(g, M_STONE, false, a, b, -side * 0.5f, y, BD, win, 3);
    if (c < gate_h) course_gap(g, M_STONE, false, a, b, side * 0.5f, y, -0.75f, 0.75f);
    else if (c == gate_h) {
      paint(g, P_DARK_STONE);
      game_add_block(g, M_STEEL, v3(0, y + BH * 0.5f, side * 0.5f), v3(1.35f, BH * 0.5f - GAP * 0.5f, BD * 0.5f - GAP), 0);
      paint(g, P_WHITE_STONE);
      course_gap(g, M_STONE, false, a, b, side * 0.5f, y, -1.35f, 1.35f);
    } else course(g, M_STONE, false, a, b, side * 0.5f, y, BD, false, 0);
    course(g, M_STONE, true, a, b, -side * 0.5f, y, BD, false, 0);
    course(g, M_STONE, true, a, b, side * 0.5f, y, BD, false, 0);
  }
  paint(g, P_NONE);
  /* the keep: a wooden-floored tower in the middle with the crystals */
  int keep_t = t > 1 ? t - 1 : t;
  tower(g, 0, 0, 3.0f, 2 + L->size / 2, 6, &keep_t, M_STONE, true);
  int rest = t - (t > 1 ? t - 1 : t) + keep_t;
  for (int i = 0; i < rest; i++) {
    float a = 0.5f + i * 1.9f, x = cw_sin(a) * side * 0.27f, z = cw_cos(a) * side * 0.27f; /* round the keep, apart */
    float y = crate(g, x, z, 0, 0.8f);
    target_on(g, x, z, y);
  }
}

static void build_scaffold(game *g, const level_def *L) {
  int t = L->targets;
  /* tall frames of loose posts creep (physcheck's thin column), and Box3D's welds will not hold a frame together
   * (a rigid weld loop oscillates apart within a second, a soft one sags): so the frame grows wider, not taller */
  int storeys = 2 + (L->size > 2) + (L->size > 4);
  int bays = 1 + L->size / 2 + (L->size > 4);
  style.banner = rndi(4);
  if (bays > 7) bays = 7;
  float bay = 2.2f, sh = 1.6f, depth = 2.4f, ph = 0.12f;
  float w = bays * bay;
  float y = 0;
  float tops[12];
  int nt = 0;
  float floor_y = 0; /* where this storey's panes and crystals stand: the ground, then each storey's planks */
  for (int s = 0; s < storeys && s < 12; s++) {
    /* steel posts at every bay line, front and back, standing on the ground or on the beam ends below */
    for (int i = 0; i <= bays; i++)
      for (int k = 0; k < 2; k++)
        game_add_block(g, M_STEEL, v3(-w * 0.5f + i * bay, y + sh * 0.5f, -depth * 0.5f + k * depth), v3(ph, sh * 0.5f - GAP, ph), 0);
    /* glass cladding between the front posts: chunky panes standing on the floor */
    for (int i = 0; i < bays; i++)
      if (((s + i) & 1) == 0)
        game_add_block(g, M_GLASS, v3(-w * 0.5f + (i + 0.5f) * bay, floor_y + 0.5f, -depth * 0.5f + 0.34f), v3(bay * 0.5f - 0.3f, 0.5f - GAP, 0.07f), 0);
    y += sh;
    /* beams along x, one per bay from post middle to post middle (a beam on three posts teeters on the middle
     * one), the end ones out to the outer posts' faces */
    for (int k = 0; k < 2; k++)
      for (int i = 0; i < bays; i++) {
        float a = -w * 0.5f + i * bay - (i == 0 ? ph : 0), b = -w * 0.5f + (i + 1) * bay + (i == bays - 1 ? ph : 0);
        game_add_block(g, M_STEEL, v3((a + b) * 0.5f, y + 0.08f, -depth * 0.5f + k * depth), v3((b - a) * 0.5f - GAP, 0.08f - GAP, ph), 0);
      }
    y += 0.16f;
    /* planks across (along z), resting on the beams in each bay, clear of the post lines (the posts above stand
     * on the beams themselves) */
    for (int i = 0; i < bays; i++) {
      float a = -w * 0.5f + i * bay + ph + 0.02f, b = -w * 0.5f + (i + 1) * bay - ph - 0.02f;
      int np = (int)((b - a) / PLANK_W + 0.5f);
      float pw = (b - a) / np;
      for (int j = 0; j < np; j++)
        game_add_block(g, M_WOOD, v3(a + pw * (j + 0.5f), y + 0.05f, 0), v3(pw * 0.5f - GAP, 0.05f - GAP * 0.5f, depth * 0.5f + ph - GAP), 0);
    }
    floor_y = y + 0.1f;
    tops[nt++] = floor_y;
  }
  /* banners on the top storey's corner posts */
  for (int i = 0; i < g->nb; i++)
    if (g->b[i].alive && g->b[i].mat == M_STEEL && g->b[i].half.y > 0.5f && g->b[i].home.y > y - 2.5f && fabsf(fabsf(g->b[i].home.x) - w * 0.5f) < 0.05f)
      game_add_banner(g, i, v3(0, g->b[i].half.y + 0.17f, 0), 1.0f, style.banner);
  for (int k = nt - 1, i = 0; i < t; i++) {
    target_on(g, -w * 0.5f + bay * (0.5f + (i % bays)) + (rnd() - 0.5f) * 0.5f, (rnd() - 0.5f) * 0.6f, tops[k]);
    k -= 1;
    if (k < 0) k = nt - 1;
  }
}

static void build_pyramid(game *g, const level_def *L) {
  int t = L->targets;
  int steps = L->size;
  float s = 1.0f; /* block size */
  float y = 0;
  float top_w = 0;
  for (int lv = 0; lv < steps; lv++) {
    int n = steps - lv + 2;
    float w = n * s;
    /* an outer ring of big blocks (stone), the inside filled with wood crates and glass now and then */
    for (int i = 0; i < n; i++)
      for (int j = 0; j < n; j++) {
        bool edge = i == 0 || j == 0 || i == n - 1 || j == n - 1;
        int mat = edge ? M_STONE : (rnd() < 0.2f ? M_GLASS : M_WOOD);
        paint(g, edge ? ((lv & 1) ? P_TURQUOISE : P_SAND_STONE) : P_NONE);
        game_add_block(g, mat, v3(-w * 0.5f + s * (i + 0.5f), y + s * 0.25f, -w * 0.5f + s * (j + 0.5f)), v3(s * 0.5f - GAP, s * 0.25f - GAP * 0.5f, s * 0.5f - GAP), 0);
      }
    y += s * 0.5f;
    top_w = w;
  }
  /* the shrine on top: four posts, a roof stepping up to a gilded point, banners on two posts */
  float hw = top_w * 0.5f - 0.3f;
  paint(g, P_TURQUOISE);
  for (int k = 0; k < 4; k++)
    game_add_block(g, M_WOOD, v3((k & 1 ? 1 : -1) * hw, y + 0.6f, (k & 2 ? 1 : -1) * hw), v3(0.12f, 0.6f - GAP, 0.12f), 0);
  paint(g, P_GOLD);
  game_add_block(g, M_WOOD, v3(0, y + 1.2f + 0.06f, 0), v3(hw + 0.25f, 0.06f - GAP, hw + 0.25f), 0);
  roof(g, 0, 0, hw * 2 + 0.2f, y + 1.32f, 0.2f, 0.18f, P_GOLD, 0);
  paint(g, P_NONE);
  /* crystals: the shrine first, then the steps' corners */
  if (t > 0) target_on(g, 0, 0, y), t--;
  for (int i = 0; t > 0; i++, t--) {
    int lv = i % (steps > 1 ? steps - 1 : 1);
    int n = steps - lv + 2;
    float w = n * s;
    float yy = (lv + 1) * s * 0.5f;
    float x = (i & 1 ? 1 : -1) * (w * 0.5f - s * 0.5f - 0.0f);
    float n2 = ((steps - lv - 1) + 2) * s * 0.5f; /* the next step's half width: stand outside it */
    float z = (i & 2 ? 1 : -1) * (n2 + (w * 0.5f - n2) * 0.5f);
    if (fabsf(z) < n2 + 0.25f) z = (i & 2 ? 1 : -1) * (n2 + 0.3f);
    target_on(g, x * 0.5f, z, yy);
  }
}

static void build_keep(game *g, const level_def *L) {
  int t = L->targets;
  float w = 5.4f + 0.3f * L->size;
  style.banner = rndi(4);
  float y = 0;
  float tops[12];
  int nt = 0;
  for (int s = 0; s < L->size; s++) {
    /* storeys of stone with a band of steel courses (an iron ring), the top storey timber */
    for (int c = 0; c < 6; c++) {
      int mat = s == L->size - 1 && s > 1 ? M_WOOD : (c == 4 && (s & 1) == 0) ? M_STEEL : M_STONE;
      float yc = y;
      paint(g, mat == M_WOOD ? ((c & 1) ? P_PLASTER : P_TIMBER) : c == 5 ? P_WHITE_STONE : P_DARK_STONE);
      y = room_from(g, mat, 0, 0, w, w, y, 1, c == 2 || c == 3 ? 0 : 99, 1, 3, c);
      /* a cross wall down the middle: the floors span half the keep (a 5 m plank on two walls rocks forever) */
      course(g, M_STONE, true, -w * 0.5f + BD + 0.01f, w * 0.5f - BD - 0.01f, 0, yc, BD, false, 0);
    }
    paint(g, P_NONE);
    /* two floors of planks meeting on the cross wall: a plank on three supports teeters on the middle one */
    floor_planks(g, M_WOOD, -w * 0.25f, 0, w * 0.5f, w, y);
    y = floor_planks(g, M_WOOD, w * 0.25f, 0, w * 0.5f, w, y);
    if (nt < 12) tops[nt++] = y;
  }
  paint(g, P_WHITE_STONE);
  battlements(g, M_STONE, 0, 0, w, w, y);
  banner_on_last(g, 2.2f, style.banner);
  paint(g, P_NONE);
  for (int k = nt - 1, i = 0; i < t; i++) {
    target_on(g, (i & 1 ? 1 : -1) * w * 0.2f, (rnd() - 0.5f) * 0.6f, tops[k]);
    if (--k < 0) k = nt - 1;
  }
}

/* powder kegs against the structure's foot (from the eighth level, every third level): a shot that finds one sets
 * off a blast, and a blast that reaches the next sets that off too. Each stands on the ground where nothing else is,
 * within a block's reach of the structure, on the launcher's half so it can be seen */
static void place_kegs(game *g, const level_def *L, int n) {
  float ext = 3;
  for (int i = 0; i < g->nb; i++)
    if (g->b[i].alive) { t3_vec3 p = g->b[i].home; float r = sqrtf(p.x * p.x + p.z * p.z); if (r > ext) ext = r; }
  int placed = 0;
  t3_vec3 last = { 1e9f, 0, 1e9f };
  for (int k = 0; k < 400 && placed < n; k++) {
    float a = (hashf(L->seed * 977u + (uint32_t)k * 31u) - 0.5f) * 2.4f; /* toward the launcher (+z) */
    float r = ext * (0.35f + 0.75f * hashf(L->seed * 131u + (uint32_t)k * 17u));
    t3_vec3 c = { r * sinf(a), KEG_HALF + 0.004f, r * cosf(a) };
    if (placed > 0) { /* the next keg near the last: a chain */
      float dx = c.x - last.x, dz = c.z - last.z;
      if (dx * dx + dz * dz > 2.8f * 2.8f) continue;
    }
    cw_qshape3d q = cw_qshape3d_box(c, v3(KEG_HALF * 0.85f + 0.06f, KEG_HALF - 0.02f, KEG_HALF * 0.85f + 0.06f), (t3_quat){ 0, 0, 0, 1 });
    cw_phys_filter f = cw_phys_filter_layers(1u << 1);
    cw_shape3d hit[1];
    if (cw_world3d_overlap(g->w, &q, &f, hit, 1) > 0) continue; /* something is there */
    cw_qshape3d near = cw_qshape3d_box(c, v3(KEG_HALF + 0.9f, KEG_HALF, KEG_HALF + 0.9f), (t3_quat){ 0, 0, 0, 1 });
    if (cw_world3d_overlap(g->w, &near, &f, hit, 1) == 0) continue; /* out in the open: not against the structure */
    if (game_add_keg(g, c.x, c.z) >= 0) placed++, last = c;
  }
}

void level_build(game *g, const level_def *L) {
  rnd_state = L->seed * 2654435761u + 12345u;
  style.wall = P_NONE, style.trim = P_TRIM_CREAM, style.base = P_DARK_STONE, style.roof = P_ROOF_RED, style.banner = 0, style.timber = false, style.tall_roof = false;
  g->paint_now = P_NONE;
  switch (L->kind) {
  case K_TOWER: build_tower(g, L); break;
  case K_WALL: build_wall(g, L); break;
  case K_BRIDGE: build_bridge(g, L); break;
  case K_CASTLE: build_castle(g, L); break;
  case K_SCAFFOLD: build_scaffold(g, L); break;
  case K_PYRAMID: build_pyramid(g, L); break;
  case K_KEEP: build_keep(g, L); break;
  case K_LANDMARK: landmark_build(g, L); return;
  default: build_tower(g, L); break;
  }
  /* the kits' levels in their own order (the landmarks come first) */
  int li = 0;
  for (const level_def *k = LEVELS; k < L; k++) li += k->kind != K_LANDMARK;
  if (li >= 7 && li % 3 == 1) place_kegs(g, L, li >= 19 ? 3 : 2);
}
