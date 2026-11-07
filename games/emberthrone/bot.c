/* emberthrone: the autopilot. It plays through the same input the pads give (a stick, the face buttons, the lock),
 * from what a player can see: the route through the game (world.c's g_route, the order a player would take:
 * rest, the canyon, the maul, the first boss, the passage, the marsh, the second boss, the shortcut, the cellar,
 * the gate, the keep, the last boss), the foes near it and their wind-ups. It locks on, cuts in combos, rolls out
 * of a blow it sees coming (and through a shockwave), drinks when low, breaks walls with the maul, rests at shrines.
 * A second seat follows the first and fights beside it. Everything it reads is the rules' state, so it plays the
 * same on every host. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

int g_bot_stuck;
static int R_i, R_t, R_wait, R_stuck_t, R_side, R_rest_t, R_press, R_stuck_n, R_step_seen = -1;
static float R_lx, R_lz;
int bot_route_index(void) { return R_i; }
int bot_step_ticks(void) { return R_t; }
void bot_jump(int i) { R_i = i; }
void bot_reset(void) { R_i = R_t = R_wait = R_stuck_t = R_rest_t = R_press = 0, R_side = 1, g_bot_stuck = 0; }

static void to_stick(float dx, float dz, float mag, input *in) {
  float l = sqrtf(dx * dx + dz * dz);
  if (l < 1e-4f) { in->mx = in->my = 0; return; }
  dx /= l, dz /= l;
  float yaw = sim_cam_yaw(), sy = cw_sin(yaw), cy = cw_cos(yaw);
  in->mx = (cy * dx - sy * dz) * mag;
  in->my = (sy * dx + cy * dz) * mag;
}
static bool done_step(const wroute *r) {
  if (r->act == RA_FIGHT || r->act == RA_USE || r->act == RA_HIT) return r->need_flag >= 0 && sim_has_flag(r->need_flag);
  if (r->act == RA_GO && r->need_flag >= 0) return sim_has_flag(r->need_flag);
  return false;
}

/* the foe that most needs answering: a boss in its fight, else the nearest awake one */
static int threat(const actor *p, float range) {
  int best = -1;
  float bd = range * range;
  for (int i = MAXP; i < MAXA; i++) {
    const actor *e = &S.a[i];
    if (!e->used || !e->active || e->state == ST_DEAD) continue;
    if (!e->aggro && e->state == ST_IDLE_HOME) continue;
    float d = dist2(p->mv.position.x, p->mv.position.z, e->mv.position.x, e->mv.position.z);
    if (fabsf(e->mv.position.y - p->mv.position.y) > 4) continue;
    if (e->boss) d *= 0.5f;
    if (d < bd) bd = d, best = i;
  }
  return best;
}
/* a blow about to land on p: from any foe in its wind-up within reach, or a shockwave arriving */
static bool incoming(const actor *p, float *ax, float *az) {
  for (int i = MAXP; i < MAXA; i++) {
    const actor *e = &S.a[i];
    if (!e->used || !e->active || e->state != ST_ATTACK) continue;
    const attack_def *d = &ATK[e->atk];
    float reach = d->reach * KIND[e->kind].scale + 1.4f + (e->atk == A_LARIAT ? 4 : 0);
    float dx = p->mv.position.x - e->mv.position.x, dz = p->mv.position.z - e->mv.position.z;
    if (dx * dx + dz * dz > reach * reach) continue;
    int lead = d->on - e->st_t;
    if (lead >= 2 && lead <= 9) { *ax = e->mv.position.x, *az = e->mv.position.z; return true; }
  }
  for (int k = 0; k < 4; k++) {
    const shock *s = &S.sh[k];
    if (!s->on || (s->hit >> p->idx) & 1) continue;
    float d = sqrtf(dist2(p->mv.position.x, p->mv.position.z, s->x, s->z));
    float gap = d - s->r;
    if (gap > 0 && gap < s->speed * 7) { *ax = s->x, *az = s->z; return true; }
  }
  return false;
}

