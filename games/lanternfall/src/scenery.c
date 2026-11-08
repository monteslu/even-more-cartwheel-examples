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
#include "world.h"

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

static void paint_props(void);
static void paint_supports(void);
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
  paint_props();
  paint_supports();
}

void scenery_decal(int material, float x, float y, uint32_t h, cw_color body, float room) {
  const decal *d = &DEC[material][h % NDEC];
  float sc = 0.75f + (float)((h >> 8) & 255) / 255.0f * 0.5f, rot = ((float)((h >> 16) & 255) / 255.0f - 0.5f) * 0.8f;
  float big = sqrtf(d->w * d->w + d->h * d->h) * sc; /* (its turned corners stay inside the rock) */
  if (big > room) sc *= room / big;
  bool flip = (h >> 24) & 1;
  /* the rock's own colour lifted to the feature's values (the rock body is dark: a feature reads a step lighter) */
  cw_color c = { fminf(1, body.r * 2.25f), fminf(1, body.g * 2.25f), fminf(1, body.b * 2.25f), 1 };
#ifdef LANTERNFALL_R2D_V2
  if (d->glow) cw_r2d_set_intensity(1.6f);
#endif
  cw_r2d_sprite(&(cw_sprite){ .texture = d->tx.t[flip], .pos = { x, y }, .size = { d->w * sc, d->h * sc }, .pivot = { 0.5f, 0.5f }, .rotation = rot, .color = c });
#ifdef LANTERNFALL_R2D_V2
  if (d->glow) cw_r2d_set_intensity(1);
#endif
}

/* ═════════════ props: painted (the rest keep their drawn shapes in view.c) ═════════════ */
typedef struct prop_part { ptex tx; float w, h, px, py; } prop_part;
static struct {
  prop_part statue, slantern, trunk, crown[3], blossom, crystals, crystal_small, housing, gear, piston, shelf, boulder, shrooms, caps, grass, fern, flowers;
} PP;
static prop_part pfin(float pvx, float pvy, float k) {
  prop_part p = { pc_finish(C, k), (float)C->w / SPT, (float)C->h / SPT, 0, 0 };
  p.px = pvx * SPT / (float)C->w, p.py = pvy * SPT / (float)C->h;
  pc_free(C), C = NULL;
  return p;
}
static void P(const float *xy, int n, float r, const pstyle *s) {
  float q[128];
  for (int i = 0; i < n * 2 && i < 128; i++) q[i] = xy[i] * SPT;
  pc_poly(C, q, n, r * SPT, s);
}

