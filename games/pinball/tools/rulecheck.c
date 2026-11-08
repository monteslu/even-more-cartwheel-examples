/* pinball_rulecheck: does each table play its own rules? (tests/pinball-rules.mjs)
 *
 * Headless, the rules and the solver, the switches fed in as the solver reports them (game_test_event: a ramp made, an
 * orbit crossed, a target hit, the scoop). For every table:
 *   modes     each of its four modes started in turn and checked by how it plays: a roving shot lights one shot and
 *             moves it on; a hurry-up's value falls and a shot collects what it shows; a sweep counts each target once;
 *             an all-shots mode puts a shot out once made; a two-ball mode sends a second ball up; the toy mode works
 *             its toy (the creature rises, a hit sinks it, it rises again; the magnet catches a slow ball over it and
 *             throws it; a hard ramp rings the striker's bell and a soft one does not). An unlit shot never counts.
 *             Then the mode is played to its goal and must be won.
 *   wizard    the four played light the wizard mode at the scoop; the scoop starts it; four balls come up; every shot
 *             is lit and pays more each time; the toy counts; it ends as the balls drain to one and the modes start over
 *   tables    the three tables' mode lists differ, and each has a toy
 * Prints a line per check and a JSON summary last. pinball_rulecheck_oneset is the same program on every table sharing
 * one rule set and no toys (the rules as they were): the control, which must fail. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/game.h"

static int fails, checks;
static void check(bool ok, int t, const char *what, const char *detail) {
  checks++;
  if (!ok) fails++;
  printf("%s table %d %s%s%s\n", ok ? "ok  " : "FAIL", t, what, detail && *detail ? ": " : "", detail ? detail : "");
}

static void steps(game *g, int n) {
  input in = { 0 };
  for (int i = 0; i < n; i++) game_step(g, &in);
}

static void start(game *g, int t) {
  game_new(g, t, 1, 4321u);
  input in = { 0 };
  for (int i = 0; i < 40; i++) { in.launch = i > 0 && i < 30; game_step(g, &in); }
  in.launch = false;
  for (int i = 0; i < 30; i++) game_step(g, &in);
  g->practice = true; /* the ball keeps coming back while the switches are fed */
}

/* the switch event that makes shot s */
static void make_shot(game *g, int s) {
  const table *T = &g->T;
  for (int i = 0; i < T->nel; i++) {
    const elem *e = &T->el[i];
    if (e->shot != s) continue;
    if (e->kind == EK_LOOP) game_test_event(g, EV_CROSS, i, 2.0f);
    else if (e->kind == EK_RAMP) game_test_event(g, EV_RAMP_DONE, i, 1.0f);
    else if (e->kind == EK_HOLE) game_test_event(g, EV_HOLE, i, 0.5f);
    /* an orbit's switch counts one way: feed it the way it counts */
    return;
  }
}

static int lit_count(const game *g, int *first) {
  int n = 0;
  *first = -1;
  for (int s = 0; s < g->T.nshot; s++)
    if (game_shot_lit(g, s)) { if (*first < 0) *first = s; n++; }
  return n;
}

static int64_t score(const game *g) { return g->P[g->cur].score; }

