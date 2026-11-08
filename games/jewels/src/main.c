/* jewels (working title): a calm, untimed match-three, readable from a couch. The original's rules on a 10 x 8
 * board of six cut stones, each its own shape as well as its own colour, ray traced through their facets.
 *
 *   play   the d-pad (or a stick, or the arrow keys) moves the cursor; the pick button picks a stone up and a
 *          direction swaps it; the hint button shows a move. Touch or mouse: tap a stone then its neighbour, or drag
 *          it onto the neighbour; tap the hint button on the panel. Any pad, the keyboard, the mouse and every
 *          finger drive the one board.
 *   jewels_auto plays itself through the same controls (pad, touch, mouse and drags in turn).
 *   jewels_bench is the perf gate's scene: a long cascade with big clears, again and again.
 * Environment (native, development): JEWELS_SEED=n, JEWELS_QUALITY=0..4. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/platform.h"
#include "cartwheel/project.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "cuts.h"
#include "fx.h"
#include "game.h"
#include "render.h"
#include "sound.h"

#ifndef JEWELS_AUTOPILOT
#define JEWELS_AUTOPILOT 0
#endif
#ifndef JEWELS_BENCH
#define JEWELS_BENCH 0
#endif

typedef struct save_data { int64_t best; int32_t games; } save_data;

static struct {
  jw_game g;
  jw_fx fx;
  bool in_match, built, baked;
  cw_action left, right, up, down, pick, hint;
  uint32_t ticks;
  int last_player;
  /* debug fields */
  int32_t score, best, moves, state, clears, cascade_max, reshuffles, hints, illegal, picks, cur_x, cur_y, sel;
  int32_t pad_swaps, touch_swaps, drag_swaps, tap_swaps, largest, finales, rumbles, playing;
  int32_t fx_power_x100, fx_max_x100, fx_steps_x100[6], fx_sparks, fx_shards, ptr_down;
  uint32_t hash;
  float bench_ms;
} G;

static cw_font serif, sans;

static save_data *save(void) { return (save_data *)cw_shell_game_save(); }

/* ── the match ───────────────────────────────────────────────────────── */

static void begin_game(void) {
  uint32_t seed = 0x51ed2701u ^ (G.ticks * 2654435761u); /* the ticks a person takes to start: every game differs, a replay is the same */
  if (getenv("JEWELS_SEED")) seed = (uint32_t)strtoul(getenv("JEWELS_SEED"), NULL, 10);
  if (JEWELS_BENCH) { extern int jw_bench_cascade; seed = 20261107u, jw_bench_cascade = 7; } /* the worst case: long cascades of big clears */
  G.g.best = save()->best;
  jw_game_new(&G.g, seed);
  jw_fx_init(&G.fx);
  G.in_match = true;
  save()->games++;
  jw_sound_music(true);
}

static void start(int players, void *u) {
  (void)players, (void)u;
  /* one board, every pad: seat 0 keeps the keyboard, mouse and touch; the other pads sit in seats 1-3 and the
   * game reads all four */
  int mine = cw_input_player_pad(0);
  int seat = 1;
  for (int p = 0; p < CW_MAX_PADS && seat < 4; p++)
    if (p != mine) cw_input_assign(seat++, p, 0);
  begin_game();
}

static void shell_sound(cw_shell_sound s, void *u) {
  (void)u;
  jw_sound_menu(s == CW_SHELL_SND_MOVE ? 0 : s == CW_SHELL_SND_BACK ? 2 : 1);
}
static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *u) {
  (void)from, (void)u;
  if (to == CW_SHELL_TITLE) jw_sound_music(true);
}
static void shell_quit(void *u) {
  (void)u;
  G.in_match = false;
}

/* ── the bot (jewels_auto, jewels_bench): it plays through the raw input, as a person would ─────────────── */

typedef struct bot_step { int frames; int kind; float x0, y0, x1, y1; uint32_t buttons; } bot_step;
enum { B_WAIT, B_PAD, B_TOUCH, B_MOUSE, B_DRAG };
static struct { bot_step q[64]; int n, i, f; int moves; } BOT;

