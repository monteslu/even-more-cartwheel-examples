/* The backdrops (src/backdrop.c): whole worlds behind the play field, each a cartwheel Backdrop node of main.cwscene
 * (cartwheel/backdrop.h) with its own .cwscene in backdrops/, its own camera, lights, fog and particles, rendered
 * behind the field and blended in by an opacity. The play field (club.c) is always in front: its rivers, notes and
 * gates never fade, and a style that shows a backdrop lays a road of dark glass under each river (club_style.plate).
 *
 *   BD_FIRE    a canyon of fire walls receding to a burning horizon, a sky of smoke lit from below, an ember storm
 *   BD_SEA     a night sea under a low moon, its path of light on the swell, harbour lights along the horizon
 *   BD_GRID    a neon grid floor running to a striped sun between wire mountains, under stars
 *   BD_LASERS  a club in haze: moving heads sweeping volumetric beams, laser fans from the back, a crowd
 *   BD_STAGE   a rock stage: the band playing on a riser, moving spots in haze, a lit wall, the crowd in front
 *   BD_CLEAN   a lit landscape under an evening sky: the other track as hills riding toward the camera
 *   BD_MAZE    an arcade maze: double neon walls on black seen at a tilt, riding toward the camera, critters wandering it
 *   BD_AURORA .. BD_LAVA  the newer worlds (worlds.h): aurora, space, rain city, night forest, underwater, desert
 *              night, lava, metal
 *
 * At most two show at once (one style fading into the next): the one coming in is drawn over the one going out. */
#ifndef BEATSYNC_BACKDROP_H
#define BEATSYNC_BACKDROP_H
#include <cartwheel/scene.h>

#include "song.h"

enum { BD_NONE, BD_FIRE, BD_SEA, BD_GRID, BD_LASERS, BD_STAGE, BD_CLEAN, BD_MAZE, BD_AURORA, BD_SPACE, BD_CITY, BD_FOREST, BD_DEEP, BD_DUNES, BD_LAVA, BD_METAL, BD_COUNT };

void backdrop_build(cw_node level);
/* every frame: each backdrop's weight (at most two above 0), which one is coming in (drawn on top), the song (NULL
 * none) and its beat (the motion follows the music), seconds shown, the music's energy now (0..1, slowly) */
void backdrop_update(const float weight[BD_COUNT], int top, const song *s, float beat, float t, float energy);
const char *backdrop_name(int bd);
/* backdrops drawn last frame (opacity above 0) */
int backdrop_drawn(void);
#endif
