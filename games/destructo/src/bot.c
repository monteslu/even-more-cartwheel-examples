/* destructo's bot: it plays through the controls a person holds. Each turn it picks a crystal still standing, works
 * out an aim (the turn, the raise, the power and the ammunition) by flying the shot's path the way the world will
 * (gravity in the world's sub-steps), then presses the aim buttons until the launcher points there (held for the long
 * moves, tapped for the last hair) and fires. It changes plan by attempt: first a shot straight at the crystal, then
 * a charge at it (the blast reaches inside a wall), then a heavy ball low at the structure under it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"
#include "opts.h"

void bot_reset(bot_state *b) {
  int jc = b->yaw_jitter_control;
  bool no = b->no_orbit;
  memset(b, 0, sizeof *b);
  b->yaw_jitter_control = jc, b->no_orbit = no;
  b->plan_tgt = -1;
}

/* the height of the shot's path where it has come d metres (horizontally) from the launcher, for an aim */
static float path_height(const game *g, float yaw, float pitch, float speed, float d) {
  float sy, cy, sp, cp;
  cw_sincos(yaw, &sy, &cy);
  cw_sincos(pitch, &sp, &cp);
  t3_vec3 dir = { -sy * cp, sp, -cy * cp };
  float px = g->muzzle.x + dir.x * 1.4f, py = g->muzzle.y + dir.y * 1.4f, pz = g->muzzle.z + dir.z * 1.4f;
  float vx = dir.x * speed, vy = dir.y * speed, vz = dir.z * speed;
  float h = TICK / 4;
  float hx = g->muzzle.x, hz = g->muzzle.z;
  for (int i = 0; i < 4 * 60 * 8; i++) {
    vy -= GRAV * h;
    px += vx * h, py += vy * h, pz += vz * h;
    float dx = px - hx, dz = pz - hz;
    if (dx * dx + dz * dz >= d * d) return py;
  }
  return -1000;
}

/* the low arc that passes through point t at this speed; false when it cannot reach */
static bool solve(const game *g, t3_vec3 t, float speed, float *yaw, float *pitch) {
  float dx = t.x - g->muzzle.x, dz = t.z - g->muzzle.z;
  *yaw = cw_atan2(-dx, -dz);
  float d = sqrtf(dx * dx + dz * dz);
  float lo = -0.08f, hi = 0.75f;
  if (path_height(g, *yaw, hi, speed, d) < t.y) return false;
  for (int i = 0; i < 26; i++) {
    float m = (lo + hi) * 0.5f;
    if (path_height(g, *yaw, m, speed, d) < t.y) lo = m; else hi = m;
  }
  *pitch = (lo + hi) * 0.5f;
  return true;
}

static int pick_ammo(const game *g, int want) {
  if (g->ammo[want] > 0) return want;
  static const int order[NAMMO] = { A_SHOT, A_HEAVY, A_CHARGE };
  for (int k = 0; k < NAMMO; k++) if (g->ammo[order[k]] > 0) return order[k];
  return want;
}

/* the high arc through t at this speed (a lob over a wall): pitch from 0.8 up, where a steeper shot lands nearer */
static bool solve_high(const game *g, t3_vec3 t, float speed, float *yaw, float *pitch) {
  float dx = t.x - g->muzzle.x, dz = t.z - g->muzzle.z;
  *yaw = cw_atan2(-dx, -dz);
  float d = sqrtf(dx * dx + dz * dz);
  float lo = 0.8f, hi = 1.38f;
  if (path_height(g, *yaw, lo, speed, d) < t.y) return false;
  if (path_height(g, *yaw, hi, speed, d) > t.y) return false;
  for (int i = 0; i < 26; i++) {
    float m = (lo + hi) * 0.5f;
    if (path_height(g, *yaw, m, speed, d) > t.y) lo = m; else hi = m;
  }
  *pitch = (lo + hi) * 0.5f;
  return true;
}

/* the crystal is in plain view from a launcher standing at m (nothing of the structure between) */
static bool in_view_from(const game *g, t3_vec3 m, int t, t3_vec3 at) {
  /* a ball the shot's size swept from the launcher to the crystal must meet the crystal first */
  float ml = sqrtf(m.x * m.x + m.z * m.z) + 1e-4f;
  t3_vec3 o = { m.x - m.x / ml * 1.6f, m.y + 0.3f, m.z - m.z / ml * 1.6f };
  t3_vec3 d = { at.x - o.x, at.y - o.y, at.z - o.z };
  cw_qshape3d q = cw_qshape3d_sphere(o, AMMO[A_SHOT].radius);
  cw_ray_hit3d h;
  if (cw_world3d_shapecast(g->w, &q, d, NULL, &h, 1) == 0) return true;
  int i = g->tgt[t].blk;
  return i >= 0 && g->b[i].alive && h.body.id == g->b[i].body.id;
}
static bool in_view(const game *g, int t, t3_vec3 at) { return in_view_from(g, g->muzzle, t, at); }

