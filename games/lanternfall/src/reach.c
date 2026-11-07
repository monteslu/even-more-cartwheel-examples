/* lanternfall: the moves the checker and the bot play (reach.h). */
#include <math.h>
#include <string.h>

#include "reach.h"

move MOVES[192];
int NMOVES;

static void add(move m) { MOVES[NMOVES++] = m; }

void moves_init(void) {
  if (NMOVES) return;
  static const uint8_t RUNS[2] = { 0, 10 }, HOLDS[3] = { 3, 8, 20 }, AIRS[3] = { 255, 8, 16 };
  /* jumps */
  for (int d = -1; d <= 1; d += 2)
    for (int r = 0; r < 2; r++)
      for (int h = 0; h < 3; h++)
        for (int a = 0; a < 3; a++) add((move){ .dir = (int8_t)d, .run = RUNS[r], .hold = HOLDS[h], .air = AIRS[a], .swim = 2 });
  add((move){ .dir = 0, .hold = 20, .air = 255, .swim = 2 });
  /* walking off a ledge, dropping through one */
  for (int d = -1; d <= 1; d += 2) {
    add((move){ .dir = (int8_t)d, .air = 255, .swim = 2 });
    add((move){ .dir = (int8_t)d, .air = 3, .swim = 2 });
  }
  for (int d = -1; d <= 1; d++) add((move){ .dir = (int8_t)d, .drop = 1, .air = 255, .swim = 2 });
  /* the second jump */
  for (int d = -1; d <= 1; d += 2)
    for (int r = 0; r < 2; r++) {
      add((move){ .dir = (int8_t)d, .run = RUNS[r], .hold = 20, .air = 255, .dj = 14, .swim = 2, .need = AB_DJUMP });
      add((move){ .dir = (int8_t)d, .run = RUNS[r], .hold = 20, .air = 255, .dj = 24, .swim = 2, .need = AB_DJUMP });
      add((move){ .dir = (int8_t)d, .run = RUNS[r], .hold = 20, .air = 22, .dj = 24, .swim = 2, .need = AB_DJUMP });
    }
  add((move){ .dir = 0, .hold = 20, .air = 255, .dj = 20, .swim = 2, .need = AB_DJUMP });
  /* the dash: on the ground, after a jump, after both jumps */
  for (int d = -1; d <= 1; d += 2) {
    add((move){ .dir = (int8_t)d, .dash = 255, .air = 255, .swim = 2, .need = AB_DASH });
    for (int r = 0; r < 2; r++) {
      add((move){ .dir = (int8_t)d, .run = RUNS[r], .hold = 20, .air = 255, .dash = 8, .swim = 2, .need = AB_DASH });
      add((move){ .dir = (int8_t)d, .run = RUNS[r], .hold = 20, .air = 255, .dash = 20, .swim = 2, .need = AB_DASH });
      add((move){ .dir = (int8_t)d, .run = RUNS[r], .hold = 20, .air = 255, .dj = 22, .dash = 30, .swim = 2, .need = AB_DASH | AB_DJUMP });
      add((move){ .dir = (int8_t)d, .run = RUNS[r], .hold = 20, .air = 255, .dj = 30, .dash = 12, .swim = 2, .need = AB_DASH | AB_DJUMP });
    }
  }
  /* climbing: up the wall and over, or off it */
  for (int d = -1; d <= 1; d += 2) {
    add((move){ .dir = (int8_t)d, .run = 4, .hold = 20, .air = 255, .climb = 1, .swim = 2, .need = AB_CLIMB });
    add((move){ .dir = (int8_t)d, .run = 4, .hold = 20, .air = 255, .climb = 1, .walljump = 30, .swim = 2, .need = AB_CLIMB });
  }
  /* the plunge */
  for (int d = -1; d <= 1; d++) add((move){ .dir = (int8_t)d, .hold = 8, .air = 6, .pound = 12, .swim = 2, .need = AB_POUND });
  /* swimming */
  for (int d = -1; d <= 1; d += 2)
    for (int v = -1; v <= 1; v++) add((move){ .dir = (int8_t)d, .air = 255, .swim = (int8_t)v, .need = AB_SWIM });
  /* the grapple line */
  static const uint8_t GT[2] = { 6, 14 }, RL[3] = { 20, 36, 54 };
  for (int d = -1; d <= 1; d += 2)
    for (int r = 0; r < 2; r++)
      for (int g = 0; g < 2; g++)
        for (int l = 0; l < 3; l++)
          for (int c = 0; c < 3; c += 2)
            add((move){ .dir = (int8_t)d, .run = RUNS[r], .hold = 20, .air = 255, .grapple = GT[g], .release = RL[l], .chain = (uint8_t)c, .swim = 2, .need = AB_GRAPPLE });
}

