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

/* a smooth swell over `rise` seconds, then (1 + x) e^-x with x the time since the peak over `fall`: no step anywhere,
 * so nothing blinks */
float jw_fx_envelope(float t, float rise, float fall) {
  if (t <= 0) return 0;
  if (t < rise) {
    float k = t / rise;
    return k * k * (3 - 2 * k);
  }
  float x = (t - rise) / fall;
  return (1 + x) * expf(-x);
}

void jw_fx_init(jw_fx *f) {
  memset(f, 0, sizeof *f);
  f->sweep_t = -1;
  f->strobe_t = -1;
  f->room = f->room_target = 1;
  f->seed = 0x2545f491u;
}

/* each kind's light: its body colour, a little toward white (a light, not a paint) */
static void kind_light(int kind, float out[3]) {
  static const float white[3] = { 1, 1, 1 };
  const float *b = jw_cuts[kind < 0 ? 0 : kind > 5 ? 5 : kind].body;
  float m = fmaxf(b[0], fmaxf(b[1], b[2]));
  if (m <= 0) m = 1, b = white; /* the cuts are not built (the soak) */
  for (int i = 0; i < 3; i++) out[i] = 0.85f * b[i] / m + 0.15f;
}

static void add_light(jw_fx *f, jw_fx_light l) {
  int slot = f->nlights;
  if (slot >= JW_FX_LIGHTS) { /* full: the one with least left gives way */
    float least = 1e9f;
    slot = 0;
    for (int i = 0; i < JW_FX_LIGHTS; i++) {
      const jw_fx_light *o = &f->lights[i];
      float v = (o->r + o->g + o->b) * jw_fx_envelope(o->t - o->delay + 0.2f, o->rise, o->fall);
      if (v < least) least = v, slot = i;
    }
  } else f->nlights++;
  f->lights[slot] = l;
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

static void add_rays(jw_fx *f, float x, float y, float power, const float *c) {
  if (f->nrays >= JW_MAX_RAYS) memmove(f->rays, f->rays + 1, sizeof(jw_rays) * (JW_MAX_RAYS - 1)), f->nrays--;
  f->seed = f->seed * 1664525u + 1013904223u;
  f->rays[f->nrays++] = (jw_rays){ x, y, 0, power, c[0], c[1], c[2], f->seed };
}

void jw_fx_events(jw_fx *f, const jw_game *g, float dt) {
  static int no_light = -1, strobe = -1;
  if (no_light < 0) no_light = getenv("JEWELS_FX_NOLIGHT") != NULL; /* the effects test's control: no light from the clears */
  if (strobe < 0) strobe = getenv("JEWELS_STROBE") != NULL;        /* the photosensitivity gate's control */
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
      /* each stone's light: it gathers while the stone swells and peaks just after it breaks (the burst is five
       * ticks in), then fades; and where it stood, an afterglow on the velvet */
      float avg[3] = { 0 };
      int n = 0;
      for (int y = 0; y < JW_H; y++)
        for (int x = 0; x < JW_W; x++) {
          if (!g->clear.hit[y][x]) continue;
          float c[3];
          kind_light(g->clear_kind[y][x] - 1, c);
          for (int k = 0; k < 3; k++) avg[k] += c[k];
          n++;
          if (no_light) continue;
          float I = 1.5f * (0.7f + 0.3f * p);
          add_light(f, (jw_fx_light){ jw_cell_cx(x), jw_cell_cy(y), 95.0f, c[0] * I, c[1] * I, c[2] * I, 115.0f + 16.0f * p, 9.0f + 2.0f * p, 1, 0,
                                      0.10f, 0.12f + 0.015f * p, 0.02f });
          float A = 0.55f + 0.08f * p;
          add_light(f, (jw_fx_light){ jw_cell_cx(x), jw_cell_cy(y), 14.0f, c[0] * A, c[1] * A, c[2] * A, 62.0f, 0, 0, 0, 0.22f, 0.22f, 0.10f });
        }
      if (n) for (int k = 0; k < 3; k++) avg[k] /= (float)n;
      else avg[0] = avg[1] = avg[2] = 1;
      if (e->a >= 4 || step >= 2) {
        float c[3] = { 0.55f + 0.45f * avg[0], 0.5f + 0.4f * avg[1], 0.35f + 0.35f * avg[2] };
        if (f->nrings < JW_MAX_RINGS) f->rings[f->nrings++] = (jw_ring_draw){ e->x, e->y, 0, p, c[0] * 0.12f * p, c[1] * 0.12f * p, c[2] * 0.12f * p };
      }
      if (p >= 2.3f && !no_light) {
        float c[3] = { 0.6f + 0.4f * avg[0], 0.55f + 0.4f * avg[1], 0.45f + 0.4f * avg[2] };
        add_rays(f, e->x, e->y, p, c);
      }
      if (step >= 2 && f->last_step == step - 1 && f->ntrails < JW_MAX_TRAILS)
        f->trails[f->ntrails++] = (jw_trail){ f->last_cx, f->last_cy, e->x, e->y, 0, 1.0f * p * 0.5f, 0.8f * p * 0.5f, 0.45f * p * 0.5f };
      f->last_cx = e->x, f->last_cy = e->y, f->last_step = step;
      f->combo = step;
      if (step >= 2) f->combo_shown = step, f->combo_t = 0, f->combo_hold = 2.2f;
      f->warmth_target = step >= 2 ? fminf(1.0f, 0.25f * (float)(step - 1)) : f->warmth_target;
      f->room_target = 1.0f + 0.035f * fminf((float)(step - 1), 6.0f);
      add_pop(f, e->x, e->y, e->c, p, step);
      rumble(f, fminf(1.0f, 0.18f + 0.14f * p), 90 + (int)(40 * p));
      if (strobe && step >= 2) f->strobe_t = 0;
      break;
    }
    case JW_EV_BURST: {
      float p = jw_fx_power(e->c, e->b, e->d);
      if (f->nbursts >= JW_FX_BURSTS) memmove(f->bursts, f->bursts + 1, sizeof(jw_burst_draw) * (JW_FX_BURSTS - 1)), f->nbursts--;
      f->seed = f->seed * 1664525u + 1013904223u;
      f->bursts[f->nbursts++] = (jw_burst_draw){ e->a - 1, e->x, e->y, 0, p, f->seed };
      int sp = (int)(10 + 8 * p), sh = (int)(4 + 3 * p);
      f->sparks_last = sp > 50 ? 50 : sp, f->shards_last = sh > 18 ? 18 : sh;
      break;
    }
    case JW_EV_SETTLE:
      if (e->a >= 4) {
        /* the climax: a slow swell of warm light over the whole board, a fan of rays, a sweep of light and a shower
         * round the frame */
        f->sweep_t = 0;
        f->finale_t = 0, f->finale_steps = e->a, f->finale_gain = e->c;
        f->finales++;
        rumble(f, 1.0f, 600);
        if (!no_light) {
          float k = 1.0f + 0.15f * fminf((float)(e->a - 4), 4.0f);
          add_light(f, (jw_fx_light){ JW_BX + 5 * JW_CELL, JW_BY + 4 * JW_CELL, 520.0f, 1.0f * k, 0.80f * k, 0.55f * k, 820.0f, 70.0f, 1, 0, 0.32f, 0.45f, 0 });
          const float c[3] = { 1.0f, 0.86f, 0.62f };
          add_rays(f, JW_BX + 5 * JW_CELL, JW_BY + 4 * JW_CELL, 6.0f, c);
        }
      }
      f->warmth_target = 0;
      f->room_target = 1;
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
  for (int i = 0; i < f->nrays; i++) f->rays[i].t += dt;
  while (f->nrays && f->rays[0].t > 1.3f) memmove(f->rays, f->rays + 1, sizeof(jw_rays) * (size_t)--f->nrays);
  for (int i = 0; i < f->ntrails; i++) f->trails[i].t += dt;
  while (f->ntrails && f->trails[0].t > 0.9f) memmove(f->trails, f->trails + 1, sizeof(jw_trail) * (size_t)--f->ntrails);
  for (int i = 0; i < f->nlights;) {
    jw_fx_light *l = &f->lights[i];
    l->t += dt;
    /* gone when the fall has dropped it under 1% (x ~ 6.6) */
    if (l->t - l->delay > l->rise + 6.6f * l->fall) f->lights[i] = f->lights[--f->nlights];
    else i++;
  }
  for (int i = 0; i < f->npops; i++) f->pops[i].t += dt;
  while (f->npops && f->pops[0].t > 1.6f) memmove(f->pops, f->pops + 1, sizeof(jw_pop) * (size_t)--f->npops);
  f->warmth += (f->warmth_target - f->warmth) * fminf(1, dt * (f->warmth_target > f->warmth ? 2.5f : 0.8f));
  f->room += (f->room_target - f->room) * fminf(1, dt * (f->room_target > f->room ? 2.0f : 2.5f));
  if (f->sweep_t >= 0 && (f->sweep_t += dt / 1.3f) > 1) f->sweep_t = -1;
  if (f->finale_steps && (f->finale_t += dt) > 2.6f) f->finale_steps = 0;
  if (f->strobe_t >= 0 && (f->strobe_t += dt) > 1.5f) f->strobe_t = -1;
  if (f->combo_hold > 0) f->combo_hold -= dt;
  f->combo_t += dt;
  if (f->reshuffle > 0) f->reshuffle = fmaxf(0, f->reshuffle - dt * 0.9f);
  f->hint_t += dt;
  /* the climax's shower: sparks round the frame, a few a tick while the sweep runs */
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
  out->nrays = 0;
  for (int i = 0; i < f->nrays; i++) {
    out->rays[out->nrays] = f->rays[i];
    out->rays[out->nrays++].t += ahead;
  }
  out->ntrails = 0;
  for (int i = 0; i < f->ntrails; i++) {
    out->trails[out->ntrails] = f->trails[i];
    out->trails[out->ntrails++].t += ahead;
  }
  /* the lights of the moment, the strongest first when there are more than the frame holds */
  out->nlights = 0;
  float strength[JW_FX_LIGHTS];
  int order[JW_FX_LIGHTS], n = 0;
  for (int i = 0; i < f->nlights; i++) {
    const jw_fx_light *l = &f->lights[i];
    float k = jw_fx_envelope(l->t + ahead - l->delay, l->rise, l->fall);
    if (k < 0.004f) continue;
    strength[i] = k * (l->r + l->g + l->b);
    order[n++] = i;
  }
  for (int a = 1; a < n; a++) /* insertion sort, strongest first (n <= 64) */
    for (int b = a; b > 0 && strength[order[b]] > strength[order[b - 1]]; b--) { int t = order[b]; order[b] = order[b - 1], order[b - 1] = t; }
  for (int j = 0; j < n && out->nlights < JW_MAX_LIGHTS; j++) {
    const jw_fx_light *l = &f->lights[order[j]];
    float k = jw_fx_envelope(l->t + ahead - l->delay, l->rise, l->fall);
    out->lights[out->nlights++] = (jw_light){ l->x, l->y, l->z, l->r * k, l->g * k, l->b * k, l->reach, l->core, l->stones };
  }
  /* the climax's sweep: a band of light passing over the board, left to right */
  static int no_sweep = -1, no_room = -1; /* development: JEWELS_FX_OFF=sweep,room (cost and look experiments) */
  if (no_sweep < 0) { const char *o = getenv("JEWELS_FX_OFF"); no_sweep = o && strstr(o, "sweep"); no_room = o && strstr(o, "room"); }
  if (f->sweep_t >= 0 && out->nlights < JW_MAX_LIGHTS && !no_sweep) {
    float s = f->sweep_t, k = sinf(s * 3.14159f);
    k = k * k * 3.5f; /* a band of brilliance through the stones (they alone see it: the velvet stays dark) */
    out->lights[out->nlights++] = (jw_light){ JW_BX + s * 10 * JW_CELL, JW_BY + 4 * JW_CELL, 260.0f, k, k * 0.86f, k * 0.6f, 300.0f, 40.0f, 2 };
  }
  /* the photosensitivity gate's control: a light over the board switched on and off ten times a second */
  if (f->strobe_t >= 0 && out->nlights < JW_MAX_LIGHTS) {
    float k = fmodf(f->strobe_t + ahead, 0.1f) < 0.05f ? 14.0f : 0.0f;
    out->lights[out->nlights++] = (jw_light){ JW_BX + 5 * JW_CELL, JW_BY + 4 * JW_CELL, 300.0f, k, k, k, 900.0f, 60.0f, 1 };
  }
  out->sweep = f->sweep_t;
  out->warmth = f->warmth;
  out->room = no_room ? 1.0f : f->room;
  out->reshuffle = f->reshuffle;
}