static void paint_props(void) {
  /* the statue: a hooded lamplighter of weathered stone on a plinth of blocks, moss in the folds, the lantern held out */
  {
    cvs(2.2f, 3.6f);
    pstyle stone = st(0xb8b4aa, 0x5a5650, 0.9f, TX_STONE, 0.05f, 0.9f);
    stone.ink = 1.8f, stone.rim = 0.4f, stone.shadow = 0.45f, stone.shadow_r = 10;
    float pl[8] = { 0.25f, 2.9f, 1.95f, 2.9f, 1.95f, 3.58f, 0.25f, 3.58f };
    P(pl, 4, 0.03f, &stone);
    pstyle joint = st(0x3a3632, 0, -0.2f, TX_NONE, 0, 0);
    joint.alpha = 0.8f;
    float j1[4] = { 0.27f, 3.22f, 1.93f, 3.22f }, j2[4] = { 1.1f, 2.92f, 1.1f, 3.22f }, j3[4] = { 0.7f, 3.22f, 0.7f, 3.56f }, j4[4] = { 1.5f, 3.22f, 1.5f, 3.56f };
    K(j1, 2, 0.012f, 0.012f, &joint), K(j2, 2, 0.012f, 0.012f, &joint), K(j3, 2, 0.012f, 0.012f, &joint), K(j4, 2, 0.012f, 0.012f, &joint);
    float robe[16] = { 0.5f, 2.92f, 1.7f, 2.92f, 1.5f, 2.0f, 1.42f, 1.1f, 1.25f, 0.62f, 0.95f, 0.62f, 0.78f, 1.1f, 0.68f, 2.0f };
    P(robe, 8, 0.06f, &stone);
    pstyle fold = st(0x5a5650, 0, -0.2f, TX_STONE, 0.03f, 0.5f);
    fold.alpha = 0.6f, fold.soft = 3;
    float f1[6] = { 0.95f, 1.2f, 0.9f, 2.0f, 0.82f, 2.88f }, f2[6] = { 1.25f, 1.2f, 1.3f, 2.0f, 1.38f, 2.88f };
    K(f1, 3, 0.02f, 0.035f, &fold), K(f2, 3, 0.02f, 0.035f, &fold);
    float hood[12] = { 0.8f, 0.72f, 0.82f, 0.32f, 1.1f, 0.08f, 1.38f, 0.3f, 1.36f, 0.7f, 1.1f, 0.82f };
    P(hood, 6, 0.06f, &stone);
    pstyle dark = st(0x24221e, 0, -0.4f, TX_NONE, 0, 0);
    dark.soft = 4;
    E(1.24f, 0.46f, 0.1f, 0.15f, 0.2f, &dark);
    float arm[6] = { 1.35f, 1.0f, 1.65f, 1.15f, 1.95f, 1.08f };
    K(arm, 3, 0.1f, 0.075f, &stone);
    pstyle moss = st(0x7aa04a, 0x2a4418, 0.4f, TX_MOSS, 0.025f, 1.0f);
    moss.soft = 5, moss.alpha = 0.85f;
    E(0.6f, 2.88f, 0.25f, 0.07f, 0, &moss), E(1.55f, 2.9f, 0.3f, 0.06f, 0, &moss), E(0.9f, 0.7f, 0.12f, 0.05f, 0.4f, &moss), E(0.75f, 1.9f, 0.06f, 0.14f, 0.1f, &moss);
    PP.statue = pfin(1.1f, 3.58f, 2.5f);
    cvs(0.5f, 0.7f);
    pstyle glass = st(0xffe8a0, 0xffa030, 0.3f, TX_NONE, 0, 0);
    glass.em = 1;
    E(0.25f, 0.4f, 0.12f, 0.16f, 0, &glass);
    pstyle iron = st(0x5a4a3a, 0x1a140e, 0.7f, TX_METAL, 0.02f, 0.5f);
    iron.ink = 1, iron.spec = 0.6f;
    float c1[4] = { 0.1f, 0.22f, 0.4f, 0.22f }, c2[4] = { 0.1f, 0.6f, 0.4f, 0.6f };
    K(c1, 2, 0.035f, 0.035f, &iron), K(c2, 2, 0.04f, 0.04f, &iron);
    for (int k = 0; k < 3; k++) { float b[4] = { 0.12f + 0.13f * (float)k, 0.22f, 0.12f + 0.13f * (float)k, 0.6f }; K(b, 2, 0.012f, 0.012f, &iron); }
    E(0.25f, 0.12f, 0.06f, 0.06f, 0, &iron);
    PP.slantern = pfin(0.25f, 0.08f, 2.5f);
  }
  /* the garden tree: a trunk with bark and flared roots, forking boughs; leaf clusters painted apart (they sway) */
  {
    cvs(3.2f, 4.6f);
    pstyle bark = st(0x7a5a40, 0x2a1a10, 0.8f, TX_BARK, 0.05f, 0.9f);
    bark.ink = 1.8f, bark.rim = 0.35f, bark.shadow = 0.4f, bark.shadow_r = 8;
    float tr[8] = { 1.6f, 4.55f, 1.55f, 3.2f, 1.7f, 1.8f, 1.6f, 0.6f };
    K(tr, 4, 0.3f, 0.12f, &bark);
    float r1[6] = { 1.5f, 4.2f, 1.1f, 4.45f, 0.75f, 4.58f }, r2[6] = { 1.7f, 4.2f, 2.1f, 4.45f, 2.5f, 4.58f };
    K(r1, 3, 0.16f, 0.05f, &bark), K(r2, 3, 0.16f, 0.05f, &bark);
    float b1[6] = { 1.65f, 2.4f, 2.3f, 1.7f, 2.85f, 1.35f }, b2[6] = { 1.62f, 2.0f, 1.0f, 1.3f, 0.45f, 1.0f }, b3[6] = { 1.66f, 1.4f, 2.0f, 0.9f, 2.2f, 0.4f };
    K(b1, 3, 0.11f, 0.04f, &bark), K(b2, 3, 0.1f, 0.035f, &bark), K(b3, 3, 0.08f, 0.03f, &bark);
    pstyle moss = st(0x8ab05a, 0x2a4418, 0.3f, TX_MOSS, 0.025f, 1.0f);
    moss.soft = 4, moss.alpha = 0.7f;
    E(1.48f, 3.3f, 0.07f, 0.4f, 0.05f, &moss);
    PP.trunk = pfin(1.6f, 4.55f, 2.5f);
    for (int v = 0; v < 3; v++) { /* a leaf cluster: many small leaves round a mass, lit from above */
      cvs(1.6f, 1.3f);
      pstyle mass = st(0x6a9a4a, 0x1e3a14, 0.7f, TX_MOSS, 0.03f, 0.9f);
      mass.soft = 4, mass.seed = 600 + (uint32_t)v, mass.shadow = 0.3f;
      E(0.8f, 0.68f, 0.62f, 0.45f, 0, &mass);
      for (int i = 0; i < 26; i++) {
        float a = H(i, 640 + v) * 6.28f, r = 0.35f + 0.3f * H(i, 650 + v), cx = 0.8f + cosf(a) * r * 1.15f, cy = 0.68f + sinf(a) * r * 0.85f;
        pstyle lf = st(i & 1 ? 0x8ac05a : 0x6aa448, 0x1e3a14, 0.5f, TX_VEINS, 0.02f, 0.5f);
        lf.ink = 0.9f, lf.seed = (uint32_t)(700 + i + v * 40), lf.rim = cy < 0.6f ? 0.5f : 0;
        L(cx, cy, cx + cosf(a + 0.6f) * 0.17f, cy + sinf(a + 0.6f) * 0.17f, 0.055f, 0.01f, &lf);
      }
      PP.crown[v] = pfin(0.8f, 0.68f, 2.5f);
    }
    cvs(0.3f, 0.3f);
    pstyle bl = st(0xfff0f0, 0xd08aa0, 0.5f, TX_NONE, 0, 0);
    bl.em = 0.4f, bl.ink = 0.8f;
    for (int k = 0; k < 5; k++) L(0.15f, 0.15f, 0.15f + cosf((float)k * 1.2566f) * 0.13f, 0.15f + sinf((float)k * 1.2566f) * 0.13f, 0.045f, 0, &bl);
    pstyle cen = st(0xffd060, 0, 0.4f, TX_NONE, 0, 0);
    cen.em = 0.6f;
    E(0.15f, 0.15f, 0.03f, 0.03f, 0, &cen);
    PP.blossom = pfin(0.15f, 0.15f, 2);
  }
  /* crystals: a tall cluster, and a small one */
  for (int big = 1; big >= 0; big--) {
    float W = big ? 2.6f : 0.8f, Hh = big ? 3.2f : 0.7f;
    cvs(W, Hh);
    pstyle sock = st(0x2a2c3a, 0, -0.3f, TX_STONE, 0.03f, 0.6f);
    sock.soft = 4;
    E(W * 0.5f, Hh - 0.05f, W * 0.42f, 0.08f, 0, &sock);
    int n = big ? 6 : 3;
    for (int i = 0; i < n; i++) {
      float u = ((float)i + 0.5f) / (float)n, bx = W * (0.15f + 0.7f * u), hgt = Hh * (0.45f + 0.55f * H(i, 660 + big)) * (1 - fabsf(u - 0.5f) * 0.6f);
      float lean = (H(i, 670 + big) - 0.5f) * 0.6f * (big ? 1 : 0.3f), w = W * (big ? 0.075f : 0.12f);
      pstyle c = st(0xd0f0ff, 0x3a5a9a, 0.6f, TX_GLASS, big ? 0.1f : 0.04f, 0.8f);
      c.ink = 1.6f, c.spec = 1, c.gloss = 0.95f, c.em = 0.3f, c.rim = 0.6f, c.seed = (uint32_t)(680 + i);
      float tip[10] = { bx - w, Hh - 0.04f, bx + w, Hh - 0.04f, bx + w * 0.8f + lean * hgt * 0.4f, Hh - hgt * 0.8f, bx + lean * hgt * 0.5f, Hh - hgt, bx - w * 0.8f + lean * hgt * 0.4f, Hh - hgt * 0.8f };
      P(tip, 5, 0.01f, &c);
      pstyle face = c;
      face.base = pc_hex(0x8ab0e0), face.alpha = 0.6f, face.ink = 0, face.rim = 0;
      float fc[8] = { bx, Hh - 0.04f, bx + w, Hh - 0.04f, bx + w * 0.8f + lean * hgt * 0.4f, Hh - hgt * 0.8f, bx + lean * hgt * 0.5f, Hh - hgt };
      P(fc, 4, 0.0f, &face);
    }
    if (big) PP.crystals = pfin(W * 0.5f, Hh, 3);
    else PP.crystal_small = pfin(W * 0.5f, Hh, 3);
  }
  /* a machine: a riveted housing with a gauge and a pipe, a gear, a piston */
  {
    cvs(2.4f, 1.6f);
    pstyle steel = st(0x7a8494, 0x2a303a, 0.6f, TX_METAL, 0.06f, 0.9f);
    steel.ink = 1.8f, steel.spec = 0.7f, steel.gloss = 0.55f, steel.rim = 0.4f, steel.shadow = 0.4f;
    float hs[8] = { 0.1f, 0.15f, 2.3f, 0.15f, 2.3f, 1.55f, 0.1f, 1.55f };
    P(hs, 4, 0.08f, &steel);
    pstyle plate = steel;
    plate.base = pc_hex(0x8a94a4), plate.dome = 0.3f, plate.lift = 0.3f, plate.ink = 1.2f;
    float pp1[8] = { 0.25f, 0.35f, 1.1f, 0.35f, 1.1f, 1.4f, 0.25f, 1.4f }, pp2[8] = { 1.3f, 0.35f, 2.15f, 0.35f, 2.15f, 1.4f, 1.3f, 1.4f };
    P(pp1, 4, 0.04f, &plate), P(pp2, 4, 0.04f, &plate);
    pstyle riv = st(0xc8d0d8, 0x4a5058, 0.9f, TX_NONE, 0, 0);
    riv.spec = 1;
    for (int k = 0; k < 9; k++) E(0.2f + (float)k * 0.25f, 0.25f, 0.03f, 0.03f, 0, &riv), E(0.2f + (float)k * 0.25f, 1.47f, 0.03f, 0.03f, 0, &riv);
    pstyle brass = st(0xd8a848, 0x5a3a10, 0.7f, TX_METAL, 0.03f, 0.6f);
    brass.spec = 0.9f, brass.ink = 1.2f;
    E(1.72f, 0.85f, 0.22f, 0.22f, 0, &brass);
    pstyle face = st(0xf0e8d0, 0x8a8070, 0.3f, TX_NONE, 0, 0);
    E(1.72f, 0.85f, 0.16f, 0.16f, 0, &face);
    pstyle ndl = st(0x8a2a20, 0, 0.2f, TX_NONE, 0, 0);
    float nd[4] = { 1.72f, 0.85f, 1.8f, 0.74f };
    K(nd, 2, 0.012f, 0.006f, &ndl);
    pstyle rust = st(0x8a4a2a, 0, 0.1f, TX_STONE, 0.03f, 0.8f);
    rust.alpha = 0.45f, rust.soft = 12;
    E(0.5f, 1.3f, 0.35f, 0.18f, 0, &rust);
    PP.housing = pfin(1.2f, 1.55f, 2.5f);
    cvs(2.3f, 2.3f);
    float g[160];
    for (int i = 0; i < 80; i++) {
      float a = (float)i / 80 * 6.2832f, r = (((int)((float)i / 80 * 24)) & 1) ? 0.95f : 1.1f;
      g[i * 2] = (1.15f + cosf(a) * r) * SPT, g[i * 2 + 1] = (1.15f + sinf(a) * r) * SPT;
    }
    pc_poly(C, g, 80, 0.015f * SPT, &brass);
    pstyle hole = st(0x1a1410, 0, -0.4f, TX_NONE, 0, 0);
    for (int i = 0; i < 6; i++) E(1.15f + cosf((float)i * 1.047f) * 0.55f, 1.15f + sinf((float)i * 1.047f) * 0.55f, 0.17f, 0.17f, 0, &hole);
    E(1.15f, 1.15f, 0.22f, 0.22f, 0, &steel);
    PP.gear = pfin(1.15f, 1.15f, 3);
    cvs(0.35f, 1.6f);
    pstyle rod = st(0xc8d0d8, 0x4a5058, 0.8f, TX_METAL, 0.02f, 0.5f);
    rod.spec = 1, rod.gloss = 0.8f, rod.ink = 1;
    float rd[4] = { 0.175f, 0.05f, 0.175f, 1.55f };
    K(rd, 2, 0.1f, 0.1f, &rod);
    PP.piston = pfin(0.175f, 1.55f, 2.5f);
  }
  /* a shelf of old books: a carved frame, books of every colour, some leaning, a scroll, dust */
  {
    cvs(1.9f, 2.7f);
    pstyle wood = st(0x6a4630, 0x24140c, 0.6f, TX_BARK, 0.04f, 0.7f);
    wood.ink = 1.6f, wood.rim = 0.3f;
    float fr[8] = { 0.05f, 0.05f, 1.85f, 0.05f, 1.85f, 2.65f, 0.05f, 2.65f };
    P(fr, 4, 0.04f, &wood);
    pstyle back = st(0x2a1a12, 0, -0.2f, TX_BARK, 0.05f, 0.5f);
    for (int r = 0; r < 3; r++) {
      float ry = 0.2f + (float)r * 0.85f;
      float bk[8] = { 0.15f, ry, 1.75f, ry, 1.75f, ry + 0.7f, 0.15f, ry + 0.7f };
      P(bk, 4, 0.01f, &back);
      static const uint32_t BOOK[6] = { 0x9a3a3a, 0x3a5a9a, 0x6a8a3a, 0xc8a060, 0x6a3a7a, 0x3a7a7a };
      float bx = 0.18f;
      for (int k = 0; k < 11 && bx < 1.68f; k++) {
        float bw = 0.09f + 0.05f * H(k, 700 + r), bh = 0.45f + 0.2f * H(k, 710 + r), lean = H(k, 720 + r) < 0.15f ? 0.25f : 0;
        pstyle b = st(BOOK[(k + r * 2) % 6], 0, 0.5f, TX_CLOTH, 0.02f, 0.6f);
        b.ink = 1.1f, b.seed = (uint32_t)(730 + k + r * 20);
        float pb[8] = { bx, ry + 0.7f, bx + bw, ry + 0.7f, bx + bw + lean * bh, ry + 0.7f - bh, bx + lean * bh, ry + 0.7f - bh };
        P(pb, 4, 0.01f, &b);
        pstyle band = st(0xe8c870, 0, 0.3f, TX_NONE, 0, 0);
        band.alpha = 0.8f;
        float bd[4] = { bx + 0.01f + lean * bh * 0.2f, ry + 0.7f - bh * 0.2f, bx + bw - 0.01f + lean * bh * 0.2f, ry + 0.7f - bh * 0.2f };
        K(bd, 2, 0.008f, 0.008f, &band);
        bx += bw + 0.012f + lean * bh;
      }
      float sh[8] = { 0.05f, ry + 0.7f, 1.85f, ry + 0.7f, 1.85f, ry + 0.8f, 0.05f, ry + 0.8f };
      P(sh, 4, 0.01f, &wood);
    }
    pstyle web = st(0xe8e8e8, 0, 0.1f, TX_NONE, 0, 0);
    web.alpha = 0.35f;
    for (int k = 0; k < 4; k++) { float wb[4] = { 1.75f, 0.2f, 1.75f - 0.12f * (float)k, 0.2f + 0.1f * (float)(4 - k) }; K(wb, 2, 0.004f, 0.004f, &web); }
    PP.shelf = pfin(0.95f, 2.65f, 2.5f);
  }
  /* a boulder, mushrooms with glowing caps, a grass tuft, a fern, flowers */
  {
    cvs(0.9f, 0.6f);
    pstyle b = st(0xa8a29a, 0x4a4440, 0.9f, TX_STONE, 0.04f, 0.9f);
    b.ink = 1.5f, b.rim = 0.4f, b.shadow = 0.4f;
    E(0.45f, 0.36f, 0.4f, 0.24f, 0, &b);
    E(0.3f, 0.3f, 0.18f, 0.12f, 0.2f, &b);
    pstyle moss = st(0x7aa04a, 0x2a4418, 0.4f, TX_MOSS, 0.02f, 1.0f);
    moss.soft = 3;
    E(0.45f, 0.16f, 0.25f, 0.05f, 0, &moss);
    PP.boulder = pfin(0.45f, 0.58f, 2.5f);
    cvs(0.9f, 0.7f);
    pstyle stem = st(0xf0e8d8, 0x8a8070, 0.6f, TX_SKIN, 0.02f, 0.4f);
    stem.ink = 1;
    for (int k = 0; k < 3; k++) { float sx = 0.2f + 0.25f * (float)k, s1[4] = { sx, 0.68f, sx + 0.02f, 0.68f - (0.25f + 0.12f * (float)k) }; K(s1, 2, 0.035f, 0.03f, &stem); }
    PP.shrooms = pfin(0.45f, 0.68f, 2.5f);
    cvs(0.9f, 0.7f);
    for (int k = 0; k < 3; k++) {
      float sx = 0.22f + 0.25f * (float)k, cy = 0.68f - (0.25f + 0.12f * (float)k), r = 0.12f + 0.03f * (float)k;
      pstyle cap = st(0xffffff, 0xa0a0a0, 0.8f, TX_SKIN, 0.02f, 0.5f);
      cap.em = 0.6f, cap.ink = 1.2f, cap.seed = (uint32_t)(800 + k);
      float cp[10] = { sx - r, cy, sx - r * 0.8f, cy - r * 0.6f, sx, cy - r * 0.85f, sx + r * 0.8f, cy - r * 0.6f, sx + r, cy };
      P(cp, 5, 0.02f, &cap);
      pstyle dot = st(0xffffff, 0, 0.3f, TX_NONE, 0, 0);
      dot.em = 1;
      E(sx - r * 0.3f, cy - r * 0.45f, 0.018f, 0.015f, 0, &dot), E(sx + r * 0.35f, cy - r * 0.35f, 0.014f, 0.012f, 0, &dot);
    }
    PP.caps = pfin(0.45f, 0.68f, 2);
    cvs(0.9f, 0.8f);
    for (int k = 0; k < 11; k++) {
      float bx = 0.12f + 0.065f * (float)k, h = 0.4f + 0.3f * H(k, 820), ln = (H(k, 821) - 0.5f) * 0.25f;
      pstyle bl = st(0xb0d080, 0x3a5a20, 0.4f, TX_VEINS, 0.02f, 0.3f);
      bl.ink = 0.7f, bl.seed = (uint32_t)(830 + k), bl.rim = 0.4f;
      L(bx, 0.78f, bx + ln, 0.78f - h, 0.022f, ln * 0.3f, &bl);
    }
    PP.grass = pfin(0.45f, 0.78f, 2);
    cvs(1.2f, 0.8f);
    for (int k = -2; k <= 2; k++) {
      float a = -1.5708f + (float)k * 0.45f, l = 0.6f - fabsf((float)k) * 0.08f, ex = 0.6f + cosf(a) * l, ey = 0.78f + sinf(a) * l;
      pstyle fr = st(0x80b050, 0x2a4a18, 0.3f, TX_FEATHER, 0.012f, 0.8f);
      fr.ink = 0.8f, fr.seed = (uint32_t)(840 + k), fr.rim = 0.3f, fr.tex_dir = a + 1.57f;
      L(0.6f, 0.78f, ex, ey, 0.09f, (float)k * 0.04f, &fr);
    }
    PP.fern = pfin(0.6f, 0.78f, 2);
    cvs(0.9f, 0.7f);
    for (int k = 0; k < 3; k++) {
      float sx = 0.2f + 0.25f * (float)k, h = 0.35f + 0.1f * (float)k, s1[6] = { sx, 0.68f, sx + 0.02f, 0.68f - h * 0.5f, sx + 0.03f, 0.68f - h };
      pstyle stem = st(0x6a9a4a, 0x24401a, 0.4f, TX_NONE, 0, 0);
      K(s1, 3, 0.018f, 0.014f, &stem);
      pstyle lf = st(0x7aaa50, 0x24401a, 0.4f, TX_VEINS, 0.02f, 0.4f);
      L(sx + 0.01f, 0.68f - h * 0.4f, sx + 0.12f, 0.68f - h * 0.5f, 0.03f, 0, &lf);
      pstyle pet = st(0xffffff, 0xb0a0b0, 0.5f, TX_VEINS, 0.015f, 0.3f);
      pet.ink = 0.8f, pet.em = 0.2f;
      for (int p = 0; p < 5; p++) L(sx + 0.03f, 0.68f - h, sx + 0.03f + cosf((float)p * 1.2566f) * 0.075f, 0.68f - h + sinf((float)p * 1.2566f) * 0.075f, 0.03f, 0, &pet);
      pstyle cen = st(0xffd060, 0, 0.4f, TX_NONE, 0, 0);
      E(sx + 0.03f, 0.68f - h, 0.02f, 0.02f, 0, &cen);
    }
    PP.flowers = pfin(0.45f, 0.68f, 2);
  }
}

