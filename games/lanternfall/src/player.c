/* lanternfall: the character's movement and its tile collision (sim.h). Simulation code: + - * / and sqrt only. */
#include <math.h>
#include <string.h>

#include "sim.h"

#define EPS 0.001f

/* tunables a test can turn off (tools/feel.c --control) */
int g_coyote = COYOTE, g_jbuf = JBUF;
float g_grav_cut = GRAV_CUT;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float toward(float x, float t, float s) { return x < t ? (x + s > t ? t : x + s) : (x - s < t ? t : x - s); }
static inline int fl(float v) { return (int)floorf(v); }

static inline int tile_at(const pworld *pw, int x, int y) { return w_in(x, y) ? pw->t[y][x] : T_SOLID; }
static inline bool solid_at(const pworld *pw, int x, int y) { return t_solid(tile_at(pw, x, y)); }

bool player_box_hits(const pworld *pw, float x, float y, int kind) {
  int x0 = fl(x - PW), x1 = fl(x + PW - EPS), y0 = fl(y - PH), y1 = fl(y - EPS);
  for (int ty = y0; ty <= y1; ty++)
    for (int tx = x0; tx <= x1; tx++)
      if (tile_at(pw, tx, ty) == kind) {
        if (kind == T_SPIKE && y < (float)ty + 0.45f) continue; /* thorns stand in the lower part of their tile */
        return true;
      }
  return false;
}

float player_water(const pworld *pw, float x, float y) {
  int tx = fl(x);
  float top = y - PH, wet = 0;
  for (int ty = fl(top); ty <= fl(y - EPS); ty++) {
    if (tile_at(pw, tx, ty) != T_WATER) continue;
    float a = fmaxf(top, (float)ty), b = fminf(y, (float)ty + 1);
    if (b > a) wet += b - a;
  }
  return wet / PH;
}

/* horizontal move with collision; returns the wall side hit (-1, 1) or 0 */
static int move_x(const pworld *pw, player *p, float dx) {
  if (dx == 0) return 0;
  float nx = p->x + dx;
  int y0 = fl(p->y - PH), y1 = fl(p->y - EPS);
  if (dx > 0) {
    int c = fl(nx + PW - EPS);
    for (int cx = fl(p->x + PW - EPS) + 1; cx <= c; cx++)
      for (int ty = y0; ty <= y1; ty++)
        if (solid_at(pw, cx, ty)) {
          p->x = (float)cx - PW;
          return 1;
        }
  } else {
    int c = fl(nx - PW);
    for (int cx = fl(p->x - PW) - 1; cx >= c; cx--)
      for (int ty = y0; ty <= y1; ty++)
        if (solid_at(pw, cx, ty)) {
          p->x = (float)(cx + 1) + PW;
          return -1;
        }
  }
  p->x = nx;
  return 0;
}

/* vertical move with collision; returns 1 landed, -1 bonked a ceiling, 0 free */
static int move_y(const pworld *pw, player *p, float dy, bool oneway_ok) {
  if (dy == 0) return 0;
  int x0 = fl(p->x - PW), x1 = fl(p->x + PW - EPS);
  float ny = p->y + dy;
  if (dy > 0) {
    int r0 = (int)ceilf(p->y - EPS), r1 = fl(ny - EPS);
    for (int r = r0; r <= r1; r++)
      for (int tx = x0; tx <= x1; tx++) {
        int t = tile_at(pw, tx, r);
        if (t_solid(t) || (t == T_ONEWAY && oneway_ok)) {
          p->y = (float)r;
          return 1;
        }
      }
  } else {
    /* rows whose bottom edge the head crosses: r + 1 in (nh, head] */
    float head = p->y - PH, nh = ny - PH;
    for (int r = fl(head + EPS) - 1; r >= fl(nh); r--) {
      bool hit = false;
      for (int tx = x0; tx <= x1; tx++)
        if (solid_at(pw, tx, r)) hit = true;
      if (hit) {
        p->y = (float)(r + 1) + PH;
        return -1;
      }
    }
  }
  p->y = ny;
  return 0;
}

