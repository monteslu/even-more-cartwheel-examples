/* Lanternfall: a 2D exploration platformer on cartwheel (DESIGN.md).
 *
 *   lanternfall        the game: one player, a second can drop in on a pad at any time
 *   lanternfall_auto   the bot plays it through the input struct the pad fills, along the checker's route
 *   lanternfall_bench  the perf gate's scene: a warden fight in a lamp-lit arena with both players (carts read no
 *                   environment, so the scene is chosen at build time)
 * Environment (development, native): LANTERNFALL_ROOM=n starts in room n; LANTERNFALL_ABILITIES=hex grants abilities;
 * LANTERNFALL_BENCH=1 prints frame timings every 120 frames; LANTERNFALL_NO_HUD=1 hides the HUD (key art). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "bot.h"
#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/gpu.h"
#include "cartwheel/input.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/platform.h"
#include "cartwheel/project.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "hud.h"
#include "view.h"

static world W;
static sim S;
static hud_state H;
static bot B;
static void *route_bytes;
static size_t route_len;

static struct {
  bool loaded, in_match, snap;
  int players, last_region;
  cw_action left, right, up, down, jump, attack, dash, shoot, grapple, map;
  int32_t tick, room, region, abilities, pickups, bosses, hp, deaths, step, px, py, relics, won, screen;
  uint32_t hash;
  bool up_was[MAXP];
  double t_frame, t_last, t_gpu;
  int t_n;
} G;

static double wall(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ── the match ── */

static void new_game(int players) {
  sim_init(&S, &W, players);
#ifdef LANTERNFALL_AUTOPILOT
  S.assist = 3; /* the bot plays with the assist: every third hit costs a leaf */
#endif
  memset(&H, 0, sizeof H);
  H.banner_region = -1;
  G.last_region = -1;
#ifdef LANTERNFALL_BENCH
  S.god = true;
  S.abilities = AB_ALL;
  for (int i = 0; i < W.nrooms; i++)
    if (W.rooms[i].kind == RK_BOSS && W.rooms[i].boss == LANTERNFALL_BENCH) {
      const room *r = &W.rooms[i];
      sim_teleport(&S, (float)(r->x * CELL_W + 20), (float)(r->y * CELL_H + CELL_H - 2));
    }
#else
  if (getenv("LANTERNFALL_ABILITIES")) S.abilities = (uint32_t)strtoul(getenv("LANTERNFALL_ABILITIES"), NULL, 16);
  if (getenv("LANTERNFALL_ROOM")) {
    int n = atoi(getenv("LANTERNFALL_ROOM"));
    if (n >= 0 && n < W.nrooms) {
      const room *r = &W.rooms[n];
      int x = r->x * CELL_W + 6;
      for (int y = r->y * CELL_H + 3; y < (r->y + r->h) * CELL_H; y++)
        if (t_solid(W.t[y][x]) && !t_solid(W.t[y - 1][x]) && !t_solid(W.t[y - 2][x])) { sim_teleport(&S, (float)x + 0.5f, (float)y); break; }
    }
  }
#ifndef LANTERNFALL_AUTOPILOT
  /* the save: abilities, finds, opened ways, shrines, from the last rest */
  save_state *sv = cw_shell_game_save();
  if (sv->world_hash == W.hash) sim_restore(&S, sv);
  else H.intro_t = 9;
#endif
#endif
  G.snap = true;
}

static void start(int players, void *u) {
  (void)u;
  G.players = players;
  G.in_match = true;
  new_game(players);
#ifdef LANTERNFALL_AUTOPILOT
  if (route_bytes && !bot_load(&B, route_bytes, route_len, W.hash)) cw_warn("lanternfall: the route is for another world");
#endif
}

static void players_changed(int players, void *u) {
  (void)u;
  G.players = players;
  if (players > S.players) {
    S.players = players;
    player *p = &S.p[1];
    int16_t hm = S.p[0].hp_max, em = S.p[0].en_max;
    player_spawn(p, S.p[0].x - 1, S.p[0].y);
    p->hp_max = hm, p->hp = hm, p->en_max = em, p->en = em, p->active = 1, p->invuln = 60;
  }
}

static void shell_sound(cw_shell_sound s, void *u) {
  (void)u;
  sfx(s == CW_SHELL_SND_MOVE ? SFX_MOVE : s == CW_SHELL_SND_BACK ? SFX_BACK : SFX_SELECT, 0.8f);
}
static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *u) {
  (void)from, (void)u;
  if (to == CW_SHELL_TITLE) music_play(MOOD_GATE);
}
static void draw_background(cw_shell_screen sc, cw_vec2 canvas, void *u) {
  (void)sc, (void)canvas, (void)u;
}

