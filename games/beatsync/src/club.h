/* The picture: a step sequencer in neon ASCII (docs/DESIGN.md "Look").
 *
 * The scene is a bold layout of flat shapes in saturated colour on black, drawn by three.c under the scene's Field
 * node; the ASCII pass (ascii.c) turns every cell of it into a character by its brightness, so a brighter shape is a
 * denser character (` .:-=+*#%@`). What the scene shows:
 *   - the sequencer ring: one ring per instrument (kick inner, clap middle, bass outer), 16 steps a bar, turning so
 *     that each note arrives at 12 o'clock exactly on its time, swelling as it comes (+ to # to @);
 *   - the music in the field: a shockwave from the centre on every kick, sparks thrown off by every clap, a bass
 *     waveform along the bottom, a slow field of faint glyphs behind everything from the song's energy;
 *   - how the player is doing: a missed instrument's ring dims to dots until it is back, a streak fills the field. */
#ifndef BEATSYNC_CLUB_H
#define BEATSYNC_CLUB_H

#include <stdbool.h>
#include <stdint.h>

#include "song.h"

enum { NOTE_PENDING, NOTE_HIT, NOTE_MISSED };

typedef struct club_view {
  const song *s;
  const note *notes;      /* the chart on the ring (NULL: none) */
  const uint8_t *state;   /* NOTE_* per note */
  int count;
  double ms;              /* the song clock (what the notes are placed by) */
  float kick, clap, bass; /* the music's pulses, 0..1, as drawn (through their flash meters) */
  int32_t kick_ms, clap_ms; /* song time of the latest kick and clap heard (the shockwave and the sparks start there) */
  float flare[LANES];     /* each instrument's hit flare at 12 o'clock, 0..1 */
  float press[LANES];     /* a button held down */
  float level[LANES];     /* each track's level (a ducked instrument's ring dims) */
  float streak;           /* 0..1: the combo, filling the field */
  float dim;              /* 0..1: the field dims under a menu panel (still moving) */
  float zoom;             /* 1 in play; more brings the camera closer (the title) */
  float t;                /* seconds since start (slow motion) */
} club_view;

void club_build(void);         /* once the scene is up (the first render) */
void club_update(const club_view *v);
/* canvas position of the hit point (12 o'clock) on an instrument's ring, for the HUD */
void club_hit_screen(int lane, float canvas_w, float canvas_h, float *x, float *y);
/* canvas position and radius of the ring's centre (the HUD keeps clear of it) */
void club_center_screen(float canvas_w, float canvas_h, float *x, float *y, float *r);
/* the instrument's colour in the palette showing now (the HUD's prompts and judgements match the ring) */
void club_lane_rgb(int lane, float rgb[3]);

#endif
