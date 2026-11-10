/* towers maps: the .cwtd parser (format in map.h). */
#include "map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "cartwheel/core.h"
#include "cartwheel/math.h"
#include "cartwheel/vfs.h"

const char *const MP_THEME_ID[MP_THEMES] = { "meadow", "autumn", "desert", "snow", "volcano", "crystal" };

/* one line of the text into out (no newline), trimmed of trailing spaces and a CR; returns the next line */
static const char *line(const char *p, char *out, int len) {
  int n = 0;
  while (*p && *p != '\n') {
    if (n < len - 1) out[n++] = *p;
    p++;
  }
  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\r' || out[n - 1] == '\t')) n--;
  out[n] = 0;
  return *p ? p + 1 : p;
}

#define FAIL(...) do { if (err) snprintf(err, (size_t)errlen, __VA_ARGS__); return false; } while (0)

bool map_parse(const char *text, tdmap *m, int (*kind_by_name)(const char *), char *err, int errlen) {
  memset(m, 0, sizeof *m);
  for (int y = 0; y < MP_MAX_H; y++)
    for (int x = 0; x < MP_MAX_W; x++) m->t[y][x] = T_ROCK;
  char ln[2048];
  const char *p = text;
  int ln_no = 0;
  bool header = false, sized = false, gridded = false, cored = false;
  m->theme = -1, m->diff = 1, m->waves = 20;
  while (*p) {
    p = line(p, ln, sizeof ln);
    ln_no++;
    if (!ln[0] || ln[0] == '#') continue;
    char w0[32] = "", w1[64] = "";
    int a, b, c, d, e;
    sscanf(ln, "%31s", w0);
    if (!header) {
      if (strcmp(w0, "towers-map")) FAIL("line %d: not a towers map (no towers-map header)", ln_no);
      header = true;
      continue;
    }
    if (!strcmp(w0, "map")) m->num = atoi(ln + 4);
    else if (!strcmp(w0, "seed")) m->seed = (uint32_t)strtoul(ln + 5, NULL, 10);
    else if (!strcmp(w0, "name")) snprintf(m->name, sizeof m->name, "%.47s", ln + 5);
    else if (!strcmp(w0, "waves")) {
      m->waves = atoi(ln + 6);
      if (m->waves < 1 || m->waves > 60) FAIL("line %d: waves must be 1 to 60", ln_no);
    } else if (!strcmp(w0, "diff")) {
      m->diff = (float)atof(ln + 5);
      if (!(m->diff > 0.05f && m->diff < 20)) FAIL("line %d: bad diff", ln_no);
    } else if (!strcmp(w0, "theme")) {
      sscanf(ln, "%*s %63s", w1);
      for (int k = 0; k < MP_THEMES; k++)
        if (!strcmp(w1, MP_THEME_ID[k])) m->theme = k;
      if (m->theme < 0) FAIL("line %d: unknown theme %s", ln_no, w1);
    } else if (!strcmp(w0, "kinds")) {
      const char *s = ln + 5;
      int n = 0;
      while (sscanf(s, " %63s%n", w1, &n) == 1) {
        int k = kind_by_name ? kind_by_name(w1) : 0;
        if (k < 0) FAIL("line %d: unknown creep %s", ln_no, w1);
        if (m->nkind >= MP_MAXKIND) FAIL("line %d: more than %d kinds", ln_no, MP_MAXKIND);
        m->kind[m->nkind++] = (uint8_t)k;
        s += n;
      }
    } else if (!strcmp(w0, "size")) {
      if (sscanf(ln, "%*s %d %d", &a, &b) != 2 || a < 8 || b < 6 || a > MP_MAX_W || b > MP_MAX_H) FAIL("line %d: bad size", ln_no);
      m->w = a, m->h = b, sized = true;
    } else if (!strcmp(w0, "grid")) {
      if (!sized) FAIL("line %d: grid before size", ln_no);
      for (int y = 0; y < m->h; y++) {
        if (!*p) FAIL("the grid ends at row %d of %d", y, m->h);
        p = line(p, ln, sizeof ln);
        ln_no++;
        if ((int)strlen(ln) != m->w) FAIL("line %d: grid row %d is %d wide, not %d", ln_no, y, (int)strlen(ln), m->w);
        for (int x = 0; x < m->w; x++) {
          char ch = ln[x];
          int t = ch == '.' ? T_PATH : ch == 'g' ? T_GROUND : ch == 'o' ? T_SLOT : ch == '#' ? T_ROCK : ch == '~' ? T_WATER : -1;
          if (t < 0) FAIL("line %d: unknown tile '%c'", ln_no, ch);
          m->t[y][x] = (uint8_t)t;
        }
      }
      p = line(p, ln, sizeof ln);
      ln_no++;
      if (strcmp(ln, "end")) FAIL("line %d: the grid must close with end", ln_no);
      gridded = true;
    } else if (!strcmp(w0, "spawn")) {
      if (sscanf(ln, "%*s %d %d", &a, &b) != 2 || m->nspawn >= MP_MAXSPAWN) FAIL("line %d: bad spawn", ln_no);
      m->spawn[m->nspawn][0] = (uint8_t)a, m->spawn[m->nspawn][1] = (uint8_t)b, m->nspawn++;
    } else if (!strcmp(w0, "core")) {
      if (sscanf(ln, "%*s %d %d", &a, &b) != 2) FAIL("line %d: bad core", ln_no);
      m->core[0] = (uint8_t)a, m->core[1] = (uint8_t)b, cored = true;
    } else if (!strcmp(w0, "slot")) {
      if (sscanf(ln, "%*s %d %d %d %d %d", &a, &b, &c, &d, &e) != 5 || m->nslot >= MP_MAXSLOT || c < 0 || c > 1 || d < 0 || d > 2 || e < 0 || e > 3)
        FAIL("line %d: bad slot", ln_no);
      m->slot[m->nslot++] = (mp_slot){ (uint8_t)a, (uint8_t)b, { 0, (uint8_t)c, (uint8_t)d, (uint8_t)e } };
    } else if (!strcmp(w0, "prop")) {
      if (sscanf(ln, "%*s %d %d %63s %d %d", &a, &b, w1, &c, &d) != 5 || m->nprop >= MP_MAXPROP) FAIL("line %d: bad prop", ln_no);
      int kind = !strcmp(w1, "decor") ? 0 : !strcmp(w1, "lamp") ? 1 : -1;
      if (kind < 0) FAIL("line %d: unknown prop %s", ln_no, w1);
      m->prop[m->nprop++] = (mp_prop){ (uint8_t)a, (uint8_t)b, (uint8_t)kind, (uint8_t)c, (uint8_t)(d & 3) };
    } else if (!strcmp(w0, "feature")) {
      mp_feat f;
      if (sscanf(ln, "%*s %63s %f %f %f %f", w1, &f.x, &f.y, &f.r, &f.h) != 5 || m->nfeat >= MP_MAXFEAT || !(f.r > 0.2f && f.r < 12)) FAIL("line %d: bad feature", ln_no);
      f.kind = !strcmp(w1, "rock") ? FEAT_ROCK : !strcmp(w1, "water") ? FEAT_WATER : 255;
      if (f.kind == 255) FAIL("line %d: unknown feature %s", ln_no, w1);
      m->feat[m->nfeat++] = f;
    } else if (!strcmp(w0, "route")) {
      if (m->nroute >= MP_MAXROUTE) FAIL("line %d: more than %d routes", ln_no, MP_MAXROUTE);
      mp_route *r = &m->route[m->nroute++];
      const char *s = ln + 5;
      int n = 0;
      float x, y, w;
      while (sscanf(s, " %f %f %f%n", &x, &y, &w, &n) == 3) {
        if (r->n >= MP_MAXWP) FAIL("line %d: more than %d waypoints", ln_no, MP_MAXWP);
        if (!(w > 0.3f && w < 16)) FAIL("line %d: bad route width", ln_no);
        r->wp[r->n][0] = x, r->wp[r->n][1] = y, r->wp[r->n][2] = w, r->n++;
        s += n;
      }
      if (r->n < 2) FAIL("line %d: a route needs two waypoints", ln_no);
    } else if (!strcmp(w0, "light")) {
      mp_light l;
      if (sscanf(ln, "%*s %d %d %f %f %f %f %f %f", &a, &b, &l.h, &l.r, &l.g, &l.b, &l.energy, &l.range) != 8 || m->nlight >= MP_MAXLIGHT) FAIL("line %d: bad light", ln_no);
      l.x = (uint8_t)a, l.y = (uint8_t)b;
      m->light[m->nlight++] = l;
    } else
      FAIL("line %d: unknown record %s", ln_no, w0);
  }
  if (!header) FAIL("empty file");
  if (m->theme < 0) FAIL("no theme");
  if (!gridded) FAIL("no grid");
  if (!m->nspawn) FAIL("no spawn");
  if (!cored) FAIL("no core");
  if (!m->nslot) FAIL("no slots");
  if (!m->nkind) FAIL("no kinds");
#define ONGRID(X, Y, WHAT) if (!mp_in(m, (X), (Y))) FAIL("%s at %d %d is off the grid", WHAT, (int)(X), (int)(Y))
  for (int i = 0; i < m->nspawn; i++) {
    int x = m->spawn[i][0], y = m->spawn[i][1];
    bool side = x == 0 || x == m->w - 1, top = y == 0 || y == m->h - 1;
    if (!side && !top) FAIL("a spawn at %d %d is not on the map's edge", x, y);
    int x2 = side ? x : x + 1, y2 = side ? y + 1 : y;
    if (!mp_in(m, x, y) || !mp_in(m, x2, y2) || m->t[y][x] != T_PATH || m->t[y2][x2] != T_PATH) FAIL("a spawn at %d %d does not open on two path tiles", x, y);
  }
  ONGRID(m->core[0], m->core[1], "the core");
  ONGRID(m->core[0] + 1, m->core[1] + 1, "the core's block");
  for (int k = 0; k < 4; k++)
    if (m->t[m->core[1] + k / 2][m->core[0] + k % 2] != T_PATH) FAIL("the core at %d %d does not stand on a 2 x 2 block of path", m->core[0], m->core[1]);
  for (int i = 0; i < m->nslot; i++) {
    ONGRID(m->slot[i].x, m->slot[i].y, "a slot");
    if (m->t[m->slot[i].y][m->slot[i].x] != T_SLOT) FAIL("a slot at %d %d is not an 'o' tile", m->slot[i].x, m->slot[i].y);
  }
  for (int i = 0; i < m->nprop; i++) ONGRID(m->prop[i].x, m->prop[i].y, "a prop");
  for (int i = 0; i < m->nlight; i++) ONGRID(m->light[i].x, m->light[i].y, "a light");
  return true;
}

