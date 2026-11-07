/* destructo_levelcheck: every level, checked three ways (native, no window, no GPU). One JSON line a level;
 * tests/destructo-levels.mjs judges them.
 *
 *   floats   every block as placed stands on something: a thin box just under its bottom face must touch another
 *            block or the ground
 *   rest     the level settles for 3 s (as the game does before play), then is left alone for 60 s: no block may
 *            move more than 5 mm from where it settled, nothing may break, no crystal may fall, and everything must
 *            be asleep at the end. Also how far anything moved while settling (a badly placed block shows here)
 *   solve    the bot plays it through the controls: it must bring down every crystal within the level's par
 *
 *   destructo_levelcheck [level ...]             (default: every level)
 *   destructo_levelcheck --control float|rest|solve   the controls: a floating block put into level 1, a plank
 *            hung off the edge of level 1's top, and a bot aiming 150 milliradians off on level 2; each must fail
 *            its check */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/jobs.h"
#include "game.h"

static game G;

static int floating(game *g, int *first) {
  int n = 0;
  *first = -1;
  for (int i = 0; i < g->nb; i++) {
    block *k = &g->b[i];
    if (!k->alive) continue;
    cw_xform3 x = cw_body3d_transform(k->body);
    /* the block's bottom face (blocks are placed upright: only turned about y) */
    t3_vec3 c = { x.p.x, x.p.y - k->half.y - 0.007f, x.p.z };
    cw_qshape3d q = cw_qshape3d_box(c, (t3_vec3){ fmaxf(0.01f, k->half.x - 0.02f), 0.008f, fmaxf(0.01f, k->half.z - 0.02f) }, x.q);
    cw_phys_filter f = cw_phys_filter_all();
    f.ignore_body = k->body.id;
    cw_shape3d hit[4];
    if (cw_world3d_overlap(g->w, &q, &f, hit, 4) == 0) {
      if (*first < 0) *first = i;
      n++;
    }
  }
  return n;
}

/* blocks placed into one another (they would burst apart when the level starts): each block shrunk by 1 cm must
 * touch no other */
static int overlapping(game *g, int *first) {
  int n = 0;
  *first = -1;
  for (int i = 0; i < g->nb; i++) {
    block *k = &g->b[i];
    if (!k->alive) continue;
    cw_xform3 x = cw_body3d_transform(k->body);
    cw_qshape3d q = cw_qshape3d_box(x.p, (t3_vec3){ fmaxf(0.005f, k->half.x - 0.01f), fmaxf(0.005f, k->half.y - 0.01f), fmaxf(0.005f, k->half.z - 0.01f) }, x.q);
    cw_phys_filter f = cw_phys_filter_layers(1u << 1); /* the block layer */
    f.ignore_body = k->body.id;
    cw_shape3d hit[4];
    if (cw_world3d_overlap(g->w, &q, &f, hit, 4) > 0) {
      if (*first < 0) *first = i;
      n++;
    }
  }
  return n;
}

