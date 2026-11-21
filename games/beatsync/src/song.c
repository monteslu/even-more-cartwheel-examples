/* The songs: song.json read into the game's charts (docs/DESIGN.md "The song package").
 *
 * A small JSON reader (a tree of values in one allocation) is enough: song.json is written by the game's own
 * tooling, so a file it cannot read is logged and that song left out, never guessed at. */
#include "song.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/platform.h"

/* ── a small JSON reader ─────────────────────────────────────────────── */

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype;
typedef struct jv {
  jtype type;
  int count;      /* array / object members */
  int first;      /* index of the first member (members are a linked list through next) */
  int next;       /* next sibling, -1 at the end */
  const char *key; /* object member's key (NUL-terminated in the text) */
  double num;
  const char *str; /* string value (NUL-terminated in the text, escapes resolved in place) */
} jv;

typedef struct jparse {
  char *p;
  jv *v;
  int n, cap;
  bool bad;
} jparse;

static void ws(jparse *j) { while (*j->p == ' ' || *j->p == '\n' || *j->p == '\r' || *j->p == '\t') j->p++; }

static int jnew(jparse *j, jtype t) {
  if (j->n == j->cap) {
    int cap = j->cap ? j->cap * 2 : 1024;
    jv *nv = cw_alloc(sizeof(jv) * (size_t)cap, CW_MEM_GAME);
    if (j->v) memcpy(nv, j->v, sizeof(jv) * (size_t)j->n), cw_free(j->v);
    j->v = nv, j->cap = cap;
  }
  j->v[j->n] = (jv){ .type = t, .first = -1, .next = -1 };
  return j->n++;
}

/* a string in place: the closing quote becomes the NUL */
static char *jstring(jparse *j) {
  if (*j->p != '"') { j->bad = true; return NULL; }
  char *start = ++j->p, *out = start;
  while (*j->p && *j->p != '"') {
    if (*j->p == '\\') {
      j->p++;
      char c = *j->p++;
      *out++ = c == 'n' ? '\n' : c == 't' ? '\t' : c == 'u' ? '?' : c;
      if (c == 'u') for (int k = 0; k < 4 && *j->p; k++) j->p++;
    } else *out++ = *j->p++;
  }
  if (*j->p != '"') { j->bad = true; return NULL; }
  j->p++;
  *out = 0;
  return start;
}

static int jvalue(jparse *j, int depth) {
  ws(j);
  if (depth > 32) { j->bad = true; return -1; }
  char c = *j->p;
  if (c == '{' || c == '[') {
    bool obj = c == '{';
    int me = jnew(j, obj ? J_OBJ : J_ARR), last = -1;
    j->p++;
    ws(j);
    if (*j->p == (obj ? '}' : ']')) { j->p++; return me; }
    for (;;) {
      ws(j);
      const char *key = NULL;
      if (obj) {
        key = jstring(j);
        ws(j);
        if (j->bad || *j->p != ':') { j->bad = true; return -1; }
        j->p++;
      }
      int child = jvalue(j, depth + 1);
      if (j->bad) return -1;
      j->v[child].key = key;
      if (last < 0) j->v[me].first = child;
      else j->v[last].next = child;
      last = child;
      j->v[me].count++;
      ws(j);
      if (*j->p == ',') { j->p++; continue; }
      if (*j->p == (obj ? '}' : ']')) { j->p++; return me; }
      j->bad = true;
      return -1;
    }
  }
  if (c == '"') {
    int me = jnew(j, J_STR);
    const char *s = jstring(j);
    j->v[me].str = s;
    return me;
  }
  if (!strncmp(j->p, "true", 4) || !strncmp(j->p, "false", 5)) {
    int me = jnew(j, J_BOOL);
    j->v[me].num = *j->p == 't';
    j->p += *j->p == 't' ? 4 : 5;
    return me;
  }
  if (!strncmp(j->p, "null", 4)) { j->p += 4; return jnew(j, J_NULL); }
  char *end;
  double d = strtod(j->p, &end);
  if (end == j->p) { j->bad = true; return -1; }
  j->p = end;
  int me = jnew(j, J_NUM);
  j->v[me].num = d;
  return me;
}

