/* The 1-bit looks (dither.h): the engine's one-bit dither (cartwheel/look.h) at the DISPLAY stage. */
#include "dither.h"
#include "game.h"
#include "opts.h"
#include <cartwheel/debug.h>
#include <cartwheel/look.h>
#include <cartwheel/pipeline.h>
#include <cartwheel/platform.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static struct {
  cw_tree *tree;
  bool ready;
  int look, level;
  cw_look handle;
  cw_look_desc desc; /* the settings the look has now */
  bool shown;        /* the pipeline draws it */
  /* debug fields */
  int dbg_look, dbg_w, dbg_h, dbg_shown;
  float dbg_ms, dbg_world, dbg_block;
} D = { .look = LOOK_DITHER, .level = -1 };

static float optf(const char *name, float def) {
  const char *s = opt_get(name);
  return s ? (float)atof(s) : def;
}

const char *const LOOK_NAMES[LOOK_COUNT] = { "Dither", "Dither warm", "Dither grid", "ASCII" };

/* the two colours as the screen shows them (sRGB display values, as the HUD draws them) */
cw_color dither_ink(void) { return D.look == LOOK_WARM ? (cw_color){ 1.0f, 0.9f, 0.74f, 1 } : (cw_color){ 1, 1, 1, 1 }; }
cw_color dither_paper(void) { return D.look == LOOK_WARM ? (cw_color){ 0.035f, 0.03f, 0.025f, 1 } : (cw_color){ 0, 0, 0, 1 }; }

/* a display value in linear light (the DISPLAY stage writes a look's colours as that linear colour shows on screen) */
static float to_linear(float d) { return d <= 0.04045f ? d / 12.92f : powf((d + 0.055f) / 1.055f, 2.4f); }

/* a level's tone: the world's gain and the landmark's gain, times the run options' (1, 1 for most) */
static void level_tone(float *world, float *block) {
  *world = 1, *block = 1;
  if (D.level < 0 || D.level >= NLEVELS) return;
  if (level_setting(&LEVELS[D.level]) == ST_DESERT) /* pale sand round a landmark the sun lights from behind */
    *world = optf("DITHER_DESERT_WORLD", 0.62f), *block = optf("DITHER_DESERT_BLOCK", 1.45f);
}

/* the look's settings for the look and level in use: blue noise (or the 8x8 Bayer matrix for Dither grid), one dither
 * pixel a screen pixel, on the finished frame; the grey world by its luminance (x world gain, its highlights kept), the
 * landmark's neon hues by their value (x block gain), a light unsharp mask, depth outlines, gamma and contrast */
static cw_look_desc desc_now(void) {
  cw_look_desc d = cw_look_default(CW_LOOK_DITHER_ORDERED);
  d.stage = CW_LOOK_STAGE_DISPLAY;
  d.dither.pattern = D.look == LOOK_GRID ? CW_DITHER_BAYER : CW_DITHER_BLUE_NOISE;
  d.dither.pixel = 1, d.dither.output_pixels = true;
  float wk, bk;
  level_tone(&wk, &bk);
  D.dbg_world = optf("DITHER_WORLD", 0.8f) * wk, D.dbg_block = optf("DITHER_BLOCK", 1.15f) * bk;
  d.dither.grey_gain = D.dbg_world;
  d.dither.hue_value = D.dbg_block;
  d.dither.sharpen = optf("DITHER_SHARPEN", 0.9f);
  d.dither.outline = optf("DITHER_OUTLINE", 1.0f);
  d.dither.outline_from = optf("DITHER_OUTLINE_FROM", 0.02f), d.dither.outline_to = optf("DITHER_OUTLINE_TO", 0.08f);
  d.dither.gamma = optf("DITHER_GAMMA", 1.3f);
  d.dither.contrast = optf("DITHER_CONTRAST", 1.3f);
  d.dither.bias = optf("DITHER_BIAS", 0.0f);
  cw_color ink = dither_ink(), paper = dither_paper();
  d.dither.light[0] = to_linear(ink.r), d.dither.light[1] = to_linear(ink.g), d.dither.light[2] = to_linear(ink.b);
  d.dither.dark[0] = to_linear(paper.r), d.dither.dark[1] = to_linear(paper.g), d.dither.dark[2] = to_linear(paper.b);
  return d;
}

