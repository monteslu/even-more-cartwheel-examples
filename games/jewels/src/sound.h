/* jewels' sound: every effect and the music made in code at load (no audio files). */
#ifndef JEWELS_SOUND_H
#define JEWELS_SOUND_H

#include "game.h"

void jw_sound_init(void);
void jw_sound_events(const jw_game *g);
void jw_sound_menu(int what); /* 0 move, 1 select, 2 back */
void jw_sound_music(bool on);
void jw_sound_update(void);

#endif