bool move_input(const move *m, int t, const player *p, int *phase, pinput *in) {
  memset(in, 0, sizeof *in);
  if (t > MOVE_MAX_TICKS) return false;
  bool air = !p->on_ground || p->state == PS_SWIM || p->state == PS_GRAPPLE || p->state == PS_CLING || p->state == PS_POUND ||
             p->state == PS_DASH;
  if (*phase == 0 && air && t > 0) *phase = 1;
  if (*phase == 0 && m->dash == 255 && t > 1) *phase = 1; /* a ground dash need not leave the ground */
  if (*phase == 1 && !air && t > 2) *phase = 2;
  if (*phase == 2) return !(p->on_ground && fabsf(p->vx) < 0.01f && p->state == PS_NORMAL);
  int d = m->dir;
  int k = t - m->run; /* ticks since the jump */
  if (m->drop) {
    if (t == 0) in->my = 1, in->jump = in->jump_held = true;
    if (*phase == 1) in->mx = (int8_t)d;
    return true;
  }
  if (m->dash == 255) {
    if (t == 0) in->dash = true;
    in->mx = (int8_t)(t < 2 ? d : 0);
    return true;
  }
  if (m->swim != 2) {
    in->mx = (int8_t)d;
    in->my = m->swim;
    in->jump = t % 30 == 29;
    in->jump_held = in->jump;
    return t < 220;
  }
  if (m->hold == 0) { /* walk off */
    if (*phase == 0) in->mx = (int8_t)d;
    else if (m->air == 255 || t < 400) {
      static int left; /* (not needed: the air hold counts from the tick the ground went) */
      (void)left;
      in->mx = (int8_t)(p->ground_t == 0 && (m->air == 255 || p->coyote > COYOTE - (int)m->air) ? d : 0);
    }
    return t < 200;
  }
  if (k < 0) {
    in->mx = (int8_t)d;
    return true;
  }
  in->mx = (int8_t)(m->air == 255 || k < m->air ? d : 0);
  if (k == 0) in->jump = true;
  in->jump_held = k < m->hold;
  if (m->dj) {
    if (k == m->dj) in->jump = true;
    if (k >= m->dj && k < m->dj + 14) in->jump_held = true;
  }
  if (m->dash && m->dash != 255 && k == m->dash) in->dash = true;
  if (m->grapple) {
    int period = m->release + 6;
    for (int i = 0; i <= m->chain; i++) {
      int gt = m->grapple + i * period;
      if (k == gt) in->grapple = true;
      if (k == gt + m->release) in->jump = true;
      if (k >= gt + m->release && k < gt + m->release + 6) in->jump_held = true;
    }
  }
  if (m->climb && k >= 2) {
    in->mx = (int8_t)d;
    in->my = -1;
    if (m->walljump && k >= m->walljump) {
      in->my = 0;
      in->mx = (int8_t)(k < m->walljump + 12 ? -d : 0);
      if (k == m->walljump) in->jump = true;
      in->jump_held = k < m->walljump + 16;
    }
  }
  if (m->pound && k == m->pound) in->my = 1, in->dash = true;
  return true;
}

int move_play(const move *m, const world *w, const uint8_t (*t)[TW], uint32_t abilities, float x, float y, player *out, int *broke) {
  player p;
  memset(&p, 0, sizeof p);
  p.hp_max = 5, p.en_max = 3;
  player_spawn(&p, x, y);
  p.on_ground = 1;
  pworld pw = { .t = t, .abilities = abilities };
  int room = w_room_at(w, x, y - 0.5f);
  pworld_anchors(&pw, w, room);
  int phase = 0;
  *broke = -1;
  for (int tick = 0; tick <= MOVE_MAX_TICKS + 60; tick++) {
    pinput in;
    if (!move_input(m, tick, &p, &phase, &in)) {
      *out = p;
      return tick;
    }
    int b = player_step(&p, &in, &pw);
    if (p.ev & (PE_HAZARD | PE_COLD)) return -1;
    if (b >= 0) {
      *broke = b;
      *out = p;
      return tick;
    }
    int nr = w_room_at(w, p.x, p.y - 0.5f);
    if (nr != room && nr >= 0) room = nr, pworld_anchors(&pw, w, room);
  }
  return -1;
}
