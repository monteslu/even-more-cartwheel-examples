/* destructo_physcheck: what the engine's 3D physics (Box3D, through cartwheel/physics3d.h) can do for a game of
 * stacked blocks knocked down by heavy shot. Native only, no window, no GPU. Prints one JSON line per measurement;
 * tests/destructo-physics.mjs judges them (each with a control that must fail).
 *
 *   rest      stacks of the game's kinds (a brick tower, a tall thin column, a pyramid, a plank scaffold) left alone
 *             for 60 s at the game's settings: the largest drift of any block from where it sat after the first
 *             second, the largest speed after the first second, and when every body went to sleep. Also the same
 *             stacks with sleeping off (the solver's own jitter, nothing hiding it).
 *             control: the tower on a 4 degree slope with ice for friction: it must creep.
 *   budget    N blocks all awake and colliding (a grid of columns blown apart), the mean and the worst step time over
 *             120 steps, on the job pool (every thread) and on one thread, for N from 250 to 4000.
 *             control: 4x the budget's body count must miss the budget.
 *   determ    one collapse (a tower hit by a 40 kg shot), stepped with 1, 2, 4 and every worker: the world's state
 *             hash after 600 steps. control: the same run with one block 1 micrometre off must hash differently.
 *   tunnel    a 0.18 m iron shot fired at 30 to 200 m/s at a 2 cm glass pane (dynamic), a 6 cm plank (dynamic) and a
 *             6 cm wall (static): through or not, with bullet on and off, 4 and 8 sub-steps.
 *             control: continuous collision off must let the fastest shot through the static wall.
 *
 *   destructo_physcheck [rest|budget|determ|tunnel ...]   (default: all) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cartwheel/jobs.h"
#include "cartwheel/physics3d.h"

#define DT (1.0f / 60.0f)

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static uint32_t rs = 1;
static float frand(void) { rs = rs * 1664525u + 1013904223u; return (float)(rs >> 8) / 16777216.0f; }

/* ── a world as the game makes it ─────────────────────────────────────── */

typedef struct setup {
  int substeps, workers;
  bool sleep, continuous;
  float slope;      /* ground tilt about z, radians */
  float friction;   /* blocks and ground */
} setup;

static setup game_setup(void) { return (setup){ .substeps = 4, .workers = 0, .sleep = true, .continuous = true, .slope = 0, .friction = 0.7f }; }

#define MAXB 8192
typedef struct scene {
  cw_world3d w;
  cw_body3d b[MAXB];
  t3_vec3 half[MAXB];
  int n;
  setup s;
} scene;

