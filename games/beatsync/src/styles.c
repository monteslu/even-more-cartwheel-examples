/* The styles (styles.h): the table, the scheduler and the blend.
 *
 * Every fade runs from one style (from) to the next (cur) with t from 0 to 1, eased (smoothstep), through a flash
 * meter (fl.style: the flash check's `flash_strobe` control strobes it). What fades:
 *   - the ASCII pass's share of the picture (ascii_set_mix: the characters thin out as the plain scene shows through);
 *   - each backdrop's weight (backdrop.c), the old one's falling as the new one's rises;
 *   - the field's colours and params (club_set_style: the palette, the land, the roads, the sky);
 *   - the bloom.
 * A fade never starts while another runs: a change asked for mid-fade waits for it to end. */
#include "styles.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/look.h"
#include "cartwheel/scene_file.h"

#include "ascii.h"
#include "backdrop.h"
#include "club.h"
#include "flash_meter.h"
#include "worlds.h"

typedef struct style_def {
  const char *id, *title;
  float ascii;   /* the characters' share: 1 or 0 */
  int backdrop;  /* BD_* */
  club_style look;
  float bloom, bloom_threshold;
  bool big;      /* what a drop asks for */
  bool held;     /* built but kept out of the rotation and the setting (an engine gap stands in its way; `style <id>` still shows it) */
  int px;        /* PX_*: an engine look over the picture (cartwheel/look.h): 0 none, PX_HALF half blocks */
  bool ssr;      /* the glass roads take sharp reflections of the backdrop too (PostProcess ssr with ssr_backdrop) */
} style_def;

/* each style's GPU cost alone at High on a Radeon 890M at 1080p (ms, GL, romdev frame timing, the mean of 180 frames;
 * the field included, with the worlds that light the field their captures and the roads' sharp reflections): a
 * fade draws two worlds, about the sum less the field (0.4; the club lasers into the rock stage, 8.4 summed, measured
 * 8.1 to 8.5 a fade frame), so the scheduler never fades between two whose costs add up to more than PAIR_BUDGET (the
 * worst fade stays under 8 ms there, tests/beatsync-hitch.mjs; Auto steps down on slower GPUs) */
#define METAL_COST 4.0f
static const float COST[STYLE_COUNT] = { [STYLE_NEON] = 0.37f, [STYLE_CLEAN] = 2.0f, [STYLE_FIRE] = 2.96f, [STYLE_WATER] = 2.86f, [STYLE_GRID] = 2.5f,
  [STYLE_TERMINAL] = 0.37f, [STYLE_LASERS] = 3.08f, [STYLE_STAGE] = 5.33f, [STYLE_HALF] = 1.98f, [STYLE_MAZE] = 1.39f, [STYLE_BIT] = 4.88f,
  [STYLE_AURORA] = 1.63f, [STYLE_SPACE] = 1.79f, [STYLE_CITY] = 1.83f, [STYLE_FOREST] = 1.67f, [STYLE_DEEP] = 2.04f, [STYLE_DESERT] = 1.26f,
  [STYLE_NEWS] = 2.52f, [STYLE_LAVA] = 1.73f, [STYLE_METAL] = METAL_COST };
#define PAIR_BUDGET 7.2f

/* the palettes: each set apart from its backdrop, so the notes read against it (the readability gate measures it) */
#define ICE { { 0.3f, 0.85f, 1 }, { 0.9f, 0.95f, 1 }, { 0.6f, 0.5f, 1 }, { 0.45f, 1, 0.8f } }
#define EMBER { { 1, 0.5f, 0.15f }, { 1, 0.88f, 0.35f }, { 1, 0.3f, 0.6f }, { 0.6f, 1, 0.4f } }
#define SYNTH { { 0.2f, 0.95f, 1 }, { 1, 0.9f, 0.35f }, { 0.55f, 1, 0.35f }, { 1, 0.55f, 0.2f } }
#define PHOSPHOR { { 0.25f, 1, 0.35f }, { 0.65f, 1, 0.25f }, { 0.2f, 1, 0.7f }, { 0.45f, 1, 0.45f } }
enum { PX_NONE, PX_HALF, PX_MAZE, PX_BIT, PX_AMBER, PX_NEWS, PX_LAVA, PX_COUNT };
#define ARCADE { { 1, 0.3f, 0.35f }, { 1, 0.85f, 0.3f }, { 0.3f, 1, 0.5f }, { 0.5f, 0.9f, 1 } }
#define SUNSET { { 1, 0.45f, 0.2f }, { 1, 0.95f, 0.75f }, { 0.35f, 0.7f, 1 }, { 0.5f, 1, 0.5f } }
#define CORAL { { 1, 0.45f, 0.3f }, { 1, 0.88f, 0.4f }, { 1, 0.35f, 0.75f }, { 0.6f, 1, 0.4f } }
#define NEON { { 0.25f, 0.95f, 1 }, { 1, 0.95f, 0.85f }, { 1, 0.3f, 0.75f }, { 1, 0.75f, 0.25f } }
#define GOLD { { 1, 0.75f, 0.25f }, { 1, 0.97f, 0.88f }, { 0.4f, 1, 0.55f }, { 0.4f, 0.8f, 1 } }
#define STAGE { { 1, 0.55f, 0.12f }, { 1, 0.95f, 0.65f }, { 1, 0.28f, 0.35f }, { 0.4f, 0.8f, 1 } }

/* the night sea: the engine's water lit by the world's moonlight with no ProceduralSky (cartwheel-engine 0.63.0) */
#ifndef WATER_HELD
#define WATER_HELD false
#endif