/* the settings desc_now sets are the same (field by field: a struct's padding is not compared) */
static bool same(const cw_look_dither *a, const cw_look_dither *b) {
  for (int i = 0; i < 3; i++)
    if (a->light[i] != b->light[i] || a->dark[i] != b->dark[i]) return false;
  return a->pattern == b->pattern && a->grey_gain == b->grey_gain && a->hue_value == b->hue_value && a->sharpen == b->sharpen &&
         a->outline == b->outline && a->outline_from == b->outline_from && a->outline_to == b->outline_to && a->gamma == b->gamma &&
         a->contrast == b->contrast && a->bias == b->bias;
}

/* the look's settings and whether the pipeline draws it, from the look, the level and the run options now */
static void apply(void) {
  if (!D.ready) return;
  cw_look_desc d = desc_now();
  if (!D.handle.id) D.handle = cw_look_new(&d), D.desc = d;
  else if (!same(&d.dither, &D.desc.dither)) cw_look_set(D.handle, &d), D.desc = d;
  /* (DITHER_OFF: the plain picture, a gate's control) */
  bool show = D.look != LOOK_ASCII && !opt_on("DITHER_OFF");
  if (show != D.shown) {
    cw_pipeline_set_looks(cw_pipeline_of(D.tree), show ? D.handle : (cw_look){ 0 }, (cw_look){ 0 }, 0, NULL);
    D.shown = show, D.dbg_shown = show;
  }
  /* the pass's GPU time, as the pipeline last measured it (dt.dither.gpu_ms; -1: no timers on this host), and the size
   * it draws at (the scene's render size: one dither pixel a pixel) */
  const cw_pass_stat *st;
  int n = cw_pipeline_pass_stats(cw_pipeline_of(D.tree), &st);
  D.dbg_ms = -1;
  for (int i = 0; i < n; i++)
    if (st[i].name && !strcmp(st[i].name, "looks_display") && st[i].ran) D.dbg_ms = st[i].gpu_ms_avg;
  if (show) cw_render_size(&D.dbg_w, &D.dbg_h);
  else D.dbg_w = D.dbg_h = 0;
}

/* a LOOK run option: a name (the old trial's names too) or a number */
static int parse_look(const char *s) {
  if (!strcmp(s, "dither") || !strcmp(s, "blue")) return LOOK_DITHER;
  if (!strcmp(s, "warm")) return LOOK_WARM;
  if (!strcmp(s, "grid") || !strcmp(s, "bayer")) return LOOK_GRID;
  if (!strcmp(s, "ascii")) return LOOK_ASCII;
  int k = atoi(s);
  return k >= 0 && k < LOOK_COUNT ? k : LOOK_DITHER;
}

void dither_init(cw_tree *tree) {
  if (D.tree) return;
  D.tree = tree;
  D.ready = true;
  dither_option_changed(NULL);
  apply();
  cw_debug_i32("dt.dither.look", &D.dbg_look);
  cw_debug_i32("dt.dither.shown", &D.dbg_shown);
  cw_debug_i32("dt.dither.w", &D.dbg_w);
  cw_debug_i32("dt.dither.h", &D.dbg_h);
  cw_debug_f32("dt.dither.gpu_ms", &D.dbg_ms);
  cw_debug_f32("dt.dither.world", &D.dbg_world);
  cw_debug_f32("dt.dither.block", &D.dbg_block);
}

int *dither_look_value(void) { return &D.look; }
int dither_look(void) { return D.look; }
void dither_set_look(int look) {
  D.look = look >= 0 && look < LOOK_COUNT ? look : LOOK_DITHER, D.dbg_look = D.look;
  apply();
}
bool dither_mono(void) { return D.look != LOOK_ASCII; }
void dither_set_level(int level) {
  D.level = level;
  D.dbg_look = D.look; /* (the shell writes a saved choice into the look's value directly) */
  apply();
}

void dither_option_changed(const char *name) {
  if ((!name || !strcmp(name, "LOOK")) && opt_get("LOOK")) dither_set_look(parse_look(opt_get("LOOK")));
  else apply();
}

cw_color dither_hud_color(cw_color c) {
  if (!dither_mono()) return c;
  float l = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
  cw_color k = l >= 0.33f ? dither_ink() : dither_paper();
  k.a = c.a;
  return k;
}
