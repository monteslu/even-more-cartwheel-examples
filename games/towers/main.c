/* towers (working title): a tower defense game for one to four players on one machine, on cartwheel and three.c
 * (DESIGN.md).
 *
 *   play      each player's cursor hops between the build slots of their own share of the map (stick or d-pad); the
 *             four face buttons build the four towers on an empty slot (the build diamond), and on a tower of theirs
 *             upgrade it, change its aim or (held) sell it. One shared wave comes down the paths for the core.
 *   camera    fixed for the map: the whole of it on one screen.
 *   towers_auto plays itself (for hosts that send no input); towers_bench is the perf gate's scene.
 * Environment (development, native): TOWERS_MAP=n starts on map n; TOWERS_PLAYERS=n the auto build's players;
 * TOWERS_BENCH=1 prints timings every 120 frames; TOWERS_BAKE=<file> bakes the map's lightmap there (the bake tool,
 * tools/mapview, does it for every map); TOWERS_QUALITY=0..4 a graphics preset; TOWERS_CROWD=n keeps n creeps on the
 * paths (the bench's); TOWERS_WAVE=n starts at wave n with towers on every slot (screenshots). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/profile.h"
#include "cartwheel/project.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "game.h"

static double wall(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

typedef struct save_data { int32_t best_map, maps_won; } save_data;

enum { PH_PICK, PH_PLAY };
static struct {
  sim S;
  tdmap M;
  bool in_match, built;
  int players, phase, pick_map, over_ticks, bench_crowd;
  int32_t state, map, wave, waves, core, enemies, kills, towers, maps_won, best_map, drawn, leaks, next_t, phase_dbg;
  int32_t gold[MAXP], slot[MAXP];
  float pu[MAXP], pv[MAXP];
  uint32_t hash;
  int banner_t, banner_kind;
  int kills0[MAXP], wave_kills[MAXP]; /* each player's kills when the last wave came, and in the wave before it (the banner's line) */
  float dmg0[MAXP], wave_dmg[MAXP];
  cw_action move[4], act[A_KINDS], call, power;
  cw_node camera;
  int lm_frames;
  int pick_held[MAXP];
  double t_sim, t_view, t_fx, t_frame, t_last;
  int t_n;
} G;

static cw_font font;
static cw_sound snd_zap, snd_frost, snd_beam, snd_mortar, snd_boom, snd_build, snd_upgrade, snd_sell, snd_leak, snd_wave, snd_hop, snd_nogold, snd_kill, snd_won,
  snd_lost, snd_move, snd_select, snd_back, snd_music;
static cw_voice music;

/* ── sounds: synthesised, so the repository needs no audio files ───── */

static cw_sound synth(float seconds, float hz0, float hz1, float noise, float gain, float decay) {
  enum { RATE = 48000 };
  static float pcm[RATE * 3];
  int n = CW_MIN((int)(seconds * RATE), RATE * 3);
  double phase = 0;
  uint32_t r = 7;
  float lp = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (float)n;
    phase += 2 * 3.14159265358979 * (hz0 + (hz1 - hz0) * t) / RATE;
    r = r * 1664525u + 1013904223u;
    float nz = ((float)(r >> 8) / 8388608.0f - 1.0f);
    lp += (nz - lp) * 0.12f;
    pcm[i] = ((float)sin(phase) * (1 - noise) + lp * noise * 2.2f) * gain * (float)exp(-t * decay) * (i < 200 ? (float)i / 200.0f : 1);
  }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}
static void play(cw_sound s, float gain) { cw_audio_play(s, &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = gain }); }
static void shell_sound(cw_shell_sound s, void *u) {
  (void)u;
  play(s == CW_SHELL_SND_MOVE ? snd_move : s == CW_SHELL_SND_BACK ? snd_back : snd_select, 1);
}
static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *u) {
  (void)from, (void)u;
  bool want = to == CW_SHELL_TITLE || to == CW_SHELL_PLAY;
  if (want && !cw_voice_is_playing(music)) music = cw_audio_play(snd_music, &(cw_play_desc){ .bus = cw_shell_music_bus(), .loop = true, .volume = 0.4f });
  if (!want && cw_voice_is_playing(music)) cw_voice_stop(music, 0.3f);
}

/* ── the match ──────────────────────────────────────────────────────── */

static bool build_world_step(void) {
  if (G.built) return true;
  static bool view_done;
  cw_node root = cw_tree_root(cw_scene_app_tree());
  if (!view_done) {
    int done, total;
    view_done = view_init_step(root, &done, &total);
    cw_loading_set(done, total + 1, CW_LOAD_MESHES);
    return false;
  }
  fx_init(root);
  G.camera = cw_node_get(root, "Main/Camera");
  G.built = true;
  return true;
}
static void build_world(void) {
  while (!build_world_step()) {}
}

#ifndef TOWERS_BENCH_CROWD
#define TOWERS_BENCH_CROWD 0
#endif
#ifndef TOWERS_BENCH_MAP
#define TOWERS_BENCH_MAP 0
#endif

/* towers on every slot (the bench's scene and the screenshots'): the four kinds in turn, levels by the wave */
static void fill_towers(int wave) {
  for (int k = 0; k < G.S.m.nslot; k++) {
    tower *t = &G.S.tw[k];
    float x = mp_wx(&G.S.m, G.S.m.slot[k].x), y = mp_wz(&G.S.m, G.S.m.slot[k].y);
    int kind = (k * 7 + 3) % TK_KINDS, lvl = CW_CLAMP(1 + wave / 8 + (k % 3 == 0), 1, 3);
    int branch = lvl < 3 ? BR_NONE : (k / TK_KINDS) % 2 ? BR_B : BR_A; /* the third levels: both branches of every kind */
    *t = (tower){ .alive = true, .kind = (uint8_t)kind, .level = (uint8_t)lvl, .owner = (uint8_t)sim_slot_owner(&G.S, k), .x = x, .y = y, .branch = (uint8_t)branch, .tgt = -1, .tgt2 = { -1, -1 }, .beam_k = 1, .age = 100,
                  .cd = k * 7 % 60, .spent = sim_tower_cost(kind, 1) };
  }
}