static const style_def STYLES[STYLE_COUNT] = {
  [STYLE_NEON] = { "neon", "Neon characters", 1, BD_NONE,
                   { .land_body = 1, .land_alpha = 1, .sky = 1, .burst = 0.65f, .flow = 1, .note_light = 1, .pocket = 1 }, 0.08f, 1.0f, false },
  [STYLE_CLEAN] = { "clean", "Clean", 0, BD_CLEAN,
                    { .pal = 1, .lane = SUNSET, .land_alpha = 0, .ridge = 0, .plate = 0.82f, .burst = 0.5f, .flow = 1, .note_light = 0.8f, .pocket = 1,
                      .lit = 1, .key = { 2.2f, 1.9f, 1.6f }, .fill_l = { 0.5f, 0.7f, 1.4f }, .fill_r = { 1.6f, 0.9f, 0.5f }, .sweep = 0.2f },
                    0.22f, 0.8f, false },
  [STYLE_FIRE] = { "fire", "Fire", 0, BD_FIRE,
                   { .pal = 1, .lane = ICE, .land_alpha = 0, .ridge = 0, .plate = 0.85f, .burst = 0.5f, .flow = 1, .note_light = 0.8f, .pocket = 1,
                     .lit = 1, .key = { 1.4f, 1.1f, 0.9f }, .fill_l = { 3.2f, 1.1f, 0.25f }, .fill_r = { 3.2f, 1.0f, 0.2f }, .sweep = 0.35f },
                   0.2f, 0.8f, true },
  [STYLE_WATER] = { "water", "Night sea", 0, BD_SEA,
                    { .pal = 1, .lane = EMBER, .land_alpha = 0, .ridge = 0, .plate = 0.8f, .burst = 0.5f, .flow = 1, .note_light = 0.8f, .pocket = 1,
                      .lit = 1, .key = { 1.0f, 1.2f, 1.6f }, .fill_l = { 0.4f, 0.6f, 1.4f }, .fill_r = { 1.2f, 0.7f, 0.5f }, .sweep = 0.15f },
                    0.22f, 0.8f, false, WATER_HELD, 0, true },
  [STYLE_GRID] = { "grid", "Synthwave grid", 0, BD_GRID,
                   { .pal = 1, .lane = SYNTH, .land_tint = 1, .land_rgb = { 0.55f, 0.1f, 0.6f }, .land_body = 0.05f, .land_alpha = 1, .ridge = 0.9f, .land_no_mid = 1, .plate = 0.985f, .burst = 0.5f, .flow = 1, .note_light = 0.8f, .pocket = 1,
                     .lit = 1, .key = { 1.2f, 1.0f, 1.4f }, .fill_l = { 2.4f, 0.4f, 2.0f }, .fill_r = { 0.3f, 1.6f, 2.4f }, .sweep = 0.5f },
                   0.24f, 0.8f, true, false, 0, true },
  [STYLE_TERMINAL] = { "terminal", "Terminal green", 1, BD_NONE,
                       { .pal = 1, .lane = PHOSPHOR, .land_tint = 1, .land_rgb = { 0.3f, 1, 0.4f }, .land_body = 0.36f, .land_alpha = 1, .sky = 1, .burst = 0.65f, .flow = 1, .note_light = 0.8f, .pocket = 1 },
                       0.08f, 1.0f, false },
  [STYLE_LASERS] = { "lasers", "Club lasers", 0, BD_LASERS,
                     { .pal = 1, .lane = STAGE, .land_alpha = 0, .ridge = 0, .plate = 0.85f, .burst = 0.5f, .flow = 1, .note_light = 0.8f, .pocket = 1,
                       .lit = 1, .key = { 1.0f, 1.0f, 1.3f }, .fill_l = { 0.4f, 2.6f, 0.8f }, .fill_r = { 2.6f, 0.4f, 1.8f }, .sweep = 1 },
                     0.24f, 0.8f, true },
  [STYLE_STAGE] = { "stage", "Rock stage", 0, BD_STAGE,
                    { .pal = 1, .lane = ICE, .land_alpha = 0, .ridge = 0, .plate = 0.85f, .burst = 0.5f, .flow = 1, .note_light = 0.8f, .pocket = 1,
                      .lit = 1, .key = { 1.6f, 1.3f, 1.0f }, .fill_l = { 2.6f, 1.3f, 0.5f }, .fill_r = { 0.6f, 0.9f, 2.6f }, .sweep = 0.6f },
                    0.22f, 0.8f, true, false, 0, true },
  [STYLE_HALF] = { "halfblock", "Half blocks", 0, BD_GRID,
                   { .pal = 1, .lane = SYNTH, .land_alpha = 0, .ridge = 0, /* no land: its dark rows riding over the sun, in big pixels, were flashes */ .plate = 0.975f, .burst = 0.5f, .flow = 1, .note_light = 0.9f, .pocket = 1,
                     .lit = 1, .key = { 1.2f, 1.0f, 1.4f }, .fill_l = { 2.4f, 0.4f, 2.0f }, .fill_r = { 0.3f, 1.6f, 2.4f }, .sweep = 0.5f },
                   0.2f, 0.8f, false, false, PX_HALF },
  [STYLE_MAZE] = { "maze", "Arcade maze", 0, BD_MAZE,
                   { .pal = 1, .lane = ARCADE, .land_alpha = 0, .ridge = 0, .plate = 0.92f, .burst = 0.5f, .flow = 1, .note_light = 0.9f, .pocket = 1,
                     .lit = 1, .key = { 1.2f, 1.1f, 1.0f }, .fill_l = { 0.4f, 0.6f, 2.4f }, .fill_r = { 0.4f, 0.6f, 2.4f }, .sweep = 0.3f,
                     .bank_tint = 1, .bank_rgb = { 0.12f, 0.22f, 1.6f } }, /* (no pellets: dots in a river read as notes) */
                   0.3f, 0.7f, true, false, PX_MAZE },
  [STYLE_BIT] = { "onebit", "One bit", 0, BD_STAGE,
                  { .pal = 1, .lane = ICE, .land_alpha = 0, .ridge = 0, .plate = 0.85f, .burst = 0.5f, .flow = 1, .note_light = 0.9f, .pocket = 1,
                    .lit = 1, .key = { 1.6f, 1.3f, 1.0f }, .fill_l = { 2.6f, 1.3f, 0.5f }, .fill_r = { 0.6f, 0.9f, 2.6f }, .sweep = 0.6f },
                  0.2f, 0.8f, false, false, PX_BIT },
  [STYLE_AURORA] = { "aurora", "Aurora", 0, BD_AURORA,
                     { .pal = 1, .lane = CORAL, .land_alpha = 0, .ridge = 0, .plate = 0.85f, .burst = 0.5f, .flow = 1, .note_light = 0.85f, .pocket = 1,
                       .lit = 1, .key = { 0.9f, 1.1f, 1.3f }, .fill_l = { 0.3f, 1.8f, 1.0f }, .fill_r = { 1.2f, 0.4f, 1.8f }, .sweep = 0.2f },
                     0.24f, 0.8f, false },
  [STYLE_SPACE] = { "space", "Deep space", 0, BD_SPACE,
                    { .pal = 1, .lane = GOLD, .land_alpha = 0, .ridge = 0, .plate = 0.88f, .burst = 0.5f, .flow = 1, .note_light = 0.85f, .pocket = 1,
                      .lit = 1, .key = { 1.3f, 1.2f, 1.1f }, .fill_l = { 1.8f, 0.4f, 2.2f }, .fill_r = { 0.3f, 1.4f, 2.2f }, .sweep = 0.3f },
                    0.26f, 0.8f, true },
  [STYLE_CITY] = { "city", "Rain city", 0, BD_CITY,
                   { .pal = 1, .lane = NEON, .land_alpha = 0, .ridge = 0, .plate = 0.88f, .burst = 0.5f, .flow = 1, .note_light = 0.85f, .pocket = 1,
                     .lit = 1, .key = { 0.9f, 0.9f, 1.3f }, .fill_l = { 2.4f, 0.4f, 1.4f }, .fill_r = { 0.3f, 1.6f, 2.4f }, .sweep = 0.25f },
                   0.24f, 0.8f, false },
  [STYLE_FOREST] = { "forest", "Night forest", 0, BD_FOREST,
                     { .pal = 1, .lane = EMBER, .land_alpha = 0, .ridge = 0, .plate = 0.85f, .burst = 0.5f, .flow = 1, .note_light = 0.85f, .pocket = 1,
                       .lit = 1, .key = { 0.8f, 0.95f, 1.3f }, .fill_l = { 0.8f, 1.6f, 0.5f }, .fill_r = { 0.5f, 0.8f, 1.8f }, .sweep = 0.15f },
                     0.22f, 0.8f, false },
  [STYLE_DEEP] = { "deep", "Underwater", 0, BD_DEEP,
                   { .pal = 1, .lane = CORAL, .land_alpha = 0, .ridge = 0, .plate = 0.9f, .burst = 0.5f, .flow = 1, .note_light = 0.85f, .pocket = 1,
                     .lit = 1, .key = { 0.8f, 1.2f, 1.4f }, .fill_l = { 0.3f, 1.4f, 2.0f }, .fill_r = { 0.3f, 1.8f, 1.6f }, .sweep = 0.25f },
                   0.22f, 0.8f, false },
  [STYLE_DESERT] = { "desert", "Desert night", 0, BD_DUNES,
                     { .pal = 1, .lane = ICE, .land_alpha = 0, .ridge = 0, .plate = 0.96f, .burst = 0.5f, .flow = 1, .note_light = 0.9f, .pocket = 1,
                       .lit = 1, .key = { 1.4f, 1.3f, 1.1f }, .fill_l = { 1.6f, 1.1f, 0.5f }, .fill_r = { 1.6f, 1.1f, 0.5f }, .sweep = 0.2f },
                     0.2f, 0.8f, false, false, PX_AMBER },
  [STYLE_NEWS] = { "newsprint", "Newsprint", 0, BD_CITY,
                   { .pal = 1, .lane = ICE, .land_alpha = 0, .ridge = 0, .plate = 0.97f, .burst = 0.3f /* a hit's flare prints as a solid white disc: at 0.5 the hats on two players flickered at 4 Hz */, .flow = 1, .note_light = 0.9f, .pocket = 1,
                     .lit = 1, .key = { 1.4f, 1.3f, 1.2f }, .fill_l = { 1.2f, 1.2f, 1.2f }, .fill_r = { 1.2f, 1.2f, 1.2f }, .sweep = 0.2f },
                   0.2f, 0.8f, false, false, PX_NEWS },
  [STYLE_LAVA] = { "lava", "Lava", 0, BD_LAVA,
                   { .pal = 1, .lane = ICE, .land_alpha = 0, .ridge = 0, .plate = 0.92f, .burst = 0.2f /* a hit's glow over the lava, a sixth of the screen, rose too fast at 0.5 (and at 0.28 on a run of hits) */, .flow = 1, .note_light = 0.9f, .pocket = 1,
                     .lit = 1, .key = { 1.3f, 1.1f, 1.0f }, .fill_l = { 3.0f, 0.9f, 0.2f }, .fill_r = { 3.0f, 0.9f, 0.2f }, .sweep = 0.3f },
                   0.22f, 0.8f, true, false, PX_LAVA },
  [STYLE_METAL] = { "metal", "Metal hall", 0, BD_METAL,
                    { .pal = 1, .lane = NEON, .land_alpha = 0, .ridge = 0, .plate = 0.92f, .burst = 0.5f, .flow = 1, .note_light = 0.9f, .pocket = 1,
                      .lit = 1, .key = { 1.2f, 1.2f, 1.3f }, .fill_l = { 1.8f, 1.4f, 0.8f }, .fill_r = { 0.9f, 1.2f, 1.8f }, .sweep = 0.4f },
                    0.22f, 0.8f, true, false, 0, true },
};