static float wrap_pi(float a) {
  while (a > 3.14159265f) a -= 6.2831853f;
  while (a < -3.14159265f) a += 6.2831853f;
  return a;
}
/* hidden from here: the nearest place on the launcher's circle the crystal can be seen from (false: nowhere) */
static bool find_view(const game *g, int t, t3_vec3 at, float *orbit) {
  float best = 1e9f;
  bool any = false;
  for (int k = 1; k < 32; k++) {
    float d = (k & 1 ? 1 : -1) * (float)((k + 1) / 2) * 0.19634954f; /* out from here both ways, pi/16 a step */
    float a = wrap_pi(g->orbit + d), s, c;
    cw_sincos(a, &s, &c);
    t3_vec3 m = { g->ring * s, g->muzzle.y, g->ring * c };
    if (fabsf(d) < best && in_view_from(g, m, t, at)) best = fabsf(d), *orbit = a, any = true;
    if (any) break;
  }
  return any;
}
static void plan(game *g, bot_state *b) {
  int t = -1;
  for (int i = 0; i < g->ntgt; i++) if (!g->tgt[i].down) { t = i; break; }
  if (g->mode == MODE_PARTY) { /* steady hands: a heavy ball at the block nearest the launcher that stands well clear
                                * of every crystal still up (the front of the structure, far from what must not fall) */
    float best = 1e9f;
    int pick = -1;
    for (int pass = 0; pass < 2 && pick < 0; pass++)
      for (int i = 0; i < g->nb; i++) {
        const block *k = &g->b[i];
        if (!k->alive || !k->tag || k->target >= 0 || k->down || k->keg) continue;
        t3_vec3 p = cw_body3d_transform(k->body).p;
        float near = 1e9f;
        for (int c = 0; c < g->ntgt; c++) {
          if (g->tgt[c].down || g->tgt[c].blk < 0) continue;
          t3_vec3 q = g->tgt[c].home;
          float d = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z);
          if (d < near) near = d;
        }
        if (pass == 0 && near < 3.0f * 3.0f) continue; /* the second pass takes any block */
        float dx = p.x - g->muzzle.x, dz = p.z - g->muzzle.z;
        float score = dx * dx + dz * dz + p.y * 4 + hashf((uint32_t)i * 2654435761u + (uint32_t)g->shots) * 20;
        if (score < best) best = score, pick = i;
      }
    if (pick < 0) return;
    b->plan_tgt = 0;
    b->want_ammo = pick_ammo(g, A_HEAVY);
    b->want_power = 1.0f, b->lob = false;
    b->aim_at = cw_body3d_transform(g->b[pick].body).p;
    b->solved = false;
    float dx = b->aim_at.x - g->muzzle.x, dz = b->aim_at.z - g->muzzle.z;
    b->want_yaw = game_yaw_from_world(g, cw_atan2(-dx, -dz)), b->want_pitch = 0.1f;
    b->planned = true, b->wait = 0;
    return;
  }
  if (t < 0 && g->sandbox) { /* free fire with every crystal down: a block still standing, any ammunition */
    int n = 0, pick = -1, want = (int)(hash32((uint32_t)g->shots * 7919u + 13u) % 997u);
    for (int i = 0; i < g->nb; i++) if (g->b[i].alive && g->b[i].tag && g->b[i].target < 0) n++;
    if (n == 0) return;
    want %= n;
    for (int i = 0; i < g->nb && pick < 0; i++) if (g->b[i].alive && g->b[i].tag && g->b[i].target < 0 && want-- == 0) pick = i;
    b->plan_tgt = 0;
    b->want_ammo = pick_ammo(g, g->shots % NAMMO);
    b->want_power = 1.0f, b->lob = false;
    b->aim_at = cw_body3d_transform(g->b[pick].body).p;
    b->solved = false;
    float dx = b->aim_at.x - g->muzzle.x, dz = b->aim_at.z - g->muzzle.z;
    b->want_yaw = game_yaw_from_world(g, cw_atan2(-dx, -dz)), b->want_pitch = 0.1f;
    b->planned = true, b->wait = 0;
    return;
  }
  if (t < 0) return;
  b->plan_tgt = t;
  target_state *ts = &g->tgt[t];
  t3_vec3 at = ts->blk >= 0 && g->b[ts->blk].alive ? cw_body3d_transform(g->b[ts->blk].body).p : ts->home;
  int a = b->attempt[t];
  bool seen = in_view(g, t, at);
  if (!seen && (a >= 1 || g->ammo[A_CHARGE] == 0) && !b->orbited[t] && !b->no_orbit && g->mode != MODE_PARTY) { /* hidden from here: roll round to where it shows */
    b->orbited[t] = true;
    float o;
    if (find_view(g, t, at, &o)) {
      b->want_orbit = o, b->orbiting = true, b->plan_tgt = t, b->planned = true, b->wait = 0;
      return;
    }
  }
  /* the plan by attempt: in view, a shot straight at it; hidden, a lob onto it (a charge when there is one);
   * then a charge at it; then a heavy ball low into what holds it up; then round again, the aim walked about */
  int ammo = A_SHOT;
  bool lob = false;
  t3_vec3 aimp = at;
  switch (a % 3) {
  case 0: if (seen) ammo = A_SHOT; else lob = true, ammo = g->ammo[A_CHARGE] > 0 ? A_CHARGE : A_SHOT; break;
  case 1: ammo = seen ? A_CHARGE : A_HEAVY; break;               /* hidden: breach what is in front of it */
  default: ammo = seen ? A_HEAVY : A_CHARGE; if (seen) aimp.y = fmaxf(0.5f, at.y * 0.5f); break;
  }
  if (a >= 3) aimp.x += ((a / 3) & 1 ? 0.6f : -0.6f) * (float)((a + 1) / 6 + 1) * 0.5f;
  b->want_ammo = pick_ammo(g, ammo);
  const ammo_info *ai = &AMMO[b->want_ammo];
  b->want_power = 1.0f; /* fixed power: a lob is a high arc, not a slower shot */
  b->lob = lob;
  b->aim_at = aimp;
  b->solved = false;
  float dx = aimp.x - g->muzzle.x, dz = aimp.z - g->muzzle.z;
  b->want_yaw = game_yaw_from_world(g, cw_atan2(-dx, -dz)) + (float)b->yaw_jitter_control * 0.001f;
  b->want_pitch = lob ? 0.95f : 0.1f;
  b->planned = true;
  b->wait = 0;
}

