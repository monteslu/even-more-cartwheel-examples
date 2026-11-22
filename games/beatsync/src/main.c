/* beatsync: a house-music rhythm game on three buttons (docs/DESIGN.md).
 *
 * Instrumental house songs split into stems. The player plays the kick, the clap and the bass: three lanes, left to
 * right, on the three lower face buttons (west, south, east; J K L on a keyboard; three zones across the bottom of
 * a touch screen in a native build, the host's on-screen pad in a cart). A hit punches that part of the song up; a miss
 * dips its track alone, which comes back by itself over a beat.
 *
 * The map of this file:
 *   - init(): songs, input, the shell (title, pause, settings, saves), sounds, the debug fields the checks read.
 *   - the screens of the game's own, inside the shell's play: song select (a preview of the song on the rivers),
 *     the difficulty, play, results, and the timing calibration (tap along to a click). The title has the rivers
 *     behind it with the first song playing itself.
 *   - fixed_update(): one tick (60 a second). The song clock (music.c) is the audio's own position, so notes are
 *     drawn and judged on what is heard, not on frames.
 *   - render(): the HUD and the screens' panels, then the shell's menus.
 *
 * The picture (club.c) is a note highway in neon ASCII: three rivers side by side as the buttons are (kick left,
 * clap middle, bass right), the notes riding each down to its gate on their beat, the other track as a land beside
 * them (what the music will do, seen coming), the streak charging the banks; the ASCII pass (ascii.c, Detexto's) turns
 * it into characters. Every light that rises and falls on its own is shaped here to swell and fade and fed through a
 * flash meter (src/flash_meter.c). The HUD and the screens sit in boxes over the field.
 *
 * <app>_auto is the same game built with GAME_AUTOPILOT: straight into play on Hard, hitting every note when it
 * reaches the hit line (the debug field bot.late_ms makes it hit late: the sync check's control), and round again.
 *
 * Determinism: the simulation runs only in fixed_update, from the input snapshot and the audio clock; on the
 * deterministic host the mixer advances by the host's fixed frame time, so the same inputs give the same game. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/platform.h"
#include "cartwheel/project.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"

#include "ascii.h"
#include "club.h"
#include "flash_meter.h"
#include "music.h"
#include "song.h"


#define LEAD_MS 4000 /* from Play to the song's first frame: the how-to and the count-in, the first notes coming round */
#define MAX_NOTES 8192
/* songs with a best kept: the save stays inside the shell's CW_SHELL_GAME_SAVE_MAX, so the first 32 songs played keep
 * theirs (a cart can list more; a song past these plays without a best) */
#define SAVE_SONGS 32
#define TIER_COMBO 16 /* hits a multiplier step: x2 at 16, x3 at 32, x4 at 48 */
#define MILESTONE 50  /* a streak of every 50 is called out */
#ifdef GAME_AUTOPILOT
#define AUTOPILOT 1
#else
#define AUTOPILOT 0
#endif

enum { SCR_NONE, SCR_SELECT, SCR_DIFF, SCR_PLAY, SCR_RESULTS, SCR_CALIB };
/* how the song is played (docs/DESIGN.md "One or two players"): one player on the three lanes, one player on a pair
 * of two lanes, or two players each on a pair, side by side (each on their own pad and difficulty, one streak) */
enum { MODE_SOLO, MODE_PAIR, MODE_DUO };
#define MAX_PLAYERS 2
enum { J_NONE, J_PERFECT, J_GOOD, J_MISS };
static const char *const GRADES[] = { "S", "A", "B", "C", "D", "F" };
/* beats of notes a river shows per difficulty: Easy slowest, so a newcomer reads a note about four seconds ahead. Twice
 * what a flat river showed (4, 3.2, 2.6): the perspective packs the far beats toward the horizon, so a note still
 * covers its last beat within about 10% of the distance it did (what the player times against), crossing the gate a
 * little faster (club.c: the speed at the gate is 45.2 units / ahead a beat) */
static const float AHEAD_BEATS[DIFFS] = { 8.0f, 6.4f, 5.2f };
/* a note's light per difficulty: Easy's quarter notes pass a point of a river at most twice a second, so they can be
 * brighter (easier to read) than Medium's and Hard's, whose eighths would pass as a 4 Hz flicker */
static const float NOTE_LIGHT[DIFFS] = { 1.8f, 1.0f, 1.0f };

typedef struct best_entry {
  uint32_t hash;
  int32_t score[DIFFS];
  uint8_t grade[DIFFS]; /* 0 none, else 1 + index into GRADES */
  uint8_t pad;
} best_entry;

/* the two-lane game's bests, one player on a pair (added at the end of the save: an older save keeps its bytes) */
typedef struct pair_best_entry {
  uint32_t hash;
  int32_t score[PAIRS][DIFFS];
  uint8_t grade[PAIRS][DIFFS];
  uint8_t pad[2];
} pair_best_entry;

typedef struct save_data {
  int32_t offset_ms; /* the calibration: how late this device's sound reaches the player */
  int32_t reserved;
  best_entry best[SAVE_SONGS];
  pair_best_entry pair_best[SAVE_SONGS];
} save_data;

static flash_meter fl_glow, fl_fade, fl_tier[MAX_PLAYERS], fl_drain[MAX_PLAYERS];

static struct {
  int32_t screen, sel, diff, pick, item;      /* the game's own screen, the song and difficulty chosen, the menu item */
  int32_t mode, players;                      /* MODE_*, and the players seated (1 or 2) */
  int32_t solo_pick;                          /* one player: 0 three lanes, 1 the drums, 2 the groove */
  int32_t part[MAX_PLAYERS], pdiff[MAX_PLAYERS], ready[MAX_PLAYERS]; /* each player's pair (PAIR_*), difficulty, and ready to play */
  int8_t owner[LANES];                        /* the player who plays each lane (-1: nobody, it plays as recorded) */
  note notes[MAX_NOTES];                      /* the chart being played: the three-lane chart, or the players' pairs merged */
  int32_t n_notes;
  int32_t p_perfect[MAX_PLAYERS], p_good[MAX_PLAYERS], p_miss[MAX_PLAYERS], p_total[MAX_PLAYERS];
  const song *s;
  uint8_t state[MAX_NOTES];
  uint8_t spoiled[MAX_NOTES];
  int32_t hit_seq[LANES];     /* the player's hits per lane (the club bursts the gate on each) */
  uint8_t hit_perfect[LANES]; /* a wrong button was pressed while this note was due: it scores Good at best */
  int32_t next;                         /* first note not judged yet */
  int32_t song_ms, raw_ms, done, failed, ended_ticks;
  int32_t total, hit, perfect, good, miss, score, best_score, new_best;
  float energy;
  float level[LANES], gain[LANES];
  int32_t cue_lane, cue_ms;
  float err_sum, err_sq;
  int32_t err_max, err_n;
  int32_t bot_late_ms;
  int32_t bot_skip[LANES], bot_skip_after, bot_skipped_ms; /* the `skip` request (the mix gate) */
  int32_t ticks, screen_ticks, miss_streak;
  /* the streak as the picture shows it: the banks' charge (toward the top multiplier), a multiplier step's band
   * rising up the rivers, a broken streak draining; the HUD's multiplier badge and the streak called out every 50 */
  struct streak { /* each player's own (two players: a miss by one never breaks the other's); one player: st[0] */
    int32_t combo, max_combo;
    float charge, tier_t, tier_light, drain_t, drain_from, drain_light;
    int32_t break_seq, tier_seq, broke_combo, broke_ticks, milestone, milestone_ticks;
  } st[MAX_PLAYERS];
  bool streakfx_off; /* the `streakfx off` request: none of it (the streak gate's control) */
  float flare[LANES], glow[LANES], glow_drawn[LANES], press[LANES];
  int32_t judge[LANES], judge_ticks[LANES];
  float fade; /* the failed song's dip, as drawn */
  int32_t preview_wait;
  bool demo;  /* the sequencer plays a song by itself (title, song select) */
  bool other_on_bass; /* the `othersrc 0` request: a note made from the other track plays on the bass lane, as 0.32.2 did (the bass gate's control) */
  bool shared_streak; /* the `sharedstreak 1` request (the duo gate's control) */
  bool crowd_stays; /* the `crowdstays 1` request: the floor never empties (a check that leaves one player idle) */
  bool pad_swap; /* the `padswap 1` request: each player plays on the other's pad (the duo gate's control) */
  bool old_rules; /* the `rules old` request: 35/70 ms on every difficulty, a wrong button misses the note */
  /* calibration */
  int64_t cal_start;
  int32_t cal_taps, cal_offsets[32], cal_result, cal_ms;
  double cal_clock;
  int32_t offset_ms;
  float vis_t;  /* seconds of frames shown (the picture's slow motion: the field's twinkle, the beams) */
  bool leave; /* the `leave` debug request: the crowd leaves (the flash check shows the dip) */
  int32_t from_ms, from_zero; /* where play started in the song; the `fromzero` request (the start gate's control) */
  /* a long wait for notes (more than GAP_BEATS between two): state 0 none, 1 the banks filling, 2 the countdown too;
   * count the bars (unit 1) or, in the last bar, the beats (unit 0) until the notes come onto the rivers */
  int32_t gap_state, gap_count, gap_unit, gap_mode, gap_shown, gap_overlap, gap_count_ticks;
  float gap_fill, gap_alpha;
} G;

static cw_action lane_act[SOLO_LANES], hand_act[2], acc, back, up, down, left, right;
static cw_sound snd_move, snd_select, snd_back, snd_click, snd_fail, snd_cheer;
static int touch_zone[SOLO_LANES];

static save_data *save(void) { return (save_data *)cw_shell_game_save(); }

static pair_best_entry *pair_best_of(const song *s) {
  save_data *sv = save();
  for (int i = 0; i < SAVE_SONGS; i++)
    if (sv->pair_best[i].hash == s->hash) return &sv->pair_best[i];
  for (int i = 0; i < SAVE_SONGS; i++)
    if (!sv->pair_best[i].hash) { sv->pair_best[i].hash = s->hash; return &sv->pair_best[i]; }
  return NULL;
}

static best_entry *best_of(const song *s) {
  save_data *sv = save();
  for (int i = 0; i < SAVE_SONGS; i++)
    if (sv->best[i].hash == s->hash) return &sv->best[i];
  for (int i = 0; i < SAVE_SONGS; i++)
    if (!sv->best[i].hash) { sv->best[i].hash = s->hash; return &sv->best[i]; }
  return NULL;
}

/* ── sounds: short tones for the menus and the calibration click (the songs are the music) ── */

static cw_sound tone(float hz, float seconds, float hz_end, float gain, float decay) {
  enum { RATE = 48000 };
  static float pcm[RATE * 2];
  int n = CW_MIN((int)(seconds * RATE), RATE * 2);
  double phase = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (float)n;
    phase += 2 * 3.14159265358979 * (hz + (hz_end - hz) * t) / RATE;
    pcm[i] = (float)sin(phase) * gain * expf(-t * decay) * fminf(1, t * 200);
  }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}
static void play(cw_sound s) { cw_audio_play(s, &(cw_play_desc){ .bus = cw_shell_sfx_bus() }); }

/* ── the song on the sequencer ─────────────────────────────────────── */

/* the judgement windows per difficulty (Easy, Medium, Hard), in ms either side of the note: wide enough for a person
 * on a living-room setup (Easy notes are at least 465 ms apart, Medium 228, Hard 114); Hard keeps its 35 ms Perfect
 * (the sync gate's control presses 50 ms late, and frame steps put some of those presses at 41 ms) */
static const int32_t PERFECT_WIN[DIFFS] = { 75, 55, 35 };
static const int32_t GOOD_WIN[DIFFS] = { 150, 110, 85 };
/* a lane's player and difficulty (two players can play at two difficulties) */
static int player_of(int lane) { return lane >= 0 && lane < LANES && G.owner[lane] > 0 ? G.owner[lane] : 0; }
static int diff_of(int lane) { return player_of(lane) ? G.pdiff[1] : G.diff; }
/* the `rules old` request (the forgiving gate's control): the windows and the wrong-button miss as they were */
static int32_t perfect_ms(int lane) { return G.old_rules ? 35 : PERFECT_WIN[diff_of(lane)]; }
static int32_t good_ms(int lane) { return G.old_rules ? 70 : GOOD_WIN[diff_of(lane)]; }

static const note *chart(void) { return G.s ? G.notes : NULL; }
static int chart_count(void) { return G.s ? G.n_notes : 0; }

/* the chart to play: the three-lane chart (and every demo), or each player's pair at their difficulty, merged in time
 * (the pairs' lanes never meet, so two notes on one tick are on two players' pads) */
