/* towers_soak: the rules and the bots with no window and no GPU.
 *
 *   towers_soak --clear <map.cwtd> [players] [minutes] [tries]
 *       The map check's clear run: bots (two by default) play the map from its start with the game's rules; exit 0
 *       when they hold the core through every wave within the time (40 game minutes by default; up to three tries,
 *       each its own seed), 1 when they do not (the line says why: the core fell, the time ran out, a check failed),
 *       2 when the file is not a map.
 *   towers_soak [minutes] [players] [first map]
 *       The long soak: map after map (a fallen core starts its map again), checking every tick: no NaN or stray
 *       position, no creep off the path, counts that agree with the tables, gold never below nothing, every tower on
 *       a slot. A line a game minute (map, wave, creeps, kills, leaks, resident memory). Exit 0 = nothing broke.
 * TOWERS_BOT_BRANCH=0|1|2: the bots' third-level branch (A, B, or each in turn by slot; game.h g_bot_branch).
 * TOWERS_BOT=newcomer: every seat played as a first-time player plays it (newcomer.c), for the difficulty check.
 * TOWERS_OLD_DIFFICULTY=1: the waves as they were (every boss at full strength, gates sending alike, no word on a new kind): the
 *     newcomer check's control. TOWERS_SEED=n: the clear run's first seed.
 * TOWERS_SOAK_INJECT=1 puts a NaN into a creep's health from tick 4000 (the first alive) (the control: the checks must catch it). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/jobs.h"
#include "game.h"

static long rss_kb(void) {
  long pages = 0, res = 0;
  FILE *f = fopen("/proc/self/statm", "r");
  if (f) { if (fscanf(f, "%ld %ld", &pages, &res) != 2) res = 0; fclose(f); }
  return res * 4;
}
static int bad(const sim *s, const char *what) {
  printf("soak FAIL map %d wave %d tick %d: %s\n", s->m.num, s->wave, s->tick, what);
  return 1;
}
static int check(const sim *s) {
  const float HW = (float)s->m.w + 1, HH = (float)s->m.h + 1;
  int n = 0;
  for (int i = 0; i < MAXE; i++) {
    const creep *e = &s->e[i];
    if (!e->alive) continue;
    n++;
    if (!isfinite(e->x) || !isfinite(e->y) || !isfinite(e->hp)) return bad(s, "creep NaN");
    if (fabsf(e->x) > HW || fabsf(e->y) > HH) return bad(s, "creep off the map");
    int tx = mp_tx(&s->m, e->x), ty = mp_ty(&s->m, e->y);
    if (!mp_in(&s->m, tx, ty)) return bad(s, "creep outside the grid");
    if (s->m.t[ty][tx] != T_PATH) { /* the crowd keeps them on the path (a hair over its edge at most) */
      bool near = false;
      for (int dy = -1; dy <= 1 && !near; dy++)
        for (int dx = -1; dx <= 1 && !near; dx++) {
          int px = mp_tx(&s->m, e->x + (float)dx * 0.3f), py = mp_ty(&s->m, e->y + (float)dy * 0.3f);
          near = mp_in(&s->m, px, py) && s->m.t[py][px] == T_PATH;
        }
      if (!near) return bad(s, "creep off the path");
    }
  }
  if (n != s->nenemies) return bad(s, "creep count disagrees with the table");
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active) continue;
    if (p->gold < 0) return bad(s, "gold below nothing");
    if (p->slot >= 0 && !sim_slot_mine(s, i, p->slot)) return bad(s, "a cursor on a slot not its own");
  }
  for (int k = 0; k < s->m.nslot; k++) {
    const tower *t = &s->tw[k];
    if (!t->alive) continue;
    if (t->level < 1 || t->level > 3 || t->kind >= TK_KINDS || t->owner >= MAXP) return bad(s, "a tower out of its table");
    if (!s->p[t->owner].active) return bad(s, "a tower of nobody");
  }
  if (s->core_hp < 0 || s->core_hp > CORE_HP) return bad(s, "core health out of range");
  return 0;
}

