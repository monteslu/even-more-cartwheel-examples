/* pinball's sound and music (sound.c) */
#ifndef PINBALL_SOUND_H
#define PINBALL_SOUND_H
#include "game.h"

void sound_init(void);
void sound_events(const game *g);   /* the tick's cues, the balls' rolling */
void sound_menu(int which);         /* 0 move, 1 select, 2 back */
void sound_spin_tick(void);
void sound_silence_balls(void);
void music_start(int style, float root_hz);
void music_level(int level);        /* 0 pad and bass, 1 the beat, 2 the lead, 3 everything */
void music_stop(void);
void music_update(void);            /* every frame: keeps the next notes scheduled */

#endif