static void fight(const actor *p, int ti, input *in, int seat) {
  const actor *t = &S.a[ti];
  float dx = t->mv.position.x - p->mv.position.x, dz = t->mv.position.z - p->mv.position.z, d = sqrtf(dx * dx + dz * dz);
  if (p->target != ti && (S.tick + (uint32_t)seat * 7) % 20 == 0) in->lock = p->target >= 0 ? (p->target != ti) : true;
  float ax, az;
  bool busy = p->state != ST_MOVE;
  static int weak = -1; /* the soak's control: a bot that never dodges or drinks */
  if (weak < 0) weak = getenv("ADV_BOT_WEAK") != NULL;
  if (weak) busy = true;
  if (!busy && incoming(p, &ax, &az) && p->stamina > 22) {
    /* roll across the blow, away from where it comes from */
    float ox = p->mv.position.x - ax, oz = p->mv.position.z - az, l = sqrtf(ox * ox + oz * oz) + 1e-3f;
    float side = (S.tick / 120) & 1 ? 1.0f : -1.0f;
    to_stick(ox / l * 0.6f + (-oz / l) * side, oz / l * 0.6f + (ox / l) * side, 1, in);
    in->dodge = true;
    return;
  }
  if (!busy && p->hp < p->hp_max * 2 / 5 && p->tonics > 0 && d > 3.5f) { in->tonic = true; return; }
  float reach = (p->weapon ? 2.2f : 2.1f) + KIND[t->kind].radius * KIND[t->kind].scale;
  if (d > reach) to_stick(dx, dz, 1, in);
  else if (d < reach - 1.2f) to_stick(-dx, -dz, 0.6f, in);
  if (d <= reach + 0.3f && p->stamina > 26 && (t->state != ST_ATTACK || t->st_t > ATK[t->atk].off)) {
    bool heavy = (S.tick / 60 + (uint32_t)ti) % 5 == 0 || t->state == ST_DOWN;
    if (p->state == ST_MOVE || (p->state == ST_ATTACK && p->st_t >= ATK[p->atk].combo_from - 4)) {
      if ((R_press++ & 1) == 0) { if (heavy) in->heavy = true; else in->attack = true; }
    }
  }
  /* back off to get stamina back */
  if (p->stamina < 20 && d < 4) to_stick(-dx, -dz, 1, in);
}

