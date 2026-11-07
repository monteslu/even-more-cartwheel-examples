/* skate (working title): a skateboarding game for one to four players in split screen, on cartwheel and three.c
 * (DESIGN.md).
 *
 *   shell     title, join (one to four players), pause, settings
 *   setup     the mode (free skate, score attack, score battle), the park, each player's skater
 *   the run   sim.c on the fixed tick from the players' input (or the bots'), view.c and pose.c draw it between ticks,
 *             hud.c on top, one view per player
 *   results   the scores, the goals, the park's high score; again, or back to the setup
 *
 * skate_auto: bots skate a score battle on each park in turn (for hosts that send no input, and the gates).
 * skate_bench: four bots in a four-way split on the mega ramp park (the perf gate's scene).
 * Environment (native development only): SKATE_PARK=n, SKATE_MODE=n, SKATE_PLAYERS=n for the auto builds, SKATE_BENCH=1
 * prints timings every 120 frames, SKATE_QUALITY=0..4 a graphics preset, SKATE_NO_HUD=1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/profile.h"
#include "cartwheel/project.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "view.h"

#ifdef SKATE_AUTOPILOT
#define AUTO 1
#else
#define AUTO 0
#endif
#ifndef SKATE_BENCH
#define SKATE_BENCH 0
#endif

enum { PH_SETUP, PH_RUN, PH_RESULTS };

typedef struct save_data {
  int32_t best[PARK_COUNT], best_combo[PARK_COUNT];
  uint8_t skin[MAXP], mode, park, pad[2];
} save_data;

static struct {
  sim S;
  skater prev[MAXP];
  bool built, in_match;
  int phase, players, results_t, best_shown, view_park_id, rag_park_id;
  bool new_best;
  ui_state ui;
  bot bots[MAXP];
  cw_action steer[4], ollie, flip, grab, grind, manual, reset;
  cw_action ui_up, ui_down, ui_left, ui_right, ui_accept, ui_back;
  float t;
  int last_tick_seen;
  /* debug fields */
  int32_t d_phase, d_park, d_mode, d_tick, d_players, d_state[MAXP], d_score[MAXP], d_tricks[MAXP], d_bails[MAXP], d_grinds[MAXP], d_manuals[MAXP], d_tokens[MAXP],
    d_best_combo[MAXP], d_lines[MAXP], d_goals[MAXP], d_rag, d_clock;
  float d_pos[MAXP * 3], d_speed[MAXP], d_air[MAXP];
  uint32_t d_hash;
  double t_frame, t_last, t_sim, t_view;
  int t_n;
} G;

static cw_font font;

