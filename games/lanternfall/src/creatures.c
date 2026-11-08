/* lanternfall: the creatures (creatures.h). Painted at PPT pixels a tile (4K draws a tile at 120), mipmapped for
 * smaller screens. Every part is painted facing right; a creature facing left draws the mirrored copies.
 *
 * Who they are (original designs):
 *   crawler   an ember-seamed husk beetle: armour plates over a furnace-hot body, the seams glow as it rears
 *   spitter   a lantern pod: a rooted gourd with a glowing sac inside, its lip-petals peel back before it spits
 *   moth      a dusk moth: a furred body, patterned wings whose eyespots blaze when it flares them to swoop
 *   hopper    a lamp toad: a warty cave toad whose throat sac glows as it crouches to leap
 *   knight    a hollow sentinel: old city-watch armour walked by a wisp, tower shield and spear with a pennant */
#include "creatures.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "paint.h"
#include "view.h"

#define PPT 128.0f /* painted pixels a tile */

/* ── parts ── */
typedef struct part { ptex tx; float px, py, w, h; } part; /* pivot (0..1), size in tiles */

static pcan *C; /* the canvas being painted */
static pcan *cv(float w, float h) { return C = pc_new((int)ceilf(w * PPT), (int)ceilf(h * PPT)); }
static part done(float pvx, float pvy, float nk) { /* pivot in tiles from the canvas's top-left */
  part p = { pc_finish(C, nk), pvx * PPT / (float)C->w, pvy * PPT / (float)C->h, (float)C->w / PPT, (float)C->h / PPT };
  pc_free(C), C = NULL;
  return p;
}
/* painting in tiles */
static void el(float cx, float cy, float rx, float ry, float rot, const pstyle *s) { pc_ellipse(C, cx * PPT, cy * PPT, rx * PPT, ry * PPT, rot, s); }
static void cap(float ax, float ay, float bx, float by, float ra, float rb, const pstyle *s) { pc_capsule(C, ax * PPT, ay * PPT, bx * PPT, by * PPT, ra * PPT, rb * PPT, s); }
static void lf(float ax, float ay, float bx, float by, float w, float bend, const pstyle *s) { pc_leaf(C, ax * PPT, ay * PPT, bx * PPT, by * PPT, w * PPT, bend * PPT, s); }
static void stk(const float *xy, int n, float r0, float r1, const pstyle *s) {
  float q[64];
  for (int i = 0; i < n * 2 && i < 64; i++) q[i] = xy[i] * PPT;
  pc_stroke(C, q, n, r0 * PPT, r1 * PPT, s);
}
static void poly(const float *xy, int n, float round, const pstyle *s) {
  float q[128];
  for (int i = 0; i < n * 2 && i < 128; i++) q[i] = xy[i] * PPT;
  pc_poly(C, q, n, round * PPT, s);
}
static pstyle sty(uint32_t base, uint32_t shade, uint32_t hi, float dome, int tex, float tscale, float tamt) {
  return (pstyle){ .base = pc_hex(base), .shade = shade ? pc_hex(shade) : (cw_color){ 0 }, .hi = hi ? pc_hex(hi) : (cw_color){ 0 }, .dome = dome, .tex = tex,
                   .tex_scale = tscale * PPT, .tex_amt = tamt, .seed = base ^ (uint32_t)tex * 7919u };
}

/* ── posing: a frame of parts, drawn in local coordinates facing right (x forward, y down, origin at the feet) ── */
typedef struct pose { float x, y, f, flash, die, alpha, s; cw_color tint, flash_c; uint32_t seed; int idx; } pose;
static pose D;
typedef struct qd { cw_sprite s; float glow; bool rim; } qd;
#define MAXQ 160
static qd Q[MAXQ];
static int nq;
static cw_color rim_c;
static float rim_k;

static cw_vec2 W(float lx, float ly) { return (cw_vec2){ D.x + D.f * lx * D.s, D.y + ly * D.s }; } /* (D.s: the whole creature drawn bigger) */
static cw_vec2 rot2(float x, float y, float a) { float c = cosf(a), s = sinf(a); return (cw_vec2){ x * c - y * s, x * s + y * c }; }
static float hsh(int i, uint32_t s) { return pn_hash(i, 7, s); }

/* a part at local (lx, ly), turned by a (radians clockwise, facing right), scaled; mirror flips it on its own
 * (the far wing of a moth); glow > 0 draws it emissive at that intensity; tint 0 = untinted */
static void putx(const part *p, float lx, float ly, float a, float sx, float sy, bool mirror, float glow, cw_color col, bool rim) {
  if (nq >= MAXQ) return;
  bool left = (D.f < 0) != mirror;
  float alpha = D.alpha;
  if (D.die > 0) { /* death: each part flies its own way and turns as it falls */
    int i = D.idx++;
    float k = D.die, vx = (hsh(i, D.seed) - 0.5f) * 3.2f, vy = -1.2f - hsh(i, D.seed + 1) * 1.8f;
    lx += vx * k, ly += vy * k + 7 * k * k;
    a += (hsh(i, D.seed + 2) - 0.5f) * 7 * k;
    alpha *= 1 - k * k;
  }
  cw_vec2 w = W(lx, ly);
  float px = left ? 1 - p->px : p->px;
  col.a *= alpha;
  Q[nq++] = (qd){ .s = { .texture = p->tx.t[left ? 1 : 0], .pos = w, .size = { p->w * sx * D.s, p->h * sy * D.s }, .pivot = { px, p->py }, .rotation = D.f * a, .color = col },
                  .glow = glow, .rim = rim };
}
static void put(const part *p, float lx, float ly, float a, float sx, float sy) { putx(p, lx, ly, a, sx, sy, false, 0, D.tint, true); }
static void putk(const part *p, float lx, float ly, float a, float sx, float sy, cw_color c) { putx(p, lx, ly, a, sx, sy, false, 0, (cw_color){ D.tint.r * c.r, D.tint.g * c.g, D.tint.b * c.b, c.a }, true); }
static void putg(const part *p, float lx, float ly, float a, float sx, float sy, float glow, float alpha) { putx(p, lx, ly, a, sx, sy, false, glow, (cw_color){ 1, 1, 1, alpha }, false); }

static void begin(float x, float y, float f, float flash, int region, uint32_t seed) {
  nq = 0;
  D = (pose){ .x = x, .y = y, .f = f, .flash = flash, .alpha = 1, .s = 1, .seed = seed };
  cw_color acc = pc_hex(REGIONS[region].accent), top = pc_hex(REGIONS[region].top);
  D.tint = pc_mix((cw_color){ 1, 1, 1, 1 }, pc_mix(acc, top, 0.5f), 0.16f);
  /* the rim: the region's light catching the top edges (warm in the cool regions, the accent in the warm) */
  rim_c = pc_mix(acc, (cw_color){ 1, 0.95f, 0.85f, 1 }, 0.45f);
  rim_k = 0.75f;
}
static void glowing(float k) {
#ifdef LANTERNFALL_R2D_V2
  cw_r2d_set_emissive(k > 1 ? 1.0f : 0.0f);
  cw_r2d_set_intensity(k);
#else
  (void)k;
#endif
}
/* the frame out: a rim of light behind every part (shifted up: light from above), then the parts in order */
static void flush(void) {
  if (D.alpha > 0.05f && rim_k > 0) {
    glowing(1.25f);
#ifdef LANTERNFALL_R2D_V2
    cw_r2d_set_flash(1);
#endif
    float dy = 0.035f * (1 - D.die);
    for (int i = 0; i < nq; i++) {
      if (!Q[i].rim) continue;
      cw_sprite s = Q[i].s;
      s.pos.y -= dy;
      s.color = (cw_color){ rim_c.r, rim_c.g, rim_c.b, rim_k * s.color.a };
      cw_r2d_sprite(&s);
    }
  }
  float fl = D.flash > D.die ? D.flash : D.die * 0.9f;
#ifdef LANTERNFALL_R2D_V2
  cw_r2d_set_flash(fl);
#endif
  float cur = -1;
  for (int i = 0; i < nq; i++) {
    float g = Q[i].glow > 0 ? Q[i].glow : 1;
    if (g != cur) glowing(g), cur = g;
    cw_sprite s = Q[i].s;
    if (fl > 0) { /* the hurt flash: white; the death flash: embers */
      cw_color fc = D.die > D.flash ? (cw_color){ 1, 0.55f, 0.25f, 1 } : (cw_color){ 1, 1, 1, 1 };
      s.color = (cw_color){ fc.r, fc.g, fc.b, s.color.a };
      if (Q[i].glow > 0) s.color = Q[i].s.color;
    }
    cw_r2d_sprite(&s);
  }
  glowing(1);
#ifdef LANTERNFALL_R2D_V2
  cw_r2d_set_flash(0);
#endif
  nq = 0;
}

/* halos for the additive layer (creatures_fx) */
typedef struct halo { float x, y, r; cw_color c; } halo;
static halo HL[96];
static int nhl;
static void halo_at(float lx, float ly, float r, cw_color c) {
  if (nhl < 96 && c.a > 0.01f && D.die <= 0) {
    cw_vec2 w = W(lx, ly);
    HL[nhl++] = (halo){ w.x, w.y, r, c };
  }
}

/* two-bone reach: from the hip to the foot, the knee bending toward bend (+1 / -1); angles of both bones */
static void ik(float hx, float hy, float fx, float fy, float l1, float l2, float bend, float *a1, float *a2) {
  float dx = fx - hx, dy = fy - hy, d = sqrtf(dx * dx + dy * dy);
  if (d > l1 + l2 - 1e-3f) d = l1 + l2 - 1e-3f;
  if (d < 1e-3f) d = 1e-3f;
  float base = atan2f(dy, dx), c = (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d);
  c = c < -1 ? -1 : c > 1 ? 1 : c;
  float k = acosf(c) * bend;
  *a1 = base - k;
  float kx = hx + cosf(*a1) * l1, ky = hy + sinf(*a1) * l1;
  *a2 = atan2f(fy - ky, fx - kx);
}

/* ═════════════════════════ the enemies ═════════════════════════ */
static struct {
  part body, seams, head, mand, leg1, leg2, ant;                        /* crawler */
  part leaf, bulb, stem, pod, sac, petal;                               /* spitter */
  part fwing, hwing, spot, thorax, abdo, mhead, mant, mleg;             /* moth */
  part tbody, teye, throat, thigh, shin, tfoot, arm;                    /* hopper */
  part kthigh, kshin, tabard, torso, helm, plume, cloak, shield, karm, spear, pennant, pauldron; /* knight */
  part growth[NMAT];                                                    /* what grows on a back, by region */
} E;