static void play_mode(game *g, int t, int m) {
  const mode_def *md = &g->T.modes[m];
  char d[160];
  game_test_start_mode(g, m);
  check(g->mode == m, t, md->name, "starts");
  int first, n = lit_count(g, &first);
  int unlit = -1;
  for (int s = 0; s < g->T.nshot; s++) if (!game_shot_lit(g, s)) { unlit = s; break; }
  if (unlit >= 0) { /* an unlit shot never counts */
    int h = g->mode_hits;
    make_shot(g, unlit);
    snprintf(d, sizeof d, "the unlit shot %s left the hits at %d (was %d)", g->T.shots[unlit].name, g->mode_hits, h);
    check(g->mode_hits == h, t, md->name, d);
  }
  switch (md->kind) {
  case MK_ROVE: {
    snprintf(d, sizeof d, "one roving shot lit (%d)", n);
    check(n == 1, t, md->name, d);
    steps(g, 60 * 4);
    int f2;
    lit_count(g, &f2);
    snprintf(d, sizeof d, "it moved on (%d to %d)", first, f2);
    check(f2 != first && f2 >= 0, t, md->name, d);
    break;
  }
  case MK_HURRY: {
    int64_t h0 = g->hurry;
    steps(g, 60 * 5);
    int64_t h1 = g->hurry, sc = score(g);
    lit_count(g, &first);
    make_shot(g, first);
    int64_t got = score(g) - sc;
    snprintf(d, sizeof d, "the value fell from %lld to %lld and the shot paid %lld (at least that)", (long long)h0, (long long)h1, (long long)got);
    check(h1 < h0 && got >= h1, t, md->name, d);
    break;
  }
  case MK_SWEEP: {
    int tgt = -1;
    for (int i = 0; i < g->T.nel; i++) if (g->T.el[i].kind == EK_TARGET || g->T.el[i].kind == EK_DROP) { tgt = i; break; }
    game_test_event(g, EV_HIT, tgt, 1.0f);
    int h = g->mode_hits;
    game_test_event(g, EV_HIT, tgt, 1.0f); /* the same one again: no */
    snprintf(d, sizeof d, "no shot lit (%d), a target counts once (%d, then %d)", n, h, g->mode_hits);
    check(n == 0 && h == 1 && g->mode_hits == 1, t, md->name, d);
    break;
  }
  case MK_ALL: {
    lit_count(g, &first);
    make_shot(g, first);
    snprintf(d, sizeof d, "%d lit; the made shot went out (%s)", n, game_shot_lit(g, first) ? "still lit" : "out");
    check(n == __builtin_popcount((unsigned)md->shots_mask) && !game_shot_lit(g, first), t, md->name, d);
    break;
  }
  case MK_SHOTS: {
    snprintf(d, sizeof d, "%d lit of mask %d", n, __builtin_popcount((unsigned)md->shots_mask));
    check(n == __builtin_popcount((unsigned)md->shots_mask), t, md->name, d);
    break;
  }
  case MK_TOY: {
    int toy = g->T.toy, el = g->T.toy_el;
    if (toy == TOY_CREATURE) {
      steps(g, 20);
      bool up = g->toy_up && !(g->W.sh[g->T.el[el].shape].flags & SF_OFF);
      int h = g->mode_hits;
      game_test_event(g, EV_HIT, el, 1.0f);
      bool sank = !g->toy_up && (g->W.sh[g->T.el[el].shape].flags & SF_OFF);
      steps(g, 60 * 3);
      snprintf(d, sizeof d, "the creature rose (%d), a hit counted (%d to %d) and sank it (%d), it rose again (%d)", up, h, g->mode_hits, sank, g->toy_up);
      check(up && g->mode_hits == h + 1 && sank && g->toy_up, t, md->name, d);
    } else if (toy == TOY_MAGNET) {
      const elem *e = &g->T.el[el];
      pb_ball *b = NULL;
      for (int i = 0; i < PB_MAXB; i++) if (g->W.b[i].state == BS_FREE) { b = &g->W.b[i]; break; }
      int h = g->mode_hits, held = 0;
      float out_v = 0;
      if (b) {
        b->x = e->x + 0.01f, b->y = e->y, b->vx = 0, b->vy = 0.05f;
        input in = { 0 };
        for (int k = 0; k < 150; k++) {
          game_step(g, &in);
          if (b->state == BS_HELD) held++;
          else if (held) { out_v = sqrtf(b->vx * b->vx + b->vy * b->vy); break; }
        }
      }
      snprintf(d, sizeof d, "the magnet caught the ball (held %d ticks), threw it at %.2f m/s, a hit (%d to %d)", held, out_v, h, g->mode_hits);
      check(held > 30 && out_v > 1.0f && g->mode_hits == h + 1, t, md->name, d);
    } else if (toy == TOY_STRIKER) {
      int ramp = -1;
      for (int i = 0; i < g->T.nel; i++) if (g->T.el[i].kind == EK_RAMP) { ramp = i; break; }
      int h = g->mode_hits, b0 = g->bells;
      game_test_event(g, EV_RAMP_IN, ramp, 1.3f); /* soft */
      int soft = g->bells - b0, h1 = g->mode_hits;
      game_test_event(g, EV_RAMP_IN, ramp, 3.2f); /* hard */
      game_test_event(g, EV_RAMP_DONE, ramp, 1.0f); /* the ramp made: the striker's ramps count by the bell alone */
      snprintf(d, sizeof d, "a soft ramp: %d bells, %d hits; a hard one rang it (%d bells, %d hits)", soft, h1 - h, g->bells - b0, g->mode_hits - h);
      check(soft == 0 && h1 == h && g->bells - b0 == 1 && g->mode_hits == h + 1, t, md->name, d);
    } else check(false, t, md->name, "a toy mode on a table with no toy");
    break;
  }
  default: check(false, t, md->name, "unknown kind");
  }
  if (md->add_balls > 0) {
    steps(g, 60 * 3);
    snprintf(d, sizeof d, "a second ball came up (%d in play)", g->balls_in_play);
    check(g->balls_in_play >= 2, t, md->name, d);
  }
  /* play it to its goal */
  for (int guard = 0; g->mode == m && guard < 40; guard++) {
    if (md->kind == MK_SWEEP) {
      for (int i = 0; i < g->T.nel && g->mode == m; i++)
        if ((g->T.el[i].kind == EK_TARGET || g->T.el[i].kind == EK_DROP) && !g->T.el[i].swept) { game_test_event(g, EV_HIT, i, 1.0f); break; }
    } else if (md->kind == MK_TOY && g->T.toy == TOY_CREATURE) {
      steps(g, 60 * 2);
      game_test_event(g, EV_HIT, g->T.toy_el, 1.0f);
    } else if (md->kind == MK_TOY && g->T.toy == TOY_STRIKER) {
      int ramp = -1;
      for (int i = 0; i < g->T.nel; i++) if (g->T.el[i].kind == EK_RAMP) { ramp = i; break; }
      game_test_event(g, EV_RAMP_IN, ramp, 3.0f);
    } else {
      int f;
      if (lit_count(g, &f) > 0) make_shot(g, f);
    }
  }
  snprintf(d, sizeof d, "won (%d of %d modes won)", g->modes_won, m + 1);
  check(g->mode < 0 && (g->P[0].modes_done >> m & 1), t, md->name, d);
}

