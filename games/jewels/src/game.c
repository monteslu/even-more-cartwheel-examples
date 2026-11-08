/* The game (game.h): the original's move pipeline and input handling, step for step, on fixed ticks. */
#include "game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* instant: every step takes no time (the rules test and the soak); set by jw_game_set_instant */
static bool instant_mode;
void jw_game_set_instant(bool on) { instant_mode = on; }

static void emit(jw_game *g, int kind, int a, int b, int c, int d, float x, float y) {
  if (g->nev >= JW_MAX_EVENTS) return;
  g->ev[g->nev++] = (jw_event){ (uint8_t)kind, a, b, c, d, x, y };
}

static void resolve(jw_game *g);
static void end_clear(jw_game *g);

static int fall_ticks(int rows) {
  int t = JW_T_FALL_MIN + JW_T_FALL_ROW * (rows > 0 ? rows - 1 : 0);
  return t > JW_T_FALL_MAX ? JW_T_FALL_MAX : t;
}

/* the group's shape: cells in a crossing of a row run and a column run (an L, a T or a plus) count extra, and
 * so do runs longer than three */
static int shape_score(const jw_matches *m) {
  uint8_t h[JW_H][JW_W] = { 0 }, v[JW_H][JW_W] = { 0 };
  int longest = 0;
  for (int i = 0; i < m->nruns; i++) {
    const jw_run *r = &m->runs[i];
    if (r->n > longest) longest = r->n;
    for (int k = 0; k < r->n; k++) {
      if (r->horiz) h[r->y][r->x + k] = 1;
      else v[r->y + k][r->x] = 1;
    }
  }
  int cross = 0;
  for (int y = 0; y < JW_H; y++)
    for (int x = 0; x < JW_W; x++) cross += h[y][x] && v[y][x];
  return (longest - 3) * 2 + cross * 3 + (m->nruns > 1 ? m->nruns - 1 : 0);
}

/* clear the matched cells: the jewels flare and break, then the board collapses */
static void do_clear(jw_game *g, const jw_matches *m) {
  g->state = JW_CLEAR, g->t = 0, g->t_end = instant_mode ? 0 : JW_T_CLEAR;
  g->cascade++;
  if (g->cascade > g->cascade_max_move) g->cascade_max_move = g->cascade;
  if (g->cascade > g->cascade_max) g->cascade_max = g->cascade;
  int64_t gain = 0;
  for (int i = 0; i < m->nruns; i++) gain += jw_run_score(m->runs[i].n, g->cascade);
  g->clear = *m;
  float sx = 0, sy = 0;
  for (int y = 0; y < JW_H; y++)
    for (int x = 0; x < JW_W; x++) {
      g->clear_kind[y][x] = m->hit[y][x] ? g->grid.c[y][x] : 0;
      if (m->hit[y][x]) sx += jw_cell_cx(x), sy += jw_cell_cy(y);
    }
  g->score += gain;
  if (g->score > g->best) g->best = g->score;
  g->move_gain += gain;
  g->last_gain = gain, g->last_gain_t = 60;
  g->clears++;
  if (m->nhit > g->largest_group) g->largest_group = m->nhit;
  g->burst_sent = false;
  emit(g, JW_EV_CLEAR, m->nhit, g->cascade, (int32_t)gain, shape_score(m), sx / (float)m->nhit, sy / (float)m->nhit);
  if (instant_mode) end_clear(g); /* the step ends at once */
}

static void send_bursts(jw_game *g) {
  if (g->burst_sent) return;
  g->burst_sent = true;
  int shape = shape_score(&g->clear);
  for (int y = 0; y < JW_H; y++)
    for (int x = 0; x < JW_W; x++)
      if (g->clear.hit[y][x]) emit(g, JW_EV_BURST, g->clear_kind[y][x], g->cascade, g->clear.nhit, shape, jw_cell_cx(x), jw_cell_cy(y));
}

/* the clear's animation is over: collapse, and the survivors and the new jewels fall */
/* the perf bench: refills drawn from four kinds until this cascade step, so long cascades of big clears come
 * every move (0 in the game) */
int jw_bench_cascade = 0;

