/* destructo's human-like player (human.h): a newcomer's eyes and hands on the pad. */
#include <math.h>
#include <string.h>

#include "cartwheel/math.h"
#include "human.h"

human_skill human_newcomer(void) {
  /* At the structure (26 to 46 m off), 0.5 m across is the launcher 11 to 19 milliradians off: lining a dotted path up
   * with a glowing crystal on screen, a person is about this good. Up and down is harder: the path shows only its
   * first stretch, so where it comes down at the crystal is a guess (0.8 m). A person sees about a quarter of a
   * second late and cannot tell an aim 4 milliradians off from a true one. */
  return (human_skill){ .across_sd = 0.5f, .height_sd = 0.8f, .learn = 0.6f, .react = 14, .tol = 0.004f };
}

static float urand(human_state *h) {
  h->rng = h->rng * 1664525u + 1013904223u;
  return (float)(h->rng >> 8) / 16777216.0f;
}
/* a normal draw (Box-Muller on the player's own generator) */
static float nrand(human_state *h) {
  float u1 = fmaxf(urand(h), 1e-6f), u2 = urand(h);
  return sqrtf(-2 * logf(u1)) * cosf(6.2831853f * u2);
}

void human_reset(human_state *h, human_skill s, uint32_t seed) {
  memset(h, 0, sizeof *h);
  h->skill = s;
  h->rng = seed * 2654435761u + 977u;
  for (int i = 0; i < 4; i++) urand(h);
  h->tgt = -1;
  h->full_power = (seed & 1) != 0;
  /* a person's error is mostly a lean they keep (how they read the path), a little of it fresh each shot */
  for (int t = 0; t < MAXTGT; t++) h->corr_across[t] = nrand(h) * s.across_sd * 0.85f, h->corr_up[t] = nrand(h) * s.height_sd * 0.85f;
}

/* the height of the flight where it has come d metres (along the ground) from the launcher */
static float wrap_pi(float a) {
  while (a > 3.14159265f) a -= 6.2831853f;
  while (a < -3.14159265f) a += 6.2831853f;
  return a;
}

static float path_height(const game *g, float yaw, float pitch, float speed, float d) {
  float sy, cy, sp, cp;
  cw_sincos(yaw, &sy, &cy);
  cw_sincos(pitch, &sp, &cp);
  float px = g->muzzle.x - sy * cp * 1.4f, py = g->muzzle.y + sp * 1.4f, pz = g->muzzle.z - cy * cp * 1.4f;
  float vx = -sy * cp * speed, vy = sp * speed, vz = -cy * cp * speed, st = TICK / 4;
  for (int i = 0; i < 4 * 60 * 8; i++) {
    vy -= GRAV * st;
    px += vx * st, py += vy * st, pz += vz * st;
    float dx = px - g->muzzle.x, dz = pz - g->muzzle.z;
    if (dx * dx + dz * dz >= d * d) return py;
  }
  return -1000;
}

/* lining the path up with a point: the low arc through it (what the path's first stretch, followed by eye, points at) */
static bool line_up(const game *g, t3_vec3 t, float speed, float *yaw, float *pitch) {
  float dx = t.x - g->muzzle.x, dz = t.z - g->muzzle.z;
  *yaw = cw_atan2(-dx, -dz);
  float d = sqrtf(dx * dx + dz * dz), lo = -0.08f, hi = 0.75f;
  if (path_height(g, *yaw, hi, speed, d) < t.y) return false;
  for (int i = 0; i < 24; i++) {
    float m = (lo + hi) * 0.5f;
    if (path_height(g, *yaw, m, speed, d) < t.y) lo = m; else hi = m;
  }
  *pitch = (lo + hi) * 0.5f;
  return true;
}

/* ... and the high arc through it (a lob over what is in front): pitch from 0.8 up, where a steeper shot lands nearer */
static bool lob_up(const game *g, t3_vec3 t, float speed, float *yaw, float *pitch) {
  float dx = t.x - g->muzzle.x, dz = t.z - g->muzzle.z;
  *yaw = cw_atan2(-dx, -dz);
  float d = sqrtf(dx * dx + dz * dz), lo = 0.8f, hi = 1.12f;
  if (path_height(g, *yaw, lo, speed, d) < t.y || path_height(g, *yaw, hi, speed, d) > t.y) return false;
  for (int i = 0; i < 24; i++) {
    float m = (lo + hi) * 0.5f;
    if (path_height(g, *yaw, m, speed, d) > t.y) lo = m; else hi = m;
  }
  *pitch = (lo + hi) * 0.5f;
  return true;
}

static t3_vec3 crystal_seen(const game *g, int t) {
  const target_state *ts = &g->tgt[t];
  return ts->blk >= 0 && g->b[ts->blk].alive ? cw_body3d_transform(g->b[ts->blk].body).p : ts->home;
}

