/* Faces that fight for the depth test (coplanar.c).
 *
 * Two faces lying in one plane, facing the same way (or either way, when one is drawn from both sides) and
 * overlapping, one of them writing depth (a trim laid flush on a wall, a deck on a platform top, two boxes sharing a
 * face) have no depth order: the depth test picks one or the other pixel by pixel, a little differently every frame
 * the camera moves, so patches of the picture swap between the two surfaces' look. Two that look the same where they
 * overlap (one material, one shading normal, the same texture coordinates and colours there) do not count: whichever
 * wins, the pixel is the same.
 *
 *   cop_scan            counts those pairs among the meshes drawn in a 3D scene (world space, every visible
 *                       depth-tested mesh and instance; skinned meshes, lines, points and sprites are left out), by
 *                       region (the scene nodes they hang under)
 *   cop_separate        layers them apart where a game builds its still world. Faces move as whole patches (the faces
 *                       of one mesh copy in one plane and one material that meet), and the walls meeting a moved patch
 *                       stretch to it, so nothing cracks. Of two patches in one mesh copy the one drawn later comes
 *                       out in front (it is what the depth test mostly showed already); of two in different meshes,
 *                       the smaller (the detail on what it is laid on). A geometry drawn more than once is layered in
 *                       its own space first; an instance lying on another instance of its mesh in an upright plane
 *                       steps aside.
 *   cop_separate_model  the same for a model's parts in its own space, before it is placed
 *   cop_init            registers the debug command `coplanar` (the scan of the app's 3D scene, as JSON) and the debug
 *                       fields level.coplanar_pairs / level.coplanar_moved; `coplanar selftest` checks the detector on
 *                       made-up faces (a doubled face must count, neighbours and faces 5 mm apart must not)
 *
 * Environment (native development and the gates): COPLANAR_CONTROL=1 adds a doubled face to the scene (two squares in
 * one plane, cut into triangles differently), which the scan must find (tests/coplanar.mjs's control);
 * COPLANAR_SEPARATE=0 leaves the world as built (the sweep's control); COPLANAR_SWEEP="from,x,y,z,tx,ty,tz,dx,dy,dz"
 * holds the game still from frame `from` and glides the camera (tests/coplanar-sweep.mjs); COPLANAR_DUMP=n prints the
 * first n pairs of a scan face by face, COPLANAR_DEBUG=1 each separation pass. */
#ifndef GAMES_COPLANAR_H
#define GAMES_COPLANAR_H

#include <stdbool.h>

#include "three.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cop_region {
  char name[96];
  int pairs;
  float width;  /* the widest overlap of its pairs (the narrowest way across one pair), m */
  float at[3];  /* that pair's middle (world) */
} cop_region;

typedef struct cop_report {
  int meshes, faces, pairs;
  int nregions;
  cop_region regions[48]; /* the most pairs first */
} cop_report;

/* the pairs among root's drawn meshes (root's matrices as they stand) */
void cop_scan(t3_object *root, cop_report *out);
/* layer the overlapping same-plane faces of root's plain meshes (not instanced, not skinned) apart by eps; a geometry
 * that two meshes share is given each its own copy first. Returns the vertices moved; *left the pairs among those
 * meshes after (0 when every one was separated). Does nothing (returns 0) under COPLANAR_SEPARATE=0. */
int cop_separate(t3_object *root, float eps, int *left);
/* the same for a model's own parts, in its own space (geometries drawn together at one transform: a model's surface,
 * trim and glow), before it is placed anywhere; every mesh drawing these geometries takes the change */
int cop_separate_model(t3_geometry *const *geos, int n, float eps);
/* the debug command and fields (call in init) */
void cop_init(void);
/* where the switches (COPLANAR_CONTROL, COPLANAR_SEPARATE, COPLANAR_SWEEP, ...) are read: the environment by default; a
 * game whose checks run as a cart passes its own run options (destructo: opt_get, set over the debug channel). Call it
 * before cop_init. */
void cop_set_options(const char *(*get)(const char *));
/* COPLANAR_SWEEP as the options hold it now (cop_init reads it once; a game that sets it later over its debug channel
 * calls this): the sweep's frames count from this call */
void cop_sweep_arm(void);
/* how near two faces' planes must be to count as one plane (m; 1 mm by default): what the game's depth buffer cannot
 * tell apart at the distances it is seen from. A game whose camera resolves far finer (a table a metre from the eye)
 * sets less, so faces it lays a fraction of a millimetre apart on purpose are not counted. */
void cop_set_plane_tol(float m);
/* a camera that sees far with a near plane close in (a skate park's town out to the horizon): the depth buffer tells two
 * depths d metres away apart only about d^2 / (near * 2^24) apart, so faces within four such steps of one plane count
 * (seen from within reach metres of center), and cop_separate lays them at least twice that apart */
void cop_set_view(float near, t3_vec3 center, float reach);
/* add to the moved count that level.coplanar_moved shows (cop_separate does) */
extern int cop_moved_total;
/* how long the last cop_separate took (ms, the search after it for *left not counted) */
extern double cop_last_ms;

#ifdef __cplusplus
}
#endif
#endif