void map_path(int num, char *out, int len) { snprintf(out, (size_t)len, "maps/%03d.cwtd", num); }
void map_bake_path(int num, char *out, int len) { snprintf(out, (size_t)len, "lightmaps/%03d.cwlm", num); }

bool map_load(int num, tdmap *m, int (*kind_by_name)(const char *)) {
  char path[64], err[160];
  map_path(num, path, sizeof path);
  size_t len = 0;
  char *buf = (char *)cw_vfs_read(path, &len);
  if (!buf) {
    cw_error("towers: cannot read %s", path);
    return false;
  }
  char *text = malloc(len + 1);
  memcpy(text, buf, len);
  text[len] = 0;
  free(buf);
  bool ok = map_parse(text, m, kind_by_name, err, sizeof err);
  if (!ok) cw_error("towers: %s: %s", path, err);
  free(text);
  return ok;
}

int map_count(void) {
  size_t len = 0;
  char *buf = (char *)cw_vfs_read("maps/count.txt", &len);
  if (!buf) return 0;
  char tmp[16] = { 0 };
  memcpy(tmp, buf, len < 15 ? len : 15);
  free(buf);
  return atoi(tmp);
}

uint64_t map_bake_hash(const char *text) {
  uint64_t h = 0xcbf29ce484222325ull;
  char ln[2048];
  const char *p = text;
  int grid = 0, rows = 0, hgt = 0;
  while (*p) {
    p = line(p, ln, sizeof ln);
    const char *s = ln;
    while (*s == ' ' || *s == '\t') s++;
    bool take = false;
    if (grid) {
      if (!strcmp(s, "end")) grid = 0;
      else take = rows++ < hgt;
      if (!take) continue;
    } else if (!strcmp(s, "grid")) {
      grid = 1;
      continue;
    } else if (!strncmp(s, "size ", 5)) {
      int w;
      sscanf(s + 5, "%d %d", &w, &hgt);
      take = true;
    } else
      take = !strncmp(s, "theme ", 6) || !strncmp(s, "prop ", 5) || !strncmp(s, "light ", 6) || !strncmp(s, "feature ", 8) || !strncmp(s, "slot ", 5) || !strncmp(s, "route ", 6);
    if (!take) continue;
    for (const char *c = s; *c; c++) h = (h ^ (uint8_t)*c) * 0x100000001b3ull;
    h = (h ^ (uint8_t)'\n') * 0x100000001b3ull;
  }
  return h;
}

