/* pinball_tablemap: a top-down picture of a table's physics, for laying tables out.
 *   pinball_tablemap <table> <out.png> [ticks]
 * Solid shapes grey (a slingshot's or a bumper's kicking surface red), sensors green, holes yellow, ramp entrances
 * cyan with their paths drawn in cyan dots, the flippers at rest in blue, the bot's ball over [ticks] ticks of play
 * as a trail (white, dimming with age) and the places a ball sat still for 3 seconds as magenta rings. 1000 pixels a
 * metre. Native only (a development tool). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#undef W

#include "../src/game.h"

#define PPM 1000.0f
#define IMW 540
#define IMH 1100
static uint8_t img[IMH][IMW][3];

static void px(int x, int y, int r, int g, int b) {
  if (x < 0 || y < 0 || x >= IMW || y >= IMH) return;
  img[y][x][0] = (uint8_t)r, img[y][x][1] = (uint8_t)g, img[y][x][2] = (uint8_t)b;
}
static int tx(float x) { return (int)((x + 0.27f) * PPM); }
static int ty(float y) { return IMH - 1 - (int)((y + 0.01f) * PPM); }
static void ring(float x, float y, float r, int cr, int cg, int cb) {
  for (int a = 0; a < 64; a++) px(tx(x + r * cosf((float)a * 0.0982f)), ty(y + r * sinf((float)a * 0.0982f)), cr, cg, cb);
}
static void line(float ax, float ay, float bx, float by, int r, int g, int b) {
  float l = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
  int n = (int)(l * PPM * 1.5f) + 1;
  for (int i = 0; i <= n; i++) px(tx(ax + (bx - ax) * (float)i / (float)n), ty(ay + (by - ay) * (float)i / (float)n), r, g, b);
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: pinball_tablemap <table> <out.png> [ticks]\n"); return 2; }
  int which = atoi(argv[1]), ticks = argc > 3 ? atoi(argv[3]) : 0;
  static game g;
  game_new(&g, which, 1, 7);
  const pb_world *w = &g.W;
  /* the solid shapes and the flippers, by distance */
  for (int y = 0; y < IMH; y++)
    for (int x = 0; x < IMW; x++) {
      float wx = (float)x / PPM - 0.27f, wy = (float)(IMH - 1 - y) / PPM - 0.01f;
      int r = 18, gg = 22, b = 30;
      for (int i = 0; i < w->nsh; i++) {
        const pb_shape *s = &w->sh[i];
        if (wx < s->minx - 0.002f || wx > s->maxx + 0.002f || wy < s->miny - 0.002f || wy > s->maxy + 0.002f) continue;
        float nx, ny, d = pb_dist_shape(s, wx, wy, &nx, &ny);
        if (s->flags & SF_SENSOR) {
          if (s->kind == SH_CIRCLE && d < 0) r = 160, gg = 140, b = 20;
          continue;
        }
        if (d < 0) { if (s->kick > 0) r = 220, gg = 70, b = 60; else r = 150, gg = 150, b = 160; }
        else if (d < PB_R && r == 18) r = 34, gg = 38, b = 48; /* where the ball's centre cannot go */
      }
      for (int f = 0; f < w->nfl; f++) {
        float nx, ny;
        if (pb_dist_flipper(&w->fl[f], w->fl[f].rest, wx, wy, &nx, &ny) < 0) r = 70, gg = 120, b = 255;
      }
      px(x, y, r, gg, b);
    }
  for (int i = 0; i < w->nsh; i++) {
    const pb_shape *s = &w->sh[i];
    if (!(s->flags & SF_SENSOR) || s->kind != SH_SEG) continue;
    bool ramp = s->flags & SF_RAMP;
    line(s->ax, s->ay, s->bx, s->by, ramp ? 40 : 60, ramp ? 230 : 220, ramp ? 255 : 90);
  }
  for (int p = 0; p < w->npath; p++)
    for (int k = 0; k < w->path[p].n; k++) {
      const float *q = w->path[p].p[k];
      ring(q[0], q[1], 0.002f + q[2] * 0.03f, 40, 230, 255);
    }
  for (int l = 0; l < g.T.nlamp; l++) ring(g.T.lamps[l].x, g.T.lamps[l].y, g.T.lamps[l].size, 255, 150, 200);
  /* the bot's play */
  uint32_t rng = 99;
  float sx[PB_MAXB] = { 0 }, sy[PB_MAXB] = { 0 };
  int still[PB_MAXB] = { 0 }, stuck = 0;
  for (int t = 0; t < ticks; t++) {
    input in;
    bot_input(&g, &in, &rng);
    game_step(&g, &in);
    if (game_over(&g)) game_new(&g, which, 1, 7 + (uint32_t)t);
    for (int i = 0; i < PB_MAXB; i++) {
      const pb_ball *b = &g.W.b[i];
      if (b->state != BS_FREE) { still[i] = 0; continue; }
      int x = tx(b->x), y = ty(b->y);
      if (x >= 0 && y >= 0 && x < IMW && y < IMH) {
        uint8_t *c = img[y][x];
        int v = 120 + (int)(135.0f * (float)t / (float)ticks);
        c[0] = c[1] = c[2] = (uint8_t)(v > c[0] ? v : c[0]);
      }
      float dx = b->x - sx[i], dy = b->y - sy[i];
      if (dx * dx + dy * dy > 0.002f * 0.002f || (b->x > 0.219f && b->y < 0.08f)) still[i] = 0, sx[i] = b->x, sy[i] = b->y;
      else if (++still[i] == 180) ring(b->x, b->y, 0.016f, 255, 0, 255), stuck++, printf("still at %.3f %.3f\n", b->x, b->y);
    }
  }
  if (ticks) printf("table %d: %d flips, %d drains, %d ramps, %d stuck, score %lld\n", which, g.flips, g.drains, g.ramps_made, stuck, (long long)g.P[0].score);
  stbi_write_png(argv[2], IMW, IMH, 3, img, IMW * 3);
  return 0;
}
