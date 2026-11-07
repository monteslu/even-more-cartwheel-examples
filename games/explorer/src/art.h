/* explorer: the art made in code at start-up (art.c): tile sets with normal maps for six materials, the bright
 * lips of walkable surfaces, ledges, thorns, back walls, the parallax silhouettes, soft glows. */
#ifndef EXPLORER_ART_H
#define EXPLORER_ART_H

#include "cartwheel/render2d.h"

enum { MAT_STONE, MAT_EARTH, MAT_CRYSTAL, MAT_BRASS, MAT_CORAL, MAT_CLOUD, NMAT };
#define TILE_PX 64
/* the parallax families: ridges, a colonnade of arches, trees, crystal spires, machinery, growth hanging from above */
enum { PAR_RIDGE, PAR_ARCH, PAR_TREES, PAR_CRYSTAL, PAR_MACHINE, PAR_HANG, NPAR };

typedef struct art {
  cw_texture tiles[NMAT];  /* 4x4 atlas of the 16 edge variants (bit 1 top open, 2 right, 4 bottom, 8 left), normal-mapped */
  cw_texture lip[NMAT];    /* 4 variants side by side, TILE_PX x 40 each: the walkable top */
  cw_texture ledge[NMAT];  /* one-way ledge, 3 pieces (left, middle, right) */
  cw_texture wall[NMAT];   /* back wall, 128 x 128, normal-mapped */
  cw_texture thorns;       /* 64 x 40 */
  cw_texture soft;         /* a radial glow, 64 x 64 */
  cw_texture spark;        /* a small hard-cored glow, 32 x 32 */
  cw_texture par[NPAR];    /* parallax silhouettes, 1024 x 512, shaded white on clear, repeat across (PAR_*) */
  cw_texture molten;       /* a molten channel's surface, 64 x 40: the Forge's hazard */
  cw_texture leaf;         /* a leaf / petal / shard for particles, 32 x 32 */
} art;
extern art A;

void art_init(void);
int region_material(int region);

#endif