static bool map_go(int n) {
  int count = map_count();
  if (count > 0 && n > count) n = count;
  if (!map_load(n, &G.M, sim_kind_by_name)) return false;
  sim_map_start(&G.S, &G.M);
  G.bench_crowd = getenv("TOWERS_CROWD") ? atoi(getenv("TOWERS_CROWD")) : TOWERS_BENCH_CROWD;
  int wave0 = getenv("TOWERS_WAVE") ? atoi(getenv("TOWERS_WAVE")) : 0;
  if (G.bench_crowd > 0 || wave0 > 0) {
    G.S.god = G.bench_crowd > 0;
    int w = wave0 > 0 ? wave0 : 18;
    fill_towers(w);
    for (int k = 1; k < w && G.S.wave < G.S.m.waves - 1; k++) G.S.wave++; /* (the waves before it: passed) */
    G.S.next_t = 1;
    if (G.bench_crowd > 0) { /* the bench's crowd starts spread along the whole path (a late wave in full flow), not piled at the gates */
      static const int MIX[8] = { CK_SKITTER, CK_GHOUL, CK_SKITTER, CK_HOUND, CK_IMP, CK_SKITTER, CK_OGRE, CK_SHADE };
      int tiles = 0;
      for (int y = 0; y < G.S.m.h; y++)
        for (int x = 0; x < G.S.m.w; x++) tiles += G.S.m.t[y][x] == T_PATH;
      int per = CW_MAX(1, (G.bench_crowd + tiles - 1) / CW_MAX(tiles, 1)), k = 0;
      for (int y = 0; y < G.S.m.h && G.S.nenemies < G.bench_crowd; y++)
        for (int x = 0; x < G.S.m.w && G.S.nenemies < G.bench_crowd; x++) {
          if (G.S.m.t[y][x] != T_PATH) continue;
          for (int q = 0; q < per && G.S.nenemies < G.bench_crowd; q++, k++) {
            int kind = MIX[k % 8];
            sim_spawn(&G.S, kind, kind == CK_OGRE ? 900 : 300, mp_wx(&G.S.m, (float)x) + (float)(q & 1) * 0.9f - 0.45f, mp_wz(&G.S.m, (float)y) + (float)((q >> 1) & 1) * 0.9f - 0.45f);
          }
        }
    }
  }
  view_map(&G.S);
  for (int p = 0; p < MAXP; p++) G.kills0[p] = G.S.p[p].kills, G.dmg0[p] = G.S.p[p].dmg, G.wave_kills[p] = 0, G.wave_dmg[p] = 0;
  G.lm_frames = 0;
  G.banner_t = 200, G.banner_kind = 0;
  save_data *sd = cw_shell_game_save();
  if (n > sd->best_map) sd->best_map = n, G.best_map = n, cw_shell_save_commit();
  return true;
}

static int first_map(void) {
  if (getenv("TOWERS_MAP")) return CW_MAX(1, atoi(getenv("TOWERS_MAP")));
  if (TOWERS_BENCH_MAP) return TOWERS_BENCH_MAP;
  return G.pick_map > 0 ? G.pick_map : 1;
}

static void begin_play(void) {
  sim_init(&G.S, G.players, 4242);
  G.phase = PH_PLAY;
  G.over_ticks = 0;
  if (!map_go(first_map())) map_go(1);
}

static void start(int players, void *u) {
  (void)u;
  build_world();
  G.players = players;
  G.in_match = true;
  G.pick_map = CW_MAX(1, CW_MIN(G.best_map, map_count() > 0 ? map_count() : 1));
#ifdef TOWERS_AUTOPILOT
  begin_play();
#else
  G.phase = PH_PICK;
  sim_init(&G.S, players, 4242);
  map_go(G.pick_map);
  G.S.no_spawn = true;
#endif
}

static void players_changed(int players, void *u) {
  (void)u;
  for (int i = 0; i < players && i < MAXP; i++)
    if (!G.S.p[i].active && cw_shell_seat_taken(i)) sim_add_player(&G.S, i);
  G.players = players;
}

static bool warmup(void *u) {
  (void)u;
  if (!build_world_step()) return false;
  if (!G.in_match) { /* a map behind the title, and the draws the match makes compiled */
    sim_init(&G.S, 1, 4242);
    map_go(CW_MAX(1, G.best_map));
    G.S.no_spawn = true;
  }
  return true;
}

#ifdef TOWERS_AUTOPILOT
static void cmd_map(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: map <n>"); return; }
  int n = atoi(argv[1]);
  if (!map_go(n)) { cw_reply_error(r, "no such map"); return; }
  cw_reply_printf(r, "{\"map\":%d}", n);
}
#endif