static void end_clear(jw_game *g) {
  send_bursts(g);
  if (jw_bench_cascade) jw_refill_kinds = g->cascade < jw_bench_cascade ? 4 : JW_KINDS;
  jw_collapse col;
  jw_collapse_grid(&g->grid, &g->clear, &g->rng, &col);
  memset(g->fall_from, 0, sizeof g->fall_from);
  int longest = 0;
  for (int i = 0; i < col.nfell; i++) {
    int rows = col.fell[i].to_y - col.fell[i].from_y;
    g->fall_from[col.fell[i].to_y][col.fell[i].x] = (int8_t)rows;
    if (fall_ticks(rows) > longest) longest = fall_ticks(rows);
  }
  for (int i = 0; i < col.nspawned; i++) {
    int rows = col.spawned[i].y - col.spawned[i].from_y;
    g->fall_from[col.spawned[i].y][col.spawned[i].x] = (int8_t)rows;
    if (fall_ticks(rows) > longest) longest = fall_ticks(rows);
  }
  memset(&g->clear, 0, sizeof g->clear);
  g->state = JW_FALL, g->t = 0, g->t_end = instant_mode ? 0 : longest + 4; /* + a small settle at the landing */
  emit(g, JW_EV_LAND, col.nfell + col.nspawned, 0, 0, 0, 0, 0);
  if (instant_mode) {
    memset(g->fall_from, 0, sizeof g->fall_from);
    resolve(g);
  }
}

/* look at the board: a match clears (and the cascade goes on); none ends the move */
static void resolve(jw_game *g) {
  jw_matches m;
  jw_find_matches(&g->grid, &m);
  if (m.nhit) {
    do_clear(g, &m);
    return;
  }
  int deepest = g->cascade_max_move;
  int64_t gained = g->move_gain;
  g->cascade = 0;
  if (jw_legal_moves(&g->grid, NULL) == 0) {
    /* never a game over: the same jewels, rearranged */
    jw_reshuffle(&g->grid, &g->rng);
    g->reshuffles++;
    g->status = 2, g->status_t = 180;
    emit(g, JW_EV_RESHUFFLE, 0, 0, 0, 0, 0, 0);
    if (jw_any_match(&g->grid)) {
      resolve(g);
      return;
    }
  }
  g->state = JW_IDLE, g->t = g->t_end = 0;
  g->hint_on = false, g->hint_t = 0;
  if (deepest > 0) emit(g, JW_EV_SETTLE, deepest, 0, (int32_t)gained, 0, 0, 0);
  g->cascade_max_move = 0, g->move_gain = 0;
}

static void try_swap(jw_game *g, int x1, int y1, int x2, int y2) {
  if (g->state != JW_IDLE) return;
  if (!jw_inside(x1, y1) || !jw_inside(x2, y2)) return;
  if (abs(x1 - x2) + abs(y1 - y2) != 1) return;
  bool legal = jw_is_legal(&g->grid, x1, y1, x2, y2);
  g->sx1 = x1, g->sy1 = y1, g->sx2 = x2, g->sy2 = y2;
  g->sel_x = g->sel_y = -1;
  g->t = 0;
  if (legal) {
    jw_swap(&g->grid, x1, y1, x2, y2);
    g->state = JW_SWAP, g->t_end = instant_mode ? 0 : JW_T_SWAP;
    emit(g, JW_EV_SWAP, x1, y1, x2, y2, 0, 0);
    if (instant_mode) g->moves++, resolve(g);
  } else {
    /* it springs back: no penalty, no move counted */
    g->state = JW_BUMP, g->t_end = instant_mode ? 0 : JW_T_BUMP;
    g->illegal++;
    emit(g, JW_EV_BUMP, x1, y1, x2, y2, 0, 0);
    if (instant_mode) g->state = JW_IDLE;
  }
}

static void show_hint(jw_game *g) {
  jw_move ms[JW_MAX_MOVES];
  int n = jw_legal_moves(&g->grid, ms);
  if (n == 0) {
    emit(g, JW_EV_NOHINT, 0, 0, 0, 0, 0, 0);
    return;
  }
  extern int jw_rules_break;
  g->hint = ms[jw_rng_int(&g->rng, n) - 1];
  if (jw_rules_break == 3) g->hint = ms[0];
  g->hint_on = true, g->hint_t = JW_T_HINT;
  g->hints++;
  g->status = 1, g->status_t = 120;
  emit(g, JW_EV_HINT, g->hint.x, g->hint.y, g->hint.dx, g->hint.dy, 0, 0);
}

void jw_game_new(jw_game *g, uint32_t seed) {
  int64_t best = g->best;
  memset(g, 0, sizeof *g);
  g->best = best;
  jw_rng_seed(&g->rng, seed);
  jw_new_board(&g->grid, &g->rng);
  g->cur_x = 4, g->cur_y = 3; /* the original's (5, 4), counted from 1 */
  g->sel_x = g->sel_y = -1;
  g->drag_slot = -1;
}

bool jw_cell_at(float px, float py, int *x, int *y) {
  int gx = (int)floorf((px - JW_BX) / JW_CELL), gy = (int)floorf((py - JW_BY) / JW_CELL);
  if (!jw_inside(gx, gy)) return false;
  *x = gx, *y = gy;
  return true;
}

