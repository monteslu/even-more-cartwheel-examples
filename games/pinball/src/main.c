/* pinball (working title): three original tables, one to four players taking turns, on cartwheel and three.c.
 *
 *   select    choose a table and how many play (each table's best scores beside it)
 *   play      flippers on the face buttons (west, east), the shoulders, the d-pad and the triggers; hold the launch
 *             button to pull the plunger and let go to fire, and once no ball waits on it a fresh press flips both;
 *             nudge the cabinet (too much tilts it); the dot display and the scores beside the table
 *   scores    a score in a table's top five takes three letters, saved
 *   pinball_auto plays itself (the bot); pinball_bench is the perf gate's scene (three balls on the table, all the
 *   time). Environment (development, native): PINBALL_TABLE=n, PINBALL_PLAYERS=n, PINBALL_QUALITY=0..4. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
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
#include "dmd.h"
#include "game.h"
#include "sound.h"
#include "view.h"

#ifndef PINBALL_BENCH
#define PINBALL_BENCH 0
#endif
#ifndef PINBALL_BENCH_TABLE
#define PINBALL_BENCH_TABLE 1
#endif

typedef struct hs_entry { int64_t score; char name[4]; } hs_entry;
typedef struct save_data {
  hs_entry best[NTABLES][5];
  int32_t last_table, last_players;
} save_data;

#define FLH 24
enum { PH_SELECT, PH_PLAY, PH_ENTRY, PH_OVER };
static struct {
  game g;
  bool in_match, built;
  int phase, phase_t;
  int sel_table, sel_players;
  int entry_player, entry_slot, entry_pos;
  char entry_name[4];
  int entry_order[MAXP], entry_n, entry_i;
  int prev_ui[8];
  cw_action fl, fr, both, launch, nudge, nudge_l, nudge_r;
  cw_node camera;
  dmd D;
  cw_texture dmd_tex;
  uint8_t *dmd_px;
  uint32_t rng;
  int music_style;
  float spin_last[MAXEL];
  /* debug fields */
  int32_t state, tick, table, balls, flips, drains, ramps, modes, mbs, jackpots, lights, lamps, players, cur, phase_dbg, saves, launches;
  int32_t score[MAXP];
  uint32_t hash;
  float ball_x, ball_y;
  /* the flippers' timing (tests/pinball-flippers.mjs), the last FLH presses of each lower flipper: the tick the coil
   * came on, the tick the flipper first moved after it, the stroke's time to full and the drop's time back to rest
   * (ms, -1 not yet), and the lowest it sagged (fraction of the stroke) while held after reaching full */
  int32_t fl_press[2][FLH], fl_move[2][FLH], fl_presses[2], fl_ticks;
  float fl_full_ms[2][FLH], fl_drop_ms[2][FLH], fl_hold_min[2][FLH], fl_frac[2];
  bool fl_waiting[2], fl_was[2];
} G;

static cw_font font;
static int flip_delay; /* PINBALL_TEST_FLIP_DELAY=n (native): tests/pinball-flippers.mjs's control */

/* ── sound hooks for the shell ───────────────────────────────────────── */

static void shell_sound(cw_shell_sound s, void *u) {
  (void)u;
  sound_menu(s == CW_SHELL_SND_MOVE ? 0 : s == CW_SHELL_SND_BACK ? 2 : 1);
}
static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *u) {
  (void)from, (void)u;
  if (to == CW_SHELL_TITLE) music_start(0, 110.0f), music_level(0);
  if (to != CW_SHELL_PLAY) sound_silence_balls();
}

/* ── the match ───────────────────────────────────────────────────────── */

static save_data *save(void) { return (save_data *)cw_shell_game_save(); }

static uint32_t ticks_run; /* fixed ticks since the app started */

static void begin_game(void) {
  G.rng = G.rng * 1664525u + 1013904223u + ticks_run * 2654435761u;
  game_new(&G.g, G.sel_table, G.sel_players, G.rng);
  G.phase = PH_PLAY, G.phase_t = 0;
  memset(G.spin_last, 0, sizeof G.spin_last);
  music_start(G.g.T.th.music_style, G.g.T.th.music_root);
  save()->last_table = G.sel_table, save()->last_players = G.sel_players;
#if !defined(PINBALL_AUTOPILOT)
  cw_shell_save_commit();
#endif
}

static void start(int players, void *u) {
  (void)u;
  G.in_match = true;
  G.sel_players = CW_CLAMP(players, 1, MAXP);
#ifdef PINBALL_AUTOPILOT
  G.sel_table = getenv("PINBALL_TABLE") ? CW_CLAMP(atoi(getenv("PINBALL_TABLE")), 0, NTABLES - 1) : PINBALL_BENCH ? PINBALL_BENCH_TABLE : 0;
  if (getenv("PINBALL_PLAYERS")) G.sel_players = CW_CLAMP(atoi(getenv("PINBALL_PLAYERS")), 1, MAXP);
  begin_game();
#else
  G.phase = PH_SELECT, G.phase_t = 0;
  int last = save()->last_table;
  G.sel_table = last >= 0 && last < NTABLES ? last : 0;
  game_new(&G.g, G.sel_table, 1, 1); /* the table behind the choice */
#endif
}