static void init(void *u) {
  (void)u;
  cw_project_load_input();
  const char *names[4] = { "move_left", "move_right", "move_up", "move_down" };
  for (int i = 0; i < 4; i++) G.move[i] = cw_action_find(names[i]);
  G.act[A_N] = cw_action_find("act_n"), G.act[A_W] = cw_action_find("act_w"), G.act[A_E] = cw_action_find("act_e"), G.act[A_S] = cw_action_find("act_s");
  G.call = cw_action_find("call");
  G.power = cw_action_find("power");
  cw_shell_init(&(cw_shell_desc){ .title = "TOWERS", .subtitle = "hold the core together", .max_players = MAXP, .shared_screen = true,
                                  .game_save_size = sizeof(save_data), .game_save_version = 1, .start = start, .players_changed = players_changed,
                                  .sound = shell_sound, .screen_changed = shell_screen });
  font = cw_ui_font_sans();
  cw_prompt_set_font(font);
  cw_quality_full_resolution(true); /* no preset draws below the output size: no upscaling */
  if (getenv("TOWERS_QUALITY")) cw_quality_set((cw_quality)CW_CLAMP(atoi(getenv("TOWERS_QUALITY")), 0, CW_QUALITY_COUNT - 1), true);
  snd_zap = synth(0.22f, 1800, 600, 0.85f, 0.35f, 6);
  snd_frost = synth(0.5f, 900, 300, 0.75f, 0.3f, 4);
  snd_beam = synth(0.3f, 300, 900, 0.2f, 0.25f, 3);
  snd_mortar = synth(0.25f, 90, 50, 0.5f, 0.6f, 6);
  snd_boom = synth(0.8f, 70, 30, 0.75f, 0.9f, 3.5f);
  snd_build = synth(0.35f, 220, 440, 0.3f, 0.5f, 3);
  snd_upgrade = synth(0.5f, 440, 1320, 0.1f, 0.45f, 2.5f);
  snd_sell = synth(0.3f, 1300, 1900, 0.1f, 0.3f, 4);
  snd_leak = synth(0.6f, 180, 90, 0.3f, 0.7f, 2.5f);
  snd_wave = synth(1.2f, 110, 165, 0.15f, 0.6f, 1.6f);
  snd_hop = synth(0.04f, 880, 880, 0, 0.18f, 2);
  snd_nogold = synth(0.15f, 140, 120, 0.2f, 0.35f, 4);
  snd_kill = synth(0.1f, 240, 110, 0.7f, 0.25f, 5);
  snd_won = synth(2.0f, 330, 990, 0.05f, 0.6f, 1.0f);
  snd_lost = synth(2.0f, 220, 55, 0.3f, 0.7f, 1.0f);
  snd_move = synth(0.05f, 660, 660, 0, 0.25f, 2);
  snd_select = synth(0.12f, 520, 1040, 0, 0.3f, 3);
  snd_back = synth(0.12f, 520, 260, 0, 0.3f, 3);
  snd_music = synth(2.0f, 65, 65, 0.05f, 0.25f, 0.2f);
  G.best_map = ((save_data *)cw_shell_game_save())->best_map;
  G.maps_won = ((save_data *)cw_shell_game_save())->maps_won;
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.phase", &G.phase_dbg);
  cw_debug_i32("game.map", &G.map);
  cw_debug_i32("game.wave", &G.wave);
  cw_debug_i32("game.waves", &G.waves);
  cw_debug_i32("game.core", &G.core);
  cw_debug_i32("game.enemies", &G.enemies);
  cw_debug_i32("game.kills", &G.kills);
  cw_debug_i32("game.leaks", &G.leaks);
  cw_debug_i32("game.towers", &G.towers);
  cw_debug_i32("game.next_wave", &G.next_t);
  cw_debug_i32("game.maps_won", &G.maps_won);
  cw_debug_field("player.gold", G.gold, CW_DBG_I32, MAXP);
  cw_debug_field("player.slot", G.slot, CW_DBG_I32, MAXP);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_i32("view.drawn", &G.drawn);
  cw_debug_f32("view.p1u", &G.pu[0]), cw_debug_f32("view.p1v", &G.pv[0]), cw_debug_f32("view.p2u", &G.pu[1]), cw_debug_f32("view.p2v", &G.pv[1]);
  cw_debug_f32("view.p3u", &G.pu[2]), cw_debug_f32("view.p3v", &G.pv[2]), cw_debug_f32("view.p4u", &G.pu[3]), cw_debug_f32("view.p4v", &G.pv[3]);
  cw_debug_f32("dev.cam_k", &g_cam_k);
  cw_debug_f32("dev.tilt", &g_tilt);
  cw_debug_f32("dev.lean", &g_lean);
#ifdef TOWERS_AUTOPILOT
  cw_debug_command("map", "map <n>: go to that map", cmd_map);
  if (getenv("TOWERS_BOT_BRANCH")) g_bot_branch = CW_CLAMP(atoi(getenv("TOWERS_BOT_BRANCH")), 0, 2);
  if (getenv("TOWERS_CAM")) sscanf(getenv("TOWERS_CAM"), "%f,%f,%f", &g_cam_k, &g_cam_x, &g_cam_z); /* key art: TOWERS_CAM=k,x,z */
#ifndef TOWERS_AUTO_PLAYERS
#define TOWERS_AUTO_PLAYERS 2
#endif
  cw_shell_start(getenv("TOWERS_PLAYERS") ? CW_CLAMP(atoi(getenv("TOWERS_PLAYERS")), 1, 4) : TOWERS_AUTO_PLAYERS);
#endif
}

/* ── input ──────────────────────────────────────────────────────────── */

static void read_input(input in[MAXP]) {
  memset(in, 0, sizeof(input) * MAXP);
  for (int i = 0; i < G.S.players; i++) {
#ifdef TOWERS_AUTOPILOT
    towers_bot(&G.S, i, &in[i]);
#else
    if (!cw_shell_seat_taken(i)) continue;
    cw_vec2 m = cw_input_vector(i, G.move[0], G.move[1], G.move[2], G.move[3], CW_DEADZONE_RADIAL, 0.5f);
    float l = sqrtf(m.x * m.x + m.y * m.y);
    if (l > 0.5f) { /* eight ways: a direction counts past 22.5 degrees off an axis */
      in[i].dx = (int8_t)(fabsf(m.x) > l * 0.38f ? (m.x > 0 ? 1 : -1) : 0);
      in[i].dy = (int8_t)(fabsf(m.y) > l * 0.38f ? (m.y > 0 ? 1 : -1) : 0);
    }
    for (int a = 0; a < A_KINDS; a++) in[i].act[a] = cw_action_just_pressed(i, G.act[a]);
    in[i].sell_held = cw_action_pressed(i, G.act[A_W]);
    in[i].wave = cw_action_just_pressed(i, G.call);
    in[i].power = cw_action_just_pressed(i, G.power);
#endif
  }
}

static void pick_update(void) {
  int count = CW_MAX(map_count(), 1), unlocked = CW_CLAMP(G.best_map + 1, 1, count);
#ifdef TOWERS_ALL_MAPS
  unlocked = count;
#endif
  for (int i = 0; i < G.players; i++) {
    if (!cw_shell_seat_taken(i)) continue;
    cw_vec2 m = cw_input_vector(i, G.move[0], G.move[1], G.move[2], G.move[3], CW_DEADZONE_RADIAL, 0.5f);
    int dir = m.x > 0.5f ? 1 : m.x < -0.5f ? -1 : 0;
    if (dir && !G.pick_held[i]) {
      int n = G.pick_map + dir;
      if (n < 1) n = unlocked;
      if (n > unlocked) n = 1;
      if (n != G.pick_map) G.pick_map = n, map_go(n), G.S.no_spawn = true, play(snd_move, 0.8f);
    }
    G.pick_held[i] = dir;
    if (cw_action_just_pressed(i, G.act[A_S])) { play(snd_select, 0.9f); begin_play(); return; }
  }
}