static void init(void *u) {
  (void)u;
  cw_project_load_input();
  G.left = cw_action_find("move_left"), G.right = cw_action_find("move_right"), G.up = cw_action_find("move_up"), G.down = cw_action_find("move_down");
  G.jump = cw_action_find("jump"), G.attack = cw_action_find("attack"), G.dash = cw_action_find("dash"), G.shoot = cw_action_find("shoot");
  G.grapple = cw_action_find("grapple"), G.map = cw_action_find("map");
  cw_shell_init(&(cw_shell_desc){ .title = "LANTERNFALL", .subtitle = "light the old city again", .max_players = MAXP, .shared_screen = true,
                                  .game_save_size = sizeof(save_state), .game_save_version = 1, .start = start, .players_changed = players_changed,
                                  .sound = shell_sound, .screen_changed = shell_screen, .draw_background = draw_background });
  cw_font font = cw_ui_font_sans();
  cw_prompt_set_font(font);
  hud_init(font);
  cw_quality_full_resolution(true);
  if (!world_load(&W, "world.bin")) cw_error("lanternfall: world.bin is missing or from another build");
  route_bytes = cw_asset_load("route.bin", &route_len);
  moves_init();
  audio_init();
  view_init();
  G.loaded = true;
  sim_init(&S, &W, 1);
  cw_debug_i32("game.tick", &G.tick);
  cw_debug_i32("game.room", &G.room);
  cw_debug_i32("game.region", &G.region);
  cw_debug_i32("game.abilities", &G.abilities);
  cw_debug_i32("game.pickups", &G.pickups);
  cw_debug_i32("game.bosses", &G.bosses);
  cw_debug_i32("game.relics", &G.relics);
  cw_debug_i32("game.won", &G.won);
  cw_debug_i32("player.hp", &G.hp);
  cw_debug_i32("player.x100", &G.px);
  cw_debug_i32("player.y100", &G.py);
  cw_debug_i32("game.deaths", &G.deaths);
  cw_debug_i32("bot.step", &G.step);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_i32("shell.screen_game", &G.screen);
#ifdef LANTERNFALL_AUTOPILOT
#ifndef LANTERNFALL_AUTO_PLAYERS
#define LANTERNFALL_AUTO_PLAYERS 1
#endif
  cw_shell_start(LANTERNFALL_AUTO_PLAYERS);
#endif
}

/* ── input ── */

static void read_input(pinput in[MAXP]) {
  memset(in, 0, sizeof(pinput) * MAXP);
#ifdef LANTERNFALL_AUTOPILOT
  if (getenv("LANTERNFALL_IDLE")) return; /* (stills: nobody at the controls) */
  bot_input(&B, &S, 0, &in[0]);
  for (int i = 1; i < S.players; i++) bot_follow(&S, i, &in[i]);
#else
  for (int i = 0; i < S.players; i++) {
    if (!cw_shell_seat_taken(i)) continue;
    cw_vec2 m = cw_input_vector(i, G.left, G.right, G.up, G.down, CW_DEADZONE_RADIAL, 0.35f);
    in[i].mx = (int8_t)(m.x > 0.4f ? 1 : m.x < -0.4f ? -1 : 0);
    in[i].my = (int8_t)(m.y > 0.5f ? 1 : m.y < -0.5f ? -1 : 0);
    in[i].jump = cw_action_just_pressed(i, G.jump);
    in[i].jump_held = cw_action_pressed(i, G.jump);
    in[i].attack = cw_action_just_pressed(i, G.attack);
    in[i].shoot = cw_action_just_pressed(i, G.shoot);
    in[i].dash = cw_action_just_pressed(i, G.dash);
    in[i].grapple = cw_action_just_pressed(i, G.grapple);
    bool up = in[i].my < 0;
    in[i].interact = up && !G.up_was[i] && S.p[i].on_ground;
    G.up_was[i] = up;
  }
#endif
}

