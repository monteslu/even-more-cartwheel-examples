/* jewels_soak: the game and its effects with no window and no GPU, for game minutes on end, through the same input
 * paths a player uses: pad presses, taps, drags and mouse clicks, illegal swaps, picks put down, hints.
 *
 *   jewels_soak <minutes> <seed>        prints "soak ok" and the totals, or "soak FAIL <why>" (exit 1)
 *
 * Checked every tick: the score never goes down; an idle board holds no match and has a legal move; a move
 * always comes to rest (no step runs past its length); the board holds only real kinds; every stone's look is a
 * finite number; the effects stay within their pools. JEWELS_SOAK_INJECT=1 (the control) breaks a cell mid-run and
 * the checks must catch it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx.h"
#include "game.h"

static int fail(const char *why, uint32_t tick) {
  printf("soak FAIL %s (tick %u)\n", why, tick);
  return 1;
}

int main(int argc, char **argv) {
  int minutes = argc > 1 ? atoi(argv[1]) : 10;
  uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 1;
  bool inject = getenv("JEWELS_SOAK_INJECT") != NULL;
  static jw_game g;
  static jw_fx fx;
  jw_game_new(&g, seed);
  jw_fx_init(&fx);
  uint32_t ticks = (uint32_t)minutes * 3600u;
  int64_t last_score = 0;
  int idle_run = 0, method = 0, plan_wait = 0;
  /* a planned gesture: up to 8 inputs, one a tick, with gaps */
  jw_input plan[64];
  int nplan = 0, iplan = 0;
  uint32_t r = seed * 2654435761u + 1;
  long long swaps_by[5] = { 0 };
  for (uint32_t t = 0; t < ticks; t++) {
    jw_input in;
    memset(&in, 0, sizeof in);
    if (iplan < nplan) in = plan[iplan++];
    else if (g.state == JW_IDLE && --plan_wait <= 0) {
      /* the next gesture */
      nplan = iplan = 0;
      memset(plan, 0, sizeof plan);
      jw_move ms[JW_MAX_MOVES];
      int n = jw_legal_moves(&g.grid, ms);
      r = r * 1664525u + 1013904223u;
      jw_move m = ms[(r >> 8) % (unsigned)(n ? n : 1)];
      method = (int)((r >> 4) % 8);
      if (method == 7 && n) { /* an illegal swap: a pair that does not match */
        for (int y = 0; y < JW_H && !nplan; y++)
          for (int x = 0; x < JW_W - 1 && !nplan; x++)
            if (!jw_is_legal(&g.grid, x, y, x + 1, y)) m = (jw_move){ (int8_t)x, (int8_t)y, 1, 0 }, nplan = -1;
        nplan = 0;
        method = 1;
      }
      if (method == 6) { plan[nplan++].hint = true; plan_wait = 30; continue; }
      if (method == 5) { /* pick up and put down */
        plan[nplan].ptr_down[1] = plan[nplan].ptr_tap[1] = true, plan[nplan].ptr_x[1] = jw_cell_cx(m.x), plan[nplan++].ptr_y[1] = jw_cell_cy(m.y);
        nplan++;
        plan[nplan].ptr_down[1] = plan[nplan].ptr_tap[1] = true, plan[nplan].ptr_x[1] = jw_cell_cx(m.x), plan[nplan++].ptr_y[1] = jw_cell_cy(m.y);
        nplan++;
        plan_wait = 10;
        continue;
      }
      if (!n) continue;
      if (method <= 1) { /* the pad */
        int dx = m.x - g.cur_x, dy = m.y - g.cur_y;
        for (int i = 0; i < abs(dx) && nplan < 40; i++) { if (dx > 0) plan[nplan].right = true; else plan[nplan].left = true; nplan += 2; }
        for (int i = 0; i < abs(dy) && nplan < 40; i++) { if (dy > 0) plan[nplan].down = true; else plan[nplan].up = true; nplan += 2; }
        plan[nplan].pick = true, nplan += 2;
        if (m.dx) plan[nplan].right = true; else plan[nplan].down = true;
        nplan++;
        swaps_by[0]++;
      } else if (method <= 3) { /* touch: tap, tap (a finger in slot 1..10) */
        int s = 1 + (int)((r >> 12) % 10);
        plan[nplan].ptr_down[s] = plan[nplan].ptr_tap[s] = true, plan[nplan].ptr_x[s] = jw_cell_cx(m.x), plan[nplan++].ptr_y[s] = jw_cell_cy(m.y);
        nplan += 2;
        plan[nplan].ptr_down[s] = plan[nplan].ptr_tap[s] = true, plan[nplan].ptr_x[s] = jw_cell_cx(m.x + m.dx), plan[nplan++].ptr_y[s] = jw_cell_cy(m.y + m.dy);
        swaps_by[1]++;
      } else if (method == 4) { /* a drag */
        int s = 1 + (int)((r >> 12) % 10);
        for (int k = 0; k <= 8; k++) {
          float f = (float)k / 8.0f;
          plan[nplan].ptr_down[s] = true, plan[nplan].ptr_tap[s] = k == 0;
          plan[nplan].ptr_x[s] = jw_cell_cx(m.x) + f * (float)m.dx * JW_CELL, plan[nplan].ptr_y[s] = jw_cell_cy(m.y) + f * (float)m.dy * JW_CELL;
          nplan++;
        }
        nplan++;
        swaps_by[2]++;
      }
      plan_wait = 2;
    }
    int64_t before = g.score;
    g.nev = 0;
    jw_game_tick(&g, &in);
    jw_fx_events(&fx, &g, 1.0f / 60.0f);
    int ms;
    jw_fx_take_rumble(&fx, &ms);
    if (inject && t == ticks / 2) g.grid.c[3][3] = 9;
    /* the checks */
    if (g.score < before || g.score < last_score) return fail("the score went down", t);
    last_score = g.score;
    for (int y = 0; y < JW_H; y++)
      for (int x = 0; x < JW_W; x++) {
        if (g.grid.c[y][x] < 1 || g.grid.c[y][x] > JW_KINDS) return fail("a cell holds no stone", t);
        jw_cell_view v = jw_game_cell(&g, x, y, 0.5f);
        if (!isfinite(v.dx) || !isfinite(v.dy) || !isfinite(v.scale) || !isfinite(v.opacity)) return fail("a stone's look is not a number", t);
      }
    if (g.state == JW_IDLE) {
      idle_run = 0;
      if (jw_any_match(&g.grid)) return fail("an idle board holds a match", t);
      if (jw_legal_moves(&g.grid, NULL) == 0) return fail("an idle board has no move", t);
    } else if (++idle_run > 60 * 60) return fail("a move never came to rest", t);
    if (g.t > g.t_end + 1) return fail("a step ran past its length", t);
    if (fx.nbursts > JW_FX_BURSTS || fx.nrings > JW_MAX_RINGS || fx.npops > JW_FX_POPS) return fail("the effects overflowed", t);
  }
  printf("totals: %d moves, score %lld, %d clears, %d reshuffles, %d hints, %d illegal, deepest cascade %d, largest group %d, "
         "swaps pad %d touch %d (tap %d, drag %d), finales %d\n",
         g.moves, (long long)g.score, g.clears, g.reshuffles, g.hints, g.illegal, g.cascade_max, g.largest_group, g.pad_swaps, g.touch_swaps, g.tap_swaps,
         g.drag_swaps, fx.finales);
  printf("soak ok\n");
  return 0;
}
