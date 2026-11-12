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
 *            stars or "brought down" only with the level's mark of it down (game_mark: each landmark's own)
 *
 *   destructo_levelcheck [level ...]             (default: every level)
 *   destructo_levelcheck --control float|rest|solve|stars|settle   the controls: a floating block put into level 1, a
 *            plank hung off the edge of level 1's top, a bot aiming 150 milliradians off on level 2, and the result
 *            rules before the fix (judged 1.5 s after the last crystal fell, stars by shots alone) on Hagia Sophia (three
 *            stars with the building standing), and the old wait (judged only once the world is still) on the
 *            Colosseum; each must fail its check
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
      if (sup[a] == sup[b] || !g->b[a].alive || !g->b[b].alive) continue;
      sup[a] = sup[b] = true, grew = true;
    }
  }
}
static int block_of(game *g, cw_shape3d s) { (void)g; return game_block_of_shape(s); }
/* as built: what the block's bottom face touches (blocks are placed upright: only turned about y) */
static void under_as_built(game *g) {
  for (int i = 0; i < g->nb; i++) {
    block *k = &g->b[i];
    nunder[i] = 0, on_ground[i] = false;
    if (!k->alive) continue;
    cw_xform3 x = game_block_xform(g, i);
    t3_vec3 c = { x.p.x, x.p.y - k->half.y - 0.007f, x.p.z };
    cw_qshape3d q = cw_qshape3d_box(c, (t3_vec3){ fmaxf(0.01f, k->half.x - 0.02f), 0.008f, fmaxf(0.01f, k->half.z - 0.02f) }, x.q);
    cw_phys_filter f = cw_phys_filter_all();
    cw_shape3d hit[32];
    int n = cw_world3d_overlap(g->w, &q, &f, hit, 32);
    for (int h = 0; h < n; h++) {
      cw_body3d ob = cw_shape3d_body(hit[h]);
      if (ob.id == g->ground.id) { on_ground[i] = true; continue; }
      int j = block_of(g, hit[h]);
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
    cw_xform3 x = game_block_xform(g, i);
    cw_qshape3d q = cw_qshape3d_box(x.p, (t3_vec3){ fmaxf(0.005f, k->half.x - 0.01f), fmaxf(0.005f, k->half.y - 0.01f), fmaxf(0.005f, k->half.z - 0.01f) }, x.q);
    cw_phys_filter f = cw_phys_filter_layers(1u << 1); /* the block layer */
    cw_shape3d hit[16];
    int nh = cw_world3d_overlap(g->w, &q, &f, hit, 16), others = 0;
    for (int h = 0; h < nh; h++) others += game_block_of_shape(hit[h]) != i;
    if (others > 0) {
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
  if (getenv("LC_DEBUG") && first >= 0) { t3_vec3 p = game_block_xform(&G, first).p; fprintf(stderr, "first float %d %s at %.3f %.3f %.3f half %.3f %.3f %.3f\n", first, MATS[G.b[first].mat].name, p.x, p.y, p.z, G.b[first].half.x, G.b[first].half.y, G.b[first].half.z); }
  int blocks = G.nalive;
  /* settle, then rest */
  static t3_vec3 p0[MAXBLK];
  for (int i = 0; i < G.nb; i++) p0[i] = G.b[i].alive ? game_block_xform(&G, i).p : (t3_vec3){ 0, 0, 0 };
  if (getenv("LC_TRACE")) for (int t = 0; t < 120; t++) { cw_world3d_step(G.w, TICK); float vm = 0; int vi = -1; for (int i = 0; i < G.nb; i++) { if (!G.b[i].alive) continue; t3_vec3 v = game_block_vel(&G, i); float s = sqrtf(v.x*v.x+v.y*v.y+v.z*v.z); if (s > vm) vm = s, vi = i; } if (t % 5 == 0 || vm > 1) { t3_vec3 p = game_block_xform(&G, vi).p; fprintf(stderr, "t%d fastest %d %s %.3f at %.2f %.2f %.2f joints %d\n", t, vi, MATS[G.b[vi].mat].name, vm, p.x, p.y, p.z, cw_world3d_stats(G.w).joints); if (vm > 1) break; } }
  if (getenv("LC_WATCH")) { /* development: the tallest block's way over the first 20 s, a line a second */
    int top = -1;
    for (int i = 0; i < G.nb; i++) if (G.b[i].alive && (top < 0 || p0[i].y > p0[top].y)) top = i;
    for (int t = 0; t <= 1200; t++) {
      if (t % 60 == 0) { t3_vec3 p = game_block_xform(&G, top).p; float mv = 0; int wi = -1, big = 0; float lowbig = 99; for (int i = 0; i < G.nb; i++) { if (!G.b[i].alive) continue; t3_vec3 q = game_block_xform(&G, i).p; float d = sqrtf((q.x-p0[i].x)*(q.x-p0[i].x)+(q.y-p0[i].y)*(q.y-p0[i].y)+(q.z-p0[i].z)*(q.z-p0[i].z)); if (d > 0.3f) { big++; if (p0[i].y < lowbig) lowbig = p0[i].y; } if (d > mv) mv = d, wi = i; } fprintf(stderr, "t=%2ds top %d at %.3f %.3f %.3f (from %.3f %.3f %.3f) awake %d; most moved %d by %.3f from y %.2f; %d moved 0.3 m+, the lowest from y %.2f\n", t / 60, top, p.x, p.y, p.z, p0[top].x, p0[top].y, p0[top].z, game_awake(&G), wi, mv, wi >= 0 ? p0[wi].y : 0, big, lowbig); }
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
    t3_vec3 p = game_block_xform(&G, i).p, h = G.b[i].home;
    float d = sqrtf((p.x - h.x) * (p.x - h.x) + (p.y - h.y) * (p.y - h.y) + (p.z - h.z) * (p.z - h.z));
    if (d > drift) drift = d, worst = i;
  }
  int awake = game_awake(&G);
  if (getenv("LC_DEBUG")) { float vm = 0; int vi = -1; for (int i = 0; i < G.nb; i++) { if (!G.b[i].alive) continue; t3_vec3 v = game_block_vel(&G, i), w = cw_body3d_angular_velocity(G.ch[G.b[i].chunk].body); float s = sqrtf(v.x*v.x+v.y*v.y+v.z*v.z) + 0.3f * sqrtf(w.x*w.x+w.y*w.y+w.z*w.z); if (s > vm) vm = s, vi = i; } if (vi >= 0) { t3_vec3 p = game_block_xform(&G, vi).p; fprintf(stderr, "fastest at end %d %s %.4f m/s at %.2f %.2f %.2f; drift worst %d\n", vi, MATS[G.b[vi].mat].name, vm, p.x, p.y, p.z, worst); } }
  int tdown = 0;
  for (int t = 0; t < G.ntgt; t++) {
    target_state *ts = &G.tgt[t];
    if (ts->blk < 0) { tdown++; continue; }
    t3_vec3 p = game_block_xform(&G, ts->blk).p;
    if (fabsf(p.y - ts->home.y) > 0.05f || fabsf(p.x - ts->home.x) > 0.05f || fabsf(p.z - ts->home.z) > 0.05f) tdown++;
  }
  /* solve: a fresh game, the bot on the controls */
  game_new(&G, level, 1, MODE_COOP);
  G.old_rules = c_old;
  G.old_settle = control && !strcmp(control, "settle");
  game_settle(&G, level_settle_ticks(G.L));
  /* the structure as it stands before the first shot: this tool's own record, for the result check */
  static int placed[MAXBLK];
  static t3_vec3 placed_at[MAXBLK];
  static float placed_vol[MAXBLK], placed_lim[MAXBLK];
  int nplaced = 0;
  float vol_all = 0;
  for (int i = 0; i < G.nb; i++) {
    block *k = &G.b[i];
    if (!k->alive || !k->tag || k->target >= 0) continue;
    placed[nplaced] = i, placed_at[nplaced] = game_block_xform(&G, i).p, placed_vol[nplaced] = k->vol;
    placed_lim[nplaced] = fmaxf(0.6f, fmaxf(k->half.x, fmaxf(k->half.y, k->half.z)));
    vol_all += k->vol, nplaced++;
  }
  bot_state bot;
  memset(&bot, 0, sizeof bot);
  bot.yaw_jitter_control = c_solve ? 400 : 0;
  bot_reset(&bot);
  replay_reset(level, 1, MODE_COOP, false, level_settle_ticks(G.L));
  int ticks = 0, max_awake = 0;
  float step_max = 0;
  while (!game_over(&G) && ticks < 60 * 60 * 6) {
    input in;
    bot_input(&G, &bot, &in);
    if (opt_on("LC_DEBUG") && in.fire) { int t = bot.plan_tgt; t3_vec3 tp = G.tgt[t].home; printf("shot %d: target %d (home %.2f %.2f %.2f) ammo %s yaw %.4f pitch %.4f speed %.1f targets left %d\n", G.shots + 1, t, tp.x, tp.y, tp.z, AMMO[G.ammo_sel].name, G.yaw, G.pitch, game_speed(&G), G.tgt_left); }
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
  int judge_lag = G.win_tick ? G.judge_tick - G.win_tick : 0;
  int judged_awake = game_awake(&G), result = game_result(&G), stars;
  float share_judged = game_share(&G);
  input idle = { 0 };
  for (int t = 0; t < 480; t++) game_step(&G, &idle);
  stars = game_stars(&G); /* the rating has read the rubble till it was still */
  float vol_down = 0;
  for (int i = 0; i < nplaced; i++) {
    bool down = !(G.b[placed[i]].alive && !G.b[placed[i]].gen);
    if (!down) {
      t3_vec3 p = game_block_xform(&G, placed[i]).p, a = placed_at[i];
      down = sqrtf((p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y) + (p.z - a.z) * (p.z - a.z)) > placed_lim[i];
    }
    if (down) vol_down += placed_vol[i];
  }
  float share_after = vol_all > 0 ? vol_down / vol_all : 0;
  printf("{\"judge_lag\":%d,\"judged_awake\":%d,\"share_judged\":%.4f,\"share_after\":%.4f,\"result\":%d,\"stars3\":%d,\"mark\":%.3f,\"replay\":%d,\"replay_ticks\":%d,", judge_lag, judged_awake, share_judged, share_after, result, stars == 3, game_mark(&G), replay_match(), replay_ticks);
  printf("\"level\":%d,\"name\":\"%s\",\"control\":\"%s\",\"blocks\":%d,\"targets\":%d,\"floating\":%d,\"first_float\":%d,\"overlapping\":%d,\"first_over\":%d,"
         "\"settle_move_mm\":%.2f,\"settle_worst\":\"%s %d y%.2f\",\"broken_settle\":%d,\"drift_mm\":%.3f,\"worst\":%d,\"awake_end\":%d,\"targets_moved\":%d,"
         "\"solved\":%s,\"shots\":%d,\"par\":%d,\"stars\":%d,\"bot_ticks\":%d,\"broken\":%d,\"fragments\":%d,\"max_awake\":%d,\"step_ms_max\":%.2f}\n",
         level, G.L->name, control ? control : "", blocks, G.ntgt, nfloat, first, nover, first_over, settle_move * 1000, settle_worst >= 0 ? MATS[G.b[settle_worst].mat].name : "", settle_worst, settle_worst >= 0 ? p0[settle_worst].y : 0.0f, broken_settle, drift * 1000, worst, awake, tdown,
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
    int n = cw_body3d_contacts(g->ch[g->b[i].chunk].body, c, 16);
    for (int k = 0; k < n; k++) {
      if (c[k].shape.id != g->b[i].shape.id) continue;
      if (c[k].normal.y >= -0.5f) continue;
      if (c[k].other.id == g->ground.id) { on_ground[i] = true; continue; }
      int j = block_of(g, c[k].other_shape);
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
  if (control && !strcmp(control, "oldwelds")) opt_set("NO_CHUNKS", "1"); /* the control: the level as loose blocks (no chunks, no support graph) */
  bool c_float = control && !strcmp(control, "float"), c_solve = control && !strcmp(control, "solve"), c_island = control && !strcmp(control, "island");
  game_new(&G, level, 1, MODE_COOP);
  if (c_float) game_add_block(&G, M_STONE, (t3_vec3){ 0.3f, G.top + 2.0f, 0.2f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0);
  if (c_island) { /* two blocks welded to each other and to nothing else, in the air: welds alone hold nothing up */
    int a = game_add_block(&G, M_STONE, (t3_vec3){ 0.3f, G.top + 2.0f, 0.2f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0);
    int b = game_add_block(&G, M_STONE, (t3_vec3){ 0.3f, G.top + 2.3f, 0.2f }, (t3_vec3){ 0.3f, 0.15f, 0.15f }, 0);
    game_weld(&G, a, b);
  }
  int first = -1, nfloat = floating(&G, &first), first_over = -1, nover = overlapping(&G, &first_over);
  t3_vec3 over_at = first_over >= 0 ? game_block_xform(&G, first_over).p : (t3_vec3){ 0, 0, 0 };
  t3_vec3 float_at = first >= 0 ? game_block_xform(&G, first).p : (t3_vec3){ 0, 0, 0 };
  static t3_vec3 p0[MAXBLK];
  for (int i = 0; i < G.nb; i++) p0[i] = G.b[i].alive ? game_block_xform(&G, i).p : (t3_vec3){ 0, 0, 0 };
  game_settle(&G, level_settle_ticks(G.L));
  if (c_float || c_island) { /* the block (the welded pair) now hangs over the structure, where nothing holds it */
    float top = 0;
    for (int i = 0; i < G.nb; i++) if (G.b[i].alive) top = fmaxf(top, game_block_xform(&G, i).p.y);
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
    t3_vec3 p = game_block_xform(&G, i).p;
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
    for (int i = 0; i < G.nb; i++) if (G.b[i].alive) { if (G.ch[G.b[i].chunk].dyn) b3Body_SetAwake(cw_body3d_raw(G.ch[G.b[i].chunk].body), true); };
    game_settle(&G, level_settle_ticks(G.L));
    for (int i = 0; i < G.nb; i++) {
      if (!G.b[i].alive || i >= MAXBLK || (p0[i].x == 0 && p0[i].y == 0 && p0[i].z == 0)) continue;
      t3_vec3 p = game_block_xform(&G, i).p;
      float d = sqrtf((p.x - p0[i].x) * (p.x - p0[i].x) + (p.y - p0[i].y) * (p.y - p0[i].y) + (p.z - p0[i].z) * (p.z - p0[i].z));
      if (d > woken) woken = d;
    }
  }
  /* the bot from the start */
  game_new(&G, level, 1, MODE_COOP);
  game_settle(&G, level_settle_ticks(G.L));
  bot_state bot;
  memset(&bot, 0, sizeof bot);
  bot.yaw_jitter_control = c_solve ? 400 : 0;
  bot_reset(&bot);
  int ticks = 0;
  while (!game_over(&G) && ticks < 60 * 60 * 6) {
    input in;
    bot_input(&G, &bot, &in);
    game_step(&G, &in);
    ticks++;
  }
  printf("{\"level\":%d,\"name\":\"%s\",\"control\":\"%s\",\"welded\":%s,\"blocks\":%d,\"floating\":%d,\"first_float\":%d,\"overlapping\":%d,\"over_at\":[%.3f,%.3f,%.3f],\"float_at\":[%.3f,%.3f,%.3f],\"unrested\":%d,\"first_unrested\":%d,\"settle_move_mm\":%.1f,\"woken_move_mm\":%.1f,\"moved_from\":[%.3f,%.3f,%.3f],\"moved_half\":[%.3f,%.3f,%.3f],\"moved_mat\":%d,\"solved\":%s,\"shots\":%d,\"par\":%d,\"orbit\":%.3f}\n",
         level, G.L->name, control ? control : "", G.nweld > 0 ? "true" : "false", blocks, nfloat, first, nover, over_at.x, over_at.y, over_at.z, float_at.x, float_at.y, float_at.z, unrested, first_un, moved * 1000, woken * 1000, moved_from.x, moved_from.y, moved_from.z, moved_half.x, moved_half.y, moved_half.z, moved_mat, G.state == GS_WON ? "true" : "false", G.shots, G.L->par, G.orbit);
  fflush(stdout);
}


/* --legout [--charge] [--opt NAME=VALUE]... [level ...]: a leg of the structure taken out, and how what is over it comes down.
 *   standing  the level left alone for 6 s: the most any block moved (mm) (a structure that stands, stands)
 *   legout    the blocks of one foot of the structure (the ground blocks round the one furthest from its middle, and what
 *             stands within 2.2 m of the ground over them) are broken. Then 8 s, a snapshot each half second:
 *             the chunks standing, the biggest chunk in the air or lying (blocks), chunks of more than 6 blocks lying,
 *             awake bodies; and, for every piece let go (the chunk it was when it went), 0.75 s after it went, how much the
 *             distances between its blocks changed (rigid_mm, the most), over how many pairs (pairs); and from its
 *             landing, how far its middle rose again against how far it fell (rebound: the worst fraction)
 *   --charge  a charge at the foot of the structure instead (the blast breaks blocks, what loses its support goes)
 * The control: --opt LOOSE=1 breaks every piece let go into single blocks at once (what a heap of loose bricks does), the
 * pieces' rigidity is then not there to measure: its pair count is 0 */
extern float g_hull_dbg[6];
extern double g_ch_ms[6];
extern float g_tipdbg[8];
extern float g_unsup_dbg[8][8];
#define PIECE_MAX 400
typedef struct piece { int n, mem[PIECE_MAX]; t3_vec3 p0[PIECE_MAX]; int t_rel; float y0, ymin, yrise, fall; bool landed, measured; float rig_mm; int pairs; } piece;
static piece PC[512];
static int npc;
static void legout_check(int level, bool charge) {
  game_new(&G, level, 1, MODE_COOP);
  bool loose = opt_on("LOOSE");
  if (opt_on("SOLIDCORE")) { /* the control: a solid mass in the middle, the way the buildings used to be (a block 0.9 of the footprint's width, up to 0.8 of the height) */
    float mx = 0, mz = 0, hy = G.top; int n = 0;
    for (int i = 0; i < G.nb; i++) if (G.b[i].alive && G.b[i].tag == 1 && G.b[i].home.y < 1.0f) mx = fmaxf(mx, fabsf(G.b[i].home.x)), mz = fmaxf(mz, fabsf(G.b[i].home.z)), n++;
    game_add_block(&G, M_STONE, (t3_vec3){ 0, hy * 0.4f, 0 }, (t3_vec3){ fmaxf(0.5f, mx * 0.6f), hy * 0.4f, fmaxf(0.5f, mz * 0.6f) }, 0); (void)n;
  }
  static t3_vec3 p0[MAXBLK];
  static uint8_t was[MAXBLK];
  int total = 0;
  for (int i = 0; i < G.nb; i++) was[i] = G.b[i].alive && G.b[i].tag == 1 && G.b[i].target < 0;
  for (int i = 0; i < G.nb; i++) { p0[i] = G.b[i].alive ? game_block_xform(&G, i).p : (t3_vec3){ 0, 0, 0 }; if (G.b[i].alive && G.b[i].tag == 1 && G.b[i].target < 0) total++; }
float vol_all = 0, vol_mass = 0;
  for (int i = 0; i < G.nb; i++) if (G.b[i].alive && G.b[i].tag && G.b[i].target < 0) { float md = fminf(G.b[i].half.x, fminf(G.b[i].half.y, G.b[i].half.z)) * 2; vol_all += G.b[i].vol; if (md > 0.45f) vol_mass += G.b[i].vol; }
  /* alone for 6 s, everything awake (what a blow does to a structure before anything breaks: the old loose blocks slump, chunks stand) */
  for (int c = 0; c < G.nch; c++) if (G.ch[c].alive && G.ch[c].dyn) b3Body_SetAwake(cw_body3d_raw(G.ch[c].body), true);
  { input none = { 0 }; int st = G.state; G.state = GS_SETTLE; for (int t = 0; t < 360; t++) game_step(&G, &none); G.state = st; }
  float mx = 0;
  for (int i = 0; i < G.nb; i++) if (G.b[i].alive) { t3_vec3 p = game_block_xform(&G, i).p; float d = sqrtf((p.x - p0[i].x) * (p.x - p0[i].x) + (p.y - p0[i].y) * (p.y - p0[i].y) + (p.z - p0[i].z) * (p.z - p0[i].z)); if (d > mx) mx = d; }
  int st_n, o6;
  int big0 = chunks_largest(&G, &o6, &st_n);
    printf("{\"level\":%d,\"name\":\"%s\",\"phase\":\"standing\",\"mass_frac\":%.3f,\"blocks\":%d,\"placed\":%d,\"move_mm\":%.2f,\"chunks_standing\":%d,\"awake\":%d,\"height\":%.2f}\n", level, G.L->name, vol_all > 0 ? vol_mass / vol_all : 0, G.nalive, total, mx * 1000, st_n, game_awake(&G), G.top);
  fflush(stdout);
  (void)big0;
  if (opt_on("FEET")) { int printed = 0; printf("{\"level\":%d,\"feet\":[", level); int nf = 0; for (int i = 0; i < G.nb; i++) if (G.b[i].alive && G.b[i].tag == 1 && G.b[i].target < 0 && p0[i].y < 0.8f) printf("%s[%.2f,%.2f,%.2f,%.2f,%.2f]", nf++ ? "," : "", p0[i].x, p0[i].z, G.b[i].half.x, G.b[i].half.z, p0[i].y); printf("],\"chunks\":%d,\"com\":[", st_n); for (int c = 0; c < G.nch; c++) if (G.ch[c].alive && !G.ch[c].dyn) printf("%s[%.2f,%.2f,%.2f,%d,%.2f]", printed++ ? "," : "", G.ch[c].com.x, G.ch[c].com.y, G.ch[c].com.z, G.ch[c].n, G.ch[c].d0); printf("]}\n"); }
  static uint8_t seen[MAXBLK];
  static int cid[MAXBLK];
  memset(seen, 0, sizeof seen);
  npc = 0;
  input none = { 0 };
  G.state = GS_SETTLE;
  int broken_leg = game_legout(&G, charge);
  int snap = 0;
  const int snaps[16] = { 30, 60, 120, 180, 240, 300, 360, 420, 480, 600, 720, 840, 960, 1080, 1200, 1500 };
  double ms_sum = 0, ms_max = 0, w_sum = 0, w_max = 0, p_sum = 0; int ms_n = 0;
  double w4_sum = 0, w4_max = 0; int w4_n = 0, aw4 = 0;
  float worst_rebound = 0, worst_rig = 0; int pairs_all = 0;
  for (int t = 1; t <= 1500; t++) {
    game_step(&G, &none);
    ms_sum += G.tick_ms, w_sum += G.world_ms, p_sum += G.post_ms, ms_n++; if (G.tick_ms > ms_max) ms_max = G.tick_ms;
    if (G.world_ms > w_max) w_max = G.world_ms;
    if (t > 60 && t <= 240) { w4_sum += G.world_ms, w4_n++; if (G.world_ms > w4_max) w4_max = G.world_ms; int aw = game_awake(&G); if (aw > aw4) aw4 = aw; }
    if (opt_on("TRACE") && t <= 10) { float vm = 0; int nd = 0, na = 0; for (int c = 0; c < G.nch; c++) if (G.ch[c].alive && G.ch[c].dyn && G.ch[c].placed) { nd++; if (cw_body3d_awake(G.ch[c].body)) na++; t3_vec3 v = cw_body3d_velocity(G.ch[c].body); float s = sqrtf(v.x*v.x+v.y*v.y+v.z*v.z); if (s > vm) vm = s; } printf("{\"trace\":%d,\"dyn\":%d,\"awake\":%d,\"vmax\":%.2f}\n", t, nd, na, vm); }
    /* chunks let go this tick: each a piece */
    for (int c = 0; c < G.nch; c++) {
      chunk *k = &G.ch[c];
      if (!k->alive || !k->dyn || !k->placed || k->n < 1 || seen[G.ch[c].head] == 2) continue;
      int h = k->head;
      if (seen[h]) continue;
      if (npc >= 512) break;
      piece *p = &PC[npc++];
      p->n = 0, p->t_rel = t, p->ymin = 1e9f, p->landed = false, p->measured = false, p->rig_mm = 0, p->pairs = 0, p->fall = 0, p->yrise = 0;
      float ys = 0;
      for (int i = k->head; i >= 0 && p->n < PIECE_MAX; i = G.b[i].cnext) { p->mem[p->n] = i; p->p0[p->n] = game_block_xform(&G, i).p; ys += p->p0[p->n].y; p->n++; }
      p->y0 = p->n ? ys / p->n : 0;
      for (int m = 0; m < p->n; m++) seen[p->mem[m]] = 1, cid[p->mem[m]] = npc - 1;
    }
    if (loose) for (int pass = 0; pass < 12; pass++) { bool any = false; for (int c = 0; c < G.nch; c++) if (G.ch[c].alive && G.ch[c].dyn && G.ch[c].placed && G.ch[c].n > 1) chunk_fracture(&G, c), any = true; if (!any) break; }
    /* the pieces: rigidity at 0.75 s, the middle's fall and rebound */
    for (int q = 0; q < npc; q++) {
      piece *p = &PC[q];
      float ys = 0; int na = 0;
      for (int m = 0; m < p->n; m++) { int i = p->mem[m]; if (G.b[i].alive && !G.b[i].gen) ys += game_block_xform(&G, i).p.y, na++; }
      if (na < 2) continue;
      float y = ys / na;
      if (y < p->ymin) { p->ymin = y; p->fall = p->y0 - y; p->yrise = 0; }
      else if (y - p->ymin > p->yrise) p->yrise = y - p->ymin;
      if (!p->measured && t >= p->t_rel + 45 && p->n >= 4) {
        p->measured = true;
        int a0 = -1;
        for (int m = 0; m < p->n && a0 < 0; m++) if (G.b[p->mem[m]].alive && !G.b[p->mem[m]].gen) a0 = m;
        for (int m = 0; m < p->n; m++) {
          int i = p->mem[m];
          if (m == a0 || !G.b[i].alive || G.b[i].gen || !G.b[p->mem[a0]].alive || G.b[i].chunk != G.b[p->mem[a0]].chunk) continue; /* still one piece with it */
          t3_vec3 a = game_block_xform(&G, p->mem[a0]).p, b = game_block_xform(&G, i).p;
          float d1 = sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
          float d0 = sqrtf((p->p0[a0].x - p->p0[m].x) * (p->p0[a0].x - p->p0[m].x) + (p->p0[a0].y - p->p0[m].y) * (p->p0[a0].y - p->p0[m].y) + (p->p0[a0].z - p->p0[m].z) * (p->p0[a0].z - p->p0[m].z));
          float e = fabsf(d1 - d0) * 1000;
          if (e > p->rig_mm) p->rig_mm = e;
          p->pairs++;
        }
        if (p->rig_mm > worst_rig) worst_rig = p->rig_mm;
        pairs_all += p->pairs;
      }
    }
    if (opt_on("FEET") && t == 20) for (int c = 0; c < G.nch; c++) if (G.ch[c].alive && !G.ch[c].dyn && G.ch[c].placed && G.ch[c].leader == c) { float d = chunk_inside_now(&G, c); printf("{\"dbg\":\"chunk\",\"c\":%d,\"n\":%d,\"d\":%.3f,\"d0\":%.3f,\"com\":[%.2f,%.2f,%.2f],\"mass\":%.0f,\"frac\":%.2f,\"hull\":[%.0f,%.2f,%.2f,%.2f,%.2f]}\n", c, G.ch[c].n, d, G.ch[c].d0, G.ch[c].com.x, G.ch[c].com.y, G.ch[c].com.z, G.ch[c].mass, G.ch[c].frac, g_hull_dbg[0], g_hull_dbg[1], g_hull_dbg[2], g_hull_dbg[3], g_hull_dbg[4]); }
    if (snap < 16 && t == snaps[snap]) {
      int o6s, sn;
      int big = chunks_largest(&G, &o6s, &sn);
      worst_rebound = 0;
      int fallers = 0;
      for (int q = 0; q < npc; q++) if (PC[q].fall > 1.0f) { fallers++; if (PC[q].yrise / PC[q].fall > worst_rebound) worst_rebound = PC[q].yrise / PC[q].fall; }
      int moved_n = 0, was_n = 0;
      for (int i = 0; i < G.nb; i++) {
        if (!was[i]) continue;
        was_n++;
        if (!G.b[i].alive || G.b[i].gen) { moved_n++; continue; }
        t3_vec3 p = game_block_xform(&G, i).p;
        if (sqrtf((p.x - p0[i].x) * (p.x - p0[i].x) + (p.y - p0[i].y) * (p.y - p0[i].y) + (p.z - p0[i].z) * (p.z - p0[i].z)) > 1.0f) moved_n++;
      }
      float top_y = 0, cog_x = 0, cog_z = 0; int cog_n = 0;
      for (int i = 0; i < G.nb; i++) if (was[i] && G.b[i].alive && !G.b[i].gen) { t3_vec3 p = game_block_xform(&G, i).p; if (p.y > top_y) top_y = p.y; cog_x += p.x, cog_z += p.z, cog_n++; }
      int alive_placed = 0;
      for (int i = 0; i < G.nb; i++) alive_placed += G.b[i].alive && G.b[i].tag == 1 && G.b[i].target < 0 && !G.b[i].gen;
      printf("{\"level\":%d,\"name\":\"%s\",\"phase\":\"legout\",\"t\":%.2f,\"broken\":%d,\"placed\":%d,\"placed_alive\":%d,\"standing\":%d,\"released\":%d,\"fractured\":%d,\"largest\":%d,\"over6\":%d,\"pieces\":%d,\"rigid_mm\":%.2f,\"pairs\":%d,\"rebound\":%.3f,\"fallers\":%d,\"moved\":%.3f,\"top_y\":%.2f,\"tip\":[%.2f,%.2f,%.2f,%.2f,%.0f,%.0f,%.2f,%.0f],\"cog\":[%.2f,%.2f],\"awake\":%d,\"tick_ms_mean\":%.2f,\"tick_ms_max\":%.2f,\"world_ms_mean\":%.2f,\"world_ms_max\":%.2f,\"post_ms_mean\":%.2f,\"ms_settype\":%.1f,\"ms_carve\":%.1f,\"ms_resolve\":%.1f,\"ms_support\":%.1f,\"height\":%.2f}\n",
             level, G.L->name, t / 60.0f, broken_leg, total, alive_placed, sn, G.released, G.fractured, big, o6s, npc, worst_rig, pairs_all, worst_rebound, fallers, was_n ? (float)moved_n / (float)was_n : 0, top_y, g_tipdbg[0], g_tipdbg[1], g_tipdbg[2], g_tipdbg[3], g_tipdbg[4], g_tipdbg[5], g_tipdbg[6], g_tipdbg[7], cog_n ? cog_x / cog_n : 0, cog_n ? cog_z / cog_n : 0, game_awake(&G), ms_n ? ms_sum / ms_n : 0, ms_max, ms_n ? w_sum / ms_n : 0, w_max, ms_n ? p_sum / ms_n : 0, g_ch_ms[0], g_ch_ms[1], g_ch_ms[2], g_ch_ms[3], G.top);
      fflush(stdout);
      snap++;
    }
  }
  printf("{\"level\":%d,\"name\":\"%s\",\"phase\":\"window\",\"world_ms_mean_1_4s\":%.2f,\"world_ms_max_1_4s\":%.2f,\"awake_max_1_4s\":%d}\n", level, G.L->name, w4_n ? w4_sum / w4_n : 0, w4_max, aw4);
  fflush(stdout);
}

/* --realshot [--opt AMMO=n] level ...: a real shot (ammunition n: 0 shot, 1 heavy ball, 2 charge) fired through the launcher at the nearest low block of the structure (a foot or a base column), then 10 s. Per snapshot:
 *   destroyed  placed blocks gone, moved  the share of the placed blocks that moved over 1 m or are gone, released  assemblies let go, unsupported  standing assemblies no longer
 *   over their support (must be 0: every tick it is counted, worst_unsupported), top: the centre of the blocks in the top third (lateral drift and drop from where they were, tilt: the
 *   angle of the line from the foot's middle to that centre from the vertical, degrees) */
static void realshot_check(int level, int ammo) {
  game_new(&G, level, 1, MODE_COOP);
  static t3_vec3 p0[MAXBLK];
  static uint8_t was[MAXBLK];
  int total = 0;
  float top0 = 0;
  for (int i = 0; i < G.nb; i++) { was[i] = G.b[i].alive && G.b[i].tag == 1 && G.b[i].target < 0; p0[i] = G.b[i].alive ? game_block_xform(&G, i).p : (t3_vec3){ 0, 0, 0 }; total += was[i]; if (was[i] && p0[i].y > top0) top0 = p0[i].y; }
  float cx0 = 0, cy0 = 0, cz0 = 0; int cn0 = 0;
  for (int i = 0; i < G.nb; i++) if (was[i] && p0[i].y > top0 * 0.66f) cx0 += p0[i].x, cy0 += p0[i].y, cz0 += p0[i].z, cn0++;
  if (cn0) cx0 /= cn0, cy0 /= cn0, cz0 /= cn0;
  int pick = game_aim_shot(&G, ammo, 0);
  if (opt_on("HALFOUT")) { /* the footing's half out (the base to 1.5 m on one side): the structure over it is held by nothing on that side */
    opt_set("LEG_HALF", "1");
    game_legout(&G, false);
    pick = 0;
  }
  if (opt_on("LOWHALF")) { /* the whole lower half of the structure destroyed at once, what the player does with a few good shots (the statue's torso must fall) */
    for (int i = 0; i < G.nb; i++) if (was[i] && G.b[i].alive && G.b[i].home.y < top0 * 0.5f) game_hit_block(&G, i, 1e9f);
    pick = 0;
  }
  if (pick < 0) { printf("{\"level\":%d,\"phase\":\"realshot\",\"error\":\"no block to aim at\"}\n", level); return; }
  t3_vec3 tg = G.b[pick].home;
  if (!opt_on("LOWHALF") && !opt_on("HALFOUT")) game_fire(&G);
  G.sandbox = true;
  input none = { 0 };
  int worst_unsup = 0, hit_tick = -1, worst_float = chunks_floating(&G), float_ticks = 0, worst_fdyn = 0, fdyn_ticks = 0;
  const int snaps[8] = { 30, 60, 90, 120, 240, 360, 480, 600 };
  int si = 0;
  int shots_left = opt_get("SHOTS") ? atoi(opt_get("SHOTS")) - 1 : 0, nsteps = shots_left > 0 ? 3000 : 600;
  for (int t = 1; t <= nsteps; t++) {
    game_step(&G, &none);
    if (shots_left > 0 && G.state == GS_AIM && !G.proj_live && t > 90) { /* the next shot, as a player fires it once the field is still */
      int p2 = game_aim_shot(&G, ammo, shots_left); if (p2 >= 0) game_fire(&G);
      shots_left--;
    }
    if (hit_tick < 0 && G.proj_hit_tick >= 0) hit_tick = t;
    int u = chunks_unsupported(&G);
    if (u > worst_unsup) worst_unsup = u;
    { int fd = chunks_floating_dyn(&G); if (fd > worst_fdyn) worst_fdyn = fd; if (fd > 0 && t > 5) fdyn_ticks++; }
    { int fl = chunks_floating(&G); if (fl > worst_float) worst_float = fl; if (fl > 0) float_ticks++; }
    int relsnap = hit_tick > 0 ? (t == hit_tick + 60 ? 1 : t == hit_tick + 120 ? 2 : 0) : 0;
    bool atsnap = si < 8 && t == snaps[si];
    if (atsnap || t == nsteps || relsnap) {
      int moved = 0, gone = 0, alive_n = 0;
      float cx = 0, cy = 0, cz = 0, rot = 0; int cn = 0;
      for (int i = 0; i < G.nb; i++) {
        if (!was[i]) continue;
        if (!G.b[i].alive || G.b[i].gen) { moved++, gone++; continue; }
        alive_n++;
        t3_vec3 p = game_block_xform(&G, i).p;
        if (sqrtf((p.x - p0[i].x) * (p.x - p0[i].x) + (p.y - p0[i].y) * (p.y - p0[i].y) + (p.z - p0[i].z) * (p.z - p0[i].z)) > 1.0f) moved++;
        if (p0[i].y > top0 * 0.66f) { cx += p.x, cy += p.y, cz += p.z, cn++; cw_xform3 xx = game_block_xform(&G, i); float upy = 1 - 2 * (xx.q.x * xx.q.x + xx.q.z * xx.q.z); rot += acosf(fmaxf(-1.0f, fminf(1.0f, upy))) * 57.2958f; }
      }
      if (cn) cx /= cn, cy /= cn, cz /= cn;
      float lat = sqrtf((cx - cx0) * (cx - cx0) + (cz - cz0) * (cz - cz0)), drop = cy0 - cy;
      float top_rot = cn ? rot / (float)cn : 0;
      float tilt = cn ? atan2f(sqrtf(cx * cx + cz * cz), fmaxf(0.01f, cy)) * 57.2958f : 0;
      int o6, sn;
      int big = chunks_largest(&G, &o6, &sn);
      printf("{\"level\":%d,\"name\":\"%s\",\"phase\":\"realshot\",\"ammo\":%d,\"target\":[%.2f,%.2f,%.2f],\"hit_tick\":%d,\"t\":%.2f,\"placed\":%d,\"gone\":%d,\"moved\":%.3f,\"released\":%d,\"standing\":%d,\"largest\":%d,\"worst_unsupported\":%d,\"worst_floating\":%d,\"floating_ticks\":%d,\"asleep_in_air\":%d,\"asleep_ticks\":%d,\"top_lateral\":%.2f,\"top_drop\":%.2f,\"top_tilt\":%.1f,\"top_rot\":%.1f,\"rel\":%d,\"awake\":%d,\"top_height\":%.2f}\n",
             level, G.L->name, ammo, tg.x, tg.y, tg.z, hit_tick, t / 60.0f, total, gone, total ? (float)moved / (float)total : 0, G.released, sn, big, worst_unsup, worst_float, float_ticks, worst_fdyn, fdyn_ticks, lat, drop, tilt, top_rot, relsnap, game_awake(&G), top0);
      fflush(stdout);
      if (atsnap) si++;
    }
  }
  chunks_unsupported(&G);
  for (int u = 0; u < chunks_unsupported(&G) && u < 8; u++) printf("{\"unsup\":%d,\"n\":%.0f,\"d\":%.3f,\"req\":%.3f,\"dirty\":%.0f,\"stamp\":%.0f,\"seen\":%.0f,\"d0\":%.3f,\"c\":%.0f}\n", u, g_unsup_dbg[u][0], g_unsup_dbg[u][1], g_unsup_dbg[u][2], g_unsup_dbg[u][3], g_unsup_dbg[u][4], g_unsup_dbg[u][5], g_unsup_dbg[u][6], g_unsup_dbg[u][7]);
}

/* --water [--control]: the lake behind the Hoover Dam. The level settles as the game settles it; then, for 12 s, a charge goes off at the dam's foot
 * at 1 s (with --control: nothing does, the dam stands). One row a half second, from the charge: the breach (bins open), the flow it passes (m^3/s),
 * the parcels made, alive, awake and their mean and highest height and kinetic energy (J), the lake's level, the bodies awake besides the water, the
 * physics step; and one summary row: the tick the first parcel was made after the charge, the peak flow, the peak kinetic energy against the
 * last, the parcels at rest at the end. tests/destructo-water.mjs judges them (the control: no parcel is ever made). */
static void water_check(bool control) {
  int level = level_find("Hoover Dam");
  if (level < 0) { printf("{\"phase\":\"water\",\"error\":\"no Hoover Dam level\"}\n"); return; }
  game_new(&G, level, 1, MODE_COOP);
  game_settle(&G, level_settle_ticks(G.L));
  input none = { 0 };
  G.state = GS_SETTLE;
  const int CHARGE_TICK = 60, TOTAL = 60 * 12, DRAIN_TOTAL = TOTAL + 60 * 15; /* then the lake is drained (its level set to the floor) and the pool left to settle */
  int first = -1, nopen_max = 0, awake_max = 0, alive_max = 0;
  float flow_max = 0, ke_max = 0, ke_last = 0, my0 = 0, ymax_all = -1e9f, wl0 = G.wt.level;
  double step_sum = 0, step_max = 0; int step_n = 0;
  for (int t = 1; t <= DRAIN_TOTAL; t++) {
    if (t == TOTAL + 1) G.wt.level = 0.0f, G.wt.flow = 0; /* the flow is shut off: the parcels in the air land, the pool comes to rest */
    if (t == CHARGE_TICK && !control) {
      const water_state *w = &G.wt;
      game_charge_at(&G, (t3_vec3){ 0.0f, 1.6f, water_dam_zc(w, 0.0f) + 1.2f });
    }
    game_step(&G, &none);
    step_sum += G.world_ms, step_n++; if (G.world_ms > step_max) step_max = G.world_ms;
    if (first < 0 && G.wt.spawned > 0) first = t;
    int alive, wawake; float my, ymx, ke;
    water_stats(&G, &alive, &wawake, &my, &ymx, &ke);
    int other = game_awake(&G);
    if (G.wt.nopen > nopen_max) nopen_max = G.wt.nopen;
    if (G.wt.flow > flow_max) flow_max = G.wt.flow;
    if (ke > ke_max) ke_max = ke;
    if (alive > alive_max) alive_max = alive;
    if (other + wawake > awake_max) awake_max = other + wawake;
    if (ymx > ymax_all) ymax_all = ymx;
    ke_last = ke;
    if (t % 30 == 0)
      printf("{\"phase\":\"water_t\",\"t\":%.1f,\"since_charge\":%.1f,\"open\":%d,\"flow\":%.2f,\"spawned\":%d,\"alive\":%d,\"awake_water\":%d,\"mean_y\":%.2f,\"max_y\":%.2f,\"ke\":%.0f,\"level\":%.2f,\"awake_other\":%d,\"world_ms\":%.2f,\"pooled\":%d}\n",
             t / 60.0f, (t - CHARGE_TICK) / 60.0f, G.wt.nopen, G.wt.flow, G.wt.spawned, alive, wawake, my, ymx, ke, G.wt.level, other, G.world_ms, G.wt.retired);
  }
  int alive, wawake; float my, ymx, ke;
  water_stats(&G, &alive, &wawake, &my, &ymx, &ke);
  (void)my0;
  printf("{\"phase\":\"water\",\"control\":%d,\"spill_start_s\":%.2f,\"spawned\":%d,\"flow_peak\":%.2f,\"open_peak\":%d,\"alive_max\":%d,\"awake_max\":%d,\"ke_peak\":%.0f,\"ke_last\":%.0f,\"asleep_end\":%d,\"alive_end\":%d,\"mean_y_end\":%.2f,\"max_y_all\":%.2f,\"level0\":%.2f,\"level_end\":%.2f,\"vol_out\":%.1f,\"step_mean_ms\":%.2f,\"step_max_ms\":%.2f,\"pooled\":%d}\n",
         control, first < 0 ? -1.0f : (first - CHARGE_TICK) / 60.0f, G.wt.spawned, flow_max, nopen_max, alive_max, awake_max, ke_max, ke_last, alive - wawake, alive, my, ymax_all, wl0, G.wt.level, G.wt.vol_out, step_n ? step_sum / step_n : 0, step_max, G.wt.retired);
  fflush(stdout);
}

int main(int argc, char **argv) {
  cw_jobs_init(-1);
  for (int a = 1; a + 1 < argc;) { /* --opt NAME=VALUE anywhere: a run option (opts.h), then dropped from the arguments */
    if (!strcmp(argv[a], "--opt")) { char *eq = strchr(argv[a + 1], '='); if (eq) { *eq = 0; opt_set(argv[a + 1], eq + 1); } for (int b = a; b + 2 <= argc; b++) argv[b] = argv[b + 2]; argc -= 2; } else a++;
  }
  if (argc > 1 && !strcmp(argv[1], "--legout")) {
    bool charge = false; int nl = 0;
    for (int a = 2; a < argc; a++) { if (!strcmp(argv[a], "--charge")) charge = true; else if (argv[a][0] >= '0' && argv[a][0] <= '9') nl++; }
    for (int a = 2; a < argc; a++) if (argv[a][0] >= '0' && argv[a][0] <= '9') legout_check(atoi(argv[a]), charge);
    if (nl == 0) for (int l = 0; l < NLEVELS; l++) legout_check(l, charge);
    game_free(&G);
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "--realshot")) {
    int am = opt_get("AMMO") ? atoi(opt_get("AMMO")) : 0;
    for (int a = 2; a < argc; a++) if (argv[a][0] >= '0' && argv[a][0] <= '9') realshot_check(atoi(argv[a]), am);
    game_free(&G);
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "--water")) {
    water_check(argc > 2 && !strcmp(argv[2], "--control"));
    game_free(&G);
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "--landmarks")) {
    const char *c = argc > 3 && !strcmp(argv[2], "--control") ? argv[3] : NULL;
    if (c && !strcmp(c, "oldwelds")) landmark_check(1, c); /* the control: the Needle with the welds as they were */
    else
      for (int l = 0; l < NLEVELS; l++) {
        landmark_check(l, c);
        if (c) break; /* a control: the first landmark */
      }
    game_free(&G);
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "--control")) {
    const char *c = argv[2];
    /* by name: the levels before them move as levels are added */
    int early = level_find(opt_get("EARLY_LEVEL") ? opt_get("EARLY_LEVEL") : "Colosseum"), stars = level_find("Hagia Sophia");
    check(!strcmp(c, "solve") ? 1 : !strcmp(c, "replay") ? 0 : (!strcmp(c, "early") || !strcmp(c, "settle")) ? (early >= 0 ? early : 23) : !strcmp(c, "stars") ? (stars >= 0 ? stars : 2) : 0, c);
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
