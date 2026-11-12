/* Destructo: knock down structures of wood, stone, glass and steel with a launcher, one to four
 * players taking turns, together (co-op: one score, one count of shots) or against each other (versus: each shot
 * scores for whoever fired it).
 *
 *   select    thirty levels, each with its best stars; how many play and how (co-op, versus, or party: steady hands,
 *             where each turn must bring a block down and whoever drops a crystal is out); free fire (any
 *             structure open, ammunition without end, nothing judged: a physics toy)
 *   play      turn and raise the launcher, set the power, pick the ammunition (shot, heavy ball, charge), fire;
 *             bring every crystal down within par for three stars; hold the wide view to look the structure over
 *   result    judged once the world is still: the stars on a card at the side, the last shot's instant replay (slow
 *             motion on the impact; any button skips it), then the next level or another try
 *   destructo_auto plays itself (the bot, through the same controls); destructo_bench is the perf gate's scene
 *   (the bot on Taj Mahal, a charge breaks about 700 blocks: the camera on the collapse). Run options (the gates and
 *   development, over the debug channel: src/opts.h): dm set LEVEL n | LEVEL_NAME name | PLAYERS n | QUALITY 0..4 |
 *   NO_HUD 1 ..., then dm restart. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
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
#include "fx.h"
#include "game.h"
#include "loader.h"
#include "opts.h"
#include "replay.h"
#include "sound.h"
#include "view.h"
#include "../../common/coplanar.h"

#ifndef DESTRUCTO_BENCH
#define DESTRUCTO_BENCH 0
#endif
#ifndef DESTRUCTO_BENCH_NAME
#define DESTRUCTO_BENCH_NAME "Taj Mahal" /* the perf and hitch gates' scene, by name (levels move as more are added) */
#endif

typedef struct save_data {
  uint8_t stars[NLEVELS];
  int32_t best[NLEVELS];
  int32_t last_level, last_mode;
} save_data;

enum { PH_SELECT, PH_PLAY, PH_RESULT };
enum { H_AIM, H_FIRE, H_ORBIT, H_AMMO, H_VIEW, HINTS };
#define HINT_TICKS 540 /* a hint stays up this long at most */
#define SEL_COLS 4 /* the level select: columns and rows in view */
#define SEL_ROWS 4
static struct {
  game g;
  bot_state bot;
  int loading; /* a level asked for (index + 1) that is still being built: the card or menu stays up meanwhile */
  bool in_match, built, have_game;
  int vstep;         /* view_level_step's next step while fin is 1 */
  int fin;           /* a built level's render side is made a step a frame behind the loading screen: 1 view, 2 effects */
  bool defer_view;   /* load_level_as leaves the render side to those steps */
  int phase, phase_t;
  int sel, players, mode;
  bool free_fire;    /* the select screen's free fire */
  int last_cur, turn_banner;
  bool result_sound;
  cw_action aim_l, aim_r, aim_u, aim_d, fire, ammo, ammo_p, view, orb_l, orb_r;
  /* the hints: a control's prompt shows until it has been used once this session (or has had its time), never for good */
  unsigned used; int hint_t[HINTS];
  /* the pointer (mouse, or a finger as the mouse): a press that drags aims, one that does not fires or picks an icon */
  bool ptr_down, ptr_drag; float ptr_x0, ptr_y0, ptr_x, ptr_y;
  cw_rect ammo_rect[NAMMO];
  cw_node camera;
  bool overview;
  int view_dbg, ammo_dbg;
  int32_t ready_dbg;
  /* the visible acknowledgement of a press (cue 0: a fire press only remembered, 1: the ammunition, 2: a menu move): the frame the press was read and the frame the cue was first drawn (the feel gate reads the difference: at most one frame) */
  int32_t cue_press[3], cue_draw[3];
  int ammo_pop, menu_pop;
  int32_t beat_ticks; float beat_min, beat_scale;
  /* debug fields */
  int32_t phase_dbg, level, state, shots, tleft, blocks, awake, broken, frags, booms, stars_dbg, cur, tick;
  int32_t score[MAXP];
  uint32_t hash;
  float yaw, pitch, power, orbit, step_ms, step_max, step_busy;
  double busy_sum;
  int busy_n;
  int32_t instances;
  float share;       /* the structure down (game_share), refreshed a few times a second for the HUD */
  int32_t result, judged_awake, judge_lag;
  bool replayed, replay_cut; /* this level's replay has been shown; the replay's first frame (the camera cuts) */
  int32_t replay_match_dbg, replay_ticks, last_result, last_awake, results;
  uint32_t replay_hash_dbg;
} G;

static cw_font font;

/* the loading screen's record: every frame from the ask to a playable level (and three after) logged, so a gate can see
 * the screen move on each one (tests/destructo-loading.mjs). Render side only. */
#define LOG_MAX 1024
static struct {
  bool active; int after;
  int32_t n, frame[LOG_MAX], shown[LOG_MAX];
  float progress[LOG_MAX], gap_ms[LOG_MAX], gap_max, last_gap;
  double last_render;
  int32_t frames_shown, phase, nwork, over33;
  float work_max, work_sum;
  float ring[256]; int32_t ring_i; /* the last 256 frames' CPU time, ms (ui.frame_work_ms): what an action cost the frame it was pressed on */
  float prog;                 /* the progress the screen shows now (stays at 1 after a build) */
  float rect[4], text_rect[4]; /* the spinner and the text, as fractions of the canvas */
  bool drawn;                 /* the overlay was drawn this frame */
} LG;
static int32_t seat_devices[MAXP]; /* debug: each seat's input devices (seat.devices) */

/* frame timing round a blast (the hitch gate reads these): the real monotonic clock, in carts too. Render only. */
static struct {
  double frame_start, last_end;
  bool in_frame;
  int window; /* ticks left in the window that opens at the first charge */
  bool opened;
  float frame_max, work_max, tick_max;
} P;
static double perf_now(void) { return (double)cw_profile_now_ns() * 1e-6; }
static void perf_tick(void) {
  if (!P.in_frame) P.frame_start = perf_now(), P.in_frame = true;
  if (!P.opened && G.g.booms > 0) P.opened = true, P.window = 180;
  if (P.window > 0 && G.g.tick_ms > P.tick_max) P.tick_max = G.g.tick_ms;
}
static double perf_frame_end(void) { /* returns the CPU this frame took (its ticks and its render), ms */
  double now = perf_now(), work = P.in_frame ? now - P.frame_start : 0;
  if (P.window > 0) {
    P.window--;
    if (P.last_end > 0 && now - P.last_end > P.frame_max) P.frame_max = (float)(now - P.last_end);
    if (P.in_frame && now - P.frame_start > P.work_max) P.work_max = (float)(now - P.frame_start);
  }
  P.last_end = now;
  P.in_frame = false;
  return work;
}

/* the render's counters (the flash check's blast frame, the perf gate's windows); dm restart clears them with P */
#define PERF_WINDOWS 24
static struct {
  uint32_t frame, boom_frame;
  double win_last, win_wall, win_work;
  float win_phys;
  int win_n;
  int32_t win_broken, win_awake, nwin;
  float win_frame_ms[PERF_WINDOWS], win_work_ms[PERF_WINDOWS], win_phys_ms[PERF_WINDOWS], win_gpu_ms[PERF_WINDOWS];
  int32_t win_draws[PERF_WINDOWS], win_tris[PERF_WINDOWS];
} R;
static uint32_t ticks_run;

/* the feel gate's trace (tests/destructo-feel.mjs): the last 256 play ticks' aim, state and the buttons read, and the
 * last 256 frames' camera, read back in one call after a scripted span (never a call a frame). Measurement only. */
#define FEEL_N 256
static struct {
  int32_t tick[FEEL_N], state[FEEL_N], ctick[FEEL_N];
  uint32_t flags[FEEL_N]; /* bit 0 fire read, 1 ammo read, 2 aim held, 3 orbit held, 4 view read */
  float yaw[FEEL_N], pitch[FEEL_N], orbit[FEEL_N], cx[FEEL_N], cy[FEEL_N], cz[FEEL_N];
  int32_t shots[FEEL_N];
  int32_t ammo[FEEL_N];
  uint32_t now;      /* the flags read this tick */
  int32_t fire_press_tick, launch_tick, aim_back_tick, ignored, last_state, last_open;
  int32_t cam_pos;
  /* frames over 33 ms (render end to render end): when, how long, the CPU the frame took and the tick's parts */
#define SLOW_N 48
  int32_t slow_n, slow_tick[SLOW_N], slow_state[SLOW_N];
  float slow_ms[SLOW_N], slow_work[SLOW_N], slow_tickms[SLOW_N], slow_world[SLOW_N], slow_post[SLOW_N], slow_pre[SLOW_N];
  float worst_ms;
  int32_t frames, over33;
} F;
static void feel_tick(const game *g) {
  int k = g->tick % FEEL_N;
  F.tick[k] = g->tick, F.state[k] = g->state, F.flags[k] = F.now, F.yaw[k] = g->yaw, F.pitch[k] = g->pitch, F.orbit[k] = g->orbit;
  F.shots[k] = g->shots, F.ammo[k] = g->ammo_sel;
  if (F.now & 1) F.fire_press_tick = g->tick;
  if (!game_aiming(g) && (F.now & 3)) F.ignored++; /* a fire or an ammo press read while the aim was shut */
  if (g->state == GS_FLIGHT && F.last_state == GS_AIM) F.launch_tick = g->tick;
  if (game_aiming(g) && !F.last_open && F.launch_tick) F.aim_back_tick = g->tick;
  F.last_open = game_aiming(g);
  F.last_state = g->state;
}

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

/* the effects fired once under the ground at load (fx_prime: their programs compile now, not at the first charge);
 * the hitch gate's control leaves this out, as it leaves out the fracture queue and the blast's shells */
static void prime_effects(void) {
#ifndef DESTRUCTO_NO_BUDGET
  fx_prime((t3_vec3){ 0, -25, 0 });
#endif
}

/* the last level load, ms (dm state: load.*): world, build, prestress and sleep are game_new's parts (on the background
 * job: the slot's build_ms is the whole job), wait is the main thread blocked on the job, take the move into the
 * live game, view the render's side, prime the effects, total the main thread's time from the ask to a playable level */
static struct { float world, build, prestress, sleep, settle, view, prime, replay, total, wait, take, job, frame0, first, ready; int32_t count, async, pending; } LD;
extern float g_ld_world_ms, g_ld_build_ms, g_ld_prestress_ms, g_ld_sleep_ms;
extern volatile int32_t g_ld_stage;
extern float g_view_ms[8];

/* the next level, and the replay's shadow, are built on a background job (loader.c) while the result card shows (or the
 * level is played): the load is then a move, and the render's side. The old path (dm set OLD_LOAD 1, the load gate's
 * control) builds both on this thread, settles them, and scans the field for coplanar faces as before. */
