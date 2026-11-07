/* explorer_soak: the game's rules and the bot with no window and no GPU (native only).
 *
 *   explorer_soak <assets dir> <minutes> [players]
 *
 * Plays from the start through the real input struct (the bot) for that many game minutes, or until the heart
 * falls, and checks every tick: positions and health are finite, the players stay inside the world and out of
 * solid rock. Prints progress each game minute (pickups, wardens, rooms seen, deaths, the bot's recoveries, rss),
 * then "soak ok: <minutes> minutes, <pickups> pickups, <bosses> wardens, won <0|1>" or "soak FAIL: <why>" (exit 1).
 * EXPLORER_SOAK_INJECT=1 puts a NaN into the player's speed at minute 1: the checks must catch it (the control). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bot.h"

static world W;
static sim S;

static long rss_kb(void) {
  FILE *f = fopen("/proc/self/statm", "r");
  long pages = 0, res = 0;
  if (f) {
    if (fscanf(f, "%ld %ld", &pages, &res) != 2) res = 0;
    fclose(f);
  }
  return res * 4;
}

static void *slurp(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *n = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  void *b = malloc(*n);
  if (fread(b, 1, *n, f) != *n) { free(b); b = NULL; }
  fclose(f);
  return b;
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: explorer_soak <assets dir> <minutes> [players]\n"); return 2; }
  char path[1024];
  snprintf(path, sizeof path, "%s/world.bin", argv[1]);
  if (!world_load(&W, path)) { printf("soak FAIL: cannot read %s\n", path); return 1; }
  size_t rn = 0;
  snprintf(path, sizeof path, "%s/route.bin", argv[1]);
  void *rb = slurp(path, &rn);
  bot B;
  if (!bot_load(&B, rb, rn, W.hash)) { printf("soak FAIL: no route for world %08x in %s\n", W.hash, path); return 1; }
  moves_init();
  int minutes = atoi(argv[2]), players = argc > 3 ? atoi(argv[3]) : 1;
  sim_init(&S, &W, players);
  bool inject = getenv("EXPLORER_SOAK_INJECT") != NULL;
  long first_rss = 0;
  int last_i = 0, flat_minutes = 0;
  for (long tick = 0; tick < (long)minutes * 3600 && !S.won; tick++) {
    pinput in[MAXP];
    memset(in, 0, sizeof in);
    bot_input(&B, &S, 0, &in[0]);
    for (int i = 1; i < S.players; i++) bot_follow(&S, i, &in[i]);
    sim_step(&S, in);
    for (int k = 0; getenv("EXPLORER_SOAK_HURT") && k < S.nev; k++)
      if (S.ev[k].kind == EV_HURT || S.ev[k].kind == EV_DIE) {
        printf("t%ld %s hp %d at %.1f,%.1f room %d state %d:", tick, S.ev[k].kind == EV_DIE ? "DIE" : "hurt", S.p[0].hp, S.p[0].x, S.p[0].y, S.room, S.p[0].state);
        for (int j = 0; j < S.nen; j++) if (S.en[j].alive && fabsf(S.en[j].x - S.p[0].x) < 4) printf(" en%d(k%d st%d %.1f,%.1f)", j, S.en[j].kind, S.en[j].state, S.en[j].x, S.en[j].y);
        for (int j = 0; j < MAXSHOTS; j++) if (S.sh[j].alive && S.sh[j].enemy && fabsf(S.sh[j].x - S.p[0].x) < 3) printf(" shot k%d", S.sh[j].kind);
        if (S.b.active) printf(" BOSS hp %d st %d mv %d at %.1f,%.1f t%d", S.b.hp, S.b.state, S.b.move, S.b.x, S.b.y, S.b.t);
        printf(" bot mode %d step %d atkcd %d facing %d\n", B.mode, B.i, S.p[0].atk_cd, S.p[0].facing_left);
      }
    if (getenv("EXPLORER_SOAK_TRACE") && tick < atoi(getenv("EXPLORER_SOAK_TRACE")) && tick > (getenv("EXPLORER_SOAK_FROM") ? atoi(getenv("EXPLORER_SOAK_FROM")) : 0)) {
      for (int j = 0; j < S.nen; j++) if (S.en[j].alive && fabsf(S.en[j].x - S.p[0].x) < 6) printf("   en%d k%d st%d t%d %.2f,%.2f\n", j, S.en[j].kind, S.en[j].state, S.en[j].t, S.en[j].x, S.en[j].y);
      const route_step *st = &B.steps[B.i < B.n ? B.i : B.n - 1];
      printf("t%ld mode %d step %d (%d,%d -> %d,%d mv %d) np %d p %.2f,%.2f v %.2f,%.2f g%d st%d in mx%d j%d\n", tick, B.mode, B.i, st->from % TW, st->from / TW, st->to % TW, st->to / TW, st->move, B.np, S.p[0].x, S.p[0].y, S.p[0].vx, S.p[0].vy, S.p[0].on_ground, S.p[0].state, in[0].mx, in[0].jump);
    }
    if (inject && tick == 3600) S.p[0].vx = NAN;
    for (int i = 0; i < S.players; i++) {
      const player *p = &S.p[i];
      bool bad = !isfinite(p->x) || !isfinite(p->y) || !isfinite(p->vx) || !isfinite(p->vy) || p->x < 0 || p->y < 0 || p->x >= TW || p->y >= TH;
      if (!bad && p->state != PS_DEAD) {
        int x = (int)floorf(p->x), y = (int)floorf(p->y - 0.9f);
        bad = t_solid(S.t[y][x]) && S.t[y][x] != T_BOSSDOOR;
      }
      if (bad) {
        printf("soak FAIL: player %d at %.2f,%.2f speed %.2f,%.2f (tick %ld, room %d)\n", i, p->x, p->y, p->vx, p->vy, tick, S.room);
        return 1;
      }
    }
    if (tick % 3600 == 3599) {
      int seen = 0;
      for (int r = 0; r < W.nrooms; r++) seen += (S.seen[r >> 3] >> (r & 7)) & 1;
      int got = 0;
      for (int k = 0; k < 4; k++) got += sim_pickups_got(&S, k);
      long rss = rss_kb();
      if (!first_rss) first_rss = rss;
      printf("minute %ld: step %d/%d, pickups %d/%d, wardens %d, rooms %d/%d, abilities %02x, deaths %d, recoveries %d, stuck %d, rss %ld kB (first minute %ld)\n",
             tick / 3600 + 1, B.i, B.n, got, W.npickups, __builtin_popcount(S.bosses_down), seen, W.nrooms, S.abilities, S.deaths, B.recoveries,
             B.stuck, rss, first_rss);
      fflush(stdout);
      flat_minutes = B.i == last_i ? flat_minutes + 1 : 0;
      last_i = B.i;
      if (flat_minutes >= 4) {
        printf("soak FAIL: the bot made no progress for 4 minutes at step %d (room %d, at %.1f,%.1f)\n", B.i, S.room, S.p[0].x, S.p[0].y);
        return 1;
      }
    }
  }
  int got = 0;
  for (int k = 0; k < 4; k++) got += sim_pickups_got(&S, k);
  printf("soak ok: %d minutes, %d pickups, %d wardens, won %d\n", minutes, got, __builtin_popcount(S.bosses_down), S.won ? 1 : 0);
  free(rb);
  return 0;
}
