/* The six cuts: each jewel kind is a convex polyhedron, the intersection of its facet planes, built the way a cutter
 * lays a stone out (facet angles and indexes) rather than modelled. Light is traced through it per pixel (gem.c's
 * shaders) and on the CPU here (the caustics a stone throws on the tray, baked at load).
 *
 * Local space: the girdle lies in the xy plane at z = 0, the crown (the table) faces +z (toward the viewer), the
 * pavilion points -z; +y is up on the screen. A stone's largest girdle half-extent is about 1. */
#ifndef JEWELS_CUTS_H
#define JEWELS_CUTS_H

#include <stdbool.h>
#include <stdint.h>

#define JW_MAX_PLANES 96
#define JW_CUT_SHARD 6 /* the seventh shape: a broken fragment, for the bursts */
#define JW_NCUTS 7

typedef struct jw_plane { float n[3], d; } jw_plane; /* inside: dot(n, p) <= d */

typedef struct jw_cut {
  const char *name, *stone;
  int nplanes;
  jw_plane p[JW_MAX_PLANES];
  float zmin, zmax;      /* the culet's and the table's heights */
  float radius;          /* bounding sphere about the origin */
  float extent_x, extent_y; /* the girdle's half-extents */
  float display;         /* world units (canvas pixels) per local unit, so every kind reads the same size */
  /* optics: index of refraction at 589 nm, dispersion (n at 430.8 nm minus n at 686.7 nm), absorption per local
   * unit of path (linear r, g, b), and the share of fire the game shows (above 1 pushes past the real stone) */
  float ior, dispersion, sigma[3], fire;
  float body[3];         /* the body colour as linear rgb (the HUD, the bursts' sparks) */
  int nverts;
  float verts[256][3];   /* the polyhedron's corners (for the outline and the tests) */
} jw_cut;

extern jw_cut jw_cuts[JW_NCUTS];
void jw_cuts_build(void);

/* Cauchy index of refraction at a wavelength in nm */
float jw_ior_at(const jw_cut *c, float nm);
/* the linear-sRGB weight of a wavelength (CIE 1931 matching functions, fitted; averages to 1, 1, 1 over 380-720) */
void jw_spectrum_rgb(float nm, float out[3]);

/* Light through a stone on the CPU: from local origin o along unit d (outside the stone), at wavelength nm.
 * Calls `exit` for every ray that leaves, with its direction and the light it carries (rgb weight). Returns false
 * when the ray misses. */
typedef void (*jw_exit_fn)(const float pos[3], const float dir[3], const float w[3], void *user);
bool jw_trace_cpu(const jw_cut *c, const float o[3], const float d[3], float nm, int bounces, jw_exit_fn exit, void *user);

/* the key light (unit, world, pointing toward the light) that the tray, the shadows and the caustics share */
extern const float jw_key_dir[3];

#endif