static void build_notes(void) {
  G.n_notes = 0;
  for (int l = 0; l < LANES; l++) G.owner[l] = -1;
  const song *s = G.s;
  if (!s) return;
  if (G.demo || G.mode == MODE_SOLO) {
    int n = CW_MIN(s->notes[G.diff], MAX_NOTES);
    memcpy(G.notes, s->chart[G.diff], sizeof(note) * (size_t)n);
    G.n_notes = n;
    for (int l = 0; l < SOLO_LANES; l++) G.owner[l] = 0;
    return;
  }
  int np = G.mode == MODE_DUO ? 2 : 1, cnt[MAX_PLAYERS] = { 0 }, k[MAX_PLAYERS] = { 0 };
  const note *src[MAX_PLAYERS] = { 0 };
  for (int p = 0; p < np; p++) {
    int pl[2];
    pair_lanes(G.part[p], pl);
    G.owner[pl[0]] = G.owner[pl[1]] = (int8_t)p;
    src[p] = s->pair_chart[G.part[p]][G.pdiff[p]];
    cnt[p] = s->pair_notes[G.part[p]][G.pdiff[p]];
  }
  while (G.n_notes < MAX_NOTES) {
    int pick = -1;
    for (int p = 0; p < np; p++)
      if (k[p] < cnt[p] && (pick < 0 || src[p][k[p]].ms < src[pick][k[pick]].ms)) pick = p;
    if (pick < 0) break;
    G.notes[G.n_notes++] = src[pick][k[pick]++];
  }
}
static unsigned played_mask(void) {
  unsigned m = 0;
  for (int l = 0; l < LANES; l++)
    if (G.owner[l] >= 0) m |= 1u << l;
  return m;
}

/* start the song s at difficulty d: from_ms with a lead-in, or a preview */
static void begin_song(const song *s, int d, int32_t from_ms, int32_t lead_ms, float volume) {
  G.s = s;
  G.diff = d;
  G.pdiff[0] = d;
  build_notes();
  for (int p = 0; p < MAX_PLAYERS; p++) G.p_perfect[p] = G.p_good[p] = G.p_miss[p] = G.p_total[p] = 0;
  for (int i = 0; i < G.n_notes; i++) G.p_total[player_of(G.notes[i].lane)]++;
  memset(G.state, NOTE_PENDING, sizeof G.state);
  memset(G.spoiled, 0, sizeof G.spoiled);
  G.next = 0;
  const note *c = chart();
  int n = chart_count();
  while (G.next < n && c[G.next].ms < from_ms) G.state[G.next++] = NOTE_HIT; /* a preview starts part way */
  G.total = n;
  G.hit = G.perfect = G.good = G.miss = G.score = 0;
  G.done = G.failed = G.ended_ticks = G.new_best = 0;
  G.energy = 0.6f;
  G.err_sum = G.err_sq = 0;
  G.err_max = G.err_n = G.miss_streak = 0;
  G.fade = 0;
  G.gap_state = G.gap_count = G.gap_shown = G.gap_overlap = 0;
  G.gap_alpha = G.gap_fill = 0;
  for (int l = 0; l < LANES; l++) G.judge_ticks[l] = 0;
  for (int p = 0; p < MAX_PLAYERS; p++) {
    struct streak *S = &G.st[p];
    S->combo = S->max_combo = 0;
    S->charge = 0, S->tier_t = S->drain_t = -1, S->broke_ticks = S->milestone_ticks = 0;
  }
  music_set_player(!G.demo);
  music_set_played(G.demo ? (1u << SOLO_LANES) - 1 : played_mask());
  music_play(s, from_ms, lead_ms, volume);
}

/* the pause menu's own item while a song plays: straight back to the song select, no quitting to the title */
static const char *const PAUSE_IN_SONG[2] = { "Choose another song", NULL };
static const char *const PAUSE_NONE[2] = { NULL, NULL };

static void to_screen(int scr) {
  G.screen = scr;
  G.item = 0;
  G.screen_ticks = 0;
  cw_shell_set_pause_items(scr == SCR_PLAY ? PAUSE_IN_SONG : PAUSE_NONE);
}

static void start_preview(void) {
  const song *s = song_get(G.sel);
  if (!s) return;
  G.demo = true;
  begin_song(s, DIFF_HARD, s->preview_ms, 0, 0.75f);
}

/* Where play starts: 8 beats before the first note of the level chosen (on the song's own beats), so there is never
 * long to wait with nothing to play; a song whose first note is near its start plays from the top. The intro is still
 * heard in the song select's preview and the title. (`fromzero 1`, the start gate's control, plays from the top.) */
#define START_BEATS 8
static int32_t play_from_ms(const song *s) {
  if (!s || s->beats < 2 || !G.n_notes || G.from_zero) return 0;
  const note *first = &G.notes[0]; /* the chart about to be played (build_notes) */
  int b = first->step / 4 - START_BEATS;
  /* on the song's own beats; before its first tracked beat (a long intro with no drums) the tempo carries on back */
  int32_t from = b >= 0 && b < s->beats ? s->beat_ms[b] : first->ms - (int32_t)lroundf(START_BEATS * 60000.0f / fmaxf(s->bpm, 1));
  return from > 2000 ? from : 0; /* a couple of seconds in: from the top */
}

static void play_song(void) {
  G.demo = false;
  const song *s = song_get(G.sel);
  G.mode = G.players > 1 ? MODE_DUO : G.solo_pick ? MODE_PAIR : MODE_SOLO;
  if (G.mode == MODE_PAIR) G.part[0] = G.solo_pick == 2 ? PAIR_GROOVE : PAIR_DRUMS;
  G.s = s, G.diff = G.pdiff[0] = G.pick;
  build_notes();
  G.from_ms = play_from_ms(s);
  begin_song(s, G.pick, G.from_ms, LEAD_MS, 1);
  to_screen(SCR_PLAY);
}

/* ── judging ────────────────────────────────────────────────────────── */

static int grade_of(void) {
  if (G.failed) return 5;
  float acc_ = G.total ? (G.perfect + 0.5f * G.good) / G.total : 0;
  return acc_ >= 0.95f ? 0 : acc_ >= 0.85f ? 1 : acc_ >= 0.7f ? 2 : acc_ >= 0.55f ? 3 : 4;
}

static int mult_of(int combo) { return 1 + CW_MIN(combo / TIER_COMBO, 3); }

/* the streak breaks: one that had reached a multiplier drains its charge from the banks, throws its lit pieces off
 * and is felt in the pad; a short one just ends */
static void combo_lost_one(int player);
/* the `sharedstreak 1` request: a break ends every player's streak (one shared streak, the duo gate's control) */
static void combo_lost(int player) {
  if (G.shared_streak) for (int p = 0; p < MAX_PLAYERS; p++) combo_lost_one(p);
  else combo_lost_one(player);
}
static void combo_lost_one(int player) {
  struct streak *S = &G.st[player];
  if (S->combo >= TIER_COMBO && !G.demo) {
    S->drain_t = 0;
    S->drain_from = S->charge;
    S->break_seq++;
    S->broke_combo = S->combo;
    S->broke_ticks = 70;
    S->milestone_ticks = 0; /* what was lost replaces the call-out */
    cw_input_rumble(player, 0.25f + 0.1f * (float)(mult_of(S->combo) - 1), 0.1f, 160); /* felt by who broke it */
  }
  S->combo = 0;
  S->charge = 0;
}

static void judged(int i, int j, int32_t err) {
  int lane = chart()[i].lane, p = player_of(lane);
  G.state[i] = j == J_MISS ? NOTE_MISSED : NOTE_HIT;
  G.judge[lane] = j;
  G.judge_ticks[lane] = 40;
  if (j == J_MISS) {
    G.miss++;
    G.p_miss[p]++;
    combo_lost(p);
    G.miss_streak++;
    music_miss(chart()[i].other && !G.other_on_bass ? MUSIC_OTHER : lane); /* a note made from the other track dips that track */
    if (!G.demo) {
      G.energy -= diff_of(lane) == DIFF_EASY ? 0.05f : 0.075f;
      cw_input_rumble(p, 0.35f, 0.15f, 90); /* a miss is felt in the pad, never a shake */
    }
    return;
  }
  G.hit++;
  G.hit_seq[lane]++;
  G.hit_perfect[lane] = j == J_PERFECT;
  struct streak *S = &G.st[p];
  S->combo++;
  G.miss_streak = 0;
  if (S->combo > S->max_combo) S->max_combo = S->combo;
  if (j == J_PERFECT) G.perfect++, G.p_perfect[p]++;
  else G.good++, G.p_good[p]++;
  int mult = mult_of(S->combo);
  if (mult > mult_of(S->combo - 1)) S->tier_t = 0, S->tier_seq++; /* a multiplier step: a band rises up the player's rivers */
  if (S->combo % MILESTONE == 0 && !G.demo) S->milestone = S->combo, S->milestone_ticks = 100;
  G.score += (j == J_PERFECT ? 300 : 100) * mult; /* one score, each player's notes at their own multiplier */
  G.energy = fminf(1, G.energy + (j == J_PERFECT ? 0.03f : 0.015f));
  G.err_sum += (float)err;
  G.err_sq += (float)err * err;
  G.err_n++;
  if (abs(err) > G.err_max) G.err_max = abs(err);
  music_hit(chart()[i].other && !G.other_on_bass ? MUSIC_OTHER : lane);
  G.flare[lane] = fminf(1, G.flare[lane] + 0.09f); /* each hit adds a little: a run of hits builds the glow up */
  if (!G.demo && lane == LANE_KICK) cw_input_rumble(p, 0.12f, 0, 40); /* feel the kick */
}

/* a press in a lane at the song time t (the clock less the device's offset) */
static void press(int lane, int32_t t, bool mash) {
  const note *c = chart();
  int n = chart_count();
  G.press[lane] = 1;
  /* the note it was meant for: the nearest not yet judged in this lane within the Good window; only this player's
   * lanes count (the other player's notes are never this pad's wrong button) */
  const int32_t GOOD = good_ms(lane), me = player_of(lane);
  int best = -1;
  int32_t berr = GOOD + 1;
  int wrong = -1; /* the nearest note due in another lane: this press was the wrong button for it */
  int32_t werr = GOOD + 1;
  for (int i = G.next; i < n && c[i].ms <= t + GOOD; i++) {
    if (G.state[i] != NOTE_PENDING || player_of(c[i].lane) != me) continue;
    int32_t e = t - c[i].ms;
    if (c[i].lane != lane && G.old_rules) { /* as it was: the nearest note in any lane, a wrong button misses it */
      if (abs(e) < abs(berr)) berr = e, best = i;
      continue;
    }
    if (c[i].lane != lane) {
      if (abs(e) <= GOOD && abs(e) < abs(werr)) werr = e, wrong = i;
      continue;
    }
    if (abs(e) < abs(berr)) berr = e, best = i;
  }
  if (best < 0) {
    /* the wrong button costs the combo and that note's Perfect, never the note: it can still be hit in its own lane
     * (so a slip is forgiven, and mashing every button scores Good at best with no multiplier) */
    if (wrong >= 0) combo_lost(me), G.spoiled[wrong] = 1;
    return; /* nothing near: a free tap */
  }
  if (G.old_rules && c[best].lane != lane) { judged(best, J_MISS, 0); return; }
  if (mash && !G.old_rules) G.spoiled[best] = 1;
  judged(best, abs(berr) <= perfect_ms(lane) && !G.spoiled[best] ? J_PERFECT : J_GOOD, berr);
}

static void tick_room(float dt) {
  for (int l = 0; l < LANES; l++) {
    /* the hit glow: what the hits add decays over about a third of a second, and the glow drawn follows it up over
     * 60 ms (a swell) and down as it decays: fast runs of hits hold it lit instead of flashing it */
    G.flare[l] *= expf(-dt / 0.8f);
    G.press[l] = fmaxf(0, G.press[l] - dt / 0.1f);
    G.glow[l] = G.flare[l] > G.glow[l] ? fminf(G.flare[l], G.glow[l] + dt / 0.25f) : G.flare[l];
    if (G.judge_ticks[l]) G.judge_ticks[l]--;
  }
  /* the streak: the banks charge toward the top multiplier, a third of the way in about a second (each hit's step
   * eases in, never a jump); a multiplier step's band swells in over 60 ms and fades as it rises; a break's drain
   * starts at its full light and fades as it falls */
  for (int p = 0; p < MAX_PLAYERS; p++) {
    struct streak *S = &G.st[p];
    float want = fminf(1, (float)S->combo / (float)(3 * TIER_COMBO));
    S->charge = want > S->charge ? fminf(want, S->charge + dt / 0.35f) : want;
    if (S->tier_t >= 0) S->tier_t = S->tier_t + dt < TIER_S ? S->tier_t + dt : -1;
    if (S->drain_t >= 0) S->drain_t = S->drain_t + dt < DRAIN_S ? S->drain_t + dt : -1;
    float te = S->tier_t < 0 ? 0 : S->tier_t < 0.06f ? S->tier_t / 0.06f : powf(1 - (S->tier_t - 0.06f) / (TIER_S - 0.06f), 1.5f);
    float de = S->drain_t < 0 ? 0 : S->drain_t < 0.05f ? S->drain_t / 0.05f : 1 - (S->drain_t - 0.05f) / (DRAIN_S - 0.05f);
    S->tier_light = flash_meter_feed(&fl_tier[p], te);
    S->drain_light = flash_meter_feed(&fl_drain[p], de);
    if (S->broke_ticks) S->broke_ticks--;
    if (S->milestone_ticks) S->milestone_ticks--;
  }
  /* the receptors' lights (a small area round each, but metered all the same: the strongest lane) */
  float g = 0;
  for (int l = 0; l < LANES; l++) g = fmaxf(g, G.glow[l]);
  float gd = flash_meter_feed(&fl_glow, g);
  for (int l = 0; l < LANES; l++) G.glow_drawn[l] = g > 0 ? G.glow[l] * gd / g : gd * (fl_glow.strobe > 0);
}