const char *style_title(int i) { return i >= 0 && i < STYLE_COUNT ? STYLES[i].title : "All styles"; }
const char *style_id(int i) { return i >= 0 && i < STYLE_COUNT ? STYLES[i].id : "auto"; }
bool style_held(int i) { return i >= 0 && i < STYLE_COUNT && STYLES[i].held; }

#define PHRASE_BEATS 32 /* 8 bars of 4 */
#define FADE_BEATS 2.0f
#define DROP_FADE_BEATS 1.0f
#define MIN_FADE_S 0.5f

static struct {
  int32_t cur, from, mode, changes, cut, readctl, pending, prepare, tour, tour_ticks;
  float t, fade_s, energy;
  float bd_w[BD_COUNT];
  /* the song being scheduled */
  const song *s;
  float beat, last_beat, hold_from, bpm, tempo;
  int32_t phrase;
  uint8_t order[STYLE_COUNT];
  uint32_t played;
  cw_node post;
  cw_look px[PX_COUNT];  /* the engine looks (a null handle: none) */
  cw_look px_a, px_b; float px_t; bool px_set;
  float pin_t; /* the `style pin` request: a fade held at this t (-1: none), for measuring two worlds at once */
  float bloom_set, thr_set, ssr_set;
  bool built, no_prewarm, blank;
} S;
static flash_meter fl_style;

