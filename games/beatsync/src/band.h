/* The band on the rock stage (src/band.c): four players on the stage backdrop's deck, each a skinned character
 * (more-cartwheel-examples' generated cast, MIT) with an instrument and a play loop made here, keyed to the beat. */
#ifndef BEATSYNC_BAND_H
#define BEATSYNC_BAND_H
#include <cartwheel/scene.h>

/* the stage world's root: loads the players and their instruments into it */
void band_build(cw_node stage_root);
/* every frame the stage shows: the beat (the loops play on it), seconds, the music's energy (0..1, slowly) */
void band_update(float beat, float t, float energy);
#endif
