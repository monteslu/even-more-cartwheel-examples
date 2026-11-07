/* The game: the board's rules (rules.h) driven by the player's input, one fixed 60 Hz tick at a time, with the
 * timing of each step of a move. No drawing here: the view reads the state and the event list.
 *
 * Design rules this keeps (they define the game):
 *   - nothing on the board moves unless the player moved it: no clock, nothing falls on a timer, no game over;
 *   - an illegal swap springs back and costs nothing;
 *   - the hint is free, unlimited and on its own button (and its own on-screen button);
 *   - a board with no move left reshuffles itself quietly;
 *   - the score only goes up; a cascade resolves on its own, slowly enough to follow.
 *
 * Input is mirrored from the original: the d-pad moves a cursor; the pick button picks a jewel up, and with one
 * held a direction swaps it that way (or pick again on a neighbour, or on the same jewel to put it down); the
 * hint button shows a move. Touch and mouse: tap a jewel then its neighbour, or press a jewel and drag it toward
 * the neighbour; tap the on-screen hint button. */
#ifndef JEWELS_GAME_H
#define JEWELS_GAME_H

#include "rules.h"

/* the screen layout in canvas pixels (1920 x 1080; the canvas scales to the output) */
#define JW_CELL 120.0f
#define JW_BX 72.0f   /* the board's top-left corner */
#define JW_BY 60.0f
#define JW_PANEL_X 1350.0f
#define JW_PANEL_W 500.0f
#define JW_HINT_X 1380.0f /* the on-screen hint button: finger-sized */
#define JW_HINT_Y 868.0f
#define JW_HINT_W 440.0f
#define JW_HINT_H 128.0f

/* step lengths, in 60 Hz ticks: slow on purpose, so every step can be followed */
#define JW_T_SWAP 13
#define JW_T_BUMP 18
#define JW_T_CLEAR 24
#define JW_T_BURST 5        /* the burst, part way into the clear: the jewel is seen to break, not to vanish */
#define JW_T_FALL_MIN 16
#define JW_T_FALL_ROW 3     /* each row of drop adds this much */
#define JW_T_FALL_MAX 34
#define JW_T_HINT 240

typedef enum { JW_IDLE, JW_SWAP, JW_BUMP, JW_CLEAR, JW_FALL } jw_state;

typedef enum {
  JW_EV_CURSOR,    /* a: x, b: y */
  JW_EV_PICK,      /* a: x, b: y */
  JW_EV_PUTDOWN,
  JW_EV_SWAP,      /* a legal swap began */
  JW_EV_BUMP,      /* an illegal one: it springs back */
  JW_EV_CLEAR,     /* a: cells cleared, b: cascade step, c: gain, d: the group's shape score (see game.c), x/y centroid */
  JW_EV_BURST,     /* one jewel breaks: a: kind, b: cascade step, c: the group's size, d: shape score, x/y cell */
  JW_EV_LAND,      /* the falling jewels landed: a: how many moved */
  JW_EV_SETTLE,    /* the move is over: a: the deepest cascade step, c: the move's total gain */
  JW_EV_RESHUFFLE,
  JW_EV_HINT,      /* a/b: first cell, c/d: the direction */
  JW_EV_NOHINT,
} jw_ev_kind;

typedef struct jw_event { uint8_t kind; int32_t a, b, c, d; float x, y; } jw_event;
#define JW_MAX_EVENTS 256

/* one tick's input: pad edges (already debounced by the input module) and every pointer slot */
#define JW_POINTERS 11 /* the mouse and ten fingers */
typedef struct jw_input {
  bool left, right, up, down, pick, hint;
  bool ptr_down[JW_POINTERS];  /* held this tick */
  bool ptr_tap[JW_POINTERS];   /* went down since the last tick (even if already lifted) */
  float ptr_x[JW_POINTERS], ptr_y[JW_POINTERS]; /* canvas pixels */
} jw_input;

typedef struct jw_game {
  jw_grid grid;
  jw_rng rng;            /* the rules' generator (boards, refills, hints) */
  jw_state state;
  int t, t_end;          /* ticks into the current step, and its length */
  int cur_x, cur_y;
  int sel_x, sel_y;      /* -1: nothing picked up */
  int64_t score, best;
  int moves, cascade, cascade_max_move;
  int64_t move_gain;
  /* the step in flight */
  int sx1, sy1, sx2, sy2;          /* the swap's cells */
  jw_matches clear;                /* the cells clearing */
  int clear_kind[JW_H][JW_W];
  int8_t fall_from[JW_H][JW_W];    /* rows each cell drops (0: still) */
  bool burst_sent;
  /* the hint */
  bool hint_on;
  jw_move hint;
  int hint_t;
  /* touch */
  bool ptr_prev[JW_POINTERS];
  bool drag_on;
  int drag_x, drag_y;
  float drag_px, drag_py;
  int drag_slot;
  /* tallies (debug state, tests) */
  int clears, reshuffles, hints, illegal, picks, cascade_max;
  int pad_swaps, touch_swaps, drag_swaps, tap_swaps;
  int largest_group;
  int status, status_t; /* a line for the HUD: 1 the hint, 2 a reshuffle, 3 no move to hint */
  int64_t last_gain;
  int last_gain_t;
  uint32_t tick;
  /* events since the view last read them */
  jw_event ev[JW_MAX_EVENTS];
  int nev;
} jw_game;

void jw_game_new(jw_game *g, uint32_t seed);
/* every step at once (the rules test and the soak) */
void jw_game_set_instant(bool on);
void jw_game_tick(jw_game *g, const jw_input *in);
/* The cell's look this instant: offset in cells, scale, opacity, spin (radians) and glow (0..1). `alpha` is the
 * part of the next tick already elapsed (render interpolation). */
typedef struct jw_cell_view { float dx, dy, scale, opacity, spin, glow; } jw_cell_view;
jw_cell_view jw_game_cell(const jw_game *g, int x, int y, float alpha);
uint32_t jw_game_hash(const jw_game *g);
/* the cell under a canvas point (false outside the board) */
bool jw_cell_at(float px, float py, int *x, int *y);
static inline float jw_cell_cx(int x) { return JW_BX + (x + 0.5f) * JW_CELL; }
static inline float jw_cell_cy(int y) { return JW_BY + (y + 0.5f) * JW_CELL; }

#endif
