/* skate_check: the simulation's checks, native, no window and no GPU (tests/skate-*.mjs run it).
 *
 *   skate_check phys            the board controller against ramp physics: a quarter pipe's apex is v^2 / 2g and it
 *                               gives the speed back; no hop on a transition; vert air comes back down onto the ramp;
 *                               no shot passes through a landing slope at mega ramp speeds; a rail snaps the board within
 *                               reach and not past it. One JSON line per solver: "game" (substeps, swept contact, smooth
 *                               normals) and "naive" (one discrete step a tick, faceted normals, no sweep: what a rigid
 *                               body stepped once a tick on the mesh does), the control that must fail.
 *   skate_check parks [n]       every park (or park n): nothing floats (each piece stands on what is under it) and every
 *                               line is skateable (a bot skates it through the real controls and does its feature: the
 *                               grind, the gap, the air, the manual). Then the controls: a piece lifted, a wall across a
 *                               rail's approach, a kicker too short for its gap: each must fail.
 *   skate_check soak <min> <players> [park]
 *                               bots skate every park in turn for <min> game minutes: no NaN, nobody leaves the park or
 *                               falls through it, memory flat. SKATE_SOAK_INJECT=1 puts a NaN in a velocity (the control). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <unistd.h>
#endif

#include "game.h"

/* ── test parks ─────────────────────────────────────────────────────────── */

static const float GREY[3] = { 0.7f, 0.7f, 0.7f };

/* flat ground and one quarter pipe facing the rider (who rides toward -z from z 0) */
static void test_quarter(park *k, float r, float h) {
  kit b;
  kit_begin(&b, k);
  kit_piece(&b);
  kit_ground(&b, -20, -40, 20, 30, NULL, 0, MAT_CONCRETE, GREY);
  kit_piece(&b);
  profile p;
  prof_quarter(&p, r, h, 14);
  ramp_opts o;
  memset(&o, 0, sizeof o);
  o.deck = 3, o.mat_face = MAT_CONCRETE, o.mat_side = MAT_PAINT, o.coping = true, o.run = -1;
  memcpy(o.face, GREY, 12), memcpy(o.side, GREY, 12), memcpy(o.deck_col, GREY, 12);
  kit_ramp(&b, 0, -10, 0, 20, &p, &o);
  park_finish(k);
}
/* a landing slope (a bank 30 degrees, 8 m high) and the ground */
static void test_slope(park *k) {
  kit b;
  kit_begin(&b, k);
  kit_piece(&b);
  kit_ground(&b, -40, -60, 40, 60, NULL, 0, MAT_CONCRETE, GREY);
  kit_piece(&b);
  profile p;
  prof_bank(&p, 14, 8, 3);
  ramp_opts o;
  memset(&o, 0, sizeof o);
  o.mat_face = MAT_CONCRETE, o.mat_side = MAT_PAINT, o.run = -1, o.no_back = false, o.deck = 4;
  memcpy(o.face, GREY, 12), memcpy(o.side, GREY, 12), memcpy(o.deck_col, GREY, 12);
  kit_ramp(&b, 0, 20, 0, 30, &p, &o); /* rises toward -z: the descent faces +z */
  park_finish(k);
}
static void test_rail(park *k) {
  kit b;
  kit_begin(&b, k);
  kit_piece(&b);
  kit_ground(&b, -30, -30, 30, 30, NULL, 0, MAT_CONCRETE, GREY);
  kit_piece(&b);
  kit_rail_on(&b, V(0, 0.5f, -8), V(0, 0.5f, 8), 0, 0, GREY);
  park_finish(k);
}

static input NOIN;

/* ride into a quarter pipe at v0, no friction, no input: the apex, the speed coming back, air ticks on the way */
static void quarter_shot(float r, float h, float v0, float *apex, float *back, int *air, int *bail, int *deck) {
  park k;
  memset(&k, 0, sizeof k);
  test_quarter(&k, r, h);
  skater s;
  memset(&s, 0, sizeof s);
  sk_place(&s, &k, V(0, 0, 0), 0, v0);
  *apex = 0, *back = 0, *air = 0, *bail = 0, *deck = 0;
  bool went_up = false;
  for (int t = 0; t < TPS * 12; t++) {
    sk_step(&s, &k, &NOIN);
    if (s.p.y > *apex) *apex = s.p.y;
    if (s.st == SK_AIR && !s.vert) (*air)++;
    if (s.st == SK_BAIL) { *bail = 1; break; }
    if (s.p.y > 0.5f) went_up = true;
    if (s.st == SK_ROLL && s.p.y > h - 0.05f && s.p.z < -10 - r - 0.1f) *deck = 1;
    if (went_up && s.st == SK_ROLL && s.p.z > -2 && s.v.z > 0) { *back = vlen(s.v); break; }
  }
  park_free(&k);
}

