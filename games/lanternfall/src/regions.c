/* lanternfall: the thirteen regions, where they sit on the cell grid and their palettes. Mood comes from colour, never
 * from darkness: every region is mid-key, and each keeps a clear value step between its background (the far
 * layers and the back wall), the playfield, and the surfaces the player stands on (the bright top lip). */
#include "world.h"

/* name, cell rect, then sky, far, mid, near, ground body, walkable top, accent */
const region_info REGIONS[NREGIONS] = {
  [R_GATE] = { "The Lantern Gate", 26, 2, 8, 7, 0x6fa7b8, 0x4f8296, 0x3d6a7a, 0x2f5563, 0x4b5f5a, 0x9fd36a, 0xffd27a },
  [R_ROOTWAY] = { "The Rootway", 14, 3, 12, 9, 0xd08a55, 0xa66a45, 0x80503a, 0x5e3c2f, 0x6b4a35, 0xf2b25c, 0xffe08a },
  [R_GLASS] = { "Glasswater Caves", 14, 12, 12, 10, 0x4f86c9, 0x3e6aa8, 0x335690, 0x283f6e, 0x3c4f7a, 0x8fe0ff, 0x9af0ff },
  [R_FEN] = { "Mirelight Fen", 26, 12, 8, 10, 0x6fa36a, 0x557f55, 0x416545, 0x334f37, 0x4d5a3b, 0xc9e06a, 0xd6ff8a },
  [R_CLOCK] = { "The Clockwork Deep", 34, 18, 12, 10, 0xb98c4f, 0x93703f, 0x735733, 0x5a4329, 0x5f5040, 0xffd36e, 0xffc05a },
  [R_TIDAL] = { "The Tidal Halls", 14, 22, 20, 9, 0x3f9fa8, 0x327f8a, 0x28666f, 0x1f4f57, 0x2f5a5e, 0x7ff0d8, 0x8affe6 },
  [R_STAR] = { "Starfall Reach", 2, 12, 12, 14, 0x6a5aa8, 0x54468c, 0x423872, 0x342b5a, 0x463c66, 0xc6a8ff, 0xfff0a0 },
  [R_SKY] = { "The Skyreach Spires", 2, 0, 12, 12, 0x9fc6e8, 0x80a8cc, 0x6688ad, 0x506d8c, 0x6c7f96, 0xffffff, 0xffe6a0 },
  [R_ARCHIVE] = { "The Sunken Archive", 34, 0, 12, 9, 0x8a5fa8, 0x6f4a8a, 0x593b70, 0x462e58, 0x5a4664, 0xe0b0ff, 0xffd0a0 },
  [R_FORGE] = { "The Ember Forge", 34, 9, 12, 9, 0xc4573f, 0x9c4535, 0x7a382d, 0x5c2c25, 0x5e3a33, 0xffb050, 0xffa040 },
  [R_FROST] = { "The Frostvault", 46, 4, 12, 12, 0x9fd0e6, 0x7fb0cc, 0x6694b0, 0x4f7790, 0x6a8496, 0xf0ffff, 0xa0e8ff },
  [R_BLOOM] = { "The Bloom Gardens", 46, 16, 12, 12, 0xd47fa6, 0xae6388, 0x8c4f6e, 0x6c3d56, 0x6a4a5c, 0xffc0e0, 0xfff09a },
  [R_HEART] = { "The Heartwood", 26, 9, 8, 3, 0xd8a64a, 0xb0853a, 0x8c6a30, 0x6c5127, 0x6a5538, 0xfff0a0, 0xffe070 },
};