static void paint_enemies(void) {
  /* ── crawler ── */
  {
    cv(1.5f, 0.95f);
    pstyle belly = sty(0x2c1416, 0x120608, 0x5a2a22, 0.3f, TX_CHITIN, 0.06f, 0.6f);
    el(0.75f, 0.66f, 0.64f, 0.2f, 0, &belly);
    pstyle fire = sty(0xff9a3a, 0xd04a10, 0xffe0a0, 0.15f, TX_VEINS, 0.05f, 0.5f);
    fire.em = 1;
    el(0.74f, 0.47f, 0.64f, 0.33f, 0, &fire);
    for (int i = 0; i < 5; i++) {
      float u = (float)i / 4, cx = 0.27f + u * 0.92f, cy = 0.46f - sinf(u * 3.1f) * 0.05f;
      pstyle pl = sty(0x8c3a28, 0x3a1218, 0xe08a58, 0.9f, TX_CHITIN, 0.05f, 0.7f);
      pl.spec = 0.55f, pl.gloss = 0.55f, pl.shadow = 0.55f, pl.shadow_r = 9, pl.ink = 1.6f, pl.rim = 0.4f, pl.seed = 31 + (uint32_t)i;
      el(cx, cy, 0.215f - u * 0.02f, 0.31f - fabsf(u - 0.45f) * 0.12f, -0.15f + u * 0.3f, &pl);
      pstyle sp = sty(0x5a2018, 0x200a0c, 0xc87050, 0.6f, TX_CHITIN, 0.03f, 0.4f);
      sp.ink = 1.2f, sp.spec = 0.4f;
      lf(cx - 0.02f, cy - 0.24f, cx - 0.16f, cy - 0.36f + u * 0.04f, 0.04f, -0.02f, &sp);
    }
    E.body = done(0.75f, 0.62f, 3.0f);
    /* the seams alone (drawn over the body, brighter as it rears) */
    cv(1.5f, 0.95f);
    pstyle sm = sty(0xffc060, 0xff7020, 0xfff0c0, 0.1f, TX_NONE, 0, 0);
    sm.em = 1, sm.soft = 2.5f;
    for (int i = 0; i < 4; i++) {
      float u = ((float)i + 0.5f) / 4, cx = 0.27f + u * 0.92f + 0.1f;
      float pts[6] = { cx - 0.06f, 0.2f, cx + 0.02f, 0.45f, cx - 0.04f, 0.7f };
      stk(pts, 3, 0.012f, 0.02f, &sm);
    }
    E.seams = done(0.75f, 0.62f, 1);
    cv(0.52f, 0.46f);
    pstyle hd = sty(0x6a2a20, 0x2a0c10, 0xd07a50, 0.9f, TX_CHITIN, 0.04f, 0.6f);
    hd.ink = 1.5f, hd.spec = 0.6f, hd.gloss = 0.6f, hd.rim = 0.4f;
    el(0.23f, 0.25f, 0.2f, 0.16f, 0.1f, &hd);
    pstyle eye = sty(0xffd060, 0xff7a10, 0xffffe0, 0.8f, TX_NONE, 0, 0);
    eye.em = 1, eye.spec = 0.9f, eye.gloss = 0.9f;
    el(0.36f, 0.19f, 0.045f, 0.04f, 0, &eye);
    el(0.29f, 0.16f, 0.035f, 0.032f, 0, &eye);
    E.head = done(0.06f, 0.26f, 3);
    cv(0.32f, 0.16f);
    pstyle md = sty(0x3a1610, 0x100406, 0xc0a080, 0.7f, TX_CHITIN, 0.03f, 0.3f);
    md.spec = 0.7f, md.gloss = 0.7f, md.ink = 1.2f;
    lf(0.03f, 0.08f, 0.3f, 0.1f, 0.045f, 0.035f, &md);
    E.mand = done(0.03f, 0.08f, 3);
    cv(0.36f, 0.12f);
    pstyle lg = sty(0x6a2a20, 0x200a0c, 0xc07050, 0.8f, TX_CHITIN, 0.03f, 0.4f);
    lg.ink = 1.2f, lg.spec = 0.4f;
    cap(0.05f, 0.06f, 0.31f, 0.06f, 0.05f, 0.036f, &lg);
    E.leg1 = done(0.05f, 0.06f, 3);
    cv(0.44f, 0.12f);
    float lp[6] = { 0.04f, 0.06f, 0.24f, 0.05f, 0.42f, 0.08f };
    stk(lp, 3, 0.034f, 0.008f, &lg);
    E.leg2 = done(0.04f, 0.06f, 3);
    cv(0.52f, 0.08f);
    pstyle an = sty(0x7a3a28, 0x2a0c0c, 0xe0a070, 0.6f, TX_SCALES, 0.025f, 0.5f);
    an.ink = 0.8f;
    float ap[6] = { 0.03f, 0.04f, 0.26f, 0.035f, 0.5f, 0.045f };
    stk(ap, 3, 0.018f, 0.007f, &an);
    E.ant = done(0.03f, 0.04f, 3);
  }
  /* ── spitter ── */
  {
    cv(0.72f, 0.26f);
    pstyle lv = sty(0x4f7a2c, 0x1e3a14, 0xa8d060, 0.5f, TX_VEINS, 0.05f, 0.5f);
    lv.ink = 1.2f, lv.rim = 0.3f, lv.spec = 0.25f;
    lf(0.03f, 0.15f, 0.7f, 0.1f, 0.095f, -0.05f, &lv);
    E.leaf = done(0.03f, 0.15f, 2.5f);
    cv(0.66f, 0.38f);
    pstyle bb = sty(0x3e5a24, 0x162a10, 0x7a9a40, 0.8f, TX_WARTS, 0.05f, 0.7f);
    bb.ink = 1.4f, bb.shadow = 0.4f;
    el(0.33f, 0.22f, 0.28f, 0.14f, 0, &bb);
    for (int i = 0; i < 4; i++) { /* roots into the ground */
      float rp[6] = { 0.16f + 0.1f * (float)i, 0.28f, 0.12f + 0.12f * (float)i, 0.33f, 0.06f + 0.16f * (float)i, 0.37f };
      stk(rp, 3, 0.025f, 0.006f, &bb);
    }
    E.bulb = done(0.33f, 0.32f, 3);
    cv(0.24f, 1.0f);
    pstyle st = sty(0x5a7a30, 0x22380f, 0x9ac060, 0.7f, TX_BARK, 0.04f, 0.5f);
    st.ink = 1.3f, st.rim = 0.35f;
    float sp[8] = { 0.12f, 0.97f, 0.1f, 0.65f, 0.14f, 0.32f, 0.12f, 0.03f };
    stk(sp, 4, 0.075f, 0.05f, &st);
    E.stem = done(0.12f, 0.96f, 3);
    cv(0.95f, 0.95f);
    for (int i = 0; i < 3; i++) { /* the gourd's lobes */
      pstyle lb = sty(0x7e2c6c, 0x2a0a2c, 0xd070b0, 1.0f, TX_MEMBRANE, 0.05f, 0.6f);
      lb.ink = 1.6f, lb.shadow = 0.35f, lb.spec = 0.45f, lb.gloss = 0.5f, lb.rim = 0.35f, lb.seed = 70 + (uint32_t)i;
      float cx = 0.3f + 0.18f * (float)i;
      el(cx, 0.48f, 0.21f + (i == 1 ? 0.05f : 0), 0.38f - (i == 1 ? 0 : 0.04f), 0, &lb);
    }
    pstyle vn = sty(0xe8ff70, 0xa0d020, 0xffffc0, 0.1f, TX_NONE, 0, 0);
    vn.em = 0.8f, vn.soft = 1.8f;
    for (int i = 0; i < 5; i++) { /* glowing veins in the skin */
      float a = -1.1f + (float)i * 0.55f, vp[6] = { 0.48f, 0.48f, 0.48f + cosf(a) * 0.2f, 0.48f + sinf(a) * 0.24f, 0.48f + cosf(a) * 0.33f, 0.48f + sinf(a) * 0.36f };
      stk(vp, 3, 0.012f, 0.004f, &vn);
    }
    pstyle mo = sty(0x1a0614, 0x080004, 0x5a1a40, -0.4f, TX_NONE, 0, 0);
    mo.ink = 1;
    el(0.82f, 0.5f, 0.1f, 0.17f, 0, &mo);
    pstyle tooth = sty(0xf0e0c8, 0x8a7060, 0xffffff, 0.5f, TX_NONE, 0, 0);
    for (int i = 0; i < 4; i++) lf(0.8f, 0.37f + 0.08f * (float)i, 0.86f, 0.4f + 0.08f * (float)i, 0.015f, 0, &tooth);
    E.pod = done(0.48f, 0.9f, 3);
    cv(0.7f, 0.7f);
    pstyle sc = sty(0xd8ff60, 0x80c020, 0xffffd0, 0.3f, TX_VEINS, 0.06f, 0.6f);
    sc.em = 1, sc.soft = 10;
    el(0.35f, 0.35f, 0.26f, 0.28f, 0, &sc);
    E.sac = done(0.35f, 0.35f, 1);
    cv(0.48f, 0.22f);
    pstyle pt = sty(0xc84a90, 0x5a1440, 0xffa0d0, 0.5f, TX_VEINS, 0.04f, 0.4f);
    pt.ink = 1.3f, pt.rim = 0.3f;
    lf(0.02f, 0.11f, 0.46f, 0.1f, 0.085f, -0.03f, &pt);
    E.petal = done(0.02f, 0.11f, 2.5f);
  }
  /* ── moth ── */
  {
    cv(1.05f, 0.75f);
    pstyle wg = sty(0x8a6a50, 0x3a2430, 0xe8c890, 0.25f, TX_SCALES, 0.03f, 0.35f);
    wg.ink = 1.6f, wg.rim = 0.3f, wg.tex_dir = 0.4f;
    lf(0.04f, 0.55f, 1.0f, 0.12f, 0.24f, -0.1f, &wg);
    pstyle band = sty(0xf0dcb0, 0xb09070, 0xffffff, 0.15f, TX_SCALES, 0.025f, 0.3f);
    band.alpha = 0.85f;
    float bp[6] = { 0.3f, 0.62f, 0.5f, 0.38f, 0.66f, 0.12f };
    stk(bp, 3, 0.03f, 0.022f, &band);
    pstyle dk = sty(0x3a2030, 0x1a0a14, 0x6a4050, 0.1f, TX_SCALES, 0.025f, 0.3f);
    dk.alpha = 0.8f;
    float dp[6] = { 0.72f, 0.5f, 0.85f, 0.32f, 0.96f, 0.14f };
    stk(dp, 3, 0.05f, 0.02f, &dk);
    pstyle ring = sty(0x1c1030, 0x080410, 0x4a3060, 0.3f, TX_NONE, 0, 0);
    el(0.6f, 0.36f, 0.1f, 0.085f, -0.4f, &ring);
    pstyle spot = sty(0x60e8ff, 0x2080c0, 0xe0ffff, 0.4f, TX_NONE, 0, 0);
    spot.em = 1;
    el(0.6f, 0.36f, 0.06f, 0.05f, -0.4f, &spot);
    pstyle pup = sty(0x101828, 0, 0, 0.2f, TX_NONE, 0, 0);
    el(0.61f, 0.355f, 0.022f, 0.02f, 0, &pup);
    E.fwing = done(0.04f, 0.55f, 2);
    cv(0.8f, 0.62f);
    pstyle hw = sty(0x7a5040, 0x30182a, 0xd8a070, 0.25f, TX_SCALES, 0.03f, 0.35f);
    hw.ink = 1.6f, hw.rim = 0.25f;
    lf(0.04f, 0.18f, 0.66f, 0.48f, 0.22f, 0.1f, &hw);
    pstyle hb = sty(0xd87a40, 0x8a3a20, 0xffc080, 0.1f, TX_SCALES, 0.025f, 0.3f);
    hb.alpha = 0.8f;
    el(0.42f, 0.36f, 0.12f, 0.06f, 0.6f, &hb);
    pstyle sp2 = spot;
    el(0.5f, 0.42f, 0.035f, 0.03f, 0, &sp2);
    E.hwing = done(0.04f, 0.18f, 2);
    cv(0.3f, 0.3f);
    pstyle gl = sty(0x80f0ff, 0x30a0e0, 0xffffff, 0.2f, TX_NONE, 0, 0);
    gl.em = 1, gl.soft = 14;
    el(0.15f, 0.15f, 0.1f, 0.1f, 0, &gl);
    E.spot = done(0.15f, 0.15f, 1);
    cv(0.46f, 0.46f);
    pstyle fur = sty(0x6a4a52, 0x2a1824, 0xc8a0a0, 0.8f, TX_FUR, 0.02f, 0.8f);
    fur.soft = 3, fur.rim = 0.5f;
    el(0.23f, 0.23f, 0.19f, 0.2f, 0, &fur);
    pstyle collar = sty(0xd8c0a0, 0x8a7060, 0xffffff, 0.5f, TX_FUR, 0.015f, 0.8f);
    collar.soft = 3;
    el(0.23f, 0.12f, 0.15f, 0.07f, 0, &collar);
    E.thorax = done(0.23f, 0.23f, 3);
    cv(0.32f, 0.66f);
    pstyle ab = sty(0x5a3a48, 0x24101c, 0xb08890, 0.8f, TX_FUR, 0.018f, 0.7f);
    ab.soft = 2.5f, ab.rim = 0.4f;
    float abp[8] = { 0.16f, 0.05f, 0.17f, 0.25f, 0.15f, 0.45f, 0.16f, 0.6f };
    stk(abp, 4, 0.13f, 0.035f, &ab);
    pstyle rg = sty(0x2a1420, 0, 0, 0.1f, TX_FUR, 0.015f, 0.6f);
    rg.alpha = 0.6f, rg.soft = 2;
    for (int i = 1; i < 5; i++) el(0.16f, 0.06f + 0.1f * (float)i, 0.12f - 0.018f * (float)i, 0.012f, 0, &rg);
    E.abdo = done(0.16f, 0.05f, 3);
    cv(0.32f, 0.3f);
    pstyle hf = sty(0x6a4a52, 0x2a1824, 0xc8a0a0, 0.7f, TX_FUR, 0.015f, 0.7f);
    hf.soft = 2.5f;
    el(0.16f, 0.16f, 0.12f, 0.11f, 0, &hf);
    pstyle ce = sty(0x24183a, 0x0a0614, 0x8070c0, 0.9f, TX_SCALES, 0.012f, 0.4f);
    ce.spec = 0.9f, ce.gloss = 0.85f, ce.ink = 1;
    el(0.23f, 0.15f, 0.06f, 0.07f, 0, &ce);
    el(0.1f, 0.15f, 0.055f, 0.065f, 0, &ce);
    pstyle gli = sty(0xc0e0ff, 0, 0, 0.5f, TX_NONE, 0, 0);
    gli.em = 0.7f;
    el(0.245f, 0.125f, 0.014f, 0.012f, 0, &gli);
    el(0.115f, 0.125f, 0.012f, 0.011f, 0, &gli);
    E.mhead = done(0.16f, 0.2f, 3);
    cv(0.5f, 0.18f);
    pstyle at = sty(0xd8c0a0, 0x7a6050, 0xffffff, 0.3f, TX_FEATHER, 0.012f, 0.7f);
    at.ink = 0.8f;
    lf(0.02f, 0.1f, 0.48f, 0.08f, 0.06f, 0.03f, &at);
    E.mant = done(0.02f, 0.1f, 2);
    cv(0.34f, 0.08f);
    pstyle ml = sty(0x4a3040, 0x1a0c14, 0x9a7080, 0.5f, TX_FUR, 0.01f, 0.5f);
    float mp[6] = { 0.03f, 0.04f, 0.16f, 0.035f, 0.31f, 0.045f };
    stk(mp, 3, 0.02f, 0.008f, &ml);
    E.mleg = done(0.03f, 0.04f, 2);
  }
  /* ── hopper ── */
  {
    cv(1.25f, 0.95f);
    pstyle bd = sty(0x8a6a2c, 0x3a2410, 0xd8b060, 0.9f, TX_WARTS, 0.06f, 0.8f);
    bd.ink = 1.8f, bd.rim = 0.45f, bd.spec = 0.35f, bd.gloss = 0.3f;
    el(0.6f, 0.52f, 0.52f, 0.37f, -0.1f, &bd);
    pstyle bl = sty(0xe8d0a0, 0x9a7a50, 0xfff0d0, 0.4f, TX_SKIN, 0.04f, 0.4f);
    bl.alpha = 0.9f, bl.soft = 6;
    el(0.78f, 0.7f, 0.3f, 0.17f, -0.2f, &bl);
    pstyle spt = sty(0x4a2a10, 0, 0, 0.1f, TX_SKIN, 0.03f, 0.5f);
    spt.alpha = 0.7f, spt.soft = 3;
    for (int i = 0; i < 6; i++) el(0.3f + 0.12f * (float)i + hsh(i, 5) * 0.05f, 0.3f + hsh(i, 6) * 0.15f, 0.05f + hsh(i, 7) * 0.03f, 0.035f, 0, &spt);
    pstyle mouth = sty(0x2a1408, 0, 0, -0.2f, TX_NONE, 0, 0);
    float mp[6] = { 0.82f, 0.56f, 0.97f, 0.58f, 1.08f, 0.52f };
    stk(mp, 3, 0.012f, 0.008f, &mouth);
    E.tbody = done(0.6f, 0.88f, 3);
    cv(0.32f, 0.32f);
    pstyle lid = sty(0x8a6a2c, 0x3a2410, 0xd8b060, 0.9f, TX_WARTS, 0.04f, 0.6f);
    lid.ink = 1.4f;
    el(0.16f, 0.17f, 0.14f, 0.13f, 0, &lid);
    pstyle ir = sty(0xffb030, 0xc05010, 0xffffa0, 0.9f, TX_NONE, 0, 0);
    ir.em = 0.85f, ir.spec = 1, ir.gloss = 0.95f, ir.ink = 1;
    el(0.17f, 0.17f, 0.095f, 0.095f, 0, &ir);
    pstyle pu = sty(0x100804, 0, 0, 0.3f, TX_NONE, 0, 0);
    el(0.18f, 0.17f, 0.022f, 0.07f, 0, &pu);
    pstyle sh = sty(0xffffff, 0, 0, 0.5f, TX_NONE, 0, 0);
    sh.em = 1;
    el(0.14f, 0.12f, 0.02f, 0.016f, 0, &sh);
    E.teye = done(0.16f, 0.17f, 3);
    cv(0.5f, 0.4f);
    pstyle th = sty(0xff7040, 0xb02810, 0xffd0a0, 0.6f, TX_VEINS, 0.04f, 0.6f);
    th.em = 0.6f, th.ink = 1.2f, th.spec = 0.6f, th.gloss = 0.7f;
    el(0.25f, 0.2f, 0.2f, 0.15f, 0, &th);
    E.throat = done(0.25f, 0.06f, 3);
    cv(0.6f, 0.4f);
    pstyle tg = sty(0x7a5a24, 0x2a1a08, 0xc8a050, 0.9f, TX_WARTS, 0.05f, 0.7f);
    tg.ink = 1.6f, tg.rim = 0.4f, tg.shadow = 0.3f;
    lf(0.08f, 0.2f, 0.56f, 0.22f, 0.15f, -0.04f, &tg);
    E.thigh = done(0.1f, 0.2f, 3);
    cv(0.52f, 0.14f);
    cap(0.05f, 0.07f, 0.47f, 0.07f, 0.06f, 0.035f, &tg);
    E.shin = done(0.05f, 0.07f, 3);
    cv(0.4f, 0.14f);
    pstyle ft = sty(0x9a7a3a, 0x3a2a10, 0xd8b070, 0.5f, TX_SKIN, 0.03f, 0.4f);
    ft.ink = 1.2f;
    for (int i = 0; i < 3; i++) lf(0.03f, 0.07f, 0.36f, 0.03f + 0.04f * (float)i, 0.03f, 0, &ft);
    E.tfoot = done(0.03f, 0.07f, 2);
    cv(0.4f, 0.12f);
    cap(0.04f, 0.06f, 0.34f, 0.06f, 0.045f, 0.03f, &tg);
    for (int i = 0; i < 3; i++) lf(0.3f, 0.06f, 0.38f, 0.03f + 0.03f * (float)i, 0.012f, 0, &ft);
    E.arm = done(0.04f, 0.06f, 3);
  }
  /* ── knight ── */
  {
    pstyle steel = sty(0x8a92a0, 0x2a3040, 0xe8eef8, 0.8f, TX_METAL, 0.05f, 0.8f);
    steel.spec = 0.85f, steel.gloss = 0.7f, steel.ink = 1.6f, steel.rim = 0.4f;
    pstyle verd = sty(0x5a9a88, 0x2a4a44, 0x9adac8, 0.2f, TX_STONE, 0.03f, 0.6f);
    verd.alpha = 0.45f, verd.soft = 6;
    pstyle gold = sty(0xd8a848, 0x6a4010, 0xfff0b0, 0.6f, TX_METAL, 0.03f, 0.5f);
    gold.spec = 0.9f, gold.gloss = 0.8f, gold.ink = 1;
    cv(0.3f, 0.6f);
    cap(0.15f, 0.06f, 0.15f, 0.48f, 0.11f, 0.08f, &steel);
    el(0.16f, 0.5f, 0.1f, 0.08f, 0, &steel);
    el(0.15f, 0.3f, 0.09f, 0.12f, 0, &verd);
    E.kthigh = done(0.15f, 0.06f, 3);
    cv(0.42f, 0.6f);
    cap(0.15f, 0.04f, 0.15f, 0.44f, 0.085f, 0.07f, &steel);
    float sab[10] = { 0.05f, 0.44f, 0.26f, 0.44f, 0.38f, 0.52f, 0.36f, 0.57f, 0.05f, 0.57f };
    poly(sab, 5, 0.01f, &steel);
    E.kshin = done(0.15f, 0.04f, 3);
    cv(0.78f, 0.72f);
    pstyle cloth = sty(0x8a1a2a, 0x2a0610, 0xd85a60, 0.4f, TX_CLOTH, 0.05f, 0.8f);
    cloth.ink = 1.4f, cloth.rim = 0.3f, cloth.shadow = 0.3f;
    float tb[16] = { 0.16f, 0.04f, 0.62f, 0.04f, 0.7f, 0.5f, 0.66f, 0.68f, 0.52f, 0.6f, 0.39f, 0.7f, 0.24f, 0.6f, 0.1f, 0.68f };
    poly(tb, 8, 0.01f, &cloth);
    float tr[6] = { 0.39f, 0.08f, 0.39f, 0.35f, 0.39f, 0.62f };
    stk(tr, 3, 0.02f, 0.018f, &gold);
    E.tabard = done(0.39f, 0.05f, 2.5f);
    cv(0.86f, 0.86f);
    float cu[14] = { 0.12f, 0.1f, 0.74f, 0.1f, 0.78f, 0.45f, 0.66f, 0.78f, 0.43f, 0.84f, 0.2f, 0.78f, 0.08f, 0.45f };
    pstyle ct = steel;
    ct.dome = 1.1f;
    poly(cu, 7, 0.05f, &ct);
    el(0.36f, 0.55f, 0.18f, 0.2f, 0.3f, &verd);
    float fl[6] = { 0.43f, 0.16f, 0.45f, 0.45f, 0.43f, 0.78f };
    stk(fl, 3, 0.022f, 0.016f, &gold);
    pstyle lamp = sty(0xffd070, 0xc07010, 0xffffd0, 0.6f, TX_NONE, 0, 0);
    lamp.em = 0.6f, lamp.ink = 1;
    el(0.45f, 0.36f, 0.06f, 0.08f, 0, &lamp);
    E.torso = done(0.43f, 0.82f, 3);
    cv(0.56f, 0.6f);
    float hm[16] = { 0.1f, 0.18f, 0.24f, 0.05f, 0.4f, 0.05f, 0.52f, 0.2f, 0.52f, 0.5f, 0.42f, 0.56f, 0.12f, 0.56f, 0.06f, 0.4f };
    pstyle hs = steel;
    hs.dome = 1.2f;
    poly(hm, 8, 0.04f, &hs);
    pstyle slit = sty(0x0a0c14, 0, 0, -0.3f, TX_NONE, 0, 0);
    float sl[4] = { 0.28f, 0.27f, 0.5f, 0.26f };
    stk(sl, 2, 0.028f, 0.02f, &slit);
    pstyle wisp = sty(0xc0f4ff, 0x4090c0, 0xffffff, 0.3f, TX_NONE, 0, 0);
    wisp.em = 1;
    float wl[4] = { 0.33f, 0.27f, 0.47f, 0.265f };
    stk(wl, 2, 0.012f, 0.009f, &wisp);
    for (int i = 0; i < 5; i++) el(0.46f, 0.35f + 0.035f * (float)i, 0.012f, 0.008f, 0, &slit); /* breaths */
    float rid[6] = { 0.12f, 0.2f, 0.3f, 0.06f, 0.5f, 0.16f };
    stk(rid, 3, 0.02f, 0.015f, &gold);
    E.helm = done(0.3f, 0.52f, 3);
    cv(0.6f, 0.32f);
    pstyle pl = cloth;
    pl.tex_dir = 1.57f;
    float pp[8] = { 0.56f, 0.16f, 0.36f, 0.12f, 0.18f, 0.18f, 0.04f, 0.26f };
    stk(pp, 4, 0.07f, 0.02f, &pl);
    E.plume = done(0.56f, 0.16f, 2);
    cv(0.8f, 1.3f);
    pstyle ck = sty(0x5a1220, 0x1a040a, 0xa83a48, 0.35f, TX_CLOTH, 0.06f, 0.9f);
    ck.ink = 1.6f, ck.rim = 0.25f;
    float cl_[22] = { 0.28f, 0.04f, 0.56f, 0.04f, 0.7f, 0.5f, 0.76f, 1.18f, 0.66f, 1.08f, 0.58f, 1.25f, 0.46f, 1.12f, 0.34f, 1.26f, 0.22f, 1.1f, 0.1f, 1.2f, 0.12f, 0.5f };
    poly(cl_, 11, 0.01f, &ck);
    E.cloak = done(0.42f, 0.05f, 2);
    cv(0.56f, 1.2f);
    float sh[16] = { 0.06f, 0.2f, 0.28f, 0.04f, 0.5f, 0.2f, 0.5f, 0.9f, 0.4f, 1.08f, 0.28f, 1.16f, 0.16f, 1.08f, 0.06f, 0.9f };
    pstyle rimm = steel;
    rimm.dome = 0.5f;
    poly(sh, 8, 0.03f, &rimm);
    float sh2[16] = { 0.11f, 0.22f, 0.28f, 0.1f, 0.45f, 0.22f, 0.45f, 0.88f, 0.37f, 1.03f, 0.28f, 1.09f, 0.19f, 1.03f, 0.11f, 0.88f };
    pstyle field = sty(0x24506a, 0x0c1a2a, 0x5a90b0, 0.4f, TX_STONE, 0.04f, 0.5f);
    field.spec = 0.3f, field.shadow = 0.4f, field.shadow_r = 6;
    poly(sh2, 8, 0.02f, &field);
    pstyle em = gold;
    em.em = 0.25f;
    el(0.28f, 0.52f, 0.08f, 0.11f, 0, &em); /* the lantern emblem */
    cap(0.28f, 0.36f, 0.28f, 0.42f, 0.03f, 0.03f, &em);
    cap(0.2f, 0.66f, 0.36f, 0.66f, 0.03f, 0.03f, &em);
    for (int i = 0; i < 7; i++) el(0.28f + 0.2f * cosf((float)i * 0.9f), 0.55f + 0.4f * sinf((float)i * 0.9f) * 0.9f, 0.016f, 0.016f, 0, &gold); /* rivets */
    E.shield = done(0.28f, 0.6f, 3);
    cv(0.26f, 0.5f);
    cap(0.13f, 0.06f, 0.13f, 0.42f, 0.09f, 0.07f, &steel);
    el(0.13f, 0.44f, 0.08f, 0.06f, 0, &steel);
    E.karm = done(0.13f, 0.06f, 3);
    cv(2.7f, 0.26f);
    pstyle wood = sty(0x6a4a30, 0x2a1a10, 0xb08a60, 0.6f, TX_BARK, 0.03f, 0.6f);
    wood.ink = 1.2f;
    cap(0.04f, 0.13f, 2.2f, 0.13f, 0.035f, 0.03f, &wood);
    for (int i = 0; i < 3; i++) cap(0.6f + 0.12f * (float)i, 0.13f, 0.64f + 0.12f * (float)i, 0.13f, 0.045f, 0.045f, &gold);
    pstyle bl = steel;
    bl.spec = 1, bl.gloss = 0.9f;
    lf(2.12f, 0.13f, 2.68f, 0.13f, 0.08f, 0, &bl);
    E.spear = done(0.7f, 0.13f, 3);
    cv(0.66f, 0.32f);
    pstyle pn = cloth;
    float pq[10] = { 0.02f, 0.04f, 0.62f, 0.12f, 0.48f, 0.17f, 0.64f, 0.26f, 0.02f, 0.26f };
    poly(pq, 5, 0.01f, &pn);
    E.pennant = done(0.02f, 0.06f, 2);
    cv(0.5f, 0.34f);
    pstyle pa = steel;
    pa.dome = 1;
    el(0.25f, 0.18f, 0.22f, 0.14f, 0, &pa);
    float pt[6] = { 0.06f, 0.2f, 0.25f, 0.1f, 0.44f, 0.2f };
    stk(pt, 3, 0.016f, 0.016f, &gold);
    E.pauldron = done(0.25f, 0.16f, 3);
  }
  /* ── what grows on a back, by the region's material ── */
  for (int m = 0; m < NMAT; m++) {
    cv(0.7f, 0.42f);
    switch (m) {
    case MAT_EARTH: {
      pstyle ms = sty(0x5a8a2a, 0x24400e, 0xb0e060, 0.5f, TX_MOSS, 0.03f, 0.9f);
      ms.soft = 4, ms.rim = 0.4f;
      el(0.35f, 0.3f, 0.3f, 0.1f, 0, &ms);
      pstyle fl = sty(0xffe080, 0xc08020, 0xffffff, 0.4f, TX_NONE, 0, 0);
      fl.em = 0.5f;
      for (int i = 0; i < 4; i++) el(0.15f + 0.13f * (float)i, 0.2f + hsh(i, 9) * 0.06f, 0.025f, 0.022f, 0, &fl);
      break;
    }
    case MAT_CRYSTAL: {
      pstyle cr = sty(0x8ad8ff, 0x2a5aa0, 0xf0ffff, 0.5f, TX_GLASS, 0.06f, 0.8f);
      cr.em = 0.35f, cr.spec = 1, cr.gloss = 0.9f, cr.ink = 1.2f;
      for (int i = 0; i < 4; i++) lf(0.18f + 0.12f * (float)i, 0.4f, 0.12f + 0.15f * (float)i, 0.06f + hsh(i, 3) * 0.12f, 0.05f, 0, &cr);
      break;
    }
    case MAT_BRASS: {
      pstyle br = sty(0xc89a48, 0x5a3a10, 0xfff0b0, 0.6f, TX_METAL, 0.04f, 0.7f);
      br.spec = 0.9f, br.gloss = 0.7f, br.ink = 1.3f;
      el(0.35f, 0.3f, 0.26f, 0.1f, 0, &br);
      pstyle rv = br;
      for (int i = 0; i < 5; i++) el(0.15f + 0.1f * (float)i, 0.28f, 0.018f, 0.018f, 0, &rv);
      break;
    }
    case MAT_CORAL: {
      pstyle co = sty(0xf08a7a, 0x8a3040, 0xffd0c0, 0.6f, TX_WARTS, 0.025f, 0.7f);
      co.ink = 1.2f, co.rim = 0.3f;
      for (int i = 0; i < 5; i++) el(0.12f + 0.11f * (float)i, 0.3f - hsh(i, 4) * 0.08f, 0.06f, 0.06f + hsh(i, 5) * 0.04f, 0, &co);
      break;
    }
    case MAT_CLOUD: {
      pstyle cl = sty(0xf8fcff, 0xa0b4d0, 0xffffff, 0.6f, TX_NONE, 0, 0);
      cl.soft = 5, cl.alpha = 0.85f;
      for (int i = 0; i < 4; i++) el(0.15f + 0.14f * (float)i, 0.28f - hsh(i, 8) * 0.06f, 0.1f, 0.08f, 0, &cl);
      break;
    }
    default: {
      pstyle li = sty(0xa0b070, 0x405030, 0xe0f0b0, 0.3f, TX_STONE, 0.02f, 0.8f);
      li.soft = 3, li.alpha = 0.9f;
      for (int i = 0; i < 3; i++) el(0.2f + 0.15f * (float)i, 0.3f, 0.09f, 0.05f, 0, &li);
      break;
    }
    }
    E.growth[m] = done(0.35f, 0.34f, 2.5f);
  }
}

