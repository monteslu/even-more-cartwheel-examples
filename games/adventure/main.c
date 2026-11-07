/* adventure (working title): a third-person action adventure through one connected world, on cartwheel and
 * three.c (DESIGN.md). One player; a second pad drops in at any time and plays beside the first.
 *
 *   move       the stick or the d-pad (the camera follows by itself; the right stick turns it)
 *   attack     a light cut, three in a row; heavy: a spinning cut (the maul: an overhead, a ground slam)
 *   dodge      a roll the way you move (invulnerable through it), a back step when you hold no direction
 *   lock       lock on to the nearest foe in view (the camera keeps you both in view); again to let go
 *   use        rest at a shrine, open a chest, pull a lever, open a gate; with nothing near, change weapon
 *   tonic      drink from the flask
 *
 * adventure_auto plays itself (for hosts that send no input); adventure_bench is the perf gate's scene.
 * Environment (development, native): ADV_FROM=<route index> starts the bot further on (teleported, progress given),
 * ADV_QUALITY=0..4 a graphics preset, ADV_BENCH=1 prints timings every 120 frames, ADV_NO_HUD=1 hides the HUD. */
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

void view_fixed(void);

static double wall_s(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static struct {
  bool in_match, built;
  int players;
  cw_action move[4], cam[4], attack, heavy, dodge, use, lock, tonic, swap;
  cw_node camera;
  int32_t state, tick, region, kills, hits_taken, hits_landed, dodges, dodged, tells, keys, hearts, maul, route, boss_hp, deaths, rests, stuck;
  int32_t hp[MAXP], stam[MAXP], px[MAXP], py[MAXP], pz[MAXP], pstate[MAXP], target, foes_live, rigs, feet, flags0, flags3, cam_clip_t, world_hash;
  uint32_t hash;
  float cam_clip, cam_jerk;
  int32_t cam_jerks;
  t3_vec3 cam_p[2];
  int cam_n;
  double t_sim, t_view, t_frame, t_last;
  int t_n;
} G;

static cw_font font;
static cw_sound snd_move, snd_select, snd_back, snd_music;
static cw_voice music;

static cw_sound tone(float seconds, float hz0, float hz1, float gain, float decay) {
  enum { RATE = 48000 };
  static float pcm[RATE * 6];
  int n = CW_MIN((int)(seconds * RATE), RATE * 6);
  double ph = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (float)n;
    ph += 2 * 3.14159265358979 * (hz0 + (hz1 - hz0) * t) / RATE;
    pcm[i] = (float)sin(ph) * gain * (float)exp(-t * decay) * (i < 300 ? (float)i / 300.0f : 1);
  }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}
/* a slow modal drone with a little melody over it: the music (computed) */
static cw_sound make_music(void) {
  enum { RATE = 32000, SECS = 16 };
  static float pcm[RATE * SECS];
  static const float NOTES[16] = { 220, 0, 261.6f, 293.7f, 329.6f, 0, 293.7f, 261.6f, 220, 0, 196, 220, 261.6f, 0, 246.9f, 220 };
  double a = 0, b = 0, m = 0;
  for (int i = 0; i < RATE * SECS; i++) {
    float t = (float)i / RATE;
    a += 2 * 3.14159265358979 * 110 / RATE, b += 2 * 3.14159265358979 * 164.8 / RATE;
    int note = (int)(t / 1.0f) % 16;
    float nt = t - floorf(t), f = NOTES[note];
    if (f > 0) m += 2 * 3.14159265358979 * f / RATE;
    float env = f > 0 ? (float)exp(-nt * 2.2) * fminf(1, nt * 40) : 0;
    float drone = (float)(sin(a) * 0.5 + sin(b) * 0.3) * (0.6f + 0.4f * (float)sin(t * 0.4));
    pcm[i] = drone * 0.08f + (float)sin(m) * env * 0.09f + (float)sin(m * 2) * env * 0.03f;
  }
  return cw_sound_from_pcm(pcm, RATE * SECS, 1, RATE, NULL);
}
static void shell_sound(cw_shell_sound s, void *u) {
  (void)u;
  cw_audio_play(s == CW_SHELL_SND_MOVE ? snd_move : s == CW_SHELL_SND_BACK ? snd_back : snd_select, &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = 1 });
}
static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *u) {
  (void)from, (void)u;
  bool want = to == CW_SHELL_TITLE || to == CW_SHELL_PLAY;
  if (want && !cw_voice_is_playing(music)) music = cw_audio_play(snd_music, &(cw_play_desc){ .bus = cw_shell_music_bus(), .loop = true, .volume = 0.5f });
  if (!want && cw_voice_is_playing(music)) cw_voice_stop(music, 0.3f);
}