static bool warmup(void *u) {
  (void)u;
  if (G.built) return true;
  cw_node root = cw_tree_root(cw_scene_app_tree());
  G.camera = cw_node_get(root, "Main/Camera");
  view_init(root);
  if (!G.in_match) game_new(&G.g, 0, 1, 1); /* a table to draw behind the title (an autopilot is already playing) */
  view_build(&G.g);
  G.built = true;
  return true;
}

static void init(void *u) {
  (void)u;
  cw_project_load_input();
  G.fl = cw_action_find("flip_left"), G.fr = cw_action_find("flip_right"), G.launch = cw_action_find("launch"), G.both = cw_action_find("flip_both");
  G.nudge = cw_action_find("nudge"), G.nudge_l = cw_action_find("nudge_left"), G.nudge_r = cw_action_find("nudge_right");
  cw_shell_init(&(cw_shell_desc){ .title = "PINBALL", .subtitle = "three tables, one to four players", .max_players = MAXP, .shared_screen = true,
                                  .game_save_size = sizeof(save_data), .game_save_version = 1, .start = start, .sound = shell_sound, .screen_changed = shell_screen });
  font = cw_ui_font_sans();
  cw_prompt_set_font(font);
  cw_quality_full_resolution(true);
  cw_quality_keep_msaa(true);
  if (getenv("PINBALL_QUALITY")) cw_quality_set((cw_quality)CW_CLAMP(atoi(getenv("PINBALL_QUALITY")), 0, CW_QUALITY_COUNT - 1), true);
  if (getenv("PINBALL_TEST_FLIP_DELAY")) flip_delay = CW_CLAMP(atoi(getenv("PINBALL_TEST_FLIP_DELAY")), 0, 7);
  sound_init();
  G.rng = 1u; /* mixed with the ticks a person takes to start a game (begin_game): every game differs, and a replayed
               * input gives the same game on every host */
  G.sel_players = 1;
  G.dmd_px = calloc(DMD_W * DMD_S * DMD_H * DMD_S, 4);
  G.dmd_tex = cw_texture_from_pixels(DMD_W * DMD_S, DMD_H * DMD_S, G.dmd_px, NULL);
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.phase", &G.phase_dbg);
  cw_debug_i32("game.tick", &G.tick);
  cw_debug_i32("game.table", &G.table);
  cw_debug_i32("game.players", &G.players);
  cw_debug_i32("game.cur", &G.cur);
  cw_debug_i32("game.balls", &G.balls);
  cw_debug_i32("game.flips", &G.flips);
  cw_debug_i32("game.launches", &G.launches);
  cw_debug_i32("game.drains", &G.drains);
  cw_debug_i32("game.saves", &G.saves);
  cw_debug_i32("game.ramps", &G.ramps);
  cw_debug_i32("game.modes", &G.modes);
  cw_debug_i32("game.multiballs", &G.mbs);
  cw_debug_i32("game.jackpots", &G.jackpots);
  cw_debug_field("player.score", G.score, CW_DBG_I32, MAXP);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_f32("ball.x", &G.ball_x);
  cw_debug_f32("ball.y", &G.ball_y);
  cw_debug_i32("view.lights", &G.lights);
  cw_debug_i32("view.lamps_lit", &G.lamps);
  cw_debug_f32("dev.cam_k", &g_cam_k);
  cw_debug_i32("flip.ticks", &G.fl_ticks);
  cw_debug_field("flip.press_tick", G.fl_press, CW_DBG_I32, 2 * FLH);
  cw_debug_field("flip.move_tick", G.fl_move, CW_DBG_I32, 2 * FLH);
  cw_debug_field("flip.presses", G.fl_presses, CW_DBG_I32, 2);
  cw_debug_field("flip.full_ms", G.fl_full_ms, CW_DBG_F32, 2 * FLH);
  cw_debug_field("flip.drop_ms", G.fl_drop_ms, CW_DBG_F32, 2 * FLH);
  cw_debug_field("flip.hold_min", G.fl_hold_min, CW_DBG_F32, 2 * FLH);
  cw_debug_field("flip.frac", G.fl_frac, CW_DBG_F32, 2);
#ifdef PINBALL_AUTOPILOT
  cw_shell_start(1);
#else
  music_start(0, 110.0f);
#endif
}

/* ── input ───────────────────────────────────────────────────────────── */

/* any seat's buttons play: players take turns, on one pad passed round or each on their own */
static bool held(cw_action a) {
  for (int i = 0; i < MAXP; i++) if (cw_action_pressed(i, a)) return true;
  return false;
}
static bool pressed(cw_action a) {
  for (int i = 0; i < MAXP; i++) if (cw_action_just_pressed(i, a)) return true;
  return false;
}
static bool ui(const char *name) {
  cw_action a = cw_action_find(name);
  return pressed(a);
}

