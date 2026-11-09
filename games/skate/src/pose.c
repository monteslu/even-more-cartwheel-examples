/* skate pose: the rider's body and the board under it, from the board's state, every frame. No clips: a few targets
 * per state (where each foot stands on the deck and how it turns, how far the knees bend, where the hips sit, how far
 * the pelvis, chest and head open toward the nose, where the arms hang) are eased toward what the board is doing; the
 * hips' height is solved from the knee bend wanted, and the legs and the grabbing hand with two-bone IK so the feet
 * stay planted on the deck wherever it is (tilted in a carve or a manual, popped nose up in an ollie, lifted to the
 * knees in a tuck). The numbers come from how skaters ride (internal research notes: coaching guides, ollie studies
 * and an open motion-capture set of a skater):
 *
 *   stance   body side on, feet about shoulder width: the front foot just behind the front bolts turned 40 degrees
 *            toward the nose, the back foot across the back bolts; knees bent about 45 degrees; the pelvis open
 *            25 degrees toward the nose, the chest a little more, the head turned on to look down the run; arms loose,
 *            the front one toward the nose, the back one toward the tail.
 *   push     the front foot turns along the board over the front bolts and the body turns with it; the back foot steps
 *            down beside the deck on the toe side, drives back past the tail on a bending front knee (to 80 degrees),
 *            lifts and swings forward again; arms swing against the legs. After the last push the back foot comes back
 *            across the tail and the front foot turns back across.
 *   carve    the body leans into the turn as far as v w / g asks: the knees drive over the toes for a toe side turn,
 *            the hips sit back over the heels for a heel side one; the chest and head lead the turn.
 *   pump     a curving transition loads the legs: they bend under it and stretch out of it.
 *   ollie    the crouch winds up (knees to 100 degrees, the back foot onto the tail, the front foot back toward the
 *            middle, arms down); the pop snaps the tail down (the board noses up to 26 degrees about its back wheels,
 *            the tail kept to the ground for a few frames) while the body springs up; the front foot drags up the grip
 *            to the nose and levels the board by about a fifth of a second; the knees pull up to the chest at the top;
 *            the legs stretch down for the landing, which the knees take (deeper the harder it came down).
 *   flips    the same pop; the front foot flicks off the nose (out over the heel edge for a kickflip, the toe edge for a
 *            heelflip) and both feet lift off while the board turns under them, the body kept centred over it; the
 *            back foot catches first, then the front.
 *   grabs    the knees pull the board up to the hand rather than the hand reaching down; the free arm out for balance.
 *   grinds   knees bent over the truck that grinds, arms wide and seesawing with the balance, the head down the rail.
 *   manuals  the hips over the truck it rolls on, that knee bent and the other leg straighter, arms out.
 *
 * The rider's model space: x along the board's nose (the left foot leads: regular stance), y up from the deck's top,
 * z toward the toe edge, where the rider faces. Presentation only: nothing here feeds the simulation. */
#include <string.h>

#include "view.h"

