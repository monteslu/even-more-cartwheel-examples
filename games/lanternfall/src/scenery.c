/* lanternfall: what is painted into the world's rock (scenery.h): features set into the rock's body so no stretch of it
 * reads as one repeating pattern: embedded boulders, crack networks, lenses of strata, fossil shells, pebbles, moss,
 * roots, crystals, glowing ore, old gears, shells and barnacles, cloud wisps; each region's material its own set. Painted
 * in code with the painter (paint.c: colour, normal and material maps), drawn on the lit tiles layer, placed by hash
 * on rock two tiles deep or more (so a feature never hangs over air). Render code only. */
#include "scenery.h"

#include <math.h>
#include <stdlib.h>

#include "art.h"
#include "paint.h"
#include "view.h"

#define SPT 96.0f /* painted pixels a tile */
enum { NDEC = 6 };
typedef struct decal { ptex tx; float w, h; bool glow; } decal;
static decal DEC[NMAT][NDEC];

static pcan *C;
static void cvs(float w, float h) { C = pc_new((int)ceilf(w * SPT), (int)ceilf(h * SPT)); }
static decal fin(float k, bool glow) {
  decal d = { pc_finish(C, k), (float)C->w / SPT, (float)C->h / SPT, glow };
  pc_free(C), C = NULL;
  return d;
}
static void E(float cx, float cy, float rx, float ry, float rot, const pstyle *s) { pc_ellipse(C, cx * SPT, cy * SPT, rx * SPT, ry * SPT, rot, s); }
static void L(float ax, float ay, float bx, float by, float w, float bend, const pstyle *s) { pc_leaf(C, ax * SPT, ay * SPT, bx * SPT, by * SPT, w * SPT, bend * SPT, s); }
static void K(const float *xy, int n, float r0, float r1, const pstyle *s) {
  float q[64];
  for (int i = 0; i < n * 2 && i < 64; i++) q[i] = xy[i] * SPT;
  pc_stroke(C, q, n, r0 * SPT, r1 * SPT, s);
}
static pstyle st(uint32_t base, uint32_t shade, float dome, int tex, float ts, float ta) {
  return (pstyle){ .base = pc_hex(base), .shade = shade ? pc_hex(shade) : (cw_color){ 0 }, .dome = dome, .tex = tex, .tex_scale = ts * SPT, .tex_amt = ta,
                   .seed = base ^ (uint32_t)tex * 131u };
}
static float H(int i, int s) { return pn_hash(i, 3, (uint32_t)s); }

enum { K_BOULDER, K_CRACKS, K_STRATA, K_FOSSIL, K_PEBBLES, K_MOSS, K_ROOTS, K_CRYSTAL, K_ORE, K_GEAR, K_SHELLS, K_CLOUD };