/* the song's order: neon first (the game's own look, the title's), then the rest shuffled by the song's seed */
static void shuffle(uint32_t seed) {
  for (int i = 0; i < STYLE_COUNT; i++) S.order[i] = (uint8_t)i;
  uint32_t x = seed ? seed : 0x9e3779b9u;
  for (int i = STYLE_COUNT - 1; i > 1; i--) {
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    int j = 1 + (int)(x % (uint32_t)i); /* 1..i: neon stays first */
    uint8_t k = S.order[i]; S.order[i] = S.order[j]; S.order[j] = k;
  }
}

static float beats_to_s(float beats) {
  float bpm = S.bpm > 0 ? S.bpm : 120, tempo = S.tempo > 0 ? S.tempo : 1;
  return fmaxf(MIN_FADE_S, beats * 60.0f / (bpm * tempo));
}

static void begin(int to, float beats) {
  if (to == S.cur && S.t >= 1) return;
  if (S.t < 1) { S.pending = to; return; } /* one fade at a time */
  S.from = S.cur, S.cur = to, S.changes++;
  /* into or out of a two-colour print (one bit, amber, newsprint) twice as long: the print turns a small change of tone
   * into pixels going from paper to ink, so the same fade rose twice as hard (a sixth of the screen by 0.22 in two
   * frames, the characters into the one-bit stage) */
  bool print = STYLES[S.from].px == PX_BIT || STYLES[S.from].px == PX_AMBER || STYLES[S.from].px == PX_NEWS ||
               STYLES[to].px == PX_BIT || STYLES[to].px == PX_AMBER || STYLES[to].px == PX_NEWS;
  S.fade_s = beats_to_s(beats * (print ? 2 : 1));
  S.t = S.cut ? 1 : 0; /* the cut: the flash check's control (a one-frame change) */
  S.played |= 1u << to;
  S.hold_from = S.beat;
}

/* a fade the scheduler may make: its two worlds' costs within the budget (or one world), and not between the arcade
 * maze and one bit (a known engine issue in cartwheel-engine 0.70: that fade costs more than its two looks together
 * and spikes on the frame it ends; fixed in a later release) */
static bool pair_fits(int a, int b) {
  int pa = STYLES[a].px, pb = STYLES[b].px;
  if ((pa == PX_MAZE && pb == PX_BIT) || (pa == PX_BIT && pb == PX_MAZE)) return false;
  return COST[a] + COST[b] <= PAIR_BUDGET || STYLES[a].backdrop == STYLES[b].backdrop;
}

/* the next style of the song's order not played yet (a drop: a big one if one is left) */
static int next_style(bool drop) {
  uint32_t all = 0;
  for (int i = 0; i < STYLE_COUNT; i++) all |= (uint32_t)!STYLES[i].held << i;
  if (((S.played | (1u << S.cur)) & all) == all) S.played = 1u << S.cur; /* every one played: round again */
  int pick = -1;
  /* first a style whose fade from this one fits the budget (two worlds at once; the same world drawn once) */
  for (int i = 0; i < STYLE_COUNT && pick < 0; i++) {
    int k = S.order[i];
    bool fits = pair_fits(S.cur, k);
    if (k != S.cur && !STYLES[k].held && !(S.played >> k & 1) && (!drop || STYLES[k].big) && fits) pick = k;
  }
  for (int i = 0; i < STYLE_COUNT && pick < 0; i++) {
    int k = S.order[i];
    bool fits = pair_fits(S.cur, k);
    if (k != S.cur && !STYLES[k].held && !(S.played >> k & 1) && fits) pick = k;
  }
  for (int i = 0; i < STYLE_COUNT && pick < 0; i++) {
    int k = S.order[i];
    if (k != S.cur && !STYLES[k].held && !(S.played >> k & 1) && (!drop || STYLES[k].big)) pick = k;
  }
  for (int i = 0; i < STYLE_COUNT && pick < 0; i++) {
    int k = S.order[i];
    if (k != S.cur && !STYLES[k].held && !(S.played >> k & 1)) pick = k;
  }
  return pick < 0 ? STYLE_NEON : pick;
}

/* the tour's next style: every style once a round, each change one the scheduler could make (its fade within the
 * budget, from the lowest index up) where one is left, else the next not yet shown */
static uint32_t tour_seen;
static int tour_next(void) {
  uint32_t all = (1u << STYLE_COUNT) - 1;
  tour_seen |= 1u << S.cur;
  if ((tour_seen & all) == all) tour_seen = 1u << S.cur;
  for (int pass = 0; pass < 2; pass++)
    for (int d = 1; d < STYLE_COUNT; d++) {
      int k = (S.cur + d) % STYLE_COUNT;
      bool fits = pair_fits(S.cur, k);
      if (!(tour_seen >> k & 1) && (fits || pass)) return k;
    }
  return (S.cur + 1) % STYLE_COUNT;
}

