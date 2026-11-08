/* pinball_flashcheck: does any light on the table strobe? (tests/pinball-flash.mjs)
 *
 * The house rule: light swells and fades, never blinks or flickers; at most three flashes a second anywhere (WCAG
 * 2.3.1, the Harding general and red thresholds), and here stricter: no single light may turn round (from brightening
 * to dimming or back) by 10% of its range more than three times in any second. This plays the rules and the bot
 * headless on every table and steps every light the view draws (src/lights.c: each insert, each standup, drop target
 * and pop cap, each pop's light, each flasher, the general illumination) exactly as the view does, frame by frame:
 *   play      bot games (the lit lanes moving with every flip, pops rattling, combos, drains)
 *   modes     each of the table's four modes started in turn while the bot plays (roving shots, hurry-ups, sweeps,
 *             the toy, two balls)
 *   wizard    the wizard mode: four balls, every shot pulsing
 *   storm     multiball with the big events fired far faster than play can (a jackpot, a mode shot, a lock, a skill
 *             shot every few ticks): the flashers' limit, not the rules, has to hold it
 * For each light: its turns (a move of 0.1 or more from its last extreme the other way) and the most in any 60
 * frames; its flashes (a pair of turns) likewise. Prints a line per table and phase and a JSON summary last.
 * pinball_flashcheck_oldstrobe is the same program on the lights as they were (5 Hz blinking inserts, 15 Hz
 * flicker on the flashers): the control, which must fail.
 *   pinball_flashcheck [seconds per phase, default 120] */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/game.h"
#include "../src/lights.h"

#define WIN 60
enum { K_LAMP, K_PART, K_POP, K_FLASH, K_GI, K_COUNT };
static const char *KN[K_COUNT] = { "inserts", "targets and caps", "pop lights", "flashers", "general light" };

typedef struct track {
  float ext;      /* the last extreme */
  int8_t dir;
  int turns[WIN]; /* ring of turn frames */
  int head;
  int worst;      /* most turns in any 60 frames */
  int total;
  bool init;
} track;

static void track_add(track *t, float v, int frame) {
  if (!t->init) { t->init = true, t->ext = v; return; }
  if ((t->dir == 1 && v > t->ext) || (t->dir == -1 && v < t->ext)) { t->ext = v; return; }
  if (fabsf(v - t->ext) < 0.1f) return;
  t->dir = v > t->ext ? 1 : -1, t->ext = v;
  t->total++;
  t->turns[t->head] = frame, t->head = (t->head + 1) % WIN;
  int n = 0;
  for (int i = 0; i < WIN; i++)
    if (t->turns[i] > 0 && frame - t->turns[i] < WIN) n++;
  if (n > t->worst) t->worst = n;
}

static track TR[K_COUNT][MAXLAMP + MAXEL];
static int worst_k[K_COUNT], total_k[K_COUNT];

static void measure(const pb_lights *L, const game *g, int frame) {
  const table *T = &g->T;
  for (int i = 0; i < T->nlamp; i++) track_add(&TR[K_LAMP][i], L->lamp[i].v, frame);
  for (int i = 0; i < T->nel; i++) track_add(&TR[K_PART][i], L->part[i].v, frame);
  for (int i = 0; i < NPOPL; i++) track_add(&TR[K_POP][i], L->pop[i].v, frame);
  for (int i = 0; i < L->nflash; i++) track_add(&TR[K_FLASH][i], L->flash[i].v, frame);
  track_add(&TR[K_GI][0], L->gi.v, frame);
}

static void reset_tracks(void) { memset(TR, 0, sizeof TR); }
static void collect(int *worst, int *total) {
  for (int k = 0; k < K_COUNT; k++) {
    worst[k] = 0, total[k] = 0;
    for (int i = 0; i < MAXLAMP + MAXEL; i++) {
      if (TR[k][i].worst > worst[k]) worst[k] = TR[k][i].worst;
      total[k] += TR[k][i].total;
    }
  }
}

static int nflashers(const table *T) {
  int n = 0;
  for (int i = 0; i < T->ndec; i++) n += T->dec[i].kind == DC_FLASHER;
  return n;
}

int main(int argc, char **argv) {
  int secs = argc > 1 ? atoi(argv[1]) : 120;
  static game g;
  static pb_lights L;
  uint32_t rng = 99;
  int fails = 0, all_worst[K_COUNT] = { 0 };
  long frames_total = 0;
  static const char *PH[4] = { "play", "modes", "wizard", "storm" };
  for (int t = 0; t < NTABLES; t++)
    for (int ph = 0; ph < 4; ph++) {
      game_new(&g, t, 1, 1234u + (uint32_t)t * 17u + (uint32_t)ph);
      lights_reset(&L, nflashers(&g.T));
      reset_tracks();
      int mode_i = 0;
      for (int f = 0; f < secs * 60; f++) {
        if (game_over(&g)) game_new(&g, t, 1, 777u + (uint32_t)f);
        if (ph >= 1) g.practice = true; /* the ball always comes back: the phase plays on */
        if (ph == 1 && g.state == GS_PLAY && g.mode < 0 && f % 600 == 0) game_test_start_mode(&g, mode_i++);
        if (ph == 2 && g.state == GS_PLAY && !g.wizard && f % 300 == 0) game_test_light_wizard(&g), game_test_event(&g, EV_HOLE, g.T.mode_hole, 1);
        input in;
        bot_input(&g, &in, &rng);
        game_step(&g, &in);
        if (ph == 3) { /* multiball kept going, the big events fired every few ticks */
          if (g.state == GS_PLAY && g.balls_in_play < 3 && f % 45 == 0) game_launch_ball(&g);
          g.ball_save_t = 600;
          static const int BIG[6] = { FX_JACKPOT, FX_MODE_SHOT, FX_LOCK, FX_SKILL, FX_MULTIBALL, FX_RAMP_DONE };
          if (f % 4 == 0 && g.nfx < 63) g.fx[g.nfx].kind = (uint8_t)BIG[(f / 4) % 6], g.fx[g.nfx].a = 0, g.fx[g.nfx].v = 1, g.nfx++;
          if (f % 3 == 0 && g.nfx < 63 && g.T.npop) g.fx[g.nfx].kind = FX_POP, g.fx[g.nfx].a = (int16_t)g.T.pops[(f / 3) % g.T.npop], g.fx[g.nfx].v = 1, g.nfx++;
        }
        lights_step(&L, &g);
        measure(&L, &g, f + 1);
        frames_total++;
      }
      int w[K_COUNT], tot[K_COUNT];
      collect(w, tot);
      bool bad = false;
      for (int k = 0; k < K_COUNT; k++) bad |= w[k] > 3, all_worst[k] = w[k] > all_worst[k] ? w[k] : all_worst[k];
      printf("%s table %d %-6s: most turns of one light in a second: inserts %d, targets and caps %d, pop lights %d, flashers %d, general light %d (turns: %d %d %d %d %d)\n",
             bad ? "FAIL" : "ok  ", t, PH[ph], w[0], w[1], w[2], w[3], w[4], tot[0], tot[1], tot[2], tot[3], tot[4]);
      fails += bad;
    }
  printf("{\"fails\":%d,\"frames\":%ld", fails, frames_total);
  for (int k = 0; k < K_COUNT; k++) printf(",\"%s\":%d", KN[k], all_worst[k]);
  printf("}\n");
  return fails ? 1 : 0;
}
