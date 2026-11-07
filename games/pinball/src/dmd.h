/* pinball's dot display (dmd.c) */
#ifndef PINBALL_DMD_H
#define PINBALL_DMD_H
#include <stdbool.h>
#include <stdint.h>

#define DMD_W 128
#define DMD_H 32
#define DMD_S 6 /* texture pixels a dot */

typedef struct dmd { uint8_t dot[DMD_H][DMD_W]; } dmd;

void dmd_clear(dmd *d);
void dmd_set(dmd *d, int x, int y, int v);
int dmd_text_width(const char *s, int scale);
void dmd_text(dmd *d, const char *s, int x, int y, int scale, int v);
void dmd_text_c(dmd *d, const char *s, int cx, int y, int scale, int v);
void dmd_rect(dmd *d, int x, int y, int w, int h, int v);
/* into DMD_W*DMD_S x DMD_H*DMD_S RGBA pixels, in the panel's colour (sRGB 0..1) */
void dmd_render(const dmd *d, uint8_t *rgba, const float col[3]);

#endif
