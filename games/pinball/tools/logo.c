/* pinball_logo: the launcher logo, "PINBALL" in the dot display's own font, each dot a lit bulb on a transparent
 * background (art/logo.png). Native only.   pinball_logo <out.png> */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "../src/dmd.h"

int main(int argc, char **argv) {
  if (argc < 2) return 2;
  static dmd d;
  dmd_clear(&d);
  const char *t = "PINBALL";
  int w = dmd_text_width(t, 2);
  dmd_text(&d, t, 2, 2, 2, 15);
  const int S = 22, W = (w + 4) * S, H = 18 * S;
  unsigned char *px = calloc((size_t)W * H, 4);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float best = 0, glow = 0;
      int cx = x / S, cy = y / S;
      for (int j = cy - 2; j <= cy + 2; j++)
        for (int i = cx - 2; i <= cx + 2; i++) {
          if (i < 0 || j < 0 || i >= DMD_W || j >= DMD_H || !d.dot[j][i]) continue;
          float dx = (float)x - ((float)i + 0.5f) * S, dy = (float)y - ((float)j + 0.5f) * S, r = sqrtf(dx * dx + dy * dy) / (S * 0.5f);
          if (r < 0.85f) best = fmaxf(best, 1 - r * r * 0.6f);
          glow = fmaxf(glow, expf(-r * r * 0.5f) * 0.45f);
        }
      unsigned char *p = px + ((size_t)y * W + x) * 4;
      float v = fmaxf(best, glow), core = best > 0 ? best : 0;
      float vy = (float)y / H;
      p[0] = (unsigned char)fminf(255, 255 * (0.9f + 0.1f * core));
      p[1] = (unsigned char)fminf(255, 255 * (0.35f + 0.5f * core * (1 - vy * 0.6f)));
      p[2] = (unsigned char)fminf(255, 255 * (0.1f + 0.45f * core * core));
      p[3] = (unsigned char)fminf(255, 255 * v);
    }
  stbi_write_png(argv[1], W, H, 4, px, W * 4);
  printf("%s: %d x %d\n", argv[1], W, H);
  return 0;
}
