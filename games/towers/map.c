/* towers maps: the .cwtd parser (format in map.h). */
#include "map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
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
  char ln[256];
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
  char ln[256];
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
      take = !strncmp(s, "theme ", 6) || !strncmp(s, "prop ", 5) || !strncmp(s, "light ", 6) || !strncmp(s, "feature ", 8);
    if (!take) continue;
    for (const char *c = s; *c; c++) h = (h ^ (uint8_t)*c) * 0x100000001b3ull;
    h = (h ^ (uint8_t)'\n') * 0x100000001b3ull;
  }
  return h;
}