static void scene_init(scene *S, setup s) {
  memset(S, 0, sizeof *S);
  S->s = s;
  cw_world3d_def wd = cw_world3d_default_def();
  wd.gravity = (t3_vec3){ 0, -9.81f, 0 };
  wd.substeps = s.substeps, wd.workers = s.workers, wd.sleep = s.sleep, wd.continuous = s.continuous;
  S->w = cw_world3d_create(&wd);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  float c = cosf(s.slope * 0.5f), sn = sinf(s.slope * 0.5f);
  bd.rotation = (t3_quat){ 0, 0, sn, c };
  bd.position = (t3_vec3){ 0, -0.5f, 0 };
  cw_body3d g = cw_body3d_create(S->w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.material.friction = s.friction;
  cw_shape3d_box(g, &sd, (t3_vec3){ 80, 0.5f, 80 }, cw_xform3_identity());
}

/* a block, its bottom at y on a ground tilted by the setup's slope (blocks are placed in the slope's frame) */
static int block(scene *S, t3_vec3 p, t3_vec3 half, float yaw, float density) {
  if (S->n >= MAXB) return -1;
  cw_body3d_def bd = cw_body3d_default_def();
  float c = cosf(S->s.slope * 0.5f), sn = sinf(S->s.slope * 0.5f);
  t3_quat tilt = { 0, 0, sn, c };
  t3_quat q = { 0, sinf(yaw * 0.5f), 0, cosf(yaw * 0.5f) };
  /* rotate p by tilt about the origin */
  float ct = cosf(S->s.slope), st = sinf(S->s.slope);
  bd.position = (t3_vec3){ p.x * ct - p.y * st, p.x * st + p.y * ct, p.z };
  bd.rotation = t3_quat_multiply(tilt, q);
  cw_body3d b = cw_body3d_create(S->w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.density = density;
  sd.material.friction = S->s.friction;
  cw_shape3d_box(b, &sd, half, cw_xform3_identity());
  S->b[S->n] = b, S->half[S->n] = half;
  return S->n++;
}

/* the game's kinds of stack */
static void build_tower(scene *S, float x0, float z0) { /* a round brick tower: 14 rings of 16 bricks, each ring turned half a brick */
  float R = 2.2f, h = 0.25f;
  for (int ring = 0; ring < 14; ring++)
    for (int i = 0; i < 16; i++) {
      float a = (i + (ring & 1) * 0.5f) * 6.2831853f / 16;
      float L = 2 * R * sinf(3.14159265f / 16) * 0.5f - 0.004f;
      block(S, (t3_vec3){ x0 + cosf(a) * R, h * ring + h * 0.5f, z0 + sinf(a) * R }, (t3_vec3){ 0.2f, h * 0.5f, L }, -a, 2400);
    }
  /* a floor of planks on top and a second tier */
  for (int i = 0; i < 9; i++) block(S, (t3_vec3){ x0 - 2.0f + i * 0.5f, 14 * h + 0.05f, z0 }, (t3_vec3){ 0.24f, 0.05f, 2.4f }, 0, 600);
}
static void build_column(scene *S, float x0, float z0) { /* 30 blocks straight up: the thinnest stack */
  for (int i = 0; i < 30; i++) block(S, (t3_vec3){ x0, 0.2f + i * 0.4f, z0 }, (t3_vec3){ 0.3f, 0.2f, 0.3f }, 0, 2400);
}
static void build_pyramid(scene *S, float x0, float z0) { /* 10 x 10 at the base, 385 blocks */
  for (int lv = 0; lv < 10; lv++) {
    int k = 10 - lv;
    for (int i = 0; i < k; i++)
      for (int j = 0; j < k; j++)
        block(S, (t3_vec3){ x0 + (i - (k - 1) * 0.5f) * 0.5f, 0.25f + lv * 0.5f, z0 + (j - (k - 1) * 0.5f) * 0.5f }, (t3_vec3){ 0.249f, 0.25f, 0.249f }, 0, 2400);
  }
}
static void build_scaffold(scene *S, float x0, float z0) { /* posts and planks, five storeys: wood, long spans */
  for (int lv = 0; lv < 5; lv++) {
    float y = lv * 1.68f; /* each storey stands on the planks of the one below */
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 2; j++) block(S, (t3_vec3){ x0 - 2.4f + i * 1.6f, y + 0.75f, z0 - 0.8f + j * 1.6f }, (t3_vec3){ 0.1f, 0.75f, 0.1f }, 0, 600);
    for (int i = 0; i < 2; i++) block(S, (t3_vec3){ x0, y + 1.55f, z0 - 0.8f + i * 1.6f }, (t3_vec3){ 2.6f, 0.05f, 0.12f }, 0, 600);
    for (int i = 0; i < 11; i++) block(S, (t3_vec3){ x0 - 2.5f + i * 0.5f, y + 1.64f, z0 }, (t3_vec3){ 0.2f, 0.04f, 1.0f }, 0, 600);
  }
}

static int count_awake(scene *S) {
  int a = 0;
  for (int i = 0; i < S->n; i++) a += cw_body3d_awake(S->b[i]);
  return a;
}

/* ── rest ─────────────────────────────────────────────────────────────── */

static void rest(const char *name, void (*build)(scene *, float, float), setup s, const char *tag) {
  static scene S;
  scene_init(&S, s);
  build(&S, 0, 0);
  static t3_vec3 p0[MAXB];
  float drift = 0, vmax = 0, first_move = 0;
  int worst = -1;
  int asleep_step = -1, wakeups = 0;
  bool was_asleep = false;
  for (int step = 0; step < 3600; step++) {
    cw_world3d_step(S.w, DT);
    if (step == 179) /* the reference: where each block sits after three seconds of settling (the game settles a level before play) */
      for (int i = 0; i < S.n; i++) p0[i] = cw_body3d_transform(S.b[i]).p;
    if (step == 0) {
      for (int i = 0; i < S.n; i++) { t3_vec3 v = cw_body3d_velocity(S.b[i]); float sp = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); if (sp > first_move) first_move = sp; }
    }
    if (step >= 180) {
      for (int i = 0; i < S.n; i++) {
        t3_vec3 p = cw_body3d_transform(S.b[i]).p, v = cw_body3d_velocity(S.b[i]);
        float dx = p.x - p0[i].x, dy = p.y - p0[i].y, dz = p.z - p0[i].z, d = sqrtf(dx * dx + dy * dy + dz * dz);
        float sp = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (d > drift) drift = d, worst = i;
        if (sp > vmax) vmax = sp;
      }
    }
    int aw = count_awake(&S);
    if (aw == 0 && !was_asleep) { was_asleep = true; if (asleep_step < 0) asleep_step = step; }
    if (aw > 0 && was_asleep) was_asleep = false, wakeups++;
  }
  printf("{\"m\":\"rest\",\"stack\":\"%s\",\"tag\":\"%s\",\"blocks\":%d,\"sleep\":%s,\"drift_mm\":%.3f,\"vmax_mm_s\":%.3f,\"asleep_s\":%.2f,\"wakeups\":%d,\"awake_end\":%d,\"worst\":%d}\n",
         name, tag, S.n, s.sleep ? "true" : "false", drift * 1000, vmax * 1000, asleep_step < 0 ? -1.0 : (asleep_step + 1) / 60.0, wakeups, count_awake(&S), worst);
  fflush(stdout);
  cw_world3d_destroy(S.w);
}

