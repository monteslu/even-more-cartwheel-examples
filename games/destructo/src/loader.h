/* destructo's level builds off the main thread (loader.c): the next level and the replay's shadow are made (game_new, then
 * the settle before play) on a background job of the engine's pool while the player is still on the result card or
 * playing, so the next level starts at once. One build runs at a time (the builders keep scratch tables), and the main
 * thread never runs its own game_new while one is under way (loader_idle_wait). */
#ifndef DESTRUCTO_LOADER_H
#define DESTRUCTO_LOADER_H
#include <stdatomic.h>
#include <stdbool.h>
#include "game.h"

typedef struct load_slot {
  game *g;                           /* where the level is built (the slot never owns the storage) */
  int level, players, mode, ticks;   /* what it holds, or is being built as */
  int state;                         /* 0 empty, 1 queued, 2 building, 3 built (main thread only) */
  int want_level, want_players, want_mode, want_ticks, want; /* asked for while a build ran */
  float ms;                          /* the build's wall time, ms (profiling only) */
  /* progress (loader_progress): the job counts the settle ticks it has done, the main thread reads them */
  atomic_int settle_done, settle_total;
  double started_ms, stage_ms;       /* main thread: when the job began, and when the stage it is in was first seen */
  int seen_stage;
  float prog_max;                    /* monotonic: the largest progress shown for this build */
} load_slot;

/* ask for a level to be built in the slot (a build already held or under way for it stays; another build under way
 * finishes first, then this one starts) */
void loader_request(load_slot *s, int level, int players, int mode, int settle_ticks);
/* the level is built (loader_pump first) */
bool loader_built(load_slot *s, int level, int players, int mode);
/* a level asked for is held or under way (a build for another level does not count) */
bool loader_asked(const load_slot *s, int level, int players, int mode);
/* wait for the slot's build (the main thread helps the pool meanwhile); true when it holds the level asked for */
bool loader_wait(load_slot *s);
/* once a frame: starts the next queued build when none runs, collects a finished one */
void loader_pump(void);
/* the slot's game is taken (the caller moves it away): the slot is empty */
void loader_release(load_slot *s);
/* wait for any build under way (before the main thread's own game_new) */
void loader_idle_wait(void);
/* the old path (tests): builds are made on the calling thread, at once, in loader_request */
void loader_set_sync(bool on);

/* what the build is doing, for the loading screen */
enum { LP_NONE, LP_WAIT, LP_BLOCKS, LP_STRUCT, LP_SETTLE, LP_DONE };
const char *loader_phase_name(int phase);
/* how far the slot's build has got, 0..1 (never goes back), and its phase. Real counts where the job has them: the
 * blocks placed (against what the level made last time), the settle ticks stepped; the structure's own pre-stress and
 * sleep (landmarks.c) have no counter, so that part is measured against how long the level took last time. */
float loader_progress(load_slot *s, int *phase, int *blocks);
#endif
