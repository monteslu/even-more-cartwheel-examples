/* The song playing: its four tracks started on one mixer frame, the song clock read from the audio, and the
 * kick, clap and bass tracks ducked on a miss (docs/DESIGN.md "Timing in the game"). */
#ifndef BEATSYNC_MUSIC_H
#define BEATSYNC_MUSIC_H

#include <stdbool.h>
#include <stdint.h>

#include "song.h"

void music_init(void); /* after the shell (which starts the mixer): the buses */
/* Starts `s` lead_ms from now at song time from_ms (lead_ms > 0: the clock counts up from -lead_ms to from_ms).
 * volume: the whole song's level (the song select's preview plays softer). */
void music_play(const song *s, int32_t from_ms, int32_t lead_ms, float volume);
void music_stop(float fade_s);
void music_pause(bool paused);
/* Once a tick (dt seconds): moves the song clock on and the ducks. */
void music_tick(float dt);
/* The song clock in milliseconds: the audio's position, smoothed between mixer blocks. Before the song starts it
 * counts up to the start; after the end it keeps counting. */
double music_ms(void);
double music_raw_ms(void); /* the audio's position as of the last mixed block (for the clock check) */
bool music_playing(void);
bool music_ended(void);    /* every track has played to its end */
const song *music_song(void);
/* A miss ducks the lane's track (a short fade down with a low-pass sweep) until a hit brings it back. */
void music_duck(int lane, bool ducked);
float music_lane_level(int lane); /* 0..1, the track's level as it is heard (debug field, visuals) */
void music_set_fade(float level, float seconds); /* the whole song (the failed song's fade out) */
void music_register_debug(void); /* the `solo` debug command */

#endif
