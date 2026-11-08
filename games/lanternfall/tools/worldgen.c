/* lanternfall_worldgen: builds the world and writes it.
 *   lanternfall_worldgen <out.bin> [seed] [--map]      --map prints the cell map (one character per cell); --list the rooms,
 *   --room n one room's tiles, --pickups the pickups, shrines and wardens */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/world.h"

static world W;

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: lanternfall_worldgen <out.bin> [seed] [--map]\n");
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
  for (int i = 2; i < argc; i++)
    if (!strcmp(argv[i], "--list")) /* each room: index, region, kind, cells, set piece */
      for (int r = 0; r < W.nrooms; r++) printf("room %d region %d kind %d %dx%d piece %d\n", r, W.rooms[r].region, W.rooms[r].kind, W.rooms[r].w, W.rooms[r].h, W.rooms[r].piece);
  for (int i = 2; i < argc; i++)
    if (!strcmp(argv[i], "--pickups")) /* each pickup, shrine and warden: id, kind, sub, arg, room, place */
      for (int e = 0; e < W.nents; e++)
        if (W.ents[e].kind == EK_PICKUP || W.ents[e].kind == EK_SAVE || W.ents[e].kind == EK_BOSS)
          printf("ent %d kind %d id %d sub %d arg %d room %d at %.1f,%.1f\n", e, W.ents[e].kind, W.ents[e].id, W.ents[e].sub, W.ents[e].arg, W.ents[e].room, W.ents[e].x, W.ents[e].y);
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
