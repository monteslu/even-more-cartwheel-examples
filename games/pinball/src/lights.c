/* pinball's lights in motion (lights.h). PINBALL_OLD_STROBE builds the lights as they were before the house rule was
 * applied (lamps blinking on and off at 5 Hz, flashers flickering at 15 Hz, hits lighting at once): the control the
 * flash checks run, which must fail. */
#include "lights.h"

#include <math.h>
#include <string.h>

void lights_reset(pb_lights *L, int nflash) {
  memset(L, 0, sizeof *L);
  L->nflash = nflash < NFLASH ? nflash : NFLASH;
  L->gi.v = 1;
  L->last_tick = -1;
}

void glow_step(glow *gl, float want, float up, float down) {
  if (gl->since < 30000) gl->since++;
  float d = want - gl->v;
  if (fabsf(d) < 1e-4f) { gl->v = want; return; }
  int8_t nd = d > 0 ? 1 : -1;
  if (nd != gl->dir) {
    if (gl->dir != 0 && gl->since < GLOW_TURN) return; /* it turned a moment ago: hold where it is */
    gl->dir = nd, gl->since = 0;
  }
  /* ease in: most of the way at the start, never faster than the light's own speed */
  float step = d * 0.35f;
  if (step > up) step = up;
  if (step < -down) step = -down;
  if (fabsf(step) < 0.004f) step = d > 0 ? fminf(d, 0.004f) : fmaxf(d, -0.004f);
  gl->v += step;
}

#ifdef PINBALL_OLD_STROBE
/* the lights before the rule (the control) */
void lights_step(pb_lights *L, const game *g) {
  const table *T = &g->T;
  const float dt = TICK;
  bool fresh = g->tick != L->last_tick;
  L->last_tick = g->tick;
  for (int i = 0; i < T->nlamp; i++) {
    float want = game_flash(g, i);
    glow *gl = &L->lamp[i];
    gl->v += (want - gl->v) * fminf(1, dt * (want > gl->v ? 40.0f : 14.0f));
  }
  for (int k = 0; fresh && k < g->nfx; k++) {
    int kind = g->fx[k].kind;
    if (kind == FX_POP)
      for (int p = 0; p < T->npop && p < NPOPL; p++) if (T->pops[p] == g->fx[k].a) L->pop[p].v = 1;
    float big = kind == FX_JACKPOT || kind == FX_MULTIBALL || kind == FX_MODE_DONE || kind == FX_EXTRA || kind == FX_SKILL || kind == FX_WIZARD ? 1.0f
              : kind == FX_MODE_START || kind == FX_MODE_SHOT || kind == FX_LOCK || kind == FX_RAMP_DONE || kind == FX_HOLE || kind == FX_KICKBACK || kind == FX_LANES ? 0.7f : 0;
    if (big > 0)
      for (int f = 0; f < L->nflash; f++)
        if (kind != FX_RAMP_DONE || f == (g->tick % L->nflash)) L->flash_want[f] = fmaxf(L->flash_want[f], big);
  }
  for (int p = 0; p < NPOPL; p++) L->pop[p].v = fmaxf(0, L->pop[p].v - dt * 6);
  L->frames++;
  for (int f = 0; f < L->nflash; f++) {
    L->flash_want[f] = fmaxf(0, L->flash_want[f] - dt * 3.5f);
    L->flash[f].v = L->flash_want[f] > 0 ? L->flash_want[f] * (0.6f + 0.4f * (float)((L->frames / 2) & 1)) : 0;
  }
  for (int i = 0; i < T->nel; i++) L->part[i].v = fminf(1, T->el[i].flash);
  L->gi.v = g->tilted ? 0.15f : g->state == GS_BONUS ? 0.6f : 1.0f;
}
#else
void lights_step(pb_lights *L, const game *g) {
  const table *T = &g->T;
  bool fresh = g->tick != L->last_tick; /* a paused game draws the same tick again: its events fire once */
  L->last_tick = g->tick;
  L->frames++;
  /* the inserts: what the rules want (a smooth wave for a pulsing lamp), eased like a bulb */
  for (int i = 0; i < T->nlamp; i++) glow_step(&L->lamp[i], game_flash(g, i), 0.12f, 0.07f);
  /* this tick's events */
  for (int k = 0; fresh && k < g->nfx; k++) {
    int kind = g->fx[k].kind, a = g->fx[k].a;
    if (kind == FX_POP) {
      for (int p = 0; p < T->npop && p < NPOPL; p++) if (T->pops[p] == a) L->pop_hold[p] = 8;
    }
    if ((kind == FX_POP || kind == FX_TARGET || kind == FX_DROP || kind == FX_TOY_HIT) && a >= 0 && a < T->nel) L->part_hold[a] = 8;
    float big = kind == FX_JACKPOT || kind == FX_MULTIBALL || kind == FX_MODE_DONE || kind == FX_EXTRA || kind == FX_SKILL || kind == FX_WIZARD ? 1.0f
              : kind == FX_MODE_START || kind == FX_MODE_SHOT || kind == FX_LOCK || kind == FX_HOLE || kind == FX_KICKBACK || kind == FX_LANES || kind == FX_TOY ? 0.7f
              : kind == FX_RAMP_DONE ? 0.5f : 0;
    if (big > 0)
      for (int f = 0; f < L->nflash; f++) {
        if (kind == FX_RAMP_DONE && f != (g->tick % L->nflash)) continue; /* a ramp: one flasher, not all */
        if (L->flash_cool[f] > 0) continue; /* fired a moment ago: it is still fading, never re-lit */
        L->flash_want[f] = fmaxf(L->flash_want[f], big), L->flash_hold[f] = 6, L->flash_cool[f] = GLOW_TURN * 2;
      }
    if ((kind == FX_MODE_START || kind == FX_WIZARD || kind == FX_MULTIBALL) && L->gi_cool == 0) L->gi_dip = 40, L->gi_cool = 240; /* a breath, at most every 4 s */
  }
  for (int p = 0; p < NPOPL; p++) {
    glow_step(&L->pop[p], L->pop_hold[p] > 0 ? 1.0f : 0.0f, 0.25f, 0.05f);
    if (L->pop_hold[p] > 0) L->pop_hold[p]--;
  }
  for (int i = 0; i < T->nel; i++) {
    glow_step(&L->part[i], L->part_hold[i] > 0 ? 1.0f : 0.0f, 0.25f, 0.05f);
    if (L->part_hold[i] > 0) L->part_hold[i]--;
  }
  /* flashers: up over four frames, a short hold, down over twenty */
  for (int f = 0; f < L->nflash; f++) {
    glow_step(&L->flash[f], L->flash_hold[f] > 0 ? L->flash_want[f] : 0.0f, 0.3f, 0.045f);
    if (L->flash_hold[f] > 0 && --L->flash_hold[f] == 0) L->flash_want[f] = 0;
    if (L->flash_cool[f] > 0) L->flash_cool[f]--;
  }
  /* the general illumination breathes: down for a drain or a tilt, a dip as a mode or multiball starts, back up over
   * half a second */
  float gi = g->tilted ? 0.2f : g->state == GS_BONUS ? 0.6f : L->gi_dip > 0 ? 0.55f : 1.0f;
  if (L->gi_dip > 0) L->gi_dip--;
  if (L->gi_cool > 0) L->gi_cool--;
  glow_step(&L->gi, gi, 0.03f, 0.03f);
}
#endif
