/* Level builds on a background job (loader.h). Main thread only: the slots' states are read and written here, the
 * job runs game_new and game_settle on the slot's game and touches nothing else. */
#include "loader.h"

#include <math.h>
#include <stdatomic.h>

#include "cartwheel/jobs.h"
#include "cartwheel/profile.h"

void cw__mem_sync(void); /* the engine's: this thread sees memory another thread grew (src/core/mem.c) */
#if CW_CART
extern int cw__jobs_stall_ms; /* the engine's watchdog on a job that finishes no chunk for this long (30 s) */
#endif

extern volatile int32_t g_ld_stage; /* sim.c: how far game_new got (1 world, 2 blocks, 3 pre-stress, 4 sleep, 5 done) */
extern float g_ld_prestress_ms, g_ld_sleep_ms;
static double now_ms(void) { return (double)cw_profile_now_ns() * 1e-6; }

/* what each level took last time (this session): the blocks it made and the ms of its pre-stress and sleep, so the part
 * of a build with no counter is measured against something real the second time */
static int EXP_NB[NLEVELS];
static float EXP_STRUCT_MS[NLEVELS];

static cw_job *JOB;

/* a level build is one long chunk (seconds, a minute on a loaded machine), not a stuck one: the wait gives it ten minutes */
static void wait_job(void) {
#if CW_CART
  int old = cw__jobs_stall_ms;
  cw__jobs_stall_ms = 600000;
  cw_job_wait(JOB);
  cw__jobs_stall_ms = old;
#else
  cw_job_wait(JOB);
#endif
}

enum { S_EMPTY, S_QUEUED, S_BUILDING, S_BUILT };
#define NSLOT 4
static load_slot *SL[NSLOT]; /* every slot that has asked, earliest first */
static int nsl;

static load_slot *RUNNING;
static bool SYNC;

static void work(void *u) {
  load_slot *s = u;
  cw__mem_sync(); /* this thread sees the memory the main thread grew since it last ran */
  double t0 = (double)cw_profile_now_ns() * 1e-6;
  atomic_store(&s->settle_done, 0), atomic_store(&s->settle_total, 0);
  game_new(s->g, s->level, s->players, s->mode);
  if (s->ticks > 0) { /* game_settle, a tick at a time so the loading screen can count them */
    atomic_store(&s->settle_total, s->ticks);
    int st = s->g->state;
    s->g->state = GS_SETTLE;
    for (int t = 0; t < s->ticks; t++) cw_world3d_step(s->g->w, TICK), atomic_store(&s->settle_done, t + 1);
    s->g->state = st;
    game_settle_finish(s->g);
  }
  s->ms = (float)((double)cw_profile_now_ns() * 1e-6 - t0);
}

static void reg(load_slot *s) {
  for (int i = 0; i < nsl; i++) if (SL[i] == s) return;
  if (nsl < NSLOT) SL[nsl++] = s;
}

static void collect(void) {
  wait_job();
  cw__mem_sync();
  JOB = NULL;
  load_slot *s = RUNNING;
  RUNNING = NULL;
  s->state = S_BUILT;
  if (s->level >= 0 && s->level < NLEVELS) EXP_NB[s->level] = s->g->nb, EXP_STRUCT_MS[s->level] = g_ld_prestress_ms + g_ld_sleep_ms;
  if (s->want && (s->want_level != s->level || s->want_players != s->players || s->want_mode != s->mode)) {
    game_free(s->g);
    s->level = s->want_level, s->players = s->want_players, s->mode = s->want_mode, s->ticks = s->want_ticks, s->state = S_QUEUED;
  }
  s->want = 0;
}

void loader_pump(void) {
  if (JOB) {
    if (!cw_job_done(JOB)) return;
    collect();
  }
  for (int i = 0; i < nsl; i++) {
    load_slot *s = SL[i];
    if (s->state != S_QUEUED) continue;
    s->state = S_BUILDING, RUNNING = s;
    s->started_ms = now_ms(), s->seen_stage = 0, s->stage_ms = s->started_ms, s->prog_max = 0;
    atomic_store(&s->settle_done, 0), atomic_store(&s->settle_total, 0);
    JOB = cw_job_background(work, s);
    return;
  }
}

