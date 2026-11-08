/* lanternfall_soak: the game's rules and the bot with no window and no GPU (native only).
 *
 *   lanternfall_soak <assets dir> <minutes> [players]
 *
 * Plays from the start through the real input struct (the bot) for that many game minutes, or until the heart
 * falls, and checks every tick: positions and health are finite, the players stay inside the world and out of
 * solid rock. Prints progress each game minute (pickups, wardens, rooms seen, deaths, the bot's recoveries, rss),
 * then "soak ok: <minutes> minutes, <pickups> pickups, <bosses> wardens, won <0|1>" or "soak FAIL: <why>" (exit 1).
 * LANTERNFALL_SOAK_SEED=n plays another run (the bot starts n % 97 ticks late, its wiggles differ).
 * LANTERNFALL_SOAK_INJECT=1 puts a NaN into the player's speed at minute 1: the checks must catch it (the control). */
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

/* LANTERNFALL_SOAK_TIMELINE=1: the seconds between meaningful events as the bot plays (the fun measure in
 * internal notes): a new region, a special room entered the first time (a shrine, an item room, an arena, a gate, a
 * hidden room, a set piece), a new kind of room, a reward taken, a secret opened, a landmark coming into view. */
enum { TL_REGION, TL_ROOM, TL_KIND, TL_REWARD, TL_SECRET, TL_LANDMARK, TL_N };
static const char *const TL_NAME[TL_N] = { "region", "special room", "new room kind", "reward", "secret", "landmark" };
static double tl_t[8192];
static int tl_k[8192], tl_room[8192], tl_n;
static int tl_cur_room;
static uint8_t tl_region[NREGIONS], tl_seen_room[MAXROOMS], tl_kind[256], tl_land[MAXENTS / 8];
static void tl_add(long tick, int k) {
  if (tl_n < 8192) tl_t[tl_n] = (double)tick / 60.0, tl_k[tl_n] = k, tl_room[tl_n] = tl_cur_room, tl_n++;
}
static void tl_tick(const sim *s, long tick) {
  tl_cur_room = s->room;
  for (int k = 0; k < s->nev; k++) {
    const event *e = &s->ev[k];
    if (e->kind == EV_PICKUP) tl_add(tick, TL_REWARD);
    if (e->kind == EV_SECRET) tl_add(tick, TL_SECRET);
  }
  int r = s->room;
  if (r >= 0 && !tl_seen_room[r]) {
    tl_seen_room[r] = 1;
    const room *rm = &s->w->rooms[r];
    if (!tl_region[rm->region]) tl_region[rm->region] = 1, tl_add(tick, TL_REGION);
    int kind = rm->kind | (room_piece(rm) << 4);
    if (rm->kind != RK_PLAIN && rm->kind != RK_SHAFT && rm->kind != RK_HALL) tl_add(tick, TL_ROOM);
    else if (room_piece(rm)) tl_add(tick, TL_ROOM);
    if (!tl_kind[kind & 255]) tl_kind[kind & 255] = 1, tl_add(tick, TL_KIND);
  }
  const player *p = &s->p[0];
  for (int i = 0; i < s->w->nents; i++) { /* landmarks in view (the camera shows about 32 x 18 tiles round the player) */
    const ent *e = &s->w->ents[i];
    if (!ent_is_landmark(e) || e->room != s->room || ((tl_land[i >> 3] >> (i & 7)) & 1)) continue;
    if (fabsf(e->x - p->x) < 15 && fabsf(e->y - p->y) < 8) tl_land[i >> 3] |= (uint8_t)(1 << (i & 7)), tl_add(tick, TL_LANDMARK);
  }
}
static int dcmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static void tl_report(long ticks) {
  int count[TL_N] = { 0 };
  for (int i = 0; i < tl_n; i++) count[tl_k[i]]++;
  static double gaps[8192];
  int ng = 0;
  double last = 0;
  for (int i = 0; i < tl_n; i++) { /* events in the same second are one moment */
    if (tl_t[i] - last >= 1.0 || i == 0) gaps[ng++] = tl_t[i] - last, last = tl_t[i];
  }
  gaps[ng++] = (double)ticks / 60.0 - last;
  qsort(gaps, (size_t)ng, sizeof gaps[0], dcmp);
  int over60 = 0, over120 = 0;
  for (int i = 0; i < ng; i++) over60 += gaps[i] > 60, over120 += gaps[i] > 120;
  printf("timeline: %.1f game minutes, %d moments;", (double)ticks / 3600.0, ng);
  for (int k = 0; k < TL_N; k++) printf(" %s %d,", TL_NAME[k], count[k]);
  printf("\ntimeline gaps (s): median %.1f, p75 %.1f, p90 %.1f, max %.1f; over 60 s: %d, over 120 s: %d\n", gaps[ng / 2], gaps[ng * 3 / 4], gaps[ng * 9 / 10],
         gaps[ng - 1], over60, over120);
  /* the same for the moments that are rewards or secrets alone (what the player gets for exploring) */
  ng = 0, last = 0;
  for (int i = 0; i < tl_n; i++)
    if (tl_k[i] == TL_REWARD || tl_k[i] == TL_SECRET) gaps[ng++] = tl_t[i] - last, last = tl_t[i];
  gaps[ng++] = (double)ticks / 60.0 - last;
  qsort(gaps, (size_t)ng, sizeof gaps[0], dcmp);
  over60 = over120 = 0;
  for (int i = 0; i < ng; i++) over60 += gaps[i] > 60, over120 += gaps[i] > 120;
  printf("timeline reward/secret gaps (s): median %.1f, p75 %.1f, p90 %.1f, max %.1f; over 60 s: %d, over 120 s: %d\n", gaps[ng / 2], gaps[ng * 3 / 4],
         gaps[ng * 9 / 10], gaps[ng - 1], over60, over120);
  if (getenv("LANTERNFALL_SOAK_TIMELINE_ALL"))
    for (int i = 0; i < tl_n; i++) printf("  %8.1f %s (room %d, %s)\n", tl_t[i], TL_NAME[tl_k[i]], tl_room[i], tl_room[i] >= 0 ? REGIONS[W.rooms[tl_room[i]].region].name : "-");
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: lanternfall_soak <assets dir> <minutes> [players]\n"); return 2; }
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
  S.assist = getenv("LANTERNFALL_ASSIST") ? atoi(getenv("LANTERNFALL_ASSIST")) : 3;
  bool inject = getenv("LANTERNFALL_SOAK_INJECT") != NULL;
  bool timeline = getenv("LANTERNFALL_SOAK_TIMELINE") != NULL;
  long tl_ticks = 0;
  /* LANTERNFALL_SOAK_SEED=n: another run of the same world (the sim has no randomness of its own): the bot waits n % 97
   * ticks before it starts, so every enemy and warden meets it at another point of its cycle, and its wiggles go
   * another way */
  unsigned seed = getenv("LANTERNFALL_SOAK_SEED") ? (unsigned)atoi(getenv("LANTERNFALL_SOAK_SEED")) : 0;
  if (seed) B.seed = 99 + seed * 7919u;
  long first_rss = 0;
  int last_i = 0, flat_minutes = 0;
  for (long tick = 0; tick < (long)minutes * 3600 && !S.won; tick++) {
    pinput in[MAXP];
    memset(in, 0, sizeof in);
    static int goal_i; /* (the step the goal trace last looked at) */
    if (tick >= (long)(seed % 97)) bot_input(&B, &S, 0, &in[0]);
    for (int i = 1; i < S.players; i++) bot_follow(&S, i, &in[i]);
    static int prev_i;
    int i_before = B.i;
    sim_step(&S, in);
    if (timeline) tl_tick(&S, tick), tl_ticks = tick + 1;
    if (getenv("LANTERNFALL_SOAK_HURT") && B.i < prev_i - 40) printf("t%ld REGRESS step %d -> %d at %.1f,%.1f room %d mode %d deaths %d\n", tick, prev_i, B.i, S.p[0].x, S.p[0].y, S.room, B.mode, S.deaths);
    prev_i = i_before > B.i ? i_before : B.i;
    if (getenv("LANTERNFALL_SOAK_GOALS") && B.i > goal_i)
      for (int k = goal_i; k < B.i && k < B.n; k++)
        if (B.steps[k].goal) printf("t%ld goal step %d at %d,%d (room %d) pickups %d\n", tick, k, B.steps[k].to % TW, B.steps[k].to / TW, w_room_at(&W, (float)(B.steps[k].to % TW) + 0.5f, (float)(B.steps[k].to / TW) - 0.5f), sim_finds(&S));
    if (B.i > goal_i) goal_i = B.i;
    for (int k = 0; getenv("LANTERNFALL_SOAK_HURT") && k < S.nev; k++)
      if (S.ev[k].kind == EV_HURT || S.ev[k].kind == EV_DIE) {
        printf("t%ld %s hp %d at %.1f,%.1f room %d state %d:", tick, S.ev[k].kind == EV_DIE ? "DIE" : "hurt", S.p[0].hp, S.p[0].x, S.p[0].y, S.room, S.p[0].state);
        for (int j = 0; j < S.nen; j++) if (S.en[j].alive && fabsf(S.en[j].x - S.p[0].x) < 12) printf(" en%d(k%d st%d %.1f,%.1f)", j, S.en[j].kind, S.en[j].state, S.en[j].x, S.en[j].y);
        for (int j = 0; j < MAXSHOTS; j++) if (S.sh[j].alive && S.sh[j].enemy && fabsf(S.sh[j].x - S.p[0].x) < 3) printf(" shot k%d", S.sh[j].kind);
        if (S.b.active) printf(" BOSS hp %d st %d mv %d at %.1f,%.1f t%d", S.b.hp, S.b.state, S.b.move, S.b.x, S.b.y, S.b.t);
        printf(" bot mode %d step %d atkcd %d facing %d\n", B.mode, B.i, S.p[0].atk_cd, S.p[0].facing_left);
      }
    if (getenv("LANTERNFALL_SOAK_TRACE") && tick < atoi(getenv("LANTERNFALL_SOAK_TRACE")) && tick > (getenv("LANTERNFALL_SOAK_FROM") ? atoi(getenv("LANTERNFALL_SOAK_FROM")) : 0)) {
      for (int j = 0; j < S.nen; j++) if (S.en[j].alive && fabsf(S.en[j].x - S.p[0].x) < (getenv("LANTERNFALL_SOAK_FAR") ? 40 : 6)) printf("   en%d k%d st%d t%d %.2f,%.2f\n", j, S.en[j].kind, S.en[j].state, S.en[j].t, S.en[j].x, S.en[j].y);
      const route_step *st = &B.steps[B.i < B.n ? B.i : B.n - 1];
      printf("t%ld mode %d mv %d t %d ph %d mend %d hp %d en %d step %d (%d,%d -> %d,%d mv %d) np %d p %.2f,%.2f v %.2f,%.2f g%d st%d in mx%d j%d a%d my%d cd%d hs%d\n", tick, B.mode, B.mv, B.t, B.phase, S.p[0].mend_t, S.p[0].hp, S.p[0].en, B.i, st->from % TW, st->from / TW, st->to % TW, st->to / TW, st->move, B.np, S.p[0].x, S.p[0].y, S.p[0].vx, S.p[0].vy, S.p[0].on_ground, S.p[0].state, in[0].mx, in[0].jump, in[0].attack, in[0].my, S.p[0].atk_cd, S.hitstop);
      if (S.b.active) printf("   boss st%d mv%d t%d x %.2f y %.2f\n", S.b.state, S.b.move, S.b.t, S.b.x, S.b.y);
    }
    if (getenv("LANTERNFALL_SOAK_TIMELINE")) { /* the moments a player notices, by game second (the diagnosis) */
      static uint8_t seen_room[MAXROOMS], seen_kind[16];
      static int last_room = -1;
      double sec = (double)tick / 60.0;
      if (S.room != last_room && S.room >= 0) {
        last_room = S.room;
        const room *rm = &W.rooms[S.room];
        if (!seen_room[S.room]) {
          seen_room[S.room] = 1;
          int ne = 0;
          for (int j = 0; j < S.nen; j++) ne += S.en[j].alive;
          printf("T %.1f room %d kind %d region %d enemies %d%s\n", sec, S.room, rm->kind, rm->region, ne, seen_kind[rm->kind] ? "" : " NEWKIND");
          seen_kind[rm->kind] = 1;
          static uint8_t seen_foe[EN_KINDS];
          for (int j = 0; j < S.nen; j++)
            if (S.en[j].alive && !seen_foe[S.en[j].kind]) seen_foe[S.en[j].kind] = 1, printf("T %.1f newfoe %d 0\n", sec, S.en[j].kind);
        }
      }
      for (int k = 0; k < S.nev; k++) {
        const event *e = &S.ev[k];
        const char *nm = e->kind == EV_KILL ? "kill" : e->kind == EV_PICKUP ? "pickup" : e->kind == EV_SAVE ? "shrine" : e->kind == EV_SECRET ? "secret"
                         : e->kind == EV_BOSS_START ? "warden" : e->kind == EV_BOSS_DOWN ? "warden_down" : e->kind == EV_HURT ? "hurt" : e->kind == EV_DIE ? "die"
                         : e->kind == EV_BREAK ? "break" : e->kind == EV_ENEMY_TELL ? "tell" : e->kind == EV_HIT ? "hit" : e->kind == EV_CHARM ? "charm"
                         : e->kind == EV_TRIAL ? "trial" : e->kind == EV_TRIAL_DONE ? "trial_done" : e->kind == EV_BOSS_PHASE ? "phase" : NULL;
        if (nm) printf("T %.1f %s %d %d\n", sec, nm, e->a, e->b);
      }
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
      for (int k = 0; k < PK_KINDS; k++) got += sim_pickups_got(&S, k);
      long rss = rss_kb();
      if (!first_rss) first_rss = rss;
      printf("minute %ld: step %d/%d, pickups %d/%d, wardens %d, rooms %d/%d, abilities %02x, deaths %d, recoveries %d, stuck %d, rss %ld kB (first minute %ld)\n",
             tick / 3600 + 1, B.i, B.n, got, W.npickups, __builtin_popcount(S.bosses_down), seen, W.nrooms, S.abilities, S.deaths, B.recoveries,
             B.stuck, rss, first_rss);
      fflush(stdout);
      flat_minutes = B.i == last_i ? flat_minutes + 1 : 0;
      last_i = B.i;
      if (flat_minutes >= 4) {
        printf("soak FAIL: the bot made no progress for 4 minutes at step %d (room %d, at %.1f,%.1f; trial %d wave %d t %d; boss %d)\n", B.i, S.room, S.p[0].x, S.p[0].y, S.trial_room, S.trial_wave, S.trial_t, S.b.active);
        for (int k = 0; k < W.nents; k++) /* the finds it passed by */
          if (W.ents[k].kind == EK_PICKUP && !sim_got(&S, W.ents[k].id)) printf("  not taken: pickup %d kind %d in room %d at %.1f,%.1f\n", W.ents[k].id, W.ents[k].sub, W.ents[k].room, W.ents[k].x, W.ents[k].y);
        for (int j = 0; j < S.nen; j++) if (S.en[j].alive) printf("  enemy %d kind %d state %d at %.1f,%.1f hp %d\n", j, S.en[j].kind, S.en[j].state, S.en[j].x, S.en[j].y, S.en[j].hp);
        for (int y = (int)S.p[0].y - 8; y <= (int)S.p[0].y + 5; y++) {
          for (int x = (int)S.p[0].x - 20; x <= (int)S.p[0].x + 20; x++) putchar(x == (int)S.p[0].x && y == (int)S.p[0].y - 1 ? '@' : " #=^~%?BS"[w_tile(&W, x, y) == S.t[y][x] ? S.t[y][x] : S.t[y][x]]);
          printf(" %d\n", y);
        }
        return 1;
      }
    }
  }
  int got = 0;
  for (int k = 0; k < PK_KINDS; k++) got += sim_pickups_got(&S, k);
  if (timeline) tl_report(tl_ticks);
  for (int k = 0; k < W.nents && getenv("LANTERNFALL_SOAK_GOALS"); k++)
    if (W.ents[k].kind == EK_PICKUP && !sim_got(&S, W.ents[k].id)) printf("  not taken: pickup %d kind %d in room %d at %.1f,%.1f\n", W.ents[k].id, W.ents[k].sub, W.ents[k].room, W.ents[k].x, W.ents[k].y);
  printf("soak ok: %d minutes, %d pickups, %d wardens, won %d\n", minutes, got, __builtin_popcount(S.bosses_down), S.won ? 1 : 0);
  free(rb);
  return 0;
}
