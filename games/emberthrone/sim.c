/* emberthrone: the rules. Everything here runs on the fixed tick from the input of each seat and the world's
 * collision, with cw maths only, so the same input gives the same state on every host (the gates compare it).
 *
 * Players: move relative to the camera, lock on to a foe (the camera turns to keep both in view and the player
 * faces it), a three-cut light combo and a heavy spin with the sword, slower heavier blows with the maul, a dive
 * roll with invulnerable frames (a back step with no direction), stamina for all of it, a tonic flask that heals.
 * Foes: small groups that wake together, an attack token (two at a time come at a player, the rest circle), a tell
 * before every blow (a wind-up, a flash, a ring on the ground for the area blows), stagger when their poise breaks,
 * a leash home. Bosses: a ring of their own, phases (a roar, quicker, new blows: charges and shockwaves).
 * Progress: hearts, a charm, the maul (it breaks cracked walls), two keys (the keep's gate), shrines (rest: health,
 * flasks, the foes back, the save), a lever that opens a door back to the meadow. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/platform.h"
#include "game.h"

sim S;

/* the blows: clip, length, hit window, damage, poise, reach, arc, lunge, stamina, combo window, knock, ring */
const attack_def ATK[A_COUNT] = {
  [A_SLASH1] = { "slash1", 28, 10, 15, 22, 20, 2.3f, 0.35f, 2.6f, 14, 13, 26, false, false, 0, 0 },
  [A_SLASH2] = { "slash2", 28, 9, 14, 24, 20, 2.3f, 0.35f, 2.6f, 14, 12, 26, false, false, 0, 0 },
  [A_SLASH3] = { "slash3", 40, 16, 22, 34, 40, 2.5f, 0.5f, 3.2f, 18, 0, 0, false, false, 0, 0 },
  [A_HEAVY] = { "heavy", 62, 30, 46, 48, 70, 2.8f, -1, 1.2f, 32, 0, 0, true, false, 0, 0 },
  [A_MAUL1] = { "swing", 38, 16, 22, 42, 60, 2.4f, 0.5f, 2.0f, 24, 22, 36, false, false, 0, 0 },
  [A_MAUL2] = { "slam", 66, 38, 46, 75, 100, 2.6f, 0.2f, 1.0f, 40, 0, 0, true, true, 2.4f, 0 },
  [A_CLEAVE] = { "slash1", 44, 26, 31, 12, 25, 1.9f, 0.4f, 2.2f, 0, 0, 0, false, false, 0, 0 },
  [A_CHOP] = { "swing", 58, 34, 40, 18, 40, 2.0f, 0.5f, 1.6f, 0, 0, 0, false, false, 0, 0 },
  [A_STAB] = { "stab", 48, 28, 34, 16, 30, 2.9f, 0.75f, 3.0f, 0, 0, 0, false, false, 0, 0 },
  [A_SLAM] = { "slam", 80, 48, 54, 30, 80, 2.4f, 0.2f, 0.6f, 0, 0, 0, true, true, 2.4f, 0 },
  [A_HAY] = { "haymaker", 76, 44, 52, 24, 60, 2.2f, 0.3f, 1.4f, 0, 0, 0, true, false, 0, 0 },
  [A_SPIN] = { "spin", 66, 30, 44, 22, 50, 2.6f, -1, 0.8f, 0, 0, 0, true, true, 2.6f, 0 },
  [A_LARIAT] = { "lariat", 70, 26, 56, 26, 60, 1.9f, 0.3f, 7.0f, 0, 0, 0, true, false, 0, 0 },
  [A_HAMMER] = { "hammer", 76, 44, 50, 34, 90, 2.6f, 0.2f, 0.8f, 0, 0, 0, true, true, 2.8f, 12 },
  [A_WAVE] = { "roar", 72, 40, 44, 20, 40, 1.0f, -1, 0, 0, 0, 0, true, false, 0, 10 },
  /* the shield bearer's shove and the slinger's throw (no blade: the stone does the harm) */
  [A_BASH] = { "stab", 46, 28, 33, 10, 45, 1.9f, 0.5f, 3.4f, 0, 0, 0, false, false, 0, 0 },
  [A_THROW] = { "swing", 58, 36, 37, 0, 0, -9, 1, 0, 0, 0, 0, false, false, 0, 0 },
};

const kind_def KIND[K_KINDS] = {
  [K_WAYFARER] = { "wayfarer", "sword", 1.0f, 100, 30, 5.4f, 0.34f, 1.75f, 1, 0, "WAYFARER" },
  [K_WARDEN] = { "warden", "sword", 1.0f, 100, 30, 5.4f, 0.36f, 1.8f, 1, 0, "WARDEN" },
  [K_RAIDER] = { "raider", "cleaver", 1.0f, 60, 45, 3.6f, 0.36f, 1.8f, 1, 12, "Hollow raider" },
  [K_BRUTE] = { "brute", "maul", 1.0f, 170, 120, 2.4f, 0.5f, 2.3f, 1, 12, "Stone brute" },
  [K_LANCER] = { "lancer", "spear", 1.0f, 80, 55, 3.2f, 0.36f, 1.9f, 1, 13, "Tide lancer" },
  [K_THORN] = { "thornwarden", "bigaxe", 1.0f, 620, 400, 3.0f, 0.8f, 3.1f, 1, 16, "THE THORNWARDEN" },
  [K_TIDE] = { "tidecaller", "spear", 1.0f, 700, 350, 3.4f, 0.65f, 2.8f, 1, 16, "THE TIDECALLER" },
  [K_EMBER] = { "emberking", "bigaxe", 1.0f, 950, 500, 3.0f, 0.9f, 3.5f, 1, 18, "THE EMBER KING" },
};

static void evc(int kind, int a, int b, float x, float y, float z, float v, int c) {
  if (S.ev_n < (int)(sizeof S.ev / sizeof S.ev[0])) S.ev[S.ev_n++] = (event){ kind, a, b, x, y, z, v, c };
}
static void ev(int kind, int a, int b, float x, float y, float z, float v) { evc(kind, a, b, x, y, z, v, -1); }

int g_rules_off, g_rules_force = -1;
static void rules_from_env(void) {
  static const char *const N[] = { "hitstop", "knock", "crit", "magnet", "guard", "stones", "flank", "spent", "clear", "string", "lurk", "rumble" };
  const char *e = getenv("ADV_RULES_OFF");
  g_rules_off = 0;
  if (g_rules_force >= 0) { g_rules_off = g_rules_force; return; }
  for (int i = 0; e && i < (int)(sizeof N / sizeof *N); i++)
    if (strstr(e, N[i])) g_rules_off |= 1 << i;
}
static bool rule(int r) { return !(g_rules_off & r); }

bool sim_has_flag(int f) { return f >= 0 && f < 128 && ((S.sv.flags[f >> 5] >> (f & 31)) & 1); }
void sim_set_flag(int f) { if (f >= 0 && f < 128) S.sv.flags[f >> 5] |= 1u << (f & 31); }
const char *sim_item_name(int item) {
  static const char *const N[IT_KINDS] = { "", "A HEART VESSEL", "A STAMINA CHARM", "THE SUN KEY", "THE MOON KEY", "THE STONEBREAKER", "A LARGER FLASK" };
  return item > 0 && item < IT_KINDS ? N[item] : "";
}

/* ── movers ────────────────────────────────────────────────────────── */
static t3_vec3 pos(const actor *a) { return a->mv.position; }
static void actor_place(actor *a, float x, float z) {
  cw_mover3d_def d = cw_mover3d_default_def();
  const kind_def *k = &KIND[a->kind];
  d.radius = k->radius, d.height = k->height;
  d.step_height = 0.42f, d.snap_length = 0.45f;
  d.max_floor_angle = 0.9f;
  d.mask = 1u; /* the world's layer */
  d.ride_platforms = false;
  float y = world_ground(x, z, world_height(x, z) + 4) + 0.05f;
  cw_mover3d_init(&a->mv, world_phys(), &d, (t3_vec3){ x, y, z });
  a->ground_y = y;
}

static void player_reset(actor *a, int i, float x, float z, float yaw) {
  int tonics = S.sv.tonic_max;
  memset(a, 0, sizeof *a);
  a->used = a->active = true;
  a->kind = i == 0 ? K_WAYFARER : K_WARDEN;
  a->idx = i;
  a->hp_max = a->hp = 100 + 25 * S.sv.hearts;
  a->stamina_max = a->stamina = 100 + 25.0f * (float)S.sv.charms;
  a->poise_max = a->poise = KIND[a->kind].poise;
  a->tonics = a->tonic_max = tonics;
  a->target = -1;
  a->weapon = S.sv.maul ? 1 : 0;
  a->yaw = a->yaw_prev = yaw;
  actor_place(a, x, z);
}

static void foe_spawn(int f) {
  const wfoe *w = &g_foe[f];
  actor *a = &S.a[MAXP + f];
  memset(a, 0, sizeof *a);
  if (w->flag >= 0 && sim_has_flag(w->flag)) return; /* a boss that fell stays fallen */
  a->used = true;
  a->kind = w->kind, a->idx = MAXP + f, a->foe = f, a->group = w->group, a->region = w->region;
  a->home_x = w->x, a->home_z = w->z, a->home_yaw = w->yaw, a->yaw = a->yaw_prev = w->yaw;
  a->hp_max = a->hp = KIND[w->kind].hp;
  a->poise_max = a->poise = KIND[w->kind].poise;
  a->state = ST_IDLE_HOME;
  a->target = -1;
  a->boss = IS_BOSS(w->kind);
  a->role = w->role;
  a->guard = a->role == R_SHIELD ? 60 : 0;
  a->think = (f * 7) % 30;
  actor_place(a, w->x, w->z);
  a->home_y = a->mv.position.y;
}
void sim_respawn_foes(void) {
  for (int f = 0; f < g_nfoe; f++) foe_spawn(f);
}

