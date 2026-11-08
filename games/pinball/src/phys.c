/* pinball's ball solver (phys.h). Deterministic: + - * / sqrt and cartwheel/math.h only. */
#include "phys.h"

#include <math.h>
#include <string.h>

#include "cartwheel/math.h"

#define SKIN 0.00005f      /* a contact is found this close to the surface */
#define REST_SPEED 0.12f   /* slower approaches do not bounce (a ball resting on a wall or a flipper) */
#define MAX_ITER 6         /* contacts resolved in one substep */

void pb_init(pb_world *w) {
  memset(w, 0, sizeof *w);
  w->g = 1.25f;
  w->gz = 9.7f * 5.0f / 7.0f;
  w->kick_min = 0.35f;
  w->max_speed = 11.0f;
  for (int i = 0; i < PB_MAXB; i++) w->b[i].roll[3] = 1;
}

void pb_shape_bounds(pb_shape *s) {
  if (s->kind == SH_CIRCLE) {
    s->minx = s->ax - s->r, s->maxx = s->ax + s->r, s->miny = s->ay - s->r, s->maxy = s->ay + s->r;
  } else {
    s->minx = fminf(s->ax, s->bx) - s->r, s->maxx = fmaxf(s->ax, s->bx) + s->r;
    s->miny = fminf(s->ay, s->by) - s->r, s->maxy = fmaxf(s->ay, s->by) + s->r;
  }
}

int pb_add_seg(pb_world *w, float ax, float ay, float bx, float by, float r, float e, float mu, int flags, int tag) {
  if (w->nsh >= PB_MAXSH) return -1;
  pb_shape *s = &w->sh[w->nsh];
  *s = (pb_shape){ .kind = SH_SEG, .flags = (uint8_t)flags, .tag = (int16_t)tag, .path = -1, .ax = ax, .ay = ay, .bx = bx, .by = by, .r = r, .e = e, .mu = mu };
  pb_shape_bounds(s);
  return w->nsh++;
}

int pb_add_circle(pb_world *w, float x, float y, float r, float e, float mu, int flags, int tag) {
  if (w->nsh >= PB_MAXSH) return -1;
  pb_shape *s = &w->sh[w->nsh];
  *s = (pb_shape){ .kind = SH_CIRCLE, .flags = (uint8_t)flags, .tag = (int16_t)tag, .path = -1, .ax = x, .ay = y, .r = r, .e = e, .mu = mu, .hold_speed = 99 };
  pb_shape_bounds(s);
  return w->nsh++;
}

int pb_add_flipper(pb_world *w, float px, float py, float len, float rb, float rt, float rest, float up, int tag) {
  if (w->nfl >= PB_MAXFL) return -1;
  pb_flipper *f = &w->fl[w->nfl];
  /* the coil: full speed (52 rad/s, 3.9 m/s at the tip) in 9 ms and the whole 58 degree stroke in about 24 ms, so the
   * button to full stroke (half a tick waiting for the step on average, one frame on a 60 Hz screen) stays inside a
   * real machine's 30 to 50 ms; the return spring brings it back to rest in about 28 ms (tests/pinball-flippers.mjs) */
  *f = (pb_flipper){ .px = px, .py = py, .len = len, .rb = rb, .rt = rt, .rest = rest, .up = up, .ang = rest,
                     .wmax = 52.0f, .alpha_up = 5700.0f, .alpha_down = 3000.0f, .e = 0.55f, .mu = 0.35f, .on = true, .tag = (int16_t)tag };
  return w->nfl++;
}

int pb_add_path(pb_world *w, const float (*pts)[3], int n, float friction, int tag) {
  if (w->npath >= PB_MAXPATH || n < 2) return -1;
  pb_path *p = &w->path[w->npath];
  memset(p, 0, sizeof *p);
  p->n = n > PB_PATHPTS ? PB_PATHPTS : n;
  for (int i = 0; i < p->n; i++) {
    p->p[i][0] = pts[i][0], p->p[i][1] = pts[i][1], p->p[i][2] = pts[i][2];
    if (i) {
      float dx = p->p[i][0] - p->p[i - 1][0], dy = p->p[i][1] - p->p[i - 1][1], dz = p->p[i][2] - p->p[i - 1][2];
      p->cum[i] = p->cum[i - 1] + sqrtf(dx * dx + dy * dy + dz * dz);
    }
  }
  p->len = p->cum[p->n - 1];
  p->friction = friction;
  p->tag = (int16_t)tag;
  p->end_hole = -1;
  return w->npath++;
}

