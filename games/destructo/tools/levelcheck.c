/* destructo_levelcheck: every level, checked three ways (native, no window, no GPU). One JSON line a level;
 * tests/destructo-levels.mjs judges them.
 *
 *   floats   every block as placed stands on something: a thin box just under its bottom face must touch another
 *            block or the ground
 *   rest     the level settles for 3 s (as the game does before play), then is left alone for 60 s: no block may
 *            move more than 5 mm from where it settled, nothing may break, no crystal may fall, and everything must
 *            be asleep at the end. Also how far anything moved while settling (a badly placed block shows here)
 *   solve    the bot plays it through the controls: it must bring down every crystal within the level's par
 *   result   the level is judged once the world is still: when the game says won or lost, at most 10 bodies are
 *            awake, and 5 s later no more than 2% more of the structure has come down. The stars and the headline
 *            are checked against this tool's own count of the structure 5 s after the judgement (every block the level
 *            placed, by volume: gone, or more than its own size and at least 0.6 m from where it settled): three
 *            stars or "brought down" only with the level's mark of it down (game_mark: a half, a third ... by kit)
 *
 *   destructo_levelcheck [level ...]             (default: every level)
 *   destructo_levelcheck --control float|rest|solve|early|stars   the controls: a floating block put into level 1, a
 *            plank hung off the edge of level 1's top, a bot aiming 150 milliradians off on level 2, and the result
 *            rules before the fix (judged 1.5 s after the last crystal fell, stars by shots alone) on Stone Giant
 *            (level 24, judged while it falls) and Garden Wall (level 3, three stars with the wall standing); each must
 *            fail its check
 *   destructo_levelcheck --kegs   two kegs 1.6 m apart in level 1's yard, the first set off: the second must go off
 *            too (a chain); the control, 8 m apart, must not
 *   replay   the level's last shot replayed from the shadow game (replay.c: the level made again and stepped with the
 *            logged inputs) ends on the live game's judged hash. Control (--control replay, level 1): the replay's
 *            last shot fired 3 milliradians off must end on another hash */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/jobs.h"
#include "game.h"
#include "replay.h"
#include "opts.h"
#include "box3d/box3d.h"

static game G;

/* support runs up from the ground: a block is supported when it stands on the ground, or on a supported block, or
 * hangs on a whole weld from a supported block (a welded member stands on what it is welded to). under[i][...] are
 * the blocks under i (UNDER_MAX of them), on_ground[i] whether the ground is; sup[] out. */
#define UNDER_MAX 8
static int under[MAXBLK][UNDER_MAX], nunder[MAXBLK];
static bool on_ground[MAXBLK];
static void propagate(game *g, bool *sup) {
  for (int i = 0; i < g->nb; i++) sup[i] = g->b[i].alive && on_ground[i];
  for (bool grew = true; grew;) {
    grew = false;
    for (int i = 0; i < g->nb; i++) {
      if (sup[i] || !g->b[i].alive) continue;
      for (int u = 0; u < nunder[i] && !sup[i]; u++) if (sup[under[i][u]]) sup[i] = true, grew = true;
    }
    for (int w = 0; w < g->nweld; w++) {
      int a = g->weld[w].a, b = g->weld[w].b;
      if (sup[a] == sup[b] || !g->b[a].alive || !g->b[b].alive || !cw_joint3d_valid(g->weld[w].j)) continue;
      sup[a] = sup[b] = true, grew = true;
    }
  }
}
static int block_of(game *g, cw_body3d body) { intptr_t u = (intptr_t)cw_body3d_user(body); return u > 0 && u <= g->nb ? (int)u - 1 : -1; }
/* as built: what the block's bottom face touches (blocks are placed upright: only turned about y) */
static void under_as_built(game *g) {
  for (int i = 0; i < g->nb; i++) {
    block *k = &g->b[i];
    nunder[i] = 0, on_ground[i] = false;
    if (!k->alive) continue;
    cw_xform3 x = cw_body3d_transform(k->body);
    t3_vec3 c = { x.p.x, x.p.y - k->half.y - 0.007f, x.p.z };
    cw_qshape3d q = cw_qshape3d_box(c, (t3_vec3){ fmaxf(0.01f, k->half.x - 0.02f), 0.008f, fmaxf(0.01f, k->half.z - 0.02f) }, x.q);
    cw_phys_filter f = cw_phys_filter_all();
    f.ignore_body = k->body.id;
    cw_shape3d hit[32];
    int n = cw_world3d_overlap(g->w, &q, &f, hit, 32);
    for (int h = 0; h < n; h++) {
      cw_body3d ob = cw_shape3d_body(hit[h]);
      if (ob.id == g->ground.id) { on_ground[i] = true; continue; }
      int j = block_of(g, ob);
      if (j >= 0 && j != i && nunder[i] < UNDER_MAX) under[i][nunder[i]++] = j;
    }
  }
}
/* blocks that stand on nothing: no chain of blocks under or welds to the ground */
static int floating(game *g, int *first) {
  static bool sup[MAXBLK];
  under_as_built(g);
  propagate(g, sup);
  int n = 0;
  *first = -1;
  for (int i = 0; i < g->nb; i++) {
    if (!g->b[i].alive || sup[i]) continue;
    if (*first < 0) *first = i;
    n++;
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
      if (getenv("LC_DEBUG") && n < 6) { cw_body3d ob = cw_shape3d_body(hit[0]); t3_vec3 op = cw_body3d_transform(ob).p; fprintf(stderr, "overlap %d %s at %.3f %.3f %.3f half %.3f %.3f %.3f with body at %.3f %.3f %.3f\n", i, MATS[k->mat].name, x.p.x, x.p.y, x.p.z, k->half.x, k->half.y, k->half.z, op.x, op.y, op.z); }
      if (*first < 0) *first = i;
      n++;
    }
  }
  return n;
}

