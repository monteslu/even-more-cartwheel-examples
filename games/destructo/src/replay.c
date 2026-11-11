/* destructo's instant replay: see replay.h */
#include <string.h>

#include "cartwheel/profile.h"
#include "loader.h"
#include "replay.h"

#define LOG_MAX 24576 /* ticks with something held: about 7 minutes of steady aiming; past it the level has no replay */
#define SLOW 3        /* slow motion: a step every third tick */
#define SLOW_TICKS 75 /* ... for this many steps from the shot's first hit */

typedef struct logged { int tick; input in; } logged;

static struct {
  game r;                 /* the shadow */
  logged log[LOG_MAX];
  int nlog, at;           /* entries logged; the next one the shadow takes */
  bool overflow, have;
  int fire_tick;          /* the tick the last shot was fired on (0: none yet) */
  int judged_tick;        /* the tick the live game was judged on (0: not yet) */
  uint32_t judged_hash;
  int last_state;
  bool running;
  int sub;                /* slow motion: ticks since the shadow's last step */
  int match;
  uint32_t hash;
  int start_gap;          /* how far the shadow had to go when the level was judged (progress) */
  int settle_left;        /* the shadow's settle before play, stepped in replay_follow's budget (not all at load) */
  bool pending, no_budget; /* the shadow is being built on the background job (loader.c) */
  int want_level, want_players, want_mode;
} R;

bool g_replay_perturb;
static load_slot SH;

/* the shadow built on the job is the shadow (block: wait for it) */
static bool shadow_ok(bool block) {
  if (!R.pending) return true;
  SH.g = &R.r;
  if (block) loader_wait(&SH);
  else loader_pump();
  if (!loader_built(&SH, R.want_level, R.want_players, R.want_mode)) return false;
  R.pending = false;
  R.r.no_budget = R.no_budget;
  loader_release(&SH);
  return true;
}

static double now_ms(void) { return (double)cw_profile_now_ns() * 1e-6; }

void replay_reset(int level, int players, int mode, bool no_budget, int settle_ticks) {
  R.pending = false;
  game_new(&R.r, level, players, mode);
  R.r.no_budget = no_budget;
  R.settle_left = settle_ticks;
  R.nlog = R.at = 0, R.overflow = false, R.have = true;
  R.fire_tick = R.judged_tick = 0, R.judged_hash = 0;
  R.last_state = GS_AIM, R.running = false, R.sub = 0, R.match = -1, R.hash = 0, R.start_gap = 0;
}

/* the same, the shadow made on a background job (settled there too): the live game goes on at once */
void replay_reset_async(int level, int players, int mode, bool no_budget, int settle_ticks) {
  SH.g = &R.r;
  R.pending = true, R.no_budget = no_budget, R.settle_left = 0;
  R.want_level = level, R.want_players = players, R.want_mode = mode;
  R.nlog = R.at = 0, R.overflow = false, R.have = true;
  R.fire_tick = R.judged_tick = 0, R.judged_hash = 0;
  R.last_state = GS_AIM, R.running = false, R.sub = 0, R.match = -1, R.hash = 0, R.start_gap = 0;
  loader_request(&SH, level, players, mode, settle_ticks);
}

void replay_log(const game *live, const input *in) {
  static const input zero;
  if (!memcmp(in, &zero, sizeof zero)) return;
  if (R.nlog >= LOG_MAX) { R.overflow = true; return; }
  R.log[R.nlog++] = (logged){ live->tick + 1, *in };
}

void replay_after_step(const game *live) {
  if (live->state == GS_FLIGHT && R.last_state == GS_AIM) R.fire_tick = live->tick;
  if (game_over(live) && !R.judged_tick) {
    shadow_ok(true);
    R.judged_tick = live->tick, R.judged_hash = game_state_hash(live);
    R.start_gap = R.fire_tick - 1 - R.r.tick + R.settle_left;
  }
  R.last_state = live->state;
}

/* one step of the shadow with the input logged for its next tick */
static void shadow_step(void) {
  if (R.settle_left > 0) { /* game_settle, a tick at a time */
    cw_world3d_step(R.r.w, TICK);
    if (--R.settle_left == 0) game_settle_finish(&R.r);
    return;
  }
  input in;
  memset(&in, 0, sizeof in);
  int t = R.r.tick + 1;
  while (R.at < R.nlog && R.log[R.at].tick < t) R.at++;
  if (R.at < R.nlog && R.log[R.at].tick == t) in = R.log[R.at++].in;
  if (g_replay_perturb && t == R.fire_tick && R.judged_tick) R.r.yaw += 0.003f;
  game_step(&R.r, &in);
}

/* where the shadow may go now: up to the live game while the players aim; the tick before the last shot otherwise */
static int target(const game *live) {
  if (live->state == GS_AIM && !R.judged_tick) return live->tick;
  return R.fire_tick > 0 ? R.fire_tick - 1 : live->tick;
}

void replay_follow(const game *live, double budget_ms) {
  if (!R.have || R.running || live->sandbox) return;
  if (!shadow_ok(false)) return;
  double t0 = now_ms();
  int to = target(live);
  while ((R.settle_left > 0 || R.r.tick < to) && now_ms() - t0 < budget_ms) shadow_step();
}

bool replay_ready(void) { return R.have && !R.pending && !R.overflow && R.judged_tick > 0 && R.fire_tick > 0 && R.settle_left == 0 && R.r.tick == R.fire_tick - 1; }

void replay_skip(void) {
  if (!R.running) return;
  R.running = false, R.have = false;
}

float replay_progress(void) {
  if (R.pending) return 0;
  if (replay_ready()) return 1;
  if (!R.judged_tick || R.start_gap <= 0) return 0;
  return 1 - (float)(R.fire_tick - 1 - R.r.tick + R.settle_left) / (float)R.start_gap;
}

void replay_start(void) {
  if (!replay_ready()) return;
  R.running = true, R.sub = 0;
}

bool replay_running(void) { return R.running; }

bool replay_slow(void) { return R.running && R.r.proj_hit_tick >= 0 && R.r.tick - R.r.proj_hit_tick < SLOW_TICKS; }

bool replay_tick(void) {
  if (!R.running) return false;
  int steps = 1;
  if (replay_slow()) {
    if (++R.sub < SLOW) return true;
    R.sub = 0;
  } else if (R.r.proj_hit_tick >= 0 && R.r.tick - R.r.proj_hit_tick > 240) steps = 2; /* the long settle at double speed */
  for (int s = 0; s < steps && R.r.tick < R.judged_tick; s++) shadow_step();
  if (R.r.tick >= R.judged_tick) {
    R.hash = game_state_hash(&R.r);
    R.match = R.hash == R.judged_hash;
    R.running = false;
    R.have = false; /* the shadow has gone past the shot's start: one replay a level */
    return false;
  }
  return true;
}

const game *replay_game(void) { return &R.r; }
float replay_alpha(void) { return replay_slow() ? (float)R.sub / SLOW : 1.0f; }
int replay_match(void) { return R.match; }
uint32_t replay_hash(void) { return R.hash; }