/* one tick of a song on the sequencer: the clock, the misses, the bot, the end */
static void tick_song(float dt, bool player) {
  music_tick(dt);
  for (int l = 0; l < LANES; l++) G.level[l] = music_lane_level(l), G.gain[l] = music_lane_gain(l);
  G.song_ms = (int32_t)lround(music_ms());
  G.raw_ms = (int32_t)lround(music_raw_ms());
  int32_t t = G.song_ms - (player ? G.offset_ms : 0);
  const note *c = chart();
  int n = chart_count();
  if (player) {
#ifdef GAME_AUTOPILOT
    /* the bot: a note is pressed on the tick nearest its time (plus bot.late_ms) */
    for (int i = G.next; i < n && c[i].ms + G.bot_late_ms <= t + 8; i++)
      if (G.state[i] == NOTE_PENDING) {
        if (G.bot_skip[c[i].lane] > 0 && c[i].ms > G.bot_skip_after) { /* the `skip` request: let this one go by */
          G.bot_skip[c[i].lane]--;
          G.bot_skipped_ms = c[i].ms;
          G.state[i] = NOTE_SKIPPED;
          continue;
        }
        press(c[i].lane, t, false);
      }
#else
    /* no chart puts two notes on one tick: two or more buttons at once is a mash, and its hit scores Good at best
     * with no combo (so pressing every button on every note never pays) */
    if (G.mode == MODE_SOLO) {
      int down = 0;
      for (int l = 0; l < SOLO_LANES; l++) down += cw_action_just_pressed(0, lane_act[l]);
      if (down > 1) combo_lost(0);
      for (int l = 0; l < SOLO_LANES; l++)
        if (cw_action_just_pressed(0, lane_act[l])) press(l, t, down > 1);
    } else /* each player on their own pad: the left hand (West or L) and the right (South or R) of their pair */
      for (int p = 0; p < (G.mode == MODE_DUO ? 2 : 1); p++) {
        int pl[2], down = 0;
        pair_lanes(G.part[p], pl);
        int pad = G.pad_swap && G.mode == MODE_DUO ? 1 - p : p;
        for (int h = 0; h < 2; h++) down += cw_action_just_pressed(pad, hand_act[h]);
        if (down > 1) combo_lost(p);
        for (int h = 0; h < 2; h++)
          if (cw_action_just_pressed(pad, hand_act[h])) press(pl[h], t, down > 1);
      }
#endif
  } else /* the demo plays every note itself */
    for (int i = G.next; i < n && c[i].ms <= t; i++)
      if (G.state[i] == NOTE_PENDING) { G.state[i] = NOTE_HIT; G.flare[c[i].lane] = fminf(1, G.flare[c[i].lane] + 0.09f); }
  /* a stretch of notes made from the other track (a break): the next note is one, or the last one was within a beat */
  if (player) {
    int32_t beat = G.s->bpm > 0 ? (int32_t)(60000.0 / G.s->bpm) : 480;
    bool brk = !G.other_on_bass && ((G.next < n && c[G.next].other) || (G.next > 0 && G.next <= n && c[G.next - 1].other && t - c[G.next - 1].ms < beat));
    music_set_other_break(brk);
  } else music_set_other_break(false);
  /* notes past the Good window are missed */
  for (int i = G.next; i < n && c[i].ms < t - 70; i++) /* 70: the narrowest Good window */
    if ((G.state[i] == NOTE_PENDING || G.state[i] == NOTE_SKIPPED) && c[i].ms < t - good_ms(c[i].lane)) judged(i, J_MISS, 0);
  while (G.next < n && G.state[G.next] != NOTE_PENDING && G.state[G.next] != NOTE_SKIPPED) G.next++;
  G.cue_lane = G.next < n ? c[G.next].lane : -1;
  G.cue_ms = G.next < n ? c[G.next].ms : -1;
  tick_room(dt);
}

/* ── the shell's hooks ──────────────────────────────────────────────── */

static void shell_sound(cw_shell_sound s, void *user) {
  (void)user;
  play(s == CW_SHELL_SND_MOVE ? snd_move : s == CW_SHELL_SND_BACK ? snd_back : snd_select);
}

static void title_demo(void) {
  if (!songs_count()) return;
  G.screen = SCR_NONE;
  G.sel = 0;
  G.demo = true;
  begin_song(song_get(0), DIFF_HARD, 0, 600, 0.8f);
}

static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *user) {
  (void)user;
  if (to == CW_SHELL_PAUSE) music_pause(true);
  if (to == CW_SHELL_PLAY && (from == CW_SHELL_PAUSE || from == CW_SHELL_SETTINGS)) music_pause(false);
  if (to == CW_SHELL_TITLE && from != CW_SHELL_SETTINGS && from != CW_SHELL_LOADING) title_demo();
}

/* players: from the join screen, or a pad joining (or one leaving) later: two players play the two pairs */
static void set_players(int n) {
  G.players = CW_CLAMP(n, 1, MAX_PLAYERS);
  G.ready[0] = G.ready[1] = 0;
  if (G.players > 1 && G.part[0] == G.part[1]) G.part[1] = !G.part[0];
}
static void players_changed(int players, void *user) {
  (void)user;
  set_players(players);
}

static void start_preview(void);
/* the pause menu's "Choose another song": the song stops and the song select comes up on the same song */
static void pause_item(int index, void *user) {
  (void)user;
  if (index != 0 || G.screen != SCR_PLAY) return;
  music_stop(0.3f);
  G.failed = 0;
  to_screen(SCR_SELECT);
  start_preview();
}

static void start(int players, void *user) {
  (void)user;
  set_players(players);
#ifdef GAME_AUTOPILOT
  G.sel = 0;
  G.pick = DIFF_HARD;
  play_song();
#else
  to_screen(SCR_SELECT);
  start_preview();
#endif
}

/* ── the game's own screens ─────────────────────────────────────────── */

static int menu_count(void) { return G.screen == SCR_SELECT ? songs_count() + 1 : G.screen == SCR_DIFF ? DIFFS : G.screen == SCR_RESULTS ? 2 : 0; }
/* a menu press from any seated player's pad (both drive the song list and the results) */
static bool any_pressed(cw_action a) {
  for (int p = 0; p < G.players; p++)
    if (cw_action_just_pressed(p, a)) return true;
  return false;
}

/* two players choose on their own pads: up and down their difficulty, left and right swap the pairs, accept is ready
 * (and back takes it back, or goes back to the songs); both ready plays */
static void duo_diff_tick(void) {
  for (int p = 0; p < 2; p++) {
    int was = G.pdiff[p];
    if (!G.ready[p] && cw_action_just_pressed(p, up)) G.pdiff[p] = (G.pdiff[p] + DIFFS - 1) % DIFFS;
    if (!G.ready[p] && cw_action_just_pressed(p, down)) G.pdiff[p] = (G.pdiff[p] + 1) % DIFFS;
    if (G.pdiff[p] != was) play(snd_move);
    if (cw_action_just_pressed(p, left) || cw_action_just_pressed(p, right)) {
      G.part[0] = !G.part[0], G.part[1] = !G.part[0];
      G.ready[0] = G.ready[1] = 0;
      play(snd_move);
    }
    if (cw_action_just_pressed(p, acc) && !G.ready[p]) { G.ready[p] = 1; play(snd_select); }
    else if (cw_action_just_pressed(p, back)) {
      play(snd_back);
      if (G.ready[p]) G.ready[p] = 0;
      else { to_screen(SCR_SELECT); return; }
    }
  }
  if (G.ready[0] && G.ready[1]) {
    G.pick = G.pdiff[0];
    play_song();
  }
}

static void calib_start(void) {
  to_screen(SCR_CALIB);
  music_stop(0.2f);
  G.demo = false;
  int rate = cw_audio_sample_rate() > 0 ? cw_audio_sample_rate() : 48000;
  G.cal_start = cw_audio_mixed_frames() + rate; /* the first click a second from now, then one every half second */
  for (int k = 0; k < 24; k++) cw_audio_play(snd_click, &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .start_frame = G.cal_start + (int64_t)k * rate / 2 });
  G.cal_clock = -1000;
  G.cal_taps = 0;
  G.cal_result = 0;
}

static void calib_tick(float dt) {
  int rate = cw_audio_sample_rate() > 0 ? cw_audio_sample_rate() : 48000;
  double raw = (double)(cw_audio_mixed_frames() - G.cal_start) * 1000.0 / rate;
  G.cal_clock += dt * 1000.0;
  double err = raw - G.cal_clock;
  G.cal_clock += fabs(err) > 120 ? err : err * 0.12;
  G.cal_ms = (int32_t)lround(G.cal_clock);
  bool tap = false;
  for (int l = 0; l < SOLO_LANES; l++) tap |= cw_action_just_pressed(0, lane_act[l]);
  for (int h = 0; h < 2; h++) tap |= cw_action_just_pressed(0, hand_act[h]);
  if (tap && G.cal_ms > 1500 && G.cal_taps < 32 && G.cal_ms < 11750) {
    int32_t nearest = (int32_t)lround(G.cal_ms / 500.0) * 500;
    G.cal_offsets[G.cal_taps++] = G.cal_ms - nearest;
    for (int l = 0; l < LANES; l++) G.press[l] = 1;
  }
  if (G.cal_taps >= 4) {
    int32_t sum = 0;
    for (int i = 0; i < G.cal_taps; i++) sum += G.cal_offsets[i];
    G.cal_result = sum / G.cal_taps;
  }
  for (int l = 0; l < LANES; l++) G.press[l] = fmaxf(0, G.press[l] - dt / 0.1f);
  if (cw_action_just_pressed(0, acc) && G.cal_taps >= 4 && G.cal_ms > 1500) {
    G.offset_ms = save()->offset_ms = CW_CLAMP(G.cal_result, -250, 400);
    cw_shell_save_commit();
    play(snd_select);
    to_screen(SCR_SELECT);
    G.sel = songs_count() > 0 ? 0 : 0;
    start_preview();
  } else if (cw_action_just_pressed(0, back)) {
    play(snd_back);
    to_screen(SCR_SELECT);
    start_preview();
  }
}

static void menu_tick(void) {
  /* a second pad joins with one press while the songs or the difficulties show */
  if ((G.screen == SCR_SELECT || G.screen == SCR_DIFF) && G.players < MAX_PLAYERS && cw_shell_join_press()) play(snd_select);
  if (G.screen == SCR_DIFF && G.players > 1) { duo_diff_tick(); return; }
  int n = menu_count();
  int *cursor = G.screen == SCR_SELECT ? &G.sel : G.screen == SCR_DIFF ? &G.pick : &G.item;
  if (G.screen == SCR_SELECT && G.sel > n - 1) G.sel = n - 1;
  int was = *cursor;
  if (any_pressed(up)) *cursor = (*cursor + n - 1) % n;
  if (any_pressed(down)) *cursor = (*cursor + 1) % n;
  if (G.screen == SCR_DIFF && (any_pressed(left) || any_pressed(right))) { /* one player: three lanes, or a pair */
    G.solo_pick = (G.solo_pick + (any_pressed(left) ? 2 : 1)) % 3;
    play(snd_move);
  }
  if (*cursor != was) {
    play(snd_move);
    if (G.screen == SCR_SELECT) G.preview_wait = 20; /* the preview follows once the choice rests */
  }
  if (G.screen == SCR_SELECT && G.preview_wait && --G.preview_wait == 0 && G.sel < songs_count()) start_preview();
  if (any_pressed(acc)) {
    play(snd_select);
    if (G.screen == SCR_SELECT) {
      if (G.sel == songs_count()) calib_start();
      else {
        if (G.preview_wait) { G.preview_wait = 0; start_preview(); }
        to_screen(SCR_DIFF);
        G.ready[0] = G.ready[1] = 0;
        G.pdiff[0] = G.pick;
      }
    } else if (G.screen == SCR_DIFF) play_song();
    else if (G.screen == SCR_RESULTS) {
      if (G.item == 0) play_song();
      else { to_screen(SCR_SELECT); start_preview(); }
    }
    return;
  }
  if (any_pressed(back)) {
    play(snd_back);
    if (G.screen == SCR_SELECT) { music_stop(0.3f); cw_shell_to_title(); }
    else if (G.screen == SCR_DIFF) to_screen(SCR_SELECT);
    else if (G.screen == SCR_RESULTS) { to_screen(SCR_SELECT); start_preview(); }
  }
}