/* shots into a landing slope at speed: count those that end up under it */
static void slope_shots(int *through, int *total, int *bad_land) {
  park k;
  memset(&k, 0, sizeof k);
  test_slope(&k);
  *through = 0, *total = 0, *bad_land = 0;
  const float ZT[3] = { 9, 12, 15 };
  for (int a = 0; a < 6; a++)
    for (int sp = 0; sp < 6; sp++)
      for (int zt = 0; zt < 3; zt++) {
        skater s;
        memset(&s, 0, sizeof s);
        sk_place(&s, &k, V(0, 0, -40), PI_F, 0);
        /* flying down onto the slope's face from just above it, toward +z and down: 12 to 27 m/s, 10 to 55 degrees */
        float speed = 12 + 3.0f * (float)sp, ang = 0.17f + 0.15f * (float)a, sa, ca;
        cw_sincos(ang, &sa, &ca);
        phit h;
        park_ground(&k, V(0, 20, ZT[zt]), 1, 30, &h);
        v3 vd = V(0, -sa, ca);
        s.st = SK_AIR;
        s.p = vadd(vmad(h.p, vd, -1.2f), V(0, 0.6f, 0));
        s.v = vmul(vd, speed);
        s.f = V(0, 0, 1), s.u = V(0, 1, 0);
        s.flip = s.grab = -1;
        s.takeoff = s.p, s.takeoff_y = s.peak_y = s.p.y;
        (*total)++;
        bool under = false;
        for (int t = 0; t < 30 && !under; t++) {
          sk_step(&s, &k, &NOIN);
          /* a ride surface above the point, straight up: it went through */
          if (park_cast(&k, vadd(s.p, V(0, 20, 0)), V(0, -19.95f, 0), 1u << SURF_RIDE, &h) && h.p.y > s.p.y + 0.05f) under = true;
        }
        if (under) (*through)++;
        if (getenv("SKATE_DEBUG")) fprintf(stderr, "shot a%d sp%d z%d: st %d p %.2f %.2f %.2f under %d bails %d\n", a, sp, zt, s.st, (double)s.p.x, (double)s.p.y, (double)s.p.z, under, s.bails);
        if (s.bails) (*bad_land)++;
      }
  park_free(&k);
}

/* air toward a rail from the side at offset off and angle ang: did it lock on? */
static bool rail_shot(float off, float ang) {
  park k;
  memset(&k, 0, sizeof k);
  test_rail(&k);
  skater s;
  memset(&s, 0, sizeof s);
  sk_place(&s, &k, V(off, 0, 10), 0, 0);
  float sa, ca;
  cw_sincos(ang, &sa, &ca);
  s.st = SK_AIR;
  s.p = V(off + 2.0f * sa, 1.2f, -2 + 2.0f * ca);
  s.v = V(-6 * sa, 1.5f, -6 * ca);
  s.f = vnorm(V(s.v.x, 0, s.v.z)), s.u = V(0, 1, 0);
  s.flip = s.grab = -1;
  s.air_t = 5;
  s.takeoff = s.p;
  input in = NOIN;
  in.grind = true;
  bool locked = false;
  for (int t = 0; t < TPS; t++) {
    in.grind = (t & 1) == 0; /* pressed every other tick: a fresh press each time */
    sk_step(&s, &k, &in);
    if (s.st == SK_GRIND) { locked = true; break; }
    if (s.st == SK_ROLL) break;
  }
  park_free(&k);
  return locked;
}

