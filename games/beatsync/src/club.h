/* The picture: a note highway in neon ASCII (docs/DESIGN.md "Look").
 *
 * The scene is a bold layout of flat shapes in saturated colour on black, drawn by three.c under the scene's Field
 * node; the ASCII pass (ascii.c) turns every cell of it into a character by its brightness, so a brighter shape is a
 * denser character (` .:-=+*#%@`). What the scene shows:
 *   - three rivers side by side, as the buttons are: kick left (west), clap middle (south), bass right (east); each a
 *     current of characters streaming down to a gate near the bottom, the notes riding it down to reach the gate
 *     exactly on their time, growing and brightening as they come (+ to # to @);
 *   - the music in the field: a shockwave on every kick, sparks thrown off the clap's gate by every clap, a bass
 *     waveform along the bottom, a slow field of faint glyphs behind everything from the song's energy;
 *   - how the player is doing: a hit blooms the gate and surges its river, a missed instrument's river dims until it
 *     is back, a streak brightens everything. */
#ifndef BEATSYNC_CLUB_H
#define BEATSYNC_CLUB_H

#include <stdbool.h>
#include <stdint.h>

#include "song.h"

enum { NOTE_PENDING, NOTE_HIT, NOTE_MISSED };

typedef struct club_view {
  const song *s;
  const note *notes;      /* the chart on the rivers (NULL: none) */
  const uint8_t *state;   /* NOTE_* per note */
  int count;
  double ms;              /* the song clock (what the notes are placed by) */
  float kick, clap, bass; /* the music's pulses, 0..1, as drawn (through their flash meters) */
  int32_t kick_ms, clap_ms; /* song time of the latest kick and clap heard (the shockwave and the sparks start there) */
  float flare[LANES];     /* each instrument's hit bloom at its gate, 0..1 */
  float press[LANES];     /* a button held down */
  float level[LANES];     /* each track's level (a ducked instrument's river dims) */
  float streak;           /* 0..1: the combo, filling the field */
  float dim;              /* 0..1: the field dims under a menu panel (still moving) */
  float zoom;             /* 1 in play; more brings the camera closer */
  float spread;           /* 1 in play; more moves the rivers apart (the title stands them round its box) */
  float ahead;            /* beats of notes a river shows: more is slower and read further ahead (Easy) */
  float t;                /* seconds since start (slow motion) */
} club_view;

void club_build(void);         /* once the scene is up (the first render) */
void club_update(const club_view *v);
/* canvas position of an instrument's gate (the hit line on its river), for the HUD */
void club_gate_screen(int lane, float canvas_w, float canvas_h, float *x, float *y);
/* canvas position of the highway's middle and its half width at the gates (the HUD keeps clear of it) */
void club_center_screen(float canvas_w, float canvas_h, float *x, float *y, float *half_w);
/* the river each lane is drawn as (0 left, 1 middle, 2 right): kick 0, clap 1, bass 2, as the buttons sit. Only the
 * layout gate's control (the `lanes` debug request) changes it, to prove the gate can see a wrong layout */
void club_set_place(const int place[LANES]);
int club_place(int lane);
/* the instrument's colour in the palette showing now (the HUD's prompts and judgements match the river) */
void club_lane_rgb(int lane, float rgb[3]);

#endif
