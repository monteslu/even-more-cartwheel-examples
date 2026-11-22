/* The styles (styles.h): the table, the scheduler and the blend.
 *
 * Every fade runs from one style (from) to the next (cur) with t from 0 to 1, eased (smoothstep), through a flash
 * meter (fl.style: the flash check's `flash_strobe` control strobes it). What fades:
 *   - the ASCII pass's share of the picture (ascii_set_mix: the characters thin out as the plain scene shows through);
 *   - each backdrop's weight (backdrop.c), the old one's falling as the new one's rises;
 *   - the field's colours and params (club_set_style: the palette, the land, the roads, the sky);
 *   - the bloom.
 * A fade never starts while another runs: a change asked for mid-fade waits for it to end. */
#include "styles.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/debug.h"

#include "ascii.h"
#include "backdrop.h"
#include "club.h"
#include "flash_meter.h"

typedef struct style_def {
  const char *id, *title;
  float ascii;   /* the characters' share: 1 or 0 */
  int backdrop;  /* BD_* */
  club_style look;
  float bloom, bloom_threshold;
  bool big;      /* what a drop asks for */
  bool held;     /* built but kept out of the rotation and the setting (an engine gap stands in its way; `style <id>` still shows it) */
} style_def;

/* the palettes: each set apart from its backdrop, so the notes read against it (the readability gate measures it) */
#define ICE { { 0.3f, 0.85f, 1 }, { 0.9f, 0.95f, 1 }, { 0.6f, 0.5f, 1 }, { 0.45f, 1, 0.8f } }
#define EMBER { { 1, 0.5f, 0.15f }, { 1, 0.88f, 0.35f }, { 1, 0.3f, 0.6f }, { 0.6f, 1, 0.4f } }
#define SYNTH { { 0.2f, 0.95f, 1 }, { 1, 0.9f, 0.35f }, { 0.55f, 1, 0.35f }, { 1, 0.55f, 0.2f } }
#define PHOSPHOR { { 0.25f, 1, 0.35f }, { 0.65f, 1, 0.25f }, { 0.2f, 1, 0.7f }, { 0.45f, 1, 0.45f } }
#define STAGE { { 1, 0.55f, 0.12f }, { 1, 0.95f, 0.65f }, { 1, 0.28f, 0.35f }, { 0.4f, 0.8f, 1 } }

static const style_def STYLES[STYLE_COUNT] = {
  [STYLE_NEON] = { "neon", "Neon characters", 1, BD_NONE,
                   { .land_body = 1, .land_alpha = 1, .sky = 1, .burst = 0.65f, .flow = 1, .note_light = 1, .pocket = 1 }, 0.08f, 1.0f, false },
  [STYLE_CLEAN] = { "clean", "Clean", 0, BD_NONE,
                    { .land_body = 0.4f, .land_alpha = 1, .ridge = 0.5f, .sky = 0, .burst = 0.45f, .flow = 1, .note_light = 0.55f, .pocket = 1 }, 0.22f, 0.8f, false },
  [STYLE_FIRE] = { "fire", "Fire", 0, BD_FIRE,
                   { .pal = 1, .lane = ICE, .land_tint = 0.7f, .land_rgb = { 1, 0.35f, 0.1f }, .land_body = 0.1f, .land_alpha = 0, .ridge = 0, .plate = 0.88f, .burst = 0.5f, .flow = 1, .note_light = 0.8f, .pocket = 1 },
                   0.25f, 0.8f, true },
  [STYLE_WATER] = { "water", "Night sea", 0, BD_WATER,
                    { .pal = 1, .lane = EMBER, .land_tint = 0.8f, .land_rgb = { 0.35f, 0.6f, 1 }, .land_body = 0.12f, .land_alpha = 0, .ridge = 0, .plate = 0.8f, .burst = 0.5f, .flow = 1, .note_light = 0.8f, .pocket = 1 },
                    0.22f, 0.8f, false, true /* held: Water3D draws black without a ProceduralSky (engine gap, reported) */ },
  [STYLE_GRID] = { "grid", "Synthwave grid", 0, BD_GRID,
                   { .pal = 1, .lane = SYNTH, .land_tint = 1, .land_rgb = { 1, 0.2f, 0.85f }, .land_body = 0.1f, .land_alpha = 0.45f, .ridge = 0.85f, .plate = 0.85f, .burst = 0.5f, .flow = 1, .note_light = 0.8f, .pocket = 1 },
                   0.22f, 0.8f, true },
  [STYLE_TERMINAL] = { "terminal", "Terminal green", 1, BD_NONE,
                       { .pal = 1, .lane = PHOSPHOR, .land_tint = 1, .land_rgb = { 0.3f, 1, 0.4f }, .land_body = 0.36f, .land_alpha = 1, .sky = 1, .burst = 0.65f, .flow = 1, .note_light = 0.8f, .pocket = 1 },
                       0.08f, 1.0f, false },
  [STYLE_LASERS] = { "lasers", "Club lasers", 0, BD_LASERS,
                     { .pal = 1, .lane = STAGE, .land_tint = 0.5f, .land_rgb = { 0.55f, 0.3f, 1 }, .land_body = 0.1f, .land_alpha = 0, .ridge = 0, .plate = 0.88f, .burst = 0.5f, .flow = 1, .note_light = 0.8f, .pocket = 1 },
                     0.28f, 0.8f, true },
};