static void apply_flags(void) {
  for (int i = 0; i < g_nobj; i++) world_obj_state(i, g_obj[i].flag >= 0 && sim_has_flag(g_obj[i].flag));
}

void sim_teleport_party(float x, float z) {
  for (int i = 0; i < MAXP; i++) {
    actor *a = &S.a[i];
    if (!a->used) continue;
    float ox = i ? 1.2f : 0, oz = i ? 0.8f : 0;
    cw_mover3d_teleport(&a->mv, (t3_vec3){ x + ox, world_ground(x + ox, z + oz, world_height(x + ox, z + oz) + 4) + 0.05f, z + oz });
    a->mv.velocity = (t3_vec3){ 0, 0, 0 };
  }
}

static void wake_at_shrine(void) {
  int so = world_shrine_obj(S.sv.shrine);
  const wobj *o = so >= 0 ? &g_obj[so] : NULL;
  float x = o ? o->x : 160, z = o ? o->z + 2.6f : 290, yaw = o ? o->yaw + CW_PI : 0;
  if (o) x += -cw_sin(o->yaw) * 2.6f, z = o->z - cw_cos(o->yaw) * -2.6f;
  for (int i = 0; i < MAXP; i++)
    if (S.a[i].used) player_reset(&S.a[i], i, x + (float)i * 1.3f, z + (float)i * 0.6f, yaw);
  S.cam_yaw = yaw + CW_PI;
  S.cam_pitch = -0.28f;
  S.cam_dist = S.cam_dist_want = 5.6f;
  S.cam_idle = 999;
}

void sim_init(int players, const save_data *sv) {
  memset(&S, 0, sizeof S);
  if (sv && sv->version == 1) S.sv = *sv;
  else S.sv = (save_data){ .version = 1, .tonic_max = 3 };
  S.players = CW_CLAMP(players, 1, MAXP);
  for (int i = 0; i < S.players; i++) S.a[i].used = true;
  S.boss = -1;
  S.interact = -1;
  S.region = -1;
  rules_from_env();
  apply_flags();
  sim_respawn_foes();
  wake_at_shrine();
  S.hash = 2166136261u;
}

void sim_add_player(int i) {
  if (i <= 0 || i >= MAXP || S.a[i].used) return;
  const actor *p0 = &S.a[0];
  S.a[i].used = true;
  player_reset(&S.a[i], i, pos(p0).x + 1.2f, pos(p0).z + 1.0f, p0->yaw);
  if (S.players < i + 1) S.players = i + 1;
}

int sim_alive_players(void) {
  int n = 0;
  for (int i = 0; i < MAXP; i++) n += S.a[i].used && S.a[i].state != ST_DEAD;
  return n;
}

float sim_cam_yaw(void) { return S.cam_yaw; }

/* ── helpers ───────────────────────────────────────────────────────── */
static float q64(float v) { return roundf(v * 64) / 64; } /* hosts quantize sticks differently */
static float turn_to(float from, float to, float rate) {
  float d = wrapf(to - from);
  if (d > rate) d = rate;
  if (d < -rate) d = -rate;
  return wrapf(from + d);
}
static float bearing(const actor *a, const actor *b) { return cw_atan2(pos(b).x - pos(a).x, pos(b).z - pos(a).z); }
static float flat_dist(const actor *a, const actor *b) { return sqrtf(dist2(pos(a).x, pos(a).z, pos(b).x, pos(b).z)); }
/* a lurker under the water is not there yet: nothing targets it and no blow finds it */
static bool hidden(const actor *a) { return a->role == R_LURKER && a->state == ST_IDLE_HOME && rule(FR_LURK); }
static bool alive(const actor *a) { return a->used && a->active && a->state != ST_DEAD && !hidden(a); }
static float scale_of(const actor *a) { return KIND[a->kind].scale; }

static void set_state(actor *a, int st, int len) { a->state = st, a->st_t = 0, a->st_len = len; }

static void start_attack(actor *a, int atk) {
  set_state(a, ST_ATTACK, ATK[atk].len);
  a->atk = atk, a->queued = false;
  memset(a->hit_mask, 0, sizeof a->hit_mask);
  if (a->idx < MAXP) {
    a->stamina -= (float)ATK[atk].stamina;
    a->stam_wait = 36;
    a->magnet = 0;
    S.swings++;
  } else {
    a->flash = 14;
    ev(EV_TELL, a->idx, atk, pos(a).x, pos(a).y, pos(a).z, 0);
    S.tells++;
  }
  ev(EV_SWING, a->idx, atk, pos(a).x, pos(a).y, pos(a).z, 0);
}

/* ── the camera (simulation: it turns the player's stick into a direction) ── */
static void camera_step(const input *in) {
  S.cam_pos_prev = S.cam_pos, S.cam_target_prev = S.cam_target;
  const actor *p = &S.a[0];
  if (!p->used) return;
  float dt = 1.0f / TPS;
  t3_vec3 c = pos(p);
  float tx = c.x, ty = c.y + 1.5f, tz = c.z;
  int np = 1;
  for (int i = 1; i < MAXP; i++)
    if (S.a[i].used && S.a[i].state != ST_DEAD) tx += pos(&S.a[i]).x, ty += pos(&S.a[i]).y + 1.5f, tz += pos(&S.a[i]).z, np++;
  tx /= (float)np, ty /= (float)np, tz /= (float)np;
  float spread = 0;
  for (int i = 1; i < MAXP; i++)
    if (S.a[i].used && S.a[i].state != ST_DEAD) spread = fmaxf(spread, sqrtf(dist2(pos(&S.a[i]).x, pos(&S.a[i]).z, c.x, c.z)));
  float cx = q64(in->cx), cy = q64(in->cy);
  bool stick = fabsf(cx) > 0.15f || fabsf(cy) > 0.15f;
  S.cam_idle = stick ? 0 : S.cam_idle + 1;
  const actor *t = p->target >= 0 ? &S.a[p->target] : NULL;
  if (t && alive(t)) {
    /* locked: behind the player, looking past them at the target */
    float b = bearing(p, t) + CW_PI; /* the camera's yaw points the view along the bearing */
    S.cam_yaw = turn_to(S.cam_yaw, b, 4.0f * dt);
    float want_p = -0.22f - 0.1f * (scale_of(t) - 1);
    S.cam_pitch += (want_p - S.cam_pitch) * 0.08f;
  } else {
    S.cam_yaw = wrapf(S.cam_yaw - cx * 2.8f * dt);
    S.cam_pitch = cw_clampf(S.cam_pitch - cy * 1.8f * dt, -0.95f, 0.3f);
    /* lazy follow: moving with the camera left alone, it swings round behind the way the player runs */
    if (S.cam_idle > 45 && p->speed > 1.5f && p->state == ST_MOVE) {
      float behind = p->yaw + CW_PI;
      float d = wrapf(behind - S.cam_yaw);
      float rate = 0.9f * dt * cw_clampf(fabsf(d), 0, 1.2f);
      if (fabsf(d) < 2.6f) S.cam_yaw = turn_to(S.cam_yaw, behind, rate);
      S.cam_pitch += (-0.26f - S.cam_pitch) * 0.01f;
    }
  }
  S.cam_dist_want = 5.6f + spread * 0.55f + (S.boss >= 0 ? 1.4f : 0);
  { /* art captures (development): ADV_CAM_DIST, ADV_CAM_PITCH, ADV_CAM_YAW (added to the yaw); read once, from
     * frame 2 on (cw_test_env: a cart host delivers the environment at frame 2) */
    static int art = -1;
    static float ad, ap, ay;
    if (art < 0 && cw_test_env("ADV_CAM_DIST") != NULL) art = 1, ad = (float)atof(cw_test_env("ADV_CAM_DIST")), ap = cw_test_env("ADV_CAM_PITCH") ? (float)atof(cw_test_env("ADV_CAM_PITCH")) : -0.2f, ay = cw_test_env("ADV_CAM_YAW") ? (float)atof(cw_test_env("ADV_CAM_YAW")) : 0;
    else if (art < 0 && cw_frame_index() > 3) art = 0; /* (not set by frame 4: none) */
    if (art > 0) S.cam_dist_want = ad, S.cam_pitch = ap, S.cam_yaw = wrapf(S.a[0].yaw + CW_PI + ay);
    /* ADV_CAM_KIND: frame the nearest living actor of that kind instead (character close-ups), ADV_CAM_H the height
     * looked at in metres above its feet (default 1.5) */
    static int ck = -2;
    static float ch;
    if (ck == -2 && cw_frame_index() > 3) ck = cw_test_env("ADV_CAM_KIND") ? atoi(cw_test_env("ADV_CAM_KIND")) : -1, ch = cw_test_env("ADV_CAM_H") ? (float)atof(cw_test_env("ADV_CAM_H")) : 1.5f;
    if (art > 0 && ck >= 0) {
      const actor *best = NULL;
      float bd = 1e9f;
      for (int i = 0; i < MAXA; i++) {
        const actor *a = &S.a[i];
        if (!alive(a) || a->kind != ck) continue;
        float d = dist2(pos(a).x, pos(a).z, c.x, c.z);
        if (d < bd) bd = d, best = a;
      }
      if (best) {
        tx = pos(best).x, ty = pos(best).y + ch, tz = pos(best).z;
        S.cam_yaw = wrapf(best->yaw + CW_PI + ay), S.cam_dist_want = ad;
      }
    }
  }
  /* the view direction and the wanted position */
  float sp = cw_sin(S.cam_pitch), cp = cw_cos(S.cam_pitch), sy = cw_sin(S.cam_yaw), cyw = cw_cos(S.cam_yaw);
  t3_vec3 dir = { sy * cp, -sp, cyw * cp }; /* from the target back toward the camera */
  t3_vec3 tgt = { tx, ty, tz };
  float want = S.cam_dist_want;
  /* never through a wall: a sphere swept from the target out to the camera; the camera stops short of what it hits */
  cw_qshape3d sph = cw_qshape3d_sphere(tgt, 0.32f);
  cw_ray_hit3d hit;
  cw_phys_filter f = cw_phys_filter_layers(1u | 8u); /* the world and the trees' crowns */
  float allowed = want;
  if (cw_world3d_shapecast(world_phys(), &sph, (t3_vec3){ dir.x * want, dir.y * want, dir.z * want }, &f, &hit, 1) > 0) allowed = fmaxf(0.6f, hit.fraction * want - 0.05f);
  /* pressed against a wall with the camera stick left alone (and nothing locked): swing round along the wall toward
   * the side with room, rather than sitting in the stone behind the player's head */
  if (allowed < want * 0.6f && S.cam_idle > 30 && !(t && alive(t))) {
    float room[2];
    for (int k = 0; k < 2; k++) {
      float yw = S.cam_yaw + (k ? 0.45f : -0.45f), sy2 = cw_sin(yw), cy2 = cw_cos(yw);
      t3_vec3 d2 = { sy2 * cp * want, -sp * want, cy2 * cp * want };
      room[k] = cw_world3d_shapecast(world_phys(), &sph, d2, &f, &hit, 1) > 0 ? hit.fraction : 1;
    }
    if (fabsf(room[1] - room[0]) > 0.1f) S.cam_yaw = wrapf(S.cam_yaw + (room[1] > room[0] ? 1 : -1) * 1.1f * dt);
  }
  if (allowed < S.cam_dist) S.cam_dist = allowed, S.cam_clip_t++;
  else S.cam_dist = fminf(allowed, S.cam_dist + 3.5f * dt);
  S.cam_clip = want - S.cam_dist;
  t3_vec3 cp3 = { tgt.x + dir.x * S.cam_dist, tgt.y + dir.y * S.cam_dist, tgt.z + dir.z * S.cam_dist };
  /* and never under the ground */
  float gy = world_height(cp3.x, cp3.z) + 0.45f;
  if (cp3.y < gy) cp3.y = gy;
  { /* the ground lift can bring a wall back into the line: a ray from the head stops the camera short of it */
    cw_ray_hit3d rh;
    t3_vec3 d3 = { cp3.x - tgt.x, cp3.y - tgt.y, cp3.z - tgt.z };
    if (cw_world3d_raycast(world_phys(), tgt, d3, &f, &rh) && rh.fraction < 0.999f) {
      float k = fmaxf(0, rh.fraction - 0.08f);
      cp3 = (t3_vec3){ tgt.x + d3.x * k, tgt.y + d3.y * k, tgt.z + d3.z * k };
      S.cam_clip_t++;
    }
  }
  S.cam_pos = cp3, S.cam_target = tgt;
  { /* the check: nothing solid between the player's head and the lens (counted, never expected) */
    cw_ray_hit3d rh;
    t3_vec3 d3 = { cp3.x - tgt.x, cp3.y - tgt.y, cp3.z - tgt.z };
    if (cw_world3d_raycast(world_phys(), tgt, d3, &f, &rh) && rh.fraction < 0.999f) S.cam_blocked++;
  }
  if (S.tick == 0 || dist2(S.cam_pos_prev.x, S.cam_pos_prev.z, cp3.x, cp3.z) > 400) S.cam_pos_prev = cp3, S.cam_target_prev = tgt;
}