static void pput(const prop_part *p, float x, float y, float rot, float sx, float sy, bool flip, cw_color c) {
  cw_r2d_sprite(&(cw_sprite){ .texture = p->tx.t[flip], .pos = { x, y }, .size = { p->w * sx, p->h * sy }, .pivot = { flip ? 1 - p->px : p->px, p->py }, .rotation = rot, .color = c });
}
static cw_color cmul(cw_color a, float k) { return (cw_color){ fminf(1, a.r * k), fminf(1, a.g * k), fminf(1, a.b * k), a.a }; }
static cw_color cmix(cw_color a, cw_color b, float t) { return pc_mix(a, b, t); }

bool scenery_prop(int sub, int arg, float x, float y, int region, float t) {
  const region_info *g = &REGIONS[region];
  cw_color leaf = cmix(pc_hex(g->top), pc_hex(g->accent), 0.3f), acc = pc_hex(g->accent), ground = pc_hex(g->ground), white = { 1, 1, 1, 1 };
  float sway = sinf(t * 1.3f + x * 0.7f) * 0.06f;
  bool flip = ((int)(x * 7) & 1) != 0;
#define GLOW(k) cw_r2d_set_intensity(k), cw_r2d_set_emissive((k) > 1 ? 1.0f : 0.0f)
  switch (sub) {
  case 0: /* tall grass, swaying */
    cw_r2d_set_sway(0.6f);
    pput(&PP.grass, x, y, 0, 1, 1, flip, cmix(leaf, white, 0.15f));
    cw_r2d_set_sway(0);
    return true;
  case 1: /* mushrooms: pale stems, caps glowing the accent */
    pput(&PP.shrooms, x, y, 0, 1, 1, flip, white);
#ifdef LANTERNFALL_R2D_V2
    GLOW(1.6f);
#endif
    pput(&PP.caps, x, y, 0, 1, 1, flip, acc);
#ifdef LANTERNFALL_R2D_V2
    GLOW(1);
#endif
    return true;
  case 2: pput(&PP.boulder, x, y + 0.02f, 0, 0.8f + 0.4f * (float)((int)(x * 3) % 3) / 2, 0.9f, flip, cmul(ground, 1.9f)); return true;
  case 3: /* a fern */
    cw_r2d_set_sway(0.5f);
    pput(&PP.fern, x, y, sway * 0.5f, 0.9f, 0.9f, flip, cmix(leaf, white, 0.1f));
    cw_r2d_set_sway(0);
    return true;
  case 4: /* small crystals */
#ifdef LANTERNFALL_R2D_V2
    GLOW(1.25f);
#endif
    pput(&PP.crystal_small, x, y, 0, 1, 1, flip, cmix(acc, white, 0.4f));
#ifdef LANTERNFALL_R2D_V2
    GLOW(1);
#endif
    return true;
  case 5: /* flowers */
    cw_r2d_set_sway(0.6f);
    pput(&PP.flowers, x, y, 0, 1, 1, flip, cmix(acc, white, 0.35f));
    cw_r2d_set_sway(0);
    return true;
  case 10: { /* PR_MACHINE: the housing, its gear turning behind, the piston pumping */
    uint8_t v = (uint8_t)arg;
    float wd = 2.0f + (float)(v & 3) * 0.3f, ht = 1.1f + (float)((v >> 2) & 3) * 0.25f, spin = t * (0.6f + (float)(v & 1)) * ((v & 8) ? 1 : -1);
    float gx = x + ((v & 16) ? 0.6f : -0.6f), gy = y - ht - 0.7f, pump = 0.25f * sinf(t * 2.4f + x);
    cw_color tint = cmix(white, acc, 0.15f);
    pput(&PP.gear, gx, gy, spin, 0.9f, 0.9f, false, tint);
    pput(&PP.piston, x - wd * 0.15f + 0.11f, y - ht + 0.05f + pump, 0, 1, 1, false, white);
    pput(&PP.housing, x, y, 0, wd / 2.2f, ht / 1.4f, flip, white);
    return true; /* (the furnace mouth and the pilot light: still drawn by view.c over it) */
  }
  case 11: { /* PR_TREE */
    uint8_t v = (uint8_t)arg;
    float h = 3.0f + (float)(v & 3) * 0.45f, lean = ((float)((v >> 2) & 3) - 1.5f) * 0.18f, sw = sinf(t * 0.9f + x) * 0.05f;
    cw_color bark = cmix(cmul(ground, 1.6f), (cw_color){ 0.6f, 0.45f, 0.35f, 1 }, 0.5f), lf = cmix(leaf, (cw_color){ 0.5f, 0.8f, 0.45f, 1 }, 0.3f);
    pput(&PP.trunk, x, y, lean * 0.6f, h / 4.4f, h / 4.4f, (v & 32) != 0, bark);
    float tx = x + lean * h * 0.65f + sw, ty = y - h;
    for (int k = 0; k < 7; k++) {
      float a = (float)k * 0.9f + (float)v, cx = tx + cosf(a) * 1.0f, cy = ty + sinf(a) * 0.55f + 0.2f, s = 0.85f + 0.2f * (float)((v + k) % 3);
      pput(&PP.crown[(k + v) % 3], cx + sw * (1 + (float)(k & 1)), cy, sinf(t * 0.8f + (float)k) * 0.03f, s, s, (k & 1) != 0, cmul(lf, 0.85f + 0.08f * (float)(k % 3)));
    }
    for (int k = 0; k < 6; k++) pput(&PP.blossom, tx + cosf((float)k * 2.1f + (float)v) * 1.3f + sw, ty + sinf((float)k * 1.7f) * 0.7f, 0, 0.8f, 0.8f, false, cmix(acc, white, 0.5f));
    return true;
  }
  case 12: { /* PR_CLUSTER */
#ifdef LANTERNFALL_R2D_V2
    GLOW(1.25f);
#endif
    pput(&PP.crystals, x, y, 0, 0.9f + 0.2f * (float)(arg & 3) / 3, 0.85f + 0.3f * (float)((arg >> 2) & 3) / 3, (arg & 16) != 0, cmix(acc, white, 0.3f));
#ifdef LANTERNFALL_R2D_V2
    GLOW(1);
#endif
    return true;
  }
  case 14: pput(&PP.shelf, x, y, 0, 1, 1, (arg & 1) != 0, white); return true; /* PR_SHELF */
  case 15: { /* PR_STATUE */
    bool left = !(arg & 1);
    pput(&PP.statue, x, y, 0, 1, 1, left, cmix(white, pc_hex(g->top), 0.12f));
    float lx = x + (left ? -0.85f : 0.85f), ly = y - 3.58f + 1.05f;
#ifdef LANTERNFALL_R2D_V2
    GLOW(2.2f);
#endif
    pput(&PP.slantern, lx, ly, sinf(t * 0.8f + x) * 0.04f, 1, 1, false, white);
#ifdef LANTERNFALL_R2D_V2
    GLOW(1);
#endif
    return true;
  }
  default: return false;
  }
#undef GLOW
}

