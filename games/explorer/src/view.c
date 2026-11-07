/* explorer: drawing the world (view.h). Canvas layers, back to front:
 *   sky          a bright hazy gradient, then three parallax depths of the region's own shapes (screen space, unlit)
 *   wall         the room's back wall (lit: the lamps and the hero's lantern shade its relief)
 *   tiles        rock, its bright walkable lip, ledges, thorns, props, lamps (lit, normal-mapped)
 *   actors       the hero (a skeleton of shapes, animated in code), enemies, wardens (lit)
 *   fx           glows, particles, shots, the lantern's halo (additive, unlit)
 *   water        pools and flooded halls over everything they cover
 *   frame        dark silhouettes nearer than the playfield at the room's sides and ceiling (unlit, sliding faster)
 *   front        the distortion layer: shockwaves
 * Mood comes from colour: every region is mid-key under a bright ambient, the lamps add warmth or chill. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "view.h"

cw_camera2d g_cam;
bool g_gi = true; /* 2D global illumination (v0.37 engines): on unless a test or a slow host turns it off */
static float zoom = 60; /* canvas units per tile */
static view_stats VS;
#include <time.h>
static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}
static float cur_tint[3];

/* ── colours ── */
static cw_color hexc(uint32_t c, float a) { return (cw_color){ (float)((c >> 16) & 255) / 255.0f, (float)((c >> 8) & 255) / 255.0f, (float)(c & 255) / 255.0f, a }; }
static cw_color mixc(cw_color a, cw_color b, float t) { return (cw_color){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t }; }
static cw_color scalec(cw_color a, float k) { return (cw_color){ a.r * k, a.g * k, a.b * k, a.a }; }
static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
/* the value structure is set by luma, the same in every region whatever its palette: a colour moved to luma L (0..1),
 * keeping its hue (scaled down, or mixed toward a white of its own tint) */