/* the values are around the rock's own (the rock's colour multiplies them), a little lighter in the lit parts */
static decal paint(int kind, int var) {
  int sd = var * 17 + kind * 101;
  switch (kind) {
  case K_BOULDER: {
    cvs(1.5f, 1.3f);
    pstyle sock = st(0x302c2a, 0, -0.4f, TX_STONE, 0.05f, 0.5f);
    sock.soft = 6, sock.alpha = 0.8f;
    E(0.75f, 0.66f, 0.62f, 0.52f, 0, &sock);
    pstyle b = st(0xa8a29a, 0x4a4440, 0.9f, TX_STONE, 0.07f, 0.9f);
    b.ink = 1.6f, b.rim = 0.35f, b.seed = (uint32_t)sd;
    E(0.75f, 0.62f, 0.5f + H(var, 1) * 0.08f, 0.4f, (H(var, 2) - 0.5f) * 0.8f, &b);
    pstyle cr = st(0x24201e, 0, -0.3f, TX_NONE, 0, 0);
    float c1[6] = { 0.5f, 0.35f, 0.7f, 0.6f, 0.62f, 0.9f };
    K(c1, 3, 0.012f, 0.004f, &cr);
    return fin(2.5f, false);
  }
  case K_CRACKS: {
    cvs(1.6f, 1.6f);
    pstyle cr = st(0x1e1a18, 0, -0.5f, TX_NONE, 0, 0);
    pstyle lip = st(0xb0a89c, 0, 0.2f, TX_NONE, 0, 0);
    lip.alpha = 0.5f;
    for (int i = 0; i < 4; i++) {
      float a = H(var * 7 + i, 3) * 6.28f, l = 0.35f + H(i, var + 4) * 0.35f;
      float p[8] = { 0.8f, 0.8f, 0.8f + cosf(a) * l * 0.4f, 0.8f + sinf(a) * l * 0.4f + 0.05f, 0.8f + cosf(a + 0.3f) * l * 0.75f, 0.8f + sinf(a + 0.3f) * l * 0.75f,
                     0.8f + cosf(a + 0.1f) * l, 0.8f + sinf(a + 0.1f) * l };
      float q[8];
      for (int k = 0; k < 8; k++) q[k] = p[k] + (k & 1 ? -0.012f : 0);
      K(q, 4, 0.012f, 0.003f, &lip);
      K(p, 4, 0.02f, 0.004f, &cr);
    }
    return fin(3, false);
  }
  case K_STRATA: {
    cvs(1.7f, 0.9f);
    for (int i = 0; i < 5; i++) {
      pstyle b = st(i & 1 ? 0x8a8278 : 0xb4aa9a, 0x4a4440, 0.3f, TX_STONE, 0.04f, 0.6f);
      b.soft = 3, b.ink = 0.8f, b.seed = (uint32_t)(sd + i);
      E(0.85f, 0.2f + 0.12f * (float)i, 0.8f - fabsf((float)i - 2) * 0.12f, 0.05f, (H(var, 5) - 0.5f) * 0.15f, &b);
    }
    return fin(2, false);
  }
  case K_FOSSIL: {
    cvs(1.0f, 1.0f);
    pstyle sh = st(0xd0c4ac, 0x6a604e, 0.6f, TX_CHITIN, 0.02f, 0.6f);
    sh.ink = 1.2f, sh.rim = 0.3f;
    float p[48];
    int n = 0;
    for (int k = 0; k < 24; k++) { /* a spiral shell */
      float a = (float)k * 0.42f, r = 0.4f * expf(-(float)k * 0.075f);
      p[n++] = 0.5f + cosf(a) * r, p[n++] = 0.5f + sinf(a) * r;
    }
    K(p, 24, 0.085f, 0.02f, &sh);
    pstyle rib = st(0x5a5040, 0, -0.2f, TX_NONE, 0, 0);
    rib.alpha = 0.7f;
    for (int k = 0; k < 12; k++) {
      float a = (float)k * 0.42f, r = 0.4f * expf(-(float)k * 0.075f), q[4] = { 0.5f + cosf(a) * (r - 0.06f), 0.5f + sinf(a) * (r - 0.06f), 0.5f + cosf(a) * (r + 0.06f), 0.5f + sinf(a) * (r + 0.06f) };
      K(q, 2, 0.006f, 0.006f, &rib);
    }
    return fin(2.5f, false);
  }
  case K_PEBBLES: {
    cvs(1.4f, 1.0f);
    static const uint32_t PC[5] = { 0xb0a898, 0x8a8478, 0xc0a880, 0x9a8a7a, 0xa0a8a0 };
    for (int i = 0; i < 9; i++) {
      pstyle p = st(PC[(i + var) % 5], 0x3a3630, 0.8f, TX_STONE, 0.02f, 0.6f);
      p.ink = 1.1f, p.shadow = 0.4f, p.shadow_r = 4, p.seed = (uint32_t)(sd + i);
      E(0.25f + H(i, sd) * 0.9f, 0.25f + H(i, sd + 1) * 0.5f, 0.06f + H(i, sd + 2) * 0.07f, 0.05f + H(i, sd + 3) * 0.04f, H(i, sd + 4) * 3, &p);
    }
    return fin(2.5f, false);
  }
  case K_MOSS: {
    cvs(1.5f, 1.0f);
    pstyle m = st(0x7aa04a, 0x2a4418, 0.5f, TX_MOSS, 0.03f, 1.0f);
    m.soft = 6, m.rim = 0.3f, m.seed = (uint32_t)sd;
    for (int i = 0; i < 4; i++) E(0.4f + 0.22f * (float)i, 0.5f + (H(i, sd) - 0.5f) * 0.3f, 0.22f, 0.15f, 0, &m);
    return fin(2, false);
  }
  case K_ROOTS: {
    cvs(1.7f, 1.4f);
    pstyle r = st(0x8a6a4a, 0x3a2414, 0.7f, TX_BARK, 0.03f, 0.7f);
    r.ink = 1.3f, r.rim = 0.3f, r.shadow = 0.35f, r.shadow_r = 5;
    for (int i = 0; i < 3; i++) {
      float y0 = 0.15f + 0.45f * H(i, sd), p[8] = { 0.05f, y0, 0.5f, y0 + (H(i, sd + 1) - 0.5f) * 0.6f, 1.1f, y0 + (H(i, sd + 2) - 0.5f) * 0.6f, 1.65f, 0.2f + 0.9f * H(i, sd + 3) };
      for (int k = 1; k < 8; k += 2) p[k] = fminf(1.3f, fmaxf(0.1f, p[k]));
      r.seed = (uint32_t)(sd + i);
      K(p, 4, 0.07f - 0.015f * (float)i, 0.02f, &r);
    }
    return fin(2.5f, false);
  }
  case K_CRYSTAL: {
    cvs(1.2f, 1.2f);
    pstyle sock = st(0x1a1c2a, 0, -0.4f, TX_NONE, 0, 0);
    sock.soft = 6, sock.alpha = 0.7f;
    E(0.6f, 0.75f, 0.4f, 0.22f, 0, &sock);
    pstyle c = st(0xa8e0ff, 0x2a4a8a, 0.6f, TX_GLASS, 0.06f, 0.8f);
    c.ink = 1.3f, c.spec = 1, c.gloss = 0.9f, c.em = 0.35f, c.rim = 0.5f;
    for (int i = 0; i < 4; i++) {
      float a = -2.0f + 0.4f * (float)i + (H(i, sd) - 0.5f) * 0.3f, l = 0.35f + 0.25f * H(i, sd + 1);
      c.seed = (uint32_t)(sd + i);
      L(0.6f, 0.78f, 0.6f + cosf(a) * l, 0.78f + sinf(a) * l, 0.07f, 0, &c);
    }
    return fin(3, true);
  }
  case K_ORE: {
    cvs(1.6f, 1.2f);
    pstyle v = st(0xffa040, 0xc04010, 0.3f, TX_VEINS, 0.03f, 0.6f);
    v.em = 0.9f, v.soft = 2;
    for (int i = 0; i < 3; i++) {
      float p[8] = { 0.1f, 0.3f + 0.3f * H(i, sd), 0.6f, 0.2f + 0.8f * H(i, sd + 1), 1.0f, 0.2f + 0.8f * H(i, sd + 2), 1.5f, 0.3f + 0.6f * H(i, sd + 3) };
      K(p, 4, 0.03f, 0.008f, &v);
    }
    pstyle fl = st(0xffe0a0, 0xd08020, 0.6f, TX_NONE, 0, 0);
    fl.em = 1;
    for (int i = 0; i < 8; i++) E(0.15f + H(i, sd + 5) * 1.3f, 0.15f + H(i, sd + 6) * 0.9f, 0.02f, 0.02f, 0, &fl);
    return fin(2, true);
  }
  case K_GEAR: {
    cvs(1.2f, 1.2f);
    pstyle br = st(0xc89a48, 0x4a2a0c, 0.7f, TX_METAL, 0.04f, 0.8f);
    br.spec = 0.9f, br.gloss = 0.6f, br.ink = 1.5f, br.rim = 0.35f;
    float g[96];
    int teeth = 10 + var * 2;
    for (int i = 0; i < 48; i++) {
      float a = (float)i / 48 * 6.2832f, r = (((int)((float)i / 48 * (float)teeth * 2)) & 1) ? 0.42f : 0.5f;
      g[i * 2] = (0.6f + cosf(a) * r) * SPT, g[i * 2 + 1] = (0.6f + sinf(a) * r) * SPT;
    }
    pc_poly(C, g, 48, 0.01f * SPT, &br);
    pstyle hole = st(0x141010, 0, -0.4f, TX_NONE, 0, 0);
    for (int i = 0; i < 5; i++) E(0.6f + cosf((float)i * 1.2566f) * 0.24f, 0.6f + sinf((float)i * 1.2566f) * 0.24f, 0.07f, 0.07f, 0, &hole);
    pstyle rust = st(0x8a4a2a, 0, 0.1f, TX_STONE, 0.03f, 0.8f);
    rust.alpha = 0.5f, rust.soft = 10;
    E(0.45f, 0.75f, 0.25f, 0.18f, 0.4f, &rust);
    return fin(3, false);
  }
  case K_SHELLS: {
    cvs(1.3f, 1.0f);
    pstyle sh = st(0xf0c0a8, 0x8a4a40, 0.7f, TX_CHITIN, 0.02f, 0.6f);
    sh.ink = 1.2f, sh.rim = 0.3f;
    for (int i = 0; i < 3; i++) { /* scallops: a fan of ribs */
      float cx = 0.3f + 0.35f * (float)i, cy = 0.45f + (H(i, sd) - 0.5f) * 0.3f, r = 0.15f + 0.06f * H(i, sd + 1);
      sh.seed = (uint32_t)(sd + i);
      for (int k = 0; k < 7; k++) {
        float a = -2.6f + (float)k * 0.36f;
        L(cx, cy + r * 0.6f, cx + cosf(a) * r, cy + r * 0.6f + sinf(a) * r, 0.035f, 0, &sh);
      }
    }
    pstyle bar = st(0xd8d0c0, 0x5a5048, 0.8f, TX_WARTS, 0.015f, 0.6f);
    bar.ink = 1;
    for (int i = 0; i < 6; i++) E(0.15f + H(i, sd + 3) * 1.0f, 0.75f + H(i, sd + 4) * 0.15f, 0.045f, 0.04f, 0, &bar);
    return fin(2.5f, false);
  }
  default: { /* cloud wisps */
    cvs(1.6f, 1.0f);
    pstyle c = st(0xffffff, 0xb0bcd8, 0.6f, TX_NONE, 0, 0);
    c.soft = 8, c.alpha = 0.7f;
    for (int i = 0; i < 5; i++) E(0.3f + 0.25f * (float)i, 0.5f + (H(i, sd) - 0.5f) * 0.25f, 0.18f + 0.08f * H(i, sd + 1), 0.14f, 0, &c);
    return fin(1.5f, false);
  }
  }
}