static void bot_push(int kind, int frames, uint32_t buttons, float x0, float y0, float x1, float y1) {
  if (BOT.n < 64) BOT.q[BOT.n++] = (bot_step){ frames, kind, x0, y0, x1, y1, buttons };
}
static void bot_plan(void) {
  jw_game *g = &G.g;
  BOT.n = BOT.i = BOT.f = 0;
  jw_move ms[JW_MAX_MOVES];
  int n = jw_legal_moves(&g->grid, ms);
  if (!n) return;
  int k = BOT.moves++;
  /* the bench wants the biggest clears: the move that clears the most */
  jw_move m = ms[(k * 7 + (int)(g->score / 10)) % n];
  if (JEWELS_BENCH) {
    int bestc = -1;
    for (int i = 0; i < n; i++) {
      jw_grid t = g->grid;
      jw_swap(&t, ms[i].x, ms[i].y, ms[i].x + ms[i].dx, ms[i].y + ms[i].dy);
      jw_matches mm;
      jw_find_matches(&t, &mm);
      if (mm.nhit > bestc) bestc = mm.nhit, m = ms[i];
    }
  }
  int method = JEWELS_BENCH ? 0 : k % 5;
  int pause = JEWELS_BENCH ? 6 : 26;
  if (!JEWELS_BENCH && k % 6 == 5) { /* now and then it asks for a hint first, on the pad or on the button */
    if (k % 12 == 5) bot_push(B_PAD, 3, 1u << CW_PAD_WEST, 0, 0, 0, 0);
    else bot_push(B_TOUCH, 3, 0, JW_HINT_X + JW_HINT_W * 0.5f, JW_HINT_Y + JW_HINT_H * 0.5f, 0, 0);
    bot_push(B_WAIT, 50, 0, 0, 0, 0, 0);
  }
  float ax = jw_cell_cx(m.x), ay = jw_cell_cy(m.y), bx = jw_cell_cx(m.x + m.dx), by = jw_cell_cy(m.y + m.dy);
  switch (method) {
  case 0: case 3: { /* the pad: walk the cursor, pick it up, push */
    int dx = m.x - g->cur_x, dy = m.y - g->cur_y;
    for (int i = 0; i < abs(dx); i++) bot_push(B_PAD, 3, 1u << (dx > 0 ? CW_PAD_RIGHT : CW_PAD_LEFT), 0, 0, 0, 0), bot_push(B_WAIT, JEWELS_BENCH ? 1 : 5, 0, 0, 0, 0, 0);
    for (int i = 0; i < abs(dy); i++) bot_push(B_PAD, 3, 1u << (dy > 0 ? CW_PAD_DOWN : CW_PAD_UP), 0, 0, 0, 0), bot_push(B_WAIT, JEWELS_BENCH ? 1 : 5, 0, 0, 0, 0, 0);
    bot_push(B_PAD, 3, 1u << CW_PAD_SOUTH, 0, 0, 0, 0), bot_push(B_WAIT, JEWELS_BENCH ? 1 : 10, 0, 0, 0, 0, 0);
    bot_push(B_PAD, 3, 1u << (m.dx > 0 ? CW_PAD_RIGHT : m.dy > 0 ? CW_PAD_DOWN : CW_PAD_UP), 0, 0, 0, 0);
    break;
  }
  case 1: /* touch: tap one, tap the other */
    bot_push(B_TOUCH, 3, 0, ax, ay, 0, 0), bot_push(B_WAIT, 14, 0, 0, 0, 0, 0), bot_push(B_TOUCH, 3, 0, bx, by, 0, 0);
    break;
  case 2: /* touch: drag it over */
    bot_push(B_DRAG, 12, 0, ax, ay, bx, by);
    break;
  case 4: /* the mouse: click, click */
    bot_push(B_MOUSE, 3, 0, ax, ay, 0, 0), bot_push(B_WAIT, 14, 0, 0, 0, 0, 0), bot_push(B_MOUSE, 3, 0, bx, by, 0, 0);
    break;
  }
  bot_push(B_WAIT, pause, 0, 0, 0, 0, 0);
}

