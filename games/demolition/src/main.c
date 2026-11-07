/* demolition (working title): knock down structures of wood, stone, glass and steel with a launcher, one to four
 * players taking turns, together (co-op: one score, one count of shots) or against each other (versus: each shot
 * scores for whoever fired it).
 *
 *   select    thirty levels, each with its best stars; how many play and how (co-op or versus)
 *   play      turn and raise the launcher, set the power, pick the ammunition (shot, heavy ball, charge), fire;
 *             bring every crystal down within par for three stars; hold the wide view to look the structure over
 *   result    the stars, then the next level or another try
 *   demolition_auto plays itself (the bot, through the same controls); demolition_bench is the perf gate's scene
 *   (the bot on the biggest castle, the camera on the collapse). Environment (development, native):
 *   DEMOLITION_LEVEL=n, DEMOLITION_PLAYERS=n, DEMOLITION_QUALITY=0..4, DEMOLITION_NO_HUD=1. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/project.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "fx.h"
#include "game.h"
#include "sound.h"
#include "view.h"

#ifndef DEMOLITION_BENCH
#define DEMOLITION_BENCH 0
#endif
#ifndef DEMOLITION_BENCH_LEVEL
#define DEMOLITION_BENCH_LEVEL 21
#endif

typedef struct save_data {
  uint8_t stars[NLEVELS];
  int32_t best[NLEVELS];
  int32_t last_level, last_mode;
} save_data;

enum { PH_SELECT, PH_PLAY, PH_RESULT };
static struct {
  game g;
  bot_state bot;
  bool in_match, built, have_game;
  int phase, phase_t;
  int sel, players, mode;
  int last_cur, turn_banner;
  bool result_sound;
  cw_action aim_l, aim_r, aim_u, aim_d, pow_u, pow_d, fire, ammo, view;
  cw_node camera;
  bool overview;
  /* debug fields */
  int32_t phase_dbg, level, state, shots, tleft, blocks, awake, broken, frags, booms, stars_dbg, cur, tick;
  int32_t score[MAXP];
  uint32_t hash;
  float yaw, pitch, power, step_ms, step_max, step_busy;
  double busy_sum;
  int busy_n;
  int32_t instances;
} G;

static cw_font font;
static uint32_t ticks_run;

static save_data *save(void) { return (save_data *)cw_shell_game_save(); }

static void shell_sound(cw_shell_sound s, void *u) {
  (void)u;
  sound_menu(s == CW_SHELL_SND_MOVE ? 0 : s == CW_SHELL_SND_BACK ? 2 : 1);
}
static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *u) {
  (void)from, (void)u;
  sound_field(to == CW_SHELL_PLAY || to == CW_SHELL_TITLE);
}

/* ── the match ───────────────────────────────────────────────────────── */

static void load_level(int level) {
  game_new(&G.g, level, G.players, G.mode);
  game_settle(&G.g, 180); /* the level settles before anyone sees it (levelcheck holds it still from here) */
  G.have_game = true;
  bot_reset(&G.bot);
  if (G.built) view_level(&G.g);
  G.last_cur = 0, G.turn_banner = 0;
  G.result_sound = false;
}

static void begin_level(int level) {
  G.sel = level;
  load_level(level);
  G.phase = PH_PLAY, G.phase_t = 0;
  save()->last_level = level, save()->last_mode = G.mode;
}

static int start_level(void) {
  int lv = DEMOLITION_BENCH ? DEMOLITION_BENCH_LEVEL : 0;
  if (getenv("DEMOLITION_LEVEL")) lv = CW_CLAMP(atoi(getenv("DEMOLITION_LEVEL")), 0, NLEVELS - 1);
  return lv;
}

