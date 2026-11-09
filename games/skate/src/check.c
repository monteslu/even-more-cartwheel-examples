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
 *   skate_check runout          landings on the flat: a near miss at a pace a skater can run off is run out on foot and
 *                               the rider is back on the board rolling; too fast, or the board right across the run,
 *                               is a fall; a clean one rolls away. SKATE_NO_RUNOUT=1 (the control): every miss falls.
 *   skate_check soak <min> <players> [park]
 *                               bots skate every park in turn for <min> game minutes: no NaN, nobody leaves the park or
 *                               falls through it, memory flat. SKATE_SOAK_INJECT=1 puts a NaN in a velocity (the control). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <time.h>
#include <unistd.h>
#endif

#include "cartwheel/physics3d.h"
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

/* ── the engine's Box3D, for the record: a frictionless sphere (a wheel's size) on the same meshes, stepped as the
 * engine steps a world (60 Hz, 4 substeps). Reported beside the solvers, not judged: the answer to "would the engine's
 * physics do for a board?" ── */
static cw_world3d b3_world(const park *k, cw_mesh3d *mesh) {
  cw_world3d_def wd = cw_world3d_default_def();
  wd.gravity = V(0, -GRAV, 0);
  wd.workers = 1;
  wd.substeps = 4;
  cw_world3d w = cw_world3d_create(&wd);
  int n = 0;
  for (int i = 0; i < k->nt; i++) n += k->t[i].surf != SURF_NONE;
  v3 *vs = malloc(sizeof *vs * (size_t)n * 3);
  int32_t *ix = malloc(sizeof *ix * (size_t)n * 3);
  int m = 0;
  for (int i = 0; i < k->nt; i++) {
    const ptri *t = &k->t[i];
    if (t->surf == SURF_NONE) continue;
    vs[m * 3] = t->a, vs[m * 3 + 1] = t->b, vs[m * 3 + 2] = t->c;
    ix[m * 3] = m * 3, ix[m * 3 + 1] = m * 3 + 1, ix[m * 3 + 2] = m * 3 + 2;
    m++;
  }
  *mesh = cw_mesh3d_create(vs, m * 3, ix, m);
  free(vs), free(ix);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  cw_body3d g = cw_body3d_create(w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.material.friction = 0;
  cw_shape3d_mesh(g, &sd, *mesh, V(1, 1, 1));
  return w;
}
static cw_body3d b3_ball(cw_world3d w, v3 p, v3 v) {
  cw_body3d_def bd = cw_body3d_default_def();
  bd.position = p;
  bd.linear_velocity = v;
  bd.bullet = true;
  bd.sleep = false;
  cw_body3d b = cw_body3d_create(w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.material.friction = 0;
  sd.material.restitution = 0;
  cw_shape3d_sphere(b, &sd, V(0, 0, 0), 0.12f);
  return b;
}
static void box3d_report(void) {
  /* the quarter pipe: apex against v^2 / 2g, and ticks the ball spent off the ramp it should hug */
  float worst = 0;
  int hops = 0;
  const float SP[5] = { 5, 7, 9, 11, 13 };
  for (int i = 0; i < 5; i++) {
    park k;
    memset(&k, 0, sizeof k);
    test_quarter(&k, 3.0f, 10.0f);
    cw_mesh3d mesh;
    cw_world3d w = b3_world(&k, &mesh);
    cw_body3d b = b3_ball(w, V(0, 0.121f, 0), V(0, 0, -SP[i]));
    float apex = 0;
    for (int t = 0; t < TPS * 6; t++) {
      cw_world3d_step(w, TICK_DT);
      v3 p = cw_body3d_transform(b).p;
      if (p.y - 0.12f > apex) apex = p.y - 0.12f;
      phit h;
      /* off the surface: more than 3 cm between the ball and the ramp under it, along the ramp's normal */
      if (park_cast(&k, p, V(0, -1.0f, 0), 1u << SURF_RIDE, &h) && h.n.y > 0.3f && vdot(vsub(p, h.p), h.n) > 0.12f + 0.03f) hops++;
      if (cw_body3d_velocity(b).z > 0 && p.z > -1) break;
    }
    float want = SP[i] * SP[i] / (2 * GRAV), e = fabsf(apex / want - 1);
    if (e > worst) worst = e;
    cw_world3d_destroy(w);
    cw_mesh3d_destroy(mesh);
    park_free(&k);
  }
  /* the landing slope at speed */
  int through = 0, total = 0;
  {
    park k;
    memset(&k, 0, sizeof k);
    test_slope(&k);
    const float ZT[3] = { 9, 12, 15 };
    for (int a = 0; a < 6; a++)
      for (int sp = 0; sp < 6; sp++)
        for (int zt = 0; zt < 3; zt++) {
          cw_mesh3d mesh;
          cw_world3d w = b3_world(&k, &mesh);
          float speed = 12 + 3.0f * (float)sp, ang = 0.17f + 0.15f * (float)a, sa, ca;
          cw_sincos(ang, &sa, &ca);
          phit h;
          park_ground(&k, V(0, 20, ZT[zt]), 1, 30, &h);
          v3 vd = V(0, -sa, ca);
          cw_body3d b = b3_ball(w, vadd(vmad(h.p, vd, -1.2f), V(0, 0.75f, 0)), vmul(vd, speed));
          bool under = false;
          for (int t = 0; t < 30 && !under; t++) {
            cw_world3d_step(w, TICK_DT);
            v3 p = cw_body3d_transform(b).p;
            if (park_cast(&k, vadd(p, V(0, 20, 0)), V(0, -19.95f, 0), 1u << SURF_RIDE, &h) && h.p.y > p.y + 0.05f) under = true;
          }
          through += under, total++;
          cw_world3d_destroy(w);
          cw_mesh3d_destroy(mesh);
        }
    park_free(&k);
  }
  printf("{\"solver\":\"box3d\",\"apex_err\":%.4f,\"off_ramp_ticks\":%d,\"slope\":{\"through\":%d,\"total\":%d}}\n", (double)worst, hops, through, total);
}

static int cmd_phys(void) {
  box3d_report();
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

/* ── run-outs ───────────────────────────────────────────────────────────── */

/* one landing on flat ground: the board turned `off` radians from the way it flies at `speed` m/s, coming down from
   0.6 m. What became of it: 0 rolled away, 1 ran it out (on foot, then back on the board rolling), 2 fell */
static int landing_shot(float speed, float off, int *back_on, float *on_speed) {
  park k;
  memset(&k, 0, sizeof k);
  test_rail(&k); /* (its flat ground; the rail is far off the line) */
  skater s;
  memset(&s, 0, sizeof s);
  sk_place(&s, &k, V(-12, 0, 0), 0, 0);
  s.st = SK_AIR;
  s.p.y += 0.6f;
  v3 dir = V(1, 0, 0);
  s.v = vmul(dir, speed);
  s.v.y = 0;
  s.f = V(cw_cos(off), 0, cw_sin(off));
  s.u = V(0, 1, 0);
  s.takeoff = s.p, s.takeoff_y = s.p.y;
  int what = -1, t_bail = -1;
  *back_on = 0, *on_speed = 0;
  for (int t = 0; t < TPS * 4; t++) {
    sk_step(&s, &k, &NOIN);
    if (what < 0 && s.st == SK_ROLL) { what = 0; break; }
    if (what < 0 && s.st == SK_BAIL) what = s.runout ? 1 : 2, t_bail = t;
    if (what > 0 && s.st == SK_ROLL) { *back_on = t - t_bail, *on_speed = vlen(s.v); break; }
  }
  park_free(&k);
  return what;
}
static int cmd_runout(void) {
  /* near misses at a pace that can be run off (sideways by a little past what rolls away), the same too fast, a board
     right across the run, and a clean one */
  int near_slow = 0, near_n = 0, fast_fell = 0, fast_n = 0, across_fell = 0, clean = 0, back_max = 0;
  float on_max = 0;
  const float SLOW[3] = { 3.0f, 4.5f, 6.0f }, NEAR[3] = { 0.9f, 1.0f, 1.15f };
  for (int a = 0; a < 3; a++)
    for (int b = 0; b < 3; b++) {
      int back;
      float on;
      int w = landing_shot(SLOW[a], NEAR[b], &back, &on);
      near_n++, near_slow += w == 1;
      if (w == 1 && back > back_max) back_max = back;
      if (w == 1 && on > on_max) on_max = on;
      int b2;
      float o2;
      fast_n++, fast_fell += landing_shot(9.0f + 2.0f * (float)a, NEAR[b], &b2, &o2) == 2;
    }
  int b3;
  float o3;
  across_fell = landing_shot(4.0f, 1.5f, &b3, &o3) == 2;
  clean = landing_shot(5.0f, 0.15f, &b3, &o3) == 0;
  printf("{\"near\":%d,\"near_n\":%d,\"fast_fell\":%d,\"fast_n\":%d,\"across_fell\":%d,\"clean\":%d,\"back_ticks\":%d,\"on_speed\":%.2f,\"runout_ticks\":%d}\n", near_slow,
         near_n, fast_fell, fast_n, across_fell, clean, back_max, (double)on_max, RUNOUT_TICKS);
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

/* the bots' routes between lines: two sharp bots skate the park freely for three minutes (as the auto cart does); how
   many line starts they reached on the board, walked to, and gave up on */
static void routes(int park_id, int *rides, int *hikes, int *given_up, int *lines) {
  static sim S;
  memset(&S, 0, sizeof S);
  sim_init(&S, park_id, MODE_FREE, 2);
  bot B[2];
  for (int i = 0; i < 2; i++) bot_init(&B[i], 4242u + (uint32_t)i * 77u + (uint32_t)park_id * 13u, 2);
  for (int t = 0; t < TPS * 180; t++) {
    input in[MAXP];
    memset(in, 0, sizeof in);
    for (int i = 0; i < 2; i++) bot_tick(&B[i], &S, i, &in[i]);
    sim_step(&S, in);
  }
  *rides = B[0].rides + B[1].rides, *hikes = B[0].hikes + B[1].hikes, *given_up = B[0].route_fails + B[1].route_fails, *lines = B[0].lines_done + B[1].lines_done;
  sim_free(&S);
}

static int cmd_parks(int only) {
  int bad = 0, ctl_gu = 0, ctl_parks = 0;
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
    /* the routes: every one the bots set off on reached its start, on the board (the planner's grid true to the park);
       the control plans blind to the park (a straight line through whatever is in the way): its routes must run into
       things and be given up */
    {
      int rides, hikes, gu, lines;
      routes(p, &rides, &hikes, &gu, &lines);
      bool ok = gu == 0 && rides >= 4;
      printf("  routes: %d starts reached on the board, %d walked to (none there on wheels), %d given up, %d lines skated: %s\n", rides, hikes, gu, lines, ok ? "ok" : "FAIL");
      if (!ok) bad++;
      g_nav_ignore_walls = true;
      routes(p, &rides, &hikes, &gu, &lines);
      g_nav_ignore_walls = false;
      printf("  control blind routes: %d reached, %d given up\n", rides, gu);
      ctl_gu += gu, ctl_parks += gu > 0;
    }
    park_free(&k);
  }
  /* (an open park can be crossed in a straight line: the control must fail on most of them, not each) */
  int ran = only >= 0 ? 1 : PARK_COUNT;
  if (ctl_parks * 2 > ran) printf("control blind routes: %d given up in %d of %d parks (fails as it must)\n", ctl_gu, ctl_parks, ran);
  else printf("control blind routes: %d given up in %d of %d parks: CONTROL PASSED (a failure)\n", ctl_gu, ctl_parks, ran), bad++;
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
  int parks_done = 0, lines_done = 0, lines_failed = 0, bails = 0, tricks = 0, grinds = 0, manuals = 0, lost = 0, rides = 0, hikes = 0, given_up = 0;
  long long score = 0;
  bool inject = getenv("SKATE_SOAK_INJECT") != NULL;
  int per_park = TPS * 60 * 2, total = TPS * 60 * minutes;
  int park_id = park0 >= 0 ? park0 : 0;
  memset(&S, 0, sizeof S);
  sim_init(&S, park_id, MODE_FREE, players);
  for (int i = 0; i < MAXP; i++) bot_init(&B[i], 1000u + (uint32_t)i, i % 3);
  for (int t = 0; t < total; t++) {
    input in[MAXP];
    memset(in, 0, sizeof in);
    for (int i = 0; i < S.players; i++) bot_tick(&B[i], &S, i, &in[i]);
    if (getenv("SKATE_TRACEP") && t > atoi(getenv("SKATE_TRACEP")) - 400 && t <= atoi(getenv("SKATE_TRACEP"))) { int ti = getenv("SKATE_TRACEI") ? atoi(getenv("SKATE_TRACEI")) : 0; const skater *s = &S.s[ti]; printf("  t%d ph%d line %d st %d p(%.2f %.2f %.2f) v(%.2f %.2f %.2f) u(%.2f %.2f %.2f) in(%.2f %.2f %c%c%c%c%c) ri %d/%d\n", t, B[ti].phase, B[ti].line, s->st, (double)s->p.x, (double)s->p.y, (double)s->p.z, (double)s->v.x, (double)s->v.y, (double)s->v.z, (double)s->u.x, (double)s->u.y, (double)s->u.z, (double)in[ti].sx, (double)in[ti].sy, in[ti].ollie ? 79 : 46, in[ti].flip ? 70 : 46, in[ti].grab ? 71 : 46, in[ti].grind ? 82 : 46, in[ti].manual ? 77 : 46, B[ti].route_i, B[ti].route_n); }
    int pv[MAXP], pf[MAXP];
    for (int i = 0; i < MAXP; i++) pv[i] = S.s[i].vert, pf[i] = S.s[i].flip;
    sim_step(&S, in);
    if (inject && t == TPS * 30) S.s[0].v.x = NAN;
    if (getenv("SKATE_BAILS")) for (int i = 0; i < S.players; i++) if (S.s[i].ev & EV_BAIL) printf("bail p%d park %d skill %d: %s (line %d phase %d, p %.1f %.1f %.1f, v %.1f %.1f %.1f, vert %d, flip %d)\n", i, S.park_id, B[i].skill, S.s[i].bail_why ? S.s[i].bail_why : "?", B[i].line, B[i].phase, (double)S.s[i].p.x, (double)S.s[i].p.y, (double)S.s[i].p.z, (double)S.s[i].bail_v.x, (double)S.s[i].bail_v.y, (double)S.s[i].bail_v.z, pv[i], pf[i]);
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
        lines_done += B[i].lines_done, lines_failed += B[i].lines_failed, rides += B[i].rides, hikes += B[i].hikes, given_up += B[i].route_fails;
        B[i].lines_done = B[i].lines_failed = B[i].rides = B[i].hikes = B[i].route_fails = 0;
      }
      printf("park %d done (cycle %d): rss %ld kB\n", S.park_id, parks_done / PARK_COUNT, rss_kb());
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
  printf("soak routes: %d starts reached on the board, %d walked to (no way there on wheels), %d routes given up\n", rides, hikes, given_up);
  if (lost) { printf("soak FAIL: %d falls out of the world\n", lost); return 1; }
  printf("soak ok: %d minutes, %d players\n", minutes, players);
  return 0;
}

/* ── a pad script, played on the simulation alone (tuning tests/skate-feel.json without a GPU) ──
 * stdin: lines "frame lx ly buttons..." (node turns the JSON into them); the run starts at frame `start` (the frame its
 * setup screen was confirmed on, plus one), park 0, free skate, one player */
static int cmd_script(int start) {
  static sim S;
  memset(&S, 0, sizeof S);
  sim_init(&S, PARK_SUNNY, MODE_FREE, 1);
  int fr[256];
  input in[256];
  int n = 0;
  char line[512];
  while (n < 256 && fgets(line, sizeof line, stdin)) {
    float lx = 0, ly = 0;
    int off = 0;
    if (sscanf(line, "%d %f %f%n", &fr[n], &lx, &ly, &off) < 3) continue;
    memset(&in[n], 0, sizeof in[n]);
    in[n].sx = lx, in[n].sy = -ly;
    const char *b = line + off;
    in[n].ollie = strstr(b, "south") != NULL, in[n].flip = strstr(b, "west") != NULL, in[n].grab = strstr(b, "east") != NULL;
    in[n].grind = strstr(b, "north") != NULL, in[n].manual = strstr(b, " l") != NULL || strstr(b, " r") != NULL, in[n].reset = strstr(b, "back") != NULL;
    n++;
  }
  int k = 0;
  input cur;
  memset(&cur, 0, sizeof cur);
  for (int f = start; f < 720; f++) {
    while (k < n && fr[k] <= f) cur = in[k++];
    input ins[MAXP];
    memset(ins, 0, sizeof ins);
    ins[0] = cur;
    sim_step(&S, ins);
    const skater *s = &S.s[0];
    if (s->ev) printf("f%d %-5s p(%.1f %.1f %.1f) v%.1f ev%x cb%d %s%s\n", f, sk_state_name(s->st), (double)s->p.x, (double)s->p.y, (double)s->p.z, (double)vlen(s->v), s->ev, s->cb.n,
                      s->ev & EV_BAIL ? "BAIL " : "", s->ev & EV_BAIL && s->bail_why ? s->bail_why : "");
  }
  printf("tricks %d bails %d score %d tick %d hash %u\n", S.s[0].tricks, S.s[0].bails, S.s[0].score, S.tick, S.hash);
  return 0;
}

/* ── the auto cart's run, simulation only: the same bots and seeds as main.c's skate_auto (tuning) ── */
static int cmd_auto(int park_id, int players, int ticks) {
  static sim S;
  memset(&S, 0, sizeof S);
  sim_init(&S, park_id, players > 1 ? MODE_BATTLE : MODE_ATTACK, players);
  bot B[MAXP];
  for (int i = 0; i < MAXP; i++) bot_init(&B[i], 1000u + (uint32_t)i * 77u + (uint32_t)park_id * 13u, 2);
  for (int t = 0; t < ticks; t++) {
    static int ph_was[MAXP] = { -1, -1, -1, -1 };
    if (getenv("SKATE_ROUTES"))
      for (int i = 0; i < S.players; i++)
        if (B[i].phase != ph_was[i]) {
          ph_was[i] = B[i].phase;
          printf("t%d p%d phase %d line %d %s at (%.1f %.1f %.1f) v %.1f st %d rides %d hikes %d given up %d\n", t, i, B[i].phase, B[i].line, B[i].line >= 0 ? S.k.line[B[i].line].name : "-", (double)S.s[i].p.x, (double)S.s[i].p.y, (double)S.s[i].p.z, (double)vlen(S.s[i].v), S.s[i].st, B[i].rides, B[i].hikes, B[i].route_fails);
        }
    input in[MAXP];
    memset(in, 0, sizeof in);
    for (int i = 0; i < S.players; i++) bot_tick(&B[i], &S, i, &in[i]);
    if (getenv("SKATE_TRACEP") && t % 6 == 0) { int i = atoi(getenv("SKATE_TRACEP")); const skater *s = &S.s[i]; printf("  t%d ph%d p(%.2f %.2f %.2f) v(%.2f %.2f %.2f) u.y %.2f in(%.2f %.2f) ri %d/%d\n", t, B[i].phase, (double)s->p.x, (double)s->p.y, (double)s->p.z, (double)s->v.x, (double)s->v.y, (double)s->v.z, (double)s->u.y, (double)in[i].sx, (double)in[i].sy, B[i].route_i, B[i].route_n); }
    sim_step(&S, in);
    for (int i = 0; i < S.players; i++) {
      if (getenv("SKATE_SCAN")) {
        if (i != 0 || t % 10) continue;
        printf("t%d", t);
        for (int j = 0; j < S.players; j++) printf(" %c%c%d", "RAGB"[S.s[j].st], S.s[j].grab >= 0 ? 'g' : S.s[j].flip >= 0 ? 'f' : S.s[j].manual ? 'm' : '.', (int)S.s[j].p.y);
        printf("\n");
        continue;
      }
      if (S.s[i].ev & (EV_BAIL | EV_COMBO) || t % 120 == 0)
        printf("t%d p%d ph%d line %d %-5s p(%.1f %.1f %.1f) v%.1f score %d%s%s\n", t, i, B[i].phase, B[i].line, sk_state_name(S.s[i].st), (double)S.s[i].p.x, (double)S.s[i].p.y,
               (double)S.s[i].p.z, (double)vlen(S.s[i].v), S.s[i].score, S.s[i].ev & EV_BAIL ? " BAIL " : "", S.s[i].ev & EV_BAIL && S.s[i].bail_why ? S.s[i].bail_why : "");
    }
  }
  printf("final tick %d clock %d hash %u score [%d,%d,%d,%d] tricks [%d,%d,%d,%d] bails [%d,%d,%d,%d] grinds [%d,%d,%d,%d] manuals [%d,%d,%d,%d] tokens [%d,%d,%d,%d] best_combo [%d,%d,%d,%d] lines [%d,%d,%d,%d] state [%d,%d,%d,%d]\n", S.tick, S.clock, S.hash,
         S.s[0].score, S.s[1].score, S.s[2].score, S.s[3].score, S.s[0].tricks, S.s[1].tricks, S.s[2].tricks, S.s[3].tricks, S.s[0].bails, S.s[1].bails, S.s[2].bails, S.s[3].bails,
         S.s[0].grinds, S.s[1].grinds, S.s[2].grinds, S.s[3].grinds, S.s[0].manuals, S.s[1].manuals, S.s[2].manuals, S.s[3].manuals, S.s[0].tokens, S.s[1].tokens, S.s[2].tokens, S.s[3].tokens,
         S.s[0].best_combo, S.s[1].best_combo, S.s[2].best_combo, S.s[3].best_combo, B[0].lines_done, B[1].lines_done, B[2].lines_done, B[3].lines_done, S.s[0].st, S.s[1].st, S.s[2].st, S.s[3].st);
  printf("routes: rides [%d,%d,%d,%d] hikes [%d,%d,%d,%d] given up [%d,%d,%d,%d] lines missed [%d,%d,%d,%d]\n", B[0].rides, B[1].rides, B[2].rides, B[3].rides, B[0].hikes, B[1].hikes, B[2].hikes, B[3].hikes, B[0].route_fails, B[1].route_fails, B[2].route_fails, B[3].route_fails, B[0].lines_failed, B[1].lines_failed, B[2].lines_failed, B[3].lines_failed);
  return 0;
}

/* ── the route planner's map: from each start point, which lines' starts it can skate to (tuning) ── */
static int cmd_nav(void) {
  if (getenv("SKATE_NAVROUTE")) { /* park,x,y,z,x,y,z: the route between two points, a cell a line */
    int p;
    v3 a, e;
    if (sscanf(getenv("SKATE_NAVROUTE"), "%d,%f,%f,%f,%f,%f,%f", &p, &a.x, &a.y, &a.z, &e.x, &e.y, &e.z) != 7) return 2;
    park k;
    memset(&k, 0, sizeof k);
    park_build(&k, p, PARKV_GAME);
    nav_flood(&k, nav_cell(&k, a));
    int r[2000], n = nav_route(&k, nav_cell(&k, e), r, 2000);
    for (int i = 0; i < n; i++) {
      v3 q = nav_point(&k, r[i]);
      printf("%d (%.2f %.2f %.2f) cost %.2f\n", i, (double)q.x, (double)q.y, (double)q.z, (double)nav_dist(&k, r[i]));
    }
    return 0;
  }
  for (int p = 0; p < PARK_COUNT; p++) {
    park k;
    memset(&k, 0, sizeof k);
    park_build(&k, p, PARKV_GAME);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int c0 = nav_cell(&k, k.spawn[0].p);
    nav_flood(&k, c0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("park %d %s: first flood %.1f ms\n", p, k.name, (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-6);
    for (int a = -1; a < k.nline; a++) {
      v3 from = a < 0 ? k.spawn[0].p : k.line[a].start;
      int c = nav_cell(&k, from);
      clock_gettime(CLOCK_MONOTONIC, &t0);
      nav_flood(&k, c);
      clock_gettime(CLOCK_MONOTONIC, &t1);
      printf("  %-16s cell %6d (%.1f ms):", a < 0 ? "spawn 0" : k.line[a].name, c, (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-6);
      for (int b = 0; b < k.nline; b++) {
        float d = nav_dist(&k, nav_cell(&k, k.line[b].start));
        if (d < 1e29f) printf(" %d:%.0f", b, (double)d);
        else printf(" %d:-", b);
      }
      printf("\n");
      int mp = -9, ml = -9;
      if (getenv("SKATE_NAVMAP")) sscanf(getenv("SKATE_NAVMAP"), "%d,%d", &mp, &ml);
      if (mp == p && ml == a) /* a metre a character: '.' reached, ':' a reached slope, 'o' skateable but not reached, ' ' not skateable */
        for (float z = k.lo.z; z < k.hi.z; z += 1.0f) {
          for (float x = k.lo.x; x < k.hi.x; x += 1.0f) {
            phit h;
            bool w = park_ground(&k, V(x, k.hi.y + 1, z), 0, k.hi.y - k.lo.y + 3, &h) && h.n.y >= 0.85f;
            int c = nav_cell(&k, V(x, w ? h.p.y : 0, z));
            float d = nav_dist(&k, c);
            putchar(!w ? ' ' : d < 1e29f ? (h.n.y < 0.985f ? ':' : '.') : 'o');
          }
          putchar('\n');
        }
    }
    park_free(&k);
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "nav")) return cmd_nav();
  if (argc >= 6 && !strcmp(argv[1], "ground")) { /* skate_check ground <park> x y z: the ride surfaces under a point (development) */
    park k;
    memset(&k, 0, sizeof k);
    park_build(&k, atoi(argv[2]), 0);
    v3 p = V((float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5]));
    for (float y = p.y + 0.5f; y > p.y - 1.0f; y -= 0.25f) {
      phit h;
      bool g = park_cast(&k, V(p.x, y, p.z), V(0, -0.3f, 0), 1u << SURF_RIDE, &h);
      printf("from %.2f: %s", (double)y, g ? "hit" : "none");
      if (g) printf(" y %.3f n %.2f %.2f %.2f tri %d piece %d", (double)h.p.y, (double)h.n.x, (double)h.n.y, (double)h.n.z, h.tri, k.t[h.tri].piece);
      printf("\n");
    }
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "phys")) return cmd_phys();
  if (argc >= 2 && !strcmp(argv[1], "runout")) { g_no_runout = getenv("SKATE_NO_RUNOUT") != NULL; return cmd_runout(); }
  if (argc >= 2 && !strcmp(argv[1], "auto")) return cmd_auto(argc > 2 ? atoi(argv[2]) : 0, argc > 3 ? atoi(argv[3]) : 2, argc > 4 ? atoi(argv[4]) : 1500);
  if (argc >= 2 && !strcmp(argv[1], "script")) return cmd_script(argc > 2 ? atoi(argv[2]) : 71);
  if (argc >= 2 && !strcmp(argv[1], "parks")) return cmd_parks(argc > 2 ? atoi(argv[2]) : -1);
  if (argc >= 4 && !strcmp(argv[1], "line")) { /* skate_check line <park> <line> [variant] [tries]: one line, traced with SKATE_TRACE */
    int ticks = 0;
    int ok = line_ok(atoi(argv[2]), argc > 4 ? atoi(argv[4]) : 0, atoi(argv[3]), argc > 5 ? atoi(argv[5]) : 1, &ticks);
    printf("line %s %s: %s\n", argv[2], argv[3], ok ? "ok" : "FAIL");
    return !ok;
  }
  if (argc >= 2 && !strcmp(argv[1], "bakehash")) { /* each park's drawn shape: its bake must have been made for it */
    for (int n = 0; n < PARK_COUNT; n++) {
      park k;
      memset(&k, 0, sizeof k);
      park_build(&k, n, argc > 2 ? atoi(argv[2]) : 0);
      printf("park %d %08x\n", n, park_geom_hash(&k));
      park_free(&k);
    }
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "soak")) return cmd_soak(argc > 2 ? atoi(argv[2]) : 5, argc > 3 ? atoi(argv[3]) : 2, argc > 4 ? atoi(argv[4]) : -1);
  fprintf(stderr, "usage: skate_check phys | parks [n] | soak <minutes> <players> [park]\n");
  return 2;
}