/* ── the ground's shape (map.h) ─────────────────────────────────────── */

/* the routes as segments of their curves (the generator's Catmull-Rom at a fifth of a tile; a route's first segment
 * runs on out past its gate), binned by tile for the distance queries; rebuilt when another map asks */
typedef struct seg { float ax, ay, bx, by, wa, wb; int route; } seg;
enum { FG_PAD = 6 }; /* tiles the bins reach past the grid */
static struct {
  bool ok;
  int num, nroute, nslot, core0, core1;
  uint32_t seed;
  float wp0, wp1;
  seg *seg;
  int nseg, cap, gw, gh;
  int *start, *idx;
} F;

static void fields_add(float ax, float ay, float bx, float by, float wa, float wb, int route) {
  if (F.nseg == F.cap) F.cap = F.cap ? F.cap * 2 : 1024, F.seg = realloc(F.seg, sizeof(seg) * (size_t)F.cap);
  F.seg[F.nseg++] = (seg){ ax, ay, bx, by, wa, wb, route };
}
static void fields_prepare(const tdmap *m) {
  float wp0 = m->nroute ? m->route[0].wp[1][0] : 0, wp1 = m->nroute ? m->route[0].wp[m->route[0].n - 1][1] : 0;
  if (F.ok && F.num == m->num && F.seed == m->seed && F.nroute == m->nroute && F.nslot == m->nslot && F.core0 == m->core[0] && F.core1 == m->core[1] && F.wp0 == wp0 && F.wp1 == wp1) return;
  F.ok = true, F.num = m->num, F.seed = m->seed, F.nroute = m->nroute, F.nslot = m->nslot, F.core0 = m->core[0], F.core1 = m->core[1], F.wp0 = wp0, F.wp1 = wp1;
  F.nseg = 0;
  for (int r = 0; r < m->nroute; r++) {
    const mp_route *rt = &m->route[r];
    float px = 0, py = 0, pw = 0;
    bool have = false;
    /* the curve through P = [wp0, wp..., wpn-1] (its ends doubled), point by point */
    for (int i = 1; i < rt->n + 2 - 2; i++) {
      const float *p0 = rt->wp[CW_MAX(i - 2, 0)], *p1 = rt->wp[i - 1], *p2 = rt->wp[i], *p3 = rt->wp[CW_MIN(i + 1, rt->n - 1)];
      float len = sqrtf((p2[0] - p1[0]) * (p2[0] - p1[0]) + (p2[1] - p1[1]) * (p2[1] - p1[1]));
      int n = CW_MAX(1, (int)ceilf(len / 0.2f));
      for (int k = 0; k < n; k++) {
        float t = (float)k / (float)n, t2 = t * t, t3 = t2 * t, q[2];
        for (int a = 0; a < 2; a++) q[a] = 0.5f * (2 * p1[a] + (-p0[a] + p2[a]) * t + (2 * p0[a] - 5 * p1[a] + 4 * p2[a] - p3[a]) * t2 + (-p0[a] + 3 * p1[a] - 3 * p2[a] + p3[a]) * t3);
        float w = p1[2] + (p2[2] - p1[2]) * t;
        if (!have) { /* the gate's end runs on 10 tiles out the way it comes in */
          float dx = p2[0] - p1[0], dy = p2[1] - p1[1], l = sqrtf(dx * dx + dy * dy);
          if (l > 1e-4f) fields_add(q[0] - dx / l * 10, q[1] - dy / l * 10, q[0], q[1], w, w, r);
        } else
          fields_add(px, py, q[0], q[1], pw, w, r);
        px = q[0], py = q[1], pw = w, have = true;
      }
    }
    const float *e = rt->wp[rt->n - 1];
    if (have) fields_add(px, py, e[0], e[1], pw, e[2], r);
  }
  /* the bins: a segment in every tile within its half-width and 3 tiles of it */
  F.gw = m->w + 2 * FG_PAD, F.gh = m->h + 2 * FG_PAD;
  int ncell = F.gw * F.gh;
  free(F.start), free(F.idx);
  F.start = calloc((size_t)ncell + 1, sizeof(int));
  int *cnt = calloc((size_t)ncell, sizeof(int));
  for (int pass = 0; pass < 2; pass++) {
    if (pass) {
      for (int c = 0; c < ncell; c++) F.start[c + 1] = F.start[c] + cnt[c];
      F.idx = malloc(sizeof(int) * (size_t)CW_MAX(F.start[ncell], 1));
      memset(cnt, 0, sizeof(int) * (size_t)ncell);
    }
    for (int i = 0; i < F.nseg; i++) {
      const seg *s = &F.seg[i];
      float r = CW_MAX(s->wa, s->wb) * 0.5f + 3;
      int x0 = (int)floorf(CW_MIN(s->ax, s->bx) - r) + FG_PAD, x1 = (int)floorf(CW_MAX(s->ax, s->bx) + r) + FG_PAD;
      int y0 = (int)floorf(CW_MIN(s->ay, s->by) - r) + FG_PAD, y1 = (int)floorf(CW_MAX(s->ay, s->by) + r) + FG_PAD;
      for (int y = CW_MAX(y0, 0); y <= CW_MIN(y1, F.gh - 1); y++)
        for (int x = CW_MAX(x0, 0); x <= CW_MIN(x1, F.gw - 1); x++) {
          int c = y * F.gw + x;
          if (pass) F.idx[F.start[c] + cnt[c]] = i;
          cnt[c]++;
        }
    }
  }
  free(cnt);
}