static void read_input(input *in) {
  memset(in, 0, sizeof *in);
#ifdef PINBALL_AUTOPILOT
  bot_input(&G.g, in, &G.rng);
#else
  in->left = held(G.fl), in->right = held(G.fr);
  if (flip_delay > 0) { /* the latency gate's control only: the flipper buttons a few ticks late (it must fail) */
    static bool past[8][2];
    for (int i = 7; i > 0; i--) past[i][0] = past[i - 1][0], past[i][1] = past[i - 1][1];
    past[0][0] = in->left, past[0][1] = in->right;
    in->left = past[flip_delay][0], in->right = past[flip_delay][1];
  }
  in->upper = in->right;
  in->launch = held(G.launch), in->both = held(G.both);
  in->nudge_up = pressed(G.nudge), in->nudge_l = pressed(G.nudge_l), in->nudge_r = pressed(G.nudge_r);
#endif
}

/* ── the high score table ────────────────────────────────────────────── */

static int hs_slot(int table, int64_t score) {
  hs_entry *b = save()->best[table];
  for (int i = 0; i < 5; i++) if (score > b[i].score) return i;
  return -1;
}

static void entry_next(void) {
  while (G.entry_i < G.entry_n) {
    int p = G.entry_order[G.entry_i++];
    int slot = hs_slot(G.g.table_id, G.g.P[p].score);
    if (slot < 0 || G.g.P[p].score <= 0) continue;
    G.entry_player = p, G.entry_slot = slot, G.entry_pos = 0;
    memcpy(G.entry_name, "AAA", 4);
    G.phase = PH_ENTRY, G.phase_t = 0;
    return;
  }
  G.phase = PH_OVER, G.phase_t = 0;
}

static void entry_commit(void) {
  hs_entry *b = save()->best[G.g.table_id];
  for (int i = 4; i > G.entry_slot; i--) b[i] = b[i - 1];
  b[G.entry_slot].score = G.g.P[G.entry_player].score;
  memcpy(b[G.entry_slot].name, G.entry_name, 4);
  cw_shell_save_commit();
}

static void game_finished(void) {
  /* the players by score, best first, each offered a place */
  G.entry_n = G.g.players, G.entry_i = 0;
  for (int i = 0; i < G.entry_n; i++) G.entry_order[i] = i;
  for (int i = 0; i < G.entry_n; i++)
    for (int j = i + 1; j < G.entry_n; j++)
      if (G.g.P[G.entry_order[j]].score > G.g.P[G.entry_order[i]].score) { int t = G.entry_order[i]; G.entry_order[i] = G.entry_order[j]; G.entry_order[j] = t; }
#ifdef PINBALL_AUTOPILOT
  G.sel_table = (G.sel_table + 1) % NTABLES;
  if (getenv("PINBALL_TABLE") || PINBALL_BENCH) G.sel_table = G.g.table_id;
  begin_game();
#else
  entry_next();
#endif
}