static bool in_hint_button(float x, float y) { return x >= JW_HINT_X && x < JW_HINT_X + JW_HINT_W && y >= JW_HINT_Y && y < JW_HINT_Y + JW_HINT_H; }

static void pick(jw_game *g, int x, int y) {
  g->sel_x = x, g->sel_y = y;
  g->picks++;
  emit(g, JW_EV_PICK, x, y, 0, 0, 0, 0);
}

static void end_step(jw_game *g) {
  switch (g->state) {
  case JW_SWAP:
    g->moves++;
    resolve(g);
    break;
  case JW_BUMP: g->state = JW_IDLE; break;
  case JW_CLEAR: end_clear(g); break;
  case JW_FALL:
    memset(g->fall_from, 0, sizeof g->fall_from);
    resolve(g);
    break;
  default: break;
  }
}

void jw_game_tick(jw_game *g, const jw_input *in) {
  g->tick++;
  /* the step in flight moves on first, as the original's tweens update before its input is read */
  if (g->state != JW_IDLE) {
    g->t++;
    if (g->state == JW_CLEAR && g->t >= JW_T_BURST) send_bursts(g);
    if (g->t >= g->t_end) end_step(g);
  }
  if (g->status_t > 0 && --g->status_t == 0) g->status = 0;
  if (g->last_gain_t > 0) g->last_gain_t--;

  /* the pad */
  if (g->state == JW_IDLE) {
    int dx = 0, dy = 0;
    if (in->left) dx = -1;
    if (in->right) dx = 1;
    if (in->up) dy = -1;
    if (in->down) dy = 1;
    if (dx || dy) {
      int nx = g->cur_x + dx, ny = g->cur_y + dy;
      nx = nx < 0 ? 0 : nx >= JW_W ? JW_W - 1 : nx;
      ny = ny < 0 ? 0 : ny >= JW_H ? JW_H - 1 : ny;
      if (g->sel_x >= 0) {
        /* a jewel is picked up: the direction is the swap */
        int before = g->moves + g->illegal;
        try_swap(g, g->sel_x, g->sel_y, g->sel_x + dx, g->sel_y + dy);
        if (g->moves + g->illegal != before || g->state != JW_IDLE) g->pad_swaps++;
      }
      g->cur_x = nx, g->cur_y = ny;
      emit(g, JW_EV_CURSOR, nx, ny, 0, 0, 0, 0);
    }
    if (in->pick) {
      if (g->sel_x == g->cur_x && g->sel_y == g->cur_y) {
        g->sel_x = g->sel_y = -1; /* the same jewel again: put it down */
        emit(g, JW_EV_PUTDOWN, g->cur_x, g->cur_y, 0, 0, 0, 0);
      } else if (g->sel_x >= 0 && abs(g->sel_x - g->cur_x) + abs(g->sel_y - g->cur_y) == 1) {
        try_swap(g, g->sel_x, g->sel_y, g->cur_x, g->cur_y);
        g->pad_swaps++;
      } else {
        pick(g, g->cur_x, g->cur_y);
      }
    }
    if (in->hint) show_hint(g);
  }

  /* touch and mouse: every pointer slot. The first one that went down this tick is the tap; the last one held is
   * the drag */
  int click = -1, held = -1;
  for (int s = 0; s < JW_POINTERS; s++) {
    bool down = in->ptr_down[s] || in->ptr_tap[s];
    if (in->ptr_down[s]) held = s;
    if (down && (!g->ptr_prev[s] || (in->ptr_tap[s] && !in->ptr_down[s])) && click < 0) click = s;
    g->ptr_prev[s] = in->ptr_down[s];
  }
  if (click >= 0) {
    float px = in->ptr_x[click], py = in->ptr_y[click];
    int gx, gy;
    if (in_hint_button(px, py)) {
      show_hint(g);
    } else if (g->state == JW_IDLE && jw_cell_at(px, py, &gx, &gy)) {
      g->cur_x = gx, g->cur_y = gy;
      if (g->sel_x == gx && g->sel_y == gy) {
        g->sel_x = g->sel_y = -1;
        emit(g, JW_EV_PUTDOWN, gx, gy, 0, 0, 0, 0);
      } else if (g->sel_x >= 0 && abs(g->sel_x - gx) + abs(g->sel_y - gy) == 1) {
        try_swap(g, g->sel_x, g->sel_y, gx, gy);
        g->touch_swaps++, g->tap_swaps++;
      } else {
        pick(g, gx, gy);
      }
    }
  }
  /* drag: press a jewel and move toward its neighbour */
  if (g->state == JW_IDLE && held >= 0) {
    float hx = in->ptr_x[held], hy = in->ptr_y[held];
    if (!g->drag_on) {
      int gx, gy;
      if (jw_cell_at(hx, hy, &gx, &gy)) g->drag_on = true, g->drag_x = gx, g->drag_y = gy, g->drag_px = hx, g->drag_py = hy;
    } else {
      float ddx = hx - g->drag_px, ddy = hy - g->drag_py;
      if (fabsf(ddx) > JW_CELL * 0.45f || fabsf(ddy) > JW_CELL * 0.45f) {
        int sx = 0, sy = 0;
        if (fabsf(ddx) > fabsf(ddy)) sx = ddx > 0 ? 1 : -1;
        else sy = ddy > 0 ? 1 : -1;
        try_swap(g, g->drag_x, g->drag_y, g->drag_x + sx, g->drag_y + sy);
        g->sel_x = g->sel_y = -1;
        g->drag_on = false;
        g->touch_swaps++, g->drag_swaps++;
      }
    }
  } else if (held < 0) {
    g->drag_on = false;
  }

  /* an asked-for hint fades on its own */
  if (g->hint_on && --g->hint_t <= 0) g->hint_on = false, g->hint_t = 0;
}