void loader_request(load_slot *s, int level, int players, int mode, int settle_ticks) {
  reg(s);
  if (s->state == S_BUILDING) { /* finishes what it is making, then this */
    s->want = 1, s->want_level = level, s->want_players = players, s->want_mode = mode, s->want_ticks = settle_ticks;
    if (s->level == level && s->players == players && s->mode == mode) s->want = 0;
    return;
  }
  if (s->state == S_BUILT || s->state == S_QUEUED) {
    if (s->level == level && s->players == players && s->mode == mode) return;
    if (s->state == S_BUILT) game_free(s->g);
  }
  s->level = level, s->players = players, s->mode = mode, s->ticks = settle_ticks, s->want = 0;
  s->state = S_QUEUED;
  if (SYNC) {
    loader_idle_wait();
    s->state = S_BUILDING;
    work(s);
    s->state = S_BUILT;
    return;
  }
  loader_pump();
}

bool loader_asked(const load_slot *s, int level, int players, int mode) {
  if (s->state == S_EMPTY) return false;
  if (s->want) return s->want_level == level && s->want_players == players && s->want_mode == mode;
  return s->level == level && s->players == players && s->mode == mode;
}

bool loader_built(load_slot *s, int level, int players, int mode) {
  loader_pump();
  return s->state == S_BUILT && s->level == level && s->players == players && s->mode == mode;
}

bool loader_wait(load_slot *s) {
  while (s->state != S_BUILT && s->state != S_EMPTY) {
    if (JOB) {
      wait_job();
      collect();
    } else loader_pump();
  }
  return s->state == S_BUILT;
}

void loader_release(load_slot *s) { s->state = S_EMPTY, s->want = 0; }

void loader_idle_wait(void) {
  while (JOB || RUNNING) {
    wait_job();
    collect();
  }
  /* (queued builds start again at the next pump) */
}

void loader_set_sync(bool on) { SYNC = on; }

const char *loader_phase_name(int phase) {
  switch (phase) {
  case LP_WAIT: return "Getting ready";
  case LP_BLOCKS: return "Placing blocks";
  case LP_STRUCT: return "Building the structure";
  case LP_SETTLE: return "Settling";
  case LP_DONE: return "Almost there";
  default: return "";
  }
}

float loader_progress(load_slot *s, int *phase, int *blocks) {
  int ph = LP_WAIT, nb = 0;
  float p = 0;
  if (s->state == S_BUILT) ph = LP_DONE, p = 1;
  else if (s->state == S_BUILDING) {
    const level_def *L = level_get(s->level);
    float b0 = 0.02f, b1 = 0.22f, s1 = 0.72f; /* blocks, structure and settle shares of the bar */
    int stage = g_ld_stage, td = atomic_load(&s->settle_done), tt = atomic_load(&s->settle_total);
    double now = now_ms();
    if (stage != s->seen_stage) s->seen_stage = stage, s->stage_ms = now;
    nb = s->g->nb;
    if (stage <= 1) ph = LP_BLOCKS, p = b0 * 0.5f;
    else if (stage == 2) {
      float est = EXP_NB[s->level] > 0 ? (float)EXP_NB[s->level] : 4000.0f;
      ph = LP_BLOCKS, p = b0 + (b1 - b0) * fminf(0.97f, nb / est);
    } else if (stage == 3 || stage == 4) {
      float est = EXP_STRUCT_MS[s->level] > 0 ? EXP_STRUCT_MS[s->level] : 1500.0f;
      float f = 1 - expf(-(float)((now - s->stage_ms)) / (0.6f * est + 50.0f));
      ph = LP_STRUCT, p = b1 + (s1 - b1) * fminf(0.97f, f);
    } else if (tt > 0) ph = LP_SETTLE, p = s1 + (0.99f - s1) * ((float)td / (float)tt);
    else ph = LP_SETTLE, p = 0.99f;
    if (nb == 0 && stage >= 5) nb = EXP_NB[s->level];
  }
  if (p < s->prog_max) p = s->prog_max;
  s->prog_max = p;
  if (phase) *phase = ph;
  if (blocks) *blocks = nb;
  return p;
}
