/* skate pose: the rider's body from the board's state, every frame. No clips: a handful of parameters (how deep the
 * crouch, the tuck of a grab, the lean of a carve, the push's phase, the balance of a grind or a manual) are eased
 * toward what the board is doing (a quick blend, a tenth of a second), the spine, arms and head are turned from them,
 * and the legs are solved with two-bone IK so the feet stay planted on the deck wherever it is: in a crouch, a tuck, a
 * flip catching the board, a manual tipping it. A grab puts a hand on the board (arm IK) at the grab's place: the nose,
 * the tail, the toe or heel edge.
 *
 * The rider's model space: x along the board's nose (the left foot leads: regular stance), y up from the deck's top,
 * z toward the toe edge, where the rider faces. Presentation only. */
#include <string.h>

#include "view.h"

static t3_quat qeuler(float x, float y, float z) {
  float sx, cx, sy, cy, sz, cz;
  cw_sincos(x * 0.5f, &sx, &cx), cw_sincos(y * 0.5f, &sy, &cy), cw_sincos(z * 0.5f, &sz, &cz);
  /* XYZ order, as the generator writes its offsets */
  return (t3_quat){ sx * cy * cz + cx * sy * sz, cx * sy * cz - sx * cy * sz, cx * cy * sz + sx * sy * cz, cx * cy * cz - sx * sy * sz };
}
static t3_quat qm(t3_quat a, t3_quat b) {
  return (t3_quat){ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
/* turn a bone from its rest rotation by an offset (x forward swing / lean, y twist, z out) */
static void rot(rig *r, int b, float x, float y, float z) {
  if (b < 0) return;
  r->pose.rot[b] = qm(r->pose.rest_rot[b], qeuler(x, y, z));
}

bool rig_bind(rig *r, t3_object *model) {
  memset(r, 0, sizeof *r);
  if (!model || !cw_pose_bind(&r->pose, model)) return false;
  cw_pose *p = &r->pose;
  r->hips = cw_pose_find(p, "Hips"), r->spine = cw_pose_find(p, "Spine"), r->chest = cw_pose_find(p, "Chest"), r->neck = cw_pose_find(p, "Neck"), r->head = cw_pose_find(p, "Head");
  r->ua_l = cw_pose_find(p, "UpperArmL"), r->la_l = cw_pose_find(p, "LowerArmL"), r->h_l = cw_pose_find(p, "HandL");
  r->ua_r = cw_pose_find(p, "UpperArmR"), r->la_r = cw_pose_find(p, "LowerArmR"), r->h_r = cw_pose_find(p, "HandR");
  r->ul_l = cw_pose_find(p, "UpperLegL"), r->ll_l = cw_pose_find(p, "LowerLegL"), r->f_l = cw_pose_find(p, "FootL");
  r->ul_r = cw_pose_find(p, "UpperLegR"), r->ll_r = cw_pose_find(p, "LowerLegR"), r->f_r = cw_pose_find(p, "FootR");
  if (r->hips < 0 || r->ul_l < 0 || r->f_l < 0 || r->ul_r < 0 || r->f_r < 0) return false;
  cw_pose_update_model(p);
  r->foot_rest_l = cw_pose_model_position(p, r->f_l);
  r->foot_rest_r = cw_pose_model_position(p, r->f_r);
  r->hip_rest = cw_pose_model_position(p, r->hips);
  v3 a = cw_pose_model_position(p, r->ul_l), k = cw_pose_model_position(p, r->ll_l);
  r->leg_len = vlen(vsub(a, k)) + vlen(vsub(k, r->foot_rest_l));
  r->bound = true;
  return true;
}

float rig_board_lift(const rig *r) { return r->s.tuck * 0.30f; }

static float ease(float cur, float want, float rate, float dt) { return cur + (want - cur) * (1 - cw_exp(-rate * dt)); }
static v3 ease3(v3 cur, v3 want, float rate, float dt) { return vlerp(cur, want, 1 - cw_exp(-rate * dt)); }

/* where a grab puts the hands (model space at the deck's top, before the board's lift) */
static void grab_hands(int g, v3 *l, v3 *r, bool *use_l, bool *use_r) {
  *use_l = *use_r = false;
  switch (g) {
  case 0: *l = V(0.33f, 0.02f, 0.0f), *use_l = true; break;            /* nose */
  case 1: *l = V(0.35f, 0.04f, 0.02f), *use_l = true; break;           /* nose, the other arm up (below) */
  case 2: *r = V(-0.33f, 0.02f, 0.0f), *use_r = true; break;           /* tail */
  case 3: *l = V(0.02f, 0.0f, -0.12f), *use_l = true; break;           /* heel edge, behind */
  case 4: *r = V(-0.02f, 0.0f, 0.12f), *use_r = true; break;           /* toe edge */
  case 5: *l = V(-0.04f, 0.0f, 0.12f), *use_l = true; break;           /* across to the toe edge */
  case 6: *r = V(0.02f, 0.0f, -0.12f), *use_r = true; break;           /* heel edge, the other arm up */
  case 7: *l = V(0.08f, 0.0f, 0.12f), *r = V(-0.08f, 0.0f, 0.12f), *use_l = *use_r = true; break; /* both, toe edge */
  default: *r = V(-0.34f, 0.03f, 0.03f), *use_r = true; break;         /* tail, kicked out */
  }
}

void rig_pose(rig *r, const skater *k, float dt, float t) {
  if (!r->bound) return;
  cw_pose *p = &r->pose;
  rider_pose *s = &r->s;
  /* what the board is doing */
  float crouch = 0.18f, tuck = 0, lean = 0, push_w = 0, grind_w = 0, flip_w = 0, look = k->fakie ? -1.0f : 1.0f;
  int grab = -1;
  float sp = vlen(k->v);
  if (k->st == SK_ROLL) {
    crouch = 0.15f + 0.6f * k->crouch + clampf(sp * 0.012f, 0, 0.1f);
    if (k->push_t > 0) push_w = 1;
    if (k->u.y < 0.8f) crouch += 0.2f; /* riding a wall: knees bent into it */
  } else if (k->st == SK_AIR) {
    crouch = 0.45f;
    if (k->flip >= 0 && k->flip_t < FLIPS[k->flip].len) flip_w = 1, crouch = 0.6f;
    if (k->grab >= 0) tuck = 1, grab = k->grab, crouch = 0.75f;
  } else if (k->st == SK_GRIND) {
    crouch = 0.4f, grind_w = 1;
  } else if (k->st == SK_BAIL) {
    crouch = 0.3f;
  }
  if (k->manual) crouch = 0.25f;
  /* a carve leans the body into the turn: the heading's turn rate times the speed (the lean a turn needs, v w / g) */
  float yawv = cw_atan2(-k->f.x, -k->f.z);
  if (k->st == SK_ROLL && sp > 1 && dt > 0) {
    float dy = wrap_pi(yawv - r->last_yaw) / dt;
    lean = clampf(-dy * sp / GRAV, -0.5f, 0.5f) * (k->fakie ? -1.0f : 1.0f);
  }
  r->last_yaw = yawv;
  float rate = 16;
  s->crouch = ease(s->crouch, crouch, rate, dt);
  s->tuck = ease(s->tuck, tuck, grab >= 0 ? 22 : 12, dt);
  s->push_w = ease(s->push_w, push_w, 8, dt);
  s->grind_w = ease(s->grind_w, grind_w, 12, dt);
  s->flip_w = ease(s->flip_w, flip_w, 20, dt);
  s->look = ease(s->look, look, 6, dt);
  s->bal = ease(s->bal, k->st == SK_GRIND ? k->bal : k->manual ? k->mbal : 0, 18, dt);
  s->manual = ease(s->manual, k->manual ? (k->manual_kind ? -1.0f : 1.0f) : 0, 14, dt);
  s->lean = ease(s->lean, lean, 6, dt);
  if (grab >= 0) s->grab = grab;
  s->grab_w = s->tuck;
  if (push_w > 0) s->push_phase = k->push_t / 0.55f;

  /* rest, then the body */
  for (int i = 0; i < p->count; i++) p->pos[i] = p->rest_pos[i], p->rot[i] = p->rest_rot[i];
  float drop = 0.05f + 0.30f * s->crouch + 0.08f * s->tuck + 0.13f * s->push_w; /* the standing knee bends to let the other foot reach the ground */
  float bob = 0.012f * cw_sin(t * 3.1f) * (1 - s->tuck);
  v3 hp = p->rest_pos[r->hips];
  /* model space: the hips over the deck's middle, a little toward the face; back for a wheelie, forward for a nose one */
  float hx = -0.07f * s->manual;
  p->pos[r->hips] = V(hp.x + hx, hp.y - drop + bob, hp.z + 0.02f);
  /* the pelvis turns toward the nose (more while pushing: the body faces the way it goes) */
  float yaw = 0.25f + 0.65f * s->push_w;
  rot(r, r->hips, 0.05f + 0.12f * s->crouch + 0.5f * s->lean, yaw, 0.18f * s->bal * s->grind_w);
  rot(r, r->spine, 0.12f + 0.25f * s->crouch + 0.2f * s->tuck - 0.12f * s->manual, 0.15f * s->look, 0.25f * s->bal);
  rot(r, r->chest, 0.06f + 0.1f * s->crouch, 0.2f * s->look - 0.1f * s->push_w, 0.1f * s->bal);
  rot(r, r->neck, -0.05f - 0.1f * s->crouch, 0.35f * s->look - 0.2f * yaw, 0);
  rot(r, r->head, -0.1f - 0.12f * s->crouch, 0.55f * s->look - 0.25f * yaw, 0);
  /* arms: out for balance, wider on a rail; up and back in the air */
  float out = 0.45f + 0.5f * s->grind_w + 0.25f * fabsf(s->bal);
  float sw = 0.25f * cw_sin(t * 1.7f) * (1 - s->grind_w) * (1 - s->tuck) * 0.3f;
  rot(r, r->ua_l, -0.25f + sw - 0.3f * s->flip_w, 0.2f, out + 0.6f * s->flip_w + 0.6f * s->bal * s->grind_w);
  rot(r, r->la_l, -0.55f - 0.3f * s->flip_w, 0, 0);
  rot(r, r->ua_r, -0.1f - sw - 0.2f * s->flip_w, -0.2f, -(out + 0.6f * s->flip_w) + 0.6f * s->bal * s->grind_w);
  rot(r, r->la_r, -0.5f - 0.3f * s->flip_w, 0, 0);
  if (s->grab == 1 || s->grab == 6) { /* the free arm up high */
    int ua = s->grab == 1 ? r->ua_r : r->ua_l;
    float sd = s->grab == 1 ? -1.0f : 1.0f;
    rot(r, ua, -1.2f * s->tuck - 0.1f, 0, sd * (out + 1.4f * s->tuck));
  }
  cw_pose_update_model(p);

  /* the feet on the deck: where the board is in model space (lifted by a grab's tuck, popped by a flip) */
  float lift = rig_board_lift(r);
  if (k->st == SK_AIR && k->flip >= 0 && k->flip_t < FLIPS[k->flip].len) {
    float e = (float)k->flip_t / (float)FLIPS[k->flip].len;
    lift += 0.18f * cw_sin(e * PI_F) + 0.06f;
  }
  float ay = r->foot_rest_l.y;
  v3 fl = V(0.19f, ay + lift, -0.04f), fr = V(-0.17f, ay + lift, -0.04f);
  if (k->manual) { /* the board tips: the raised end's foot rides up */
    if (k->manual_kind) fr.y += 0.07f; else fl.y += 0.07f;
  }
  if (s->push_w > 0.01f) { /* the back foot pushes on the ground beside the deck */
    float ph = s->push_phase - floorf(s->push_phase);
    float ground = -DECK_Y + ay;
    v3 push;
    if (ph < 0.55f) {
      float u = ph / 0.55f;
      push = V(0.0f - 0.42f * u, ground + 0.01f, 0.16f);
    } else {
      float u = (ph - 0.55f) / 0.45f;
      push = V(-0.42f + 0.42f * u, ground + 0.12f * cw_sin(u * PI_F), 0.16f);
    }
    fr = vlerp(fr, push, s->push_w);
  }
  float H = r->hip_rest.y;
  cw_ik2_opts o = CW_IK2_OPTS_DEFAULT;
  o.use_pole = true;
  o.use_end_rotation = true;
  o.pole = V(0.45f, H * 0.5f, 0.9f);
  o.end_rotation = qeuler(0, 0.45f + 0.9f * s->push_w, 0);
  cw_ik_two_bone(p, r->ul_l, r->ll_l, r->f_l, fl, &o);
  o.pole = V(-0.35f, H * 0.5f, 0.9f);
  o.end_rotation = qeuler(0, -0.05f, 0);
  cw_ik_two_bone(p, r->ul_r, r->ll_r, r->f_r, fr, &o);
  /* how far the feet are from where they belong (the deck, the ground for a push): a reach the legs cannot make */
  r->foot_err = fmaxf(vlen(vsub(cw_pose_model_position(p, r->f_l), fl)), vlen(vsub(cw_pose_model_position(p, r->f_r), fr)));

  /* a grab's hands */
  if (s->tuck > 0.02f && s->grab >= 0 && r->ua_l >= 0 && r->ua_r >= 0) {
    v3 hl, hr;
    bool ul, ur;
    grab_hands(s->grab, &hl, &hr, &ul, &ur);
    cw_ik2_opts a = CW_IK2_OPTS_DEFAULT;
    a.use_pole = true;
    a.weight = clampf(s->tuck * 1.2f, 0, 1);
    hl.y += lift, hr.y += lift;
    if (ul) a.pole = V(0.6f, H, 0.2f), cw_ik_two_bone(p, r->ua_l, r->la_l, r->h_l, hl, &a);
    if (ur) a.pole = V(-0.6f, H, 0.2f), cw_ik_two_bone(p, r->ua_r, r->la_r, r->h_r, hr, &a);
  }
}