static void bot_filter(cw_raw_input *in, uint64_t frame, void *u) {
  (void)frame, (void)u;
  if (!G.in_match || cw_shell_screen_now() != CW_SHELL_PLAY) return;
  /* under the debugger's pause a frame that runs no tick still reads input: the bot waits for a frame that ticks
   * (tests/jewels-frames.mjs steps the game a tick at a time) */
  if (cw_debug_paused() && !cw_debug_steps_left()) return;
  /* the bot's fingers and mouse button are up unless a step holds them (a host may keep the last frame's state) */
  in->touches[1].active = in->touches[1].tapped = false;
  in->touches[2].active = in->touches[2].tapped = false;
  in->mouse_buttons &= ~1u, in->mouse_pressed &= ~1u;
  if (BOT.i >= BOT.n) {
    if (G.g.state != JW_IDLE) return;
    bot_plan();
    if (!BOT.n) return;
  }
  bot_step *s = &BOT.q[BOT.i];
  int w, h;
  cw_window_size(&w, &h);
  float sx = (float)w / 1920.0f, sy = (float)h / 1080.0f;
  float t = s->frames > 1 ? (float)BOT.f / (float)(s->frames - 1) : 1;
  switch (s->kind) {
  case B_PAD: in->pads[0].buttons |= s->buttons, in->pads[0].pressed |= BOT.f == 0 ? s->buttons : 0; break;
  case B_TOUCH: in->touches[1].active = true, in->touches[1].tapped = BOT.f == 0, in->touches[1].x = s->x0 * sx, in->touches[1].y = s->y0 * sy; break;
  case B_DRAG:
    in->touches[2].active = BOT.f < s->frames - 1, in->touches[2].tapped = BOT.f == 0;
    in->touches[2].x = (s->x0 + (s->x1 - s->x0) * t) * sx, in->touches[2].y = (s->y0 + (s->y1 - s->y0) * t) * sy;
    break;
  case B_MOUSE:
    in->mouse_x = s->x0 * sx, in->mouse_y = s->y0 * sy, in->mouse_buttons |= 1, in->mouse_present = true;
    if (BOT.f == 0) in->mouse_pressed |= 1;
    break;
  default: break;
  }
  if (++BOT.f >= s->frames) BOT.f = 0, BOT.i++;
}

/* ── input ───────────────────────────────────────────────────────────── */

static bool pressed_any(cw_action a, int *who) {
  for (int p = 0; p < 4; p++)
    if (cw_action_just_pressed(p, a)) {
      if (who) *who = p;
      return true;
    }
  return false;
}

static void read_input(jw_input *in) {
  memset(in, 0, sizeof *in);
  int who = G.last_player;
  in->left = pressed_any(G.left, &who), in->right = pressed_any(G.right, &who);
  in->up = pressed_any(G.up, &who), in->down = pressed_any(G.down, &who);
  in->pick = pressed_any(G.pick, &who), in->hint = pressed_any(G.hint, &who);
  G.last_player = who;
  const cw_raw_input *raw = cw_input_raw();
  if (!raw) return;
  int w, h;
  cw_window_size(&w, &h);
  float sx = 1920.0f / (float)(w > 0 ? w : 1920), sy = 1080.0f / (float)(h > 0 ? h : 1080);
  /* pointer slot 0: the mouse (a finger driving it too is the same finger, so it is skipped then) */
  if (raw->mouse_present && !raw->mouse_from_touch) {
    in->ptr_down[0] = raw->mouse_buttons & 1, in->ptr_tap[0] = raw->mouse_pressed & 1;
    in->ptr_x[0] = raw->mouse_x * sx, in->ptr_y[0] = raw->mouse_y * sy;
  }
  for (int i = 0; i < CW_MAX_TOUCHES && i + 1 < JW_POINTERS; i++) {
    const cw_touch_state *t = &raw->touches[i];
    in->ptr_down[i + 1] = t->active, in->ptr_tap[i + 1] = t->tapped;
    in->ptr_x[i + 1] = t->x * sx, in->ptr_y[i + 1] = t->y * sy;
  }
}

/* ── the tick ────────────────────────────────────────────────────────── */

static void publish(void) {
  const jw_game *g = &G.g;
  G.score = (int32_t)(g->score > 2000000000 ? 2000000000 : g->score), G.best = (int32_t)(g->best > 2000000000 ? 2000000000 : g->best);
  G.moves = g->moves, G.state = g->state, G.clears = g->clears, G.cascade_max = g->cascade_max, G.reshuffles = g->reshuffles;
  G.hints = g->hints, G.illegal = g->illegal, G.picks = g->picks, G.cur_x = g->cur_x, G.cur_y = g->cur_y;
  G.sel = g->sel_x >= 0 ? g->sel_y * JW_W + g->sel_x : -1;
  G.pad_swaps = g->pad_swaps, G.touch_swaps = g->touch_swaps, G.drag_swaps = g->drag_swaps, G.tap_swaps = g->tap_swaps;
  G.largest = g->largest_group, G.hash = jw_game_hash(g);
  G.finales = G.fx.finales, G.rumbles = G.fx.rumbles;
  G.fx_power_x100 = (int32_t)lroundf(G.fx.power_last * 100), G.fx_max_x100 = (int32_t)lroundf(G.fx.power_max * 100);
  G.fx_sparks = G.fx.sparks_last, G.fx_shards = G.fx.shards_last;
  G.playing = G.in_match && cw_shell_screen_now() == CW_SHELL_PLAY;
}