const char *style_title(int i) { return i >= 0 && i < STYLE_COUNT ? STYLES[i].title : "All styles"; }
const char *style_id(int i) { return i >= 0 && i < STYLE_COUNT ? STYLES[i].id : "auto"; }
bool style_held(int i) { return i >= 0 && i < STYLE_COUNT && STYLES[i].held; }

#define PHRASE_BEATS 32 /* 8 bars of 4 */
#define FADE_BEATS 2.0f
#define DROP_FADE_BEATS 1.0f
#define MIN_FADE_S 0.5f

static struct {
  int32_t cur, from, mode, changes, cut, readctl, pending, prepare, tour, tour_ticks;
  float t, fade_s, energy;
  float bd_w[BD_COUNT];
  /* the song being scheduled */
  const song *s;
  float beat, last_beat, hold_from, bpm, tempo;
  int32_t phrase;
  uint8_t order[STYLE_COUNT];
  uint32_t played;
  cw_node post;
  float bloom_set, thr_set;
  bool built;
} S;
static flash_meter fl_style;

/* the song's order: neon first (the game's own look, the title's), then the rest shuffled by the song's seed */
static void shuffle(uint32_t seed) {
  for (int i = 0; i < STYLE_COUNT; i++) S.order[i] = (uint8_t)i;
  uint32_t x = seed ? seed : 0x9e3779b9u;
  for (int i = STYLE_COUNT - 1; i > 1; i--) {
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    int j = 1 + (int)(x % (uint32_t)i); /* 1..i: neon stays first */
    uint8_t k = S.order[i]; S.order[i] = S.order[j]; S.order[j] = k;
  }
}

static float beats_to_s(float beats) {
  float bpm = S.bpm > 0 ? S.bpm : 120, tempo = S.tempo > 0 ? S.tempo : 1;
  return fmaxf(MIN_FADE_S, beats * 60.0f / (bpm * tempo));
}

static void begin(int to, float beats) {
  if (to == S.cur && S.t >= 1) return;
  if (S.t < 1) { S.pending = to; return; } /* one fade at a time */
  S.from = S.cur, S.cur = to, S.changes++;
  S.fade_s = beats_to_s(beats);
  S.t = S.cut ? 1 : 0; /* the cut: the flash check's control (a one-frame change) */
  S.played |= 1u << to;
  S.hold_from = S.beat;
}

/* the next style of the song's order not played yet (a drop: a big one if one is left) */
static int next_style(bool drop) {
  uint32_t all = 0;
  for (int i = 0; i < STYLE_COUNT; i++) all |= (uint32_t)!STYLES[i].held << i;
  if (((S.played | (1u << S.cur)) & all) == all) S.played = 1u << S.cur; /* every one played: round again */
  int pick = -1;
  for (int i = 0; i < STYLE_COUNT && pick < 0; i++) {
    int k = S.order[i];
    if (k != S.cur && !STYLES[k].held && !(S.played >> k & 1) && (!drop || STYLES[k].big)) pick = k;
  }
  for (int i = 0; i < STYLE_COUNT && pick < 0; i++) {
    int k = S.order[i];
    if (k != S.cur && !STYLES[k].held && !(S.played >> k & 1)) pick = k;
  }
  return pick < 0 ? STYLE_NEON : pick;
}

static float smooth01(float x) { x = fminf(1, fmaxf(0, x)); return x * x * (3 - 2 * x); }