static int cmd_phys(void) {
  for (int solver = 0; solver < 2; solver++) {
    physparams keep = g_phys;
    g_phys.no_friction = true;
    if (solver == 1) g_phys.no_substeps = g_phys.no_sweep = g_phys.no_smooth = true;
    /* energy: tall quarter (no lip), apex and return at several speeds */
    float worst_apex = 0, worst_back = 0;
    int hops = 0, bails = 0;
    char apexes[256] = "";
    size_t u = 0;
    const float SP[5] = { 5, 7, 9, 11, 13 };
    for (int i = 0; i < 5; i++) {
      float apex, back;
      int air, bail, deck;
      quarter_shot(3.0f, 10.0f, SP[i], &apex, &back, &air, &bail, &deck);
      float want = SP[i] * SP[i] / (2 * GRAV);
      float ea = fabsf(apex / want - 1), eb = back > 0 ? fabsf(back / SP[i] - 1) : 1;
      if (ea > worst_apex) worst_apex = ea;
      if (eb > worst_back) worst_back = eb;
      hops += air, bails += bail;
      u += (size_t)snprintf(apexes + u, sizeof apexes - u, "%s[%.2f,%.3f,%.3f]", i ? "," : "", (double)SP[i], (double)apex, (double)want);
    }
    /* vert: a 3.5 m quarter with vert at 10 m/s: out over the coping and back onto the ramp */
    float vapex, vback;
    int vair, vbail, vdeck;
    quarter_shot(3.0f, 3.5f, 10.0f, &vapex, &vback, &vair, &vbail, &vdeck);
    float vwant = 100.0f / (2 * GRAV);
    int through, total, bad_land;
    slope_shots(&through, &total, &bad_land);
    int lock_in = 0, lock_n = 0, lock_out = 0;
    const float OFF[4] = { 0, 0.2f, 0.35f, 0.5f }, ANG[4] = { 0, 0.3f, 0.6f, 0.9f };
    for (int a = 0; a < 4; a++)
      for (int o = 0; o < 4; o++) lock_n++, lock_in += rail_shot(OFF[o], ANG[a]);
    lock_out += rail_shot(0.9f, 0) + rail_shot(1.2f, 0.3f);
    printf("{\"solver\":\"%s\",\"apex_err\":%.4f,\"back_err\":%.4f,\"hop_ticks\":%d,\"bails\":%d,\"apexes\":[%s],\"vert\":{\"apex\":%.3f,\"want\":%.3f,\"back\":%.3f,"
           "\"bail\":%d,\"deck\":%d},\"slope\":{\"through\":%d,\"total\":%d,\"bails\":%d},\"rail\":{\"locked\":%d,\"of\":%d,\"wrong\":%d}}\n",
           solver ? "naive" : "game", (double)worst_apex, (double)worst_back, hops, bails, apexes, (double)vapex, (double)vwant, (double)vback, vbail, vdeck, through,
           total, bad_land, lock_in, lock_n, lock_out);
    g_phys = keep;
  }
  return 0;
}

/* ── parks ──────────────────────────────────────────────────────────────── */

/* a bot on one line: up to `tries` goes (each a different trick plan); 1 when one did the feature */
static int line_ok(int park_id, int variant, int line, int tries, int *ticks) {
  for (int t = 0; t < tries; t++) {
    static sim S;
    memset(&S, 0, sizeof S);
    S.park_id = park_id;
    S.players = 1;
    park_build(&S.k, park_id, variant);
    S.s[0].rng = 777u + (uint32_t)t * 31u;
    sk_spawn(&S.s[0], &S.k, 0);
    S.state = RUN_PLAY;
    bot b;
    bot_init(&b, 99u + (uint32_t)t * 7919u + (uint32_t)line * 13u, 2);
    b.want_line = line;
    input in[MAXP];
    memset(in, 0, sizeof in);
    int ok = 0;
    bool trace = getenv("SKATE_TRACE") != NULL;
    for (int tk = 0; tk < TPS * 40; tk++) {
      bot_tick(&b, &S, 0, &in[0]);
      sim_step(&S, in);
      const skater *s = &S.s[0];
      if (trace && (tk % 4 == 0 || s->ev))
        printf("    t%4d ph%d %-5s p(%6.2f %5.2f %6.2f) v%5.2f vy%5.2f u.y%5.2f in(%4.1f %4.1f %c%c%c%c%c%c) ev%x cb%d air%d spin%.2f\n", tk, b.phase, sk_state_name(s->st), (double)s->p.x,
               (double)s->p.y, (double)s->p.z, (double)vlen(s->v), (double)s->v.y, (double)s->u.y, (double)in[0].sx, (double)in[0].sy, in[0].ollie ? 'O' : '.', in[0].flip ? 'F' : '.',
               in[0].grab ? 'G' : '.', in[0].grind ? 'R' : '.', in[0].manual ? 'M' : '.', in[0].reset ? 'B' : '.', s->ev, s->cb.n, s->air_t, (double)s->spin);
      if (b.lines_done) { ok = 1; *ticks = tk; break; }
      if (b.lines_failed) break;
    }
    park_free(&S.k);
    if (ok) return 1;
  }
  return 0;
}

