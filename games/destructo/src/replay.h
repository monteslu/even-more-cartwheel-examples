/* destructo's instant replay of a level's last shot (simulation only: no view, no clock but the budget's).
 *
 * The simulation is deterministic, so a replay needs no saved world: a second game (the shadow) is made the way the
 * level was (game_new, then the same settle) and stepped with the inputs the live game took, logged tick by tick
 * (only the ticks something was held). While the players aim the shadow catches up with the live game, a few
 * milliseconds a frame (re-running the shot before, while the live world is still); while a shot flies it waits on
 * the tick before that shot was fired. When the level is judged the shadow therefore stands where the last shot
 * started, and the replay steps it from there to the tick the live game was judged on: its hash there must be the
 * live game's (replay_match). */
#ifndef DESTRUCTO_REPLAY_H
#define DESTRUCTO_REPLAY_H
#include "game.h"

/* a new level: the shadow is made as the live game was (call after the live game's own game_new and settle) */
void replay_reset(int level, int players, int mode, bool no_budget, int settle_ticks);
/* the same with the shadow built on a background job (loader.h): the level starts at once */
void replay_reset_async(int level, int players, int mode, bool no_budget, int settle_ticks);
/* before every live game_step in play: the input that step takes */
void replay_log(const game *live, const input *in);
/* after every live game_step in play */
void replay_after_step(const game *live);
/* the shadow catches up with the live game for at most budget_ms (a frame's spare time) */
void replay_follow(const game *live, double budget_ms);
/* the shadow stands on the tick before the last shot was fired, and the level is judged: a replay can start */
bool replay_ready(void);
/* how far the shadow has to go (0..1, 1 = ready), for "getting the replay ready" */
float replay_progress(void);
/* the replay: start it, then one call a tick; false when it has reached the judged tick (or was never ready) */
void replay_start(void);
bool replay_tick(void);
bool replay_running(void);
void replay_skip(void);      /* a button pressed during it: the replay ends (one a level) */
const game *replay_game(void);
float replay_alpha(void);    /* the view's interpolation between the shadow's last two steps (slow motion) */
bool replay_slow(void);      /* in the slow motion after the impact */
/* 1: the replay ended on the live game's judged hash; 0: it did not; -1: no replay has ended this level */
int replay_match(void);
uint32_t replay_hash(void);
/* tests: nudge the replay's last shot (the turn of the launcher on the tick it fires) so it must differ */
extern bool g_replay_perturb;

#endif
