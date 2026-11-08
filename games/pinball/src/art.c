/* pinball's playfield art, painted in code at load: each table's own colours and pattern (a reef's light bands and
 * coral, a forge's star field and plating, a fair's sunburst and bulb rows), the lanes and shots marked in paint,
 * a printed ring round every insert, and soft shadow along every wall and post. sRGB bytes, row 0 the top of the
 * table. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "view.h"

#define AW 1024
#define AH 2048
#define TW 0.52f   /* the art's width in metres (x -0.26 .. 0.26) */
#define TL 1.09f   /* its length (y 0 .. 1.09) */

static float srgb(float l) { l = l < 0 ? 0 : l > 1 ? 1 : l; return l <= 0.0031308f ? l * 12.92f : 1.055f * powf(l, 1 / 2.4f) - 0.055f; }

static uint32_t hsh(int x, int y, int s) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float vnoise(float x, float y, int s) {
  int xi = (int)floorf(x), yi = (int)floorf(y);
  float fx = x - (float)xi, fy = y - (float)yi;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = (float)(hsh(xi, yi, s) & 0xffff) / 65535.0f, b = (float)(hsh(xi + 1, yi, s) & 0xffff) / 65535.0f;
  float c = (float)(hsh(xi, yi + 1, s) & 0xffff) / 65535.0f, d = (float)(hsh(xi + 1, yi + 1, s) & 0xffff) / 65535.0f;
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float x, float y, int s) { return vnoise(x, y, s) * 0.55f + vnoise(x * 2.1f, y * 2.1f, s + 1) * 0.3f + vnoise(x * 4.3f, y * 4.3f, s + 2) * 0.15f; }