static double wall(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ── the world ──────────────────────────────────────────────────────────── */

static bool build_world_step(void) {
  if (G.built) return true;
  cw_node root = cw_tree_root(cw_scene_app_tree());
  int done, total;
  if (!view_init_step(root, &done, &total)) {
    cw_loading_set(done, total + 1, CW_LOAD_MESHES);
    return false;
  }
  G.built = true;
  G.view_park_id = G.rag_park_id = -1;
  return true;
}
static void build_world(void) {
  while (!build_world_step()) {}
}

/* the sim on park p (the run's, or the setup's preview) and the view of it */
static void park_go(int p, int mode, int players) {
  for (int i = 0; i < MAXP; i++) G.S.skin[i] = (int)G.ui.skin[i];
  sim_init(&G.S, p, mode, players);
  memcpy(G.prev, G.S.s, sizeof G.prev);
  if (G.view_park_id != p) view_park(&G.S.k), G.view_park_id = p;
  if (G.rag_park_id != p) rag_park(&G.S.k), G.rag_park_id = p;
}

static void begin_run(void) {
  park_go(G.ui.park, G.ui.mode, G.players);
  view_rigs(G.ui.skin, G.players);
  for (int i = 0; i < MAXP; i++) bot_init(&G.bots[i], 1000u + (uint32_t)i * 77u + (uint32_t)G.ui.park * 13u, AUTO ? 2 : 1);
  G.phase = PH_RUN;
  G.results_t = 0;
  save_data *sd = cw_shell_game_save();
  sd->mode = (uint8_t)G.ui.mode, sd->park = (uint8_t)G.ui.park;
  for (int i = 0; i < MAXP; i++) sd->skin[i] = (uint8_t)G.ui.skin[i];
  cw_shell_save_commit();
}

static int env_int(const char *name, int def) {
  const char *e = getenv(name);
  return e && *e ? atoi(e) : def;
}

static void start(int players, void *u) {
  (void)u;
  build_world();
  G.players = players;
  G.in_match = true;
  save_data *sd = cw_shell_game_save();
  G.ui.mode = sd->mode % MODE_COUNT, G.ui.park = sd->park % PARK_COUNT;
  for (int i = 0; i < MAXP; i++) G.ui.skin[i] = sd->skin[i] % NSKINS;
  if (!sd->skin[1] && !sd->skin[2] && !sd->skin[3]) for (int i = 0; i < MAXP; i++) G.ui.skin[i] = i % NSKINS;
#if AUTO
  G.ui.mode = env_int("SKATE_MODE", SKATE_BENCH ? MODE_FREE : players > 1 ? MODE_BATTLE : MODE_ATTACK);
  G.ui.park = env_int("SKATE_PARK", SKATE_BENCH ? PARK_MEGA : PARK_SUNNY) % PARK_COUNT;
  for (int i = 0; i < MAXP; i++) G.ui.skin[i] = i % NSKINS;
  begin_run();
#else
  G.phase = PH_SETUP;
  G.ui.row = 0;
  park_go(G.ui.park, MODE_FREE, players);
  view_rigs(G.ui.skin, players);
#endif
}

static void players_changed(int players, void *u) {
  (void)u;
  if (players > G.players && G.phase == PH_RUN) {
    for (int i = G.players; i < players && i < MAXP; i++) {
      G.ui.skin[i] = i % NSKINS;
      G.S.s[i].rng = 0x51ed2701u + (uint32_t)i * 0x9e3779b9u;
      sk_spawn(&G.S.s[i], &G.S.k, i);
    }
    G.S.players = players;
  }
  G.players = players;
  view_rigs(G.ui.skin, players);
}

static void shell_sound(cw_shell_sound s, void *u) {
  (void)u;
  audio_menu(s == CW_SHELL_SND_MOVE ? 0 : s == CW_SHELL_SND_BACK ? 2 : 1);
}
static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *u) {
  (void)from, (void)u;
  audio_music(to == CW_SHELL_TITLE || to == CW_SHELL_PLAY || to == CW_SHELL_JOIN);
}
static void quit_to_title(void *u) {
  (void)u;
  G.in_match = false;
  G.phase = PH_SETUP;
}
static void draw_background(cw_shell_screen s, cw_vec2 canvas, void *u) { (void)s, (void)canvas, (void)u; } /* the park shows through */

static bool warmup(void *u) {
  (void)u;
  if (!build_world_step()) return false;
  if (G.view_park_id < 0) park_go(PARK_SUNNY, MODE_FREE, 1); /* a park behind the title */
  return true;
}

/* ── debug commands (the gates and romdev) ─────────────────────────────── */
static void cmd_run(int argc, const char **argv, cw_reply *r) {
  /* run <park> [mode] [players]: a run now */
  if (argc < 2) { cw_reply_error(r, "usage: run <park> [mode] [players]"); return; }
  G.ui.park = atoi(argv[1]) % PARK_COUNT;
  if (argc > 2) G.ui.mode = atoi(argv[2]) % MODE_COUNT;
  if (argc > 3) G.players = CW_CLAMP(atoi(argv[3]), 1, MAXP);
  begin_run();
  cw_reply_printf(r, "{\"park\":%d,\"mode\":%d,\"players\":%d}", G.ui.park, G.ui.mode, G.players);
}

