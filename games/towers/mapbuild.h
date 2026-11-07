/* towers map builder: a map's still scenery built under one node, the way the game and the bake tool (tools/mapview)
 * both draw it, so a bake made by the tool applies in the game (a lightmap matches its meshes by node path and
 * geometry).
 *
 * Under a Node3D "Level" added to the given parent:
 *   - "Light": the theme's light scene (scenes/th_<theme>.cwscene: Sun, Fill, Env, Post);
 *   - "Ground": the raised ground, the banks down to the path and the rock, one heightfield over the map and a margin
 *     round it (STATIC, its uv0 the lightmap layout), the theme's ground texture under vertex colours;
 *   - "Path": the low path the creeps walk (STATIC, the same layout), the theme's path texture;
 *   - "Water": a still sheet over the water tiles (not baked);
 *   - "Decor<f>_<g>", "Lamp_<g>": the props (surface and glow groups), STATIC instanced;
 *   - "BakeLights": the map's light records as bake-only lights, only while baking.
 * Pads, towers, the core, the gates and everything that moves are the view's (view.c). */
#ifndef TOWERS_MAPBUILD_H
#define TOWERS_MAPBUILD_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/lightmap.h"
#include "cartwheel/scene.h"
#include "map.h"
#include "three.h"

/* a .cwchar read into geometries: frame f of group g (group 0 the lit surface, 1 the glow, 2 the team trim) */
enum { MB_MAXF = 24, MB_MAXG = 3, MB_MAXC = 4 };
typedef struct mb_chr { int nclip, ngroup, nf[MB_MAXC], first[MB_MAXC], total; t3_geometry *geo[MB_MAXG][MB_MAXF]; float lo_y, hi_y, rad; } mb_chr;
bool mb_chr_load(mb_chr *c, const char *path); /* false (logged) when it cannot be read */
void mb_chr_free(mb_chr *c);
void mb_chr_simplify(mb_chr *c, int g, float cell); /* fewer triangles for a model seen small (one topology kept) */
t3_color mb_lin(float r, float g, float b);

#define MB_MARGIN 3 /* tiles of scenery drawn round the map (the camera sees past its edges) */

typedef struct mb_built {
  cw_node node;      /* "Level" */
  const tdmap *m;
  int theme;
  cw_lightmap_data *lm;
} mb_built;

/* the ground's height at a world point (the heightfield's, bilinear) */
float mapbuild_height(const tdmap *m, float x, float z);
/* Build the map under parent. The lightmap is not touched: call mapbuild_lightmap after the tree has run a frame
 * (the light scene places its sun). */
bool mapbuild_build(cw_node parent, const tdmap *m, mb_built *out);
/* Bake the map and write bake_path (.cwlm) and its .hash beside it when bake_path is set (native), else load the
 * map's shipped bake; then apply it (the ground draws BAKED). text: the map's .cwtd text (for the hash). false when
 * neither worked. */
bool mapbuild_lightmap(cw_tree *t, mb_built *b, const char *bake_path, const char *text);
/* The baked light a thing standing at (x, z) takes, as a colour multiplier (1 where nothing was baked). */
t3_color mapbuild_light_at(const mb_built *b, float x, float z);
void mapbuild_free(mb_built *b);
/* The bake's fingerprint: map_bake_hash of the map's text folded with the bytes of the theme's decor kit and the
 * lamp (a changed kit makes every bake of its theme stale). tools/check-maps.mjs computes the same. */
uint64_t mapbuild_bake_hash(const char *text, int theme);
void mapbuild_kit(int theme, char *out, int len); /* the theme's decor kit file */

#endif
