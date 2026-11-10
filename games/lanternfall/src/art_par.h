/* lanternfall: the parallax families (included once by art.c, which has the noise helpers). Each is 1024 x 512, tiles
 * across, white shaded by form (lit tops and faces lighter, undersides darker) on clear, so a tint and the haze of
 * its depth give it colour. Features are placed by hash along x: no two trees, spires or gears alike. */

static float wrapd(float a, float b) { /* distance across the wrap, 0..0.5 */
  float d = fabsf(a - b);
  return d > 0.5f ? 1 - d : d;
}
static float wrapdx(float a, float b) { /* signed, across the wrap */
  float d = a - b;
  return d > 0.5f ? d - 1 : d < -0.5f ? d + 1 : d;
}

/* Everything that stands in a family stands on ground painted in the same texture: a band from the family's ground
 * line (PAR_GROUND, a share of the height) to the bottom row, which the sky draws on down to the canvas's bottom edge
 * (view.c draw_sky). The ridges are ground themselves; the hanging growth stands on nothing. */
#define PAR_GROUND_TREES 0.925f
#define PAR_GROUND_CRYSTAL 0.925f
#define PAR_GROUND_ARCH 0.88f
#define PAR_GROUND_MACHINE 0.9f
float art_par_ground(int k) {
  return k == PAR_TREES ? PAR_GROUND_TREES : k == PAR_CRYSTAL ? PAR_GROUND_CRYSTAL : k == PAR_ARCH ? PAR_GROUND_ARCH : k == PAR_MACHINE ? PAR_GROUND_MACHINE : -1;
}
/* the control of tests/lanternfall-grounding.mjs: one tree's trunk stops this many rows above its ground (0: none) */
static int par_lift_rows;
#define PAR_LIFT_TREE 3
/* earth under the trees and the spires: a lit rim along the ground line, then soil darkening toward the bottom row,
 * with a little grain (luma only: the band's edge is one straight line, so the gate can measure against it) */
static float par_ground_luma(float fx, float fy, float g, int seed) {
  float d = (fy - g) / (1 - g);
  float rim = sstep(0.08f, 0.0f, d) * 0.1f;
  float grain = 0.05f * (vnoise(fx * 700, fy * 350, seed, 700) - 0.5f);
  return 0.74f - 0.2f * d + rim + grain;
}