void styles_tick(const song *s, double ms, float tempo, float dt) {
  S.tempo = tempo;
  if (S.tour && ++S.tour_ticks % 75 == 0 && S.t >= 1) begin((S.cur + 1) % STYLE_COUNT, FADE_BEATS); /* (held ones too: their draw states) */ /* the programs gate's tour */
  if (S.t < 1) {
    S.t = fminf(1, S.t + dt / fmaxf(S.fade_s, 1e-3f));
    if (S.t >= 1) S.from = S.cur;
  }
  if (S.t >= 1 && S.pending >= 0) {
    int p = S.pending;
    S.pending = -1;
    begin(p, FADE_BEATS);
  }
  S.prepare = 0;
  if (!s) { S.s = NULL; return; }
  float beat = song_beat_at(s, (float)ms);
  S.bpm = s->bpm;
  /* the music's loudness, slowly (a backdrop breathes with it; never a pulse on the beat) */
  float h, hue, br, ton;
  song_look_at(s, beat, &h, &hue, &br, &ton);
  S.energy += (h - S.energy) * fminf(1, dt / 1.5f);
  int phrase = (int)floorf((beat - (float)s->downbeat) / PHRASE_BEATS);
  if (s != S.s || beat < S.last_beat - 2) { /* a new song, or this one again from the top */
    S.s = s, S.beat = beat, S.last_beat = beat, S.phrase = phrase, S.played = 0;
    shuffle(s->hash);
    if (S.mode < 0 && !S.tour) begin(STYLE_NEON, FADE_BEATS);
    S.hold_from = beat;
    return;
  }
  S.beat = beat;
  if (S.tour) {
  } else if (S.mode >= 0) {
    begin(S.mode, FADE_BEATS);
  } else {
    bool drop = false;
    for (int i = 0; i < s->sections; i++) {
      if (s->section[i].kind != SEC_DROP) continue;
      float db = song_beat_at(s, (float)s->section[i].ms);
      if (db > S.last_beat && db <= beat && beat - S.hold_from >= 14) drop = true;
    }
    if (drop) begin(next_style(true), DROP_FADE_BEATS);
    else if (phrase > S.phrase && beat - S.hold_from >= PHRASE_BEATS - 2) begin(next_style(false), FADE_BEATS);
    /* the characters' scene renders at full size from a beat and a half before a change could come */
    float to_phrase = (float)(phrase + 1) * PHRASE_BEATS + (float)s->downbeat - beat;
    if (to_phrase < 1.5f && beat - S.hold_from >= PHRASE_BEATS - 4) S.prepare = 1;
    for (int i = 0; i < s->sections; i++) {
      if (s->section[i].kind != SEC_DROP) continue;
      float db = song_beat_at(s, (float)s->section[i].ms) - beat;
      if (db > 0 && db < 1.5f) S.prepare = 1;
    }
  }
  S.phrase = phrase;
  S.last_beat = beat;
  if (S.pending >= 0 || S.t < 1) S.prepare = 1;
}

void styles_apply(float vis_t) {
  float k = smooth01(flash_meter_feed(&fl_style, S.t));
  const style_def *a = &STYLES[S.from], *b = &STYLES[S.cur];
  club_style la = a->look, lb = b->look;
  if (S.readctl) la.plate = lb.plate = la.pocket = lb.pocket = 0; /* the readability gate's control: no roads or pockets, the notes dimmed (club.c) */
  club_set_style(&la, &lb, k);
  club_set_note_gain(S.readctl ? 0.2f : 1);
  ascii_set_mix(a->ascii + (b->ascii - a->ascii) * k, S.prepare != 0);
  for (int i = 0; i < BD_COUNT; i++) S.bd_w[i] = 0;
  S.bd_w[a->backdrop] += 1 - k;
  S.bd_w[b->backdrop] += k;
  backdrop_update(S.bd_w, S.s ? S.beat : vis_t * 2, vis_t, S.energy);
  float bloom = a->bloom + (b->bloom - a->bloom) * k, thr = a->bloom_threshold + (b->bloom_threshold - a->bloom_threshold) * k;
  if (S.post.id && (fabsf(bloom - S.bloom_set) > 1e-4f || fabsf(thr - S.thr_set) > 1e-4f)) {
    cw_node_set(S.post, "bloom_intensity", cw_val_float(bloom));
    cw_node_set(S.post, "bloom_threshold", cw_val_float(thr));
    S.bloom_set = bloom, S.thr_set = thr;
  }
}