void pb_finish(pb_world *w) {
  for (int i = 0; i < w->nsh; i++) pb_shape_bounds(&w->sh[i]);
}

int pb_ball_add(pb_world *w, float x, float y, float vx, float vy) {
  for (int i = 0; i < PB_MAXB; i++) {
    pb_ball *b = &w->b[i];
    if (b->state != BS_OFF) continue;
    memset(b, 0, sizeof *b);
    b->state = BS_FREE;
    b->x = x, b->y = y, b->vx = vx, b->vy = vy;
    b->wx = b->pwx = x, b->wy = b->pwy = y, b->wz = b->pwz = 0;
    b->roll[3] = 1;
    b->held_tag = -1;
    return i;
  }
  return -1;
}

void pb_path_at(const pb_path *p, float s, float out[3], float tan[3]) {
  if (s < 0) s = 0;
  if (s > p->len) s = p->len;
  int i = 1;
  while (i < p->n - 1 && p->cum[i] < s) i++;
  float seg = p->cum[i] - p->cum[i - 1], t = seg > 1e-6f ? (s - p->cum[i - 1]) / seg : 0;
  for (int k = 0; k < 3; k++) out[k] = p->p[i - 1][k] + (p->p[i][k] - p->p[i - 1][k]) * t;
  float dx = p->p[i][0] - p->p[i - 1][0], dy = p->p[i][1] - p->p[i - 1][1], dz = p->p[i][2] - p->p[i - 1][2];
  float l = sqrtf(dx * dx + dy * dy + dz * dz);
  if (l < 1e-6f) l = 1;
  tan[0] = dx / l, tan[1] = dy / l, tan[2] = dz / l;
}

/* ── distances ───────────────────────────────────────────────────────── */

float pb_dist_shape(const pb_shape *s, float x, float y, float *nx, float *ny) {
  float cx, cy;
  if (s->kind == SH_CIRCLE) {
    cx = s->ax, cy = s->ay;
  } else {
    float ex = s->bx - s->ax, ey = s->by - s->ay, l2 = ex * ex + ey * ey;
    float t = l2 > 1e-12f ? ((x - s->ax) * ex + (y - s->ay) * ey) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    cx = s->ax + ex * t, cy = s->ay + ey * t;
  }
  float dx = x - cx, dy = y - cy, l = sqrtf(dx * dx + dy * dy);
  if (l > 1e-9f) *nx = dx / l, *ny = dy / l;
  else if (s->kind == SH_SEG) { /* on the wire's line: its left normal */
    float ex = s->bx - s->ax, ey = s->by - s->ay, el = sqrtf(ex * ex + ey * ey);
    *nx = el > 0 ? -ey / el : 0, *ny = el > 0 ? ex / el : 1;
  } else *nx = 0, *ny = 1;
  return l - s->r;
}

/* the distance to a flipper at angle ang: a capsule of two radii (base rb at the pivot, tip rt at len along its
 * axis), from Inigo Quilez's uneven capsule */
float pb_dist_flipper(const pb_flipper *f, float ang, float x, float y, float *nx, float *ny) {
  float c = cw_cos(ang), s = cw_sin(ang);
  float dx = x - f->px, dy = y - f->py;
  float lx = c * dx + s * dy, ly = -s * dx + c * dy; /* along the flipper, across it */
  float sg = ly < 0 ? -1.0f : 1.0f, py = ly * sg;
  float b = (f->rb - f->rt) / f->len, a = sqrtf(1 - b * b);
  float k = -b * py + a * lx, d, nlx, nly;
  if (k < 0) {
    float l = sqrtf(lx * lx + py * py);
    d = l - f->rb;
    nlx = l > 1e-9f ? lx / l : -1, nly = l > 1e-9f ? py / l : 0;
  } else if (k > a * f->len) {
    float qx = lx - f->len, l = sqrtf(qx * qx + py * py);
    d = l - f->rt;
    nlx = l > 1e-9f ? qx / l : 1, nly = l > 1e-9f ? py / l : 0;
  } else {
    d = py * a + lx * b - f->rb;
    nlx = b, nly = a;
  }
  nly *= sg;
  *nx = c * nlx - s * nly, *ny = s * nlx + c * nly;
  return d;
}