static void start(int players, void *u) {
  (void)u;
  G.in_match = true;
  G.players = CW_CLAMP(players, 1, MAXP);
#ifdef DEMOLITION_AUTOPILOT
  if (getenv("DEMOLITION_PLAYERS")) G.players = CW_CLAMP(atoi(getenv("DEMOLITION_PLAYERS")), 1, MAXP);
  G.mode = G.players > 1 ? MODE_VERSUS : MODE_COOP;
  begin_level(start_level());
#else
  G.phase = PH_SELECT, G.phase_t = 0;
  G.mode = CW_CLAMP(save()->last_mode, 0, 1);
  int last = save()->last_level;
  G.sel = last >= 0 && last < NLEVELS ? last : 0;
  load_level(G.sel); /* the level behind the choice */
#endif
}

static bool warmup(void *u) {
  (void)u;
  if (G.built) return true;
  cw_node root = cw_tree_root(cw_scene_app_tree());
  G.camera = cw_node_get(root, "Main/Camera");
  view_init(root);
  fx_init(root);
  if (!G.have_game) G.players = 1, load_level(start_level()); /* a structure behind the title */
  G.built = true;
  view_level(&G.g);
  return true;
}

static void init(void *u) {
  (void)u;
  cw_project_load_input();
  G.aim_l = cw_action_find("aim_left"), G.aim_r = cw_action_find("aim_right"), G.aim_u = cw_action_find("aim_up"), G.aim_d = cw_action_find("aim_down");
  G.pow_u = cw_action_find("power_up"), G.pow_d = cw_action_find("power_down"), G.fire = cw_action_find("fire"), G.ammo = cw_action_find("ammo");
  G.view = cw_action_find("view");
  cw_shell_init(&(cw_shell_desc){ .title = "DEMOLITION", .subtitle = "bring them down, one to four players", .max_players = MAXP, .shared_screen = true,
                                  .game_save_size = sizeof(save_data), .game_save_version = 1, .start = start, .sound = shell_sound, .screen_changed = shell_screen });
  font = cw_ui_font_sans();
  cw_prompt_set_font(font);
  cw_quality_full_resolution(true);
  if (getenv("DEMOLITION_QUALITY")) cw_quality_set((cw_quality)CW_CLAMP(atoi(getenv("DEMOLITION_QUALITY")), 0, CW_QUALITY_COUNT - 1), true);
  sound_init();
  G.players = 1;
  cw_debug_i32("game.phase", &G.phase_dbg);
  cw_debug_i32("game.level", &G.level);
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.tick", &G.tick);
  cw_debug_i32("game.shots", &G.shots);
  cw_debug_i32("game.targets_left", &G.tleft);
  cw_debug_i32("game.blocks", &G.blocks);
  cw_debug_i32("game.awake", &G.awake);
  cw_debug_i32("game.broken", &G.broken);
  cw_debug_i32("game.fragments", &G.frags);
  cw_debug_i32("game.booms", &G.booms);
  cw_debug_i32("game.stars", &G.stars_dbg);
  cw_debug_i32("game.cur", &G.cur);
  cw_debug_field("player.score", G.score, CW_DBG_I32, MAXP);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_f32("aim.yaw", &G.yaw);
  cw_debug_f32("aim.pitch", &G.pitch);
  cw_debug_f32("aim.power", &G.power);
  cw_debug_f32("phys.step_ms", &G.step_ms);
  cw_debug_f32("phys.step_max", &G.step_max);   /* the slowest step since the app started (host-dependent) */
  cw_debug_f32("phys.step_busy", &G.step_busy); /* the mean step while more than 500 bodies were awake */
  cw_debug_i32("view.instances", &G.instances);
#ifdef DEMOLITION_AUTOPILOT
  cw_shell_start(1);
#endif
}

/* ── input ───────────────────────────────────────────────────────────── */

/* whose buttons play: the seat whose turn it is when it has a pad or keys of its own, else any seat (one pad
 * passed round) */
static int seat_of_turn(void) {
  int c = G.g.cur;
  if (G.players > 1 && cw_input_player_devices(c) != 0) return c;
  return -1;
}
static bool held(cw_action a, int seat) {
  if (seat >= 0) return cw_action_pressed(seat, a);
  for (int i = 0; i < MAXP; i++) if (cw_action_pressed(i, a)) return true;
  return false;
}
static bool pressed(cw_action a, int seat) {
  if (seat >= 0) return cw_action_just_pressed(seat, a);
  for (int i = 0; i < MAXP; i++) if (cw_action_just_pressed(i, a)) return true;
  return false;
}
static float strength(cw_action a, int seat) {
  float s = 0;
  for (int i = 0; i < MAXP; i++) if (seat < 0 || seat == i) s = fmaxf(s, cw_action_strength(i, a));
  return s;
}
static bool ui(const char *name) { return pressed(cw_action_find(name), -1); }

