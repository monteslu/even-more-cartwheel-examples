/* The 1-bit looks: every pixel of the 3D picture one of two colours, at the output's full size.
 *
 * The engine's one-bit dither (cartwheel/look.h, CW_LOOK_DITHER_ORDERED) at the DISPLAY stage: drawn last in the
 * post chain (the POST pass "looks_display", after the tone map, the grade and the lens) on the finished picture, so
 * nothing after it adds a third colour. One dither pixel a screen pixel (output_pixels), against a threshold
 * anchored to the screen (a still camera never crawls): the engine's 64x64 blue noise, or the 8x8 ordered (Bayer)
 * matrix for Dither grid. The tone: the grey world by its luminance (grey_gain, its highlights kept), the landmark's
 * neon hues by their value (hue_value), so a block reads bright whatever its hue, a light unsharp mask, outlines from
 * the depth (silhouettes and creases), then gamma and contrast. A level can lift the landmark against its ground
 * (dither_set_level). The scene renders at the output's full size (no scaler). The HUD and the game's own screens
 * draw after it (OVERLAY) in the same two colours (dither_hud_color).
 *
 * The look is a Settings row ("Look", saved): the values below are its choices, in its order. The run option LOOK
 * (dither | warm | grid | ascii) sets it too, for tests. */
#ifndef DETEXTO_DITHER_H
#define DETEXTO_DITHER_H
#include <cartwheel/render2d.h>
#include <cartwheel/scene.h>
#include <stdbool.h>

enum {
  LOOK_DITHER, /* blue noise, black and white (the default) */
  LOOK_WARM,   /* blue noise, warm white on near black */
  LOOK_GRID,   /* the 8x8 ordered (Bayer) matrix, black and white */
  LOOK_ASCII,  /* the neon characters (ascii.h) */
  LOOK_COUNT
};
extern const char *const LOOK_NAMES[LOOK_COUNT]; /* the Settings row's choices */

/* Make the look and set it on the tree's pipeline (needs the GPU: warmup or later). */
void dither_init(cw_tree *tree);
/* the look in use (the Settings row's value; the shell writes it there) */
int *dither_look_value(void);
int dither_look(void);
void dither_set_look(int look);
/* the run options this file reads changed (LOOK) */
void dither_option_changed(const char *name);
/* a 1-bit look is on (the HUD goes to two colours) */
bool dither_mono(void);
/* the level in view (levels.h index, -1 for none): its tone (a desert's bright ground under a landmark lit from
 * behind takes less of the world's gain and more of the landmark's) */
void dither_set_level(int level);
/* a HUD colour in the look's two colours: light colours take the ink, dark ones the paper, alpha kept. Unchanged
 * under the ASCII look. */
cw_color dither_hud_color(cw_color c);
/* the ink and the paper (LOOK_WARM: warm white on near black) */
cw_color dither_ink(void);
cw_color dither_paper(void);
#endif
