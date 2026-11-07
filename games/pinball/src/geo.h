/* pinball's mesh builder: boxes, cylinders, tubes, extruded outlines and ribbons appended into one geometry (world
 * metres, y up), so a table's walls of one material are one draw. */
#ifndef PINBALL_GEO_H
#define PINBALL_GEO_H
#include <stdbool.h>

#include "cartwheel/render3d.h"

typedef struct gb {
  float *pos, *nrm, *uv;
  unsigned *idx;
  int nv, ni, cv, ci;
} gb;

void gb_free(gb *b);
/* a box: centre, half sizes (x, y, z), turned yaw radians about y */
void gb_box(gb *b, float cx, float cy, float cz, float hx, float hy, float hz, float yaw);
/* a cylinder standing on (cx, y0, cz) to y1, radius r, segs sides, capped top */
void gb_cyl(gb *b, float cx, float y0, float cz, float y1, float r, int segs, bool cap);
/* a ring (torus-ish band): a short cylinder shell with rounded look, inner and outer radius, from y0 to y1 */
void gb_band(gb *b, float cx, float y0, float cz, float y1, float r_in, float r_out, int segs);
/* a tube of radius r along points (x, y, z), segs around */
void gb_tube(gb *b, const float (*p)[3], int n, float r, int segs);
/* a flat outline (x, z pairs, counter-clockwise seen from above, convex or star-shaped about its first point's
 * centroid) extruded from y0 to y1 */
void gb_prism(gb *b, const float (*p)[2], int n, float y0, float y1);
/* a ramp's trough along points: a floor w wide and walls h high on both sides */
void gb_trough(gb *b, const float (*p)[3], int n, float w, float h);
/* a sphere cap (a dome) at (cx, cy, cz), radius r, upper half */
void gb_dome(gb *b, float cx, float cy, float cz, float r, int segs);
t3_geometry *gb_geometry(gb *b); /* builds (bounds computed) and frees the builder's arrays */

#endif
