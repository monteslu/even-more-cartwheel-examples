/* destructo_humancheck: can a person new to the game clear every level? (native, no window, no GPU). One JSON line per
 * level and player; tests/destructo-human.mjs judges them.
 *
 *   play     the human-like player (src/human.c: eyes on what the screen shows, an aim error that it corrects from
 *            where its last shot struck, a reaction delay, the d-pad held then tapped, the pad mapped to the launcher
 *            the way main.c maps a person's pad) plays the level from its start with the ammunition the level gives,
 *            once for each of several players (seeds: each its own lean in how it reads the aim path, half of them
 *            pushing the power to full). It must bring every crystal down before the ammunition runs out
 *   shot     what one shot does: from the level's starting aim (the launcher as the level opens, power as it starts,
 *            turned at the middle of the structure and raised to put the shot a third of the way up it), each kind of
 *            ammunition fired once into the fresh level: blocks broken, placed blocks moved, the share of the structure
 *            down once still, the energy of the first strike and the most a single hit put into a block
 *
 *   destructo_humancheck [--seeds n] [--skill k] [level ...]   play (default every level, 8 players, skill 1: the
 *            newcomer; k scales the aim errors)
 *   destructo_humancheck --shot [level ...]
 * The gate's control is the build before the fix (tests/destructo-human.mjs builds it from git): it must fail. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/jobs.h"
#include "game.h"
#include "human.h"

static game G;
#ifdef PRE_R3 /* the build before round 3 (the gate's measurement of it): no judged share, no marks */
#define JUDGED_SHARE 0.0f
#define KEG(k) 0
#define MARK 0.0f
#else
#define JUDGED_SHARE G.judged_share
#define KEG(k) ((k)->keg)
#define MARK game_mark(&G)
#endif

static int ammo_total(const game *g) { return g->L->ammo[0] + g->L->ammo[1] + g->L->ammo[2]; }

static bool old_damage;
static bool never_moves; /* --never-moves: the player who never goes round the structure (the orbit check's control) */

static void play(int level, int seed, float skill_k) {
  game_new(&G, level, 1, MODE_COOP);
#ifndef PRE_R3
  G.old_damage = old_damage;
#endif
  game_settle(&G, level_settle_ticks(G.L));
  human_skill s = human_newcomer();
  s.across_sd *= skill_k, s.height_sd *= skill_k;
  human_state h;
  human_reset(&h, s, (uint32_t)(level * 131 + seed * 7 + 1));
  h.never_moves = never_moves;
  int orbit_ticks = 0;
  pad_buttons pad, last;
  memset(&last, 0, sizeof last);
  int ticks = 0, first_down = -1;
  while (!game_over(&G) && ticks < 60 * 60 * 8) {
    human_buttons(&G, &h, &pad);
    input in;
    human_to_input(&pad, &last, &in);
    last = pad;
    int left = G.tgt_left, st0 = G.state;
    orbit_ticks += in.orbit_left || in.orbit_right;
    game_step(&G, &in);
    if (getenv("HC_DEBUG") && st0 == GS_AIM && G.state == GS_FLIGHT) fprintf(stderr, "shot %d %s at crystal %d (%.2f %.2f %.2f) aim point %.2f %.2f %.2f power %.2f\n", G.shots, AMMO[G.proj_kind].name, h.tgt, h.tgt_seen.x, h.tgt_seen.y, h.tgt_seen.z, h.aim_point.x, h.aim_point.y, h.aim_point.z, G.power);
    if (getenv("HC_DEBUG") && st0 == GS_SETTLE && G.state != GS_SETTLE) fprintf(stderr, "   struck %.2f %.2f %.2f (hit %d), broken %d, crystals left %d, share %.3f\n", G.impact.x, G.impact.y, G.impact.z, G.proj_hit_tick >= 0, G.broken, G.tgt_left, JUDGED_SHARE);
    if (first_down < 0 && G.tgt_left < left) first_down = G.shots;
    ticks++;
  }
  printf("{\"level\":%d,\"name\":\"%s\",\"kind\":%d,\"seed\":%d,\"full_power\":%d,\"cleared\":%s,\"shots\":%d,\"ammo\":%d,\"par\":%d,\"stars\":%d,"
         "\"targets\":%d,\"left\":%d,\"first_down_shot\":%d,\"share\":%.3f,\"mark\":%.3f,\"broken\":%d,\"ticks\":%d,\"orbit_ticks\":%d,\"orbit\":%.3f}\n",
         level, G.L->name, G.L->kind, seed, h.full_power, G.state == GS_WON ? "true" : "false", G.shots, ammo_total(&G), G.L->par, game_stars(&G),
         G.ntgt, G.tgt_left, first_down, JUDGED_SHARE, MARK, G.broken, ticks, orbit_ticks, G.orbit);
  fflush(stdout);
}