static void check(int level, const char *control) {
  bool c_float = control && !strcmp(control, "float"), c_rest = control && !strcmp(control, "rest"), c_solve = control && !strcmp(control, "solve");
  bool c_old = control && (!strcmp(control, "early") || !strcmp(control, "stars"));
  g_replay_perturb = control && !strcmp(control, "replay");
  game_new(&G, level, 1, MODE_COOP);
  if (c_float) game_add_block(&G, M_WOOD, (t3_vec3){ 1.0f, 4.2f, 3.5f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0);
  if (c_float) game_add_block(&G, M_STONE, (t3_vec3){ -1.0f, 0.15f, 2.0f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0), game_add_block(&G, M_STONE, (t3_vec3){ -0.8f, 0.2f, 2.0f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0); /* and two blocks in one another */
  if (c_rest) { /* a 3 m post in the yard, a plank on it with its middle 1.8 m out over nothing: it must tip */
    game_add_block(&G, M_STONE, (t3_vec3){ 7.0f, 1.5f, 6.0f }, (t3_vec3){ 0.25f, 1.5f - 0.004f, 0.25f }, 0);
    game_add_block(&G, M_STONE, (t3_vec3){ 8.8f, 3.05f, 6.0f }, (t3_vec3){ 2.0f, 0.05f, 0.15f }, 0);
  }
  if (getenv("LC_SIZES")) { float mn = 1e9f, mx = 0; int mi = -1; for (int i = 0; i < G.nb; i++) { if (!G.b[i].alive) continue; t3_vec3 h = G.b[i].half; float m = fminf(h.x, fminf(h.y, h.z)), M = fmaxf(h.x, fmaxf(h.y, h.z)); if (m < mn || m != m) mn = m, mi = i; if (M > mx) mx = M; } fprintf(stderr, "sizes: smallest half %.4f (block %d), largest %.3f\n", mn, mi, mx); }
  int first = -1;
  int nfloat = floating(&G, &first);
  int first_over = -1, nover = overlapping(&G, &first_over);
  if (getenv("LC_DEBUG") && first >= 0) { t3_vec3 p = cw_body3d_transform(G.b[first].body).p; fprintf(stderr, "first float %d %s at %.3f %.3f %.3f half %.3f %.3f %.3f\n", first, MATS[G.b[first].mat].name, p.x, p.y, p.z, G.b[first].half.x, G.b[first].half.y, G.b[first].half.z); }
  int blocks = G.nalive, nkegs = 0;
  for (int i = 0; i < G.nb; i++) nkegs += G.b[i].alive && G.b[i].keg;
  /* settle, then rest */
  static t3_vec3 p0[MAXBLK];
  for (int i = 0; i < G.nb; i++) p0[i] = G.b[i].alive ? cw_body3d_transform(G.b[i].body).p : (t3_vec3){ 0, 0, 0 };
  if (getenv("LC_TRACE")) for (int t = 0; t < 120; t++) { cw_world3d_step(G.w, TICK); float vm = 0; int vi = -1; for (int i = 0; i < G.nb; i++) { if (!G.b[i].alive) continue; t3_vec3 v = cw_body3d_velocity(G.b[i].body); float s = sqrtf(v.x*v.x+v.y*v.y+v.z*v.z); if (s > vm) vm = s, vi = i; } if (t % 5 == 0 || vm > 1) { t3_vec3 p = cw_body3d_transform(G.b[vi].body).p; fprintf(stderr, "t%d fastest %d %s %.3f at %.2f %.2f %.2f joints %d\n", t, vi, MATS[G.b[vi].mat].name, vm, p.x, p.y, p.z, cw_world3d_stats(G.w).joints); if (vm > 1) break; } }
  if (getenv("LC_WATCH")) { /* development: the tallest block's way over the first 20 s, a line a second */
    int top = -1;
    for (int i = 0; i < G.nb; i++) if (G.b[i].alive && (top < 0 || p0[i].y > p0[top].y)) top = i;
    for (int t = 0; t <= 1200; t++) {
      if (t % 60 == 0) { t3_vec3 p = cw_body3d_transform(G.b[top].body).p; float mv = 0; int wi = -1, big = 0; float lowbig = 99; for (int i = 0; i < G.nb; i++) { if (!G.b[i].alive) continue; t3_vec3 q = cw_body3d_transform(G.b[i].body).p; float d = sqrtf((q.x-p0[i].x)*(q.x-p0[i].x)+(q.y-p0[i].y)*(q.y-p0[i].y)+(q.z-p0[i].z)*(q.z-p0[i].z)); if (d > 0.3f) { big++; if (p0[i].y < lowbig) lowbig = p0[i].y; } if (d > mv) mv = d, wi = i; } fprintf(stderr, "t=%2ds top %d at %.3f %.3f %.3f (from %.3f %.3f %.3f) awake %d; most moved %d by %.3f from y %.2f; %d moved 0.3 m+, the lowest from y %.2f\n", t / 60, top, p.x, p.y, p.z, p0[top].x, p0[top].y, p0[top].z, game_awake(&G), wi, mv, wi >= 0 ? p0[wi].y : 0, big, lowbig); }
      cw_world3d_step(G.w, TICK);
    }
  }
  game_settle(&G, level_settle_ticks(G.L));
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
  G.old_rules = c_old;
  game_settle(&G, level_settle_ticks(G.L));
  /* the structure as it stands before the first shot: this tool's own record, for the result check */
  static cw_body3d placed[MAXBLK];
  static t3_vec3 placed_at[MAXBLK];
  static float placed_vol[MAXBLK], placed_lim[MAXBLK];
  int nplaced = 0;
  float vol_all = 0;
  for (int i = 0; i < G.nb; i++) {
    block *k = &G.b[i];
    if (!k->alive || !k->tag || k->target >= 0) continue;
    placed[nplaced] = k->body, placed_at[nplaced] = cw_body3d_transform(k->body).p, placed_vol[nplaced] = k->vol;
    placed_lim[nplaced] = fmaxf(0.6f, fmaxf(k->half.x, fmaxf(k->half.y, k->half.z)));
    vol_all += k->vol, nplaced++;
  }
  bot_state bot;
  memset(&bot, 0, sizeof bot);
  bot.yaw_jitter_control = c_solve ? 150 : 0;
  bot_reset(&bot);
  replay_reset(level, 1, MODE_COOP, false, level_settle_ticks(G.L));
  int ticks = 0, max_awake = 0;
  float step_max = 0;
  while (!game_over(&G) && ticks < 60 * 60 * 6) {
    input in;
    bot_input(&G, &bot, &in);
    if (getenv("LC_DEBUG") && in.fire) { int t = bot.plan_tgt; t3_vec3 tp = G.tgt[t].home; fprintf(stderr, "shot %d: target %d (home %.2f %.2f %.2f) ammo %s yaw %.4f pitch %.4f speed %.1f targets left %d\n", G.shots + 1, t, tp.x, tp.y, tp.z, AMMO[G.ammo_sel].name, G.yaw, G.pitch, game_speed(&G), G.tgt_left); }
    { static int was; if (getenv("LC_DEBUG") && was == GS_FLIGHT && G.state != GS_FLIGHT) fprintf(stderr, "   first hit at %.2f %.2f %.2f (tick %d)\n", G.impact.x, G.impact.y, G.impact.z, G.proj_hit_tick); was = G.state; }
    replay_log(&G, &in);
    game_step(&G, &in);
    replay_after_step(&G);
    replay_follow(&G, 1e9);
    ticks++;
    if (G.max_awake > max_awake) max_awake = G.max_awake;
  }
  step_max = G.step_ms_max;
  /* the replay of the last shot */
  int replay_ticks = 0;
  replay_follow(&G, 1e9);
  if (replay_ready()) {
    replay_start();
    while (replay_tick()) replay_ticks++;
  }
  /* the result: what the game judged, then the world 5 s on */
  int judged_awake = game_awake(&G), stars = game_stars(&G), result = game_result(&G);
  float share_judged = game_share(&G);
  input idle = { 0 };
  for (int t = 0; t < 300; t++) game_step(&G, &idle);
  float vol_down = 0;
  for (int i = 0; i < nplaced; i++) {
    bool down = !cw_body3d_valid(placed[i]);
    if (!down) {
      t3_vec3 p = cw_body3d_transform(placed[i]).p, a = placed_at[i];
      down = sqrtf((p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y) + (p.z - a.z) * (p.z - a.z)) > placed_lim[i];
    }
    if (down) vol_down += placed_vol[i];
  }
  float share_after = vol_all > 0 ? vol_down / vol_all : 0;
  printf("{\"judged_awake\":%d,\"share_judged\":%.4f,\"share_after\":%.4f,\"result\":%d,\"stars3\":%d,\"mark\":%.3f,\"replay\":%d,\"replay_ticks\":%d,", judged_awake, share_judged, share_after, result, stars == 3, game_mark(&G), replay_match(), replay_ticks);
  printf("\"level\":%d,\"name\":\"%s\",\"kind\":%d,\"control\":\"%s\",\"blocks\":%d,\"kegs\":%d,\"targets\":%d,\"floating\":%d,\"first_float\":%d,\"overlapping\":%d,\"first_over\":%d,"
         "\"settle_move_mm\":%.2f,\"settle_worst\":\"%s %d y%.2f\",\"broken_settle\":%d,\"drift_mm\":%.3f,\"worst\":%d,\"awake_end\":%d,\"targets_moved\":%d,"
         "\"solved\":%s,\"shots\":%d,\"par\":%d,\"stars\":%d,\"bot_ticks\":%d,\"broken\":%d,\"fragments\":%d,\"max_awake\":%d,\"step_ms_max\":%.2f}\n",
         level, G.L->name, G.L->kind, control ? control : "", blocks, nkegs, G.ntgt, nfloat, first, nover, first_over, settle_move * 1000, settle_worst >= 0 ? MATS[G.b[settle_worst].mat].name : "", settle_worst, settle_worst >= 0 ? p0[settle_worst].y : 0.0f, broken_settle, drift * 1000, worst, awake, tdown,
         G.state == GS_WON ? "true" : "false", G.shots, G.L->par, stars, ticks, G.broken, G.fragments, max_awake, step_max);
  fflush(stdout);
}

/* a block resting on something: a contact under it (the normal from it to the other pointing down) */
/* after the settle: what the block has a contact under it with (the ground, or blocks), or what its bottom face lies on
 * within a hair (the probe as built: a brick in a wall held by its neighbours can sit a hair over the one below), then
 * support run up the same way */
static void resting(game *g, bool *sup) {
  under_as_built(g);
  for (int i = 0; i < g->nb; i++) {
    if (!g->b[i].alive) continue;
    cw_contact3d c[16];
    int n = cw_body3d_contacts(g->b[i].body, c, 16);
    for (int k = 0; k < n; k++) {
      if (c[k].normal.y >= -0.5f) continue;
      if (c[k].other.id == g->ground.id) { on_ground[i] = true; continue; }
      int j = block_of(g, c[k].other);
      if (j >= 0 && j != i && nunder[i] < UNDER_MAX) under[i][nunder[i]++] = j;
    }
  }
  propagate(g, sup);
}

/* --landmarks: every landmark level, as the game starts it: as built every block stands on something (its bottom face
 * on a block or the ground) and none is placed inside another; after the level's settle every block lies on something
 * (a contact under it) and nothing moved more than 0.6 m settling; and the bot clears it within par.
 * Support runs up from the ground: a block stands when it is on the ground, on a block that stands, or welded (a whole
 * weld) to one; a welded member stands on what it is welded to.
 * Controls: float (a block put 1 m over the first landmark's highest block once it has settled, unstepped: nothing
 * under it; and one put in the air as built), island (two blocks welded to each other and to nothing, put there the
 * same way: welds alone hold nothing up), solve (the bot aiming 150 milliradians off) */
static void landmark_check(int level, const char *control) {
  if (control && !strcmp(control, "oldwelds")) opt_set("OLD_WELDS", "1"); /* the control: the welds as they were */
  bool c_float = control && !strcmp(control, "float"), c_solve = control && !strcmp(control, "solve"), c_island = control && !strcmp(control, "island");
  game_new(&G, level, 1, MODE_COOP);
  if (c_float) game_add_block(&G, M_STONE, (t3_vec3){ 0.3f, G.top + 2.0f, 0.2f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0);
  if (c_island) { /* two blocks welded to each other and to nothing else, in the air: welds alone hold nothing up */
    int a = game_add_block(&G, M_STONE, (t3_vec3){ 0.3f, G.top + 2.0f, 0.2f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0);
    int b = game_add_block(&G, M_STONE, (t3_vec3){ 0.3f, G.top + 2.3f, 0.2f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0);
    game_weld(&G, a, b);
  }
  int first = -1, nfloat = floating(&G, &first), first_over = -1, nover = overlapping(&G, &first_over);
  t3_vec3 over_at = first_over >= 0 ? cw_body3d_transform(G.b[first_over].body).p : (t3_vec3){ 0, 0, 0 };
  static t3_vec3 p0[MAXBLK];
  for (int i = 0; i < G.nb; i++) p0[i] = G.b[i].alive ? cw_body3d_transform(G.b[i].body).p : (t3_vec3){ 0, 0, 0 };
  game_settle(&G, level_settle_ticks(G.L));
  if (c_float || c_island) { /* the block (the welded pair) now hangs over the structure, where nothing holds it */
    float top = 0;
    for (int i = 0; i < G.nb; i++) if (G.b[i].alive) top = fmaxf(top, cw_body3d_transform(G.b[i].body).p.y);
    int a = game_add_block(&G, M_STONE, (t3_vec3){ 0.3f, top + 1.0f, 0.2f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0);
    if (c_island) game_weld(&G, a, game_add_block(&G, M_STONE, (t3_vec3){ 0.3f, top + 1.3f, 0.2f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0));
  }
  static bool sup[MAXBLK];
  resting(&G, sup);
  int unrested = 0, first_un = -1, moved_i = -1;
  float moved = 0;
  for (int i = 0; i < G.nb; i++) {
    if (!G.b[i].alive) continue;
    if (!sup[i]) { unrested++; if (first_un < 0) first_un = i; }
    t3_vec3 p = cw_body3d_transform(G.b[i].body).p;
    if (i < MAXBLK && (p0[i].x != 0 || p0[i].y != 0 || p0[i].z != 0)) {
      float d = sqrtf((p.x - p0[i].x) * (p.x - p0[i].x) + (p.y - p0[i].y) * (p.y - p0[i].y) + (p.z - p0[i].z) * (p.z - p0[i].z));
      if (d > moved) moved = d, moved_i = i;
    }
  }
  t3_vec3 moved_from = moved_i >= 0 ? p0[moved_i] : (t3_vec3){ 0, 0, 0 }, moved_half = moved_i >= 0 ? G.b[moved_i].half : (t3_vec3){ 0, 0, 0 };
  int moved_mat = moved_i >= 0 ? G.b[moved_i].mat : -1;
  int blocks = G.nalive;
  /* a hit wakes the whole structure: wake it now and let it settle again; nothing may have moved more than 20 mm from where it was built */
  float woken = 0;
  if (!c_float && !c_island) {
    for (int i = 0; i < G.nb; i++) if (G.b[i].alive) b3Body_SetAwake(cw_body3d_raw(G.b[i].body), true);
    game_settle(&G, level_settle_ticks(G.L));
    for (int i = 0; i < G.nb; i++) {
      if (!G.b[i].alive || i >= MAXBLK || (p0[i].x == 0 && p0[i].y == 0 && p0[i].z == 0)) continue;
      t3_vec3 p = cw_body3d_transform(G.b[i].body).p;
      float d = sqrtf((p.x - p0[i].x) * (p.x - p0[i].x) + (p.y - p0[i].y) * (p.y - p0[i].y) + (p.z - p0[i].z) * (p.z - p0[i].z));
      if (d > woken) woken = d;
    }
  }
  /* the bot from the start */
  game_new(&G, level, 1, MODE_COOP);
  game_settle(&G, level_settle_ticks(G.L));
  bot_state bot;
  memset(&bot, 0, sizeof bot);
  bot.yaw_jitter_control = c_solve ? 150 : 0;
  bot_reset(&bot);
  int ticks = 0;
  while (!game_over(&G) && ticks < 60 * 60 * 6) {
    input in;
    bot_input(&G, &bot, &in);
    game_step(&G, &in);
    ticks++;
  }
  printf("{\"level\":%d,\"name\":\"%s\",\"control\":\"%s\",\"welded\":%s,\"blocks\":%d,\"floating\":%d,\"first_float\":%d,\"overlapping\":%d,\"over_at\":[%.3f,%.3f,%.3f],\"unrested\":%d,\"first_unrested\":%d,\"settle_move_mm\":%.1f,\"woken_move_mm\":%.1f,\"moved_from\":[%.3f,%.3f,%.3f],\"moved_half\":[%.3f,%.3f,%.3f],\"moved_mat\":%d,\"solved\":%s,\"shots\":%d,\"par\":%d,\"orbit\":%.3f}\n",
         level, G.L->name, control ? control : "", G.nweld > 0 ? "true" : "false", blocks, nfloat, first, nover, over_at.x, over_at.y, over_at.z, unrested, first_un, moved * 1000, woken * 1000, moved_from.x, moved_from.y, moved_from.z, moved_half.x, moved_half.y, moved_half.z, moved_mat, G.state == GS_WON ? "true" : "false", G.shots, G.L->par, G.orbit);
  fflush(stdout);
}


/* --rigid [--opt NAME=VALUE]... [--charge] [level ...]: how rigid a level stands and how it comes apart.
 *   settle   each placed block's move over the level's settle, from where it was built (max and how many past 20 mm)
 *   charge   (with --charge) a charge at the foot of the structure, then at 0.5 1 2 4 6 8 s: blocks alive, the largest
 *            clump (blocks joined by an unbroken weld, or touching and moving together), clumps over 6 blocks, the
 *            share of blocks in a clump of 2 or more, welds left, awake bodies; and for the first 1.5 s the biggest
 *            move of a block outside the blast (4.6 m + 0.3) that still has every weld it was built with: that move
 *            is the structure giving before the blast reaches it (0 when it is rigid) */
static bool getenv_series(void) { return opt_on("SERIES"); }
static t3_quat qm(t3_quat a, t3_quat b) { return (t3_quat){ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z }; }
static t3_vec3 qr(t3_quat q, t3_vec3 v) { t3_vec3 u = { q.x, q.y, q.z }, c = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x }, t = { 2 * c.x, 2 * c.y, 2 * c.z }, c2 = { u.y * t.z - u.z * t.y, u.z * t.x - u.x * t.z, u.x * t.y - u.y * t.x }; return (t3_vec3){ v.x + q.w * t.x + c2.x, v.y + q.w * t.y + c2.y, v.z + q.w * t.z + c2.z }; }
static int uf[MAXBLK];
static int ufind(int a) { while (uf[a] != a) a = uf[a] = uf[uf[a]]; return a; }
static void uunion(int a, int b) { a = ufind(a), b = ufind(b); if (a != b) uf[a] = b; }
static void clumps(game *g, int *largest, int *over6, float *attached, int *nalive, int *welds_left) {
  static int cnt[MAXBLK];
  for (int i = 0; i < g->nb; i++) uf[i] = i, cnt[i] = 0;
  int wl = 0;
  for (int w = 0; w < g->nweld; w++) {
    int a = g->weld[w].a, b = g->weld[w].b;
    if (!cw_joint3d_valid(g->weld[w].j)) continue;
    wl++;
    if (g->b[a].alive && g->b[b].alive) uunion(a, b);
  }
  for (int i = 0; i < g->nb; i++) {
    if (!g->b[i].alive) continue;
    t3_vec3 vi = cw_body3d_velocity(g->b[i].body);
    float si = sqrtf(vi.x * vi.x + vi.y * vi.y + vi.z * vi.z);
    if (si < 0.5f) continue;
    cw_contact3d c[16];
    int n = cw_body3d_contacts(g->b[i].body, c, 16);
    for (int k = 0; k < n; k++) {
      int j = block_of(g, c[k].other);
      if (j < 0 || !g->b[j].alive) continue;
      t3_vec3 vj = cw_body3d_velocity(g->b[j].body);
      float sj = sqrtf(vj.x * vj.x + vj.y * vj.y + vj.z * vj.z), dv = sqrtf((vi.x - vj.x) * (vi.x - vj.x) + (vi.y - vj.y) * (vi.y - vj.y) + (vi.z - vj.z) * (vi.z - vj.z));
      if (sj >= 0.5f && dv < 0.3f) uunion(i, j);
    }
  }
  int na = 0;
  for (int i = 0; i < g->nb; i++) if (g->b[i].alive) cnt[ufind(i)]++, na++;
  int big = 0, o6 = 0, att = 0;
  for (int i = 0; i < g->nb; i++) if (g->b[i].alive && ufind(i) == i) { if (cnt[i] > big) big = cnt[i]; if (cnt[i] > 6) o6++; if (cnt[i] >= 2) att += cnt[i]; }
  *largest = big, *over6 = o6, *attached = na ? (float)att / na : 0, *nalive = na, *welds_left = wl;
}
static void rigid_check(int level, bool charge) {
  game_new(&G, level, 1, MODE_COOP);
  static t3_vec3 p0[MAXBLK];
  static uint8_t welds0[MAXBLK];
  memset(welds0, 0, sizeof welds0);
  for (int i = 0; i < G.nb; i++) p0[i] = G.b[i].alive ? cw_body3d_transform(G.b[i].body).p : (t3_vec3){ 0, 0, 0 };
  for (int w = 0; w < G.nweld; w++) welds0[G.weld[w].a]++, welds0[G.weld[w].b]++;
  int nw0 = G.nweld, blocks0 = G.nalive;
  if (opt_on("WAKE")) for (int i = 0; i < G.nb; i++) if (G.b[i].alive) b3Body_SetAwake(cw_body3d_raw(G.b[i].body), true); /* a hit wakes the whole structure: measure what it does awake */
  {
    int stt = G.state;
    G.state = GS_SETTLE;
    for (int t = 1; t <= level_settle_ticks(G.L); t++) {
      cw_world3d_step(G.w, TICK);
      if (t % 30 == 0 && getenv_series()) {
        double sb = 0, st2 = 0, sa = 0, mm = 0; int nb2 = 0, nt = 0, na2 = 0;
        for (int i = 0; i < G.nb; i++) {
          if (!G.b[i].alive) continue;
          t3_vec3 p = cw_body3d_transform(G.b[i].body).p;
          float d = sqrtf((p.x - p0[i].x) * (p.x - p0[i].x) + (p.y - p0[i].y) * (p.y - p0[i].y) + (p.z - p0[i].z) * (p.z - p0[i].z));
          sa += d, na2++; if (d > mm) mm = d;
          if (p0[i].y < 1.0f) sb += d, nb2++;
          if (p0[i].y > G.top * 0.8f) st2 += d, nt++;
        }
        printf("{\"level\":%d,\"phase\":\"series\",\"tick\":%d,\"max_mm\":%.1f,\"mean_mm\":%.1f,\"base_mm\":%.1f,\"top_mm\":%.1f,\"awake\":%d}\n", level, t, mm * 1000, na2 ? sa / na2 * 1000 : 0, nb2 ? sb / nb2 * 1000 : 0, nt ? st2 / nt * 1000 : 0, game_awake(&G));
      }
    }
    G.state = stt;
    game_settle_finish(&G);
  }
  float mx = 0, mean = 0; int over20 = 0, worst = -1, n = 0;
  for (int i = 0; i < G.nb; i++) {
    if (!G.b[i].alive) continue;
    t3_vec3 p = cw_body3d_transform(G.b[i].body).p;
    float d = sqrtf((p.x - p0[i].x) * (p.x - p0[i].x) + (p.y - p0[i].y) * (p.y - p0[i].y) + (p.z - p0[i].z) * (p.z - p0[i].z));
    mean += d, n++;
    if (d > 0.02f) over20++;
    if (d > mx) mx = d, worst = i;
  }
  printf("{\"level\":%d,\"name\":\"%s\",\"phase\":\"settle\",\"blocks\":%d,\"welds\":%d,\"settle_max_mm\":%.1f,\"settle_mean_mm\":%.2f,\"over20mm\":%d,\"worst_y\":%.2f,\"worst_mat\":%d,\"awake\":%d,\"height\":%.2f}\n",
         level, G.L->name, blocks0, nw0, mx * 1000, n ? mean / n * 1000 : 0, over20, worst >= 0 ? p0[worst].y : 0, worst >= 0 ? G.b[worst].mat : -1, game_awake(&G), G.top);
  fflush(stdout);
  if (!charge) return;
  static t3_vec3 q0[MAXBLK];
  static t3_quat r0[MAXBLK];
  static float dist[MAXBLK];
  float cx = 0, cz = 0; int cn = 0;
  for (int i = 0; i < G.nb; i++) if (G.b[i].alive && G.b[i].tag == 1 && G.b[i].home.y < 1.5f) cx += G.b[i].home.x, cz += G.b[i].home.z, cn++;
  t3_vec3 at = { cn ? cx / cn : 0, 0.5f, cn ? cz / cn : 0 };
  int base_n = 0;
  static uint8_t id0[MAXBLK];
  for (int i = 0; i < G.nb; i++) { q0[i] = G.b[i].alive ? cw_body3d_transform(G.b[i].body).p : (t3_vec3){ 0, 0, 0 }; r0[i] = G.b[i].alive ? cw_body3d_transform(G.b[i].body).q : (t3_quat){ 0, 0, 0, 1 }; dist[i] = sqrtf((q0[i].x - at.x) * (q0[i].x - at.x) + (q0[i].y - at.y) * (q0[i].y - at.y) + (q0[i].z - at.z) * (q0[i].z - at.z)); id0[i] = G.b[i].alive; if (G.b[i].alive) base_n++; }
  G.state = GS_SETTLE;
  game_charge_at(&G, at);
  input none = { 0 };
  float rig = 0; int rig_i = -1, rig_over50 = 0; static bool rig_seen[MAXBLK];
  memset(rig_seen, 0, sizeof rig_seen);
  double ms_sum = 0, ms_max = 0; int ms_n = 0;
  const int snaps[6] = { 30, 60, 120, 240, 360, 480 };
  int si = 0;
  for (int t = 1; t <= 480; t++) {
    game_step(&G, &none);
    ms_sum += G.tick_ms, ms_n++; if (G.tick_ms > ms_max) ms_max = G.tick_ms;
    static uint8_t vwc[MAXBLK];
    if (t <= 30) {
      memset(vwc, 0, sizeof vwc);
      for (int k = 0; k < G.nweld; k++) if (cw_joint3d_valid(G.weld[k].j)) vwc[G.weld[k].a]++, vwc[G.weld[k].b]++;
    }
    if (t <= 30) { /* the structure giving: each member against its piece's anchor (the lowest member of the piece outside the blast), as that piece's turn and move would put it */
      static int anc[MAXBLK];
      for (int i = 0; i < G.nb; i++) uf[i] = i, anc[i] = -1;
      for (int k = 0; k < G.nweld; k++) if (cw_joint3d_valid(G.weld[k].j) && G.b[G.weld[k].a].alive && G.b[G.weld[k].b].alive) uunion(G.weld[k].a, G.weld[k].b);
      for (int i = 0; i < G.nb; i++) if (id0[i] && G.b[i].alive && !G.b[i].gen && dist[i] >= 4.9f && vwc[i] > 0) { int r = ufind(i); if (anc[r] < 0 || q0[i].y < q0[anc[r]].y) anc[r] = i; }
      for (int i = 0; i < G.nb; i++) {
        if (!id0[i] || !G.b[i].alive || dist[i] < 4.9f || G.b[i].gen || vwc[i] == 0) continue;
        int a = anc[ufind(i)];
        if (a < 0 || a == i) continue;
        cw_xform3 xa = cw_body3d_transform(G.b[a].body), xi = cw_body3d_transform(G.b[i].body);
        t3_quat Ra = qm(xa.q, (t3_quat){ -r0[a].x, -r0[a].y, -r0[a].z, r0[a].w });
        t3_vec3 rel0 = { q0[i].x - q0[a].x, q0[i].y - q0[a].y, q0[i].z - q0[a].z }, e = qr(Ra, rel0);
        float dx = (xi.p.x - xa.p.x) - e.x, dy = (xi.p.y - xa.p.y) - e.y, dz = (xi.p.z - xa.p.z) - e.z, d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > rig) rig = d, rig_i = i;
        if (d > 0.05f && !rig_seen[i]) rig_seen[i] = true, rig_over50++;
      }
    }
    if (si < 6 && t == snaps[si]) {
      int big, o6, na, wl; float att;
      clumps(&G, &big, &o6, &att, &na, &wl);
      static float ds[MAXBLK]; int nd = 0;
      for (int i = 0; i < G.nb; i++) if (G.b[i].alive) { t3_vec3 p = cw_body3d_transform(G.b[i].body).p; ds[nd++] = sqrtf((p.x - at.x) * (p.x - at.x) + (p.z - at.z) * (p.z - at.z)); }
      for (int a = 1; a < nd; a++) { float v = ds[a]; int b = a - 1; while (b >= 0 && ds[b] > v) ds[b + 1] = ds[b], b--; ds[b + 1] = v; }
      printf("{\"level\":%d,\"name\":\"%s\",\"phase\":\"charge\",\"t\":%.1f,\"alive\":%d,\"of\":%d,\"largest\":%d,\"over6\":%d,\"attached\":%.3f,\"welds\":%d,\"welds0\":%d,\"awake\":%d,\"spread90\":%.2f,\"height\":%.2f,\"rigid_mm\":%.1f,\"rigid_over50mm\":%d,\"rigid_y\":%.2f,\"tick_ms_mean\":%.2f,\"tick_ms_max\":%.2f}\n",
             level, G.L->name, t / 60.0f, na, base_n, big, o6, att, wl, nw0, game_awake(&G), nd ? ds[(int)(nd * 0.9f)] : 0, G.top, rig * 1000, rig_over50, rig_i >= 0 ? q0[rig_i].y : 0, ms_n ? ms_sum / ms_n : 0, ms_max);
      fflush(stdout);
      si++;
    }
  }
}

/* kegs: two powder kegs in level 1's yard, gap metres apart; the first is set off; how many blasts follow */
static void kegs(float gap) {
  game_new(&G, 0, 1, MODE_COOP);
  int a = game_add_keg(&G, 7.0f, 6.0f);
  game_add_keg(&G, 7.0f + gap, 6.0f);
  game_settle(&G, 60);
  game_hit_block(&G, a, 1e6f);
  input none = { 0 };
  G.state = GS_SETTLE;
  for (int t = 0; t < 240; t++) game_step(&G, &none);
  int kegs_left = 0;
  for (int i = 0; i < G.nb; i++) kegs_left += G.b[i].alive && G.b[i].keg;
  printf("{\"kegs_gap\":%.1f,\"booms\":%d,\"kegs_left\":%d}\n", gap, G.booms, kegs_left);
}

int main(int argc, char **argv) {
  cw_jobs_init(-1);
  for (int a = 1; a + 1 < argc;) { /* --opt NAME=VALUE anywhere: a run option (opts.h), then dropped from the arguments */
    if (!strcmp(argv[a], "--opt")) { char *eq = strchr(argv[a + 1], '='); if (eq) { *eq = 0; opt_set(argv[a + 1], eq + 1); } for (int b = a; b + 2 <= argc; b++) argv[b] = argv[b + 2]; argc -= 2; } else a++;
  }
  if (argc > 1 && !strcmp(argv[1], "--rigid")) {
    bool charge = false; int nl = 0;
    for (int a = 2; a < argc; a++) {
      if (!strcmp(argv[a], "--charge")) charge = true;
      else if (!strcmp(argv[a], "--opt") && a + 1 < argc) { char *eq = strchr(argv[++a], '='); if (eq) { *eq = 0; opt_set(argv[a], eq + 1); } }
      else nl++;
    }
    for (int a = 2; a < argc; a++) if (argv[a][0] >= '0' && argv[a][0] <= '9') rigid_check(atoi(argv[a]), charge);
    if (nl == 0) for (int l = 0; l < NLEVELS; l++) if (LEVELS[l].kind == K_LANDMARK) rigid_check(l, charge);
    game_free(&G);
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "--kegs")) { /* the chain, and its control (too far apart to set each other off) */
    kegs(1.6f), kegs(8.0f);
    game_free(&G);
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "--landmarks")) {
    const char *c = argc > 3 && !strcmp(argv[2], "--control") ? argv[3] : NULL;
    if (c && !strcmp(c, "oldwelds")) landmark_check(1, c); /* the control: the Needle with the welds as they were */
    else
      for (int l = 0; l < NLEVELS; l++) {
        if (LEVELS[l].kind != K_LANDMARK) continue;
        landmark_check(l, c);
        if (c) break; /* a control: the first landmark */
      }
    game_free(&G);
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "--control")) {
    const char *c = argv[2];
    /* by name: the levels before them move as levels are added */
    int early = level_find("Stone Giant"), stars = level_find("Garden Wall");
    check(!strcmp(c, "solve") ? 1 : !strcmp(c, "replay") ? 0 : !strcmp(c, "early") ? (early >= 0 ? early : 23) : !strcmp(c, "stars") ? (stars >= 0 ? stars : 2) : 0, c);
    game_free(&G);
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