static float luma(cw_color c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
static cw_color toluma(cw_color c, float L) {
  float l = luma(c);
  if (l >= L) return scalec(c, L / (l > 1e-4f ? l : 1e-4f));
  cw_color w = mixc(rgba(1, 1, 1, c.a), c, 0.25f);
  float lw = luma(w), k = (L - l) / (lw - l > 1e-4f ? lw - l : 1e-4f);
  return mixc(c, w, k > 1 ? 1 : k);
}
/* the value steps (luma): the hazy depth, the parallax depths far to near, the back wall, the rock's body */
enum { V_HAZE = 218, V_TOP = 178, V_FAR = 196, V_MID = 170, V_NEAR = 134, V_WALL = 126, V_ROCK = 76 };
#define VAL(v) ((float)(v) / 255.0f)

/* ── particles ── */
#define MAXPART 2400
typedef struct part { float x, y, vx, vy, life, max, size, rot, spin, grav; cw_color c; uint8_t kind, add; } part;
static part P[MAXPART];
static int np;
static uint32_t prng = 12345;
static float frand(void) {
  prng ^= prng << 13, prng ^= prng >> 17, prng ^= prng << 5;
  return (float)(prng & 0xffffff) / 16777215.0f;
}
static void emit(float x, float y, float vx, float vy, float life, float size, cw_color c, int kind, bool add, float grav) {
  if (np >= MAXPART) return;
  P[np++] = (part){ x, y, vx, vy, life, life, size, frand() * 6.28f, (frand() - 0.5f) * 6, grav, c, (uint8_t)kind, add };
}
static void burst(float x, float y, int n, float speed, float life, float size, cw_color c, bool add, float grav) {
  for (int i = 0; i < n; i++) {
    float a = frand() * 6.2832f, s = speed * (0.3f + 0.7f * frand());
    emit(x, y, cosf(a) * s, sinf(a) * s, life * (0.6f + 0.4f * frand()), size * (0.6f + 0.6f * frand()), c, 0, add, grav);
  }
}

/* flashes the effects draw: screen pulses for a warden's tell, a pickup's light */
static float flash_t;
static cw_color flash_c;
static float last_hp_t[MAXP];

void view_init(void) {
  art_init();
  if (getenv("EXPLORER_NO_GI")) g_gi = false; /* (the perf breakdown) */
  np = 0;
}

/* the canvas a window pass draws on: cw_r2d_canvas_size answers for the current or last pass, so before the first one
 * (the first frame) it is 0 x 0; the window's logical size is the same thing then. 0 x 0 still: nothing is drawn (a
 * zero zoom made the view NaN, every culling test false, and the first frame drew every entity in the world). */
cw_vec2 view_canvas(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  if (cs.x >= 1 && cs.y >= 1) return cs;
  int w = 0, h = 0;
  cw_window_size(&w, &h);
  return (cw_vec2){ (float)w, (float)h };
}

float view_px_per_tile(void) { return zoom; }
cw_vec2 view_world_to_screen(cw_vec2 w) {
  cw_vec2 cs = view_canvas();
  return (cw_vec2){ (w.x - g_cam.position.x) * zoom + cs.x * 0.5f, (w.y - g_cam.position.y) * zoom + cs.y * 0.5f };
}

void view_camera(const sim *s, float dt, bool snap) {
  cw_vec2 cs = view_canvas();
  if (cs.y < 1) return;
  zoom = cs.y / (float)CELL_H;
  float vw = cs.x / zoom, vh = cs.y / zoom;
  /* the players' middle, a little ahead of where player 1 runs */
  float tx = 0, ty = 0;
  int n = 0;
  for (int i = 0; i < s->players; i++)
    if (s->p[i].active) tx += s->p[i].x, ty += s->p[i].y - 1.2f, n++;
  if (n) tx /= (float)n, ty /= (float)n;
  tx += s->p[0].vx * 0.18f;
  if (s->room >= 0) { /* the room's walls bound the view */
    const room *r = &s->w->rooms[s->room];
    float x0 = (float)(r->x * CELL_W) + vw * 0.5f, x1 = (float)((r->x + r->w) * CELL_W) - vw * 0.5f;
    float y0 = (float)(r->y * CELL_H) + vh * 0.5f, y1 = (float)((r->y + r->h) * CELL_H) - vh * 0.5f;
    tx = x1 < x0 ? (x0 + x1) * 0.5f : tx < x0 ? x0 : tx > x1 ? x1 : tx;
    ty = y1 < y0 ? (y0 + y1) * 0.5f : ty < y0 ? y0 : ty > y1 ? y1 : ty;
  }
  if (snap || g_cam.zoom == 0) g_cam.position = (cw_vec2){ tx, ty };
  else {
    float k = 1 - expf(-dt * 9);
    g_cam.position.x += (tx - g_cam.position.x) * k;
    g_cam.position.y += (ty - g_cam.position.y) * k;
  }
  g_cam.zoom = zoom;
}

/* ── shapes in world units ── */
static void quadc(cw_vec2 a, cw_vec2 b, cw_vec2 c, cw_vec2 d, cw_color col) {
  cw_vec2 p[4] = { a, b, c, d };
  cw_r2d_polygon(p, 4, col);
}
/* a tapered limb from a to b */
static void limb(cw_vec2 a, cw_vec2 b, float wa, float wb, cw_color c) {
  float dx = b.x - a.x, dy = b.y - a.y, l = sqrtf(dx * dx + dy * dy) + 1e-5f, nx = -dy / l, ny = dx / l;
  quadc((cw_vec2){ a.x + nx * wa, a.y + ny * wa }, (cw_vec2){ b.x + nx * wb, b.y + ny * wb }, (cw_vec2){ b.x - nx * wb, b.y - ny * wb },
        (cw_vec2){ a.x - nx * wa, a.y - ny * wa }, c);
  cw_r2d_circle(a, wa, 10, c);
  cw_r2d_circle(b, wb, 10, c);
}
static cw_vec2 v2(float x, float y) { return (cw_vec2){ x, y }; }
static cw_vec2 polar(cw_vec2 o, float a, float l) { return (cw_vec2){ o.x + sinf(a) * l, o.y + cosf(a) * l }; } /* a = 0 straight down */
static void glow(float x, float y, float r, cw_color c) {
  cw_r2d_sprite(&(cw_sprite){ .texture = A.soft, .pos = { x, y }, .size = { r * 2, r * 2 }, .pivot = { 0.5f, 0.5f }, .color = c });
}
/* the perf breakdown (development): EXPLORER_PERF_OFF=shadows,vol,post,water,dist,occl,lights turns parts off */
static bool perf_off(const char *what) {
  static const char *e;
  static int read;
  if (!read) e = getenv("EXPLORER_PERF_OFF"), read = 1;
  return e && strstr(e, what);
}
static void light(float x, float y, float r, cw_color c, float e, bool shadows) {
  if (perf_off("lights")) return;
  if (perf_off("shadows")) shadows = false;
#ifdef EXPLORER_R2D_V2
  /* soft penumbras from the distance field; a breath of glow in the air round each lamp */
  cw_r2d_light(&(cw_light2d){ .pos = { x, y }, .radius = r, .color = c, .energy = e * 1.25f, .falloff = 1.6f, .height = r * 0.35f, .shadows = shadows,
                              .softness = 0.45f, .volumetric = perf_off("vol") ? 0 : shadows ? 0.03f : 0.015f });
#else
  cw_r2d_light(&(cw_light2d){ .pos = { x, y }, .radius = r, .color = c, .energy = e, .falloff = 1.6f, .height = r * 0.35f, .shadows = shadows });
#endif
  VS.lights++;
}
/* things that glow: they ignore the lights (emission) and go past 1 (intensity) so the bloom picks them up */
static void glowing(float intensity) {
#ifdef EXPLORER_R2D_V2
  cw_r2d_set_emissive(intensity > 1 ? 1.0f : 0.0f);
  cw_r2d_set_intensity(intensity);
#else
  (void)intensity;
#endif
}

/* ── background ── */
/* each region's complementary accent: cool steel blue against the warm regions, lantern amber against the cool */
static cw_color comp_accent(int region) {
  cw_color k = hexc(REGIONS[region].sky, 1);
  return k.r > k.b + 0.05f ? rgba(0.42f, 0.66f, 1.0f, 1) : rgba(1.0f, 0.68f, 0.32f, 1);
}
static void draw_sky(const sim *s, float t, int region) {
  const region_info *ri = &REGIONS[region];
  /* a little more light behind the rock-heavy halls of the Forge, a little less in the snow (luma steps) */
  static const int8_t EXPO[NREGIONS] = { [R_FORGE] = 22, [R_CLOCK] = 10, [R_ROOTWAY] = 4, [R_FROST] = -14, [R_SKY] = -8 };
  int ex = EXPO[region];
  cw_vec2 cs = view_canvas();
  cw_r2d_use_layer(L_SKY);
  /* a bright hazy depth behind everything: the playfield and the rock read as darker shapes against it */
  cw_color haze = toluma(hexc(ri->sky, 1), VAL(V_HAZE + ex));
  cw_color comp = comp_accent(region);
  cw_color top = toluma(hexc(ri->sky, 1), VAL(V_TOP + ex)), bot = toluma(mixc(haze, comp, 0.35f), VAL(V_HAZE + ex));
  cw_vertex2d v[4] = { { { 0, 0 }, { 0, 0 }, top }, { { cs.x, 0 }, { 1, 0 }, top }, { { cs.x, cs.y }, { 1, 1 }, bot }, { { 0, cs.y }, { 0, 1 }, bot } };
  uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
  cw_r2d_triangles((cw_texture){ 0 }, v, 4, idx, 6);
  /* a soft light shaft or two */
  for (int k = 0; k < 3; k++) {
    float x = fmodf((float)k * 0.37f * cs.x - g_cam.position.x * zoom * 0.05f + cs.x * 2, cs.x * 1.2f) - cs.x * 0.1f;
    cw_vec2 q[4] = { { x, 0 }, { x + cs.x * 0.08f, 0 }, { x + cs.x * 0.22f, cs.y }, { x + cs.x * 0.1f, cs.y } };
    cw_color sc = mixc(rgba(1, 1, 0.95f, 1), comp, 0.75f);
    sc.a = 0.07f + 0.02f * sinf(t * 0.3f + (float)k);
    cw_r2d_polygon(q, 4, sc);
  }
  /* parallax: three depths of the region's own shapes (far ridges, then its family of arches, trees, crystals,
   * machinery or hanging growth), each a value step darker than the one behind, the haze of its depth baked in.
   * One unlit layer: no fog pass, no extra full-screen layers. */
  static const uint8_t SET[NREGIONS][3] = {
    [R_GATE] = { PAR_RIDGE, PAR_ARCH, PAR_TREES },      [R_ROOTWAY] = { PAR_RIDGE, PAR_TREES, PAR_HANG },
    [R_GLASS] = { PAR_RIDGE, PAR_CRYSTAL, PAR_HANG },   [R_FEN] = { PAR_RIDGE, PAR_TREES, PAR_HANG },
    [R_CLOCK] = { PAR_RIDGE, PAR_MACHINE, PAR_ARCH },   [R_TIDAL] = { PAR_RIDGE, PAR_ARCH, PAR_HANG },
    [R_STAR] = { PAR_RIDGE, PAR_CRYSTAL, PAR_ARCH },    [R_SKY] = { PAR_RIDGE, PAR_ARCH, PAR_CRYSTAL },
    [R_ARCHIVE] = { PAR_RIDGE, PAR_ARCH, PAR_HANG },    [R_FORGE] = { PAR_RIDGE, PAR_MACHINE, PAR_MACHINE },
    [R_FROST] = { PAR_RIDGE, PAR_CRYSTAL, PAR_HANG },   [R_BLOOM] = { PAR_RIDGE, PAR_TREES, PAR_HANG },
    [R_HEART] = { PAR_RIDGE, PAR_TREES, PAR_HANG },
  };
  static const float PAR[3] = { 0.14f, 0.32f, 0.55f };
  cw_color cols[3] = { toluma(mixc(hexc(ri->far, 1), comp, 0.85f), VAL(V_FAR + ex)), toluma(region == R_FORGE || region == R_CLOCK ? mixc(comp, rgba(0.3f, 0.36f, 0.48f, 1), 0.35f) : mixc(hexc(ri->mid, 1), comp, comp.b > comp.r ? 0.6f : 0.25f), VAL(V_MID + ex)), toluma(hexc(ri->near, 1), VAL(V_NEAR + ex)) };
  if (region == R_FORGE || region == R_CLOCK) cols[2] = mixc(cols[2], rgba(0.42f, 0.5f, 0.6f, 1), 0.45f); /* cool steel against the heat */
  for (int k = 0; k < 3; k++) {
    int tex = SET[region][k];
    float w = cs.y * (1.7f + 0.35f * (float)((region * 3 + k) % 4)) * (k == 2 && SET[region][1] == SET[region][2] ? 1.45f : 1), h = cs.y * (0.85f - 0.06f * (float)k);
    float u0 = g_cam.position.x * zoom * PAR[k] / w + (float)region * 0.37f + (float)k * 0.21f;
    float yoff = -(g_cam.position.y - (float)(s->room >= 0 ? s->w->rooms[s->room].y * CELL_H : 0)) * zoom * PAR[k] * 0.35f;
    float y0 = cs.y - h + yoff + cs.y * 0.1f * (float)(2 - k);
    if (tex == PAR_HANG) y0 = -cs.y * 0.05f + yoff; /* the hanging growth comes from above */
    cw_vec2 pos[4] = { { 0, y0 }, { cs.x, y0 }, { cs.x, y0 + h }, { 0, y0 + h } };
    float su = cs.x / w;
    cw_vec2 uv[4] = { { u0, 0.002f }, { u0 + su, 0.002f }, { u0 + su, 0.998f }, { u0, 0.998f } }; /* (inset: no bleed from the wrapped edge) */
    cw_r2d_quad(A.par[tex], pos, uv, cols[k]);
  }
}

/* ── tiles ── */
static bool open_tile(int t) { return !t_solid(t); }

/* distance (tiles, capped) from every open tile to the nearest rock: the back wall fades out away from the rock,
 * so the middle of a big room opens onto the deep parallax behind it */
static uint8_t (*rockdist)[TW];
static bool rock_dirty;
static void build_rockdist(const sim *s) {
  if (!rockdist) rockdist = malloc(sizeof(uint8_t) * TH * TW);
  for (int y = 0; y < TH; y++)
    for (int x = 0; x < TW; x++) rockdist[y][x] = t_solid(s->t[y][x]) ? 0 : 20;
  for (int y = 0; y < TH; y++) /* two chamfer passes */
    for (int x = 0; x < TW; x++) {
      int d = rockdist[y][x];
      if (x > 0 && rockdist[y][x - 1] + 1 < d) d = rockdist[y][x - 1] + 1;
      if (y > 0 && rockdist[y - 1][x] + 1 < d) d = rockdist[y - 1][x] + 1;
      rockdist[y][x] = (uint8_t)d;
    }
  for (int y = TH - 1; y >= 0; y--)
    for (int x = TW - 1; x >= 0; x--) {
      int d = rockdist[y][x];
      if (x < TW - 1 && rockdist[y][x + 1] + 1 < d) d = rockdist[y][x + 1] + 1;
      if (y < TH - 1 && rockdist[y + 1][x] + 1 < d) d = rockdist[y + 1][x] + 1;
      rockdist[y][x] = (uint8_t)d;
    }
}

static float wall_alpha(int x, int y) {
  x = x < 0 ? 0 : x >= TW ? TW - 1 : x, y = y < 0 ? 0 : y >= TH ? TH - 1 : y;
  float d = (float)rockdist[y][x];
  return 0.6f * (d <= 1 ? 1 : d >= 6 ? 0 : 1 - (d - 1) / 5.0f); /* (a band round the rock: the room opens onto the depth behind) */
}

static void draw_room_wall(const sim *s, int ri, float x0, float y0, float x1, float y1) {
  const room *r = &s->w->rooms[ri];
  const region_info *g = &REGIONS[r->region];
  int m = region_material(r->region);
  cw_color c = toluma(mixc(hexc(g->mid, 1), hexc(g->sky, 1), 0.35f), VAL(V_WALL));
  int rx0 = r->x * CELL_W + 2, ry0 = r->y * CELL_H + 2, rx1 = (r->x + r->w) * CELL_W - 2, ry1 = (r->y + r->h) * CELL_H - 2;
  int ax = rx0 > (int)x0 - 2 ? rx0 : ((int)x0 - 2) & ~1, ay = ry0 > (int)y0 - 2 ? ry0 : ((int)y0 - 2) & ~1;
  /* 2 x 2 tile quads of the wall texture, each corner's alpha from its distance to the rock: a smooth fade */
  static cw_vertex2d v[4 * 1200];
  static uint32_t idx[6 * 1200];
  int nq = 0;
  for (int y = ay; y < ry1 && y < y1 + 2; y += 2)
    for (int x = ax; x < rx1 && x < x1 + 2; x += 2) {
      float a[4] = { wall_alpha(x, y), wall_alpha(x + 2, y), wall_alpha(x + 2, y + 2), wall_alpha(x, y + 2) };
      if (a[0] + a[1] + a[2] + a[3] < 0.02f || nq >= 1200) continue;
      /* hidden behind rock (the quad and the ring round it all solid): not drawn, the lit pass never pays for it */
      bool covered = true;
      for (int yy = y - 1; yy <= y + 2 && covered; yy++)
        for (int xx = x - 1; xx <= x + 2 && covered; xx++) covered = w_in(xx, yy) && t_solid(s->t[yy][xx]);
      if (covered) continue;
      static const float CX[4] = { 0, 2, 2, 0 }, CY[4] = { 0, 0, 2, 2 };
      for (int k = 0; k < 4; k++) {
        cw_color cc = c;
        cc.a = a[k];
        float wx = (float)x + CX[k], wy = (float)y + CY[k];
        float bx = (float)(x & ~7), by = (float)(y & ~7); /* (one texture across eight tiles, each 2 x 2 quad inside one copy: no grid) */
        v[nq * 4 + k] = (cw_vertex2d){ { wx, wy }, { (wx - bx) / 8.0f, (wy - by) / 8.0f }, cc };
      }
      uint32_t b = (uint32_t)nq * 4;
      uint32_t q[6] = { b, b + 1, b + 2, b, b + 2, b + 3 };
      memcpy(idx + nq * 6, q, sizeof q);
      nq++;
    }
  if (nq) cw_r2d_triangles(A.wall[m], v, nq * 4, idx, nq * 6);
  VS.sprites += nq;
}

static void draw_tiles(const sim *s, float x0, float y0, float x1, float y1, float t) {
  int tx0 = (int)floorf(x0) - 1, ty0 = (int)floorf(y0) - 1, tx1 = (int)ceilf(x1) + 1, ty1 = (int)ceilf(y1) + 1;
  for (int y = ty0; y <= ty1; y++)
    for (int x = tx0; x <= tx1; x++) {
      if (!w_in(x, y)) continue;
      int tt = s->t[y][x];
      int cr = s->w->cell_room[y / CELL_H][x / CELL_W];
      int region = cr != 0xffff ? s->w->rooms[cr].region : s->room >= 0 ? s->w->rooms[s->room].region : 0;
      const region_info *g = &REGIONS[region];
      int m = region_material(region);
      if (t_solid(tt)) {
        int up = w_tile(s->w, x, y - 1), mask = 0;
        bool o_up = open_tile(s->t[y - 1 < 0 ? 0 : y - 1][x]), o_r = x + 1 < TW && open_tile(s->t[y][x + 1]);
        bool o_dn = y + 1 < TH && open_tile(s->t[y + 1][x]), o_l = x > 0 && open_tile(s->t[y][x - 1]);
        (void)up;
        mask = (o_up ? 1 : 0) | (o_r ? 2 : 0) | (o_dn ? 4 : 0) | (o_l ? 8 : 0);
        /* skip the rock deep inside walls that no open tile is near: it is a flat dark mass */
        bool deep = !mask;
        if (deep) { /* rock deep in a wall: a flat fill, darker (no lip, no bevel to show) */
          bool near = false;
          for (int dy = -2; dy <= 2 && !near; dy++)
            for (int dx = -2; dx <= 2 && !near; dx++) near = w_in(x + dx, y + dy) && open_tile(s->t[y + dy][x + dx]);
          if (!near) { /* the rock's grain carries on into the mass, a step darker */
            cw_r2d_sprite(&(cw_sprite){ .texture = A.tiles[m], .src = { 0, 0, TILE_PX, TILE_PX }, .pos = { (float)x, (float)y }, .size = { 1.02f, 1.02f },
                                        .color = scalec(toluma(hexc(g->ground, 1), VAL(V_ROCK)), 0.8f) });
            continue;
          }
        }
        cw_color body = toluma(hexc(g->ground, 1), VAL(V_ROCK));
        if (tt == T_CRACKED) body = mixc(body, rgba(1, 0.85f, 0.6f, 1), 0.18f);
        if (tt == T_SECRET) body = scalec(body, 0.93f);
        if (tt == T_BOSSDOOR || tt == T_SEAL) continue; /* drawn in the effects layer */
        cw_r2d_sprite(&(cw_sprite){ .texture = A.tiles[m], .src = { (float)((mask % 4) * TILE_PX), (float)((mask / 4) * TILE_PX), TILE_PX, TILE_PX },
                                    .pos = { (float)x, (float)y }, .size = { 1.0f, 1.0f }, .color = body });
        VS.sprites++;
        if (tt == T_CRACKED) { /* the cracks show */
          cw_vec2 a = v2((float)x + 0.15f, (float)y + 0.3f), b = v2((float)x + 0.5f, (float)y + 0.55f), c = v2((float)x + 0.85f, (float)y + 0.35f);
          limb(a, b, 0.03f, 0.02f, rgba(0.15f, 0.1f, 0.08f, 0.8f));
          limb(b, c, 0.02f, 0.015f, rgba(0.15f, 0.1f, 0.08f, 0.8f));
          limb(b, v2((float)x + 0.45f, (float)y + 0.95f), 0.025f, 0.01f, rgba(0.15f, 0.1f, 0.08f, 0.8f));
        }
        if (o_up && tt != T_CRACKED) { /* the walkable lip: the brightest value in the scene's ground */
          int v = (x * 7 + y * 3) & 3;
          /* over a rounded corner the lip starts in from the edge, following the curve */
          float l0 = o_l ? 0.28f : 0, l1 = o_r ? 0.28f : 0;
          cw_r2d_sprite(&(cw_sprite){ .texture = A.lip[m], .src = { (float)(v * TILE_PX) + l0 * TILE_PX, 0, (1 - l0 - l1) * TILE_PX, 40 },
                                      .pos = { (float)x + l0, (float)y - 12.0f / 64.0f }, .size = { 1.0f - l0 - l1, 40.0f / 64.0f }, .color = hexc(g->top, 1) });
          VS.sprites++;
        }
      } else if (tt == T_ONEWAY) {
        bool l = x > 0 && s->t[y][x - 1] == T_ONEWAY, r = x + 1 < TW && s->t[y][x + 1] == T_ONEWAY;
        int piece = !l ? 0 : !r ? 2 : 1;
        cw_r2d_sprite(&(cw_sprite){ .texture = A.ledge[m], .src = { (float)(piece * TILE_PX), 0, TILE_PX, 28 }, .pos = { (float)x, (float)y },
                                    .size = { 1.0f, 28.0f / 64.0f }, .color = mixc(hexc(g->ground, 1), hexc(g->top, 1), 0.55f) });
        VS.sprites++;
      } else if (tt == T_SPIKE && (region == R_FORGE || region == R_CLOCK)) { /* a molten channel, flowing, glowing */
        float fl = t * 0.35f;
        if ((x % 4) == 0) light((float)x + 0.5f, (float)y + 0.2f, 4.5f, rgba(1, 0.55f, 0.2f, 1), 0.55f, false);
        glowing(2.2f);
        cw_r2d_sprite(&(cw_sprite){ .texture = A.molten, .src = { fl * 64.0f + (float)(x % 4) * 16.0f, 0, TILE_PX, 40 }, .pos = { (float)x, (float)y + 0.25f },
                                    .size = { 1.0f, 0.75f }, .color = rgba(1, 0.62f, 0.22f, 1) });
        glowing(1);
        VS.sprites++;
      } else if (tt == T_SPIKE) {
        cw_r2d_sprite(&(cw_sprite){ .texture = A.thorns, .pos = { (float)x, (float)y + 1 - 40.0f / 64.0f }, .size = { 1.0f, 40.0f / 64.0f },
                                    .color = mixc(hexc(g->accent, 1), rgba(0.95f, 0.3f, 0.35f, 1), 0.65f) });
        VS.sprites++;
      }
    }
  (void)t;
}

/* doors that are shut by a warden, and the heart's seal: bright bars of light */
static void draw_barriers(const sim *s, float x0, float y0, float x1, float y1, float t) {
  for (int y = (int)y0 - 1; y <= (int)y1 + 1; y++)
    for (int x = (int)x0 - 1; x <= (int)x1 + 1; x++) {
      if (!w_in(x, y)) continue;
      int tt = s->t[y][x];
      if (tt != T_BOSSDOOR && tt != T_SEAL) continue;
      cw_color c = tt == T_SEAL ? rgba(1, 0.8f, 0.35f, 1) : rgba(0.9f, 0.35f, 1, 1);
      float pulse = 0.75f + 0.25f * sinf(t * 4 + (float)(x + y) * 0.7f);
      cw_r2d_rect((cw_rect){ (float)x + 0.15f, (float)y, 0.7f, 1 }, scalec(c, 0.55f * pulse));
      glow((float)x + 0.5f, (float)y + 0.5f, 1.1f, scalec(c, 0.35f * pulse));
    }
}

/* ── props and lamps ── */
static void draw_prop(const ent *e, const region_info *g, float t) {
  float x = e->x, y = e->y;
  cw_color leaf = mixc(hexc(g->top, 1), hexc(g->accent, 1), 0.3f), dark = hexc(g->ground, 1), acc = hexc(g->accent, 1);
  float sway = sinf(t * 1.3f + x * 0.7f) * 0.06f;
  switch (e->sub) {
  case 0: /* a tuft of tall grass */
    for (int k = 0; k < 4; k++) {
      float bx = x - 0.3f + (float)k * 0.2f, h = 0.5f + 0.15f * (float)((k * 5 + (int)x) % 3);
      limb(v2(bx, y), v2(bx + sway * 4 + (float)(k - 2) * 0.05f, y - h), 0.05f, 0.01f, scalec(leaf, 0.85f + 0.05f * (float)k));
    }
    break;
  case 1: /* a mushroom cluster with glowing caps */
    for (int k = 0; k < 3; k++) {
      float bx = x - 0.25f + (float)k * 0.25f, h = 0.3f + 0.12f * (float)k;
      limb(v2(bx, y), v2(bx, y - h), 0.04f, 0.035f, rgba(0.92f, 0.88f, 0.8f, 1));
      cw_r2d_arc(v2(bx, y - h), 0, 0.14f + 0.03f * (float)k, -1.5708f, 1.5708f, 12, acc);
    }
    break;
  case 2: /* a rock */
    cw_r2d_circle(v2(x, y - 0.05f), 0.32f, 10, scalec(dark, 1.25f));
    cw_r2d_circle(v2(x - 0.08f, y - 0.14f), 0.16f, 8, scalec(dark, 1.5f));
    break;
  case 3: /* a fern */
    for (int k = -2; k <= 2; k++) limb(v2(x, y), v2(x + (float)k * 0.22f + sway * 3, y - 0.55f + fabsf((float)k) * 0.12f), 0.05f, 0.015f, leaf);
    break;
  case 4: /* crystals */
    for (int k = 0; k < 3; k++) {
      float bx = x - 0.2f + (float)k * 0.2f, h = 0.35f + 0.2f * (float)(k == 1);
      cw_vec2 p[3] = { { bx - 0.09f, y }, { bx + 0.09f, y }, { bx + (float)(k - 1) * 0.05f, y - h } };
      cw_r2d_polygon(p, 3, mixc(acc, rgba(1, 1, 1, 1), 0.3f));
    }
    break;
  case 5: /* flowers */
    for (int k = 0; k < 3; k++) {
      float bx = x - 0.25f + (float)k * 0.25f, h = 0.35f + 0.1f * (float)k;
      limb(v2(bx, y), v2(bx + sway * 3, y - h), 0.025f, 0.02f, scalec(leaf, 0.8f));
      cw_r2d_circle(v2(bx + sway * 3, y - h), 0.09f, 8, k == 1 ? acc : mixc(acc, rgba(1, 1, 1, 1), 0.5f));
    }
    break;
  case PR_PIPE: { /* a steel pipe down from the ceiling: flanges, a valve, a spout breathing steam */
    float len = (float)e->arg * 0.6f + 0.4f;
    cw_color steel = rgba(0.42f, 0.5f, 0.6f, 1);
    cw_r2d_rect((cw_rect){ x - 0.16f, y, 0.32f, len }, steel);
    cw_r2d_rect((cw_rect){ x - 0.06f, y, 0.07f, len }, rgba(0.68f, 0.76f, 0.85f, 1)); /* its lit edge */
    for (float k = 0.5f; k < len; k += 1.1f) cw_r2d_rect((cw_rect){ x - 0.24f, y + k, 0.48f, 0.12f }, scalec(steel, 0.75f));
    cw_r2d_circle_lines(v2(x, y + len * 0.6f), 0.22f, 0.06f, 10, rgba(0.8f, 0.3f, 0.2f, 1));
    limb(v2(x, y + len), v2(x + 0.35f, y + len + 0.3f), 0.15f, 0.13f, steel);
    break;
  }
  case PR_MACHINE: { /* a machine: a riveted housing, a turning gear, a piston, a furnace mouth or a pilot light */
    uint8_t v = e->arg;
    float wd = 2.0f + (float)(v & 3) * 0.3f, ht = 1.1f + (float)((v >> 2) & 3) * 0.25f, spin = t * (0.6f + (float)(v & 1)) * ((v & 8) ? 1 : -1);
    cw_color steel = rgba(0.4f, 0.47f, 0.56f, 1), brass = mixc(acc, rgba(0.8f, 0.62f, 0.3f, 1), 0.6f);
    cw_vec2 gc = v2(x + ((v & 16) ? 0.6f : -0.6f), y - ht - 0.7f);
    for (int k = 0; k < 10; k++) { /* the gear's teeth, then its body and hub */
      float a = spin + (float)k * 0.6283f;
      limb(gc, v2(gc.x + cosf(a) * 1.0f, gc.y + sinf(a) * 1.0f), 0.13f, 0.13f, scalec(brass, 0.8f));
    }
    cw_r2d_circle(gc, 0.85f, 20, brass);
    cw_r2d_circle(gc, 0.55f, 16, scalec(brass, 0.7f));
    cw_r2d_circle(gc, 0.2f, 10, rgba(0.9f, 0.85f, 0.7f, 1));
    float pump = 0.25f * sinf(t * 2.4f + x);
    cw_r2d_rect((cw_rect){ x - wd * 0.15f, y - ht - 1.5f + pump, 0.22f, 1.5f }, rgba(0.75f, 0.8f, 0.86f, 1)); /* the piston */
    cw_r2d_round_rect((cw_rect){ x - wd * 0.5f, y - ht, wd, ht }, 0.12f, 4, steel);
    cw_r2d_rect((cw_rect){ x - wd * 0.5f, y - ht, wd, 0.12f }, rgba(0.62f, 0.7f, 0.8f, 1));
    for (int k = 0; k < 4; k++) cw_r2d_circle(v2(x - wd * 0.4f + (float)k * wd * 0.27f, y - ht + 0.3f), 0.05f, 6, rgba(0.75f, 0.8f, 0.85f, 1));
    if (g->accent == REGIONS[R_FORGE].accent) { /* a furnace mouth, glowing */
      glowing(2.4f);
      cw_r2d_round_rect((cw_rect){ x - 0.4f, y - ht * 0.6f, 0.8f, ht * 0.45f }, 0.15f, 4, rgba(1, 0.6f, 0.2f, 1));
      glowing(1);
    }
    glowing(2.6f); /* a pilot light: the cool accent */
    cw_r2d_circle(v2(x + wd * 0.35f, y - ht + 0.32f), 0.09f, 8, rgba(0.45f, 0.75f, 1, 1));
    glowing(1);
    break;
  }
  case PR_TREE: { /* a tree of the gardens: a leaning trunk, branches, a crown of leaf masses and blossom */
    uint8_t v = e->arg;
    float h = 3.0f + (float)(v & 3) * 0.45f, lean = ((float)((v >> 2) & 3) - 1.5f) * 0.18f, sw = sinf(t * 0.9f + x) * 0.05f;
    cw_color bark = mixc(dark, rgba(0.35f, 0.24f, 0.18f, 1), 0.5f), lf = mixc(leaf, rgba(0.35f, 0.62f, 0.3f, 1), 0.35f);
    cw_vec2 top = v2(x + lean * h + sw, y - h);
    limb(v2(x, y), top, 0.3f, 0.14f, bark);
    limb(v2(x + lean * h * 0.5f, y - h * 0.55f), v2(x + lean * h * 0.5f + 1.1f, y - h * 0.8f), 0.1f, 0.05f, bark);
    limb(v2(x + lean * h * 0.6f, y - h * 0.65f), v2(x + lean * h * 0.6f - 1.0f, y - h * 0.92f), 0.09f, 0.04f, bark);
    for (int k = 0; k < 7; k++) {
      float a = (float)k * 0.9f + (float)v, r = 0.6f + 0.25f * (float)((v + k) % 3);
      cw_vec2 c = v2(top.x + cosf(a) * 1.0f, top.y + sinf(a) * 0.55f + 0.1f);
      cw_r2d_circle(c, r, 14, scalec(lf, 0.82f + 0.06f * (float)(k % 3)));
      cw_r2d_circle(v2(c.x - 0.15f, c.y - 0.2f), r * 0.6f, 12, scalec(lf, 1.08f)); /* the lit side */
    }
    for (int k = 0; k < 6; k++) cw_r2d_circle(v2(top.x + cosf((float)k * 2.1f + (float)v) * 1.3f, top.y + sinf((float)k * 1.7f) * 0.7f), 0.1f, 6, acc);
    break;
  }
  case PR_CLUSTER: { /* a cluster of crystals taller than the hero, faceted, glowing a little from within */
    uint8_t v = e->arg;
    cw_color cc = mixc(acc, rgba(1, 1, 1, 1), 0.25f);
    for (int k = 0; k < 5; k++) {
      float bx = x - 1.0f + (float)k * 0.5f, hgt = 1.0f + (float)((v + k * 3) % 5) * 0.4f, lean = ((float)((v >> k) & 3) - 1.5f) * 0.25f, w = 0.22f + 0.05f * (float)(k & 1);
      cw_vec2 l[3] = { v2(bx - w, y), v2(bx, y), v2(bx + lean, y - hgt) }, r[3] = { v2(bx, y), v2(bx + w, y), v2(bx + lean, y - hgt) };
      glowing(1.25f);
      cw_r2d_polygon(l, 3, cc);
      cw_r2d_polygon(r, 3, scalec(cc, 0.6f));
      glowing(1);
    }
    break;
  }
  case PR_KELP: { /* tall weed swaying in the current, bulbs along it */
    for (int k = 0; k < 3; k++) {
      float bx = x - 0.5f + (float)k * 0.5f, hgt = 2.2f + (float)((e->arg + k) % 3) * 0.6f;
      cw_vec2 prev = v2(bx, y);
      for (int j = 1; j <= 6; j++) {
        float f = (float)j / 6;
        cw_vec2 p = v2(bx + sinf(t * 1.1f + bx + f * 2.5f) * 0.35f * f, y - hgt * f);
        limb(prev, p, 0.09f * (1.1f - f * 0.6f), 0.08f * (1.1f - f * 0.6f), scalec(leaf, 0.7f + 0.1f * (float)k));
        if (j == 3 || j == 5) cw_r2d_circle(p, 0.13f, 8, acc);
        prev = p;
      }
    }
    break;
  }
  case PR_SHELF: { /* a shelf of old books */
    float wd = 1.8f, ht = 2.6f;
    cw_color wood = rgba(0.36f, 0.25f, 0.2f, 1);
    cw_r2d_rect((cw_rect){ x - wd * 0.5f, y - ht, wd, ht }, wood);
    for (int r = 0; r < 3; r++) {
      float ry = y - ht + 0.15f + (float)r * 0.85f;
      for (int k = 0; k < 9; k++) {
        uint32_t hsh = (uint32_t)(e->arg * 31 + r * 7 + k * 13);
        static const uint32_t BOOK[5] = { 0x8a3a3a, 0x3a5a8a, 0x6a7a3a, 0xc0a060, 0x5a3a6a };
        float bh = 0.5f + (float)(hsh % 3) * 0.08f;
        cw_r2d_rect((cw_rect){ x - wd * 0.45f + (float)k * 0.19f, ry + 0.7f - bh, 0.16f, bh }, hexc(BOOK[hsh % 5], 1));
      }
      cw_r2d_rect((cw_rect){ x - wd * 0.5f, ry + 0.7f, wd, 0.1f }, scalec(wood, 1.3f));
    }
    break;
  }
  case PR_STATUE: { /* a hooded lamplighter of stone on a plinth, a lantern held out, still burning */
    cw_color stone = rgba(0.5f, 0.5f, 0.5f, 1);
    float f = (e->arg & 1) ? 1.0f : -1.0f;
    cw_r2d_rect((cw_rect){ x - 0.8f, y - 0.7f, 1.6f, 0.7f }, scalec(stone, 0.8f));
    cw_vec2 robe[5] = { v2(x - 0.55f, y - 0.7f), v2(x + 0.55f, y - 0.7f), v2(x + 0.3f, y - 2.6f), v2(x, y - 3.1f), v2(x - 0.35f, y - 2.5f) };
    cw_r2d_polygon(robe, 5, stone);
    cw_r2d_circle(v2(x, y - 3.0f), 0.32f, 12, stone);
    limb(v2(x + f * 0.2f, y - 2.4f), v2(x + f * 0.9f, y - 2.6f), 0.1f, 0.08f, stone);
    glowing(2.8f);
    cw_r2d_circle(v2(x + f * 0.95f, y - 2.35f), 0.14f, 10, rgba(1, 0.85f, 0.5f, 1));
    glowing(1);
    break;
  }
  default: { /* hanging vines / roots / chains from the ceiling: arg is the length */
    float len = (float)e->arg * 0.5f;
    cw_vec2 prev = v2(x, y);
    for (int k = 1; k <= 6; k++) {
      float f = (float)k / 6;
      cw_vec2 p = v2(x + sinf(t * 0.9f + x + f * 2) * 0.12f * f, y + len * f);
      limb(prev, p, 0.045f * (1.2f - f * 0.5f), 0.04f * (1.2f - f * 0.5f), e->sub == 7 ? scalec(dark, 1.4f) : scalec(leaf, 0.75f));
      if (e->sub == 6 && (k & 1)) cw_r2d_circle(p, 0.09f, 6, leaf);
      prev = p;
    }
    if (e->sub == 8) glow(prev.x, prev.y, 0.25f, acc), cw_r2d_circle(prev, 0.08f, 8, acc);
    break;
  }
  }
}

static void draw_lamp(const ent *e, const region_info *g, float t, bool lights_on) {
  cw_color acc = hexc(g->accent, 1);
  if (acc.r - acc.b < 0.2f) acc = mixc(acc, rgba(1, 0.72f, 0.4f, 1), 0.7f); /* warm lamps in cool regions */
  /* glowing growth on the floor of a warm region glows its cool complement (bioluminescent blue) */
  { cw_color k = hexc(g->sky, 1); if (e->hold == HOLD_FLOOR && (e->sub == 5 || e->sub == 6) && k.r > k.b + 0.05f) acc = rgba(0.42f, 0.7f, 1.0f, 1); }
  float x = e->x, y = e->y, flick = 0.9f + 0.1f * sinf(t * 7.3f + x) * sinf(t * 3.1f + y);
  cw_vec2 core;
  if (e->hold == HOLD_CEIL) { /* a lantern on a chain */
    float len = (float)e->arg * 0.45f, sw = sinf(t * 0.8f + x) * 0.05f;
    core = v2(x + sw * len, y + len + 0.35f);
    limb(v2(x, y), v2(core.x, core.y - 0.35f), 0.03f, 0.03f, rgba(0.25f, 0.22f, 0.2f, 1));
    cw_r2d_round_rect((cw_rect){ core.x - 0.22f, core.y - 0.32f, 0.44f, 0.6f }, 0.08f, 4, rgba(0.3f, 0.25f, 0.22f, 1));
    glowing(3.0f);
    cw_r2d_round_rect((cw_rect){ core.x - 0.15f, core.y - 0.24f, 0.3f, 0.44f }, 0.06f, 4, mixc(acc, rgba(1, 1, 1, 1), 0.45f));
    glowing(1);
  } else if (e->sub == 7) { /* a shrine candle by an item */
    core = v2(x, y - 1.1f);
    cw_r2d_rect((cw_rect){ x - 0.12f, y - 1.0f, 0.24f, 1.0f }, rgba(0.85f, 0.8f, 0.7f, 1));
  } else if (e->sub == 9) { /* a lantern post: an iron post, a lantern of warm glass */
    core = v2(x, y - 1.75f);
    cw_r2d_rect((cw_rect){ x - 0.07f, y - 1.6f, 0.14f, 1.6f }, rgba(0.2f, 0.18f, 0.18f, 1));
    cw_r2d_rect((cw_rect){ x - 0.22f, y - 0.12f, 0.44f, 0.12f }, rgba(0.2f, 0.18f, 0.18f, 1));
    cw_r2d_round_rect((cw_rect){ x - 0.24f, y - 2.1f, 0.48f, 0.62f }, 0.08f, 4, rgba(0.22f, 0.2f, 0.2f, 1));
    glowing(2.4f);
    cw_r2d_round_rect((cw_rect){ x - 0.16f, y - 2.02f, 0.32f, 0.46f }, 0.05f, 4, rgba(1, 0.62f, 0.24f, 1));
    glowing(1);
    if (lights_on) light(core.x, core.y, 6.0f, rgba(1, 0.66f, 0.32f, 1), 0.5f * flick, false);
    return;
  } else if (e->sub == 8) { /* a pilot light: a steel jet with a small blue flame */
    core = v2(x, y - 0.75f);
    cw_r2d_rect((cw_rect){ x - 0.08f, y - 0.5f, 0.16f, 0.5f }, rgba(0.45f, 0.52f, 0.6f, 1));
    glowing(1.6f);
    cw_vec2 fl[3] = { v2(x - 0.12f, y - 0.5f), v2(x + 0.12f, y - 0.5f), v2(x + sinf(t * 9 + x) * 0.04f, y - 0.95f - 0.08f * flick) };
    cw_r2d_polygon(fl, 3, rgba(0.5f, 0.8f, 1, 1));
    glowing(1);
    if (lights_on) light(core.x, core.y, 4.5f, rgba(0.45f, 0.7f, 1, 1), 0.55f * flick, false);
    return;
  } else { /* a glowing growth or brazier on the floor */
    core = v2(x, y - 0.55f);
    if (e->sub == 4) {
      cw_r2d_round_rect((cw_rect){ x - 0.3f, y - 0.5f, 0.6f, 0.5f }, 0.1f, 4, rgba(0.3f, 0.26f, 0.24f, 1));
      glowing(3.5f);
      cw_r2d_circle(core, 0.2f * flick, 10, mixc(acc, rgba(1, 1, 0.8f, 1), 0.5f));
      glowing(1);
    } else {
      for (int k = 0; k < 3; k++) {
        cw_vec2 p[3] = { { x - 0.2f + (float)k * 0.15f, y }, { x - 0.05f + (float)k * 0.15f, y }, { x - 0.1f + (float)k * 0.12f, y - 0.5f - 0.25f * (float)(k == 1) } };
        glowing(1.5f);
        cw_r2d_polygon(p, 3, mixc(acc, rgba(1, 1, 1, 1), 0.35f));
        glowing(1);
      }
    }
  }
  /* lamps lean warm against cool regions and cool against warm ones: colour contrast, not darkness */
  float warm = acc.r - acc.b;
  cw_color lc = warm > 0.2f ? mixc(acc, rgba(0.75f, 0.9f, 1, 1), 0.35f) : mixc(acc, rgba(1, 0.7f, 0.38f, 1), 0.8f);
  if (acc.b > acc.r + 0.3f) lc = acc; /* (the blue growth lights blue) */
  if (lights_on) light(core.x, core.y, e->hold == HOLD_CEIL ? 9.0f : 6.0f, lc, (e->hold == HOLD_CEIL ? 0.8f : 0.5f) * flick, e->hold == HOLD_CEIL);
}

/* ── framing: dark silhouettes in front of everything, nearer than the playfield (their layer's camera is zoomed in,
 * so they slide past faster), at the room's sides and hanging from its ceiling: columns, trunks, chains, shards ── */
#define FRAME_PAR 1.32f
/* a group drawn at its world place when the camera stands at e, and sliding FRAME_PAR times as fast as the world */
static void frame_at(float ex, float ey) {
  cw_xform2d m = cw_xform2d_identity();
  m.tx = -(FRAME_PAR - 1) * (g_cam.position.x - ex), m.ty = -(FRAME_PAR - 1) * (g_cam.position.y - ey);
  cw_r2d_set_transform(&m);
}
static uint32_t fhash(uint32_t a, uint32_t b) {
  uint32_t h = a * 2654435761u ^ (b + 0x9e3779b9u) * 2246822519u;
  h ^= h >> 15, h *= 2246822519u, h ^= h >> 13;
  return h;
}
static void draw_framing(const sim *s, float t) {
  if (s->room < 0) return;
  const room *r = &s->w->rooms[s->room];
  const region_info *g = &REGIONS[r->region];
  cw_color ink = mixc(scalec(hexc(g->ground, 1), 0.32f), scalec(hexc(g->sky, 1), 0.2f), 0.35f), ink2 = scalec(ink, 1.35f);
  float X0 = (float)(r->x * CELL_W), X1 = (float)((r->x + r->w) * CELL_W), Y0 = (float)(r->y * CELL_H), Y1 = (float)((r->y + r->h) * CELL_H);
  cw_vec2 cs = view_canvas();
  float vw = cs.x / zoom, vh = cs.y / zoom;
  float cxL = X0 + vw * 0.5f, cxR = X1 - vw * 0.5f, cyT = Y0 + vh * 0.5f;
  if (cxR < cxL) cxL = cxR = (X0 + X1) * 0.5f;
  int fam;
  switch (r->region) {
  case R_ROOTWAY: case R_FEN: case R_BLOOM: case R_HEART: fam = 1; break;
  case R_CLOCK: case R_FORGE: fam = 2; break;
  case R_GLASS: case R_FROST: case R_STAR: fam = 3; break;
  case R_TIDAL: fam = 4; break;
  default: fam = 0; break;
  }
  for (int side = 0; side < 2; side++) { /* the sides: something big standing at each wall */
    uint32_t h = fhash((uint32_t)s->room, (uint32_t)side);
    float x = side ? X1 - 1.2f - (float)(h % 3) * 0.6f : X0 + 1.2f + (float)(h % 3) * 0.6f, dir = side ? -1.0f : 1.0f;
    frame_at(side ? cxR : cxL, g_cam.position.y);
    switch (fam) {
    case 0: { /* a column with its capital and base */
      float w = 1.4f + (float)((h >> 4) % 3) * 0.3f;
      cw_r2d_rect((cw_rect){ x - w * 0.5f, Y0, w, Y1 - Y0 }, ink);
      cw_r2d_rect((cw_rect){ x - w * 0.75f, Y0 + 2.2f, w * 1.5f, 0.6f }, ink2);
      cw_r2d_rect((cw_rect){ x - w * 0.75f, Y1 - 2.8f, w * 1.5f, 0.7f }, ink2);
      cw_r2d_rect((cw_rect){ x - w * 0.12f + dir * w * 0.2f, Y0, w * 0.1f, Y1 - Y0 }, scalec(ink2, 1.15f)); /* a fluted edge */
      break;
    }
    case 1: { /* a trunk with roots, leaves hanging off it */
      float w = 1.2f + (float)((h >> 4) % 3) * 0.35f;
      limb(v2(x, Y0), v2(x + dir * 0.4f, Y1), w * 0.45f, w * 0.6f, ink);
      for (int k = 0; k < 3; k++) limb(v2(x + dir * 0.3f, Y1 - 2.2f), v2(x + dir * (1.2f + (float)k * 0.7f), Y1 - 1.6f + (float)k * 0.3f), 0.25f, 0.06f, ink);
      for (int k = 0; k < 6; k++) {
        float ly = Y0 + 2.5f + (float)k * 1.3f, sw = sinf(t * 1.2f + (float)k + x) * 0.1f;
        cw_r2d_circle(v2(x + dir * (0.7f + (float)(k % 3) * 0.3f) + sw, ly), 0.32f + 0.08f * (float)(k % 2), 8, ink2);
      }
      break;
    }
    case 2: { /* a girder and a chain hanging beside it, a hook at its end */
      float w = 1.0f;
      cw_r2d_rect((cw_rect){ x - w * 0.5f, Y0, w, Y1 - Y0 }, ink);
      for (float yy = Y0 + 0.5f; yy < Y1; yy += 1.6f) limb(v2(x - w * 0.5f, yy), v2(x + w * 0.5f, yy + 1.6f), 0.06f, 0.06f, ink2);
      float cx = x + dir * 1.8f, len = 4.0f + (float)((h >> 8) % 4), sw = sinf(t * 0.7f + x) * 0.12f;
      for (int k = 0; k < (int)(len * 2.5f); k++)
        cw_r2d_circle_lines(v2(cx + sw * (float)k / (len * 2.5f), Y0 + 1.6f + (float)k * 0.4f), 0.16f, 0.06f, 8, ink2);
      cw_r2d_arc(v2(cx + sw, Y0 + 1.6f + len + 0.3f), 0.2f, 0.32f, 0, 3.4f, 10, ink2);
      break;
    }
    case 3: { /* shards of crystal leaning in from the corner */
      for (int k = 0; k < 3; k++) {
        float bx = x + dir * (float)k * 0.8f, hgt = 4.0f + (float)((h >> (k * 3)) % 4) * 1.2f, lean = dir * (0.6f + 0.3f * (float)k);
        cw_vec2 p[3] = { v2(bx - 0.5f, Y1), v2(bx + 0.5f, Y1), v2(bx + lean, Y1 - hgt) };
        cw_r2d_polygon(p, 3, k == 1 ? ink2 : ink);
      }
      cw_vec2 q[3] = { v2(x - 0.6f, Y0), v2(x + dir * 1.4f, Y0), v2(x + dir * 0.5f, Y0 + 3.0f + (float)(h % 3)) };
      cw_r2d_polygon(q, 3, ink);
      break;
    }
    default: { /* weed rising and swaying, a rock at its foot */
      cw_r2d_circle(v2(x, Y1 - 0.4f), 1.4f, 16, ink);
      for (int k = 0; k < 3; k++) {
        cw_vec2 prev = v2(x + dir * (float)k * 0.5f, Y1 - 1.2f);
        for (int j = 1; j <= 6; j++) {
          float f = (float)j / 6;
          cw_vec2 p = v2(prev.x + sinf(t * 0.9f + f * 3 + (float)k) * 0.18f, Y1 - 1.2f - f * (5.0f + (float)k));
          limb(prev, p, 0.16f * (1.1f - f * 0.5f), 0.14f * (1.1f - f * 0.5f), k == 1 ? ink2 : ink);
          prev = p;
        }
      }
      break;
    }
    }
  }
  /* hanging from the ceiling, a few to a cell: roots, vines or chains, short, kept to the top of the frame */
  for (int c = 0; c < r->w * 3; c++) {
    uint32_t h = fhash((uint32_t)s->room * 7u + 3u, (uint32_t)c);
    float x = X0 + 4 + (float)c * ((X1 - X0 - 8) / (float)(r->w * 3)) + (float)(h % 5), len = 1.0f + (float)((h >> 3) % 4) * 0.6f;
    float sw = sinf(t * 0.8f + x) * 0.08f;
    frame_at(x < cxL ? cxL : x > cxR ? cxR : x, cyT);
    if (fam == 2) {
      for (int k = 0; k < (int)(len * 2.5f); k++) cw_r2d_circle_lines(v2(x + sw * (float)k * 0.2f, Y0 + 1.4f + (float)k * 0.4f), 0.14f, 0.05f, 8, ink);
    } else if (fam == 3) {
      cw_vec2 p[3] = { v2(x - 0.35f, Y0 + 1.5f), v2(x + 0.35f, Y0 + 1.5f), v2(x + 0.1f, Y0 + 1.5f + len * 1.4f) };
      cw_r2d_polygon(p, 3, ink);
    } else {
      cw_vec2 prev = v2(x, Y0 + 1.2f);
      for (int k = 1; k <= 4; k++) {
        float f = (float)k / 4;
        cw_vec2 p = v2(x + sw * f * 3 + sinf(f * 4 + x) * 0.2f, Y0 + 1.2f + len * f * 1.4f);
        limb(prev, p, 0.12f * (1.2f - f * 0.6f), 0.1f * (1.2f - f * 0.6f), ink);
        prev = p;
      }
      if (fam == 1) cw_r2d_circle(prev, 0.22f, 8, ink2);
    }
  }
  cw_r2d_set_transform(NULL);
}

/* ── the hero: a hooded lamplighter, drawn from a skeleton every frame ── */
static float run_phase[MAXP];
static void draw_hero(const sim *s, int i, float t) {
  const player *p = &s->p[i];
  if (!p->active) return;
  bool blink = p->invuln > 0 && ((p->invuln / 4) & 1);
  float f = p->facing_left ? -1.0f : 1.0f;
  float x = p->x, y = p->y;
  float sp = fabsf(p->vx) / RUN;
  if (p->on_ground) run_phase[i] += fabsf(p->vx) * DT * 1.35f;
  float ph = run_phase[i];
  bool air = !p->on_ground && p->state != PS_SWIM;
  cw_color cloak = i == 0 ? rgba(0.13f, 0.56f, 0.62f, 1) : rgba(0.72f, 0.36f, 0.62f, 1);
  cw_color cloak_d = scalec(cloak, 0.7f), skin = rgba(0.98f, 0.86f, 0.72f, 1), boot = rgba(0.32f, 0.22f, 0.18f, 1), trim = rgba(1.0f, 0.82f, 0.38f, 1);
  if (blink) cloak = mixc(cloak, rgba(1, 1, 1, 1), 0.6f), cloak_d = mixc(cloak_d, rgba(1, 1, 1, 1), 0.5f);
  float bob = p->on_ground ? fabsf(sinf(ph * 3.1416f)) * 0.08f * sp : 0;
  float lean = p->state == PS_DASH ? 0.35f : air ? -p->vy * 0.008f : sp * 0.18f;
  if (p->state == PS_CLING) lean = -0.1f;
  cw_vec2 hip = v2(x, y - 0.82f - bob);
  /* legs: run cycle, tucked in the air, splayed when clinging */
  for (int k = 0; k < 2; k++) {
    float side = k ? 1.0f : -1.0f, a1, a2;
    if (p->state == PS_CLING) a1 = f * (0.5f + side * 0.3f), a2 = -f * 0.4f;
    else if (p->state == PS_SWIM) a1 = sinf(t * 6 + side * 1.5f) * 0.4f, a2 = 0.3f;
    else if (air) a1 = f * (p->vy < 0 ? 0.6f : 0.2f) + side * 0.25f * f, a2 = -f * (p->vy < 0 ? 1.0f : 0.4f);
    else {
      float c = ph * 3.1416f + side * 1.5708f;
      a1 = f * sinf(c) * 0.75f * sp;
      a2 = -f * (0.15f + fmaxf(0, cosf(c)) * 0.9f * sp);
    }
    cw_vec2 knee = polar(hip, a1, 0.42f), foot = polar(knee, a1 + a2, 0.42f);
    cw_color lc = k ? cloak_d : scalec(cloak_d, 0.8f);
    limb(hip, knee, 0.11f, 0.095f, lc);
    limb(knee, foot, 0.095f, 0.08f, boot);
    cw_r2d_round_rect((cw_rect){ foot.x - 0.06f + f * 0.04f, foot.y - 0.06f, 0.2f, 0.12f }, 0.05f, 3, boot);
  }
  /* the cloak: a polygon swept back by speed and falling */
  cw_vec2 neck = v2(hip.x + lean * f * 0.6f, hip.y - 0.62f);
  float sweep = -f * (0.25f + sp * 0.5f + (p->state == PS_DASH ? 0.6f : 0)), lift = air ? -p->vy * 0.012f : 0;
  float wave = sinf(t * 9 + ph * 2) * 0.06f;
  cw_vec2 cl[6] = { neck, v2(neck.x + f * 0.18f, neck.y + 0.1f), v2(hip.x + f * 0.22f, hip.y + 0.25f), v2(hip.x + sweep * 0.6f, hip.y + 0.38f + lift + wave),
                    v2(hip.x + sweep, hip.y + 0.2f + lift * 1.5f - wave), v2(neck.x - f * 0.22f, neck.y + 0.15f) };
  cw_r2d_polygon(cl, 6, cloak);
  limb(hip, neck, 0.2f, 0.17f, cloak_d);
  cw_r2d_rect((cw_rect){ hip.x - 0.2f, hip.y - 0.12f, 0.4f, 0.07f }, trim);
  /* the arm and the lantern staff; during a strike it sweeps an arc */
  float arm = p->state == PS_CLING ? -2.6f : 0.6f + sinf(ph * 3.1416f) * 0.4f * sp;
  if (p->atk_t > 0) {
    float k = 1 - (float)p->atk_t / 14.0f;
    arm = p->atk_dir == 1 ? -2.6f + k * 2.2f : p->atk_dir == 2 ? -0.4f + k * 2.0f : -1.8f + k * 3.0f;
  }
  cw_vec2 sh = v2(neck.x + f * 0.05f, neck.y + 0.12f), hand = polar(sh, f * arm, 0.5f);
  limb(sh, hand, 0.075f, 0.065f, cloak_d);
  float sa = f * (arm + 2.9f);
  cw_vec2 tip = polar(hand, sa, 0.85f), butt = polar(hand, sa, -0.35f);
  limb(butt, tip, 0.04f, 0.035f, rgba(0.45f, 0.3f, 0.2f, 1));
  glowing(4.0f);
  cw_r2d_circle(tip, 0.13f, 10, rgba(1, 0.92f, 0.6f, 1));
  glowing(1);
  cw_r2d_circle(hand, 0.07f, 8, skin);
  /* the hood and the face in its shadow: two bright eyes */
  cw_vec2 head = v2(neck.x + f * 0.08f, neck.y - 0.24f);
  cw_r2d_circle(head, 0.27f, 16, cloak);
  cw_vec2 hood[4] = { v2(head.x - f * 0.25f, head.y - 0.1f), v2(head.x - f * 0.05f, head.y - 0.42f), v2(head.x - f * 0.5f, head.y - 0.2f + wave), v2(head.x - f * 0.3f, head.y + 0.05f) };
  cw_r2d_polygon(hood, 4, cloak);
  cw_r2d_circle(v2(head.x + f * 0.09f, head.y + 0.03f), 0.17f, 12, rgba(0.12f, 0.12f, 0.18f, 1));
  glowing(2.5f);
  for (int k = 0; k < 2; k++) cw_r2d_circle(v2(head.x + f * (0.06f + 0.09f * (float)k), head.y + 0.0f), 0.032f, 6, rgba(1, 0.95f, 0.7f, 1));
  glowing(1);
  /* the lantern lights the world around the hero (every lamp in a lit layer adds to the light buffer) */
  light(tip.x, tip.y, 8, rgba(1, 0.85f, 0.6f, 1), 0.55f, true);
}

/* ── enemies ── */
static void draw_enemy(const sim *s, const enemy *e, float t) {
  const region_info *g = &REGIONS[s->room >= 0 ? s->w->rooms[s->room].region : 0];
  float f = e->facing_left ? -1.0f : 1.0f, x = e->x, y = e->y;
  bool tell = e->state == ES_TELL;
  float tk = tell ? (float)e->t / 30.0f : 0;
  cw_color hot = rgba(1, 0.42f, 0.25f, 1), shell = mixc(rgba(0.55f, 0.2f, 0.45f, 1), hexc(g->accent, 1), 0.2f);
  if (e->flash > 0) shell = mixc(shell, rgba(1, 1, 1, 1), 0.7f);
  cw_color eye = tell ? mixc(rgba(1, 0.9f, 0.4f, 1), rgba(1, 1, 1, 1), 0.5f + 0.5f * sinf(t * 30)) : rgba(1, 0.85f, 0.35f, 1);
  switch (e->kind) {
  case EN_CRAWLER: {
    float rear = tell ? fminf(1, tk * 2) * 0.35f : 0;
    for (int k = 0; k < 3; k++) {
      float lx = x + (float)(k - 1) * 0.35f, sw = sinf(t * 14 + (float)k * 2) * 0.12f * (e->state == ES_ACT ? 2 : 1);
      limb(v2(lx, y - 0.35f), v2(lx + sw - 0.2f, y), 0.04f, 0.03f, scalec(shell, 0.6f));
      limb(v2(lx, y - 0.35f), v2(lx + sw + 0.2f, y), 0.04f, 0.03f, scalec(shell, 0.6f));
    }
    cw_vec2 c = v2(x, y - 0.45f - rear * 0.5f);
    cw_r2d_arc(c, 0, 0.62f, -1.5708f - rear * f, 1.5708f - rear * f, 18, shell);
    cw_r2d_arc(c, 0.2f, 0.32f, -1.2f - rear * f, 1.2f - rear * f, 12, mixc(shell, hot, 0.5f));
    cw_r2d_circle(v2(x + f * 0.5f, y - 0.35f - rear), 0.08f, 6, eye);
    if (tell) glow(x + f * 0.5f, y - 0.35f - rear, 0.6f, scalec(rgba(1, 0.6f, 0.2f, 1), 0.6f));
    break;
  }
  case EN_SPITTER: {
    float swell = tell ? 1 + tk * 0.45f : 1 + sinf(t * 2 + x) * 0.04f;
    limb(v2(x, y), v2(x + sinf(t + x) * 0.1f, y - 0.8f), 0.12f, 0.09f, rgba(0.35f, 0.55f, 0.3f, 1));
    cw_r2d_circle(v2(x, y - 1.0f), 0.38f * swell, 16, shell);
    cw_r2d_circle(v2(x + f * 0.18f, y - 1.0f), 0.16f * swell, 12, tell ? eye : hot);
    if (tell) glow(x + f * 0.18f, y - 1.0f, 0.9f * swell, scalec(rgba(1, 0.5f, 0.2f, 1), 0.5f + 0.5f * tk));
    break;
  }
  case EN_FLYER: {
    float flap = sinf(t * (tell ? 30 : 16) + x) * 0.6f, spread = tell ? 1.4f : 1;
    for (int k = -1; k <= 1; k += 2) {
      cw_vec2 w[3] = { v2(x, y - 0.45f), v2(x + (float)k * 0.9f * spread, y - 0.45f - 0.4f - flap * 0.4f), v2(x + (float)k * 0.7f * spread, y - 0.2f + flap * 0.2f) };
      cw_r2d_polygon(w, 3, tell ? mixc(shell, hot, 0.6f) : mixc(shell, rgba(0.9f, 0.85f, 1, 1), 0.3f));
    }
    cw_r2d_circle(v2(x, y - 0.45f), 0.25f, 12, scalec(shell, 0.8f));
    cw_r2d_circle(v2(x + f * 0.1f, y - 0.5f), 0.06f, 6, eye);
    break;
  }
  case EN_HOPPER: {
    float sq = tell ? 1 - tk * 0.35f : e->state == ES_ACT ? 1.2f : 1;
    cw_r2d_circle(v2(x, y - 0.42f * sq), 0.5f, 16, shell);
    cw_r2d_circle(v2(x + f * 0.3f, y - 0.7f * sq), 0.13f, 10, rgba(1, 1, 1, 1));
    cw_r2d_circle(v2(x + f * 0.33f, y - 0.7f * sq), 0.07f, 8, eye);
    limb(v2(x - f * 0.2f, y - 0.2f), v2(x - f * 0.55f, y), 0.08f, 0.06f, scalec(shell, 0.7f));
    if (tell) glow(x, y - 0.4f, 0.9f, scalec(rgba(1, 0.5f, 0.3f, 1), 0.4f * tk));
    break;
  }
  default: { /* knight */
    float raise = tell ? fminf(1, tk) : 0, thrust = e->state == ES_ACT ? 1 : 0;
    limb(v2(x - 0.15f, y - 0.9f), v2(x - 0.2f, y), 0.12f, 0.1f, scalec(shell, 0.6f));
    limb(v2(x + 0.15f, y - 0.9f), v2(x + 0.2f, y), 0.12f, 0.1f, scalec(shell, 0.6f));
    cw_r2d_round_rect((cw_rect){ x - 0.4f, y - 1.75f, 0.8f, 0.95f }, 0.15f, 4, mixc(shell, rgba(0.7f, 0.72f, 0.8f, 1), 0.5f));
    cw_r2d_circle(v2(x, y - 1.95f), 0.25f, 12, rgba(0.7f, 0.72f, 0.8f, 1));
    cw_r2d_rect((cw_rect){ x + f * 0.05f - 0.03f, y - 2.0f, 0.2f * f, 0.06f }, eye);
    /* shield in front */
    cw_r2d_round_rect((cw_rect){ x + f * 0.45f - 0.18f, y - 1.6f, 0.36f, 1.1f }, 0.12f, 4, mixc(hexc(g->accent, 1), rgba(0.5f, 0.5f, 0.6f, 1), 0.5f));
    cw_vec2 hand = v2(x + f * 0.25f, y - 1.3f - raise * 0.5f);
    cw_vec2 tip = v2(hand.x + f * (1.2f + thrust * 1.3f), hand.y - raise * 0.6f + thrust * 0.4f);
    limb(v2(hand.x - f * 0.6f, hand.y + raise * 0.3f), tip, 0.04f, 0.03f, rgba(0.6f, 0.5f, 0.4f, 1));
    if (tell) glow(tip.x, tip.y, 0.5f, rgba(1, 1, 0.8f, 0.8f * raise));
    break;
  }
  }
}

/* ── the wardens ── */
static void draw_boss(const sim *s, float t) {
  const boss *b = &s->b;
  if (!b->active) return;
  float f = b->facing_left ? -1.0f : 1.0f, x = b->x, y = b->y, hw = boss_hw(b->idx), hh = boss_hh(b->idx);
  bool tell = b->state == ES_TELL;
  float tk = tell ? (float)b->t / (float)boss_tell_ticks(b->move) : 0;
  static const uint32_t BODY[NBOSSES] = { 0x5f8a3a, 0x3f8cb8, 0xc89a48, 0x2f9aa0, 0xc8c8dc, 0x9a3a58 }; /* (mid values: lit, they stay shapes) */
  static const uint32_t GLOW[NBOSSES] = { 0xd6ff6a, 0xbff6ff, 0xffd060, 0x8affe6, 0xffe6a0, 0xffb060 };
  cw_color body = hexc(BODY[b->idx], 1), gl = hexc(GLOW[b->idx], 1);
  if (b->flash > 0) body = mixc(body, rgba(1, 1, 1, 1), 0.6f);
  float breathe = sinf(t * 2.2f) * 0.05f;
  float crouch = tell && (b->move == BM_LEAP || b->move == BM_CHARGE) ? tk * 0.35f : 0;
  cw_vec2 c = v2(x, y - hh - breathe + crouch * hh);
  switch (b->idx) {
  case 0: /* a mossy beast: a broad back of moss, horns, glowing eyes */
    cw_r2d_arc(v2(x, y), 0, hw * 1.15f, -1.5708f, 1.5708f, 24, scalec(body, 0.7f));
    cw_r2d_circle(v2(c.x, c.y + 0.2f), hh * 0.95f, 24, body);
    for (int k = 0; k < 5; k++) cw_r2d_circle(v2(c.x - hw * 0.8f + (float)k * hw * 0.4f, c.y - hh * 0.7f + fabsf((float)k - 2) * 0.15f), 0.35f, 10, mixc(body, gl, 0.4f));
    limb(v2(c.x + f * hw * 0.5f, c.y - hh * 0.4f), v2(c.x + f * hw * 1.1f, c.y - hh * 1.1f), 0.18f, 0.05f, rgba(0.9f, 0.85f, 0.7f, 1));
    break;
  case 1: /* a glass mantis: thin limbs, scythe arms */
    limb(v2(x - 0.6f, y), v2(c.x, c.y + 0.5f), 0.1f, 0.12f, body);
    limb(v2(x + 0.6f, y), v2(c.x, c.y + 0.5f), 0.1f, 0.12f, body);
    cw_r2d_round_rect((cw_rect){ c.x - 0.45f, c.y - 1.0f, 0.9f, 1.6f }, 0.4f, 6, body);
    for (int k = -1; k <= 1; k += 2) {
      float up = tell && (b->move == BM_SWEEP_LOW || b->move == BM_SWEEP_HIGH) ? tk : 0;
      cw_vec2 el = v2(c.x + (float)k * 0.6f, c.y - 0.6f - up * 0.6f), sc = v2(el.x + f * 1.4f, el.y + 0.3f - up);
      limb(v2(c.x, c.y - 0.6f), el, 0.08f, 0.07f, body);
      limb(el, sc, 0.12f, 0.02f, mixc(body, gl, 0.6f));
    }
    break;
  case 2: /* a brass sentinel: a boxy torso with a core */
    cw_r2d_round_rect((cw_rect){ c.x - hw, c.y - hh * 0.9f, hw * 2, hh * 1.6f }, 0.3f, 6, body);
    cw_r2d_rect((cw_rect){ x - hw * 0.7f, y - 0.7f, 0.5f, 0.7f }, scalec(body, 0.7f));
    cw_r2d_rect((cw_rect){ x + hw * 0.7f - 0.5f, y - 0.7f, 0.5f, 0.7f }, scalec(body, 0.7f));
    cw_r2d_circle(v2(c.x, c.y - 0.1f), 0.5f + tk * 0.2f, 18, mixc(gl, rgba(1, 1, 1, 1), tk));
    break;
  case 3: /* a serpent: a coiling body of segments */
    for (int k = 6; k >= 0; k--) {
      float u = (float)k / 6, sx = c.x - f * u * 3.2f, sy = c.y + sinf(t * 3 + u * 5) * 0.5f + u * 0.4f;
      cw_r2d_circle(v2(sx, sy), 0.95f - u * 0.45f, 16, k & 1 ? body : mixc(body, gl, 0.25f));
    }
    break;
  case 4: { /* the sky warden: a great bird of pale feathers */
    float flap = sinf(t * 5) * 0.5f;
    for (int k = -1; k <= 1; k += 2) {
      cw_vec2 w[4] = { v2(c.x, c.y - 0.2f), v2(c.x + (float)k * 2.8f, c.y - 1.4f - flap), v2(c.x + (float)k * 3.2f, c.y - 0.3f - flap * 0.5f), v2(c.x + (float)k * 1.0f, c.y + 0.5f) };
      cw_r2d_polygon(w, 4, mixc(body, gl, 0.15f));
    }
    cw_r2d_circle(c, 0.9f, 16, body);
    cw_r2d_circle(v2(c.x + f * 0.7f, c.y - 0.6f), 0.5f, 14, body);
    cw_vec2 beak[3] = { v2(c.x + f * 1.1f, c.y - 0.7f), v2(c.x + f * 1.7f, c.y - 0.5f), v2(c.x + f * 1.1f, c.y - 0.4f) };
    cw_r2d_polygon(beak, 3, gl);
    break;
  }
  default: /* the heart of thorns: a heart wrapped in thorny vines, pulsing */
    for (int k = 0; k < 10; k++) {
      float a = (float)k * 0.628f + t * 0.3f;
      limb(c, v2(c.x + cosf(a) * hw * 1.6f, c.y + sinf(a) * hh * 1.1f), 0.18f, 0.04f, scalec(body, 0.6f));
    }
    cw_r2d_circle(c, hw * (0.9f + 0.06f * sinf(t * 6)), 28, body);
    cw_r2d_circle(v2(c.x, c.y - 0.2f), hw * 0.45f, 20, mixc(body, gl, 0.5f + 0.5f * tk));
    break;
  }
  /* the eye every warden has: where it looks, it will strike */
  cw_vec2 eye = v2(c.x + f * hw * 0.55f, c.y - hh * 0.25f);
  if (b->idx == 3) eye = v2(c.x + f * 0.5f, c.y - 0.3f);
  if (b->idx == 4) eye = v2(c.x + f * 0.85f, c.y - 0.7f);
  glowing(tell ? 5.0f : 2.5f);
  cw_r2d_circle(eye, 0.16f + tk * 0.06f, 10, tell ? rgba(1, 1, 1, 1) : gl);
  glowing(1);
  light(c.x + f * hw * 1.5f, c.y - hh, 8, gl, 0.3f + tk * 0.5f, false); /* (in front of it: the warden is lit, not blown out) */
}

/* tells the player reads: marks where rain or pillars will fall, the line a sweep will cut, the dive's aim */
static void draw_tells(const sim *s, float t) {
  const boss *b = &s->b;
  if (!b->active || b->state != ES_TELL) return;
  float tk = (float)b->t / (float)boss_tell_ticks(b->move);
  float pulse = 0.55f + 0.45f * sinf(t * 18);
  switch (b->move) {
  case BM_RAIN:
    for (int i = 0; i < b->nmarks; i++) {
      cw_r2d_rect((cw_rect){ b->marks[i] - 0.45f, b->floor_y - 0.12f, 0.9f, 0.12f }, rgba(1, 0.4f, 0.3f, 0.6f * pulse));
      glow(b->marks[i], b->ay0 + 0.8f, 0.9f, rgba(1, 0.6f, 0.4f, 0.7f * tk));
    }
    break;
  case BM_PILLARS:
    for (int i = 0; i < b->nmarks; i++) glow(b->marks[i], b->floor_y, 1.4f, rgba(1, 0.55f, 0.25f, (0.3f + 0.6f * tk) * pulse));
    break;
  case BM_SWEEP_LOW: case BM_SWEEP_HIGH: {
    float yy = b->move == BM_SWEEP_LOW ? b->floor_y - 0.7f : b->floor_y - 2.75f;
    cw_r2d_rect((cw_rect){ b->ax0, yy - 0.04f, b->ax1 - b->ax0, 0.08f }, rgba(1, 0.5f, 0.9f, 0.35f + 0.5f * tk * pulse));
    break;
  }
  case BM_DIVE: {
    float dx = b->tx - b->x, dy = b->ty - b->y, l = sqrtf(dx * dx + dy * dy) + 0.01f;
    for (int k = 1; k < 10; k++) cw_r2d_circle(v2(b->x + dx * (float)k / 10, b->y + dy * (float)k / 10), 0.08f, 6, rgba(1, 0.7f, 0.5f, 0.5f * tk));
    (void)l;
    break;
  }
  default: break;
  }
}

static void draw_shots(const sim *s, float t) {
  for (int i = 0; i < MAXSHOTS; i++) {
    const shot *h = &s->sh[i];
    if (!h->alive) continue;
    cw_color c = h->enemy ? rgba(1, 0.45f, 0.3f, 1) : rgba(0.6f, 0.95f, 1, 1);
    if (h->kind == SH_SPORE) c = rgba(0.8f, 1, 0.4f, 1);
    if (h->kind == SH_FEATHER) c = rgba(1, 0.95f, 0.75f, 1);
    if (h->kind == SH_BEAM) {
      float k = h->t < 6 ? 0.3f : 1;
      cw_r2d_rect((cw_rect){ h->x - h->w, h->y - h->h, h->w * 2, h->h * 2 }, rgba(1, 0.4f, 0.9f, 0.55f * k));
      cw_r2d_rect((cw_rect){ h->x - h->w, h->y - h->h * 0.3f, h->w * 2, h->h * 0.6f }, rgba(1, 0.9f, 1, 0.8f * k));
      continue;
    }
    if (h->kind == SH_PILLAR) {
      float k = h->t < 6 ? 0.3f : 1;
      cw_r2d_rect((cw_rect){ h->x - h->w, h->y - h->h, h->w * 2, h->h * 2 }, rgba(1, 0.55f, 0.2f, 0.5f * k));
      cw_r2d_rect((cw_rect){ h->x - h->w * 0.35f, h->y - h->h, h->w * 0.7f, h->h * 2 }, rgba(1, 0.95f, 0.7f, 0.7f * k));
      continue;
    }
    if (h->kind == SH_WAVE) {
      cw_vec2 p[3] = { v2(h->x - 0.5f, h->y + 0.5f), v2(h->x + 0.5f, h->y + 0.5f), v2(h->x + h->vx * 0.03f, h->y - 0.6f) };
      cw_r2d_polygon(p, 3, rgba(1, 0.8f, 0.5f, 0.9f));
      glow(h->x, h->y, 1.0f, rgba(1, 0.6f, 0.3f, 0.6f));
      continue;
    }
    glow(h->x, h->y, h->w * 3.5f, scalec(c, 0.8f));
    cw_r2d_sprite(&(cw_sprite){ .texture = A.spark, .pos = { h->x, h->y }, .size = { h->w * 2.2f, h->w * 2.2f }, .pivot = { 0.5f, 0.5f }, .color = c });
    (void)t;
  }
}

static void shot_lights(const sim *s) {
  for (int i = 0; i < MAXSHOTS; i++) {
    const shot *h = &s->sh[i];
    if (!h->alive || h->kind == SH_BEAM) continue;
    cw_color c = h->enemy ? rgba(1, 0.5f, 0.3f, 1) : rgba(0.5f, 0.9f, 1, 1);
    light(h->x, h->y, h->kind == SH_PILLAR ? 6 : 4, c, 0.7f, false);
  }
}

static void draw_pickups(const sim *s, float t, float x0, float x1, float y0, float y1) {
  for (int i = 0; i < s->w->nents; i++) {
    const ent *e = &s->w->ents[i];
    if (e->x < x0 - 2 || e->x > x1 + 2 || e->y < y0 - 3 || e->y > y1 + 3) continue;
    if (e->kind == EK_PICKUP && !sim_got(s, e->id)) {
      float bob = sinf(t * 2.5f + e->x) * 0.12f, y = e->y - 0.9f + bob;
      cw_color c = e->sub == PK_ABILITY ? rgba(1, 0.85f, 0.4f, 1) : e->sub == PK_HEALTH ? rgba(1, 0.45f, 0.5f, 1) : e->sub == PK_ENERGY ? rgba(0.45f, 0.85f, 1, 1) : rgba(0.85f, 0.6f, 1, 1);
      float spin = t * 1.5f;
      cw_vec2 d[4] = { v2(e->x, y - 0.42f), v2(e->x + 0.3f * cosf(spin), y), v2(e->x, y + 0.42f), v2(e->x - 0.3f * cosf(spin), y) };
      glowing(2.6f);
      cw_r2d_polygon(d, 4, c);
      glowing(1);
      cw_r2d_circle(v2(e->x, y), 0.12f, 8, rgba(1, 1, 1, 1));
      light(e->x, y, 6, c, 0.9f, false);
    }
    if (e->kind == EK_SAVE) { /* a shrine: a stone with a ring that glows once lit */
      bool lit = (s->saves_lit[e->id >> 3] >> (e->id & 7)) & 1;
      cw_r2d_round_rect((cw_rect){ e->x - 0.7f, e->y - 2.2f, 1.4f, 2.2f }, 0.5f, 6, rgba(0.72f, 0.7f, 0.66f, 1));
      cw_r2d_circle_lines(v2(e->x, e->y - 1.45f), 0.38f, 0.1f, 20, lit ? rgba(0.6f, 1, 0.9f, 1) : rgba(0.4f, 0.4f, 0.42f, 1));
      if (lit) light(e->x, e->y - 1.45f, 8, rgba(0.5f, 1, 0.9f, 1), 0.8f, false);
    }
  }
}

#ifdef EXPLORER_R2D_V2
/* the rock casts shadows: every run of solid tiles in a row in view is an occluder (the engine makes a distance field
 * of them, so the count costs little) */
static void submit_occluders(const sim *s, float x0, float y0, float x1, float y1) {
  for (int y = (int)floorf(y0) - 2; y <= (int)ceilf(y1) + 2; y++) {
    if (y < 0 || y >= TH) continue;
    int x = (int)floorf(x0) - 4, xe = (int)ceilf(x1) + 4;
    while (x <= xe) {
      while (x <= xe && !(w_in(x, y) && t_solid(s->t[y][x]))) x++;
      int a = x;
      while (x <= xe && w_in(x, y) && t_solid(s->t[y][x])) x++;
      if (x > a) {
        cw_vec2 q[4] = { { (float)a, (float)y }, { (float)x, (float)y }, { (float)x, (float)y + 1 }, { (float)a, (float)y + 1 } };
        cw_r2d_occluder(q, 4, true);
      }
    }
  }
}

/* shockwaves: a plunge's landing, a warden's fall, a hidden wall breaking */
typedef struct wave_fx { float x, y, age, size; } wave_fx;
static wave_fx waves[8];
static void add_wave(float x, float y, float size) {
  for (int i = 0; i < 8; i++)
    if (waves[i].age <= 0 || waves[i].age > 1) { waves[i] = (wave_fx){ x, y, 0.0001f, size }; return; }
}
static void draw_waves(void) {
  for (int i = 0; i < 8; i++) {
    wave_fx *w = &waves[i];
    if (w->age <= 0 || w->age > 1) continue;
    cw_r2d_shockwave((cw_vec2){ w->x, w->y }, w->age * w->size, 1.2f + w->size * 0.12f, 1 - w->age);
  }
}

/* water pools: each body in view drawn by the engine from the screen texture (refraction, the world mirrored on the
 * surface, caustics, a lit surface line), its surface a spring per column that splashes ripple */
typedef struct pool_fx { int x0, x1, y, depth; cw_water2d *surf; int seen; } pool_fx;
static pool_fx pools[12];
static pool_fx *pool_at(int x0, int x1, int y, int depth) {
  pool_fx *free_slot = NULL, *oldest = &pools[0];
  for (int i = 0; i < 12; i++) {
    pool_fx *p = &pools[i];
    if (p->surf && p->x0 == x0 && p->y == y) { p->x1 = x1, p->depth = depth; return p; }
    if (!p->surf && !free_slot) free_slot = p;
    if (p->seen < oldest->seen) oldest = p;
  }
  pool_fx *p = free_slot ? free_slot : oldest;
  if (p->surf) cw_water2d_free(p->surf);
  *p = (pool_fx){ x0, x1, y, depth, cw_water2d_new(&(cw_water2d_desc){ .columns = (x1 - x0) * 4 < 8 ? 8 : (x1 - x0) * 4, .width = (float)(x1 - x0) }), 0 };
  return p;
}
static void draw_water_v2(const sim *s, float x0, float y0, float x1, float y1, float t) {
  static int frame;
  frame++;
  for (int y = (int)y0 - 1; y <= (int)y1 + 1; y++)
    for (int x = (int)x0 - 1; x <= (int)x1 + 1;) {
      if (!w_in(x, y) || s->t[y][x] != T_WATER || (y > 0 && s->t[y - 1][x] == T_WATER)) { x++; continue; }
      int a = x;
      while (w_in(x, y) && s->t[y][x] == T_WATER && s->t[y - 1][x] != T_WATER) x++;
      int depth = 1;
      while (w_in(a, y + depth) && s->t[y + depth][a] == T_WATER) depth++;
      pool_fx *p = pool_at(a, x, y, depth);
      p->seen = frame;
      cw_vec2 tl = view_world_to_screen((cw_vec2){ (float)a, (float)y }), br = view_world_to_screen((cw_vec2){ (float)x, (float)(y + depth) });
      cw_r2d_water(&(cw_r2d_water_desc){ .area = { tl.x, tl.y, br.x - tl.x, br.y - tl.y }, .surface = p->surf, .time = t, .wave_height = zoom * 0.05f,
                                        .tint = rgba(0.06f, 0.3f, 0.42f, 0.55f), .refraction = zoom * 0.06f, .reflection = 0.55f, .caustics = 0.5f,
                                        .surface_glow = 0.7f });
    }
}
static void water_splash(float x, float y, float speed) {
  for (int i = 0; i < 12; i++) {
    pool_fx *p = &pools[i];
    if (p->surf && x >= (float)p->x0 && x <= (float)p->x1 && y >= (float)p->y - 2 && y <= (float)(p->y + p->depth)) cw_water2d_splash(p->surf, x - (float)p->x0, speed);
  }
}
#endif

static void draw_water(const sim *s, float x0, float y0, float x1, float y1, float t) {
  int region = s->room >= 0 ? s->w->rooms[s->room].region : 0;
  cw_color deep = mixc(hexc(REGIONS[region].mid, 1), rgba(0.2f, 0.55f, 0.75f, 1), 0.6f);
  for (int y = (int)y0 - 1; y <= (int)y1 + 1; y++)
    for (int x = (int)x0 - 1; x <= (int)x1 + 1; x++) {
      if (!w_in(x, y) || s->t[y][x] != T_WATER) continue;
      bool surface = y > 0 && s->t[y - 1][x] != T_WATER;
      cw_r2d_rect((cw_rect){ (float)x, (float)y, 1, 1 }, rgba(deep.r, deep.g, deep.b, 0.5f));
      if (surface) {
        float w0 = sinf(t * 2.1f + (float)x * 0.9f) * 0.05f, w1 = sinf(t * 2.1f + (float)(x + 1) * 0.9f) * 0.05f;
        quadc(v2((float)x, (float)y + w0), v2((float)x + 1, (float)y + w1), v2((float)x + 1, (float)y + 0.12f + w1), v2((float)x, (float)y + 0.12f + w0), rgba(0.85f, 1, 1, 0.55f));
      }
    }
}

void view_draw(const sim *s, float t) {
  double tv0 = now_ms();
  memset(&VS, 0, sizeof VS);

  if (!rockdist || rock_dirty) build_rockdist(s), rock_dirty = false;
  static int empty = -1; /* the perf gate's control: nothing of the world drawn */
  if (empty < 0) empty = getenv("EXPLORER_BENCH_EMPTY") != NULL;
  if (empty || view_canvas().y < 1) {
    cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = true, .clear_color = rgba(0, 0, 0, 1) });
    cw_r2d_end();
    return;
  }
  cw_vec2 cs = view_canvas();
  int region = s->room >= 0 ? s->w->rooms[s->room].region : 0;
  const region_info *g = &REGIONS[region];
  cw_color amb = mixc(hexc(g->sky, 1), rgba(1, 1, 1, 1), 0.55f);