static void fixed_update(void *u, double dt) {
  (void)u;
  G.ticks++;
  if (!cw_shell_update()) {
    publish();
    return;
  }
  if (!G.in_match) return;
  jw_input in;
  read_input(&in);
  G.g.nev = 0;
  jw_game_tick(&G.g, &in);
  int rumbles_before = G.fx.rumbles;
  jw_fx_events(&G.fx, &G.g, (float)dt);
  static int log_fx = -1;
  if (log_fx < 0) log_fx = getenv("JEWELS_FX_LOG") != NULL; /* tests/jewels-effects.mjs reads these lines */
  if (log_fx)
    for (int i = 0; i < G.g.nev; i++) {
      const jw_event *e = &G.g.ev[i];
      if (e->kind != JW_EV_CLEAR) continue;
      char hit[JW_W * JW_H + 1]; /* the cells it cleared, row by row (the effects test measures the light round them) */
      for (int k = 0; k < JW_W * JW_H; k++) hit[k] = G.g.clear.hit[k / JW_W][k % JW_W] ? '1' : '0';
      hit[JW_W * JW_H] = 0;
      fprintf(stderr, "fx clear frame=%u step=%d cells=%d shape=%d power=%.3f sparks=%d shards=%d ring=%d trail=%d rumble=%.3f x=%.0f y=%.0f hit=%s\n", (unsigned)cw_frame_index(),
              e->b, e->a, e->d, G.fx.power_last, (int)(10 + 8 * G.fx.power_last > 50 ? 50 : 10 + 8 * G.fx.power_last),
              (int)(4 + 3 * G.fx.power_last > 18 ? 18 : 4 + 3 * G.fx.power_last), e->a >= 4 || e->b >= 2, e->b >= 2,
              G.fx.rumbles > rumbles_before ? G.fx.rumble_last : 0.0f, e->x, e->y, hit);
    }
  /* the strongest clear of each step, for the effects test (kept until the next move starts) */
  for (int i = 0; i < 6; i++)
    if (G.fx.step_power[i] > 0) G.fx_steps_x100[i] = (int32_t)lroundf(G.fx.step_power[i] * 100);
  for (int i = 0; i < G.g.nev; i++)
    if (G.g.ev[i].kind == JW_EV_SWAP) memset(G.fx_steps_x100, 0, sizeof G.fx_steps_x100);
  jw_sound_events(&G.g);
  int ms;
  float r = jw_fx_take_rumble(&G.fx, &ms);
  if (r > 0) cw_input_rumble(G.last_player, r, r * 0.6f, ms);
  if (G.g.best > save()->best) {
    save()->best = G.g.best;
#if !JEWELS_AUTOPILOT
    if (G.g.state == JW_IDLE) cw_shell_save_commit();
#endif
  }
  publish();
}

/* ── the frame the passes draw ───────────────────────────────────────── */

static void frame_fn(jw_frame *f, double alpha, void *u) {
  (void)u;
  const jw_game *g = &G.g;
  f->time = cw_time();
  float t = (float)f->time;
  f->board_only = !G.in_match || cw_shell_screen_now() != CW_SHELL_PLAY;
  f->ngems = 0;
  for (int y = 0; y < JW_H; y++)
    for (int x = 0; x < JW_W; x++) {
      int k = g->grid.c[y][x];
      if (!k) continue;
      /* a stone being cleared still shows its old kind until it is gone */
      if (g->state == JW_CLEAR && g->clear.hit[y][x]) k = g->clear_kind[y][x];
      jw_cell_view v = jw_game_cell(g, x, y, (float)alpha);
      jw_gem_draw *d = &f->gems[f->ngems++];
      d->kind = k - 1;
      d->x = jw_cell_cx(x) + v.dx * JW_CELL, d->y = jw_cell_cy(y) + v.dy * JW_CELL;
      d->scale = v.scale, d->opacity = v.opacity, d->glow = v.glow;
      d->lift = (v.scale > 1 ? (v.scale - 1) * 260.0f : 0);
      /* idle shimmer: each stone rocks a few degrees on its own slow rhythm, so its facets catch the lights;
       * the board itself never changes */
      float ph = (float)(x * 7 + y * 13) * 0.61f;
      d->tilt_x = 0.060f * sinf(t * 0.33f + ph) + 0.02f * sinf(t * 0.71f + ph * 2.1f);
      d->tilt_y = 0.060f * sinf(t * 0.27f + ph * 1.7f) + 0.02f * sinf(t * 0.83f + ph * 0.4f);
      d->spin = 0.05f * sinf(t * 0.19f + ph * 2.9f) + v.spin;
      if (g->sel_x == x && g->sel_y == y && !f->board_only) d->lift += 18.0f, d->glow = fmaxf(d->glow, 0.08f + 0.05f * sinf(t * 6));
    }
  if (getenv("JEWELS_SHOWCASE")) { /* development: one stone, large, in the board's middle (look development) */
    int k = atoi(getenv("JEWELS_SHOWCASE"));
    float sc = getenv("JEWELS_SHOWCASE_SCALE") ? (float)atof(getenv("JEWELS_SHOWCASE_SCALE")) : 4.0f;
    f->ngems = 1;
    f->gems[0] = (jw_gem_draw){ .kind = k % 6, .x = JW_BX + 5 * JW_CELL, .y = JW_BY + 4 * JW_CELL, .scale = sc, .opacity = 1,
                                .tilt_x = 0.04f * sinf(t * 0.33f), .tilt_y = 0.04f * sinf(t * 0.27f) };
  }
  jw_fx_frame(&G.fx, f, (float)alpha / 60.0f);
  cw_prompt_device dev = cw_prompt_device_of(0);
  f->cursor_on = G.in_match && (dev == CW_PROMPT_PAD || dev == CW_PROMPT_KEYBOARD) && g->state == JW_IDLE;
  f->cursor_x = jw_cell_cx(g->cur_x), f->cursor_y = jw_cell_cy(g->cur_y);
  f->sel_on = g->sel_x >= 0;
  f->sel_x = jw_cell_cx(g->sel_x), f->sel_y = jw_cell_cy(g->sel_y);
  f->hint_on = g->hint_on;
  f->hint_x = g->hint.x, f->hint_y = g->hint.y, f->hint_dx = g->hint.dx, f->hint_dy = g->hint.dy;
  f->hint_t = (float)(JW_T_HINT - g->hint_t) / 60.0f;
  f->button_glow = g->hint_on ? 0.6f + 0.4f * sinf(t * 5) : (dev == CW_PROMPT_TOUCH || dev == CW_PROMPT_MOUSE) ? 0.25f : 0.0f;
  f->button_press = g->hint_on && g->hint_t > JW_T_HINT - 8 ? 1.0f : 0.0f;
}