/* ═════════════ what holds a one-way ledge up (the eye must see it attached) ═════════════ */
static struct { prop_part bracket, chain, cleat, roots[2]; } LS;
static void paint_supports(void) {
  /* a bracket: an iron-shod timber arm under the ledge, a post against the wall, a brace between, bolts */
  cvs(1.0f, 1.0f);
  pstyle wood = st(0x7a5638, 0x2a1a10, 0.6f, TX_BARK, 0.03f, 0.7f);
  wood.ink = 1.4f, wood.rim = 0.35f, wood.shadow = 0.35f, wood.shadow_r = 6;
  float arm[8] = { 0.02f, 0.02f, 0.95f, 0.02f, 0.95f, 0.14f, 0.02f, 0.14f }, post[8] = { 0.02f, 0.02f, 0.14f, 0.02f, 0.14f, 0.95f, 0.02f, 0.95f };
  P(arm, 4, 0.02f, &wood), P(post, 4, 0.02f, &wood);
  float br[4] = { 0.1f, 0.85f, 0.85f, 0.1f };
  K(br, 2, 0.06f, 0.05f, &wood);
  pstyle iron = st(0x5a5a62, 0x1a1a20, 0.8f, TX_METAL, 0.02f, 0.5f);
  iron.spec = 0.7f, iron.ink = 1;
  E(0.08f, 0.08f, 0.05f, 0.05f, 0, &iron), E(0.85f, 0.08f, 0.035f, 0.035f, 0, &iron), E(0.08f, 0.85f, 0.035f, 0.035f, 0, &iron), E(0.47f, 0.48f, 0.035f, 0.035f, 0, &iron);
  LS.bracket = pfin(0.02f, 0.02f, 2.5f);
  /* a chain: one tile of links, drawn as many times as it is long */
  cvs(0.3f, 1.0f);
  pstyle link = st(0xb0b0b8, 0x4a4a52, 0.8f, TX_METAL, 0.015f, 0.6f);
  link.spec = 0.8f, link.gloss = 0.6f, link.ink = 1;
  pstyle hole = st(0x101014, 0, -0.3f, TX_NONE, 0, 0);
  for (int k = 0; k < 4; k++) {
    float cy = 0.125f + 0.25f * (float)k;
    if (k & 1) { E(0.15f, cy, 0.035f, 0.15f, 0, &link); }
    else { E(0.15f, cy, 0.09f, 0.15f, 0, &link), E(0.15f, cy, 0.04f, 0.09f, 0, &hole); }
  }
  LS.chain = pfin(0.15f, 0.0f, 2.5f);
  cvs(0.5f, 0.25f);
  pstyle plate = st(0x6a6a72, 0x22222a, 0.6f, TX_METAL, 0.02f, 0.5f);
  plate.ink = 1.2f, plate.spec = 0.6f;
  float pl[8] = { 0.03f, 0.03f, 0.47f, 0.03f, 0.47f, 0.2f, 0.03f, 0.2f };
  P(pl, 4, 0.03f, &plate);
  E(0.12f, 0.11f, 0.03f, 0.03f, 0, &iron), E(0.38f, 0.11f, 0.03f, 0.03f, 0, &iron);
  LS.cleat = pfin(0.25f, 0.11f, 2.5f);
  /* roots knotted under the ledge, hanging free below it */
  for (int v = 0; v < 2; v++) {
    cvs(1.1f, 1.0f);
    pstyle rt = st(0x7a5a40, 0x2a1a10, 0.6f, TX_BARK, 0.025f, 0.7f);
    rt.ink = 1.2f, rt.rim = 0.3f, rt.shadow = 0.3f;
    pstyle mass = rt;
    mass.dome = 0.4f;
    E(0.55f, 0.08f, 0.5f, 0.1f, 0, &mass);
    for (int i = 0; i < 5; i++) {
      float x0 = 0.1f + 0.22f * (float)i + (H(i, 900 + v) - 0.5f) * 0.1f, l = 0.35f + 0.55f * H(i, 910 + v), p[8] = { x0, 0.08f, x0 + (H(i, 920 + v) - 0.5f) * 0.2f, 0.08f + l * 0.4f,
                                                                                                                x0 + (H(i, 930 + v) - 0.5f) * 0.3f, 0.08f + l * 0.75f, x0 + (H(i, 940 + v) - 0.5f) * 0.3f, 0.08f + l };
      rt.seed = (uint32_t)(950 + i + v * 10);
      K(p, 4, 0.045f, 0.008f, &rt);
    }
    pstyle moss = st(0x7aa04a, 0x2a4418, 0.3f, TX_MOSS, 0.02f, 1.0f);
    moss.soft = 3, moss.alpha = 0.8f;
    E(0.4f + 0.3f * (float)v, 0.1f, 0.18f, 0.05f, 0, &moss);
    LS.roots[v] = pfin(0.55f, 0.02f, 2.5f);
  }
}