static const jv *jget(const jparse *j, const jv *o, const char *key) {
  if (!o || o->type != J_OBJ) return NULL;
  for (int i = o->first; i >= 0; i = j->v[i].next)
    if (j->v[i].key && !strcmp(j->v[i].key, key)) return &j->v[i];
  return NULL;
}
static double jnum(const jv *v, double def) { return v && v->type == J_NUM ? v->num : def; }
static void jstr(const jv *v, char *out, size_t cap) { snprintf(out, cap, "%s", v && v->type == J_STR ? v->str : ""); }

/* ── the songs ───────────────────────────────────────────────────────── */

static song SONGS[MAX_SONGS];
static int nsongs;

static section_kind kind_of(const char *s) {
  return !strcmp(s, "intro") ? SEC_INTRO : !strcmp(s, "build") ? SEC_BUILD : !strcmp(s, "drop") ? SEC_DROP
       : !strcmp(s, "break") ? SEC_BREAK : !strcmp(s, "outro") ? SEC_OUTRO : SEC_OTHER;
}

/* a JSON array of numbers into floats (times k), allocated; returns the count (0: no array) */
static int jfloats(const jparse *j, const jv *a, float k, float **out) {
  *out = NULL;
  if (!a || a->type != J_ARR || !a->count) return 0;
  float *f = cw_alloc(sizeof(float) * (size_t)a->count, CW_MEM_GAME);
  int n = 0;
  for (int i = a->first; i >= 0; i = j->v[i].next) f[n++] = (float)(j->v[i].num * k);
  *out = f;
  return n;
}

static float wrap180(float d) { return d - 360.0f * floorf((d + 180.0f) / 360.0f); }

/* song.json "look.other" (tools/lib/look.mjs) into what the land draws: loudness against the song's own range,
 * smoothed over a beat either side; the harmony held from change to change (each stretch's mean, weighted by how
 * clearly it sounds) and spread over the song's own range of harmonies, so a song that stays near one key still
 * shows its chord moves as colour */
static void load_look(const jparse *j, const jv *o, song *s) {
  song_look *L = &s->look;
  memset(L, 0, sizeof *L);
  if (!o || o->type != J_OBJ) return;
  float *level, *hue, *bright, *tonal, *changes, *stabs, *band;
  int n = jfloats(j, jget(j, o, "level"), 1.0f / 99, &level);
  int nh = jfloats(j, jget(j, o, "hue"), 1, &hue);
  int nb = jfloats(j, jget(j, o, "bright"), 1.0f / 99, &bright);
  int nt = jfloats(j, jget(j, o, "tonal"), 1.0f / 99, &tonal);
  int nc = jfloats(j, jget(j, o, "changes"), 1, &changes);
  int ns = jfloats(j, jget(j, o, "stabs"), 1, &stabs);
  int nd = jfloats(j, jget(j, o, "bands"), 1.0f / 50, &band);
  if (n < 2 || nh != n || nb != n || nt != n) {
    if (n || nh || nb || nt) cw_error("songs/%s: look.other's level, hue, bright and tonal must each have a value a beat", s->id);
    cw_free(level), cw_free(hue), cw_free(bright), cw_free(tonal), cw_free(changes), cw_free(stabs), cw_free(band);
    return;
  }
  L->beats = n;
  L->bright = bright, L->tonal = tonal;
  L->band = nd == n * 6 ? band : (cw_free(band), NULL);
  L->change = cw_alloc((size_t)n, CW_MEM_GAME);
  memset(L->change, 0, (size_t)n);
  for (int i = 0; i < nc; i++)
    if (changes[i] >= 0 && changes[i] < n) L->change[(int)changes[i]] = 1;
  cw_free(changes);
  /* the loudness against the song's quiet (10th percentile of the sounding beats) and loud (95th) */
  float sorted[1024];
  int m = 0;
  for (int i = 0; i < n && m < 1024; i++) if (level[i] > 0.05f) sorted[m++] = level[i];
  for (int a = 1; a < m; a++) for (int b = a; b > 0 && sorted[b - 1] > sorted[b]; b--) { float t = sorted[b]; sorted[b] = sorted[b - 1]; sorted[b - 1] = t; }
  float lo = m ? sorted[m / 10] : 0, hi = m ? sorted[(m * 95) / 100 < m ? (m * 95) / 100 : m - 1] : 1;
  if (hi - lo < 0.05f) lo = hi - 0.05f;
  L->height = cw_alloc(sizeof(float) * (size_t)n, CW_MEM_GAME);
  for (int i = 0; i < n; i++) {
    float a = level[i > 0 ? i - 1 : i], b = level[i], c = level[i + 1 < n ? i + 1 : i];
    float v = 0.25f * a + 0.5f * b + 0.25f * c;
    L->height[i] = b <= 0.01f ? 0 : fminf(1, fmaxf(0.05f, (v - lo) / (hi - lo)));
  }
  cw_free(level);
  /* the harmony held from change to change */
  L->hue = hue;
  for (int i0 = 0; i0 < n;) {
    int i1 = i0 + 1;
    while (i1 < n && !L->change[i1]) i1++;
    float x = 0, y = 0;
    for (int i = i0; i < i1; i++) {
      float w = tonal[i] + 0.01f, r = hue[i] * 3.14159265f / 180;
      x += w * cosf(r), y += w * sinf(r);
    }
    float h = atan2f(y, x) * 180 / 3.14159265f;
    for (int i = i0; i < i1; i++) hue[i] = h;
    i0 = i1;
  }
  /* spread over the song's own range: its mean harmony at 220 (blue-violet), its moves wider apart */
  float x = 0, y = 0;
  for (int i = 0; i < n; i++) x += cosf(hue[i] * 3.14159265f / 180), y += sinf(hue[i] * 3.14159265f / 180);
  float mean = atan2f(y, x) * 180 / 3.14159265f, dev = 0;
  for (int i = 0; i < n; i++) dev += fabsf(wrap180(hue[i] - mean));
  dev /= (float)n;
  float k = 55.0f / fmaxf(dev, 6.0f);
  for (int i = 0; i < n; i++) hue[i] = 220 + fminf(150, fmaxf(-150, wrap180(hue[i] - mean) * k));
  /* the stabs */
  L->stabs = ns / 2;
  if (L->stabs) {
    L->stab_beat = cw_alloc(sizeof(float) * (size_t)L->stabs, CW_MEM_GAME);
    L->stab_str = cw_alloc((size_t)L->stabs, CW_MEM_GAME);
    for (int i = 0; i < L->stabs; i++) L->stab_beat[i] = stabs[2 * i] / 100, L->stab_str[i] = (uint8_t)stabs[2 * i + 1];
  }
  cw_free(stabs);
}

