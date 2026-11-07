/* explorer_worldgen: builds the world and writes it.
 *   explorer_worldgen <out.bin> [seed] [--map]      --map prints the cell map (one character per cell) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/world.h"

static world W;

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: explorer_worldgen <out.bin> [seed] [--map]\n");
    return 2;
  }
  uint32_t seed = argc > 2 && argv[2][0] != '-' ? (uint32_t)strtoul(argv[2], NULL, 0) : 1u;
  world_generate(&W, seed);
  bool map = false;
  for (int i = 2; i + 1 < argc; i++)
    if (!strcmp(argv[i], "--room")) { /* the tiles of a room and its border */
      const room *rm = &W.rooms[atoi(argv[i + 1])];
      printf("room %s kind %d gate %d region %d at cells %d,%d size %dx%d\n", argv[i + 1], rm->kind, rm->gate, rm->region, rm->x, rm->y, rm->w, rm->h);
      for (int y = rm->y * CELL_H - 2; y < (rm->y + rm->h) * CELL_H + 2; y++) {
        for (int x = rm->x * CELL_W - 2; x < (rm->x + rm->w) * CELL_W + 2; x++) putchar(" #=^~%?BS"[w_tile(&W, x, y)]);
        printf(" %d\n", y);
      }
    }
  for (int i = 2; i < argc; i++) map |= strcmp(argv[i], "--map") == 0;
  if (map) {
    for (int y = 0; y < GH; y++) {
      for (int x = 0; x < GW; x++) {
        int r = W.cell_room[y][x];
        char c = ' ';
        if (r != 0xffff) {
          const room *rm = &W.rooms[r];
          c = (char)('a' + rm->region);
          if (rm->kind == RK_GATE) c = (char)('0' + rm->gate);
          if (rm->kind == RK_BOSS) c = 'B';
          if (rm->kind == RK_ITEM) c = '*';
          if (rm->kind == RK_SAVE) c = 'S';
          if (rm->kind == RK_SECRET) c = '?';
          if (rm->kind == RK_START) c = '@';
        }
        putchar(c);
      }
      putchar('\n');
    }
  }
  int cells = 0;
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) cells += W.cell_room[y][x] != 0xffff;
  printf("world %08x: %d rooms, %d cells, %d doors, %d entities, %d pickups, %d bosses, %d shrines\n", W.hash, W.nrooms, cells,
         W.ndoors, W.nents, W.npickups, W.nbosses, W.nsaves);
  if (!world_save(&W, argv[1])) {
    fprintf(stderr, "cannot write %s\n", argv[1]);
    return 1;
  }
  return 0;
}