/* the stick in the world: up the screen is away from the camera */
static void stick_world(const input *in, float *wx, float *wz) {
  float mx = q64(in->mx), my = q64(in->my);
  float l = sqrtf(mx * mx + my * my);
  if (l < 0.18f) { *wx = *wz = 0; return; }
  if (l > 1) mx /= l, my /= l;
  float sy = cw_sin(S.cam_yaw), cy = cw_cos(S.cam_yaw);
  /* forward (away from the camera) is -dir: (-sin, -cos); right is (cos, -sin) */
  *wx = -sy * (-my) + cy * mx;
  *wz = -cy * (-my) - sy * mx;
}

/* ── targets ───────────────────────────────────────────────────────── */
int sim_find_target(int pi, int prefer_dir) {
  const actor *p = &S.a[pi];
  int best = -1;
  float bs = 1e30f;
  float fx = -cw_sin(S.cam_yaw), fz = -cw_cos(S.cam_yaw);
  for (int i = MAXP; i < MAXA; i++) {
    const actor *e = &S.a[i];
    if (!alive(e) || i == p->target) continue;
    float dx = pos(e).x - pos(p).x, dz = pos(e).z - pos(p).z, d = sqrtf(dx * dx + dz * dz);
    if (d > 20 || fabsf(pos(e).y - pos(p).y) > 6) continue;
    float along = (dx * fx + dz * fz) / (d + 1e-3f);
    if (prefer_dir) { /* a flick: the next one that side of the current target */
      float side = dx * fz - dz * fx;
      if ((side > 0) != (prefer_dir > 0)) continue;
    }
    float score = d * (1.6f - along);
    if (score < bs) bs = score, best = i;
  }
  return best;
}

/* ── damage ────────────────────────────────────────────────────────── */
bool g_bench; /* the perf scene: nobody falls, so the fight holds for the whole run */
bool g_foe_passive; /* the rules test: foes stand where they are put and never swing */

/* the hold: a blow that lands stops the whole world for a few ticks (both bodies, the camera, the clips), longer the
 * harder the blow; presses made in it are kept for the tick after */