static void check(int level, const char *control) {
  bool c_float = control && !strcmp(control, "float"), c_rest = control && !strcmp(control, "rest"), c_solve = control && !strcmp(control, "solve");
  game_new(&G, level, 1, MODE_COOP);
  if (c_float) game_add_block(&G, M_WOOD, (t3_vec3){ 1.0f, 4.2f, 3.5f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0);
  if (c_float) game_add_block(&G, M_STONE, (t3_vec3){ -1.0f, 0.15f, 2.0f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0), game_add_block(&G, M_STONE, (t3_vec3){ -0.8f, 0.2f, 2.0f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0); /* and two blocks in one another */
  if (c_rest) { /* a 3 m post in the yard, a plank on it with its middle 1.8 m out over nothing: it must tip */
    game_add_block(&G, M_STONE, (t3_vec3){ 7.0f, 1.5f, 6.0f }, (t3_vec3){ 0.25f, 1.5f - 0.004f, 0.25f }, 0);
    game_add_block(&G, M_STONE, (t3_vec3){ 8.8f, 3.05f, 6.0f }, (t3_vec3){ 2.0f, 0.05f, 0.15f }, 0);
  }
  int first = -1;
  int nfloat = floating(&G, &first);
  int first_over = -1, nover = overlapping(&G, &first_over);
  if (getenv("LC_DEBUG") && first >= 0) { t3_vec3 p = cw_body3d_transform(G.b[first].body).p; fprintf(stderr, "first float %d %s at %.3f %.3f %.3f half %.3f %.3f %.3f\n", first, MATS[G.b[first].mat].name, p.x, p.y, p.z, G.b[first].half.x, G.b[first].half.y, G.b[first].half.z); }
  int blocks = G.nalive;
  /* settle, then rest */
  static t3_vec3 p0[MAXBLK];
  for (int i = 0; i < G.nb; i++) p0[i] = G.b[i].alive ? cw_body3d_transform(G.b[i].body).p : (t3_vec3){ 0, 0, 0 };
  if (getenv("LC_TRACE")) for (int t = 0; t < 120; t++) { cw_world3d_step(G.w, TICK); float vm = 0; int vi = -1; for (int i = 0; i < G.nb; i++) { if (!G.b[i].alive) continue; t3_vec3 v = cw_body3d_velocity(G.b[i].body); float s = sqrtf(v.x*v.x+v.y*v.y+v.z*v.z); if (s > vm) vm = s, vi = i; } if (t % 5 == 0 || vm > 1) { t3_vec3 p = cw_body3d_transform(G.b[vi].body).p; fprintf(stderr, "t%d fastest %d %s %.3f at %.2f %.2f %.2f joints %d\n", t, vi, MATS[G.b[vi].mat].name, vm, p.x, p.y, p.z, cw_world3d_stats(G.w).joints); if (vm > 1) break; } }
  game_settle(&G, 180);
  float settle_move = 0;
  int settle_worst = -1;
  for (int i = 0; i < G.nb; i++) {
    if (!G.b[i].alive) continue;
    t3_vec3 p = G.b[i].home;
    float d = sqrtf((p.x - p0[i].x) * (p.x - p0[i].x) + (p.y - p0[i].y) * (p.y - p0[i].y) + (p.z - p0[i].z) * (p.z - p0[i].z));
    if (d > settle_move) settle_move = d, settle_worst = i;
  }
  int broken_settle = G.broken;
  if (getenv("LC_DEBUG") && settle_worst >= 0) fprintf(stderr, "settle worst %d: from %.3f %.3f %.3f to %.3f %.3f %.3f half %.3f %.3f %.3f\n", settle_worst, p0[settle_worst].x, p0[settle_worst].y, p0[settle_worst].z, G.b[settle_worst].home.x, G.b[settle_worst].home.y, G.b[settle_worst].home.z, G.b[settle_worst].half.x, G.b[settle_worst].half.y, G.b[settle_worst].half.z);
  if (getenv("LC_DEBUG")) for (int i = 0; i < G.nb; i++) { if (!G.b[i].alive) continue; t3_vec3 p = G.b[i].home; float d = fabsf(p.y - p0[i].y) + fabsf(p.x - p0[i].x) + fabsf(p.z - p0[i].z); if (d > 0.02f) fprintf(stderr, "  moved %d %s %.3f %.3f %.3f -> %.3f %.3f %.3f\n", i, MATS[G.b[i].mat].name, p0[i].x, p0[i].y, p0[i].z, p.x, p.y, p.z); }
  input none = { 0 };
  int st = G.state;
  G.state = GS_SETTLE; /* no aiming input, no turn ends: the world alone */
  float drift = 0;
  int worst = -1;
  for (int t = 0; t < 3600; t++) {
    cw_world3d_step(G.w, TICK);
    (void)none;
  }
  G.state = st;
  for (int i = 0; i < G.nb; i++) {
    if (!G.b[i].alive) continue;
    t3_vec3 p = cw_body3d_transform(G.b[i].body).p, h = G.b[i].home;
    float d = sqrtf((p.x - h.x) * (p.x - h.x) + (p.y - h.y) * (p.y - h.y) + (p.z - h.z) * (p.z - h.z));
    if (d > drift) drift = d, worst = i;
  }
  int awake = game_awake(&G);
  if (getenv("LC_DEBUG")) { float vm = 0; int vi = -1; for (int i = 0; i < G.nb; i++) { if (!G.b[i].alive) continue; t3_vec3 v = cw_body3d_velocity(G.b[i].body), w = cw_body3d_angular_velocity(G.b[i].body); float s = sqrtf(v.x*v.x+v.y*v.y+v.z*v.z) + 0.3f * sqrtf(w.x*w.x+w.y*w.y+w.z*w.z); if (s > vm) vm = s, vi = i; } if (vi >= 0) { t3_vec3 p = cw_body3d_transform(G.b[vi].body).p; fprintf(stderr, "fastest at end %d %s %.4f m/s at %.2f %.2f %.2f; drift worst %d\n", vi, MATS[G.b[vi].mat].name, vm, p.x, p.y, p.z, worst); } }
  int tdown = 0;
  for (int t = 0; t < G.ntgt; t++) {
    target_state *ts = &G.tgt[t];
    if (ts->blk < 0) { tdown++; continue; }
    t3_vec3 p = cw_body3d_transform(G.b[ts->blk].body).p;
    if (fabsf(p.y - ts->home.y) > 0.05f || fabsf(p.x - ts->home.x) > 0.05f || fabsf(p.z - ts->home.z) > 0.05f) tdown++;
  }
  /* solve: a fresh game, the bot on the controls */
  game_new(&G, level, 1, MODE_COOP);
  game_settle(&G, 180);
  bot_state bot;
  memset(&bot, 0, sizeof bot);
  bot.yaw_jitter_control = c_solve ? 150 : 0;
  bot_reset(&bot);
  int ticks = 0, max_awake = 0;
  float step_max = 0;
  while (!game_over(&G) && ticks < 60 * 60 * 6) {
    input in;
    bot_input(&G, &bot, &in);
    if (getenv("LC_DEBUG") && in.fire) { int t = bot.plan_tgt; t3_vec3 tp = G.tgt[t].home; fprintf(stderr, "shot %d: target %d (home %.2f %.2f %.2f) ammo %s yaw %.4f pitch %.4f speed %.1f targets left %d\n", G.shots + 1, t, tp.x, tp.y, tp.z, AMMO[G.ammo_sel].name, G.yaw, G.pitch, game_speed(&G), G.tgt_left); }
    { static int was; if (getenv("LC_DEBUG") && was == GS_FLIGHT && G.state != GS_FLIGHT) fprintf(stderr, "   first hit at %.2f %.2f %.2f (tick %d)\n", G.impact.x, G.impact.y, G.impact.z, G.proj_hit_tick); was = G.state; }
    game_step(&G, &in);
    ticks++;
    if (G.max_awake > max_awake) max_awake = G.max_awake;
  }
  step_max = G.step_ms_max;
  printf("{\"level\":%d,\"name\":\"%s\",\"kind\":%d,\"control\":\"%s\",\"blocks\":%d,\"targets\":%d,\"floating\":%d,\"first_float\":%d,\"overlapping\":%d,\"first_over\":%d,"
         "\"settle_move_mm\":%.2f,\"settle_worst\":\"%s %d y%.2f\",\"broken_settle\":%d,\"drift_mm\":%.3f,\"worst\":%d,\"awake_end\":%d,\"targets_moved\":%d,"
         "\"solved\":%s,\"shots\":%d,\"par\":%d,\"stars\":%d,\"bot_ticks\":%d,\"broken\":%d,\"fragments\":%d,\"max_awake\":%d,\"step_ms_max\":%.2f}\n",
         level, G.L->name, G.L->kind, control ? control : "", blocks, G.ntgt, nfloat, first, nover, first_over, settle_move * 1000, settle_worst >= 0 ? MATS[G.b[settle_worst].mat].name : "", settle_worst, settle_worst >= 0 ? p0[settle_worst].y : 0.0f, broken_settle, drift * 1000, worst, awake, tdown,
         G.state == GS_WON ? "true" : "false", G.shots, G.L->par, game_stars(&G), ticks, G.broken, G.fragments, max_awake, step_max);
  fflush(stdout);
}

int main(int argc, char **argv) {
  cw_jobs_init(-1);
  if (argc > 2 && !strcmp(argv[1], "--control")) {
    const char *c = argv[2];
    check(!strcmp(c, "solve") ? 1 : 0, c);
    game_free(&G);
    return 0;
  }
  if (argc > 1)
    for (int a = 1; a < argc; a++) check(atoi(argv[a]), NULL);
  else
    for (int l = 0; l < NLEVELS; l++) check(l, NULL);
  game_free(&G);
  return 0;
}
