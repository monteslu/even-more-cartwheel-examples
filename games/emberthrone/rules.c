/* emberthrone: the combat feel rules, measured on the real rules and the real world (emberthrone_auto with ADV_RULES=1,
 * natively; tests/emberthrone-rules.mjs runs it). Each check sets up a small scene in the open meadow (the player, one
 * or a few foes, everyone else switched off), plays it through sim_step and prints one line: the name, what it
 * measured, its bound, ok or FAIL. Then every check runs again with its own rule switched off (g_rules_force), and
 * that check must FAIL: a check nothing can fail is not a check.
 *
 *   hitstop   a cut that lands holds the world 4 ticks: the cutter's swing and the foe stand still through it, and a
 *             press made in the hold is kept (the next cut is queued)
 *   ladder    the hold grows with the blow: a cut 4, the third cut 5, the maul 6, a blow taken 6, a broken poise 8,
 *             a critical 9, a boss's phase 10
 *   knock     the push follows the blow: the third cut moves a foe at least 1.3 times as far as the first, the maul's
 *             swing 1.2 times as far again
 *   crit      a foe whose poise broke takes double damage from the next blow
 *   magnet    the first cut steps toward a locked foe out of plain reach and lands (and toward the nearest one in front
 *             with no lock)
 *   guard     a shield bearer turns a cut from the front (no harm) but not from behind; a heavy knocks the shield aside
 *   stones    a slinger's stones hit a player who stands still and miss one who keeps moving
 *   flank     foes waiting their turn circle round to the player's back (most of the waiting time behind the shoulder)
 *   spent     a boss stands spent after a big blow (no blow for a good while) and staggers sooner then
 *   string    in its later phase a boss chains blows: the next wind-up starts within a few ticks of the last blow
 *   clear     beating a whole group puts a sip back in the flask
 *   lurk      a lurker under the water cannot be targeted, and rises when the player wades close
 *   rumble    a landed cut asks the pad for rumble */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

extern bool g_bench;
static int fails;
static char failed[512];
static bool quiet;
static char want_name[24], want_detail[256]; /* a control's own check: what it measured */