static void init(void *u) {
  (void)u;
  cw_project_load_input();
  G.steer[0] = cw_action_find("steer_left"), G.steer[1] = cw_action_find("steer_right"), G.steer[2] = cw_action_find("steer_up"), G.steer[3] = cw_action_find("steer_down");
  G.ollie = cw_action_find("ollie"), G.flip = cw_action_find("flip"), G.grab = cw_action_find("grab"), G.grind = cw_action_find("grind"), G.manual = cw_action_find("manual");
  G.reset = cw_action_find("reset");
  cw_shell_init(&(cw_shell_desc){ .title = "SKATE", .subtitle = "parks, lines and combos for one to four", .max_players = MAXP, .game_save_size = sizeof(save_data),
                                  .game_save_version = 1, .start = start, .players_changed = players_changed, .sound = shell_sound, .screen_changed = shell_screen,
                                  .quit = quit_to_title, .draw_background = draw_background });
  G.ui_up = cw_action_find("ui_up"), G.ui_down = cw_action_find("ui_down"), G.ui_left = cw_action_find("ui_left"), G.ui_right = cw_action_find("ui_right");
  G.ui_accept = cw_action_find("ui_accept"), G.ui_back = cw_action_find("ui_back");
  font = cw_ui_font_sans();
  cw_prompt_set_font(font);
  hud_init(font);
  audio_init();
  cw_quality_full_resolution(true); /* no preset draws below the output size */
  if (getenv("SKATE_QUALITY")) cw_quality_set((cw_quality)CW_CLAMP(atoi(getenv("SKATE_QUALITY")), 0, CW_QUALITY_COUNT - 1), true);
  cw_debug_i32("game.phase", &G.d_phase);
  cw_debug_i32("game.park", &G.d_park);
  cw_debug_i32("game.mode", &G.d_mode);
  cw_debug_i32("game.tick", &G.d_tick);
  cw_debug_i32("game.players", &G.d_players);
  cw_debug_i32("game.clock", &G.d_clock);
  cw_debug_u32("game.hash", &G.d_hash);
  cw_debug_field("skater.state", G.d_state, CW_DBG_I32, MAXP);
  cw_debug_field("skater.score", G.d_score, CW_DBG_I32, MAXP);
  cw_debug_field("skater.tricks", G.d_tricks, CW_DBG_I32, MAXP);
  cw_debug_field("skater.bails", G.d_bails, CW_DBG_I32, MAXP);
  cw_debug_field("skater.grinds", G.d_grinds, CW_DBG_I32, MAXP);
  cw_debug_field("skater.manuals", G.d_manuals, CW_DBG_I32, MAXP);
  cw_debug_field("skater.tokens", G.d_tokens, CW_DBG_I32, MAXP);
  cw_debug_field("skater.best_combo", G.d_best_combo, CW_DBG_I32, MAXP);
  cw_debug_field("skater.goals", G.d_goals, CW_DBG_I32, MAXP);
  cw_debug_field("skater.pos", G.d_pos, CW_DBG_F32, MAXP * 3);
  cw_debug_field("skater.speed", G.d_speed, CW_DBG_F32, MAXP);
  cw_debug_field("skater.max_air", G.d_air, CW_DBG_F32, MAXP);
  cw_debug_field("bot.lines", G.d_lines, CW_DBG_I32, MAXP);
  cw_debug_i32("rag.live", &G.d_rag);
  cw_debug_f32("view.cam_turn_max", &g_view_metrics.cam_turn_max); /* degrees a frame, the most the view turned */
  cw_debug_f32("view.cam_dist", &g_view_metrics.cam_dist);
  cw_debug_f32("view.foot_err_max", &g_view_metrics.foot_err_max); /* metres, the feet off the deck at worst */
  cw_debug_i32("view.foot_err_state", &g_view_metrics.foot_err_state); /* what it was doing then: state * 10 + 1 pushing + 2 manual */
  cw_debug_u32("tex.hash", &tex_hash);
  cw_debug_command("run", "run <park> [mode] [players]: start a run now", cmd_run);
#if AUTO
  if (getenv("SKATE_CAM")) sscanf(getenv("SKATE_CAM"), "%f,%f", &g_cam_k, &g_cam_side); /* key art: SKATE_CAM=k,side */
  cw_shell_start(env_int("SKATE_PLAYERS", SKATE_BENCH ? 4 : 2));
#endif
}

/* ── input ──────────────────────────────────────────────────────────────── */

static void read_input(input in[MAXP]) {
  memset(in, 0, sizeof(input) * MAXP);
  for (int i = 0; i < G.S.players; i++) {
#if AUTO
    bot_tick(&G.bots[i], &G.S, i, &in[i]);
#else
    if (!cw_shell_seat_taken(i)) continue;
    cw_vec2 m = cw_input_vector(i, G.steer[0], G.steer[1], G.steer[2], G.steer[3], CW_DEADZONE_SCALED_RADIAL, 0.18f);
    in[i].sx = m.x, in[i].sy = -m.y;
    in[i].ollie = cw_action_pressed(i, G.ollie);
    in[i].flip = cw_action_pressed(i, G.flip);
    in[i].grab = cw_action_pressed(i, G.grab);
    in[i].grind = cw_action_pressed(i, G.grind);
    in[i].manual = cw_action_pressed(i, G.manual);
    in[i].reset = cw_action_pressed(i, G.reset);
#endif
  }
}