static void par_pixel(int k, float fx, float fy, float *a, float *l) {
  *a = 0, *l = 0.85f + 0.15f * (1 - fy);
  switch (k) {
  case PAR_RIDGE: { /* ragged far ridges, a few spires and mesas, a lit rim along the crest */
    float r = 0.42f + 0.22f * fbm(fx * 5, 0.5f, 101, 5, 5) + 0.05f * fbm(fx * 23, 1.5f, 102, 3, 23);
    for (int s = 0; s < 6; s++) {
      float cx = ((float)s + hash2(s, 1, 103)) / 6, w = 0.006f + 0.01f * hash2(s, 2, 104), h = 0.1f + 0.15f * hash2(s, 3, 105);
      float d = wrapd(fx, cx);
      if (d < w * 3) r -= h * sstep(w * 3, 0, d);
    }
    *a = sstep(r - 0.004f, r + 0.004f, fy);
    *l = 0.78f + 0.22f * sstep(r + 0.06f, r, fy);
    break;
  }
  case PAR_ARCH: { /* a colonnade: piers, round arches, broken tops and fallen bays, a plinth */
    const int nb = 7;
    float u = fx * (float)nb, bay = floorf(u), f = u - bay;
    int b = (int)bay % nb;
    float top = 0.2f + 0.2f * hash2(b, 4, 106) + 0.03f * fbm(fx * 40, 0.3f, 107, 2, 40);
    float pier = fminf(f, 1 - f);
    float spring = 0.52f + 0.06f * hash2(b, 5, 108), rad = 0.36f, ox = (f - 0.5f) / rad;
    bool solid = fy > top;
    bool open = pier > 0.13f && fy > spring - rad * 0.9f * sqrtf(fmaxf(0, 1 - ox * ox)) && fy < 0.88f;
    if (hash2(b, 6, 109) < 0.3f && pier > 0.13f && fy < 0.6f) solid = false;
    *a = solid && !open ? 1 : 0;
    *l = pier < 0.13f ? 0.78f + 0.22f * sstep(0.13f, 0.02f, pier) : 0.68f;
    { /* masonry: courses of blocks, joints a shade darker, each block its own value, chips off the edges */
      float course = fy * 40, row = floorf(course), cu = fx * 220 + (((int)row) & 1) * 0.5f, blk = floorf(cu);
      float jy = fabsf(course - row - 0.5f), jx = fabsf(cu - blk - 0.5f);
      *l *= (0.92f + 0.1f * hash2((int)blk, (int)row, 144)) * (jy > 0.42f || jx > 0.46f ? 0.82f : 1);
      if (*a > 0 && fy > top && fy < top + 0.03f && hash2((int)(fx * 300), 0, 145) < 0.3f) *a = 0; /* (chipped tops) */
      float ivy = fbm(fx * 60, fy * 8, 146, 3, 60);
      if (*a > 0 && fy < top + 0.18f * ivy && ivy > 0.5f) *l = 0.6f + 0.2f * vnoise(fx * 800, fy * 400, 147, 800); /* (ivy over the tops) */
    }
    if (fy > 0.88f) *a = 1, *l = 0.72f + 0.1f * (fy < 0.9f);
    break;
  }
  case PAR_TREES: { /* trees of every size: a leaning trunk with bark and roots, branches forking up into a crown of
                     * leaf clusters (scalloped edges, each cluster lit from above with its own value, gaps between them) */
    float lv = 0.8f;
    float bark = 0.5f + 0.5f * sinf(fx * 2400 + fbm(fx * 300, fy * 12, 141, 2, 300) * 6); /* (vertical bark grain) */
    for (int t = 0; t < 9; t++) {
      float cx = ((float)t + 0.2f + 0.6f * hash2(t, 7, 110)) / 9, h = 0.35f + 0.45f * hash2(t, 8, 111), tw = 0.004f + 0.006f * hash2(t, 9, 112);
      float ctop = 1 - h, dx = wrapdx(fx, cx), lean = (hash2(t, 10, 113) - 0.5f) * 0.03f;
      float tx = dx - lean * (1 - fy) / h;
      float flare = 1.0f + 1.5f * (fy - ctop) / h + 3 * sstep(PAR_GROUND_TREES - 0.08f, PAR_GROUND_TREES, fy); /* (the roots spread where they meet the ground) */
      bool foot = fy > ctop + h * 0.3f && (t != PAR_LIFT_TREE || !par_lift_rows || fy < PAR_GROUND_TREES - (float)par_lift_rows / 512);
      if (foot && fabsf(tx) < tw * flare) *a = 1, lv = 0.6f + 0.1f * bark + 0.12f * sstep(tw * flare, -tw * flare, tx * 1.5f);
      for (int c = 0; c < 6; c++) {
        float ccx = (hash2(t, 20 + c, 114) - 0.5f) * 0.18f * h + lean, ccy = ctop + h * (0.08f + 0.28f * hash2(t, 30 + c, 115));
        float r = h * (0.1f + 0.13f * hash2(t, 40 + c, 116));
        /* a branch from the trunk up to the cluster */
        float bx0 = lean * 0.6f, by0 = ctop + h * 0.45f, ux = ccx - bx0, uy = ccy - by0, ul = sqrtf(ux * ux * 4 + uy * uy) + 1e-5f;
        float pu = fmaxf(0, fminf(1, ((dx - bx0) * 2 * ux * 2 + (fy - by0) * uy) / (ul * ul)));
        float qx = (dx - bx0 - ux * pu) * 2, qy = fy - by0 - uy * pu, bw = tw * (1.1f - 0.7f * pu);
        if (qx * qx + qy * qy < bw * bw) *a = 1, lv = 0.62f + 0.08f * bark;
        float ddx = (dx - ccx) * 2.0f, ddy = fy - ccy, d = sqrtf(ddx * ddx + ddy * ddy);
        /* leaf clusters: the edge scalloped by small cells, a ragged fringe of leaves */
        float f1, f2, id;
        pcells(fx * 1024 * 0.5f, fy * 512 * 0.5f, 142 + t, 48, &f1, &f2, &id);
        float leafy = vnoise(fx * 900, fy * 450, 143, 900);
        float edge = r * (0.8f + 0.14f * vnoise(fx * 300, fy * 150, 117, 300) + 0.12f * leafy);
        if (d < edge) {
          *a = 1;
          float lit = sstep(edge, -edge, ddy + ddx * 0.3f), clump = 0.94f + 0.08f * id - 0.12f * sstep(0.35f, 0.0f, f2 - f1) * (d / edge);
          lv = (0.66f + 0.34f * lit) * clump + 0.05f * (leafy - 0.5f);
        }
      }
    }
    if (fy > PAR_GROUND_TREES) *a = 1, lv = par_ground_luma(fx, fy, PAR_GROUND_TREES, 148); /* the earth the trunks stand in */
    *l = lv;
    break;
  }
  case PAR_CRYSTAL: { /* spires of crystal, leaning, each with a lit face and a shaded one, out of a floor of rock */
    for (int s = 0; s < 14; s++) {
      float cx = ((float)s + hash2(s, 11, 118)) / 14, h = 0.25f + 0.55f * hash2(s, 12, 119), w = 0.01f + 0.022f * hash2(s, 13, 120);
      float lean = (hash2(s, 14, 121) - 0.5f) * 0.6f, top = 1 - h;
      if (fy < top) continue;
      float t = (fy - top) / h, dx = wrapdx(fx, cx) - lean * w * (1 - t) * 3, half = w * fminf(1, t * 4);
      if (fabsf(dx) < half) *a = 1, *l = dx < 0 ? 0.97f : 0.66f;
    }
    if (fy > PAR_GROUND_CRYSTAL) *a = 1, *l = par_ground_luma(fx, fy, PAR_GROUND_CRYSTAL, 149) - 0.04f;
    break;
  }
  case PAR_MACHINE: { /* gears with spokes, pipes across, chimney stacks, a gantry */
    float lv = 0.75f;
    for (int g = 0; g < 6; g++) {
      float cx = ((float)g + hash2(g, 15, 122)) / 6, cy = 0.42f + 0.38f * hash2(g, 16, 123), r = 0.05f + 0.09f * hash2(g, 17, 124);
      float dx = wrapdx(fx, cx) * 2, dy = fy - cy, d = sqrtf(dx * dx + dy * dy), ang = atan2f(dy, dx);
      float teeth = (float)(10 + (int)(hash2(g, 18, 125) * 8));
      float rt = r * (sinf(ang * teeth) > 0 ? 1.12f : 1.0f);
      bool hole = d < r * 0.72f && d > r * 0.24f && fmodf(ang + 6.2832f, 1.2566f) > 0.42f;
      if (d < rt && !hole) *a = 1, lv = d < r * 0.24f ? 1.0f : 0.7f + 0.24f * sstep(r, -r, dy);
    }
    for (int p = 0; p < 4; p++) {
      float py = 0.3f + 0.5f * hash2(p, 19, 126), pw = 0.012f + 0.01f * hash2(p, 20, 127);
      if (fabsf(fy - py) < pw) *a = 1, lv = 0.68f + 0.3f * sstep(pw, -pw, fy - py);
      float sx = ((float)p + hash2(p, 21, 128)) / 4, sw = 0.01f + 0.012f * hash2(p, 22, 129), st = 0.1f + 0.3f * hash2(p, 23, 130);
      if (fy > st && wrapd(fx, sx) < sw * (fy < st + 0.03f ? 1.5f : 1)) *a = 1, lv = 0.8f;
    }
    if (fy > 0.82f && fy < 0.84f && fmodf(fx * 60, 1) > 0.15f) *a = 1, lv = 0.82f;
    if (fy > 0.9f) *a = 1, lv = 0.7f;
    *l = lv;
    break;
  }
  default: { /* growth hanging from above: roots and vines, and rock dripping into stalactites */
    float len = 0.15f + 0.4f * fbm(fx * 18, 3, 104, 3, 18) * fbm(fx * 4, 7, 105, 2, 4) * 2;
    float strand = 0.5f + 0.5f * sinf(fx * 220 + fbm(fx * 9, fy * 4, 106, 2, 9) * 4);
    *a = sstep(len + 0.02f, len - 0.02f, fy) * sstep(0.2f, 0.6f, strand + 0.4f * (1 - fy / (len + 0.01f)));
    for (int s = 0; s < 10; s++) {
      float cx = ((float)s + hash2(s, 24, 131)) / 10, h = 0.15f + 0.35f * hash2(s, 25, 132), w = 0.01f + 0.015f * hash2(s, 26, 133);
      float d = wrapd(fx, cx);
      if (fy < h && d < w * (1 - fy / h)) *a = 1, *l = d < w * 0.3f ? 0.95f : 0.72f;
    }
    *a = fmaxf(*a, sstep(0.1f, 0.06f, fy));
    break;
  }
  }
}

