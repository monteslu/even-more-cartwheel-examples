/* jewels_rules_check: replays a trace of the original Lua game (tests/jewels-rules/<seed>.txt, written by
 * tests/jewels-rules.mjs from tests/jewels-rules-driver.lua) through the C game and compares every step.
 *
 *   jewels_rules_check <trace.txt>      exit 0 when every step matches, 1 on the first difference
 *
 * A trace line is one of
 *   seed <n>
 *   step <button> <grid 80 digits> <score> <best> <moves> <cur x> <cur y> <sel x|-> <sel y|-> <hint x,y,dx,dy|-> <reshuffled 0|1>
 *   reshuffle <grid before> <grid after> <kept 0|1>
 * with the original's 1-based cells. Buttons are the original's names: left right up down a b x y.
 *
 * JEWELS_RULES_BREAK (a control that must fail): "score" pays a cascade's second step like its first, "refill"
 * draws refills from the other end of the generator, "hint" always hints the first move. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/game.h"

void jw_game_set_instant(bool on);
extern int jw_rules_break;

static void grid_str(const jw_grid *g, char *out) {
  int i = 0;
  for (int y = 0; y < JW_H; y++)
    for (int x = 0; x < JW_W; x++) out[i++] = (char)('0' + g->c[y][x]);
  out[i] = 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: jewels_rules_check <trace.txt>\n");
    return 2;
  }
  const char *brk = getenv("JEWELS_RULES_BREAK");
  if (brk && !strcmp(brk, "score")) jw_rules_break = 1;
  if (brk && !strcmp(brk, "refill")) jw_rules_break = 2;
  if (brk && !strcmp(brk, "hint")) jw_rules_break = 3;
  FILE *f = fopen(argv[1], "r");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 2;
  }
  jw_game_set_instant(true);
  static jw_game g;
  jw_rng rr;
  char line[512];
  int steps = 0, reshuffles = 0, lineno = 0, prev_resh = 0;
  while (fgets(line, sizeof line, f)) {
    lineno++;
    char kind[16];
    if (sscanf(line, "%15s", kind) != 1) continue;
    if (!strcmp(kind, "seed")) {
      unsigned seed;
      sscanf(line, "seed %u", &seed);
      jw_game_new(&g, seed);
      continue;
    }
    if (!strcmp(kind, "reshuffle")) {
      char before[96], after[96], got[96];
      int kept;
      if (sscanf(line, "reshuffle %95s %95s %d", before, after, &kept) != 3) return fprintf(stderr, "line %d: bad\n", lineno), 2;
      jw_grid gg;
      for (int i = 0; i < JW_W * JW_H; i++) gg.c[i / JW_W][i % JW_W] = (uint8_t)(before[i] - '0');
      /* the original reshuffles these from the generator as the game left it */
      rr = g.rng;
      bool k = jw_reshuffle(&gg, &rr);
      g.rng = rr;
      grid_str(&gg, got);
      if (strcmp(got, after) || (int)k != kept) {
        printf("MISMATCH line %d reshuffle\n  lua %s %d\n  c   %s %d\n", lineno, after, kept, got, (int)k);
        return 1;
      }
      reshuffles++;
      continue;
    }
    if (strcmp(kind, "step")) continue;
    char button[16], grid[96], sxs[8], sys[8], hints[32];
    long long score, best;
    int moves, cx, cy, resh;
    if (sscanf(line, "step %15s %95s %lld %lld %d %d %d %7s %7s %31s %d", button, grid, &score, &best, &moves, &cx, &cy, sxs, sys, hints, &resh) != 11)
      return fprintf(stderr, "line %d: bad step\n", lineno), 2;
    if (strcmp(button, "-")) {
      jw_input in = { 0 };
      in.left = !strcmp(button, "left"), in.right = !strcmp(button, "right"), in.up = !strcmp(button, "up"), in.down = !strcmp(button, "down");
      in.pick = !strcmp(button, "a") || !strcmp(button, "b");
      in.hint = !strcmp(button, "x") || !strcmp(button, "y");
      jw_game_tick(&g, &in);
    }
    steps++;
    char got[96], gh[48] = "-", gsx[16] = "-", gsy[16] = "-";
    grid_str(&g.grid, got);
    if (g.sel_x >= 0) snprintf(gsx, sizeof gsx, "%d", g.sel_x + 1), snprintf(gsy, sizeof gsy, "%d", g.sel_y + 1);
    if (g.hint_on) snprintf(gh, sizeof gh, "%d,%d,%d,%d", g.hint.x + 1, g.hint.y + 1, g.hint.dx, g.hint.dy);
    int gresh = g.reshuffles != prev_resh;
    prev_resh = g.reshuffles;
    bool ok = !strcmp(got, grid) && g.score == score && g.best == best && g.moves == moves && g.cur_x + 1 == cx && g.cur_y + 1 == cy &&
              !strcmp(gsx, sxs) && !strcmp(gsy, sys) && !strcmp(gh, hints) && gresh == resh && g.state == JW_IDLE;
    if (!ok) {
      printf("MISMATCH line %d (step %d, button %s)\n", lineno, steps, button);
      printf("  lua %s score %lld best %lld moves %d cur %d,%d sel %s,%s hint %s resh %d\n", grid, score, best, moves, cx, cy, sxs, sys, hints, resh);
      printf("  c   %s score %lld best %lld moves %d cur %d,%d sel %s,%s hint %s resh %d state %d\n", got, (long long)g.score, (long long)g.best, g.moves,
             g.cur_x + 1, g.cur_y + 1, gsx, gsy, gh, gresh, (int)g.state);
      return 1;
    }
  }
  fclose(f);
  printf("ok %s: %d steps, %d reshuffles, score %lld, %d moves, %d clears, deepest cascade %d\n", argv[1], steps, reshuffles, (long long)g.score,
         g.moves, g.clears, g.cascade_max);
  return 0;
}