void scenery_ledge(int x0, int x1, int y, bool wall_l, bool wall_r, int ceil_l, int ceil_r, int material, cw_color tint) {
  (void)material;
  float top = (float)y + 0.36f; /* (under the ledge's plank) */
  cw_color white = { 1, 1, 1, 1 };
  cw_color wood = pc_mix(white, tint, 0.25f);
  /* walls beside it: a bracket from each; no wall but a ceiling over it: chains from the ceiling to its ends; else roots */
  if (wall_l) pput(&LS.bracket, (float)x0, top, 0, 0.9f, 0.9f, false, wood);
  if (wall_r) pput(&LS.bracket, (float)x1 + 1, top, 0, 0.9f, 0.9f, true, wood);
  for (int side = 0; side < 2; side++) {
    if (side ? wall_r : wall_l) continue;
    int c = side ? ceil_r : ceil_l;
    float cx = side ? (float)x1 + 0.75f : (float)x0 + 0.25f;
    if (c > 0 && c <= 12) { /* (a chain: c tiles up to the rock) */
      float y0 = (float)(y - c + 1);
      for (float yy = y0; yy < (float)y + 0.05f; yy += 1) pput(&LS.chain, cx, yy, 0, 1, fminf(1, (float)y + 0.1f - yy), false, white);
      pput(&LS.cleat, cx, (float)y + 0.3f, 0, 0.8f, 0.8f, false, white);
      pput(&LS.cleat, cx, y0 + 0.05f, 0, 0.8f, 0.8f, false, white);
    } else /* roots knotted under that end */
      pput(&LS.roots[(x0 + side) & 1], cx + (side ? -0.2f : 0.2f), top - 0.05f, 0, 0.8f, 0.8f, side != 0, pc_mix(white, tint, 0.3f));
  }
  for (int x = x0 + 1; x < x1; x += 2) /* and a knot of roots under its middle now and then */
    if (((x * 7 + y * 3) & 3) == 0) pput(&LS.roots[x & 1], (float)x + 0.5f, top - 0.05f, 0, 0.6f, 0.6f, (x & 2) != 0, pc_mix(white, tint, 0.3f));
}