/* ── the HUD ─────────────────────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static const cw_color GOLD = { 0.96f, 0.80f, 0.46f, 1 }, PALE = { 0.95f, 0.92f, 0.86f, 1 }, QUIET = { 0.72f, 0.64f, 0.52f, 1 };

static float text(cw_font f, const char *s, float x, float y, float size, cw_color c, cw_text_align al, float spacing) {
  cw_text_style st = { .size = size, .color = c, .letter_spacing = spacing, .shadow_color = rgba(0, 0, 0, 0.75f), .shadow_offset = cw_v2(0, size * 0.05f),
                       .shadow_softness = size * 0.08f };
  cw_text_metrics m = cw_font_measure(f, s, &st);
  if (al == CW_ALIGN_CENTER) x -= m.width * 0.5f;
  else if (al == CW_ALIGN_RIGHT) x -= m.width;
  return cw_r2d_text(f, s, cw_v2(x, y), &st).height;
}
/* text over the board: a dark outline and a soft shadow, so it reads over any stone */
static void text_on_board(const char *s, float x, float y, float size, cw_color c) {
  cw_text_style st = { .size = size, .color = c, .letter_spacing = size * 0.04f, .outline_width = size * 0.07f, .outline_color = rgba(0.05f, 0.03f, 0.01f, 0.9f * c.a),
                       .shadow_color = rgba(0, 0, 0, 0.6f * c.a), .shadow_offset = cw_v2(0, size * 0.06f), .shadow_softness = size * 0.12f, .shadow_spread = size * 0.04f };
  cw_text_metrics m = cw_font_measure(serif, s, &st);
  cw_r2d_text(serif, s, cw_v2(x - m.width * 0.5f, y), &st);
}
static void fmt_num(int64_t v, char *out, size_t cap) {
  char raw[32];
  snprintf(raw, sizeof raw, "%lld", (long long)v);
  int n = (int)strlen(raw), o = 0;
  for (int i = 0; i < n && o < (int)cap - 2; i++) {
    out[o++] = raw[i];
    if ((n - i - 1) % 3 == 0 && i < n - 1) out[o++] = ',';
  }
  out[o] = 0;
}

/* the panel's text boxes this frame: any two that overlap are a layout bug (debug field hud.overlaps, the worst
 * frame so far; tests/jewels-gates.json's jewels-hud gate holds it at 0 with every element showing) */
