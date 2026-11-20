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
        if (n.lane >= LANES || n.ms < last) { cw_error("%s: charts.%s note %d is out of order or has no lane", path, DN[d], s->notes[d]); ok = false; break; }
        last = n.ms;
        s->chart[d][s->notes[d]++] = n;
      }
    }
  }
  if (ok) {
    s->hash = cw_hash32(s->id, strlen(s->id), 2166136261u);
    s->preview_ms = (int32_t)(s->length_s * 1000 / 3);
    for (int i = 0; i < s->sections; i++)
      if (s->section[i].kind == SEC_DROP) { s->preview_ms = s->section[i].ms; break; }
  } else if (!j.bad) {
    for (int d = 0; d < DIFFS; d++) cw_free(s->chart[d]);
    cw_free(s->beat_ms);
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