#ifdef EXPLORER_R2D_V2
  /* the HDR canvas: bloom on what glows past 1, the soft tone curve (art under its knee is untouched), a light grade
   * toward the region's accent, a soft vignette; the lit layers get bounce light (GI) from what glows */
  cw_color accent = hexc(g->accent, 1);
  static cw_r2d_post post;
  post = (cw_r2d_post){ .hud_layer = L_HUD, .bloom = true, .bloom_threshold = 1.0f, .bloom_intensity = 0.55f, .bloom_levels = 4, .tone_knee = 0.78f,
                        .saturation = 1.12f, .contrast = 1.06f, .gain = mixc(rgba(1, 1, 1, 1), accent, 0.06f), .vignette = 0.22f };
  /* (a quarter of the canvas, the engine's High: on v0.37 it costs less than an eighth, whose source pass reads 16
   * taps a texel, and the bounce is finer) */
  static cw_r2d_gi gi = { .intensity = 0.8f, .resolution = 0.25f };
  static int gi_env = -1; /* EXPLORER_GI_RES: another share (measuring) */
  if (gi_env < 0) gi_env = getenv("EXPLORER_GI_RES") != NULL, gi.resolution = gi_env ? (float)atof(getenv("EXPLORER_GI_RES")) : gi.resolution;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = true, .clear_color = hexc(g->sky, 1), .ambient = scalec(amb, 0.92f), .time = t, .post = perf_off("post") ? NULL : &post,
                                    .gi = g_gi ? &gi : NULL });
  cw_camera2d cam = { .position = g_cam.position, .zoom = zoom };
  for (int l = L_WALL; l <= L_FRONT; l++)
    cw_r2d_layer_set(l, &(cw_r2d_layer_desc){ .has_camera = l != L_WATER, .camera = cam, .lit = l >= L_WALL && l <= L_ACTORS,
                                              .occluder_depth = l == L_WALL ? 0.9f : 0, .screen_read = l == L_WATER, .distortion = l == L_FRONT ? 10 : 0 });