static void hold(int ticks) {
  if (rule(FR_HITSTOP) && ticks > S.hitstop) S.hitstop = ticks;
}
static int hold_for(const actor *by, int atk) {
  if (!by || by->idx >= MAXP) return by && by->boss ? 8 : 6; /* a blow taken */
  switch (atk) {
  case A_SLASH3: return 5;
  case A_HEAVY: case A_MAUL1: return 6;
  case A_MAUL2: return 8;
  default: return 4;
  }
}
/* how hard a body is to move: size squared, brutes and bosses heavier still */
static float mass_of(const actor *v) { float s = scale_of(v); return s * s * (v->boss ? 3.0f : v->kind == K_BRUTE ? 1.8f : 1.0f); }
/* knocked away from bx,bz: the push follows the blow's poise damage (a light cut nudges, a maul throws) */
static void knock_from(actor *v, float bx, float bz, int poise, bool down) {
  float k = rule(FR_KNOCK) ? (down ? 1.6f + 0.025f * (float)poise : 1.0f + 0.05f * (float)poise) / mass_of(v) : 3.0f;
  float b = cw_atan2(pos(v).x - bx, pos(v).z - bz);
  v->vx = cw_sin(b) * k, v->vz = cw_cos(b) * k;
}
static bool group_left(int group, int but) {
  for (int i = MAXP; i < MAXA; i++) {
    const actor *e = &S.a[i];
    if (i != but && e->used && !e->boss && e->group == group && e->state != ST_DEAD) return true;
  }
  return false;
}
/* a shield bearer's shield: up while it walks and winds up, toward the blow's side */
static bool guarding(const actor *v, const actor *by) {
  if (v->role != R_SHIELD || v->guard <= 0 || !by || !rule(FR_GUARD)) return false;
  if (v->state == ST_HURT || v->state == ST_DOWN || v->state == ST_DEAD) return false;
  if (v->state == ST_ATTACK && v->st_t >= ATK[v->atk].on) return false; /* the shove opens it */
  float dx = pos(by).x - pos(v).x, dz = pos(by).z - pos(v).z, d = sqrtf(dx * dx + dz * dz) + 1e-4f;
  return (dx * cw_sin(v->yaw) + dz * cw_cos(v->yaw)) / d > 0.25f;
}
static void stagger(actor *v, int len, int crit) {
  set_state(v, ST_HURT, len);
  v->poise = v->poise_max;
  if (v->idx >= MAXP && rule(FR_CRIT)) v->crit_t = crit;
  S.stagger++;
  ev(EV_STAGGER, v->idx, len, pos(v).x, pos(v).y + 1.2f * scale_of(v), pos(v).z, 0);
}
static void hurt(actor *v, actor *by, int atk, int dmg, int poise, bool knock) {
  if (g_bench) { if (v->idx < MAXP) dmg = 0; else if (v->hp - dmg < 1) dmg = v->hp - 1; }
  if (v->iframes > 0 || v->state == ST_DEAD) {
    if (v->idx < MAXP && v->iframes > 0) { S.dodged++; ev(EV_DODGE, v->idx, 1, pos(v).x, pos(v).y, pos(v).z, 0); }
    return;
  }
  int byi = by ? by->idx : -1;
  bool heavy = atk == A_HEAVY || atk == A_MAUL1 || atk == A_MAUL2;
  float bx = by ? pos(by).x : pos(v).x, bz = by ? pos(by).z : pos(v).z;
  if (byi >= 0 && byi < MAXP && v->idx >= MAXP && guarding(v, by)) {
    v->guard -= heavy ? 999 : poise;
    v->think = 0;
    if (v->guard > 0) { /* the shield takes it: the striker bounces off */
      S.blocked++;
      evc(EV_BLOCKED, v->idx, atk, (pos(v).x + bx) * 0.5f, pos(v).y + 1.1f, (pos(v).z + bz) * 0.5f, 0, byi);
      hold(3);
      set_state(by, ST_HURT, 14);
      knock_from(by, pos(v).x, pos(v).z, 30, false);
      return;
    }
    /* knocked aside (down for five seconds): the blow goes through, and the foe is wide open */
    S.guard_breaks++;
    evc(EV_GUARD_BREAK, v->idx, atk, pos(v).x, pos(v).y + 1.1f, pos(v).z, 0, byi);
    v->guard = -300;
  }
  bool crit = false;
  if (byi >= 0 && byi < MAXP && v->idx >= MAXP && v->crit_t > 0 && rule(FR_CRIT)) {
    crit = true, dmg *= 2, v->crit_t = 0;
    S.crits++;
  }
  if (v->idx >= MAXP && v->spent > 0 && rule(FR_SPENT)) poise *= 2; /* a boss catching its breath staggers sooner */
  v->hp -= dmg;
  v->last_hit_by = byi;
  v->flash = 8;
  evc(crit ? EV_CRIT : EV_HIT, v->idx, atk, pos(v).x, pos(v).y + 1.2f * scale_of(v), pos(v).z, (float)dmg, byi);
  if (v->idx < MAXP) S.hits_taken++;
  else S.hits_landed++;
  hold(crit ? 9 : hold_for(by, atk));
  if (v->hp <= 0) {
    v->hp = 0;
    set_state(v, ST_DEAD, v->idx < MAXP ? 999999 : 240);
    v->iframes = 0;
    if (v->idx >= MAXP) {
      S.kills++;
      hold(crit ? 10 : hold_for(by, atk) + 2);
      ev(EV_KILL, v->idx, v->kind, pos(v).x, pos(v).y, pos(v).z, cw_atan2(pos(v).x - bx, pos(v).z - bz));
      const wfoe *w = &g_foe[v->foe];
      if (!v->boss && rule(FR_CLEAR) && !group_left(v->group, v->idx)) { /* the group is beaten: a sip back in every flask */
        S.clears++;
        int filled = 0;
        for (int i = 0; i < MAXP; i++)
          if (S.a[i].used && S.a[i].state != ST_DEAD && S.a[i].tonics < S.a[i].tonic_max) S.a[i].tonics++, filled++;
        evc(EV_CLEAR, v->idx, v->group, pos(v).x, pos(v).y + 1, pos(v).z, (float)filled, byi);
      }
      if (w->flag >= 0) { /* a boss falls: its reward, the save flag */
        sim_set_flag(w->flag);
        int item = v->kind == K_THORN ? IT_KEY_SUN : v->kind == K_TIDE ? IT_KEY_MOON : 0;
        if (item == IT_KEY_SUN) S.sv.keys |= 1;
        if (item == IT_KEY_MOON) S.sv.keys |= 2;
        if (v->kind != K_EMBER) {
          S.sv.hearts++;
          for (int i = 0; i < MAXP; i++) if (S.a[i].used) S.a[i].hp_max += 25, S.a[i].hp = S.a[i].hp_max;
        }
        S.banner_t = 300, S.banner_kind = 2, S.banner_item = item;
        ev(EV_BOSS, v->idx, 2, pos(v).x, pos(v).y, pos(v).z, 0);
        if (v->kind == K_EMBER) S.state = GS_WON, S.state_t = 0, ev(EV_WIN, 0, 0, 0, 0, 0, 0);
        S.boss = -1;
      }
    } else ev(EV_FALL, v->idx, 0, pos(v).x, pos(v).y, pos(v).z, 0);
    return;
  }
  v->poise -= poise;
  if (v->guard == -300) { /* the shield just went: a long stumble, its opening makes the next blow a critical */
    stagger(v, 56, 70);
    if (by) knock_from(v, bx, bz, 50, false);
    hold(8);
    return;
  }
  /* hyper armour: brutes and bosses keep swinging through light blows once the swing is under way */
  bool armour = (v->boss || v->kind == K_BRUTE) && v->state == ST_ATTACK && v->st_t > 6;
  if (knock && !(v->boss && v->poise > 0)) {
    set_state(v, ST_DOWN, v->idx < MAXP ? 66 : 84);
    v->iframes = v->idx < MAXP ? 66 : 0;
    v->poise = v->poise_max;
    v->yaw = cw_atan2(bx - pos(v).x, bz - pos(v).z);
    knock_from(v, bx, bz, poise, true);
    if (v->idx >= MAXP && rule(FR_CRIT)) v->crit_t = 50; /* down: a blow before it rises is a critical */
    ev(EV_DOWN, v->idx, 0, pos(v).x, pos(v).y, pos(v).z, 0);
    S.stagger++;
    hold(8);
  } else if (v->poise <= 0) { /* the poise breaks: a long stumble, the opening */
    stagger(v, v->idx < MAXP ? 34 : 48, 60);
    if (by) knock_from(v, bx, bz, poise + 20, false);
    hold(8);
  } else if ((!armour && v->idx < MAXP) || (!armour && !v->boss && v->kind != K_BRUTE)) {
    set_state(v, ST_HURT, 20);
    if (by) knock_from(v, bx, bz, poise, false);
    else v->vx = v->vz = 0;
  }
}

/* the rules test: one blow, as if it landed */
void sim_test_blow(int victim, int attacker, int atk) {
  const attack_def *d = &ATK[atk];
  actor *by = attacker >= 0 ? &S.a[attacker] : NULL;
  hurt(&S.a[victim], by, atk, d->dmg, d->poise, d->knock && (attacker >= MAXP || !S.a[victim].boss));
}

/* a swing's window: everyone in front within reach (once per swing) */
static void resolve_swing(actor *a) {
  const attack_def *d = &ATK[a->atk];
  if (a->st_t < d->on || a->st_t > d->off) return;
  float reach = d->reach * scale_of(a);
  float fx = cw_sin(a->yaw), fz = cw_cos(a->yaw);
  int lo = a->idx < MAXP ? MAXP : 0, hi = a->idx < MAXP ? MAXA : MAXP;
  for (int i = lo; i < hi; i++) {
    actor *v = &S.a[i];
    if (!alive(v) || (a->hit_mask[i >> 5] >> (i & 31)) & 1) continue;
    float dx = pos(v).x - pos(a).x, dz = pos(v).z - pos(a).z, dist = sqrtf(dx * dx + dz * dz);
    float r = KIND[v->kind].radius * scale_of(v);
    if (dist > reach + r || fabsf(pos(v).y - pos(a).y) > 2.2f * scale_of(a)) continue;
    if (d->arc > -1 && dist > 0.3f && (dx * fx + dz * fz) / dist < d->arc) continue;
    a->hit_mask[i >> 5] |= 1u << (i & 31);
    int dmg = d->dmg;
    if (a->idx >= MAXP) dmg = dmg * KIND[a->kind].dmg_k;
    hurt(v, a, a->atk, dmg, d->poise, d->knock && (a->idx >= MAXP || !v->boss));
  }
  /* the maul breaks cracked walls */
  if (a->idx < MAXP && a->weapon == 1 && a->st_t == d->on) {
    for (int k = 0; k < g_nobj; k++) {
      wobj *o = &g_obj[k];
      if (o->kind != OB_CRACKED || sim_has_flag(o->flag)) continue;
      float px = pos(a).x + fx * 1.4f, pz = pos(a).z + fz * 1.4f;
      float dx = px - o->x, dz = pz - o->z, s = cw_sin(o->yaw), c = cw_cos(o->yaw);
      float lx = dx * c - dz * s, lz = dx * s + dz * c;
      if (fabsf(lx) < o->sx / 2 + 0.6f && fabsf(lz) < 1.6f) {
        sim_set_flag(o->flag);
        world_obj_state(k, true);
        ev(EV_BREAK, a->idx, k, o->x, o->y + 1.5f, o->z, 0);
      }
    }
  }
  /* an area blow without a shockwave still lands with a thud (dust, rumble near it) */
  if (d->ring && !d->shock && a->st_t == d->on) {
    float hx = pos(a).x + fx * (d->arc > -1 ? 1.4f : 0) * scale_of(a), hz = pos(a).z + fz * (d->arc > -1 ? 1.4f : 0) * scale_of(a);
    ev(EV_SHOCK, a->idx, -1, hx, pos(a).y, hz, 0);
  }
  /* a blow with a shockwave sends it out from where it lands */
  if (d->shock && a->st_t == d->on) {
    for (int k = 0; k < 4; k++)
      if (!S.sh[k].on) {
        float hx = pos(a).x + fx * 1.6f * scale_of(a), hz = pos(a).z + fz * 1.6f * scale_of(a);
        S.sh[k] = (shock){ true, a->idx, hx, world_ground(hx, hz, pos(a).y + 2), hz, 0.6f, (float)d->shock / 100.0f, 18, 0 };
        ev(EV_SHOCK, a->idx, k, hx, pos(a).y, hz, 0);
        break;
      }
  }
}