/* sounds, banners and cards from the tick's events */
static void react(void) {
  for (int i = 0; i < S.players; i++) {
    uint32_t e = S.p[i].ev;
    if (e & PE_JUMP) sfx(SFX_JUMP, 0.5f);
    if (e & (PE_DJUMP | PE_WALLJ)) sfx(SFX_DJUMP, 0.5f);
    if ((e & PE_LAND) && S.p[i].land_v > 14) sfx(SFX_LAND, fminf(0.6f, S.p[i].land_v / 40));
    if (e & PE_DASH) sfx(SFX_DASH, 0.5f);
    if (e & PE_POUND_LAND) sfx(SFX_POUND, 0.8f);
    if (e & PE_SPLASH) sfx(SFX_SPLASH, 0.5f);
    if (e & PE_STROKE) sfx(SFX_STROKE, 0.4f);
    if (e & PE_GRAPPLE) sfx(SFX_GRAPPLE, 0.5f);
    if (e & PE_CLING) sfx(SFX_CLING, 0.4f);
  }
  for (int i = 0; i < S.nev; i++) {
    const event *e = &S.ev[i];
    switch (e->kind) {
    case EV_STRIKE: sfx_pitch(SFX_STRIKE, 0.45f, 0.9f + 0.1f * (float)(S.tick % 3)); break;
    case EV_HIT: sfx(SFX_HIT, 0.5f); break;
    case EV_KILL: sfx(SFX_KILL, 0.5f); break;
    case EV_BOLT: sfx(SFX_BOLT, 0.45f); break;
    case EV_HURT: sfx(SFX_HURT, 0.6f); break;
    case EV_DIE: sfx(SFX_DIE, 0.7f); G.snap = true; break;
    case EV_PICKUP: sfx(SFX_PICKUP, 0.7f), H.card_kind = e->a, H.card_arg = e->b, H.card_t = 5; break;
    case EV_SAVE:
      sfx(SFX_SAVE, 0.6f);
#ifndef LANTERNFALL_AUTOPILOT
      {
        save_state *sv = cw_shell_game_save();
        sim_save(&S, sv);
        cw_shell_save_commit();
        hud_menu_build(&S, &H);
        H.menu_open = H.menu_n > 1;
      }
#endif
      break;
    case EV_BREAK: sfx(SFX_BREAK, 0.6f); break;
    case EV_SECRET: sfx(SFX_SECRET, 0.6f); break;
    case EV_BOSS_START: H.boss_idx = e->a, H.boss_t = 3; music_play(MOOD_BOSS); break;
    case EV_BOSS_TELL: sfx(SFX_TELL, 0.5f); break;
    case EV_BOSS_ACT: sfx(SFX_BOSS_ACT, 0.5f); break;
    case EV_BOSS_HIT: sfx(SFX_HIT, 0.45f); break;
    case EV_BOSS_DOWN: sfx(SFX_BOSS_DOWN, 0.8f); break;
    case EV_ENEMY_TELL: sfx(SFX_ENEMY_TELL, 0.3f); break;
    case EV_SHOT: sfx(SFX_SHOT, 0.3f); break;
    case EV_SEAL: sfx(SFX_DOOR, 0.8f); break;
    default: break;
    }
  }
  int region = S.room >= 0 ? W.rooms[S.room].region : 0;
  if (region != G.last_region) {
    H.banner_region = region, H.banner_t = 3.5f;
    G.last_region = region;
  }
  if (!S.b.active) music_play(region_mood(region));
}

