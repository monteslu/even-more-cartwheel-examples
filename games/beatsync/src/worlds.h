/* The newer backdrop worlds (src/worlds.c), each built into its Backdrop's world root (backdrop.c calls these):
 *
 *   aurora  a snow field under a polar night: aurora curtains rippling across the sky, pines, a far range under snow
 *   space   deep space: a nebula, three depths of stars, a ringed planet, dust streaming past
 *   city    a street at night in the rain: towers lit window by window, neon blade signs, the wet street a mirror
 *   forest  a pine clearing under a full moon: moonlit trunks, shafts of light, low mist, fireflies
 *   deep    underwater: sand under clear blue water, caustics dancing on it, shafts from the surface, kelp, a fish school
 *   dunes   desert night: dunes with sharp lit crests under a low moon, the Milky Way (seen in amber dither)
 *   metal   a hall of brushed and polished metal: steel plates, chrome columns ringed in gold, sweeping lights
 *   lava    a basalt plain split by lava rivers running toward the camera, a volcano pouring lava (seen in half blocks)
 *
 * Every motion is slow (a drift, a sway, a ripple over seconds on the beat's clock); nothing blinks. */
#ifndef BEATSYNC_WORLDS_H
#define BEATSYNC_WORLDS_H
#include <cartwheel/scene.h>

void world_aurora_build(cw_node root);
void world_aurora_update(float beat, float t, float energy);
void world_space_build(cw_node root);
void world_space_update(float beat, float t, float energy);
void world_city_build(cw_node root);
void world_city_update(float beat, float t, float energy);
/* the windows' light and the street's mirror, 1 as built (the newsprint style prints them dimmer, down to 0.3) */
void world_city_windows(float k);
void world_forest_build(cw_node root);
void world_forest_update(float beat, float t, float energy);
void world_deep_build(cw_node root);
void world_deep_update(float beat, float t, float energy);
void world_dunes_build(cw_node root);
void world_dunes_update(float beat, float t, float energy);
void world_metal_build(cw_node root);
void world_metal_update(float beat, float t, float energy);
void world_lava_build(cw_node root);
void world_lava_update(float beat, float t, float energy);
#endif