/* ── the step ────────────────────────────────────────────────────────── */

static void emit(pb_world *w, int kind, int ball, int tag, float speed, int dir) {
  if (w->nev >= PB_MAXEV) return;
  w->ev[w->nev++] = (pb_event){ (uint8_t)kind, (uint8_t)ball, (int16_t)tag, speed, (int8_t)dir };
}

static bool solid(const pb_shape *s) { return !(s->flags & (SF_SENSOR | SF_OFF)); }

/* one flipper's angle at substep time t (0..h): linear between the substep's two angles */
typedef struct flip_sub { float a0, a1, w; } flip_sub;

/* the time of the first contact of a ball moving from p (at t0) with velocity v, over [t0, t1], against one shape
 * (fl < 0) or a turning flipper (fl >= 0); -1 when none. Conservative advancement: the distance is exact and its rate
 * of change is bounded by L, so stepping by distance / L never steps past the surface. */
static float sweep(const pb_world *w, const pb_shape *s, int fl, const flip_sub *fs, float h, float px, float py, float vx, float vy, float t0, float t1) {
  float sp = sqrtf(vx * vx + vy * vy), L = sp, nx, ny;
  if (fl >= 0) {
    const pb_flipper *f = &w->fl[fl];
    float ddx = px - f->px, ddy = py - f->py;
    L += fabsf(fs[fl].w) * (sqrtf(ddx * ddx + ddy * ddy) + sp * (t1 - t0));
  }
  float t = t0;
  for (int it = 0; it < 64; it++) {
    float x = px + vx * (t - t0), y = py + vy * (t - t0), d;
    if (fl >= 0) {
      float a = fs[fl].a0 + (fs[fl].a1 - fs[fl].a0) * (h > 0 ? t / h : 1);
      d = pb_dist_flipper(&w->fl[fl], a, x, y, &nx, &ny);
    } else d = pb_dist_shape(s, x, y, &nx, &ny);
    d -= PB_R;
    if (d < SKIN) return t;
    if (L < 1e-7f) return -1;
    t += d / L;
    if (t > t1) return -1;
  }
  return -1;
}

/* is the shape's surface round where the ball touches it (a post, a wall's end)? A ball at rest there is balanced on a
 * point, which a real ball never stays on */
static bool convex_at(const pb_shape *s, float x, float y) {
  if (s->kind == SH_CIRCLE) return true;
  float ex = s->bx - s->ax, ey = s->by - s->ay, l2 = ex * ex + ey * ey;
  float t = l2 > 1e-12f ? ((x - s->ax) * ex + (y - s->ay) * ey) / l2 : 0;
  return t <= 0.0005f || t >= 0.9995f;
}

static bool one_way_blocks(const pb_shape *s, float vx, float vy) {
  /* a one-way segment stops only a ball moving against its normal (coming from its left) */
  float ex = s->bx - s->ax, ey = s->by - s->ay;
  return (-ey) * vx + ex * vy < 0;
}