static cw_color white(float a) { return (cw_color){ 1, 1, 1, a }; }
static cw_color dark(float k) { return (cw_color){ k, k, k, 1 }; }
static cw_vec2 v2l(float x, float y) { return (cw_vec2){ x, y }; }
/* a part laid from a to b (local), stretched along its length from its painted length l0 */
static void bone(const part *p, cw_vec2 a, cw_vec2 b, float l0, cw_color c) {
  float dx = b.x - a.x, dy = b.y - a.y, l = sqrtf(dx * dx + dy * dy);
  putk(p, a.x, a.y, atan2f(dy, dx), l / l0, 1, c);
}

static void draw_crawler(const enemy *e, int region, float t, float tk) {
  bool tell = e->state == ES_TELL, act = e->state == ES_ACT;
  float rear = tell ? fminf(1, tk * 1.6f) : 0, ph = act ? t * 26 : e->state == ES_IDLE ? t * 10 + e->x : 0;
  float bob = e->state == ES_IDLE || act ? fabsf(sinf(ph)) * 0.025f : 0;
  float brot = -rear * 0.42f + (act ? 0.08f : 0);
  float hipx = -0.05f, hipy = -0.4f - bob - rear * 0.06f + (act ? 0.06f : 0);
  /* far legs, darker */
  static const float AX[3] = { -0.38f, -0.02f, 0.34f };
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1) {
      put(&E.body, hipx, hipy, brot, 1, 1);
      put(&E.growth[region_material(region)], hipx - 0.12f + rot2(0, -0.32f, brot).x, hipy + rot2(0, -0.32f, brot).y, brot, 1, 1);
      float sg = tell ? 1.5f + 3.5f * rear : act ? 2.5f : 1.2f + 0.3f * sinf(t * 3);
      putg(&E.seams, hipx, hipy, brot, 1, 1, sg, 0.55f + 0.45f * rear);
      cw_vec2 hp = rot2(0.6f, -0.08f, brot);
      float hx = hipx + hp.x, hy = hipy + hp.y, hrot = brot * 0.6f - (act ? 0.15f : 0);
      float open = tell ? 0.25f + rear * 0.55f : act ? 0.35f + sinf(t * 30) * 0.1f : 0.08f + sinf(t * 2.3f) * 0.05f;
      cw_vec2 mp = rot2(0.36f, 0.06f, hrot);
      put(&E.mand, hx + mp.x, hy + mp.y, hrot + open, 1, 1);
      /* antennae, the tip lagging the base */
      for (int k = 0; k < 2; k++) {
        float sw = sinf(t * 3.1f + (float)k * 1.7f) * 0.18f, base = -0.75f - (float)k * 0.3f + (act ? 1.15f : 0) - rear * 0.3f + sw;
        cw_vec2 ab = rot2(0.3f, -0.12f, hrot);
        float ax = hx + ab.x, ay = hy + ab.y;
        cw_vec2 a1 = rot2(0.47f, 0, base);
        putk(&E.ant, ax, ay, base, 1, 1, k ? dark(0.7f) : white(1));
        float b2 = base + 0.5f + sinf(t * 3.1f + (float)k * 1.7f - 0.9f) * 0.35f + (act ? 0.3f : 0);
        putk(&E.ant, ax + a1.x, ay + a1.y, b2, 0.9f, 0.8f, k ? dark(0.7f) : white(1));
      }
      put(&E.head, hx, hy, hrot, 1, 1);
      put(&E.mand, hx + mp.x, hy + mp.y + 0.03f, hrot - open * 0.6f + 0.1f, 1, 0.9f);
      if (tell) halo_at(hx + 0.3f, hy - 0.05f, 0.4f + 0.6f * rear, (cw_color){ 1, 0.55f, 0.2f, 0.5f * rear });
    }
    for (int k = 0; k < 3; k++) {
      float lphase = ph + (float)k * 2.1f + (pass ? 3.14159f : 0);
      cw_vec2 at = rot2(AX[k], 0.12f, brot);
      float lx = hipx + at.x + (pass ? 0.04f : -0.04f), ly = hipy + at.y;
      float stride = act ? 0.22f : 0.15f, fx = AX[k] * 1.25f + (e->state == ES_IDLE || act ? sinf(lphase) * stride : 0), fy = -fmaxf(0, cosf(lphase)) * 0.09f * (e->state == ES_IDLE || act);
      if (k == 2 && rear > 0) fx += 0.15f * rear, fy = -0.25f * rear + sinf(t * 18 + (float)pass) * 0.05f * rear; /* front legs paw the air */
      float a1, a2;
      ik(lx, ly, fx, fy, 0.26f, 0.37f, k == 0 ? 1 : -1, &a1, &a2);
      cw_color c = pass ? white(1) : dark(0.55f);
      putk(&E.leg1, lx, ly, a1, 1, 1, c);
      putk(&E.leg2, lx + cosf(a1) * 0.26f, ly + sinf(a1) * 0.26f, a2, 1, 1, c);
    }
  }
  if (act && ((int)(t * 60) % 5) == 0 && D.die <= 0) {
    cw_vec2 w = W(-0.6f, 0);
    view_emit(w.x, w.y - 0.05f, -D.f * 1.5f, -0.8f, 0.45f, 0.35f, (cw_color){ 0.85f, 0.8f, 0.72f, 0.35f }, false, 1);
  }
}