static int g_ticks; /* fixed ticks run since the last frame drew: the view's clock and the effects run on them */
static void fixed_update(void *u, double dt) {
  (void)u, (void)dt;
  g_ticks++;
  if (!cw_shell_update()) return;
  if (!G.in_match) return;
  if (cw_shell_join_press()) {} /* (players_changed adds them) */
  G.phase_dbg = G.phase;
  if (G.phase == PH_PICK) { pick_update(); return; }
  input in[MAXP];
  read_input(in);
  double t0 = wall();
  sim_step(&G.S, in);
  if (G.bench_crowd > 0) { /* the bench keeps its crowd up: more from the gates as they fall */
    for (int k = 0; k < 24 && G.S.nenemies < G.bench_crowd; k++) {
      float gx, gz;
      mp_gate_pos(&G.S.m, k % G.S.m.nspawn, &gx, &gz);
      static const int MIX[8] = { CK_SKITTER, CK_GHOUL, CK_SKITTER, CK_HOUND, CK_IMP, CK_SKITTER, CK_OGRE, CK_SHADE };
      int kind = MIX[(G.S.tick + k) % 8];
      sim_spawn(&G.S, kind, kind == CK_OGRE ? 900 : 300, gx + (float)((k * 37) % 13) * 0.1f - 0.6f, gz + (float)((k * 53) % 11) * 0.1f - 0.5f);
    }
  }
  G.t_sim += (wall() - t0) * 1000;
  int kills = 0;
  for (int k = G.S.ev_start; k < G.S.ev_n; k++) { /* sounds */
    const event *e = &G.S.ev[k];
    switch (e->kind) {
    case EV_ZAP: if (e->b == 0) play(snd_zap, 0.25f); break;
    case EV_POWER: play(snd_boom, 0.6f), play(snd_zap, 0.6f); break;
    case EV_FROST: play(snd_frost, e->b ? 0.4f : 0.22f); break;
    case EV_BEAM_ON: play(snd_beam, 0.2f); break;
    case EV_MORTAR: play(snd_mortar, 0.35f); break;
    case EV_BOOM: play(snd_boom, e->b ? 0.5f : 0.3f); break;
    case EV_BUILD: play(snd_build, 0.6f); break;
    case EV_UPGRADE: play(snd_upgrade, 0.6f); break;
    case EV_SELL: play(snd_sell, 0.6f); break;
    case EV_NOGOLD: play(snd_nogold, 0.5f); break;
    case EV_HOP: play(snd_hop, 0.5f); break;
    case EV_AIM: play(snd_select, 0.5f); break;
    case EV_LEAK: play(snd_leak, 0.6f); break;
    case EV_WAVE:
      play(snd_wave, 0.7f), G.banner_t = 150, G.banner_kind = 1;
      for (int p = 0; p < MAXP; p++) /* who did what in the wave that went before: the banner's line */
        G.wave_kills[p] = G.S.p[p].kills - G.kills0[p], G.kills0[p] = G.S.p[p].kills, G.wave_dmg[p] = G.S.p[p].dmg - G.dmg0[p], G.dmg0[p] = G.S.p[p].dmg;
      break;
    case EV_WON: play(snd_won, 0.8f); break;
    case EV_LOST: play(snd_lost, 0.8f); break;
    case EV_KILL: kills++; break;
    default: break;
    }
  }
  if (kills && G.S.tick % 4 == 0) play(snd_kill, CW_MIN(0.15f + 0.02f * (float)kills, 0.4f));
  if (G.S.state == ST_WON) {
    if (G.S.state_ticks == 1) {
      save_data *sd = cw_shell_game_save();
      sd->maps_won++, G.maps_won = sd->maps_won;
      if (G.S.m.num + 1 > sd->best_map) sd->best_map = G.S.m.num + 1, G.best_map = sd->best_map;
      cw_shell_save_commit();
    }
    if (G.S.state_ticks > 60 * 5) { /* on to the next map */
      int next = G.S.m.num + 1;
      if (map_count() > 0 && next > map_count()) next = 1;
      int players = G.S.players;
      sim_init(&G.S, players, 4242 + (uint32_t)next);
      for (int i = 0; i < MAXP; i++) G.S.p[i].active = i < players && (i == 0 || cw_shell_seat_taken(i));
      map_go(next);
    }
  } else if (G.S.state == ST_LOST) {
    if (G.S.state_ticks > 60 * 5) { /* the core fell: the map again */
      int players = G.S.players;
      sim_init(&G.S, players, 4242 + (uint32_t)G.S.tick);
      for (int i = 0; i < MAXP; i++) G.S.p[i].active = i < players && (i == 0 || cw_shell_seat_taken(i));
      map_go(G.M.num);
    }
  }
  G.state = G.S.state, G.map = G.S.m.num, G.wave = G.S.wave, G.waves = G.S.m.waves, G.core = G.S.core_hp, G.enemies = G.S.nenemies, G.kills = G.S.kills;
  G.leaks = G.S.leaks, G.next_t = G.S.next_t, G.hash = G.S.hash;
  int tw = 0;
  for (int k = 0; k < G.S.m.nslot; k++) tw += G.S.tw[k].alive;
  G.towers = tw;
  for (int i = 0; i < MAXP; i++) G.gold[i] = G.S.p[i].gold, G.slot[i] = G.S.p[i].slot;
  if (G.banner_t > 0) G.banner_t--;
}

/* ── the HUD ────────────────────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static cw_text_style tstyle(float size, cw_color c) { return (cw_text_style){ .size = size, .color = c, .outline_width = size * 0.09f, .outline_color = rgba(0, 0, 0, 0.9f) }; }
static cw_text_metrics measure(const char *s, float size) {
  cw_text_style st = tstyle(size, rgba(1, 1, 1, 1));
  return cw_font_measure(font, s, &st);
}
static cw_text_metrics text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_style st = tstyle(size, c);
  if (al != CW_ALIGN_LEFT) {
    cw_text_metrics m = cw_font_measure(font, s, &st);
    x -= al == CW_ALIGN_CENTER ? m.width * 0.5f : m.width;
  }
  return cw_r2d_text(font, s, cw_v2(x, y), &st);
}
static const char *const THEME_NAME[MP_THEMES] = { "MEADOW", "AUTUMN WOODS", "DESERT", "SNOWFIELDS", "VOLCANO", "CRYSTAL VALE" };
static const char *const ROMAN[4] = { "", "I", "II", "III" };
static const char *const ACT_TAG[A_KINDS] = { "{act_n}", "{act_w}", "{act_e}", "{act_s}" };

/* a pill of text with prompts at (x, y): anchored by ax, ay (0 left/top .. 1 right/bottom) */
static cw_vec2 pill(const char *markup, int player, float x, float y, float ax, float ay, float size, cw_color c, cw_color bg) {
  cw_vec2 sz = cw_prompt_text_size(font, markup, player, size);
  float pad = size * 0.28f, w = sz.x + pad * 2, h = sz.y + pad * 1.2f;
  float l = x - w * ax, t = y - h * ay;
  cw_r2d_rect(cw_rect_make(l, t, w, h), bg);
  cw_prompt_text(font, markup, player, cw_v2(l + pad, t + pad * 0.6f), size, c, CW_ALIGN_LEFT);
  return (cw_vec2){ w, h };
}