static int cmd_parks(int only) {
  int bad = 0;
  for (int p = 0; p < PARK_COUNT; p++) {
    if (only >= 0 && p != only) continue;
    park k;
    memset(&k, 0, sizeof k);
    park_build(&k, p, PARKV_GAME);
    char why[4096];
    int fl = park_check_floats(&k, why, sizeof why);
    printf("park %d %s: %d triangles, %d edges, %d lines, %d gaps, %d tokens, %d start points, %d rest points: %d floating\n", p, k.name, k.nt, k.ne, k.nline, k.ngap,
           k.ntoken, k.nspawn, k.nrest, fl);
    if (fl) printf("%s", why), bad++;
    for (int l = 0; l < k.nline; l++) {
      int ticks = 0;
      int ok = line_ok(p, PARKV_GAME, l, 4, &ticks);
      printf("  line %-2d %-16s %s%s (%.1f s)\n", l, k.line[l].name, ok ? "ok" : "FAIL", ok ? "" : ": not skated", (double)ticks / TPS);
      if (!ok) bad++;
    }
    /* the controls */
    park c;
    memset(&c, 0, sizeof c);
    park_build(&c, p, PARKV_FLOAT);
    int cf = park_check_floats(&c, NULL, 0);
    printf("  control float: %d floating %s\n", cf, cf ? "(fails as it must)" : "CONTROL PASSED (a failure)");
    if (!cf) bad++;
    park_free(&c);
    /* the blocked line (a wall across its approach) and the short kicker's gap: each must not be skated */
    int rl = k.ctl_block_line, gl = k.ctl_short_line;
    if (rl >= 0) {
      int ticks;
      int ok = line_ok(p, PARKV_BLOCK, rl, 4, &ticks);
      printf("  control block (%s): %s\n", k.line[rl].name, ok ? "CONTROL PASSED (a failure)" : "fails as it must");
      if (ok) bad++;
    }
    if (gl >= 0) {
      int ticks;
      int ok = line_ok(p, PARKV_SHORT, gl, 4, &ticks);
      printf("  control short kicker (%s): %s\n", k.line[gl].name, ok ? "CONTROL PASSED (a failure)" : "fails as it must");
      if (ok) bad++;
    }
    park_free(&k);
  }
  printf(bad ? "parks FAIL: %d problems\n" : "parks ok\n", bad);
  return bad ? 1 : 0;
}

/* ── soak ───────────────────────────────────────────────────────────────── */

static long rss_kb(void) {
#if defined(__linux__)
  FILE *f = fopen("/proc/self/statm", "r");
  long pages = 0, res = 0;
  if (f) {
    if (fscanf(f, "%ld %ld", &pages, &res) != 2) res = 0;
    fclose(f);
  }
  return res * (sysconf(_SC_PAGESIZE) / 1024);
#else
  return 0;
#endif
}
static bool finite3(v3 v) { return isfinite(v.x) && isfinite(v.y) && isfinite(v.z); }