/* ── budget ───────────────────────────────────────────────────────────── */

static double budget_once(int n, int workers, double *worst_out, int *awake_out, int *contacts_out) {
  static scene S;
  setup s = game_setup();
  s.workers = workers;
  scene_init(&S, s);
  rs = 7;
  /* columns of 0.5 m blocks over a square, 8 high: then everything gets a kick outward and up */
  int cols = (n + 7) / 8, side = (int)ceilf(sqrtf((float)cols));
  for (int i = 0; i < n; i++) {
    int c = i / 8, lv = i % 8;
    float x = (c % side - side * 0.5f) * 0.56f, z = (c / side - side * 0.5f) * 0.56f;
    block(&S, (t3_vec3){ x, 0.25f + lv * 0.5f, z }, (t3_vec3){ 0.25f, 0.25f, 0.25f }, 0, 2400);
  }
  for (int i = 0; i < 30; i++) cw_world3d_step(S.w, DT);
  for (int i = 0; i < S.n; i++) {
    t3_vec3 p = cw_body3d_transform(S.b[i]).p;
    float r = sqrtf(p.x * p.x + p.z * p.z) + 0.5f;
    cw_body3d_set_velocity(S.b[i], (t3_vec3){ p.x / r * 6 + (frand() - 0.5f) * 2, 3 + frand() * 4, p.z / r * 6 + (frand() - 0.5f) * 2 });
    cw_body3d_set_angular_velocity(S.b[i], (t3_vec3){ (frand() - 0.5f) * 8, (frand() - 0.5f) * 8, (frand() - 0.5f) * 8 });
  }
  double sum = 0, worst = 0;
  int awake_min = n, contacts_max = 0;
  for (int step = 0; step < 120; step++) {
    double t0 = now_ms();
    cw_world3d_step(S.w, DT);
    double t = now_ms() - t0;
    sum += t;
    if (t > worst) worst = t;
    cw_phys_stats st = cw_world3d_stats(S.w);
    if (st.awake_bodies < awake_min) awake_min = st.awake_bodies;
    if (st.contacts > contacts_max) contacts_max = st.contacts;
  }
  cw_world3d_destroy(S.w);
  *worst_out = worst, *awake_out = awake_min, *contacts_out = contacts_max;
  return sum / 120;
}
/* the best of three runs: the machine is shared, and a run another process slowed says nothing about the solver */
static void budget(int n, int workers) {
  double best = 1e9, worst = 0, w;
  int aw = 0, co = 0;
  for (int k = 0; k < 3; k++) {
    double m = budget_once(n, workers, &w, &aw, &co);
    if (m < best) best = m, worst = w;
  }
  printf("{\"m\":\"budget\",\"bodies\":%d,\"workers\":%d,\"threads\":%d,\"mean_ms\":%.3f,\"worst_ms\":%.3f,\"awake_min\":%d,\"contacts_max\":%d}\n",
         n, workers, workers ? workers : cw_jobs_thread_count(), best, worst, aw, co);
  fflush(stdout);
}