static struct { cw_rect r[32]; int n; } LAY;
static int32_t hud_overlaps;
static void lay(float x, float y, float w, float h) {
  if (LAY.n < 32) LAY.r[LAY.n++] = cw_rect_make(x, y, w, h);
}
static float ptext(cw_font f, const char *s, float x, float y, float size, cw_color c, float spacing) {
  cw_text_style st = { .size = size, .color = c, .letter_spacing = spacing, .shadow_color = rgba(0, 0, 0, 0.75f), .shadow_offset = cw_v2(0, size * 0.05f),
                       .shadow_softness = size * 0.08f };
  cw_text_metrics m = cw_font_measure(f, s, &st);
  cw_r2d_text(f, s, cw_v2(x - m.width * 0.5f, y), &st);
  /* the ink's box: the line's height less the font's leading */
  lay(x - m.width * 0.5f, y + m.height * 0.12f, m.width, m.height * 0.76f);
  return m.height;
}
static void pprompt(cw_font f, const char *s, float x, float y, float size, cw_color c) {
  cw_vec2 sz = cw_prompt_text_size(f, s, -1, size);
  cw_prompt_text(f, s, -1, cw_v2(x, y), size, c, CW_ALIGN_CENTER);
  lay(x - sz.x * 0.5f, y + sz.y * 0.12f, sz.x, sz.y * 0.76f);
}
static void lay_check(void) {
  int n = 0;
  for (int i = 0; i < LAY.n; i++)
    for (int j = i + 1; j < LAY.n; j++) {
      const cw_rect *a = &LAY.r[i], *b = &LAY.r[j];
      if (a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h) n++;
    }
  if (n > hud_overlaps) hud_overlaps = n;
  LAY.n = 0;
}

static void hud(void) {
  const jw_game *g = &G.g;
  const jw_fx *fx = &G.fx;
  float px = JW_PANEL_X + JW_PANEL_W * 0.5f;
  char b[64];
  float t = (float)cw_time();
  /* JEWELS_HUD_ALL (the layout gate): every element at once */
  static int all = -1;
  if (all < 0) all = getenv("JEWELS_HUD_ALL") != NULL;
  int64_t gain = all ? 450 : g->last_gain;
  int gain_t = all ? 60 : g->last_gain_t;
  int combo = all ? 6 : fx->combo_shown;
  bool combo_on = all || (fx->combo_shown >= 2 && (fx->combo_hold > 0 || g->state != JW_IDLE));
  int status = all ? 2 : g->status;
  /* the score, on its plaque: it swells a little when it grows */
  ptext(serif, "SCORE", px, 92, 30, QUIET, 6);
  fmt_num(all ? 1234567 : g->score, b, sizeof b);
  float pop = gain_t > 0 ? 1.0f + 0.10f * (float)gain_t / 60.0f : 1.0f;
  ptext(serif, b, px, 132 - (pop - 1) * 40, 84 * pop, gain_t > 0 ? GOLD : PALE, 0);
  if (gain_t > 0) {
    snprintf(b, sizeof b, "+");
    fmt_num(gain, b + 1, sizeof b - 1);
    ptext(serif, b, px, 246, 40, rgba(GOLD.r, GOLD.g, GOLD.b, fminf(1, (float)gain_t / 30.0f)), 0);
  }
  /* the best and the moves, on the second plaque */
  ptext(serif, "BEST", px, 336, 26, QUIET, 6);
  fmt_num(g->best, b, sizeof b);
  ptext(serif, b, px, 366, 52, PALE, 0);
  ptext(serif, "MOVES", px, 440, 26, QUIET, 6);
  snprintf(b, sizeof b, "%d", g->moves);
  ptext(serif, b, px, 470, 46, PALE, 0);
  /* the cascade: a counter that builds a step at a time; when a long one comes to rest its finale takes the counter's
   * place on the panel (never over the stones: the board must show where the new stones fell). JEWELS_HUD_ALL shows
   * the finale for the second half of every four seconds */
  bool finale_on = all ? fmodf(t, 4.0f) >= 2.0f : fx->finale_steps != 0;
  if (finale_on) {
    float k = all ? 0.5f : fx->finale_t / 2.6f, a = k < 0.12f ? k / 0.12f : k > 0.7f ? fmaxf(0, (1 - k) / 0.3f) : 1;
    int steps = all ? 6 : fx->finale_steps;
    float sw = 1 + 0.03f * sinf(t * 3);
    ptext(serif, steps >= 6 ? "MAGNIFICENT" : "BRILLIANT", px, 552 - (sw - 1) * 23, 46 * sw, rgba(1, 0.86f, 0.5f, a), 4);
    snprintf(b, sizeof b, "A CASCADE OF %d", steps);
    ptext(serif, b, px, 618, 28, rgba(1, 0.95f, 0.85f, a), 3);
  } else if (combo_on) {
    float k = all ? 1 : fminf(1, fx->combo_t * 5), a = all ? 1 : fminf(1, fmaxf(fx->combo_hold, 0) * 2 + (g->state != JW_IDLE));
    float sz = 56 + 6 * (float)(combo > 6 ? 6 : combo);
    float s = sz * (1.2f - 0.2f * k);
    ptext(serif, "CASCADE", px, 556, 26, rgba(1, 0.86f, 0.58f, a), 8);
    snprintf(b, sizeof b, "x%d", combo);
    ptext(serif, b, px, 588 - (s - sz) * 0.5f, s, rgba(1, 0.80f + 0.03f * (float)(combo > 6 ? 6 : combo), 0.42f, a), 0);
  }
  /* how to play, for the device in hand */
  cw_prompt_device dev = cw_prompt_device_of(0);
  float hy = 718;
  cw_color hc = rgba(0.93f, 0.90f, 0.84f, 0.92f);
  ptext(sans, "Line up three of a kind.", px, hy, 27, hc, 0);
  hy += 38;
  if (dev == CW_PROMPT_TOUCH || dev == CW_PROMPT_MOUSE) {
    ptext(sans, "Tap a jewel, then its neighbour,", px, hy, 26, hc, 0);
    ptext(sans, "or drag it across.", px, hy + 34, 26, hc, 0);
  } else {
    pprompt(sans, "{move_left|move_right|move_up|move_down}: move   {pick}: pick up", px, hy, 26, hc);
    ptext(sans, "then push it toward its neighbour", px, hy + 36, 26, hc, 0);
  }
  /* a status line just over the button */
  float sy = getenv("JEWELS_HUD_BREAK") ? JW_HINT_Y - 44 : JW_HINT_Y - 36; /* the layout gate's control: the old place */
  if (getenv("JEWELS_HUD_BREAK")) sy = 790;
  float sa = all ? 1 : fminf(1, (float)g->status_t / 20.0f);
  if (status == 1) ptext(sans, "Swap these two.", px, sy, 24, rgba(0.6f, 0.95f, 1, sa), 0);
  if (status == 2) ptext(sans, "No moves left: reshuffled.", px, sy, 24, rgba(0.6f, 0.95f, 1, sa), 0);
  /* the hint button's label (the button itself is drawn on the panel, in the scene) */
  float by = JW_HINT_Y + JW_HINT_H * 0.5f;
  if (dev == CW_PROMPT_PAD || dev == CW_PROMPT_KEYBOARD) pprompt(serif, "{hint}  HINT", px, by - 26, 44, PALE);
  else ptext(serif, "HINT", px, by - 26, 48, PALE, 6);
  lay_check();
  /* the gains, rising from where they cleared, outlined so they read over the stones */
  for (int i = 0; i < fx->npops; i++) {
    const jw_pop *p = &fx->pops[i];
    float k = p->t / 1.6f, a = k < 0.7f ? 1 : 1 - (k - 0.7f) / 0.3f;
    snprintf(b, sizeof b, "+");
    fmt_num(p->value, b + 1, sizeof b - 1);
    float sz = 40 + 8 * fminf(p->power, 5);
    text_on_board(b, p->x, p->y - 30 - 60 * k - 26 * (float)(i % 2), sz, rgba(1, 0.92f, 0.7f, a));
  }
}