static void read_input(input *in) {
  memset(in, 0, sizeof *in);
#ifdef DEMOLITION_AUTOPILOT
  bot_input(&G.g, &G.bot, in);
  G.overview = false;
#else
  int s = seat_of_turn();
  in->left = held(G.aim_l, s), in->right = held(G.aim_r, s), in->up = held(G.aim_u, s), in->down = held(G.aim_d, s);
  in->turn_k = fmaxf(strength(G.aim_l, s), strength(G.aim_r, s));
  in->raise_k = fmaxf(strength(G.aim_u, s), strength(G.aim_d, s));
  in->power_up = held(G.pow_u, s), in->power_down = held(G.pow_d, s);
  in->fire = pressed(G.fire, s);
  in->ammo_next = pressed(G.ammo, s);
  G.overview = held(G.view, s);
#endif
}

static int unlocked(void) {
  int best = -1;
  for (int i = 0; i < NLEVELS; i++) if (save()->stars[i]) best = i;
  return CW_MIN(NLEVELS - 1, best + 3);
}

static void level_done(void) {
  int st = game_stars(&G.g);
  save_data *s = save();
  if (st > s->stars[G.g.level]) s->stars[G.g.level] = (uint8_t)st;
  int total = 0;
  for (int i = 0; i < G.g.players; i++) total += G.g.P[i].score;
  if (total > s->best[G.g.level]) s->best[G.g.level] = total;
#if !defined(DEMOLITION_AUTOPILOT)
  cw_shell_save_commit();
#endif
}

static void fixed_update(void *u, double dt) {
  (void)u, (void)dt;
  ticks_run++;
  sound_tick(ticks_run);
  if (!cw_shell_update()) return;
  if (!G.in_match) return;
  cw_shell_join_press();
  G.phase_t++;
  switch (G.phase) {
  case PH_SELECT: {
    int dx = ui("ui_right") - ui("ui_left"), dy = ui("ui_down") - ui("ui_up");
    int n = G.sel + dx + dy * 6;
    if ((dx || dy) && n >= 0 && n <= unlocked()) G.sel = n, sound_menu(0), load_level(G.sel);
    if (pressed(G.ammo, -1) && G.players > 1) G.mode ^= 1, sound_menu(0);
    if (ui("ui_accept") || pressed(G.fire, -1)) sound_menu(1), begin_level(G.sel);
    G.players = CW_MAX(1, cw_shell_players());
    break;
  }
  case PH_PLAY: {
    input in;
    read_input(&in);
    game_step(&G.g, &in);
    fx_events(&G.g, G.g.cur);
    sound_events(&G.g, cw_node3d_global_position(G.camera));
    if (G.g.cur != G.last_cur) G.last_cur = G.g.cur, G.turn_banner = 100, sound_turn();
    if (G.turn_banner > 0) G.turn_banner--;
    if (game_over(&G.g)) {
      level_done();
      G.phase = PH_RESULT, G.phase_t = 0;
      sound_result(G.g.state == GS_WON);
    }
    break;
  }
  case PH_RESULT: {
    /* the world goes on settling behind the card */
    input none = { 0 };
    game_step(&G.g, &none);
    fx_events(&G.g, G.g.cur);
    sound_events(&G.g, cw_node3d_global_position(G.camera));
    bool won = G.g.state == GS_WON;
#ifdef DEMOLITION_AUTOPILOT
    if (G.phase_t > 60 * 4) {
      int next = won ? (G.g.level + 1) % NLEVELS : G.g.level;
      if (DEMOLITION_BENCH || getenv("DEMOLITION_LEVEL")) next = G.g.level;
      begin_level(next);
    }
#else
    if (G.phase_t > 45) {
      if (ui("ui_accept") || pressed(G.fire, -1)) {
        sound_menu(1);
        begin_level(won ? CW_MIN(G.g.level + 1, NLEVELS - 1) : G.g.level);
      } else if (ui("ui_back")) {
        sound_menu(2);
        G.phase = PH_SELECT, G.phase_t = 0;
        load_level(G.sel);
      }
    }
#endif
    break;
  }
  }
  const game *g = &G.g;
  G.phase_dbg = G.phase, G.level = g->level, G.state = g->state, G.tick = g->tick, G.shots = g->shots, G.tleft = g->tgt_left;
  G.blocks = g->nalive, G.awake = game_awake(g), G.broken = g->broken, G.frags = g->fragments, G.booms = g->booms, G.stars_dbg = game_stars(g);
  G.cur = g->cur, G.hash = g->hash;
  for (int i = 0; i < MAXP; i++) G.score[i] = g->P[i].score;
  G.yaw = g->yaw, G.pitch = g->pitch, G.power = g->power, G.step_ms = g->step_ms;
  if (g->step_ms > G.step_max) G.step_max = g->step_ms;
  if (g->awake_now > 500) G.busy_sum += g->step_ms, G.busy_n++, G.step_busy = (float)(G.busy_sum / G.busy_n);
}