static void fixed_update(void *u, double dt) {
  (void)u, (void)dt;
  ticks_run++;
  if (!cw_shell_update()) return;
  if (!G.in_match) return;
  cw_shell_join_press();
  G.phase_t++;
  switch (G.phase) {
  case PH_SELECT: {
    int dx = (ui("ui_right") || pressed(G.fr)) - (ui("ui_left") || pressed(G.fl));
    int dy = ui("ui_up") - ui("ui_down");
    if (dx) {
      G.sel_table = (G.sel_table + dx + NTABLES) % NTABLES, sound_menu(0);
      game_new(&G.g, G.sel_table, 1, 1);
    }
    if (dy) G.sel_players = CW_CLAMP(G.sel_players + dy, 1, MAXP), sound_menu(0);
    if (ui("ui_accept") || pressed(G.launch)) sound_menu(1), begin_game();
    break;
  }
  case PH_PLAY: {
    input in;
    read_input(&in);
    if (PINBALL_BENCH) { /* three balls always on the table */
      G.g.ball_save_t = 600;
      if (G.g.balls_in_play < 3 && G.g.tick % 45 == 0) game_launch_ball(&G.g);
    }
    game_step(&G.g, &in);
    sound_events(&G.g);
    music_level(G.g.multiball ? 3 : G.g.mode >= 0 ? 2 : G.g.state == GS_PLAY ? 1 : 0);
    /* the spinners tick as their plates turn */
    for (int i = 0; i < G.g.T.nel; i++) {
      const elem *e = &G.g.T.el[i];
      if (e->kind != EK_SPINNER) continue;
      if (floorf(e->spin / 3.14159265f) != floorf(G.spin_last[i] / 3.14159265f)) sound_spin_tick();
      G.spin_last[i] = e->spin;
    }
    if (game_over(&G.g)) game_finished();
    break;
  }
  case PH_ENTRY: {
    int d = (pressed(G.fr) || ui("ui_up") || ui("ui_right")) - (pressed(G.fl) || ui("ui_down") || ui("ui_left"));
    if (d) {
      char c = G.entry_name[G.entry_pos];
      c = (char)(c + d);
      if (c > 'Z') c = ' ';
      else if (c < ' ') c = 'Z';
      else if (c > ' ' && c < 'A') c = d > 0 ? 'A' : ' ';
      G.entry_name[G.entry_pos] = c;
      sound_menu(0);
    }
    if (ui("ui_accept") || pressed(G.launch)) {
      sound_menu(1);
      if (++G.entry_pos >= 3) entry_commit(), entry_next();
    }
    if (ui("ui_back") && G.entry_pos > 0) G.entry_pos--, sound_menu(2);
    break;
  }
  case PH_OVER:
    if (G.phase_t > 60 && (ui("ui_accept") || pressed(G.launch))) {
      sound_menu(1);
      G.phase = PH_SELECT, G.phase_t = 0;
      game_new(&G.g, G.sel_table, 1, 1);
      music_start(0, 110.0f), music_level(0);
    }
    break;
  }
  /* debug fields */
  const game *g = &G.g;
  G.state = g->state, G.tick = g->tick, G.table = g->table_id, G.balls = g->balls_in_play, G.flips = g->flips, G.drains = g->drains;
  G.ramps = g->ramps_made, G.modes = g->modes_started, G.mbs = g->mbs, G.jackpots = g->jackpots_total, G.hash = g->hash;
  G.players = g->players, G.cur = g->cur, G.phase_dbg = G.phase, G.saves = g->saves, G.launches = g->launches;
  for (int i = 0; i < MAXP; i++) G.score[i] = (int32_t)(g->P[i].score > 2000000000 ? 2000000000 : g->P[i].score);
  G.ball_x = g->W.b[0].x, G.ball_y = g->W.b[0].y;
  G.fl_ticks = (int32_t)ticks_run;
  if (G.phase == PH_PLAY) {
    int fi[2] = { g->T.left_flipper, g->T.right_flipper };
    for (int k = 0; k < 2; k++) {
      const pb_flipper *F = &g->W.fl[fi[k]];
      float frac = (F->ang - F->rest) / (F->up - F->rest);
      G.fl_frac[k] = frac;
      bool coil = F->held && F->on;
      if (coil && !G.fl_was[k]) { /* a new press: its slot in the history (the oldest goes) */
        if (G.fl_presses[k] >= FLH)
          for (int i = 1; i < FLH; i++)
            G.fl_press[k][i - 1] = G.fl_press[k][i], G.fl_move[k][i - 1] = G.fl_move[k][i], G.fl_full_ms[k][i - 1] = G.fl_full_ms[k][i],
            G.fl_drop_ms[k][i - 1] = G.fl_drop_ms[k][i], G.fl_hold_min[k][i - 1] = G.fl_hold_min[k][i];
        int i = G.fl_presses[k] < FLH ? G.fl_presses[k] : FLH - 1;
        G.fl_press[k][i] = (int32_t)ticks_run, G.fl_move[k][i] = -1, G.fl_full_ms[k][i] = -1, G.fl_drop_ms[k][i] = -1, G.fl_hold_min[k][i] = 1;
        G.fl_presses[k]++, G.fl_waiting[k] = true;
      }
      int i = (G.fl_presses[k] < FLH ? G.fl_presses[k] : FLH) - 1;
      if (i >= 0) {
        if (G.fl_waiting[k] && frac > 0.001f) G.fl_move[k][i] = (int32_t)ticks_run, G.fl_waiting[k] = false;
        if (coil && F->reach_t >= 0) G.fl_full_ms[k][i] = F->reach_t * 1000, G.fl_hold_min[k][i] = fminf(G.fl_hold_min[k][i], frac);
        if (!coil && G.fl_drop_ms[k][i] < 0 && F->reach_t >= 0) G.fl_drop_ms[k][i] = F->reach_t * 1000;
      }
      G.fl_was[k] = coil;
    }
  }
}

/* ── the dot display ─────────────────────────────────────────────────── */

/* centred text at a scale, one size down (and a few dots lower) when it would not fit the panel */
static void dmd_fit(dmd *d, const char *s, int y, int scale, int v) {
  while (scale > 1 && dmd_text_width(s, scale) > 126) scale--, y += 4;
  dmd_text_c(d, s, 64, y, scale, v);
}