#else
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = true, .clear_color = hexc(g->sky, 1), .ambient = scalec(amb, 0.97f), .time = t });
  cw_camera2d cam = { .position = g_cam.position, .zoom = zoom };
  for (int l = L_WALL; l <= L_FRONT; l++) cw_r2d_layer_set(l, &(cw_r2d_layer_desc){ .has_camera = true, .camera = cam, .lit = l >= L_WALL && l <= L_ACTORS });
#endif
  cw_r2d_layer_set(L_SKY, NULL);
  cw_r2d_layer_set(L_HUD, NULL);
  draw_sky(s, t, region);
  float vw = cs.x / zoom, vh = cs.y / zoom;
  float x0 = g_cam.position.x - vw * 0.5f, x1 = g_cam.position.x + vw * 0.5f, y0 = g_cam.position.y - vh * 0.5f, y1 = g_cam.position.y + vh * 0.5f;
  /* back walls of every room in view */
  cw_r2d_use_layer(L_WALL);
  for (int cy = (int)(y0 / CELL_H); cy <= (int)(y1 / CELL_H) && cy < GH; cy++)
    for (int cx = (int)(x0 / CELL_W); cx <= (int)(x1 / CELL_W) && cx < GW; cx++) {
      if (cx < 0 || cy < 0) continue;
      int r = s->w->cell_room[cy][cx];
      if (r == 0xffff) continue;
      const room *rm = &s->w->rooms[r];
      if (rm->x != cx && cx != (int)(x0 / CELL_W)) continue; /* once per room in view */
      if (rm->y != cy && cy != (int)(y0 / CELL_H)) continue;
      draw_room_wall(s, r, x0, y0, x1, y1);
    }
  cw_r2d_use_layer(L_TILES);
  draw_tiles(s, x0, y0, x1, y1, t);