/* ── the HUD ─────────────────────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static cw_text_style tstyle(float size, cw_color c) { return (cw_text_style){ .size = size, .color = c, .outline_width = size * 0.09f, .outline_color = rgba(0.02f, 0.02f, 0.05f, 0.9f) }; }
static cw_text_metrics text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_style st = tstyle(size, c);
  if (al != CW_ALIGN_LEFT) { cw_text_metrics m = cw_font_measure(font, s, &st); x -= al == CW_ALIGN_CENTER ? m.width * 0.5f : m.width; }
  return cw_r2d_text(font, s, cw_v2(x, y), &st);
}
static void panel(float x, float y, float w, float h, float u, cw_color edge) {
  cw_r2d_round_rect(cw_rect_make(x, y, w, h), 12 * u, 6, rgba(0.05f, 0.06f, 0.1f, 0.72f));
  cw_r2d_round_rect_lines(cw_rect_make(x, y, w, h), 12 * u, 2.5f * u, 6, edge);
}
static const cw_color GOLD = { 1, 0.82f, 0.3f, 1 }, PINK = { 1, 0.4f, 0.85f, 1 }, WHITE = { 1, 1, 1, 0.95f };
static const cw_color SEAT[MAXP] = { { 1, 0.55f, 0.2f, 1 }, { 0.3f, 0.75f, 1, 1 }, { 0.5f, 0.95f, 0.4f, 1 }, { 1, 0.45f, 0.7f, 1 } };

static void star_row(float x, float y, float s, int n, int of) {
  for (int i = 0; i < of; i++) {
    float cx = x + (i - (of - 1) * 0.5f) * s * 1.15f;
    cw_vec2 pts[10];
    for (int k = 0; k < 10; k++) {
      float a = -1.5707963f + k * 0.6283185f, r = (k & 1) ? s * 0.22f : s * 0.5f;
      pts[k] = cw_v2(cx + cosf(a) * r, y + sinf(a) * r);
    }
    cw_color c = i < n ? GOLD : rgba(0.3f, 0.3f, 0.35f, 0.8f);
    cw_r2d_polygon(pts, 10, c);
  }
}

static void hud_play(cw_vec2 cs, float u) {
  const game *g = &G.g;
  char b[96];
  /* the level: its name, crystals left, shots against par */
  float x = 28 * u, y = 24 * u;
  panel(x, y, 430 * u, 150 * u, u, rgba(1, 0.82f, 0.3f, 0.8f));
  snprintf(b, sizeof b, "%d  %s", g->level + 1, g->L->name);
  text(b, x + 20 * u, y + 12 * u, 34 * u, GOLD, CW_ALIGN_LEFT);
  snprintf(b, sizeof b, "Crystals left: %d of %d", g->tgt_left, g->ntgt);
  text(b, x + 20 * u, y + 60 * u, 27 * u, PINK, CW_ALIGN_LEFT);
  snprintf(b, sizeof b, "Shots: %d   Par: %d", g->shots, g->L->par);
  text(b, x + 20 * u, y + 98 * u, 27 * u, g->shots > g->L->par ? rgba(1, 0.6f, 0.45f, 1) : WHITE, CW_ALIGN_LEFT);
  /* the players (versus: each one's score; co-op: the team's) */
  if (g->players > 1) {
    float rx = cs.x - 28 * u - 330 * u, ry = 24 * u;
    panel(rx, ry, 330 * u, (30 + 46 * g->players) * u, u, SEAT[g->cur]);
    for (int i = 0; i < g->players; i++) {
      bool now = i == g->cur;
      snprintf(b, sizeof b, "%sPlayer %d", now ? "> " : "", i + 1);
      text(b, rx + 18 * u, ry + (14 + 46 * i) * u, 28 * u, now ? SEAT[i] : rgba(0.8f, 0.8f, 0.85f, 1), CW_ALIGN_LEFT);
      snprintf(b, sizeof b, "%d", g->mode == MODE_VERSUS ? g->P[i].score : g->P[i].shots);
      text(b, rx + 312 * u, ry + (14 + 46 * i) * u, 28 * u, WHITE, CW_ALIGN_RIGHT);
    }
    text(g->mode == MODE_VERSUS ? "VERSUS: points" : "CO-OP: shots each", rx + 165 * u, ry + (20 + 46 * g->players) * u, 22 * u, rgba(0.85f, 0.85f, 0.9f, 0.9f), CW_ALIGN_CENTER);
  } else {
    int score = g->P[0].score;
    snprintf(b, sizeof b, "%d", score);
    text(b, cs.x - 32 * u, 28 * u, 46 * u, WHITE, CW_ALIGN_RIGHT);
    text("POINTS", cs.x - 32 * u, 84 * u, 22 * u, rgba(0.85f, 0.85f, 0.9f, 0.9f), CW_ALIGN_RIGHT);
  }
  if (G.turn_banner > 0 && g->players > 1) {
    snprintf(b, sizeof b, "Player %d", g->cur + 1);
    float a = fminf(1, G.turn_banner / 20.0f);
    cw_color c = SEAT[g->cur];
    c.a = a;
    text(b, cs.x * 0.5f, cs.y * 0.18f, 64 * u, c, CW_ALIGN_CENTER);
  }
  /* the launcher: ammunition, power, and the controls while aiming */
  float bw = 680 * u, bx = cs.x * 0.5f - bw * 0.5f, by = cs.y - 166 * u;
  if (g->state == GS_AIM) {
    panel(bx, by, bw, 152 * u, u, SEAT[g->cur]);
    float cw = 200 * u;
    for (int a = 0; a < NAMMO; a++) {
      float cx = bx + 25 * u + a * (cw + 15 * u);
      bool sel = a == g->ammo_sel;
      cw_r2d_round_rect(cw_rect_make(cx, by + 12 * u, cw, 46 * u), 8 * u, 6, sel ? rgba(0.95f, 0.62f, 0.2f, 0.9f) : rgba(0.15f, 0.16f, 0.22f, 0.85f));
      snprintf(b, sizeof b, "%s  %d", AMMO[a].name, g->ammo[a]);
      text(b, cx + cw * 0.5f, by + 22 * u, 23 * u, g->ammo[a] ? (sel ? rgba(0.08f, 0.05f, 0.02f, 1) : WHITE) : rgba(0.6f, 0.6f, 0.6f, 0.7f), CW_ALIGN_CENTER);
    }
    /* power */
    float px = bx + 25 * u, py = by + 68 * u, pw = bw - 50 * u;
    cw_r2d_round_rect(cw_rect_make(px, py, pw, 12 * u), 6 * u, 6, rgba(0.15f, 0.15f, 0.2f, 0.9f));
    cw_r2d_round_rect(cw_rect_make(px, py, pw * g->power, 12 * u), 6 * u, 6, rgba(1, 0.35f + 0.5f * (1 - g->power), 0.15f, 1));
    float sz = 21 * u;
    int seat = G.players > 1 ? g->cur : -1;
    cw_prompt_text(font, "{aim_left|aim_right}: turn   {aim_up|aim_down}: raise   {power_down|power_up}: power", seat, cw_v2(cs.x * 0.5f, by + 88 * u), sz, WHITE, CW_ALIGN_CENTER);
    cw_prompt_text(font, "{ammo}: ammunition   {view}: hold for the wide view   {fire}: fire", seat, cw_v2(cs.x * 0.5f, by + 116 * u), sz, rgba(1, 0.92f, 0.65f, 1), CW_ALIGN_CENTER);
  } else if (g->state == GS_SETTLE || g->state == GS_FLIGHT) {
    text(g->state == GS_FLIGHT ? "" : "Settling...", cs.x * 0.5f, cs.y - 70 * u, 28 * u, rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER);
  }
}