static void dmd_draw(void) {
  dmd *d = &G.D;
  const game *g = &G.g;
  dmd_clear(d);
  char a[48], b[48];
  int t = g->tick;
  if (G.phase == PH_SELECT) {
    dmd_fit(d, table_name(G.sel_table), 3, 2, 15);
    snprintf(a, sizeof a, "%d PLAYER%s", G.sel_players, G.sel_players > 1 ? "S" : "");
    dmd_text_c(d, a, 64, 23, 1, 9);
  } else if (G.phase == PH_ENTRY) {
    snprintf(a, sizeof a, "PLAYER %d  HIGH SCORE", G.entry_player + 1);
    dmd_text_c(d, a, 64, 1, 1, 12);
    for (int i = 0; i < 3; i++) {
      char c[2] = { G.entry_name[i], 0 };
      bool on = i != G.entry_pos || (G.phase_t / 12) % 2;
      dmd_text(d, c, 40 + i * 18, 11, 2, on ? 15 : 4);
      if (i == G.entry_pos) dmd_rect(d, 40 + i * 18, 27, 10, 1, 15);
    }
  } else if (g->msg != MSG_NONE && G.phase == PH_PLAY) {
    int mt = g->msg_t;
    bool blink = (mt / 6) % 2;
    switch (g->msg) {
    case MSG_JACKPOT: case MSG_MULTIBALL: case MSG_SKILL: case MSG_EXTRA:
      if (blink || g->msg == MSG_MULTIBALL) dmd_fit(d, g->msg_a, g->msg_value ? 1 : 9, 2, 15);
      if (g->msg_value) fmt_score(g->msg_value, a, sizeof a), dmd_text_c(d, a, 64, 18, 2, 13);
      else if (g->msg_b[0]) dmd_text_c(d, g->msg_b, 64, 25, 1, 10);
      for (int k = 0; k < 6; k++) { /* sparks */
        int sx = (int)((uint32_t)(k * 2654435761u + (uint32_t)mt * 40503u) % 128u), sy = (int)((uint32_t)(k * 97u + (uint32_t)mt * 13u) % 32u);
        dmd_set(d, sx, sy, 15);
      }
      break;
    case MSG_MODE:
      dmd_fit(d, g->msg_a, 2, 2, 15);
      dmd_text_c(d, g->msg_b, 64, 22, 1, mt < 30 && blink ? 15 : 10);
      break;
    case MSG_TILT: case MSG_WARN:
      dmd_fit(d, g->msg_a, g->msg_b[0] ? 2 : 9, 2, blink ? 15 : 6);
      if (g->msg_b[0]) dmd_text_c(d, g->msg_b, 64, 23, 1, 12);
      break;
    case MSG_BONUS:
      dmd_text_c(d, g->msg_a, 64, 2, 1, 12);
      if (g->msg_b[0]) dmd_text_c(d, g->msg_b, 64, 11, 1, 9);
      fmt_score(g->msg_value * CW_MIN(mt, 120) / 120, a, sizeof a);
      dmd_text_c(d, a, 64, 18, 2, 15);
      break;
    case MSG_BIG:
      dmd_text_c(d, g->msg_a, 64, 1, 1, 11);
      if (g->msg_value) fmt_score(g->msg_value, a, sizeof a), dmd_text_c(d, a, 64, 10, 2, 15);
      dmd_text_c(d, g->msg_b, 64, 25, 1, 9);
      break;
    default:
      if (dmd_text_width(g->msg_a, 2) > 126) dmd_text_c(d, g->msg_a, 64, g->msg_b[0] ? 6 : 12, 1, 15);
      else dmd_fit(d, g->msg_a, g->msg_b[0] ? 3 : 9, 2, 15);
      if (g->msg_b[0]) dmd_text_c(d, g->msg_b, 64, 23, 1, 11);
      break;
    }
  } else {
    /* the current player's score, big; the ball and the mode below */
    const player_state *p = &g->P[g->cur];
    fmt_score(p->score, a, sizeof a);
    int sc = dmd_text_width(a, 2) > 124 ? 1 : 2;
    dmd_text_c(d, a, 64, sc == 2 ? 3 : 8, sc, 15);
    if (G.phase == PH_OVER) snprintf(b, sizeof b, "GAME OVER");
    else if (g->mode >= 0) snprintf(b, sizeof b, "%s  %d", g->T.modes[g->mode].name, (g->mode_t + 59) / 60);
    else if (g->multiball) snprintf(b, sizeof b, "MULTIBALL  JACKPOT IS LIT");
    else if (g->state == GS_PLUNGE) snprintf(b, sizeof b, "SKILL SHOT: THE LIT LANE");
    else if (g->players > 1) snprintf(b, sizeof b, "PLAYER %d   BALL %d", g->cur + 1, CW_MIN(p->ball, BALLS_PER_GAME));
    else snprintf(b, sizeof b, "BALL %d", CW_MIN(p->ball, BALLS_PER_GAME));
    dmd_text_c(d, b, 64, 24, 1, g->mode >= 0 && (t / 20) % 2 ? 13 : 8);
  }
  float col[3];
  for (int i = 0; i < 3; i++) col[i] = powf(g->T.th.dmd[i], 1 / 2.2f);
  dmd_render(d, G.dmd_px, col);
  cw_texture_update(G.dmd_tex, G.dmd_px);
}