void bot_input(int seat, input *in) {
  memset(in, 0, sizeof *in);
  const actor *p = &S.a[seat];
  if (!p->used || p->state == ST_DEAD || S.state != GS_PLAY) return;
  if (seat > 0) { /* the second seat: beside the first, into its fights */
    int ti = threat(p, 9);
    if (ti >= 0) { fight(p, ti, in, seat); return; }
    const actor *l = &S.a[0];
    float dx = l->mv.position.x - p->mv.position.x, dz = l->mv.position.z - p->mv.position.z, d = sqrtf(dx * dx + dz * dz);
    if (d > 2.6f) to_stick(dx, dz, cw_clampf((d - 2) / 3, 0.3f, 1), in);
    return;
  }
  /* woke at a shrine after a fall: walk the route again from that shrine (the ways between are walked, not flown) */
  static int deaths_seen;
  if (S.deaths != deaths_seen) {
    deaths_seen = S.deaths;
    int so = world_shrine_obj(S.sv.shrine);
    for (int k = 0; k < g_nroute; k++) if (g_route[k].act == RA_REST && g_route[k].obj == so) { R_i = k + 1, R_t = 0; break; }
  }
  /* skip what is already done (a continued game) */
  while (R_i < g_nroute && done_step(&g_route[R_i])) R_i++, R_t = 0;
  if (R_i >= g_nroute) return;
  const wroute *r = &g_route[R_i];
  if (R_i != R_step_seen) R_step_seen = R_i, R_stuck_n = 0;
  if (p->state == ST_REST) { /* rested: get up after a moment */
    if (++R_rest_t > 70) { to_stick(0, 1, 1, in); if (r->act == RA_REST) R_i++, R_t = 0; }
    return;
  }
  R_rest_t = 0;
  /* worn down with the flask empty: back to the last shrine to rest, then on again */
  static bool retreat;
  if (p->hp < p->hp_max * 3 / 10 && p->tonics == 0 && S.boss < 0) retreat = true;
  if (retreat) {
    int so = world_shrine_obj(S.sv.shrine);
    if (so < 0 || p->hp == p->hp_max) retreat = false;
    else {
      float sx = g_obj[so].x - p->mv.position.x, sz = g_obj[so].z - p->mv.position.z;
      if (sx * sx + sz * sz > 2.2f * 2.2f) { to_stick(sx, sz, 1, in); return; }
      if (p->state == ST_MOVE && S.interact >= 0 && (S.tick % 20) == 0) in->use = true;
      return;
    }
  }
  bool fighting_boss = r->act == RA_FIGHT;
  int ti = threat(p, fighting_boss ? 30 : 11);
  if (ti >= 0 && (r->act != RA_HIT || S.a[ti].aggro)) { fight(p, ti, in, 0); return; }
  if (p->target >= 0 && (S.tick % 30) == 0) in->lock = true; /* nothing to fight: let go */
  float dx = r->x - p->mv.position.x, dz = r->z - p->mv.position.z, d = sqrtf(dx * dx + dz * dz);
  /* stuck: no progress for two seconds, sidestep round it for a while */
  if ((S.tick % 60) == 0) {
    float moved = sqrtf(dist2(p->mv.position.x, p->mv.position.z, R_lx, R_lz));
    if (moved < 0.6f && (d > 1.5f || R_stuck_n > 0) && p->state == ST_MOVE) {
      R_stuck_t = 50, R_side = -R_side, g_bot_stuck++;
      /* stuck again and again on one step (fallen below it, a wall between): walk back to the point before it */
      if (++R_stuck_n >= 5 && R_i > 0) {
        /* the second time on the same step (fallen off a stair below its top: the steps back are overhead too): back
         * to the last shrine before it and the whole way on from there */
        static int backed_from = -1, backs;
        backs = backed_from == R_i ? backs + 1 : 1, backed_from = R_i;
        R_i--, R_stuck_n = 0, R_t = 0;
        if (backs >= 2) { while (R_i > 0 && g_route[R_i].act != RA_REST) R_i--; R_i++, backs = 0; }
        else while (R_i > 0 && (done_step(&g_route[R_i]) || g_route[R_i].act != RA_GO)) R_i--;
      }
    }
    R_lx = p->mv.position.x, R_lz = p->mv.position.z;
  }
  if (R_stuck_t > 0) {
    R_stuck_t--;
    float l = d + 1e-3f;
    to_stick(dx / l * 0.3f - dz / l * (float)R_side, dz / l * 0.3f + dx / l * (float)R_side, 1, in);
    if (R_stuck_t == 25 && p->stamina > 40) in->dodge = true;
    return;
  }
  float arrive = r->act == RA_GO ? 1.6f : 1.0f;
  if (d > arrive) { to_stick(dx, dz, 1, in); return; }
  R_t++; /* (counts only the time spent at the step itself: not the walk there, not the fights on the way) */
  switch (r->act) {
  case RA_GO: if (r->need_flag < 0) R_i++, R_t = 0; break;
  case RA_REST: case RA_USE: {
    /* walk up to the thing until it can be used */
    int ob = r->obj;
    for (int k = 0; ob < 0 && k < g_nobj; k++) if (g_obj[k].flag == r->need_flag && g_obj[k].kind != OB_PICKUP) ob = k;
    if (S.interact < 0 && ob >= 0) to_stick(g_obj[ob].x - p->mv.position.x, g_obj[ob].z - p->mv.position.z, 0.4f, in);
    if (p->state == ST_MOVE && S.interact >= 0 && (R_t % 20) == 0) in->use = true;
    if (R_t > 400) R_i++, R_t = 0; /* (never stalls the run) */
    break;
  }
  case RA_HIT: {
    if (p->weapon != 1) { if ((R_t % 30) == 0) in->swap = true; break; }
    /* face the wall (east of the point by the route's layout: toward the object) and swing */
    for (int k = 0; k < g_nobj; k++)
      if (g_obj[k].flag == r->need_flag) { float ox = g_obj[k].x - p->mv.position.x, oz = g_obj[k].z - p->mv.position.z; to_stick(ox, oz, 0.25f, in); }
    if (p->state == ST_MOVE && (R_t % 24) == 0) in->attack = true;
    if (R_t > 600) R_i++, R_t = 0;
    break;
  }
  case RA_FIGHT: if (R_t > 60 * 90) R_i++, R_t = 0; break;
  default: R_i++; break;
  }
}