static float smooth01(float x) { x = fminf(1, fmaxf(0, x)); return x * x * (3 - 2 * x); }

void styles_tick(const song *s, double ms, float tempo, float dt) {
  S.tempo = tempo;
  if (S.tour && ++S.tour_ticks % 75 == 0 && S.t >= 1) begin(tour_next(), FADE_BEATS); /* (held ones too: their draw states) */ /* the programs gate's tour */
  if (S.t < 1) {
    S.t = fminf(1, S.t + dt / fmaxf(S.fade_s, 1e-3f));
    if (S.t >= 1) S.from = S.cur;
  }
  if (S.t >= 1 && S.pending >= 0) {
    int p = S.pending;
    S.pending = -1;
    begin(p, FADE_BEATS);
  }
  S.prepare = 0;
  if (!s) { S.s = NULL; return; }
  float beat = song_beat_at(s, (float)ms);
  S.bpm = s->bpm;
  /* the music's loudness, slowly (a backdrop breathes with it; never a pulse on the beat) */
  float h, hue, br, ton;
  song_look_at(s, beat, &h, &hue, &br, &ton);
  S.energy += (h - S.energy) * fminf(1, dt / 1.5f);
  int phrase = (int)floorf((beat - (float)s->downbeat) / PHRASE_BEATS);
  if (s != S.s || beat < S.last_beat - 2) { /* a new song, or this one again from the top */
    S.s = s, S.beat = beat, S.last_beat = beat, S.phrase = phrase, S.played = 0;
    shuffle(s->hash);
    if (S.mode < 0 && !S.tour) begin(STYLE_NEON, FADE_BEATS);
    S.hold_from = beat;
    return;
  }
  S.beat = beat;
  if (S.tour) {
  } else if (S.mode >= 0) {
    begin(S.mode, FADE_BEATS);
  } else {
    bool drop = false;
    for (int i = 0; i < s->sections; i++) {
      if (s->section[i].kind != SEC_DROP) continue;
      float db = song_beat_at(s, (float)s->section[i].ms);
      if (db > S.last_beat && db <= beat && beat - S.hold_from >= 14) drop = true;
    }
    if (drop) begin(next_style(true), DROP_FADE_BEATS);
    else if (phrase > S.phrase && beat - S.hold_from >= PHRASE_BEATS - 2) begin(next_style(false), FADE_BEATS);
    /* the characters' scene renders at full size from a beat and a half before a change could come */
    float to_phrase = (float)(phrase + 1) * PHRASE_BEATS + (float)s->downbeat - beat;
    if (to_phrase < 1.5f && beat - S.hold_from >= PHRASE_BEATS - 4) S.prepare = 1;
    for (int i = 0; i < s->sections; i++) {
      if (s->section[i].kind != SEC_DROP) continue;
      float db = song_beat_at(s, (float)s->section[i].ms) - beat;
      if (db > 0 && db < 1.5f) S.prepare = 1;
    }
  }
  S.phrase = phrase;
  S.last_beat = beat;
  if (S.pending >= 0 || S.t < 1) S.prepare = 1;
}

/* The game's own prewarm. cartwheel-engine 0.70.0 prewarms every backdrop world and every look under the loading
 * screen (and the game asks again for its worlds built in code and its looks, styles_build), which took most first
 * showings into budget, but not all: the night sea's first showing still cost 80 ms of CPU on WebGPU (a Steam
 * Machine; with the sea's environment off too, and with its water in the scene file too), and an environment capture
 * is left to a world's first shown frame. So it stays (sent to the engine as a gap), and the hitch check's control
 * turns off both (`env CW_TEST_NO_PREWARM=1 -- style noprewarm`). Before anything is shown (the play cart's loading
 * screen covers it; the autopilot does it in its first frames): every world drawn, two at a time as a fade draws them (each on top in its turn), then a fade held half way
 * between every two kinds of picture (each look, the characters or neither, from and to: one style of each kind) drawn
 * as the game draws it, so every program, pipeline, render target and particle system is made and simulated before a
 * song: a style change in play is never a first use (a first use was a hitch of 20 to 350 ms). */
#define PREWARM_WORLD_FRAMES 2
#define PREWARM_ENV_FRAMES 6 /* a world that lights the field (environment): whole captures and their filter, a few times over */
#define PREWARM_PAIR_FRAMES 1
static int prewarm_frame, pw_kinds[STYLE_COUNT], pw_nkinds = -1;
static int world_frames(int w) { return backdrop_lights(w) ? PREWARM_ENV_FRAMES : PREWARM_WORLD_FRAMES; }
static int worlds_frames(void) { int n = 0; for (int w = 1; w < BD_COUNT; w++) n += world_frames(w); return n; }
static int prewarm_frames(void) { return worlds_frames() + pw_nkinds * (pw_nkinds - 1) * PREWARM_PAIR_FRAMES + 2; }
/* 0: done; 1: this frame drawn (worlds); 2: draw the pair now in S.from, S.cur at half way */
static int prewarm_step(float vis_t) {
  if (pw_nkinds < 0) { /* one style of each kind of picture: its look and whether the characters draw it */
    pw_nkinds = 0;
    for (int i = 0; i < STYLE_COUNT; i++) {
      bool seen = false;
      for (int k = 0; k < pw_nkinds; k++) seen |= STYLES[pw_kinds[k]].px == STYLES[i].px && (STYLES[pw_kinds[k]].ascii > 0) == (STYLES[i].ascii > 0) && STYLES[pw_kinds[k]].ssr == STYLES[i].ssr;
      if (!seen) pw_kinds[pw_nkinds++] = i;
    }
  }
  int nw = BD_COUNT - 1, wf = worlds_frames(), f = prewarm_frame++;
  if (f > prewarm_frames()) return 0;
  if (f == prewarm_frames()) { S.from = S.cur = STYLE_NEON, S.t = 1, S.prepare = 0; return 0; } /* (once: back to the game's own) */
  if (f < wf) { /* world w with the next one round, each the one on top in its turn */
    for (int i = 0; i < BD_COUNT; i++) S.bd_w[i] = 0;
    int w = 1, at = f;
    while (w < BD_COUNT - 1 && at >= world_frames(w)) at -= world_frames(w), w++;
    int w2 = 1 + (w % nw);
    S.bd_w[w] = 0.5f, S.bd_w[w2] = 0.5f;
    backdrop_env_mode(2); /* (the prewarm captures each world that lights the field: its programs made now) */
    backdrop_update(S.bd_w, (at % 2) ? w : w2, NULL, vis_t * 2, vis_t, 0.5f);
    return 1;
  }
  int q = (f - wf) / PREWARM_PAIR_FRAMES, n = pw_nkinds * (pw_nkinds - 1);
  if (q >= n) { S.from = S.cur = STYLE_NEON, S.t = 1; return 2; }
  int x = q / (pw_nkinds - 1), y = q % (pw_nkinds - 1);
  if (y >= x) y++;
  S.from = pw_kinds[x], S.cur = pw_kinds[y], S.t = 0.5f, S.prepare = 1;
  return 2;
}
bool styles_prewarming(void) { return !S.no_prewarm && (pw_nkinds < 0 || prewarm_frame <= prewarm_frames()); }
float styles_prewarm_progress(void) { return pw_nkinds < 0 ? 0 : fminf(1, (float)prewarm_frame / (float)prewarm_frames()); }

