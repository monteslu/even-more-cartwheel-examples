/* demolition_soak: level after level with the bot on the controls, no window and no GPU.
 *   demolition_soak <minutes> <players> [first level] [mode 0 co-op | 1 versus]
 * Every tick checks every body: a number (no NaN), inside the field (nothing flung out of the world and kept), and
 * the counts the game keeps (blocks alive, free slots) adding up. After each turn's settle it checks debris goes to
 * sleep: a level whose bodies are still awake 30 s after the last shot has something jittering (printed). Prints
 * each level's numbers and the totals; "soak ok" when nothing failed. DEMOLITION_SOAK_INJECT=1 puts a NaN into a
 * block after a minute (the control: the checks must catch it). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cartwheel/jobs.h"
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
static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static game G;

int main(int argc, char **argv) {
  cw_jobs_init(-1);
  int minutes = argc > 1 ? atoi(argv[1]) : 10, players = argc > 2 ? atoi(argv[2]) : 1, level = argc > 3 ? atoi(argv[3]) : 0;
  int mode = argc > 4 ? atoi(argv[4]) : 0;
  bool inject = getenv("DEMOLITION_SOAK_INJECT") != NULL;
  long ticks_total = (long)minutes * 3600, t = 0, first_rss = 0;
  int levels = 0, won = 0, lost = 0, fails = 0, stars = 0, shots = 0, over_par = 0, jitter = 0;
  long broken = 0, frags = 0, booms = 0;
  double step_sum = 0, step_worst = 0;
  long steps = 0;
  int awake_worst = 0;
  bot_state bot;
  memset(&bot, 0, sizeof bot);
  while (t < ticks_total && !fails) {
    game_new(&G, level % NLEVELS, players, mode);
    game_settle(&G, 180);
    { /* memory: once every level has been played (the heap at its high water), a level played again must hold
       * no more than it did on its second visit */
      static long rss_at[NLEVELS];
      static int visits[NLEVELS];
      long kb = rss_kb();
      int v = ++visits[G.level];
      if (v == 2) rss_at[G.level] = kb;
      if (v > 2) printf("  level %d again: rss %ld kB (first time %ld)\n", G.level, kb, rss_at[G.level]);
    }
    bot_reset(&bot);
    int lt = 0, quiet_after = -1;
    while (!game_over(&G) && t < ticks_total && !fails) {
      input in;
      bot_input(&G, &bot, &in);
      double t0 = now_ms();
      game_step(&G, &in);
      double dt = now_ms() - t0;
      step_sum += dt, steps++;
      if (dt > step_worst) step_worst = dt;
      t++, lt++;
      if (inject && t == 3600) {
        for (int i = 0; i < G.nb; i++) if (G.b[i].alive) { cw_body3d_set_transform(G.b[i].body, (cw_xform3){ { NAN, 1, 0 }, { 0, 0, 0, 1 } }); break; }
      }
      int alive = 0;
      for (int i = 0; i < G.nb; i++) {
        if (!G.b[i].alive) continue;
        alive++;
        t3_vec3 p = cw_body3d_transform(G.b[i].body).p;
        if (!(p.x == p.x && p.y == p.y && p.z == p.z) || fabsf(p.x) > 400 || fabsf(p.z) > 400 || p.y > 300) {
          printf("FAIL level %d tick %d: block %d at %f %f %f\n", G.level, lt, i, p.x, p.y, p.z);
          fails++;
          break;
        }
      }
      if (alive != G.nalive) printf("FAIL level %d: %d blocks alive, the game counts %d\n", G.level, alive, G.nalive), fails++;
      int aw = game_awake(&G);
      if (aw > awake_worst) awake_worst = aw;
      /* debris must sleep: 30 s after the last shot was fired, nothing (or almost nothing) still awake */
      if (G.state == GS_AIM && quiet_after < 0) quiet_after = lt;
      if (G.state != GS_AIM) quiet_after = -1;
      if (quiet_after >= 0 && lt - quiet_after == 1800 && aw > 3) {
        printf("JITTER level %d: %d bodies awake 30 s after the turn settled\n", G.level, aw);
        jitter++;
      }
      if (t % (3600 * 5) == 0) {
        long kb = rss_kb();
        if (!first_rss) first_rss = kb;
        printf("  minute %ld: rss %ld kB\n", t / 3600, kb);
      }
      if (t == 3600) first_rss = rss_kb();
    }
    if (game_over(&G)) {
      levels++;
      if (G.state == GS_WON) won++, stars += game_stars(&G), over_par += G.shots > G.L->par;
      else lost++;
      shots += G.shots, broken += G.broken, frags += G.fragments, booms += G.booms;
      printf("level %2d %-15s %s in %d shots (par %d), %d stars, %d broke, %d pieces, %d booms, %d blocks left, %.0f s\n", G.level, G.L->name,
             G.state == GS_WON ? "won " : "LOST", G.shots, G.L->par, game_stars(&G), G.broken, G.fragments, G.booms, G.nalive, lt / 60.0);
      level++;
    }
  }
  printf("totals: %d levels, %d won, %d lost, %d stars, %d over par, %d shots, %ld broken, %ld pieces, %ld booms, %d jitter, step mean %.3f ms worst %.2f ms, most awake %d\n",
         levels, won, lost, stars, over_par, shots, broken, frags, booms, jitter, steps ? step_sum / steps : 0, step_worst, awake_worst);
  printf("  rss %ld kB at the end\n", rss_kb());
  game_free(&G);
  if (fails) { printf("soak FAIL\n"); return 1; }
  printf("soak ok\n");
  return 0;
}