static void finish_song(void) {
  G.leave = false;
  G.done = G.failed ? 0 : 1;
  /* the best: one player's, per difficulty (three lanes, or each pair); two players play for the song, not a record */
  int32_t *bs = NULL;
  uint8_t *bg = NULL;
  if (G.mode == MODE_SOLO) {
    best_entry *b = best_of(G.s);
    if (b) bs = &b->score[G.diff], bg = &b->grade[G.diff];
  } else if (G.mode == MODE_PAIR) {
    pair_best_entry *b = pair_best_of(G.s);
    if (b) bs = &b->score[G.part[0]][G.diff], bg = &b->grade[G.part[0]][G.diff];
  }
  G.best_score = bs ? *bs : 0;
  if (bs && !G.failed && G.score > *bs) {
    *bs = G.score;
    G.best_score = G.score;
    G.new_best = 1;
  }
  int gr = grade_of();
  if (bg && (!*bg || gr + 1 < *bg)) *bg = (uint8_t)(gr + 1);
  cw_shell_save_commit();
  play(G.failed ? snd_fail : snd_cheer);
  to_screen(SCR_RESULTS);
}

/* the wait's indicator: while the next note is more than GAP_BEATS after the last one and not yet on its river, the
 * banks fill as the wait passes and, over its last four bars, a countdown to the notes coming on (bars, then the last
 * bar's beats); it fades out over half a second once they are on. gap_mode 1 never shows it, 2 always (the wait
 * gate's controls). gap.shown counts frames it is fully up, gap.overlap frames it still shows 0.6 s after the notes
 * came on (must stay 0) */
#define GAP_BEATS 12
static float view_ahead(void);
static void gap_tick(float dt) {
  const note *c = chart();
  float beat = G.s && G.s->bpm > 0 ? 60000.0f / G.s->bpm : 500;
  float t = (float)(G.song_ms - G.offset_ms);
  int last = G.next - 1;
  while (last >= 0 && G.state[last] == NOTE_GONE) last--;
  float prev = last >= 0 ? (float)c[last].ms : (float)G.from_ms, next = (float)G.cue_ms;
  float appear = next - view_ahead() * beat; /* when the next note comes onto the far end of its river */
  bool on = G.cue_ms >= 0 && next - prev > GAP_BEATS * beat && t < appear && !G.failed;
  if (G.gap_mode == 1) on = false;
  if (G.gap_mode == 2) on = !G.failed;
  int state = 0, count = 0, unit = 0;
  if (on) {
    float rem = (appear - t) / beat;
    state = rem <= 16 ? 2 : 1;
    if (state == 2) unit = rem > 4, count = rem > 4 ? (int)ceilf(rem / 4) : (int)ceilf(fmaxf(rem, 0.01f));
    G.gap_fill = G.gap_mode == 2 ? fminf(1, fmaxf(0, t / (G.s->length_s * 1000))) : fminf(1, fmaxf(0, (t - prev) / fmaxf(1, appear - prev)));
  }
  if (count != G.gap_count || unit != G.gap_unit) G.gap_count_ticks = 0;
  G.gap_count_ticks++;
  G.gap_state = state, G.gap_count = count, G.gap_unit = unit;
  G.gap_alpha = on ? fminf(1, G.gap_alpha + dt / 0.5f) : fmaxf(0, G.gap_alpha - dt / 0.5f);
  if (G.gap_alpha >= 0.99f) G.gap_shown++;
  if (G.gap_alpha > 0.01f && G.cue_ms >= 0 && t > appear + 600) G.gap_overlap++;
}

/* what the rivers show: beats ahead (two players at two difficulties share the slower, so both read their notes as
 * early as their own level would), the notes' light (Easy's brighter notes only where they come a beat apart: the
 * three-lane game), and which rivers (the choice shows on the rivers as it is made) */
static float view_ahead(void) {
  float a = AHEAD_BEATS[G.diff];
  if (G.mode == MODE_DUO && !G.demo) a = fmaxf(a, AHEAD_BEATS[G.pdiff[1]]);
  return a;
}
static float view_note_light(void) { return G.demo || G.mode == MODE_SOLO ? NOTE_LIGHT[G.diff] : 1.0f; }
static int view_layout(void) {
  if (G.screen == SCR_DIFF) {
    if (G.players > 1) return G.part[0] == PAIR_DRUMS ? CLUB_DUO_DRUMS_LEFT : CLUB_DUO_GROOVE_LEFT;
    return G.solo_pick == 1 ? CLUB_DRUMS : G.solo_pick == 2 ? CLUB_GROOVE : CLUB_SOLO;
  }
  if (G.demo || (G.screen != SCR_PLAY && G.screen != SCR_RESULTS) || G.mode == MODE_SOLO) return CLUB_SOLO;
  if (G.mode == MODE_PAIR) return G.part[0] == PAIR_GROOVE ? CLUB_GROOVE : CLUB_DRUMS;
  return G.part[0] == PAIR_DRUMS ? CLUB_DUO_DRUMS_LEFT : CLUB_DUO_GROOVE_LEFT;
}

static void play_tick(float dt) {
  tick_song(dt, true);
  gap_tick(dt);
  if ((G.energy <= 0 || G.leave) && !G.failed && (G.leave || (!AUTOPILOT && !G.crowd_stays))) { /* the floor empties: the song fades out */
    G.energy = 0;
    G.failed = 1;
    G.ended_ticks = 0;
    music_set_fade(0, 1.5f);
    cw_input_rumble(0, 0.5f, 0.2f, 300);
  }
  G.energy = fmaxf(0, G.energy);
  float f = G.failed ? fminf(1, G.ended_ticks / 90.0f) * 0.55f : 0;
  G.fade = flash_meter_feed(&fl_fade, f);
  bool over = G.failed ? G.ended_ticks > 100 : G.s && (music_ended() || G.song_ms > (int32_t)(G.s->length_s * 1000) + 300) && G.next >= chart_count();
  if (G.failed || (G.s && G.next >= chart_count())) G.ended_ticks++;
  if (over) {
    if (G.failed) music_stop(0.2f);
    finish_song();
  }
}

/* every frame: outside play (the title and its settings, where the scene app holds the simulation still) the
 * sequencer plays the first song by itself, a picture behind the menus that nothing in the game reads; under the
 * pause menu all holds. The clock it runs is the audio's, as in play. */
static void update(void *user, double dt) {
  (void)user;
  G.vis_t += (float)dt;
  cw_shell_screen sh = cw_shell_screen_now();
  if (sh == CW_SHELL_PLAY || sh == CW_SHELL_PAUSE || (sh == CW_SHELL_SETTINGS && G.screen != SCR_NONE) || !G.demo) return;
  tick_song((float)fmin(dt, 0.1), false);
  if (music_ended()) title_demo();
}

static void fixed_update(void *user, double dt) {
  (void)user;
  bool playing = cw_shell_update();
  G.ticks++;
  G.screen_ticks++;
  cw_shell_screen sh = cw_shell_screen_now();
  (void)sh;
  if (!playing) return; /* (the scene app does not call this while the shell's menus are up: update() runs the title's demo) */
  switch (G.screen) {
  case SCR_PLAY: play_tick((float)dt); break;
  case SCR_CALIB: calib_tick((float)dt); break;
  case SCR_RESULTS:
#ifdef GAME_AUTOPILOT
    if (G.screen_ticks > 240) play_song(); /* the autopilot goes round again */
#else
    menu_tick();
#endif
    tick_room((float)dt);
    break;
  default:
    menu_tick();
    if (G.demo && G.screen != SCR_CALIB) {
      tick_song((float)dt, false);
      if (music_ended() || G.song_ms > G.s->preview_ms + 30000) start_preview();
    }
    break;
  }
}

/* ── drawing: the HUD and the screens in boxes over the field (ascii.h: every element records its rectangle, the
 * pass keeps the characters under the boxes dim, the boxes get a neon edge) ───────────────────────────────────── */

static cw_font font(void) { return cw_ui_font_sans(); }
static const cw_color WHITE = { 1, 1, 1, 1 }, DIM = { 0.7f, 0.74f, 0.86f, 1 }, ACCENT = { 1.0f, 0.82f, 0.3f, 1 };
static float U; /* canvas pixels per 1080th of its height */

static cw_text_style ts(float size, cw_color c) {
  return (cw_text_style){ .size = size, .color = c, .shadow_color = cw_rgba(c.r, c.g, c.b, 0.4f * c.a), .shadow_spread = size * 0.04f,
                          .shadow_softness = size * 0.2f, .own_backdrop = true };
}
/* text at x (align -1 left, 0 centre, 1 right), boxed */
static void text_at(const char *s, float x, float y, float size, cw_color c, int align) {
  cw_text_style st = ts(size, c);
  cw_text_metrics m = cw_font_measure(font(), s, &st);
  x -= align == 0 ? m.width * 0.5f : align > 0 ? m.width : 0;
  ascii_hud_rect(x, y, m.width, m.height, c.a);
  cw_r2d_text(font(), s, cw_v2(x, y), &st);
}
/* A song's name in a box of its own width: one this long fits at this size; a longer one gets a little smaller
 * (to min_k of the size, never under the engine's text floor: every size here is at least 1.2 times it) and then ends
 * in an ellipsis. Writes the text to show into out and returns the size to draw it at. */
static float fit_text(const char *s, float size, float max_w, float min_k, char *out, size_t cap) {
  snprintf(out, cap, "%s", s);
  cw_text_style st = ts(size, WHITE);
  float w = cw_font_measure(font(), out, &st).width;
  if (w <= max_w) return size;
  size *= fmaxf(min_k, max_w / w);
  st = ts(size, WHITE);
  if (cw_font_measure(font(), out, &st).width <= max_w) return size;
  size_t n = strlen(out);
  while (n > 0) {
    do n--; while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80); /* a whole UTF-8 character at a time */
    size_t m = n;
    while (m > 0 && (out[m - 1] == ' ' || out[m - 1] == ',' || out[m - 1] == '-')) m--;
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%.*s\xE2\x80\xA6", (int)m, s);
    if (cw_font_measure(font(), tmp, &st).width <= max_w || m == 0) { snprintf(out, cap, "%s", tmp); break; }
  }
  return size;
}
/* fitted and drawn at x (align as text_at); y keeps the line's middle where the full size would have it */
static void text_fit(const char *s, float x, float y, float size, float max_w, cw_color c, int align) {
  char b[256];
  float k = fit_text(s, size, max_w, 0.8f, b, sizeof b);
  text_at(b, x, y + (size - k) * 0.5f, k, c, align);
}
/* wrapped to max_w on at most two lines (shrunk a little and then cut with an ellipsis past that), boxed, at x, y (left
 * aligned); returns its height */
static float text_wrap2(const char *s, float x, float y, float size, float max_w, cw_color c) {
  char b[256];
  snprintf(b, sizeof b, "%s", s);
  cw_text_style st = ts(size, c);
  st.wrap_width = max_w;
  cw_text_metrics m = cw_font_measure(font(), b, &st);
  if (m.lines > 2) { /* a little smaller, then whole words off the end */
    size *= 0.85f;
    st = ts(size, c), st.wrap_width = max_w;
    m = cw_font_measure(font(), b, &st);
    for (size_t n = strlen(b); m.lines > 2 && n > 0;) {
      do n--; while (n > 0 && ((unsigned char)b[n] & 0xC0) == 0x80);
      size_t k = n;
      while (k > 0 && b[k - 1] == ' ') k--;
      char tmp[256];
      snprintf(tmp, sizeof tmp, "%.*s\xE2\x80\xA6", (int)k, s);
      snprintf(b, sizeof b, "%s", tmp);
      m = cw_font_measure(font(), b, &st);
    }
  }
  ascii_hud_rect(x, y, fminf(m.width, max_w), m.height, c.a);
  cw_r2d_text(font(), b, cw_v2(x, y), &st);
  return m.height;
}
static void ptext(const char *s, float x, float y, float size, cw_color c) { /* a prompt line, centred, boxed */
  cw_vec2 sz = cw_prompt_text_size(font(), s, 0, size);
  ascii_hud_rect(x - sz.x * 0.5f, y, sz.x, sz.y, 1);
  cw_prompt_text(font(), s, 0, cw_v2(x, y), size, c, CW_ALIGN_CENTER);
}
static void panel(cw_rect r, cw_color edge) { ascii_hud_rect_edge(r.x, r.y, r.w, r.h, 1, edge); }
static const cw_color PANEL_EDGE = { 0.65f, 0.4f, 1.0f, 1 };
/* each judgement's own colour, the same on every river and on the results: Perfect green, Good turquoise, Miss red
 * (never the river's colour, so a word reads the same wherever it shows) */