static float ease_in_out(float t) { return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) * 0.5f; }
static float ease_out(float t) { float u = 1 - t; return 1 - u * u * u; }

jw_cell_view jw_game_cell(const jw_game *g, int x, int y, float alpha) {
  jw_cell_view v = { 0, 0, 1, 1, 0, 0 };
  float t = (float)g->t + alpha;
  if (g->state == JW_SWAP || g->state == JW_BUMP) {
    bool a = x == g->sx1 && y == g->sy1, b = x == g->sx2 && y == g->sy2;
    if (a || b) {
      float k = fminf(t / (float)(g->t_end ? g->t_end : 1), 1);
      float ddx = (float)(g->sx2 - g->sx1), ddy = (float)(g->sy2 - g->sy1);
      if (b) ddx = -ddx, ddy = -ddy;
      if (g->state == JW_SWAP) {
        float e = 1 - ease_in_out(k);
        v.dx = ddx * e, v.dy = ddy * e;
        v.scale = 1 + 0.08f * sinf(k * 3.14159f) * (a ? 1 : -0.5f); /* the moved jewel lifts a little as it passes */
      } else {
        /* out a third of the way and back, with a soft overshoot */
        float o = k < 0.4f ? ease_out(k / 0.4f) : 1 - ease_in_out((k - 0.4f) / 0.6f);
        o -= (k > 0.75f) ? 0.12f * sinf((k - 0.75f) / 0.25f * 3.14159f) : 0;
        v.dx = ddx * 0.34f * o, v.dy = ddy * 0.34f * o;
      }
    }
  } else if (g->state == JW_CLEAR && g->clear.hit[y][x]) {
    float k = fminf(t / (float)JW_T_CLEAR, 1);
    /* it flares and swells a little, breaks at the burst, and what is left shrinks away fast */
    v.glow = k < 0.25f ? k / 0.25f * 0.7f : 0.7f * (1 - (k - 0.25f) / 0.75f);
    if (k < 0.2f) v.scale = 1 + 0.12f * ease_out(k / 0.2f);
    else v.scale = fmaxf(0.0f, 1.12f * (1 - ease_out(fminf((k - 0.2f) / 0.45f, 1))));
    v.opacity = k < 0.2f ? 1 : fmaxf(0, 1 - (k - 0.2f) / 0.4f);
    v.spin = 2.6f * k * k;
  } else if (g->state == JW_FALL && g->fall_from[y][x]) {
    int rows = g->fall_from[y][x];
    float d = (float)fall_ticks(rows);
    if (t < d) {
      float k = t / d;
      v.dy = -(float)rows * (1 - k * k); /* falls like a weight */
    } else {
      float k = fminf((t - d) / 4.0f, 1); /* and settles */
      v.dy = 0.05f * sinf(k * 3.14159f);
      v.scale = 1 - 0.03f * sinf(k * 3.14159f);
    }
  }
  return v;
}

uint32_t jw_game_hash(const jw_game *g) {
  uint32_t h = 2166136261u;
  for (int y = 0; y < JW_H; y++)
    for (int x = 0; x < JW_W; x++) h = (h ^ g->grid.c[y][x]) * 16777619u;
  h = (h ^ (uint32_t)g->score) * 16777619u;
  h = (h ^ (uint32_t)g->moves) * 16777619u;
  return h;
}