static void draw_spitter(const enemy *e, float t, float tk) {
  bool tell = e->state == ES_TELL;
  float recoil = e->state == ES_IDLE && e->t < 14 ? 1 - (float)e->t / 14 : 0;
  float sway = sinf(t * 1.3f + e->x * 0.7f) * 0.07f, lean = tell ? -0.28f * fminf(1, tk * 1.4f) : 0;
  lean += recoil * 0.22f;
  float swell = tell ? 1 + tk * 0.42f : 1 + sinf(t * 2.1f + e->x) * 0.035f - recoil * 0.1f;
  float a = sway + lean;
  put(&E.leaf, -0.08f, -0.1f, -2.75f + sinf(t * 1.1f) * 0.06f, 1, 1);
  put(&E.leaf, 0.08f, -0.1f, -0.4f - sinf(t * 1.2f + 1) * 0.06f, 1, 1);
  put(&E.stem, 0, -0.06f, a, 1, 1);
  cw_vec2 top = rot2(0, -0.92f, a);
  float px = top.x, py = -0.06f + top.y;
  /* the petals behind the mouth, then the pod, then the glow inside and the petals in front */
  float open = tell ? 0.25f + 0.95f * tk : recoil * 1.2f + 0.12f + sinf(t * 1.7f) * 0.06f;
  cw_vec2 mouth = rot2(0.34f * swell, -0.42f * swell, a);
  float mx = px + mouth.x, my = py + mouth.y;
  for (int k = 0; k < 2; k++) put(&E.petal, mx - 0.02f, my, a + (k ? 1 : -1) * (0.3f + open * 0.9f), 0.9f, 1);
  put(&E.pod, px, py, a, swell, swell);
  float ga = tell ? 0.35f + 0.65f * tk : 0.3f + 0.1f * sinf(t * 2.5f) + recoil * 0.6f;
  cw_vec2 sc = rot2(-0.02f, -0.42f * swell, a);
  putg(&E.sac, px + sc.x, py + sc.y, 0, swell * (0.8f + 0.25f * tk), swell * (0.8f + 0.25f * tk), 1.2f + 2.6f * ga, ga);
  for (int k = 0; k < 2; k++) put(&E.petal, mx, my + (k ? 0.08f : -0.08f), a + (k ? 1 : -1) * (0.55f + open * 1.1f), 1, 1);
  put(&E.leaf, -0.02f, -0.06f, -3.0f + sinf(t * 1.4f + 2) * 0.05f, 0.8f, 1);
  put(&E.bulb, 0, 0, 0, 1, 1);
  put(&E.leaf, 0.04f, -0.04f, -0.15f + sinf(t * 1.3f + 3) * 0.05f, 0.75f, 1);
  if (tell || recoil > 0) halo_at(mx, my, 0.5f + 0.9f * fmaxf(tk, recoil), (cw_color){ 0.85f, 1, 0.35f, 0.45f * fmaxf(tk, recoil) });
}

static void draw_moth(const enemy *e, float t, float tk) {
  bool tell = e->state == ES_TELL, act = e->state == ES_ACT;
  float flare = tell ? fminf(1, tk * 1.5f) : 0;
  float cy = -0.45f;
  float pitch = tell ? -0.25f * flare : act ? 0.35f : sinf(t * 1.7f + e->x) * 0.06f;
  float flap = sinf(t * (tell ? 34 : act ? 10 : 15) + e->x);
  /* wing angle from the body's up: open (tell) high and held, swept back in the swoop */
  float wa = tell ? -1.15f - flare * 0.25f + flap * 0.08f : act ? 0.55f + flap * 0.1f : -0.55f + flap * 0.55f;
  float wsy = tell ? 1.05f : act ? 0.65f : 0.75f + 0.25f * fabsf(flap);
  float spot = tell ? 1.3f + 1.7f * flare : 1.2f;
  for (int side = -1; side <= 1; side += 2) { /* the far side's wings first (behind the body), a little darker */
    float sd = (float)side, k = side < 0 ? 0.62f : 1;
    cw_color c = { D.tint.r * k, D.tint.g * k, D.tint.b * k, 1 };
    float rx = sd * 0.06f, ry = cy - 0.1f, fa = sd * wa, ha = sd * (wa * 0.7f + 0.55f);
    putx(&E.hwing, rx, ry + 0.07f, ha, 1, wsy, side < 0, 0, c, true);
    putx(&E.fwing, rx, ry, fa, 1, wsy, side < 0, 0, c, true);
    /* the eyespot's blaze, where the spot sits on the turned wing */
    cw_vec2 sp = rot2(0.56f * sd, -0.19f * wsy, fa);
    putg(&E.spot, rx + sp.x, ry + sp.y, 0, 0.6f + flare * 0.9f, 0.6f + flare * 0.9f, spot, (0.35f + 0.65f * flare) * k);
    if (tell) halo_at(rx + sp.x, ry + sp.y, 0.3f + 0.6f * flare, (cw_color){ 0.4f, 0.9f, 1, 0.45f * flare });
  }
  for (int k = 0; k < 3; k++) put(&E.mleg, 0.04f, cy + 0.05f, 1.2f + (float)k * 0.25f + sinf(t * 4 + (float)k) * 0.1f, 1, 1);
  put(&E.abdo, 0, cy + 0.08f, pitch + sinf(t * 2.2f) * 0.08f, 1, 1);
  put(&E.thorax, 0, cy, pitch, 1, 1);
  cw_vec2 hp = rot2(0.06f, -0.2f, pitch);
  for (int k = 0; k < 2; k++)
    putk(&E.mant, hp.x + 0.02f * (float)k, cy + hp.y - 0.08f, -1.9f + (float)k * 0.65f + pitch + sinf(t * 2.6f + (float)k) * 0.12f - flare * 0.25f, 1, 1, k ? white(1) : dark(0.75f));
  put(&E.mhead, hp.x, cy + hp.y, pitch, 1, 1);
}

static void draw_hopper(const enemy *e, int region, float t, float tk) {
  bool tell = e->state == ES_TELL, act = e->state == ES_ACT;
  float crouch = tell ? fminf(1, tk * 1.3f) : 0;
  float sq = tell ? 1 - crouch * 0.32f : act ? (e->vy < 0 ? 1.18f : 1.05f) : 1 + sinf(t * 2.4f + e->x) * 0.02f;
  float sx = 1 + (1 - sq) * 0.45f;
  float pitch = act ? (e->vy < 0 ? -0.35f : 0.2f) : tell ? 0.12f * crouch : 0;
  float by = -0.08f;
  /* the hind leg folded under it (the crouch folds it tighter), thrown out straight behind in the leap */
  float hx = -0.3f, hy = -0.3f * sq;
  cw_vec2 K, A;
  if (act) K = v2l(hx - 0.36f, hy + 0.2f), A = v2l(hx - 0.7f, hy + 0.42f);
  else K = v2l(0.04f + crouch * 0.04f, -0.17f * sq), A = v2l(-0.44f - crouch * 0.05f, -0.035f);
  for (int pass = 0; pass < 2; pass++) {
    cw_color c = pass ? white(1) : dark(0.6f);
    if (pass == 1) {
      put(&E.tbody, 0, by, pitch, sx, sq);
      put(&E.growth[region_material(region)], -0.1f, by - 0.62f * sq, pitch, 0.9f, 0.9f);
      float sw = tell ? 1 + crouch * 0.6f : 1 + sinf(t * 5.5f) * 0.06f;
      float ga = tell ? 0.35f + 0.65f * crouch : 0.25f;
      putx(&E.throat, 0.38f, by - 0.3f * sq, pitch, sw, sw, false, 1 + 2.5f * ga, white(1), true);
      float blink = fmodf(t + e->x * 0.37f, 3.7f) < 0.12f ? 0.15f : 1;
      put(&E.teye, 0.3f, by - 0.74f * sq, pitch, 1, blink);
      putk(&E.teye, 0.1f, by - 0.76f * sq, pitch, 0.85f, blink * 0.85f, dark(0.8f));
      if (tell) halo_at(0.4f, by - 0.25f, 0.4f + 0.5f * crouch, (cw_color){ 1, 0.5f, 0.25f, 0.45f * crouch });
    }
    float ox = pass ? 0.05f : -0.05f;
    putk(&E.arm, 0.36f + ox, by - 0.2f * sq, act ? -0.5f : 1.15f, 1, 1, c);
    bone(&E.thigh, v2l(hx + ox, hy), v2l(K.x + ox, K.y), 0.46f, c);
    bone(&E.shin, v2l(K.x + ox, K.y), v2l(A.x + ox, A.y), 0.42f, c);
    putk(&E.tfoot, A.x + ox, A.y, act ? 0.7f : 0.05f, 1, 1, c);
  }
}

static void draw_knight(const enemy *e, float t, float tk) {
  bool tell = e->state == ES_TELL, act = e->state == ES_ACT;
  float raise = tell ? fminf(1, tk * 1.25f) : 0, thrust = act ? fminf(1, (float)e->t / 4) : 0;
  bool walk = e->state == ES_IDLE;
  float ph = walk ? t * 6.5f + e->x : 0, bob = walk ? fabsf(sinf(ph)) * 0.035f : 0;
  float lean = -raise * 0.12f + thrust * 0.22f;
  float hipy = -0.98f - bob + thrust * 0.06f, hipx = thrust * 0.12f;
  /* the cloak behind, swaying with the walk */
  float csw = sinf(t * 2.3f + e->x) * 0.06f + (walk ? -0.08f : 0) - thrust * 0.25f + raise * 0.05f;
  put(&E.cloak, hipx - 0.12f + lean * 0.4f, hipy - 0.72f, csw + 0.05f, 1, 1);
  /* legs: the far one darker */
  for (int k = 0; k < 2; k++) { /* (a positive turn swings the leg back: clockwise from hanging down) */
    float s = k ? 1.0f : -1.0f, p2 = ph + (k ? 3.14159f : 0);
    float sw = walk ? sinf(p2) * 0.38f : thrust * (k ? -0.55f : 0.4f) + raise * (k ? 0.12f : -0.18f);
    float kneeb = walk ? fmaxf(0, cosf(p2)) * 0.55f : 0.06f + thrust * 0.25f * (float)k;
    cw_color c = k ? white(1) : dark(0.6f);
    float hx = hipx + s * 0.07f, hy = hipy + 0.06f;
    putk(&E.kthigh, hx, hy, sw, 1, 1, c);
    cw_vec2 kp = rot2(0, 0.45f, sw);
    putk(&E.kshin, hx + kp.x, hy + kp.y, sw + kneeb, 1, 1, c);
  }
  /* the back arm and the spear: raised up and back in the tell, driven out in the thrust */
  float sha = act ? 0 : tell ? -0.55f * raise : -0.08f + sinf(t * 1.5f) * 0.03f;
  float handx = hipx + 0.15f - raise * 0.35f + thrust * 0.65f, handy = hipy - 0.12f - raise * 0.55f;
  float spear_a = act ? 0 : tell ? -0.25f - 0.35f * raise : 0.05f;
  if (act) handy = -1.1f - 0.13f + 0.13f;
  put(&E.pennant, handx + cosf(spear_a) * 1.38f, handy + sinf(spear_a) * 1.38f - 0.02f, spear_a + 3.14159f * 0 + sinf(t * 9 + e->x) * 0.18f - (act ? 0.2f : 0), -1, 1 + 0.15f * sinf(t * 11));
  put(&E.spear, handx, handy, spear_a, 1, 1);
  putk(&E.karm, hipx + 0.02f + lean * 0.5f, hipy - 0.6f, -0.9f + raise * 1.6f - thrust * 0.9f, 1, 1, dark(0.7f));
  (void)sha;
  put(&E.tabard, hipx + lean * 0.1f, hipy - 0.06f, lean * 0.3f + csw * 0.5f, 1, 1);
  put(&E.torso, hipx + lean * 0.2f, hipy + 0.02f, lean, 1, 1);
  cw_vec2 nk = rot2(0.02f, -0.8f, lean);
  float hx = hipx + nk.x, hy = hipy + nk.y;
  put(&E.plume, hx - 0.12f, hy - 0.44f, sinf(t * 3 + e->x) * 0.15f - thrust * 0.3f, 1, 1);
  put(&E.helm, hx, hy, lean * 0.6f, 1, 1);
  put(&E.pauldron, hx - 0.02f, hy + 0.08f, lean, 1, 1);
  /* the shield in front (lowered a little as the spear rises) */
  put(&E.shield, hipx + 0.42f + thrust * 0.1f, hipy - 0.4f + raise * 0.12f, -0.05f + raise * 0.12f - thrust * 0.15f, 1, 1);
  if (tell) halo_at(hx + 0.12f, hy - 0.25f, 0.3f + 0.5f * raise, (cw_color){ 0.7f, 0.95f, 1, 0.55f * raise });
  if (tell) halo_at(handx + cosf(spear_a) * 1.95f, handy + sinf(spear_a) * 1.95f, 0.35f + 0.35f * raise, (cw_color){ 1, 1, 0.85f, 0.6f * raise });
}

void creature_enemy(const enemy *e, int region, float t) {
  float f = e->facing_left ? -1.0f : 1.0f;
  int tl = enemy_tell_ticks(e->kind);
  float tk = e->state == ES_TELL ? fminf(1, (float)e->t / (float)(tl > 0 ? tl : 30)) : 0;
  begin(e->x, e->y, f, e->flash > 0 ? fminf(1, (float)e->flash / 6) : 0, region, (uint32_t)e->ent * 31u + e->kind);
  switch (e->kind) {
  case EN_CRAWLER: draw_crawler(e, region, t, tk); break;
  case EN_SPITTER: draw_spitter(e, t, tk); break;
  case EN_FLYER: draw_moth(e, t, tk); break;
  case EN_HOPPER: draw_hopper(e, region, t, tk); break;
  default: draw_knight(e, t, tk); break;
  }
  flush();
}

/* ═════════════════════════ the wardens ═════════════════════════
 *   0 the Moss Warden    a hunched beast of bark and moss, root antlers, glowing fungi on its back that lob spores
 *   1 the Glass Mantis   a mantis of glass: faceted head, scythes with lit edges, glass wings, a glowing abdomen
 *   2 the Brass Sentinel a clockwork giant: a riveted barrel chest with a furnace grille, piston limbs, turning gears
 *   3 the Tide Serpent   an eel of the deep halls: a frilled head, scaled segments with glowing lateral lights, fins
 *   4 the Sky Warden     a great pale bird: layered wings, a crest that burns like a lantern, long tail streamers
 *   5 the Heart of Thorns a heart-shaped seed of flesh, veins aglow, wrapped in writhing thorn vines, rooted */
static struct {
  part mbody, mhead, mjaw, mant, mleg, mhleg, mfungi, mbeard;
  part gabdo, gthorax, ghead, garm, gblade, gleg, gwing;
  part bleg1, bleg2, bpelvis, btorso, bcore, bhead, bgear, barm1, barm2, bstack;
  part shead, sjaw, sseg, sfin, stail, swhisk;
  part kbody, khead, kwing, ktail, kleg, kcrest;
  part hroots, hheart, hcore, hvine, hcrown, heye;
} B;