static void check(const char *name, bool ok, const char *fmt, ...) {
  char b[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  if (!quiet) printf("%-8s %-4s %s\n", name, ok ? "ok" : "FAIL", b);
  if (!strcmp(name, want_name)) snprintf(want_detail, sizeof want_detail, "%s", b);
  if (!ok) {
    fails++;
    if (strlen(failed) + strlen(name) + 2 < sizeof failed) strcat(failed, name), strcat(failed, " ");
  }
}

/* ── a scene ──────────────────────────────────────────────────────── */
static const float AX = 172, AZ = 268; /* an open stretch of the meadow by the road */
static input IN[MAXP];

static float ground(float x, float z) { return world_ground(x, z, world_height(x, z) + 4) + 0.05f; }
static void put(actor *a, float x, float z, float yaw) {
  cw_mover3d_teleport(&a->mv, (t3_vec3){ x, ground(x, z), z });
  a->mv.velocity = (t3_vec3){ 0, 0, 0 };
  a->yaw = a->yaw_prev = yaw;
}
/* a fresh world with only the player, standing at the arena facing +z (north) */
static void fresh(bool passive) {
  sim_init(1, NULL);
  for (int i = MAXP; i < MAXA; i++) S.a[i].used = false;
  g_foe_passive = passive;
  g_bench = false;
  put(&S.a[0], AX, AZ, 0);
  S.cam_yaw = CW_PI, S.cam_pitch = -0.28f, S.cam_idle = 0;
  memset(IN, 0, sizeof IN);
}
/* a foe of this kind and role (the first the world places), dz metres in front of the player and dx to its right */
static actor *foe_at(int kind, int role, float dx, float dz, float yaw, int nth) {
  for (int f = 0, seen = 0; f < g_nfoe; f++) {
    if (g_foe[f].kind != kind || g_foe[f].role != role || seen++ < nth) continue;
    actor *e = &S.a[MAXP + f];
    if (!e->used && S.a[MAXP + f].kind != kind) continue;
    e->used = true, e->active = true;
    e->region = RG_MEADOW;
    float x = AX - dx, z = AZ + dz; /* facing +z, the right hand is -x */
    put(e, x, z, yaw);
    e->home_x = x, e->home_z = z, e->home_y = e->mv.position.y, e->home_yaw = yaw;
    e->state = ST_IDLE_HOME, e->st_t = 0;
    e->hp = e->hp_max, e->poise = e->poise_max, e->guard = role == R_SHIELD ? 60 : 0;
    return e;
  }
  printf("rules: no foe of kind %d role %d in the world\n", kind, role);
  exit(2);
}
static void step(int n) {
  for (int k = 0; k < n; k++) {
    sim_step(IN);
    fx_feel();
    memset(IN, 0, sizeof IN); /* presses last one tick */
  }
}
static float moved(const actor *a, t3_vec3 from) { return sqrtf(dist2(a->mv.position.x, a->mv.position.z, from.x, from.z)); }

/* ── the checks ───────────────────────────────────────────────────── */
static void t_hitstop(void) {
  fresh(true);
  actor *e = foe_at(K_RAIDER, R_PLAIN, 0, 1.9f, CW_PI, 0);
  int r0 = g_rumble_n;
  IN[0].attack = true;
  int k = 0;
  while (S.hits_landed == 0 && k++ < 40) step(1);
  int hold = S.hitstop, pst = S.a[0].st_t;
  t3_vec3 ep = e->mv.position;
  int held = 0;
  bool still = true;
  for (int i = 0; i < 12 && S.hitstop > 0; i++) {
    if (i == 1) IN[0].attack = true; /* pressed in the hold */
    step(1);
    held++;
    still = still && S.a[0].st_t == pst && moved(e, ep) < 1e-4f;
  }
  step(1);
  check("hitstop", S.hits_landed == 1 && hold == 4 && held == 4 && still && S.a[0].queued,
        "cut landed %d, held %d ticks (4), both still %s, press in the hold kept %s", S.hits_landed, held, still ? "yes" : "no", S.a[0].queued ? "yes" : "no");
  check("rumble", g_rumble_n > r0, "rumble requests on the landed cut: %d", g_rumble_n - r0);
}

static int hold_of(int kind, int victim_is_player, int atk, int setup) {
  fresh(true);
  actor *e = foe_at(kind, R_PLAIN, 0, 1.6f, CW_PI, 0);
  if (setup == 1) e->poise = 1;       /* the blow breaks its poise */
  if (setup == 2) e->crit_t = 30;     /* an opening: the blow is a critical */
  if (setup == 3) e->poise = 999, e->hp = 9999;
  S.hitstop = 0;
  if (victim_is_player) sim_test_blow(0, e->idx, atk);
  else sim_test_blow(e->idx, 0, atk);
  return S.hitstop;
}
static void t_ladder(void) {
  int cut = hold_of(K_RAIDER, 0, A_SLASH1, 3), third = hold_of(K_RAIDER, 0, A_SLASH3, 3), maul = hold_of(K_RAIDER, 0, A_MAUL1, 3);
  int taken = hold_of(K_RAIDER, 1, A_CLEAVE, 0), broke = hold_of(K_RAIDER, 0, A_SLASH1, 1), crit = hold_of(K_RAIDER, 0, A_SLASH1, 2);
  /* a boss's phase: the Thornwarden brought under two thirds */
  fresh(true);
  g_foe_passive = false, g_bench = true;
  actor *b = foe_at(K_THORN, R_PLAIN, 0, 6, CW_PI, 0);
  b->aggro = true, b->target = 0, b->state = ST_MOVE;
  S.boss = b->idx;
  b->hp = b->hp_max / 2;
  int phase = 0;
  for (int i = 0; i < 4 && !phase; i++) { step(1); phase = S.hitstop; }
  bool ok = cut == 4 && third == 5 && maul == 6 && taken == 6 && broke == 8 && crit == 9 && phase == 10;
  check("ladder", ok, "cut %d third %d maul %d taken %d poise broken %d critical %d phase %d (4 5 6 6 8 9 10)", cut, third, maul, taken, broke, crit, phase);
}

static float push_of(int atk) {
  fresh(true);
  actor *e = foe_at(K_RAIDER, R_PLAIN, 0, 1.6f, CW_PI, 0);
  e->poise = 999, e->hp = 9999;
  t3_vec3 p0 = e->mv.position;
  sim_test_blow(e->idx, 0, atk);
  step(50);
  return moved(e, p0);
}
static void t_knock(void) {
  float a = push_of(A_SLASH1), b = push_of(A_SLASH3), c = push_of(A_MAUL1);
  check("knock", b >= a * 1.3f && c >= b * 1.2f && a > 0.05f, "pushed by the first cut %.2f m, the third %.2f m (%.2fx, 1.3), the maul %.2f m (%.2fx, 1.2)", a, b, b / fmaxf(a, 1e-3f), c,
        c / fmaxf(b, 1e-3f));
}

static void t_crit(void) {
  fresh(true);
  actor *e = foe_at(K_RAIDER, R_PLAIN, 0, 1.6f, CW_PI, 0);
  e->hp = 9999;
  int h0 = e->hp;
  sim_test_blow(e->idx, 0, A_SLASH1);
  int plain = h0 - e->hp;
  e->poise = 1;
  sim_test_blow(e->idx, 0, A_SLASH1); /* breaks the poise: the opening */
  step(12);
  int h1 = e->hp;
  sim_test_blow(e->idx, 0, A_SLASH1);
  int opened = h1 - e->hp;
  check("crit", opened == plain * 2 && S.crits == 1, "a cut %d, the same cut in the opening %d (twice), criticals %d", plain, opened, S.crits);
}

static bool cut_at(float dz, bool lock) {
  fresh(true);
  foe_at(K_RAIDER, R_PLAIN, 0, dz, CW_PI, 0);
  if (lock) { IN[0].lock = true; step(1); }
  IN[0].attack = true;
  step(40);
  return S.hits_landed > 0;
}
static void t_magnet(void) {
  bool locked = cut_at(3.4f, true), free = cut_at(3.3f, false), close = cut_at(2.0f, false);
  check("magnet", locked && free && close, "a cut at a locked foe 3.4 m off lands %s, at the nearest in front 3.3 m off %s (at 2 m %s)", locked ? "yes" : "no", free ? "yes" : "no", close ? "yes" : "no");
}

static void t_guard(void) {
  fresh(true);
  actor *e = foe_at(K_RAIDER, R_SHIELD, 0, 1.6f, CW_PI, 0); /* facing the player */
  int h0 = e->hp;
  sim_test_blow(e->idx, 0, A_SLASH1);
  int front = h0 - e->hp, blocked = S.blocked;
  fresh(true);
  e = foe_at(K_RAIDER, R_SHIELD, 0, 1.6f, 0, 0); /* its back to the player */
  h0 = e->hp;
  sim_test_blow(e->idx, 0, A_SLASH1);
  int back = h0 - e->hp;
  fresh(true);
  e = foe_at(K_RAIDER, R_SHIELD, 0, 1.6f, CW_PI, 0);
  sim_test_blow(e->idx, 0, A_HEAVY);
  bool knocked = S.guard_breaks == 1 && e->guard <= 0 && e->state == ST_HURT && e->crit_t > 0;
  check("guard", front == 0 && blocked == 1 && back > 0 && knocked, "a cut from the front does %d (blocked %d), from behind %d; a heavy knocks the shield aside %s", front, blocked, back,
        knocked ? "yes" : "no");
}

static void stones_run(bool move, int *thrown, int *hit) {
  fresh(false);
  g_bench = true;
  actor *e = foe_at(K_RAIDER, R_SLINGER, 0, 9, CW_PI, 0);
  (void)e;
  for (int k = 0; k < 600; k++) {
    if (move) { /* circle round it, keeping the distance: the stick to the side of the line to it, a little in */
      float dx = e->mv.position.x - S.a[0].mv.position.x, dz = e->mv.position.z - S.a[0].mv.position.z, l = sqrtf(dx * dx + dz * dz) + 1e-3f;
      float wx = -dz / l + dx / l * (l - 9) * 0.3f, wz = dx / l + dz / l * (l - 9) * 0.3f;
      float yaw = S.cam_yaw, sy = cw_sin(yaw), cy = cw_cos(yaw);
      IN[0].mx = cy * wx - sy * wz, IN[0].my = sy * wx + cy * wz;
    }
    S.a[0].hp = S.a[0].hp_max;
    sim_step(IN);
    fx_feel();
    IN[0].attack = IN[0].dodge = false;
  }
  *thrown = S.stones_thrown, *hit = S.stones_hit;
}
static void t_stones(void) {
  int t0, h0, t1, h1;
  stones_run(false, &t0, &h0);
  stones_run(true, &t1, &h1);
  check("stones", t0 >= 3 && h0 * 2 >= t0 && t1 >= 3 && h1 * 4 <= t1, "standing still: %d of %d stones hit (half or more); moving: %d of %d (a quarter or fewer)", h0, t0, h1, t1);
}

static void t_flank(void) {
  fresh(false);
  g_bench = true;
  actor *f[4];
  for (int i = 0; i < 4; i++) f[i] = foe_at(K_RAIDER, R_PLAIN, (float)(i - 1.5f) * 2.0f, 7, CW_PI, i);
  for (int i = 0; i < 4; i++) f[i]->group = 99;
  int behind = 0, waiting = 0, side = 0, starts = 0, prev[4] = { 0 };
  float mean = 0;
  for (int k = 0; k < 1800; k++) {
    S.a[0].hp = S.a[0].hp_max;
    S.a[0].yaw = 0; /* the player holds its facing (north) */
    sim_step(IN);
    for (int i = 0; i < 4; i++) {
      float a = fabsf(wrapf(cw_atan2(f[i]->mv.position.x - S.a[0].mv.position.x, f[i]->mv.position.z - S.a[0].mv.position.z) - S.a[0].yaw));
      if (f[i]->state == ST_ATTACK && prev[i] != ST_ATTACK) starts++, side += a > CW_PI / 3;
      prev[i] = f[i]->state;
      if (k < 120 || f[i]->state != ST_MOVE || f[i]->token) continue;
      waiting++, behind += a > CW_PI / 2, mean += a;
    }
  }
  float k = waiting ? (float)behind / (float)waiting : 0, s = starts ? (float)side / (float)starts : 0;
  check("flank", starts >= 8 && s >= 0.5f, "blows from beyond the player's shoulder %.0f%% of %d (half or more); waiting behind %.0f%%, mean angle %.2f", s * 100, starts, k * 100, waiting ? mean / waiting : 0);
}

/* a boss fight against an untouchable player at arm's length: the ticks between its blows */
static void boss_run(int kind, float hp_k, int *spent_max, int *chain, int *big_gap_ok) {
  fresh(false);
  g_bench = true;
  actor *b = foe_at(kind, R_PLAIN, 0, 3.0f, CW_PI, 0);
  b->hp = (int)((float)b->hp_max * hp_k);
  b->aggro = true, b->target = 0, b->state = ST_MOVE;
  b->phase = hp_k < 0.66f ? 1 : 0;
  S.boss = b->idx;
  int last_end = -1, quick = 0, smax = 0, prev = ST_MOVE;
  bool open_ok = true;
  for (int k = 0; k < 2400; k++) {
    S.a[0].hp = S.a[0].hp_max;
    sim_step(IN);
    if (b->state == ST_ATTACK && prev != ST_ATTACK) {
      if (last_end >= 0 && (int)S.tick - last_end <= 8) quick++;
      if (smax > 0 && b->spent > 0) open_ok = false; /* a blow started while spent */
    }
    if (b->state != ST_ATTACK && prev == ST_ATTACK) last_end = (int)S.tick;
    if (b->spent > smax) smax = b->spent;
    prev = b->state;
  }
  *spent_max = smax, *chain = quick, *big_gap_ok = open_ok;
}
static void t_spent(void) {
  int smax, chain, open;
  boss_run(K_EMBER, 0.9f, &smax, &chain, &open);
  fresh(true);
  actor *b = foe_at(K_EMBER, R_PLAIN, 0, 2.5f, CW_PI, 0);
  b->spent = 30;
  int p0 = b->poise;
  sim_test_blow(b->idx, 0, A_SLASH1);
  int took = p0 - b->poise;
  check("spent", smax >= 40 && open && took == ATK[A_SLASH1].poise * 2, "spent after a big blow for %d ticks (40), no blow while spent %s, poise taken while spent %d (twice %d)", smax,
        open ? "yes" : "no", took, ATK[A_SLASH1].poise);
}
static void t_string(void) {
  int smax, chain, open;
  boss_run(K_EMBER, 0.5f, &smax, &chain, &open);
  int c0 = chain;
  boss_run(K_EMBER, 0.9f, &smax, &chain, &open);
  check("string", c0 >= 2 && chain == 0, "blows chained within 8 ticks: %d in the later phase (2 or more), %d in the first (none)", c0, chain);
}

static void t_clear(void) {
  fresh(true);
  actor *a = foe_at(K_RAIDER, R_PLAIN, -1, 2, CW_PI, 0), *b = foe_at(K_RAIDER, R_PLAIN, 1, 2, CW_PI, 1);
  a->group = b->group = 98;
  S.a[0].tonics = 1;
  a->hp = 1;
  sim_test_blow(a->idx, 0, A_SLASH1);
  int mid = S.a[0].tonics;
  b->hp = 1;
  sim_test_blow(b->idx, 0, A_SLASH1);
  check("clear", mid == 1 && S.a[0].tonics == 2 && S.clears == 1, "flask after the first of two falls %d, after the last %d (one more)", mid, S.a[0].tonics);
}

static void t_lurk(void) {
  fresh(false);
  g_bench = true;
  actor *e = foe_at(K_LANCER, R_LURKER, 0, 10, CW_PI, 0);
  step(2);
  bool unseen = sim_find_target(0, 0) < 0;
  /* wade in */
  int k = 0;
  while (e->state == ST_IDLE_HOME && k++ < 400) { IN[0].my = -0.6f; step(1); }
  int rose = e->state == ST_ROAR;
  step(60);
  bool fights = e->state != ST_IDLE_HOME && sim_find_target(0, 0) == e->idx;
  check("lurk", unseen && rose && fights, "under the water it cannot be locked %s, rises as the player nears %s (at %.1f m), then fights %s", unseen ? "yes" : "no", rose ? "yes" : "no",
        sqrtf(dist2(e->mv.position.x, e->mv.position.z, S.a[0].mv.position.x, S.a[0].mv.position.z)), fights ? "yes" : "no");
}

/* ── the run ──────────────────────────────────────────────────────── */
typedef struct { const char *name; void (*fn)(void); int rule; } rcheck;
static const rcheck CHECKS[] = {
  { "hitstop", t_hitstop, FR_HITSTOP }, { "ladder", t_ladder, FR_HITSTOP }, { "knock", t_knock, FR_KNOCK }, { "crit", t_crit, FR_CRIT },
  { "magnet", t_magnet, FR_MAGNET },    { "guard", t_guard, FR_GUARD },     { "stones", t_stones, FR_STONES }, { "flank", t_flank, FR_FLANK },
  { "spent", t_spent, FR_SPENT },       { "string", t_string, FR_STRING }, { "clear", t_clear, FR_CLEAR },   { "lurk", t_lurk, FR_LURK },
  { "rumble", t_hitstop, FR_RUMBLE },
};

int rules_run(void) {
  const char *only = getenv("ADV_RULES_ONLY");
  printf("rules: the arena at %.0f,%.0f (ground %.2f)\n", AX, AZ, ground(AX, AZ));
  int n = (int)(sizeof CHECKS / sizeof *CHECKS);
  /* every check with every rule on (the rumble line comes from the hitstop scene) */
  g_rules_force = 0;
  for (int i = 0; i < n; i++)
    if (CHECKS[i].fn != t_hitstop || !strcmp(CHECKS[i].name, "hitstop"))
      if (!only || strstr(only, CHECKS[i].name)) CHECKS[i].fn();
  int bad = fails;
  printf("rules %s\n", bad ? "FAIL" : "ok");
  /* the controls: each rule off, its own check must fail */
  int ctl_bad = 0;
  for (int i = 0; i < n; i++) {
    if (only && !strstr(only, CHECKS[i].name)) continue;
    g_rules_force = CHECKS[i].rule;
    fails = 0, failed[0] = 0, quiet = true;
    snprintf(want_name, sizeof want_name, "%s", CHECKS[i].name), want_detail[0] = 0;
    CHECKS[i].fn();
    quiet = false;
    char want[24];
    snprintf(want, sizeof want, "%s ", CHECKS[i].name);
    bool caught = strstr(failed, want) != NULL;
    printf("control  %-4s %s off: the %s check %s (%s)\n", caught ? "ok" : "FAIL", CHECKS[i].name, CHECKS[i].name, caught ? "fails as it must" : "PASSED (a check nothing can fail)", want_detail);
    ctl_bad += !caught;
  }
  g_rules_force = -1;
  printf("controls %s\n", ctl_bad ? "FAIL" : "ok");
  fflush(stdout);
  return bad || ctl_bad ? 1 : 0;
}
