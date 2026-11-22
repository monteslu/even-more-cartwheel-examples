/* The 1-bit looks: every pixel of the 3D picture one of two colours, at the output's full size.
 *
 * One POST pass ("dither", last in the chain, after the tone map, the grade and the lens) in two draws:
 *   1. tone: each pixel from the finished picture (the grey world by its luma, the landmark's neon hues by their
 *      value, so a block reads bright whatever its hue), a light unsharp mask (radius 2 px) and outlines from the
 *      depth (where the depth stops being a plane: silhouettes and creases), then gamma, contrast and bias, into a
 *      half float target at the output's size. A level can lift the landmark against its ground (dither_set_level);
 *   2. quantize: each pixel against a threshold anchored to the screen (so a still camera never crawls), a 64x64
 *      blue noise matrix (void and cluster, made when the cart starts) or the 8x8 ordered (Bayer) matrix, and written
 *      as ink or paper.
 * The scene renders at the output's full size (no scaler). The HUD and the game's own screens draw after it
 * (OVERLAY) in the same two colours (dither_hud_color).
 * GL and WebGPU run the same shader (GLSL and WGSL line for line).
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

/* Register the pass on the tree's pipeline and make the threshold matrices (needs the GPU: warmup or later). */
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
