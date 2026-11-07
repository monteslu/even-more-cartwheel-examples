/* The effects (fx.h). */
#include "fx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cuts.h"

static float rumble_pending;
static int rumble_ms;

float jw_fx_power(int cells, int step, int shape) {
  if (getenv("JEWELS_FX_FLAT")) return 1.0f; /* the effects test's control: every clear the same */
  float p = 1.0f + 0.32f * (float)(cells - 3) + 0.30f * (float)shape + 0.45f * (float)(step - 1);
  return p > 6.0f ? 6.0f : p;
}

void jw_fx_init(jw_fx *f) {
  memset(f, 0, sizeof *f);
  f->sweep_t = -1;
  f->seed = 0x2545f491u;
}

static void add_flash(jw_fx *f, float x, float y, float power, const float *rgb) {
  int slot = f->nflash < 8 ? f->nflash++ : 0;
  if (f->nflash >= 8) { /* replace the weakest */
    float weakest = 1e9f;
    for (int i = 0; i < 8; i++) {
      float v = f->flash[i].power * (1 - f->flash[i].t);
      if (v < weakest) weakest = v, slot = i;
    }
  }
  f->flash[slot].x = x, f->flash[slot].y = y, f->flash[slot].t = 0, f->flash[slot].power = power;
  f->flash[slot].r = rgb[0], f->flash[slot].g = rgb[1], f->flash[slot].b = rgb[2];
}

static void rumble(jw_fx *f, float s, int ms) {
  if (s > rumble_pending) rumble_pending = s, rumble_ms = ms;
  f->rumbles++;
  f->rumble_last = s;
}

float jw_fx_take_rumble(jw_fx *f, int *ms) {
  (void)f;
  float s = rumble_pending;
  *ms = rumble_ms;
  rumble_pending = 0;
  return s;
}

static void add_pop(jw_fx *f, float x, float y, int64_t v, float power, int step) {
  if (f->npops >= JW_FX_POPS) memmove(f->pops, f->pops + 1, sizeof(jw_pop) * (JW_FX_POPS - 1)), f->npops--;
  f->pops[f->npops++] = (jw_pop){ x, y, 0, power, v, step };
}