static void fixed_update(void *u, double dt) {
  (void)u, (void)dt;
  G.screen = (int32_t)cw_shell_screen_now();
  if (!cw_shell_update()) return;
  if (!G.in_match) return;
  cw_shell_join_press();
  if (H.banner_t > 0) H.banner_t -= (float)DT;
  if (H.card_t > 0) H.card_t -= (float)DT;
  if (H.intro_t > 0) H.intro_t -= (float)DT;
  if (S.won) H.won_t += (float)DT;
#ifndef LANTERNFALL_AUTOPILOT
  /* the map and the shrine's menu hold the game still */
  if (cw_action_just_pressed(0, G.map) && !H.menu_open) { H.map_open = !H.map_open; sfx(SFX_MAP, 0.5f); return; }
  if (H.map_open) {
    if (cw_action_just_pressed(0, G.attack)) H.map_open = false, sfx(SFX_BACK, 0.5f);
    return;
  }
  if (H.menu_open) {
    cw_vec2 m = cw_input_vector(0, G.left, G.right, G.up, G.down, CW_DEADZONE_RADIAL, 0.5f);
    static int held;
    int dir = m.y > 0.5f ? 1 : m.y < -0.5f ? -1 : 0;
    if (dir && !held) H.menu_sel = (H.menu_sel + dir + H.menu_n) % H.menu_n, sfx(SFX_MOVE, 0.5f);
    held = dir;
    if (cw_action_just_pressed(0, G.attack)) H.menu_open = false, sfx(SFX_BACK, 0.5f);
    if (cw_action_just_pressed(0, G.jump)) {
      int e = H.menu_ents[H.menu_sel];
      H.menu_open = false;
      if (e != S.save_ent) {
        S.save_ent = e;
        sim_teleport(&S, W.ents[e].x, W.ents[e].y);
        G.snap = true;
        sfx(SFX_SAVE, 0.6f);
      }
    }
    return;
  }
#endif
  pinput in[MAXP];
  read_input(in);
  sim_step(&S, in);
  react();
  view_events(&S);
  G.tick = (int32_t)S.tick, G.room = S.room, G.region = S.room >= 0 ? W.rooms[S.room].region : -1, G.abilities = (int32_t)S.abilities;
  G.pickups = 0;
  for (int k = 0; k < 4; k++) G.pickups += sim_pickups_got(&S, k);
  G.bosses = __builtin_popcount(S.bosses_down), G.hp = S.p[0].hp, G.deaths = S.deaths, G.relics = S.relics, G.won = S.won;
  G.px = (int32_t)lroundf(S.p[0].x * 100), G.py = (int32_t)lroundf(S.p[0].y * 100), G.step = B.i, G.hash = S.hash;
}

static void render(void *u, double alpha) {
  (void)u, (void)alpha;
  if (!G.loaded) return;
  cw_shell_screen sc = cw_shell_screen_now();
  static float t;
  t += 1.0f / 60.0f;
  static bool bench_env, bench_read;
  if (!bench_read) bench_env = getenv("LANTERNFALL_BENCH") != NULL, bench_read = true;
  double tb0 = 0;
  if (bench_env) cw_gpu_finish(), tb0 = wall(); /* (the bench: this frame alone, to GPU completion) */
  view_camera(&S, 1.0f / 60.0f, G.snap);
  G.snap = false;
  if (sc == CW_SHELL_PLAY || sc == CW_SHELL_PAUSE || !G.in_match) view_tick(&S, sc == CW_SHELL_PLAY ? 1.0f / 60.0f : 0);
  view_draw(&S, t);
  bool show_hud = G.in_match && (sc == CW_SHELL_PLAY || sc == CW_SHELL_PAUSE);
#ifdef LANTERNFALL_AUTOPILOT
  if (getenv("LANTERNFALL_NO_HUD")) show_hud = false;
#endif
  if (show_hud) hud_draw(&S, &H, t);
  if (bench_env) cw_gpu_finish(), G.t_gpu += (wall() - tb0) * 1000;
  double now = wall();
  if (G.t_last > 0) G.t_frame += (now - G.t_last) * 1000;
  G.t_last = now;
  G.t_n++;
  if (getenv("LANTERNFALL_BENCH") && sc == CW_SHELL_PLAY && G.t_n >= 120) {
    view_stats vs = view_get_stats();
    cw_pipeline *pl = cw_pipeline_of(cw_scene_app_tree());
    fprintf(stderr, "bench submit_ms=%.3f end_ms=%.3f frame_ms=%.2f draw_ms=%.3f gpu_ms=%.3f sprites=%d lights=%d particles=%d enemies=%d room=%d boss=%d\n", (double)vs.submit_ms, (double)vs.end_ms, G.t_frame / G.t_n, G.t_gpu / G.t_n,
            pl ? cw_pipeline_gpu_ms(pl) : -1.0, vs.sprites, vs.lights, vs.particles, vs.enemies, S.room, S.b.active);
    const cw_pass_stat *pst;
    int np = pl ? cw_pipeline_pass_stats(pl, &pst) : 0;
    fprintf(stderr, "bench_passes gpu_total=%.3f", pl ? cw_pipeline_gpu_ms(pl) : -1.0);
    for (int k = 0; k < np; k++)
      if (pst[k].ran) fprintf(stderr, " %s=%.3f/%.3f", pst[k].name, pst[k].gpu_ms_avg, pst[k].cpu_ms_avg);
    fprintf(stderr, "\n");
    G.t_frame = 0, G.t_n = 0, G.t_gpu = 0;
  }
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Lanternfall", .width = 1280, .height = 720, .debug = true, .init = init, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/main.cwscene", &app);
}
