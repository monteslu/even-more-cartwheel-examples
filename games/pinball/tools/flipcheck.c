/* pinball_flipcheck: when do the flipper buttons work? (tests/pinball-drain.mjs)
 *
 * On a real machine the flippers stay live from the plunge until the ball has gone down the outhole, through the
 * bonus count, in ball save and multiball, and only a tilt kills them. This plays scripted balls on every table, the
 * rules and the solver headless, with both flipper buttons pressed four ticks and let go four ticks over and over, and
 * checks every press: the flipper must move on the tick of the press.
 *   middle    a ball down the middle between the flippers, ball save off: it drains; the presses go on through the
 *             drain and the bonus count until the next ball sits on the plunger
 *   kickback  a ball down the left outlane with the kickback lit: sent back up (the presses are checked from the
 *             start, through the kick and its fall back, until it drains or 400 ticks have gone)
 *   bounce    a ball already past the flipper tips, sent back up between them (a rubber under the flippers would do
 *             it; these tables have none, so the script gives it the bounce) and then flipped as it comes down again
 *   saved     a ball down the middle in ball save: a new ball comes up the shooter lane
 *   tilt      the cabinet nudged into a tilt, then a ball past the flippers: here the presses must do NOTHING (the one
 *             thing that kills the flippers)
 * Prints a line per table and case and a JSON summary last. pinball_flipcheck_oldrule is the same program on the rules
 * as they were (the flippers died the moment a ball crossed the drain line under them): the control, which must fail. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/game.h"

enum { C_MIDDLE, C_KICKBACK, C_BOUNCE, C_SAVED, C_TILT, C_COUNT };
static const char *NAME[C_COUNT] = { "middle", "kickback", "bounce", "saved", "tilt" };

static float frac(const pb_flipper *F) { return (F->ang - F->rest) / (F->up - F->rest); }

/* a game on table t with its first ball launched and in play */
static void start(game *g, int t) {
  game_new(g, t, 1, 777u);
  input in = { 0 };
  for (int i = 0; i < 40; i++) { in.launch = i > 0 && i < 30; game_step(g, &in); } /* hold the plunger half a second, let go */
  in.launch = false;
  for (int i = 0; i < 30; i++) game_step(g, &in);
  if (g->state != GS_PLAY) fprintf(stderr, "flipcheck: table %d: the plunge did not put the ball in play\n", t);
}

static pb_ball *ball0(game *g) {
  for (int i = 0; i < PB_MAXB; i++) if (g->W.b[i].state == BS_FREE) return &g->W.b[i];
  return NULL;
}

typedef struct result {
  int presses, dead;      /* presses checked, presses the flipper did not answer */
  int dead_first_tick;    /* the first unanswered press (ticks since the ball passed the flippers) */
  bool drained, back_up, served, flipped_after;
  int past_tick, drain_tick, ticks;
} result;