static char *read_text(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *t = malloc((size_t)n + 1);
  if (fread(t, 1, (size_t)n, f) != (size_t)n) n = 0;
  t[n] = 0;
  fclose(f);
  return t;
}
static bool load_map(int n, tdmap *m) {
  char path[512], err[200];
#ifdef TOWERS_ASSETS_DIR
  snprintf(path, sizeof path, "%s/maps/%03d.cwtd", TOWERS_ASSETS_DIR, n);
#else
  snprintf(path, sizeof path, "assets/maps/%03d.cwtd", n);
#endif
  char *t = read_text(path);
  if (!t) return false;
  bool ok = map_parse(t, m, sim_kind_by_name, err, sizeof err);
  if (!ok) printf("soak: %s: %s\n", path, err);
  free(t);
  return ok;
}

static sim S;
static tdmap M;
static bool g_newcomer; /* TOWERS_BOT=newcomer: every seat a first-time player (newcomer.c) */

static void step(int players) {
  input in[MAXP];
  memset(in, 0, sizeof in);
  for (int i = 0; i < players; i++) {
    if (g_newcomer) towers_newcomer(&S, i, &in[i]);
    else towers_bot(&S, i, &in[i]);
  }
  sim_step(&S, in);
  S.ev_n = 0; /* (no effects here to read them) */
}

static int clear_once(const char *file, int players, int minutes, uint32_t seed) {
  char *t = read_text(file), err[200];
  if (!t || !map_parse(t, &M, sim_kind_by_name, err, sizeof err)) { printf("clear: %s: %s\n", file, t ? err : "cannot read"); free(t); return 2; }
  free(t);
  sim_init(&S, players, seed);
  sim_map_start(&S, &M);
  towers_newcomer_reset(), g_newcomer_seed = seed;
  long total = (long)minutes * 3600;
  for (long k = 0; k < total; k++) {
    step(players);
    if (check(&S)) return 1;
    if (getenv("TOWERS_TRACE") && k % 1800 == 0) {
      printf("t=%.0f wave %d/%d creeps %d kills %d core %d", (float)k / 60, S.wave, S.m.waves, S.nenemies, S.kills, S.core_hp);
      for (int i = 0; i < players; i++) {
        int n = 0, lv = 0;
        for (int q = 0; q < S.m.nslot; q++) if (S.tw[q].alive && S.tw[q].owner == i) n++, lv += S.tw[q].level;
        printf(" p%d(gold %d towers %d levels %d slot %d)", i, S.p[i].gold, n, lv, S.p[i].slot);
      }
      printf("\n");
      if (S.nenemies > 0 && S.nenemies < 4)
        for (int i = 0; i < MAXE; i++)
          if (S.e[i].alive)
            printf("  creep %d %s at %.2f %.2f tile %d %d prog %d hp %.1f age %d frozen %d slow %d\n", i, sim_kind_name(S.e[i].kind), S.e[i].x, S.e[i].y, mp_tx(&S.m, S.e[i].x),
                   mp_ty(&S.m, S.e[i].y), S.e[i].prog, S.e[i].hp, S.e[i].age, S.e[i].frozen, S.e[i].slow_t);
    }
    if (S.state == ST_WON) {
      int n = 0, ba = 0, bb = 0;
      for (int q = 0; q < S.m.nslot; q++) n += S.tw[q].alive, ba += S.tw[q].alive && S.tw[q].branch == BR_A, bb += S.tw[q].alive && S.tw[q].branch == BR_B;
      printf("clear: map %d held in %.1f min (%d waves, %d kills, core %d of %d, %d towers (third level: %d A, %d B), most creeps %d)\n", M.num, (float)S.map_ticks / 3600.0f,
             S.m.waves, S.kills, S.core_hp, CORE_HP, n, ba, bb, S.most_alive);
      return 0;
    }
    if (S.state == ST_LOST) {
      printf("clear: map %d: the core fell at wave %d of %d after %.1f min (%d kills, most creeps %d)\n", M.num, S.wave, S.m.waves, (float)S.map_ticks / 3600.0f, S.kills,
             S.most_alive);
      return 1;
    }
  }
  printf("clear: map %d: not held in %d minutes (wave %d of %d, %d creeps left)\n", M.num, minutes, S.wave, S.m.waves, S.nenemies);
  return 1;
}