static void ball_free_substep(pb_world *w, int bi, float h, const flip_sub *fs) {
  pb_ball *b = &w->b[bi];
  float x0 = b->x, y0 = b->y;
  /* the shapes near the sweep */
  /* a box round everywhere the ball can reach this substep, a bounce included (a kick or a flipper can turn it round
   * faster than it came) */
  float sp0 = sqrtf(b->vx * b->vx + b->vy * b->vy), reach = fmaxf(sp0, 9.0f) * h + PB_R + 0.002f;
  float minx = b->x - reach, maxx = b->x + reach, miny = b->y - reach, maxy = b->y + reach;
  static int near[PB_MAXSH];
  int nn = 0;
  for (int i = 0; i < w->nsh; i++) {
    const pb_shape *s = &w->sh[i];
    if (s->maxx < minx || s->minx > maxx || s->maxy < miny || s->miny > maxy) continue;
    near[nn++] = i;
  }
  bool flnear[PB_MAXFL];
  for (int f = 0; f < w->nfl; f++) {
    const pb_flipper *F = &w->fl[f];
    float rr = F->len + F->rb;
    flnear[f] = !(F->px + rr < minx || F->px - rr > maxx || F->py + rr < miny || F->py - rr > maxy);
  }
  /* contacts along the sweep, earliest first */
  float t = 0;
  uint64_t skip_fl = 0;
  static uint8_t skip[PB_MAXSH];
  for (int k = 0; k < nn; k++) skip[near[k]] = 0;
  for (int iter = 0; iter < MAX_ITER && t < h; iter++) {
    float best = 2 * h;
    int bs = -1, bf = -1;
    for (int k = 0; k < nn; k++) {
      int i = near[k];
      const pb_shape *s = &w->sh[i];
      if (!solid(s) || skip[i]) continue;
      if ((s->flags & SF_ONEWAY) && !one_way_blocks(s, b->vx, b->vy)) continue;
      float tc = sweep(w, s, -1, fs, h, b->x, b->y, b->vx, b->vy, t, h);
      if (tc >= 0 && tc < best) best = tc, bs = i, bf = -1;
    }
    for (int f = 0; f < w->nfl; f++) {
      if (!flnear[f] || (skip_fl >> f & 1)) continue;
      float tc = sweep(w, NULL, f, fs, h, b->x, b->y, b->vx, b->vy, t, h);
      if (tc >= 0 && tc < best) best = tc, bs = -1, bf = f;
    }
    if (bs < 0 && bf < 0) {
      b->x += b->vx * (h - t), b->y += b->vy * (h - t);
      t = h;
      break;
    }
    /* move to the contact and resolve it */
    b->x += b->vx * (best - t), b->y += b->vy * (best - t);
    t = best;
    float nx, ny, vsx = 0, vsy = 0, e, mu;
    if (bf >= 0) {
      const pb_flipper *F = &w->fl[bf];
      float a = fs[bf].a0 + (fs[bf].a1 - fs[bf].a0) * (h > 0 ? t / h : 1);
      pb_dist_flipper(F, a, b->x, b->y, &nx, &ny);
      float cx = b->x - nx * PB_R - F->px, cy = b->y - ny * PB_R - F->py;
      vsx = -fs[bf].w * cy, vsy = fs[bf].w * cx;
      e = F->e, mu = F->mu;
    } else {
      pb_dist_shape(&w->sh[bs], b->x, b->y, &nx, &ny);
      e = w->sh[bs].e, mu = w->sh[bs].mu;
    }
    float rvx = b->vx - vsx, rvy = b->vy - vsy, vn = rvx * nx + rvy * ny;
    if (vn > -1e-4f) { /* touching but not closing: let it slide on */
      if (bf >= 0) skip_fl |= 1ull << bf;
      else skip[bs] = 1;
      continue;
    }
    float approach = -vn;
    if (bf < 0 && (w->sh[bs].flags & SF_ONEWAY)) e *= 0.6f;
    if (approach < REST_SPEED) e = 0;
    else if (approach < 2 * REST_SPEED) e *= (approach - REST_SPEED) / REST_SPEED;
    float tx = rvx - vn * nx, ty = rvy - vn * ny, tl = sqrtf(tx * tx + ty * ty);
    float jn = (1 + e) * approach, df = fminf(tl, mu * jn);
    if (tl > 1e-9f) tx -= tx / tl * df, ty -= ty / tl * df;
    float nvn = e * approach;
    b->vx = tx + nx * nvn + vsx, b->vy = ty + ny * nvn + vsy;
    /* resting on top of a round thing (a post, the end of a wall): off it, to the side it leans */
    if (approach < REST_SPEED && ny > 0.85f && fabsf(b->vx - vsx) < 0.015f && (bf >= 0 || convex_at(&w->sh[bs], b->x, b->y)))
      b->vx += nx >= 0 ? 0.025f : -0.025f;
    w->contacts++;
    if (bf >= 0) {
      if (approach > 0.25f) emit(w, EV_FLIP, bi, w->fl[bf].tag, approach, 0);
    } else {
      const pb_shape *s = &w->sh[bs];
      if (s->kick > 0 && approach > (s->kick_min > 0 ? s->kick_min : w->kick_min)) {
        b->vx += nx * s->kick, b->vy += ny * s->kick;
        emit(w, EV_KICK, bi, s->tag, approach, 0);
      } else if ((s->flags & SF_REPORT) && approach > 0.05f) emit(w, EV_HIT, bi, s->tag, approach, 0);
      else if (approach > 0.4f) emit(w, EV_HIT, bi, -1, approach, 0); /* a plain wall: its sound */
    }
    b->x += nx * 1e-6f, b->y += ny * 1e-6f;
  }
  /* overlaps left (a contact budget used up, a gate closing on the ball): push out */
  for (int k = 0; k < nn; k++) {
    const pb_shape *s = &w->sh[near[k]];
    if (!solid(s)) continue;
    float nx, ny, d = pb_dist_shape(s, b->x, b->y, &nx, &ny) - PB_R;
    if (d >= 0) continue;
    if ((s->flags & SF_ONEWAY) && !one_way_blocks(s, b->vx, b->vy)) continue; /* going through a one-way gate */
    if (-d > w->max_pen) w->max_pen = -d;
    b->x += nx * -d, b->y += ny * -d;
    float vn = b->vx * nx + b->vy * ny;
    if (vn < 0) b->vx -= vn * nx, b->vy -= vn * ny;
  }
  for (int f = 0; f < w->nfl; f++) {
    if (!flnear[f]) continue;
    const pb_flipper *F = &w->fl[f];
    float nx, ny, d = pb_dist_flipper(F, fs[f].a1, b->x, b->y, &nx, &ny) - PB_R;
    if (d >= 0) continue;
    if (-d > w->max_pen) w->max_pen = -d;
    b->x += nx * -d, b->y += ny * -d;
    float cx = b->x - nx * PB_R - F->px, cy = b->y - ny * PB_R - F->py;
    float vsx = -fs[f].w * cy, vsy = fs[f].w * cx;
    float vn = (b->vx - vsx) * nx + (b->vy - vsy) * ny;
    if (vn < 0) b->vx -= vn * nx, b->vy -= vn * ny;
  }
  /* sensors the centre crossed this substep */
  for (int k = 0; k < nn && b->state == BS_FREE; k++) {
    const pb_shape *s = &w->sh[near[k]];
    if (!(s->flags & SF_SENSOR) || (s->flags & SF_OFF)) continue;
    if (s->kind == SH_SEG) {
      float ax = s->ax, ay = s->ay, sx = s->bx - ax, sy = s->by - ay;
      float s0 = sx * (y0 - ay) - sy * (x0 - ax), s1 = sx * (b->y - ay) - sy * (b->x - ax);
      if ((s0 > 0) == (s1 > 0)) continue;
      /* where the centre's path meets the line: inside the segment? */
      float mx = b->x - x0, my = b->y - y0, den = sx * my - sy * mx;
      if (fabsf(den) < 1e-12f) continue;
      float u = ((x0 - ax) * my - (y0 - ay) * mx) / den;
      if (u < 0 || u > 1) continue;
      int dir = s1 > 0 ? 1 : -1;
      float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
      if (s->flags & SF_DRAIN) {
        emit(w, EV_DRAIN, bi, s->tag, sp, dir);
        b->state = BS_OFF;
        return;
      }
      if ((s->flags & SF_RAMP) && dir > 0 && s->path >= 0 && s->path < w->npath) {
        const pb_path *P = &w->path[s->path];
        float p3[3], tn[3];
        pb_path_at(P, 0, p3, tn);
        float tl = sqrtf(tn[0] * tn[0] + tn[1] * tn[1]);
        float along = tl > 1e-6f ? (b->vx * tn[0] + b->vy * tn[1]) / tl : sp;
        if (along > 0.05f) {
          b->state = BS_PATH, b->path = s->path, b->s = 0, b->sv = along;
          emit(w, EV_RAMP_IN, bi, P->tag, along, 1);
          return;
        }
      }
      emit(w, EV_CROSS, bi, s->tag, sp, dir);
    } else {
      float dx = b->x - s->ax, dy = b->y - s->ay, d2 = dx * dx + dy * dy;
      float px = x0 - s->ax, py = y0 - s->ay, p2 = px * px + py * py, r2 = s->r * s->r;
      if (s->flags & SF_MAGNET) {
        if (d2 < r2 && d2 > 1e-8f) {
          float d = sqrtf(d2), k = s->kick * h * (1 - d / s->r);
          b->vx -= dx / d * k, b->vy -= dy / d * k;
          b->vx *= 1 - 3 * h, b->vy *= 1 - 3 * h;
        }
        continue;
      }
      if (d2 < r2 && p2 >= r2) {
        float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
        if (s->flags & SF_DRAIN) {
          emit(w, EV_DRAIN, bi, s->tag, sp, 1);
          b->state = BS_OFF;
          return;
        }
        if ((s->flags & SF_HOLE) && sp < s->hold_speed) {
          b->state = BS_HELD, b->held_tag = s->tag, b->held_ticks = 0;
          b->x = s->ax, b->y = s->ay, b->vx = b->vy = 0;
          emit(w, EV_HOLE, bi, s->tag, sp, 1);
          return;
        }
        emit(w, EV_CROSS, bi, s->tag, sp, 1);
      }
    }
  }
}