static result run_case(int t, int c) {
  static game g;
  start(&g, t);
  result r = { .dead_first_tick = -1, .past_tick = -1, .drain_tick = -1 };
  const table *T = &g.T;
  pb_ball *b = ball0(&g);
  if (!b) return r;
  float cx = T->flipper_cx, line = g.W.fl[T->left_flipper].py; /* the flippers' pivots: under it the ball is past them */
  if (c != C_SAVED) g.ball_save_t = 0;
  if (c == C_TILT) { /* nudge into the tilt first (three warnings) */
    input in = { 0 };
    for (int i = 0; i < 600 && !g.tilted; i++) { in.nudge_up = i % 2 == 0; game_step(&g, &in); }
    b = ball0(&g);
    if (!b || !g.tilted) { r.dead = -1; return r; }
  }
  if (c == C_KICKBACK) {
    g.P[g.cur].kickback_lit = true;
    b->x = -0.2365f, b->y = 0.33f, b->vx = 0, b->vy = -1.0f; /* in the left outlane, going down */
  } else if (c == C_BOUNCE) {
    b->x = cx, b->y = line - 0.03f, b->vx = 0, b->vy = -0.6f; /* between the flipper tips, past them, going down */
  } else {
    b->x = cx, b->y = line + 0.12f, b->vx = 0, b->vy = -1.0f; /* above the flippers, straight down the middle */
  }
  int start_ball = g.P[g.cur].ball, start_drains = g.drains;
  input in = { 0 };
  int bounced = 0;
  for (int k = 0; k < 600; k++) {
    int p0 = c == C_BOUNCE ? 12 : 0; /* the bounce: hands off until the ball is on its way back up */
    bool press = k >= p0 && ((k - p0) / 4) % 2 == 0, edge = k >= p0 && (k - p0) % 8 == 0;
    memset(&in, 0, sizeof in);
    in.left = in.right = press;
    float before[2] = { frac(&g.W.fl[T->left_flipper]), frac(&g.W.fl[T->right_flipper]) };
    game_step(&g, &in);
    r.ticks = k;
    const pb_ball *nb = ball0(&g);
    bool any_past = false;
    for (int i = 0; i < PB_MAXB; i++) if (g.W.b[i].state == BS_FREE && g.W.b[i].y < line && g.W.b[i].x < 0.219f) any_past = true;
    if (r.past_tick < 0 && any_past) r.past_tick = k;
    for (int e = 0; e < g.W.nev; e++) {
      if (g.W.ev[e].kind == EV_DRAIN && r.drain_tick < 0) r.drain_tick = k, r.drained = true;
      if (g.W.ev[e].kind == EV_FLIP && r.past_tick >= 0 && (c == C_BOUNCE ? bounced : 1)) r.flipped_after = true;
    }
    if (c == C_BOUNCE && !bounced && r.past_tick >= 0 && k >= r.past_tick + 2) { /* the bounce off a rubber below */
      pb_ball *bb = ball0(&g);
      if (bb) bb->vy = 1.6f, bb->vx = 0.05f, bounced = k;
    }
    if (c == C_KICKBACK) { if (nb && nb->vy > 2.5f) r.back_up = true; } /* the kickback fired */
    else if (nb && nb->y > line + 0.08f && r.past_tick >= 0 && nb->x < 0.219f) r.back_up = true;
    /* every press once the ball has passed the flippers (the kickback and the tilt: from the start) is checked */
    if (edge && (r.past_tick >= 0 || c == C_TILT || c == C_KICKBACK)) {
      float after[2] = { frac(&g.W.fl[T->left_flipper]), frac(&g.W.fl[T->right_flipper]) };
      for (int s = 0; s < 2; s++) {
        r.presses++;
        bool moved = after[s] > before[s] + 0.01f;
        if (!moved) {
          r.dead++;
          if (r.dead_first_tick < 0) r.dead_first_tick = k - (r.past_tick >= 0 ? r.past_tick : 0);
        }
      }
    }
    bool next = g.P[g.cur].ball != start_ball || (g.drains != start_drains && g.state == GS_PLUNGE);
    if (c == C_SAVED && r.drained && k > r.drain_tick + 120) break;
    if (c == C_KICKBACK && (r.drained || k > 400)) break;
    if (c == C_BOUNCE && r.back_up && r.flipped_after && k > bounced + 120) break;
    if ((c == C_MIDDLE || c == C_TILT) && next) { r.served = true; break; }
    if (g.state == GS_OVER) break;
  }
  return r;
}

int main(void) {
  int fails = 0;
  printf("{\"note\":\"pinball_flipcheck\"}\n");
  for (int t = 0; t < NTABLES; t++)
    for (int c = 0; c < C_COUNT; c++) {
      result r = run_case(t, c);
      const char *why = NULL;
      char buf[160];
      if (r.dead < 0) why = "could not set the case up";
      else if (c == C_TILT) {
        if (r.presses < 8) why = "too few presses checked";
        else if (r.dead != r.presses) snprintf(buf, sizeof buf, "tilted, yet %d of %d presses moved a flipper", r.presses - r.dead, r.presses), why = buf;
        else if (!r.served) why = "the next ball never came";
      } else {
        if (r.past_tick < 0 && c != C_KICKBACK) why = "the ball never passed the flippers";
        else if (r.presses < 8) why = "too few presses checked";
        else if (r.dead) snprintf(buf, sizeof buf, "%d of %d presses after the ball passed the flippers did nothing (the first %d ticks after it passed)", r.dead, r.presses, r.dead_first_tick), why = buf;
        else if (c == C_MIDDLE && !(r.drained && r.served)) why = "the ball did not drain and come back on the plunger";
        else if (c == C_KICKBACK && !r.back_up) why = "the kickback did not send it back up";
        else if (c == C_BOUNCE && (r.drained || !r.back_up || !r.flipped_after)) why = "the ball did not come back up and get flipped";
        else if (c == C_SAVED && !r.drained) why = "the ball did not drain";
      }
      if (c == C_KICKBACK && r.past_tick < 0) r.past_tick = 0;
      printf("%s table %d %-8s: %d presses checked, %d did nothing; passed the flippers at tick %d, drained at %d, %d ticks%s%s\n", why ? "FAIL" : "ok  ", t,
             NAME[c], r.presses, r.dead, r.past_tick, r.drain_tick, r.ticks, why ? ": " : "", why ? why : "");
      if (why) fails++;
    }
  printf("{\"fails\":%d}\n", fails);
  return fails ? 1 : 0;
}
