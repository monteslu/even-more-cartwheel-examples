/* The shader sources (shaders.c): GLSL ES 3.00 for GL, WGSL for WebGPU. Each returns a malloc'd string; the gem and
 * mark programs also return their GLSL vertex stage in *vs (WGSL keeps both stages in one module). */
#ifndef JEWELS_SHADERS_H
#define JEWELS_SHADERS_H

#include <stdbool.h>

#include "cuts.h"

#define JW_CAUSTIC_SPAN 2.6f

char *jw_shader_tray(bool wgsl);
char *jw_shader_present(bool wgsl); /* the cached tray, with its light laid over it */
char *jw_shader_gem(bool wgsl, const jw_cut *c, char **vs);
char *jw_shader_under(bool wgsl, char **vs);
char *jw_shader_spark(bool wgsl, char **vs);
char *jw_shader_mark(bool wgsl, char **vs);
char *jw_shader_glint(bool wgsl, int stage); /* 0 the bright pass, 1 the spokes, 2 over the picture */

#endif