static game NXG;
static load_slot NX = { .g = &NXG };
static bool old_load(void) {
#ifdef DESTRUCTO_AUTOPILOT
  return opt_on("OLD_LOAD");
#else
  return false;
#endif
}
/* free fire: ammunition without end, of every kind (a structure's own ammunition is its few shots, a heavy ball or two) */
static void set_sandbox(void) {
  G.g.sandbox = G.free_fire;
  if (G.free_fire) for (int a = 0; a < NAMMO; a++) G.g.ammo[a] = CW_MAX(G.g.ammo[a], 99);
}
static int settle_for(int level) { return level_settle_ticks(level_get(level)); }

/* the level after this one: the next when it was brought down, this one again otherwise */
static int level_after(const game *g, bool won) {
  int next = won ? g->level + 1 : g->level;
#ifdef DESTRUCTO_AUTOPILOT
  if (won) next = next % NLEVELS;
  if (DESTRUCTO_BENCH || opt_get("LEVEL") || opt_get("LEVEL_NAME")) next = g->level;
#else
  if (won) next = CW_MIN(next, NLEVELS - 1);
#endif
  return next;
}
static void prebuild(int level) {
  if (!old_load()) loader_request(&NX, level, G.players, G.mode, settle_for(level));
}
/* NX's game becomes the live game (the old live world goes) */
static void take_next(void) {
  game_free(&G.g);
  memcpy(&G.g, &NXG, sizeof G.g);
  game_disown(&NXG); /* (the live game owns the world and the arrays now) */
  loader_release(&NX);
}

/* reuse: the live game is this level built and never stepped (a preview the player chose from) */
static void load_level_as(int level, bool settle) {
  double t0 = perf_now();
  double t1 = t0, t2;
  memset(&LD.world, 0, sizeof LD.world * 14); /* (ready is kept: the whole wait, set by the caller) */
  LD.pending = 2;
  LD.count++;
  bool reuse = !old_load() && G.have_game && G.g.level == level && G.g.tick == 0 && G.g.players == G.players && G.g.mode == G.mode && !G.g.proj_live && settle;
  if (old_load()) {
    loader_idle_wait();
    game_new(&G.g, level, G.players, G.mode);
    LD.world = g_ld_world_ms, LD.build = g_ld_build_ms, LD.prestress = g_ld_prestress_ms, LD.sleep = g_ld_sleep_ms;
    t1 = perf_now();
    if (settle) game_settle(&G.g, settle_for(level));
    LD.async = 0;
  } else if (!reuse) {
    int want = settle_for(level);
    loader_request(&NX, level, G.players, G.mode, want);
    loader_wait(&NX);
    if (!loader_built(&NX, level, G.players, G.mode)) { /* (a build for another level came out: this one is made now) */
      loader_request(&NX, level, G.players, G.mode, want);
      loader_wait(&NX);
    }
    t1 = perf_now();
    LD.wait = (float)(t1 - t0), LD.job = NX.ms;
    take_next();
    LD.take = (float)(perf_now() - t1);
    t1 = perf_now();
    LD.async = 1;
  }
  set_sandbox();
#ifdef DESTRUCTO_NO_BUDGET
  G.g.no_budget = true; /* the hitch gate's control: every break and the whole blast in one tick */
#endif
  t2 = perf_now();
  LD.settle = (float)(t2 - t1);
#ifdef DESTRUCTO_AUTOPILOT
  if (opt_get("ORBIT")) G.g.orbit = (float)atof(opt_get("ORBIT")), G.g.ring = G.g.L->range, G.g.muzzle = (t3_vec3){ G.g.ring * sinf(G.g.orbit), 1.7f, G.g.ring * cosf(G.g.orbit) }; /* development: looking from round the circle */
#endif
  G.have_game = true;
  bot_reset(&G.bot);
  if (G.built && !reuse && G.defer_view) G.fin = 1, G.vstep = 0;
  else if (G.built && !reuse) {
    fx_clear(), view_level(&G.g);
    double t3 = perf_now();
    prime_effects();
    LD.view = (float)(t3 - t2), LD.prime = (float)(perf_now() - t3);
  }
  G.last_cur = 0, G.turn_banner = 0;
  G.result_sound = false;
  G.share = 0;
  LD.total = (float)(perf_now() - t0);
}
static void load_level(int level) { load_level_as(level, true); }
/* behind the menu: the chosen level is built in the background and shown when it is ready (the first one is waited for) */
static void preview_level(int level) {
  if (!G.have_game || old_load()) { load_level_as(level, false); return; }
  prebuild(level);
}
/* once a frame in the menu: the level asked for is built, and becomes the one behind the choice */
static void preview_poll(void) {
  if (old_load() || G.g.level == G.sel || !loader_built(&NX, G.sel, G.players, G.mode)) return;
  take_next();
  set_sandbox();
  bot_reset(&G.bot);
  if (G.built) fx_clear(), view_level(&G.g), prime_effects();
  G.last_cur = 0, G.turn_banner = 0, G.result_sound = false, G.share = 0;
}

/* the level asked for: begun once it is built (loader.c), the frames go on meanwhile */
static double t_accept;
static void begin_level_now(int level);
/* the live game is this level, built and never stepped (a preview the player chose from): beginning it is a move */
static bool level_held(int level) {
  return G.have_game && G.g.level == level && G.g.tick == 0 && G.g.players == G.players && G.g.mode == G.mode && !G.g.proj_live;
}
static void log_begin(void) { LG.active = true, LG.after = 0, LG.n = 0, LG.gap_max = 0, LG.nwork = LG.over33 = 0, LG.work_max = LG.work_sum = 0, LG.prog = 0, LG.phase = LP_WAIT; }
static void request_level(int level) {
  if (G.loading) return;
  t_accept = perf_now();
  if (old_load() || opt_on("OLD_WAIT")) { /* the old path: the main thread waits for the build, the screen standing still */
    log_begin();
    begin_level_now(level);
    LD.ready = (float)(perf_now() - t_accept);
    return;
  }
  if (level_held(level)) { begin_level_now(level); LD.ready = (float)(perf_now() - t_accept); return; }
  log_begin();
  prebuild(level);
  G.loading = level + 1; /* the loading screen shows on the next frame; the ticks go on meanwhile (fixed_update) */
}

static void begin_level_now(int level) {
  G.sel = level;
  load_level(level);
  double tr = perf_now();
  bool nb =
#ifdef DESTRUCTO_NO_BUDGET
    true;
#else
    false;
#endif
  if (old_load()) replay_reset(level, G.players, G.mode, nb, settle_for(level));
  else replay_reset_async(level, G.players, G.mode, nb, settle_for(level));
  LD.replay = (float)(perf_now() - tr);
  G.replayed = false;
#ifdef DESTRUCTO_AUTOPILOT
  /* tests/destructo-result.mjs's controls (native: carts read no environment): the result rules before the fix, and
   * a replay whose last shot is nudged */
  if (opt_get("OLD_RULES")) G.g.old_rules = true;
  if (opt_get("OLD_SETTLE")) G.g.old_settle = true;
  if (opt_get("REPLAY_PERTURB")) g_replay_perturb = true;
#endif
  G.phase = PH_PLAY, G.phase_t = 0, G.overview = false;
  if (!G.fin) LG.prog = 1;
  if (!G.free_fire) save()->last_level = level, save()->last_mode = G.mode;
}

static int start_level(void) {
  int lv = DESTRUCTO_BENCH ? CW_MAX(0, level_find(DESTRUCTO_BENCH_NAME)) : 0;
  if (opt_get("LEVEL")) lv = CW_CLAMP(atoi(opt_get("LEVEL")), 0, NLEVELS - 1);
  if (opt_get("LEVEL_NAME") && level_find(opt_get("LEVEL_NAME")) >= 0) lv = level_find(opt_get("LEVEL_NAME")); /* tests: a level by its name */
  return lv;
}

static void start(int players, void *u) {
  (void)u;
  G.in_match = true;
  G.players = CW_CLAMP(players, 1, MAXP);
#ifdef DESTRUCTO_AUTOPILOT
  if (opt_get("PLAYERS")) G.players = CW_CLAMP(atoi(opt_get("PLAYERS")), 1, MAXP);
  G.mode = G.players > 1 ? MODE_VERSUS : MODE_COOP;
  if (opt_on("ASYNC_RESTART")) request_level(start_level());
  else begin_level_now(start_level());
#else
  G.phase = PH_SELECT, G.phase_t = 0;
  G.mode = CW_CLAMP(save()->last_mode, 0, 2);
  int last = save()->last_level;
  G.sel = last >= 0 && last < NLEVELS ? last : 0;
  preview_level(G.sel); /* the level behind the choice */
#endif
}

/* dm restart (opts.h): the level the options name, from its start, every counter and timing window cleared */
static void restart(void) {
  uint32_t frame = R.frame;
  memset(&P, 0, sizeof P), memset(&R, 0, sizeof R);
  R.frame = frame;
  G.results = 0, G.last_result = 0, G.last_awake = 0, G.replay_match_dbg = -1, G.replay_ticks = 0;
  G.step_max = 0, G.busy_sum = 0, G.busy_n = 0, G.step_busy = 0;
  if (!G.in_match) return;
  if (opt_get("PLAYERS")) G.players = CW_CLAMP(atoi(opt_get("PLAYERS")), 1, MAXP);
  G.mode = G.players > 1 ? MODE_VERSUS : MODE_COOP;
  if (opt_on("ASYNC_RESTART")) { G.loading = 0; request_level(start_level()); } /* (a gate: the loading screen, as a player gets it) */
  else begin_level_now(start_level());
}
static void option_changed(const char *name) {
  if (!strcmp(name, "COPLANAR_SWEEP")) cop_sweep_arm(); /* tests/coplanar-sweep.mjs: the camera glide, from now */
  if (!strcmp(name, "OLD_FEEL")) {
    cw_action as[] = { G.aim_l, G.aim_r, G.aim_u, G.aim_d, G.orb_l, G.orb_r };
    for (size_t k = 0; k < sizeof as / sizeof as[0]; k++) cw_action_set_deadzone(as[k], opt_on("OLD_FEEL") ? 0.5f : 0.12f);
  }
  if (!strcmp(name, "QUALITY") && opt_get("QUALITY")) cw_quality_set((cw_quality)CW_CLAMP(atoi(opt_get("QUALITY")), 0, CW_QUALITY_COUNT - 1), true);
}

static bool warmup(void *u) {
  (void)u;
  if (G.built) return true;
  static bool scene;
  if (!scene) {
    scene = true;
    cw_node root = cw_tree_root(cw_scene_app_tree());
    G.camera = cw_node_get(root, "Main/Camera");
    view_init(root);
    fx_init(root);
  }
  if (!G.have_game) { /* a structure behind the title: built on the job while the engine's loading screen shows its bar */
    G.players = 1;
    if (old_load() || opt_on("OLD_WAIT")) preview_level(start_level());
    else {
      static bool asked;
      if (!asked) asked = true, prebuild(start_level());
      if (cw_is_deterministic_run()) loader_wait(&NX); /* (a test: the whole warm-up in the first frame) */
      if (!loader_built(&NX, start_level(), G.players, G.mode)) {
        int ph;
        cw_loading_set((int)(loader_progress(&NX, &ph, NULL) * 1000), 1000, CW_LOAD_SCENE);
        return false;
      }
      load_level_as(start_level(), false);
    }
  }
  G.built = true;
  view_level(&G.g);
  prime_effects();
  return true;
}

