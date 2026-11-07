/* demolition's sound (sound.c) */
#ifndef DEMOLITION_SOUND_H
#define DEMOLITION_SOUND_H
#include "game.h"

void sound_init(void);
void sound_menu(int which); /* 0 move, 1 accept, 2 back */
void sound_turn(void);
void sound_result(bool won);
void sound_events(const game *g, t3_vec3 cam);
void sound_field(bool on);
void sound_tick(uint32_t tick);

#endif