void scenery_init(void) {
  static const int SET[NMAT][NDEC] = {
    [MAT_STONE] = { K_BOULDER, K_CRACKS, K_STRATA, K_FOSSIL, K_PEBBLES, K_MOSS },
    [MAT_EARTH] = { K_ROOTS, K_PEBBLES, K_BOULDER, K_ROOTS, K_MOSS, K_CRACKS },
    [MAT_CRYSTAL] = { K_CRYSTAL, K_CRYSTAL, K_CRACKS, K_BOULDER, K_PEBBLES, K_STRATA },
    [MAT_BRASS] = { K_ORE, K_GEAR, K_BOULDER, K_CRACKS, K_ORE, K_PEBBLES },
    [MAT_CORAL] = { K_SHELLS, K_FOSSIL, K_PEBBLES, K_BOULDER, K_SHELLS, K_MOSS },
    [MAT_CLOUD] = { K_CLOUD, K_CLOUD, K_PEBBLES, K_STRATA, K_CRACKS, K_BOULDER },
  };
  for (int m = 0; m < NMAT; m++)
    for (int v = 0; v < NDEC; v++) DEC[m][v] = paint(SET[m][v], v);
}

void scenery_decal(int material, float x, float y, uint32_t h, cw_color body, float room) {
  const decal *d = &DEC[material][h % NDEC];
  float sc = 0.75f + (float)((h >> 8) & 255) / 255.0f * 0.5f, rot = ((float)((h >> 16) & 255) / 255.0f - 0.5f) * 0.8f;
  float big = sqrtf(d->w * d->w + d->h * d->h) * sc; /* (its turned corners stay inside the rock) */
  if (big > room) sc *= room / big;
  bool flip = (h >> 24) & 1;
  /* the rock's own colour lifted to the feature's values (the rock body is dark: a feature reads a step lighter) */
  cw_color c = { fminf(1, body.r * 2.1f), fminf(1, body.g * 2.1f), fminf(1, body.b * 2.1f), 1 };
#ifdef LANTERNFALL_R2D_V2
  if (d->glow) cw_r2d_set_intensity(1.6f);
#endif
  cw_r2d_sprite(&(cw_sprite){ .texture = d->tx.t[flip], .pos = { x, y }, .size = { d->w * sc, d->h * sc }, .pivot = { 0.5f, 0.5f }, .rotation = rot, .color = c });
#ifdef LANTERNFALL_R2D_V2
  if (d->glow) cw_r2d_set_intensity(1);
#endif
}
