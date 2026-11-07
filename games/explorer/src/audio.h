/* explorer: synthesised sound effects and music (audio.c). */
#ifndef EXPLORER_AUDIO_H
#define EXPLORER_AUDIO_H

#include "cartwheel/audio.h"

enum {
  SFX_JUMP, SFX_DJUMP, SFX_LAND, SFX_STRIKE, SFX_HIT, SFX_KILL, SFX_BOLT, SFX_DASH, SFX_HURT, SFX_DIE, SFX_PICKUP, SFX_SAVE,
  SFX_BREAK, SFX_POUND, SFX_SPLASH, SFX_STROKE, SFX_GRAPPLE, SFX_CLING, SFX_TELL, SFX_BOSS_ACT, SFX_BOSS_DOWN, SFX_ENEMY_TELL,
  SFX_SHOT, SFX_SECRET, SFX_DOOR, SFX_MOVE, SFX_SELECT, SFX_BACK, SFX_MAP, SFX_COUNT
};
enum { MOOD_GATE, MOOD_ROOTS, MOOD_CAVES, MOOD_DEEP, MOOD_HIGH, MOOD_BLOOM, MOOD_HEART, MOOD_BOSS, MOOD_COUNT };

void audio_init(void);
void sfx(int id, float gain);
void sfx_pitch(int id, float gain, float pitch);
void music_play(int mood); /* -1 stops */
int region_mood(int region);

#endif