static void setup_update(void) {
  ui_state *ui = &G.ui;
  int p1 = 0;
  if (cw_action_just_pressed(p1, G.ui_up)) ui->row = (ui->row + 2) % 3, audio_menu(0);
  if (cw_action_just_pressed(p1, G.ui_down)) ui->row = (ui->row + 1) % 3, audio_menu(0);
  int d = cw_action_just_pressed(p1, G.ui_right) - cw_action_just_pressed(p1, G.ui_left);
  if (d) {
    audio_menu(0);
    if (ui->row == 0) ui->mode = (ui->mode + MODE_COUNT + d) % MODE_COUNT;
    if (ui->row == 1) {
      ui->park = (ui->park + PARK_COUNT + d) % PARK_COUNT;
      park_go(ui->park, MODE_FREE, G.players);
    }
    if (ui->row == 2) ui->skin[0] = (ui->skin[0] + NSKINS + d) % NSKINS, view_rigs(ui->skin, G.players);
  }
  for (int i = 1; i < G.players; i++) {
    if (!cw_shell_seat_taken(i)) continue;
    int e = cw_action_just_pressed(i, G.steer[1]) - cw_action_just_pressed(i, G.steer[0]);
    if (e) ui->skin[i] = (ui->skin[i] + NSKINS + e) % NSKINS, view_rigs(ui->skin, G.players), audio_menu(0);
  }
  save_data *sd = cw_shell_game_save();
  ui->best = sd->best[ui->park];
  if (cw_action_just_pressed(p1, G.ui_accept)) { audio_menu(1); begin_run(); }
}

static void run_update(void) {
  input in[MAXP];
  read_input(in);
  memcpy(G.prev, G.S.s, sizeof G.prev);
  double t0 = wall();
  sim_step(&G.S, in);
  if (AUTO && getenv("SKATE_BAIL_AT") && G.S.tick == atoi(getenv("SKATE_BAIL_AT"))) sk_force_bail(&G.S.s[0]);
  G.t_sim += (wall() - t0) * 1000;
  audio_events(&G.S, G.players);
  for (int i = 0; i < G.players; i++) {
    const skater *s = &G.S.s[i];
    char b[48];
    if (s->ev & EV_COMBO) {
      snprintf(b, sizeof b, "+%d", s->last_banked);
      hud_popup(i, b, s->last_banked, 0);
    }
    if (s->ev & EV_LOST) hud_popup(i, "BAIL", 0, 1);
    if (s->ev & EV_TOKEN) hud_popup(i, "TOKEN", 0, 2);
    if ((s->ev & EV_GAP) && !(s->ev & EV_COMBO)) hud_popup(i, "GAP", 0, 2);
  }
  if (G.S.state == RUN_OVER) {
    G.phase = PH_RESULTS;
    G.results_t = 0;
    save_data *sd = cw_shell_game_save();
    int best = 0;
    for (int i = 0; i < G.players; i++) best = CW_MAX(best, G.S.s[i].score);
    G.new_best = G.S.mode == MODE_ATTACK && best > sd->best[G.S.park_id];
    if (G.new_best) sd->best[G.S.park_id] = best;
    for (int i = 0; i < G.players; i++) sd->best_combo[G.S.park_id] = CW_MAX(sd->best_combo[G.S.park_id], G.S.s[i].best_combo);
    G.best_shown = sd->best[G.S.park_id];
    cw_shell_save_commit();
  }
}

static void results_update(void) {
  G.results_t++;
  audio_roll(&G.S, G.players, false);
#if AUTO
  if (G.results_t > TPS * 6) { /* on to the next park */
    G.ui.park = (G.ui.park + 1) % PARK_COUNT;
    begin_run();
  }
  return;
#endif
  if (G.results_t < 30) return;
  if (cw_action_just_pressed(0, G.ui_accept)) { audio_menu(1); begin_run(); }
  else if (cw_action_just_pressed(0, G.ui_back)) {
    audio_menu(2);
    G.phase = PH_SETUP;
    park_go(G.ui.park, MODE_FREE, G.players);
  }
}