/* ── the match ──────────────────────────────────────────────────────── */
static bool build_step(void) {
  if (G.built) return true;
  static bool view_done;
  cw_node root = cw_tree_root(cw_scene_app_tree());
  if (!view_done) {
    int done = 0, total = 1;
    view_done = view_init_step(root, &done, &total);
    cw_loading_set(done, total + 1, CW_LOAD_MESHES);
    return false;
  }
  fx_init(root);
  if (getenv("ADV_GROUND_CHECK")) {
    float worst;
    int bad = world_ground_check(&worst);
    bad += world_tree_check(&worst);
    const wprop *pp;
    extern int world_scatter(const float (**pos)[5], const int **layer);
    const float (*sp)[5];
    const int *sl;
    fprintf(stderr, "adv-ground props=%d boxes=%d scatter=%d floating=%d worst_cm=%d\n", world_props(&pp), world_box_count(), world_scatter(&sp, &sl), bad, (int)(worst * 100));
  }
  G.camera = cw_node_get(root, "World/Camera");
  G.built = true;
  return true;
}
static void build_all(void) { while (!build_step()) {} }

/* development: start further along the route with the progress a player would have there */
static void jump_ahead(int to) {
  for (int i = 0; i < to && i < g_nroute; i++) {
    const wroute *r = &g_route[i];
    if (r->need_flag >= 0) sim_set_flag(r->need_flag);
  }
  if (sim_has_flag(3)) S.sv.maul = 1;
  if (sim_has_flag(100 + K_THORN)) S.sv.keys |= 1, S.sv.hearts++;
  if (sim_has_flag(100 + K_TIDE)) S.sv.keys |= 2, S.sv.hearts++;
  if (sim_has_flag(2)) S.sv.hearts++;
  if (sim_has_flag(4)) S.sv.hearts++;
  if (sim_has_flag(6)) S.sv.charms++;
  save_data sv = S.sv;
  sim_init(S.players, &sv);
  for (int k = 0; k < g_nobj; k++) world_obj_state(k, g_obj[k].flag >= 0 && sim_has_flag(g_obj[k].flag));
  const wroute *r = &g_route[CW_MAX(0, to - 1)];
  if (to < g_nroute && g_route[to].act == RA_FIGHT) /* a boss: start at the edge of its ring */
    sim_teleport_party(lerpf(r->x, g_route[to].x, 0.55f), lerpf(r->z, g_route[to].z, 0.55f));
  else sim_teleport_party(r->x, r->z);
  bot_jump(to);
}

static void start(int players, void *u) {
  (void)u;
  build_all();
  G.players = players;
  G.in_match = true;
  save_data *sd = cw_shell_game_save();
#ifdef ADV_AUTOPILOT
  sim_init(players, NULL); /* the bot always starts a new journey */
  bot_reset();
  const char *from = getenv("ADV_FROM");
#ifdef ADV_BENCH
  if (!from) from = "22"; /* the bench: the Tidecaller's court, the fight on */
#endif
  if (from) jump_ahead(atoi(from));
  extern bool g_bench;
#ifdef ADV_BENCH
  g_bench = true;
#endif
  if (getenv("ADV_GOD")) g_bench = true;
  (void)sd;
#else
  sim_init(players, sd && sd->version == 1 ? sd : NULL);
#endif
  view_reset();
}
static void players_changed(int players, void *u) {
  (void)u;
  for (int i = 0; i < players && i < MAXP; i++)
    if (!S.a[i].used && cw_shell_seat_taken(i)) sim_add_player(i);
  G.players = players;
}
static bool warmup(void *u) {
  (void)u;
  if (!build_step()) return false;
  if (!G.in_match) sim_init(1, NULL); /* a world behind the title, and every program it draws compiled */
  return true;
}