/* the energy of the first strike and the most any hit put into a block, read off the world's hit events the way sim.c
 * reads them; the structure's share down by this tool's own count (every placed block by volume: gone, or more than
 * its own size and at least 0.6 m from where it settled), so the build before round 3 is measured the same way */
static void shot(int level, int ammo) {
  game_new(&G, level, 1, MODE_COOP);
#ifndef PRE_R3
  G.old_damage = old_damage;
#endif
  game_settle(&G, level_settle_ticks(G.L));
  static int body[MAXBLK];
  static t3_vec3 at[MAXBLK];
  static float vol[MAXBLK], lim[MAXBLK];
  int placed = 0;
  float vol_all = 0;
  /* the aim: the placed block nearest the launcher on its line (|x| < 1.5 m), between 0.5 m up and half the height */
  t3_vec3 aim = { 0, G.top * 0.33f, 0 };
  float best = 1e9f;
  for (int i = 0; i < G.nb; i++) {
    block *k = &G.b[i];
    if (!k->alive || !k->tag || k->target >= 0) continue;
    t3_vec3 p = game_block_xform(&G, i).p;
    body[placed] = i, at[placed] = p, vol[placed] = k->vol, lim[placed] = fmaxf(0.6f, fmaxf(k->half.x, fmaxf(k->half.y, k->half.z)));
    vol_all += k->vol, placed++;
    if (KEG(k) || fabsf(p.x) > 1.5f || p.y < 0.5f || p.y > fmaxf(1.0f, G.top * 0.5f)) continue;
    float d = (G.muzzle.z - p.z) + fabsf(p.x) * 0.5f + fabsf(p.y - G.top * 0.33f) * 0.5f;
    if (d < best) best = d, aim = p;
  }
  G.ammo[ammo] = 1, G.ammo_sel = ammo;
  float sp = game_speed(&G), d = sqrtf((aim.x - G.muzzle.x) * (aim.x - G.muzzle.x) + (aim.z - G.muzzle.z) * (aim.z - G.muzzle.z));
  float yaw = cw_atan2(-(aim.x - G.muzzle.x), -(aim.z - G.muzzle.z)), lo = -0.08f, hi = 0.75f;
  for (int i = 0; i < 30; i++) { /* the pitch whose flight is at the block's height where it stands */
    float m = (lo + hi) * 0.5f, spp, cp;
    cw_sincos(m, &spp, &cp);
    float px = 1.4f * cp, py = G.muzzle.y + spp * 1.4f, vx = cp * sp, vy = spp * sp, st = TICK / 4;
    for (int k = 0; k < 4 * 60 * 8 && px < d; k++) vy -= GRAV * st, px += vx * st, py += vy * st;
    if (py < aim.y) lo = m; else hi = m;
  }
  G.yaw = yaw, G.pitch = (lo + hi) * 0.5f;
  float ke = 0.5f * AMMO[ammo].mass * sp * sp;
  input none = { 0 };
  game_fire(&G);
  float first_e = -1, max_e = 0;
  int ticks = 0;
  while (G.state != GS_AIM && !game_over(&G) && ticks < 60 * 30) {
    game_step(&G, &none);
    int n;
    const cw_phys_event3d *ev = cw_world3d_events(G.w, &n);
    for (int e = 0; e < n; e++) {
      if (ev[e].type != CW_PHYS_HIT) continue;
      float ma = cw_body3d_valid(ev[e].body_a) ? cw_body3d_mass(ev[e].body_a) : 0, mb = cw_body3d_valid(ev[e].body_b) ? cw_body3d_mass(ev[e].body_b) : 0;
      float mu = ma > 0 && mb > 0 ? ma * mb / (ma + mb) : ma > 0 ? ma : mb, en = 0.5f * mu * ev[e].speed * ev[e].speed;
      bool proj = (cw_body3d_valid(ev[e].body_a) && ev[e].body_a.id == G.proj.id) || (cw_body3d_valid(ev[e].body_b) && ev[e].body_b.id == G.proj.id);
      if (proj && first_e < 0) first_e = en;
      if (en > max_e) max_e = en;
    }
    ticks++;
  }
  int moved = 0, gone = 0;
  float vol_down = 0;
  for (int i = 0; i < placed; i++) {
    bool down = !(G.b[body[i]].alive && !G.b[body[i]].gen);
    gone += down;
    float dd = 0;
    if (!down) {
      t3_vec3 p = game_block_xform(&G, body[i]).p;
      dd = sqrtf((p.x - at[i].x) * (p.x - at[i].x) + (p.y - at[i].y) * (p.y - at[i].y) + (p.z - at[i].z) * (p.z - at[i].z));
      down = dd > lim[i];
    }
    moved += !(G.b[body[i]].alive && !G.b[body[i]].gen) || dd > 0.1f;
    if (down) vol_down += vol[i];
  }
  printf("{\"level\":%d,\"name\":\"%s\",\"ammo\":\"%s\",\"speed\":%.1f,\"ke_kj\":%.1f,\"momentum\":%.0f,\"first_hit_kj\":%.1f,\"max_hit_kj\":%.1f,\"broken\":%d,\"placed\":%d,\"gone\":%d,\"moved\":%d,"
         "\"share\":%.4f,\"crystals_down\":%d,\"targets\":%d}\n",
         level, G.L->name, AMMO[ammo].name, sp, ke / 1000, AMMO[ammo].mass * sp, first_e / 1000, max_e / 1000, G.broken, placed, gone, moved, vol_all > 0 ? vol_down / vol_all : 0, G.ntgt - G.tgt_left, G.ntgt);
  fflush(stdout);
}

int main(int argc, char **argv) {
  cw_jobs_init(-1);
  int seeds = 8, a = 1;
  float k = 1;
  bool shots = false;
  int levels[NLEVELS], nl = 0;
  for (; a < argc; a++) {
    if (!strcmp(argv[a], "--seeds") && a + 1 < argc) seeds = atoi(argv[++a]);
    else if (!strcmp(argv[a], "--skill") && a + 1 < argc) k = (float)atof(argv[++a]);
    else if (!strcmp(argv[a], "--shot")) shots = true;
    else if (!strcmp(argv[a], "--old-damage")) old_damage = true;
    else if (!strcmp(argv[a], "--never-moves")) never_moves = true;
    else if (nl < NLEVELS) levels[nl++] = atoi(argv[a]);
  }
  if (nl == 0) for (int l = 0; l < NLEVELS; l++) levels[nl++] = l;
  for (int i = 0; i < nl; i++) {
    if (shots) for (int m = 0; m < NAMMO; m++) shot(levels[i], m);
    else for (int s = 0; s < seeds; s++) play(levels[i], s, k);
  }
  game_free(&G);
  return 0;
}