static void fixed_update(void *u, double dt) {
  (void)u, (void)dt;
  if (!cw_shell_update()) return;
  if (!G.in_match) return;
  cw_shell_join_press();
  if (G.phase == PH_SETUP) setup_update();
  else if (G.phase == PH_RUN) run_update();
  else results_update();
  rag_step();
  /* debug fields */
  G.d_phase = G.phase, G.d_park = G.S.park_id, G.d_mode = G.S.mode, G.d_tick = G.S.tick, G.d_players = G.S.players, G.d_hash = G.S.hash, G.d_clock = G.S.clock;
  for (int i = 0; i < MAXP; i++) {
    const skater *s = &G.S.s[i];
    G.d_state[i] = s->st, G.d_score[i] = s->score, G.d_tricks[i] = s->tricks, G.d_bails[i] = s->bails, G.d_grinds[i] = s->grinds, G.d_manuals[i] = s->manuals;
    G.d_tokens[i] = s->tokens, G.d_best_combo[i] = s->best_combo, G.d_goals[i] = (int32_t)G.S.goals[i], G.d_lines[i] = G.bots[i].lines_done;
    G.d_pos[i * 3] = s->p.x, G.d_pos[i * 3 + 1] = s->p.y, G.d_pos[i * 3 + 2] = s->p.z, G.d_speed[i] = vlen(s->v), G.d_air[i] = s->max_air;
  }
  G.d_rag = rag_live();
}

/* ── the frame ──────────────────────────────────────────────────────────── */

static void render(void *u, double alpha) {
  (void)u;
  cw_shell_screen sc = cw_shell_screen_now();
  float dt = 1.0f / 60.0f;
  G.t += dt;
  if (G.built) {
    double t0 = wall();
    if (G.in_match && (G.phase == PH_RUN || G.phase == PH_RESULTS)) {
      view_frame(&G.S, G.prev, sc == CW_SHELL_PLAY ? (float)alpha : 1.0f, G.players, sc == CW_SHELL_PLAY ? dt : 0);
      audio_roll(&G.S, G.players, sc == CW_SHELL_PLAY && G.phase == PH_RUN);
    } else {
      if (G.in_match) view_frame(&G.S, G.prev, 1.0f, 0, dt);
      view_title_camera(&G.S.k, G.t);
      audio_roll(&G.S, G.players, false);
    }
    G.t_view += (wall() - t0) * 1000;
  }
  bool hud = !getenv("SKATE_NO_HUD");
  if (hud && G.in_match && (sc == CW_SHELL_PLAY || sc == CW_SHELL_PAUSE)) {
    CW_PROFILE_SCOPE("skate.hud");
    cw_r2d_begin(NULL);
    if (G.phase == PH_SETUP) hud_setup(&G.ui, G.players);
    else {
      hud_draw(&G.S, G.players, 1, G.t);
      if (G.phase == PH_RESULTS) hud_results(&G.S, G.players, G.best_shown, G.new_best);
    }
    cw_r2d_end();
  }
  cw_quality_auto_measure(sc == CW_SHELL_PLAY && G.phase == PH_RUN);
  double now = wall();
  if (G.t_last > 0) G.t_frame += (now - G.t_last) * 1000;
  G.t_last = now;
  G.t_n++;
  if (getenv("SKATE_BENCH") && sc == CW_SHELL_PLAY && G.t_n >= 120) {
    cw_render3d_stats rs;
    cw_render3d_get_stats(cw_scene_app_tree(), &rs);
    fprintf(stderr, "bench frame_ms=%.2f sim_ms=%.2f view_ms=%.2f tick=%d park=%d players=%d draws=%d tris=%d score=%d,%d,%d,%d rag=%d\n", G.t_frame / G.t_n, G.t_sim / G.t_n,
            G.t_view / G.t_n, G.S.tick, G.S.park_id, G.players, rs.draw_calls, rs.triangles, G.S.s[0].score, G.S.s[1].score, G.S.s[2].score, G.S.s[3].score, rag_live());
    const cw_pass_stat *pst;
    int np = cw_pipeline_pass_stats(cw_pipeline_of(cw_scene_app_tree()), &pst);
    fprintf(stderr, "bench_passes gpu_total=%.2f", cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree())));
    for (int k = 0; k < np; k++)
      if (pst[k].ran && (pst[k].gpu_ms_avg > 0.02f || pst[k].cpu_ms_avg > 0.3f)) fprintf(stderr, " %s=%.2f/%.2f", pst[k].name, pst[k].gpu_ms_avg, pst[k].cpu_ms_avg);
    fprintf(stderr, "\n");
    fflush(stderr);
    G.t_sim = G.t_view = G.t_frame = 0;
    G.t_n = 0;
  }
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Skate", .width = 1280, .height = 720, .debug = true, .init = init, .warmup = warmup, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app(NULL, &app);
}