/* the debug channel: the launcher moved round the structure (looking at a landmark from a chosen side) */
static void cmd_orbit(int argc, const char **argv, cw_reply *r) {
  if (argc > 1 && G.have_game) game_set_orbit(&G.g, (float)atof(argv[1]));
  cw_reply_printf(r, "{\"orbit\":%.4f}", G.have_game ? G.g.orbit : 0.0f);
}

/* the debug channel: free fire on or off for the level in play (the feel gate's ammunition without end) */
static void cmd_freefire(int argc, const char **argv, cw_reply *r) {
  if (argc > 1 && G.have_game) G.free_fire = atoi(argv[1]) != 0, set_sandbox();
  cw_reply_printf(r, "{\"free_fire\":%d}", G.free_fire);
}

/* behind the pause menu: the controls, once, in one card (the sticks, the d-pad and the face buttons: nothing else is read) */
static cw_color rgba(float r, float g, float b, float a);
static void draw_background(cw_shell_screen screen, cw_vec2 cs, void *u) {
  (void)u;
  if (screen != CW_SHELL_PAUSE) return;
  float k = cs.y / 1080.0f, x = cs.x * 0.5f, y = cs.y * 0.03f, s = 30 * k, dy = 44 * k;
  const cw_color c = rgba(1, 0.92f, 0.65f, 1);
  cw_prompt_text(font, "{aim_left|aim_right|aim_up|aim_down}: aim (left stick or d-pad)", -1, cw_v2(x, y), s, c, CW_ALIGN_CENTER);
  cw_prompt_text(font, "{orbit_left|orbit_right}: go round the structure (right stick)", -1, cw_v2(x, y + dy), s, c, CW_ALIGN_CENTER);
  cw_prompt_text(font, "{fire}: fire   {ammo_prev|ammo}: change ammunition   {view}: wide view", -1, cw_v2(x, y + 2 * dy), s, c, CW_ALIGN_CENTER);
}

static void init(void *u) {
  (void)u;
  cw_project_load_input();
  G.aim_l = cw_action_find("aim_left"), G.aim_r = cw_action_find("aim_right"), G.aim_u = cw_action_find("aim_up"), G.aim_d = cw_action_find("aim_down");
  G.fire = cw_action_find("fire"), G.ammo = cw_action_find("ammo"), G.ammo_p = cw_action_find("ammo_prev");
  G.view = cw_action_find("view");
  G.orb_l = cw_action_find("orbit_left"), G.orb_r = cw_action_find("orbit_right");
  cw_shell_init(&(cw_shell_desc){ .title = "DESTRUCTO", .subtitle = "bring them down, one to four players", .max_players = MAXP, .shared_screen = true,
                                  .game_save_size = sizeof(save_data), .game_save_version = 1, .start = start, .sound = shell_sound, .screen_changed = shell_screen, .draw_background = draw_background });
  font = cw_ui_font_sans();
  cw_prompt_set_font(font);
  cw_quality_full_resolution(true);
  if (opt_get("QUALITY")) cw_quality_set((cw_quality)CW_CLAMP(atoi(opt_get("QUALITY")), 0, CW_QUALITY_COUNT - 1), true);
  sound_init();
  G.players = 1;
  opt_init(restart, option_changed);
  cop_set_options(opt_get); /* its switches (COPLANAR_CONTROL, COPLANAR_SEPARATE) are run options too: dm set ... */
  cop_init(); /* the coplanar debug command: faces that fight for the depth test (games/common/coplanar.h) */
  cw_debug_command("freefire", "freefire <0|1>: free fire (ammunition without end, nothing judged) for the level in play", cmd_freefire);
  cw_debug_command("orbit", "orbit <radians>: put the launcher (and the camera) at this place on its circle round the structure", cmd_orbit);
  cw_debug_f32("load.world_ms", &LD.world), cw_debug_f32("load.build_ms", &LD.build), cw_debug_f32("load.prestress_ms", &LD.prestress), cw_debug_f32("load.sleep_ms", &LD.sleep);
  cw_debug_f32_array("load.view_parts_ms", g_view_ms, 8);
  cw_debug_f32("load.settle_ms", &LD.settle), cw_debug_f32("load.view_ms", &LD.view), cw_debug_f32("load.prime_ms", &LD.prime), cw_debug_f32("load.replay_ms", &LD.replay), cw_debug_f32("load.total_ms", &LD.total);
  cw_debug_f32("load.wait_ms", &LD.wait), cw_debug_f32("load.take_ms", &LD.take), cw_debug_f32("load.job_ms", &LD.job), cw_debug_f32("load.frame0_ms", &LD.frame0), cw_debug_f32("load.first_ms", &LD.first); cw_debug_f32("load.ready_ms", &LD.ready);
  cw_debug_i32("load.stage", (int32_t *)&g_ld_stage);
  cw_debug_i32("load.count", &LD.count), cw_debug_i32("load.async", &LD.async);
  cw_debug_field("feel.ring_tick", F.tick, CW_DBG_I32, FEEL_N), cw_debug_field("feel.ring_state", F.state, CW_DBG_I32, FEEL_N), cw_debug_field("feel.ring_flags", F.flags, CW_DBG_U32, FEEL_N);
  cw_debug_field("feel.ring_yaw", F.yaw, CW_DBG_F32, FEEL_N), cw_debug_field("feel.ring_pitch", F.pitch, CW_DBG_F32, FEEL_N), cw_debug_field("feel.ring_orbit", F.orbit, CW_DBG_F32, FEEL_N);
  cw_debug_field("feel.ring_shots", F.shots, CW_DBG_I32, FEEL_N), cw_debug_field("feel.ring_ammo", F.ammo, CW_DBG_I32, FEEL_N);
  cw_debug_field("feel.cam_tick", F.ctick, CW_DBG_I32, FEEL_N), cw_debug_field("feel.cam_x", F.cx, CW_DBG_F32, FEEL_N), cw_debug_field("feel.cam_y", F.cy, CW_DBG_F32, FEEL_N), cw_debug_field("feel.cam_z", F.cz, CW_DBG_F32, FEEL_N);
  cw_debug_i32("feel.cam_pos", &F.cam_pos), cw_debug_i32("feel.fire_press_tick", &F.fire_press_tick), cw_debug_i32("feel.launch_tick", &F.launch_tick), cw_debug_i32("feel.aim_back_tick", &F.aim_back_tick), cw_debug_i32("feel.ignored", &F.ignored);
  cw_debug_i32("feel.slow_n", &F.slow_n), cw_debug_i32("feel.over33", &F.over33), cw_debug_i32("feel.frames", &F.frames), cw_debug_f32("feel.worst_ms", &F.worst_ms);
  cw_debug_field("feel.slow_tick", F.slow_tick, CW_DBG_I32, SLOW_N), cw_debug_field("feel.slow_state", F.slow_state, CW_DBG_I32, SLOW_N), cw_debug_field("feel.slow_ms", F.slow_ms, CW_DBG_F32, SLOW_N), cw_debug_field("feel.slow_work", F.slow_work, CW_DBG_F32, SLOW_N);
  cw_debug_field("feel.slow_tickms", F.slow_tickms, CW_DBG_F32, SLOW_N), cw_debug_field("feel.slow_world", F.slow_world, CW_DBG_F32, SLOW_N), cw_debug_field("feel.slow_post", F.slow_post, CW_DBG_F32, SLOW_N), cw_debug_field("feel.slow_pre", F.slow_pre, CW_DBG_F32, SLOW_N);
  cw_debug_field("ui.cue_press", G.cue_press, CW_DBG_I32, 3), cw_debug_field("ui.cue_draw", G.cue_draw, CW_DBG_I32, 3);
  cw_debug_i32("beat.ticks", &G.beat_ticks), cw_debug_f32("beat.min", &G.beat_min), cw_debug_f32("beat.scale", &G.beat_scale); /* the winning blow's slow-motion beat: ticks it ran, the slowest clock, the clock now */
  cw_debug_i32("ui.loading", &G.loading); /* the level being built behind the loading screen (index + 1), 0 when none */
  cw_debug_f32("ui.loading_progress", &LG.prog), cw_debug_i32("ui.loading_phase", &LG.phase), cw_debug_i32("ui.loading_frames", &LG.frames_shown);
  cw_debug_field("ui.loading_rect", LG.rect, CW_DBG_F32, 4), cw_debug_field("ui.loading_text_rect", LG.text_rect, CW_DBG_F32, 4);
  cw_debug_i32("ui.log_n", &LG.n), cw_debug_f32("ui.gap_max_ms", &LG.gap_max);
  cw_debug_field("ui.frame_work_ms", LG.ring, CW_DBG_F32, 256), cw_debug_i32("ui.frame_i", &LG.ring_i);
  cw_debug_i32("ui.load_frames", &LG.nwork), cw_debug_i32("ui.load_over33", &LG.over33), cw_debug_f32("ui.load_work_max_ms", &LG.work_max); /* the transition: frames, frames whose CPU time passed 33 ms, the longest */
  cw_debug_field("ui.log_frame", LG.frame, CW_DBG_I32, LOG_MAX), cw_debug_field("ui.log_shown", LG.shown, CW_DBG_I32, LOG_MAX);
  cw_debug_field("ui.log_progress", LG.progress, CW_DBG_F32, LOG_MAX), cw_debug_field("ui.log_gap_ms", LG.gap_ms, CW_DBG_F32, LOG_MAX);
  cw_debug_i32("game.phase", &G.phase_dbg);
  cw_debug_i32("game.level", &G.level);
  cw_debug_i32("menu.sel", &G.sel);
  cw_debug_i32("game.ready", &G.ready_dbg); /* the aim is open under the settling rubble (coop and free fire) */
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
  cw_debug_i32("game.result", &G.result);             /* 0 playing, 1 brought down, 2 crystals down, 3 out of ammunition */
  cw_debug_i32("game.judged_awake", &G.judged_awake); /* bodies awake when the level was judged */
  cw_debug_f32("game.share", &G.share);
  cw_debug_i32("game.judge_lag", &G.judge_lag);  /* ticks from the last target down to the judgement (0 while playing) */
  cw_debug_i32("replay.match", &G.replay_match_dbg);  /* 1: the last replay ended on the live game's judged state; 0: elsewhere; -1: none yet */
  cw_debug_i32("replay.ticks", &G.replay_ticks);      /* ticks the last replay ran */
  cw_debug_i32("result.last", &G.last_result);        /* the last level judged: its result (game.result's values) ... */
  cw_debug_i32("result.awake", &G.last_awake);        /* ... the bodies awake when it was judged ... */
  cw_debug_i32("result.count", &G.results);           /* ... and how many levels have been judged */
  G.replay_match_dbg = -1;
  cw_debug_u32("replay.hash", &G.replay_hash_dbg);
  cw_debug_field("player.score", G.score, CW_DBG_I32, MAXP);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_f32("aim.yaw", &G.yaw);
  cw_debug_f32("aim.pitch", &G.pitch);
  cw_debug_f32("aim.power", &G.power); /* fixed: no control changes it */
  cw_debug_u32("ui.used", &G.used);          /* the controls used so far this session (bits: aim, fire, orbit, ammo, view) */
  cw_debug_i32("ui.view", &G.view_dbg);      /* the wide view is on */
  cw_debug_i32("aim.ammo", &G.ammo_dbg); /* the chosen ammunition (0 shot, 1 heavy ball, 2 charge) */
  cw_debug_f32("aim.orbit", &G.orbit); /* where the launcher stands on its circle round the structure (radians from the start) */
  cw_debug_f32("phys.step_ms", &G.step_ms);
  cw_debug_f32("phys.step_max", &G.step_max);   /* the slowest step since the app started (host-dependent) */
  cw_debug_f32("phys.step_busy", &G.step_busy); /* the mean step while more than 500 bodies were awake */
  cw_debug_i32("view.instances", &G.instances);
  cw_debug_f32("perf.blast_frame_max", &P.frame_max); /* the slowest frame (render end to render end) in the 3 s from the first charge */
  cw_debug_f32("perf.blast_work_max", &P.work_max);   /* the most CPU a frame took there (its ticks and its render) */
  cw_debug_f32("perf.blast_tick_max", &P.tick_max);   /* the slowest game tick there */
  cw_debug_field("seat.devices", seat_devices, CW_DBG_I32, MAXP); /* each seat's input devices (the seat gate) */
  { extern float g_phys_worst[10]; cw_debug_field("phys.worst", g_phys_worst, CW_DBG_F32, 10); } /* Box3D's split of the slowest step (sim.c) */
  cw_debug_u32("fx.boom_frame", &R.boom_frame);       /* the render frame (counted from the first) the first charge went off in; 0 before */
  cw_debug_u32("fx.frame", &R.frame);
  /* the perf gate: 120-frame windows while playing, from the first with a hundred blocks broken (the real clock) */
  cw_debug_i32("perf.nwin", &R.nwin);
  cw_debug_field("perf.win_frame_ms", R.win_frame_ms, CW_DBG_F32, PERF_WINDOWS); /* render end to render end */
  cw_debug_field("perf.win_work_ms", R.win_work_ms, CW_DBG_F32, PERF_WINDOWS);   /* the CPU a frame took (ticks and render) */
  cw_debug_field("perf.win_phys_ms", R.win_phys_ms, CW_DBG_F32, PERF_WINDOWS);   /* a physics step */
  cw_debug_field("perf.win_gpu_ms", R.win_gpu_ms, CW_DBG_F32, PERF_WINDOWS);     /* the pipeline's GPU time (-1: no timers, as in every cart) */
  cw_debug_field("perf.win_draws", R.win_draws, CW_DBG_I32, PERF_WINDOWS);
  cw_debug_field("perf.win_tris", R.win_tris, CW_DBG_I32, PERF_WINDOWS);
  cw_debug_i32("perf.broken_max", &R.win_broken);
  cw_debug_i32("perf.awake_max", &R.win_awake);
#ifdef DESTRUCTO_AUTOPILOT
  cw_shell_start(1);
#endif
}