void styles_set_mode(int mode) {
  S.mode = mode >= 0 && mode < STYLE_COUNT ? mode : -1;
  if (S.mode >= 0) begin(S.mode, FADE_BEATS);
}
int styles_mode(void) { return S.mode; }

static int style_arg(const char *a) {
  for (int i = 0; i < STYLE_COUNT; i++)
    if (!strcmp(a, STYLES[i].id)) return i;
  char *end;
  long v = strtol(a, &end, 10);
  return *end == 0 && v >= 0 && v < STYLE_COUNT ? (int)v : -2;
}

/* `style`: the state; `style <name|index>`: that style only (it fades in); `style auto`: every style with the song;
 * `style next`: the next change now; `style cut 1|0`: changes as one-frame cuts (the flash check's control);
 * `style readctl 1|0`: no roads or pockets and dim notes (the readability gate's control); `style tour`: every style in turn (the
 * programs gate: every backdrop's draw states at every preset) */
static void cmd_style(int argc, const char **argv, cw_reply *r) {
  if (argc >= 2) {
    if (!strcmp(argv[1], "auto")) S.mode = -1;
    else if (!strcmp(argv[1], "next")) { /* `style next drop`: as a drop changes it (a big style, a one-beat fade) */
      bool drop = argc >= 3 && !strcmp(argv[2], "drop");
      begin(next_style(drop), drop ? DROP_FADE_BEATS : FADE_BEATS);
    }
    else if (!strcmp(argv[1], "cut") && argc >= 3) S.cut = atoi(argv[2]) != 0;
    else if (!strcmp(argv[1], "readctl") && argc >= 3) S.readctl = atoi(argv[2]) != 0;
    else if (!strcmp(argv[1], "tour")) S.tour = 1; /* every style in turn, one about every 1.25 s */
    else {
      int k = style_arg(argv[1]);
      if (k < 0) { cw_reply_error(r, "style <name|index> | auto | next | cut 1|0 | readctl 1|0"); return; }
      styles_set_mode(k);
    }
  }
  cw_reply_printf(r, "{\"style\":\"%s\",\"index\":%d,\"from\":\"%s\",\"t\":%.3f,\"mode\":\"%s\",\"changes\":%d,\"cut\":%d,\"readctl\":%d",
                  STYLES[S.cur].id, S.cur, STYLES[S.from].id, S.t, style_id(S.mode), S.changes, S.cut, S.readctl);
  if (argc >= 2) { cw_reply_printf(r, "}"); return; } /* the table only when asked with no argument */
  cw_reply_printf(r, ",\"styles\":[");
  for (int i = 0; i < STYLE_COUNT; i++)
    cw_reply_printf(r, "%s{\"id\":\"%s\",\"title\":\"%s\",\"ascii\":%d,\"backdrop\":\"%s\",\"big\":%d,\"held\":%d}", i ? "," : "", STYLES[i].id, STYLES[i].title,
                    STYLES[i].ascii > 0, backdrop_name(STYLES[i].backdrop), STYLES[i].big, STYLES[i].held);
  cw_reply_printf(r, "]}");
}

void styles_init(void) {
  S.mode = -1, S.pending = -1, S.t = 1, S.cur = S.from = STYLE_NEON;
  S.bloom_set = S.thr_set = -1;
  flash_meter_init(&fl_style, "style");
  flash_meter_register(&fl_style);
  cw_debug_i32("style.cur", &S.cur);
  cw_debug_i32("style.from", &S.from);
  cw_debug_f32("style.t", &S.t);
  cw_debug_i32("style.mode", &S.mode);
  cw_debug_i32("style.changes", &S.changes);
  cw_debug_i32("style.prepare", &S.prepare);
  cw_debug_f32_array("style.backdrop", S.bd_w, BD_COUNT);
  cw_debug_command("style", "style [<name|index>|auto|next|cut 1|0|readctl 1|0]: the visual style now (src/styles.c): one style only, every style with the song, the next change now, changes as cuts (the flash check's control), no roads or pockets and dim notes (the readability gate's control)", cmd_style);
}

void styles_build(cw_node level) {
  if (S.built) return;
  S.built = true;
  S.post = cw_node_get(level, "Post");
  backdrop_build(level);
}