static void init(void *u) {
  (void)u;
  cw_project_load_input();
  const char *mv[4] = { "move_left", "move_right", "move_up", "move_down" }, *cm[4] = { "cam_left", "cam_right", "cam_up", "cam_down" };
  for (int i = 0; i < 4; i++) G.move[i] = cw_action_find(mv[i]), G.cam[i] = cw_action_find(cm[i]);
  G.attack = cw_action_find("attack"), G.heavy = cw_action_find("heavy"), G.dodge = cw_action_find("dodge"), G.use = cw_action_find("use");
  G.lock = cw_action_find("lock"), G.tonic = cw_action_find("tonic"), G.swap = cw_action_find("swap");
  cw_shell_init(&(cw_shell_desc){ .title = "ADVENTURE", .subtitle = "one world, three wardens, a long way round", .max_players = MAXP, .shared_screen = true,
                                  .game_save_size = sizeof(save_data), .game_save_version = 1, .start = start, .players_changed = players_changed,
                                  .sound = shell_sound, .screen_changed = shell_screen });
  font = cw_ui_font_sans();
  cw_prompt_set_font(font);
  cw_quality_full_resolution(true); /* no preset draws below the output size */
  if (getenv("ADV_QUALITY")) cw_quality_set((cw_quality)CW_CLAMP(atoi(getenv("ADV_QUALITY")), 0, CW_QUALITY_COUNT - 1), true);
  snd_move = tone(0.05f, 660, 660, 0.25f, 2), snd_select = tone(0.12f, 520, 1040, 0.3f, 3), snd_back = tone(0.12f, 520, 260, 0.3f, 3);
  snd_music = make_music();
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.tick", &G.tick);
  cw_debug_i32("game.region", &G.region);
  cw_debug_i32("game.kills", &G.kills);
  cw_debug_i32("game.hits_taken", &G.hits_taken);
  cw_debug_i32("game.hits_landed", &G.hits_landed);
  cw_debug_i32("game.dodges", &G.dodges);
  cw_debug_i32("game.dodged", &G.dodged);
  cw_debug_i32("game.tells", &G.tells);
  cw_debug_i32("game.keys", &G.keys);
  cw_debug_i32("game.hearts", &G.hearts);
  cw_debug_i32("game.maul", &G.maul);
  cw_debug_i32("game.route", &G.route);
  cw_debug_i32("game.boss_hp", &G.boss_hp);
  cw_debug_i32("game.deaths", &G.deaths);
  cw_debug_i32("game.rests", &G.rests);
  cw_debug_i32("game.flags0", &G.flags0);
  cw_debug_i32("game.flags3", &G.flags3);
  cw_debug_i32("game.target", &G.target);
  cw_debug_i32("game.interact", &S.interact);
  { extern int bot_step_ticks(void); static int32_t rt; (void)rt; }
  cw_debug_i32("game.foes_live", &G.foes_live);
  cw_debug_field("player.hp", G.hp, CW_DBG_I32, MAXP);
  cw_debug_field("player.stamina", G.stam, CW_DBG_I32, MAXP);
  cw_debug_field("player.x", G.px, CW_DBG_I32, MAXP);
  cw_debug_field("player.y", G.py, CW_DBG_I32, MAXP);
  cw_debug_field("player.z", G.pz, CW_DBG_I32, MAXP);
  cw_debug_field("player.state", G.pstate, CW_DBG_I32, MAXP);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_i32("world.hash", &G.world_hash);
  cw_debug_i32("view.rigs", &G.rigs);
  cw_debug_i32("view.feet", &G.feet);
  cw_debug_i32("view.cam_clip_t", &G.cam_clip_t);
  cw_debug_f32("view.cam_clip", &G.cam_clip);
  cw_debug_i32("view.cam_jerks", &G.cam_jerks);
  cw_debug_i32("view.cam_blocked", &S.cam_blocked);
  cw_debug_i32("bot.stuck", &G.stuck);
#ifdef ADV_AUTOPILOT
  cw_shell_start(getenv("ADV_PLAYERS") ? CW_CLAMP(atoi(getenv("ADV_PLAYERS")), 1, MAXP) : 1);
#endif
}