static bool load_one(const char *id, song *s) {
  char path[160];
  snprintf(path, sizeof path, "songs/%s/song.json", id);
  size_t len;
  char *text = cw_asset_load(path, &len);
  if (!text) { cw_error("%s: missing (the build lists it: rebuild after removing a song)", path); return false; }
  jparse j = { .p = text };
  int root = jvalue(&j, 0);
  bool ok = !j.bad && root >= 0 && j.v[root].type == J_OBJ;
  const jv *R = ok ? &j.v[root] : NULL;
  if (ok && jnum(jget(&j, R, "format"), 0) != 1) { cw_error("%s: format is not 1", path); ok = false; }
  if (ok) {
    memset(s, 0, sizeof *s);
    snprintf(s->id, sizeof s->id, "%s", id);
    jstr(jget(&j, R, "title"), s->title, sizeof s->title);
    if (!s->title[0]) snprintf(s->title, sizeof s->title, "%s", id);
    jstr(jget(&j, R, "artist"), s->artist, sizeof s->artist);
    s->bpm = (float)jnum(jget(&j, R, "bpm"), 120);
    s->length_s = (float)jnum(jget(&j, R, "length_s"), 0);
    const jv *tr = jget(&j, R, "tracks");
    static const char *const TN[TRACKS] = { "kick", "clap", "bass", "other" };
    for (int t = 0; t < TRACKS && ok; t++) {
      char f[64];
      jstr(jget(&j, tr, TN[t]), f, sizeof f);
      if (!f[0]) { cw_error("%s: no tracks.%s", path, TN[t]); ok = false; }
      snprintf(s->track[t], sizeof s->track[t], "songs/%s/%s", id, f);
    }
    const jv *b = jget(&j, R, "beats");
    if (ok && b && b->type == J_ARR && b->count > 1) {
      s->beat_ms = cw_alloc(sizeof(int32_t) * (size_t)b->count, CW_MEM_GAME);
      for (int i = b->first; i >= 0; i = j.v[i].next) s->beat_ms[s->beats++] = (int32_t)lround(j.v[i].num * 1000);
    } else if (ok) { cw_error("%s: no beats", path); ok = false; }
    s->downbeat = (int)jnum(jget(&j, R, "downbeat"), 0);
    const jv *sec = jget(&j, R, "sections");
    if (sec && sec->type == J_ARR)
      for (int i = sec->first; i >= 0 && s->sections < MAX_SECTIONS; i = j.v[i].next) {
        char k[16];
        jstr(jget(&j, &j.v[i], "kind"), k, sizeof k);
        s->section[s->sections].ms = (int32_t)lround(jnum(jget(&j, &j.v[i], "t"), 0) * 1000);
        s->section[s->sections++].kind = kind_of(k);
      }
    const jv *look = jget(&j, R, "look");
    if (ok) load_look(&j, look ? jget(&j, look, "other") : NULL, s);
    const jv *ch = jget(&j, R, "charts");
    static const char *const DN[DIFFS] = { "easy", "medium", "hard" };
    for (int d = 0; d < DIFFS && ok; d++) {
      const jv *a = jget(&j, ch, DN[d]);
      if (!a || a->type != J_ARR || !a->count) { cw_error("%s: charts.%s is empty", path, DN[d]); ok = false; break; }
      s->chart[d] = cw_alloc(sizeof(note) * (size_t)a->count, CW_MEM_GAME);
      int32_t last = -1000000;
      for (int i = a->first; i >= 0; i = j.v[i].next) {
        note n = { (int32_t)lround(jnum(jget(&j, &j.v[i], "t"), 0) * 1000), (int16_t)jnum(jget(&j, &j.v[i], "step"), 0),
                   (uint8_t)jnum(jget(&j, &j.v[i], "lane"), 0), (uint8_t)(jnum(jget(&j, &j.v[i], "o"), 0) != 0) };
        if (n.lane >= SOLO_LANES || n.ms < last) { cw_error("%s: charts.%s note %d is out of order or has no lane", path, DN[d], s->notes[d]); ok = false; break; }
        last = n.ms;
        s->chart[d][s->notes[d]++] = n;
      }
    }
    /* the two-lane charts: each pair's notes on its own two lanes */
    const jv *pr = jget(&j, R, "pairs");
    if (ok && !pr) { cw_error("%s: no two-lane charts (\"pairs\"): re-chart it, tools/make-song.mjs --charts-only", path); ok = false; }
    for (int p = 0; p < PAIRS && ok; p++) {
      int pl[2];
      pair_lanes(p, pl);
      const jv *pc = jget(&j, pr, pair_name(p));
      for (int d = 0; d < DIFFS && ok; d++) {
        const jv *a = pc ? jget(&j, pc, DN[d]) : NULL;
        if (!a || a->type != J_ARR || !a->count) { cw_error("%s: pairs.%s.%s is empty", path, pair_name(p), DN[d]); ok = false; break; }
        s->pair_chart[p][d] = cw_alloc(sizeof(note) * (size_t)a->count, CW_MEM_GAME);
        int32_t last = -1000000;
        for (int i = a->first; i >= 0; i = j.v[i].next) {
          note n = { (int32_t)lround(jnum(jget(&j, &j.v[i], "t"), 0) * 1000), (int16_t)jnum(jget(&j, &j.v[i], "step"), 0),
                     (uint8_t)jnum(jget(&j, &j.v[i], "lane"), 0), 0 };
          if ((n.lane != pl[0] && n.lane != pl[1]) || n.ms < last) { cw_error("%s: pairs.%s.%s note %d is out of order or off its pair", path, pair_name(p), DN[d], s->pair_notes[p][d]); ok = false; break; }
          last = n.ms;
          s->pair_chart[p][d][s->pair_notes[p][d]++] = n;
        }
      }
    }
  }
  if (ok) {
    s->hash = cw_hash32(s->id, strlen(s->id), 2166136261u);
    s->preview_ms = (int32_t)(s->length_s * 1000 / 3);
    for (int i = 0; i < s->sections; i++)
      if (s->section[i].kind == SEC_DROP) { s->preview_ms = s->section[i].ms; break; }
  } else if (!j.bad) {
    for (int d = 0; d < DIFFS; d++) {
      cw_free(s->chart[d]);
      for (int p = 0; p < PAIRS; p++) cw_free(s->pair_chart[p][d]);
    }
    cw_free(s->beat_ms);
    song_look *L = &s->look;
    cw_free(L->height), cw_free(L->hue), cw_free(L->bright), cw_free(L->tonal), cw_free(L->change), cw_free(L->band);
    cw_free(L->stab_beat), cw_free(L->stab_str);
  } else cw_error("%s: not valid JSON", path);
  cw_free(j.v);
  free(text);
  return ok;
}