void styles_apply(float vis_t) {
  int pw = S.no_prewarm ? 0 : prewarm_step(vis_t);
  if (pw == 1) return;
  float k = pw == 2 ? 0.5f : smooth01(flash_meter_feed(&fl_style, S.t));
  if (S.pin_t >= 0) k = S.pin_t;
  const style_def *a = &STYLES[S.from], *b = &STYLES[S.cur];
  club_style la = a->look, lb = b->look;
  if (S.readctl) la.plate = lb.plate = la.pocket = lb.pocket = 0; /* the readability gate's control: no roads or pockets, the notes dimmed (club.c) */
  club_set_style(&la, &lb, k);
  club_set_note_gain(S.readctl ? 0.2f : 1);
  ascii_set_mix(S.blank ? 0 : a->ascii + (b->ascii - a->ascii) * k, S.prepare != 0);
  for (int i = 0; i < BD_COUNT; i++) S.bd_w[i] = 0;
  S.bd_w[a->backdrop] += 1 - k;
  S.bd_w[b->backdrop] += k;
  if (S.blank) for (int i = 0; i < BD_COUNT; i++) S.bd_w[i] = 0;
  /* the city's windows a third as bright in print (lit windows printed as solid paper blocks read as notes) */
  world_city_windows((a->px == PX_NEWS ? 0.3f : 1.0f) * (1 - k) + (b->px == PX_NEWS ? 0.3f : 1.0f) * k);
  backdrop_env_mode(pw == 2 ? 2 : S.t < 1 || S.pin_t >= 0 ? 1 : 0);
  backdrop_update(S.bd_w, b->backdrop, S.s, S.s ? S.beat : vis_t * 2, vis_t, S.energy);
  /* an engine look over the picture (half blocks): set when the pair or the blend changes; it comes in by a dissolve
   * (each pixel turning over as a soft noise passes, never the whole screen at once); the HUD's boxes stay clean */
  {
    cw_look la = S.px[a->px], lb = S.px[b->px];
    float t = a->px == b->px ? 0 : k;
    if (a->px == b->px) lb = (cw_look){ 0 };
    if (!S.px_set || la.id != S.px_a.id || lb.id != S.px_b.id || fabsf(t - S.px_t) > 1e-4f) {
      cw_pipeline *p = cw_pipeline_of(cw_scene_app_tree());
      if (p) cw_pipeline_set_looks(p, la, lb, t, &(cw_look_mix){ .blend = CW_LOOK_DISSOLVE, .softness = 0.35f, .scale = 28 });
      S.px_a = la, S.px_b = lb, S.px_t = t, S.px_set = true;
    }
    if (la.id || lb.id) {
      float bx[CW_LOOK_MAX_BOXES][4];
      int n = ascii_hud_boxes(bx, CW_LOOK_MAX_BOXES);
      cw_look_box lbx[CW_LOOK_MAX_BOXES];
      for (int i = 0; i < n; i++) lbx[i] = (cw_look_box){ bx[i][0], bx[i][1], bx[i][2], bx[i][3], 0, true };
      cw_pipeline *p = cw_pipeline_of(cw_scene_app_tree());
      if (p) cw_pipeline_set_look_boxes(p, lbx, n);
    }
  }
  float bloom = a->bloom + (b->bloom - a->bloom) * k, thr = a->bloom_threshold + (b->bloom_threshold - a->bloom_threshold) * k;
  if (S.post.id && (fabsf(bloom - S.bloom_set) > 1e-4f || fabsf(thr - S.thr_set) > 1e-4f)) {
    cw_node_set(S.post, "bloom_intensity", cw_val_float(bloom));
    cw_node_set(S.post, "bloom_threshold", cw_val_float(thr));
    S.bloom_set = bloom, S.thr_set = thr;
  }
  /* sharp reflections of the backdrop in the glass roads (cartwheel-engine 0.66.0 ssr_backdrop) where a style asks for
   * them, faded by the style's weight (the trace stays on through a fade into or out of one) */
  float ssr = (a->ssr ? 1 - k : 0) + (b->ssr ? k : 0);
  if (pw == 2 && (a->ssr || b->ssr)) ssr = fmaxf(ssr, 0.5f);
  if (S.post.id && fabsf(ssr - S.ssr_set) > 1e-4f) {
    bool on = ssr > 0.001f;
    if ((S.ssr_set > 0.001f) != on || S.ssr_set < 0) {
      cw_node_set(S.post, "ssr", cw_val_bool(on));
      cw_node_set(S.post, "ssr_backdrop", cw_val_bool(on));
    }
    if (on) cw_node_set(S.post, "ssr_intensity", cw_val_float(ssr));
    S.ssr_set = ssr;
  }
}

