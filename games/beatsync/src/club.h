/* The picture: a note highway in neon ASCII (docs/DESIGN.md "Look").
 *
 * The scene is a bold layout of flat shapes in saturated colour on black, drawn by three.c under the scene's Field
 * node; the ASCII pass (ascii.c) turns every cell of it into a character by its brightness, so a brighter shape is a
 * denser character (` .:-=+*#%@`). What the scene shows:
 *   - three rivers side by side, as the buttons are: kick left (west), clap middle (south), bass right (east); each a
 *     current of characters streaming down to a gate near the bottom, the notes riding it down to reach the gate
 *     exactly on their time, small far off and growing as they come, on a highway receding toward a horizon;
 *   - the other track as a land beside the rivers, riding down with the notes: its loudness, bass, mid and treble,
 *     harmony and stabs, so what the music will do is seen coming (the song's look, song.h);
 *   - how the player is doing: a hit blooms the gate and surges its river, a missed instrument's river dims for the
 *     beat its track is down; the streak charges the banks from the gate up, a multiplier step sends a band up the
 *     rivers, a broken streak drains its charge and its pieces fall;
 *   - a long wait for notes: the banks fill with light as it passes. */
#ifndef BEATSYNC_CLUB_H
#define BEATSYNC_CLUB_H

#include <stdbool.h>
#include <stdint.h>

#include "song.h"

#define TIER_S 0.8f  /* seconds a multiplier step's band takes to rise up the rivers */
#define DRAIN_S 0.7f /* seconds a broken streak's charge takes to drain */

enum { NOTE_PENDING, NOTE_HIT, NOTE_MISSED, NOTE_SKIPPED /* the autopilot lets it go by (drawn as pending) */,
       NOTE_GONE /* cut out of the chart (the `hole` request: the wait gate's long wait) */ };

typedef struct club_view {
  const song *s;
  const note *notes;      /* the chart on the rivers (NULL: none) */
  const uint8_t *state;   /* NOTE_* per note */
  int count;
  double ms;              /* the song clock (what the notes are placed by) */
  float flare[LANES];     /* each instrument's hit bloom at its gate, 0..1 */
  int32_t hit_seq[LANES]; /* counts the player's hits per lane: a new value bursts that gate (shards and a ring) */
  uint8_t hit_perfect[LANES]; /* the latest hit in the lane was a Perfect (a bigger burst) */
  float press[LANES];     /* a button held down */
  float level[LANES];     /* each track's level (a ducked instrument's river dims) */
  float charge;           /* 0..1: the streak's way to the top multiplier: the banks lit from the gate up that far */
  float tier_t;           /* seconds since the multiplier stepped up (its band rising up the rivers), <0 none */
  float tier_light;       /* that band's light, 0..1 (through its flash meter) */
  float drain_t;          /* seconds since a streak with a multiplier broke, <0 none */
  float drain_from;       /* the charge it had */
  float drain_light;      /* the draining light and the falling pieces, 0..1 (through its flash meter) */
  int32_t break_seq;      /* counts the breaks: a new value throws the lit bank pieces off */
  float dim;              /* 0..1: the field dims under a menu panel (still moving) */
  float zoom;             /* 1 in play; more brings the camera closer */
  float spread;           /* 1 in play; more moves the rivers apart (the title stands them round its box) */
  float ahead;            /* beats of notes a river shows: more is slower and read further ahead (Easy) */
  float note_light;       /* a note's light, 1 the default: brighter where notes come at most once a beat a river (Easy),
                           * so their passing is never a regular flicker over 3 Hz */
  float t;                /* seconds since start (slow motion) */
  float gap_fill;         /* 0..1: how far through a long wait for notes (the banks fill from the gate up as it passes) */
  float gap_alpha;        /* 0..1: the wait's indicator, swelling in and fading out */
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
void club_register_debug(void); /* the `notes` and `notescale` debug requests (the size gate) */
/* the instrument's colour in the palette showing now (the HUD's prompts and judgements match the river) */
void club_lane_rgb(int lane, float rgb[3]);

#endif