/* a slinger lets go: a stone straight at where its target stands now, a little over its head height, at 15 m/s */
static void throw_stone(actor *e, const actor *t) {
  if (!rule(FR_STONES)) return;
  for (int k = 0; k < MAXSTONE; k++)
    if (!S.st[k].on) {
      float fx = cw_sin(e->yaw), fz = cw_cos(e->yaw);
      float x = pos(e).x + fx * 0.6f, y = pos(e).y + 1.6f, z = pos(e).z + fz * 0.6f;
      float dx = pos(t).x - x, dy = pos(t).y + 1.1f - y, dz = pos(t).z - z, d = sqrtf(dx * dx + dy * dy + dz * dz) + 1e-3f;
      float sp = 15.0f / TPS;
      S.st[k] = (stone){ true, e->idx, 0, x, y, z, dx / d * sp, dy / d * sp, dz / d * sp };
      S.stones_thrown++;
      evc(EV_THROW, e->idx, k, x, y, z, 0, t->idx);
      return;
    }
}
static void stones_step(void) {
  for (int k = 0; k < MAXSTONE; k++) {
    stone *s = &S.st[k];
    if (!s->on) continue;
    s->x += s->vx, s->y += s->vy, s->z += s->vz, s->t++;
    if (s->t > 90 || s->y < world_height(s->x, s->z)) { s->on = false; ev(EV_STONE, -1, k, s->x, s->y, s->z, 0); continue; }
    for (int i = 0; i < MAXP; i++) {
      actor *v = &S.a[i];
      if (!alive(v)) continue;
      float dx = pos(v).x - s->x, dz = pos(v).z - s->z, dy = s->y - pos(v).y;
      if (dx * dx + dz * dz < 0.5f * 0.5f && dy > 0.1f && dy < 1.9f) {
        s->on = false;
        int before = S.hits_taken;
        hurt(v, &S.a[s->from], A_THROW, 12, 25, false);
        if (S.hits_taken > before) S.stones_hit++;
        ev(EV_STONE, i, k, s->x, s->y, s->z, 1);
        break;
      }
    }
  }
}

static void shocks_step(void) {
  for (int k = 0; k < 4; k++) {
    shock *s = &S.sh[k];
    if (!s->on) continue;
    s->r += s->speed;
    if (s->r > 18) { s->on = false; continue; }
    for (int i = 0; i < MAXP; i++) {
      actor *v = &S.a[i];
      if (!alive(v) || (s->hit >> i) & 1) continue;
      float d = sqrtf(dist2(pos(v).x, pos(v).z, s->x, s->z));
      if (fabsf(d - s->r) < 0.45f && fabsf(pos(v).y - s->y) < 1.2f) {
        s->hit |= 1u << i;
        hurt(v, &S.a[s->from], A_WAVE, s->dmg, 30, true);
      }
    }
  }
}

/* ── movement shared by everyone ───────────────────────────────────── */
static void move_actor(actor *a, float vx, float vz) {
  float dt = 1.0f / TPS;
  t3_vec3 v = a->mv.velocity;
  bool wet = pos(a).y < 8.15f && a->region == RG_MARSH;
  float k = wet ? 0.72f : 1;
  v.x = vx * k, v.z = vz * k;
  if (a->mv.on_floor) v.y = -2.0f;
  else v.y = fmaxf(v.y - 22.0f * dt, -30);
  a->mv.velocity = v;
  cw_mover3d_move(&a->mv, dt);
  a->speed = sqrtf(vx * vx + vz * vz) * k;
  /* fell out of the world (never on a built course, but a guard): back on the ground */
  if (pos(a).y < -10) cw_mover3d_teleport(&a->mv, (t3_vec3){ pos(a).x, world_ground(pos(a).x, pos(a).z, 200) + 0.1f, pos(a).z });
}

/* bodies keep apart: a soft push for anyone overlapping */
static void separate(void) {
  for (int i = 0; i < MAXA; i++) {
    actor *a = &S.a[i];
    if (!alive(a)) continue;
    for (int j = i + 1; j < MAXA; j++) {
      actor *b = &S.a[j];
      if (!alive(b)) continue;
      float dx = pos(b).x - pos(a).x, dz = pos(b).z - pos(a).z, d2 = dx * dx + dz * dz;
      float r = (KIND[a->kind].radius * scale_of(a) + KIND[b->kind].radius * scale_of(b)) * 1.15f;
      if (d2 >= r * r || d2 < 1e-6f || fabsf(pos(b).y - pos(a).y) > 2) continue;
      float d = sqrtf(d2), push = (r - d) * 0.5f;
      float ma = a->boss ? 0.1f : 1, mb = b->boss ? 0.1f : 1, sum = ma + mb;
      t3_vec3 pa = pos(a), pb = pos(b);
      pa.x -= dx / d * push * 2 * ma / sum, pa.z -= dz / d * push * 2 * ma / sum;
      pb.x += dx / d * push * 2 * mb / sum, pb.z += dz / d * push * 2 * mb / sum;
      /* move through the mover so walls still hold */
      a->mv.velocity = (t3_vec3){ (pa.x - pos(a).x) * TPS, 0, (pa.z - pos(a).z) * TPS };
      cw_mover3d_move(&a->mv, 1.0f / TPS);
      b->mv.velocity = (t3_vec3){ (pb.x - pos(b).x) * TPS, 0, (pb.z - pos(b).z) * TPS };
      cw_mover3d_move(&b->mv, 1.0f / TPS);
    }
  }
}

/* ── players ───────────────────────────────────────────────────────── */
static int interact_obj(const actor *p) {
  int best = -1;
  float bd = 2.4f * 2.4f;
  for (int k = 0; k < g_nobj; k++) {
    const wobj *o = &g_obj[k];
    if (o->kind == OB_PICKUP || o->kind == OB_CRACKED) continue;
    if (o->flag >= 0 && sim_has_flag(o->flag) && o->kind != OB_SHRINE) continue;
    float r = o->kind == OB_GATE ? 3.6f : o->kind == OB_SHRINE ? 3.2f : 2.2f;
    float d2 = dist2(pos(p).x, pos(p).z, o->x, o->z);
    if (d2 > r * r || fabsf(pos(p).y - o->y) > 2.5f) continue;
    if (o->kind == OB_DOOR) {
      if (o->side == 0) continue; /* opened by a lever only */
      float s = cw_sin(o->yaw), c = cw_cos(o->yaw);
      float side = (pos(p).x - o->x) * s + (pos(p).z - o->z) * c;
      if ((side > 0 ? 1 : -1) != o->side) continue;
    }
    if (d2 < bd + r * r) bd = d2, best = k;
  }
  return best;
}
static void use_obj(actor *p, int k) {
  wobj *o = &g_obj[k];
  p->yaw = cw_atan2(o->x - pos(p).x, o->z - pos(p).z);
  switch (o->kind) {
  case OB_SHRINE:
    set_state(p, ST_REST, 999999);
    for (int s = 0; s < 8; s++) if (world_shrine_obj(s) == k) S.sv.shrine = s;
    S.sv.rests++;
    for (int i = 0; i < MAXP; i++) if (S.a[i].used) { S.a[i].hp = S.a[i].hp_max; S.a[i].tonics = S.a[i].tonic_max; S.a[i].stamina = S.a[i].stamina_max; if (S.a[i].state == ST_DEAD) player_reset(&S.a[i], i, pos(p).x + 1, pos(p).z + 1, p->yaw); }
    sim_respawn_foes();
    S.boss = -1;
    ev(EV_REST, p->idx, k, o->x, o->y, o->z, 0);
    break;
  case OB_CHEST: set_state(p, ST_ACT, 40); p->atk = k; break;
  case OB_LEVER: set_state(p, ST_ACT, 50); p->atk = k; break;
  case OB_GATE:
    if ((S.sv.keys & o->item) == o->item) set_state(p, ST_ACT, 60), p->atk = k;
    else S.banner_t = 150, S.banner_kind = 3, S.banner_item = o->item;
    break;
  case OB_DOOR: set_state(p, ST_ACT, 60); p->atk = k; break;
  default: break;
  }
}
static void give(actor *p, int item) {
  if (item == IT_MAUL && getenv("ADV_NO_MAUL")) item = IT_NONE; /* the soak's control: an empty chest, a wall that never breaks */
  switch (item) {
  case IT_HEART: S.sv.hearts++; for (int i = 0; i < MAXP; i++) if (S.a[i].used) S.a[i].hp_max += 25, S.a[i].hp = S.a[i].hp_max; break;
  case IT_CHARM: S.sv.charms++; for (int i = 0; i < MAXP; i++) if (S.a[i].used) S.a[i].stamina_max += 25; break;
  case IT_KEY_SUN: S.sv.keys |= 1; break;
  case IT_KEY_MOON: S.sv.keys |= 2; break;
  case IT_MAUL: S.sv.maul = 1; for (int i = 0; i < MAXP; i++) if (S.a[i].used) S.a[i].weapon = 1; break;
  case IT_TONIC_UP: S.sv.tonic_max++; for (int i = 0; i < MAXP; i++) S.a[i].tonic_max++; break;
  default: break;
  }
  S.banner_t = 240, S.banner_kind = 1, S.banner_item = item;
  ev(EV_PICK, p->idx, item, pos(p).x, pos(p).y + 1, pos(p).z, 0);
}
static void act_done(actor *p) {
  int k = p->atk;
  if (k < 0 || k >= g_nobj) return;
  wobj *o = &g_obj[k];
  sim_set_flag(o->flag);
  world_obj_state(k, true);
  if (o->kind == OB_CHEST) { give(p, o->item); ev(EV_OPEN, p->idx, k, o->x, o->y, o->z, 0); }
  if (o->kind == OB_LEVER) {
    ev(EV_LEVER, p->idx, k, o->x, o->y, o->z, 0);
    if (o->link >= 0) { sim_set_flag(g_obj[o->link].flag); world_obj_state(o->link, true); ev(EV_GATE, p->idx, o->link, g_obj[o->link].x, g_obj[o->link].y, g_obj[o->link].z, 0); }
  }
  if (o->kind == OB_GATE || o->kind == OB_DOOR) ev(EV_GATE, p->idx, k, o->x, o->y, o->z, 0);
}

/* the cut's magnet: the first blow of a combo steps up to a metre toward the locked foe (or the nearest one in front
 * when nothing is locked), spread over its wind-up, so a foe that sidesteps the wind-up is still met */