/* the build diamond (an empty slot) or the tower's actions round a player's cursor, while the player is choosing (for
 * three seconds after the cursor hops or the tower under it changes, and while selling); then the labels fade and one
 * short line stays under the cursor. Where two cursors stand close, the later one's labels move up or down out of the
 * earlier ones' way (their P1..P4 says whose they are). One player alone keeps the full labels. */
static float g_lab[MAXP][2];
static int g_nlab;
float g_cursor_k[MAXP]; /* how much of each cursor's detail shows (the labels and the reach rings): 0 .. 1 */
static void cursor_detail(void) {
  static int last_slot[MAXP] = { -1, -1, -1, -1 }, last_key[MAXP], quiet[MAXP];
  for (int i = 0; i < MAXP; i++) {
    const player *p = &G.S.p[i];
    const tower *t = p->slot >= 0 ? &G.S.tw[p->slot] : NULL;
    int key = t && t->alive ? 1 + t->kind * 64 + t->level * 8 + t->aim + t->branch * 512 : 0;
    if (p->slot != last_slot[i] || key != last_key[i] || p->sell_t > 0 || G.S.wave == 0) quiet[i] = 0;
    else quiet[i]++;
    last_slot[i] = p->slot, last_key[i] = key;
    float k = 1.0f - cw_smoothstep(180, 210, (float)quiet[i]);
    g_cursor_k[i] = G.S.players <= 1 ? 1.0f : k;
  }
}
static void hud_cursor(int i, float u, cw_vec2 cs) {
  const player *p = &G.S.p[i];
  if (!p->active || p->slot < 0) return;
  float x = mp_wx(&G.S.m, G.S.m.slot[p->slot].x), z = mp_wz(&G.S.m, G.S.m.slot[p->slot].y), su, sv;
  if (!view_screen((t3_vec3){ x, MP_GROUND_H, z }, &su, &sv)) return;
  float cx = su * cs.x, cy = sv * cs.y, bs = 26 * u, gap = view_px_per_m(cs.y) * 1.25f, k = g_cursor_k[i];
  if (k > 0.01f) {
    for (int pass = 0, dir = cy > cs.y * 0.5f ? -1 : 1; pass < MAXP; pass++) {
      bool hit = false;
      for (int j = 0; j < g_nlab; j++) hit |= fabsf(cx - g_lab[j][0]) < 15 * gap && fabsf(cy - g_lab[j][1]) < 4.6f * gap;
      if (!hit) break;
      cy += (float)dir * 4.8f * gap;
    }
    if (g_nlab < MAXP) g_lab[g_nlab][0] = cx, g_lab[g_nlab++][1] = cy;
  }
  const float *pc = PCOL[i];
  cw_color bg = rgba(0.02f, 0.03f, 0.06f, 0.72f * k), grey = rgba(0.6f, 0.6f, 0.62f, 0.75f * k);
  const tower *t = &G.S.tw[p->slot];
  char b[128];
  cw_color pcol = rgba(0.6f + 0.4f * pc[0], 0.6f + 0.4f * pc[1], 0.6f + 0.4f * pc[2], 1);
  if (k > 0.01f) {
    if (!t->alive) {
      /* north, west, east, south: the four towers where their buttons sit */
      static const float AX[A_KINDS] = { 0.5f, 1.0f, 0.0f, 0.5f }, AY[A_KINDS] = { 1.0f, 0.5f, 0.5f, 0.0f };
      static const float DX[A_KINDS] = { 0, -1, 1, 0 }, DY[A_KINDS] = { -1, 0, 0, 1 };
      for (int a = 0; a < A_KINDS; a++) {
        int cost = sim_tower_cost(a, 1);
        bool afford = p->gold >= cost;
        snprintf(b, sizeof b, "%s %s %d", ACT_TAG[a], sim_tower_name(a), cost);
        const float *tc = TCOL[a];
        cw_color c = afford ? rgba(0.55f + 0.45f * tc[0], 0.55f + 0.45f * tc[1], 0.55f + 0.45f * tc[2], k) : grey;
        pill(b, i, cx + DX[a] * gap, cy + DY[a] * gap * 0.8f, AX[a], AY[a], bs, c, bg);
      }
    } else {
      if (t->level == 1) {
        int cost = sim_tower_cost(t->kind, 2);
        snprintf(b, sizeof b, "{act_s} upgrade %d", cost);
        pill(b, i, cx, cy + gap * 0.8f, 0.5f, 0.0f, bs, p->gold >= cost ? rgba(0.7f, 1, 0.6f, k) : grey, bg);
      } else if (t->level == 2) { /* the third level: two branches, south and east, each with what it does */
        int cost = sim_tower_cost(t->kind, 3);
        bool ok = p->gold >= cost;
        snprintf(b, sizeof b, "{act_s} %s %d: %s", sim_branch_name(t->kind, BR_A), cost, sim_branch_does(t->kind, BR_A));
        pill(b, i, cx, cy + gap * 0.8f, 0.5f, 0.0f, bs, ok ? rgba(0.7f, 1, 0.6f, k) : grey, bg);
        snprintf(b, sizeof b, "{act_e} %s %d: %s", sim_branch_name(t->kind, BR_B), cost, sim_branch_does(t->kind, BR_B));
        pill(b, i, cx + gap, cy, 0.0f, 0.5f, bs, ok ? rgba(1, 0.85f, 0.55f, k) : grey, bg);
      }
      if (t->kind != TK_FROST) {
        snprintf(b, sizeof b, "{act_n} aim: %s", sim_aim_name(t->aim));
        pill(b, i, cx, cy - gap * 0.8f, 0.5f, 1.0f, bs, rgba(0.85f, 0.9f, 1, k), bg);
      }
      snprintf(b, sizeof b, "{act_w} hold: sell %d", sim_sell_value(t));
      cw_vec2 sz = pill(b, i, cx - gap, cy, 1.0f, 0.5f, bs, rgba(1, 0.8f, 0.6f, k), bg);
      if (p->sell_t > 0) { /* the hold's progress under it */
        float q = (float)p->sell_t / 36.0f;
        cw_r2d_rect(cw_rect_make(cx - gap - sz.x, cy + sz.y * 0.5f, sz.x * q, 5 * u), rgba(1, 0.6f, 0.3f, 1));
      }
    }
  }
  /* the cursor's own line, always: P1..P4 and the tower under it (its level and branch) over the cursor */
  if (t->alive)
    snprintf(b, sizeof b, "P%d %s %s%s%s", i + 1, sim_tower_name(t->kind), ROMAN[t->level], t->level >= 3 ? " " : "", t->level >= 3 ? sim_branch_name(t->kind, t->branch) : "");
  else
    snprintf(b, sizeof b, "P%d", i + 1);
  float top = cy - gap * 0.8f - (k > 0.01f && t->alive && t->kind != TK_FROST ? bs * 1.55f : 0) - measure(b, bs).height - 2 * u;
  if (k < 0.99f && t->alive) top = top + (cy - gap * 0.6f - measure(b, bs).height - top) * (1 - k); /* settles in close over the cursor as the labels fade */
  text(b, cx, top, bs, pcol, CW_ALIGN_CENTER);
}

