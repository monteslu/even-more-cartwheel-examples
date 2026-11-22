/* The 1-bit look: every pixel of the 3D picture black or white (or a tinted pair), at the output's full size.
 *
 * One POST pass ("dither", last in the chain, after the tone map, the grade and the lens) in two draws:
 *   1. tone: each pixel from the finished picture (the grey world by its luma, the landmark's neon hues by their
 *      value, so a block reads bright whatever its hue), a light unsharp mask (radius 2 px) and outlines from the
 *      depth (where the depth stops being a plane: silhouettes and creases), then gamma, contrast and bias, into a
 *      half float target at the output's size;
 *   2. quantize: each pixel against a threshold anchored to the screen (so a still camera never crawls):
 *        LOOK bayer     an 8x8 ordered (Bayer) matrix
 *        LOOK blue      a 64x64 blue noise matrix (void and cluster, made when the cart starts)
 *        LOOK feedback  the blue noise matrix, then each pixel's neighbours' quantization error (5x5, Gaussian,
 *                       the pixel itself left out) added before it is quantized again: one parallel step of error
 *                       diffusion, so edges and gradients sharpen as they do under error diffusion, while every
 *                       pixel still depends only on this frame's picture and its screen position (no crawl, no
 *                       serial scan)
 *   and written as ink or paper.
 * The scene renders at the output's full size (no scaler). The HUD and the shell draw after it (OVERLAY), in the
 * same two colours (dither_hud_color).
 * GL and WebGPU run the same shader (GLSL and WGSL line for line).
 * When cartwheel-engine's look API gains a dither look this pass moves there. */
#ifndef DETEXTO_DITHER_H
#define DETEXTO_DITHER_H
#include <cartwheel/render2d.h>
#include <cartwheel/scene.h>
#include <stdbool.h>

enum { LOOK_ASCII, LOOK_BAYER, LOOK_BLUE, LOOK_FEEDBACK, LOOK_COUNT };
/* the look a run starts with (the run option LOOK changes it) */
#ifndef DETEXTO_DEFAULT_LOOK
#define DETEXTO_DEFAULT_LOOK LOOK_BLUE
#endif

/* Register the pass on the tree's pipeline and make the threshold matrices (needs the GPU: warmup or later). */
void dither_init(cw_tree *tree);
/* the look in use: LOOK_ASCII (the ASCII pass, ascii.h) or a dither method. Read from the run option LOOK
 * (ascii | bayer | blue | feedback) when it changes (dither_option_changed). */
int dither_look(void);
void dither_set_look(int look);
/* the run options this file reads changed (LOOK, DITHER_TINT) */
void dither_option_changed(const char *name);
/* a 1-bit look is on (the HUD goes to two colours) */
bool dither_mono(void);
/* a HUD colour in the look's two colours: light colours take the ink, dark ones the paper, alpha kept. Unchanged
 * under the ASCII look. */
cw_color dither_hud_color(cw_color c);
/* the ink and the paper (DITHER_TINT 1: warm white on black) */
cw_color dither_ink(void);
cw_color dither_paper(void);
#endif
