/* The songs: song.json (docs/DESIGN.md "The song package") read into charts the game plays.
 *
 * Every songs/<id>/ is baked in at build time (CMakeLists.txt writes the list, song_list.c); a song's facts and
 * charts load from its song.json at start, its four tracks stream when it plays. */
#ifndef BEATSYNC_SONG_H
#define BEATSYNC_SONG_H

#include <stdbool.h>
#include <stdint.h>

/* the parts a player plays: the one-player game's three lanes (kick, clap, bass) and the synth, the other track's
 * onsets, which the two-lane game plays (docs/DESIGN.md "Two lanes") */
enum { LANE_KICK, LANE_CLAP, LANE_BASS, LANE_SYNTH, LANES };
#define SOLO_LANES 3 /* the one-player three-lane game: kick, clap, bass */
/* the two-lane game's pairs: the drums (kick and clap, the hats on the clap) and the groove (bass and synth) */
enum { PAIR_DRUMS, PAIR_GROOVE, PAIRS };
enum { DIFF_EASY, DIFF_MEDIUM, DIFF_HARD, DIFFS };
/* the four tracks every song has, then the vocals, which a song may have (a cart of songs with their vocals: they
 * play as recorded, never reacting to the player; song.has_vocals) */
enum { TRACK_KICK, TRACK_CLAP, TRACK_BASS, TRACK_OTHER, TRACKS, TRACK_VOCALS = TRACKS, ALL_TRACKS };

#define MAX_SONGS 512 /* songs a cart can list (songs/index.txt can grow without a rebuild) */
#define MAX_SECTIONS 64

typedef struct note {
  int32_t ms;   /* its time in the song, milliseconds */
  int16_t step; /* its 16th from beat 0 */
  uint8_t lane; /* LANE_* */
  uint8_t other; /* made from the other stem (a break note): its hit and miss act on the other track */
} note;

typedef enum { SEC_INTRO, SEC_BUILD, SEC_DROP, SEC_BREAK, SEC_OUTRO, SEC_OTHER } section_kind;

/* What the picture reads from the other track (song.json "look", tools/lib/look.mjs), a value a beat, ready to draw:
 * the land beside the rivers rises with it, takes its harmony's colour, carries its stabs and lights a crest where the
 * harmony moves, so a player sees the music coming. A song without a look gets a quiet flat land. */
typedef struct song_look {
  int beats;          /* values a beat (0: the song has no look) */
  float *height;      /* 0..1: the other track's loudness, against the song's own quiet and loud beats */
  float *hue;         /* degrees: the harmony's colour, one held from change to change, spread over the song's own range */
  float *bright;      /* 0..1: its tone, dark pad to bright stab */
  float *tonal;       /* 0..1: how clearly one harmony sounds */
  uint8_t *change;    /* 1 on a beat where the harmony moves */
  float *band;        /* bass, mid, treble a HALF beat (3 each), against each band's song average: 1 its usual energy */
  float *stab_beat;   /* the other track's attacks: beat positions, ascending */
  uint8_t *stab_str;  /* their strength, 1..9 */
  int stabs;
} song_look;

typedef struct song {
  char id[48], title[64], artist[64];
  char track[ALL_TRACKS][96]; /* asset paths of the four tracks and the vocals ("": none) */
  bool has_vocals;
  float bpm, length_s;
  int32_t *beat_ms; /* every beat */
  int beats, downbeat;
  struct { int32_t ms; section_kind kind; } section[MAX_SECTIONS];
  int sections;
  note *chart[DIFFS];        /* the three-lane game (song.json "charts") */
  int notes[DIFFS];
  note *pair_chart[PAIRS][DIFFS]; /* the two-lane game, a pair's two parts (song.json "pairs"; lanes are LANE_*) */
  int pair_notes[PAIRS][DIFFS];
  uint32_t hash; /* of the id: the save keys on it */
  int32_t preview_ms; /* where the song select's preview starts: the first drop, else a third in */
  song_look look;
} song;

extern const char *const SONG_IDS[]; /* song_list.c, written by CMakeLists.txt */
extern const int SONG_COUNT;

/* Loads every song's song.json; returns how many loaded (a broken one is logged and left out). The songs are every
 * songs/<id>/song.json among the cart's assets (the host lists them: a cart folder gets a song by having its folder
 * dropped in, no rebuild), in songs/order.txt's order then by artist and title; with no listing, the songs the build
 * found (song_list.c). */
int songs_load(void);
int songs_count(void);
const song *song_get(int i);
const char *diff_name(int d);
const char *pair_name(int p); /* "drums", "groove" */
/* the two lanes of a pair, left then right */
void pair_lanes(int p, int out[2]);
/* the beat phase at a time: beats since the first (fractional), from the song's tracked beats */
float song_beat_at(const song *s, float ms);
/* the look at a beat position (clamped to the song): height, hue, bright, tonal; a song without a look gives a quiet land */
void song_look_at(const song *s, float beat, float *height, float *hue, float *bright, float *tonal);
/* the strongest stab in [b0, b1) (0: none), and whether the harmony moves on a beat in [b0, b1) */
int song_stab_in(const song *s, float b0, float b1);
bool song_change_in(const song *s, float b0, float b1);
/* bass, mid and treble at a beat position (1 each band's usual energy; a song without a look gives 0.4) */
void song_bands_at(const song *s, float beat, float out[3]);

#endif