static void hud_player(int i, float u, cw_vec2 cs) {
  const player *p = &G.S.p[i];
  if (!p->active) return;
  const float *pc = PCOL[i];
  float bs = 26 * u;
  char b[64], g[32];
  snprintf(b, sizeof b, "P%d", i + 1);
  snprintf(g, sizeof g, "%d", p->gold);
  cw_text_metrics nm = measure(b, bs), gm = measure(g, 36 * u);
  float w = 16 * u + nm.width + 14 * u + 26 * u + gm.width + 14 * u, h = fmaxf(nm.height, gm.height) + 12 * u;
  bool right = i & 1, bottom = i & 2;
  float x = right ? cs.x - w - 12 * u : 12 * u, y = bottom ? cs.y - h - 12 * u : 12 * u;
  cw_r2d_rect(cw_rect_make(x, y, w, h), rgba(0.02f, 0.03f, 0.06f, 0.72f));
  cw_r2d_rect(cw_rect_make(x, y, 6 * u, h), rgba(pc[0], pc[1], pc[2], 1));
  text(b, x + 16 * u, y + (h - nm.height) * 0.5f, bs, rgba(0.6f + 0.4f * pc[0], 0.6f + 0.4f * pc[1], 0.6f + 0.4f * pc[2], 1), CW_ALIGN_LEFT);
  float cx = x + 16 * u + nm.width + 14 * u + 10 * u, cy = y + h * 0.5f;
  cw_r2d_circle(cw_v2(cx, cy), 10 * u, 20, rgba(1, 0.8f, 0.25f, 1)); /* a coin */
  cw_r2d_circle(cw_v2(cx, cy), 6 * u, 20, rgba(1, 0.92f, 0.55f, 1));
  text(g, cx + 16 * u, y + (h - gm.height) * 0.5f, 36 * u, rgba(1, 0.92f, 0.6f, 1), CW_ALIGN_LEFT);
  { /* the storm: ready (its button) or charging (a bar filling under the panel) */
    float by = bottom ? y - 6 * u : y + h + 6 * u;
    if (p->power_cd <= 0) {
      float glow = 0.85f + 0.15f * cw_sin((float)G.S.tick * 0.05f); /* (a slow glow, never a blink) */
      pill("{power} storm", i, right ? x + w : x, by, right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f, bs, rgba(0.75f * glow, 0.85f * glow, 1, 1), rgba(0.02f, 0.03f, 0.06f, 0.72f));
    } else {
      float k = 1.0f - (float)p->power_cd / (float)POWER_CD, bh = 8 * u, bx = x, bt = bottom ? by - bh : by;
      cw_r2d_rect(cw_rect_make(bx, bt, w, bh), rgba(0.02f, 0.03f, 0.06f, 0.72f));
      cw_r2d_rect(cw_rect_make(bx, bt, w * k, bh), rgba(0.45f, 0.6f, 1, 0.9f));
    }
  }
}