static void aim_cut(actor *p, const actor *t, float wx, float wz, float mag) {
  if (!rule(FR_MAGNET)) return;
  const attack_def *d = &ATK[p->atk];
  if (!t) { /* the nearest awake foe within 3.6 m in front (of the stick, or of the player) */
    float fx = mag > 0.2f ? wx / mag : cw_sin(p->yaw), fz = mag > 0.2f ? wz / mag : cw_cos(p->yaw), bs = 1e30f;
    for (int i = MAXP; i < MAXA; i++) {
      const actor *e = &S.a[i];
      if (!alive(e) || fabsf(pos(e).y - pos(p).y) > 2) continue;
      float dx = pos(e).x - pos(p).x, dz = pos(e).z - pos(p).z, dd = sqrtf(dx * dx + dz * dz) + 1e-4f;
      if (dd > 3.6f + KIND[e->kind].radius * scale_of(e) || (dx * fx + dz * fz) / dd < 0.5f) continue;
      if (dd < bs) bs = dd, t = e;
    }
    if (!t) return;
  }
  float dd = flat_dist(p, t), gap = dd - (KIND[t->kind].radius * scale_of(t) + d->reach * 0.8f);
  if (gap <= 0 || dd > 3.6f + d->reach) return;
  p->yaw = bearing(p, t);
  p->magnet = fminf(gap, 1.0f) / ((float)d->on / TPS);
}

static void player_step(actor *p, const input *in) {
  float dt = 1.0f / TPS;
  p->yaw_prev = p->yaw;
  if (p->iframes > 0) p->iframes--;
  if (p->flash > 0) p->flash--;
  float wx, wz;
  stick_world(in, &wx, &wz);
  float mag = sqrtf(wx * wx + wz * wz);
  /* stamina comes back after a short wait */
  if (p->stam_wait > 0) p->stam_wait--;
  else p->stamina = fminf(p->stamina_max, p->stamina + 48 * dt);
  /* the lock */
  if (p->target >= 0 && !alive(&S.a[p->target])) p->target = sim_find_target(p->idx, 0);
  if (p->target >= 0 && flat_dist(p, &S.a[p->target]) > 24) p->target = -1;
  if (in->lock) p->target = p->target >= 0 ? -1 : sim_find_target(p->idx, 0);
  { /* a flick of the camera stick while locked: the next foe that side */
    static bool held[MAXP];
    bool flick = fabsf(q64(in->cx)) > 0.75f;
    if (p->target >= 0 && flick && !held[p->idx]) { int t2 = sim_find_target(p->idx, in->cx > 0 ? 1 : -1); if (t2 >= 0) p->target = t2; }
    held[p->idx] = fabsf(q64(in->cx)) > 0.4f;
  }
  const actor *t = p->target >= 0 ? &S.a[p->target] : NULL;
  p->st_t++;
  float vx = 0, vz = 0;
  switch (p->state) {
  case ST_MOVE: {
    float sp = KIND[p->kind].speed;
    if (t) sp *= 0.72f; /* locked: a circling walk */
    vx = wx * sp, vz = wz * sp;
    float face = t ? bearing(p, t) : mag > 0.1f ? cw_atan2(wx, wz) : p->yaw;
    p->yaw = turn_to(p->yaw, face, (t ? 10.0f : 12.0f) * dt);
    int k = S.interact;
    if (in->use && k >= 0 && p->idx == 0) { use_obj(p, k); break; }
    if (in->use && p->idx > 0) { int k2 = interact_obj(p); if (k2 >= 0) { use_obj(p, k2); break; } }
    if ((in->swap || (in->use && k < 0)) && S.sv.maul) { p->weapon ^= 1; ev(EV_PICK, p->idx, -1, pos(p).x, pos(p).y + 1, pos(p).z, 0); }
    if (in->tonic && p->tonics > 0) { set_state(p, ST_DRINK, 54); break; }
    if (in->dodge && p->stamina > 0) {
      p->stamina -= 22, p->stam_wait = 36;
      if (mag > 0.2f) { set_state(p, ST_DODGE, 37); p->dodge_dir = 0; p->roll_x = wx / mag, p->roll_z = wz / mag; p->yaw = cw_atan2(wx, wz); }
      else { set_state(p, ST_DODGE, 25); p->dodge_dir = 1; p->roll_x = -cw_sin(p->yaw), p->roll_z = -cw_cos(p->yaw); }
      S.dodges++;
      ev(EV_DODGE, p->idx, 0, pos(p).x, pos(p).y, pos(p).z, 0);
      break;
    }
    if ((in->attack || in->heavy) && p->stamina > 0) {
      int a = p->weapon ? (in->heavy ? A_MAUL2 : A_MAUL1) : (in->heavy ? A_HEAVY : A_SLASH1);
      if (t) p->yaw = bearing(p, t);
      else if (mag > 0.2f) p->yaw = cw_atan2(wx, wz);
      start_attack(p, a);
      aim_cut(p, t, wx, wz, mag);
    }
    break;
  }
  case ST_ATTACK: {
    const attack_def *d = &ATK[p->atk];
    if (p->st_t < 6) { /* steer the first frames */
      float face = t ? bearing(p, t) : mag > 0.2f ? cw_atan2(wx, wz) : p->yaw;
      p->yaw = turn_to(p->yaw, face, 8.0f * dt);
    }
    if (p->st_t <= d->off) {
      float k = p->st_t < d->on ? 0.6f : 1;
      float m = p->st_t < d->on ? p->magnet : 0;
      if (t && flat_dist(p, t) < KIND[t->kind].radius * scale_of(t) + 0.9f) k = 0, m = 0; /* do not run into the target */
      vx = cw_sin(p->yaw) * (d->lunge * k + m), vz = cw_cos(p->yaw) * (d->lunge * k + m);
    }
    resolve_swing(p);
    if ((in->attack || in->heavy) && p->st_t >= d->combo_from - 8) p->queued = true, p->queue_atk = in->heavy ? 1 : 0;
    if (d->combo_to && p->queued && p->st_t >= d->combo_from && p->st_t <= d->combo_to && p->stamina > 0) {
      int next = p->queue_atk ? (p->weapon ? A_MAUL2 : A_HEAVY) : p->atk == A_SLASH1 ? A_SLASH2 : p->atk == A_SLASH2 ? A_SLASH3 : p->atk == A_MAUL1 ? A_MAUL1 : A_SLASH1;
      if (t) p->yaw = bearing(p, t);
      else if (mag > 0.2f) p->yaw = cw_atan2(wx, wz);
      start_attack(p, next);
      break;
    }
    if (in->dodge && p->st_t > d->off && p->stamina > 0) { set_state(p, ST_MOVE, 0); player_step(p, in); return; }
    if (p->st_t >= p->st_len) set_state(p, ST_MOVE, 0);
    break;
  }
  case ST_DODGE: {
    bool roll = p->dodge_dir == 0;
    int i0 = roll ? 2 : 1, i1 = roll ? 21 : 13;
    p->iframes = p->st_t >= i0 && p->st_t <= i1 ? 1 : 0;
    float sp = roll ? (p->st_t < 26 ? 7.2f : 2.0f) : (p->st_t < 14 ? 5.6f : 0.5f);
    vx = p->roll_x * sp, vz = p->roll_z * sp;
    if (p->st_t >= p->st_len) set_state(p, ST_MOVE, 0);
    break;
  }
  case ST_HURT:
    vx = p->vx, vz = p->vz, p->vx *= 0.85f, p->vz *= 0.85f;
    if (p->st_t >= p->st_len) set_state(p, ST_MOVE, 0);
    break;
  case ST_DOWN:
    if (p->st_t < 14) vx = p->vx, vz = p->vz;
    if (p->st_t >= p->st_len) set_state(p, ST_MOVE, 0), p->iframes = 10;
    break;
  case ST_DRINK:
    vx = wx * 1.4f, vz = wz * 1.4f;
    if (p->st_t == 30) { p->tonics--; S.heal_ticks = 40; p->hp = CW_MIN(p->hp_max, p->hp + 60 + 10 * S.sv.hearts); ev(EV_DRINK, p->idx, 0, pos(p).x, pos(p).y + 1, pos(p).z, 0); }
    if (p->st_t >= p->st_len) set_state(p, ST_MOVE, 0);
    break;
  case ST_ACT:
    if (p->st_t == p->st_len / 2) act_done(p);
    if (p->st_t >= p->st_len) set_state(p, ST_MOVE, 0);
    break;
  case ST_REST:
    if (p->st_t > 50 && (mag > 0.3f || in->attack || in->dodge || in->use)) set_state(p, ST_ACT, 36), p->atk = -1;
    break;
  case ST_DEAD:
    if (p->idx > 0 && p->st_t > 300 && alive(&S.a[0])) { /* a second player comes back beside the first */
      player_reset(p, p->idx, pos(&S.a[0]).x + 1.2f, pos(&S.a[0]).z + 1, S.a[0].yaw);
      p->iframes = 90;
    }
    break;
  default: set_state(p, ST_MOVE, 0); break;
  }
  if (p->state != ST_DEAD) move_actor(p, vx, vz);
  /* pickups: walk over them */
  for (int k = 0; k < g_nobj; k++) {
    wobj *o = &g_obj[k];
    if (o->kind != OB_PICKUP || sim_has_flag(o->flag)) continue;
    if (dist2(pos(p).x, pos(p).z, o->x, o->z) < 1.3f * 1.3f && fabsf(pos(p).y - o->y) < 2) { sim_set_flag(o->flag); world_obj_state(k, true); give(p, o->item); }
  }
}