static void paint_wardens(void) {
  /* ── 0 the Moss Warden ── */
  {
    pstyle bark = sty(0x4a3a24, 0x1c140a, 0x8a7448, 0.9f, TX_BARK, 0.07f, 0.8f);
    bark.ink = 2, bark.rim = 0.35f, bark.shadow = 0.4f, bark.shadow_r = 14;
    pstyle moss = sty(0x5f8a3a, 0x203a10, 0xb8e070, 0.6f, TX_MOSS, 0.05f, 1.0f);
    moss.soft = 6, moss.rim = 0.5f, moss.shadow = 0.3f;
    cv(3.1f, 2.1f);
    el(1.5f, 1.15f, 1.3f, 0.75f, -0.05f, &bark);
    el(1.4f, 0.85f, 1.2f, 0.58f, -0.08f, &moss);
    el(0.75f, 0.95f, 0.55f, 0.4f, 0.3f, &moss);
    el(2.25f, 0.95f, 0.5f, 0.38f, -0.3f, &moss);
    pstyle lichen = sty(0xc8d890, 0x6a7a40, 0xffffd0, 0.3f, TX_STONE, 0.02f, 0.6f);
    lichen.alpha = 0.6f, lichen.soft = 4;
    for (int i = 0; i < 7; i++) el(0.6f + 0.3f * (float)i, 0.7f + hsh(i, 21) * 0.4f, 0.08f + hsh(i, 22) * 0.06f, 0.05f, 0, &lichen);
    pstyle fl = sty(0xfff0a0, 0xc09040, 0xffffff, 0.4f, TX_NONE, 0, 0);
    fl.em = 0.4f;
    for (int i = 0; i < 9; i++) el(0.5f + 0.25f * (float)i, 0.55f + hsh(i, 23) * 0.25f, 0.035f, 0.03f, 0, &fl);
    B.mbody = done(1.5f, 1.75f, 3);
    cv(1.3f, 1.05f);
    float sk[16] = { 0.1f, 0.3f, 0.45f, 0.12f, 0.9f, 0.22f, 1.25f, 0.5f, 1.2f, 0.7f, 0.85f, 0.78f, 0.4f, 0.9f, 0.1f, 0.7f };
    pstyle mask = sty(0xd8c8a0, 0x6a5a3a, 0xfff8e0, 1.0f, TX_BARK, 0.04f, 0.6f);
    mask.ink = 2, mask.rim = 0.4f, mask.shadow = 0.4f;
    poly(sk, 8, 0.05f, &mask);
    pstyle sock = sty(0x140c06, 0, 0, -0.5f, TX_NONE, 0, 0);
    sock.soft = 3;
    el(0.78f, 0.42f, 0.13f, 0.1f, 0.2f, &sock);
    el(0.45f, 0.4f, 0.1f, 0.08f, 0, &sock);
    pstyle eye = sty(0xd6ff6a, 0x80c020, 0xffffff, 0.8f, TX_NONE, 0, 0);
    eye.em = 1;
    el(0.8f, 0.42f, 0.06f, 0.05f, 0, &eye);
    el(0.46f, 0.4f, 0.045f, 0.04f, 0, &eye);
    float ns[6] = { 1.05f, 0.5f, 1.15f, 0.55f, 1.18f, 0.62f };
    stk(ns, 3, 0.02f, 0.01f, &sock);
    pstyle ms2 = moss;
    el(0.3f, 0.2f, 0.32f, 0.16f, 0.2f, &ms2);
    B.mhead = done(0.15f, 0.55f, 3);
    cv(0.95f, 0.4f);
    float jw[10] = { 0.05f, 0.08f, 0.85f, 0.12f, 0.92f, 0.22f, 0.5f, 0.33f, 0.08f, 0.28f };
    poly(jw, 5, 0.04f, &mask);
    pstyle tooth = sty(0xf0e8d0, 0x8a7a60, 0xffffff, 0.5f, TX_NONE, 0, 0);
    tooth.ink = 0.8f;
    for (int i = 0; i < 5; i++) lf(0.3f + 0.12f * (float)i, 0.12f, 0.33f + 0.12f * (float)i, 0.01f, 0.02f, 0, &tooth);
    B.mjaw = done(0.08f, 0.15f, 3);
    cv(1.4f, 1.2f);
    float tr[8] = { 0.15f, 1.12f, 0.3f, 0.8f, 0.6f, 0.45f, 1.05f, 0.12f };
    stk(tr, 4, 0.08f, 0.025f, &bark);
    float b1[6] = { 0.45f, 0.6f, 0.75f, 0.55f, 1.15f, 0.62f };
    stk(b1, 3, 0.045f, 0.015f, &bark);
    float b2[6] = { 0.3f, 0.82f, 0.18f, 0.5f, 0.25f, 0.2f };
    stk(b2, 3, 0.045f, 0.012f, &bark);
    float b3[6] = { 0.82f, 0.3f, 1.0f, 0.38f, 1.32f, 0.3f };
    stk(b3, 3, 0.03f, 0.01f, &bark);
    pstyle mt = moss;
    mt.alpha = 0.9f;
    el(0.5f, 0.55f, 0.1f, 0.06f, 0, &mt);
    B.mant = done(0.15f, 1.1f, 3);
    cv(0.7f, 1.2f);
    float lg[8] = { 0.35f, 0.08f, 0.32f, 0.5f, 0.38f, 0.85f, 0.36f, 1.08f };
    stk(lg, 4, 0.2f, 0.12f, &bark);
    el(0.35f, 0.25f, 0.24f, 0.22f, 0, &moss);
    pstyle cl = sty(0x2a2018, 0x0a0806, 0x8a8070, 0.6f, TX_STONE, 0.02f, 0.4f);
    cl.ink = 1, cl.spec = 0.4f;
    for (int i = 0; i < 3; i++) lf(0.2f + 0.12f * (float)i, 1.1f, 0.3f + 0.14f * (float)i, 1.18f, 0.035f, 0, &cl);
    B.mleg = done(0.35f, 0.1f, 3);
    cv(0.8f, 1.1f);
    float hl[8] = { 0.4f, 0.08f, 0.55f, 0.45f, 0.35f, 0.8f, 0.42f, 1.0f };
    stk(hl, 4, 0.24f, 0.1f, &bark);
    el(0.42f, 0.22f, 0.28f, 0.2f, 0, &moss);
    for (int i = 0; i < 3; i++) lf(0.28f + 0.12f * (float)i, 1.02f, 0.38f + 0.14f * (float)i, 1.08f, 0.03f, 0, &cl);
    B.mhleg = done(0.4f, 0.1f, 3);
    cv(1.8f, 0.8f);
    for (int i = 0; i < 6; i++) {
      float x = 0.2f + 0.27f * (float)i, h = 0.25f + hsh(i, 31) * 0.3f;
      pstyle stl = sty(0xe8e0c0, 0x8a8060, 0xffffff, 0.4f, TX_SKIN, 0.02f, 0.4f);
      cap(x, 0.75f, x + 0.03f, 0.75f - h, 0.04f, 0.03f, &stl);
      pstyle capst = sty(0xd6ff6a, 0x5a9a20, 0xffffd0, 0.7f, TX_SKIN, 0.03f, 0.5f);
      capst.em = 0.9f, capst.ink = 1.2f, capst.spec = 0.4f;
      el(x + 0.03f, 0.75f - h, 0.12f + hsh(i, 32) * 0.05f, 0.07f, 0, &capst);
    }
    B.mfungi = done(0.9f, 0.75f, 2.5f);
    cv(1.4f, 0.9f);
    pstyle bd = moss;
    bd.tex_dir = 0, bd.tex = TX_FUR, bd.tex_scale = 0.03f * PPT;
    for (int i = 0; i < 7; i++) {
      float x = 0.12f + 0.19f * (float)i, l = 0.4f + hsh(i, 33) * 0.4f, bp[6] = { x, 0.04f, x + 0.04f, l * 0.5f, x - 0.02f, l };
      stk(bp, 3, 0.07f, 0.015f, &bd);
    }
    B.mbeard = done(0.7f, 0.05f, 2);
  }
  /* ── 1 the Glass Mantis ── */
  {
    pstyle glass = sty(0x3a72a8, 0x0a1838, 0xe8fbff, 0.7f, TX_GLASS, 0.09f, 0.7f);
    glass.spec = 1, glass.gloss = 0.95f, glass.ink = 2.4f, glass.rim = 0.6f, glass.em = 0.18f, glass.shadow = 0.35f;
    pstyle edge = sty(0xdffaff, 0x7ac0e8, 0xffffff, 0.5f, TX_NONE, 0, 0);
    edge.em = 0.9f, edge.spec = 1, edge.gloss = 1;
    cv(1.75f, 1.2f);
    float ab[8] = { 0.12f, 0.4f, 0.55f, 0.52f, 1.0f, 0.65f, 1.5f, 0.82f };
    stk(ab, 4, 0.34f, 0.1f, &glass);
    pstyle inner = sty(0xbff6ff, 0x40a0e0, 0xffffff, 0.3f, TX_VEINS, 0.05f, 0.6f);
    inner.em = 1, inner.alpha = 0.7f, inner.soft = 8;
    float ib[6] = { 0.25f, 0.46f, 0.7f, 0.57f, 1.2f, 0.72f };
    stk(ib, 3, 0.16f, 0.05f, &inner);
    pstyle seg = sty(0x1a3a6a, 0, 0, -0.2f, TX_NONE, 0, 0);
    seg.alpha = 0.6f;
    for (int i = 1; i < 6; i++) el(0.12f + 0.24f * (float)i, 0.42f + 0.07f * (float)i, 0.015f, 0.3f - 0.035f * (float)i, -0.2f, &seg);
    B.gabdo = done(0.14f, 0.4f, 3);
    cv(0.6f, 1.7f);
    float th[8] = { 0.3f, 1.62f, 0.33f, 1.1f, 0.28f, 0.55f, 0.32f, 0.1f };
    stk(th, 4, 0.25f, 0.16f, &glass);
    float te[6] = { 0.42f, 1.5f, 0.44f, 0.8f, 0.4f, 0.15f };
    stk(te, 3, 0.02f, 0.015f, &edge);
    B.gthorax = done(0.3f, 1.55f, 3);
    cv(1.0f, 0.85f);
    float hd[12] = { 0.1f, 0.25f, 0.5f, 0.06f, 0.9f, 0.25f, 0.62f, 0.62f, 0.48f, 0.8f, 0.36f, 0.62f };
    pstyle hg = glass;
    hg.dome = 0.9f;
    poly(hd, 6, 0.04f, &hg);
    pstyle ce = sty(0x60f0ff, 0x1080c0, 0xffffff, 0.9f, TX_SCALES, 0.018f, 0.5f);
    ce.em = 1, ce.spec = 1, ce.gloss = 1, ce.ink = 1.2f;
    el(0.76f, 0.3f, 0.13f, 0.15f, 0.3f, &ce);
    el(0.25f, 0.28f, 0.1f, 0.12f, -0.3f, &ce);
    float an[6] = { 0.55f, 0.1f, 0.7f, -0.1f + 0.12f, 0.95f, 0.02f };
    stk(an, 3, 0.015f, 0.006f, &edge);
    B.ghead = done(0.48f, 0.65f, 3);
    cv(1.1f, 0.3f);
    cap(0.06f, 0.15f, 1.0f, 0.15f, 0.13f, 0.1f, &glass);
    B.garm = done(0.06f, 0.15f, 3);
    cv(1.7f, 0.5f);
    float bl[16] = { 0.05f, 0.12f, 0.6f, 0.02f, 1.15f, 0.06f, 1.6f, 0.25f, 1.68f, 0.45f, 1.3f, 0.3f, 0.7f, 0.3f, 0.08f, 0.32f };
    poly(bl, 8, 0.02f, &glass);
    for (int i = 0; i < 6; i++) lf(0.35f + 0.2f * (float)i, 0.3f, 0.42f + 0.2f * (float)i, 0.42f, 0.025f, 0, &edge); /* the serrated edge */
    float be[6] = { 0.15f, 0.14f, 0.8f, 0.1f, 1.6f, 0.33f };
    stk(be, 3, 0.02f, 0.008f, &edge);
    B.gblade = done(0.06f, 0.2f, 3);
    cv(1.2f, 0.14f);
    pstyle lg = glass;
    lg.em = 0.1f;
    float lp[6] = { 0.05f, 0.07f, 0.6f, 0.06f, 1.15f, 0.08f };
    stk(lp, 3, 0.075f, 0.03f, &lg);
    B.gleg = done(0.05f, 0.07f, 3);
    cv(1.8f, 0.6f);
    pstyle wg = sty(0xa8e8ff, 0x3a7ab0, 0xffffff, 0.2f, TX_MEMBRANE, 0.05f, 0.6f);
    wg.alpha = 0.55f, wg.spec = 1, wg.gloss = 1, wg.em = 0.15f, wg.ink = 1.5f;
    lf(0.08f, 0.3f, 1.75f, 0.3f, 0.22f, -0.05f, &wg);
    float wv[6] = { 0.1f, 0.3f, 0.9f, 0.26f, 1.7f, 0.3f };
    stk(wv, 3, 0.012f, 0.006f, &edge);
    B.gwing = done(0.08f, 0.3f, 2);
  }
  /* ── 2 the Brass Sentinel ── */
  {
    pstyle brass = sty(0xc89a48, 0x4a2a0c, 0xfff0b8, 0.8f, TX_METAL, 0.07f, 0.8f);
    brass.spec = 0.9f, brass.gloss = 0.65f, brass.ink = 2, brass.rim = 0.4f, brass.shadow = 0.45f, brass.shadow_r = 12;
    pstyle iron = sty(0x4a4a50, 0x18181c, 0x9a9aa8, 0.7f, TX_METAL, 0.05f, 0.8f);
    iron.spec = 0.6f, iron.gloss = 0.5f, iron.ink = 1.6f;
    pstyle rivet = sty(0xe0b860, 0x5a3a10, 0xffffff, 0.8f, TX_NONE, 0, 0);
    rivet.spec = 1, rivet.gloss = 0.9f;
    pstyle verd = sty(0x5aa890, 0x2a5a4a, 0xa0e0d0, 0.2f, TX_STONE, 0.03f, 0.7f);
    verd.alpha = 0.5f, verd.soft = 8;
    cv(0.8f, 1.15f);
    cap(0.4f, 0.12f, 0.4f, 0.95f, 0.3f, 0.22f, &brass);
    cap(0.4f, 0.3f, 0.4f, 0.9f, 0.06f, 0.05f, &iron);
    for (int i = 0; i < 4; i++) el(0.16f, 0.3f + 0.18f * (float)i, 0.025f, 0.025f, 0, &rivet), el(0.64f, 0.3f + 0.18f * (float)i, 0.025f, 0.025f, 0, &rivet);
    el(0.42f, 0.7f, 0.15f, 0.2f, 0, &verd);
    B.bleg1 = done(0.4f, 0.12f, 3);
    cv(1.0f, 1.2f);
    cap(0.42f, 0.1f, 0.42f, 0.85f, 0.24f, 0.2f, &iron);
    float ft[10] = { 0.05f, 0.85f, 0.75f, 0.85f, 0.98f, 1.1f, 0.95f, 1.18f, 0.05f, 1.18f };
    poly(ft, 5, 0.04f, &brass);
    for (int i = 0; i < 4; i++) el(0.2f + 0.2f * (float)i, 1.0f, 0.025f, 0.025f, 0, &rivet);
    B.bleg2 = done(0.42f, 0.1f, 3);
    cv(1.8f, 0.7f);
    el(0.9f, 0.35f, 0.8f, 0.3f, 0, &iron);
    for (int i = 0; i < 5; i++) cap(0.35f + 0.27f * (float)i, 0.1f, 0.35f + 0.27f * (float)i, 0.6f, 0.08f, 0.06f, &brass);
    B.bpelvis = done(0.9f, 0.3f, 3);
    cv(2.7f, 2.2f);
    float bt[16] = { 0.35f, 0.2f, 1.35f, 0.05f, 2.35f, 0.2f, 2.6f, 1.0f, 2.3f, 1.95f, 1.35f, 2.12f, 0.4f, 1.95f, 0.1f, 1.0f };
    pstyle chest = brass;
    chest.dome = 1.2f;
    poly(bt, 8, 0.15f, &chest);
    for (int r = 0; r < 3; r++) { /* plate seams with rivets */
      float y = 0.55f + 0.5f * (float)r;
      pstyle seam = sty(0x3a200a, 0, 0, -0.1f, TX_NONE, 0, 0);
      seam.alpha = 0.7f, seam.soft = 2;
      float sp[6] = { 0.25f, y, 1.35f, y - 0.08f, 2.45f, y };
      stk(sp, 3, 0.012f, 0.012f, &seam);
      for (int i = 0; i < 9; i++) el(0.35f + 0.25f * (float)i, y + 0.07f - fabsf((float)i - 4) * 0.005f, 0.028f, 0.028f, 0, &rivet);
    }
    el(0.7f, 1.6f, 0.35f, 0.25f, 0.4f, &verd);
    el(2.0f, 0.7f, 0.3f, 0.2f, -0.3f, &verd);
    pstyle grille = iron;
    grille.dome = -0.3f;
    el(1.35f, 1.1f, 0.5f, 0.45f, 0, &grille);
    pstyle glow = sty(0xffb040, 0xd05010, 0xfff0b0, 0.2f, TX_VEINS, 0.05f, 0.5f);
    glow.em = 1;
    el(1.35f, 1.1f, 0.42f, 0.37f, 0, &glow);
    for (int i = 0; i < 6; i++) cap(0.97f + 0.15f * (float)i, 0.75f, 0.97f + 0.15f * (float)i, 1.45f, 0.035f, 0.035f, &iron);
    pstyle ring = brass;
    ring.dome = 0.4f;
    pc_ellipse(C, 1.35f * PPT, 1.1f * PPT, 0.53f * PPT, 0.48f * PPT, 0, &(pstyle){ .base = pc_hex(0), .alpha = 0.0001f }); /* (nothing) */
    B.btorso = done(1.35f, 2.0f, 3);
    cv(1.0f, 1.0f);
    pstyle cg = sty(0xffd080, 0xff7010, 0xffffe0, 0.3f, TX_NONE, 0, 0);
    cg.em = 1, cg.soft = 18;
    el(0.5f, 0.5f, 0.3f, 0.28f, 0, &cg);
    B.bcore = done(0.5f, 0.5f, 1);
    cv(1.0f, 0.8f);
    float hd[12] = { 0.1f, 0.6f, 0.15f, 0.25f, 0.5f, 0.08f, 0.85f, 0.25f, 0.92f, 0.6f, 0.5f, 0.72f };
    pstyle hb = brass;
    hb.dome = 1.1f;
    poly(hd, 6, 0.06f, &hb);
    pstyle lens = sty(0xffe080, 0xc06010, 0xffffff, 0.9f, TX_NONE, 0, 0);
    lens.em = 1, lens.spec = 1, lens.gloss = 1, lens.ink = 2;
    el(0.66f, 0.42f, 0.13f, 0.13f, 0, &iron);
    el(0.66f, 0.42f, 0.085f, 0.085f, 0, &lens);
    B.bhead = done(0.5f, 0.68f, 3);
    cv(1.1f, 1.1f);
    { /* a gear: teeth round a disc, spokes cut out */
      float g[96];
      for (int i = 0; i < 48; i++) {
        float a = (float)i / 48 * 6.2832f, r = ((i / 2) & 1) ? 0.42f : 0.52f;
        g[i * 2] = 0.55f + cosf(a) * r, g[i * 2 + 1] = 0.55f + sinf(a) * r;
      }
      poly(g, 48, 0.01f, &brass);
      pstyle hole = sty(0x1a1008, 0, 0, -0.4f, TX_NONE, 0, 0);
      for (int i = 0; i < 5; i++) {
        float a = (float)i / 5 * 6.2832f;
        el(0.55f + cosf(a) * 0.24f, 0.55f + sinf(a) * 0.24f, 0.08f, 0.08f, 0, &hole);
      }
      el(0.55f, 0.55f, 0.1f, 0.1f, 0, &iron);
    }
    B.bgear = done(0.55f, 0.55f, 3);
    cv(0.6f, 1.15f);
    cap(0.3f, 0.12f, 0.3f, 1.0f, 0.22f, 0.17f, &brass);
    cap(0.3f, 0.2f, 0.3f, 0.95f, 0.05f, 0.05f, &iron);
    el(0.3f, 0.6f, 0.12f, 0.2f, 0, &verd);
    B.barm1 = done(0.3f, 0.12f, 3);
    cv(0.9f, 1.45f);
    cap(0.38f, 0.1f, 0.38f, 0.95f, 0.2f, 0.16f, &iron);
    for (int i = 0; i < 3; i++) {
      float a = -0.5f + 0.5f * (float)i, cl[6] = { 0.38f, 0.95f, 0.38f + sinf(a) * 0.2f, 1.18f, 0.38f + sinf(a) * 0.3f + 0.08f, 1.38f };
      stk(cl, 3, 0.08f, 0.02f, &brass);
    }
    B.barm2 = done(0.38f, 0.1f, 3);
    cv(0.6f, 1.3f);
    cap(0.3f, 0.2f, 0.3f, 1.2f, 0.16f, 0.2f, &iron);
    el(0.3f, 0.2f, 0.2f, 0.07f, 0, &brass);
    pstyle ember = sty(0xff9040, 0xc03010, 0xffe0a0, 0.2f, TX_NONE, 0, 0);
    ember.em = 1;
    el(0.3f, 0.18f, 0.13f, 0.04f, 0, &ember);
    B.bstack = done(0.3f, 1.2f, 3);
  }
  /* ── 3 the Tide Serpent ── */
  {
    pstyle scale = sty(0x2f9aa0, 0x0c3a44, 0xb0f8f0, 0.9f, TX_SCALES, 0.07f, 0.8f);
    scale.spec = 0.7f, scale.gloss = 0.7f, scale.ink = 2, scale.rim = 0.45f, scale.shadow = 0.4f, scale.shadow_r = 14;
    pstyle belly = sty(0xd8f0d0, 0x6a9a90, 0xffffff, 0.5f, TX_SKIN, 0.04f, 0.4f);
    belly.alpha = 0.85f, belly.soft = 8;
    pstyle lamp = sty(0x8affe6, 0x20c0a0, 0xffffff, 0.6f, TX_NONE, 0, 0);
    lamp.em = 1;
    pstyle fin = sty(0x5ad0c8, 0x1a5a64, 0xd0fff8, 0.2f, TX_MEMBRANE, 0.04f, 0.7f);
    fin.alpha = 0.85f, fin.ink = 1.5f, fin.spec = 0.6f, fin.em = 0.15f;
    pstyle spine = sty(0x1a4a54, 0x08202a, 0x7ac0c0, 0.4f, TX_NONE, 0, 0);
    cv(1.2f, 1.2f);
    for (int i = 0; i < 4; i++) lf(0.3f + 0.2f * (float)i, 0.4f, 0.25f + 0.22f * (float)i, 0.04f, 0.09f, -0.04f, &fin);
    el(0.6f, 0.65f, 0.48f, 0.4f, 0, &scale);
    el(0.62f, 0.85f, 0.38f, 0.16f, 0, &belly);
    for (int i = 0; i < 3; i++) el(0.32f + 0.28f * (float)i, 0.62f, 0.045f, 0.045f, 0, &lamp);
    B.sseg = done(0.6f, 0.65f, 3);
    cv(1.6f, 1.25f);
    for (int i = 0; i < 7; i++) { /* the frill: spines and membrane fanning back */
      float a = 2.2f + (float)i * 0.28f, sx = 0.55f, sy = 0.6f;
      lf(sx, sy, sx + cosf(a) * 0.62f, sy - sinf(a) * 0.6f, 0.1f, 0.02f, &fin);
      float sp[4] = { sx, sy, sx + cosf(a) * 0.6f, sy - sinf(a) * 0.58f };
      stk(sp, 2, 0.02f, 0.006f, &spine);
    }
    float hd[14] = { 0.25f, 0.55f, 0.6f, 0.35f, 1.15f, 0.42f, 1.55f, 0.62f, 1.45f, 0.72f, 0.9f, 0.85f, 0.3f, 0.88f };
    pstyle hs = scale;
    hs.dome = 1.1f, hs.tex_scale = 0.05f * PPT;
    poly(hd, 7, 0.08f, &hs);
    el(1.05f, 0.78f, 0.38f, 0.08f, 0.05f, &belly);
    pstyle sock = sty(0x061418, 0, 0, -0.3f, TX_NONE, 0, 0);
    el(1.08f, 0.52f, 0.11f, 0.08f, 0, &sock);
    pstyle eye = lamp;
    eye.spec = 1, eye.gloss = 1;
    el(1.09f, 0.52f, 0.07f, 0.055f, 0, &eye);
    for (int i = 0; i < 4; i++) el(0.55f + 0.18f * (float)i, 0.68f, 0.03f, 0.03f, 0, &lamp);
    B.shead = done(0.4f, 0.68f, 3);
    cv(1.2f, 0.42f);
    float jw[10] = { 0.05f, 0.08f, 1.05f, 0.1f, 1.15f, 0.18f, 0.6f, 0.36f, 0.08f, 0.3f };
    pstyle js = scale;
    js.tex_scale = 0.04f * PPT;
    poly(jw, 5, 0.04f, &js);
    pstyle tooth = sty(0xf0f8f0, 0x7a9a9a, 0xffffff, 0.5f, TX_NONE, 0, 0);
    for (int i = 0; i < 7; i++) lf(0.3f + 0.11f * (float)i, 0.12f, 0.32f + 0.11f * (float)i, 0.0f, 0.018f, 0, &tooth);
    B.sjaw = done(0.08f, 0.12f, 3);
    cv(0.9f, 0.6f);
    lf(0.05f, 0.1f, 0.85f, 0.45f, 0.16f, 0.08f, &fin);
    for (int i = 0; i < 4; i++) {
      float sp[4] = { 0.05f, 0.1f, 0.6f + 0.06f * (float)i, 0.2f + 0.08f * (float)i };
      stk(sp, 2, 0.012f, 0.005f, &spine);
    }
    B.sfin = done(0.05f, 0.1f, 2);
    cv(1.3f, 1.1f);
    lf(0.1f, 0.55f, 1.25f, 0.12f, 0.24f, -0.1f, &fin);
    lf(0.1f, 0.55f, 1.25f, 0.98f, 0.24f, 0.1f, &fin);
    for (int i = 0; i < 6; i++) {
      float a = -0.6f + 0.24f * (float)i, sp[4] = { 0.1f, 0.55f, 0.1f + cosf(a) * 1.1f, 0.55f + sinf(a) * 0.5f };
      stk(sp, 2, 0.015f, 0.006f, &spine);
    }
    B.stail = done(0.1f, 0.55f, 2);
    cv(0.9f, 0.12f);
    pstyle wh = sty(0x9ae8e0, 0x3a7a7a, 0xffffff, 0.4f, TX_NONE, 0, 0);
    wh.em = 0.4f;
    float wp[6] = { 0.03f, 0.06f, 0.4f, 0.05f, 0.85f, 0.07f };
    stk(wp, 3, 0.02f, 0.005f, &wh);
    B.swhisk = done(0.03f, 0.06f, 2);
  }
  /* ── 4 the Sky Warden ── */
  {
    pstyle plume = sty(0xe4e6f2, 0x7a80a8, 0xffffff, 0.7f, TX_FEATHER, 0.05f, 0.8f);
    plume.ink = 1.8f, plume.rim = 0.5f, plume.shadow = 0.35f, plume.shadow_r = 12;
    pstyle warm = sty(0xf0d8b0, 0x9a7a5a, 0xffffff, 0.5f, TX_FEATHER, 0.035f, 0.7f);
    warm.soft = 5;
    pstyle dark = sty(0x4a4e6a, 0x1a1a2a, 0x9a9ac0, 0.5f, TX_FEATHER, 0.04f, 0.8f);
    dark.ink = 1.5f;
    cv(1.8f, 1.4f);
    el(0.9f, 0.72f, 0.8f, 0.58f, -0.15f, &plume);
    el(1.05f, 0.95f, 0.5f, 0.32f, -0.2f, &warm);
    B.kbody = done(0.9f, 0.72f, 3);
    cv(1.1f, 0.9f);
    el(0.42f, 0.5f, 0.32f, 0.3f, 0, &plume);
    pstyle beak = sty(0xffc860, 0x9a5a10, 0xfff0c0, 0.8f, TX_CHITIN, 0.03f, 0.3f);
    beak.ink = 1.5f, beak.spec = 0.7f, beak.gloss = 0.6f;
    float bk[10] = { 0.62f, 0.4f, 1.0f, 0.5f, 1.06f, 0.62f, 0.95f, 0.66f, 0.64f, 0.6f };
    poly(bk, 5, 0.02f, &beak);
    pstyle mask = sty(0x30324a, 0x101020, 0x6a6a90, 0.3f, TX_FEATHER, 0.02f, 0.5f);
    mask.alpha = 0.8f, mask.soft = 3;
    el(0.55f, 0.43f, 0.12f, 0.07f, 0.1f, &mask);
    pstyle eye = sty(0xffe6a0, 0xd08020, 0xffffff, 0.9f, TX_NONE, 0, 0);
    eye.em = 1, eye.spec = 1, eye.gloss = 1;
    el(0.57f, 0.42f, 0.05f, 0.05f, 0, &eye);
    B.khead = done(0.3f, 0.65f, 3);
    cv(1.1f, 0.9f);
    pstyle crest = sty(0xffe6a0, 0xd88a30, 0xffffff, 0.4f, TX_FEATHER, 0.02f, 0.6f);
    crest.em = 0.9f, crest.ink = 1;
    for (int i = 0; i < 5; i++) lf(0.5f, 0.82f, 0.15f + 0.08f * (float)i, 0.1f + 0.12f * fabsf((float)i - 2), 0.06f, 0.06f, &crest);
    B.kcrest = done(0.5f, 0.82f, 2);
    cv(2.8f, 1.4f);
    for (int i = 0; i < 8; i++) { /* primaries and secondaries, longest at the tip */
      float u = (float)i / 7, ax = 0.3f + u * 1.6f, ay = 0.35f + u * 0.05f, l = 0.6f + u * 0.7f;
      pstyle fp = i > 4 ? dark : plume;
      fp.seed = 300 + (uint32_t)i, fp.tex_dir = 0.5f;
      lf(ax, ay, ax + 0.35f + u * 0.5f, ay + l, 0.13f, 0.05f, &fp);
    }
    float arm[6] = { 0.15f, 0.42f, 1.0f, 0.3f, 2.1f, 0.35f };
    pstyle cov = plume;
    cov.dome = 0.8f;
    stk(arm, 3, 0.26f, 0.1f, &cov);
    for (int i = 0; i < 6; i++) { /* covert rows */
      pstyle c2 = warm;
      c2.alpha = 0.9f, c2.seed = 400 + (uint32_t)i;
      lf(0.3f + 0.28f * (float)i, 0.38f, 0.42f + 0.3f * (float)i, 0.7f, 0.1f, 0.03f, &c2);
    }
    B.kwing = done(0.15f, 0.42f, 2.5f);
    cv(2.0f, 0.4f);
    pstyle rib = plume;
    rib.tex_dir = 1.57f;
    lf(0.05f, 0.2f, 1.95f, 0.2f, 0.12f, 0.06f, &rib);
    pstyle tip = crest;
    tip.em = 0.6f;
    el(1.75f, 0.2f, 0.12f, 0.07f, 0, &tip);
    B.ktail = done(0.05f, 0.2f, 2);
    cv(0.5f, 0.8f);
    pstyle sk = sty(0xd8a050, 0x6a4010, 0xffe0a0, 0.6f, TX_SCALES, 0.03f, 0.6f);
    sk.ink = 1.3f;
    cap(0.25f, 0.05f, 0.25f, 0.6f, 0.07f, 0.05f, &sk);
    pstyle cl = sty(0x2a2020, 0x0a0808, 0x8a7a70, 0.6f, TX_NONE, 0, 0);
    cl.spec = 0.6f;
    for (int i = 0; i < 3; i++) lf(0.25f, 0.6f, 0.08f + 0.17f * (float)i, 0.78f, 0.03f, 0.02f, &cl);
    B.kleg = done(0.25f, 0.05f, 3);
  }
  /* ── 5 the Heart of Thorns ── */
  {
    pstyle bark = sty(0x4a2a2a, 0x180a0c, 0x8a5a50, 0.8f, TX_BARK, 0.06f, 0.8f);
    bark.ink = 2, bark.rim = 0.3f, bark.shadow = 0.4f, bark.shadow_r = 16;
    cv(4.8f, 1.3f);
    for (int i = 0; i < 9; i++) {
      float a = (float)i / 8, x0 = 2.4f + (a - 0.5f) * 1.2f, x2 = 0.15f + a * 4.5f, rp[6] = { x0, 0.15f, (x0 + x2) * 0.5f, 0.6f + hsh(i, 41) * 0.3f, x2, 1.2f };
      stk(rp, 3, 0.2f, 0.04f, &bark);
    }
    B.hroots = done(2.4f, 0.95f, 3);
    cv(3.6f, 3.8f);
    pstyle flesh = sty(0x9a3a58, 0x3a0a20, 0xf090a8, 1.0f, TX_VEINS, 0.12f, 0.8f);
    flesh.ink = 2.2f, flesh.rim = 0.4f, flesh.shadow = 0.4f, flesh.shadow_r = 18, flesh.spec = 0.45f, flesh.gloss = 0.55f, flesh.em = 0.15f;
    { /* the heart's outline (the classic curve), then its two lobes swelling inside it */
      float hp[96];
      for (int i = 0; i < 48; i++) {
        float a = (float)i / 48 * 6.2832f, sx = sinf(a), hx = 16 * sx * sx * sx, hy = 13 * cosf(a) - 5 * cosf(2 * a) - 2 * cosf(3 * a) - cosf(4 * a);
        hp[i * 2] = 1.8f + hx * 0.1f, hp[i * 2 + 1] = 1.75f - hy * 0.1f;
      }
      poly(hp, 48, 0.1f, &flesh);
      pstyle lobe = flesh;
      lobe.alpha = 0.55f, lobe.soft = 14, lobe.ink = 0, lobe.shadow = 0, lobe.seed = 77, lobe.dome = 0.6f, lobe.lift = 0.4f;
      el(1.15f, 1.35f, 0.75f, 0.85f, -0.4f, &lobe);
      el(2.45f, 1.35f, 0.75f, 0.85f, 0.4f, &lobe);
    }
    pstyle cleft = sty(0x2a0612, 0, 0, -0.5f, TX_NONE, 0, 0);
    cleft.soft = 6, cleft.alpha = 0.8f;
    float cp[6] = { 1.8f, 0.6f, 1.75f, 1.3f, 1.8f, 2.0f };
    stk(cp, 3, 0.06f, 0.02f, &cleft);
    pstyle vein = sty(0xffb060, 0xff5020, 0xffffd0, 0.3f, TX_NONE, 0, 0);
    vein.em = 0.9f, vein.soft = 2.5f;
    for (int i = 0; i < 10; i++) {
      float a = (float)i / 10 * 6.2832f, r1 = 0.5f, r2 = 1.25f + hsh(i, 43) * 0.25f, vp[6] = { 1.8f + cosf(a) * r1, 2.0f + sinf(a) * r1, 1.8f + cosf(a + 0.3f) * (r1 + r2) * 0.5f, 2.0f + sinf(a + 0.3f) * (r1 + r2) * 0.5f * 1.1f, 1.8f + cosf(a + 0.1f) * r2, 2.0f + sinf(a + 0.1f) * r2 * 1.15f };
      stk(vp, 3, 0.035f, 0.01f, &vein);
    }
    B.hheart = done(1.8f, 3.5f, 3);
    cv(1.6f, 1.6f);
    pstyle core = sty(0xffd080, 0xff6020, 0xffffe0, 0.3f, TX_VEINS, 0.06f, 0.5f);
    core.em = 1, core.soft = 30;
    el(0.8f, 0.8f, 0.5f, 0.55f, 0, &core);
    B.hcore = done(0.8f, 0.8f, 1);
    cv(3.2f, 0.9f);
    pstyle vine = sty(0x3a4a24, 0x101a08, 0x8aa050, 0.8f, TX_BARK, 0.04f, 0.7f);
    vine.ink = 1.6f, vine.rim = 0.35f, vine.shadow = 0.35f;
    float vp[10] = { 0.05f, 0.45f, 0.8f, 0.2f, 1.6f, 0.62f, 2.4f, 0.26f, 3.1f, 0.48f };
    stk(vp, 5, 0.11f, 0.035f, &vine);
    pstyle thorn = sty(0xd8c0a0, 0x5a4030, 0xffffff, 0.6f, TX_NONE, 0, 0);
    thorn.ink = 1, thorn.spec = 0.5f;
    for (int i = 0; i < 10; i++) {
      float x = 0.25f + 0.28f * (float)i, y = 0.42f + sinf(x * 3.93f - 0.55f) * -0.2f, up = (i & 1) ? -1.0f : 1.0f;
      lf(x, y + up * 0.05f, x + 0.12f, y + up * 0.26f, 0.035f, 0, &thorn);
    }
    B.hvine = done(0.05f, 0.45f, 3);
    cv(3.2f, 1.3f);
    for (int i = 0; i < 9; i++) {
      float x = 0.25f + 0.34f * (float)i, h = 0.5f + 0.5f * sinf((float)i / 8 * 3.1416f);
      lf(x, 1.2f, x + (hsh(i, 51) - 0.5f) * 0.3f, 1.2f - h, 0.09f, (hsh(i, 52) - 0.5f) * 0.1f, &thorn);
    }
    float cr[6] = { 0.1f, 1.15f, 1.6f, 1.0f, 3.1f, 1.15f };
    stk(cr, 3, 0.12f, 0.12f, &vine);
    B.hcrown = done(1.6f, 1.15f, 3);
    cv(0.9f, 0.7f);
    pstyle lid = sty(0x7a2a44, 0x2a0818, 0xd07090, 0.9f, TX_SKIN, 0.03f, 0.5f);
    lid.ink = 2;
    el(0.45f, 0.35f, 0.4f, 0.28f, 0, &lid);
    pstyle iris = sty(0xffe080, 0xff8020, 0xffffff, 0.8f, TX_VEINS, 0.03f, 0.5f);
    iris.em = 1, iris.spec = 1, iris.gloss = 1;
    el(0.45f, 0.35f, 0.27f, 0.19f, 0, &iris);
    pstyle pup = sty(0x200008, 0, 0, 0.3f, TX_NONE, 0, 0);
    el(0.45f, 0.35f, 0.05f, 0.17f, 0, &pup);
    B.heye = done(0.45f, 0.35f, 3);
  }
}