static void across_up(const game *g, t3_vec3 c, t3_vec3 *across, t3_vec3 *fwd) {
  float dx = c.x - g->muzzle.x, dz = c.z - g->muzzle.z, l = sqrtf(dx * dx + dz * dz) + 1e-4f;
  *fwd = (t3_vec3){ dx / l, 0, dz / l };
  *across = (t3_vec3){ -fwd->z, 0, fwd->x };
}

/* the last shot is over: where it struck against the crystal it was meant for, a share of the miss taken off */
static void learn(const game *g, human_state *h) {
  int t = h->tgt;
  if (t < 0) return;
  bool down = g->tgt_left < h->down_before;
  h->dry[t] = down ? 0 : h->dry[t] + 1;
  if (g->tgt[t].down) return;
  t3_vec3 c = h->tgt_seen, across, fwd;
  across_up(g, c, &across, &fwd);
  if (g->proj_hit_tick < 0) { h->corr_up[t] -= 1.0f; return; } /* it struck nothing: well over */
  t3_vec3 i = g->impact, m = { i.x - c.x, i.y - c.y, i.z - c.z };
  float ma = m.x * across.x + m.z * across.z, along = m.x * fwd.x + m.z * fwd.z, mu = m.y;
  /* struck well short of it (the ground or the face in front, low): low; well past it: high */
  if (along < -2.0f && mu > -0.5f) mu = fminf(mu, -0.5f);
  if (along > 2.0f) mu = fmaxf(mu, 0.5f);
  h->corr_across[t] -= h->skill.learn * ma;
  h->corr_up[t] -= h->skill.learn * mu;
}

/* what the screen shows: the crystal's glow, or a block in front of it (a ray from the aiming camera's eye: behind the
 * launcher, up and to the right, view.c) */
static bool hidden_from(const game *g, float orbit, int t, t3_vec3 c) {
  float s, co;
  cw_sincos(orbit, &s, &co);
  t3_vec3 m = { g->ring * s, g->muzzle.y, g->ring * co }, out = { s, 0, co }, right = { co, 0, -s };
  t3_vec3 eye = { m.x + out.x * 8.5f + right.x * 2.6f, m.y + 3.6f, m.z + out.z * 8.5f + right.z * 2.6f };
  cw_phys_filter f = cw_phys_filter_layers(1u << 1); /* the block layer */
  cw_ray_hit3d hit;
  if (!cw_world3d_raycast(g->w, eye, (t3_vec3){ c.x - eye.x, c.y - eye.y, c.z - eye.z }, &f, &hit)) return false;
  int i = g->tgt[t].blk;
  return !(i >= 0 && g->b[i].alive && hit.body.id == g->b[i].body.id);
}
static bool hidden(const game *g, int t, t3_vec3 c) { return hidden_from(g, g->orbit, t, c); }

static void plan(const game *g, human_state *h) {
  int best = -1;
  float bd = 1e9f;
  for (int t = 0; t < g->ntgt; t++) {
    if (g->tgt[t].down) continue;
    t3_vec3 c = crystal_seen(g, t);
    float dx = c.x - g->muzzle.x, dz = c.z - g->muzzle.z, d = dx * dx + dz * dz;
    if (d < bd) bd = d, best = t;
  }
  h->tgt = best;
  if (best < 0) return;
  t3_vec3 c = crystal_seen(g, best), across, fwd;
  /* hidden from here: go round toward its side (its beacon shows where it stands) to where its glow shows, a step of
   * the circle at a time out from its own side */
  if (!h->never_moves && !h->moved_for[best] && hidden(g, best, c)) {
    h->moved_for[best] = true;
    float side = cw_atan2(c.x, c.z);
    for (int k = 0; k < 13; k++) {
      float a = wrap_pi(side + (k & 1 ? 1 : -1) * (float)((k + 1) / 2) * 0.3f);
      if (!hidden_from(g, a, best, c)) { h->want_orbit = a, h->moving_round = true; return; }
    }
  }
  h->tgt_seen = c;
  across_up(g, c, &across, &fwd);
  float ea = h->corr_across[best] + nrand(h) * h->skill.across_sd * 0.5f, eu = h->corr_up[best] + nrand(h) * h->skill.height_sd * 0.5f;
  h->aim_point = (t3_vec3){ c.x + across.x * ea, fmaxf(0.2f, c.y + eu), c.z + across.z * ea };
  /* the ammunition: shot, until shots at this crystal have twice brought nothing down; then a charge, a heavy ball */
  int want = A_SHOT;
  if (h->dry[best] >= 2 || g->ammo[A_SHOT] == 0) {
    static const int order[NAMMO] = { A_CHARGE, A_HEAVY, A_SHOT };
    for (int k = 0; k < NAMMO; k++) if (g->ammo[order[k]] > 0) { want = order[k]; break; }
  }
  if (g->ammo[want] <= 0) want = g->ammo_sel;
  /* hidden behind the structure (its glow blocked from where the player looks): once a straight shot has found the
   * wall, a lob */
  h->lob = h->dry[best] >= 1 && hidden(g, best, c);
  h->want_ammo = want;
  h->want_power = g->power; /* fixed */
  h->planned = true, h->still = 0;
}