static void hud(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 1080.0f, bs = 26 * u;
  char b[128];
  cw_r2d_begin(NULL);
  if (G.phase == PH_PICK) {
    int count = CW_MAX(map_count(), 1);
    float y = cs.y * 0.08f;
    y += text("CHOOSE A MAP", cs.x * 0.5f, y, 60 * u, rgba(1, 0.88f, 0.5f, 1), CW_ALIGN_CENTER).height + 10 * u;
    float pw = cs.x * 0.42f, ph = 0;
    const char *nm = G.S.m.name[0] ? G.S.m.name : "UNNAMED";
    snprintf(b, sizeof b, "MAP %d OF %d", G.S.m.num, count);
    ph = measure(b, 30 * u).height + measure(nm, 52 * u).height + measure(THEME_NAME[0], bs).height * 2 + 70 * u;
    cw_r2d_rect(cw_rect_make(cs.x * 0.5f - pw * 0.5f, y, pw, ph), rgba(0.02f, 0.03f, 0.06f, 0.75f));
    float ty = y + 14 * u;
    ty += text(b, cs.x * 0.5f, ty, 30 * u, rgba(0.8f, 0.85f, 1, 1), CW_ALIGN_CENTER).height + 6 * u;
    ty += text(nm, cs.x * 0.5f, ty, 52 * u, rgba(1, 1, 1, 1), CW_ALIGN_CENTER).height + 6 * u;
    ty += text(THEME_NAME[CW_CLAMP(G.S.m.theme, 0, MP_THEMES - 1)], cs.x * 0.5f, ty, bs, rgba(1, 0.85f, 0.55f, 1), CW_ALIGN_CENTER).height + 4 * u;
    snprintf(b, sizeof b, "%d waves", G.S.m.waves);
    text(b, cs.x * 0.5f, ty, bs, rgba(0.9f, 0.9f, 0.9f, 1), CW_ALIGN_CENTER);
    float py = cs.y * 0.86f;
    pill("{move_left|move_right}: choose a map      {act_s}: start", -1, cs.x * 0.5f, py, 0.5f, 0.0f, bs, rgba(1, 1, 1, 1), rgba(0.02f, 0.03f, 0.06f, 0.75f));
    cw_r2d_end();
    return;
  }
  for (int i = 0; i < G.S.players; i++) hud_player(i, u, cs);
#ifdef TOWERS_AUTOPILOT
  bool cursors = !getenv("TOWERS_NO_CURSORS");
#else
  bool cursors = true;
#endif
  g_nlab = 0;
  if (cursors && G.S.state == ST_PLAY)
    for (int i = 0; i < G.S.players; i++) hud_cursor(i, u, cs);
  /* the top line: the wave, the core, the next wave */
  {
    snprintf(b, sizeof b, "WAVE %d / %d", G.S.wave, G.S.m.waves);
    cw_text_metrics wm = measure(b, 34 * u);
    char c[32];
    snprintf(c, sizeof c, "CORE %d", G.S.core_hp);
    cw_text_metrics cm = measure(c, 34 * u);
    float w = wm.width + cm.width + 60 * u, h = wm.height + 12 * u, x = cs.x * 0.5f - w * 0.5f, y = 10 * u;
    cw_r2d_rect(cw_rect_make(x, y, w, h), rgba(0.02f, 0.03f, 0.06f, 0.72f));
    text(b, x + 16 * u, y + 6 * u, 34 * u, rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_LEFT);
    float hurt = (float)G.S.core_hp / (float)CORE_HP;
    /* low: the count glows amber and back, slowly (never a blink, never a red flash) */
    float lo = hurt < 0.35f ? 0.5f + 0.5f * cw_sin((float)G.S.tick * 0.07f) : 0.0f;
    cw_color cc = rgba(0.55f + 0.45f * (1 - hurt), 0.85f, 1, 1), warn = rgba(1, 0.6f, 0.32f, 1);
    if (g_strobe_control && hurt < 0.35f) lo = (G.S.tick / 15) % 2 ? 1.0f : 0.0f, warn = rgba(1, 0.3f, 0.25f, 1);
    text(c, x + w - 16 * u, y + 6 * u, 34 * u, rgba(cc.r + (warn.r - cc.r) * lo, cc.g + (warn.g - cc.g) * lo, cc.b + (warn.b - cc.b) * lo, 1), CW_ALIGN_RIGHT);
    float ny = y + h + 6 * u;
    if (G.S.state == ST_PLAY && G.S.next_t > 0) {
      int secs = (G.S.next_t + 59) / 60;
      if (G.S.wave == 0) snprintf(b, sizeof b, "the first wave in %d   {call}: send it now", secs);
      else snprintf(b, sizeof b, "the next wave in %d   {call}: send it now, +%d gold each", secs, G.S.next_t / 60);
      cw_vec2 tsz = pill(b, -1, cs.x * 0.5f, ny, 0.5f, 0.0f, bs, rgba(1, 1, 1, 0.95f), rgba(0.02f, 0.03f, 0.06f, 0.6f));
      int ks[2], nk = sim_wave_kinds(&G.S, G.S.wave + 1, ks);
      if (nk) { /* what comes next, so the towers can be chosen for it */
        static const char *const KN[CK_KINDS] = { "skitters", "ghouls", "hounds", "imps", "sporelings", "bloaters", "shades", "ogres" };
        bool boss = (G.S.wave + 1) % 10 == 0 || G.S.wave + 1 == G.S.m.waves;
        snprintf(b, sizeof b, "next: %s%s%s%s", KN[ks[0]], nk > 1 ? " and " : "", nk > 1 ? KN[ks[1]] : "", boss ? ", and a boss" : "");
        pill(b, -1, cs.x * 0.5f, ny + tsz.y + 4 * u, 0.5f, 0.0f, bs, rgba(1, 0.85f, 0.7f, 0.95f), rgba(0.02f, 0.03f, 0.06f, 0.6f));
      }
    } else if (G.S.state == ST_PLAY && G.S.next_t < 0)
      pill("THE LAST WAVE", -1, cs.x * 0.5f, ny, 0.5f, 0.0f, bs, rgba(1, 0.7f, 0.5f, 1), rgba(0.02f, 0.03f, 0.06f, 0.6f));
  }
  /* boss health over the boss */
  for (int i = 0; i < MAXE; i++) {
    const creep *e = &G.S.e[i];
    float su, sv;
    if (!e->alive || !e->boss || !view_screen((t3_vec3){ e->x, 3.5f, e->y }, &su, &sv)) continue;
    float w = 90 * u, h = 9 * u, x = su * cs.x - w * 0.5f, y = sv * cs.y - h;
    cw_r2d_rect(cw_rect_make(x - 2 * u, y - 2 * u, w + 4 * u, h + 4 * u), rgba(0, 0, 0, 0.8f));
    cw_r2d_rect(cw_rect_make(x, y, w * CW_CLAMP(e->hp / e->maxhp, 0.0f, 1.0f), h), rgba(1, 0.25f, 0.2f, 1));
  }
  /* banners: the map's name at the start, each wave as it comes, the end */
  if (G.banner_t > 0 && cw_shell_screen_now() != CW_SHELL_PAUSE && G.S.state == ST_PLAY) {
    float a = cw_clampf((float)G.banner_t / 40.0f, 0, 1), y = cs.y * 0.3f;
    if (G.banner_kind == 0) {
      { /* a band behind the map's name: it reads over any ground */
        cw_text_metrics nm = measure(G.S.m.name[0] ? G.S.m.name : "TOWERS", 72 * u);
        cw_r2d_rect(cw_rect_make(0, y - 10 * u, cs.x, nm.height + 34 * u + 30 * u), rgba(0.02f, 0.03f, 0.06f, 0.55f * a));
      }
      y += text(G.S.m.name[0] ? G.S.m.name : "TOWERS", cs.x * 0.5f, y, 72 * u, rgba(1, 0.88f, 0.5f, a), CW_ALIGN_CENTER).height;
      snprintf(b, sizeof b, "%s   %d WAVES", THEME_NAME[CW_CLAMP(G.S.m.theme, 0, MP_THEMES - 1)], G.S.m.waves);
      y += text(b, cs.x * 0.5f, y, 34 * u, rgba(1, 0.95f, 0.85f, a), CW_ALIGN_CENTER).height + 10 * u;
      if (G.S.m.num <= 1)
        pill("build on your own pads: {move_left|move_right|move_up|move_down} hop, the four buttons build", -1, cs.x * 0.5f, y, 0.5f, 0.0f, bs, rgba(1, 1, 1, a),
             rgba(0.02f, 0.03f, 0.06f, 0.6f * a));
    } else {
      snprintf(b, sizeof b, G.S.wave == G.S.m.waves ? "THE LAST WAVE" : "WAVE %d", G.S.wave);
      y += text(b, cs.x * 0.5f, y, 64 * u, rgba(1, 0.75f, 0.45f, a), CW_ALIGN_CENTER).height + 8 * u;
      if (G.S.players > 1 && G.S.wave > 1) { /* the wave before it, player by player: kills and damage, each in their colour */
        float tw = 0, gapx = 40 * u;
        char pb[MAXP][48];
        for (int i = 0; i < G.S.players; i++) {
          snprintf(pb[i], sizeof pb[i], "P%d  %d kills  %.1fk damage", i + 1, G.wave_kills[i], (double)(G.wave_dmg[i] / 1000.0f));
          tw += measure(pb[i], 30 * u).width + (i ? gapx : 0);
        }
        float x = cs.x * 0.5f - tw * 0.5f, h = measure(pb[0], 30 * u).height;
        cw_r2d_rect(cw_rect_make(x - 16 * u, y - 6 * u, tw + 32 * u, h + 12 * u), rgba(0.02f, 0.03f, 0.06f, 0.6f * a));
        for (int i = 0; i < G.S.players; i++) {
          const float *pc = PCOL[i];
          x += text(pb[i], x, y, 30 * u, rgba(0.6f + 0.4f * pc[0], 0.6f + 0.4f * pc[1], 0.6f + 0.4f * pc[2], a), CW_ALIGN_LEFT).width + gapx;
        }
      }
    }
  }
  if (G.S.state == ST_WON) {
    float y = cs.y * 0.36f;
    y += text("THE CORE HOLDS", cs.x * 0.5f, y, 72 * u, rgba(0.6f, 1, 0.75f, 1), CW_ALIGN_CENTER).height;
    snprintf(b, sizeof b, "map %d cleared: on to the next", G.S.m.num);
    text(b, cs.x * 0.5f, y + 6 * u, 30 * u, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  } else if (G.S.state == ST_LOST) {
    float y = cs.y * 0.36f;
    y += text("THE CORE HAS FALLEN", cs.x * 0.5f, y, 72 * u, rgba(1, 0.35f, 0.3f, 1), CW_ALIGN_CENTER).height;
    text("the map begins again", cs.x * 0.5f, y + 6 * u, 30 * u, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
  cw_r2d_end();
}

/* ── frame ──────────────────────────────────────────────────────────── */

static void render(void *u, double alpha) {
  (void)u, (void)alpha;
  cw_shell_screen sc = cw_shell_screen_now();
  g_fx_level = CW_MAX(0, cw_gfx_effective(CW_GFX_EFFECTS) - 1);
  if (G.built) {
    /* the view's clock runs with the game's ticks, not with frames drawn: a frame drawn twice (a fast display, a
     * pause, the debugger's step) shows the same moment, so nothing animates faster than the game runs */
    static float t;
    int ticks = CW_MIN(g_ticks, 8);
    g_ticks = 0;
    t += (float)ticks / 60.0f;
    int w, h;
    cw_window_size(&w, &h);
    view_camera(&G.S, G.camera, (float)w / (float)h);
    if (G.lm_frames >= 0 && ++G.lm_frames > 1) {
      const char *bake = getenv("TOWERS_BAKE");
      view_lightmap(cw_scene_app_tree(), bake && *bake ? bake : NULL);
      G.lm_frames = -1;
    }
    double t0 = wall();
    bool show[MAXP];
    for (int i = 0; i < MAXP; i++) show[i] = G.in_match && G.phase == PH_PLAY && G.S.state == ST_PLAY;
#ifdef TOWERS_AUTOPILOT
    if (getenv("TOWERS_NO_CURSORS")) for (int i = 0; i < MAXP; i++) show[i] = false;
#endif
    if (G.in_match && G.phase == PH_PLAY) cursor_detail();
    { CW_PROFILE_SCOPE("towers.view_sync"); view_sync(&G.S, t, show); }
    G.t_view += (wall() - t0) * 1000;
    t0 = wall();
    { CW_PROFILE_SCOPE("towers.fx"); fx_events(&G.S); if (sc == CW_SHELL_PLAY || !G.in_match) for (int k = 0; k < ticks; k++) fx_tick(&G.S, t); }
    G.t_fx += (wall() - t0) * 1000;
    view_stats vs = view_get_stats();
    G.drawn = vs.creeps_drawn;
    for (int i = 0; i < MAXP; i++)
      if (G.S.p[i].active && G.S.p[i].slot >= 0) view_screen((t3_vec3){ mp_wx(&G.S.m, G.S.m.slot[G.S.p[i].slot].x), 1, mp_wz(&G.S.m, G.S.m.slot[G.S.p[i].slot].y) }, &G.pu[i], &G.pv[i]);
  }
  bool show_hud = true;
#ifdef TOWERS_AUTOPILOT
  show_hud = !getenv("TOWERS_NO_HUD");
#endif
  if (show_hud && G.in_match && (sc == CW_SHELL_PLAY || sc == CW_SHELL_PAUSE)) { CW_PROFILE_SCOPE("towers.hud"); hud(); }
  cw_quality_auto_measure(sc == CW_SHELL_PLAY);
  double now = wall();
  if (G.t_last > 0) G.t_frame += (now - G.t_last) * 1000;
  G.t_last = now;
  G.t_n++;
  if (getenv("TOWERS_BENCH") && sc == CW_SHELL_PLAY) {
    static int n;
    if (++n % 120 == 0) {
      cw_render3d_stats rs;
      cw_render3d_get_stats(cw_scene_app_tree(), &rs);
      cw_particles_stats ps;
      cw_particles_get_stats(cw_scene_app_tree(), &ps);
      view_stats vs = view_get_stats();
      fprintf(stderr, "bench frame_ms=%.2f sim_ms=%.2f view_ms=%.2f fx_ms=%.2f tick=%d map=%d wave=%d enemies=%d drawn=%d towers=%d zaps=%d crowd_ms=%.2f cpu3d_ms=%.2f draws=%d tris=%d psys=%d palive=%d\n",
              G.t_frame / G.t_n, G.t_sim / G.t_n, G.t_view / G.t_n, G.t_fx / G.t_n, G.S.tick, G.S.m.num, G.S.wave, G.S.nenemies, vs.creeps_drawn, vs.towers, vs.zaps,
              G.S.crowd_ms, rs.cpu_ms, rs.draw_calls, rs.triangles, ps.systems, ps.alive);
      const cw_pass_stat *pst;
      int np = cw_pipeline_pass_stats(cw_pipeline_of(cw_scene_app_tree()), &pst);
      fprintf(stderr, "bench_passes gpu_total=%.2f", cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree())));
      for (int k = 0; k < np; k++)
        if (pst[k].ran && (pst[k].gpu_ms_avg > 0.02f || pst[k].cpu_ms_avg > 0.3f)) fprintf(stderr, " %s=%.2f/%.2f", pst[k].name, pst[k].gpu_ms_avg, pst[k].cpu_ms_avg);
      fprintf(stderr, "\n");
      fflush(stderr);
      G.t_sim = G.t_view = G.t_fx = G.t_frame = 0;
      G.t_n = 0;
    }
  }
  { CW_PROFILE_SCOPE("towers.shell_draw"); cw_shell_draw(); }
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Towers", .width = 1280, .height = 720, .debug = true, .init = init, .warmup = warmup, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/main.cwscene", &app);
}