static const cw_color JUDGE_COL[4] = { { 1, 1, 1, 1 }, { 0.35f, 1.0f, 0.45f, 1 }, { 0.25f, 0.9f, 0.95f, 1 }, { 1.0f, 0.32f, 0.32f, 1 } };
static cw_color lane_color(int l, float a) {
  float c[3];
  club_lane_rgb(l, c);
  return cw_rgba(c[0], c[1], c[2], a);
}
static void select_bar(cw_rect r) { cw_r2d_round_rect(r, 6 * U, 6, cw_rgba(0.55f, 0.3f, 1.0f, 0.4f)); }

static void draw_select(cw_vec2 cv, float size) {
  int n = songs_count();
  cw_rect lp = { cv.x * 0.04f, cv.y * 0.1f, cv.x * 0.3f, cv.y * 0.72f };
  panel(lp, PANEL_EDGE);
  text_at("Songs", lp.x + size * 0.6f, lp.y + size * 0.3f, size * 1.15f, ACCENT, -1);
  float row = size * 1.7f, y0 = lp.y + size * 2.1f;
  int rows = (int)((lp.h - size * 2.4f) / row);
  int first = CW_CLAMP(G.sel - rows / 2, 0, CW_MAX(0, n + 1 - rows));
  for (int i = first; i < n + 1 && i < first + rows; i++) {
    float y = y0 + (float)(i - first) * row;
    bool on = i == G.sel;
    if (on) select_bar((cw_rect){ lp.x + size * 0.3f, y - size * 0.2f, lp.w - size * 0.6f, row - size * 0.15f });
    if (i < n) {
      const song *s = song_get(i);
      char b[16], tb[256];
      snprintf(b, sizeof b, "%.0f", s->bpm);
      cw_text_style st = ts(size * 0.8f, DIM);
      float bw = cw_font_measure(font(), b, &st).width;
      cw_r2d_text(font(), b, cw_v2(lp.x + lp.w - size * 0.7f - bw, y + size * 0.1f), &st);
      /* the title in the room left of its BPM (the panel beside shows it whole) */
      float k = fit_text(s->title, size, lp.w - size * 1.4f - bw - size * 0.6f, 0.8f, tb, sizeof tb);
      st = ts(k, on ? WHITE : DIM);
      cw_r2d_text(font(), tb, cw_v2(lp.x + size * 0.7f, y + (size - k) * 0.5f), &st);
    } else {
      cw_text_style st = ts(size, on ? WHITE : DIM);
      cw_r2d_text(font(), "Timing calibration", cw_v2(lp.x + size * 0.7f, y), &st);
    }
  }
  cw_rect rp = { cv.x * 0.66f, cv.y * 0.1f, cv.x * 0.3f, cv.y * 0.72f };
  panel(rp, PANEL_EDGE);
  char b[96];
  if (G.sel < n) {
    const song *s = song_get(G.sel);
    /* the whole title here, on two lines if it needs them; the lines under it move down by the second */
    float th = text_wrap2(s->title, rp.x + size * 0.6f, rp.y + size * 0.3f, size * 1.3f, rp.w - size * 1.2f, WHITE);
    float dy = fmaxf(0, th - size * 1.3f * 1.3f);
    if (s->artist[0]) text_fit(s->artist, rp.x + size * 0.6f, rp.y + size * 2.0f + dy, size * 0.85f, rp.w - size * 1.2f, DIM, -1);
    int len = (int)s->length_s;
    snprintf(b, sizeof b, "%.0f BPM    %d:%02d", s->bpm, len / 60, len % 60);
    text_at(b, rp.x + size * 0.6f, rp.y + size * 3.2f + dy, size * 0.9f, DIM, -1);
    best_entry *be = best_of(s);
    for (int d = 0; d < DIFFS; d++) {
      float y = rp.y + size * (5.0f + (float)d * 1.6f) + dy;
      text_at(diff_name(d), rp.x + size * 0.6f, y, size, WHITE, -1);
      if (be && be->grade[d]) snprintf(b, sizeof b, "%s  %d", GRADES[be->grade[d] - 1], be->score[d]);
      else snprintf(b, sizeof b, "new");
      text_at(b, rp.x + rp.w - size * 0.6f, y, size * 0.9f, be && be->grade[d] ? ACCENT : DIM, 1);
    }
  } else {
    text_at("Calibration", rp.x + size * 0.6f, rp.y + size * 0.3f, size * 1.3f, WHITE, -1);
    cw_text_style st = ts(size * 0.85f, DIM);
    st.wrap_width = rp.w - size * 1.2f;
    cw_r2d_text(font(), "Tap along to a click, so the notes meet the beat you hear on this screen and speakers.", cw_v2(rp.x + size * 0.6f, rp.y + size * 2.2f), &st);
    snprintf(b, sizeof b, "now %+d ms", G.offset_ms);
    text_at(b, rp.x + size * 0.6f, rp.y + size * 7.0f, size, ACCENT, -1);
  }
  ptext("{ui_up|ui_down} choose    {ui_accept} select    {ui_back} title", cv.x * 0.5f, cv.y * 0.9f, size * 0.85f, (cw_color){ 0 });
}

/* the notes a choice plays: the three-lane chart or a pair's */
static int notes_for(const song *s, int mode_pick, int part, int d) {
  return mode_pick == 0 ? s->notes[d] : s->pair_notes[part][d];
}
static const char *const PART_HINT[PAIRS] = { "kick and clap", "bass and synth" };

/* one player: the cards as before, and over them how to play it (three lanes, or a pair on two buttons) */
static void draw_diff_solo(cw_vec2 cv, float size, const song *s) {
  static const char *const HINT[DIFFS] = { "on the beat", "eighths", "sixteenths" };
  static const char *const MODES[3] = { "3 lanes", "2 lanes: drums", "2 lanes: groove" };
  int part = G.solo_pick == 2 ? PAIR_GROOVE : PAIR_DRUMS;
  char b[64];
  snprintf(b, sizeof b, "< %s >", MODES[G.solo_pick]);
  text_at(b, cv.x * 0.5f, cv.y * 0.05f + size * 2.0f, size * 1.05f, ACCENT, 0);
  best_entry *be = best_of(s);
  pair_best_entry *pb = pair_best_of(s);
  /* three cards down the left and right of the rivers: Easy and Medium left, Hard right */
  for (int d = 0; d < DIFFS; d++) {
    float w = cv.x * 0.22f, h = cv.y * 0.22f;
    cw_rect r = { d < 2 ? cv.x * 0.04f : cv.x * 0.74f, d == 1 ? cv.y * 0.45f : cv.y * 0.18f, w, h };
    panel(r, d == G.pick ? ACCENT : PANEL_EDGE);
    if (d == G.pick) select_bar((cw_rect){ r.x + size * 0.2f, r.y + size * 0.2f, r.w - size * 0.4f, size * 1.6f });
    text_at(diff_name(d), r.x + r.w * 0.5f, r.y + size * 0.3f, size * 1.25f, d == G.pick ? WHITE : DIM, 0);
    text_at(G.solo_pick ? PART_HINT[part] : HINT[d], r.x + r.w * 0.5f, r.y + size * 2.0f, size * 0.85f, DIM, 0);
    snprintf(b, sizeof b, "%d notes", notes_for(s, G.solo_pick, part, d));
    text_at(b, r.x + r.w * 0.5f, r.y + size * 3.2f, size * 0.85f, WHITE, 0);
    int32_t sc = G.solo_pick ? (pb ? pb->score[part][d] : 0) : (be ? be->score[d] : 0);
    uint8_t gr = G.solo_pick ? (pb ? pb->grade[part][d] : 0) : (be ? be->grade[d] : 0);
    if (gr) snprintf(b, sizeof b, "best %s  %d", GRADES[gr - 1], sc);
    else snprintf(b, sizeof b, "new");
    text_at(b, r.x + r.w * 0.5f, r.y + size * 4.4f, size * 0.85f, DIM, 0);
  }
  ptext("{ui_left|ui_right} lanes    {ui_up|ui_down} difficulty    {ui_accept} play    {ui_back} songs", cv.x * 0.5f, cv.y * 0.9f, size * 0.85f, (cw_color){ 0 });
}

/* two players: a panel each, on their side of the rivers (the left player's pair on the left), each chosen on its own
 * pad; both ready plays */
static void draw_diff_duo(cw_vec2 cv, float size, const song *s) {
  char b[64];
  for (int p = 0; p < 2; p++) {
    float w = cv.x * 0.22f, h = cv.y * 0.6f;
    cw_rect r = { p == 0 ? cv.x * 0.04f : cv.x * 0.74f, cv.y * 0.16f, w, h };
    panel(r, G.ready[p] ? ACCENT : PANEL_EDGE);
    float x = r.x + r.w * 0.5f;
    snprintf(b, sizeof b, "Player %d", p + 1);
    text_at(b, x, r.y + size * 0.3f, size * 1.2f, WHITE, 0);
    snprintf(b, sizeof b, "%s", pair_name(G.part[p]));
    text_at(b, x, r.y + size * 1.9f, size * 1.05f, ACCENT, 0);
    text_at(PART_HINT[G.part[p]], x, r.y + size * 3.1f, size * 0.8f, DIM, 0);
    for (int d = 0; d < DIFFS; d++) {
      float y = r.y + size * (4.8f + 1.7f * (float)d);
      bool on = d == G.pdiff[p];
      if (on) select_bar((cw_rect){ r.x + size * 0.3f, y - size * 0.2f, r.w - size * 0.6f, size * 1.5f });
      snprintf(b, sizeof b, "%s  %d", diff_name(d), s->pair_notes[G.part[p]][d]);
      text_at(b, x, y, size * 0.95f, on ? WHITE : DIM, 0);
    }
    text_at(G.ready[p] ? "ready" : "", x, r.y + r.h - size * 1.6f, size * 1.0f, ACCENT, 0);
  }
  ptext("{ui_up|ui_down} difficulty    {ui_left|ui_right} swap parts    {ui_accept} ready    {ui_back} back", cv.x * 0.5f, cv.y * 0.9f, size * 0.85f, (cw_color){ 0 });
}

static void draw_diff(cw_vec2 cv, float size) {
  const song *s = song_get(G.sel);
  if (!s) return;
  text_fit(s->title, cv.x * 0.5f, cv.y * 0.05f, size * 1.4f, cv.x * 0.38f, WHITE, 0); /* between the side cards, clear of them (their boxes would merge with its) */
  if (G.players > 1) draw_diff_duo(cv, size, s);
  else draw_diff_solo(cv, size, s);
}

static int played_lane(void) {
  for (int l = 0; l < LANES; l++)
    if (G.owner[l] >= 0) return l;
  return LANE_CLAP;
}