/* ── input ──────────────────────────────────────────────────────────── */
static void read_input(input in[MAXP]) {
  memset(in, 0, sizeof(input) * MAXP);
  for (int i = 0; i < MAXP; i++) {
    if (!S.a[i].used) continue;
#ifdef ADV_AUTOPILOT
    bot_input(i, &in[i]);
#else
    cw_vec2 m = cw_input_vector(i, G.move[0], G.move[1], G.move[2], G.move[3], CW_DEADZONE_SCALED_RADIAL, 0.2f);
    cw_vec2 c = cw_input_vector(i, G.cam[0], G.cam[1], G.cam[2], G.cam[3], CW_DEADZONE_SCALED_RADIAL, 0.2f);
    in[i] = (input){ m.x, m.y, c.x, c.y, cw_action_just_pressed(i, G.attack), cw_action_just_pressed(i, G.heavy), cw_action_just_pressed(i, G.dodge),
                     cw_action_just_pressed(i, G.use), cw_action_just_pressed(i, G.lock), cw_action_just_pressed(i, G.tonic), cw_action_just_pressed(i, G.swap),
                     cw_action_pressed(i, G.attack), cw_action_pressed(i, G.heavy), cw_action_pressed(i, G.dodge) };
#endif
  }
}

/* ADV_LOG=1: one status line a minute of game time (the soak reads them) */
static void status_line(void) {
  int nan = 0, live = 0;
  for (int i = 0; i < MAXA; i++) {
    const actor *a = &S.a[i];
    if (!a->used) continue;
    t3_vec3 p = a->mv.position;
    if (p.x != p.x || p.y != p.y || p.z != p.z) nan++;
    live += i >= MAXP && a->active && a->state != ST_DEAD;
  }
  fprintf(stderr, "adv-status tick=%u route=%d state=%d region=%d kills=%d deaths=%d rests=%d hp=%d keys=%d maul=%d hearts=%d stuck=%d camblock=%d nan=%d live=%d x=%d z=%d boss=%d pst=%d bst=%d bx=%d bz=%d by=%d py=%d shrine=%d\n", S.tick, bot_route_index(), S.state,
          S.region, S.kills, S.deaths, S.sv.rests, S.a[0].hp, S.sv.keys, S.sv.maul, S.sv.hearts, g_bot_stuck, S.cam_blocked, nan, live, (int)S.a[0].mv.position.x, (int)S.a[0].mv.position.z, S.boss >= 0 ? S.a[S.boss].hp : -1, S.a[0].state, S.boss >= 0 ? S.a[S.boss].state : -1,
          S.boss >= 0 ? (int)S.a[S.boss].mv.position.x : 0, S.boss >= 0 ? (int)S.a[S.boss].mv.position.z : 0, S.boss >= 0 ? (int)S.a[S.boss].mv.position.y : 0, (int)S.a[0].mv.position.y, S.sv.shrine);
  fflush(stderr);
}

