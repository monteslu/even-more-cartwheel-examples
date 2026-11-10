/* destructo_soak: level after level with the bot on the controls, no window and no GPU.
 *   destructo_soak <minutes> <players> [first level] [mode 0 co-op | 1 versus | 2 free fire: the one structure, shot at
 *   for the whole run with ammunition without end, never judged | 3 party: steady hands, round after round]
 * Every tick checks every body: a number (no NaN), inside the field (nothing flung out of the world and kept), and
 * the counts the game keeps (blocks alive, free slots) adding up. After each turn's settle it checks debris goes to
 * sleep: a level whose bodies are still awake 30 s after the last shot has something jittering (printed). Prints
 * each level's last shot is replayed the way the game replays it (src/replay.c: the level made again in a second
 * world and stepped with the logged inputs) and must end on the live state (a replay that ends elsewhere fails). Prints
 * each level's numbers and the totals; "soak ok" when nothing failed. A sixth argument "inject" puts a NaN into a
 * block after a minute (the control: the checks must catch it). The gate runs it in the check cart (destructo_check:
 * check run soak <minutes> <players> <level> <mode> [inject]), where rss is the cart's linear memory. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cartwheel/jobs.h"
#include "game.h"
#include "replay.h"

static long rss_kb(void) {
#ifdef __wasm__ /* a cart (destructo_check): its linear memory, which grows as the heap's high water does and never shrinks */
  return (long)__builtin_wasm_memory_size(0) * 64;
#endif
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

static double soak_clock(void) { return now_ms() / 1000.0; }

int main(int argc, char **argv) {
  g_sim_clock = soak_clock;
  cw_jobs_init(-1);
  int minutes = argc > 1 ? atoi(argv[1]) : 10, players = argc > 2 ? atoi(argv[2]) : 1, level = argc > 3 ? atoi(argv[3]) : 0;
  int mode = argc > 4 ? atoi(argv[4]) : 0;
  if (argc > 3 && level_find(argv[3]) >= 0) level = level_find(argv[3]); /* a level by its name */
  bool sandbox = mode == 2;
  if (sandbox) mode = MODE_COOP;
  else if (mode == 3) mode = MODE_PARTY;
  int party_rounds = 0, party_won = 0, party_crystal = 0, party_strikes = 0;
  int most_alive = 0;
  bool inject = argc > 5 && !strcmp(argv[5], "inject"); /* the control: a NaN put into a block after a minute */
  bool spike = argc > 5 && !strcmp(argv[5], "spike"); /* development: every tick over 3 ms, printed */
  long ticks_total = (long)minutes * 3600, t = 0, first_rss = 0;
  int levels = 0, won = 0, lost = 0, fails = 0, stars = 0, shots = 0, over_par = 0, jitter = 0, replays = 0;
  long broken = 0, frags = 0, booms = 0;
  double step_sum = 0, step_worst = 0;
  long steps = 0;
  int awake_worst = 0;
  bot_state bot;
  memset(&bot, 0, sizeof bot);
  while (t < ticks_total && !fails) {
    game_new(&G, level % NLEVELS, players, mode);
    G.sandbox = sandbox;
    game_settle(&G, level_settle_ticks(G.L));
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
    replay_reset(G.level, players, mode, false, level_settle_ticks(G.L));
    int lt = 0, quiet_after = -1;
    while (!game_over(&G) && t < ticks_total && !fails) {
      input in;
      bot_input(&G, &bot, &in);
      double t0 = now_ms();
      int nb0 = G.nalive, br0 = G.broken;
      replay_log(&G, &in);
      game_step(&G, &in);
      replay_after_step(&G);
      replay_follow(&G, G.awake_now < 150 ? 3.0 : 0.0);
      if (spike && G.tick_ms > 3) printf("  spike level %d tick %d: %.2f ms (pre %.2f, world %.2f [box3d %.2f], post %.2f), awake %d, %d broke, bodies %d -> %d\n", G.level, G.tick, G.tick_ms, G.pre_ms, G.world_ms, G.step_ms, G.post_ms, G.awake_now, G.broken - br0, nb0, G.nalive);
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
      if (alive > most_alive) most_alive = alive;
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
      if (t == 3600) first_rss = rss_kb(), printf("  minute 1: rss %ld kB\n", first_rss);
    }
    if (game_over(&G)) {
      replay_follow(&G, 1e9);
      if (replay_ready()) {
        replay_start();
        while (replay_tick()) {}
      }
      if (replay_match() != 1) printf("FAIL level %d: the replay %s\n", G.level, replay_match() == 0 ? "ended on another state" : "never ran"), fails++;
      else replays++;
      levels++;
      if (G.mode == MODE_PARTY) {
        party_rounds++, party_won += G.party_winner >= 0;
        for (int p = 0; p < G.players; p++) party_crystal += G.P[p].out == 1, party_strikes += G.P[p].out == 2;
        int in = 0;
        for (int p = 0; p < G.players; p++) in += !G.P[p].out;
        if (in > 1 && G.tgt_left > 0) printf("FAIL level %d: the party ended with %d players in\n", G.level, in), fails++;
      }
      if (G.state == GS_WON) won++, stars += game_stars(&G), over_par += G.shots > G.L->par;
      else lost++;
      shots += G.shots, broken += G.broken, frags += G.fragments, booms += G.booms;
      printf("level %2d %-15s %s in %d shots (par %d), %d stars, %d broke, %d pieces, %d booms, %d blocks left, %.0f s\n", G.level, G.L->name,
             G.state == GS_WON ? "won " : "LOST", G.shots, G.L->par, game_stars(&G), G.broken, G.fragments, G.booms, G.nalive, lt / 60.0);
      level++;
    }
  }
  printf("totals: %d levels, %d won, %d lost, %d stars, %d over par, %d shots, %ld broken, %ld pieces, %ld booms, %d jitter, %d replays on the live state, step mean %.3f ms worst %.2f ms, most awake %d\n",
         levels, won, lost, stars, over_par, shots, broken, frags, booms, jitter, replays, steps ? step_sum / steps : 0, step_worst, awake_worst);
  if (G.mode == MODE_PARTY) printf("party: %d rounds, %d with one player left, %d out for a crystal, %d out on strikes\n", party_rounds, party_won, party_crystal, party_strikes);
  if (sandbox) printf("free fire: %d shots, %d broken, %d pieces, %d charges, most bodies %d, level judged %s\n", G.shots, G.broken, G.fragments, G.booms, most_alive, game_over(&G) ? "(a failure)" : "never");
  if (sandbox && game_over(&G)) fails++;
  printf("  rss %ld kB at the end\n", rss_kb());
  game_free(&G);
  if (fails) { printf("soak FAIL\n"); return 1; }
  printf("soak ok\n");
  return 0;
}