static bool ground_below(const pworld *pw, const player *p, bool oneway_ok, bool *on_oneway) {
  float fy = p->y;
  if (fabsf(fy - roundf(fy)) > 0.002f) return false;
  int r = (int)roundf(fy), x0 = fl(p->x - PW), x1 = fl(p->x + PW - EPS);
  bool solid = false, ow = false;
  for (int tx = x0; tx <= x1; tx++) {
    int t = tile_at(pw, tx, r);
    if (t_solid(t)) solid = true;
    else if (t == T_ONEWAY) ow = true;
  }
  if (on_oneway) *on_oneway = ow && !solid;
  return solid || (ow && oneway_ok);
}

/* touching a wall on side s (-1 left, 1 right) with the upper (upper = true) or lower half of the body */
static bool wall_touch(const pworld *pw, const player *p, int s, bool upper) {
  float x = s > 0 ? p->x + PW + 0.05f : p->x - PW - 0.05f;
  float ya = upper ? p->y - PH + 0.05f : p->y - PH * 0.5f, yb = upper ? p->y - PH * 0.5f : p->y - 0.1f;
  for (int ty = fl(ya); ty <= fl(yb); ty++)
    if (solid_at(pw, fl(x), ty)) return true;
  return false;
}

static bool box_free(const pworld *pw, float x, float y) {
  for (int ty = fl(y - PH); ty <= fl(y - EPS); ty++)
    for (int tx = fl(x - PW); tx <= fl(x + PW - EPS); tx++)
      if (solid_at(pw, tx, ty)) return false;
  return true;
}

static bool los_clear(const pworld *pw, float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  int n = (int)(len * 4) + 1;
  for (int i = 1; i < n; i++) {
    float k = (float)i / (float)n;
    if (solid_at(pw, fl(x0 + dx * k), fl(y0 + dy * k))) return false;
  }
  return true;
}

void pworld_anchors(pworld *pw, const world *w, int room) {
  pw->nanchors = 0;
  if (room < 0) return;
  const struct room *r = &w->rooms[room];
  float x0 = (float)(r->x * CELL_W) - 16, x1 = (float)((r->x + r->w) * CELL_W) + 16;
  float y0 = (float)(r->y * CELL_H) - 10, y1 = (float)((r->y + r->h) * CELL_H) + 10;
  for (int i = 0; i < w->nents && pw->nanchors < 64; i++) {
    const ent *e = &w->ents[i];
    if (e->kind == EK_ANCHOR && e->x >= x0 && e->x <= x1 && e->y >= y0 && e->y <= y1) pw->anchors[pw->nanchors++] = e;
  }
}

void player_spawn(player *p, float x, float y) {
  int hp = p->hp_max ? p->hp_max : 5, en = p->en_max ? p->en_max : 3;
  bool act = p->active;
  memset(p, 0, sizeof *p);
  p->x = x, p->y = y, p->safe_x = x, p->safe_y = y;
  p->hp_max = (int16_t)hp, p->hp = (int16_t)hp, p->en_max = (int16_t)en, p->en = (int16_t)en;
  p->anchor = -1;
  p->active = act;
}

static void to_safe(player *p) {
  p->x = p->safe_x, p->y = p->safe_y, p->vx = p->vy = 0;
  p->state = PS_NORMAL;
  p->cold = 0;
  p->anchor = -1;
  p->dash_t = 0;
}

static void grapple_try(player *p, const pworld *pw) {
  float hx = p->x, hy = p->y - PH * 0.6f, best = GRAPPLE_REACH * GRAPPLE_REACH;
  int pick = -1;
  for (int i = 0; i < pw->nanchors; i++) {
    const ent *a = pw->anchors[i];
    float dx = a->x - hx, dy = a->y - hy, d2 = dx * dx + dy * dy;
    if (dy > -1.0f || d2 >= best) continue; /* only anchors above */
    if (!los_clear(pw, hx, hy, a->x, a->y + 0.6f)) continue;
    best = d2, pick = i;
  }
  if (pick < 0) return;
  p->anchor = (int16_t)pick;
  p->rope = clampf(sqrtf(best), 2.5f, GRAPPLE_REACH);
  p->state = PS_GRAPPLE;
  p->dash_t = 0;
  p->ev |= PE_GRAPPLE;
}