static void hud_result(cw_vec2 cs, float u) {
  const game *g = &G.g;
  bool won = g->state == GS_WON;
  float w = 640 * u, h = 300 * u, x = cs.x * 0.5f - w * 0.5f, y = cs.y * 0.3f;
  panel(x, y, w, h, u, won ? GOLD : rgba(1, 0.45f, 0.35f, 1));
  text(won ? "BROUGHT DOWN" : "OUT OF AMMUNITION", cs.x * 0.5f, y + 20 * u, 50 * u, won ? GOLD : rgba(1, 0.55f, 0.45f, 1), CW_ALIGN_CENTER);
  char b[96];
  if (won) {
    star_row(cs.x * 0.5f, y + 125 * u, 64 * u, game_stars(g), 3);
    snprintf(b, sizeof b, "%d shots, par %d", g->shots, g->L->par);
  } else snprintf(b, sizeof b, "%d of %d crystals still stand", g->tgt_left, g->ntgt);
  text(b, cs.x * 0.5f, y + 172 * u, 28 * u, WHITE, CW_ALIGN_CENTER);
  if (g->players > 1 && g->mode == MODE_VERSUS) {
    int best = 0;
    for (int i = 1; i < g->players; i++) if (g->P[i].score > g->P[best].score) best = i;
    snprintf(b, sizeof b, "Player %d has the most points: %d", best + 1, g->P[best].score);
    text(b, cs.x * 0.5f, y + 208 * u, 26 * u, SEAT[best], CW_ALIGN_CENTER);
  }
  if (G.phase_t > 45) {
    cw_prompt_text(font, won ? "{ui_accept}: next level   {ui_back}: choose a level" : "{ui_accept}: try again   {ui_back}: choose a level", -1,
                   cw_v2(cs.x * 0.5f, y + h - 50 * u), 26 * u, WHITE, CW_ALIGN_CENTER);
  }
}