/* ── input ───────────────────────────────────────────────────────────── */

/* whose buttons play. Co-op is one team: any seat's buttons aim and fire (a seat that joined and walked away never
 * holds the launcher up). Versus and party: the seat whose turn it is when it has a pad or keys of its own, else any
 * seat (one pad passed round); and a seat that has touched nothing for SEAT_AWAY ticks of its own turn is away, and
 * any seat plays that turn for it (the HUD says so) */
#define SEAT_AWAY (60 * 8)
static int seat_idle; /* ticks of this turn the turn's seat has pressed nothing */
static bool seat_away(void) { return seat_idle >= SEAT_AWAY; }
/* a seat with a pad or keys of its own (cw_input_player_devices counts keys, mouse and touch only, never the pad) */
static bool seat_has_input(int c) { return cw_input_player_pad(c) >= 0 || cw_input_player_devices(c) != 0; }
static int seat_of_turn(void) {
  int c = G.g.cur;
  /* tests/destructo-gates.json's seat gate control (dm set SEAT_CONTROL 1): the seat rule before the fix */
  if (opt_on("SEAT_CONTROL")) return G.players > 1 && seat_has_input(c) ? c : -1;
  if (G.mode == MODE_COOP || seat_away()) return -1;
  if (G.players > 1 && seat_has_input(c)) return c;
  return -1;
}
static void seat_watch(void) {
  for (int i = 0; i < MAXP; i++) seat_devices[i] = (int32_t)cw_input_player_devices(i);
  static int turn_of = -1, shots = -1;
  if (G.g.state != GS_AIM || G.g.cur != turn_of || G.g.shots != shots) {
    if (G.g.state == GS_AIM) turn_of = G.g.cur, shots = G.g.shots;
    seat_idle = 0;
    return;
  }
  bool any = false;
  cw_action acts[] = { G.aim_l, G.aim_r, G.aim_u, G.aim_d, G.fire, G.ammo, G.ammo_p, G.view, G.orb_l, G.orb_r };
  for (size_t k = 0; k < sizeof acts / sizeof acts[0] && !any; k++) any = cw_action_pressed(G.g.cur, acts[k]);
  seat_idle = any ? 0 : seat_idle + 1;
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
/* a menu direction: the press, then, held, a repeat after a third of a second that speeds up to every 3 ticks (a long list is
 * a hold away, not thirty presses) */
static int nav(const char *name, int slot) {
  static int t[4];
  if (!held(cw_action_find(name), -1)) { t[slot] = 0; return 0; }
  int k = t[slot]++;
  if (k == 0) return 1;
  if (k < 20) return 0;
  int gap = k < 50 ? 6 : 3;
  return (k - 20) % gap == 0 ? 1 : 0;
}

/* a stick's push to a speed: a smooth curve from nothing at the dead zone's edge, fine control near the middle and the full
 * speed only at the end of the throw (the dead zone itself is the action's, assets/project.cwproj) */
static float curve(float s) { return s <= 0 ? 0 : powf(fminf(1, s), 1.8f); }
static float curve_old(float s) { return s <= 0 ? 0 : fmaxf(0.12f, s * s); }
static bool old_feel(void) { return opt_on("OLD_FEEL"); } /* the feel gate's control: the pad as it was (tests/destructo-feel.mjs) */
/* the launcher's speed, eased: a held direction picks up speed over a third of a second (a tap is a hair of a step, a hold is
 * quick), and the speed follows what the stick asks with a short lag both ways (no snap on, no snap off; what a tap
 * asked is still all moved: the lag only spreads it over a few ticks). The sim sees radians a tick (input.aim_dx). */
#define AIM_YAW_MAX 0.0205f   /* rad a tick: 1.2 rad/s, the whole span of the launcher's turn in 1.3 s */
#define AIM_PITCH_MAX 0.0150f
#define ORBIT_MAX 0.0160f     /* a full turn round the structure in 6.5 s */
typedef struct drive { int held; float v; } drive;
static drive d_turn, d_raise, d_orbit;
static float ease_in(int held, float start) {
  float t = fminf(1, held / 22.0f);
  return start + (1 - start) * t * t * (3 - 2 * t);
}
/* dir: -1, 0 or 1; k: the push 0..1 past the dead zone; returns this tick's move (rad) */
static float drive_step(drive *d, int dir, float k, float vmax, float start) {
  d->held = dir ? d->held + 1 : 0;
  float target = dir ? dir * vmax * fminf(curve(k), ease_in(d->held, start)) : 0;
  d->v += (target - d->v) * 0.25f;
  if (fabsf(d->v) < 1e-6f) d->v = 0;
  return d->v;
}

/* the pointer: a mouse, or a finger (the platform gives a touch as the left button). A press that drags aims (the
 * launcher follows the drag); one that lifts without a drag fires, or picks the ammunition icon it landed on. */
static void pointer_input(input *in) {
  const cw_raw_input *raw = cw_input_raw();
  if (!raw || !raw->mouse_present) return;
  int w, h;
  cw_window_size(&w, &h);
  cw_vec2 cs = cw_r2d_canvas_size();
  float sx = w > 0 ? cs.x / (float)w : 1, sy = h > 0 ? cs.y / (float)h : 1;
  float x = raw->mouse_x * sx, y = raw->mouse_y * sy;
  bool down = (raw->mouse_buttons & 1) != 0;
  if (down && !G.ptr_down) G.ptr_down = true, G.ptr_drag = false, G.ptr_x0 = G.ptr_x = x, G.ptr_y0 = G.ptr_y = y;
  else if (down) {
    if (!G.ptr_drag && hypotf(x - G.ptr_x0, y - G.ptr_y0) > 14 * cs.y / 1080.0f) G.ptr_drag = true;
    if (G.ptr_drag) in->aim_dx += (x - G.ptr_x) * 0.0016f, in->aim_dy += (y - G.ptr_y) * 0.0016f;
    G.ptr_x = x, G.ptr_y = y;
  } else if (G.ptr_down) {
    G.ptr_down = false;
    if (!G.ptr_drag) {
      int pick = 0;
      for (int a = 0; a < NAMMO; a++) {
        cw_rect r = G.ammo_rect[a];
        if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) pick = a + 1;
      }
      if (pick) in->ammo_set = pick;
      else in->fire = true;
    }
  }
}