static int clear_run(const char *file, int players, int minutes, int tries) {
  int r = 1;
  for (int k = 0; k < tries && r == 1; k++) {
    r = clear_once(file, players, minutes, (getenv("TOWERS_SEED") ? (uint32_t)atoi(getenv("TOWERS_SEED")) : 4242u) + (uint32_t)k * 7919u);
    if (r == 0 && k) printf("clear: (on try %d)\n", k + 1);
  }
  return r;
}

int main(int argc, char **argv) {
  g_newcomer = getenv("TOWERS_BOT") && !strcmp(getenv("TOWERS_BOT"), "newcomer");
  g_old_difficulty = getenv("TOWERS_OLD_DIFFICULTY") != NULL;
  if (getenv("TOWERS_BOT_BRANCH")) g_bot_branch = CW_CLAMP(atoi(getenv("TOWERS_BOT_BRANCH")), 0, 2);
  cw_jobs_init(3);
  if (argc > 2 && !strcmp(argv[1], "--clear")) return clear_run(argv[2], argc > 3 ? atoi(argv[3]) : 2, argc > 4 ? atoi(argv[4]) : 40, argc > 5 ? CW_CLAMP(atoi(argv[5]), 1, 3) : 3);
  int minutes = argc > 1 ? atoi(argv[1]) : 30, players = argc > 2 ? atoi(argv[2]) : 4, map0 = argc > 3 ? atoi(argv[3]) : 1;
  sim_init(&S, players, 4242);
  if (!load_map(map0, &M)) { printf("soak: no map %d\n", map0); return 2; }
  sim_map_start(&S, &M);
  long total = (long)minutes * 3600, rss0 = 0;
  int maps = 0, falls = 0, most = 0;
  bool injected = !getenv("TOWERS_SOAK_INJECT");
  for (long t = 1; t <= total; t++) {
    if (!injected && t >= 4000) /* the first creep alive from tick 4000 on */
      for (int i = 0; i < MAXE && !injected; i++) if (S.e[i].alive) S.e[i].hp = NAN, injected = true;
    step(players);
    if (check(&S)) return 1;
    if (S.nenemies > most) most = S.nenemies;
    if (S.state == ST_WON && S.state_ticks > 60) {
      maps++;
      int next = S.m.num + 1;
      if (!load_map(next, &M)) next = 1, load_map(1, &M);
      printf("map %d held at %.1f min: to map %d\n", S.m.num, (float)t / 3600.0f, next);
      sim_init(&S, players, 4242 + (uint32_t)t);
      sim_map_start(&S, &M);
    } else if (S.state == ST_LOST && S.state_ticks > 60) {
      falls++;
      printf("map %d: the core fell at wave %d (%.1f min); again\n", S.m.num, S.wave, (float)t / 3600.0f);
      sim_init(&S, players, 4242 + (uint32_t)t);
      sim_map_start(&S, &M);
    }
    if (t % 3600 == 0) {
      if (!rss0) rss0 = rss_kb();
      printf("minute %ld: map %d wave %d creeps %d kills %d leaks %d rss %ld kB (first minute %ld)\n", t / 3600, S.m.num, S.wave, S.nenemies, S.kills, S.leaks, rss_kb(), rss0);
      fflush(stdout);
    }
  }
  printf("soak ok: %d minutes, %d players, %d maps held, %d falls, most creeps %d, rss %ld kB\n", minutes, players, maps, falls, most, rss_kb());
  sim_free(&S);
  return 0;
}