int songs_load(void) {
  nsongs = 0;
  for (int i = 0; i < SONG_COUNT && nsongs < MAX_SONGS; i++)
    if (load_one(SONG_IDS[i], &SONGS[nsongs])) nsongs++;
  return nsongs;
}
int songs_count(void) { return nsongs; }
const song *song_get(int i) { return i >= 0 && i < nsongs ? &SONGS[i] : NULL; }
const char *pair_name(int p) { return p == PAIR_GROOVE ? "groove" : "drums"; }
void pair_lanes(int p, int out[2]) { out[0] = p == PAIR_GROOVE ? LANE_BASS : LANE_KICK, out[1] = p == PAIR_GROOVE ? LANE_SYNTH : LANE_CLAP; }
const char *diff_name(int d) { static const char *const N[DIFFS] = { "Easy", "Medium", "Hard" }; return d >= 0 && d < DIFFS ? N[d] : "?"; }

float song_beat_at(const song *s, float ms) {
  if (!s || s->beats < 2) return 0;
  int lo = 0, hi = s->beats - 1;
  if (ms <= s->beat_ms[0]) return (ms - s->beat_ms[0]) / (float)(s->beat_ms[1] - s->beat_ms[0]);
  if (ms >= s->beat_ms[hi]) return hi + (ms - s->beat_ms[hi]) / (float)(s->beat_ms[hi] - s->beat_ms[hi - 1]);
  while (hi - lo > 1) {
    int mid = (lo + hi) / 2;
    if (s->beat_ms[mid] <= ms) lo = mid;
    else hi = mid;
  }
  return lo + (ms - s->beat_ms[lo]) / (float)(s->beat_ms[lo + 1] - s->beat_ms[lo]);
}