static void read_input(input *in) {
  memset(in, 0, sizeof *in);
#ifdef DESTRUCTO_AUTOPILOT
  bot_input(&G.g, &G.bot, in);
  G.overview = opt_get("OVERVIEW") != NULL; /* development: the wide view */
#else
  int s = seat_of_turn();
  /* the left stick or the d-pad aims, the right stick rolls the launcher round the structure; South fires at the
   * ammunition's fixed power, West and East step through the ammunition, North toggles the wide view. Nothing reads a
   * shoulder or a trigger. A small push of a stick is slow (a squared response), a full push is quick. */
  bool l = held(G.aim_l, s), r = held(G.aim_r, s), u_ = held(G.aim_u, s), d_ = held(G.aim_d, s), ol = held(G.orb_l, s), orr = held(G.orb_r, s);
  /* the sim's own held-key aiming is for the bot; a person's pad goes through the eased drive (aim_dx, aim_dy, orbit_dx) */
  if (old_feel()) {
    in->left = l, in->right = r, in->up = u_, in->down = d_;
    in->turn_k = curve_old(fmaxf(strength(G.aim_l, s), strength(G.aim_r, s)));
    in->orbit_left = ol, in->orbit_right = orr;
    in->orbit_k = curve_old(fmaxf(strength(G.orb_l, s), strength(G.orb_r, s)));
    in->raise_k = curve_old(fmaxf(strength(G.aim_u, s), strength(G.aim_d, s)));
  } else {
    int dt_ = (r ? 1 : 0) - (l ? 1 : 0), dr_ = (u_ ? 1 : 0) - (d_ ? 1 : 0), do_ = (orr ? 1 : 0) - (ol ? 1 : 0);
    float kt = dt_ > 0 ? strength(G.aim_r, s) : strength(G.aim_l, s), kr = dr_ > 0 ? strength(G.aim_u, s) : strength(G.aim_d, s), ko = do_ > 0 ? strength(G.orb_r, s) : strength(G.orb_l, s);
    in->aim_dx = drive_step(&d_turn, dt_, kt, AIM_YAW_MAX, 0.06f);
    in->aim_dy = -drive_step(&d_raise, dr_, kr, AIM_PITCH_MAX, 0.06f);
    in->orbit_dx = drive_step(&d_orbit, do_, ko, ORBIT_MAX, 0.2f);
  }
  in->fire = pressed(G.fire, s);
  in->ammo_next = pressed(G.ammo, s);
  in->ammo_prev = pressed(G.ammo_p, s);
  if (pressed(G.view, s)) G.overview = !G.overview;
  G.view_dbg = G.overview;
  pointer_input(in);
  if (l || r || u_ || d_ || fabsf(in->aim_dx) > 1e-4f || fabsf(in->aim_dy) > 1e-4f) G.used |= 1u << H_AIM;
  if (in->fire) G.used |= 1u << H_FIRE;
  if (ol || orr) G.used |= 1u << H_ORBIT;
  if (in->ammo_next || in->ammo_prev || in->ammo_set) G.used |= 1u << H_AMMO;
  if (pressed(G.view, s)) G.used |= 1u << H_VIEW;
  if (!old_feel()) {
    if (in->fire && !game_aiming(&G.g) && G.g.state == GS_FLIGHT) G.cue_press[0] = (int32_t)R.frame + 1, G.cue_draw[0] = 0; /* remembered, not fired: the cue says so */
    if (in->ammo_next || in->ammo_prev || in->ammo_set) G.ammo_pop = 8, G.cue_press[1] = (int32_t)R.frame + 1, G.cue_draw[1] = 0;
  }
  F.now = (in->fire ? 1u : 0) | (in->ammo_next || in->ammo_prev || in->ammo_set ? 2u : 0) | (l || r || u_ || d_ ? 4u : 0) | (ol || orr ? 8u : 0) | (pressed(G.view, s) ? 16u : 0);
#endif
}

static int unlocked(void) {
  int best = -1;
  for (int i = 0; i < NLEVELS; i++) if (save()->stars[i]) best = i;
  return CW_MIN(NLEVELS - 1, best + 3);
}

/* the clock's dip: in over 6 ticks, 12 at a third, out over 6 */
static float beat_scale(int t) {
  float k = fminf(fminf(1, t / 6.0f), fminf(1, (24 - t) / 6.0f));
  if (k < 0) k = 0;
  k = k * k * (3 - 2 * k);
  return 1 - 0.65f * k;
}

static void level_done(void) {
  int st = game_stars(&G.g);
  save_data *s = save();
  if (st > s->stars[G.g.level]) s->stars[G.g.level] = (uint8_t)st;
  int total = 0;
  for (int i = 0; i < G.g.players; i++) total += G.g.P[i].score;
  if (total > s->best[G.g.level]) s->best[G.g.level] = total;
#if !defined(DESTRUCTO_AUTOPILOT)
  cw_shell_save_commit();
#endif
}

