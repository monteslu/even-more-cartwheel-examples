/* The backdrops (src/backdrop.c): scenes behind the play field, each faded by a weight (0 gone, 1 full), built in the
 * tree under Level/Backdrop. The play field (club.c) is always in front of them: its rivers, notes and gates never
 * fade, and a style that shows a backdrop lays a dark road under each river (club_style.plate).
 *
 *   BD_FIRE    a field of flames along the bottom of a far wall of heat, embers rising, the air shimmering over it
 *   BD_WATER   a night sea under a low moon, its path of light on the swell, harbour lights along the horizon
 *   BD_GRID    a neon grid floor running to the horizon under a striped sun
 *   BD_LASERS  laser fans from the back of a club, sweeping slowly through haze (never blinking) */
#ifndef BEATSYNC_BACKDROP_H
#define BEATSYNC_BACKDROP_H
#include <cartwheel/scene.h>

enum { BD_NONE, BD_FIRE, BD_WATER, BD_GRID, BD_LASERS, BD_COUNT };

void backdrop_build(cw_node level);
/* every frame: each backdrop's weight, the song's beat (the motion follows the music), seconds of frames shown, the
 * music's energy now (0..1, the other track's loudness) */
void backdrop_update(const float weight[BD_COUNT], float beat, float t, float energy);
const char *backdrop_name(int bd);
#endif
