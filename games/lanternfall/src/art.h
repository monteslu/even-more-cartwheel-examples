/* lanternfall: the art made in code at start-up (art.c): each region's rock with normal maps, the bright
 * lips of walkable surfaces, ledges, thorns, back walls, the parallax silhouettes, soft glows. */
#ifndef LANTERNFALL_ART_H
#define LANTERNFALL_ART_H

#include "cartwheel/render2d.h"
#include "world.h"

enum { MAT_STONE, MAT_EARTH, MAT_CRYSTAL, MAT_BRASS, MAT_CORAL, MAT_CLOUD, NMAT };
#define TILE_PX 64
/* the parallax families: ridges, a colonnade of arches, trees, crystal spires, machinery, growth hanging from above */
enum { PAR_RIDGE, PAR_ARCH, PAR_TREES, PAR_CRYSTAL, PAR_MACHINE, PAR_HANG, NPAR };

typedef struct art {
  cw_texture rock[NREGIONS]; /* each region's rock, normal-mapped: 2x2 blocks (the tile's place in the body's two-tile period) of
                                * 4x4 edge variants (bit 1 top open, 2 right, 4 bottom, 8 left), TILE_PX each */
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

/* the parallax families' ground (art_par.h): everything painted standing in a family stands on a band of ground in
 * the same texture, from the family's ground line down to its bottom row. These are what tests/lanternfall-grounding.mjs
 * measures the painted trees against, through the game's debug channel (main.c). */
typedef struct par_feature { float x, hw, top; int id; } par_feature; /* the foot's centre and half width on the ground line, the top, in texture shares */
float art_par_ground(int k);                                /* the ground line as a share of the height, -1 for a family with none */
int art_par_features(int k, par_feature *out, int max);     /* what stands in family k */
void art_par_lift(int rows);                                /* the control: the trees painted again with one trunk stopped rows above its ground (0 restores) */
int art_par_lift_rows(void);

#endif