/* ── the HUD ─────────────────────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static cw_text_style tstyle(float size, cw_color c) { return (cw_text_style){ .size = size, .color = c, .outline_width = size * 0.08f, .outline_color = rgba(0, 0, 0, 0.85f) }; }
static cw_text_metrics measure(const char *s, float size) { cw_text_style st = tstyle(size, rgba(1, 1, 1, 1)); return cw_font_measure(font, s, &st); }
static cw_text_metrics text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_style st = tstyle(size, c);
  if (al != CW_ALIGN_LEFT) { cw_text_metrics m = cw_font_measure(font, s, &st); x -= al == CW_ALIGN_CENTER ? m.width * 0.5f : m.width; }
  return cw_r2d_text(font, s, cw_v2(x, y), &st);
}
static cw_color tcol(const float *c, float k, float a) { return rgba(powf(fminf(1, c[0] * k), 1 / 2.2f), powf(fminf(1, c[1] * k), 1 / 2.2f), powf(fminf(1, c[2] * k), 1 / 2.2f), a); }

/* the left panel: the dot display in a bezel, the controls under it */
static float panel_dmd(cw_vec2 cs, float u) {
  const theme *th = &G.g.T.th;
  float pw = fminf(cs.x * 0.3f, 600 * u), ph = pw / 4.0f;
  float x = cs.x * 0.165f - pw * 0.5f, y = cs.y * 0.08f;
  cw_r2d_round_rect(cw_rect_make(x - 14 * u, y - 14 * u, pw + 28 * u, ph + 28 * u), 10 * u, 6, rgba(0.06f, 0.05f, 0.05f, 0.95f));
  cw_r2d_round_rect_lines(cw_rect_make(x - 14 * u, y - 14 * u, pw + 28 * u, ph + 28 * u), 10 * u, 3 * u, 6, tcol(th->col[2], 0.8f, 0.9f));
  cw_r2d_sprite(&(cw_sprite){ .texture = G.dmd_tex, .pos = cw_v2(x, y), .size = cw_v2(pw, ph), .color = rgba(1, 1, 1, 1) });
  return y + ph + 28 * u;
}

