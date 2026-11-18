/* The ASCII renderer: every frame of the 3D scene turned into glowing characters on black.
 *
 * One POST pass ("ascii", before the bloom, so the characters glow) in two draws:
 *   1. cells: one texel per character cell. Six sample regions per cell (3 rows x 2 columns) give the cell's shape
 *      vector (how bright each part of the cell is, plus depth edges on the near side), sharpened, then matched to
 *      the nearest of the font's glyph shape vectors (the glyphs are measured the same way when the atlas is made).
 *      A cell keeps the glyph it had last frame unless a new one fits clearly better, so the text does not shimmer.
 *      The cell's colour is the scene's own hue pushed toward neon.
 *   2. text: every pixel draws its cell's glyph from the atlas (the font rasterized at the cell's size) in the
 *      cell's colour, on black.
 * The HUD and the shell draw after the post chain (OVERLAY), clear and untouched.
 * GL and WebGPU run the same shader (GLSL and WGSL line for line). */
#ifndef DETEXTO_ASCII_H
#define DETEXTO_ASCII_H
#include <cartwheel/scene.h>
#include <stdbool.h>

/* Register the pass on the tree's pipeline and load the font (needs the GPU: warmup or later). */
void ascii_init(cw_tree *tree);
/* On or off (off: the plain 3D picture, the control the gates compare with). */
void ascii_set_enabled(bool on);
bool ascii_enabled(void);



/* ── the HUD on clean black ──────────────────────────────────────────────
 * Every HUD element records its rectangle (canvas units, top left origin) while it draws. ascii_hud_end merges
 * the rectangles that touch into boxes, draws each box's neon border, and hands the boxes to the pass: next frame
 * the cells under a box stay black, so HUD text never sits on characters. alpha: how visible the element is (a
 * faded title keeps no box). */
#include <cartwheel/render2d.h>
void ascii_hud_begin(cw_vec2 canvas, float u);
void ascii_hud_rect(float x, float y, float w, float h, float alpha);
/* the same with the box's edge colour (a panel's: a player's colour) */
void ascii_hud_rect_edge(float x, float y, float w, float h, float alpha, cw_color edge);
void ascii_hud_end(void);
/* boxes drawn the same frame (the shell's screens: the game knows their place before the shell draws):
 * ascii_hud_reset, ascii_hud_rect..., ascii_hud_end, then ascii_hud_draw */
void ascii_hud_reset(cw_vec2 canvas, float u);
void ascii_hud_draw(void);
/* a frame with no HUD: no boxes */
void ascii_hud_none(void);
#endif