static void fixed_update(void *u, double dt) {
  (void)u, (void)dt;
  ticks_run++;
  perf_tick();
  loader_pump();
  sound_tick(ticks_run);
  if (!cw_shell_update()) return;
  if (!G.in_match) return;
  cw_shell_join_press();
  if (G.loading) {
    if (loader_built(&NX, G.loading - 1, G.players, G.mode)) {
      int lv = G.loading - 1;
      G.defer_view = true;
      begin_level_now(lv);
      G.defer_view = false;
      if (G.fin) return; /* the render side follows, a step a frame, the loading screen up */
      G.loading = 0;
      LD.ready = (float)(perf_now() - t_accept);
    } else if (G.fin) {
      double t0 = perf_now();
      if (G.fin == 1 && G.vstep == 0) LD.view = 0;
      if (G.fin == 1) { /* the render side, a slice a frame */
        if (G.vstep == 0) fx_clear();
        G.vstep = view_level_step(&G.g, G.vstep);
        LD.view += (float)(perf_now() - t0);
        if (G.vstep < 0) G.fin = 2;
      }
      else prime_effects(), G.fin = 0, G.loading = 0, LG.prog = 1, LD.prime = (float)(perf_now() - t0), LD.ready = (float)(perf_now() - t_accept);
      return;
    } else {
      /* the level is being built on the job: nothing steps, input is ignored but Back, which gives it up where there is a
       * menu to go back to (the build goes on and is kept for the menu's preview) */
      if (ui("ui_back") && G.phase != PH_PLAY) {
        sound_menu(2);
        G.loading = 0, LG.active = false;
        if (G.phase == PH_RESULT) G.phase = PH_SELECT, G.phase_t = 0, preview_level(G.sel);
      }
      return;
    }
  }
  G.phase_t++;
  if (G.ammo_pop > 0) G.ammo_pop--;
  if (G.menu_pop > 0) G.menu_pop--;
  switch (G.phase) {
  case PH_SELECT: {
    preview_poll();
    int dx = old_feel() ? ui("ui_right") - ui("ui_left") : nav("ui_right", 0) - nav("ui_left", 1), dy = old_feel() ? ui("ui_down") - ui("ui_up") : nav("ui_down", 2) - nav("ui_up", 3);
    int top = G.free_fire ? NLEVELS - 1 : unlocked(), count = top + 1;
    int n = G.sel + dx + dy * SEL_COLS;
    if (dx || dy) { /* the choice wraps round the open structures (a hold runs along them: nav) */
      n = old_feel() ? (n >= 0 && n < count ? n : G.sel) : ((n % count) + count) % count;
      if (n != G.sel) { G.sel = n, sound_menu(0), preview_level(G.sel); if (!old_feel()) G.menu_pop = 7, G.cue_press[2] = (int32_t)R.frame + 1, G.cue_draw[2] = 0; }
    }
    if (pressed(G.ammo, -1) && G.players > 1) G.mode = (G.mode + 1) % 3, sound_menu(0);
    if (G.players < 2 && G.mode == MODE_PARTY) G.mode = MODE_COOP;
    if (pressed(G.view, -1)) {
      G.free_fire = !G.free_fire, sound_menu(0);
      if (!G.free_fire && G.sel > unlocked()) G.sel = unlocked();
      preview_level(G.sel);
    }
    if (ui("ui_accept") || pressed(G.fire, -1)) sound_menu(1), request_level(G.sel); /* (the loading screen when the preview's build is not done) */
    G.players = CW_MAX(1, cw_shell_players());
    break;
  }
  case PH_PLAY: {
#ifdef DESTRUCTO_AUTOPILOT
    if (opt_get("FREEZE")) break; /* development: the level as built, nothing stepped (looking at a builder's work) */
    if (opt_on("HOLD")) break;    /* tests/destructo-coplanar.mjs: the level settled as for play, then held (no shot fired) */
    if (opt_get("SCENE")) { /* the look sheets: a foot of the structure broken (dm set SCENE legout) or a charge at its middle (boom), or at the Hoover Dam's foot (dam); no shot; the world steps, the view draws it */
      input none = { 0 };
      G.overview = !opt_get("SCENE_CAM"); /* the wide view unless SCENE_CAM is set (the game's own collapse camera) */
      if (!strncmp(opt_get("SCENE"), "shot", 4)) { /* a real shot at the nearest foot: shot, heavy or charge (SCENE shotN, N the ammunition) */
        if (G.g.tick == 40) { int a = opt_get("SCENE")[4] - '0'; if (game_aim_shot(&G.g, a >= 0 && a < NAMMO ? a : 0, 0) >= 0) game_fire(&G.g); }
        G.g.sandbox = true;
        game_step(&G.g, &none);
        fx_events(&G.g, G.g.cur);
        sound_events(&G.g, cw_node3d_global_position(G.camera));
        break;
      }
      if (G.g.tick == 40 && !strcmp(opt_get("SCENE"), "cut")) opt_set("NO_SUPPORT", "1"), game_cutaway(&G.g);
      else if (G.g.tick == 40 && !strcmp(opt_get("SCENE"), "dam")) game_charge_at(&G.g, (t3_vec3){ 0.0f, 0.8f, water_dam_zc(&G.g.wt, 0.0f) + 0.6f }); /* the Hoover Dam: a charge at the dam's foot (the spill's look sheet) */
      else if (G.g.tick == 40) game_legout(&G.g, !strcmp(opt_get("SCENE"), "boom"));
      G.g.state = GS_SETTLE;
      game_step(&G.g, &none);
      fx_events(&G.g, G.g.cur);
      sound_events(&G.g, cw_node3d_global_position(G.camera));
      break;
    }
#endif
    input in;
    seat_watch();
    G.g.old_feel = old_feel();
    read_input(&in);
    replay_log(&G.g, &in);
    game_step(&G.g, &in);
    feel_tick(&G.g);
    replay_after_step(&G.g);
    /* the replay's shadow catches up while the live world is quiet (aiming, or a settled field) */
    replay_follow(&G.g, G.g.awake_now < 150 ? 3.0 : 0.0);
    fx_events(&G.g, G.g.cur);
    sound_events(&G.g, cw_node3d_global_position(G.camera));
    if (G.g.cur != G.last_cur) G.last_cur = G.g.cur, G.turn_banner = 100, sound_turn();
    if (G.turn_banner > 0) G.turn_banner--;
    if (game_over(&G.g)) {
      level_done();
      G.last_result = game_result(&G.g), G.last_awake = G.g.judged_awake, G.results++;
      G.phase = PH_RESULT, G.phase_t = 0;
      prebuild(level_after(&G.g, G.g.state == GS_WON)); /* the next level is built while the card shows */
      sound_result(G.g.state == GS_WON);
    }
    break;
  }
  case PH_RESULT: {
    if (replay_running()) { /* the last shot again: any button skips it */
      bool skip = false;
#ifndef DESTRUCTO_AUTOPILOT
      skip = ui("ui_accept") || ui("ui_back") || pressed(G.fire, -1) || pressed(G.ammo, -1);
#endif
      if (skip) replay_skip();
      else if (replay_tick()) {
        const game *r = replay_game();
        if (r->nev) fx_events(r, r->cur), sound_events(r, cw_node3d_global_position(G.camera));
        G.replay_ticks++;
      }
      if (!replay_running()) G.phase_t = 0, G.replay_match_dbg = replay_match(), G.replay_hash_dbg = replay_hash(); /* the card slides in again */
      break;
    }
    /* the world goes on settling behind the card; the replay's shadow gets to the last shot's start */
    input none = { 0 };
    /* the winning blow: the world runs at a third for a beat (0.4 s), eased in and out, input never held (the card's keys work throughout) */
    if (G.phase_t == 1) G.beat_ticks = 0, G.beat_min = 0;
    G.g.time_scale = G.g.state == GS_WON && !old_feel() && G.phase_t < 24 ? beat_scale(G.phase_t) : 0;
    if (G.g.time_scale > 0 && G.g.time_scale < 0.99f) { G.beat_ticks++; if (G.beat_min == 0 || G.g.time_scale < G.beat_min) G.beat_min = G.g.time_scale; }
    G.beat_scale = G.g.time_scale > 0 ? G.g.time_scale : 1;
    game_step(&G.g, &none);
    fx_events(&G.g, G.g.cur);
    sound_events(&G.g, cw_node3d_global_position(G.camera));
    replay_follow(&G.g, 4.0);
    bool won = G.g.state == GS_WON;
    bool watch = !G.replayed && replay_ready();
#ifdef DESTRUCTO_AUTOPILOT
    if (watch && G.phase_t > 60) watch = false, G.replayed = true, G.replay_cut = true, G.replay_ticks = 0, fx_clear(), replay_start();
    if (!G.loading && G.phase_t > 60 * 4 && (G.replayed || G.phase_t > 60 * 12)) {
      int next = won ? (G.g.level + 1) % NLEVELS : G.g.level;
      if (DESTRUCTO_BENCH || opt_get("LEVEL") || opt_get("LEVEL_NAME")) next = G.g.level;
      level_done();
      request_level(next);
    }
#else
    if (G.phase_t > 6 && !G.loading) {
      if (watch && pressed(G.ammo, -1)) {
        sound_menu(1);
        G.replayed = true, G.replay_cut = true, G.replay_ticks = 0, fx_clear(), replay_start();
      } else if (ui("ui_accept") || pressed(G.fire, -1)) {
        sound_menu(1);
        level_done();
        request_level(won ? CW_MIN(G.g.level + 1, NLEVELS - 1) : G.g.level);
      } else if (ui("ui_back")) {
        sound_menu(2);
        level_done();
        G.phase = PH_SELECT, G.phase_t = 0;
        preview_level(G.sel);
      }
    }
#endif
    break;
  }
  }
  const game *g = &G.g;
  G.phase_dbg = G.phase, G.level = g->level, G.state = g->state, G.tick = g->tick, G.shots = g->shots, G.tleft = g->tgt_left;
  G.blocks = g->nalive, G.awake = game_awake(g), G.broken = g->broken, G.frags = g->fragments, G.booms = g->booms, G.stars_dbg = game_stars(g);
  G.cur = g->cur, G.hash = g->hash, G.ready_dbg = g->ready;
  G.judge_lag = g->judge_tick && g->win_tick ? g->judge_tick - g->win_tick : 0;
  G.result = game_result(g), G.judged_awake = g->judged_awake;
  if (G.phase == PH_RESULT) G.share = g->judged_share;
  else if (G.phase == PH_PLAY && (g->tick % 10 == 0 || g->state == GS_AIM)) G.share = game_share(g);
  for (int i = 0; i < MAXP; i++) G.score[i] = g->P[i].score;
  G.ammo_dbg = g->ammo_sel, G.yaw = g->yaw, G.pitch = g->pitch, G.power = g->power, G.orbit = g->orbit, G.step_ms = g->step_ms;
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

/* a gem: a crystal's mark in the corner, its glow as a soft ring */
static void gem(float cx, float cy, float r, cw_color c) {
  cw_vec2 p[6] = { cw_v2(cx, cy - r), cw_v2(cx + r * 0.8f, cy - r * 0.2f), cw_v2(cx + r * 0.5f, cy + r * 0.8f), cw_v2(cx - r * 0.5f, cy + r * 0.8f), cw_v2(cx - r * 0.8f, cy - r * 0.2f), cw_v2(cx, cy - r) };
  cw_r2d_polygon(p, 5, c);
}

/* the control prompts, one at a time: the first control not yet used this session, for at most HINT_TICKS, and only
 * once the aim is open (never during a flight or over the result) */
static void hud_hint(cw_vec2 cs, float u, int seat) {
  static const char *TXT[HINTS] = {
    "{aim_left|aim_right|aim_up|aim_down}: aim",
    "{fire}: fire",
    "{orbit_left|orbit_right}: go round the structure",
    "{ammo_prev|ammo}: change ammunition",
    "{view}: wide view",
  };
  if (opt_on("NO_HINTS") || G.g.state != GS_AIM || G.phase != PH_PLAY) return;
  for (int k = 0; k < HINTS; k++) {
    if ((G.used >> k) & 1 || G.hint_t[k] >= HINT_TICKS) continue;
    if (k > H_FIRE && !((G.used >> H_FIRE) & 1) && G.g.shots == 0 && k != H_AMMO) continue; /* aim, then fire, before the rest */
    G.hint_t[k]++;
    float a = fminf(1, fminf(G.hint_t[k] / 30.0f, (HINT_TICKS - G.hint_t[k]) / 30.0f));
    cw_prompt_text(font, TXT[k], seat, cw_v2(cs.x * 0.5f, cs.y - 190 * u), 30 * u, rgba(1, 0.92f, 0.65f, a), CW_ALIGN_CENTER);
    return;
  }
}

/* the play HUD: the share of the structure down (against its mark) and the shots, the crystals left, and the ammunition
 * beside the launcher; the level's name and par for three seconds at the start. Nothing over the structure. */
static void hud_play(cw_vec2 cs, float u) {
  const game *g = &G.g;
  char b[96];
  bool plain = g->sandbox || g->mode == MODE_PARTY;
  /* the share bar: top middle, thin */
  float share = G.share, mark = game_mark(g);
  float bw = 520 * u, bx = cs.x * 0.5f - bw * 0.5f, by = 30 * u;
  cw_r2d_round_rect(cw_rect_make(bx, by, bw, 14 * u), 7 * u, 6, rgba(0.05f, 0.06f, 0.1f, 0.7f));
  cw_r2d_round_rect(cw_rect_make(bx, by, bw * fminf(1, share / mark), 14 * u), 7 * u, 6, share >= mark ? GOLD : rgba(0.95f, 0.62f, 0.2f, 0.95f));
  snprintf(b, sizeof b, "%d%%", (int)(share * 100 + 0.5f));
  text(b, bx + bw + 14 * u, by - 9 * u, 32 * u, share >= mark ? GOLD : WHITE, CW_ALIGN_LEFT);
  if (!plain) snprintf(b, sizeof b, "Shots %d / %d", g->shots, g->L->par);
  else snprintf(b, sizeof b, "Shots %d", g->shots);
  text(b, bx - 14 * u, by - 9 * u, 32 * u, !plain && g->shots > g->L->par ? rgba(1, 0.6f, 0.45f, 1) : WHITE, CW_ALIGN_RIGHT);
  /* the crystals: a gem and the count left, top left */
  gem(52 * u, 50 * u, 20 * u, PINK);
  snprintf(b, sizeof b, "%d", g->tgt_left);
  text(b, 84 * u, 30 * u, 40 * u, PINK, CW_ALIGN_LEFT);
  /* the level's name and par: three seconds, faded in and out */
  if (G.phase == PH_PLAY && G.phase_t < 180) {
    float a = fminf(1, fminf(G.phase_t / 20.0f, (180 - G.phase_t) / 30.0f));
    snprintf(b, sizeof b, "%d  %s", g->level + 1, g->L->name);
    text(b, cs.x * 0.5f, 70 * u, 40 * u, rgba(1, 0.82f, 0.3f, a), CW_ALIGN_CENTER);
    if (g->L->place) text(g->L->place, cs.x * 0.5f, 122 * u, 26 * u, rgba(0.9f, 0.88f, 0.82f, a), CW_ALIGN_CENTER);
    if (!plain) snprintf(b, sizeof b, "par %d", g->L->par);
    else snprintf(b, sizeof b, g->sandbox ? "free fire" : "steady hands");
    text(b, cs.x * 0.5f, (g->L->place ? 158 : 122) * u, 26 * u, rgba(1, 1, 1, a), CW_ALIGN_CENTER);
  }
  /* the players (versus: each one's score; co-op: the team's) */
  if (g->players > 1) {
    float rx = cs.x - 28 * u - 330 * u, ry = 24 * u;
    panel(rx, ry, 330 * u, (30 + 46 * g->players) * u, u, SEAT[g->cur]);
    for (int i = 0; i < g->players; i++) {
      bool now = i == g->cur;
      snprintf(b, sizeof b, "%sPlayer %d", now ? "> " : "", i + 1);
      text(b, rx + 18 * u, ry + (14 + 46 * i) * u, 28 * u, now ? SEAT[i] : rgba(0.8f, 0.8f, 0.85f, 1), CW_ALIGN_LEFT);
      if (g->mode == MODE_PARTY) snprintf(b, sizeof b, "%s", g->P[i].out ? "out" : g->P[i].strikes ? "1 strike" : "in");
      else snprintf(b, sizeof b, "%d", g->mode == MODE_VERSUS ? g->P[i].score : g->P[i].shots);
      text(b, rx + 312 * u, ry + (14 + 46 * i) * u, 28 * u, g->mode == MODE_PARTY && g->P[i].out ? rgba(0.6f, 0.6f, 0.65f, 0.8f) : WHITE, CW_ALIGN_RIGHT);
    }
  }
  if (G.turn_banner > 0 && g->players > 1) {
    snprintf(b, sizeof b, "Player %d", g->cur + 1);
    float a = fminf(1, G.turn_banner / 20.0f);
    cw_color c = SEAT[g->cur];
    c.a = a;
    text(b, cs.x * 0.5f, cs.y * 0.18f, 64 * u, c, CW_ALIGN_CENTER);
  }
  /* a press that is only remembered (South in the air): at once a ring and "FIRE QUEUED" over the cannon's end of the screen */
  if (g->fire_buf > 0 && !old_feel()) {
    float pulse = 0.5f + 0.5f * sinf((float)G.phase_t * 0.35f);
    cw_color qc = rgba(1, 0.85f, 0.35f, 0.75f + 0.25f * pulse);
    text("FIRE QUEUED", cs.x * 0.5f, cs.y - 150 * u, 34 * u, qc, CW_ALIGN_CENTER);
    cw_r2d_round_rect_lines(cw_rect_make(cs.x * 0.5f - 130 * u, cs.y - 160 * u, 260 * u, 58 * u), 14 * u, 3 * u, 6, qc);
    if (!G.cue_draw[0]) G.cue_draw[0] = (int32_t)R.frame + 1;
  }
  /* the ammunition: three icons low in the corner (a tap picks one), the chosen one lit with its count */
  if (game_aiming(g)) {
    float cw = 190 * u, ch = 56 * u, gap = 10 * u, x0 = 28 * u, y0 = cs.y - ch - 28 * u;
    for (int a = 0; a < NAMMO; a++) {
      float cx = x0 + a * (cw + gap);
      bool sel = a == g->ammo_sel;
      G.ammo_rect[a] = cw_rect_make(cx, y0, cw, ch);
      float pop = sel ? G.ammo_pop / 8.0f : 0; /* a pressed state: the chosen one swells a little and brightens, and settles */
      cw_rect rr = G.ammo_rect[a];
      rr.x -= pop * 7 * u, rr.y -= pop * 7 * u, rr.w += pop * 14 * u, rr.h += pop * 14 * u;
      cw_r2d_round_rect(rr, 10 * u, 6, sel ? rgba(0.95f + 0.05f * pop, 0.62f + 0.3f * pop, 0.2f + 0.5f * pop, 0.92f) : rgba(0.05f, 0.06f, 0.1f, 0.7f));
      if (sel && G.ammo_pop > 0 && !G.cue_draw[1]) G.cue_draw[1] = (int32_t)R.frame + 1;
      if (plain) snprintf(b, sizeof b, "%s", AMMO[a].name);
      else snprintf(b, sizeof b, "%s  %d", AMMO[a].name, g->ammo[a]);
      text(b, cx + cw * 0.5f, y0 + 10 * u, 30 * u, g->ammo[a] ? (sel ? rgba(0.08f, 0.05f, 0.02f, 1) : WHITE) : rgba(0.6f, 0.6f, 0.6f, 0.7f), CW_ALIGN_CENTER);
    }
    int seat = G.players > 1 && G.mode != MODE_COOP && !seat_away() ? g->cur : -1;
    if (G.players > 1 && G.mode != MODE_COOP && seat_away()) {
      char aw[64];
      snprintf(aw, sizeof aw, "Player %d is away: any pad can take this turn", g->cur + 1);
      text(aw, cs.x * 0.5f, cs.y * 0.3f, 30 * u, SEAT[g->cur], CW_ALIGN_CENTER);
    }
    hud_hint(cs, u, seat);
  }
}

/* the result: judged once the world is still (game_over), on a card that slides in at the right edge, clear of
 * the pile in the middle of the picture */
static void hud_result(cw_vec2 cs, float u) {
  const game *g = &G.g;
  int res = game_result(g);
  bool won = g->state == GS_WON;
  float w = 560 * u, h = (g->players > 1 && g->mode == MODE_VERSUS ? 420 : 380) * u;
  float k = fminf(1, G.phase_t / 24.0f);
  k = 1 - (1 - k) * (1 - k) * (1 - k); /* eases in */
  float x = cs.x - (w + 28 * u) * k, y = cs.y * 0.5f - h * 0.5f, cx = x + w * 0.5f;
  cw_color head = res == RES_BROUGHT_DOWN || res == RES_PARTY ? GOLD : res == RES_CLEARED ? rgba(0.75f, 0.9f, 1, 1) : rgba(1, 0.55f, 0.45f, 1);
  panel(x, y, w, h, u, head);
  if (res == RES_PARTY) { /* steady hands: who stayed in, and how the others went out */
    char b[96];
    int win = g->party_winner;
    if (win >= 0) snprintf(b, sizeof b, "PLAYER %d STAYS STEADY", win + 1);
    else snprintf(b, sizeof b, "EVERYONE IS OUT");
    text(b, cx, y + 22 * u, 40 * u, win >= 0 ? SEAT[win] : head, CW_ALIGN_CENTER);
    for (int k = 1, line = 0; k <= g->players; k++)
      for (int p = 0; p < g->players; p++) {
        if (g->P[p].out_order != k) continue;
        snprintf(b, sizeof b, "Player %d out: %s", p + 1, g->P[p].out == 1 ? "dropped a crystal" : "two turns with nothing down");
        text(b, cx, y + (100 + 40 * line++) * u, 26 * u, SEAT[p], CW_ALIGN_CENTER);
      }
    if (G.phase_t > 6) {
      if (!G.replayed && replay_ready())
        cw_prompt_text(font, "{ammo}: watch the last shot again", -1, cw_v2(cx, y + h - 84 * u), 24 * u, rgba(1, 0.92f, 0.65f, 1), CW_ALIGN_CENTER);
      cw_prompt_text(font, "{ui_accept}: next structure   {ui_back}: levels", -1, cw_v2(cx, y + h - 48 * u), 24 * u, WHITE, CW_ALIGN_CENTER);
    }
    return;
  }
  text(res == RES_BROUGHT_DOWN ? "BROUGHT DOWN" : res == RES_CLEARED ? "CRYSTALS DOWN" : "OUT OF AMMUNITION", cx, y + 22 * u, 46 * u, head, CW_ALIGN_CENTER);
  char b[96];
  float ty = y + 160 * u;
  if (won) {
    star_row(cx, y + 118 * u, 60 * u, game_stars(g), 3);
    snprintf(b, sizeof b, "%d %s, par %d", g->shots, g->shots == 1 ? "shot" : "shots", g->L->par);
    ty = y + 168 * u;
  } else snprintf(b, sizeof b, "%d of %d crystals still stand", g->tgt_left, g->ntgt);
  text(b, cx, ty, 28 * u, WHITE, CW_ALIGN_CENTER);
  snprintf(b, sizeof b, "%d%% of the structure down (mark %d%%)", (int)(g->judged_share * 100 + 0.5f), (int)(game_mark(g) * 100 + 0.5f));
  text(b, cx, ty + 40 * u, 26 * u, g->judged_share >= game_mark(g) ? GOLD : WHITE, CW_ALIGN_CENTER);
  if (won && g->shots <= g->L->par && g->judged_share < game_mark(g)) text("bring more of it down for the third star", cx, ty + 76 * u, 22 * u, rgba(0.85f, 0.85f, 0.9f, 0.9f), CW_ALIGN_CENTER);
  if (g->players > 1 && g->mode == MODE_VERSUS) {
    int best = 0;
    for (int i = 1; i < g->players; i++) if (g->P[i].score > g->P[best].score) best = i;
    snprintf(b, sizeof b, "Player %d has the most points: %d", best + 1, g->P[best].score);
    text(b, cx, ty + 112 * u, 26 * u, SEAT[best], CW_ALIGN_CENTER);
  }
  if (G.phase_t > 6) {
    if (!G.replayed && replay_ready())
      cw_prompt_text(font, "{ammo}: watch the last shot again", -1, cw_v2(cx, y + h - 84 * u), 24 * u, rgba(1, 0.92f, 0.65f, 1), CW_ALIGN_CENTER);
    cw_prompt_text(font, won ? "{ui_accept}: next level   {ui_back}: levels" : "{ui_accept}: try again   {ui_back}: levels", -1,
                   cw_v2(cx, y + h - 48 * u), 24 * u, WHITE, CW_ALIGN_CENTER);
  }
}

/* the replay: a quiet tag in the corner, nothing over the picture */
static void hud_replay(cw_vec2 cs, float u) {
  float x = 28 * u, y = 24 * u;
  panel(x, y, 300 * u, (replay_slow() ? 96 : 64) * u, u, rgba(1, 1, 1, 0.5f));
  text("REPLAY", x + 20 * u, y + 12 * u, 34 * u, WHITE, CW_ALIGN_LEFT);
  if (replay_slow()) text("slow motion", x + 20 * u, y + 56 * u, 24 * u, rgba(0.85f, 0.85f, 0.9f, 0.9f), CW_ALIGN_LEFT);
  cw_prompt_text(font, "{fire}: skip", -1, cw_v2(cs.x - 40 * u, cs.y - 60 * u), 24 * u, rgba(1, 1, 1, 0.8f), CW_ALIGN_RIGHT);
}

static void hud_select(cw_vec2 cs, float u) {
  float y0 = 40 * u;
  text("CHOOSE A STRUCTURE", cs.x * 0.5f, y0, 46 * u, GOLD, CW_ALIGN_CENTER);
  float cw = 400 * u, ch = 118 * u, gap = 16 * u;
  float gx = cs.x * 0.5f - (SEL_COLS * cw + (SEL_COLS - 1) * gap) * 0.5f, gy = y0 + 84 * u;
  int open = G.free_fire ? NLEVELS - 1 : unlocked(); /* free fire: every structure */
  /* four rows in view, scrolled to keep the choice in view */
  int rows = (NLEVELS + SEL_COLS - 1) / SEL_COLS, first = CW_CLAMP(G.sel / SEL_COLS - 1, 0, rows - SEL_ROWS);
  if (first > 0) cw_r2d_polygon((cw_vec2[]){ cw_v2(cs.x * 0.5f - 18 * u, gy - 10 * u), cw_v2(cs.x * 0.5f + 18 * u, gy - 10 * u), cw_v2(cs.x * 0.5f, gy - 26 * u) }, 3, rgba(1, 0.82f, 0.3f, 0.9f));
  if (first + SEL_ROWS < rows) { float by = gy + SEL_ROWS * (ch + gap) - 4 * u; cw_r2d_polygon((cw_vec2[]){ cw_v2(cs.x * 0.5f - 18 * u, by), cw_v2(cs.x * 0.5f, by + 16 * u), cw_v2(cs.x * 0.5f + 18 * u, by) }, 3, rgba(1, 0.82f, 0.3f, 0.9f)); }
  for (int i = first * SEL_COLS; i < NLEVELS && i < (first + SEL_ROWS) * SEL_COLS; i++) {
    float x = gx + (i % SEL_COLS) * (cw + gap), y = gy + (i / SEL_COLS - first) * (ch + gap);
    bool sel = i == G.sel, lock = i > open;
    float mp = sel ? G.menu_pop / 7.0f : 0; /* a pressed state: the chosen tile swells and lights, then settles */
    if (sel && G.menu_pop > 0 && !G.cue_draw[2]) G.cue_draw[2] = (int32_t)R.frame + 1;
    cw_r2d_round_rect(cw_rect_make(x - mp * 6 * u, y - mp * 6 * u, cw + mp * 12 * u, ch + mp * 12 * u), 12 * u, 6, sel ? rgba(0.95f + 0.05f * mp, 0.6f + 0.3f * mp, 0.2f + 0.5f * mp, 0.96f) : lock ? rgba(0.16f, 0.16f, 0.19f, 0.9f) : rgba(0.06f, 0.07f, 0.12f, 0.9f));
    char b[64];
    cw_color ink = sel ? rgba(0.08f, 0.05f, 0.02f, 1) : lock ? rgba(0.78f, 0.78f, 0.82f, 1) : WHITE;
    snprintf(b, sizeof b, "%d", i + 1);
    text(b, x + 16 * u, y + 8 * u, 40 * u, sel ? ink : lock ? ink : GOLD, CW_ALIGN_LEFT);
    text(LEVELS[i].name, x + 16 * u, y + 62 * u, 30 * u, ink, CW_ALIGN_LEFT);
    if (!lock) star_row(x + cw - 74 * u, y + 32 * u, 38 * u, save()->stars[i], 3);
    else { /* a padlock: a body and a shackle */
      cw_r2d_round_rect(cw_rect_make(x + cw - 62 * u, y + 28 * u, 34 * u, 26 * u), 5 * u, 6, ink);
      cw_r2d_round_rect_lines(cw_rect_make(x + cw - 58 * u, y + 10 * u, 26 * u, 28 * u), 11 * u, 4 * u, 6, ink);
    }
  }
  float y = gy + SEL_ROWS * (ch + gap) + 10 * u;
  char b[128];
  if (G.players > 1) {
    snprintf(b, sizeof b, "%d players, %s  ({ammo}: co-op, versus or party)", G.players,
             G.mode == MODE_PARTY ? "party: steady hands" : G.mode == MODE_VERSUS ? "versus" : "co-op");
    cw_prompt_text(font, b, -1, cw_v2(cs.x * 0.5f, y), 26 * u, WHITE, CW_ALIGN_CENTER);
    y += 40 * u;
  }
  cw_prompt_text(font, G.free_fire ? "{view}: free fire ON (every structure, ammunition without end)" : "{view}: free fire", -1, cw_v2(cs.x * 0.5f, y), 24 * u,
                 G.free_fire ? GOLD : rgba(0.85f, 0.85f, 0.9f, 0.9f), CW_ALIGN_CENTER);
  y += 38 * u;
  cw_prompt_text(font, "{ui_left|ui_right}: choose   {ui_accept}: play", -1, cw_v2(cs.x * 0.5f, y), 30 * u, rgba(1, 0.92f, 0.65f, 1), CW_ALIGN_CENTER);
}

/* the loading screen: the picture dimmed behind a ring that turns with the real clock (not the frame count), the level's
 * name, what the build is doing and a bar of how far it has got. Drawn on every frame of the wait, never blocking. */
static void hud_loading(cw_vec2 cs, float u) {
  int lv = G.loading - 1, phase = LP_WAIT, nb = 0;
  float p = loader_progress(&NX, &phase, &nb);
  if (G.fin) p = fmaxf(p, 0.99f), phase = LP_DONE;
  if (p < LG.prog) p = LG.prog;
  LG.prog = p, LG.phase = phase;
  cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), rgba(0.02f, 0.03f, 0.06f, 0.72f));
  float cx = cs.x * 0.5f, cy = cs.y * 0.4f, R = 70 * u, W = 14 * u;
  cw_vec2 c = cw_v2(cx, cy);
  cw_r2d_arc(c, R - W, R, 0, 6.2831853f, 64, rgba(1, 1, 1, 0.14f));
  double t = perf_now() * 1e-3;
  float head = (float)(fmod(t / 1.1, 1.0) * 6.2831853);
  for (int i = 0; i < 14; i++) { /* a comet: the head bright, the tail fading */
    float a1 = head - i * 0.12f, k = 1 - i / 14.0f;
    cw_r2d_arc(c, R - W, R, a1 - 0.13f, a1, 6, rgba(1, 0.82f, 0.3f, 0.95f * k * k));
  }
  char b[160];
  snprintf(b, sizeof b, "Building %s", level_get(lv)->name);
  cw_text_style st = tstyle(44 * u, WHITE);
  cw_text_metrics m = cw_font_measure(font, b, &st);
  text(b, cx, cy + R + 36 * u, 44 * u, WHITE, CW_ALIGN_CENTER);
  float bw = 560 * u, bh = 14 * u, bx = cx - bw * 0.5f, by = cy + R + 36 * u + m.height + 26 * u;
  cw_r2d_round_rect(cw_rect_make(bx, by, bw, bh), bh * 0.5f, 6, rgba(1, 1, 1, 0.16f));
  if (p > 0.005f) cw_r2d_round_rect(cw_rect_make(bx, by, fmaxf(bh, bw * p), bh), bh * 0.5f, 6, GOLD);
  if (nb > 0 && phase == LP_BLOCKS) snprintf(b, sizeof b, "%s: %d blocks", loader_phase_name(phase), nb);
  else snprintf(b, sizeof b, "%s", loader_phase_name(phase));
  text(b, cx, by + bh + 14 * u, 26 * u, rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER);
  if (G.phase != PH_PLAY) cw_prompt_text(font, "{ui_back}: back", -1, cw_v2(cx, by + bh + 60 * u), 22 * u, rgba(1, 1, 1, 0.55f), CW_ALIGN_CENTER);
  LG.rect[0] = (cx - R - 4 * u) / cs.x, LG.rect[1] = (cy - R - 4 * u) / cs.y, LG.rect[2] = (2 * R + 8 * u) / cs.x, LG.rect[3] = (2 * R + 8 * u) / cs.y;
  LG.text_rect[0] = (cx - m.width * 0.5f) / cs.x, LG.text_rect[1] = (cy + R + 36 * u) / cs.y, LG.text_rect[2] = m.width / cs.x, LG.text_rect[3] = m.height / cs.y;
  LG.drawn = true, LG.frames_shown++;
}