static void draw_play(cw_vec2 cv, float size) {
  char b[64];
  if (G.s) { /* the song and how far through it, top left */
    text_fit(G.s->title, size * 0.8f, size * 0.5f, size, cv.x * 0.28f, WHITE, -1);
    float p = G.s->length_s > 0 ? CW_CLAMP((float)G.song_ms / (G.s->length_s * 1000), 0, 1) : 0, w = cv.x * 0.18f;
    ascii_hud_rect(size * 0.8f, size * 1.9f, w, size * 0.25f, 1);
    cw_r2d_round_rect((cw_rect){ size * 0.8f, size * 1.9f, w, size * 0.25f }, size * 0.12f, 4, cw_rgba(1, 1, 1, 0.18f));
    cw_r2d_round_rect((cw_rect){ size * 0.8f, size * 1.9f, w * p, size * 0.25f }, size * 0.12f, 4, cw_rgba(0.7f, 0.45f, 1, 0.95f));
    if (G.mode == MODE_DUO) snprintf(b, sizeof b, "%s %s  /  %s %s", diff_name(G.pdiff[0]), pair_name(G.part[0]), diff_name(G.pdiff[1]), pair_name(G.part[1]));
    else if (G.mode == MODE_PAIR) snprintf(b, sizeof b, "%s  %s", diff_name(G.diff), pair_name(G.part[0]));
    else snprintf(b, sizeof b, "%s", diff_name(G.diff));
    text_at(b, size * 0.8f, size * 2.5f, size * 0.8f, DIM, -1);
  }
  snprintf(b, sizeof b, "%d", G.score); /* the score and the streak, top right */
  text_at(b, cv.x - size * 0.8f, size * 0.4f, size * 1.4f, WHITE, 1);
  if (G.mode == MODE_DUO) { /* each player's own streak */
    snprintf(b, sizeof b, "P1 %d   P2 %d streak", G.st[0].combo, G.st[1].combo);
    text_at(b, cv.x - size * 0.8f, size * 2.2f, size * 0.9f, ACCENT, 1);
  } else if (G.st[0].combo >= 4) {
    snprintf(b, sizeof b, "%d streak", G.st[0].combo);
    text_at(b, cv.x - size * 0.8f, size * 2.2f, size * 0.9f, ACCENT, 1);
  }
  /* the crowd's energy: a meter down the right side */
  cw_rect m = { cv.x - size * 1.7f, cv.y * 0.3f, size * 0.55f, cv.y * 0.4f };
  ascii_hud_rect(m.x, m.y, m.w, m.h + size * 1.4f, 1);
  cw_r2d_round_rect(m, size * 0.27f, 6, cw_rgba(1, 1, 1, 0.12f));
  float e = CW_CLAMP(G.energy, 0, 1);
  cw_color ec = e < 0.25f ? cw_rgba(1, 0.35f, 0.3f, 0.95f) : cw_rgba(0.45f, 1, 0.6f, 0.95f);
  cw_r2d_round_rect((cw_rect){ m.x, m.y + m.h * (1 - e), m.w, m.h * e }, size * 0.27f, 6, ec);
  cw_text_style cst = ts(size * 0.75f, DIM);
  float cw_ = cw_font_measure(font(), "crowd", &cst).width;
  cw_r2d_text(font(), "crowd", cw_v2(m.x + m.w * 0.5f - cw_ * 0.5f, m.y + m.h + size * 0.3f), &cst);
  /* the latest judgement under its river's gate, in its instrument's colour, rising and fading */
  static const char *const JN[] = { "", "Perfect", "Good", "Miss" };
  /* each player's own (with two players one word jumping between the roads on every hit blinked under each road) */
  for (int p = 0; p < (G.mode == MODE_DUO ? 2 : 1); p++) {
  int jl = -1;
  for (int l = 0; l < LANES; l++)
    if (G.judge_ticks[l] && (G.mode != MODE_DUO || G.owner[l] == p) && (jl < 0 || G.judge_ticks[l] > G.judge_ticks[jl])) jl = l;
  if (jl >= 0 && !G.failed) {
    float x, y, t = (float)G.judge_ticks[jl] / 40.0f;
    club_gate_screen(jl, cv.x, cv.y, &x, &y);
    y += size * 1.5f;
    cw_color c = JUDGE_COL[G.judge[jl]];
    c.a = fminf(1, t * 2.5f);
    text_at(JN[G.judge[jl]], x, y - size * 0.7f - (1 - t) * size * 0.4f, size * (G.judge[jl] == J_PERFECT ? 1.1f : 0.95f), c, 0);
  }
  }
  /* the multiplier, under the middle gate where the eyes already are: x1 to x4 and the hits to the next step as a
   * bar; every 50 the streak is called out there instead, rising and fading; a broken streak shows what was lost */
  /* two players: each one's under their own road, from their own streak */
  for (int p = 0; G.song_ms >= 0 && !G.failed && !G.streakfx_off && p < (G.mode == MODE_DUO ? 2 : 1); p++) {
    const struct streak *S = &G.st[p];
    float x, y, r_, gy;
    club_center_screen(cv.x, cv.y, &x, &y, &r_); /* the middle of the rivers */
    club_gate_screen(played_lane(), cv.x, cv.y, &r_, &gy); /* every gate is on one line */
    if (G.mode == MODE_DUO) { /* the middle of this player's road */
      int pl[2];
      float x0, x1, yy;
      pair_lanes(G.part[p], pl);
      club_gate_screen(pl[0], cv.x, cv.y, &x0, &yy);
      club_gate_screen(pl[1], cv.x, cv.y, &x1, &yy);
      x = (x0 + x1) * 0.5f;
    }
    y = gy + size * 2.35f;
    int m = mult_of(S->combo);
    if (S->milestone_ticks) {
      float t = (float)S->milestone_ticks / 100.0f, a = fminf(1, t * 3) * fminf(1, (1 - t) * 8 + 0.2f);
      snprintf(b, sizeof b, "%d streak", S->milestone);
      text_at(b, x, y - (1 - t) * size * 0.5f, size * 1.15f, cw_rgba(1, 0.95f, 0.7f, a), 0);
    } else if (S->broke_ticks) {
      float a = fminf(1, (float)S->broke_ticks / 30.0f);
      cw_color c = JUDGE_COL[J_MISS];
      c.a = a;
      snprintf(b, sizeof b, "streak %d lost", S->broke_combo);
      text_at(b, x, y, size * 0.9f, c, 0);
    } else if (S->combo > 0) {
      float w = size * 3.6f, bh = size * 0.22f, prog = m >= 4 ? 1 : (float)(S->combo % TIER_COMBO) / TIER_COMBO;
      snprintf(b, sizeof b, "x%d", m);
      cw_color c = m >= 4 ? cw_rgba(1, 0.95f, 0.7f, 1) : m >= 2 ? WHITE : DIM;
      text_at(b, x, y - size * 0.15f, size * (0.85f + 0.08f * (float)m), c, 0);
      float by = y + size * 1.05f;
      cw_r2d_round_rect((cw_rect){ x - w * 0.5f, by, w, bh }, bh * 0.5f, 4, cw_rgba(1, 1, 1, 0.15f));
      cw_r2d_round_rect((cw_rect){ x - w * 0.5f, by, w * prog, bh }, bh * 0.5f, 4, m >= 4 ? cw_rgba(1, 0.9f, 0.6f, 0.95f) : cw_rgba(0.7f, 0.45f, 1, 0.95f));
    }
  }
  /* how to play, in the count-in only: each button under the river it plays (kick left, clap middle, bass right, as
   * the buttons sit). Once the song is going the rivers' places are the only cue: no button stays on screen in play */
  static const char *const LP[SOLO_LANES] = { "{lane_kick} kick", "{lane_clap} clap", "{lane_bass} bass" };
  static const char *const PART[LANES] = { "kick", "clap", "bass", "synth" };
  if (G.song_ms < 0 && !G.failed) {
    float fade = fminf(1, (float)-G.song_ms / 300.0f);
    for (int l = 0; l < LANES; l++) {
      if (G.owner[l] < 0) continue;
      /* a pair's buttons: its left lane West or L, its right South or R, on the pad of the player who plays it */
      char lp[48];
      int pl[2];
      pair_lanes(G.mode == MODE_SOLO ? PAIR_DRUMS : G.part[G.owner[l]], pl);
      if (G.mode == MODE_SOLO) snprintf(lp, sizeof lp, "%s", LP[l]);
      else snprintf(lp, sizeof lp, "{%s} %s", l == pl[0] ? "hand_left" : "hand_right", PART[l]);
      float hx, hy, sz = size * 0.95f;
      club_gate_screen(l, cv.x, cv.y, &hx, &hy);
      cw_vec2 ps = cw_prompt_text_size(font(), lp, 0, sz);
      float x = hx, y = hy + size * 1.0f;
      ascii_hud_rect_edge(x - ps.x * 0.5f, y, ps.x, ps.y, fade, lane_color(l, fade));
      cw_prompt_text(font(), lp, 0, cw_v2(x, y), sz, lane_color(l, fade), CW_ALIGN_CENTER);
    }
    if (G.mode == MODE_DUO) /* who plays which pair, over its two rivers */
      for (int p = 0; p < 2; p++) {
        int pl[2];
        float x0, x1, y;
        pair_lanes(G.part[p], pl);
        club_gate_screen(pl[0], cv.x, cv.y, &x0, &y);
        club_gate_screen(pl[1], cv.x, cv.y, &x1, &y);
        snprintf(b, sizeof b, "Player %d", p + 1);
        text_at(b, (x0 + x1) * 0.5f, y + size * 2.4f, size * 0.9f, cw_rgba(1, 1, 1, fade), 0);
      }
  }
  if (G.mode == MODE_SOLO && cw_touch_overlay_visible()) /* on a touch screen: the three zones across the bottom */
    for (int l = 0; l < SOLO_LANES; l++)
      cw_r2d_round_rect_lines((cw_rect){ cv.x * (float)l / 3.0f + size * 0.3f, cv.y * 0.62f, cv.x / 3.0f - size * 0.6f, cv.y * 0.36f }, size * 0.5f, size * 0.08f, 8, lane_color(l, 0.35f + 0.5f * G.press[l]));
  if (G.gap_state == 2 && G.gap_alpha > 0 && G.song_ms >= 0) { /* a long wait's last four bars: the countdown */
    float x, y, r, a = G.gap_alpha; /* the whole countdown swells in and fades out; the number just changes */
    club_center_screen(cv.x, cv.y, &x, &y, &r);
    snprintf(b, sizeof b, G.gap_unit ? "%d bars" : "%d", G.gap_count);
    text_at("get ready", x, y - size * 2.4f, size * 0.9f, cw_rgba(0.75f, 0.7f, 0.95f, a), 0);
    text_at(b, x, y - size * 0.9f, size * 1.6f, cw_rgba(1, 1, 1, a), 0);
  }
  if (G.song_ms < 0) { /* the count in */
    int beats = (int)ceilf((float)-G.song_ms / 500.0f);
    snprintf(b, sizeof b, beats > 3 ? "Get ready" : "%d", beats);
    float x, y, r;
    club_center_screen(cv.x, cv.y, &x, &y, &r);
    text_at(b, x, y - size * 0.9f, size * 1.6f, WHITE, 0);
  }
  if (G.failed || fl_fade.strobe) {
    cw_r2d_rect((cw_rect){ 0, 0, cv.x, cv.y }, cw_rgba(0.02f, 0, 0.04f, G.fade));
    if (G.failed) text_at("The floor emptied", cv.x * 0.5f, cv.y * 0.42f, size * 1.6f, cw_rgba(1, 0.6f, 0.6f, fminf(1, (float)G.ended_ticks / 30.0f)), 0);
  }
}

static void draw_results(cw_vec2 cv, float size) {
  cw_rect r = { cv.x * 0.3f, cv.y * 0.08f, cv.x * 0.4f, cv.y * 0.8f };
  panel(r, G.failed ? cw_rgba(1, 0.4f, 0.4f, 1) : ACCENT);
  char b[96];
  float x = r.x + r.w * 0.5f;
  cw_text_style st;
#define LINE(s, y, sz, c) (st = ts(sz, c), cw_r2d_text(font(), s, cw_v2(x - cw_font_measure(font(), s, &st).width * 0.5f, y), &st))
  if (G.s) text_fit(G.s->title, x, r.y + size * 0.6f, size * 1.2f, r.w - size * 1.2f, WHITE, 0);
  if (G.mode == MODE_DUO) snprintf(b, sizeof b, "two players  %s", G.failed ? "the floor emptied" : "cleared");
  else if (G.mode == MODE_PAIR) snprintf(b, sizeof b, "%s %s  %s", diff_name(G.diff), pair_name(G.part[0]), G.failed ? "the floor emptied" : "cleared");
  else snprintf(b, sizeof b, "%s  %s", diff_name(G.diff), G.failed ? "the floor emptied" : "cleared");
  LINE(b, r.y + size * 2.2f, size * 0.85f, DIM);
  LINE(GRADES[grade_of()], r.y + size * 3.4f, size * 3.6f, ACCENT);
  snprintf(b, sizeof b, "%d", G.score);
  LINE(b, r.y + size * 7.6f, size * 1.4f, WHITE);
  if (G.mode == MODE_DUO) { /* each player's part: their difficulty and how it went */
    for (int p = 0; p < 2; p++) {
      int tot = G.p_total[p];
      snprintf(b, sizeof b, "P%d %s %s  %d%%  miss %d", p + 1, diff_name(G.pdiff[p]), pair_name(G.part[p]), tot ? (int)lroundf(100.0f * ((float)G.p_perfect[p] + 0.5f * (float)G.p_good[p]) / (float)tot) : 0, G.p_miss[p]);
      LINE(b, r.y + size * (8.9f + 0.9f * (float)p), size * 0.75f, DIM);
    }
  } else if (G.new_best) LINE("new best", r.y + size * 9.3f, size * 0.85f, ACCENT);
  else { snprintf(b, sizeof b, "best %d", G.best_score); LINE(b, r.y + size * 9.3f, size * 0.85f, DIM); }
  { /* the counts, each in its judgement's colour, centred as one line */
    char part[3][32];
    snprintf(part[0], sizeof part[0], "Perfect %d", G.perfect);
    snprintf(part[1], sizeof part[1], "   Good %d", G.good);
    snprintf(part[2], sizeof part[2], "   Miss %d", G.miss);
    float w[3], total = 0, h = 0, y = r.y + size * 10.8f; /* x: the panel middle (LINE) */
    for (int i = 0; i < 3; i++) {
      cw_text_style st = ts(size * 0.85f, WHITE);
      cw_text_metrics m = cw_font_measure(font(), part[i], &st);
      w[i] = m.width, total += m.width, h = fmaxf(h, m.height);
    }
    float px = x - total * 0.5f; /* centred on the panel, as LINE centres its lines */
    (void)h;
    for (int i = 0; i < 3; i++) {
      cw_text_style pst = ts(size * 0.85f, JUDGE_COL[J_PERFECT + i]);
      cw_r2d_text(font(), part[i], cw_v2(px, y), &pst);
      px += w[i];
    }
  }
  if (G.mode == MODE_DUO) snprintf(b, sizeof b, "longest streaks P1 %d, P2 %d", G.st[0].max_combo, G.st[1].max_combo);
  else snprintf(b, sizeof b, "longest streak %d of %d", G.st[0].max_combo, G.total);
  LINE(b, r.y + size * 12.1f, size * 0.85f, DIM);
  static const char *const ITEMS[2] = { "Play again", "Songs" };
  for (int i = 0; i < 2; i++) {
    float y = r.y + size * (13.8f + (float)i * 1.5f);
    if (i == G.item) select_bar((cw_rect){ r.x + r.w * 0.25f, y - size * 0.15f, r.w * 0.5f, size * 1.35f });
    LINE(ITEMS[i], y, size, i == G.item ? WHITE : DIM);
  }
#undef LINE
}

