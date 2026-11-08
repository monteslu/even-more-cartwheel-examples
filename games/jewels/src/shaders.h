/* The shader sources (shaders.c): GLSL ES 3.00 for GL, WGSL for WebGPU. Each returns a malloc'd string; the gem and
 * mark programs also return their GLSL vertex stage in *vs (WGSL keeps both stages in one module). */
#ifndef JEWELS_SHADERS_H
#define JEWELS_SHADERS_H

#include <stdbool.h>

#include "cuts.h"

#define JW_CAUSTIC_SPAN 2.6f
#define JW_STUDIO_N 1024 /* the studio map: N x N texels of sky, then two rows of lamps */

/* rows of the studio map (RGBA float, JW_STUDIO_N wide, JW_STUDIO_N + 2 rows): the stones' environment */
void jw_studio_rows(float *rgba, int row0, int nrows);

char *jw_shader_tray(bool wgsl);
char *jw_shader_present(bool wgsl); /* the cached tray, with its light laid over it */
char *jw_shader_gem(bool wgsl, const jw_cut *c, char **vs);
char *jw_shader_under(bool wgsl, char **vs);
char *jw_shader_spark(bool wgsl, char **vs);
char *jw_shader_mark(bool wgsl, char **vs);
char *jw_shader_glint(bool wgsl, int stage, char **vs); /* 0 the bright pass, 1 the spokes, 2 over the board (with its vertex stage) */
char *jw_shader_light(bool wgsl, char **vs); /* one light of the moment into the tray's light buffer */
char *jw_shader_grab(bool wgsl);             /* a copy of part of the scene (full-screen) */
char *jw_shader_shock(bool wgsl, char **vs); /* a shockwave: the copy, bent round a ring */

#endif