int player_step(player *p, const pinput *in, const pworld *pw) {
  p->ev = 0;
  if (p->state == PS_DEAD) return -1;
  uint32_t ab = pw->abilities;
  int broke = -1;
  int mx = in->mx, my = in->my;
  if (p->state == PS_HURT) {
    mx = my = 0;
    if (--p->hurt_t <= 0) p->state = PS_NORMAL;
  }
  if (p->wall_lock > 0) {
    p->wall_lock--;
    if (mx == p->lock_side) mx = 0;
  }
  if (p->drop_t > 0) p->drop_t--;
  if (p->dash_cd > 0) p->dash_cd--;
  bool on_ow = false;
  bool ground = p->state != PS_GRAPPLE && p->vy >= 0 && ground_below(pw, p, p->drop_t == 0, &on_ow);
  bool was_ground = p->on_ground;
  p->on_ground = ground;
  /* water */
  float wet = player_water(pw, p->x, p->y);
  bool was_wet = p->in_water;
  p->in_water = wet > 0.05f;
  p->deep = wet > 0.85f;
  if (p->in_water && !was_wet && p->vy > 4) p->ev |= PE_SPLASH;
  /* timers: coyote time and the buffered press */
  if (ground) {
    p->coyote = (int16_t)g_coyote;
    p->air_jumps = (ab & AB_DJUMP) ? 1 : 0;
    p->dash_ready = true;
    p->ground_t++;
    if (p->state == PS_CLING) p->state = PS_NORMAL;
  } else {
    /* (the coyote window counts down after the jump checks below) */
    p->ground_t = 0;
  }
  if (in->jump) p->jbuf = (int16_t)g_jbuf;
  else if (p->jbuf > 0) p->jbuf--;
  if (mx) p->facing_left = mx < 0;

  /* the firm ground thorns and the cold send you back to */
  if (ground && !on_ow && p->ground_t >= 4 && !p->in_water) {
    bool near_spike = false;
    for (int dx = -2; dx <= 2; dx++)
      for (int dy = -1; dy <= 0; dy++)
        if (tile_at(pw, fl(p->x) + dx, (int)roundf(p->y) + dy) == T_SPIKE) near_spike = true;
    if (!near_spike) p->safe_x = p->x, p->safe_y = p->y;
  }

  /* ── the grapple line ── */
  if (p->state == PS_GRAPPLE) {
    const ent *a = p->anchor >= 0 && p->anchor < pw->nanchors ? pw->anchors[p->anchor] : NULL;
    if (!a || in->grapple) {
      p->state = PS_NORMAL, p->anchor = -1;
      p->ev |= PE_RELEASE;
    } else if (in->jump) {
      p->state = PS_NORMAL, p->anchor = -1;
      p->vy -= 7;
      if (p->vy > -12) p->vy = -12;
      p->air_jumps = (ab & AB_DJUMP) ? 1 : 0;
      p->dash_ready = true;
      p->jump_rising = true;
      p->jbuf = 0;
      p->ev |= PE_RELEASE;
    } else {
      p->vy += GRAV_FALL * 0.85f * DT;
      p->vx += (float)mx * 30.0f * DT;
      p->vx *= 0.997f, p->vy *= 0.997f;
      if (my < 0) p->rope = fmaxf(2.5f, p->rope - 8 * DT);
      if (my > 0) p->rope = fminf(GRAPPLE_REACH, p->rope + 8 * DT);
      if (move_x(pw, p, p->vx * DT)) p->vx = 0;
      int r = move_y(pw, p, p->vy * DT, true);
      if (r) p->vy = 0;
      float hx = p->x - a->x, hy = p->y - PH * 0.6f - a->y, d = sqrtf(hx * hx + hy * hy);
      if (d > p->rope && d > 0) {
        float nx = hx / d, ny = hy / d, k = d - p->rope;
        if (move_x(pw, p, -nx * k)) p->vx = 0;
        if (move_y(pw, p, -ny * k, true)) p->vy = 0;
        float vr = p->vx * nx + p->vy * ny;
        if (vr > 0) p->vx -= nx * vr, p->vy -= ny * vr;
      }
      return -1;
    }
  }

  /* ── the plunge ── */
  if (p->state == PS_POUND) {
    p->vx = 0;
    if (p->pound_t > 0) {
      p->pound_t--;
      p->vy = 0;
      return -1;
    }
    p->vy = POUND_V;
    int r = move_y(pw, p, p->vy * DT, true);
    if (r == 1) {
      p->state = PS_NORMAL;
      p->vy = 0;
      p->ev |= PE_POUND_LAND | PE_LAND;
      p->land_v = POUND_V;
      int row = (int)roundf(p->y);
      for (int tx = fl(p->x - PW - 0.5f); tx <= fl(p->x + PW + 0.5f) && broke < 0; tx++)
        if (tile_at(pw, tx, row) == T_CRACKED) broke = row * TW + tx;
    }
    if (player_box_hits(pw, p->x, p->y, T_SPIKE)) to_safe(p), p->ev |= PE_HAZARD;
    return broke;
  }

  /* ── the dash ── */
  if (p->state == PS_DASH) {
    if (--p->dash_t <= 0) {
      p->state = PS_NORMAL;
      p->vx = (float)p->dash_dir * RUN;
    } else {
      p->vy = 0;
      p->vx = (float)p->dash_dir * DASH_V;
      if (move_x(pw, p, p->vx * DT)) p->state = PS_NORMAL, p->vx = 0;
      if (player_box_hits(pw, p->x, p->y, T_SPIKE)) to_safe(p), p->ev |= PE_HAZARD;
      return -1;
    }
  }

  /* ── swimming, and deep water without the charm ── */
  if (p->deep && (ab & AB_SWIM)) {
    if (p->state != PS_SWIM) p->state = PS_SWIM, p->air_jumps = (ab & AB_DJUMP) ? 1 : 0;
  } else if (p->state == PS_SWIM)
    p->state = PS_NORMAL;
  if (p->state == PS_SWIM) {
    p->vx = toward(p->vx, (float)mx * SWIM_V, 40 * DT);
    float ty = my ? (float)my * SWIM_V : -0.8f;
    p->vy = toward(p->vy, ty, 40 * DT);
    if (in->jump) p->vy = -9, p->ev |= PE_STROKE;
    if (move_x(pw, p, p->vx * DT)) p->vx = 0;
    if (move_y(pw, p, p->vy * DT, true)) p->vy = 0;
    /* surfacing with a stroke carries out of the water */
    if (player_water(pw, p->x, p->y) < 0.85f && p->vy < -5) {
      p->state = PS_NORMAL;
      p->vy = -JUMP_V * 0.85f;
      p->jump_rising = true;
    }
    p->dash_ready = true;
    p->cold = 0;
    return -1;
  }
  if (p->deep) {
    if (++p->cold >= 50) {
      to_safe(p);
      p->ev |= PE_COLD;
      return -1;
    }
  } else p->cold = 0;

  /* ── clinging and climbing ── */
  int wside = 0;
  if (!ground) {
    if (wall_touch(pw, p, 1, false) || wall_touch(pw, p, 1, true)) wside = 1;
    else if (wall_touch(pw, p, -1, false) || wall_touch(pw, p, -1, true)) wside = -1;
  }
  p->wall = (int8_t)wside;
  if ((ab & AB_CLIMB) && p->state == PS_NORMAL && wside && mx == wside && p->vy > -4 && !p->deep) {
    p->state = PS_CLING;
    p->ev |= PE_CLING;
  }
  if (p->state == PS_CLING) {
    if (!wside || ground || mx == -wside) p->state = PS_NORMAL;
    else if (in->jump) { /* off the wall */
      p->state = PS_NORMAL;
      p->vx = (float)-wside * WALLJ_VX;
      p->vy = -WALLJ_VY;
      p->wall_lock = WALL_LOCK;
      p->lock_side = (int8_t)wside;
      p->jump_rising = true;
      p->jbuf = 0;
      p->dash_ready = true;
      p->ev |= PE_WALLJ;
    } else {
      p->vx = 0;
      p->vy = my < 0 ? -CLIMB_V : my > 0 ? CLIMB_V : 0;
      bool upper = wall_touch(pw, p, wside, true);
      if (!upper && my < 0) { /* over the top */
        p->state = PS_NORMAL;
        p->vy = -10;
        p->vx = (float)wside * 5;
        p->ev |= PE_MANTLE;
      }
      if (p->state == PS_CLING) {
        int r = move_y(pw, p, p->vy * DT, true);
        if (r) p->vy = 0;
        p->facing_left = wside > 0 ? 0 : 1;
        return -1;
      }
    }
  }

  /* ── jumping ── */
  bool dropping = my > 0 && in->jump && ground && on_ow;
  if (dropping) {
    p->drop_t = 10;
    p->y += 0.05f;
    p->jbuf = 0;
    p->on_ground = ground = false;
    p->coyote = 0;
    p->ev |= PE_DROP;
  } else if ((in->jump || p->jbuf > 0) && (ground || p->coyote > 0) && p->state == PS_NORMAL) {
    p->vy = -JUMP_V;
    p->coyote = 0, p->jbuf = 0;
    p->jump_rising = true;
    p->on_ground = ground = false;
    p->ev |= PE_JUMP;
  } else if (in->jump && !ground && p->coyote == 0 && p->air_jumps > 0 && p->state == PS_NORMAL && !p->deep) {
    p->vy = -DJUMP_V;
    p->air_jumps--;
    p->jbuf = 0;
    p->jump_rising = true;
    p->ev |= PE_DJUMP;
  }

  if (!ground && p->coyote > 0) p->coyote--;

  /* ── the dash, the plunge, the grapple start ── */
  if (in->dash && p->state == PS_NORMAL) {
    if ((ab & AB_POUND) && !ground && my > 0) {
      p->state = PS_POUND;
      p->pound_t = POUND_STALL;
      p->vx = p->vy = 0;
      p->ev |= PE_POUND_START;
      return -1;
    }
    if ((ab & AB_DASH) && p->dash_cd == 0 && (ground || p->dash_ready)) {
      p->state = PS_DASH;
      p->dash_t = DASH_T;
      p->dash_dir = (int8_t)(mx ? mx : p->facing_left ? -1 : 1);
      p->dash_cd = DASH_CD + DASH_T;
      if (!ground) p->dash_ready = false;
      p->vy = 0;
      p->ev |= PE_DASH;
      p->vx = (float)p->dash_dir * DASH_V;
      if (move_x(pw, p, p->vx * DT)) p->state = PS_NORMAL, p->vx = 0;
      return -1;
    }
  }
  if (in->grapple && (ab & AB_GRAPPLE) && (p->state == PS_NORMAL || p->state == PS_CLING)) {
    grapple_try(p, pw);
    if (p->state == PS_GRAPPLE) return -1;
  }

  /* ── running ── */
  float target = (float)mx * RUN;
  if (p->in_water) target *= 0.75f;
  if (p->deep) target = (float)mx * 3.5f;
  float acc = ground ? (mx ? G_ACC : G_DEC) : (mx ? A_ACC : A_DEC);
  if (mx && p->vx * (float)mx < 0) acc = ground ? G_DEC : A_ACC; /* turning round */
  if (p->state == PS_HURT) acc = ground ? 30 : 8;
  p->vx = toward(p->vx, target, acc * DT);

  /* ── gravity: lighter while a held jump rises, heavier when it is let go, a hang at the top ── */
  if (!ground || p->vy < 0) {
    float g;
    if (p->vy < 0) g = (in->jump_held && p->jump_rising) ? GRAV_UP : g_grav_cut;
    else g = GRAV_FALL, p->jump_rising = false;
    if (in->jump_held && fabsf(p->vy) < APEX_V) g *= APEX_K;
    if (p->deep) g = 20;
    p->vy += g * DT;
    float mf = p->deep ? 3.0f : MAX_FALL;
    if (p->vy > mf) p->vy = mf;
  } else p->vy = 0;

  int hx = move_x(pw, p, p->vx * DT);
  if (hx) p->vx = 0;
  int r = move_y(pw, p, p->vy * DT, p->drop_t == 0);
  if (r == -1) { /* a bonk: round a ceiling corner when the head only clipped it */
    float vy0 = p->vy;
    p->vy = 0;
    p->jump_rising = false;
    for (int s = -1; s <= 1; s += 2) {
      player q = *p;
      q.x += (float)s * 0.3f;
      if (box_free(pw, q.x, q.y) && move_y(pw, &q, vy0 * DT, false) == 0) {
        p->x = q.x, p->y = q.y, p->vy = vy0;
        break;
      }
    }
  }
  if (r == 1) {
    if (!was_ground && p->vy > 2) p->ev |= PE_LAND, p->land_v = p->vy;
    p->vy = 0;
  }
  if (player_box_hits(pw, p->x, p->y, T_SPIKE)) {
    to_safe(p);
    p->ev |= PE_HAZARD;
  }
  return broke;
}