static void render(void *u, double alpha) {
  (void)u, (void)alpha;
  jw_sound_update();
  if (G.in_match && cw_shell_hud_shown() && !getenv("JEWELS_NO_HUD")) {
    cw_r2d_begin(NULL);
    hud();
    cw_r2d_end();
  }
  cw_quality_auto_measure(cw_shell_screen_now() == CW_SHELL_PLAY);
  if (JEWELS_BENCH && getenv("JEWELS_BENCH_PRINT")) {
    static int n;
    static double t_last, t_sum;
    double now = cw_time();
    if (t_last > 0) t_sum += (now - t_last) * 1000;
    t_last = now;
    if (++n % 120 == 0) {
      const cw_pass_stat *pst;
      int np = cw_pipeline_pass_stats(cw_pipeline_of(cw_scene_app_tree()), &pst);
      fprintf(stderr, "bench frame_ms=%.2f gpu_total=%.2f", t_sum / 120, cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree())));
      for (int k = 0; k < np; k++)
        if (pst[k].ran && (pst[k].gpu_ms_avg > 0.01f)) fprintf(stderr, " %s=%.2f", pst[k].name, pst[k].gpu_ms_avg);
      fprintf(stderr, " cascade_max=%d clears=%d\n", G.g.cascade_max, G.g.clears);
      t_sum = 0;
    }
  }
  cw_shell_draw();
}

/* ── start up ────────────────────────────────────────────────────────── */