static int cmd_soak(int minutes, int players, int park0) {
  static sim S;
  bot B[MAXP];
  long first = 0;
  int parks_done = 0, lines_done = 0, lines_failed = 0, bails = 0, tricks = 0, grinds = 0, manuals = 0, lost = 0;
  long long score = 0;
  bool inject = getenv("SKATE_SOAK_INJECT") != NULL;
  int per_park = TPS * 60 * 3, total = TPS * 60 * minutes;
  int park_id = park0 >= 0 ? park0 : 0;
  memset(&S, 0, sizeof S);
  sim_init(&S, park_id, MODE_FREE, players);
  for (int i = 0; i < MAXP; i++) bot_init(&B[i], 1000u + (uint32_t)i, i % 3);
  for (int t = 0; t < total; t++) {
    input in[MAXP];
    memset(in, 0, sizeof in);
    for (int i = 0; i < S.players; i++) bot_tick(&B[i], &S, i, &in[i]);
    sim_step(&S, in);
    if (inject && t == TPS * 30) S.s[0].v.x = NAN;
    for (int i = 0; i < S.players; i++) {
      const skater *s = &S.s[i];
      if (!finite3(s->p) || !finite3(s->v) || !finite3(s->f) || !finite3(s->u)) {
        printf("soak FAIL: skater %d not finite at tick %d (park %d)\n", i, t, S.park_id);
        return 1;
      }
      if (s->p.x < S.k.lo.x - 1 || s->p.x > S.k.hi.x + 1 || s->p.z < S.k.lo.z - 1 || s->p.z > S.k.hi.z + 1 || s->p.y < S.k.lo.y - 1) {
        printf("soak FAIL: skater %d left the park at (%.2f %.2f %.2f) tick %d (park %d)\n", i, (double)s->p.x, (double)s->p.y, (double)s->p.z, t, S.park_id);
        return 1;
      }
      if (s->st == SK_ROLL) { /* on a surface: never under one */
        phit h;
        if (park_cast(&S.k, vadd(s->p, V(0, -0.08f, 0)), V(0, 3, 0), 1u << SURF_RIDE, &h) && h.p.y > s->p.y + 0.05f && h.fn.y > 0.5f) {
          printf("soak FAIL: skater %d under a surface at (%.2f %.2f %.2f) tick %d (park %d)\n", i, (double)s->p.x, (double)s->p.y, (double)s->p.z, t, S.park_id);
          return 1;
        }
      }
    }
    if ((t + 1) % per_park == 0 || t + 1 == total) {
      for (int i = 0; i < S.players; i++) {
        const skater *s = &S.s[i];
        bails += s->bails, tricks += s->tricks, grinds += s->grinds, manuals += s->manuals, lost += s->lost, score += s->score;
        lines_done += B[i].lines_done, lines_failed += B[i].lines_failed;
        B[i].lines_done = B[i].lines_failed = 0;
      }
      parks_done++;
      if (park0 < 0) park_id = (park_id + 1) % PARK_COUNT;
      sim_init(&S, park_id, MODE_FREE, players);
      for (int i = 0; i < MAXP; i++) B[i].phase = 0;
    }
    if ((t + 1) % (TPS * 60) == 0) {
      long r = rss_kb();
      if (!first) first = r;
      printf("minute %d: rss %ld kB (first minute %ld)\n", (t + 1) / (TPS * 60), r, first);
      fflush(stdout);
    }
  }
  printf("soak stats: %d parks, %d lines skated, %d missed, %d tricks, %d grinds, %d manuals, %d bails, %lld points, %d lost\n", parks_done, lines_done, lines_failed, tricks,
         grinds, manuals, bails, score, lost);
  if (lost) { printf("soak FAIL: %d falls out of the world\n", lost); return 1; }
  printf("soak ok: %d minutes, %d players\n", minutes, players);
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "phys")) return cmd_phys();
  if (argc >= 2 && !strcmp(argv[1], "parks")) return cmd_parks(argc > 2 ? atoi(argv[2]) : -1);
  if (argc >= 4 && !strcmp(argv[1], "line")) { /* skate_check line <park> <line> [variant] [tries]: one line, traced with SKATE_TRACE */
    int ticks = 0;
    int ok = line_ok(atoi(argv[2]), argc > 4 ? atoi(argv[4]) : 0, atoi(argv[3]), argc > 5 ? atoi(argv[5]) : 1, &ticks);
    printf("line %s %s: %s\n", argv[2], argv[3], ok ? "ok" : "FAIL");
    return !ok;
  }
  if (argc >= 2 && !strcmp(argv[1], "soak")) return cmd_soak(argc > 2 ? atoi(argv[2]) : 5, argc > 3 ? atoi(argv[3]) : 2, argc > 4 ? atoi(argv[4]) : -1);
  fprintf(stderr, "usage: skate_check phys | parks [n] | soak <minutes> <players> [park]\n");
  return 2;
}
