/* destructo's water: the lake behind a dam, and what spills when the dam breaks.
 *
 * The lake is a level surface (the view draws it) at height `level`. It holds back nothing in the physics: what keeps it in
 * is the dam, and what the dam is made of is read off the blocks. The dam's face is cut into bins (BIN metres square, across
 * x and up y); each bin lists the dam blocks that cover it. A bin is CLOSED while any block of its list still stands where it
 * was built, and OPEN once every one is broken or has moved: then water runs through it, below the lake's surface, at the
 * rate of an orifice (Torricelli: Cd x area x sqrt(2 g h), h the water over the bin's middle).
 *
 * The water that runs is PARCELS: small spheres, each standing for WATER_VP cubic metres, made on the lake side of the open
 * bin, thrown through it at 0.85 sqrt(2 g h). They are bodies in the world (so they fall down the dam's foot, bounce off
 * the rubble and run into each other) on a layer of their own that the shot passes through and the blasts do not see,
 * with no bounce, little friction and damping (water loses its energy in the fall), and they sleep when they stop (a pool
 * costs nothing). At most MAXWATER are alive: a parcel at rest for a while is made again at the breach, so the flow goes on
 * as long as the lake has water over the breach, at the pace the cap allows. Each parcel made lowers the lake by WATER_VP
 * over its area, so the flow thins as the lake falls and stops when it is level with the breach's floor.
 *
 * Deterministic like everything here: integer hashes, no clocks. Parcels are never counted as awake bodies by the rules
 * (a level is judged with water still running). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "box3d/box3d.h"
#include "cartwheel/math.h"
#include "game.h"

#define LAYER_WATER 3
#define WATER_R 0.13f
#define WATER_VP 0.22f /* m^3 a parcel stands for */
#define CD 0.6f        /* orifice discharge */
#define PER_TICK 3     /* parcels made in a tick at most */

static inline t3_vec3 V3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }

float water_dam_zc(const water_state *w, float x) { return w->arch_a * x * x + w->arch_z0; }

/* the lake's spill gate: which blocks are the dam (the face the water stands against), found by where their middles are */
static bool is_dam(const water_state *w, t3_vec3 p) {
  float ax = fabsf(p.x), zc = water_dam_zc(w, p.x);
  if (p.y < 0.3f || p.y > w->dam_top - 0.1f) return false;
  if (ax > 4.6f + 0.2f * p.y + 0.9f) return false;
  return p.z > zc - 0.8f && p.z < zc + 0.7f + (w->dam_top - p.y) * 0.07f + 0.6f;
}

void water_setup(game *g, float level, float area, float dam_top, float arch_a, float arch_z0) {
  water_state *w = &g->wt;
  memset(w, 0, sizeof *w);
  w->on = true;
  w->level = w->level0 = level, w->area = area, w->dam_top = dam_top, w->arch_a = arch_a, w->arch_z0 = arch_z0;
  w->bx0 = -WATER_BX * 0.5f * WATER_BIN;
  int cnt[WATER_NB] = { 0 };
  /* two passes over the dam's blocks: how many a bin lists, then the lists */
  for (int pass = 0; pass < 2; pass++) {
    int fill[WATER_NB] = { 0 };
    for (int i = 0; i < g->nb; i++) {
      const block *k = &g->b[i];
      if (!k->alive || k->mat == M_TARGET || k->target >= 0) continue;
      t3_vec3 p = game_block_xform(g, i).p;
      if (!is_dam(w, p)) continue;
      int ix0 = (int)floorf((p.x - k->half.x - w->bx0) / WATER_BIN + 0.5f), ix1 = (int)floorf((p.x + k->half.x - w->bx0) / WATER_BIN - 0.5f);
      int iy0 = (int)floorf((p.y - k->half.y) / WATER_BIN + 0.5f), iy1 = (int)floorf((p.y + k->half.y) / WATER_BIN - 0.5f);
      for (int iy = iy0 < 0 ? 0 : iy0; iy <= iy1 && iy < WATER_BY; iy++)
        for (int ix = ix0 < 0 ? 0 : ix0; ix <= ix1 && ix < WATER_BX; ix++) {
          int b = iy * WATER_BX + ix;
          if (pass == 0) cnt[b]++;
          else if (w->bin_off[b] + fill[b] < WATER_LIST) w->bin_blk[w->bin_off[b] + fill[b]++] = (uint16_t)i;
        }
    }
    if (pass == 0) {
      int o = 0;
      for (int b = 0; b < WATER_NB; b++) w->bin_off[b] = (uint16_t)(o > 65535 ? 65535 : o), o += cnt[b];
      w->bin_off[WATER_NB] = (uint16_t)(o > 65535 ? 65535 : o);
    }
  }
  for (int b = 0; b < WATER_NB; b++) w->bin_n[b] = (uint8_t)(w->bin_off[b + 1] > w->bin_off[b] ? w->bin_off[b + 1] - w->bin_off[b] : 0);
}