/* ── foes ──────────────────────────────────────────────────────────── */
static int choose_attack(const actor *e, float d) {
  int ph = e->phase;
  uint32_t r = (S.tick * 2654435761u) ^ (uint32_t)e->idx * 40503u;
  r ^= r >> 13;
  if (e->role == R_SLINGER) return A_THROW;
  if (e->role == R_SHIELD) return (r % 3) == 0 ? A_CLEAVE : A_BASH;
  if (e->string > 0 && rule(FR_STRING)) { /* a boss's string: the follow-up is fixed, so it can be learned */
    switch (e->kind) {
    case K_THORN: return A_SPIN;
    case K_TIDE: return e->string > 1 ? A_STAB : A_WAVE;
    case K_EMBER: return e->string > 1 ? A_SPIN : A_HAMMER;
    }
  }
  switch (e->kind) {
  case K_RAIDER: return (r & 3) == 0 ? A_CHOP : A_CLEAVE;
  case K_LANCER: return A_STAB;
  case K_BRUTE: return (r & 1) ? A_SLAM : A_HAY;
  case K_THORN:
    if (ph >= 1 && d > 5 && (r & 1)) return A_LARIAT;
    return (r % 3) == 0 ? A_SPIN : (r % 3) == 1 ? A_SLAM : A_HAY;
  case K_TIDE:
    if (ph >= 1 && (r % 4) == 0) return A_WAVE;
    if (d > 5 && (r & 1)) return A_LARIAT;
    return (r % 3) == 0 ? A_SPIN : A_STAB;
  case K_EMBER:
    if (ph >= 1 && (r % 3) == 0) return A_HAMMER;
    if (d > 6 && (r & 1)) return A_LARIAT;
    return (r % 3) == 0 ? A_SPIN : (r % 3) == 1 ? A_SLAM : A_HAY;
  }
  return A_CLEAVE;
}
static float attack_range(const actor *e, int atk) { return ATK[atk].reach * scale_of(e) + 0.5f; }

static int tokens_on(int p) {
  int n = 0;
  for (int i = MAXP; i < MAXA; i++) n += alive(&S.a[i]) && S.a[i].target == p && S.a[i].token;
  return n;
}

/* the side of a player a circling foe makes for: round toward their back (the group closes from more than one side) */
static int flank_side(const actor *e, const actor *t) {
  float a = bearing(t, e), back = t->yaw + CW_PI;
  float d = wrapf(back - a) + (float)((e->idx % 3) - 1) * 0.55f; /* (spread over the back, not one spot) */
  if (fabsf(d) < 0.35f) return 0; /* behind: hold there */
  return d > 0 ? -1 : 1;
}

static void foe_step(actor *e) {
  float dt = 1.0f / TPS;
  e->yaw_prev = e->yaw;
  if (e->flash > 0) e->flash--;
  if (e->cooldown > 0) e->cooldown--;
  if (e->crit_t > 0) e->crit_t--;
  if (e->spent > 0) e->spent--;
  /* the arm recovers: a knocked-aside shield (guard below 0) comes back up after five seconds, a worn one slowly */
  if (e->role == R_SHIELD && e->guard < 60 && (e->guard < 0 || (e->state == ST_MOVE && (S.tick % 4) == 0))) e->guard++;
  e->st_t++;
  float vx = 0, vz = 0;
  const kind_def *K = &KIND[e->kind];
  if (g_foe_passive && e->state != ST_HURT && e->state != ST_DOWN && e->state != ST_DEAD) { move_actor(e, 0, 0); return; }
  /* who to hunt: the nearest living player */
  int best = -1;
  float bd = 1e30f;
  for (int i = 0; i < MAXP; i++) {
    if (!alive(&S.a[i])) continue;
    float d = flat_dist(e, &S.a[i]);
    if (d < bd) bd = d, best = i;
  }
  float hx = e->home_x - pos(e).x, hz = e->home_z - pos(e).z, home_d = sqrtf(hx * hx + hz * hz);
  /* knocked or charged off a ledge it cannot climb back: after four seconds it is back at its post, healed */
  if (e->boss && pos(e).y < e->home_y - 3.0f && e->state != ST_DEAD) e->off_t++; /* (bosses: their rings are flat; ordinary foes may follow you down a slope) */
  else e->off_t = 0;
  if (e->off_t > 240) {
    cw_mover3d_teleport(&e->mv, (t3_vec3){ e->home_x, e->home_y + 0.05f, e->home_z });
    e->off_t = 0, e->hp = e->hp_max, e->aggro = false, e->target = -1, e->token = 0, e->phase = 0, e->string = 0, e->spent = 0;
    set_state(e, ST_IDLE_HOME, 0);
    if (S.boss == e->idx) S.boss = -1;
    return;
  }
  float leash = e->boss ? 26 : 24;
  if (e->state == ST_IDLE_HOME) {
    if (e->role == R_LURKER && rule(FR_LURK)) { /* under the water at its post until someone wades close: then it rises */
      if (home_d > 0.8f) vx = hx / home_d * K->speed, vz = hz / home_d * K->speed;
      if (best >= 0 && bd < 6.5f && fabsf(pos(&S.a[best]).y - pos(e).y) < 3) {
        e->aggro = true, e->target = best, e->yaw = bearing(e, &S.a[best]);
        set_state(e, ST_ROAR, 44);
        ev(EV_RISE, e->idx, 0, pos(e).x, pos(e).y, pos(e).z, 0);
      }
      move_actor(e, vx, vz);
      return;
    }
    if (home_d > 0.8f) { /* walk home */
      float sp = K->speed * 0.6f;
      vx = hx / home_d * sp, vz = hz / home_d * sp;
      e->yaw = turn_to(e->yaw, cw_atan2(hx, hz), 5 * dt);
    } else e->yaw = turn_to(e->yaw, e->home_yaw, 2 * dt);
    if (e->hp < e->hp_max && home_d < 1) e->hp = CW_MIN(e->hp_max, e->hp + 2);
    if (best >= 0 && bd < K->aggro && fabsf(pos(&S.a[best]).y - pos(e).y) < 5) {
      e->aggro = true, e->target = best;
      set_state(e, ST_MOVE, 0);
      for (int i = MAXP; i < MAXA; i++) /* the group wakes together (a lurker waits for its own moment) */
        if (alive(&S.a[i]) && S.a[i].group == e->group && S.a[i].state == ST_IDLE_HOME) S.a[i].aggro = true, S.a[i].target = best, set_state(&S.a[i], ST_MOVE, 0);
      if (e->boss) { S.boss = e->idx; ev(EV_BOSS, e->idx, 1, pos(e).x, pos(e).y, pos(e).z, 0); S.banner_t = 200, S.banner_kind = 4, S.banner_item = e->kind; }
    }
    move_actor(e, vx, vz);
    return;
  }
  /* a boss holds its ground while anyone is near its ring; others give up when they are drawn too far from home */
  bool lost = best < 0 || (e->boss ? sqrtf(dist2(pos(&S.a[best]).x, pos(&S.a[best]).z, e->home_x, e->home_z)) > 34 : home_d > leash || bd > 30);
  if (lost) { /* lost them: back home */
    e->aggro = false, e->target = -1, e->token = 0;
    if (e->state == ST_MOVE) set_state(e, ST_IDLE_HOME, 0);
    if (e->boss && S.boss == e->idx) S.boss = -1, e->hp = e->hp_max, e->phase = 0, e->string = 0, e->spent = 0;
  }
  if (e->target >= 0 && !alive(&S.a[e->target])) e->target = best;
  actor *t = e->target >= 0 ? &S.a[e->target] : NULL;
  switch (e->state) {
  case ST_MOVE: {
    if (!t) { set_state(e, ST_IDLE_HOME, 0); break; }
    float d = flat_dist(e, t), b = bearing(e, t);
    float sp = K->speed * (e->phase ? 1.18f : 1);
    float fx = cw_sin(b), fz = cw_cos(b);
    if (e->role == R_SLINGER) { /* keeps its distance and throws: it punishes standing still */
      float want = 9.0f;
      if (d > want + 2) vx = fx * sp, vz = fz * sp;
      else if (d < 5.0f) vx = -fx * sp * 0.85f, vz = -fz * sp * 0.85f;
      else { /* sidestep while it waits */
        if (e->think-- <= 0) e->think = 80 + (e->idx * 11) % 50, e->strafe = (S.tick / 89 + (uint32_t)e->idx) & 1 ? 1 : -1;
        vx = fz * (float)e->strafe * sp * 0.4f, vz = -fx * (float)e->strafe * sp * 0.4f;
      }
      e->yaw = turn_to(e->yaw, b, 6.0f * dt);
      e->atk = A_THROW;
      if (e->cooldown == 0 && d > 3.5f && d < 15 && fabsf(wrapf(b - e->yaw)) < 0.3f && fabsf(pos(t).y - pos(e).y) < 4) start_attack(e, A_THROW), e->atk = A_THROW;
      break;
    }
    /* the token: bosses always; others while fewer than two hold one on this player */
    if (!e->token && (e->boss || tokens_on(e->target) < 2) && e->cooldown == 0) e->token = 1;
    int atk = e->atk ? e->atk : choose_attack(e, d);
    float want = e->token ? attack_range(e, atk) - 0.6f : 4.6f + (float)(e->idx % 3) * 0.6f;
    if (e->kind == K_LANCER && e->token && e->role == R_PLAIN) want = 2.4f;
    if (e->boss && e->spent > 0) want = fmaxf(want, 3.5f), e->token = 1; /* catching its breath: it holds back */
    if (d > want + 0.4f) vx = fx * sp, vz = fz * sp;
    else if (d < want - 0.8f) vx = -fx * sp * 0.6f, vz = -fz * sp * 0.6f;
    if (!e->token) { /* circle at a distance, round toward the player's back */
      if (rule(FR_FLANK)) {
        if (e->think-- <= 0) e->think = 20, e->strafe = flank_side(e, t);
      } else if (e->think-- <= 0) e->think = 90 + (e->idx * 13) % 60, e->strafe = (S.tick / 97 + (uint32_t)e->idx) & 1 ? 1 : -1;
      float k = rule(FR_FLANK) ? 0.6f : 0.45f;
      vx += fz * (float)e->strafe * sp * k, vz += -fx * (float)e->strafe * sp * k;
    }
    e->yaw = turn_to(e->yaw, b, (e->boss ? 3.5f : e->role == R_SHIELD ? 8.0f : 6.0f) * dt);
    e->atk = atk;
    if (e->token && e->cooldown == 0 && e->spent == 0 && d <= attack_range(e, atk) && fabsf(wrapf(b - e->yaw)) < 0.5f) {
      start_attack(e, atk);
      e->atk = atk;
    }
    break;
  }
  case ST_ATTACK: {
    const attack_def *d = &ATK[e->atk];
    if (t && e->st_t < d->on - 8) e->yaw = turn_to(e->yaw, bearing(e, t), (e->boss ? 2.4f : 3.5f) * dt); /* tracks through the wind-up, not the blow */
    if (e->st_t >= d->on - 6 && e->st_t <= d->off) { float k = e->atk == A_LARIAT ? 1 : 0.8f; vx = cw_sin(e->yaw) * d->lunge * k, vz = cw_cos(e->yaw) * d->lunge * k; }
    if (e->atk == A_THROW && e->st_t == d->on && t) throw_stone(e, t);
    resolve_swing(e);
    if (e->st_t >= e->st_len) {
      int done = e->atk;
      set_state(e, ST_MOVE, 0);
      e->atk = 0;
      e->cooldown = e->boss ? 26 - 8 * e->phase : e->role == R_SLINGER ? 120 + (e->idx * 17) % 50 : 70 + (e->idx * 17) % 50;
      if (!e->boss) e->token = 0;
      if (e->boss) {
        bool big = done == A_SLAM || done == A_HAMMER || done == A_LARIAT || done == A_SPIN || done == A_WAVE;
        if (e->string > 0) e->string--;
        else if (e->phase >= 1 && rule(FR_STRING) && !big) { /* later phases: the blow opens a string of two or three */
          e->string = e->phase >= 2 ? 2 : 1;
          S.strings++;
        }
        if (e->string > 0) e->cooldown = 4;
        else if (big && rule(FR_SPENT)) { /* after a big blow (or a string's last) it stands spent: the opening */
          e->spent = 56 - 8 * e->phase;
          ev(EV_LAND, e->idx, done, pos(e).x, pos(e).y + 2.2f * scale_of(e), pos(e).z, 0);
        }
      }
    }
    break;
  }
  case ST_HURT:
    vx = e->vx, vz = e->vz, e->vx *= 0.85f, e->vz *= 0.85f;
    if (e->st_t >= e->st_len) set_state(e, ST_MOVE, 0), e->cooldown = CW_MAX(e->cooldown, 20);
    break;
  case ST_DOWN:
    if (e->st_t < 14) vx = e->vx, vz = e->vz;
    if (e->st_t >= e->st_len) set_state(e, ST_MOVE, 0), e->cooldown = 30;
    break;
  case ST_ROAR:
    e->iframes = 1;
    if (e->st_t >= e->st_len) set_state(e, ST_MOVE, 0), e->iframes = 0, e->cooldown = e->role == R_LURKER ? 0 : 10;
    break;
  case ST_DEAD:
    if (e->st_t >= e->st_len) e->active = false;
    return;
  default: set_state(e, ST_MOVE, 0); break;
  }
  if (e->iframes > 0 && e->state != ST_ROAR) e->iframes--;
  /* bosses: a phase at two thirds and one third of their health */
  if (e->boss && e->state != ST_ROAR) {
    int want = e->hp < e->hp_max / 3 ? 2 : e->hp < e->hp_max * 2 / 3 ? 1 : 0;
    if (e->kind == K_THORN && want > 1) want = 1;
    if (want > e->phase) {
      e->phase = want, e->string = 0, e->spent = 0;
      set_state(e, ST_ROAR, 72);
      hold(10);
      ev(EV_ROAR, e->idx, want, pos(e).x, pos(e).y, pos(e).z, 0);
      for (int i = 0; i < MAXP; i++) { /* the roar's breath pushes everyone near back out of reach (no harm): room to read the new phase */
        actor *p = &S.a[i];
        if (!alive(p) || flat_dist(p, e) > 5.5f) continue;
        if (p->state == ST_MOVE || p->state == ST_ATTACK || p->state == ST_DRINK) set_state(p, ST_HURT, 24);
        knock_from(p, pos(e).x, pos(e).z, 160, false);
      }
    }
  }
  move_actor(e, vx, vz);
}