static void ball_path_substep(pb_world *w, int bi, float h) {
  pb_ball *b = &w->b[bi];
  const pb_path *P = &w->path[b->path];
  float p3[3], tn[3];
  pb_path_at(P, b->s, p3, tn);
  float a = -(w->g * tn[1] + w->gz * tn[2]);
  b->sv += a * h;
  float fr = P->friction * h;
  b->sv = b->sv > fr ? b->sv - fr : b->sv < -fr ? b->sv + fr : 0;
  b->s += b->sv * h;
  if (b->s >= P->len) { /* made it */
    pb_path_at(P, P->len, p3, tn);
    float tl = sqrtf(tn[0] * tn[0] + tn[1] * tn[1]);
    float sp = P->exit_speed > 0 ? P->exit_speed : b->sv;
    b->x = p3[0], b->y = p3[1], b->z = 0;
    b->vx = tl > 1e-6f ? tn[0] / tl * sp : 0, b->vy = tl > 1e-6f ? tn[1] / tl * sp : -sp;
    b->state = BS_FREE;
    emit(w, EV_RAMP_DONE, bi, P->tag, b->sv, 1);
    if (P->end_hole >= 0) {
      b->state = BS_HELD, b->held_tag = P->end_hole, b->held_ticks = 0, b->vx = b->vy = 0;
      emit(w, EV_HOLE, bi, P->end_hole, sp, 1);
    }
    return;
  }
  if (b->s <= 0 && b->sv <= 0) { /* rolled back out of the entrance */
    pb_path_at(P, 0, p3, tn);
    float tl = sqrtf(tn[0] * tn[0] + tn[1] * tn[1]);
    float sp = -b->sv;
    b->x = p3[0] - (tl > 1e-6f ? tn[0] / tl : 0) * 0.004f, b->y = p3[1] - (tl > 1e-6f ? tn[1] / tl : 1) * 0.004f, b->z = 0;
    b->vx = tl > 1e-6f ? -tn[0] / tl * sp : 0, b->vy = tl > 1e-6f ? -tn[1] / tl * sp : -sp;
    b->state = BS_FREE;
    emit(w, EV_RAMP_BACK, bi, P->tag, sp, -1);
    return;
  }
  pb_path_at(P, b->s, p3, tn);
  b->x = p3[0], b->y = p3[1], b->z = p3[2];
}