static void hud_play(cw_vec2 cs, float u) {
  const game *g = &G.g;
  const theme *th = &g->T.th;
  float bs = 26 * u;
  float y = panel_dmd(cs, u);
  /* under the display: the table, and the controls while they matter */
  y += 12 * u;
  y += text(g->T.name, cs.x * 0.165f, y, 34 * u, tcol(th->col[2], 1.4f, 1), CW_ALIGN_CENTER).height + 6 * u;
  bool plunging = false;
  for (int i = 0; i < PB_MAXB; i++) if (g->W.b[i].state == BS_FREE && g->W.b[i].x > 0.219f && g->W.b[i].y < 0.08f) plunging = true;
  if (plunging || g->ball_ticks < 60 * 12) {
    float x = cs.x * 0.165f;
    y += 14 * u;
    /* a pad's diamond: west and east the flippers, south the plunger and then both flippers, north the nudge */
    const char *south = plunging ? "{launch}: hold, let go to launch" : "{flip_both}: both flippers";
    cw_prompt_text(font, south, -1, cw_v2(x, y), bs, rgba(1, 1, 1, 0.92f), CW_ALIGN_CENTER);
    y += cw_prompt_text_size(font, south, -1, bs).y + 12 * u;
    bool pad = false; /* players on a pad: the diamond's sides first, the shoulders and the d-pad after */
    for (int i = 0; i < MAXP; i++) pad |= cw_prompt_device_of(i) == CW_PROMPT_PAD;
    const char *fl = pad ? "{pad:west} {pad:east}: flippers" : "{flip_left|flip_right}: flippers";
    cw_prompt_text(font, fl, -1, cw_v2(x, y), bs, rgba(1, 1, 1, 0.92f), CW_ALIGN_CENTER);
    y += cw_prompt_text_size(font, fl, -1, bs).y + 12 * u;
    if (pad) {
      const char *also = "{pad:lshoulder} {pad:rshoulder} {flip_left|flip_right}: flippers too";
      cw_prompt_text(font, also, -1, cw_v2(x, y), bs, rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER);
      y += cw_prompt_text_size(font, also, -1, bs).y + 12 * u;
    }
    cw_prompt_text(font, "{nudge}: nudge (not too hard)", -1, cw_v2(x, y), bs, rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER);
  }
  /* the right panel: every player's score, whose turn, the ball */
  float rx = cs.x * 0.835f, ry = cs.y * 0.08f;
  for (int i = 0; i < g->players; i++) {
    const player_state *p = &g->P[i];
    bool now = i == g->cur && G.phase == PH_PLAY;
    char nm[16], sc[32];
    snprintf(nm, sizeof nm, "PLAYER %d", i + 1);
    fmt_score(p->score, sc, sizeof sc);
    float h = 30 * u + measure(sc, now ? 54 * u : 40 * u).height + 18 * u;
    float w = fminf(cs.x * 0.29f, 560 * u);
    cw_r2d_round_rect(cw_rect_make(rx - w * 0.5f, ry, w, h), 8 * u, 6, now ? rgba(0.08f, 0.06f, 0.1f, 0.85f) : rgba(0.03f, 0.03f, 0.04f, 0.6f));
    if (now) cw_r2d_round_rect_lines(cw_rect_make(rx - w * 0.5f, ry, w, h), 8 * u, 3 * u, 6, tcol(th->col[g->cur % 4], 1.2f, 1));
    text(nm, rx - w * 0.5f + 16 * u, ry + 6 * u, bs, now ? tcol(th->col[i % 4], 1.4f, 1) : rgba(0.7f, 0.7f, 0.75f, 1), CW_ALIGN_LEFT);
    if (p->ball <= BALLS_PER_GAME || g->players == 1) {
      char bl[16];
      snprintf(bl, sizeof bl, "BALL %d", CW_MIN(p->ball, BALLS_PER_GAME));
      text(bl, rx + w * 0.5f - 16 * u, ry + 6 * u, bs, rgba(0.8f, 0.8f, 0.85f, 1), CW_ALIGN_RIGHT);
    }
    text(sc, rx + w * 0.5f - 16 * u, ry + 30 * u, now ? 54 * u : 40 * u, rgba(1, 1, 1, now ? 1 : 0.75f), CW_ALIGN_RIGHT);
    ry += h + 10 * u;
  }
  ry += 10 * u;
  /* what is going on: the mode and its clock, multiball, the lock, the bonus */
  char b[64];
  const player_state *p = &g->P[g->cur];
  if (g->mode >= 0) {
    snprintf(b, sizeof b, "%s  %d", g->T.modes[g->mode].name, (g->mode_t + 59) / 60);
    ry += text(b, rx, ry, 32 * u, tcol(th->col[g->mode % 4], 1.4f, 1), CW_ALIGN_CENTER).height;
    snprintf(b, sizeof b, "%s: %d to go", g->T.modes[g->mode].goal, g->T.modes[g->mode].need - g->mode_hits);
    ry += text(b, rx, ry, bs, rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER).height + 8 * u;
  }
  if (g->multiball) ry += text("MULTIBALL: the jackpot is lit", rx, ry, bs, tcol(th->col[2], 1.5f, 1), CW_ALIGN_CENTER).height + 4 * u;
  else if (p->lock_lit) ry += text("LOCK IS LIT", rx, ry, bs, tcol(th->col[3], 1.5f, 1), CW_ALIGN_CENTER).height + 4 * u;
  if (p->mode_lit && g->mode < 0) ry += text("A MODE IS LIT AT THE SCOOP", rx, ry, bs, tcol(th->col[2], 1.4f, 1), CW_ALIGN_CENTER).height + 4 * u;
  snprintf(b, sizeof b, "BONUS X%d", p->bonus_x);
  ry += text(b, rx, ry, bs, rgba(0.85f, 0.85f, 0.9f, 1), CW_ALIGN_CENTER).height;
  if (g->tilt_warn > 0 && !g->tilted) text(g->tilt_warn == 1 ? "TILT WARNING" : "TILT WARNING 2", rx, ry + 8 * u, bs, rgba(1, 0.4f, 0.3f, 1), CW_ALIGN_CENTER);
}

static void hud_select(cw_vec2 cs, float u) {
  float y = panel_dmd(cs, u) + 20 * u;
  float bs = 26 * u, x = cs.x * 0.165f;
  const table *T = &G.g.T;
  y += text(T->name, x, y, 44 * u, tcol(T->th.col[2], 1.4f, 1), CW_ALIGN_CENTER).height + 4 * u;
  cw_text_style st = tstyle(bs, rgba(1, 1, 1, 0.85f));
  st.align = CW_ALIGN_CENTER, st.wrap_width = cs.x * 0.3f;
  y += cw_r2d_text(font, T->tagline, cw_v2(x - cs.x * 0.15f, y), &st).height + 24 * u;
  cw_prompt_text(font, "{ui_left|ui_right}: table", -1, cw_v2(x, y), bs, rgba(1, 1, 1, 0.92f), CW_ALIGN_CENTER);
  y += cw_prompt_text_size(font, "{ui_left}", -1, bs).y + 12 * u;
  char b[64];
  snprintf(b, sizeof b, "{ui_up|ui_down}: players (%d)", G.sel_players);
  cw_prompt_text(font, b, -1, cw_v2(x, y), bs, rgba(1, 1, 1, 0.92f), CW_ALIGN_CENTER);
  y += cw_prompt_text_size(font, "{ui_up}", -1, bs).y + 12 * u;
  cw_prompt_text(font, "{ui_accept}: play", -1, cw_v2(x, y), bs, rgba(1, 0.95f, 0.6f, 1), CW_ALIGN_CENTER);
  /* the right: this table's best five */
  float rx = cs.x * 0.835f, ry = cs.y * 0.1f;
  ry += text("BEST SCORES", rx, ry, 34 * u, tcol(T->th.col[2], 1.4f, 1), CW_ALIGN_CENTER).height + 12 * u;
  const hs_entry *hb = save()->best[G.sel_table];
  for (int i = 0; i < 5; i++) {
    char sc[32], line[64];
    if (hb[i].score > 0) snprintf(line, sizeof line, "%d  %.3s  %s", i + 1, hb[i].name, fmt_score(hb[i].score, sc, sizeof sc));
    else snprintf(line, sizeof line, "%d  ---", i + 1);
    ry += text(line, rx, ry, 30 * u, rgba(1, 1, 1, hb[i].score > 0 ? 0.95f : 0.5f), CW_ALIGN_CENTER).height + 6 * u;
  }
}

