/* The song playing: its four tracks started on one mixer frame, the song clock read from the audio, and the
 * kick, clap and bass tracks punched by a hit and dipped by a miss (docs/DESIGN.md "Timing in the game"). */
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
/* The lanes in play (player true, from the next music_play): a little under the record between hits, punched to full
 * by a hit; a miss drops that lane alone and it recovers over a beat. false (the demo): every lane at full. */
void music_set_player(bool player);
/* a hit or a miss on a lane (LANE_*), or on MUSIC_OTHER: the other track, which the synth lane plays and a
 * three-lane note made from the other stem (a break) acts on */
#define MUSIC_OTHER LANE_SYNTH
/* the lanes someone plays (a bit a LANE_*), from the next music_play: an unplayed lane sits at full like the record;
 * with the synth played the other track is a lane (under the record between hits), else the backing */
void music_set_played(unsigned mask);
/* the chart is playing notes made from the other track (a break): the other track sits lower between hits */
void music_set_other_break(bool on);
void music_hit(int lane);
void music_miss(int lane);
float music_lane_level(int lane); /* 0..1, how far a lane is from a miss (1 none): the river's light */
float music_lane_gain(int lane);  /* the lane's level as it is heard, 0..1 (debug field lane.gain) */
void music_set_fade(float level, float seconds); /* the whole song (the failed song's fade out) */
void music_register_debug(void); /* the `solo` debug command */

#endif