/* ── determinism ──────────────────────────────────────────────────────── */

static uint32_t collapse_hash(int workers, float nudge) {
  static scene S;
  setup s = game_setup();
  s.workers = workers;
  scene_init(&S, s);
  build_tower(&S, 0, 0);
  build_pyramid(&S, 7, 0);
  t3_vec3 p = cw_body3d_transform(S.b[40]).p;
  p.x += nudge;
  cw_body3d_set_transform(S.b[40], (cw_xform3){ p, cw_body3d_transform(S.b[40]).q });
  for (int i = 0; i < 60; i++) cw_world3d_step(S.w, DT);
  /* the shot: a 40 kg iron ball at 60 m/s through the tower into the pyramid */
  cw_body3d_def bd = cw_body3d_default_def();
  bd.position = (t3_vec3){ -12, 1.2f, 0.3f }, bd.linear_velocity = (t3_vec3){ 60, 2, 0 }, bd.bullet = true;
  cw_body3d ball = cw_body3d_create(S.w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.density = 1650; /* 40 kg in a 0.18 m sphere */
  cw_shape3d_sphere(ball, &sd, (t3_vec3){ 0, 0, 0 }, 0.18f);
  for (int i = 0; i < 600; i++) cw_world3d_step(S.w, DT);
  uint32_t h = cw_world3d_stats(S.w).hash;
  cw_world3d_destroy(S.w);
  return h;
}

/* ── tunnelling ───────────────────────────────────────────────────────── */

typedef struct tun { int through, shots; } tun;

/* target kinds: 0 glass pane (dynamic, 2 cm), 1 plank (dynamic, 6 cm), 2 wall (static, 6 cm) */
static bool shot_through(int kind, float speed, bool bullet, int substeps, bool continuous, float yoff) {
  static scene S;
  setup s = game_setup();
  s.substeps = substeps, s.continuous = continuous;
  scene_init(&S, s);
  float th = kind == 0 ? 0.01f : 0.03f;
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = kind == 2 ? CW_BODY_STATIC : CW_BODY_DYNAMIC;
  bd.position = (t3_vec3){ 0, 1.0f, 0 };
  cw_body3d tb = cw_body3d_create(S.w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.density = kind == 0 ? 2500 : 600;
  cw_shape3d_box(tb, &sd, (t3_vec3){ th, 1.0f, 1.0f }, cw_xform3_identity());
  /* the shot starts a little way off: at 200 m/s it covers 3.3 m a tick */
  bd = cw_body3d_default_def();
  bd.position = (t3_vec3){ -4.0f, 1.0f + yoff, 0.1f };
  bd.linear_velocity = (t3_vec3){ speed, 0, 0 };
  bd.gravity_scale = 0;
  bd.bullet = bullet;
  cw_body3d ball = cw_body3d_create(S.w, &bd);
  sd = cw_shape3d_default_def();
  sd.density = 1650;
  cw_shape3d_sphere(ball, &sd, (t3_vec3){ 0, 0, 0 }, 0.18f);
  bool hit = false;
  for (int i = 0; i < 30; i++) {
    cw_world3d_step(S.w, DT);
    cw_contact3d c[8];
    if (cw_body3d_contacts(ball, c, 8) > 0) hit = true;
    if (kind != 2) {
      t3_vec3 v = cw_body3d_velocity(tb);
      if (fabsf(v.x) > 0.05f) hit = true;
    }
  }
  t3_vec3 bp = cw_body3d_transform(ball).p;
  t3_vec3 bv = cw_body3d_velocity(ball);
  /* through: past the target with no sign it ever touched it, or past a static wall at all (it can never pass) */
  bool through = kind == 2 ? bp.x > 0 : (bp.x > 0.5f && !hit && bv.x > speed * 0.98f);
  cw_world3d_destroy(S.w);
  return through;
}

static void tunnel(bool bullet, int substeps, bool continuous, const char *tag) {
  static const float speeds[] = { 30, 60, 90, 120, 160, 200 };
  for (int kind = 0; kind < 3; kind++) {
    tun t = { 0, 0 };
    char per[256] = "";
    for (int si = 0; si < 6; si++) {
      int th = 0;
      for (int k = 0; k < 5; k++) th += shot_through(kind, speeds[si], bullet, substeps, continuous, -0.3f + 0.15f * k), t.shots++;
      t.through += th;
      char b[24];
      snprintf(b, sizeof b, "%s%d", si ? "," : "", th);
      strcat(per, b);
    }
    static const char *names[] = { "glass2cm", "plank6cm", "wall6cm" };
    printf("{\"m\":\"tunnel\",\"tag\":\"%s\",\"target\":\"%s\",\"bullet\":%s,\"substeps\":%d,\"continuous\":%s,\"through\":%d,\"shots\":%d,\"by_speed\":[%s]}\n",
           tag, names[kind], bullet ? "true" : "false", substeps, continuous ? "true" : "false", t.through, t.shots, per);
    fflush(stdout);
  }
}

/* ── worlds: does making and destroying a world give its memory back? ── */

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
/* a pyramid and a tower made, stepped 2 s with bodies made and destroyed (as breaking does), the world destroyed;
 * 60 times. The resident memory after the 10th and the 60th */
static void worlds(void) {
  static scene S;
  long at10 = 0, at60 = 0;
  int iters = getenv("PC_ITERS") ? atoi(getenv("PC_ITERS")) : 60;
  for (int it = 0; it < iters; it++) {
    scene_init(&S, game_setup());
    build_pyramid(&S, 0, 0);
    build_tower(&S, 8, 0);
    int steps = getenv("PC_STEPS") ? atoi(getenv("PC_STEPS")) : 120;
    for (int i = 0; i < steps; i++) {
      cw_world3d_step(S.w, DT);
      if (!getenv("PC_NOSWAP") && i % 10 == 0 && S.n > 50) { cw_body3d_destroy(S.b[S.n - 1]); S.n--; block(&S, (t3_vec3){ -6, 0.25f, (float)(i % 7) }, (t3_vec3){ 0.2f, 0.2f, 0.2f }, 0, 600); }
    }
    cw_world3d_destroy(S.w);
    if (it == 9) at10 = rss_kb();
    if (it == iters - 1) at60 = rss_kb();
  }
  printf("{\"m\":\"worlds\",\"rss10_kb\":%ld,\"rss60_kb\":%ld}\n", at10, at60);
  fflush(stdout);
}

int main(int argc, char **argv) {
  cw_jobs_init(-1);
  bool all = argc < 2;
  for (int a = all ? 0 : 1; a < (all ? 1 : argc); a++) {
    const char *what = all ? "all" : argv[a];
    bool every = !strcmp(what, "all");
    if (every || !strcmp(what, "rest")) {
      setup s = game_setup();
      rest("tower", build_tower, s, "game");
      rest("column", build_column, s, "game");
      rest("pyramid", build_pyramid, s, "game");
      rest("scaffold", build_scaffold, s, "game");
      s.sleep = false;
      rest("tower", build_tower, s, "nosleep");
      rest("column", build_column, s, "nosleep");
      rest("pyramid", build_pyramid, s, "nosleep");
      rest("scaffold", build_scaffold, s, "nosleep");
      setup c = game_setup();
      c.slope = 4 * 3.14159265f / 180, c.friction = 0.03f;
      rest("tower", build_tower, c, "control");
    }
    if (every || !strcmp(what, "budget")) {
      static const int ns[] = { 250, 500, 1000, 1500, 2000, 3000, 4000, 6000 };
      for (int i = 0; i < 8; i++) budget(ns[i], 0);
      for (int i = 0; i < 8; i++) budget(ns[i], 1);
    }
    if (every || !strcmp(what, "determ")) {
      uint32_t h[5];
      static const int ws[] = { 1, 2, 4, 0 };
      for (int i = 0; i < 4; i++) h[i] = collapse_hash(ws[i], 0);
      h[4] = collapse_hash(0, 1e-6f);
      printf("{\"m\":\"determ\",\"w1\":%u,\"w2\":%u,\"w4\":%u,\"wall\":%u,\"control\":%u}\n", h[0], h[1], h[2], h[3], h[4]);
      fflush(stdout);
    }
    if (every || !strcmp(what, "worlds")) worlds();
    if (every || !strcmp(what, "tunnel")) {
      tunnel(false, 4, true, "plain");
      tunnel(true, 4, true, "bullet");
      tunnel(true, 8, true, "bullet8");
      tunnel(false, 8, true, "plain8");
      tunnel(true, 4, false, "control");
    }
  }
  return 0;
}