/* a block of the dam is still where it was built */
static bool in_place(const game *g, int i) {
  const block *k = &g->b[i];
  if (!k->alive) return false;
  if (!g->ch[k->chunk].dyn) return true;
  t3_vec3 p = game_block_xform(g, i).p;
  float dx = p.x - k->home.x, dy = p.y - k->home.y, dz = p.z - k->home.z;
  return dx * dx + dy * dy + dz * dz < 0.25f * 0.25f;
}

int water_awake(const game *g) {
  const water_state *w = &g->wt;
  int n = 0;
  for (int i = 0; i < w->n; i++) n += w->live[i] && cw_body3d_awake(w->body[i]);
  return n;
}

static void make_parcel(game *g, int slot, t3_vec3 p, t3_vec3 v) {
  water_state *w = &g->wt;
  if (slot < w->n) { /* made again (a body that came to rest long ago) */
    cw_body3d b = w->body[slot];
    cw_body3d_set_transform(b, (cw_xform3){ p, { 0, 0, 0, 1 } });
    cw_body3d_set_velocity(b, v);
    cw_body3d_set_angular_velocity(b, V3(0, 0, 0));
    cw_body3d_set_awake(b, true);
  } else {
    cw_body3d_def bd = cw_body3d_default_def();
    bd.position = p, bd.linear_velocity = v;
    bd.linear_damping = 0.5f, bd.angular_damping = 6.0f;
    bd.interpolate = true;
    cw_body3d b = cw_body3d_create(g->w, &bd);
    b3Body_SetSleepThreshold(cw_body3d_raw(b), 0.3f);
    cw_shape3d_def sd = cw_shape3d_default_def();
    sd.density = 1000;
    sd.material.friction = 0.3f, sd.material.restitution = 0;
    sd.layer = LAYER_WATER;
    sd.mask = (1u << 0) | (1u << 1) | (1u << LAYER_WATER); /* the ground, the blocks, itself: not the shot */
    sd.contact_events = false, sd.hit_events = false;
    cw_shape3d_sphere(b, &sd, V3(0, 0, 0), WATER_R);
    w->body[w->n] = b;
    w->n++;
  }
  w->born[slot] = g->tick;
  w->live[slot] = 1;
}

/* a parcel comes to rest low down: it joins the pool (a disc where it lay) and its body is parked asleep under the ground, so a pool costs nothing */
static void retire_to_pool(game *g, int i, bool keep_slot) {
  water_state *w = &g->wt;
  t3_vec3 p = cw_body3d_transform(w->body[i]).p;
  w->pool[w->npool % WATER_POOL] = V3(p.x, p.y - WATER_R, p.z);
  w->npool++, w->retired++;
  (void)keep_slot;
  cw_body3d b = w->body[i];
  cw_body3d_set_velocity(b, V3(0, 0, 0));
  cw_body3d_set_transform(b, (cw_xform3){ V3(0, -40.0f - (float)i * 0.3f, 0), { 0, 0, 0, 1 } });
  cw_body3d_set_awake(b, false);
  w->live[i] = 0;
}