void jw_fx_events(jw_fx *f, const jw_game *g, float dt) {
  f->time += dt;
  for (int i = 0; i < g->nev; i++) {
    const jw_event *e = &g->ev[i];
    switch (e->kind) {
    case JW_EV_CLEAR: {
      int step = e->b;
      float p = jw_fx_power(e->a, step, e->d);
      f->power_last = p;
      if (p > f->power_max) f->power_max = p;
      if (step >= 1 && step <= 8 && p > f->step_power[step - 1]) f->step_power[step - 1] = p;
      /* the clear's light: it lights the stones round it */
      const float warm[3] = { 1.0f, 0.86f, 0.62f };
      add_flash(f, e->x, e->y, 0.9f + 0.55f * p, warm);
      if (e->a >= 4 || step >= 2) {
        float c[3] = { 1.0f, 0.82f, 0.5f };
        if (f->nrings < JW_MAX_RINGS) f->rings[f->nrings++] = (jw_ring_draw){ e->x, e->y, 0, p, c[0] * 0.5f * p, c[1] * 0.5f * p, c[2] * 0.5f * p };
      }
      if (step >= 2 && f->last_step == step - 1 && f->ntrails < JW_MAX_TRAILS)
        f->trails[f->ntrails++] = (jw_trail){ f->last_cx, f->last_cy, e->x, e->y, 0, 1.0f * p * 0.5f, 0.8f * p * 0.5f, 0.45f * p * 0.5f };
      f->last_cx = e->x, f->last_cy = e->y, f->last_step = step;
      f->combo = step;
      if (step >= 2) f->combo_shown = step, f->combo_t = 0, f->combo_hold = 2.2f;
      f->warmth_target = step >= 2 ? fminf(1.0f, 0.25f * (float)(step - 1)) : f->warmth_target;
      add_pop(f, e->x, e->y, e->c, p, step);
      rumble(f, fminf(1.0f, 0.18f + 0.14f * p), 90 + (int)(40 * p));
      break;
    }
    case JW_EV_BURST: {
      float p = jw_fx_power(e->c, e->b, e->d);
      if (f->nbursts >= JW_FX_BURSTS) memmove(f->bursts, f->bursts + 1, sizeof(jw_burst_draw) * (JW_FX_BURSTS - 1)), f->nbursts--;
      f->seed = f->seed * 1664525u + 1013904223u;
      f->bursts[f->nbursts++] = (jw_burst_draw){ e->a - 1, e->x, e->y, 0, p, f->seed };
      int sp = (int)(14 + 16 * p), sh = (int)(4 + 3 * p);
      f->sparks_last = sp > 90 ? 90 : sp, f->shards_last = sh > 18 ? 18 : sh;
      break;
    }
    case JW_EV_SETTLE:
      if (e->a >= 4) {
        /* the finale: a sweep of light across the board and a shower round the frame */
        f->sweep_t = 0;
        f->finale_t = 0, f->finale_steps = e->a, f->finale_gain = e->c;
        f->finales++;
        rumble(f, 1.0f, 450);
      }
      f->warmth_target = 0;
      memset(f->step_power, 0, sizeof f->step_power);
      f->last_step = 0;
      break;
    case JW_EV_RESHUFFLE: f->reshuffle = 1; break;
    case JW_EV_HINT: f->hint_t = 0; break;
    case JW_EV_BUMP: rumble(f, 0.12f, 60); break;
    default: break;
    }
  }
  /* time moves on */
  for (int i = 0; i < f->nbursts; i++) f->bursts[i].t += dt;
  while (f->nbursts && f->bursts[0].t > 1.6f) memmove(f->bursts, f->bursts + 1, sizeof(jw_burst_draw) * (size_t)--f->nbursts);
  for (int i = 0; i < f->nrings; i++) f->rings[i].t += dt;
  while (f->nrings && f->rings[0].t > 1.0f) memmove(f->rings, f->rings + 1, sizeof(jw_ring_draw) * (size_t)--f->nrings);
  for (int i = 0; i < f->ntrails; i++) f->trails[i].t += dt;
  while (f->ntrails && f->trails[0].t > 0.9f) memmove(f->trails, f->trails + 1, sizeof(jw_trail) * (size_t)--f->ntrails);
  for (int i = 0; i < f->nflash; i++) f->flash[i].t += dt * 2.2f;
  for (int i = 0; i < f->nflash;)
    if (f->flash[i].t >= 1) f->flash[i] = f->flash[--f->nflash];
    else i++;
  for (int i = 0; i < f->npops; i++) f->pops[i].t += dt;
  while (f->npops && f->pops[0].t > 1.6f) memmove(f->pops, f->pops + 1, sizeof(jw_pop) * (size_t)--f->npops);
  f->warmth += (f->warmth_target - f->warmth) * fminf(1, dt * (f->warmth_target > f->warmth ? 4.0f : 0.8f));
  if (f->sweep_t >= 0 && (f->sweep_t += dt / 1.3f) > 1) f->sweep_t = -1;
  if (f->finale_steps && (f->finale_t += dt) > 3.0f) f->finale_steps = 0;
  if (f->combo_hold > 0) f->combo_hold -= dt;
  f->combo_t += dt;
  if (f->reshuffle > 0) f->reshuffle = fmaxf(0, f->reshuffle - dt * 0.9f);
  f->hint_t += dt;
  /* the finale's shower: sparks round the frame, a few a tick while the sweep runs */
  if (f->sweep_t >= 0 && f->nbursts < JW_FX_BURSTS - 2) {
    static const int kinds[] = { 5, 0, 1, 2, 3, 4 };
    f->seed = f->seed * 1664525u + 1013904223u;
    float u = (float)(f->seed >> 8) / 16777216.0f;
    float x = JW_BX + f->sweep_t * 10 * JW_CELL, y = JW_BY + u * 8 * JW_CELL;
    f->bursts[f->nbursts++] = (jw_burst_draw){ kinds[(f->seed >> 3) % 6], x, y, 0, 1.4f, f->seed };
  }
}

void jw_fx_frame(const jw_fx *f, jw_frame *out, float ahead) {
  out->nbursts = 0;
  for (int i = 0; i < f->nbursts && out->nbursts < JW_MAX_BURSTS; i++) {
    out->bursts[out->nbursts] = f->bursts[i];
    out->bursts[out->nbursts++].t += ahead;
  }
  out->nrings = 0;
  for (int i = 0; i < f->nrings; i++) {
    out->rings[out->nrings] = f->rings[i];
    out->rings[out->nrings++].t += ahead;
  }
  out->ntrails = 0;
  for (int i = 0; i < f->ntrails; i++) {
    out->trails[out->ntrails] = f->trails[i];
    out->trails[out->ntrails++].t += ahead;
  }
  /* the four strongest flashes light the scene */
  out->nflashes = 0;
  int used[8] = { 0 };
  for (int n = 0; n < JW_MAX_FLASHES; n++) {
    int best = -1;
    float bv = 0;
    for (int i = 0; i < f->nflash; i++) {
      float t = f->flash[i].t + ahead * 2.2f, v = f->flash[i].power * (1 - t) * (1 - t);
      if (!used[i] && v > bv) bv = v, best = i;
    }
    if (best < 0) break;
    used[best] = 1;
    float k = bv * 2.4f;
    out->flashes[out->nflashes++] = (jw_flash){ f->flash[best].x, f->flash[best].y, 70.0f, f->flash[best].r * k, f->flash[best].g * k, f->flash[best].b * k };
  }
  /* the finale's sweep: a band of light passing over the board, left to right */
  if (f->sweep_t >= 0 && out->nflashes < JW_MAX_FLASHES) {
    float s = f->sweep_t, k = sinf(s * 3.14159f) * 5.0f;
    out->flashes[out->nflashes++] = (jw_flash){ JW_BX + s * 10 * JW_CELL, JW_BY + 4 * JW_CELL, 220.0f, k, k * 0.86f, k * 0.6f };
  }
  out->sweep = f->sweep_t;
  out->warmth = f->warmth;
  out->reshuffle = f->reshuffle;
}