#ifdef EXPLORER_R2D_V2
  if (!perf_off("occl")) submit_occluders(s, x0, y0, x1, y1);
#endif
  for (int i = 0; i < s->w->nents; i++) {
    const ent *e = &s->w->ents[i];
    if (e->x < x0 - 3 || e->x > x1 + 3 || e->y < y0 - 4 || e->y > y1 + 6) continue;
    int er = s->w->rooms[e->room].region;
    if (e->kind == EK_PROP) draw_prop(e, &REGIONS[er], t);
    else if (e->kind == EK_LAMP) draw_lamp(e, &REGIONS[er], t, true);
    else if (e->kind == EK_ANCHOR) {
      cw_r2d_circle_lines(v2(e->x, e->y), 0.32f, 0.1f, 16, rgba(0.95f, 0.85f, 0.55f, 1));
      limb(v2(e->x, e->y - 0.3f), v2(e->x, e->y - 1.5f), 0.05f, 0.05f, rgba(0.5f, 0.45f, 0.4f, 1));
      light(e->x, e->y, 3.5f, rgba(1, 0.9f, 0.6f, 1), 0.5f, false);
    }
  }
  draw_pickups(s, t, x0, x1, y0, y1);
  shot_lights(s);
  cw_r2d_use_layer(L_ACTORS);
  for (int i = 0; i < s->nen; i++)
    if (s->en[i].alive) draw_enemy(s, &s->en[i], t), VS.enemies++;
  draw_boss(s, t);
  for (int i = s->players - 1; i >= 0; i--) {
    const player *p = &s->p[i];
    if (p->state == PS_GRAPPLE && p->anchor >= 0 && p->anchor < s->pw.nanchors) { /* the line */
      const ent *a = s->pw.anchors[p->anchor];
      limb(v2(p->x, p->y - 1.1f), v2(a->x, a->y), 0.04f, 0.04f, rgba(1, 0.9f, 0.6f, 1));
    }
    draw_hero(s, i, t);
  }
  /* effects: additive glows and particles over the lit scene */
  cw_r2d_use_layer(L_FX);
  cw_r2d_set_blend(CW_BLEND_ADD);
  glowing(1.8f); /* (unlit additive: intensity alone lifts them past 1 for the bloom) */
  draw_barriers(s, x0, y0, x1, y1, t);
  for (int i = 0; i < s->w->nents; i++) { /* halos round every light source, and the shrines' warmth */
    const ent *e = &s->w->ents[i];
    if (e->kind != EK_LAMP || e->x < x0 - 3 || e->x > x1 + 3 || e->y < y0 - 4 || e->y > y1 + 6) continue;
    cw_color acc = hexc(REGIONS[s->w->rooms[e->room].region].accent, 1);
    if (acc.r - acc.b < 0.2f) acc = mixc(acc, rgba(1, 0.72f, 0.4f, 1), 0.7f); /* (as the lamp) */
    if (e->sub == 8 && e->hold == HOLD_FLOOR) acc = rgba(0.45f, 0.75f, 1, 1); /* (a pilot light: blue) */
    cw_color sk = hexc(REGIONS[s->w->rooms[e->room].region].sky, 1);
    if (e->hold == HOLD_FLOOR && (e->sub == 5 || e->sub == 6) && sk.r > sk.b + 0.05f) acc = rgba(0.42f, 0.7f, 1, 1); /* (as the lamp) */
    if (e->sub == 9 && e->hold == HOLD_FLOOR) acc = rgba(1, 0.52f, 0.14f, 1);   /* (a lantern post: amber) */
    float cy = e->hold == HOLD_CEIL ? e->y + (float)e->arg * 0.45f + 0.35f : e->sub == 7 ? e->y - 1.1f : e->sub == 8 ? e->y - 0.75f : e->sub == 9 ? e->y - 1.79f : e->y - 0.55f;
    float fl = 0.85f + 0.15f * sinf(t * 7 + e->x);
    bool tinted = e->hold == HOLD_FLOOR && (e->sub == 8 || e->sub == 9 || acc.b > acc.r + 0.3f); /* (a coloured core: the accent stays its colour) */
    glow(e->x, cy, (tinted ? 2.2f : 1.6f) * fl, scalec(acc, tinted ? 0.62f : 0.45f));
    glow(e->x, cy, 0.5f, tinted ? scalec(acc, 0.7f) : rgba(1, 1, 0.9f, 0.8f));
  }
  for (int i = 0; i < s->players; i++) { /* the lantern's glow and the strike's arc */
    const player *p = &s->p[i];
    if (!p->active) continue;
    glow(p->x, p->y - 1.0f, 2.6f, rgba(1, 0.8f, 0.5f, 0.12f));
    if (p->atk_t > 4) {
      float k = 1 - (float)(p->atk_t - 4) / 10.0f, f = p->facing_left ? -1.0f : 1.0f;
      float a0, a1;
      cw_vec2 c = v2(p->x, p->y - 1.0f);
      if (p->atk_dir == 1) a0 = -2.6f, a1 = -0.5f;
      else if (p->atk_dir == 2) a0 = 0.5f, a1 = 2.6f;
      else a0 = f > 0 ? -1.2f : 1.9f, a1 = f > 0 ? 1.2f : 4.3f;
      cw_r2d_arc(c, 1.2f, 2.1f, a0 + (a1 - a0) * (k - 0.5f), a0 + (a1 - a0) * k, 14, rgba(0.85f, 0.95f, 1, 0.55f * (1 - k * 0.6f)));
    }
    if (p->state == PS_DASH) cw_r2d_rect((cw_rect){ p->x - (p->dash_dir > 0 ? 2.5f : 0), p->y - 1.3f, 2.5f, 0.9f }, rgba(0.6f, 0.9f, 1, 0.25f));
  }
  draw_tells(s, t);
  glowing(3.0f);
  draw_shots(s, t);
  glowing(1.8f);
  for (int i = 0; i < np; i++) {
    const part *q = &P[i];
    float k = q->life / q->max;
    if (!q->add) continue;
    cw_color c = q->c;
    c.a *= k;
    cw_r2d_sprite(&(cw_sprite){ .texture = A.spark, .pos = { q->x, q->y }, .size = { q->size, q->size }, .pivot = { 0.5f, 0.5f }, .color = c });
  }
  cw_r2d_set_blend(CW_BLEND_ALPHA);
  glowing(1);
  for (int i = 0; i < np; i++) {
    const part *q = &P[i];
    if (q->add) continue;
    float k = q->life / q->max;
    cw_color c = q->c;
    c.a *= fminf(1, k * 3);
    cw_r2d_sprite(&(cw_sprite){ .texture = q->kind == 1 ? A.leaf : A.soft, .pos = { q->x, q->y }, .size = { q->size, q->kind == 1 ? q->size * 0.5f : q->size },
                                .pivot = { 0.5f, 0.5f }, .rotation = q->rot, .color = c });
  }
  VS.particles = np;
  cw_r2d_use_layer(L_WATER);