static void hud_entry(cw_vec2 cs, float u) {
  float y = panel_dmd(cs, u) + 24 * u, x = cs.x * 0.165f, bs = 26 * u;
  char b[64];
  snprintf(b, sizeof b, "PLAYER %d: A TOP FIVE SCORE", G.entry_player + 1);
  y += text(b, x, y, 30 * u, rgba(1, 0.9f, 0.5f, 1), CW_ALIGN_CENTER).height + 16 * u;
  cw_prompt_text(font, "{flip_left|flip_right}: letter", -1, cw_v2(x, y), bs, rgba(1, 1, 1, 0.92f), CW_ALIGN_CENTER);
  y += cw_prompt_text_size(font, "{flip_left}", -1, bs).y + 12 * u;
  cw_prompt_text(font, "{launch}: next letter", -1, cw_v2(x, y), bs, rgba(1, 1, 1, 0.92f), CW_ALIGN_CENTER);
}

static void hud(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 1080.0f;
  cw_r2d_begin(NULL);
  if (G.phase == PH_SELECT) hud_select(cs, u);
  else if (G.phase == PH_ENTRY) hud_entry(cs, u);
  else {
    hud_play(cs, u);
    if (G.phase == PH_OVER) {
      float y = cs.y * 0.8f;
      text("GAME OVER", cs.x * 0.165f, y, 48 * u, rgba(1, 0.4f, 0.3f, 1), CW_ALIGN_CENTER);
      if (G.phase_t > 60) cw_prompt_text(font, "{ui_accept}: choose a table", -1, cw_v2(cs.x * 0.165f, y + 64 * u), 26 * u, rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER);
    }
  }
  cw_r2d_end();
}

/* ── frame ───────────────────────────────────────────────────────────── */

static void render(void *u, double alpha) {
  (void)u, (void)alpha;
  const float dt = 1.0f / 60.0f; /* the view animates by frames: the same pictures on every host */
  music_update();
  if (G.built) {
    int w, h;
    cw_window_size(&w, &h);
    view_camera(&G.g, G.camera, (float)w / (float)h, dt);
    /* the balls where the simulation has them now, as the flippers are: drawn between the last two ticks (the step's
     * leftover, about 0 on a 60 Hz screen) they showed the ball a tick behind the flipper that had already hit it */
    view_sync(&G.g, 1.0f, dt);
    view_stats vs = view_get_stats();
    G.lights = vs.lights, G.lamps = vs.lamps_lit;
  }
  bool show_hud = true;
#ifdef PINBALL_AUTOPILOT
  show_hud = !getenv("PINBALL_NO_HUD"); /* key-art captures: the table alone */
#endif
  if (show_hud && G.in_match && cw_shell_screen_now() == CW_SHELL_PLAY) {
    dmd_draw();
    hud();
  }
  cw_quality_auto_measure(cw_shell_screen_now() == CW_SHELL_PLAY);
  if (getenv("PINBALL_BENCH_PRINT") && cw_shell_screen_now() == CW_SHELL_PLAY) { /* the perf gate reads these */
    static int n;
    static double t_last, t_sum;
    double now = cw_time();
    if (t_last > 0) t_sum += (now - t_last) * 1000;
    t_last = now;
    if (++n % 120 == 0) {
      cw_render3d_stats rs;
      cw_render3d_get_stats(cw_scene_app_tree(), &rs);
      fprintf(stderr, "bench frame_ms=%.2f balls=%d lights=%d lamps=%d draws=%d tris=%d\n", t_sum / 120, G.g.balls_in_play, G.lights, G.lamps, rs.draw_calls, rs.triangles);
      const cw_pass_stat *pst;
      int np = cw_pipeline_pass_stats(cw_pipeline_of(cw_scene_app_tree()), &pst);
      fprintf(stderr, "bench_passes gpu_total=%.2f", cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree())));
      for (int k = 0; k < np; k++)
        if (pst[k].ran && (pst[k].gpu_ms_avg > 0.02f || pst[k].cpu_ms_avg > 0.3f)) fprintf(stderr, " %s=%.2f/%.2f", pst[k].name, pst[k].gpu_ms_avg, pst[k].cpu_ms_avg);
      fprintf(stderr, "\n");
      t_sum = 0;
    }
  }
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Pinball", .width = 1280, .height = 720, .debug = true, .init = init, .warmup = warmup, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/main.cwscene", &app);
}
