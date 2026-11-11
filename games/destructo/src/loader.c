/* Level builds on a background job (loader.h). Main thread only: the slots' states are read and written here, the
 * job runs game_new and game_settle on the slot's game and touches nothing else. */
#include "loader.h"

#include <stdatomic.h>

#include "cartwheel/jobs.h"
#include "cartwheel/profile.h"

void cw__mem_sync(void); /* the engine's: this thread sees memory another thread grew (src/core/mem.c) */
#if CW_CART
extern int cw__jobs_stall_ms; /* the engine's watchdog on a job that finishes no chunk for this long (30 s) */
#endif

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
  game_new(s->g, s->level, s->players, s->mode);
  if (s->ticks > 0) game_settle(s->g, s->ticks);
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
