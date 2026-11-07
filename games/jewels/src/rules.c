/* The match-three rules (rules.h): a port of the original Lua rules, function for function and draw for draw. */
#include "rules.h"

#include <stdlib.h>
#include <string.h>

/* a test control that must fail (tools/rules_check.c, JEWELS_RULES_BREAK); 0 in the game */
int jw_rules_break;
/* the kinds a refill draws from: JW_KINDS in the game; the perf bench narrows it so cascades run long */
int jw_refill_kinds = JW_KINDS;

void jw_rng_seed(jw_rng *r, uint32_t seed) { r->s = seed ? seed : 0x9e3779b9u; }
uint32_t jw_rng_u32(jw_rng *r) {
  uint32_t x = r->s;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return r->s = x;
}
int jw_rng_int(jw_rng *r, int n) { return (int)(jw_rng_u32(r) % (uint32_t)n) + 1; }
float jw_rng_float(jw_rng *r) { return (float)(jw_rng_u32(r) / 4294967296.0); }

bool jw_inside(int x, int y) { return x >= 0 && x < JW_W && y >= 0 && y < JW_H; }

/* one line of cells (a row or a column): runs of 3+ of a non-empty kind */
static void scan(const jw_grid *g, jw_matches *m, bool horiz, int line) {
  int len = horiz ? JW_W : JW_H, run = 1;
  for (int i = 1; i <= len; i++) {
    int a = -1, b;
    if (i < len) a = horiz ? g->c[line][i] : g->c[i][line];
    b = horiz ? g->c[line][i - 1] : g->c[i - 1][line];
    if (a >= 0 && a == b && a != 0) {
      run++;
      continue;
    }
    if (run >= 3) {
      for (int j = i - run; j < i; j++) {
        int x = horiz ? j : line, y = horiz ? line : j;
        if (!m->hit[y][x]) m->hit[y][x] = 1, m->nhit++;
      }
      if (m->nruns < JW_MAX_RUNS) {
        jw_run *r = &m->runs[m->nruns++];
        r->n = (uint8_t)run, r->horiz = horiz, r->x = (uint8_t)(horiz ? i - run : line), r->y = (uint8_t)(horiz ? line : i - run);
      }
    }
    run = 1;
  }
}

void jw_find_matches(const jw_grid *g, jw_matches *m) {
  memset(m, 0, sizeof *m);
  for (int y = 0; y < JW_H; y++) scan(g, m, true, y);
  for (int x = 0; x < JW_W; x++) scan(g, m, false, x);
}

bool jw_any_match(const jw_grid *g) {
  jw_matches m;
  jw_find_matches(g, &m);
  return m.nhit > 0;
}

void jw_swap(jw_grid *g, int x1, int y1, int x2, int y2) {
  uint8_t t = g->c[y1][x1];
  g->c[y1][x1] = g->c[y2][x2];
  g->c[y2][x2] = t;
}

bool jw_is_legal(jw_grid *g, int x1, int y1, int x2, int y2) {
  if (!jw_inside(x1, y1) || !jw_inside(x2, y2)) return false;
  if (abs(x1 - x2) + abs(y1 - y2) != 1) return false;
  jw_swap(g, x1, y1, x2, y2);
  bool ok = jw_any_match(g);
  jw_swap(g, x1, y1, x2, y2);
  return ok;
}

int jw_legal_moves(jw_grid *g, jw_move *out) {
  int n = 0;
  for (int y = 0; y < JW_H; y++)
    for (int x = 0; x < JW_W; x++) {
      if (x < JW_W - 1 && jw_is_legal(g, x, y, x + 1, y)) {
        if (out) out[n] = (jw_move){ (int8_t)x, (int8_t)y, 1, 0 };
        n++;
      }
      if (y < JW_H - 1 && jw_is_legal(g, x, y, x, y + 1)) {
        if (out) out[n] = (jw_move){ (int8_t)x, (int8_t)y, 0, 1 };
        n++;
      }
    }
  return n;
}

void jw_collapse_grid(jw_grid *g, const jw_matches *m, jw_rng *r, jw_collapse *out) {
  out->nfell = out->nspawned = 0;
  for (int y = 0; y < JW_H; y++)
    for (int x = 0; x < JW_W; x++)
      if (m->hit[y][x]) g->c[y][x] = 0;
  for (int x = 0; x < JW_W; x++) {
    int write = JW_H - 1; /* bottom up, packing the survivors down */
    for (int y = JW_H - 1; y >= 0; y--) {
      if (!g->c[y][x]) continue;
      if (write != y) {
        g->c[write][x] = g->c[y][x];
        g->c[y][x] = 0;
        out->fell[out->nfell++] = (jw_fall){ (int8_t)x, (int8_t)y, (int8_t)write };
      }
      write--;
    }
    int above = 0; /* the gap at the top; the highest new jewel starts furthest above */
    for (int y = write; y >= 0; y--) {
      above++;
      g->c[y][x] = (uint8_t)(jw_rules_break == 2 ? JW_KINDS + 1 - jw_rng_int(r, JW_KINDS) : jw_rng_int(r, jw_refill_kinds));
      out->spawned[out->nspawned++] = (jw_spawn){ (int8_t)x, (int8_t)y, (int8_t)g->c[y][x], (int8_t)(-above) };
    }
  }
}

static void new_grid(jw_grid *g, jw_rng *r) {
  for (int y = 0; y < JW_H; y++)
    for (int x = 0; x < JW_W; x++) g->c[y][x] = (uint8_t)jw_rng_int(r, JW_KINDS);
}

void jw_new_board(jw_grid *g, jw_rng *r) {
  for (int attempt = 0; attempt < 200; attempt++) {
    new_grid(g, r);
    jw_matches m;
    for (;;) {
      jw_find_matches(g, &m);
      if (!m.nhit) break;
      for (int y = 0; y < JW_H; y++) /* reroll just the matched cells, in row order */
        for (int x = 0; x < JW_W; x++)
          if (m.hit[y][x]) g->c[y][x] = (uint8_t)jw_rng_int(r, JW_KINDS);
    }
    if (jw_legal_moves(g, NULL) > 0) return;
  }
  /* 200 boards in a row without a move does not happen with six kinds on 10 x 8; keep the last one */
}

bool jw_reshuffle(jw_grid *g, jw_rng *r) {
  uint8_t bag[JW_W * JW_H];
  int n = 0;
  for (int y = 0; y < JW_H; y++)
    for (int x = 0; x < JW_W; x++) bag[n++] = g->c[y][x];
  for (int attempt = 0; attempt < 400; attempt++) {
    for (int i = n; i >= 2; i--) { /* Fisher-Yates, 1-based as the original draws it */
      int j = jw_rng_int(r, i);
      uint8_t t = bag[i - 1];
      bag[i - 1] = bag[j - 1];
      bag[j - 1] = t;
    }
    int i = 0;
    for (int y = 0; y < JW_H; y++)
      for (int x = 0; x < JW_W; x++) g->c[y][x] = bag[i++];
    if (!jw_any_match(g) && jw_legal_moves(g, NULL) > 0) return true;
  }
  jw_new_board(g, r); /* a pathological bag: a fresh board rather than a dead one */
  return false;
}

int64_t jw_run_score(int n, int cascade) {
  if (jw_rules_break == 1 && cascade == 2) cascade = 1;
  int64_t base = 30 * n + 20 * (n > 3 ? n - 3 : 0) * n; /* 3: 90, 4: 200, 5: 450 */
  return base * (cascade + 1) / 2;                       /* x (1 + (cascade - 1) / 2), floored */
}