static float smin(float a, float b, float k) {
  float h = CW_MAX(k - fabsf(a - b), 0.0f) / k;
  return CW_MIN(a, b) - h * h * k * 0.25f;
}
static float smax(float a, float b, float k) { return -smin(-a, -b, k); }

float map_floor_d(const tdmap *m, float x, float y, float *wide) {
  fields_prepare(m);
  float d = 3, wd = 2;
  if (!m->nroute) { /* no routes: the tiles' own steps */
    int tx = (int)floorf(x), ty = (int)floorf(y);
    d = mp_in(m, tx, ty) && m->t[ty][tx] == T_PATH ? -0.5f : 0.6f;
  } else {
    float best[MP_MAXROUTE], bw[MP_MAXROUTE];
    for (int r = 0; r < MP_MAXROUTE; r++) best[r] = 3, bw[r] = 2;
    int cx = (int)floorf(x) + FG_PAD, cy = (int)floorf(y) + FG_PAD;
    if (cx >= 0 && cy >= 0 && cx < F.gw && cy < F.gh) {
      int c = cy * F.gw + cx;
      for (int k = F.start[c]; k < F.start[c + 1]; k++) {
        const seg *s = &F.seg[F.idx[k]];
        float ex = s->bx - s->ax, ey = s->by - s->ay, l2 = ex * ex + ey * ey;
        float t = l2 > 1e-8f ? CW_CLAMP(((x - s->ax) * ex + (y - s->ay) * ey) / l2, 0.0f, 1.0f) : 0;
        float qx = s->ax + ex * t - x, qy = s->ay + ey * t - y, w = s->wa + (s->wb - s->wa) * t;
        float sd = sqrtf(qx * qx + qy * qy) - (w * 0.5f - MP_INSET);
        if (sd < best[s->route]) best[s->route] = sd, bw[s->route] = w;
      }
    }
    float bmin = 9;
    for (int r = 0; r < m->nroute; r++) {
      if (best[r] < bmin) bmin = best[r], wd = bw[r];
      d = r ? smin(d, best[r], 0.6f) : best[r];
    }
    /* the core's yard */
    float kx = x - ((float)m->core[0] + 1), ky = y - ((float)m->core[1] + 1);
    d = smin(d, sqrtf(kx * kx + ky * ky) - 1.2f, 0.6f);
  }
  if (d < 1.2f) /* every slot's pad on level ground */
    for (int i = 0; i < m->nslot; i++) {
      float sx = x - ((float)m->slot[i].x + 0.5f), sy = y - ((float)m->slot[i].y + 0.5f);
      if (sx * sx + sy * sy > 2.5f * 2.5f) continue;
      d = smax(d, MP_PAD_R + MP_CURB - sqrtf(sx * sx + sy * sy), 0.25f);
    }
  if (wide) *wide = wd;
  return d;
}