void song_look_at(const song *s, float beat, float *height, float *hue, float *bright, float *tonal) {
  const song_look *L = s ? &s->look : NULL;
  if (!L || !L->beats) { *height = 0.3f, *hue = 220, *bright = 0.3f, *tonal = 0.3f; return; }
  int i = (int)floorf(beat);
  i = i < 0 ? 0 : i >= L->beats ? L->beats - 1 : i;
  *height = beat < 0 || beat >= (float)L->beats ? 0 : L->height[i];
  *hue = L->hue[i], *bright = L->bright[i], *tonal = L->tonal[i];
}

int song_stab_in(const song *s, float b0, float b1) {
  const song_look *L = s ? &s->look : NULL;
  if (!L || !L->stabs) return 0;
  int lo = 0, hi = L->stabs;
  while (lo < hi) { int m = (lo + hi) / 2; if (L->stab_beat[m] < b0) lo = m + 1; else hi = m; }
  int best = 0;
  for (int i = lo; i < L->stabs && L->stab_beat[i] < b1; i++) if (L->stab_str[i] > best) best = L->stab_str[i];
  return best;
}

bool song_change_in(const song *s, float b0, float b1) {
  const song_look *L = s ? &s->look : NULL;
  if (!L || !L->beats) return false;
  for (int i = (int)ceilf(b0); (float)i < b1; i++)
    if (i >= 0 && i < L->beats && L->change[i]) return true;
  return false;
}

void song_bands_at(const song *s, float beat, float out[3]) {
  const song_look *L = s ? &s->look : NULL;
  if (!L || !L->band || beat < 0 || beat >= (float)L->beats) { out[0] = out[1] = out[2] = beat < 0 || !L || !L->beats ? 0.4f : 0; return; }
  int h = (int)floorf(beat * 2);
  if (h >= L->beats * 2) h = L->beats * 2 - 1;
  for (int k = 0; k < 3; k++) out[k] = L->band[h * 3 + k];
}