/* the one-bit print's share of the picture now (the HUD draws in its two colours past a half) */
float styles_onebit(void) {
  if (S.blank) return 0;
  float k = S.pin_t >= 0 ? S.pin_t : smooth01(S.t);
  return (STYLES[S.from].px == PX_BIT ? 1 - k : 0) + (STYLES[S.cur].px == PX_BIT ? k : 0);
}

void styles_set_mode(int mode) {
  S.mode = mode >= 0 && mode < STYLE_COUNT ? mode : -1;
  if (S.mode >= 0) begin(S.mode, FADE_BEATS);
}
int styles_mode(void) { return S.mode; }

static int style_arg(const char *a) {
  for (int i = 0; i < STYLE_COUNT; i++)
    if (!strcmp(a, STYLES[i].id)) return i;
  char *end;
  long v = strtol(a, &end, 10);
  return *end == 0 && v >= 0 && v < STYLE_COUNT ? (int)v : -2;
}

/* `style`: the state; `style <name|index>`: that style only (it fades in); `style auto`: every style with the song;
 * `style next`: the next change now; `style cut 1|0`: changes as one-frame cuts (the flash check's control);
 * `style readctl 1|0`: no roads or pockets and dim notes (the readability gate's control); `style tour`: every style in turn (the
 * programs gate: every backdrop's draw states at every preset) */
static void cmd_style(int argc, const char **argv, cw_reply *r) {
  if (argc >= 2) {
    if (!strcmp(argv[1], "auto")) S.mode = -1;
    else if (!strcmp(argv[1], "next")) { /* `style next drop`: as a drop changes it (a big style, a one-beat fade) */
      bool drop = argc >= 3 && !strcmp(argv[2], "drop");
      begin(next_style(drop), drop ? DROP_FADE_BEATS : FADE_BEATS);
    }
    else if (!strcmp(argv[1], "cut") && argc >= 3) S.cut = atoi(argv[2]) != 0;
    else if (!strcmp(argv[1], "readctl") && argc >= 3) S.readctl = atoi(argv[2]) != 0;
    else if (!strcmp(argv[1], "pin") && argc >= 5) { /* `style pin <from> <to> <t>`: hold a fade there (`style pin off` lets it go) */
      int a = style_arg(argv[2]), b = style_arg(argv[3]);
      if (a < 0 || b < 0) { cw_reply_error(r, "style pin <from> <to> <t> | style pin off"); return; }
      S.from = a, S.cur = b, S.t = 1, S.pending = -1, S.mode = b, S.pin_t = fminf(1, fmaxf(0, (float)atof(argv[4])));
    }
    else if (!strcmp(argv[1], "pin")) S.pin_t = -1;
    else if (!strcmp(argv[1], "blank") && argc >= 3) S.blank = atoi(argv[2]) != 0; /* `style blank 1`: no backdrop, look or characters (the footprint gate counts the notes' pixels on black) */
    else if (!strcmp(argv[1], "noprewarm")) S.no_prewarm = true; /* the hitch check's control (sent before the first frame,
                                                                     * with the engine's own off: romdev loadMedia firstCommand) */
    else if (!strcmp(argv[1], "tour")) S.tour = 1; /* every style in turn, one about every 1.25 s */
    else {
      int k = style_arg(argv[1]);
      if (k < 0) { cw_reply_error(r, "style <name|index> | auto | next | cut 1|0 | readctl 1|0"); return; }
      styles_set_mode(k);
    }
  }
  cw_reply_printf(r, "{\"style\":\"%s\",\"index\":%d,\"from\":\"%s\",\"t\":%.3f,\"mode\":\"%s\",\"changes\":%d,\"cut\":%d,\"readctl\":%d",
                  STYLES[S.cur].id, S.cur, STYLES[S.from].id, S.t, style_id(S.mode), S.changes, S.cut, S.readctl);
  if (argc >= 2) { cw_reply_printf(r, "}"); return; } /* the table only when asked with no argument */
  cw_reply_printf(r, ",\"styles\":[");
  for (int i = 0; i < STYLE_COUNT; i++)
    cw_reply_printf(r, "%s{\"id\":\"%s\",\"title\":\"%s\",\"ascii\":%d,\"backdrop\":\"%s\",\"bd\":%d,\"look\":%d,\"big\":%d,\"held\":%d}", i ? "," : "", STYLES[i].id, STYLES[i].title,
                    STYLES[i].ascii > 0, backdrop_name(STYLES[i].backdrop), STYLES[i].backdrop, STYLES[i].px, STYLES[i].big, STYLES[i].held);
  cw_reply_printf(r, "]}");
}

void styles_init(void) {
  S.mode = -1, S.pending = -1, S.t = 1, S.cur = S.from = STYLE_NEON, S.pin_t = -1;
  S.bloom_set = S.thr_set = S.ssr_set = -1;
  flash_meter_init(&fl_style, "style");
  flash_meter_register(&fl_style);
  cw_debug_i32("style.cur", &S.cur);
  cw_debug_i32("style.from", &S.from);
  cw_debug_f32("style.t", &S.t);
  cw_debug_i32("style.mode", &S.mode);
  cw_debug_i32("style.changes", &S.changes);
  cw_debug_i32("style.prepare", &S.prepare);
  cw_debug_f32_array("style.backdrop", S.bd_w, BD_COUNT);
  cw_debug_command("style", "style [<name|index>|auto|next|cut 1|0|readctl 1|0]: the visual style now (src/styles.c): one style only, every style with the song, the next change now, changes as cuts (the flash check's control), no roads or pockets and dim notes (the readability gate's control)", cmd_style);
}

