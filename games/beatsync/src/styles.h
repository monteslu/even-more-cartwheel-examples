/* The styles (src/styles.c): how the song is drawn, changing with the music.
 *
 * A style is a look (the game's ASCII pass on or off), a backdrop (backdrop.h), a palette for the rivers and the land,
 * and the field's params (the land's light and ridge, the dark road under each river, the sky, the bloom). While a
 * song plays the scheduler changes style at phrase boundaries (every 8 bars on the song's beat grid) and at a drop,
 * with a fade measured in beats (2, or 1 at a drop, never under half a second). The rivers, notes and gates never
 * fade: only what is round them changes. Each song starts in the neon characters; after that the song's own seed
 * fixes the order, every style once before any repeats, a drop taking a big one (fire, lasers, the grid) when one is
 * left. A player can pin one style instead (styles_set_mode). */
#ifndef BEATSYNC_STYLES_H
#define BEATSYNC_STYLES_H
#include <stdbool.h>

#include <cartwheel/scene.h>

#include "song.h"

enum { STYLE_NEON, STYLE_CLEAN, STYLE_FIRE, STYLE_WATER, STYLE_GRID, STYLE_TERMINAL, STYLE_LASERS, STYLE_STAGE, STYLE_HALF, STYLE_MAZE, STYLE_BIT, STYLE_COUNT };

/* init: the debug requests and fields (before the scene is up) */
void styles_init(void);
/* the scene is up: the backdrops and the post node */
void styles_build(cw_node level);
/* every fixed tick: the song shown (NULL: none), its clock in ms, the tempo played at (1 the song's own), seconds */
void styles_tick(const song *s, double ms, float tempo, float dt);
/* every frame, before the picture is drawn: hands the blend of the two styles to the field, the ASCII pass and the
 * backdrops */
void styles_apply(float vis_t);
/* -1: every style, changing with the song (the default); 0..STYLE_COUNT-1: that one only */
void styles_set_mode(int mode);
int styles_mode(void);
const char *style_title(int i);
const char *style_id(int i);
bool style_held(int i); /* built but out of the rotation and the setting */
#endif