float map_plaza_d(const tdmap *m, float x, float y) {
  float w, d = map_floor_d(m, x, y, &w);
  return CW_MAX(d + MP_CURB + 0.1f, (4.4f - w) * 0.5f);
}

static float hnoise(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) & 0xffffff) / 16777216.0f;
}
static float vnoise2(float x, float y, uint32_t seed) {
  int ix = (int)floorf(x), iy = (int)floorf(y);
  float fx = x - (float)ix, fy = y - (float)iy;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = hnoise(ix, iy, seed), b = hnoise(ix + 1, iy, seed), c = hnoise(ix, iy + 1, seed), d = hnoise(ix + 1, iy + 1, seed);
  return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

static float bump(float t) { return t >= 1 ? 0 : 0.5f + 0.5f * cw_cos(3.14159265f * t); }
/* a feature's own numbers (0..1) from where it stands, so it takes the same shape wherever it is built or checked */
static float feat_n(const mp_feat *f, int k) { return hnoise((int)floorf(f->x * 64 + 0.5f), (int)floorf(f->y * 64 + 0.5f), 300u + (uint32_t)k); }

/* a pool's bowl (0 at its rim, 1 at its deepest): an organic shoreline, never a circle. The distance is stretched along
 * one axis, by lobes round the pool (three harmonics) and by a little noise, never shrunk, so the bowl stays inside its
 * radius (the generator's clearances hold), and squared, so the banks are short and the water wide */
static float pool_bowl(const mp_feat *f, float x, float y) {
  float dx = x - f->x, dy = y - f->y;
  if (dx * dx + dy * dy >= f->r * f->r) return 0;
  float a = feat_n(f, 0) * 3.14159265f, ca = cw_cos(a), sa = cw_sin(a);
  float u = dx * ca + dy * sa, v = (dy * ca - dx * sa) * (1.0f + 0.45f * feat_n(f, 1));
  float th = cw_atan2(v, u);
  float g = cw_sin(2 * th + 6.2831853f * feat_n(f, 2)) + 0.7f * cw_sin(3 * th + 6.2831853f * feat_n(f, 3)) + 0.45f * cw_sin(5 * th + 6.2831853f * feat_n(f, 4));
  float t = sqrtf(u * u + v * v) / f->r * (1 + 0.42f * (0.5f + 0.5f * g / 2.15f) + 0.14f * vnoise2(x * 1.3f, y * 1.3f, 77));
  return bump(t * t);
}

/* an outcrop: a block of rock with a polygon's outline (seven fracture planes round it, inside its radius), cliffs at its
 * edge, a roof of three tilted planes broken into strata (ledges and risers), and loose boulders round its foot */
/* the two largest of n values: the difference of the earlier one (by index) and the later one, so it changes sign
 * exactly where they cross (a crease between two planes) */
static float crease(const float *v, int n) {
  int i1 = 0, i2 = -1;
  for (int i = 1; i < n; i++)
    if (v[i] > v[i1]) i2 = i1, i1 = i;
    else if (i2 < 0 || v[i] > v[i2]) i2 = i;
  return i2 < 0 ? 1 : v[CW_MIN(i1, i2)] - v[CW_MAX(i1, i2)];
}
/* cr (when wanted): the outcrop's creases at the point as four fields that change sign across them (the cliff's top, the
 * strata's ledges and risers, the roof's planes, the walls' corners), so the terrain can be cut along them */
static float rock_h(const mp_feat *f, float x, float y, float *cr) {
  float dx = (x - f->x) / f->r, dy = (y - f->y) / f->r, d2 = dx * dx + dy * dy;
  if (cr) cr[0] = cr[1] = cr[2] = cr[3] = 1;
  if (d2 >= 1) return 0;
  float P = sqrtf(d2), a0 = feat_n(f, 10) * 6.2831853f, h = 0, wall[8], roof[4];
  for (int i = 0; i < 7; i++) {
    float a = a0 + (float)i * 0.8975979f + (feat_n(f, 11 + i) - 0.5f) * 0.45f, rho = 0.56f + 0.3f * feat_n(f, 20 + i);
    wall[i] = (dx * cw_cos(a) + dy * cw_sin(a)) / rho;
    P = CW_MAX(P, wall[i]);
  }
  wall[7] = sqrtf(d2);
  if (P < 1) {
    float cliff = CW_MIN((1 - P) / 0.13f, 1.0f), top = 1;
    roof[3] = -1;
    for (int j = 0; j < 3; j++) {
      float a = feat_n(f, 30 + j) * 6.2831853f, m = 0.45f + 0.65f * feat_n(f, 33 + j), c = 0.92f + 0.12f * feat_n(f, 36 + j);
      float t = c - m * (dx * cw_cos(a) + dy * cw_sin(a));
      top = CW_MIN(top, t), roof[j] = -t;
    }
    h = CW_MAX(CW_MIN(cliff, top), 0.0f) * f->h;
    float st = f->h / (float)(2 + (int)(feat_n(f, 40) * 2.99f)), u = h / st, fl = floorf(u), fr = u - fl;
    h += (st * (fl + CW_CLAMP((fr - 0.62f) / 0.38f, 0.0f, 1.0f)) - h) * 0.7f;
    if (cr) cr[0] = (1 - P) / 0.13f - top, cr[1] = fr < 0.62f ? -fr * (0.62f - fr) : (fr - 0.62f) * (1 - fr), cr[2] = crease(roof, 4), cr[3] = crease(wall, 8);
  }
  int nb = 3 + (int)(feat_n(f, 50) * 2.99f);
  for (int b = 0; b < nb; b++) {
    float rb = 0.11f + 0.08f * feat_n(f, 51 + b * 4), ab = feat_n(f, 52 + b * 4) * 6.2831853f, db = 1 - rb * 1.45f - 0.05f * feat_n(f, 53 + b * 4);
    float bx = (dx - cw_cos(ab) * db) / rb, by = (dy - cw_sin(ab) * db) / rb, pb = bx * bx + by * by, side[6];
    if (pb >= 1) continue;
    pb = side[5] = sqrtf(pb);
    for (int i = 0; i < 5; i++) {
      float a = ab + (float)i * 1.2566371f + feat_n(f, 54 + b * 4) * 1.2f, rho = 0.7f + 0.25f * feat_n(f, 80 + b * 5 + i);
      side[i] = (bx * cw_cos(a) + by * cw_sin(a)) / rho;
      pb = CW_MAX(pb, side[i]);
    }
    float hb = pb < 1 ? CW_MIN(f->h * 0.3f, 0.5f) * CW_MIN((1 - pb) / 0.4f, 1.0f) * (0.85f + 0.15f * by) : 0;
    if (hb > h) {
      h = hb;
      if (cr) cr[0] = (1 - pb) / 0.4f - 1, cr[1] = cr[2] = 1, cr[3] = crease(side, 6);
    }
  }
  return h;
}

void map_rock_creases(const tdmap *m, float x, float y, float cr[4]) {
  float best = 0, c[4];
  cr[0] = cr[1] = cr[2] = cr[3] = 1;
  for (int k = 0; k < m->nfeat; k++) {
    if (m->feat[k].kind != FEAT_ROCK) continue;
    float h = rock_h(&m->feat[k], x, y, c);
    if (h > best) best = h, memcpy(cr, c, sizeof c);
  }
}

void map_feat_at(const tdmap *m, float x, float y, float *rock, float *water) {
  *rock = 0, *water = 0;
  for (int k = 0; k < m->nfeat; k++) {
    const mp_feat *f = &m->feat[k];
    if (f->kind == FEAT_ROCK) *rock = CW_MAX(*rock, rock_h(f, x, y, NULL));
    else *water = CW_MAX(*water, pool_bowl(f, x, y));
  }
}

float map_ground_h(const tdmap *m, float x, float y) {
  float d = map_floor_d(m, x, y, NULL);
  float t = CW_CLAMP(d / MP_CURB, 0.0f, 1.0f), h = MP_GROUND_H * t * t * (3 - 2 * t);
  int tx = (int)floorf(x), ty = (int)floorf(y);
  if (mp_in(m, tx, ty) && m->t[ty][tx] == T_ROCK) h = CW_MAX(h, MP_ROCK_H);
  if (mp_in(m, tx, ty) && m->t[ty][tx] == T_WATER) h = CW_MIN(h, -0.4f);
  float rock, water;
  map_feat_at(m, x, y, &rock, &water);
  if (rock > 0) h += rock * (h / MP_GROUND_H); /* an outcrop stands on the raised ground */
  if (water > 0) h = CW_MIN(h, MP_GROUND_H - (MP_GROUND_H + 0.6f) * water);
  return h;
}

bool map_walkable(const tdmap *m, float x, float y) {
  int tx = (int)floorf(x), ty = (int)floorf(y);
  return mp_in(m, tx, ty) && m->t[ty][tx] == T_PATH && map_floor_d(m, x, y, NULL) <= 0;
}