static void make_par_one(int k, uint8_t *px) {
  for (int y = 0; y < 512; y++)
    for (int x = 0; x < 1024; x++) {
      float a = 0, l = 0; /* four samples a pixel: smooth edges */
      for (int q = 0; q < 4; q++) {
        float sa, sl;
        par_pixel(k, ((float)x + 0.25f + 0.5f * (float)(q & 1)) / 1024, ((float)y + 0.25f + 0.5f * (float)(q >> 1)) / 512, &sa, &sl);
        a += sa * 0.25f, l += sl * sa;
      }
      uint8_t *o = px + 4 * (y * 1024 + x);
      o[0] = o[1] = o[2] = u8(a > 0 ? l / (a * 4) : 0.8f), o[3] = u8(a);
    }
  if (A.par[k].id) cw_texture_destroy(A.par[k]);
  A.par[k] = cw_texture_from_pixels(1024, 512, px, &(cw_texture_desc){ .repeat = true });
  const char *dump = getenv("LANTERNFALL_PAR_DUMP"); /* (the grounding gate, natively: each family's pixels, raw RGBA rows) */
  if (dump) {
    char path[512];
    snprintf(path, sizeof path, "%s/par%d.rgba", dump, k);
    FILE *f = fopen(path, "wb");
    if (f) fwrite(px, 1, 1024 * 512 * 4, f), fclose(f);
  }
}
static void make_parallax(uint8_t *px) {
  if (getenv("LANTERNFALL_PAR_LIFT")) par_lift_rows = atoi(getenv("LANTERNFALL_PAR_LIFT"));
  for (int k = 0; k < NPAR; k++) make_par_one(k, px);
}
/* the trees painted again with one trunk stopped `rows` texture rows above the ground (the gate's control; 0 restores) */
void art_par_lift(int rows) {
  par_lift_rows = rows;
  make_par_one(PAR_TREES, par_px);
}
int art_par_lift_rows(void) { return par_lift_rows; }