static void hud(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 1080.0f;
  cw_r2d_begin(NULL);
  { /* every glyph the HUD can show, baked in its first frame off the screen: a glyph first seen mid-play (the percent
     * of the structure down, from the charge on) took a millisecond of that frame to bake */
    static bool baked;
    if (!baked) baked = true, text("0123456789%:/.,-+! ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", -cs.x * 4, -cs.y * 4, 27 * u, WHITE, CW_ALIGN_LEFT);
  }
  if (G.loading) { /* the loading screen stands alone: the old level's HUD or card would read as the new one's */
    hud_loading(cs, u);
    cw_r2d_end();
    return;
  }
  if (G.phase == PH_SELECT) hud_select(cs, u);
  else if (G.phase == PH_RESULT && replay_running()) hud_replay(cs, u);
  else if (G.phase == PH_RESULT) hud_play(cs, u), hud_result(cs, u);
  else hud_play(cs, u);
  cw_r2d_end();
}

/* ── frame ───────────────────────────────────────────────────────────── */

static void render(void *u, double alpha) {
  (void)u;
  double t_render = perf_now();
  LG.drawn = false;
  if (LG.active && LG.n < LOG_MAX) { /* (the gap to the render before: a blocked main thread shows here) */
    LG.last_gap = LG.last_render > 0 ? (float)(t_render - LG.last_render) : 0;
    if (LG.last_gap > LG.gap_max) LG.gap_max = LG.last_gap;
  }
  LG.last_render = t_render;
  const float dt = 1.0f / 60.0f; /* the view animates by frames: the same pictures on every host */
  bool playing = cw_shell_screen_now() == CW_SHELL_PLAY;
  if (G.built && G.fin != 1) { /* (fin 2: the new level's instances are put in place under the loading screen, so its first frame is not the old one's) */
    int w, h;
    cw_window_size(&w, &h);
    float a = playing ? (float)alpha : 1.0f;
    if (replay_running()) {
      view_sync(replay_game(), replay_alpha());
      view_camera_replay(replay_game(), G.camera, (float)w / (float)h, dt * (replay_slow() ? 0.34f : 1.0f), replay_slow(), G.replay_cut, replay_alpha());
      G.replay_cut = false;
    } else {
      view_sync(&G.g, a);
      view_camera(&G.g, G.camera, (float)w / (float)h, dt, G.overview || G.phase == PH_SELECT, a);
      if (G.phase == PH_PLAY) {
        t3_vec3 e = cw_node3d_global_position(G.camera);
        int k = F.cam_pos++ % FEEL_N;
        F.ctick[k] = G.g.tick, F.cx[k] = e.x, F.cy[k] = e.y, F.cz[k] = e.z;
      }
    }
    fx_update(dt);
    G.instances = view_get_stats().instances;
  }
  bool show_hud = !opt_get("NO_HUD");
  if (show_hud && G.in_match && playing) hud();
  { /* the flash check (tests/destructo-flash.mjs) finds the blast by fx.boom_frame; its control (dm set STROBE_CONTROL 1)
     * lays a white wash over the screen at 10 Hz for 1.5 s from the blast: a strobe the check must catch */
    R.frame++;
    if (!R.boom_frame && G.g.booms > 0) {
      R.boom_frame = R.frame;
      if (opt_on("FX_LOG")) cw_info("destructo boom frame=%u", R.frame);
    }
    if (R.boom_frame && opt_on("STROBE_CONTROL") && R.frame - R.boom_frame < 90 && ((R.frame - R.boom_frame) / 3) & 1) {
      cw_vec2 cs = cw_r2d_canvas_size();
      cw_r2d_begin(NULL);
      cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), rgba(1, 1, 1, 0.6f));
      cw_r2d_end();
    }
  }
  cw_quality_auto_measure(playing);
  if (playing) { /* the perf gate reads these: 120-frame windows from the first with a hundred blocks broken (perf.win_*) */
    double now = perf_now();
    if (R.win_last > 0) R.win_wall += now - R.win_last;
    R.win_last = now;
    R.win_phys += G.g.step_ms;
    if (G.g.broken > R.win_broken) R.win_broken = G.g.broken;
    if (game_awake(&G.g) > R.win_awake) R.win_awake = game_awake(&G.g);
    if (++R.win_n == 120) {
      if (G.g.broken >= 100 && R.nwin < PERF_WINDOWS) {
        cw_render3d_stats rs;
        cw_render3d_get_stats(cw_scene_app_tree(), &rs);
        int k = R.nwin++;
        R.win_frame_ms[k] = (float)(R.win_wall / 120), R.win_work_ms[k] = (float)(R.win_work / 120), R.win_phys_ms[k] = R.win_phys / 120;
        R.win_gpu_ms[k] = cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree()));
        R.win_draws[k] = rs.draw_calls, R.win_tris[k] = rs.triangles;
      }
      R.win_n = 0, R.win_wall = 0, R.win_work = 0, R.win_phys = 0;
    }
  }
  cw_shell_draw();
  {
    double last_ = P.last_end;
    double w_ = perf_frame_end();
    if (playing && last_ > 0) {
      float gap = (float)(P.last_end - last_);
      F.frames++;
      if ((float)w_ > F.worst_ms) F.worst_ms = (float)w_; /* the CPU a frame took (the gap between frames also holds the debug host's own waits) */
      if (w_ > 16.0) F.over33++;
      if (w_ > 6.0) {
        if (F.slow_n < SLOW_N) {
          int k = F.slow_n++;
          F.slow_tick[k] = G.g.tick, F.slow_state[k] = G.phase * 10 + G.g.state, F.slow_ms[k] = gap, F.slow_work[k] = (float)w_;
          F.slow_tickms[k] = G.g.tick_ms, F.slow_world[k] = G.g.world_ms, F.slow_post[k] = G.g.post_ms, F.slow_pre[k] = G.g.pre_ms;
        }
      }
    }
    R.win_work += w_;
    LG.ring[LG.ring_i++ & 255] = (float)w_;
    if (LG.active) { /* the transition's record: this frame's CPU time (its ticks and its render), where it blocked would show */
      LG.nwork++, LG.work_sum += (float)w_;
      if ((float)w_ > LG.work_max) LG.work_max = (float)w_;
      if (w_ > 33) LG.over33++;
      if (LG.n < LOG_MAX) {
        int i = LG.n++;
        LG.frame[i] = (int32_t)R.frame, LG.shown[i] = LG.drawn, LG.progress[i] = LG.drawn ? LG.prog : 0, LG.gap_ms[i] = (float)w_;
      }
      if (!G.loading && ++LG.after > 3) LG.active = false;
    }
    if (LD.pending == 2) LD.frame0 = (float)w_, LD.pending = 1; /* the frame that made the level, then the next */
    else if (LD.pending == 1) LD.first = (float)w_, LD.pending = 0;
  }
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Destructo", .width = 1280, .height = 720, .debug = true, .init = init, .warmup = warmup, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/main.cwscene", &app);
}