static void draw_calib(cw_vec2 cv, float size) {
  cw_rect r = { cv.x * 0.25f, cv.y * 0.1f, cv.x * 0.5f, cv.y * 0.74f };
  panel(r, PANEL_EDGE);
  text_at("Timing calibration", r.x + r.w * 0.5f, r.y + size * 0.5f, size * 1.3f, WHITE, 0);
  ptext("{lane_kick|lane_clap|lane_bass} tap on each click", r.x + r.w * 0.5f, r.y + size * 2.3f, size, (cw_color){ 0 });
  /* the beat: a ring that swells on the click and fades */
  float ph = G.cal_ms < 0 ? 1 : fmodf((float)G.cal_ms, 500.0f) / 500.0f, k = expf(-ph * 6);
  cw_vec2 c = cw_v2(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
  cw_r2d_circle_lines(c, size * (2.2f + 0.4f * k), size * 0.18f, 48, cw_rgba(0.6f, 0.4f, 1, 0.4f + 0.5f * k));
  cw_r2d_circle(c, size * 1.4f * (1 - 0.15f * G.press[0]), 40, cw_rgba(1, 0.82f, 0.3f, 0.3f + 0.5f * G.press[0]));
  /* the taps so far: early left of the centre line, late right */
  float x0 = r.x + r.w * 0.1f, w = r.w * 0.8f, y = r.y + r.h * 0.76f;
  cw_r2d_line(cw_v2(x0, y), cw_v2(x0 + w, y), size * 0.06f, cw_rgba(1, 1, 1, 0.3f));
  cw_r2d_line(cw_v2(x0 + w * 0.5f, y - size * 0.5f), cw_v2(x0 + w * 0.5f, y + size * 0.5f), size * 0.08f, cw_rgba(1, 1, 1, 0.6f));
  for (int i = 0; i < G.cal_taps; i++) {
    float u = CW_CLAMP((float)G.cal_offsets[i] / 250.0f, -1, 1);
    cw_r2d_circle(cw_v2(x0 + w * (0.5f + 0.5f * u), y), size * 0.22f, 12, cw_rgba(0.4f, 0.9f, 1, 0.8f));
  }
  char b[64];
  if (G.cal_taps >= 4) snprintf(b, sizeof b, "%+d ms (%d taps)", G.cal_result, G.cal_taps);
  else snprintf(b, sizeof b, "%d taps", G.cal_taps);
  text_at(b, r.x + r.w * 0.5f, y + size * 0.8f, size, ACCENT, 0);
  ptext(G.cal_taps >= 4 ? "{ui_accept} keep    {ui_back} cancel" : "{ui_back} cancel", cv.x * 0.5f, cv.y * 0.9f, size * 0.85f, (cw_color){ 0 });
  if (G.cal_ms > 12500 && G.cal_taps < 4) calib_start(); /* the clicks ran out: again */
}

/* the shell's screens (title, pause, settings) over the field: a box behind the menu column and one behind the
 * prompt line at the foot, as Detexto does */
static void draw_background(cw_shell_screen screen, cw_vec2 cs, void *user) {
  (void)user;
  float k = cs.y / 1080.0f;
  ascii_hud_reset(cs, k);
  if (screen == CW_SHELL_JOIN) { /* a box behind each seat, the heading and the hints, where the shell draws them */
    float u = cs.y / 20.0f;
    ascii_hud_rect(cs.x * 0.4f, cs.y * 0.06f, cs.x * 0.2f, cs.y * 0.14f, 1);
    for (int s = 0; s < MAX_PLAYERS; s++) { /* under the seats where the shell centres them */
      cw_rect r = cw_shell_join_seat_rect(s);
      ascii_hud_rect(r.x, r.y, r.w, r.h, 1);
    }
    ascii_hud_rect(cs.x * 0.25f, cs.y - u * 2.3f, cs.x * 0.5f, u * 1.3f, 1);
  } else {
    ascii_hud_rect(cs.x * 0.3f, cs.y * 0.08f, cs.x * 0.4f, cs.y * 0.7f, 1);
    ascii_hud_rect(cs.x * 0.3f, cs.y * 0.895f, cs.x * 0.4f, cs.y * 0.075f, 1);
  }
  ascii_hud_end();
  ascii_hud_draw();
}

static void touch_layout(void) {
  if (!cw_touch_controls_on()) return; /* in a cart the host draws the pad (the manifest's controls) */
  int w, h;
  cw_window_size(&w, &h);
  for (int l = 0; l < SOLO_LANES; l++) cw_touch_set_rect(touch_zone[l], (float)w * (float)l / 3.0f, (float)h * 0.55f, (float)w / 3.0f, (float)h * 0.45f);
}

static bool look_ready;
static void ensure_look(void) { /* the picture and the ASCII pass, once the GPU is up (the first render) */
  if (look_ready) return;
  look_ready = true;
  club_build();
  ascii_init(cw_scene_app_tree());
}

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  ensure_look();
  cw_quality_auto_measure(cw_shell_hud_shown() && G.screen == SCR_PLAY);
  touch_layout();
  bool menu = G.screen == SCR_SELECT || G.screen == SCR_DIFF || G.screen == SCR_CALIB || G.screen == SCR_RESULTS;
  club_view v = {
    .s = G.s, .notes = chart(), .state = G.state, .count = chart_count(),
    .ms = G.s ? (double)G.song_ms - (G.demo ? 0 : G.offset_ms) : 0,
    .dim = menu ? 1 : 0, .zoom = 1, .spread = G.screen == SCR_NONE ? 2.3f : 1, .ahead = view_ahead(), .note_light = view_note_light(), .t = G.vis_t, .layout = view_layout(),
    .gap_fill = G.screen == SCR_PLAY ? G.gap_fill : 0, .gap_alpha = G.screen == SCR_PLAY ? G.gap_alpha : 0,
  };
  bool fx = G.screen == SCR_PLAY && !G.streakfx_off;
  for (int p = 0; p < MAX_PLAYERS; p++) { /* each player's streak on their own rivers */
    const struct streak *S = &G.st[p];
    v.charge[p] = fx ? S->charge : 0;
    v.tier_t[p] = fx ? S->tier_t : -1, v.tier_light[p] = fx ? S->tier_light : 0;
    v.drain_t[p] = fx ? S->drain_t : -1, v.drain_from[p] = S->drain_from, v.drain_light[p] = fx ? S->drain_light : 0;
    v.break_seq[p] = fx ? S->break_seq : 0;
  }
  for (int l = 0; l < LANES; l++) v.lane_player[l] = (int8_t)(G.owner[l] > 0 ? G.owner[l] : 0);
  for (int l = 0; l < LANES; l++) {
    v.flare[l] = G.glow_drawn[l];
    v.hit_seq[l] = G.hit_seq[l];
    v.hit_perfect[l] = G.hit_perfect[l];
    v.press[l] = G.press[l];
    v.level[l] = G.level[l];
  }
  club_update(&v);
  if (cw_shell_hud_shown() && G.screen != SCR_NONE) {
    cw_r2d_begin(NULL);
    cw_vec2 cv = cw_r2d_canvas_size();
    U = cv.y / 1080.0f;
    ascii_hud_begin(cv, U);
    float size = cv.y * 0.036f; /* body text: over the 2.4% floor (cartwheel/render2d.h) */
    switch (G.screen) {
    case SCR_SELECT: draw_select(cv, size); break;
    case SCR_DIFF: draw_diff(cv, size); break;
    case SCR_PLAY: draw_play(cv, size); break;
    case SCR_RESULTS: draw_results(cv, size); break;
    case SCR_CALIB: draw_calib(cv, size); break;
    default: break;
    }
    ascii_hud_end();
    cw_r2d_end();
  } else if (cw_shell_hud_shown()) ascii_hud_none();
  cw_shell_draw();
  cw_audio_update(); /* the game started the mixer (init), so the shell leaves its frame update to the game */
}

/* ── init ───────────────────────────────────────────────────────────── */

static int32_t lane_level_dbg[LANES];
static float lane_level_f[LANES];

static void cmd_streakfx(int argc, const char **argv, cw_reply *r) {
  if (argc >= 2) G.streakfx_off = !strcmp(argv[1], "off");
  cw_reply_printf(r, "{\"streakfx\":%d}", !G.streakfx_off);
}

static void cmd_leave(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  if (G.screen != SCR_PLAY) { cw_reply_error(r, "leave: only while a song plays"); return; }
  G.leave = true;
  cw_reply_printf(r, "{\"leave\":1}");
}

/* lanes <kick> <clap> <bass>: the river each lane is drawn as (0 left, 1 middle, 2 right; the game is 0 1 2). The
 * layout gate (tests/beatsync-layout.mjs) swaps it for its control, a layout that must fail */
static void cmd_lanes(int argc, const char **argv, cw_reply *r) {
  if (argc != 1 + SOLO_LANES) { cw_reply_error(r, "lanes <kick> <clap> <bass> (each 0 left, 1 middle, 2 right)"); return; }
  int p[SOLO_LANES];
  for (int l = 0; l < SOLO_LANES; l++) p[l] = atoi(argv[1 + l]);
  club_set_place(p);
  cw_reply_printf(r, "{\"kick\":%d,\"clap\":%d,\"bass\":%d}", club_place(0), club_place(1), club_place(2));
}

/* `hole <from_ms> <to_ms>`: cuts the notes between those song times out of the chart being played (a long wait to
 * test the wait indicator on, tests/beatsync-wait.mjs) */
static void cmd_hole(int argc, const char **argv, cw_reply *r) {
  if (argc < 3 || G.screen != SCR_PLAY) { cw_reply_error(r, "hole <from_ms> <to_ms> (while a song plays)"); return; }
  int32_t a = atoi(argv[1]), b = atoi(argv[2]), n = 0;
  const note *c = chart();
  for (int i = G.next; i < chart_count(); i++)
    if (c[i].ms >= a && c[i].ms < b && G.state[i] == NOTE_PENDING) G.state[i] = NOTE_GONE, n++;
  cw_reply_printf(r, "{\"cut\":%d}", n);
}
static void cmd_gap(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "gap game|off|force"); return; }
  G.gap_mode = !strcmp(argv[1], "off") ? 1 : !strcmp(argv[1], "force") ? 2 : 0;
  cw_reply_printf(r, "{\"gap\":%d}", G.gap_mode);
}
static void cmd_fromzero(int argc, const char **argv, cw_reply *r) {
  G.from_zero = argc > 1 && atoi(argv[1]) != 0;
  cw_reply_printf(r, "{\"fromzero\":%d}", G.from_zero);
}

static void cmd_othersrc(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) G.other_on_bass = atoi(argv[1]) == 0;
  cw_reply_printf(r, "{\"othersrc\":%d}", !G.other_on_bass);
}

static void cmd_sharedstreak(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) G.shared_streak = atoi(argv[1]) != 0;
  cw_reply_printf(r, "{\"sharedstreak\":%d}", G.shared_streak);
}
static void cmd_crowdstays(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) G.crowd_stays = atoi(argv[1]) != 0;
  cw_reply_printf(r, "{\"crowdstays\":%d}", G.crowd_stays);
}
static void cmd_padswap(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) G.pad_swap = atoi(argv[1]) != 0;
  cw_reply_printf(r, "{\"padswap\":%d}", G.pad_swap);
}

