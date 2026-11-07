/* lanternfall: reading and writing the world file, and lookups. The file is a small header and the world struct as it
 * lies in memory (both ends are little-endian 32-bit C: wasm32 and the 64-bit natives agree on this layout, which
 * holds only fixed-size integers and floats). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world.h"

#ifndef LANTERNFALL_NO_ENGINE
#include "cartwheel/platform.h"
#endif

#define MAGIC 0x31585057u /* "WPX1" */

int w_room_at(const world *w, float x, float y) {
  int cx = (int)(x / CELL_W), cy = (int)(y / CELL_H);
  if (x < 0 || y < 0 || cx >= GW || cy >= GH) return -1;
  int r = w->cell_room[cy][cx];
  return r == 0xffff ? -1 : r;
}

uint32_t world_hash(const world *w) {
  /* FNV-1a over the tiles, rooms, doors and entities */
  uint32_t h = 2166136261u;
#define MIX(p, n)                                                    \
  do {                                                               \
    const uint8_t *b_ = (const uint8_t *)(p);                        \
    for (size_t i_ = 0; i_ < (size_t)(n); i_++) h = (h ^ b_[i_]) * 16777619u; \
  } while (0)
  MIX(w->t, sizeof w->t);
  MIX(w->rooms, sizeof(room) * (size_t)w->nrooms);
  MIX(w->doors, sizeof(door) * (size_t)w->ndoors);
  MIX(w->ents, sizeof(ent) * (size_t)w->nents);
#undef MIX
  return h;
}

bool world_save(const world *w, const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  uint32_t hdr[2] = { MAGIC, (uint32_t)sizeof *w };
  bool ok = fwrite(hdr, sizeof hdr, 1, f) == 1 && fwrite(w, sizeof *w, 1, f) == 1;
  return fclose(f) == 0 && ok;
}

static bool from_bytes(world *w, const uint8_t *b, size_t n) {
  uint32_t hdr[2];
  if (n < sizeof hdr) return false;
  memcpy(hdr, b, sizeof hdr);
  if (hdr[0] != MAGIC || hdr[1] != sizeof *w || n < sizeof hdr + sizeof *w) {
    fprintf(stderr, "world: not a world file of this build (magic %08x, %u bytes, want %zu)\n", hdr[0], hdr[1], sizeof *w);
    return false;
  }
  memcpy(w, b + sizeof hdr, sizeof *w);
  return world_hash(w) == w->hash;
}

bool world_load(world *w, const char *path) {
#ifndef LANTERNFALL_NO_ENGINE
  size_t n = 0;
  void *data = cw_asset_load(path, &n);
  if (data) {
    bool ok = from_bytes(w, data, n);
    free(data);
    return ok;
  }
#endif
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n2 = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *buf = malloc((size_t)n2);
  bool ok = buf && fread(buf, 1, (size_t)n2, f) == (size_t)n2 && from_bytes(w, buf, (size_t)n2);
  free(buf);
  fclose(f);
  return ok;
}