#ifdef EXPLORER_R2D_V2
  if (!perf_off("water")) draw_water_v2(s, x0, y0, x1, y1, t);
  cw_r2d_use_layer(L_FRAME);
  if (!perf_off("frame")) draw_framing(s, t);
  cw_r2d_use_layer(L_FRONT); /* the distortion layer: shockwaves */
  if (!perf_off("dist")) draw_waves();
#else
  draw_water(s, x0, y0, x1, y1, t);
  cw_r2d_use_layer(L_FRAME);
  draw_framing(s, t);
#endif
  /* a warden's tell or a pickup: a brief wash of colour */
  if (flash_t > 0) {
    cw_r2d_use_layer(L_HUD);
    cw_r2d_rect((cw_rect){ 0, 0, cs.x, cs.y }, rgba(flash_c.r, flash_c.g, flash_c.b, flash_c.a * flash_t));
  }
  double te0 = now_ms();
  cw_r2d_end();
  VS.end_ms = (float)(now_ms() - te0), VS.submit_ms = (float)(te0 - tv0);
  (void)cur_tint, (void)last_hp_t;
}

/* ── effects from events ── */
void view_events(const sim *s) {
  int region = s->room >= 0 ? s->w->rooms[s->room].region : 0;
  cw_color acc = hexc(REGIONS[region].accent, 1), top = hexc(REGIONS[region].top, 1);
  for (int i = 0; i < s->players; i++) {
    const player *p = &s->p[i];
    uint32_t e = p->ev;
    if (e & (PE_JUMP | PE_WALLJ))
      for (int k = 0; k < 6; k++) emit(p->x + (frand() - 0.5f) * 0.6f, p->y, (frand() - 0.5f) * 3, -frand() * 1.5f, 0.4f, 0.35f, rgba(1, 1, 1, 0.35f), 0, false, 0);
    if (e & PE_DJUMP) burst(p->x, p->y - 0.2f, 14, 4, 0.45f, 0.35f, rgba(0.7f, 0.95f, 1, 0.8f), true, 0);
    if (e & PE_LAND)
      for (int k = 0; k < (p->land_v > 18 ? 14 : 6); k++) emit(p->x + (frand() - 0.5f) * 0.8f, p->y, (frand() - 0.5f) * 6, -frand() * 2, 0.5f, 0.45f, rgba(1, 1, 1, 0.3f), 0, false, 2);
    if (e & PE_DASH) burst(p->x, p->y - 0.9f, 10, 6, 0.3f, 0.3f, rgba(0.6f, 0.9f, 1, 0.9f), true, 0);
    if (e & PE_POUND_LAND) burst(p->x, p->y, 30, 10, 0.6f, 0.5f, scalec(acc, 1), true, 6), flash_t = 0.25f, flash_c = rgba(1, 0.9f, 0.7f, 0.25f);
#ifdef EXPLORER_R2D_V2
    if (e & PE_POUND_LAND) add_wave(p->x, p->y - 0.3f, 7);
    if (e & PE_SPLASH) water_splash(p->x, p->y, 3.5f);
    if (p->state == PS_SWIM && (s->tick % 12) == 0) water_splash(p->x, p->y - 1.6f, 0.8f);
#endif
    if (e & PE_SPLASH)
      for (int k = 0; k < 16; k++) emit(p->x + (frand() - 0.5f), p->y - 1.2f, (frand() - 0.5f) * 5, -4 - frand() * 5, 0.7f, 0.25f, rgba(0.8f, 1, 1, 0.8f), 0, false, 25);
    if (e & PE_GRAPPLE) burst(p->x, p->y - 1.1f, 8, 3, 0.3f, 0.25f, rgba(1, 0.9f, 0.6f, 1), true, 0);
    if (p->state == PS_SWIM && (s->tick % 9) == 0) emit(p->x, p->y - 1.6f, (frand() - 0.5f) * 0.5f, -1.5f, 1.2f, 0.18f, rgba(0.85f, 1, 1, 0.7f), 0, false, -1);
    if (p->state == PS_DASH && (s->tick & 1)) emit(p->x, p->y - 0.9f, 0, 0, 0.25f, 1.2f, rgba(0.5f, 0.85f, 1, 0.35f), 0, true, 0);
    if (p->on_ground && fabsf(p->vx) > 6 && (s->tick % 7) == 0) emit(p->x, p->y, -p->vx * 0.05f, -0.6f, 0.4f, 0.3f, rgba(1, 1, 1, 0.22f), 0, false, 0);
  }
  for (int i = 0; i < s->nev; i++) {
    const event *e = &s->ev[i];
    switch (e->kind) {
    case EV_KILL: burst(e->x, e->y, 22, 7, 0.6f, 0.4f, rgba(1, 0.55f, 0.35f, 1), true, 4); break;
    case EV_HIT: burst(e->x, e->y, 8, 5, 0.3f, 0.3f, e->a == 2 ? rgba(0.8f, 0.85f, 1, 1) : rgba(1, 0.85f, 0.6f, 1), true, 0); break;
    case EV_BOSS_HIT: burst(e->x, e->y, 10, 6, 0.35f, 0.35f, rgba(1, 0.9f, 0.7f, 1), true, 0); break;
    case EV_BOSS_DOWN:
#ifdef EXPLORER_R2D_V2
      add_wave(e->x, e->y, 14);
#endif
      rock_dirty = true; burst(e->x, e->y, 120, 12, 1.4f, 0.6f, rgba(1, 0.85f, 0.5f, 1), true, 2), flash_t = 1, flash_c = rgba(1, 0.95f, 0.8f, 0.5f); break;
    case EV_BOSS_TELL: flash_t = 0.15f, flash_c = rgba(1, 0.5f, 0.4f, 0.08f); break;
    case EV_PICKUP: burst(e->x, e->y - 0.9f, 50, 6, 1.0f, 0.45f, rgba(1, 0.9f, 0.55f, 1), true, -1), flash_t = 0.6f, flash_c = rgba(1, 0.95f, 0.8f, 0.3f); break;
    case EV_SAVE: burst(e->x, e->y - 1.45f, 30, 3, 1.2f, 0.4f, rgba(0.6f, 1, 0.9f, 1), true, -2); break;
    case EV_SEAL: case EV_BOSS_DOWN + 100: rock_dirty = true; break;
    case EV_BREAK: rock_dirty = true; burst(e->x, e->y, 26, 7, 0.9f, 0.4f, scalec(top, 0.8f), false, 22); break;
    case EV_SECRET:
#ifdef EXPLORER_R2D_V2
      add_wave(e->x + 0.5f, e->y + 0.5f, 5);
#endif
      rock_dirty = true; burst(e->x + 0.5f, e->y + 0.5f, 30, 6, 0.9f, 0.45f, rgba(0.85f, 0.75f, 0.65f, 1), false, 18); break;
    case EV_SHOT_HIT: burst(e->x, e->y, 8, 4, 0.3f, 0.3f, rgba(1, 0.8f, 0.6f, 1), true, 0); break;
    case EV_HURT: flash_t = 0.3f, flash_c = rgba(1, 0.2f, 0.25f, 0.2f); break;
    case EV_BOUNCE: burst(e->x, e->y, 10, 4, 0.3f, 0.3f, rgba(1, 1, 1, 0.9f), true, 0); break;
    default: break;
    }
  }
}