static t3_quat qeuler(float x, float y, float z) {
  float sx, cx, sy, cy, sz, cz;
  cw_sincos(x * 0.5f, &sx, &cx), cw_sincos(y * 0.5f, &sy, &cy), cw_sincos(z * 0.5f, &sz, &cz);
  /* XYZ order, as the generator writes its offsets */
  return (t3_quat){ sx * cy * cz + cx * sy * sz, cx * sy * cz - sx * cy * sz, cx * cy * sz + sx * sy * cz, cx * cy * cz - sx * sy * sz };
}
static t3_quat qm(t3_quat a, t3_quat b) { return t3_quat_multiply(a, b); }
static t3_quat qax(v3 ax, float a) { return cw_quat_axis_angle_det(ax, a); }
static t3_quat qconj(t3_quat q) { return (t3_quat){ -q.x, -q.y, -q.z, q.w }; }
static v3 qv(t3_quat q, v3 v) { return t3_vec3_apply_quat(v, q); }
/* turn a bone from its rest rotation by an offset (x forward swing / lean, y twist, z out) */
static void rot(rig *r, int b, float x, float y, float z) {
  if (b < 0) return;
  r->pose.rot[b] = qm(r->pose.rest_rot[b], qeuler(x, y, z));
}
static float sstep(float a, float b, float x) {
  float t = clampf((x - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
}
static float ease(float cur, float want, float rate, float dt) { return cur + (want - cur) * (1 - cw_exp(-rate * dt)); }

/* the deck's top over the trucks' plane at x along it (the kicktails rise over the last 13 cm; view.c's board) */
float deck_top(float x) {
  float u = clampf((fabsf(x) - 0.27f) / 0.13f, 0, 1);
  return DECK_Y + 0.06f * cw_pow(u, 1.6f);
}

#define FOOT_C 0.05f /* the sole's middle ahead of the ankle */

bool rig_bind(rig *r, t3_object *model) {
  memset(r, 0, sizeof *r);
  if (!model || !cw_pose_bind(&r->pose, model)) return false;
  cw_pose *p = &r->pose;
  r->hips = cw_pose_find(p, "Hips"), r->spine = cw_pose_find(p, "Spine"), r->chest = cw_pose_find(p, "Chest"), r->neck = cw_pose_find(p, "Neck"), r->head = cw_pose_find(p, "Head");
  r->ua_l = cw_pose_find(p, "UpperArmL"), r->la_l = cw_pose_find(p, "LowerArmL"), r->h_l = cw_pose_find(p, "HandL");
  r->ua_r = cw_pose_find(p, "UpperArmR"), r->la_r = cw_pose_find(p, "LowerArmR"), r->h_r = cw_pose_find(p, "HandR");
  r->ul_l = cw_pose_find(p, "UpperLegL"), r->ll_l = cw_pose_find(p, "LowerLegL"), r->f_l = cw_pose_find(p, "FootL");
  r->ul_r = cw_pose_find(p, "UpperLegR"), r->ll_r = cw_pose_find(p, "LowerLegR"), r->f_r = cw_pose_find(p, "FootR");
  if (r->hips < 0 || r->ul_l < 0 || r->ll_l < 0 || r->f_l < 0 || r->ul_r < 0 || r->ll_r < 0 || r->f_r < 0) return false;
  cw_pose_update_model(p);
  r->foot_rest_l = cw_pose_model_position(p, r->f_l);
  r->foot_rest_r = cw_pose_model_position(p, r->f_r);
  r->foot_rot_l = cw_pose_model_rotation(p, r->f_l);
  r->foot_rot_r = cw_pose_model_rotation(p, r->f_r);
  r->hip_rest = cw_pose_model_position(p, r->hips);
  v3 a = cw_pose_model_position(p, r->ul_l), k = cw_pose_model_position(p, r->ll_l);
  r->thigh = vlen(vsub(a, k)), r->shin = vlen(vsub(k, r->foot_rest_l));
  r->leg_len = r->thigh + r->shin;
  r->ankle_h = 0.5f * (r->foot_rest_l.y + r->foot_rest_r.y);
  r->s.knee = 0.75f, r->s.open = 0.45f, r->s.front_yaw = 0.7f, r->s.front_x = 0.17f, r->s.back_x = -0.22f, r->s.drop_air = 0.1f;
  r->board_q = (t3_quat){ 0, 0, 0, 1 };
  r->last_st = -1;
  /* the mirror's partners (a bone named ...L and one named ...R) and the rest pose in model space */
  r->nb = p->count < RIG_MAXB ? p->count : 0;
  for (int i = 0; i < r->nb; i++) {
    r->mate[i] = i;
    r->rest_mrot[i] = cw_pose_model_rotation(p, i), r->rest_mpos[i] = cw_pose_model_position(p, i);
    const char *n = p->name ? p->name[i] : NULL;
    size_t l = n ? strlen(n) : 0;
    if (l < 2 || (n[l - 1] != 'L' && n[l - 1] != 'R')) continue;
    char o[64];
    if (l >= sizeof o) continue;
    memcpy(o, n, l + 1);
    o[l - 1] = n[l - 1] == 'L' ? 'R' : 'L';
    int j = cw_pose_find(p, o);
    if (j >= 0) r->mate[i] = j;
  }
  r->bound = true;
  return true;
}

/* goofy: the pose mirrored. Each bone takes its partner's turn from rest, reflected across the body's middle (the
   rider's model is side on: its left and right lie along x), and its partner's place reflected the same way; then the
   whole turns half round about the vertical, so the rider faces the heel edge with the right foot toward the nose. The
   board's placement is reflected across its own middle line to match (toe and heel edges swap). Parents first, each
   set in model space (cw_pose keeps the children with it until their own turn comes). */
static void rig_mirror(rig *r) {
  cw_pose *p = &r->pose;
  if (!r->nb) return;
  cw_pose_update_model(p);
  static t3_quat Q[RIG_MAXB];
  static t3_vec3 X[RIG_MAXB];
  for (int i = 0; i < r->nb; i++) Q[i] = cw_pose_model_rotation(p, i), X[i] = cw_pose_model_position(p, i);
  const t3_quat ry = { 0, 1, 0, 0 }; /* half a turn about y */
  for (int i = 0; i < r->nb; i++) {
    int j = r->mate[i];
    t3_quat d = qm(Q[j], qconj(r->rest_mrot[j])); /* the partner's turn from rest, model space */
    t3_quat dm = { d.x, -d.y, -d.z, d.w };         /* reflected across x = 0 */
    cw_pose_set_model_rotation(p, i, qm(qm(ry, dm), r->rest_mrot[i]));
    t3_vec3 want = { X[j].x, X[j].y, -X[j].z }, at = cw_pose_model_position(p, i);
    cw_pose_translate_model(p, i, vsub(want, at));
  }
  cw_pose_update_model(p);
  /* the board: reflected across its middle line (z = 0) */
  r->board_q = (t3_quat){ -r->board_q.x, -r->board_q.y, r->board_q.z, r->board_q.w }, r->board_p.z = -r->board_p.z;
  r->deck_q = (t3_quat){ -r->deck_q.x, -r->deck_q.y, r->deck_q.z, r->deck_q.w }, r->deck_p.z = -r->deck_p.z;
}

/* the leg's length (hip joint to ankle) at a knee bend of k radians */
static float leg_at(const rig *r, float k) {
  float c = cw_cos(k);
  return sqrtf(r->thigh * r->thigh + r->shin * r->shin + 2 * r->thigh * r->shin * c);
}

/* the targets a state asks for (eased into rider_pose) */
typedef struct want {
  float knee, hx, hz, open, pitch, chest, look, head_pitch;
  float front_x, front_z, front_yaw, back_x, back_z, back_yaw;
  float arm_out, arm_fwd, arm_back, elbow;
  float push_w, grind_w, manual, tuck, air_w, drop_air, support; /* support: 1 the front leg carries the hips' height, -1 the back, 0 both */
} want;

/* the board's own placement under the rider (root space: the board's contact frame, x nose, y up, z toe), without the
   flip's turn (feet stand on this) and with it (drawn) */
static void board_frame(rig *r, const skater *k, t3_quat *q_base, v3 *p_base, t3_quat *q_draw, v3 *p_draw) {
  rider_pose *s = &r->s;
  t3_quat q = { 0, 0, 0, 1 };
  v3 p = V(0, 0, 0);
  if (k->st == SK_ROLL && k->manual) { /* on two wheels: about the back (or front) axle */
    float a = (k->manual_kind ? -1.0f : 1.0f) * (0.2f + 0.03f * k->mbal);
    q = qax(V(0, 0, 1), a);
    p = V(0.21f * (k->manual_kind ? 1.0f : -1.0f) * (1 - cw_cos(a)), 0.21f * fabsf(cw_sin(a)), 0);
  } else if (k->st == SK_GRIND) {
    /* the trucks on the rail: a nose or a tail run tips it */
    if (k->grind_kind == 1) q = qax(V(0, 0, 1), -0.25f), p = V(0, 0.06f, 0);
    if (k->grind_kind == 2) q = qax(V(0, 0, 1), 0.25f), p = V(0, 0.06f, 0);
    p.y -= 0.03f;
  } else {
    /* the ollie's pitch: nose up about the back wheels (the tail snapped to the ground), levelled by the front foot */
    if (fabsf(s->board_pitch) > 1e-4f) {
      v3 c = V(-0.21f, 0.028f, 0);
      q = qax(V(0, 0, 1), s->board_pitch);
      p = vsub(c, qv(q, c));
    }
    /* a carve tips the deck onto its edge, the way the rider leans (the trucks let it go about 10 degrees) */
    if (k->st == SK_ROLL) q = qm(q, qax(V(1, 0, 0), clampf(s->lean, -0.6f, 0.6f) * 0.32f));
  }
  p.y += s->tuck + s->tail_drop; /* the knees pull the board up; at the pop the tail stays down a moment */
  *q_base = q, *p_base = p;
  *q_draw = q, *p_draw = p;
  if (k->st == SK_AIR && k->flip >= 0 && k->flip_t < FLIPS[k->flip].len) {
    float e = (float)k->flip_t / (float)FLIPS[k->flip].len;
    e = e * e * (3 - 2 * e);
    const trick_def *d = &FLIPS[k->flip];
    /* a kickflip turns the grip toward the heel edge first (about +x, negative): the game's roll +1 is that */
    t3_quat lq = qm(qm(qax(V(1, 0, 0), -d->roll * 2 * PI_F * e), qax(V(0, 1, 0), d->yaw * 2 * PI_F * e)), qax(V(0, 0, 1), d->pitch * 2 * PI_F * e));
    v3 lp = V(0, 0.16f * cw_sin(e * PI_F) + 0.04f * cw_sin(e * PI_F), 0); /* it rises to the feet and comes back under them */
    v3 c = V(0, 0.07f, 0); /* about the deck's middle, not its wheels' line */
    lp = vadd(lp, vsub(c, qv(lq, c)));
    *q_draw = qm(q, lq);
    *p_draw = vadd(p, qv(q, lp));
  }
}

/* a foot's ankle target and its rotation (model space) for the sole's middle at board point (x, z) turned yaw toward
   the nose, hovering h over the deck */
static void foot_on_deck(const rig *r, t3_quat bq, v3 bp, float x, float z, float yaw, float h, t3_quat rest, v3 *ankle, t3_quat *frot) {
  float sy, cy;
  cw_sincos(yaw, &sy, &cy);
  v3 sole = vadd(bp, qv(bq, V(x, deck_top(x) + h, z)));
  sole.y -= DECK_Y; /* root space to the rider's model space (the rider stands DECK_Y up the board's normal) */
  v3 fwd = qv(bq, V(sy, 0, cy)), up = qv(bq, V(0, 1, 0));
  *ankle = vadd(vsub(sole, vmul(fwd, FOOT_C)), vmul(up, r->ankle_h));
  *frot = qm(qm(bq, qax(V(0, 1, 0), yaw)), rest);
}

/* a run-out: off the board and running it out on foot, facing the way it runs (view.c turns the rider's root to the
   run: model +z along it). The strides start as it steps off and close up as it slows and steps back on; the body leans
   into the run with the arms pumping against the legs; the feet planted on the ground by two-bone IK. The cadence
   follows the speed (about 1.1 m a stride, never under two strides a second) */
static void rig_run(rig *r, const skater *k, float dt, float t) {
  cw_pose *p = &r->pose;
  float sp = sqrtf(k->v.x * k->v.x + k->v.z * k->v.z), bt = (float)k->bail_t;
  float gait = sstep(0, 6, bt) * (1 - sstep(RUNOUT_TICKS - 16, RUNOUT_TICKS - 2, bt));
  r->run_ph += fmaxf(sp / 1.1f, 2.2f) * dt * 0.5f; /* cycles: two strides each */
  float ph = (r->run_ph - floorf(r->run_ph)) * 2 * PI_F, sn = cw_sin(ph), cs = cw_cos(ph);
  float amp = gait * clampf(sp / 4.0f, 0.45f, 1.0f);
  for (int i = 0; i < p->count; i++) p->pos[i] = p->rest_pos[i], p->rot[i] = p->rest_rot[i];
  float ground = -DECK_Y + r->ankle_h;
  /* the body: down and forward into the run, a little bob at each footfall */
  rot(r, r->hips, 0.22f * gait, 0.08f * sn * amp, 0);
  v3 hp = p->rest_pos[r->hips];
  p->pos[r->hips] = V(hp.x, hp.y - DECK_Y - 0.06f - 0.05f * gait - 0.025f * fabsf(sn) * amp, hp.z);
  rot(r, r->spine, 0.12f * gait, -0.12f * sn * amp, 0);
  rot(r, r->chest, 0.08f * gait, -0.1f * sn * amp, 0);
  rot(r, r->neck, -0.15f * gait, 0, 0), rot(r, r->head, -0.12f * gait, 0, 0);
  /* the arms pump against the legs, elbows bent */
  rot(r, r->ua_l, -0.75f * sn * amp, 0.1f, 0.25f), rot(r, r->la_l, -1.25f * gait - 0.2f, 0, 0);
  rot(r, r->ua_r, 0.75f * sn * amp, -0.1f, -0.25f), rot(r, r->la_r, -1.25f * gait - 0.2f, 0, 0);
  cw_pose_update_model(p);
  /* the feet: one forward while the other drives back, each lifted as it swings through */
  v3 al = V(r->foot_rest_l.x * 0.9f, ground + 0.16f * fmaxf(0, cs) * amp, 0.42f * sn * amp);
  v3 ar = V(r->foot_rest_r.x * 0.9f, ground + 0.16f * fmaxf(0, -cs) * amp, -0.42f * sn * amp);
  cw_ik2_opts o = CW_IK2_OPTS_DEFAULT;
  o.use_pole = true;
  o.use_end_rotation = true;
  o.pole = vadd(al, V(0, 0.45f, 0.6f)), o.end_rotation = r->foot_rot_l;
  cw_ik_two_bone(p, r->ul_l, r->ll_l, r->f_l, al, &o);
  o.pole = vadd(ar, V(0, 0.45f, 0.6f)), o.end_rotation = r->foot_rot_r;
  cw_ik_two_bone(p, r->ul_r, r->ll_r, r->f_r, ar, &o);
  r->foot_err = 0, r->free_l = r->free_r = 1;
  (void)t;
}

void rig_pose(rig *r, const skater *k, float dt, float t) {
  if (!r->bound) return;
  if (k->st == SK_BAIL && k->runout) {
    if (r->last_st != SK_BAIL) r->run_ph = 0.0f;
    r->last_st = k->st;
    rig_run(r, k, dt, t);
    return;
  }
  cw_pose *p = &r->pose;
  rider_pose *s = &r->s;
  float sp = vlen(k->v);
  bool flipping = k->st == SK_AIR && k->flip >= 0 && k->flip_t < FLIPS[k->flip].len;
  float fe = flipping ? (float)k->flip_t / (float)FLIPS[k->flip].len : 0;

  /* ── timers: the air since takeoff (and whether it was popped), the landing since touchdown ── */
  if (k->st != r->last_st) {
    if (k->st == SK_AIR) {
      r->air_time = 0;
      r->popped = (k->ev & EV_POP) || (k->cb.n > 0 && !strcmp(k->cb.it[k->cb.n - 1].name, "Pop"));
      r->from_grind = r->last_st == SK_GRIND;
    }
    if (k->st == SK_BAIL) r->air_time = 0;
    if ((k->st == SK_ROLL || k->st == SK_GRIND) && r->last_st == SK_AIR) {
      r->land_time = 0;
      r->impact = clampf(-vdot(r->air_v, k->u) / 5.0f, 0, 1.4f); /* how hard it came down, into the surface */
    }
    r->last_st = k->st;
  }
  r->air_time += dt, r->land_time += dt;
  if (k->st == SK_AIR) r->air_v = k->v;
  /* the carve's lean: the heading's turn rate times the speed (the lean a turn needs, atan(v w / g)); > 0 toward the toes */
  float yawv = cw_atan2(-k->f.x, -k->f.z), lean = 0, pump = 0;
  if (k->st == SK_ROLL && sp > 1 && dt > 0) {
    float dy = wrap_pi(yawv - r->last_yaw) / dt;
    lean = cw_atan(clampf(-dy * sp / GRAV, -1.2f, 1.2f)) * (k->fakie ? -1.0f : 1.0f) * (r->goofy ? -1.0f : 1.0f); /* (goofy: posed as regular, then mirrored) */
    /* a transition's curve loads the legs: the surface's normal turning back against the run (concave) */
    v3 du = vmul(vsub(k->u, r->last_u), 1.0f / dt);
    pump = clampf(-vdot(du, vmul(k->v, 1.0f / sp)) * sp / GRAV, -0.6f, 1.2f);
  }
  r->last_yaw = yawv, r->last_u = k->u;

  /* ── what the state asks for ─────────────────────────────────────────────────────────────────────────── */
  want w = { .knee = 0.92f, .hx = -0.03f, .hz = -0.03f, .open = 0.36f, .pitch = 0.18f, .chest = 0.1f, .look = k->fakie ? -0.85f : 1.0f, .head_pitch = 0.18f,
             .front_x = 0.17f, .front_z = 0.0f, .front_yaw = 0.7f, .back_x = -0.22f, .back_z = 0.0f, .back_yaw = 0.12f,
             .arm_out = 0.32f, .arm_fwd = -0.12f, .arm_back = 0.08f, .elbow = -0.38f, .drop_air = 0.12f };
  w.knee += clampf(sp * 0.012f, 0, 0.12f); /* lower at speed */
  if (k->st == SK_ROLL) {
    /* the wind up for a pop: knees to 100 degrees, the back foot onto the tail, the front foot back toward the middle */
    float c = k->crouch;
    w.knee += 1.0f * c, w.pitch += 0.45f * c, w.hz -= 0.06f * c, w.head_pitch += 0.15f * c;
    w.back_x += -0.085f * c, w.back_yaw += 0.12f * c, w.front_x -= 0.04f * c, w.front_yaw += 0.12f * c;
    w.arm_fwd += 0.4f * c, w.arm_back += 0.35f * c, w.arm_out -= 0.1f * c;
    /* a carve: lean into it, the knees over the toes or the hips back over the heels; the chest and head lead */
    float ln = s->lean;
    w.hz += 0.55f * cw_sin(ln) + (ln < 0 ? 0.06f * ln : 0), w.knee += 0.35f * fabsf(ln), w.chest -= 0.45f * ln, w.look -= 0.3f * ln;
    w.arm_out += 0.25f * fabsf(ln);
    /* a transition's load */
    w.knee += 0.35f * clampf(pump, 0, 1);
    /* riding up a wall: knees bent into it */
    if (k->u.y < 0.8f) w.knee += 0.25f, w.pitch += 0.1f;
    if (k->push_t > 0 && !k->manual) w.push_w = 1;
    if (k->manual) {
      float m = k->manual_kind ? -1.0f : 1.0f; /* 1: on the back wheels */
      w.manual = m;
      w.hx = -0.17f * m + 0.05f * k->mbal * m, w.support = -m, w.knee = 0.85f, w.pitch = 0.1f, w.open = 0.4f;
      if (m > 0) w.front_x = 0.15f, w.back_x = -0.25f, w.back_yaw = 0.2f;
      else w.front_x = 0.27f, w.front_yaw = 0.55f, w.back_x = -0.19f;
      w.arm_out = 0.75f, w.arm_fwd = -0.5f, w.arm_back = -0.3f, w.elbow = -0.45f;
    }
    /* the landing: the knees take it, deeper the harder it came down, then come back up */
    float lt = r->land_time, ab = r->impact * sstep(0, 0.07f, lt) * (1 - sstep(0.1f, 0.45f, lt));
    w.knee += 1.0f * ab, w.pitch += 0.3f * ab, w.arm_out += 0.4f * ab, w.hz -= 0.04f * ab;
  } else if (k->st == SK_AIR) {
    float at = r->air_time;
    w.air_w = 1;
    w.arm_out = 0.55f, w.arm_fwd = -0.2f, w.arm_back = 0.1f, w.elbow = -0.8f, w.pitch = 0.22f, w.head_pitch = 0.35f;
    /* the legs stretch for the landing as it falls (about a fifth of a second before it lands) */
    float ext = clampf((k->v.y + 3.4f) / 2.2f, 0, 1);
    if (k->vert) ext = 1;
    if (r->popped) {
      /* the pop: the body springs up out of the crouch while the front foot drags up the grip to the nose and the
         knees pull up to the chest; the board leads the rider up (its top comes before the rider's) */
      float drag = sstep(0.03f, 0.16f, at);
      w.tuck = (r->from_grind ? 0.14f : 0.24f) * sstep(0.03f, 0.2f, at) * ext;
      w.front_x = 0.13f + 0.13f * drag * ext + 0.04f * (1 - ext), w.front_yaw = 0.8f - 0.25f * drag * ext;
      w.back_x = -0.30f + 0.06f * (1 - ext), w.back_yaw = 0.25f;
      w.drop_air = 0.1f + 0.06f * (1 - ext);
      w.arm_out = 0.45f + 0.4f * sstep(0.0f, 0.15f, at), w.arm_fwd = -0.75f + 0.3f * sstep(0.05f, 0.25f, at);
      w.head_pitch = 0.45f; /* eyes on the board */
    } else {
      w.tuck = (k->vert ? 0.16f : 0.1f) * sstep(0.0f, 0.25f, at) * ext;
      w.drop_air = 0.12f;
    }
    if (k->grab >= 0) w.tuck = 0.32f, w.pitch = 0.45f, w.head_pitch = 0.5f, w.drop_air = 0.14f;
    if (flipping) {
      w.tuck = fmaxf(w.tuck, 0.14f), w.pitch = 0.24f, w.head_pitch = 0.55f;
      w.arm_out = 0.9f, w.elbow = -0.7f;
    }
    if (k->vert) w.head_pitch = 0.2f + 0.5f * sstep(0.0f, -3.0f, k->v.y); /* down to the coping on the way back */
  } else if (k->st == SK_GRIND) {
    w.grind_w = 1, w.knee = 0.95f, w.pitch = 0.22f, w.head_pitch = 0.3f;
    w.arm_out = 0.95f, w.arm_fwd = -0.35f, w.arm_back = -0.3f, w.elbow = -0.45f;
    w.back_x = -0.25f, w.back_yaw = 0.2f;
    if (k->grind_kind == 1) w.hx = 0.12f, w.support = 1, w.front_x = 0.2f, w.back_x = -0.2f;   /* the front truck */
    if (k->grind_kind == 2) w.hx = -0.14f, w.support = -1, w.front_x = 0.14f, w.back_x = -0.26f; /* the back truck */
    if (k->grind_kind == 3) w.front_x = 0.19f, w.back_x = -0.21f, w.open = 0.3f;               /* across the rail */
    /* the balance: the hips over the rail's side it tips to, the arms against it */
    if (k->grind_kind == 3) w.hx += 0.06f * k->bal; else w.hz += 0.06f * k->bal;
  } else if (k->st == SK_BAIL) {
    /* a bail: the body braces as it goes (the ragdoll starts from this pose): knees bent, the chin tucked, the arms in
       and forward, not thrown out straight */
    w.knee = 1.3f, w.pitch = 0.55f, w.head_pitch = 0.6f, w.arm_out = 0.35f, w.arm_fwd = -1.1f, w.arm_back = -1.0f, w.elbow = -1.2f;
  }
  if (w.push_w > 0) {
    /* the push: the front foot along the board over the front bolts, the body turned to the nose and over the front
       knee, which carries the hips' height (it bends to 80 degrees while the back foot is down) */
    w.front_x = 0.19f, w.front_yaw = 1.42f, w.open = 1.15f, w.chest = 0.12f, w.pitch = 0.45f, w.support = 1, w.hx = 0.07f, w.hz = 0.0f;
    float ph = s->push_phase - floorf(s->push_phase), down = sstep(0.0f, 0.15f, ph) * (1 - sstep(0.52f, 0.66f, ph));
    w.knee = 0.9f + 0.6f * down;
    w.look = 0.55f, w.head_pitch = 0.2f;
    w.arm_out = 0.2f, w.elbow = -0.45f;
  }

  /* ── ease toward it (a quick blend; the pop and the landing quicker) ── */
  float fast = k->st == SK_AIR && r->air_time < 0.15f ? 26.0f : 14.0f;
  s->knee = ease(s->knee, w.knee, r->land_time < 0.12f ? 30.0f : fast, dt);
  s->hx = ease(s->hx, w.hx, 10, dt), s->hz = ease(s->hz, w.hz, 12, dt);
  s->open = ease(s->open, w.open, 9, dt), s->pitch = ease(s->pitch, w.pitch, 12, dt), s->chest = ease(s->chest, w.chest, 9, dt);
  s->look = ease(s->look, w.look, 6, dt), s->head_pitch = ease(s->head_pitch, w.head_pitch, 8, dt);
  float fr = w.push_w > 0 || s->push_w > 0.5f ? 14.0f : 18.0f;
  s->front_x = ease(s->front_x, w.front_x, fr, dt), s->front_z = ease(s->front_z, w.front_z, fr, dt), s->front_yaw = ease(s->front_yaw, w.front_yaw, w.push_w > 0 ? 12.0f : 8.0f, dt);
  s->back_x = ease(s->back_x, w.back_x, fr, dt), s->back_z = ease(s->back_z, w.back_z, fr, dt), s->back_yaw = ease(s->back_yaw, w.back_yaw, fr, dt);
  s->arm_out = ease(s->arm_out, w.arm_out, 9, dt), s->arm_fwd = ease(s->arm_fwd, w.arm_fwd, 9, dt), s->arm_back = ease(s->arm_back, w.arm_back, 9, dt), s->elbow = ease(s->elbow, w.elbow, 9, dt);
  s->push_w = ease(s->push_w, w.push_w, w.push_w > 0 ? 10.0f : 7.0f, dt);
  s->grind_w = ease(s->grind_w, w.grind_w, 12, dt);
  s->manual = ease(s->manual, w.manual, 14, dt);
  s->tuck = ease(s->tuck, w.tuck, 18, dt);
  s->air_w = ease(s->air_w, w.air_w, 20, dt);
  s->drop_air = ease(s->drop_air, w.drop_air, 16, dt);
  s->support = ease(s->support, w.support, 10, dt);
  s->bal = ease(s->bal, k->st == SK_GRIND ? k->bal : k->manual ? k->mbal : 0, 18, dt);
  s->lean = ease(s->lean, lean, 6, dt);
  if (k->push_t > 0) s->push_phase = k->push_t / 0.55f;
  if (k->st == SK_AIR && k->grab >= 0) s->grab = k->grab;
  s->grab_w = ease(s->grab_w, k->st == SK_AIR && k->grab >= 0 ? 1.0f : 0.0f, k->grab >= 0 ? 22.0f : 12.0f, dt);
  s->flip_w = ease(s->flip_w, flipping ? 1.0f : 0.0f, 20, dt);
  if (k->st == SK_BAIL && r->air_time <= dt) /* the brace at once: the ragdoll takes this frame's pose */
    s->knee = w.knee, s->pitch = w.pitch, s->head_pitch = w.head_pitch, s->arm_out = w.arm_out, s->arm_fwd = w.arm_fwd, s->arm_back = w.arm_back, s->elbow = w.elbow;
  /* the ollie's board pitch: the tail snaps down (26 degrees nose up in 60 ms), the front foot levels it by 0.22 s */
  float bpitch = 0, tail = 0;
  if (k->st == SK_AIR && r->popped && !k->vert && !r->from_grind) {
    float at = r->air_time;
    bpitch = 0.45f * sstep(0.0f, 0.06f, at) * (1 - sstep(0.07f, 0.22f, at));
    tail = -fminf(k->p.y - k->takeoff_y, 0.2f) * (1 - sstep(0.03f, 0.09f, at)); /* the tail on the ground a few frames */
  }
  s->board_pitch = bpitch, s->tail_drop = tail;

  /* ── the board under the feet ── */
  t3_quat bq, dq;
  v3 bpos, dpos;
  board_frame(r, k, &bq, &bpos, &dq, &dpos);
  r->board_q = dq, r->board_p = dpos;
  r->deck_q = bq, r->deck_p = bpos;

  /* ── rest, then the body ── */
  for (int i = 0; i < p->count; i++) p->pos[i] = p->rest_pos[i], p->rot[i] = p->rest_rot[i];
  float bal = s->bal;
  /* the pelvis: open toward the nose, pitched forward with the crouch, rolled with a grind's balance */
  rot(r, r->hips, s->pitch * 0.5f, s->open, 0.1f * bal * s->grind_w);
  v3 hp = p->rest_pos[r->hips];
  p->pos[r->hips] = V(hp.x + s->hx, hp.y, hp.z + s->hz);
  /* the spine and chest: the rest of the forward lean, a little more turn, the balance's sway */
  rot(r, r->spine, s->pitch * 0.35f - 0.1f * s->manual * (s->manual > 0 ? 1.0f : 0.0f), 0.08f + 0.5f * s->chest, 0.18f * bal);
  rot(r, r->chest, s->pitch * 0.25f, 0.08f + 0.5f * s->chest - 0.1f * s->push_w, 0.1f * bal);
  /* the head: turned on round to look down the run (the body's turn so far taken off), eyes a few metres ahead */
  float body_turn = s->open + 0.16f + s->chest, head = s->look * 1.25f - body_turn * (s->look > 0 ? 1.0f : 0.6f);
  head = clampf(head, -1.6f, 1.3f);
  rot(r, r->neck, -0.3f * s->pitch + 0.4f * s->head_pitch, 0.4f * head, -0.05f * bal);
  rot(r, r->head, -0.3f * s->pitch + 0.6f * s->head_pitch, 0.6f * head, 0);
  /* the arms: loose and a little out, the front one toward the nose and the back one toward the tail; out wide in the
     air and on a rail; they swing against the push */
  float ph = s->push_phase - floorf(s->push_phase);
  float swing = s->push_w * 0.45f * cw_sin(ph * 2 * PI_F);
  float idle = 0.04f * cw_sin(t * 1.3f) * (1 - s->air_w) * (1 - s->grind_w);
  float seesaw = 0.55f * bal * (s->grind_w + 0.6f * fabsf(s->manual));
  rot(r, r->ua_l, s->arm_fwd + swing + idle, 0.15f, s->arm_out + seesaw + 0.1f);
  rot(r, r->la_l, s->elbow, 0, 0);
  rot(r, r->ua_r, s->arm_back - swing - idle, -0.15f, -(s->arm_out - seesaw) - 0.05f);
  rot(r, r->la_r, s->elbow + 0.05f, 0, 0);
  if (s->grab_w > 0.02f && (s->grab == 1 || s->grab == 6)) { /* the free arm up high */
    int ua = s->grab == 1 ? r->ua_r : r->ua_l;
    float sd = s->grab == 1 ? -1.0f : 1.0f;
    rot(r, ua, -1.0f * s->grab_w - 0.2f, 0, sd * (1.0f + 1.0f * s->grab_w));
  }
  cw_pose_update_model(p);

  /* ── the feet: where they stand on the deck (or the ground, pushing; or off it, flicking a flip) ── */
  v3 al, ar;
  t3_quat ql, qr;
  foot_on_deck(r, bq, bpos, s->front_x, s->front_z, s->front_yaw, 0, r->foot_rot_l, &al, &ql);
  foot_on_deck(r, bq, bpos, s->back_x, s->back_z, s->back_yaw, 0, r->foot_rot_r, &ar, &qr);
  r->free_l = r->free_r = 0;
  if (s->flip_w > 0.01f && k->flip >= 0) {
    /* the flick: the front foot off the nose (over the heel edge for a roll the kickflip way, the toe edge the other),
       the back foot up off the tail; they come back down to catch, the back one first */
    const trick_def *d = &FLIPS[k->flip];
    float e = flipping ? fe : 1;
    float up_f = sstep(0.0f, 0.18f, e) * (1 - sstep(0.7f, 0.92f, e)), up_b = sstep(0.0f, 0.12f, e) * (1 - sstep(0.6f, 0.82f, e));
    float side = d->roll > 0 ? -1.0f : d->roll < 0 ? 1.0f : 0.0f, hi = fabsf(d->pitch) > 0 ? 0.26f : 0.13f;
    v3 a2, b2;
    t3_quat q2, q3;
    foot_on_deck(r, bq, bpos, s->front_x + 0.12f * up_f, s->front_z + 0.13f * side * up_f, s->front_yaw + 0.3f * up_f, hi * up_f, r->foot_rot_l, &a2, &q2);
    foot_on_deck(r, bq, bpos, s->back_x - 0.02f * up_b, s->back_z, s->back_yaw, (hi - 0.02f) * up_b, r->foot_rot_r, &b2, &q3);
    float wf = s->flip_w;
    al = vlerp(al, a2, wf), ar = vlerp(ar, b2, wf), ql = t3_quat_slerp(ql, q2, wf), qr = t3_quat_slerp(qr, q3, wf);
    r->free_l = wf * up_f, r->free_r = wf * up_b;
  }
  if (s->push_w > 0.01f) {
    /* the back foot on the ground beside the deck on the toe side: it steps down level with the front foot, drives
       back past the tail, lifts and swings forward again; the toes along the run */
    float ground = -DECK_Y + r->ankle_h;
    v3 push;
    float lift;
    if (ph < 0.12f) {
      float u = ph / 0.12f;
      push = V(0.12f - 0.1f * u, 0, 0.23f), lift = 0.06f * (1 - u);
    } else if (ph < 0.58f) {
      float u = sstep(0.12f, 0.58f, ph);
      push = V(0.02f - 0.5f * u, 0, 0.23f), lift = 0;
    } else {
      float u = sstep(0.58f, 1.0f, ph);
      push = V(-0.48f + 0.6f * u, 0, 0.23f - 0.02f * cw_sin(u * PI_F)), lift = 0.14f * cw_sin(u * PI_F);
    }
    push.y = ground + lift;
    push.x -= FOOT_C; /* the ankle behind the sole's middle (the foot along +x) */
    /* coming back onto the tail (or leaving it): over the deck, not through it */
    float w = s->push_w;
    v3 back = vadd(vlerp(ar, push, w), V(0, 0.12f * cw_sin(w * PI_F) * (k->push_t > 0 ? 0.0f : 1.0f), 0));
    ar = back;
    t3_quat qp = qm(qm(qax(V(0, 1, 0), 1.45f), qax(V(1, 0, 0), ph > 0.4f && ph < 0.6f ? 0.5f * sstep(0.4f, 0.58f, ph) : 0)), r->foot_rot_r);
    qr = t3_quat_slerp(qr, qp, w);
  }
  if (getenv("SKATE_FEET_CONTROL")) { /* the foot placement check's control: the feet together in the deck's middle */
    foot_on_deck(r, bq, bpos, 0.04f, 0, 0.3f, 0, r->foot_rot_l, &al, &ql);
    foot_on_deck(r, bq, bpos, -0.04f, 0, 0.1f, 0, r->foot_rot_r, &ar, &qr);
  }

  /* ── the hips' height: from the knee bend wanted (the supporting leg's, or both), in reach of both feet; in the air
     the body keeps its height and the legs fold under it ── */
  {
    v3 jl = cw_pose_model_position(p, r->ul_l), jr = cw_pose_model_position(p, r->ul_r);
    float L = leg_at(r, s->knee);
    float hl = sqrtf((jl.x - al.x) * (jl.x - al.x) + (jl.z - al.z) * (jl.z - al.z)), hr = sqrtf((jr.x - ar.x) * (jr.x - ar.x) + (jr.z - ar.z) * (jr.z - ar.z));
    float yl = al.y + sqrtf(fmaxf(L * L - hl * hl, 0.01f)), yr = ar.y + sqrtf(fmaxf(L * L - hr * hr, 0.01f));
    float sup = clampf(s->support, -1, 1);
    float want_j = yl * (0.5f + 0.5f * sup) + yr * (0.5f - 0.5f * sup); /* the supporting leg's height, or both legs' mean */
    float d_ground = jl.y - want_j; /* both joints sit at one height near enough: the pelvis is level */
    float d = d_ground + (s->drop_air - d_ground) * s->air_w;
    /* never further than either leg reaches (the pushing foot on the ground too: the standing knee bends for it) */
    float reach = r->leg_len * 0.985f;
    float maxl = al.y + sqrtf(fmaxf(reach * reach - hl * hl, 0.01f)), maxr = ar.y + sqrtf(fmaxf(reach * reach - hr * hr, 0.01f));
    float lowest = fminf(maxl, maxr);
    if (jl.y - d > lowest) d = jl.y - lowest;
    p->pos[r->hips].y = hp.y - d + 0.004f * cw_sin(t * 2.3f) * (1 - s->air_w);
    cw_pose_update_model(p);
  }

  /* ── the legs: two-bone IK, the knees over the toes ── */
  cw_ik2_opts o = CW_IK2_OPTS_DEFAULT;
  o.use_pole = true;
  o.use_end_rotation = true;
  v3 up = qv(bq, V(0, 1, 0));
  v3 fwl = qv(ql, qv(qconj(r->foot_rot_l), V(0, 0, 1))), fwr = qv(qr, qv(qconj(r->foot_rot_r), V(0, 0, 1)));
  o.pole = vadd(vadd(al, vmul(fwl, 0.6f)), vmul(up, 0.45f));
  o.end_rotation = ql;
  cw_ik_two_bone(p, r->ul_l, r->ll_l, r->f_l, al, &o);
  o.pole = vadd(vadd(ar, vmul(fwr, 0.6f)), vmul(up, 0.45f));
  o.end_rotation = qr;
  cw_ik_two_bone(p, r->ul_r, r->ll_r, r->f_r, ar, &o);
  /* how far the feet are from where they belong (the deck, the ground for a push): a reach the legs cannot make */
  if (getenv("SKATE_POSELOG")) { /* development: the knees as posed */
    v3 hl = cw_pose_model_position(p, r->ul_l), kl = cw_pose_model_position(p, r->ll_l), fl = cw_pose_model_position(p, r->f_l);
    v3 hr = cw_pose_model_position(p, r->ul_r), kr = cw_pose_model_position(p, r->ll_r), fr2 = cw_pose_model_position(p, r->f_r);
    float kal = cw_acos(clampf(vdot(vnorm(vsub(kl, hl)), vnorm(vsub(fl, kl))), -1, 1)), kar = cw_acos(clampf(vdot(vnorm(vsub(kr, hr)), vnorm(vsub(fr2, kr))), -1, 1));
    fprintf(stderr, "pose land %.2f impact %.2f airw %.2f st %d knee want %.2f got %.2f %.2f hips y %.3f (rest %.3f) ankles %.3f %.3f %.3f / %.3f %.3f %.3f thigh %.3f shin %.3f ankle_h %.3f\n", r->land_time, r->impact, s->air_w, k->st, s->knee, kal, kar,
            p->pos[r->hips].y, r->hip_rest.y, al.x, al.y, al.z, ar.x, ar.y, ar.z, r->thigh, r->shin, r->ankle_h);
    v3 cf = qv(qm(cw_pose_model_rotation(p, r->chest), qconj(p->rest_rot[r->chest])), V(0, 0, 1)), hf = qv(cw_pose_model_rotation(p, r->head), V(0, 0, 1)), pf = qv(cw_pose_model_rotation(p, r->hips), V(0, 0, 1));
    fprintf(stderr, "  facing (deg toward the nose from the toe side): hips %.0f chest %.0f head %.0f; rest hips rot %.2f %.2f %.2f %.2f\n", cw_atan2(pf.x, pf.z) * 57.3f, cw_atan2(cf.x, cf.z) * 57.3f, cw_atan2(hf.x, hf.z) * 57.3f, p->rest_rot[r->hips].x, p->rest_rot[r->hips].y, p->rest_rot[r->hips].z, p->rest_rot[r->hips].w);
  }
  r->foot_err = fmaxf(vlen(vsub(cw_pose_model_position(p, r->f_l), al)), vlen(vsub(cw_pose_model_position(p, r->f_r), ar)));

  /* ── a grab's hands: on the board where the grab is, the board pulled up to them ── */
  if (s->grab_w > 0.02f && s->grab >= 0 && r->ua_l >= 0 && r->ua_r >= 0) {
    static const struct { float x, z; int hand; } G[] = {
      { 0.34f, 0.0f, 0 },   /* nose: the front hand */
      { 0.33f, 0.04f, 0 },  /* nose, the other arm up */
      { -0.34f, 0.0f, 1 },  /* tail: the back hand behind */
      { 0.03f, -0.11f, 0 }, /* the heel edge between the feet: the front hand */
      { -0.03f, 0.11f, 1 }, /* the toe edge between the feet: the back hand */
      { 0.05f, 0.11f, 0 },  /* the front hand across to the toe edge */
      { 0.0f, -0.11f, 1 },  /* the heel edge, the back hand round the back leg; the front arm up */
      { 0.06f, 0.11f, 2 },  /* both hands, the toe edge */
      { -0.33f, 0.03f, 1 }, /* the tail, kicked out */
    };
    int g = s->grab < 9 ? s->grab : 8;
    cw_ik2_opts a = CW_IK2_OPTS_DEFAULT;
    a.use_pole = true;
    a.weight = clampf(s->grab_w * 1.2f, 0, 1);
    float H = r->hip_rest.y;
    for (int hnd = 0; hnd < 2; hnd++) {
      if (!(G[g].hand == hnd || G[g].hand == 2)) continue;
      float x = G[g].x + (G[g].hand == 2 ? (hnd ? -0.1f : 0.1f) : 0);
      v3 hpt = vadd(bpos, qv(bq, V(x, deck_top(x) + 0.03f, G[g].z)));
      hpt.y -= DECK_Y;
      if (hnd == 0) a.pole = V(0.6f, H, 0.3f), cw_ik_two_bone(p, r->ua_l, r->la_l, r->h_l, hpt, &a);
      else a.pole = V(-0.6f, H, 0.3f), cw_ik_two_bone(p, r->ua_r, r->la_r, r->h_r, hpt, &a);
    }
  }
  if (r->goofy) rig_mirror(r);
}

/* where the feet stand, measured on the posed skeleton against the board as drawn: each sole's middle (the ankle plus
   the foot's own turn) in the board's frame, and how far it is outside where a skater's feet go (the front foot over
   the front truck's bolts to the nose's kick, the back foot over the back bolts to the tail, across the deck's
   middle, on its top). The feet off the deck on purpose (a flip's flick, the push's foot on the ground) are left out.
   *which: 1 the front foot, 2 the back. */
float rig_foot_place(const rig *r, int *which) {
  *which = 0;
  if (!r->bound) return 0;
  const cw_pose *p = &r->pose;
  float worst = 0;
  for (int f = 0; f < 2; f++) {
    if (f == 0 && r->free_l > 0.02f) continue;
    if (f == 1 && (r->free_r > 0.02f || r->s.push_w > 0.02f)) continue;
    bool left = (f == 0) != r->goofy; /* the front foot is the left one (goofy: the right) */
    int b = left ? r->f_l : r->f_r;
    t3_quat rest = left ? r->foot_rot_l : r->foot_rot_r, mr = cw_pose_model_rotation(p, b);
    t3_quat rel = qm(mr, qconj(rest));
    v3 ank = cw_pose_model_position(p, b);
    v3 sole = vsub(vadd(ank, vmul(qv(rel, V(0, 0, 1)), FOOT_C)), vmul(qv(rel, V(0, 1, 0)), r->ankle_h));
    sole.y += DECK_Y;
    v3 lb = qv(qconj(r->deck_q), vsub(sole, r->deck_p)); /* into the board's frame */
    float x0 = f ? -0.37f : 0.08f, x1 = f ? -0.13f : 0.32f;
    float out = fmaxf(fmaxf(x0 - lb.x, lb.x - x1), 0);
    out = fmaxf(out, fabsf(lb.z) - 0.08f);
    out = fmaxf(out, fabsf(lb.y - deck_top(lb.x)) - 0.03f);
    if (out > worst) worst = out, *which = f + 1;
  }
  return worst;
}