void human_buttons(const game *g, human_state *h, pad_buttons *pad) {
  memset(pad, 0, sizeof *pad);
  /* what the launcher showed, a reaction delay ago */
  int n = h->hist_n % 64;
  h->hist_yaw[n] = g->yaw, h->hist_pitch[n] = g->pitch, h->hist_n++;
  int back = h->hist_n > h->skill.react ? h->skill.react : h->hist_n - 1;
  int k = (h->hist_n - 1 - back) % 64;
  float seen_yaw = h->hist_yaw[k], seen_pitch = h->hist_pitch[k];
  if (g->state != GS_AIM) {
    h->planned = false;
    return;
  }
  if (h->awaiting) { learn(g, h); h->awaiting = 0; }
  if (h->moving_round) { /* the right stick, left or right: held while far, let go a reaction late, tapped near */
    float e = wrap_pi(h->want_orbit - g->orbit);
    if (fabsf(e) <= 0.03f) { h->moving_round = false; h->release_o = 0; return; }
    if (h->release_o > 0) { h->release_o--; return; }
    pad->rx_left = e < 0, pad->rx_right = e > 0;
    if (fabsf(e) < 0.15f) h->release_o = h->skill.react;
    return;
  }
  if (!h->planned) plan(g, h);
  if (!h->planned || h->moving_round) return;
  /* the ammunition on the HUD */
  if (g->ammo_sel != h->want_ammo) {
    if (h->prev_ammo_press == 0) pad->west = true, h->prev_ammo_press = 8;
    else h->prev_ammo_press--;
    return;
  }
  float sp = game_speed(g), yaw, pitch;
  bool ok = h->lob ? lob_up(g, h->aim_point, sp, &yaw, &pitch) || line_up(g, h->aim_point, sp, &yaw, &pitch) : line_up(g, h->aim_point, sp, &yaw, &pitch);
  if (!ok) {
    float dx = h->aim_point.x - g->muzzle.x, dz = h->aim_point.z - g->muzzle.z;
    yaw = cw_atan2(-dx, -dz), pitch = 0.6f;
  }
  h->want_yaw = cw_clampf(game_yaw_from_world(g, yaw), -0.75f, 0.75f), h->want_pitch = cw_clampf(pitch, -0.08f, 1.4f);
  /* the hands: held while far off (let go once it looks there, a reaction late), tapped when close */
  float ey = h->want_yaw - seen_yaw, ep = h->want_pitch - seen_pitch, tol = h->skill.tol;
  bool moving = false;
  if (fabsf(ey) > tol) {
    if (h->release_t > 0) h->release_t--;
    else if (fabsf(ey) > 0.03f) { if (ey > 0) pad->left = true; else pad->right = true; moving = true; }
    else { if (ey > 0) pad->left = true; else pad->right = true; h->release_t = h->skill.react; moving = true; }
  }
  if (fabsf(ep) > tol) {
    if (h->release_r > 0) h->release_r--;
    else if (fabsf(ep) > 0.03f) { if (ep > 0) pad->up = true; else pad->down = true; moving = true; }
    else { if (ep > 0) pad->up = true; else pad->down = true; h->release_r = h->skill.react; moving = true; }
  }
  bool looks = fabsf(ey) <= tol && fabsf(ep) <= tol;
  h->still = moving || !looks ? 0 : h->still + 1;
  if (h->still > h->skill.react + 6) { /* it looks right and has been still a moment: fire */
    pad->south = true;
    h->shots_at[h->tgt]++;
    h->down_before = g->tgt_left;
    h->awaiting = 1;
    h->planned = false;
  }
}

void human_to_input(const pad_buttons *pad, const pad_buttons *last, input *in) {
  memset(in, 0, sizeof *in);
  in->left = pad->left, in->right = pad->right, in->up = pad->up, in->down = pad->down;
  in->orbit_left = pad->rx_left, in->orbit_right = pad->rx_right;
  in->orbit_k = in->orbit_left || in->orbit_right ? 1 : 0;
  in->turn_k = in->left || in->right ? 1 : 0;
  in->raise_k = pad->up || pad->down ? 1 : 0;
  in->fire = pad->south && !last->south;
  in->ammo_next = pad->west && !last->west;
}