int main(void) {
  static game g;
  int kinds[NTABLES][MAXMODE];
  int toys[NTABLES];
  for (int t = 0; t < NTABLES; t++) {
    start(&g, t);
    toys[t] = g.T.toy;
    check(g.T.toy != TOY_NONE && g.T.toy_el >= 0, t, "toy", g.T.toy == TOY_CREATURE ? "the creature" : g.T.toy == TOY_MAGNET ? "the magnet" : g.T.toy == TOY_STRIKER ? "the high striker" : "none");
    for (int m = 0; m < g.T.nmode; m++) kinds[t][m] = g.T.modes[m].kind * 10 + g.T.modes[m].add_balls;
    for (int m = 0; m < g.T.nmode; m++) play_mode(&g, t, m);
    char d[160];
    snprintf(d, sizeof d, "lit after four modes (%d)", g.P[0].wizard_lit);
    check(g.P[0].wizard_lit, t, g.T.wizard_name, d);
    /* the modes' ramps may have locked balls into a multiball: drain to one ball first (the wizard mode never starts
     * in a multiball) */
    g.practice = false, g.ball_save_t = 0;
    for (int i = 0; i < PB_MAXB && g.balls_in_play > 1; i++)
      if (g.W.b[i].state != BS_OFF) g.W.b[i].state = BS_OFF, game_test_event(&g, EV_DRAIN, -1, 1.0f);
    g.practice = true;
    game_test_event(&g, EV_HOLE, g.T.mode_hole, 0.5f);
    g.practice = false; /* goals count only outside practice (the wizard mode's ball save holds the balls) */
    steps(&g, 60 * 5);
    g.practice = true;
    int first, n = lit_count(&g, &first);
    snprintf(d, sizeof d, "started at the scoop (%d), %d balls up, %d of %d shots lit", g.wizard, g.balls_in_play, n, g.T.nshot);
    check(g.wizard && g.balls_in_play >= 4 && n == g.T.nshot, t, g.T.wizard_name, d);
    int64_t v0 = g.wizard_value, s0 = score(&g);
    make_shot(&g, first);
    int64_t p1 = score(&g) - s0, v1 = g.wizard_value;
    s0 = score(&g);
    make_shot(&g, (first + 1) % g.T.nshot);
    int64_t p2 = score(&g) - s0;
    snprintf(d, sizeof d, "shots paid %lld then %lld (value %lld to %lld)", (long long)p1, (long long)p2, (long long)v0, (long long)v1);
    check(p1 >= v0 && p2 > p1, t, g.T.wizard_name, d);
    /* the balls drain to one: it ends and the modes start over */
    g.practice = false, g.ball_save_t = 0;
    for (int i = 0; i < PB_MAXB && g.balls_in_play > 1; i++)
      if (g.W.b[i].state != BS_OFF) g.W.b[i].state = BS_OFF, game_test_event(&g, EV_DRAIN, -1, 1.0f);
    snprintf(d, sizeof d, "ended at one ball (%d), modes played reset (%d), wizards %d", g.wizard, g.P[0].modes_played, g.P[0].wizards);
    check(!g.wizard && g.P[0].modes_played == 0 && g.P[0].wizards == 1, t, g.T.wizard_name, d);
    snprintf(d, sizeof d, "the wizard mode met its goal (goals %x)", g.objectives);
    check(g.objectives >> 4 & 1, t, "goals", d);
    /* two modes started at the scoop on one ball: a goal */
    start(&g, t);
    g.practice = false, g.ball_save_t = 60 * 90; /* the ball save keeps this one ball in play */
    int before = g.objectives;
    g.P[0].mode_lit = true;
    game_test_event(&g, EV_HOLE, g.T.mode_hole, 0.5f);
    int m1 = g.mode;
    for (int k = 0; k < 200 && g.mode >= 0; k++) g.ball_save_t = 600, steps(&g, 60); /* it runs out */
    g.P[0].mode_lit = true;
    game_test_event(&g, EV_HOLE, g.T.mode_hole, 0.5f);
    steps(&g, 2);
    snprintf(d, sizeof d, "two modes on one ball (%d, then %d): goals %x to %x", m1, g.mode, before, g.objectives);
    check(m1 >= 0 && g.mode >= 0 && !(before >> 1 & 1) && (g.objectives >> 1 & 1), t, "goals", d);
  }
  bool differ = memcmp(kinds[0], kinds[1], sizeof kinds[0]) && memcmp(kinds[0], kinds[2], sizeof kinds[0]) && memcmp(kinds[1], kinds[2], sizeof kinds[0]);
  check(differ && toys[0] != toys[1] && toys[1] != toys[2] && toys[0] != toys[2], -1, "tables", "each plays its own modes and toy");
  printf("{\"checks\":%d,\"fails\":%d}\n", checks, fails);
  return fails ? 1 : 0;
}
