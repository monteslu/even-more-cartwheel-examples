/* pinball's backbox picture: each table's illustration, painted in code at load (no image files), in the table's own
 * colours. Original scenes:
 *   Coral Deep    light falling through deep water, coral fans, and the long scaled creature of the Leviathan mode
 *                 coiling up out of the dark with a lit eye
 *   Star Foundry  a nebula, the toothed forge ring turning round a new sun, molten light pouring out of it
 *   Neon Fair     a night sky over a lit wheel, a striped tent, strings of bulbs and the high striker's bell
 * Linear colour, soft edges, a vignette; sRGB bytes out. */
#include "backbox.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float r, g, b; } c3;
static c3 *P;
static c3 C(const float *c, float k) { return (c3){ c[0] * k, c[1] * k, c[2] * k }; }
static c3 mixc(c3 a, c3 b, float t) { return (c3){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
static float srgb(float l) { l = l < 0 ? 0 : l > 1 ? 1 : l; return l <= 0.0031308f ? l * 12.92f : 1.055f * powf(l, 1 / 2.4f) - 0.055f; }
static uint32_t hsh(int x, int y, int s) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float rnd(int k, int s) { return (float)(hsh(k, s, 77) & 0xffff) / 65535.0f; }
static float vnoise(float x, float y, int s) {
  int xi = (int)floorf(x), yi = (int)floorf(y);
  float fx = x - (float)xi, fy = y - (float)yi;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = rnd(xi * 31 + yi, s), b = rnd((xi + 1) * 31 + yi, s), c = rnd(xi * 31 + yi + 1, s), d = rnd((xi + 1) * 31 + yi + 1, s);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float x, float y, int s) { return vnoise(x, y, s) * 0.55f + vnoise(x * 2.1f, y * 2.1f, s + 1) * 0.3f + vnoise(x * 4.4f, y * 4.4f, s + 2) * 0.15f; }

static void put(int x, int y, c3 c, float a) {
  if (x < 0 || y < 0 || x >= BB_W || y >= BB_H || a <= 0) return;
  c3 *p = &P[y * BB_W + x];
  *p = mixc(*p, c, a > 1 ? 1 : a);
}
static void add(int x, int y, c3 c, float a) {
  if (x < 0 || y < 0 || x >= BB_W || y >= BB_H || a <= 0) return;
  c3 *p = &P[y * BB_W + x];
  p->r += c.r * a, p->g += c.g * a, p->b += c.b * a;
}
/* a disc (pixels), soft edge; glow: added light instead of paint */
static void disc(float cx, float cy, float r, c3 c, float a, bool glow) {
  for (int y = (int)(cy - r - 2); y <= (int)(cy + r + 2); y++)
    for (int x = (int)(cx - r - 2); x <= (int)(cx + r + 2); x++) {
      float d = sqrtf(((float)x - cx) * ((float)x - cx) + ((float)y - cy) * ((float)y - cy)), k = r - d + 0.5f;
      if (k <= 0) continue;
      if (glow) add(x, y, c, a * (1 - d / (r + 1)) * (1 - d / (r + 1)));
      else put(x, y, c, a * (k > 1 ? 1 : k));
    }
}
static void ring(float cx, float cy, float r0, float r1, c3 c, float a) {
  for (int y = (int)(cy - r1 - 2); y <= (int)(cy + r1 + 2); y++)
    for (int x = (int)(cx - r1 - 2); x <= (int)(cx + r1 + 2); x++) {
      float d = sqrtf(((float)x - cx) * ((float)x - cx) + ((float)y - cy) * ((float)y - cy)), k = fminf(r1 - d, d - r0) + 0.5f;
      if (k > 0) put(x, y, c, a * (k > 1 ? 1 : k));
    }
}
static void stroke(float ax, float ay, float bx, float by, float w, c3 c, float a) {
  float ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey;
  for (int y = (int)(fminf(ay, by) - w - 2); y <= (int)(fmaxf(ay, by) + w + 2); y++)
    for (int x = (int)(fminf(ax, bx) - w - 2); x <= (int)(fmaxf(ax, bx) + w + 2); x++) {
      float t = l2 > 0 ? (((float)x - ax) * ex + ((float)y - ay) * ey) / l2 : 0;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      float dx = (float)x - (ax + ex * t), dy = (float)y - (ay + ey * t), k = w - sqrtf(dx * dx + dy * dy) + 0.5f;
      if (k > 0) put(x, y, c, a * (k > 1 ? 1 : k));
    }
}
static void glow_line(float ax, float ay, float bx, float by, float w, c3 c, float a) {
  float ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey;
  for (int y = (int)(fminf(ay, by) - w - 2); y <= (int)(fmaxf(ay, by) + w + 2); y++)
    for (int x = (int)(fminf(ax, bx) - w - 2); x <= (int)(fmaxf(ax, bx) + w + 2); x++) {
      float t = l2 > 0 ? (((float)x - ax) * ex + ((float)y - ay) * ey) / l2 : 0;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      float dx = (float)x - (ax + ex * t), dy = (float)y - (ay + ey * t), d = sqrtf(dx * dx + dy * dy) / w;
      if (d < 1) add(x, y, c, a * (1 - d) * (1 - d));
    }
}

static void coral(const table *T) {
  const theme *th = &T->th;
  for (int y = 0; y < BB_H; y++)
    for (int x = 0; x < BB_W; x++) {
      float v = (float)y / BB_H, u = (float)x / BB_W;
      c3 c = mixc(C(th->base2, 0.9f), C(th->base, 0.12f), powf(v, 0.8f));
      float shaft = powf(fmaxf(0, sinf((u * 3.0f - v * 1.2f) * 6.0f + fbm(u * 3, v * 2, 3) * 2.0f)), 6) * (1 - v) * 0.5f;
      c = mixc(c, C(th->base2, 2.2f), shaft);
      float caus = powf(fmaxf(0, 1 - fabsf(fbm(u * 14, v * 9, 5) - 0.5f) * 8), 3) * (1 - v) * 0.25f;
      c = mixc(c, C(th->base2, 2.5f), caus);
      P[y * BB_W + x] = c;
    }
  /* coral fans along the bottom */
  for (int k = 0; k < 14; k++) {
    float bx = 20 + (float)k * 46 + rnd(k, 1) * 20, by = BB_H + 4;
    c3 cc = C(th->col[k % 3 == 0 ? 0 : k % 3 == 1 ? 3 : 2], 0.55f);
    for (int br = 0; br < 9; br++) {
      float a = -1.5708f + (rnd(k, br) - 0.5f) * 1.6f, l = 40 + rnd(k, br + 20) * 70;
      float mx = bx + cosf(a) * l * 0.5f, my = by + sinf(a) * l * 0.5f;
      stroke(bx, by, mx, my, 3.2f, cc, 0.9f);
      stroke(mx, my, mx + cosf(a - 0.4f) * l * 0.45f, my + sinf(a - 0.4f) * l * 0.45f, 2.2f, cc, 0.85f);
      stroke(mx, my, mx + cosf(a + 0.4f) * l * 0.45f, my + sinf(a + 0.4f) * l * 0.45f, 2.2f, cc, 0.85f);
    }
  }
  /* the creature: a body of overlapping plates along a coil, from the deep up to a head turned toward the viewer */
  c3 body = C(th->col[1], 0.35f), plate = C(th->col[1], 0.75f), belly = C(th->col[2], 0.5f);
  float px = 0, py = 0;
  for (int i = 0; i <= 140; i++) {
    float t = (float)i / 140.0f;
    float x = 70 + t * 420 + sinf(t * 7.5f) * 70 * (1 - t * 0.4f), y = BB_H + 30 - t * 300 + cosf(t * 6.0f) * 40;
    float r = 26 - t * 10;
    disc(x, y, r, body, 1, false);
    if (i % 4 == 0) ring(x, y, r - 4, r, plate, 0.8f);
    disc(x - r * 0.25f, y + r * 0.3f, r * 0.45f, belly, 0.35f, false);
    if (i == 140) px = x, py = y;
  }
  /* its head, its crest, its lit eye */
  disc(px + 28, py - 8, 30, body, 1, false);
  disc(px + 52, py + 2, 18, body, 1, false);
  for (int k = 0; k < 6; k++) stroke(px - 4 + (float)k * 9, py - 32, px - 16 + (float)k * 9, py - 62 - (float)(k % 2) * 10, 3, plate, 0.9f);
  disc(px + 38, py - 14, 7, C(th->col[2], 3.0f), 1, false);
  disc(px + 38, py - 14, 26, C(th->col[2], 0.9f), 1, true);
  stroke(px + 42, py + 10, px + 66, py + 6, 2, C(th->base, 0.05f), 0.9f); /* the mouth */
  /* bubbles rising */
  for (int k = 0; k < 40; k++) ring(rnd(k, 51) * BB_W, rnd(k, 52) * BB_H * 0.8f, 1.5f + rnd(k, 53) * 4, 3 + rnd(k, 53) * 5, C(th->base2, 2.5f), 0.5f);
}

static void foundry(const table *T) {
  const theme *th = &T->th;
  float cx = 330, cy = 180;
  for (int y = 0; y < BB_H; y++)
    for (int x = 0; x < BB_W; x++) {
      float u = (float)x / BB_W, v = (float)y / BB_H;
      c3 c = mixc(C(th->base, 1.0f), C(th->base2, 0.9f), fbm(u * 3, v * 3, 11));
      c = mixc(c, C(th->col[3], 0.4f), powf(fbm(u * 4 + 5, v * 4, 21), 3) * 1.5f);
      c = mixc(c, C(th->col[1], 0.3f), powf(fbm(u * 5, v * 5 + 3, 31), 4) * 1.8f);
      if ((hsh(x, y, 9) & 1023) < 3) c = mixc(c, (c3){ 1, 1, 1 }, 0.9f);
      P[y * BB_W + x] = c;
    }
  /* the new sun and its light */
  disc(cx, cy, 150, C(th->col[0], 0.5f), 1, true);
  disc(cx, cy, 70, C(th->col[2], 2.5f), 1, false);
  disc(cx, cy, 90, C(th->col[2], 1.4f), 1, true);
  /* the forge ring, toothed, round it */
  c3 metal = C(th->col[0], 0.25f), rim = C(th->col[0], 0.9f);
  ring(cx, cy, 104, 128, metal, 1);
  ring(cx, cy, 104, 108, rim, 1), ring(cx, cy, 124, 128, rim, 1);
  for (int k = 0; k < 24; k++) {
    float a = (float)k / 24 * 6.2832f;
    stroke(cx + cosf(a) * 128, cy + sinf(a) * 128, cx + cosf(a) * 146, cy + sinf(a) * 146, 6, metal, 1);
    disc(cx + cosf(a) * 116, cy + sinf(a) * 116, 3, rim, 1, false);
  }
  /* molten light pouring down from the ring into a mould */
  for (int i = 0; i < 60; i++) {
    float t = (float)i / 60;
    float x = cx + 90 + sinf(t * 2.0f) * 30 + t * 40, y = cy + 90 + t * 200;
    disc(x, y, 9 - t * 3, C(th->col[0], 2.2f), 1, false);
    disc(x, y, 22, C(th->col[0], 0.25f), 1, true);
  }
  stroke(380, BB_H - 20, 560, BB_H - 20, 14, C(th->base, 0.6f), 1);
  /* sparks */
  for (int k = 0; k < 70; k++) {
    float a = rnd(k, 3) * 6.2832f, r = 150 + rnd(k, 4) * 160;
    disc(cx + cosf(a) * r, cy + sinf(a) * r * 0.7f, 1.2f + rnd(k, 5) * 1.5f, C(th->col[2], 2.0f), 1, false);
  }
}

static void fair(const table *T) {
  const theme *th = &T->th;
  for (int y = 0; y < BB_H; y++)
    for (int x = 0; x < BB_W; x++) {
      float v = (float)y / BB_H;
      c3 c = mixc(C(th->col[3], 0.12f), C(th->base2, 0.7f), powf(v, 1.4f));
      if ((hsh(x, y, 4) & 2047) < 3 && v < 0.6f) c = mixc(c, (c3){ 1, 1, 1 }, 0.8f);
      P[y * BB_W + x] = c;
    }
  /* the wheel: a rim, spokes, a bulb at each spoke's end, cars hanging */
  float wx = 210, wy = 170, wr = 140;
  c3 steel = C(th->col[3], 0.6f), bulb = C(th->col[2], 3.0f);
  ring(wx, wy, wr - 5, wr, steel, 1), ring(wx, wy, wr * 0.55f - 3, wr * 0.55f, steel, 1);
  for (int k = 0; k < 16; k++) {
    float a = (float)k / 16 * 6.2832f;
    stroke(wx, wy, wx + cosf(a) * wr, wy + sinf(a) * wr, 1.6f, steel, 1);
    disc(wx + cosf(a) * wr, wy + sinf(a) * wr, 4, bulb, 1, false);
    disc(wx + cosf(a) * wr, wy + sinf(a) * wr, 14, C(th->col[2], 0.5f), 1, true);
    float cxp = wx + cosf(a) * wr, cyp = wy + sinf(a) * wr + 12;
    disc(cxp, cyp, 8, C(th->col[k % 2 ? 0 : 1], 0.7f), 1, false);
  }
  stroke(wx, wy, wx - 90, BB_H, 5, steel, 1), stroke(wx, wy, wx + 90, BB_H, 5, steel, 1);
  disc(wx, wy, 10, bulb, 1, false);
  /* the tent: stripes under a peaked roof */
  for (int y = 250; y < BB_H; y++)
    for (int x = 360; x < 620; x++) {
      float top = 250 + fabsf((float)x - 490) * 0.5f;
      if ((float)y < top) continue;
      int stripe = (int)(((float)x - 490) / ((float)y - 210) * 6.0f + 20) % 2;
      put(x, y, C(stripe ? th->col[0] : th->base2, stripe ? 0.6f : 1.4f), 1);
    }
  stroke(490, 250, 490, 222, 2, steel, 1), disc(490, 220, 4, bulb, 1, false);
  /* the high striker at the right edge, its bell lit */
  stroke(600, BB_H, 600, 90, 4, C(th->col[1], 0.5f), 1);
  for (int k = 0; k < 10; k++) stroke(590, 120 + (float)k * 26, 610, 120 + (float)k * 26, 5, C(th->col[k < 3 ? 0 : 1], 0.8f), 1);
  disc(600, 82, 16, C(th->col[2], 1.6f), 1, false);
  disc(600, 82, 40, C(th->col[2], 0.7f), 1, true);
  /* strings of bulbs across the sky */
  for (int s = 0; s < 2; s++)
    for (int k = 0; k <= 24; k++) {
      float t = (float)k / 24, x = t * BB_W, y = 26 + (float)s * 40 + sinf(t * 3.1416f) * 40;
      disc(x, y, 3, C(th->col[(k + s) % 3], 2.5f), 1, false);
      disc(x, y, 10, C(th->col[(k + s) % 3], 0.4f), 1, true);
    }
}

uint8_t *backbox_paint(const table *T) {
  P = calloc(BB_W * BB_H, sizeof(c3));
  if (T->th.pattern == 1) foundry(T);
  else if (T->th.pattern == 2) fair(T);
  else coral(T);
  uint8_t *out = malloc(BB_W * BB_H * 4);
  for (int y = 0; y < BB_H; y++)
    for (int x = 0; x < BB_W; x++) {
      float u = (float)x / BB_W - 0.5f, v = (float)y / BB_H - 0.5f, vig = 1 - (u * u + v * v) * 0.9f;
      c3 c = P[y * BB_W + x];
      uint8_t *o = out + (y * BB_W + x) * 4;
      o[0] = (uint8_t)(srgb(c.r * vig) * 255 + 0.5f), o[1] = (uint8_t)(srgb(c.g * vig) * 255 + 0.5f), o[2] = (uint8_t)(srgb(c.b * vig) * 255 + 0.5f), o[3] = 255;
    }
  free(P);
  P = NULL;
  return out;
}

static cw_texture tex;
static int painted = -1;
static const char *painted_name;

void backbox_table(const table *T) {
  if (painted >= 0 && painted_name == T->name) return;
  uint8_t *px = backbox_paint(T);
  if (painted < 0) tex = cw_texture_from_pixels(BB_W, BB_H, px, NULL);
  else cw_texture_update(tex, px);
  free(px);
  painted = 1, painted_name = T->name;
}

cw_texture backbox_texture(void) { return tex; }