static void hud_select(cw_vec2 cs, float u) {
  float y0 = 40 * u;
  text("CHOOSE A STRUCTURE", cs.x * 0.5f, y0, 46 * u, GOLD, CW_ALIGN_CENTER);
  float cw = 250 * u, ch = 92 * u, gap = 14 * u;
  float gx = cs.x * 0.5f - (6 * cw + 5 * gap) * 0.5f, gy = y0 + 80 * u;
  int open = unlocked();
  for (int i = 0; i < NLEVELS; i++) {
    float x = gx + (i % 6) * (cw + gap), y = gy + (i / 6) * (ch + gap);
    bool sel = i == G.sel, lock = i > open;
    cw_r2d_round_rect(cw_rect_make(x, y, cw, ch), 10 * u, 6, sel ? rgba(0.95f, 0.6f, 0.2f, 0.92f) : rgba(0.06f, 0.07f, 0.12f, lock ? 0.5f : 0.78f));
    char b[64];
    snprintf(b, sizeof b, "%d  %s", i + 1, LEVELS[i].name);
    text(b, x + 12 * u, y + 10 * u, 22 * u, lock ? rgba(0.6f, 0.6f, 0.65f, 0.7f) : sel ? rgba(0.08f, 0.05f, 0.02f, 1) : WHITE, CW_ALIGN_LEFT);
    if (!lock) star_row(x + cw * 0.5f, y + ch - 28 * u, 30 * u, save()->stars[i], 3);
    else text("locked", x + cw * 0.5f, y + ch - 42 * u, 20 * u, rgba(0.6f, 0.6f, 0.65f, 0.7f), CW_ALIGN_CENTER);
  }
  float y = gy + 5 * (ch + gap) + 16 * u;
  char b[128];
  if (G.players > 1) {
    snprintf(b, sizeof b, "%d players, %s  ({ammo}: co-op or versus)", G.players, G.mode == MODE_VERSUS ? "versus" : "co-op");
    cw_prompt_text(font, b, -1, cw_v2(cs.x * 0.5f, y), 26 * u, WHITE, CW_ALIGN_CENTER);
    y += 40 * u;
  }
  cw_prompt_text(font, "{ui_left|ui_right}: choose   {ui_accept}: play", -1, cw_v2(cs.x * 0.5f, y), 26 * u, rgba(1, 0.92f, 0.65f, 1), CW_ALIGN_CENTER);
}