/* what stands in a family: each feature's foot on the ground line, in texture shares (x across the wrap, hw its half
 * width there), as the painter places them. The ridges and the hanging growth have none. */
int art_par_features(int k, par_feature *out, int max) {
  int n = 0;
  switch (k) {
  case PAR_TREES:
    for (int t = 0; t < 9 && n < max; t++) {
      float cx = ((float)t + 0.2f + 0.6f * hash2(t, 7, 110)) / 9, h = 0.35f + 0.45f * hash2(t, 8, 111), tw = 0.004f + 0.006f * hash2(t, 9, 112);
      float ctop = 1 - h, lean = (hash2(t, 10, 113) - 0.5f) * 0.03f, g = PAR_GROUND_TREES;
      float flare = 1.0f + 1.5f * (g - ctop) / h + 3;
      float x = cx + lean * (1 - g) / h;
      out[n++] = (par_feature){ .x = x - floorf(x), .hw = tw * flare, .top = ctop, .id = t };
    }
    break;
  case PAR_CRYSTAL:
    for (int s = 0; s < 14 && n < max; s++) {
      float cx = ((float)s + hash2(s, 11, 118)) / 14, h = 0.25f + 0.55f * hash2(s, 12, 119), w = 0.01f + 0.022f * hash2(s, 13, 120);
      float lean = (hash2(s, 14, 121) - 0.5f) * 0.6f, top = 1 - h, g = PAR_GROUND_CRYSTAL, t = (g - top) / h;
      float x = cx + lean * w * (1 - t) * 3;
      out[n++] = (par_feature){ .x = x - floorf(x), .hw = w * fminf(1, t * 4), .top = top, .id = s };
    }
    break;
  case PAR_ARCH:
    for (int b = 0; b < 7 && n < max; b++) out[n++] = (par_feature){ .x = (float)b / 7, .hw = 0.13f / 7, .top = 0.2f + 0.2f * hash2(b, 4, 106), .id = b };
    break;
  case PAR_MACHINE:
    for (int p = 0; p < 4 && n < max; p++) {
      float sx = ((float)p + hash2(p, 21, 128)) / 4, sw = 0.01f + 0.012f * hash2(p, 22, 129), st = 0.1f + 0.3f * hash2(p, 23, 130);
      out[n++] = (par_feature){ .x = sx, .hw = sw, .top = st, .id = p };
    }
    break;
  default: break;
  }
  return n;
}