typedef struct { float r, g, b; } col3; /* linear */
static col3 C(const float *c) { return (col3){ c[0], c[1], c[2] }; }
static col3 mixc(col3 a, col3 b, float t) { return (col3){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
static col3 scl(col3 a, float k) { return (col3){ a.r * k, a.g * k, a.b * k }; }

static col3 *buf;
static void put(int x, int y, col3 c, float a) {
  if (x < 0 || y < 0 || x >= AW || y >= AH || a <= 0) return;
  col3 *p = &buf[y * AW + x];
  *p = mixc(*p, c, a > 1 ? 1 : a);
}
static int PX(float x) { return (int)((x + TW * 0.5f) / TW * AW); }
static int PY(float y) { return (int)((TL - y) / TL * AH); }
static float WX(int px) { return ((float)px + 0.5f) / AW * TW - TW * 0.5f; }
static float WY(int py) { return TL - ((float)py + 0.5f) / AH * TL; }

/* a filled disc (soft edge) in metres */
static void disc(float x, float y, float r, col3 c, float a) {
  int x0 = PX(x - r) - 2, x1 = PX(x + r) + 2, y0 = PY(y + r) - 2, y1 = PY(y - r) + 2;
  float px_m = TW / AW;
  for (int py = y0; py <= y1; py++)
    for (int px = x0; px <= x1; px++) {
      float dx = WX(px) - x, dy = WY(py) - y, d = sqrtf(dx * dx + dy * dy);
      float k = (r - d) / px_m + 0.5f;
      if (k > 0) put(px, py, c, a * (k > 1 ? 1 : k));
    }
}
static void ring(float x, float y, float r0, float r1, col3 c, float a) {
  int x0 = PX(x - r1) - 2, x1 = PX(x + r1) + 2, y0 = PY(y + r1) - 2, y1 = PY(y - r1) + 2;
  float px_m = TW / AW;
  for (int py = y0; py <= y1; py++)
    for (int px = x0; px <= x1; px++) {
      float dx = WX(px) - x, dy = WY(py) - y, d = sqrtf(dx * dx + dy * dy);
      float k = fminf((r1 - d), (d - r0)) / px_m + 0.5f;
      if (k > 0) put(px, py, c, a * (k > 1 ? 1 : k));
    }
}
/* a thick soft line */
static void stroke(float ax, float ay, float bx, float by, float w, col3 c, float a, float soft) {
  float mnx = fminf(ax, bx) - w - soft, mxx = fmaxf(ax, bx) + w + soft, mny = fminf(ay, by) - w - soft, mxy = fmaxf(ay, by) + w + soft;
  float ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey;
  for (int py = PY(mxy); py <= PY(mny); py++)
    for (int px = PX(mnx); px <= PX(mxx); px++) {
      float x = WX(px), y = WY(py), t = l2 > 0 ? ((x - ax) * ex + (y - ay) * ey) / l2 : 0;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      float dx = x - (ax + ex * t), dy = y - (ay + ey * t), d = sqrtf(dx * dx + dy * dy);
      float k = d < w ? 1 : soft > 0 ? 1 - (d - w) / soft : 0;
      if (k > 0) put(px, py, c, a * k);
    }
}
/* a filled convex polygon (metres) */
static void poly(const float (*p)[2], int n, col3 c, float a) {
  float mnx = 9, mxx = -9, mny = 9, mxy = -9;
  for (int i = 0; i < n; i++) mnx = fminf(mnx, p[i][0]), mxx = fmaxf(mxx, p[i][0]), mny = fminf(mny, p[i][1]), mxy = fmaxf(mxy, p[i][1]);
  for (int py = PY(mxy); py <= PY(mny); py++)
    for (int px = PX(mnx); px <= PX(mxx); px++) {
      float x = WX(px), y = WY(py);
      bool in = false;
      for (int i = 0, j = n - 1; i < n; j = i++)
        if ((p[i][1] > y) != (p[j][1] > y) && x < (p[j][0] - p[i][0]) * (y - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0]) in = !in;
      if (in) put(px, py, c, a);
    }
}

static void pattern(const table *T) {
  const theme *th = &T->th;
  col3 b0 = C(th->base), b1 = C(th->base2);
  for (int py = 0; py < AH; py++)
    for (int px = 0; px < AW; px++) {
      float x = WX(px), y = WY(py), v = y / TL;
      col3 c = mixc(b0, b1, v * v * 0.8f + 0.1f);
      float n = fbm(x * 18, y * 18, 3);
      if (th->pattern == 0) { /* the reef: light bands from above, caustic cells, a sandy bottom */
        float band = 0.5f + 0.5f * sinf((x * 1.6f + y) * 26 + fbm(x * 9, y * 9, 7) * 6);
        float caus = powf(fmaxf(0, 1 - fabsf(fbm(x * 40, y * 40, 11) - 0.5f) * 7), 3);
        c = scl(c, 0.82f + 0.25f * band * v + 0.12f * n);
        c = mixc(c, scl(b1, 2.2f), caus * 0.35f * (0.4f + v));
        if (y < 0.3f) c = mixc(c, (col3){ 0.45f, 0.32f, 0.16f }, (0.3f - y) / 0.3f * 0.55f * (0.7f + 0.3f * n));
      } else if (th->pattern == 1) { /* the forge: nebula, stars, plated panels with rivets */
        float neb = fbm(x * 6, y * 6, 21);
        c = mixc(c, scl(C(th->col[3]), 0.35f), powf(neb, 3) * 1.2f);
        c = mixc(c, scl(C(th->col[1]), 0.25f), powf(fbm(x * 5 + 3, y * 5, 31), 4) * 1.5f);
        uint32_t h = hsh(px / 3, py / 3, 5);
        if ((h & 1023) < 3) c = mixc(c, (col3){ 1, 1, 1 }, 0.8f);
        float gx = fmodf(fabsf(x + 0.26f), 0.065f), gy = fmodf(y, 0.065f);
        if (y < 0.36f && (gx < 0.0012f || gy < 0.0012f)) c = scl(c, 0.5f);
        if (y < 0.36f) c = mixc(c, scl(b1, 0.5f), 0.3f);
      } else { /* the fair: a sunburst from the top, stripes, a warm glow */
        float ang = atan2f(y - 1.0f, x + 0.0165f), ray = sinf(ang * 26);
        c = scl(c, 0.78f + 0.3f * (ray > 0 ? 1.0f : 0.0f) * (0.4f + 0.6f * v));
        float stripe = fmodf((x - y) * 22 + 100, 1.0f);
        if (y < 0.4f) c = mixc(c, stripe < 0.5f ? scl(C(th->col[2]), 0.3f) : scl(b0, 0.9f), 0.2f + 0.25f * (0.4f - y));
        c = scl(c, 0.9f + 0.18f * n);
      }
      buf[py * AW + px] = c;
    }
  /* reef: coral fans up the sides; forge: orbit rings; fair: bulb rows */
  if (th->pattern == 0) {
    for (int k = 0; k < 9; k++) {
      float bx = -0.24f + (float)k * 0.055f, by = 0.4f + (float)(hsh(k, 1, 2) & 255) / 255.0f * 0.5f;
      if (bx > -0.18f && bx < 0.17f) continue;
      col3 cc = scl(C(th->col[k % 2 ? 0 : 3]), 0.55f);
      for (int br = 0; br < 7; br++) {
        float a = 1.2f + (float)br * 0.12f + ((float)(hsh(k, br, 3) & 255) / 255.0f - 0.5f) * 0.3f, l = 0.05f + 0.03f * (float)(hsh(k, br, 4) & 255) / 255.0f;
        stroke(bx, by, bx + cosf(a + (bx > 0 ? 0.5f : -0.5f)) * l, by + sinf(a) * l, 0.003f, cc, 0.7f, 0.002f);
      }
    }
    for (int k = 0; k < 60; k++) {
      float x = -0.25f + (float)(hsh(k, 9, 1) & 1023) / 1023.0f * 0.46f, y = 0.35f + (float)(hsh(k, 9, 2) & 1023) / 1023.0f * 0.7f;
      ring(x, y, 0.002f, 0.003f + 0.004f * (float)(hsh(k, 9, 3) & 255) / 255.0f, scl(C(th->base2), 2.4f), 0.35f);
    }
  } else if (th->pattern == 1) {
    for (int k = 0; k < 5; k++) ring(-0.03f, 0.79f, 0.06f + 0.035f * (float)k, 0.0625f + 0.035f * (float)k, scl(C(th->col[k % 2 ? 1 : 0]), 0.9f), 0.45f);
    disc(-0.03f, 0.79f, 0.05f, scl(C(th->col[2]), 1.2f), 0.35f);
  } else {
    for (int k = 0; k < 16; k++) {
      disc(-0.235f, 0.42f + (float)k * 0.034f, 0.0035f, scl(C(th->col[2]), 1.4f), 0.85f);
      disc(0.198f, 0.42f + (float)k * 0.022f, 0.0035f, scl(C(th->col[2]), 1.4f), 0.85f);
    }
  }
}

/* each table's illustration, painted under the inserts at half strength: the creature coiling up through the reef,
 * the forge ring round its new sun over the magnet with the pour running down, the fair's wheel lit over the flippers */
static void illustration(const table *T) {
  const theme *th = &T->th;
  if (th->pattern == 0) {
    col3 body = scl(C(th->col[1]), 0.32f), plate = scl(C(th->col[1]), 0.7f), eye = scl(C(th->col[2]), 1.6f);
    float px = 0, py = 0;
    for (int i = 0; i <= 160; i++) {
      float t = (float)i / 160.0f;
      float x = -0.17f + t * 0.2f + sinf(t * 9.0f) * 0.075f * (1 - t * 0.5f), y = 0.33f + t * 0.36f;
      float r = 0.018f - t * 0.007f;
      disc(x, y, r, body, 0.55f);
      if (i % 5 == 0) ring(x, y, r * 0.75f, r, plate, 0.45f);
      px = x, py = y;
    }
    disc(px + 0.012f, py + 0.008f, 0.02f, body, 0.6f);
    disc(px + 0.02f, py + 0.012f, 0.004f, eye, 0.8f);
    for (int k = 0; k < 12; k++) { /* a shoal */
      float x = -0.2f + (float)(hsh(k, 4, 9) & 1023) / 1023.0f * 0.4f, y = 0.72f + (float)(hsh(k, 5, 9) & 1023) / 1023.0f * 0.2f;
      disc(x, y, 0.0045f, scl(C(th->col[0]), 0.6f), 0.5f);
      float tri[3][2] = { { x - 0.004f, y }, { x - 0.011f, y + 0.004f }, { x - 0.011f, y - 0.004f } };
      poly((const float (*)[2])tri, 3, scl(C(th->col[0]), 0.6f), 0.5f);
    }
  } else if (th->pattern == 1) {
    float cx = -0.025f, cy = 0.665f;
    disc(cx, cy, 0.05f, scl(C(th->col[2]), 0.9f), 0.35f);
    ring(cx, cy, 0.062f, 0.075f, scl(C(th->col[0]), 0.5f), 0.55f);
    for (int k = 0; k < 16; k++) {
      float a = (float)k / 16 * 6.2832f;
      stroke(cx + cosf(a) * 0.075f, cy + sinf(a) * 0.075f, cx + cosf(a) * 0.086f, cy + sinf(a) * 0.086f, 0.004f, scl(C(th->col[0]), 0.5f), 0.55f, 0.001f);
    }
    for (int i = 0; i < 40; i++) { /* the pour */
      float t = (float)i / 40;
      disc(cx + 0.03f + sinf(t * 3) * 0.02f, cy - 0.08f - t * 0.2f, 0.007f - t * 0.003f, scl(C(th->col[0]), 1.0f), 0.4f);
    }
  } else {
    float cx = -0.017f, cy = 0.35f, r = 0.085f;
    col3 steel = scl(C(th->col[3]), 0.55f), bulb = scl(C(th->col[2]), 1.3f);
    ring(cx, cy, r - 0.003f, r, steel, 0.6f), ring(cx, cy, r * 0.5f - 0.002f, r * 0.5f, steel, 0.5f);
    for (int k = 0; k < 12; k++) {
      float a = (float)k / 12 * 6.2832f;
      stroke(cx, cy, cx + cosf(a) * r, cy + sinf(a) * r, 0.0012f, steel, 0.5f, 0.0008f);
      disc(cx + cosf(a) * r, cy + sinf(a) * r, 0.004f, bulb, 0.7f);
    }
    stroke(cx, cy, cx - 0.05f, cy - 0.12f, 0.0025f, steel, 0.5f, 0.001f), stroke(cx, cy, cx + 0.05f, cy - 0.12f, 0.0025f, steel, 0.5f, 0.001f);
  }
}

uint8_t *art_paint(const table *T, int *w, int *h) {
  buf = malloc(sizeof(col3) * AW * AH);
  pattern(T);
  illustration(T);
  const theme *th = &T->th;
  /* the shots marked in paint: a wedge of the shot's colour behind each arrow, pointing up its lane */
  for (int s = 0; s < T->nshot; s++) {
    const lamp *l = &T->lamps[T->shots[s].arrow];
    col3 c = scl(C(th->col[l->col]), 0.6f);
    float a = l->ang * 3.14159265f / 180.0f, fx = cosf(a), fy = sinf(a), sx = -fy, sy = fx;
    float p[3][2] = { { l->x + fx * 0.05f, l->y + fy * 0.05f }, { l->x - fx * 0.03f + sx * 0.03f, l->y - fy * 0.03f + sy * 0.03f }, { l->x - fx * 0.03f - sx * 0.03f, l->y - fy * 0.03f - sy * 0.03f } };
    poly((const float (*)[2])p, 3, c, 0.35f);
  }
  /* rays from the flippers up the table, the classic lower-playfield frame */
  for (int k = 0; k < 2; k++) {
    float x = k ? 0.06f : -0.093f;
    stroke(x, 0.16f, x + (k ? 0.12f : -0.12f), 0.45f, 0.0015f, scl(C(th->col[2]), 1.1f), 0.5f, 0.001f);
  }
  /* the apron's middle: a panel behind the shoot again and bonus inserts */
  {
    float p[4][2] = { { -0.09f, 0.06f }, { 0.057f, 0.06f }, { 0.04f, 0.25f }, { -0.073f, 0.25f } };
    poly((const float (*)[2])p, 4, (col3){ 0.02f, 0.02f, 0.03f }, 0.45f);
  }
  /* every insert sits in a printed ring of its colour */
  for (int i = 0; i < T->nlamp; i++) {
    const lamp *l = &T->lamps[i];
    col3 c = C(th->col[l->col]);
    disc(l->x, l->y, l->size * 1.9f, scl(c, 0.25f), 0.5f);
    ring(l->x, l->y, l->size * 1.35f, l->size * 1.6f, scl(c, 1.1f), 0.8f);
  }
  /* holes: dark, with a lit rim of paint */
  for (int i = 0; i < T->nel; i++) {
    const elem *e = &T->el[i];
    if (e->kind == EK_HOLE) disc(e->x, e->y, 0.017f, (col3){ 0.003f, 0.003f, 0.004f }, 1);
    if (e->kind == EK_POP) disc(e->x, e->y, 0.034f, scl(C(th->col[e->col]), 0.2f), 0.6f);
  }
  /* shadows: every wall and post darkens the paint close by (soft, like the light from above) */
  for (int wi = 0; wi < T->nwall; wi++) {
    const wall *wl = &T->walls[wi];
    int segs = wl->closed ? wl->n : wl->n - 1;
    for (int i = 0; i < segs; i++) {
      int j = (i + 1) % wl->n;
      stroke(wl->p[i][0], wl->p[i][1], wl->p[j][0], wl->p[j][1], wl->r + 0.001f, (col3){ 0, 0, 0 }, 0.45f, 0.007f);
    }
  }
  for (int i = 0; i < T->nel; i++)
    if (T->el[i].kind == EK_POST) disc(T->el[i].x, T->el[i].y - 0.001f, T->el[i].w + 0.006f, (col3){ 0, 0, 0 }, 0.35f);
  /* the shooter lane and the outside of the cabinet: plain dark wood-stained board */
  for (int py = 0; py < AH; py++)
    for (int px = 0; px < AW; px++) {
      float x = WX(px), y = WY(py);
      bool lane = x > 0.219f && y < 0.82f;
      float dx = x + 0.0005f, dy = y - 0.82f;
      bool outside = x < -0.258f || x > 0.257f || (y > 0.82f && dx * dx + dy * dy > 0.2575f * 0.2575f);
      if (lane) buf[py * AW + px] = mixc(buf[py * AW + px], (col3){ 0.05f, 0.04f, 0.035f }, 0.85f);
      if (outside) buf[py * AW + px] = scl(C(th->wood), 0.7f + 0.3f * vnoise(x * 400, y * 12, 9));
    }
  uint8_t *out = malloc(AW * AH * 4);
  for (int i = 0; i < AW * AH; i++) {
    out[i * 4] = (uint8_t)(srgb(buf[i].r) * 255 + 0.5f);
    out[i * 4 + 1] = (uint8_t)(srgb(buf[i].g) * 255 + 0.5f);
    out[i * 4 + 2] = (uint8_t)(srgb(buf[i].b) * 255 + 0.5f);
    out[i * 4 + 3] = 255;
  }
  free(buf);
  buf = NULL;
  *w = AW, *h = AH;
  return out;
}