static void fixed_update(void *u, double dt) {
  (void)u, (void)dt;
  if (!cw_shell_update()) return;
  if (!G.in_match) return;
  cw_shell_join_press(); /* a second pad drops in with one press (players_changed adds them) */
  input in[MAXP];
  read_input(in);
  double t0 = wall_s();
  /* ADV_TURBO=n (native soak runs): n rule ticks per engine tick, the bot reading the state between them */
  static int turbo = -1;
  if (turbo < 0) turbo = getenv("ADV_TURBO") ? CW_CLAMP(atoi(getenv("ADV_TURBO")), 1, 240) : 1;
  for (int k = 0; k < turbo; k++) {
    if (k) read_input(in);
    sim_step(in);
    if (getenv("ADV_LOG") && S.tick % (getenv("ADV_LOG_EVERY") ? atoi(getenv("ADV_LOG_EVERY")) : 3600) == 0) status_line();
    if (getenv("ADV_LOG") && S.state == GS_WON && S.state_t == 1) { fprintf(stderr, "adv-won tick=%u kills=%d deaths=%d\n", S.tick, S.kills, S.deaths); status_line(); }
  }
  view_fixed();
  G.t_sim += (wall_s() - t0) * 1000;
  G.state = S.state, G.tick = (int32_t)S.tick, G.region = S.region, G.kills = S.kills, G.hits_taken = S.hits_taken, G.hits_landed = S.hits_landed;
  G.dodges = S.dodges, G.dodged = S.dodged, G.tells = S.tells, G.keys = S.sv.keys, G.hearts = S.sv.hearts, G.maul = S.sv.maul, G.route = bot_route_index();
  G.boss_hp = S.boss >= 0 ? S.a[S.boss].hp : -1, G.deaths = S.deaths, G.rests = S.sv.rests, G.flags0 = (int32_t)S.sv.flags[0], G.flags3 = (int32_t)S.sv.flags[3];
  G.target = S.a[0].target, G.hash = S.hash, G.world_hash = (int32_t)world_hash(), G.stuck = g_bot_stuck, G.cam_clip_t = S.cam_clip_t, G.cam_clip = S.cam_clip;
  int live = 0;
  for (int i = MAXP; i < MAXA; i++) live += S.a[i].used && S.a[i].active && S.a[i].state != ST_DEAD;
  G.foes_live = live;
  for (int i = 0; i < MAXP; i++) {
    const actor *a = &S.a[i];
    G.hp[i] = a->used ? a->hp : 0, G.stam[i] = a->used ? (int)a->stamina : 0, G.pstate[i] = a->used ? a->state : -1;
    G.px[i] = (int)(a->mv.position.x * 100), G.py[i] = (int)(a->mv.position.y * 100), G.pz[i] = (int)(a->mv.position.z * 100);
  }
  view_stats vs = view_get_stats();
  G.rigs = vs.rigs, G.feet = vs.feet;
}

/* ── the HUD ────────────────────────────────────────────────────────── */
static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static cw_text_style tstyle(float size, cw_color c) { return (cw_text_style){ .size = size, .color = c, .outline_width = size * 0.09f, .outline_color = rgba(0, 0, 0, 0.9f) }; }
static cw_text_metrics measure(const char *s, float size) { cw_text_style st = tstyle(size, rgba(1, 1, 1, 1)); return cw_font_measure(font, s, &st); }
static cw_text_metrics text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_style st = tstyle(size, c);
  if (al != CW_ALIGN_LEFT) { cw_text_metrics m = cw_font_measure(font, s, &st); x -= al == CW_ALIGN_CENTER ? m.width * 0.5f : m.width; }
  return cw_r2d_text(font, s, cw_v2(x, y), &st);
}
static void bar(float x, float y, float w, float h, float k, cw_color c, cw_color back) {
  cw_r2d_rect(cw_rect_make(x - 2, y - 2, w + 4, h + 4), rgba(0, 0, 0, 0.6f));
  cw_r2d_rect(cw_rect_make(x, y, w, h), back);
  cw_r2d_rect(cw_rect_make(x, y, w * cw_clampf(k, 0, 1), h), c);
}