void styles_build(cw_node level) {
  if (S.built) return;
  S.built = true;
  S.post = cw_node_get(level, "Post");
  /* half blocks: 120 rows (each cell two square pixels: 240 down), each the mean colour of its square */
  cw_look_desc d = cw_look_default(CW_LOOK_HALFBLOCK);
  d.halfblock.rows = 120, d.halfblock.levels = 0; /* no colour steps: a stepped colour jumps as a light passes, a regular flicker */
  S.px[PX_HALF] = cw_look_new(&d);
  /* the arcade maze's chunky pixels: 90 rows of half blocks (180 pixels down, 6 a pixel at 1080) on a small palette:
   * black, the maze's blues, pellet peach, white and the lanes' colours */
  static const float MAZE_PAL[] = { 0, 0, 0, 0.003f, 0.005f, 0.04f, 0.02f, 0.05f, 0.9f, 0.25f, 0.4f, 1, 1, 0.6f, 0.42f, 1, 1, 1,
                                    1, 0.06f, 0.08f, 1, 0.65f, 0.05f, 0.05f, 1, 0.2f, 0.2f, 0.8f, 1, 1, 0.3f, 0.6f, 0.4f, 0.12f, 0.15f,
                                    0.04f, 0.035f, 0.07f, 0.09f, 0.08f, 0.16f, 0.16f, 0.14f, 0.27f }; /* (the travellers' dim violet-greys) */
  cw_look_desc dm = cw_look_default(CW_LOOK_HALFBLOCK);
  dm.halfblock.rows = 90, dm.halfblock.palette = MAZE_PAL, dm.halfblock.palette_count = (int)(sizeof MAZE_PAL / sizeof MAZE_PAL[0] / 3);
  S.px[PX_MAZE] = cw_look_new(&dm);
  /* one bit: every screen pixel warm white or near black after the tone curve (the DISPLAY stage, so it stays two
   * colours), a blue noise anchored to the screen, outlines from the depth: the rock stage as a print */
  cw_look_desc db = cw_look_default(CW_LOOK_DITHER_ORDERED);
  db.stage = CW_LOOK_STAGE_DISPLAY;
  db.dither.pattern = CW_DITHER_BAYER;
  db.dither.pixel = 2;
  db.dither.light[0] = 1.0f, db.dither.light[1] = 0.93f, db.dither.light[2] = 0.8f;
  db.dither.dark[0] = 0.012f, db.dither.dark[1] = 0.01f, db.dither.dark[2] = 0.016f;
  db.dither.hue_value = 1.15f, db.dither.grey_gain = 0.8f, db.dither.outline = 1; /* (no sharpen: it costs about 3 ms on a Radeon 890M, an engine gap sent) */
  db.dither.gamma = 1.25f, db.dither.contrast = 1.25f;
  S.px[PX_BIT] = cw_look_new(&db);
  /* desert night: the dunes in amber dither (an ordered blue noise at the DISPLAY stage: every pixel deep brown or
   * amber), the moon's light and the crests amber, the shadows the dark */
  cw_look_desc da = db;
  da.dither.pattern = CW_DITHER_BLUE_NOISE;
  da.dither.pixel = 2;
  da.dither.light[0] = 1.0f, da.dither.light[1] = 0.62f, da.dither.light[2] = 0.2f;
  da.dither.dark[0] = 0.03f, da.dither.dark[1] = 0.012f, da.dither.dark[2] = 0.004f;
  da.dither.outline = 0, da.dither.gamma = 0.75f, da.dither.contrast = 1.5f, da.dither.sharpen = 0, da.dither.bias = 0.05f;
  S.px[PX_AMBER] = cw_look_new(&da);
  /* newsprint: the rain city printed in ink on paper, by error diffusion (no pattern: dots spread as a press does) */
  cw_look_desc dn = cw_look_default(CW_LOOK_DITHER_DIFFUSE);
  dn.stage = CW_LOOK_STAGE_DISPLAY;
  dn.dither.pixel = 2, dn.dither.sweeps = 3;
  dn.dither.light[0] = 0.93f, dn.dither.light[1] = 0.9f, dn.dither.light[2] = 0.82f;
  dn.dither.dark[0] = 0.05f, dn.dither.dark[1] = 0.045f, dn.dither.dark[2] = 0.06f;
  dn.dither.hue_value = 1.1f, dn.dither.grey_gain = 1.6f, dn.dither.outline = 1;
  dn.dither.gamma = 1.0f, dn.dither.contrast = 0.95f, dn.dither.bias = 0.0f; /* (soft: no lit window prints solid, a note-like block) */
  S.px[PX_NEWS] = cw_look_new(&dn);
  /* lava: half blocks (110 rows) of the scene's own colours: on a palette, a hit's glow at the middle gate snapped a
   * wide block to a far brighter colour in one frame (a hard onset over the flash bound) */
  cw_look_desc dl = cw_look_default(CW_LOOK_HALFBLOCK);
  dl.halfblock.rows = 110, dl.halfblock.levels = 0;
  S.px[PX_LAVA] = cw_look_new(&dl);
  backdrop_build(level);
  /* the worlds are built in code and the looks made here, at the first render, after the engine's own warm-up prewarm
   * may have run: each is prewarmed again now (cartwheel-engine 0.70.0: drawn once at the next frame, unseen) */
  /* the hitch check's control: the engine's test switch (`env CW_TEST_NO_PREWARM=1`, romdev loadMedia firstCommand,
   * there before this first render) turns off the game's prewarm too */
  const char *np = getenv("CW_TEST_NO_PREWARM");
  if (np && *np && *np != '0') S.no_prewarm = true;
  if (!S.no_prewarm) {
    backdrop_prewarm_all();
    cw_pipeline *p = cw_pipeline_of(cw_scene_app_tree());
    if (p) cw_look_prewarm_all(p);
  }
}
