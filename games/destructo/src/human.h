/* destructo's human-like player: the playability gate's bot (tools/humancheck.c). It plays as a person new to the game
 * does, from what is on the screen only:
 *
 *   sees      each standing crystal (its glow and the beacon over it), where on its own launcher the aim path points,
 *             the ammunition on the HUD, and where its last shot struck. It never reads a block, a hit point left, a
 *             mass or the solver's state
 *   aims      at the crystal nearest the launcher: it lines the aim path up with it the way the path's first stretch
 *             suggests, which is never exact (an error drawn per shot, across and up/down at the structure), and it
 *             learns from where the last shot struck (a share of the miss taken off the next one)
 *   hands     the d-pad held while the launcher is far off and let go late (a reaction delay), then tapped; it fires
 *             once the aim looks right (to a few milliradians, what a person can see) and has been still a moment
 *   ammo      the shot first; once shots at a crystal have twice brought nothing down, the next kind on the HUD
 *   moves     a crystal hidden behind the structure from where it stands (its glow blocked, its beacon still showing
 *             where it is): it rolls the launcher round toward that side until the glow shows, then aims from there
 *
 * The buttons it holds go through human_buttons (the pad the game reads in main.c) and then the game's own mapping
 * from buttons to the launcher (human_to_input, the same one main.c uses for a person's pad). */
#ifndef DESTRUCTO_HUMAN_H
#define DESTRUCTO_HUMAN_H
#include <stdbool.h>
#include <stdint.h>

#include "game.h"

/* the pad as main.c reads it: d-pad (or left stick) aims, the right stick rolls the launcher round the structure, South
 * fires, West picks the next ammunition. No shoulders, no triggers, no power. */
typedef struct pad_buttons {
  bool left, right, up, down, rx_left, rx_right, south, west;
} pad_buttons;

/* how good the hands are (the gate's newcomer: human_newcomer) */
typedef struct human_skill {
  float across_sd;   /* m: the aim's error across the line of fire at the structure, per shot */
  float height_sd;   /* m: ... and up and down (the path's end is not shown: harder to judge) */
  float learn;       /* share of the last miss taken off the next aim at the same crystal */
  int react;         /* ticks between the launcher reaching the aim and the hand letting go */
  float tol;         /* rad: the aim error a person can no longer see */
} human_skill;
human_skill human_newcomer(void);

typedef struct human_state {
  human_skill skill;
  uint32_t rng;
  int tgt;                     /* the crystal aimed at, -1 none */
  int shots_at[MAXTGT];        /* shots fired at each crystal */
  int dry[MAXTGT];             /* shots in a row at it that brought nothing down */
  float corr_across[MAXTGT], corr_up[MAXTGT]; /* what it learned from the last miss */
  bool planned;
  float want_yaw, want_pitch, want_power;
  int want_ammo;
  t3_vec3 aim_point;           /* where it means the shot to go (the crystal plus its error and correction) */
  t3_vec3 tgt_seen;            /* the crystal as it stood when it fired */
  int still, release_t, release_r; /* ticks the aim has looked right; ticks left before the next tap */
  float hist_yaw[64], hist_pitch[64]; /* what the launcher showed, ticks ago (the reaction delay reads back) */
  int hist_n;
  bool full_power;
  int down_before, awaiting;    /* crystals standing when it fired; a shot whose end it has yet to read */
  int prev_ammo_press;
  bool lob;                    /* a high arc: the crystal is hidden behind the structure and a straight shot found the wall */
  bool moving_round;           /* rolling the launcher round the circle to want_orbit */
  float want_orbit;
  bool moved_for[MAXTGT];      /* it has gone round for this crystal already */
  bool never_moves;            /* tests: the player who never rolls the launcher (the orbit gate's control) */
  int release_o;
} human_state;

void human_reset(human_state *h, human_skill s, uint32_t seed);
/* the pad this tick */
void human_buttons(const game *g, human_state *h, pad_buttons *pad);
/* the pad to the launcher's controls: main.c's mapping for a person's pad (d-pad: turn and raise, right stick: orbit,
 * South: fire, West: ammunition), presses edge-detected against the last tick's pad */
void human_to_input(const pad_buttons *pad, const pad_buttons *last, input *in);

#endif