/* ── the step ──────────────────────────────────────────────────────── */
static void hash_state(void) {
  uint32_t h = S.hash;
  for (int i = 0; i < MAXA; i++) {
    const actor *a = &S.a[i];
    if (!a->used) continue;
    int32_t v[6] = { (int32_t)(pos(a).x * 256), (int32_t)(pos(a).y * 256), (int32_t)(pos(a).z * 256), a->hp, a->state, (int32_t)(a->yaw * 1000) };
    const uint8_t *b = (const uint8_t *)v;
    for (size_t k = 0; k < sizeof v; k++) h = (h ^ b[k]) * 16777619u;
  }
  S.hash = h;
}

bool g_held_now; /* this tick was held (the view stops the clips) */
void sim_step(const input in_[MAXP]) {
  S.ev_n = 0;
  S.tick++;
  S.sv.play_ticks++;
  if (S.hitstop > 0 && S.state != GS_PLAY) S.hitstop = 0; /* (a fall or the win ends a hold) */
  if (S.hitstop > 0) { /* the hold: nothing moves; presses are kept for the tick after */
    S.hitstop--, S.held_ticks++;
    g_held_now = true;
    for (int i = 0; i < MAXP; i++) {
      input *h = &S.held[i];
      const input *q = &in_[i];
      h->attack |= q->attack, h->heavy |= q->heavy, h->dodge |= q->dodge, h->use |= q->use, h->lock |= q->lock, h->tonic |= q->tonic, h->swap |= q->swap;
    }
    S.cam_pos_prev = S.cam_pos, S.cam_target_prev = S.cam_target;
    hash_state();
    return;
  }
  g_held_now = false;
  input in[MAXP];
  for (int i = 0; i < MAXP; i++) {
    const input *h = &S.held[i];
    in[i] = in_[i];
    in[i].attack |= h->attack, in[i].heavy |= h->heavy, in[i].dodge |= h->dodge, in[i].use |= h->use, in[i].lock |= h->lock, in[i].tonic |= h->tonic, in[i].swap |= h->swap;
  }
  memset(S.held, 0, sizeof S.held);
  if (S.banner_t > 0) S.banner_t--;
  if (S.heal_ticks > 0) S.heal_ticks--;
  camera_step(&in[0]);
  /* the region the lead player is in: paging and the banner */
  const actor *lead = &S.a[0];
  int rg = world_region_at(pos(lead).x, pos(lead).z);
  if (rg != S.region) {
    if (S.region >= 0) S.banner_t = 180, S.banner_kind = 0, S.banner_item = rg;
    S.region_prev = S.region, S.region = rg, S.region_t = 0;
    ev(EV_REGION, rg, S.region_prev, 0, 0, 0, 0);
  }
  S.region_t++;
  world_page(pos(lead).x, pos(lead).z, rg);
  for (int i = MAXP; i < MAXA; i++) {
    actor *e = &S.a[i];
    if (!e->used) continue;
    bool live = (g_world_paged >> e->region) & 1;
    if (e->state == ST_DEAD && e->st_t >= e->st_len) live = false;
    e->active = live;
  }
  S.interact = alive(lead) && lead->state == ST_MOVE ? interact_obj(lead) : -1;
  if (S.state == GS_PLAY) {
    for (int i = 0; i < MAXP; i++)
      if (S.a[i].used) player_step(&S.a[i], &in[i]);
    for (int i = MAXP; i < MAXA; i++)
      if (S.a[i].used && (S.a[i].active || S.a[i].state == ST_DEAD)) foe_step(&S.a[i]);
    separate();
    shocks_step();
    stones_step();
    /* a second player too far from the first comes back to them */
    for (int i = 1; i < MAXP; i++)
      if (alive(&S.a[i]) && alive(lead) && flat_dist(&S.a[i], lead) > 22) cw_mover3d_teleport(&S.a[i].mv, (t3_vec3){ pos(lead).x + 1, pos(lead).y + 0.2f, pos(lead).z + 1 });
    if (sim_alive_players() == 0) S.state = GS_FALLEN, S.state_t = 0, S.sv.deaths++, S.deaths++;
  } else if (S.state == GS_FALLEN) {
    if (++S.state_t > 60 * 4) { /* back at the shrine, the foes back where they stood */
      S.state = GS_PLAY;
      sim_respawn_foes();
      S.boss = -1;
      wake_at_shrine();
      memset(S.sh, 0, sizeof S.sh);
      memset(S.st, 0, sizeof S.st);
    }
  } else if (S.state == GS_WON) {
    S.state_t++;
    for (int i = 0; i < MAXP; i++)
      if (S.a[i].used && S.a[i].state != ST_DEAD) move_actor(&S.a[i], 0, 0);
  }
  hash_state();
}