/* once the power is set, the exact aim for the speed the launcher really has */
static void resolve(game *g, bot_state *b) {
  float yaw, pitch, sp = game_speed(g);
  bool ok = b->lob ? solve_high(g, b->aim_at, sp, &yaw, &pitch) : solve(g, b->aim_at, sp, &yaw, &pitch);
  if (!ok && b->lob) ok = solve(g, b->aim_at, sp, &yaw, &pitch);
  if (!ok) {
    float dx = b->aim_at.x - g->muzzle.x, dz = b->aim_at.z - g->muzzle.z;
    yaw = cw_atan2(-dx, -dz), pitch = 0.6f;
  }
  b->want_yaw = cw_clampf(game_yaw_from_world(g, yaw) + (float)b->yaw_jitter_control * 0.001f, -0.75f, 0.75f);
  b->want_pitch = cw_clampf(pitch, -0.08f, 1.4f);
  b->solved = true;
  if (opt_get("BOT_DEBUG")) fprintf(stderr, "resolve: lob %d ok %d speed %.3f power %.4f aim %.2f %.2f %.2f pitch %.4f h_at_target %.3f\n", b->lob, ok, sp, g->power, b->aim_at.x, b->aim_at.y, b->aim_at.z, pitch, path_height(g, yaw, pitch, sp, sqrtf((b->aim_at.x - g->muzzle.x) * (b->aim_at.x - g->muzzle.x) + (b->aim_at.z - g->muzzle.z) * (b->aim_at.z - g->muzzle.z))));
}

/* hold toward a value: held while far, tapped (a press every other tick) when close */
static void steer(float err, int tick, bool *neg, bool *pos, float tol) {
  if (fabsf(err) <= tol) return;
  bool press = fabsf(err) > 0.03f || (tick & 1) == 0;
  if (!press) return;
  if (err > 0) *pos = true; else *neg = true;
}

void bot_input(game *g, bot_state *b, input *in) {
  memset(in, 0, sizeof *in);
  if (g->state != GS_AIM) {
    if (b->planned) b->planned = false;
    return;
  }
  if (!b->planned) plan(g, b);
  if (!b->planned) return;
  b->wait++;
  int tk = g->tick;
  if (b->orbiting) { /* rolling round the circle: held while far, tapped when near, then a fresh plan from there */
    float e = wrap_pi(b->want_orbit - g->orbit);
    if (fabsf(e) <= 0.004f) { b->orbiting = false, b->planned = false; return; }
    bool press = fabsf(e) > 0.05f || (tk & 1) == 0;
    if (press) { if (e > 0) in->orbit_right = true; else in->orbit_left = true; }
    in->orbit_k = 1;
    return;
  }
  /* the turn: right lowers the yaw */
  steer(b->want_yaw - g->yaw, tk, &in->right, &in->left, 0.0012f);
  steer(b->want_pitch - g->pitch, tk, &in->down, &in->up, 0.0012f);
  in->turn_k = in->raise_k = 1;
  bool ammo_ok = g->ammo_sel == b->want_ammo;
  if (!ammo_ok && (tk % 6) == 0) in->ammo_next = true;
  bool power_ok = true;
  if (ammo_ok && power_ok && !b->solved) resolve(g, b);
  bool aimed = b->solved && fabsf(b->want_yaw - g->yaw) <= 0.0012f && fabsf(b->want_pitch - g->pitch) <= 0.0012f;
  if (aimed && ammo_ok && b->wait > 20 && !in->left && !in->right && !in->up && !in->down) {
    in->fire = true;
    b->attempt[b->plan_tgt]++;
    b->planned = false;
  }
}