void view_tick(const sim *s, float dt) {
  if (flash_t > 0) flash_t -= dt * 2.5f;
#ifdef EXPLORER_R2D_V2
  for (int i = 0; i < 8; i++) if (waves[i].age > 0 && waves[i].age <= 1) waves[i].age += dt * 1.6f;
  for (int i = 0; i < 12; i++) if (pools[i].surf) cw_water2d_step(pools[i].surf, dt > 0 ? dt : 0);
#endif
  /* ambient motes for the region: fireflies, embers, snow, bubbles, petals, spores */
  int region = s->room >= 0 ? s->w->rooms[s->room].region : 0;
  cw_vec2 cs = view_canvas();
  float vw = cs.x / zoom, vh = cs.y / zoom;
  if (frand() < 0.5f) {
    float x = g_cam.position.x + (frand() - 0.5f) * vw, y = g_cam.position.y + (frand() - 0.5f) * vh;
    cw_color acc = hexc(REGIONS[region].accent, 1);
    switch (region) {
    case R_FORGE: emit(x, g_cam.position.y + vh * 0.5f, (frand() - 0.5f) * 0.5f, -1.5f - frand(), 4, 0.18f, rgba(1, 0.6f, 0.2f, 0.9f), 0, true, -0.2f); break;
    case R_FROST: case R_SKY: emit(x, g_cam.position.y - vh * 0.5f, 0.6f, 1.2f + frand(), 7, 0.16f, rgba(1, 1, 1, 0.8f), 0, false, 0); break;
    case R_TIDAL: case R_GLASS: emit(x, y, 0, -0.6f, 4, 0.14f, rgba(0.8f, 1, 1, 0.5f), 0, true, -0.1f); break;
    case R_BLOOM: case R_ROOTWAY: emit(x, g_cam.position.y - vh * 0.5f, 0.8f, 0.9f, 7, 0.3f, scalec(acc, 1), 1, false, 0.05f); break;
    default: emit(x, y, (frand() - 0.5f) * 0.4f, (frand() - 0.5f) * 0.4f, 5, 0.15f, rgba(acc.r, acc.g, acc.b, 0.9f), 0, true, 0); break;
    }
  }
  for (int i = 0; i < np;) {
    part *q = &P[i];
    q->life -= dt;
    if (q->life <= 0) { P[i] = P[--np]; continue; }
    q->vy += q->grav * dt;
    q->x += q->vx * dt, q->y += q->vy * dt;
    q->rot += q->spin * dt;
    if (q->kind == 1) q->vx += sinf(q->life * 2 + q->x) * 0.6f * dt;
    i++;
  }
}

view_stats view_get_stats(void) { return VS; }