static void hud(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 1080.0f;
  cw_r2d_begin(NULL);
  if (G.phase == PH_SELECT) hud_select(cs, u);
  else if (G.phase == PH_RESULT) hud_play(cs, u), hud_result(cs, u);
  else hud_play(cs, u);
  cw_r2d_end();
}

/* ── frame ───────────────────────────────────────────────────────────── */

static void render(void *u, double alpha) {
  (void)u;
  const float dt = 1.0f / 60.0f; /* the view animates by frames: the same pictures on every host */
  bool playing = cw_shell_screen_now() == CW_SHELL_PLAY;
  if (G.built) {
    int w, h;
    cw_window_size(&w, &h);
    float a = playing ? (float)alpha : 1.0f;
    view_sync(&G.g, a);
    view_camera(&G.g, G.camera, (float)w / (float)h, dt, G.overview || G.phase == PH_SELECT, a);
    fx_update(dt);
    G.instances = view_get_stats().instances;
  }
  bool show_hud = !getenv("DEMOLITION_NO_HUD");
  if (show_hud && G.in_match && playing) hud();
  cw_quality_auto_measure(playing);
  if (getenv("DEMOLITION_BENCH_PRINT") && playing) { /* the perf gate reads these */
    static int n;
    static double t_last, t_sum;
    static float phys_sum;
    double now = cw_time();
    if (t_last > 0) t_sum += (now - t_last) * 1000;
    t_last = now;
    phys_sum += G.g.step_ms;
    if (++n % 120 == 0) {
      cw_render3d_stats rs;
      cw_render3d_get_stats(cw_scene_app_tree(), &rs);
      fprintf(stderr, "bench frame_ms=%.2f phys_ms=%.2f blocks=%d awake=%d broken=%d instances=%d draws=%d tris=%d\n", t_sum / 120, phys_sum / 120,
              G.g.nalive, game_awake(&G.g), G.g.broken, G.instances, rs.draw_calls, rs.triangles);
      const cw_pass_stat *pst;
      int np = cw_pipeline_pass_stats(cw_pipeline_of(cw_scene_app_tree()), &pst);
      fprintf(stderr, "bench_passes gpu_total=%.2f", cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree())));
      for (int k = 0; k < np; k++)
        if (pst[k].ran && (pst[k].gpu_ms_avg > 0.02f || pst[k].cpu_ms_avg > 0.3f)) fprintf(stderr, " %s=%.2f/%.2f", pst[k].name, pst[k].gpu_ms_avg, pst[k].cpu_ms_avg);
      fprintf(stderr, "\n");
      t_sum = 0, phys_sum = 0;
    }
  }
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Demolition", .width = 1280, .height = 720, .debug = true, .init = init, .warmup = warmup, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/main.cwscene", &app);
}