/* ── posing the wardens ── */
static void boss_moss(const boss *b, float t, float tk) {
  bool tell = b->state == ES_TELL, act = b->state == ES_ACT;
  float breathe = sinf(t * 2.2f) * 0.04f;
  float crouch = tell && (b->move == BM_LEAP || b->move == BM_CHARGE) ? tk : 0;
  bool charging = act && b->move == BM_CHARGE, leaping = act && b->move == BM_LEAP;
  float ph = charging ? t * 14 : 0;
  float pitch = charging ? 0.12f : leaping ? (b->vy < 0 ? -0.2f : 0.15f) : -breathe * 0.5f - crouch * 0.05f;
  float by = -1.15f + crouch * 0.35f - breathe + (charging ? fabsf(sinf(ph)) * -0.08f : 0);
  float headdrop = (tell && b->move == BM_CHARGE ? tk * 0.45f : 0) + (charging ? 0.35f : 0);
  /* legs: far pair, then body, then near pair */
  for (int pass = 0; pass < 2; pass++) {
    cw_color c = pass ? white(1) : dark(0.55f);
    float o = pass ? 0.12f : -0.12f, g = pass ? 0 : 3.14159f;
    float fsw = charging ? sinf(ph + g) * 0.5f : leaping ? -0.6f : crouch * 0.35f;
    float hsw = charging ? sinf(ph + g + 1.6f) * 0.5f : leaping ? 0.7f : -crouch * 0.4f;
    if (pass == 1) {
      put(&B.mbody, 0, by, pitch, 1, 1);
      float fg = tell && b->move == BM_VOLLEY ? 1.4f + 3.6f * tk : act && b->move == BM_VOLLEY ? 4 : 1.15f + 0.25f * sinf(t * 3);
      cw_vec2 fp = rot2(-0.15f, -1.02f, pitch);
      putx(&B.mfungi, fp.x, by + fp.y, pitch, 1 + 0.12f * (tell && b->move == BM_VOLLEY ? tk : 0), 1 + 0.25f * (tell && b->move == BM_VOLLEY ? tk : 0), false, fg, white(1), true);
      if (tell && b->move == BM_VOLLEY) halo_at(fp.x, by + fp.y - 0.3f, 1 + 1.4f * tk, (cw_color){ 0.8f, 1, 0.4f, 0.5f * tk });
      put(&B.mbeard, 0.2f, by + 0.45f, sinf(t * 1.8f) * 0.06f + (charging ? -0.3f : 0), 1, 1 + 0.05f * sinf(t * 2.5f));
      cw_vec2 nk = rot2(1.15f, -0.25f, pitch);
      float hx = nk.x, hy = by + nk.y + headdrop * 0.5f, hr = pitch + headdrop * 0.6f;
      for (int k = 0; k < 2; k++) putk(&B.mant, hx + 0.1f - 0.15f * (float)k, hy - 0.35f, hr - 0.15f - 0.25f * (float)k + sinf(t * 1.3f + (float)k) * 0.02f, 1 - 0.1f * (float)k, 1 - 0.1f * (float)k, k ? dark(0.7f) : white(1));
      float jaw = tell && b->move == BM_VOLLEY ? 0.2f * tk : charging ? 0.35f : 0.05f + sinf(t * 1.5f) * 0.03f;
      cw_vec2 jp = rot2(0.2f, 0.25f, hr);
      put(&B.mjaw, hx + jp.x, hy + jp.y, hr + jaw, 1, 1);
      put(&B.mhead, hx, hy, hr, 1, 1);
      cw_vec2 ep = rot2(0.65f, -0.15f, hr);
      if (tell) halo_at(hx + ep.x, hy + ep.y, 0.5f + 0.6f * tk, (cw_color){ 0.85f, 1, 0.4f, 0.55f * tk });
    }
    cw_vec2 fh = rot2(0.75f + o, -0.2f, pitch), hh = rot2(-0.85f + o, -0.25f, pitch);
    putk(&B.mleg, fh.x, by + fh.y, fsw, 1, (-(by + fh.y)) / 1.08f, c);
    putk(&B.mhleg, hh.x, by + hh.y, hsw, 1, (-(by + hh.y)) / 1.0f, c);
  }
}