static bool warmup(void *u) {
  (void)u;
  if (!G.built) {
    jw_cuts_build();
    jw_render_init(frame_fn, NULL);
    G.built = true;
    if (!G.in_match) jw_game_new(&G.g, 0x5eed1234u); /* a board to show behind the title */
    return false;
  }
  if (!G.baked) {
    for (int i = 0; i < 4 && !G.baked; i++) G.baked = jw_render_bake_step();
    { static int steps; cw_loading_set(++steps, 22, CW_LOAD_TEXTURES); }
    return G.baked;
  }
  return true;
}

static void init(void *u) {
  (void)u;
  cw_project_load_input();
  G.left = cw_action_find("move_left"), G.right = cw_action_find("move_right"), G.up = cw_action_find("move_up"), G.down = cw_action_find("move_down");
  G.pick = cw_action_find("pick"), G.hint = cw_action_find("hint");
  serif = cw_font_load("fonts/Cinzel.ttf", &(cw_font_desc){ .sdf_padding = 10 }); /* room for the outlines over the board */
  sans = cw_ui_font_sans();
  cw_prompt_set_font(sans);
  cw_shell_init(&(cw_shell_desc){ .title = "JEWELS", .subtitle = "a calm match-three", .max_players = 1, .game_save_size = sizeof(save_data),
                                  .game_save_version = 1, .start = start, .quit = shell_quit, .sound = shell_sound, .screen_changed = shell_screen,
                                  .font = serif, .accent = (cw_color){ 0.98f, 0.82f, 0.45f, 1 } });
  cw_quality_full_resolution(true);
  if (getenv("JEWELS_QUALITY")) cw_quality_set((cw_quality)CW_CLAMP(atoi(getenv("JEWELS_QUALITY")), 0, CW_QUALITY_COUNT - 1), true);
  jw_sound_init();
  jw_fx_init(&G.fx);
  cw_post_settings *ps = cw_pipeline_settings(cw_pipeline_of(cw_scene_app_tree()));
  ps->hdr = true;
  ps->tonemap = getenv("JEWELS_AGX") ? CW_TONEMAP_AGX : CW_TONEMAP_NEUTRAL;
  ps->exposure = 1.0f;
  ps->bloom = false, ps->bloom_intensity = 0.045f, ps->bloom_threshold = 1.6f, ps->bloom_knee = 0.5f, ps->bloom_radius = 0.7f, ps->bloom_levels = 4;
  ps->vignette = true, ps->vignette_intensity = 0.28f, ps->vignette_smoothness = 0.6f;
  ps->msaa = 0;
  ps->taa = getenv("JEWELS_NO_TAA") == NULL; /* High and Ultra: the stones are traced once a pixel and resolved over frames */
  ps->aa = CW_AA_NONE;
  static const char *names[] = { "game.score", "game.best", "game.moves", "game.state", "game.clears", "game.cascade_max", "game.reshuffles",
                                 "game.hints", "game.illegal", "game.picks", "game.cur_x", "game.cur_y", "game.sel", "game.pad_swaps",
                                 "game.touch_swaps", "game.drag_swaps", "game.tap_swaps", "game.largest", "fx.finales", "fx.rumbles",
                                 "game.playing", "fx.power_x100", "fx.max_x100", "fx.sparks", "fx.shards" };
  int32_t *ptrs[] = { &G.score, &G.best, &G.moves, &G.state, &G.clears, &G.cascade_max, &G.reshuffles, &G.hints, &G.illegal, &G.picks, &G.cur_x,
                      &G.cur_y, &G.sel, &G.pad_swaps, &G.touch_swaps, &G.drag_swaps, &G.tap_swaps, &G.largest, &G.finales, &G.rumbles, &G.playing,
                      &G.fx_power_x100, &G.fx_max_x100, &G.fx_sparks, &G.fx_shards };
  for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++) cw_debug_i32(names[i], ptrs[i]);
  cw_debug_field("fx.step_x100", G.fx_steps_x100, CW_DBG_I32, 6);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_field("game.grid", G.g.grid.c, CW_DBG_U8, JW_W * JW_H); /* the board, row by row (tests/jewels-inputs.mjs plans its moves from it) */
  cw_debug_u32("game.rng", &G.g.rng.s);
  cw_debug_i32("hud.overlaps", &hud_overlaps);
#if JEWELS_AUTOPILOT
  cw_set_raw_input_filter(bot_filter, NULL);
  cw_shell_start(1);
#else
  (void)bot_filter;
  jw_sound_music(true);
#endif
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Jewels", .width = 1920, .height = 1080, .debug = true, .init = init, .warmup = warmup, .fixed_update = fixed_update,
                             .render = render, .touch_pointer = false };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/main.cwscene", &app);
}
