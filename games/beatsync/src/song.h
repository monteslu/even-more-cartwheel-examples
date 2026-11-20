/* The songs: song.json (docs/DESIGN.md "The song package") read into charts the game plays.
 *
 * Every songs/<id>/ is baked in at build time (CMakeLists.txt writes the list, song_list.c); a song's facts and
 * charts load from its song.json at start, its four tracks stream when it plays. */
#ifndef BEATSYNC_SONG_H
#define BEATSYNC_SONG_H

#include <stdbool.h>
#include <stdint.h>

enum { LANE_KICK, LANE_CLAP, LANE_BASS, LANES };
enum { DIFF_EASY, DIFF_MEDIUM, DIFF_HARD, DIFFS };
enum { TRACK_KICK, TRACK_CLAP, TRACK_BASS, TRACK_OTHER, TRACKS };

#define MAX_SONGS 32
#define MAX_SECTIONS 64

typedef struct note {
  int32_t ms;   /* its time in the song, milliseconds */
  int16_t step; /* its 16th from beat 0 */
  uint8_t lane; /* LANE_* */
  uint8_t other; /* made from the other stem (a break note): its hit and miss act on the other track */
} note;

typedef enum { SEC_INTRO, SEC_BUILD, SEC_DROP, SEC_BREAK, SEC_OUTRO, SEC_OTHER } section_kind;

typedef struct song {
  char id[48], title[64], artist[64];
  char track[TRACKS][96]; /* asset paths of the four tracks */
  float bpm, length_s;
  int32_t *beat_ms; /* every beat */
  int beats, downbeat;
  struct { int32_t ms; section_kind kind; } section[MAX_SECTIONS];
  int sections;
  note *chart[DIFFS];
  int notes[DIFFS];
  uint32_t hash; /* of the id: the save keys on it */
  int32_t preview_ms; /* where the song select's preview starts: the first drop, else a third in */
} song;

extern const char *const SONG_IDS[]; /* song_list.c, written by CMakeLists.txt */
extern const int SONG_COUNT;

/* Loads every listed song's song.json; returns how many loaded (a broken one is logged and left out). */
int songs_load(void);
int songs_count(void);
const song *song_get(int i);
const char *diff_name(int d);
/* the beat phase at a time: beats since the first (fractional), from the song's tracked beats */
float song_beat_at(const song *s, float ms);

#endif