static void boss_mantis(const boss *b, float t, float tk) {
  bool tell = b->state == ES_TELL, act = b->state == ES_ACT;
  bool sweep = b->move == BM_SWEEP_LOW || b->move == BM_SWEEP_HIGH;
  float up = tell && sweep ? tk : 0, crouch = tell && b->move == BM_LEAP ? tk : 0;
  float swing = act && sweep ? fminf(1, (float)b->t / 8) : 0;
  float sway = sinf(t * 1.6f) * 0.05f, hipy = -1.45f + crouch * 0.4f;
  float lean = sway - up * 0.15f + swing * 0.25f + (tell && b->move == BM_RAIN ? -0.2f * tk : 0);
  /* far legs, wings, abdomen */
  for (int k = 0; k < 2; k++) {
    float lx = -0.25f + 0.35f * (float)k, fx = lx + (k ? 0.7f : -0.9f);
    float a1, a2;
    ik(lx, hipy, fx, 0, 0.95f, 1.05f, k ? -1 : 1, &a1, &a2);
    putk(&B.gleg, lx, hipy, a1, 0.85f, 1, dark(0.55f));
    putk(&B.gleg, lx + cosf(a1) * 0.95f, hipy + sinf(a1) * 0.95f, a2, 0.92f, 1, dark(0.55f));
  }
  float wopen = tell && b->move == BM_RAIN ? tk : 0;
  put(&B.gwing, -0.1f, hipy - 0.9f, 2.55f - wopen * 1.1f + sinf(t * 2) * 0.03f, 1, 1);
  float ag = tell && b->move == BM_RAIN ? 1.3f + 3.5f * tk : 1.2f + 0.3f * sinf(t * 2.4f);
  putx(&B.gabdo, -0.2f, hipy + 0.05f, 2.45f + sway * 0.5f, 1.1f, 1.1f, false, 0, D.tint, true);
  putx(&B.gabdo, -0.2f, hipy + 0.05f, 2.45f + sway * 0.5f, 1.1f, 1.1f, false, ag, (cw_color){ 1, 1, 1, 0.25f + 0.4f * (tell && b->move == BM_RAIN ? tk : 0) }, false);
  put(&B.gthorax, 0, hipy, lean, 1, 1);
  cw_vec2 sh = rot2(0.05f, -1.15f, lean), nk = rot2(0.1f, -1.5f, lean);
  /* the scythes: raised high and back in a sweep's tell, slashing through in its act */
  for (int k = 0; k < 2; k++) {
    float a = -0.9f - up * 1.3f + swing * 1.9f + (float)k * 0.15f + sinf(t * 1.8f + (float)k) * 0.04f;
    float bladea = a + 2.2f - up * 0.6f - swing * 0.5f;
    cw_color c = k ? white(1) : dark(0.6f);
    float sx = sh.x - 0.08f * (float)(1 - k), sy = hipy + sh.y;
    putk(&B.garm, sx, sy, a, 1, 1, c);
    putk(&B.gblade, sx + cosf(a) * 0.94f, sy + sinf(a) * 0.94f, bladea, 1, 1, c);
    if (k && tell && sweep) halo_at(sx + cosf(a) * 0.94f + cosf(bladea) * 0.8f, sy + sinf(a) * 0.94f + sinf(bladea) * 0.8f, 0.5f + 0.7f * up, (cw_color){ 0.6f, 0.95f, 1, 0.55f * up });
    if (k == 0) { /* near legs after the far scythe */
      for (int j = 0; j < 2; j++) {
        float lx = -0.15f + 0.3f * (float)j, fx = lx + (j ? 0.95f : -0.7f), a1, a2;
        ik(lx, hipy + 0.05f, fx, 0, 0.95f, 1.05f, j ? -1 : 1, &a1, &a2);
        put(&B.gleg, lx, hipy + 0.05f, a1, 0.85f, 1);
        put(&B.gleg, lx + cosf(a1) * 0.95f, hipy + 0.05f + sinf(a1) * 0.95f, a2, 0.92f, 1);
      }
    }
  }
  float hr = lean * 0.5f - (tell && b->move == BM_RAIN ? 0.4f * tk : 0) + sinf(t * 0.9f) * 0.08f;
  put(&B.ghead, nk.x, hipy + nk.y, hr, 1, 1);
  if (tell) halo_at(nk.x + 0.3f, hipy + nk.y - 0.3f, 0.4f + 0.5f * tk, (cw_color){ 0.5f, 0.95f, 1, 0.5f * tk });
}

static void boss_brass(const boss *b, float t, float tk) {
  bool tell = b->state == ES_TELL, act = b->state == ES_ACT;
  float raise = tell && b->move == BM_SWEEP_HIGH ? tk : 0, crouch = tell && b->move == BM_CHARGE ? tk : 0;
  bool charging = act && b->move == BM_CHARGE;
  float ph = charging ? t * 10 : 0, step = charging ? fabsf(sinf(ph)) * 0.08f : 0;
  float hipy = -1.95f + crouch * 0.3f - step, lean = crouch * 0.18f + (charging ? 0.2f : 0) + sinf(t * 0.8f) * 0.015f;
  float coreg = tell && (b->move == BM_VOLLEY || b->move == BM_SUMMON) ? 1.5f + 4 * tk : act && b->move == BM_VOLLEY ? 4 : 1.3f + 0.3f * sinf(t * 3);
  /* the stack and the far arm and leg behind */
  cw_vec2 sp = rot2(-0.95f, -2.6f, lean);
  put(&B.bstack, sp.x, hipy + sp.y + 1.2f, lean - 0.2f, 1, 1);
  for (int pass = 0; pass < 2; pass++) {
    cw_color c = pass ? white(1) : dark(0.55f);
    float o = pass ? 0.35f : -0.35f, g = pass ? 0 : 3.14159f;
    float sw = charging ? sinf(ph + g) * 0.35f : -crouch * 0.2f * (pass ? 1 : -1);
    if (pass == 1) {
      put(&B.bpelvis, 0, hipy, lean * 0.3f, 1, 1);
      put(&B.btorso, 0, hipy - 0.05f, lean, 1, 1);
      cw_vec2 cp = rot2(0, -0.95f, lean);
      putg(&B.bcore, cp.x, hipy + cp.y, 0, 1 + 0.3f * (tell ? tk : 0), 1 + 0.3f * (tell ? tk : 0), coreg, 0.55f + 0.45f * (tell ? tk : 0));
      if (tell && (b->move == BM_VOLLEY || b->move == BM_SUMMON)) halo_at(cp.x, hipy + cp.y, 0.8f + 1.2f * tk, (cw_color){ 1, 0.6f, 0.2f, 0.55f * tk });
      cw_vec2 hp = rot2(0.25f, -1.85f, lean);
      put(&B.bhead, hp.x, hipy + hp.y, lean * 0.5f + (charging ? 0.15f : 0), 1, 1);
      if (tell) halo_at(hp.x + 0.16f, hipy + hp.y - 0.26f, 0.35f + 0.5f * tk, (cw_color){ 1, 0.85f, 0.4f, 0.5f * tk });
    }
    putk(&B.bleg1, o * 0.8f, hipy + 0.1f, sw, 1, 1, c);
    cw_vec2 kn = rot2(0, 0.85f, sw);
    putk(&B.bleg2, o * 0.8f + kn.x, hipy + 0.1f + kn.y, -sw * 0.6f + crouch * 0.1f, 1, (1.95f - crouch * 0.3f + step - 0.95f) / 1.08f, c);
    /* arms hang from the gears; raised over the head in the high sweep's tell, swung through in its act */
    cw_vec2 shd = rot2(o * 2.7f + 0.1f, -1.75f, lean);
    float swing = act && b->move == BM_SWEEP_HIGH ? fminf(1, (float)b->t / 6) : 0;
    float aa = (pass ? -0.25f : 0.2f) - raise * 2.4f + swing * 1.4f + (charging ? -0.6f : 0) + (tell && b->move == BM_SUMMON ? (pass ? -0.9f : 0.9f) * tk : 0);
    float ax = shd.x, ay = hipy + shd.y;
    putk(&B.barm1, ax, ay, aa, 1, 1, c);
    cw_vec2 el = rot2(0, 0.95f, aa);
    putk(&B.barm2, ax + el.x, ay + el.y, aa - 0.25f - raise * 0.3f, 1, 1, c);
    float ga = t * (0.8f + (tell ? 2.5f * tk : 0) + (act ? 3 : 0)) * (pass ? 1 : -1);
    putk(&B.bgear, ax, ay, ga, 1, 1, c);
  }
  if (((int)(t * 60) % 9) == 0 && D.die <= 0) {
    cw_vec2 w = W(sp.x, hipy + sp.y);
    view_emit(w.x, w.y, -D.f * 0.4f, -1.6f, 1.6f, 0.9f, (cw_color){ 0.55f, 0.5f, 0.48f, 0.35f }, false, -0.6f);
  }
}

