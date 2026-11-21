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
#define TIER_COMBO 16 /* hits a multiplier step: x2 at 16, x3 at 32, x4 at 48 */
#define MILESTONE 50  /* a streak of every 50 is called out */
#ifdef GAME_AUTOPILOT
#define AUTOPILOT 1
#else
#define AUTOPILOT 0
#endif

enum { SCR_NONE, SCR_SELECT, SCR_DIFF, SCR_PLAY, SCR_RESULTS, SCR_CALIB };
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

typedef struct save_data {
  int32_t offset_ms; /* the calibration: how late this device's sound reaches the player */
  int32_t reserved;
  best_entry best[MAX_SONGS];
} save_data;

static flash_meter fl_glow, fl_fade, fl_tier, fl_drain;

static struct {
  int32_t screen, sel, diff, pick, item;      /* the game's own screen, the song and difficulty chosen, the menu item */
  const song *s;
  uint8_t state[MAX_NOTES];
  uint8_t spoiled[MAX_NOTES];
  int32_t hit_seq[LANES];     /* the player's hits per lane (the club bursts the gate on each) */
  uint8_t hit_perfect[LANES]; /* a wrong button was pressed while this note was due: it scores Good at best */
  int32_t next;                         /* first note not judged yet */
  int32_t song_ms, raw_ms, done, failed, ended_ticks;
  int32_t total, hit, perfect, good, miss, combo, max_combo, score, best_score, new_best;
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
  float charge, tier_t, tier_light, drain_t, drain_from, drain_light;
  int32_t break_seq, tier_seq, broke_combo, broke_ticks, milestone, milestone_ticks;
  bool streakfx_off; /* the `streakfx off` request: none of it (the streak gate's control) */
  float flare[LANES], glow[LANES], glow_drawn[LANES], press[LANES];
  int32_t judge[LANES], judge_ticks[LANES];
  float fade; /* the failed song's dip, as drawn */
  int32_t preview_wait;
  bool demo;  /* the sequencer plays a song by itself (title, song select) */
  bool other_on_bass; /* the `othersrc 0` request: a note made from the other track plays on the bass lane, as 0.32.2 did (the bass gate's control) */
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

static cw_action lane_act[LANES], acc, back, up, down;
static cw_sound snd_move, snd_select, snd_back, snd_click, snd_fail, snd_cheer;
static int touch_zone[LANES];

static save_data *save(void) { return (save_data *)cw_shell_game_save(); }

static best_entry *best_of(const song *s) {
  save_data *sv = save();
  for (int i = 0; i < MAX_SONGS; i++)
    if (sv->best[i].hash == s->hash) return &sv->best[i];
  for (int i = 0; i < MAX_SONGS; i++)
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
/* the `rules old` request (the forgiving gate's control): the windows and the wrong-button miss as they were */
#define PERFECT_MS (G.old_rules ? 35 : PERFECT_WIN[G.diff])
#define GOOD_MS (G.old_rules ? 70 : GOOD_WIN[G.diff])

static const note *chart(void) { return G.s ? G.s->chart[G.diff] : NULL; }
static int chart_count(void) { return G.s ? CW_MIN(G.s->notes[G.diff], MAX_NOTES) : 0; }

/* start the song s at difficulty d: from_ms with a lead-in, or a preview */
static void begin_song(const song *s, int d, int32_t from_ms, int32_t lead_ms, float volume) {
  G.s = s;
  G.diff = d;
  memset(G.state, NOTE_PENDING, sizeof G.state);
  memset(G.spoiled, 0, sizeof G.spoiled);
  G.next = 0;
  const note *c = chart();
  int n = chart_count();
  while (G.next < n && c[G.next].ms < from_ms) G.state[G.next++] = NOTE_HIT; /* a preview starts part way */
  G.total = n;
  G.hit = G.perfect = G.good = G.miss = G.combo = G.max_combo = G.score = 0;
  G.done = G.failed = G.ended_ticks = G.new_best = 0;
  G.energy = 0.6f;
  G.err_sum = G.err_sq = 0;
  G.err_max = G.err_n = G.miss_streak = 0;
  G.fade = 0;
  G.gap_state = G.gap_count = G.gap_shown = G.gap_overlap = 0;
  G.gap_alpha = G.gap_fill = 0;
  for (int l = 0; l < LANES; l++) G.judge_ticks[l] = 0;
  G.charge = 0, G.tier_t = G.drain_t = -1, G.broke_ticks = G.milestone_ticks = 0;
  music_set_player(!G.demo);
  music_play(s, from_ms, lead_ms, volume);
}

static void to_screen(int scr) {
  G.screen = scr;
  G.item = 0;
  G.screen_ticks = 0;
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
static int32_t play_from_ms(const song *s, int d) {
  if (!s || s->beats < 2 || !s->notes[d] || G.from_zero) return 0;
  const note *first = &s->chart[d][0];
  int b = first->step / 4 - START_BEATS;
  /* on the song's own beats; before its first tracked beat (a long intro with no drums) the tempo carries on back */
  int32_t from = b >= 0 && b < s->beats ? s->beat_ms[b] : first->ms - (int32_t)lroundf(START_BEATS * 60000.0f / fmaxf(s->bpm, 1));
  return from > 2000 ? from : 0; /* a couple of seconds in: from the top */
}

static void play_song(void) {
  G.demo = false;
  const song *s = song_get(G.sel);
  G.from_ms = play_from_ms(s, G.pick);
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
static void combo_lost(void) {
  if (G.combo >= TIER_COMBO && !G.demo) {
    G.drain_t = 0;
    G.drain_from = G.charge;
    G.break_seq++;
    G.broke_combo = G.combo;
    G.broke_ticks = 70;
    G.milestone_ticks = 0; /* what was lost replaces the call-out */
    cw_input_rumble(0, 0.25f + 0.1f * (float)(mult_of(G.combo) - 1), 0.1f, 160);
  }
  G.combo = 0;
  G.charge = 0;
}

static void judged(int i, int j, int32_t err) {
  int lane = chart()[i].lane;
  G.state[i] = j == J_MISS ? NOTE_MISSED : NOTE_HIT;
  G.judge[lane] = j;
  G.judge_ticks[lane] = 40;
  if (j == J_MISS) {
    G.miss++;
    combo_lost();
    G.miss_streak++;
    music_miss(chart()[i].other && !G.other_on_bass ? MUSIC_OTHER : lane); /* a note made from the other track dips that track */
    if (!G.demo) {
      G.energy -= G.diff == DIFF_EASY ? 0.05f : 0.075f;
      cw_input_rumble(0, 0.35f, 0.15f, 90); /* a miss is felt in the pad, never a shake */
    }
    return;
  }
  G.hit++;
  G.hit_seq[lane]++;
  G.hit_perfect[lane] = j == J_PERFECT;
  G.combo++;
  G.miss_streak = 0;
  if (G.combo > G.max_combo) G.max_combo = G.combo;
  if (j == J_PERFECT) G.perfect++;
  else G.good++;
  int mult = mult_of(G.combo);
  if (mult > mult_of(G.combo - 1)) G.tier_t = 0, G.tier_seq++; /* a multiplier step: a band rises up the rivers */
  if (G.combo % MILESTONE == 0 && !G.demo) G.milestone = G.combo, G.milestone_ticks = 100;
  G.score += (j == J_PERFECT ? 300 : 100) * mult;
  G.energy = fminf(1, G.energy + (j == J_PERFECT ? 0.03f : 0.015f));
  G.err_sum += (float)err;
  G.err_sq += (float)err * err;
  G.err_n++;
  if (abs(err) > G.err_max) G.err_max = abs(err);
  music_hit(chart()[i].other && !G.other_on_bass ? MUSIC_OTHER : lane);
  G.flare[lane] = fminf(1, G.flare[lane] + 0.09f); /* each hit adds a little: a run of hits builds the glow up */
  if (!G.demo && lane == LANE_KICK) cw_input_rumble(0, 0.12f, 0, 40); /* feel the kick */
}

/* a press in a lane at the song time t (the clock less the device's offset) */
static void press(int lane, int32_t t, bool mash) {
  const note *c = chart();
  int n = chart_count();
  G.press[lane] = 1;
  /* the note it was meant for: the nearest not yet judged in this lane within the Good window */
  int best = -1;
  int32_t berr = GOOD_MS + 1;
  int wrong = -1; /* the nearest note due in another lane: this press was the wrong button for it */
  int32_t werr = GOOD_MS + 1;
  for (int i = G.next; i < n && c[i].ms <= t + GOOD_MS; i++) {
    if (G.state[i] != NOTE_PENDING) continue;
    int32_t e = t - c[i].ms;
    if (c[i].lane != lane && G.old_rules) { /* as it was: the nearest note in any lane, a wrong button misses it */
      if (abs(e) < abs(berr)) berr = e, best = i;
      continue;
    }
    if (c[i].lane != lane) {
      if (abs(e) <= GOOD_MS && abs(e) < abs(werr)) werr = e, wrong = i;
      continue;
    }
    if (abs(e) < abs(berr)) berr = e, best = i;
  }
  if (best < 0) {
    /* the wrong button costs the combo and that note's Perfect, never the note: it can still be hit in its own lane
     * (so a slip is forgiven, and mashing every button scores Good at best with no multiplier) */
    if (wrong >= 0) combo_lost(), G.spoiled[wrong] = 1;
    return; /* nothing near: a free tap */
  }
  if (G.old_rules && c[best].lane != lane) { judged(best, J_MISS, 0); return; }
  if (mash && !G.old_rules) G.spoiled[best] = 1;
  judged(best, abs(berr) <= PERFECT_MS && !G.spoiled[best] ? J_PERFECT : J_GOOD, berr);
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
  float want = fminf(1, (float)G.combo / (float)(3 * TIER_COMBO));
  G.charge = want > G.charge ? fminf(want, G.charge + dt / 0.35f) : want;
  if (G.tier_t >= 0) G.tier_t = G.tier_t + dt < TIER_S ? G.tier_t + dt : -1;
  if (G.drain_t >= 0) G.drain_t = G.drain_t + dt < DRAIN_S ? G.drain_t + dt : -1;
  float te = G.tier_t < 0 ? 0 : G.tier_t < 0.06f ? G.tier_t / 0.06f : powf(1 - (G.tier_t - 0.06f) / (TIER_S - 0.06f), 1.5f);
  float de = G.drain_t < 0 ? 0 : G.drain_t < 0.05f ? G.drain_t / 0.05f : 1 - (G.drain_t - 0.05f) / (DRAIN_S - 0.05f);
  G.tier_light = flash_meter_feed(&fl_tier, te);
  G.drain_light = flash_meter_feed(&fl_drain, de);
  if (G.broke_ticks) G.broke_ticks--;
  if (G.milestone_ticks) G.milestone_ticks--;
  /* the receptors' lights (a small area round each, but metered all the same: the strongest lane) */
  float g = fmaxf(G.glow[0], fmaxf(G.glow[1], G.glow[2]));
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
    int down = 0;
    for (int l = 0; l < LANES; l++) down += cw_action_just_pressed(0, lane_act[l]);
    if (down > 1) combo_lost();
    for (int l = 0; l < LANES; l++)
      if (cw_action_just_pressed(0, lane_act[l])) press(l, t, down > 1);
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
  for (int i = G.next; i < n && c[i].ms < t - GOOD_MS; i++)
    if (G.state[i] == NOTE_PENDING || G.state[i] == NOTE_SKIPPED) judged(i, J_MISS, 0);
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

static void start(int players, void *user) {
  (void)players, (void)user;
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
  for (int l = 0; l < LANES; l++) tap |= cw_action_just_pressed(0, lane_act[l]);
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
  int n = menu_count();
  int *cursor = G.screen == SCR_SELECT ? &G.sel : G.screen == SCR_DIFF ? &G.pick : &G.item;
  if (G.screen == SCR_SELECT && G.sel > n - 1) G.sel = n - 1;
  int was = *cursor;
  if (cw_action_just_pressed(0, up)) *cursor = (*cursor + n - 1) % n;
  if (cw_action_just_pressed(0, down)) *cursor = (*cursor + 1) % n;
  if (*cursor != was) {
    play(snd_move);
    if (G.screen == SCR_SELECT) G.preview_wait = 20; /* the preview follows once the choice rests */
  }
  if (G.screen == SCR_SELECT && G.preview_wait && --G.preview_wait == 0 && G.sel < songs_count()) start_preview();
  if (cw_action_just_pressed(0, acc)) {
    play(snd_select);
    if (G.screen == SCR_SELECT) {
      if (G.sel == songs_count()) calib_start();
      else {
        if (G.preview_wait) { G.preview_wait = 0; start_preview(); }
        to_screen(SCR_DIFF);
      }
    } else if (G.screen == SCR_DIFF) play_song();
    else if (G.screen == SCR_RESULTS) {
      if (G.item == 0) play_song();
      else { to_screen(SCR_SELECT); start_preview(); }
    }
    return;
  }
  if (cw_action_just_pressed(0, back)) {
    play(snd_back);
    if (G.screen == SCR_SELECT) { music_stop(0.3f); cw_shell_to_title(); }
    else if (G.screen == SCR_DIFF) to_screen(SCR_SELECT);
    else if (G.screen == SCR_RESULTS) { to_screen(SCR_SELECT); start_preview(); }
  }
}

static void finish_song(void) {
  G.leave = false;
  G.done = G.failed ? 0 : 1;
  best_entry *b = best_of(G.s);
  G.best_score = b ? b->score[G.diff] : 0;
  if (b && !G.failed && G.score > b->score[G.diff]) {
    b->score[G.diff] = G.score;
    G.best_score = G.score;
    G.new_best = 1;
  }
  int gr = grade_of();
  if (b && (!b->grade[G.diff] || gr + 1 < b->grade[G.diff])) b->grade[G.diff] = (uint8_t)(gr + 1);
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
static void gap_tick(float dt) {
  const note *c = chart();
  float beat = G.s && G.s->bpm > 0 ? 60000.0f / G.s->bpm : 500;
  float t = (float)(G.song_ms - G.offset_ms);
  int last = G.next - 1;
  while (last >= 0 && G.state[last] == NOTE_GONE) last--;
  float prev = last >= 0 ? (float)c[last].ms : (float)G.from_ms, next = (float)G.cue_ms;
  float appear = next - AHEAD_BEATS[G.diff] * beat; /* when the next note comes onto the far end of its river */
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

static void play_tick(float dt) {
  tick_song(dt, true);
  gap_tick(dt);
  if ((G.energy <= 0 || G.leave) && !G.failed && (G.leave || !AUTOPILOT)) { /* the floor empties: the song fades out */
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
      cw_text_style st = ts(size, on ? WHITE : DIM);
      cw_r2d_text(font(), s->title, cw_v2(lp.x + size * 0.7f, y), &st);
      char b[16];
      snprintf(b, sizeof b, "%.0f", s->bpm);
      st = ts(size * 0.8f, DIM);
      cw_r2d_text(font(), b, cw_v2(lp.x + lp.w - size * 0.7f - cw_font_measure(font(), b, &st).width, y + size * 0.1f), &st);
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
    text_at(s->title, rp.x + size * 0.6f, rp.y + size * 0.3f, size * 1.3f, WHITE, -1);
    if (s->artist[0]) text_at(s->artist, rp.x + size * 0.6f, rp.y + size * 2.0f, size * 0.85f, DIM, -1);
    int len = (int)s->length_s;
    snprintf(b, sizeof b, "%.0f BPM    %d:%02d", s->bpm, len / 60, len % 60);
    text_at(b, rp.x + size * 0.6f, rp.y + size * 3.2f, size * 0.9f, DIM, -1);
    best_entry *be = best_of(s);
    for (int d = 0; d < DIFFS; d++) {
      float y = rp.y + size * (5.0f + (float)d * 1.6f);
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

static void draw_diff(cw_vec2 cv, float size) {
  const song *s = song_get(G.sel);
  if (!s) return;
  text_at(s->title, cv.x * 0.5f, cv.y * 0.05f, size * 1.4f, WHITE, 0);
  best_entry *be = best_of(s);
  static const char *const HINT[DIFFS] = { "on the beat", "eighths", "sixteenths" };
  char b[64];
  /* three cards down the left and right of the rivers: Easy and Medium left, Hard right */
  for (int d = 0; d < DIFFS; d++) {
    float w = cv.x * 0.22f, h = cv.y * 0.22f;
    cw_rect r = { d < 2 ? cv.x * 0.04f : cv.x * 0.74f, d == 1 ? cv.y * 0.45f : cv.y * 0.18f, w, h };
    panel(r, d == G.pick ? ACCENT : PANEL_EDGE);
    if (d == G.pick) select_bar((cw_rect){ r.x + size * 0.2f, r.y + size * 0.2f, r.w - size * 0.4f, size * 1.6f });
    text_at(diff_name(d), r.x + r.w * 0.5f, r.y + size * 0.3f, size * 1.25f, d == G.pick ? WHITE : DIM, 0);
    text_at(HINT[d], r.x + r.w * 0.5f, r.y + size * 2.0f, size * 0.85f, DIM, 0);
    snprintf(b, sizeof b, "%d notes", s->notes[d]);
    text_at(b, r.x + r.w * 0.5f, r.y + size * 3.2f, size * 0.85f, WHITE, 0);
    if (be && be->grade[d]) snprintf(b, sizeof b, "best %s  %d", GRADES[be->grade[d] - 1], be->score[d]);
    else snprintf(b, sizeof b, "new");
    text_at(b, r.x + r.w * 0.5f, r.y + size * 4.4f, size * 0.85f, DIM, 0);
  }
  ptext("{ui_up|ui_down} choose    {ui_accept} play    {ui_back} songs", cv.x * 0.5f, cv.y * 0.9f, size * 0.85f, (cw_color){ 0 });
}

static void draw_play(cw_vec2 cv, float size) {
  char b[64];
  if (G.s) { /* the song and how far through it, top left */
    text_at(G.s->title, size * 0.8f, size * 0.5f, size, WHITE, -1);
    float p = G.s->length_s > 0 ? CW_CLAMP((float)G.song_ms / (G.s->length_s * 1000), 0, 1) : 0, w = cv.x * 0.18f;
    ascii_hud_rect(size * 0.8f, size * 1.9f, w, size * 0.25f, 1);
    cw_r2d_round_rect((cw_rect){ size * 0.8f, size * 1.9f, w, size * 0.25f }, size * 0.12f, 4, cw_rgba(1, 1, 1, 0.18f));
    cw_r2d_round_rect((cw_rect){ size * 0.8f, size * 1.9f, w * p, size * 0.25f }, size * 0.12f, 4, cw_rgba(0.7f, 0.45f, 1, 0.95f));
    text_at(diff_name(G.diff), size * 0.8f, size * 2.5f, size * 0.8f, DIM, -1);
  }
  snprintf(b, sizeof b, "%d", G.score); /* the score and the streak, top right */
  text_at(b, cv.x - size * 0.8f, size * 0.4f, size * 1.4f, WHITE, 1);
  if (G.combo >= 4) {
    snprintf(b, sizeof b, "%d streak", G.combo);
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
  int jl = -1;
  for (int l = 0; l < LANES; l++)
    if (G.judge_ticks[l] && (jl < 0 || G.judge_ticks[l] > G.judge_ticks[jl])) jl = l;
  if (jl >= 0 && !G.failed) {
    float x, y, t = (float)G.judge_ticks[jl] / 40.0f;
    club_gate_screen(jl, cv.x, cv.y, &x, &y);
    y += size * 1.5f;
    cw_color c = JUDGE_COL[G.judge[jl]];
    c.a = fminf(1, t * 2.5f);
    text_at(JN[G.judge[jl]], x, y - size * 0.7f - (1 - t) * size * 0.4f, size * (G.judge[jl] == J_PERFECT ? 1.1f : 0.95f), c, 0);
  }
  /* the multiplier, under the middle gate where the eyes already are: x1 to x4 and the hits to the next step as a
   * bar; every 50 the streak is called out there instead, rising and fading; a broken streak shows what was lost */
  if (G.song_ms >= 0 && !G.failed && !G.streakfx_off) {
    float x, y;
    club_gate_screen(LANE_CLAP, cv.x, cv.y, &x, &y);
    y += size * 2.35f;
    int m = mult_of(G.combo);
    if (G.milestone_ticks) {
      float t = (float)G.milestone_ticks / 100.0f, a = fminf(1, t * 3) * fminf(1, (1 - t) * 8 + 0.2f);
      snprintf(b, sizeof b, "%d streak", G.milestone);
      text_at(b, x, y - (1 - t) * size * 0.5f, size * 1.15f, cw_rgba(1, 0.95f, 0.7f, a), 0);
    } else if (G.broke_ticks) {
      float a = fminf(1, (float)G.broke_ticks / 30.0f);
      cw_color c = JUDGE_COL[J_MISS];
      c.a = a;
      snprintf(b, sizeof b, "streak %d lost", G.broke_combo);
      text_at(b, x, y, size * 0.9f, c, 0);
    } else if (G.combo > 0) {
      float w = size * 3.6f, bh = size * 0.22f, prog = m >= 4 ? 1 : (float)(G.combo % TIER_COMBO) / TIER_COMBO;
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
  static const char *const LP[LANES] = { "{lane_kick} kick", "{lane_clap} clap", "{lane_bass} bass" };
  if (G.song_ms < 0 && !G.failed) {
    float fade = fminf(1, (float)-G.song_ms / 300.0f);
    for (int l = 0; l < LANES; l++) {
      float hx, hy, sz = size * 0.95f;
      club_gate_screen(l, cv.x, cv.y, &hx, &hy);
      cw_vec2 ps = cw_prompt_text_size(font(), LP[l], 0, sz);
      float x = hx, y = hy + size * 1.0f;
      ascii_hud_rect_edge(x - ps.x * 0.5f, y, ps.x, ps.y, fade, lane_color(l, fade));
      cw_prompt_text(font(), LP[l], 0, cw_v2(x, y), sz, lane_color(l, fade), CW_ALIGN_CENTER);
    }
  }
  if (cw_touch_overlay_visible()) /* on a touch screen: the three zones across the bottom */
    for (int l = 0; l < LANES; l++)
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
  LINE(G.s ? G.s->title : "", r.y + size * 0.6f, size * 1.2f, WHITE);
  snprintf(b, sizeof b, "%s  %s", diff_name(G.diff), G.failed ? "the floor emptied" : "cleared");
  LINE(b, r.y + size * 2.2f, size * 0.85f, DIM);
  LINE(GRADES[grade_of()], r.y + size * 3.4f, size * 3.6f, ACCENT);
  snprintf(b, sizeof b, "%d", G.score);
  LINE(b, r.y + size * 7.6f, size * 1.4f, WHITE);
  if (G.new_best) LINE("new best", r.y + size * 9.3f, size * 0.85f, ACCENT);
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
  snprintf(b, sizeof b, "longest streak %d of %d", G.max_combo, G.total);
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
  (void)screen, (void)user;
  float k = cs.y / 1080.0f;
  ascii_hud_reset(cs, k);
  ascii_hud_rect(cs.x * 0.3f, cs.y * 0.08f, cs.x * 0.4f, cs.y * 0.7f, 1);
  ascii_hud_rect(cs.x * 0.3f, cs.y * 0.895f, cs.x * 0.4f, cs.y * 0.075f, 1);
  ascii_hud_end();
  ascii_hud_draw();
}

static void touch_layout(void) {
  if (!cw_touch_controls_on()) return; /* in a cart the host draws the pad (the manifest's controls) */
  int w, h;
  cw_window_size(&w, &h);
  for (int l = 0; l < LANES; l++) cw_touch_set_rect(touch_zone[l], (float)w * (float)l / 3.0f, (float)h * 0.55f, (float)w / 3.0f, (float)h * 0.45f);
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
    .dim = menu ? 1 : 0, .zoom = 1, .spread = G.screen == SCR_NONE ? 2.3f : 1, .ahead = AHEAD_BEATS[G.diff], .note_light = NOTE_LIGHT[G.diff], .t = G.vis_t,
    .gap_fill = G.screen == SCR_PLAY ? G.gap_fill : 0, .gap_alpha = G.screen == SCR_PLAY ? G.gap_alpha : 0,
  };
  bool fx = G.screen == SCR_PLAY && !G.streakfx_off;
  v.charge = fx ? G.charge : 0;
  v.tier_t = fx ? G.tier_t : -1, v.tier_light = fx ? G.tier_light : 0;
  v.drain_t = fx ? G.drain_t : -1, v.drain_from = G.drain_from, v.drain_light = fx ? G.drain_light : 0;
  v.break_seq = fx ? G.break_seq : 0;
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
  if (argc != 1 + LANES) { cw_reply_error(r, "lanes <kick> <clap> <bass> (each 0 left, 1 middle, 2 right)"); return; }
  int p[LANES];
  for (int l = 0; l < LANES; l++) p[l] = atoi(argv[1 + l]);
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

static void cmd_rules(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) G.old_rules = !strcmp(argv[1], "old");
  cw_reply_printf(r, "{\"rules\":\"%s\"}", G.old_rules ? "old" : "new");
}

#ifdef GAME_AUTOPILOT
static void cmd_skip(int argc, const char **argv, cw_reply *r) {
  int l = argc > 1 ? atoi(argv[1]) : -1;
  if (argc < 3 || l < 0 || l >= LANES) { cw_reply_error(r, "skip <lane 0..2> <n> [after_ms]"); return; }
  G.bot_skip[l] = atoi(argv[2]);
  G.bot_skip_after = argc > 3 ? atoi(argv[3]) : G.song_ms;
  cw_reply_printf(r, "{\"lane\":%d,\"n\":%d,\"after\":%d}", l, G.bot_skip[l], G.bot_skip_after);
}

/* song <index|id> [easy|medium|hard]: the autopilot plays that song from its start (the flash sweep's densest song) */
static void cmd_song(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "song <index|id> [easy|medium|hard]"); return; }
  int pick = -1;
  for (int i = 0; i < songs_count(); i++)
    if (!strcmp(song_get(i)->id, argv[1])) pick = i;
  if (pick < 0 && argv[1][0] >= '0' && argv[1][0] <= '9') pick = atoi(argv[1]);
  if (pick < 0 || pick >= songs_count()) { cw_reply_error(r, "song: no such song"); return; }
  int diff = DIFF_HARD;
  if (argc > 2) diff = !strcmp(argv[2], "easy") ? 0 : !strcmp(argv[2], "medium") ? 1 : DIFF_HARD;
  G.sel = pick;
  G.pick = diff;
  play_song();
  cw_reply_printf(r, "{\"song\":%d,\"diff\":%d}", pick, diff);
}
#endif

static void init(void *user) {
  (void)user;
  cw_project_load_input();
  static const char *const LANE_ACTIONS[LANES] = { "lane_kick", "lane_clap", "lane_bass" };
  for (int l = 0; l < LANES; l++) {
    /* native builds: three touch zones, laid out every frame (touch_layout); in a cart the host draws the pad */
    touch_zone[l] = cw_touch_controls_on() ? cw_touch_add_button(0, 0, 1, 1) : -1;
    lane_act[l] = cw_action_find(LANE_ACTIONS[l]);
  }
  cw_quality_auto(true, 0);
  cw_shell_init(&(cw_shell_desc){
    .title = "beatsync",
    .subtitle = "play the kick, the clap and the bass",
    .max_players = 1,
    .game_save_size = sizeof(save_data),
    .game_save_version = 1,
    .start = start,
    .sound = shell_sound,
    .screen_changed = shell_screen,
    .draw_background = draw_background,
    .accent = { 0.75f, 0.5f, 1.0f, 1 },
  });
  acc = cw_action_find("ui_accept");
  back = cw_action_find("ui_back");
  up = cw_action_find("ui_up");
  down = cw_action_find("ui_down");
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
  cw_debug_i32("game.combo", &G.combo);
  cw_debug_i32("game.max_combo", &G.max_combo);
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
  cw_debug_command("rules", "rules old|new: the judgement as it was (35/70 ms everywhere, a wrong button misses the note; the forgiving gate's control) or as it is", cmd_rules);
#ifdef GAME_AUTOPILOT
  cw_debug_command("skip", "skip <lane> <n> [after_ms]: the autopilot lets the next n notes of a lane (0 kick, 1 clap, 2 bass) after after_ms go by, so they are missed (the mix gate)", cmd_skip);
  cw_debug_command("song", "song <index|id> [easy|medium|hard]: the autopilot plays that song from its start", cmd_song);
#endif
  flash_meter_init(&fl_glow, "hit");
  flash_meter_init(&fl_fade, "fail");
  flash_meter_init(&fl_tier, "tier");
  flash_meter_init(&fl_drain, "drain");
  flash_meter_register(&fl_glow);
  flash_meter_register(&fl_fade);
  flash_meter_register(&fl_tier);
  flash_meter_register(&fl_drain);
  cw_debug_f32("streak.charge", &G.charge);
  cw_debug_f32("streak.tier_t", &G.tier_t);
  cw_debug_f32("streak.drain_t", &G.drain_t);
  cw_debug_f32("streak.drain_from", &G.drain_from);
  cw_debug_i32("streak.tiers", &G.tier_seq);
  cw_debug_i32("streak.breaks", &G.break_seq);
  cw_debug_i32("streak.milestone", &G.milestone);
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
