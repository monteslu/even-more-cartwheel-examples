/* pinball_soak: the rules and the bot with no window and no GPU, game after game.
 *   pinball_soak <minutes> <players> [table|-1 all] [seed]
 * Every tick checks every ball: a number (no NaN), on the table (inside the cabinet), and not stuck (a free ball that
 * has not moved 3 mm in 8 seconds, away from the plunger and the holes, is a trap in the table's geometry: its place
 * is printed). Prints each game's numbers and the totals; "soak ok" when nothing failed. PINBALL_SOAK_INJECT=1 puts a
 * NaN into a ball after a minute (the control: the checks must catch it). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

static long rss_kb(void) {
  FILE *f = fopen("/proc/self/status", "r");
  if (!f) return 0;
  char line[256];
  long kb = 0;
  while (fgets(line, sizeof line, f))
    if (!strncmp(line, "VmRSS:", 6)) kb = atol(line + 6);
  fclose(f);
  return kb;
}

int main(int argc, char **argv) {
  int minutes = argc > 1 ? atoi(argv[1]) : 10, players = argc > 2 ? atoi(argv[2]) : 1, only = argc > 3 ? atoi(argv[3]) : -1;
  uint32_t seed = argc > 4 ? (uint32_t)atoi(argv[4]) : 12345u;
  bool inject = getenv("PINBALL_SOAK_INJECT") != NULL;
  static game g;
  uint32_t rng = seed;
  long ticks_total = (long)minutes * 60 * 60, t = 0, first_rss = 0;
  int games = 0, fails = 0, stuck_reports = 0;
  long long score_sum = 0;
  int64_t best = 0;
  long flips = 0, ramps = 0, modes = 0, mbs = 0, jps = 0, drains = 0, ball_ticks = 0, saves = 0, tilts = 0, hits = 0, modes_won = 0;
  float last_x[PB_MAXB], last_y[PB_MAXB], hi_x[PB_MAXB] = { 0 };
  long drain_where[3] = { 0 }; /* left outlane, the middle, right outlane: where each ball last was above the slings */
  int still[PB_MAXB];
  while (t < ticks_total && fails == 0) {
    int tb = only >= 0 ? only : games % NTABLES;
    game_new(&g, tb, players, seed + (uint32_t)games * 7919u);
    for (int i = 0; i < PB_MAXB; i++) still[i] = 0, last_x[i] = last_y[i] = 0;
    int gt = 0;
    while (!game_over(&g) && t < ticks_total) {
      input in;
      bot_input(&g, &in, &rng);
      game_step(&g, &in);
      t++, gt++;
      for (int k = 0; k < g.W.nev; k++)
        if (g.W.ev[k].kind == EV_DRAIN) { float x = hi_x[g.W.ev[k].ball]; drain_where[x < -0.21f ? 0 : x > 0.18f ? 2 : 1]++; }
      if (inject && t >= 3600) /* the first free ball after a minute (at the minute itself there may be none: a drain) */
        for (int i = 0; i < PB_MAXB; i++) if (g.W.b[i].state == BS_FREE) { g.W.b[i].x = NAN, inject = false; break; }
      for (int i = 0; i < PB_MAXB; i++) {
        const pb_ball *b = &g.W.b[i];
        if (b->state == BS_OFF) { still[i] = 0; continue; }
        if (b->y > 0.2f && b->y < 0.33f) hi_x[i] = b->x;
        if (!(b->x == b->x) || !(b->y == b->y) || !(b->vx == b->vx) || !(b->vy == b->vy)) {
          printf("soak FAIL: table %d game %d tick %d: ball %d is not a number\n", tb, games, gt, i);
          fails++;
          break;
        }
        if (b->state == BS_FREE && (b->x < -0.27f || b->x > 0.27f || b->y < -0.02f || b->y > 1.1f)) {
          printf("soak FAIL: table %d game %d tick %d: ball %d left the table at %.3f %.3f\n", tb, games, gt, i, b->x, b->y);
          fails++;
          break;
        }
        bool parked = b->state != BS_FREE || (b->x > 0.219f && b->y < 0.08f);
        float dx = b->x - last_x[i], dy = b->y - last_y[i];
        if (parked || dx * dx + dy * dy > 0.003f * 0.003f) still[i] = 0, last_x[i] = b->x, last_y[i] = b->y;
        else if (++still[i] == 8 * 60) {
          printf("soak STUCK: table %d (%s) game %d tick %d: ball %d at x %.3f y %.3f (v %.3f %.3f)\n", tb, g.T.name, games, gt, i, b->x, b->y, b->vx, b->vy);
          stuck_reports++;
          /* free it as a player would (a nudge), and carry on */
          g.W.b[i].vx += 0.4f, g.W.b[i].vy += 0.6f;
          still[i] = 0;
        }
      }
      if (t == 3600) first_rss = rss_kb();
      if (first_rss && t % (3600 * 5) == 0) printf("rss %ld kB (first minute %ld)\n", rss_kb(), first_rss);
    }
    if (fails) break;
    games++;
    for (int p = 0; p < g.players; p++) { score_sum += g.P[p].score; if (g.P[p].score > best) best = g.P[p].score; }
    flips += g.flips, ramps += g.ramps_made, modes += g.modes_started, mbs += g.mbs, jps += g.jackpots_total, drains += g.drains;
    ball_ticks += g.ball_ticks, saves += g.saves, tilts += g.tilts, hits += g.hits, modes_won += g.modes_won;
    char sb[32];
    printf("game %d table %d (%s): %d players, %.1f min, score p1 %s, flips %d, ramps %d, modes %d (%d won), multiballs %d, jackpots %d, saves %d, tilts %d, balls %d\n",
           games, tb, g.T.name, g.players, (float)gt / 3600.0f, fmt_score(g.P[0].score, sb, sizeof sb), g.flips, g.ramps_made, g.modes_started, g.modes_won, g.mbs,
           g.jackpots_total, g.saves, g.tilts, g.drains);
  }
  printf("drains by way: left outlane %ld, the middle %ld, right outlane %ld\n", drain_where[0], drain_where[1], drain_where[2]);
  printf("totals: %d games, %ld drains, mean ball %.1f s, flips/min %.1f, ramps %ld, modes %ld (%ld won), multiballs %ld, jackpots %ld, saves %ld, tilts %ld, hits %ld, mean score %lld, best %lld, stuck %d\n",
         games, drains, drains ? (float)ball_ticks / 60.0f / (float)drains : 0, (float)flips / ((float)t / 3600.0f), ramps, modes, modes_won, mbs, jps, saves, tilts, hits,
         games ? score_sum / (games * players) : 0, (long long)best, stuck_reports);
  if (fails) { printf("soak FAIL\n"); return 1; }
  printf("soak ok: %d minutes, %d games\n", minutes, games);
  return 0;
}