static void cmd_rules(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) G.old_rules = !strcmp(argv[1], "old");
  cw_reply_printf(r, "{\"rules\":\"%s\"}", G.old_rules ? "old" : "new");
}

#ifdef GAME_AUTOPILOT
static void cmd_skip(int argc, const char **argv, cw_reply *r) {
  int l = argc > 1 ? atoi(argv[1]) : -1;
  if (argc < 3 || l < 0 || l >= LANES) { cw_reply_error(r, "skip <lane 0..3> <n> [after_ms]"); return; }
  G.bot_skip[l] = atoi(argv[2]);
  G.bot_skip_after = argc > 3 ? atoi(argv[3]) : G.song_ms;
  cw_reply_printf(r, "{\"lane\":%d,\"n\":%d,\"after\":%d}", l, G.bot_skip[l], G.bot_skip_after);
}

static int diff_arg(const char *s) { return !strcmp(s, "easy") ? 0 : !strcmp(s, "medium") ? 1 : DIFF_HARD; }
/* song <index|id> [easy|medium|hard] [solo|drums|groove|duo|duo-swap] [easy|medium|hard]: the autopilot plays that
 * song from its start (the flash sweep's densest song): on the three lanes, one pair, or two players (player 1 on the
 * drums and player 2 on the groove, or swapped; the last difficulty is player 2's) */
static void cmd_song(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "song <index|id> [easy|medium|hard] [solo|drums|groove|duo|duo-swap] [easy|medium|hard]"); return; }
  int pick = -1;
  for (int i = 0; i < songs_count(); i++)
    if (!strcmp(song_get(i)->id, argv[1])) pick = i;
  if (pick < 0 && argv[1][0] >= '0' && argv[1][0] <= '9') pick = atoi(argv[1]);
  if (pick < 0 || pick >= songs_count()) { cw_reply_error(r, "song: no such song"); return; }
  int diff = DIFF_HARD;
  if (argc > 2) diff = diff_arg(argv[2]);
  const char *mode = argc > 3 ? argv[3] : "solo";
  bool duo = !strncmp(mode, "duo", 3);
  G.players = duo ? 2 : 1;
  G.solo_pick = !strcmp(mode, "drums") ? 1 : !strcmp(mode, "groove") ? 2 : 0;
  G.part[0] = !strcmp(mode, "duo-swap") ? PAIR_GROOVE : PAIR_DRUMS, G.part[1] = !G.part[0];
  G.pdiff[1] = argc > 4 ? diff_arg(argv[4]) : diff;
  G.sel = pick;
  G.pick = diff;
  play_song();
  cw_reply_printf(r, "{\"song\":%d,\"diff\":%d,\"mode\":%d,\"players\":%d,\"diff2\":%d}", pick, diff, G.mode, G.players, G.pdiff[1]);
}
#endif

static void init(void *user) {
  (void)user;
  cw_project_load_input();
  static const char *const LANE_ACTIONS[SOLO_LANES] = { "lane_kick", "lane_clap", "lane_bass" };
  hand_act[0] = cw_action_find("hand_left");
  hand_act[1] = cw_action_find("hand_right");
  for (int l = 0; l < SOLO_LANES; l++) {
    /* native builds: three touch zones, laid out every frame (touch_layout); in a cart the host draws the pad */
    touch_zone[l] = cw_touch_controls_on() ? cw_touch_add_button(0, 0, 1, 1) : -1;
    lane_act[l] = cw_action_find(LANE_ACTIONS[l]);
  }
  cw_quality_auto(true, 0);
  /* a song streams four tracks and maybe its vocals; changing songs starts the next while the last still fades out:
   * twice that, and room to spare (the shell starts the mixer with 8 streams unless the game has; when the game has,
   * the game updates it each frame, cw_audio_update in render) */
  cw_audio_init(&(cw_audio_desc){ .max_streams = 2 * ALL_TRACKS + 2 }); /* the game owns the mixer: render() updates it */
  cw_shell_init(&(cw_shell_desc){
    .title = "beatsync",
    .subtitle = "play the kick, the clap and the bass",
    .max_players = MAX_PLAYERS,
    .game_save_size = sizeof(save_data),
    .game_save_version = 1,
    .start = start,
    .pause_item = pause_item,
    .players_changed = players_changed,
    .sound = shell_sound,
    .screen_changed = shell_screen,
    .draw_background = draw_background,
    .accent = { 0.75f, 0.5f, 1.0f, 1 },
  });
  acc = cw_action_find("ui_accept");
  back = cw_action_find("ui_back");
  up = cw_action_find("ui_up");
  down = cw_action_find("ui_down");
  left = cw_action_find("ui_left");
  right = cw_action_find("ui_right");
  G.players = 1;
  music_init();
  snd_move = tone(880, 0.05f, 880, 0.15f, 3);
  snd_select = tone(660, 0.12f, 1320, 0.2f, 3);
  snd_back = tone(660, 0.12f, 330, 0.2f, 3);
  snd_click = tone(1500, 0.05f, 1500, 0.5f, 6);
  snd_fail = tone(330, 0.9f, 110, 0.25f, 2);
  snd_cheer = tone(523, 0.6f, 1046, 0.2f, 2);
  if (!songs_load()) cw_error("no song loaded: songs/<id>/song.json (docs/DESIGN.md)");
  G.offset_ms = save()->offset_ms;
  G.cue_lane = -1;
  cw_debug_i32("game.screen", &G.screen);
  cw_debug_i32("game.mode", &G.mode);
  cw_debug_i32("game.players", &G.players);
  cw_debug_field("game.part", G.part, CW_DBG_I32, MAX_PLAYERS);
  cw_debug_field("game.pdiff", G.pdiff, CW_DBG_I32, MAX_PLAYERS);
  cw_debug_field("game.ready", G.ready, CW_DBG_I32, MAX_PLAYERS);
  cw_debug_field("player.perfect", G.p_perfect, CW_DBG_I32, MAX_PLAYERS);
  cw_debug_field("player.good", G.p_good, CW_DBG_I32, MAX_PLAYERS);
  cw_debug_field("player.miss", G.p_miss, CW_DBG_I32, MAX_PLAYERS);
  cw_debug_field("player.total", G.p_total, CW_DBG_I32, MAX_PLAYERS);
  cw_debug_i32("game.solo_pick", &G.solo_pick);
  cw_debug_i32("song.index", &G.sel);
  cw_debug_i32("song.diff", &G.diff);
  cw_debug_i32("song.ms", &G.song_ms);
  cw_debug_i32("song.raw_ms", &G.raw_ms);
  cw_debug_i32("song.done", &G.done);
  cw_debug_i32("song.failed", &G.failed);
  cw_debug_i32("notes.total", &G.total);
  cw_debug_i32("notes.hit", &G.hit);
  cw_debug_i32("notes.perfect", &G.perfect);
  cw_debug_i32("notes.good", &G.good);
  cw_debug_i32("notes.miss", &G.miss);
  cw_debug_i32("game.combo", &G.st[0].combo); /* player 1's (the only one alone); player 2's: streak2.* */
  cw_debug_i32("game.max_combo", &G.st[0].max_combo);
  cw_debug_i32("streak2.combo", &G.st[1].combo);
  cw_debug_i32("streak2.max_combo", &G.st[1].max_combo);
  cw_debug_i32("streak2.tiers", &G.st[1].tier_seq);
  cw_debug_i32("streak2.breaks", &G.st[1].break_seq);
  cw_debug_f32("streak2.charge", &G.st[1].charge);
  cw_debug_i32("game.score", &G.score);
  cw_debug_f32("game.energy", &G.energy);
  cw_debug_f32_array("lane.level", G.level, LANES);
  cw_debug_f32_array("lane.gain", G.gain, LANES);
  cw_debug_i32("cue.lane", &G.cue_lane);
  cw_debug_i32("cue.ms", &G.cue_ms);
  cw_debug_f32("judge.err_sum", &G.err_sum);
  cw_debug_f32("judge.err_sq", &G.err_sq);
  cw_debug_i32("judge.err_n", &G.err_n);
  cw_debug_i32("judge.err_max", &G.err_max);
  cw_debug_i32("bot.late_ms", &G.bot_late_ms);
  cw_debug_i32("bot.skipped_ms", &G.bot_skipped_ms);
  cw_debug_i32("game.offset_ms", &G.offset_ms);
  cw_debug_i32("game.ticks", &G.ticks);
  (void)lane_level_dbg, (void)lane_level_f;
  music_register_debug();
  club_register_debug();
  cw_debug_command("leave", "leave: the crowd leaves the song now (the failed song, for the flash check)", cmd_leave);
  cw_debug_command("lanes", "lanes <kick> <clap> <bass>: the river each lane is drawn as, 0 left 1 middle 2 right (the layout gate's control)", cmd_lanes);
  cw_debug_i32("song.pick", &G.pick);
  cw_debug_i32("song.from_ms", &G.from_ms);
  cw_debug_i32("gap.state", &G.gap_state);
  cw_debug_i32("gap.count", &G.gap_count);
  cw_debug_i32("gap.unit", &G.gap_unit);
  cw_debug_f32("gap.fill", &G.gap_fill);
  cw_debug_f32("gap.alpha", &G.gap_alpha);
  cw_debug_i32("gap.shown", &G.gap_shown);
  cw_debug_i32("gap.overlap", &G.gap_overlap);
  cw_debug_i32("gap.mode", &G.gap_mode);
  cw_debug_command("hole", "hole <from_ms> <to_ms>: cut the notes between those song times out of the chart playing (the wait gate's long wait)", cmd_hole);
  cw_debug_command("gap", "gap game|off|force: the wait indicator as the game shows it, never, or always (the wait gate's controls)", cmd_gap);
  cw_debug_command("fromzero", "fromzero 1|0: play every song from its top, not 8 beats before its first note (the start gate's control)", cmd_fromzero);
  cw_debug_command("othersrc", "othersrc 1|0: a note made from the other track plays its hit and miss on the other track (1, the game) or on the bass lane (0, the bass gate's control)", cmd_othersrc);
  cw_debug_command("sharedstreak", "sharedstreak 1|0: one streak for both players, a break by either ends it (the duo gate's control: each player keeps their own)", cmd_sharedstreak);
  cw_debug_command("crowdstays", "crowdstays 1|0: the floor never empties however many notes are missed (the duo gate leaves one player idle)", cmd_crowdstays);
  cw_debug_command("padswap", "padswap 1|0: with two players, each plays on the other's pad (the duo gate's control: a pad must play only its own player's lanes)", cmd_padswap);
  cw_debug_command("rules", "rules old|new: the judgement as it was (35/70 ms everywhere, a wrong button misses the note; the forgiving gate's control) or as it is", cmd_rules);
#ifdef GAME_AUTOPILOT
  cw_debug_command("skip", "skip <lane> <n> [after_ms]: the autopilot lets the next n notes of a lane (0 kick, 1 clap, 2 bass) after after_ms go by, so they are missed (the mix gate)", cmd_skip);
  cw_debug_command("song", "song <index|id> [easy|medium|hard]: the autopilot plays that song from its start", cmd_song);
#endif
  flash_meter_init(&fl_glow, "hit");
  flash_meter_init(&fl_fade, "fail");
  flash_meter_init(&fl_tier[0], "tier");
  flash_meter_init(&fl_drain[0], "drain");
  flash_meter_init(&fl_tier[1], "tier2");
  flash_meter_init(&fl_drain[1], "drain2");
  flash_meter_register(&fl_glow);
  flash_meter_register(&fl_fade);
  for (int p = 0; p < MAX_PLAYERS; p++) flash_meter_register(&fl_tier[p]), flash_meter_register(&fl_drain[p]);
  cw_debug_f32("streak.charge", &G.st[0].charge);
  cw_debug_f32("streak.tier_t", &G.st[0].tier_t);
  cw_debug_f32("streak.drain_t", &G.st[0].drain_t);
  cw_debug_f32("streak.drain_from", &G.st[0].drain_from);
  cw_debug_i32("streak.tiers", &G.st[0].tier_seq);
  cw_debug_i32("streak.breaks", &G.st[0].break_seq);
  cw_debug_i32("streak.milestone", &G.st[0].milestone);
  cw_debug_command("streakfx", "streakfx on|off: the streak's charge, bands, drain and badge as the game shows them, or none (the streak gate's control)", cmd_streakfx);
#ifdef GAME_AUTOPILOT
  cw_shell_start(1); /* straight into play, no menus */
#else
  title_demo();
#endif
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .width = 960, .height = 540, .debug = true, .init = init, .fixed_update = fixed_update, .update = update, .render = render };
  cw_project_apply_app(&app); /* render.* and the name from project.cwproj */
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &app);
}