void pb_step(pb_world *w, float dt) {
  w->nev = 0;
  w->contacts = 0;
  w->max_pen = 0;
  float vmax = 0, wfl = 0;
  for (int i = 0; i < PB_MAXB; i++) {
    pb_ball *b = &w->b[i];
    if (b->state == BS_OFF) continue;
    b->pwx = b->wx, b->pwy = b->wy, b->pwz = b->wz;
    if (b->state == BS_FREE) {
      float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
      if (sp > w->max_speed) b->vx *= w->max_speed / sp, b->vy *= w->max_speed / sp, sp = w->max_speed;
      if (!(sp < 1e9f)) b->vx = b->vy = 0, sp = 0; /* never a NaN */
      vmax = fmaxf(vmax, sp + w->g * dt);
    } else if (b->state == BS_PATH) vmax = fmaxf(vmax, fabsf(b->sv) + 10 * dt);
  }
  for (int f = 0; f < w->nfl; f++) {
    pb_flipper *F = &w->fl[f];
    float target = F->held && F->on ? F->up : F->rest;
    if (F->ang != target || F->omega != 0) wfl = fmaxf(wfl, F->wmax);
    bool coil = F->held && F->on;
    if (coil != F->coil_was) F->coil_was = coil, F->coil_t = 0, F->reach_t = F->ang == target ? 0 : -1;
  }
  int n = (int)ceilf(fmaxf(vmax * dt / (0.5f * PB_R), wfl * dt / 0.02f));
  n = n < 4 ? 4 : n > 96 ? 96 : n;
  w->substeps = n;
  float h = dt / (float)n;
  flip_sub fs[PB_MAXFL];
  for (int sub = 0; sub < n; sub++) {
    /* the flippers turn: toward up while held (the coil), back to rest when let go (the spring) */
    for (int f = 0; f < w->nfl; f++) {
      pb_flipper *F = &w->fl[f];
      float a0 = F->ang, target = F->held && F->on ? F->up : F->rest;
      float sg = target > F->ang ? 1.0f : target < F->ang ? -1.0f : 0.0f;
      bool lifting = F->held && F->on;
      if (sg != 0) {
        F->omega += sg * (lifting ? F->alpha_up : F->alpha_down) * h;
        if (F->omega > F->wmax) F->omega = F->wmax;
        if (F->omega < -F->wmax) F->omega = -F->wmax;
        if (F->omega * sg < 0) F->omega = 0; /* reversing: the coil (or the spring) stops it first */
        F->ang += F->omega * h;
        if ((sg > 0 && F->ang >= target) || (sg < 0 && F->ang <= target)) F->ang = target, F->omega = 0;
      } else F->omega = 0;
      F->coil_t += h;
      if (F->reach_t < 0 && F->ang == target) F->reach_t = F->coil_t;
      fs[f] = (flip_sub){ a0, F->ang, (F->ang - a0) / h };
    }
    for (int i = 0; i < PB_MAXB; i++) {
      pb_ball *b = &w->b[i];
      if (b->state == BS_FREE) {
        b->vy -= w->g * h;
        ball_free_substep(w, i, h, fs);
      } else if (b->state == BS_PATH) ball_path_substep(w, i, h);
    }
    /* balls on balls (multiball) */
    for (int i = 0; i < PB_MAXB; i++) {
      pb_ball *a = &w->b[i];
      if (a->state != BS_FREE) continue;
      for (int j = i + 1; j < PB_MAXB; j++) {
        pb_ball *c = &w->b[j];
        if (c->state != BS_FREE) continue;
        float dx = a->x - c->x, dy = a->y - c->y, d2 = dx * dx + dy * dy;
        if (d2 >= 4 * PB_R * PB_R || d2 < 1e-12f) continue;
        float d = sqrtf(d2), nx = dx / d, ny = dy / d, pen = 2 * PB_R - d;
        a->x += nx * pen * 0.5f, a->y += ny * pen * 0.5f, c->x -= nx * pen * 0.5f, c->y -= ny * pen * 0.5f;
        float vn = (a->vx - c->vx) * nx + (a->vy - c->vy) * ny;
        if (vn < 0) {
          float j2 = -(1 + 0.9f) * vn * 0.5f;
          a->vx += nx * j2, a->vy += ny * j2, c->vx -= nx * j2, c->vy -= ny * j2;
          if (-vn > 0.3f) emit(w, EV_BALLS, i, j, -vn, 0);
        }
      }
    }
  }
  for (int i = 0; i < PB_MAXB; i++) {
    pb_ball *b = &w->b[i];
    if (b->state == BS_HELD) b->held_ticks++;
    if (b->state == BS_FREE) b->z = 0;
    if (b->state != BS_OFF) b->wx = b->x, b->wy = b->y, b->wz = b->state == BS_HELD ? -0.006f : b->z;
  }
}

float pb_flipper_angle(const pb_flipper *f) { return f->ang; }