void water_step(game *g) {
  water_state *w = &g->wt;
  if (!w->on) return;
  const float dt = TICK;
  /* parcels at rest (or nearly) low down, after a second, are pooled */
  for (int i = 0; i < w->n; i++) {
    if (!w->live[i] || g->tick - w->born[i] < 60) continue;
    cw_xform3 x = cw_body3d_transform(w->body[i]);
    t3_vec3 v = cw_body3d_velocity(w->body[i]);
    if ((x.p.y < 1.4f && v.x * v.x + v.y * v.y + v.z * v.z < 0.8f * 0.8f) || (g->tick - w->born[i] > 150 && v.x * v.x + v.y * v.y + v.z * v.z < 0.3f * 0.3f)) retire_to_pool(g, i, false);
  }
  /* the breach, read every third tick: the open bins under the surface and the flow they pass */
  if (g->tick % 3 == 0 || w->nopen < 0) {
    w->nopen = 0;
    float q = 0;
    for (int b = 0; b < WATER_NB; b++) {
      if (!w->bin_n[b]) continue;
      float cy = ((float)(b / WATER_BX) + 0.5f) * WATER_BIN, h = w->level - cy;
      if (h < 0.05f) continue;
      bool open = true;
      for (int k = w->bin_off[b]; k < w->bin_off[b + 1] && k < WATER_LIST; k++)
        if (in_place(g, w->bin_blk[k])) { open = false; break; }
      if (!open) continue;
      if (w->nopen < WATER_NB) w->open_bin[w->nopen++] = (uint16_t)b;
      q += CD * WATER_BIN * WATER_BIN * sqrtf(2 * GRAV * h);
    }
    w->flow = q;
  }
  /* parcels made: the orifice's flow over the tick, in parcels */
  if (w->nopen > 0 && w->flow > 0) {
    w->carry += w->flow * dt / WATER_VP;
    if (w->carry > 12) w->carry = 12;
    int made = 0;
    while (w->carry >= 1 && made < PER_TICK) {
      int slot = -1;
      for (int i = 0; i < w->n && slot < 0; i++) if (!w->live[i]) slot = i; /* one retired into the pool */
      if (slot < 0 && w->n < MAXWATER) slot = w->n;
      if (slot < 0) { /* all in play: the oldest past two seconds (far downstream) */
        int age = 120;
        for (int i = 0; i < w->n; i++) {
          int a = g->tick - w->born[i];
          if (a > age) slot = i, age = a;
        }
        if (slot >= 0) retire_to_pool(g, slot, true);
      }
      if (slot < 0) { w->carry = fminf(w->carry, 1.0f); break; }
      int b = w->open_bin[(w->rr++) % w->nopen];
      float cx = w->bx0 + ((float)(b % WATER_BX) + 0.5f) * WATER_BIN, cy = ((float)(b / WATER_BX) + 0.5f) * WATER_BIN;
      float h = w->level - cy;
      uint32_t s = (uint32_t)(w->spawned * 2654435761u) ^ 0x9e3779b9u;
      float jx = (hashf(s) - 0.5f) * 0.12f, jy = (hashf(s ^ 0x85ebca6bu) - 0.5f) * 0.12f, jz = (float)(made & 3) * 0.27f;
      float v = fminf(0.85f * sqrtf(2 * GRAV * h), 6.5f);
      make_parcel(g, slot, V3(cx + jx, cy + jy, water_dam_zc(w, cx) - 0.7f - jz), V3((hashf(s ^ 0x1b873593u) - 0.5f) * 0.4f, 0, v));
      if (w->spawned == 0) w->first_tick = g->tick;
      w->spawned++, made++;
      w->carry -= 1;
      w->vol_out += WATER_VP;
      w->level -= WATER_VP / w->area;
    }
    if (made > 0 && (g->tick % 20 == 0 || w->spawned == made)) game_ev(g, EV_SPILL, 0, V3(0, w->level, 0), V3(0, 1, 0), w->flow, 0, w->spawned);
  } else w->carry = 0;
}

/* the water's own numbers for the checks: parcels alive, awake, at rest; their mean height and the kinetic energy (J) of them all */
void water_stats(const game *g, int *alive, int *awake, float *mean_y, float *max_y, float *ke) {
  const water_state *w = &g->wt;
  *alive = 0, *awake = 0;
  double ys = 0, e = 0;
  float my = -1e9f;
  for (int i = 0; i < w->n; i++) {
    if (!w->live[i]) continue;
    (*alive)++;
    cw_body3d b = w->body[i];
    *awake += cw_body3d_awake(b);
    t3_vec3 p = cw_body3d_transform(b).p, v = cw_body3d_velocity(b);
    ys += p.y, e += 0.5 * (1000.0 * 4.18879 * WATER_R * WATER_R * WATER_R) * (v.x * v.x + v.y * v.y + v.z * v.z);
    if (p.y > my) my = p.y;
  }
  *mean_y = *alive ? (float)(ys / *alive) : 0, *max_y = my, *ke = (float)e;
}