static void boss_serpent(const boss *b, float t, float tk) {
  bool tell = b->state == ES_TELL, act = b->state == ES_ACT;
  float coil = tell && b->move == BM_DIVE ? tk : 0, flare = tell && b->move == BM_PILLARS ? tk : 0;
  float cy = -boss_hh(3);
  float amp = 0.45f + coil * 0.45f, spd = 3 + coil * 3 + (act ? 4 : 0);
  /* the body: a wave travelling to the tail; the head leads */
  enum { N = 8 };
  cw_vec2 p[N + 1];
  /* walked from the head back: each step as long as its segment is big, its heading swinging on the wave (the coil
   * winds the wave tighter), so the segments always touch */
  p[0] = (cw_vec2){ 0.6f - coil * 0.6f, cy };
  for (int i = 1; i <= N; i++) {
    float u = (float)i / N, step = 0.5f * (1.05f - u * 0.55f);
    float head = sinf(t * spd - u * (5.5f + coil * 3)) * amp * 1.3f * (0.4f + u) + 0.15f;
    p[i] = (cw_vec2){ p[i - 1].x - cosf(head) * step, p[i - 1].y + sinf(head) * step };
  }
  put(&B.stail, p[N].x, p[N].y, atan2f(p[N].y - p[N - 1].y, p[N].x - p[N - 1].x) + sinf(t * spd - 6) * 0.3f, 1, 1);
  for (int i = N - 1; i >= 1; i--) {
    float u = (float)i / N, a = atan2f(p[i - 1].y - p[i].y, p[i - 1].x - p[i].x);
    put(&B.sseg, p[i].x, p[i].y, a, 1.05f - u * 0.55f, 1.05f - u * 0.55f);
    if (i == 2) put(&B.sfin, p[i].x, p[i].y + 0.2f, 0.6f + sinf(t * 4) * 0.25f, 1, 1);
  }
  float ha = atan2f(p[0].y - p[1].y, p[0].x - p[1].x) * 0.6f - coil * 0.35f;
  float jaw = (tell && (b->move == BM_DIVE || b->move == BM_VOLLEY)) ? 0.15f + 0.4f * tk : act ? 0.45f : 0.06f + sinf(t * 1.7f) * 0.04f;
  cw_vec2 jp = rot2(0.2f, 0.12f, ha);
  put(&B.sjaw, p[0].x + jp.x, p[0].y + jp.y, ha + jaw, 1, 1);
  for (int k = 0; k < 2; k++) put(&B.swhisk, p[0].x + rot2(0.95f, 0.1f, ha).x, p[0].y + rot2(0.95f, 0.1f, ha).y, ha + 0.6f + 0.3f * (float)k + sinf(t * 3 + (float)k) * 0.25f, 1, 1);
  putx(&B.shead, p[0].x, p[0].y, ha, 1, 1 + flare * 0.12f, false, flare > 0 ? 1 + 2 * flare : 0, flare > 0 ? (cw_color){ 1, 1, 1, 1 } : D.tint, true);
  if (tell) halo_at(p[0].x + 0.2f, p[0].y - 0.5f, 0.8f + 1.2f * tk, (cw_color){ 0.5f, 1, 0.9f, 0.5f * tk });
}

static void boss_bird(const boss *b, float t, float tk) {
  bool tell = b->state == ES_TELL, act = b->state == ES_ACT;
  float fold = tell && b->move == BM_DIVE ? tk : act && b->move == BM_DIVE ? 1 : 0;
  float spread = tell && (b->move == BM_VOLLEY || b->move == BM_RAIN) ? tk : 0;
  float cy = -boss_hh(4);
  float flap = sinf(t * (5 + spread * 4)) * (1 - fold) * (1 - spread * 0.6f);
  float pitch = fold * 0.5f - spread * 0.15f;
  for (int k = 0; k < 3; k++) put(&B.ktail, -0.5f, cy + 0.2f, 2.7f + (float)k * 0.18f + sinf(t * 2.2f + (float)k) * 0.12f + fold * 0.3f, 1 - 0.1f * (float)k, 1);
  /* seen from the side: both wings rise from the shoulders and sweep back (the mirrored wing: its arm toward the tail,
   * the feathers trailing under it); a turn of +1.57 holds it straight up, 0 straight back; the far wing behind */
  float wa = 0.65f + flap * 0.75f + spread * 0.55f + fold * 0.55f;
  putx(&B.kwing, 0.15f, cy - 0.3f, wa - 0.3f, 0.9f, 1 - fold * 0.4f, true, 0,(cw_color){ D.tint.r * 0.6f, D.tint.g * 0.6f, D.tint.b * 0.6f, 1 }, true);
  for (int k = 0; k < 2; k++) put(&B.kleg, 0.1f + 0.15f * (float)k, cy + 0.35f, 0.3f + fold * 0.9f - (float)k * 0.1f, 1, 1);
  put(&B.kbody, 0, cy, pitch, 1, 1);
  cw_vec2 hp = rot2(0.75f, -0.45f, pitch);
  float hr = pitch * 0.5f + sinf(t * 1.3f) * 0.05f;
  float cg = tell ? 1.4f + 3.6f * tk : 1.3f + 0.3f * sinf(t * 2.7f);
  cw_vec2 cp = rot2(-0.02f, -0.45f, hr);
  putg(&B.kcrest, hp.x + cp.x, cy + hp.y + cp.y, hr - 0.4f - spread * 0.3f, 1 + 0.2f * tk, 1 + 0.3f * tk, cg, 1);
  put(&B.khead, hp.x, cy + hp.y, hr, 1, 1);
  putx(&B.kwing, 0.05f, cy - 0.25f, wa, 1, 1 - fold * 0.4f, true, 0, D.tint, true);
  if (tell) halo_at(hp.x, cy + hp.y - 0.6f, 0.6f + 0.8f * tk, (cw_color){ 1, 0.9f, 0.6f, 0.55f * tk });
}

static void boss_heart(const boss *b, float t, float tk) {
  bool tell = b->state == ES_TELL;
  float rate = 6 + (tell ? 6 * tk : 0), beat = powf(fmaxf(0, sinf(t * rate)), 6);
  float s = 1 + beat * 0.05f + (tell ? tk * 0.04f : 0);
  float rise = tell && b->move == BM_SUMMON ? tk : 0, lash = tell && b->move == BM_SWEEP_LOW ? tk : 0;
  put(&B.hroots, 0, 0.1f, 0, 1, 1);
  float cy = -0.2f;
  /* vines behind, the heart, vines in front */
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1) {
      put(&B.hheart, 0, cy, sinf(t * 0.7f) * 0.02f, s, s);
      float cg = 1.3f + beat * 1.5f + (tell ? 3 * tk : 0);
      putg(&B.hcore, 0, cy - 1.7f * s, 0, s * (1 + beat * 0.2f), s * (1 + beat * 0.2f), cg, 0.55f + 0.35f * beat + 0.1f * tk);
      put(&B.hcrown, 0, cy - 3.25f * s, sinf(t * 0.9f) * 0.03f, 1, 1 + rise * 0.3f);
      float look = sinf(t * 0.6f) * 0.08f;
      putx(&B.heye, 0.15f + look, cy - 1.9f * s, 0, 1, tell ? 1.15f : 0.85f + 0.15f * beat, false, tell ? 2 + 2 * tk : 1.6f, white(1), true);
      if (tell) halo_at(0.15f, cy - 1.9f, 1.2f + 1.6f * tk, (cw_color){ 1, 0.6f, 0.35f, 0.5f * tk });
      continue;
    }
    for (int k = 0; k < 6; k++) {
      float a0 = (float)k / 6 * 6.2832f + t * 0.25f, wr = sinf(t * 1.4f + (float)k * 1.3f) * 0.18f;
      float r = 0.9f, ang = a0 + wr - rise * 0.5f;
      float vx = cosf(a0) * r, vy = cy - 1.7f + sinf(a0) * r * 1.2f;
      put(&B.hvine, vx, vy, ang + 1.2f, 0.75f + 0.25f * sinf((float)k), 1);
    }
  }
  for (int k = 0; k < 4; k++) { /* vines in front, lashing low in that tell */
    float a0 = 0.6f + (float)k * 1.5f + sinf(t * 0.9f + (float)k) * 0.2f;
    float vx = cosf(a0) * 1.2f, vy = cy - 1.5f + sinf(a0) * 1.2f;
    put(&B.hvine, vx - 1.2f * cosf(a0 + 0.9f), vy, a0 + 0.9f + lash * 0.5f * ((k & 1) ? 1 : -1) + rise * -0.6f, 0.8f, 1);
  }
}

void creature_boss(const boss *b, int region, float t) {
  if (!b->active && !(D.die > 0)) return;
  float f = b->facing_left ? -1.0f : 1.0f;
  float tk = b->state == ES_TELL ? fminf(1, (float)b->t / (float)boss_tell_ticks(b->move)) : 0;
  float die = D.die;
  begin(b->x, b->y, f, b->flash > 0 ? fminf(1, (float)b->flash / 5) : 0, region, 900u + b->idx);
  D.die = die;
  static const float SCALE[NBOSSES] = { 1.15f, 1.15f, 1, 1, 1.4f, 1 }; /* (to fill each one's body) */
  D.s = SCALE[b->idx];
  switch (b->idx) {
  case 0: boss_moss(b, t, tk); break;
  case 1: boss_mantis(b, t, tk); break;
  case 2: boss_brass(b, t, tk); break;
  case 3: boss_serpent(b, t, tk); break;
  case 4: boss_bird(b, t, tk); break;
  default: boss_heart(b, t, tk); break;
  }
  flush();
  /* in front of it: the warden is lit, not blown out */
  if (D.die <= 0) {
    static const uint32_t GLOW[NBOSSES] = { 0xd6ff6a, 0xbff6ff, 0xffd060, 0x8affe6, 0xffe6a0, 0xffb060 };
    float hw = boss_hw(b->idx), hh = boss_hh(b->idx);
    view_light(b->x + f * hw * 1.5f, b->y - hh * 2, 8, pc_hex(GLOW[b->idx]), 0.3f + tk * 0.5f, false);
  }
  D.die = 0;
}

/* ═════════════════════════ deaths ═════════════════════════ */
typedef struct remnant { enemy e; boss b; int region; float age, life; bool is_boss; } remnant;
static remnant RM[24];
static int nrm;
static enemy prev_en[MAXEN];
static int prev_nen;
static boss prev_b;

void creatures_events(const sim *s, int region) {
  for (int i = 0; i < s->nev; i++) {
    const event *ev = &s->ev[i];
    if (ev->kind == EV_KILL) {
      for (int k = 0; k < s->nen && k < prev_nen; k++)
        if (prev_en[k].alive && !s->en[k].alive && prev_en[k].kind == ev->a && nrm < 24) {
          RM[nrm++] = (remnant){ .e = prev_en[k], .region = region, .life = 0.7f };
          prev_en[k].alive = 0;
          break;
        }
    } else if (ev->kind == EV_BOSS_DOWN && prev_b.active && nrm < 24) {
      RM[nrm++] = (remnant){ .b = prev_b, .region = region, .life = 1.6f, .is_boss = true };
    }
  }
  memcpy(prev_en, s->en, sizeof(enemy) * (size_t)s->nen);
  prev_nen = s->nen;
  prev_b = s->b;
}

void creatures_tick(float dt) {
  for (int i = 0; i < nrm;) {
    remnant *r = &RM[i];
    r->age += dt;
    if (r->age >= r->life) { RM[i] = RM[--nrm]; continue; }
    /* embers rise off what is left */
    float k = r->age / r->life, x = r->is_boss ? r->b.x : r->e.x, y = r->is_boss ? r->b.y - boss_hh(r->b.idx) : r->e.y - enemy_hh(r->e.kind);
    float sz = r->is_boss ? 2.5f : 0.7f;
    for (int n = 0; n < (r->is_boss ? 4 : 1); n++) {
      float a = pn_hash((int)(r->age * 977) + n, i, 3) * 6.28f, d = pn_hash((int)(r->age * 977) + n, i, 4) * sz;
      view_emit(x + cosf(a) * d, y + sinf(a) * d * 0.7f, (pn_hash(n, i, 5) - 0.5f) * 1.5f, -1.5f - 2 * k, 0.8f, 0.22f, (cw_color){ 1, 0.6f + 0.3f * k, 0.3f, 0.9f }, true, -1);
    }
    i++;
  }
}

void creatures_draw_remnants(float t) {
  for (int i = 0; i < nrm; i++) {
    remnant *r = &RM[i];
    float k = r->age / r->life;
    if (r->is_boss) {
      D.die = k * 0.85f; /* (the warden comes apart more slowly: it burns as it falls) */
      creature_boss(&r->b, r->region, t);
      continue;
    }
    enemy e = r->e;
    e.flash = 0;
    float f = e.facing_left ? -1.0f : 1.0f;
    begin(e.x, e.y, f, 0, r->region, (uint32_t)i * 77u + 5u);
    D.die = k;
    switch (e.kind) {
    case EN_CRAWLER: draw_crawler(&e, r->region, t, 0); break;
    case EN_SPITTER: draw_spitter(&e, t, 0); break;
    case EN_FLYER: draw_moth(&e, t, 0); break;
    case EN_HOPPER: draw_hopper(&e, r->region, t, 0); break;
    default: draw_knight(&e, t, 0); break;
    }
    flush();
  }
}

void creatures_fx(const sim *s, float t) {
  (void)s, (void)t;
  for (int i = 0; i < nhl; i++) {
    const halo *h = &HL[i];
    cw_r2d_sprite(&(cw_sprite){ .texture = A.soft, .pos = { h->x, h->y }, .size = { h->r * 2, h->r * 2 }, .pivot = { 0.5f, 0.5f }, .color = h->c });
  }
  nhl = 0;
}

#include <stdio.h>
#include <time.h>
void creatures_init(void) {
  struct timespec a, b;
  clock_gettime(CLOCK_MONOTONIC, &a);
  paint_enemies();
  paint_wardens();
  clock_gettime(CLOCK_MONOTONIC, &b);
  if (getenv("LANTERNFALL_BENCH") || getenv("LANTERNFALL_SHOWCASE")) fprintf(stderr, "lanternfall: creatures painted in %.0f ms\n", (double)(b.tv_sec - a.tv_sec) * 1e3 + (double)(b.tv_nsec - a.tv_nsec) * 1e-6);
}
