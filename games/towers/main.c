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
 * paths (the bench's); TOWERS_WAVE=n starts at wave n with towers on every slot (screenshots). A cart host reads the
 * environment only from frame 2 (the engine's env request), so the auto cart answers `map <n>` (TOWERS_WAVE and
 * TOWERS_CROWD sent with it apply) and `cam <k> [x] [z]` (TOWERS_CAM) for the launcher art (tools/make-art.sh). */
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
#include "../common/coplanar.h"

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
  int32_t gold[MAXP], slot[MAXP], players_dbg;
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

/* a development switch read as the game runs: natively from its environment; on a cart from the engine's env request,
 * which a cart host delivers at frame 2 (a name read before that fails the request, so a cart never reads it in
 * frame 1; the switches a cart cannot have by then come as the `map` and `cam` requests instead) */
static const char *dev_env(const char *name) {
#if CW_CART
  return cw_test_env(name);
#else
  return getenv(name);
#endif
}

static int g_wave_cmd; /* the `players` request's starting wave (TOWERS_WAVE read in a frame, for hosts whose environment arrives late) */
static bool map_go(int n) {
  int count = map_count();
  if (count > 0 && n > count) n = count;
  if (!map_load(n, &G.M, sim_kind_by_name)) return false;
  sim_map_start(&G.S, &G.M);
  G.bench_crowd = dev_env("TOWERS_CROWD") ? atoi(dev_env("TOWERS_CROWD")) : TOWERS_BENCH_CROWD;
  int wave0 = g_wave_cmd > 0 ? g_wave_cmd : dev_env("TOWERS_WAVE") ? atoi(dev_env("TOWERS_WAVE")) : 0;
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

static int g_hint_t[MAXP]; /* the diamond's hint (the HUD, below): frames left, -1 once shown this match */
static void begin_play(void) {
  sim_init(&G.S, G.players, 4242);
  for (int i = 0; i < MAXP; i++) g_hint_t[i] = 0; /* the diamond's hint once a match, per seat */
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

static void cmd_map(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: map <n>"); return; }
  int n = atoi(argv[1]);
  if (!map_go(n)) { cw_reply_error(r, "no such map"); return; }
  cw_reply_printf(r, "{\"map\":%d}", n);
}
#ifdef TOWERS_AUTOPILOT
static void cmd_cam(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: cam <k> [x] [z]"); return; }
  float k = (float)atof(argv[1]);
  if (!(k > 0)) { cw_reply_error(r, "k must be positive (1 = the whole map)"); return; }
  g_cam_k = k, g_cam_x = argc > 2 ? (float)atof(argv[2]) : 0, g_cam_z = argc > 3 ? (float)atof(argv[3]) : 0;
  cw_reply_printf(r, "{\"k\":%g,\"x\":%g,\"z\":%g}", (double)g_cam_k, (double)g_cam_x, (double)g_cam_z);
}
/* players <n> [wave]: the self-playing match again with n bots, from that wave with towers on every pad (what
 * TOWERS_PLAYERS and TOWERS_WAVE do in the environment, for a host whose environment arrives after the first frame:
 * romdev's env request lands at frame 2, after init read them) */
static void cmd_players(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: players <n> [wave]"); return; }
  int n = CW_CLAMP(atoi(argv[1]), 1, MAXP);
  g_wave_cmd = argc >= 3 ? atoi(argv[2]) : 0;
  G.players = n;
  if (!G.in_match) { cw_shell_start(n); cw_reply_printf(r, "{\"players\":%d,\"wave\":%d}", n, g_wave_cmd); return; }
  sim_init(&G.S, n, 4242);
  G.phase = PH_PLAY;
  G.over_ticks = 0;
  if (!map_go(first_map())) map_go(1);
  cw_reply_printf(r, "{\"players\":%d,\"wave\":%d,\"map\":%d}", n, g_wave_cmd, G.S.m.num);
}
#endif

static void cmd_hud(int argc, const char **argv, cw_reply *r); /* the HUD, below */
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
  cop_init(); /* the coplanar debug command: faces that fight for the depth test (games/common/coplanar.h) */
  cop_set_plane_tol(0.0005f); /* (the camera's depth range is the map's own few metres: it tells faces apart microns apart, and
                               * the models lay their glows a millimetre over their bodies on purpose) */
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
  cw_debug_i32("game.players", &G.players_dbg);
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
  cw_debug_command("map", "map <n>: go to that map", cmd_map);
#ifdef TOWERS_AUTOPILOT
  cw_debug_command("cam", "cam <k> [x] [z]: the camera k times closer over the point x,z of the map (key art)", cmd_cam);
  cw_debug_command("players", "players <n> [wave]: the match again with n bots, from that wave", cmd_players);
#endif
  cw_debug_command("hud", "hud [labels 0|1 | style 0|1 | push <px> | sweep [singles|pairs] | slots | cursor <seat> <slot>]: the HUD's rectangles (the overlap check)", cmd_hud);
#ifdef TOWERS_AUTOPILOT
  if (getenv("TOWERS_BOT_BRANCH")) g_bot_branch = CW_CLAMP(atoi(getenv("TOWERS_BOT_BRANCH")), 0, 2);
  if (getenv("TOWERS_CAM")) sscanf(getenv("TOWERS_CAM"), "%f,%f,%f", &g_cam_k, &g_cam_x, &g_cam_z); /* key art: TOWERS_CAM=k,x,z (natively; a cart host answers `cam`) */
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
    { /* tests (the flash check's storm scene): TOWERS_STORMS=t1,t2,..: seat i calls its storm at map tick ti (and the
       * bots call none of their own) */
      static int at[MAXP] = { -2 };
      if (at[0] == -2) {
        for (int k = 0; k < MAXP; k++) at[k] = -1;
        const char *e = getenv("TOWERS_STORMS");
        for (int k = 0; e && *e && k < MAXP; k++) { at[k] = atoi(e); e = strchr(e, ','); if (e) e++; }
      }
      if (at[0] >= 0) in[i].power = G.S.map_ticks == at[i];
    }
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
  G.leaks = G.S.leaks, G.next_t = G.S.next_t, G.hash = G.S.hash, G.players_dbg = G.S.players;
  int tw = 0;
  for (int k = 0; k < G.S.m.nslot; k++) tw += G.S.tw[k].alive;
  G.towers = tw;
  for (int i = 0; i < MAXP; i++) G.gold[i] = G.S.p[i].gold, G.slot[i] = G.S.p[i].slot;
  if (G.banner_t > 0) G.banner_t--;
}

/* ── the HUD ────────────────────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static cw_text_style tstyle(float size, cw_color c) { return (cw_text_style){ .size = size, .color = c, .outline_width = size * 0.09f, .outline_color = rgba(0, 0, 0, 0.9f) }; }
/* measurements cached by string, seat (its device: the glyphs differ), size and the player's text scale: the HUD asks
 * for the same few strings every frame, and the overlap sweep (below) for thousands of layouts of them in one frame */
typedef struct msz { char s[128]; int player, device; float size, scale; bool prompt; cw_vec2 sz; cw_text_metrics tm; } msz;
#define MSZ_N 512
static msz g_msz[MSZ_N];
static msz *msz_find(const char *s, int player, float size, bool prompt, bool *found) {
  int device = prompt && player >= 0 ? (int)cw_prompt_device_of(player) : -1;
  float scale = cw_text_scale();
  uint32_t h = 2166136261u;
  for (const char *q = s; *q; q++) h = (h ^ (uint8_t)*q) * 16777619u;
  h = (h ^ (uint32_t)(player + 7)) * 16777619u, h = (h ^ (uint32_t)(device + 3)) * 16777619u;
  uint32_t fb;
  memcpy(&fb, &size, 4), h = (h ^ fb) * 16777619u;
  memcpy(&fb, &scale, 4), h = (h ^ fb) * 16777619u;
  *found = false;
  for (int k = 0; k < MSZ_N; k++) {
    msz *e = &g_msz[(h + (uint32_t)k) % MSZ_N];
    if (!e->s[0]) { snprintf(e->s, sizeof e->s, "%s", s), e->player = player, e->device = device, e->size = size, e->scale = scale, e->prompt = prompt; return e; }
    if (e->prompt == prompt && e->player == player && e->device == device && e->size == size && e->scale == scale && !strcmp(e->s, s)) { *found = true; return e; }
  }
  return NULL; /* full (never, with the HUD's few strings): measured uncached */
}
static cw_text_metrics measure(const char *s, float size) {
  bool found;
  msz *e = strlen(s) < 127 ? msz_find(s, -1, size, false, &found) : NULL;
  if (e && found) return e->tm;
  cw_text_style st = tstyle(size, rgba(1, 1, 1, 1));
  cw_text_metrics tm = cw_font_measure(font, s, &st);
  if (e) e->tm = tm;
  return tm;
}
static cw_vec2 prompt_size(const char *markup, int player, float size) {
  bool found;
  msz *e = strlen(markup) < 127 ? msz_find(markup, player, size, true, &found) : NULL;
  if (e && found) return e->sz;
  cw_vec2 sz = cw_prompt_text_size(font, markup, player, size);
  if (e) e->sz = sz;
  return sz;
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
  cw_vec2 sz = prompt_size(markup, player, size);
  float pad = size * 0.28f, w = sz.x + pad * 2, h = sz.y + pad * 1.2f;
  float l = x - w * ax, t = y - h * ay;
  cw_r2d_rect(cw_rect_make(l, t, w, h), bg);
  cw_prompt_text(font, markup, player, cw_v2(l + pad, t + pad * 0.6f), size, c, CW_ALIGN_LEFT);
  return (cw_vec2){ w, h };
}

/* ── the HUD's rectangles ───────────────────────────────────────────────
 * Everything the HUD draws that text could hide or be hidden by, in canvas pixels, kept for the frame: the players'
 * panels, the top line with its wave pills, each cursor's buttons (the pills round it) and its P1..P4 label. Later
 * things keep out of the extents of earlier ones (a cursor's pills and label move out of a panel's or an earlier
 * cursor's way, by exactly as much as they need), and the `hud` request lists the rectangles for the overlap check
 * (tests/towers-hud-overlap.mjs: no label over any button, on any map, at any size, with any number of seats). */
enum { HR_LABEL, HR_PANEL, HR_TOP, HR_BUTTON };
typedef struct hud_rect { uint8_t kind, player; cw_rect box, text; char s[48]; } hud_rect;
#define MAXHR 96
static hud_rect g_hr[MAXHR];
static int g_nhr;
typedef struct extent { float l, t, r, b; } extent;
static extent g_ext[MAXHR];
static int g_next, g_nfixed;      /* the extents; the first g_nfixed are the panels and the top line */
static cw_vec2 g_hud_cs;          /* the canvas the last HUD was drawn on */
static int g_hud_style;           /* the deploy pills: 0 each with its button's glyph; 1 the text alone, one diamond of the four face buttons at
                                   * the cursor's centre (the pill's place says which), and a hint the first time a seat's cursor opens (`hud style`) */
#define HINT_FRAMES 120
static bool g_hud_labels = true;  /* tests: the cursor labels hidden (`hud labels 0`) */
static float g_hud_push;          /* tests: the labels pushed this far into their nearest button (`hud push 20`: the check's control) */
static void hr_add(int kind, int player, cw_rect box, cw_rect text, const char *s) {
  if (g_nhr >= MAXHR) return;
  hud_rect *r = &g_hr[g_nhr++];
  r->kind = (uint8_t)kind, r->player = (uint8_t)player, r->box = box, r->text = text;
  strncpy(r->s, s, sizeof r->s - 1), r->s[sizeof r->s - 1] = 0;
}
static void ext_add(float l, float t, float r, float b) {
  if (g_next < MAXHR) g_ext[g_next++] = (extent){ l, t, r, b };
}
static void ext_add_rect(cw_rect q) { ext_add(q.x, q.y, q.x + q.w, q.y + q.h); }
static bool ext_hit(const extent *e, float l, float t, float r, float b, float m) {
  return l < e->r + m && r > e->l - m && t < e->b + m && b > e->t - m;
}
/* a pill's box and the box of its text, as pill() lays them out */
static void pill_box(const char *markup, int player, float x, float y, float ax, float ay, float size, cw_rect *box, cw_rect *tb) {
  cw_vec2 sz = prompt_size(markup, player, size);
  float pad = size * 0.28f, w = sz.x + pad * 2, h = sz.y + pad * 1.2f;
  float l = x - w * ax, t = y - h * ay;
  *box = cw_rect_make(l, t, w, h);
  *tb = cw_rect_make(l + pad, t + pad * 0.6f, sz.x, sz.y);
}

/* the build diamond (an empty slot) or the tower's actions round a player's cursor, while the player is choosing (for
 * three seconds after the cursor hops or the tower under it changes, and while selling); then the labels fade and one
 * short line stays over the cursor. The cursor's P1..P4 label sits over the top pill (under the bottom one when the
 * screen's top is too near), never on one. Where a cursor's pills and label would land on a panel, the top line or an
 * earlier cursor's, the whole lot moves up or down, whichever is the shorter move, just far enough to clear it. One
 * player alone keeps the full labels. */
float g_cursor_k[MAXP]; /* how much of each cursor's detail shows (the labels and the reach rings): 0 .. 1 */
#define PILL_MIN 0.1f   /* with less detail than this the pills are gone, and the label settles in over the cursor */
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
typedef struct cur_item { char s[128]; float x, y, ax, ay; cw_color c; cw_rect box, tb; } cur_item;
typedef struct cur_lay {
  int n, sell;          /* the pills (sell: which one is the sell pill, -1 none) */
  cur_item it[6];
  bool pills;           /* the pills show this frame */
  bool diamond, hint;   /* style 1 on an empty slot: the face-button diamond at the centre (its box), the hint over the top pill */
  cw_rect db, hb, htb;
  char lab[128];        /* the label */
  cw_rect lb;           /* its box */
  float l, t, r, b;     /* the extents of what is drawn */
} cur_lay;
/* the pills and the label of player i's cursor at (cx, cy) on screen, with detail k, over a tower t (or an empty slot) */
#define HINT_TEXT "face buttons choose"
static const float DIAMOND_K = 1.1f; /* the diamond's height in text sizes */
static void cursor_layout(int i, const tower *t, int gold, float cx, float cy, float k, float u, cw_vec2 cs, bool hint, cur_lay *L) {
  float bs = 26 * u, gap = view_px_per_m(cs.y) * 1.25f;
  cw_color grey = rgba(0.6f, 0.6f, 0.62f, 0.75f * k);
  char b[128];
  L->n = 0, L->sell = -1, L->pills = k > PILL_MIN;
  L->diamond = L->hint = false;
#define ITEM(X, Y, AX, AY, C) do { cur_item *q = &L->it[L->n++]; snprintf(q->s, sizeof q->s, "%s", b); q->x = (X), q->y = (Y), q->ax = (AX), q->ay = (AY), q->c = (C); \
    pill_box(q->s, i, q->x, q->y, q->ax, q->ay, bs, &q->box, &q->tb); } while (0)
  if (!t->alive) {
    /* north, west, east, south: the four towers where their buttons sit */
    static const float AX[A_KINDS] = { 0.5f, 1.0f, 0.0f, 0.5f }, AY[A_KINDS] = { 1.0f, 0.5f, 0.5f, 0.0f };
    static const float DX[A_KINDS] = { 0, -1, 1, 0 }, DY[A_KINDS] = { -1, 0, 0, 1 };
    for (int a = 0; a < A_KINDS; a++) {
      int cost = sim_tower_cost(a, 1);
      if (g_hud_style) snprintf(b, sizeof b, "%s %d", sim_tower_name(a), cost);
      else snprintf(b, sizeof b, "%s %s %d", ACT_TAG[a], sim_tower_name(a), cost);
      const float *tc = TCOL[a];
      cw_color c = gold >= cost ? rgba(0.55f + 0.45f * tc[0], 0.55f + 0.45f * tc[1], 0.55f + 0.45f * tc[2], k) : grey;
      ITEM(cx + DX[a] * gap, cy + DY[a] * gap * 0.8f, AX[a], AY[a], c);
    }
    if (g_hud_style && L->pills) { /* the diamond at the centre: the four face buttons where their pills are */
      cw_vec2 gs = cw_prompt_action_size(G.act[A_S], i, bs * DIAMOND_K);
      bool pad = cw_prompt_device_of(i) == CW_PROMPT_PAD; /* a pad: one diamond; a keyboard or touch: its four glyphs round the centre */
      float r = pad ? 0 : gs.y * 0.9f, w = gs.x + 2 * r, h = gs.y + 2 * r;
      L->diamond = true, L->db = cw_rect_make(cx - w * 0.5f, cy - h * 0.5f, w, h);
      if (hint) {
        float pt = 1e9f;
        for (int n = 0; n < L->n; n++) pt = fminf(pt, L->it[n].box.y);
        pill_box(HINT_TEXT, -1, cx, pt - 2 * u, 0.5f, 1.0f, bs, &L->hb, &L->htb);
        L->hint = true;
      }
    }
  } else {
    if (t->level == 1) {
      int cost = sim_tower_cost(t->kind, 2);
      snprintf(b, sizeof b, "{act_s} upgrade %d", cost);
      ITEM(cx, cy + gap * 0.8f, 0.5f, 0.0f, gold >= cost ? rgba(0.7f, 1, 0.6f, k) : grey);
    } else if (t->level == 2) { /* the third level: two branches, south and east, each with what it does */
      int cost = sim_tower_cost(t->kind, 3);
      bool ok = gold >= cost;
      snprintf(b, sizeof b, "{act_s} %s %d: %s", sim_branch_name(t->kind, BR_A), cost, sim_branch_does(t->kind, BR_A));
      ITEM(cx, cy + gap * 0.8f, 0.5f, 0.0f, ok ? rgba(0.7f, 1, 0.6f, k) : grey);
      snprintf(b, sizeof b, "{act_e} %s %d: %s", sim_branch_name(t->kind, BR_B), cost, sim_branch_does(t->kind, BR_B));
      ITEM(cx + gap, cy, 0.0f, 0.5f, ok ? rgba(1, 0.85f, 0.55f, k) : grey);
    }
    if (t->kind != TK_FROST) {
      snprintf(b, sizeof b, "{act_n} aim: %s", sim_aim_name(t->aim));
      ITEM(cx, cy - gap * 0.8f, 0.5f, 1.0f, rgba(0.85f, 0.9f, 1, k));
    }
    snprintf(b, sizeof b, "{act_w} hold: sell %d", sim_sell_value(t));
    L->sell = L->n;
    ITEM(cx - gap, cy, 1.0f, 0.5f, rgba(1, 0.8f, 0.6f, k));
  }
#undef ITEM
  /* the label: P1..P4 and the tower under the cursor (its level and branch) */
  if (t->alive)
    snprintf(L->lab, sizeof L->lab, "P%d %s %s%s%s", i + 1, sim_tower_name(t->kind), ROMAN[t->level], t->level >= 3 ? " " : "", t->level >= 3 ? sim_branch_name(t->kind, t->branch) : "");
  else
    snprintf(L->lab, sizeof L->lab, "P%d", i + 1);
  cw_text_metrics lm = measure(L->lab, bs);
  float pt = 1e9f, pb = -1e9f;
  for (int n = 0; n < L->n; n++) pt = fminf(pt, L->it[n].box.y), pb = fmaxf(pb, L->it[n].box.y + L->it[n].box.h);
  float ppt = pt; /* the pills' top */
  if (L->hint) pt = L->hb.y; /* the label goes over the hint */
  bool above = pt - 2 * u - lm.height >= 4 * u;                   /* over the top pill, or under the bottom one when the screen's top is too near */
  float with = above ? pt - 2 * u - lm.height : pb + 2 * u;        /* with the pills */
  float without = cy - gap * 0.8f - lm.height - 2 * u;             /* without them: close over the cursor */
  float s = L->pills ? 0 : 1 - cw_clampf(k / PILL_MIN, 0, 1);      /* it settles as the last of the detail goes */
  float top = with + (without - with) * s;
  if (g_hud_push != 0 && L->pills) top = above ? ppt - lm.height + g_hud_push : pb - g_hud_push; /* the control: into the top pill */
  L->lb = cw_rect_make(cx - lm.width * 0.5f, top, lm.width, lm.height);
  L->l = L->lb.x, L->t = L->lb.y, L->r = L->lb.x + L->lb.w, L->b = L->lb.y + L->lb.h;
  if (L->pills)
    for (int n = 0; n < L->n; n++) {
      cw_rect q = L->it[n].box;
      L->l = fminf(L->l, q.x), L->t = fminf(L->t, q.y), L->r = fmaxf(L->r, q.x + q.w), L->b = fmaxf(L->b, q.y + q.h);
    }
  if (L->diamond) L->l = fminf(L->l, L->db.x), L->t = fminf(L->t, L->db.y), L->r = fmaxf(L->r, L->db.x + L->db.w), L->b = fmaxf(L->b, L->db.y + L->db.h);
  if (L->hint) L->l = fminf(L->l, L->hb.x), L->t = fminf(L->t, L->hb.y), L->r = fmaxf(L->r, L->hb.x + L->hb.w), L->b = fmaxf(L->b, L->hb.y + L->hb.h);
}
/* lay the cursor out at (cx, cy), slid in from a side; where it lands on an extent already placed (the panels, the
 * top line, earlier cursors), it moves to the nearest place just over or just under one of them that is clear of
 * them all and on screen; returns the cy it ended on (and cx, slid in) */
static bool lay_hits(const cur_lay *L, float m) {
  for (int j = 0; j < g_next; j++)
    if (ext_hit(&g_ext[j], L->l, L->t, L->r, L->b, m)) return true;
  return false;
}
static float cursor_place(int i, const tower *t, int gold, float *cx, float cy, float k, float u, cw_vec2 cs, bool hint, cur_lay *L) {
  float m = 4 * u;
  cursor_layout(i, t, gold, *cx, cy, k, u, cs, hint, L);
  /* a slot near a side: the lot slides in off the edge (a pill cut by the screen cannot be read) */
  if (L->l < 2 * u) *cx += 2 * u - L->l;
  else if (L->r > cs.x - 2 * u) *cx -= L->r - (cs.x - 2 * u);
  cursor_layout(i, t, gold, *cx, cy, k, u, cs, hint, L);
  if (!lay_hits(L, m * 0.5f)) return cy;
  float best = cy, best_d = 1e9f;
  for (int j = 0; j < g_next; j++)
    for (int side = 0; side < 2; side++) {
      /* just under extent j (side 1) or just over it (side 0), by this layout's reach over and under its cy */
      float c = side ? g_ext[j].b + m + (cy - L->t) : g_ext[j].t - m - (L->b - cy);
      if (fabsf(c - cy) >= best_d) continue;
      static cur_lay T;
      cursor_layout(i, t, gold, *cx, c, k, u, cs, hint, &T);
      c = side ? g_ext[j].b + m + (c - T.t) : g_ext[j].t - m - (T.b - c); /* (the label may have changed sides) */
      cursor_layout(i, t, gold, *cx, c, k, u, cs, hint, &T);
      if (T.t < 2 * u || T.b > cs.y - 2 * u || lay_hits(&T, m * 0.5f)) continue; /* (tested at half the margin it was placed with: no knife edge) */
      best = c, best_d = fabsf(c - cy);
    }
  cursor_layout(i, t, gold, *cx, best, k, u, cs, hint, L);
  return best;
}
static void hud_cursor(int i, float u, cw_vec2 cs) {
  const player *p = &G.S.p[i];
  if (!p->active || p->slot < 0) return;
  float x = mp_wx(&G.S.m, G.S.m.slot[p->slot].x), z = mp_wz(&G.S.m, G.S.m.slot[p->slot].y), su, sv;
  if (!view_screen((t3_vec3){ x, MP_GROUND_H, z }, &su, &sv)) return;
  float cx = su * cs.x, cy = sv * cs.y, bs = 26 * u, k = g_cursor_k[i];
  const tower *t = &G.S.tw[p->slot];
  static cur_lay L;
  if (g_hud_style && !t->alive && k > PILL_MIN && g_hint_t[i] == 0) g_hint_t[i] = HINT_FRAMES; /* the first time this seat's list opens */
  bool hint = g_hud_style && !t->alive && k > PILL_MIN && g_hint_t[i] > 0;
  if (hint) g_hint_t[i] = g_hint_t[i] > 1 ? g_hint_t[i] - 1 : -1;
  cy = cursor_place(i, t, p->gold, &cx, cy, k, u, cs, hint, &L);
  ext_add(L.l, L.t, L.r, L.b);
  const float *pc = PCOL[i];
  cw_color bg = rgba(0.02f, 0.03f, 0.06f, 0.72f * k);
  cw_color pcol = rgba(0.6f + 0.4f * pc[0], 0.6f + 0.4f * pc[1], 0.6f + 0.4f * pc[2], 1);
  if (L.pills) {
    for (int n = 0; n < L.n; n++) {
      const cur_item *q = &L.it[n];
      cw_r2d_rect(q->box, bg);
      cw_prompt_text(font, q->s, i, cw_v2(q->tb.x, q->tb.y), bs, q->c, CW_ALIGN_LEFT);
      hr_add(HR_BUTTON, i, q->box, q->tb, q->s);
    }
    if (L.diamond) { /* the four face buttons as the shell's prompts draw them: on a pad, one diamond with every dot solid */
      cw_vec2 gs = cw_prompt_action_size(G.act[A_S], i, bs * DIAMOND_K);
      bool pad = cw_prompt_device_of(i) == CW_PROMPT_PAD;
      float r = pad ? 0 : gs.y * 0.9f;
      static const float DX[A_KINDS] = { 0, -1, 1, 0 }, DY[A_KINDS] = { -1, 0, 0, 1 };
      for (int a = 0; a < A_KINDS; a++)
        cw_prompt_action(G.act[a], i, cw_v2(cx - gs.x * 0.5f + DX[a] * r, cy - gs.y * 0.5f + DY[a] * r), bs * DIAMOND_K, rgba(1, 1, 1, k));
    }
    if (L.hint) {
      cw_r2d_rect(L.hb, bg);
      cw_prompt_text(font, HINT_TEXT, -1, cw_v2(L.htb.x, L.htb.y), bs, rgba(1, 0.95f, 0.75f, k), CW_ALIGN_LEFT);
      hr_add(HR_LABEL, i, L.hb, L.htb, HINT_TEXT);
    }
    if (p->sell_t > 0 && L.sell >= 0) { /* the hold's progress under the sell pill */
      const cw_rect *sb = &L.it[L.sell].box;
      float q = (float)p->sell_t / 36.0f;
      cw_r2d_rect(cw_rect_make(sb->x, sb->y + sb->h, sb->w * q, 5 * u), rgba(1, 0.6f, 0.3f, 1));
    }
  }
  if (g_hud_labels) text(L.lab, cx, L.lb.y, bs, pcol, CW_ALIGN_CENTER);
  hr_add(HR_LABEL, i, L.lb, L.lb, L.lab);
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
  cw_rect panel = cw_rect_make(x, y, w, h);
  cw_r2d_rect(panel, rgba(0.02f, 0.03f, 0.06f, 0.72f));
  cw_r2d_rect(cw_rect_make(x, y, 6 * u, h), rgba(pc[0], pc[1], pc[2], 1));
  float ny = y + (h - nm.height) * 0.5f;
  text(b, x + 16 * u, ny, bs, rgba(0.6f + 0.4f * pc[0], 0.6f + 0.4f * pc[1], 0.6f + 0.4f * pc[2], 1), CW_ALIGN_LEFT);
  hr_add(HR_PANEL, i, panel, cw_rect_make(x + 16 * u, ny, nm.width, nm.height), b);
  ext_add_rect(panel);
  float cx = x + 16 * u + nm.width + 14 * u + 10 * u, cy = y + h * 0.5f;
  cw_r2d_circle(cw_v2(cx, cy), 10 * u, 20, rgba(1, 0.8f, 0.25f, 1)); /* a coin */
  cw_r2d_circle(cw_v2(cx, cy), 6 * u, 20, rgba(1, 0.92f, 0.55f, 1));
  text(g, cx + 16 * u, y + (h - gm.height) * 0.5f, 36 * u, rgba(1, 0.92f, 0.6f, 1), CW_ALIGN_LEFT);
  { /* the storm: ready (its button) or charging (a bar filling under the panel) */
    float by = bottom ? y - 6 * u : y + h + 6 * u;
    if (p->power_cd <= 0) {
      float glow = 0.85f + 0.15f * cw_sin((float)G.S.tick * 0.05f); /* (a slow glow, never a blink) */
      cw_rect box, tb;
      pill_box("{power} storm", i, right ? x + w : x, by, right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f, bs, &box, &tb);
      pill("{power} storm", i, right ? x + w : x, by, right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f, bs, rgba(0.75f * glow, 0.85f * glow, 1, 1), rgba(0.02f, 0.03f, 0.06f, 0.72f));
      hr_add(HR_PANEL, i, box, tb, "storm");
      ext_add_rect(box);
    } else {
      float k = 1.0f - (float)p->power_cd / (float)POWER_CD, bh = 8 * u, bx = x, bt = bottom ? by - bh : by;
      cw_r2d_rect(cw_rect_make(bx, bt, w, bh), rgba(0.02f, 0.03f, 0.06f, 0.72f));
      cw_r2d_rect(cw_rect_make(bx, bt, w * k, bh), rgba(0.45f, 0.6f, 1, 0.9f));
      ext_add(bx, bt, bx + w, bt + bh);
    }
  }
}

/* the top line: the wave, the core, the next wave (drawn before the cursors, which keep out of it) */
static void hud_top(float u, cw_vec2 cs) {
  float bs = 26 * u;
  char b[128], c[32];
  snprintf(b, sizeof b, "WAVE %d / %d", G.S.wave, G.S.m.waves);
  cw_text_metrics wm = measure(b, 34 * u);
  snprintf(c, sizeof c, "CORE %d", G.S.core_hp);
  cw_text_metrics cm = measure(c, 34 * u);
  float w = wm.width + cm.width + 60 * u, h = wm.height + 12 * u, x = cs.x * 0.5f - w * 0.5f, y = 10 * u;
  cw_rect panel = cw_rect_make(x, y, w, h);
  cw_r2d_rect(panel, rgba(0.02f, 0.03f, 0.06f, 0.72f));
  text(b, x + 16 * u, y + 6 * u, 34 * u, rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_LEFT);
  float hurt = (float)G.S.core_hp / (float)CORE_HP;
  /* low: the count glows amber and back, slowly (never a blink, never a red flash) */
  float lo = hurt < 0.35f ? 0.5f + 0.5f * cw_sin((float)G.S.tick * 0.07f) : 0.0f;
  cw_color cc = rgba(0.55f + 0.45f * (1 - hurt), 0.85f, 1, 1), warn = rgba(1, 0.6f, 0.32f, 1);
  if (g_strobe_control && hurt < 0.35f) lo = (G.S.tick / 15) % 2 ? 1.0f : 0.0f, warn = rgba(1, 0.3f, 0.25f, 1);
  text(c, x + w - 16 * u, y + 6 * u, 34 * u, rgba(cc.r + (warn.r - cc.r) * lo, cc.g + (warn.g - cc.g) * lo, cc.b + (warn.b - cc.b) * lo, 1), CW_ALIGN_RIGHT);
  hr_add(HR_TOP, 255, panel, cw_rect_make(x + 16 * u, y + 6 * u, w - 32 * u, wm.height), "WAVE / CORE");
  ext_add_rect(panel);
  float ny = y + h + 6 * u;
  cw_color pbg = rgba(0.02f, 0.03f, 0.06f, 0.6f);
  cw_rect box, tb;
  if (G.S.state == ST_PLAY && G.S.next_t > 0) {
    int secs = (G.S.next_t + 59) / 60;
    if (G.S.wave == 0) snprintf(b, sizeof b, "the first wave in %d   {call}: send it now", secs);
    else snprintf(b, sizeof b, "the next wave in %d   {call}: send it now, +%d gold each", secs, G.S.next_t / 60);
    pill_box(b, -1, cs.x * 0.5f, ny, 0.5f, 0.0f, bs, &box, &tb);
    cw_vec2 tsz = pill(b, -1, cs.x * 0.5f, ny, 0.5f, 0.0f, bs, rgba(1, 1, 1, 0.95f), pbg);
    hr_add(HR_TOP, 255, box, tb, "next wave"), ext_add_rect(box);
    int ks[2], nk = sim_wave_kinds(&G.S, G.S.wave + 1, ks);
    if (nk) { /* what comes next, so the towers can be chosen for it */
      static const char *const KN[CK_KINDS] = { "skitters", "ghouls", "hounds", "imps", "sporelings", "bloaters", "shades", "ogres" };
      bool boss = (G.S.wave + 1) % 10 == 0 || G.S.wave + 1 == G.S.m.waves;
      snprintf(b, sizeof b, "next: %s%s%s%s", KN[ks[0]], nk > 1 ? " and " : "", nk > 1 ? KN[ks[1]] : "", boss ? ", and a boss" : "");
      pill_box(b, -1, cs.x * 0.5f, ny + tsz.y + 4 * u, 0.5f, 0.0f, bs, &box, &tb);
      cw_vec2 nsz = pill(b, -1, cs.x * 0.5f, ny + tsz.y + 4 * u, 0.5f, 0.0f, bs, rgba(1, 0.85f, 0.7f, 0.95f), pbg);
      hr_add(HR_TOP, 255, box, tb, "next kinds"), ext_add_rect(box);
      int nw = sim_wave_new_kind(&G.S, G.S.wave + 1);
      if (nw >= 0 && sim_kind_hint(nw)[0]) { /* a kind not met on this map yet: what it does, and what answers it */
        snprintf(b, sizeof b, "new: %s, %s", KN[nw], sim_kind_hint(nw));
        pill_box(b, -1, cs.x * 0.5f, ny + tsz.y + nsz.y + 8 * u, 0.5f, 0.0f, bs, &box, &tb);
        pill(b, -1, cs.x * 0.5f, ny + tsz.y + nsz.y + 8 * u, 0.5f, 0.0f, bs, rgba(1, 0.95f, 0.6f, 1), pbg);
        hr_add(HR_TOP, 255, box, tb, "new kind"), ext_add_rect(box);
      }
    }
  } else if (G.S.state == ST_PLAY && G.S.next_t < 0) {
    pill_box("THE LAST WAVE", -1, cs.x * 0.5f, ny, 0.5f, 0.0f, bs, &box, &tb);
    pill("THE LAST WAVE", -1, cs.x * 0.5f, ny, 0.5f, 0.0f, bs, rgba(1, 0.7f, 0.5f, 1), pbg);
    hr_add(HR_TOP, 255, box, tb, "THE LAST WAVE"), ext_add_rect(box);
  }
}

/* the `hud` request: the frame's rectangles; `hud labels 0|1`, `hud push <px>` (tests); `hud sweep`: every slot of
 * the map, alone in every state and in pairs with every other player's slot, laid out as the HUD would lay them out
 * (the panels and the top line as they stand), counting what would overlap: the check's whole-map case */
static void reply_rect(cw_reply *r, cw_rect q) { cw_reply_printf(r, "[%.1f,%.1f,%.1f,%.1f]", (double)q.x, (double)q.y, (double)q.w, (double)q.h); }
static bool rect_hit(cw_rect a, cw_rect b) { return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y; }
static void hud_sweep(cw_reply *r, bool singles, bool pairs, int only) {
  cw_vec2 cs = g_hud_cs;
  if (cs.x <= 0 || !G.in_match || G.phase != PH_PLAY) { cw_reply_error(r, "hud sweep: the match's HUD has not been drawn yet"); return; }
  float u = cs.y / 1080.0f;
  int n = G.S.players, bad = 0, cases = 0;
  /* the states a cursor can be over: an empty slot, a bolt at each level (the aim pill, upgrade, the two branches),
   * a frost (no aim pill) */
  const tower ST[5] = { { .alive = false }, { .alive = true, .kind = TK_BOLT, .level = 1, .tgt = -1 }, { .alive = true, .kind = TK_BOLT, .level = 2, .tgt = -1 },
                        { .alive = true, .kind = TK_BOLT, .level = 3, .branch = BR_A, .tgt = -1 }, { .alive = true, .kind = TK_FROST, .level = 1, .tgt = -1 } };
  static cur_lay A, B;
  int saved_next = g_next;
  cw_reply_printf(r, "{\"players\":%d,\"slots\":%d,\"first\":[", n, G.S.m.nslot);
  int listed = 0;
  for (int a = 0; a < G.S.m.nslot; a++) {
    if (only >= 0 && a != only) continue;
    int i = mp_owner(&G.S.m.slot[a], n);
    float su, sv;
    if (!view_screen((t3_vec3){ mp_wx(&G.S.m, G.S.m.slot[a].x), MP_GROUND_H, mp_wz(&G.S.m, G.S.m.slot[a].y) }, &su, &sv)) continue;
    float ax = su * cs.x, ay = sv * cs.y;
    for (int sa = 0; sa < 5 && singles; sa++) { /* alone: on screen, clear of the panels and the top line */
      g_next = g_nfixed;
      float x1 = ax;
      cursor_place(i, &ST[sa], 999, &x1, ay, 1, u, cs, false, &A);
      bool off = A.l < 0 || A.t < 0 || A.r > cs.x || A.b > cs.y, hit = false;
      int on = -1;
      for (int j = 0; j < g_nfixed; j++) if (ext_hit(&g_ext[j], A.l, A.t, A.r, A.b, 0)) hit = true, on = on < 0 ? j : on;
      for (int q = 0; q < A.n; q++) hit |= rect_hit(A.lb, A.it[q].box);
      cases++;
      if (off || hit) {
        bad++;
        if (listed++ < 8) {
          cw_reply_printf(r, "%s{\"slot\":%d,\"state\":%d,\"why\":\"%s\",\"lay\":[%.1f,%.1f,%.1f,%.1f],\"at\":[%.1f,%.1f]", listed > 1 ? "," : "", a, sa, off ? "off screen" : on >= 0 ? "on a panel" : "label on own pill",
                          (double)A.l, (double)A.t, (double)A.r, (double)A.b, (double)x1, (double)ay);
          if (on >= 0) cw_reply_printf(r, ",\"ext\":[%.1f,%.1f,%.1f,%.1f]", (double)g_ext[on].l, (double)g_ext[on].t, (double)g_ext[on].r, (double)g_ext[on].b);
          cw_reply_printf(r, "}");
        }
      }
    }
    if (n < 2 || !pairs) continue;
    for (int b = 0; b < G.S.m.nslot; b++) { /* in pairs with another player's slot: the lower seat placed first */
      int j = mp_owner(&G.S.m.slot[b], n);
      if (j == i || !view_screen((t3_vec3){ mp_wx(&G.S.m, G.S.m.slot[b].x), MP_GROUND_H, mp_wz(&G.S.m, G.S.m.slot[b].y) }, &su, &sv)) continue;
      float bx = su * cs.x, by = sv * cs.y;
      for (int sa = 0; sa < 5; sa += 2) /* empty, the second level, frost */
        for (int sb = 0; sb < 5; sb += 2)
          for (int kk = 0; kk < 3; kk++) { /* both showing their pills; the first faded; the second faded */
            float ka = kk == 1 ? 0 : 1, kb = kk == 2 ? 0 : 1;
            g_next = g_nfixed;
            float xa = ax, xb = bx;
            if (i < j) {
              cursor_place(i, &ST[sa], 999, &xa, ay, ka, u, cs, false, &A), ext_add(A.l, A.t, A.r, A.b);
              cursor_place(j, &ST[sb], 999, &xb, by, kb, u, cs, false, &B);
            } else {
              cursor_place(j, &ST[sb], 999, &xb, by, kb, u, cs, false, &B), ext_add(B.l, B.t, B.r, B.b);
              cursor_place(i, &ST[sa], 999, &xa, ay, ka, u, cs, false, &A);
            }
            cases++;
            bool hit = rect_hit(A.lb, B.lb), off = A.l < 0 || A.t < 0 || A.r > cs.x || A.b > cs.y || B.l < 0 || B.t < 0 || B.r > cs.x || B.b > cs.y;
            for (int q = 0; q < A.n && A.pills; q++) hit |= rect_hit(A.it[q].box, B.lb) || rect_hit(A.lb, A.it[q].box);
            for (int q = 0; q < B.n && B.pills; q++) hit |= rect_hit(B.it[q].box, A.lb) || rect_hit(B.lb, B.it[q].box);
            for (int q = 0; q < A.n && A.pills; q++)
              for (int w = 0; w < B.n && B.pills; w++) hit |= rect_hit(A.it[q].box, B.it[w].box);
            for (int q = 0; q < g_nfixed; q++) hit |= ext_hit(&g_ext[q], A.l, A.t, A.r, A.b, 0) || ext_hit(&g_ext[q], B.l, B.t, B.r, B.b, 0);
            if (hit || off) {
              bad++;
              if (listed++ < 8)
                cw_reply_printf(r, "%s{\"slot\":%d,\"other\":%d,\"state\":[%d,%d],\"detail\":[%g,%g],\"why\":\"%s\"}", listed > 1 ? "," : "", a, b, sa, sb, (double)ka, (double)kb, off ? "off screen" : "overlap");
            }
          }
    }
  }
  g_next = saved_next;
  cw_reply_printf(r, "],\"cases\":%d,\"bad\":%d,\"fixed\":[", cases, bad);
  for (int j = 0; j < g_nfixed; j++) cw_reply_printf(r, "%s[%.1f,%.1f,%.1f,%.1f]", j ? "," : "", (double)g_ext[j].l, (double)g_ext[j].t, (double)g_ext[j].r, (double)g_ext[j].b);
  cw_reply_printf(r, "]}");
}
static void hud_place_trace(cw_reply *r, int slot, int state) {
  cw_vec2 cs = g_hud_cs;
  float u = cs.y / 1080.0f, m = 4 * u, su, sv;
  const tower ST[5] = { { .alive = false }, { .alive = true, .kind = TK_BOLT, .level = 1, .tgt = -1 }, { .alive = true, .kind = TK_BOLT, .level = 2, .tgt = -1 },
                        { .alive = true, .kind = TK_BOLT, .level = 3, .branch = BR_A, .tgt = -1 }, { .alive = true, .kind = TK_FROST, .level = 1, .tgt = -1 } };
  if (slot < 0 || slot >= G.S.m.nslot || state < 0 || state > 4) { cw_reply_error(r, "hud place <slot> <state 0..4>"); return; }
  view_screen((t3_vec3){ mp_wx(&G.S.m, G.S.m.slot[slot].x), MP_GROUND_H, mp_wz(&G.S.m, G.S.m.slot[slot].y) }, &su, &sv);
  float cx = su * cs.x, cy = sv * cs.y;
  int i = mp_owner(&G.S.m.slot[slot], G.S.players), saved = g_next;
  g_next = g_nfixed;
  static cur_lay L, T;
  cursor_layout(i, &ST[state], 999, cx, cy, 1, u, cs, false, &L);
  cw_reply_printf(r, "{\"at\":[%.1f,%.1f],\"lay\":[%.1f,%.1f,%.1f,%.1f],\"hits\":%d,\"fixed\":%d,\"candidates\":[", (double)cx, (double)cy, (double)L.l, (double)L.t, (double)L.r, (double)L.b, lay_hits(&L, m * 0.5f), g_nfixed);
  for (int j = 0; j < g_next; j++)
    for (int side = 0; side < 2; side++) {
      float c = side ? g_ext[j].b + m + (cy - L.t) : g_ext[j].t - m - (L.b - cy);
      cursor_layout(i, &ST[state], 999, cx, c, 1, u, cs, false, &T);
      c = side ? g_ext[j].b + m + (c - T.t) : g_ext[j].t - m - (T.b - c);
      cursor_layout(i, &ST[state], 999, cx, c, 1, u, cs, false, &T);
      int on = -1;
      for (int q = 0; q < g_next && on < 0; q++) if (ext_hit(&g_ext[q], T.l, T.t, T.r, T.b, m * 0.5f)) on = q;
      cw_reply_printf(r, "%s{\"ext\":%d,\"side\":%d,\"c\":%.1f,\"lay\":[%.1f,%.1f,%.1f,%.1f],\"off\":%d,\"on\":%d}", j || side ? "," : "", j, side, (double)c, (double)T.l, (double)T.t, (double)T.r, (double)T.b,
                      T.t < 2 * u || T.b > cs.y - 2 * u, on);
    }
  float x1 = cx, placed = cursor_place(i, &ST[state], 999, &x1, cy, 1, u, cs, false, &T);
  cw_reply_printf(r, "],\"placed\":%.1f,\"lay\":[%.1f,%.1f,%.1f,%.1f]}", (double)placed, (double)T.l, (double)T.t, (double)T.r, (double)T.b);
  g_next = saved;
}
static void cmd_hud(int argc, const char **argv, cw_reply *r) {
  if (argc >= 4 && !strcmp(argv[1], "place")) { hud_place_trace(r, atoi(argv[2]), atoi(argv[3])); return; }
  if (argc >= 3 && !strcmp(argv[1], "labels")) { g_hud_labels = atoi(argv[2]) != 0; cw_reply_printf(r, "{\"labels\":%d}", g_hud_labels); return; }
  if (argc >= 3 && !strcmp(argv[1], "style")) { g_hud_style = atoi(argv[2]) ? 1 : 0; for (int i = 0; i < MAXP; i++) g_hint_t[i] = 0; cw_reply_printf(r, "{\"style\":%d}", g_hud_style); return; }
  if (argc >= 3 && !strcmp(argv[1], "push")) { g_hud_push = (float)atof(argv[2]); cw_reply_printf(r, "{\"push\":%g}", (double)g_hud_push); return; }
  if (argc >= 2 && !strcmp(argv[1], "sweep")) { bool one = argc >= 3; hud_sweep(r, !one || !strcmp(argv[2], "singles"), !one || !strcmp(argv[2], "pairs"), argc >= 4 ? atoi(argv[3]) : -1); return; }
  if (argc >= 2 && !strcmp(argv[1], "slots")) { /* the map's slots: tile, owner (for the seats playing), where on the canvas */
    cw_vec2 cs = g_hud_cs;
    cw_reply_printf(r, "{\"players\":%d,\"slots\":[", G.S.players);
    for (int k = 0; k < G.S.m.nslot; k++) {
      float su = -1, sv = -1;
      view_screen((t3_vec3){ mp_wx(&G.S.m, G.S.m.slot[k].x), MP_GROUND_H, mp_wz(&G.S.m, G.S.m.slot[k].y) }, &su, &sv);
      cw_reply_printf(r, "%s{\"slot\":%d,\"x\":%d,\"y\":%d,\"owner\":%d,\"sx\":%.1f,\"sy\":%.1f,\"tower\":%d}", k ? "," : "", k, G.S.m.slot[k].x, G.S.m.slot[k].y,
                      mp_owner(&G.S.m.slot[k], G.S.players), (double)(su * cs.x), (double)(sv * cs.y), G.S.tw[k].alive ? G.S.tw[k].kind * 8 + G.S.tw[k].level : -1);
    }
    cw_reply_printf(r, "]}");
    return;
  }
  if (argc >= 4 && !strcmp(argv[1], "cursor")) { /* tests: a seat's cursor put on one of its slots */
    int i = atoi(argv[2]), k = atoi(argv[3]);
    if (i < 0 || i >= MAXP || !G.S.p[i].active) { cw_reply_error(r, "hud cursor: seat %d is not playing", i); return; }
    if (!sim_slot_mine(&G.S, i, k)) { cw_reply_error(r, "hud cursor: slot %d is not seat %d's", k, i); return; }
    G.S.p[i].slot = k;
    cw_reply_printf(r, "{\"seat\":%d,\"slot\":%d}", i, k);
    return;
  }
  if (argc >= 2) { cw_reply_error(r, "usage: hud | hud labels 0|1 | hud style 0|1 | hud push <px> | hud sweep [singles|pairs [slot]] | hud slots | hud cursor <seat> <slot>"); return; }
  static const char *const KIND[4] = { "label", "panel", "top", "button" };
  cw_reply_printf(r, "{\"canvas\":[%g,%g],\"players\":%d,\"labels\":%d,\"style\":%d,\"push\":%g,\"rects\":[", (double)g_hud_cs.x, (double)g_hud_cs.y, G.S.players, g_hud_labels, g_hud_style, (double)g_hud_push);
  for (int k = 0; k < g_nhr; k++) {
    const hud_rect *q = &g_hr[k];
    cw_reply_printf(r, "%s{\"kind\":\"%s\",\"p\":%d,\"s\":", k ? "," : "", KIND[q->kind], q->player == 255 ? -1 : q->player + 1);
    cw_reply_str(r, q->s);
    cw_reply_printf(r, ",\"box\":"), reply_rect(r, q->box);
    cw_reply_printf(r, ",\"text\":"), reply_rect(r, q->text);
    cw_reply_printf(r, "}");
  }
  cw_reply_printf(r, "]}");
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
  g_nhr = 0, g_next = 0, g_hud_cs = cs;
  for (int i = 0; i < G.S.players; i++) hud_player(i, u, cs);
  hud_top(u, cs);
  g_nfixed = g_next;
  /* banners: the map's name at the start, each wave as it comes (under the cursors' pills, which stay readable) */
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
#ifdef TOWERS_AUTOPILOT
  bool cursors = !dev_env("TOWERS_NO_CURSORS");
#else
  bool cursors = true;
#endif
  if (cursors && G.S.state == ST_PLAY)
    for (int i = 0; i < G.S.players; i++) hud_cursor(i, u, cs);
  /* boss health over the boss */
  for (int i = 0; i < MAXE; i++) {
    const creep *e = &G.S.e[i];
    float su, sv;
    if (!e->alive || !e->boss || !view_screen((t3_vec3){ e->x, 3.5f, e->y }, &su, &sv)) continue;
    float w = 90 * u, h = 9 * u, x = su * cs.x - w * 0.5f, y = sv * cs.y - h;
    cw_r2d_rect(cw_rect_make(x - 2 * u, y - 2 * u, w + 4 * u, h + 4 * u), rgba(0, 0, 0, 0.8f));
    cw_r2d_rect(cw_rect_make(x, y, w * CW_CLAMP(e->hp / e->maxhp, 0.0f, 1.0f), h), rgba(1, 0.25f, 0.2f, 1));
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
    if (dev_env("TOWERS_NO_CURSORS")) for (int i = 0; i < MAXP; i++) show[i] = false;
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
  show_hud = !dev_env("TOWERS_NO_HUD");
#endif
  if (show_hud && G.in_match && (sc == CW_SHELL_PLAY || sc == CW_SHELL_PAUSE)) { CW_PROFILE_SCOPE("towers.hud"); hud(); }
  cw_quality_auto_measure(sc == CW_SHELL_PLAY);
  double now = wall();
  if (G.t_last > 0) G.t_frame += (now - G.t_last) * 1000;
  G.t_last = now;
  G.t_n++;
  if (getenv("TOWERS_BENCH") && sc == CW_SHELL_PLAY) {
    static int n, gn;
    static double gsum, gmax;
    { /* every frame's latest GPU time over the window: its mean and its worst (the line's gpu_total is one frame's) */
      float g = cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree()));
      if (g > 0) gsum += g, gn++, gmax = g > gmax ? g : gmax;
    }
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
      fprintf(stderr, " gpu_window=%.2f gpu_worst=%.2f\n", gn ? gsum / gn : 0.0, gmax);
      gsum = gmax = 0, gn = 0;
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
