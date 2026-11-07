/* demolition's bot: it plays through the controls a person holds. Each turn it picks a crystal still standing, works
 * out an aim (the turn, the raise, the power and the ammunition) by flying the shot's path the way the world will
 * (gravity in the world's sub-steps), then presses the aim buttons until the launcher points there (held for the long
 * moves, tapped for the last hair) and fires. It changes plan by attempt: first a shot straight at the crystal, then
 * a charge at it (the blast reaches inside a wall), then a heavy ball low at the structure under it. */
#include <math.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

void bot_reset(bot_state *b) {
  int jc = b->yaw_jitter_control;
  memset(b, 0, sizeof *b);
  b->yaw_jitter_control = jc;
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
  float lo = 0.8f, hi = 1.12f;
  if (path_height(g, *yaw, lo, speed, d) < t.y) return false;
  if (path_height(g, *yaw, hi, speed, d) > t.y) return false;
  for (int i = 0; i < 26; i++) {
    float m = (lo + hi) * 0.5f;
    if (path_height(g, *yaw, m, speed, d) > t.y) lo = m; else hi = m;
  }
  *pitch = (lo + hi) * 0.5f;
  return true;
}

/* the crystal is in plain view from the launcher (nothing of the structure between) */
static bool in_view(const game *g, int t, t3_vec3 at) {
  /* a ball the shot's size swept from the launcher to the crystal must meet the crystal first */
  t3_vec3 o = { g->muzzle.x, g->muzzle.y + 0.3f, g->muzzle.z - 1.6f };
  t3_vec3 d = { at.x - o.x, at.y - o.y, at.z - o.z };
  cw_qshape3d q = cw_qshape3d_sphere(o, AMMO[A_SHOT].radius);
  cw_ray_hit3d h;
  if (cw_world3d_shapecast(g->w, &q, d, NULL, &h, 1) == 0) return true;
  int i = g->tgt[t].blk;
  return i >= 0 && g->b[i].alive && h.body.id == g->b[i].body.id;
}

static void plan(game *g, bot_state *b) {
  int t = -1;
  for (int i = 0; i < g->ntgt; i++) if (!g->tgt[i].down) { t = i; break; }
  if (t < 0) return;
  b->plan_tgt = t;
  target_state *ts = &g->tgt[t];
  t3_vec3 at = ts->blk >= 0 && g->b[ts->blk].alive ? cw_body3d_transform(g->b[ts->blk].body).p : ts->home;
  int a = b->attempt[t];
  bool seen = in_view(g, t, at);
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
  if (lob) { /* the speed that lands it at about 57 degrees */
    float dx = aimp.x - g->muzzle.x, dz = aimp.z - g->muzzle.z, d = sqrtf(dx * dx + dz * dz);
    float v = sqrtf(GRAV * d / 0.91f) * 1.03f;
    b->want_power = cw_clampf((v - ai->vmin) / (ai->vmax - ai->vmin), 0, 1);
  } else b->want_power = 1.0f;
  b->lob = lob;
  b->aim_at = aimp;
  b->solved = false;
  float dx = aimp.x - g->muzzle.x, dz = aimp.z - g->muzzle.z;
  b->want_yaw = cw_atan2(-dx, -dz) + (float)b->yaw_jitter_control * 0.001f;
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
  b->want_yaw = cw_clampf(yaw + (float)b->yaw_jitter_control * 0.001f, -0.75f, 0.75f);
  b->want_pitch = cw_clampf(pitch, -0.08f, 1.15f);
  b->solved = true;
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
  /* the turn: right lowers the yaw */
  steer(b->want_yaw - g->yaw, tk, &in->right, &in->left, 0.0012f);
  steer(b->want_pitch - g->pitch, tk, &in->down, &in->up, 0.0012f);
  in->turn_k = in->raise_k = 1;
  if (g->power < b->want_power - 0.004f) in->power_up = true;
  if (g->power > b->want_power + 0.004f) in->power_down = true;
  bool ammo_ok = g->ammo_sel == b->want_ammo;
  if (!ammo_ok && (tk % 6) == 0) in->ammo_next = true;
  bool power_ok = fabsf(g->power - b->want_power) <= 0.006f;
  if (ammo_ok && power_ok && !b->solved) resolve(g, b);
  bool aimed = b->solved && fabsf(b->want_yaw - g->yaw) <= 0.0012f && fabsf(b->want_pitch - g->pitch) <= 0.0012f && power_ok;
  if (aimed && ammo_ok && b->wait > 20 && !in->left && !in->right && !in->up && !in->down) {
    in->fire = true;
    b->attempt[b->plan_tgt]++;
    b->planned = false;
  }
}
