/* pinball's bot: plays the table as a person would, from what it can see (the ball's position and speed).
 *   flippers  each flipper watches the balls over it. A good player knows what a flip will do: the bot plays the
 *             table forward from now (a copy of the physics, 30 ticks) with that flipper up, and flips when that
 *             sends the ball back up the table; a tick or two early or late at random is its aim. A ball rolling
 *             slowly onto a flipper is caught (held up: a cradle), then shot after a pause.
 *   plunger   pulled for a random time, then let go.
 *   nudge     now and then, when a ball runs toward an outlane, and never when the tilt meter is up.
 * It sees nothing a player could not and reads no rules state, so its scores say how the table plays. */
#include <math.h>

#include "cartwheel/math.h"
#include "game.h"

static uint32_t rnd(uint32_t *r) {
  *r = *r * 1664525u + 1013904223u;
  return *r >> 8;
}
static float rnd01(uint32_t *r) { return (float)(rnd(r) & 0xffff) / 65536.0f; }

typedef struct bot_state {
  int hold[3];     /* ticks a flipper stays up */
  int cradle[3];   /* ticks a caught ball has sat */
  int pull;        /* plunger ticks left to pull (-1 not pulling) */
  int nudge_cool;
} bot_state;
static bot_state B[4]; /* one per game a process runs (the soak runs one game at a time per slot) */

/* does flipping flipper k now send a ball up the table within half a second? */
static bool flip_works(const game *g, int k_flipper, const bool held_now[3], const int fi[3]) {
  static pb_world sim;
  sim = g->W;
  for (int k = 0; k < 3; k++)
    if (fi[k] >= 0) sim.fl[fi[k]].held = held_now[k] || fi[k] == k_flipper;
  for (int t = 0; t < 30; t++) {
    pb_step(&sim, TICK);
    for (int i = 0; i < PB_MAXB; i++) {
      const pb_ball *b = &sim.b[i];
      if (b->state == BS_PATH || (b->state == BS_FREE && b->y > 0.42f && b->vy > 0.3f)) return true;
    }
  }
  return false;
}

void bot_input(const game *g, input *in, uint32_t *rng) {
  bot_state *s = &B[0];
  const table *T = &g->T;
  *in = (input){ 0 };
  int fi[3] = { T->left_flipper, T->right_flipper, T->upper_flipper };
  bool want[3] = { false, false, false };
  for (int k = 0; k < 3; k++) {
    if (fi[k] < 0) continue;
    const pb_flipper *F = &g->W.fl[fi[k]];
    float side = F->up > F->rest ? 1.0f : -1.0f; /* +1 a left flipper (turns up counter-clockwise) */
    float c = cw_cos(F->rest), sn = cw_sin(F->rest);
    bool flip = false, catching = false;
    for (int i = 0; i < PB_MAXB; i++) {
      const pb_ball *b = &g->W.b[i];
      if (b->state != BS_FREE) continue;
      float dx = b->x - F->px, dy = b->y - F->py;
      float lx = c * dx + sn * dy, ly = (-sn * dx + c * dy) * side; /* along the flipper, above it */
      float vl = (c * b->vx + sn * b->vy), vy = (-sn * b->vx + c * b->vy) * side;
      if (lx < -0.005f || lx > F->len + 0.01f || ly < -0.01f || ly > 0.16f) continue;
      float gap = ly - (F->rb + PB_R);
      float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
      if (sp < 0.35f && gap < 0.012f && lx > 0.01f) { catching = true; continue; } /* rolling onto it slowly: catch it */
      if (vy >= -0.05f) continue;
      float ttc = gap / -vy;
      /* where it will meet the flipper: shoot from the middle to near the tip */
      float meet = lx + vl * ttc;
      if (ttc < 0.12f && meet > F->len * 0.1f && meet < F->len * 1.3f) flip = true;
    }
    if (flip && s->hold[k] <= 0) { /* would it work? (a flip that cannot reach the ball waits) */
      bool held_now[3] = { s->hold[0] > 0, s->hold[1] > 0, s->hold[2] > 0 };
      if (rnd01(rng) < 0.8f && flip_works(g, fi[k], held_now, fi)) s->hold[k] = 10 + (int)(rnd(rng) % 10);
    }
    if (catching && s->hold[k] <= 0) { /* hold it up a moment, then let it go and shoot */
      s->cradle[k]++;
      want[k] = s->cradle[k] < 40 + (int)(rnd(rng) % 30);
      if (!want[k]) s->cradle[k] = -14; /* drop, then flip as it rolls */
    } else if (s->cradle[k] < 0) {
      if (++s->cradle[k] == 0) s->hold[k] = 14;
    } else s->cradle[k] = 0;
    if (s->hold[k] > 0) want[k] = true, s->hold[k]--;
  }
  in->left = want[0], in->right = want[1], in->upper = want[2];
  /* the plunger */
  bool on_plunger = false;
  for (int i = 0; i < PB_MAXB; i++) {
    const pb_ball *b = &g->W.b[i];
    if (b->state == BS_FREE && b->x > 0.219f && b->y < 0.075f && fabsf(b->vy) < 0.3f) on_plunger = true;
  }
  if (on_plunger && s->pull < 0) s->pull = 25 + (int)(rnd(rng) % 55);
  if (s->pull > 0) in->launch = true, s->pull--;
  else if (s->pull == 0) s->pull = -1; /* let go: it fires */
  /* a nudge when a ball runs at an outlane */
  if (s->nudge_cool > 0) s->nudge_cool--;
  for (int i = 0; i < PB_MAXB && s->nudge_cool == 0; i++) {
    const pb_ball *b = &g->W.b[i];
    if (b->state != BS_FREE || g->tilt_meter > 0.6f || b->y > 0.42f || b->y < 0.3f || b->vy > 0) continue;
    if (b->x < -0.2f && rnd01(rng) < 0.25f) in->nudge_r = true, s->nudge_cool = 90;
    else if (b->x > 0.17f && b->x < 0.219f && rnd01(rng) < 0.25f) in->nudge_l = true, s->nudge_cool = 90;
    else s->nudge_cool = 20;
  }
}
