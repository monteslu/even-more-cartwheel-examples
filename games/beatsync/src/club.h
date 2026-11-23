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

#define CLUB_PLAYERS 2 /* players whose streaks the rivers show */
#define TIER_S 0.8f  /* seconds a multiplier step's band takes to rise up the rivers */
#define DRAIN_S 0.7f /* seconds a broken streak's charge takes to drain */

/* the rivers on screen: the three-lane game (kick, clap, bass), one pair of the two-lane game, or two players' pairs
 * side by side (the left player's pair on the left) */
enum { CLUB_SOLO, CLUB_DRUMS, CLUB_GROOVE, CLUB_DUO_DRUMS_LEFT, CLUB_DUO_GROOVE_LEFT };

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
  /* each player's streak (one player: [0]), drawn on that player's rivers (lane_player) */
  float charge[CLUB_PLAYERS];     /* 0..1: the streak's way to the top multiplier: the banks lit from the gate up that far */
  float tier_t[CLUB_PLAYERS];     /* seconds since the multiplier stepped up (its band rising up the rivers), <0 none */
  float tier_light[CLUB_PLAYERS]; /* that band's light, 0..1 (through its flash meter) */
  float drain_t[CLUB_PLAYERS];    /* seconds since a streak with a multiplier broke, <0 none */
  float drain_from[CLUB_PLAYERS]; /* the charge it had */
  float drain_light[CLUB_PLAYERS];/* the draining light and the falling pieces, 0..1 (through its flash meter) */
  int32_t break_seq[CLUB_PLAYERS];/* counts the breaks: a new value throws the lit bank pieces off */
  int8_t lane_player[LANES];      /* whose streak a lane's river shows */
  float dim;              /* 0..1: the field dims under a menu panel (still moving) */
  float zoom;             /* 1 in play; more brings the camera closer */
  float spread;           /* 1 in play; more moves the rivers apart (the title stands them round its box) */
  float ahead;            /* beats of notes a river shows: more is slower and read further ahead (Easy) */
  float note_light;       /* a note's light, 1 the default: brighter where notes come at most once a beat a river (Easy),
                           * so their passing is never a regular flicker over 3 Hz */
  float t;                /* seconds since start (slow motion) */
  float gap_fill;         /* 0..1: how far through a long wait for notes (the banks fill from the gate up as it passes) */
  float gap_alpha;        /* 0..1: the wait's indicator, swelling in and fading out */
  int layout;             /* CLUB_* : which rivers show and where (they glide from one layout to the next) */
} club_view;

/* What a style asks of the picture (src/styles.c): the rivers, notes and gates are always drawn; a style changes their
 * colours and what is round them. */
typedef struct club_style {
  float pal;             /* 0: the section's palette; 1: lane (a style's own colours, held through every section) */
  float lane[LANES][3];
  float land_tint;       /* 0: the harmony's colour; 1: land_rgb */
  float land_rgb[3];
  float land_body;       /* the land's light (1 as it is; low: a dark skyline against a backdrop) */
  float land_alpha;      /* how much of what is behind the land it covers (1 all: the land as it is; 0 only its ridge) */
  float ridge;           /* a line of light along the land's tops */
  float plate;           /* the dark road under each river, 0..1 opaque */
  float sky;             /* the faint harmony glow above the horizon (1 as it is) */
  float burst;           /* a hit's burst (shards, ring, the gate's flare): 1 as the game draws them; the plain picture
                          * draws them softer, as the characters do (a run of eighths is a burst four times a second) */
  float flow;            /* the rivers' current (1 as the game draws it; the characters read a note better over a quieter
                          * current) */
  float pocket;          /* a dark pocket round each coming note, clearing the current there (0 none, 1 the full pocket) */
  float note_light;      /* the notes' light (1 as the game draws them; a crisp plain picture shows a run of notes
                          * harder than the characters do, so it draws them a little dimmer) */
  float lit;             /* 0: the flat shapes the characters read; 1: the picture's own materials: notes as lit gems with
                          * highlights, glowing banks with bloom, roads of glossy dark glass catching the field's lights */
  float key[3];          /* the field's key light (colour times strength), from above and in front */
  float fill_l[3], fill_r[3]; /* the field's two low lights either side, in the backdrop's colours */
  float sweep;           /* 0..1: how far the low lights sweep across the field (slowly, over bars) */
  float pellets;         /* 0..1: the rivers as corridors: a line of pellets down each in place of the current (the arcade maze) */
  float bank_tint;       /* 0: the banks in their lane's colour; 1: bank_rgb (the arcade maze's blue walls) */
  float bank_rgb[3];
  float land_no_mid;      /* 1: no land between two players' roads (the grid's sun shows there: dark rows riding over it
                          * on the half beat were flashes) */
} club_style;
/* the style fading out (a), the one fading in (b) and how far from a to b (0..1) */
void club_set_style(const club_style *a, const club_style *b, float t);
/* the notes' light times k (1 the game; the readability gate's control dims them) */
void club_set_note_gain(float k);
void club_build(void);         /* once the scene is up (the first render) */
void club_update(const club_view *v);
/* canvas position of an instrument's gate (the hit line on its river), for the HUD */
void club_gate_screen(int lane, float canvas_w, float canvas_h, float *x, float *y);
/* canvas position of the highway's middle and its half width at the gates (the HUD keeps clear of it) */
void club_center_screen(float canvas_w, float canvas_h, float *x, float *y, float *half_w);
/* the river each lane of the three-lane game is drawn as (0 left, 1 middle, 2 right): kick 0, clap 1, bass 2, as the
 * buttons sit. Only the layout gate's control (the `lanes` debug request) changes it, to prove the gate can see a
 * wrong layout */
void club_set_place(const int place[SOLO_LANES]);
/* a lane's river shows (0..1, it fades in and out as the layout changes) */
float club_lane_shown(int lane);
int club_place(int lane);
void club_register_debug(void); /* the `notes` and `notescale` debug requests (the size gate) */
/* the instrument's colour in the palette showing now (the HUD's prompts and judgements match the river) */
void club_lane_rgb(int lane, float rgb[3]);

#endif