static void hud_player(int i, float u, cw_vec2 cs) {
  const actor *p = &S.a[i];
  float x = i == 0 ? 28 * u : cs.x - 28 * u - 420 * u, y = 26 * u;
  float hw = (300 + 1.0f * (float)(p->hp_max - 100)) * u, sw = (240 + 1.0f * (p->stamina_max - 100)) * u;
  float bs = 26 * u;
  char b[48];
  snprintf(b, sizeof b, "P%d", i + 1);
  cw_text_metrics nm = text(b, x, y - 4 * u, bs, i == 0 ? rgba(0.4f, 1, 0.95f, 1) : rgba(1, 0.55f, 0.5f, 1), CW_ALIGN_LEFT);
  float bx = x + nm.width + 14 * u;
  bool low = p->hp < p->hp_max / 4;
  bar(bx, y, hw, 18 * u, (float)p->hp / (float)p->hp_max, low && (S.tick / 12) % 2 ? rgba(1, 0.5f, 0.4f, 1) : rgba(0.92f, 0.2f, 0.25f, 1), rgba(0.25f, 0.05f, 0.06f, 0.85f));
  bar(bx, y + 28 * u, sw, 12 * u, p->stamina / p->stamina_max, rgba(0.35f, 0.95f, 0.45f, 1), rgba(0.06f, 0.2f, 0.08f, 0.85f));
  /* the flask: a round shape with its count; the keys */
  float fy = y + 56 * u, iu = u * cw_text_drawn_size(font, bs) / bs;
  cw_r2d_circle(cw_v2(bx + 12 * iu, fy + 14 * iu), 11 * iu, 16, rgba(0.35f, 1, 0.55f, 0.95f));
  cw_r2d_rect(cw_rect_make(bx + 8 * iu, fy - 4 * iu, 8 * iu, 9 * iu), rgba(0.7f, 0.5f, 0.3f, 1));
  snprintf(b, sizeof b, "%d", p->tonics);
  cw_text_metrics tm = text(b, bx + 30 * iu, fy, bs, rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
  float kx = bx + 30 * iu + tm.width + 24 * u;
  for (int k = 0; k < 2; k++) {
    bool have = (S.sv.keys >> k) & 1;
    cw_color kc = k == 0 ? rgba(1, 0.8f, 0.3f, have ? 1 : 0.25f) : rgba(0.75f, 0.55f, 1, have ? 1 : 0.25f);
    cw_r2d_circle(cw_v2(kx + 9 * iu, fy + 8 * iu), 7 * iu, 14, kc);
    cw_r2d_rect(cw_rect_make(kx + 6 * iu, fy + 12 * iu, 5 * iu, 16 * iu), kc);
    kx += 30 * iu;
  }
  if (p->weapon == 1) text("STONEBREAKER", kx + 12 * u, fy, bs, rgba(1, 0.75f, 0.4f, 1), CW_ALIGN_LEFT);
  if (p->state == ST_DEAD && i > 0) text("back soon", bx, fy + 34 * u, bs, rgba(1, 0.6f, 0.5f, 1), CW_ALIGN_LEFT);
}

static void hud(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 1080.0f, bs = 26 * u;
  cw_r2d_begin(NULL);
  for (int i = 0; i < MAXP; i++) if (S.a[i].used) hud_player(i, u, cs);
  if (!S.a[1].used) cw_prompt_text(font, "{ui_accept}: a second player joins", -1, cw_v2(cs.x - 28 * u, 28 * u), bs, rgba(1, 1, 1, 0.55f), CW_ALIGN_RIGHT); /* prompt-ok: the join press is the shell's */
  /* the lock reticle: a diamond over the target */
  const actor *p = &S.a[0];
  if (p->target >= 0) {
    const actor *t = &S.a[p->target];
    float su, sv;
    if (view_screen((t3_vec3){ t->mv.position.x, t->mv.position.y + KIND[t->kind].height * 0.62f, t->mv.position.z }, &su, &sv)) {
      float x = su * cs.x, y = sv * cs.y, r = 12 * u;
      for (int k = 0; k < 4; k++) {
        float a = (float)k * CW_PI / 2;
        cw_r2d_circle(cw_v2(x + cw_cos(a) * r, y + cw_sin(a) * r), 3.2f * u, 8, rgba(1, 0.95f, 0.6f, 0.95f));
      }
      cw_r2d_circle(cw_v2(x, y), 4 * u, 10, rgba(1, 1, 1, 0.9f));
      if (!t->boss) bar(x - 40 * u, y - 46 * u, 80 * u, 7 * u, (float)t->hp / (float)t->hp_max, rgba(1, 0.35f, 0.3f, 1), rgba(0.2f, 0, 0, 0.8f));
    }
  }
  /* the boss: a name and a long bar along the bottom */
  if (S.boss >= 0) {
    const actor *b = &S.a[S.boss];
    float w = cs.x * 0.56f, x = (cs.x - w) / 2, y = cs.y - 84 * u;
    text(KIND[b->kind].name, x, y - 40 * u, 30 * u, rgba(1, 0.92f, 0.8f, 1), CW_ALIGN_LEFT);
    bar(x, y, w, 16 * u, (float)b->hp / (float)b->hp_max, rgba(0.95f, 0.45f, 0.15f, 1), rgba(0.2f, 0.06f, 0.02f, 0.85f));
  }
  /* what the lead player can use here */
  if (S.interact >= 0 && p->state == ST_MOVE) {
    const wobj *o = &g_obj[S.interact];
    const char *what = o->kind == OB_SHRINE ? "{use}: rest" : o->kind == OB_CHEST ? "{use}: open" : o->kind == OB_LEVER ? "{use}: pull" : o->kind == OB_GATE ? ((S.sv.keys & o->item) == o->item ? "{use}: unlock" : "the gate wants two keys") : "{use}: open";
    cw_prompt_text(font, what, 0, cw_v2(cs.x * 0.5f, cs.y * 0.7f), 30 * u, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
  /* the first minute: the controls */
  if (S.sv.play_ticks < 60 * 50 && S.sv.rests == 0) {
    cw_prompt_text(font, "{attack}: cut   {heavy}: heavy   {dodge}: roll   {lock}: lock on   {tonic}: drink", 0, cw_v2(cs.x * 0.5f, cs.y - 60 * u), bs, rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER);
  }
  /* banners: a region's name, an item found, a boss */
  if (S.banner_t > 0 && cw_shell_screen_now() != CW_SHELL_PAUSE) {
    float a = cw_clampf((float)S.banner_t / 40.0f, 0, 1), y = cs.y * 0.24f;
    char b[96];
    const char *line = S.banner_kind == 0 ? REGION_NAME[CW_CLAMP(S.banner_item, 0, RG_COUNT - 1)] : S.banner_kind == 1 ? sim_item_name(S.banner_item) : S.banner_kind == 2 ? "WARDEN FALLEN" : S.banner_kind == 4 ? KIND[S.banner_item].name : "LOCKED";
    y += text(line, cs.x * 0.5f, y, 56 * u, rgba(1, 0.9f, 0.6f, a), CW_ALIGN_CENTER).height;
    if (S.banner_kind == 2 && S.banner_item) { snprintf(b, sizeof b, "%s and a heart vessel", sim_item_name(S.banner_item)); text(b, cs.x * 0.5f, y + 6 * u, 30 * u, rgba(1, 1, 1, a), CW_ALIGN_CENTER); }
    if (S.banner_kind == 1 && S.banner_item == IT_MAUL) cw_prompt_text(font, "{swap}: change weapon   it breaks cracked stone", 0, cw_v2(cs.x * 0.5f, y + 8 * u), bs, rgba(1, 1, 1, a), CW_ALIGN_CENTER);
    if (S.banner_kind == 3) text("the gate wants the Sun Key and the Moon Key", cs.x * 0.5f, y + 6 * u, 30 * u, rgba(1, 1, 1, a), CW_ALIGN_CENTER);
  }
  if (S.state == GS_FALLEN) {
    float a = cw_clampf((float)S.state_t / 60.0f, 0, 0.75f);
    cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), rgba(0.1f, 0.02f, 0.03f, a));
    text("FALLEN", cs.x * 0.5f, cs.y * 0.4f, 80 * u, rgba(1, 0.35f, 0.3f, 1), CW_ALIGN_CENTER);
    text("you wake at the last shrine you rested at", cs.x * 0.5f, cs.y * 0.4f + 96 * u, 30 * u, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
  if (S.state == GS_WON) {
    float a = cw_clampf((float)S.state_t / 120.0f, 0, 0.6f);
    cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), rgba(0.05f, 0.03f, 0.0f, a));
    text("THE EMBER KING HAS FALLEN", cs.x * 0.5f, cs.y * 0.38f, 64 * u, rgba(1, 0.85f, 0.45f, 1), CW_ALIGN_CENTER);
    char b[96];
    snprintf(b, sizeof b, "%d foes   %d falls   %d minutes", S.kills, S.sv.deaths, S.sv.play_ticks / 3600);
    text(b, cs.x * 0.5f, cs.y * 0.38f + 84 * u, 30 * u, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
  cw_r2d_end();
}

/* ── frame ──────────────────────────────────────────────────────────── */
static void render(void *u, double alpha) {
  (void)u;
  cw_shell_screen sc = cw_shell_screen_now();
  g_fx_level = CW_MAX(0, cw_gfx_effective(CW_GFX_EFFECTS) - 1);
  if (G.built) {
    static float t;
    t += cw_frame_dt();
    int w, h;
    cw_window_size(&w, &h);
    view_camera(G.camera, (float)alpha, (float)w / (float)h);
    t3_vec3 p = cw_node3d_position(G.camera);
    if (G.cam_n >= 2 && S.tick > 2 && S.state == GS_PLAY) {
      float jx = p.x - 2 * G.cam_p[0].x + G.cam_p[1].x, jy = p.y - 2 * G.cam_p[0].y + G.cam_p[1].y, jz = p.z - 2 * G.cam_p[0].z + G.cam_p[1].z;
      if (jx * jx + jy * jy + jz * jz > 0.05f * 0.05f) G.cam_jerks++;
    }
    G.cam_p[1] = G.cam_p[0], G.cam_p[0] = p, G.cam_n++;
    world_colour(p.x, p.z, cw_frame_dt());
    world_wind(t);
    world_sky(p, t);
    double t0 = wall_s();
    view_sync((float)alpha);
    fx_events();
    if (sc == CW_SHELL_PLAY) fx_tick(t);
    G.t_view += (wall_s() - t0) * 1000;
  }
  bool show = !getenv("ADV_NO_HUD") && !g_hide_hud;
  if (show && G.in_match && (sc == CW_SHELL_PLAY || sc == CW_SHELL_PAUSE)) hud();
  cw_quality_auto_measure(sc == CW_SHELL_PLAY);
  double now = wall_s();
  if (G.t_last > 0) G.t_frame += (now - G.t_last) * 1000;
  G.t_last = now, G.t_n++;
  if (getenv("ADV_BENCH") && sc == CW_SHELL_PLAY) {
    static int n;
    if (++n % 120 == 0) {
      cw_render3d_stats rs;
      cw_render3d_get_stats(cw_scene_app_tree(), &rs);
      fprintf(stderr, "bench frame_ms=%.2f sim_ms=%.2f view_ms=%.2f tick=%d region=%d foes=%d rigs=%d draws=%d tris=%d boss_hp=%d\n", G.t_frame / G.t_n, G.t_sim / G.t_n,
              G.t_view / G.t_n, (int)S.tick, S.region, G.foes_live, G.rigs, rs.draw_calls, rs.triangles, G.boss_hp);
      const cw_pass_stat *pst;
      int np = cw_pipeline_pass_stats(cw_pipeline_of(cw_scene_app_tree()), &pst);
      fprintf(stderr, "bench_passes gpu_total=%.2f", cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree())));
      for (int k = 0; k < np; k++)
        if (pst[k].ran && (pst[k].gpu_ms_avg > 0.02f || pst[k].cpu_ms_avg > 0.3f)) fprintf(stderr, " %s=%.2f/%.2f", pst[k].name, pst[k].gpu_ms_avg, pst[k].cpu_ms_avg);
      fprintf(stderr, "\n");
      fflush(stderr);
      G.t_sim = G.t_view = G.t_frame = 0, G.t_n = 0;
    }
  }
  { /* a state the program table lacks: each one on its own short log line, once (a long list is cut short in some hosts' logs) */
    static char seen[8192];
    const char *m = cw_pipeline_missing_states(cw_scene_app_tree());
    if (m && *m) {
      char line[256];
      for (const char *p = m; *p;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n && n < sizeof line - 2) {
          memcpy(line, p, n), line[n] = 0;
          char key[260];
          snprintf(key, sizeof key, "|%s|", line);
          if (!strstr(seen, key) && strlen(seen) + strlen(key) < sizeof seen) strcat(seen, key), cw_warn("adventure: missing program state %s", line);
        }
        p += n + (e ? 1 : 0);
      }
    }
  }
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Adventure", .width = 1280, .height = 720, .debug = true, .init = init, .warmup = warmup, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/main.cwscene", &app);
}
