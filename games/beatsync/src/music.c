/* The song playing (music.h).
 *
 * The four tracks start on the same mixer frame (cw_play_desc.start_frame), so they stay sample-locked. The song
 * clock is the "other" track's cw_voice_frames_played over its rate: exact to the sample, but only as of the last
 * mixed block, so between blocks the clock runs on by the tick's time and is pulled toward the audio a little at
 * each new block (a jump larger than RESYNC_MS snaps it: a stall, a seek). Before the song starts the clock counts
 * the mixer's frames up to the start frame, so it runs smoothly from the lead-in into the song.
 *
 * Bus graph: music (the shell's, the Music volume) <- song (the song's own level, the failed song's fade) <- kick,
 * clap, bass (one bus a lane, each with a low-pass) and the other track straight into song.
 *
 * The lanes in play: each sits a little under the record between the player's hits, and a hit punches it to full
 * and opens its top (the drum or bass note just played stands out), falling back over about a quarter second. A miss
 * drops only that lane, with a low-pass, and it comes back by itself over a beat (or at the next hit): a few misses
 * never empty the mix. */
#include "music.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/shell.h"

#define RESYNC_MS 120.0
#define PULL 0.12     /* share of the clock's error to the audio taken at each new block */
/* A lane's level in play: IDLE between the player's hits (a little under the record, with its top a little dulled),
 * full and open on a hit (PUNCH_HOLD_S, then back to idle over about PUNCH_TAU_S), MISS_LEVEL with a low-pass on a
 * miss, recovering by itself to idle over a beat or at the next hit. Only the lane played moves: the other lanes and
 * the other track never change. The demo (title, song select) plays every lane at full. */
#define IDLE_LEVEL 0.6f        /* about -4.4 dB */
#define IDLE_CUTOFF 7000.0f    /* the idle lane's top, dulled a little; a hit opens it */
#define PUNCH_HOLD_S 0.09f
#define PUNCH_TAU_S 0.22f
#define MISS_LEVEL 0.22f       /* about -13 dB */
#define MISS_CUTOFF 700.0f
#define SLEW_DOWN_S 0.04f      /* a level falls by at most its full range in 40 ms */
#define SLEW_UP_S 0.012f       /* and rises in 12 ms (a hit's punch is near instant) */

static const int LANE_TRACK[LANES] = { TRACK_KICK, TRACK_CLAP, TRACK_BASS, TRACK_OTHER };
/* The mix's slots: a lane each, the synth's being the other track (the two-lane game plays it; in the three-lane game
 * a bass-lane note the chart made from the other stem, in a break, plays on it: hitting or missing it acts on the
 * other track, the sound the player is actually playing). */
#define SLOTS LANES
/* The bass sits well under the drums in these songs (8 to 11 dB under the mix), so its reward is wider: lower between
 * hits and a hit lifts it over the record for a moment (the kick and clap keep IDLE_LEVEL and full, as the mix gate
 * holds them). The other track is the backing, at full; while the chart plays notes made from it (a break), it sits
 * lower between hits as a lane does, so a hit punches the note up and a miss leaves it down. It eases between the two
 * over half a second, so a section change never pumps. */
#define BASS_IDLE 0.4f   /* about -8 dB */
#define OTHER_BREAK_IDLE 0.5f /* about -6 dB, while the chart plays the other track */
#define OTHER_EASE_S 0.5f
#define BASS_TOP 1.5f    /* about +3.5 dB over the record, for the punch */
#define OTHER_TOP 1.45f  /* about +3.2 dB */

static struct {
  cw_bus song_bus, lane_bus[SLOTS]; /* lane_bus[MUSIC_OTHER] is the other track's */
  int lowpass_slot[SLOTS];
  cw_sound snd[TRACKS];
  cw_voice v[TRACKS];
  const song *s;
  int64_t start_frame;  /* mixer frame the tracks start on */
  int64_t offset;       /* source frame they start from */
  int rate;             /* the other track's source rate */
  int32_t from_ms;
  double clock, raw;
  int64_t last_seen;    /* the audio position the clock last pulled toward */
  bool playing, paused, ended, started;
  float level[SLOTS], cutoff[SLOTS];
  float punch[SLOTS], hold[SLOTS], miss_t[SLOTS], health[SLOTS];
  float idle, idle_cutoff, recover_s;
  bool other_break;  /* the chart is playing notes made from the other track (music_set_other_break) */
  float other_idle;  /* the other track's level between hits, easing toward 1 or OTHER_BREAK_IDLE */
  bool miss_all; /* the `missall` request: a miss ducks every lane (the mix gate's control) */
  bool bass_old; /* the `bassmix old` request: the bass lane at the kick and clap's levels, as 0.32.2 had it (the bass gate's control) */
  bool no_reward; /* the `reward 0` request: the lanes sit at full between hits, as 0.31 did (the reward gate's control) */
  unsigned played; /* the lanes someone plays (music_set_played): the rest sit at full, the other track as the backing */
  float song_level, song_target, song_rate;
} M;

/* a slot's level between hits, the top of its punch and its dulled top, as the player hears it (the demo and the
 * reward gate's control: every slot at full, no punch over it) */
static bool played(int s) { return (M.played >> s) & 1u; }
static float idle_of(int s) {
  if (M.idle >= 1) return 1;
  if (s == MUSIC_OTHER) return played(s) ? OTHER_BREAK_IDLE : M.other_idle; /* the synth's lane, or the backing */
  if (!played(s)) return 1;
  return s == LANE_BASS && !M.bass_old ? BASS_IDLE : M.idle;
}
static float top_of(int s) { return M.idle >= 1 ? 1 : s == MUSIC_OTHER ? OTHER_TOP : !played(s) ? 1 : s == LANE_BASS && !M.bass_old ? BASS_TOP : 1; }
static float idle_cutoff_of(int s) { return s == MUSIC_OTHER || !played(s) ? 20000 : M.idle_cutoff; }

void music_init(void) {
  M.song_bus = cw_bus_create("song", cw_shell_music_bus());
  static const char *const N[SLOTS] = { "kick", "clap", "bass", "other" };
  for (int l = 0; l < SLOTS; l++) {
    M.lane_bus[l] = cw_bus_create(N[l], M.song_bus);
    M.lowpass_slot[l] = cw_bus_add_fx(M.lane_bus[l], &(cw_fx_desc){ .type = CW_FX_LOWPASS, .filter = { .cutoff_hz = 20000 } });
    cw_bus_set_fx_bypass(M.lane_bus[l], M.lowpass_slot[l], true);
    M.level[l] = 1;
    M.cutoff[l] = 20000;
  }
  M.idle = 1, M.idle_cutoff = 20000, M.recover_s = 0.47f;
  M.other_idle = 1;
  M.played = (1u << LANE_KICK) | (1u << LANE_CLAP) | (1u << LANE_BASS);
  M.song_level = M.song_target = 1;
}

void music_stop(float fade_s) {
  for (int t = 0; t < TRACKS; t++) {
    cw_voice_stop(M.v[t], fade_s);
    M.v[t] = (cw_voice){ 0 };
  }
  M.playing = false;
}

void music_play(const song *s, int32_t from_ms, int32_t lead_ms, float volume) {
  music_stop(0.05f);
  if (M.s != s) {
    for (int t = 0; t < TRACKS; t++) {
      cw_sound_free(M.snd[t]);
      M.snd[t] = cw_sound_load(s->track[t], &(cw_sound_desc){ .mode = CW_SOUND_STREAM });
    }
    M.s = s;
  }
  int mix = cw_audio_sample_rate() > 0 ? cw_audio_sample_rate() : 48000;
  M.rate = cw_sound_source_rate(M.snd[TRACK_OTHER]);
  if (M.rate <= 0) M.rate = mix;
  /* at least a few blocks ahead: the play commands reach the mixer at its next block */
  int64_t lead = (int64_t)lead_ms * mix / 1000;
  if (lead < 4 * CW_AUDIO_BLOCK) lead = 4 * CW_AUDIO_BLOCK;
  M.start_frame = cw_audio_mixed_frames() + lead;
  M.offset = (int64_t)from_ms * M.rate / 1000;
  M.from_ms = from_ms;
  M.recover_s = s->bpm > 0 ? 60.0f / s->bpm : 0.47f; /* a miss recovers over a beat */
  for (int l = 0; l < SLOTS; l++) {
    M.level[l] = idle_of(l);
    M.punch[l] = M.hold[l] = M.miss_t[l] = 0;
    M.health[l] = 1;
    cw_bus_set_volume(M.lane_bus[l], M.level[l]);
    M.cutoff[l] = idle_cutoff_of(l);
    cw_bus_set_fx_bypass(M.lane_bus[l], M.lowpass_slot[l], M.cutoff[l] > 19000);
    if (M.cutoff[l] <= 19000) cw_bus_set_fx(M.lane_bus[l], M.lowpass_slot[l], &(cw_fx_desc){ .type = CW_FX_LOWPASS, .filter = { .cutoff_hz = M.cutoff[l] } });
  }
  M.song_level = M.song_target = volume;
  cw_bus_set_volume(M.song_bus, volume);
  for (int t = 0; t < TRACKS; t++) {
    cw_bus bus = M.lane_bus[t]; /* LANE_TRACK: track t is lane t's slot */
    M.v[t] = cw_audio_play(M.snd[t], &(cw_play_desc){ .bus = bus, .start_frame = M.start_frame, .offset = M.offset, .priority = 10,
                                              .fade_in = M.offset > 0 ? 1.0f : 0 }); /* a start part way in swells up, never a cut in */
  }
  M.clock = M.raw = from_ms - (double)lead * 1000 / mix;
  M.last_seen = -1;
  M.playing = true;
  M.paused = M.ended = M.started = false;
}

void music_pause(bool paused) {
  if (!M.playing || paused == M.paused) return;
  M.paused = paused;
  for (int t = 0; t < TRACKS; t++) cw_voice_set_paused(M.v[t], paused);
}

void music_set_played(unsigned mask) { M.played = mask; }
void music_set_player(bool player) {
  M.idle = player && !M.no_reward ? IDLE_LEVEL : 1;
  M.idle_cutoff = player && !M.no_reward ? IDLE_CUTOFF : 20000;
}
void music_hit(int lane) {
  if (lane < 0 || lane >= SLOTS) return;
  M.punch[lane] = 1, M.hold[lane] = PUNCH_HOLD_S, M.miss_t[lane] = 0;
}
void music_miss(int lane) {
  for (int l = 0; l < SLOTS; l++)
    if (l == lane || (M.miss_all && lane >= 0 && played(l))) M.miss_t[l] = M.recover_s, M.punch[l] = 0;
}
float music_lane_level(int lane) { return lane >= 0 && lane < LANES ? M.health[lane] : 0; }
float music_lane_gain(int lane) { return lane >= 0 && lane < LANES ? M.level[lane] : 0; }
void music_set_fade(float level, float seconds) {
  M.song_target = level;
  M.song_rate = seconds > 0 ? fabsf(M.song_level - level) / seconds : 1000;
}

/* the audio's position: frames played of the other track once it has started, else the mixer's frames counted up
 * to the start frame (both exact as of the last mixed block) */
static int64_t audio_pos(double *ms) {
  int64_t fp = cw_voice_frames_played(M.v[TRACK_OTHER]);
  if (fp < 0) return -1; /* ended (a stale voice) */
  if (fp > M.offset) {
    M.started = true;
    *ms = (double)fp * 1000.0 / M.rate;
    return fp;
  }
  int mix = cw_audio_sample_rate() > 0 ? cw_audio_sample_rate() : 48000;
  int64_t mf = cw_audio_mixed_frames();
  *ms = M.from_ms + (double)(mf - M.start_frame) * 1000.0 / mix;
  if (*ms > M.from_ms) *ms = M.from_ms; /* started on this block: the voice's count shows it next block */
  return -2 - mf;
}

void music_set_other_break(bool on) { M.other_break = on; }

void music_tick(float dt) {
  {
    float want = M.other_break ? OTHER_BREAK_IDLE : 1, step = dt * (1 - OTHER_BREAK_IDLE) / OTHER_EASE_S;
    M.other_idle = want < M.other_idle ? fmaxf(want, M.other_idle - step) : fminf(want, M.other_idle + step);
  }
  /* each lane: its base (idle, or a miss recovering to idle), the hit's punch on top, the level slewed toward that;
   * the low-pass follows: closed on a miss, the idle's dulled top, open on a hit */
  for (int l = 0; l < SLOTS; l++) {
    float idle = idle_of(l), icut = idle_cutoff_of(l), base = idle, cbase = icut;
    if (M.miss_t[l] > 0) {
      float r = 1 - M.miss_t[l] / M.recover_s; /* 0 at the miss .. 1 recovered */
      r *= r;                                   /* it stays down a moment, then comes back */
      base = MISS_LEVEL + (idle - MISS_LEVEL) * r;
      cbase = MISS_CUTOFF * powf(icut / MISS_CUTOFF, r);
      M.miss_t[l] = fmaxf(0, M.miss_t[l] - dt);
    }
    M.health[l] = idle > 0 ? base / idle : 1;
    if (M.hold[l] > 0) M.hold[l] -= dt;
    else M.punch[l] *= expf(-dt / PUNCH_TAU_S);
    float t = base + (top_of(l) - base) * M.punch[l], v = M.level[l];
    float step = dt / (t < v ? SLEW_DOWN_S : SLEW_UP_S);
    v = t < v ? fmaxf(t, v - step) : fminf(t, v + step);
    if (v != M.level[l]) {
      M.level[l] = v;
      cw_bus_set_volume(M.lane_bus[l], v);
    }
    float cutoff = cbase * powf(20000.0f / cbase, M.punch[l]);
    if (fabsf(cutoff - M.cutoff[l]) > 1) {
      M.cutoff[l] = cutoff;
      bool open = cutoff > 19000;
      cw_bus_set_fx_bypass(M.lane_bus[l], M.lowpass_slot[l], open);
      if (!open) cw_bus_set_fx(M.lane_bus[l], M.lowpass_slot[l], &(cw_fx_desc){ .type = CW_FX_LOWPASS, .filter = { .cutoff_hz = cutoff } });
    }
  }
  if (M.song_level != M.song_target) {
    float s = M.song_rate * dt;
    M.song_level = M.song_target < M.song_level ? fmaxf(M.song_target, M.song_level - s) : fminf(M.song_target, M.song_level + s);
    cw_bus_set_volume(M.song_bus, M.song_level);
  }
  if (!M.playing || M.paused) return;
  M.clock += dt * 1000.0;
  double ms;
  int64_t pos = audio_pos(&ms);
  if (pos == -1) { /* the track has ended: the clock runs on */
    M.ended = true;
    return;
  }
  M.raw = ms;
  if (pos != M.last_seen) { /* a new block was mixed: pull toward it */
    M.last_seen = pos;
    double err = ms - M.clock;
    if (fabs(err) > RESYNC_MS) M.clock = ms;
    else M.clock += err * PULL;
  }
}

double music_ms(void) { return M.clock; }
double music_raw_ms(void) { return M.raw; }
bool music_playing(void) { return M.playing && !M.ended; }
bool music_ended(void) { return M.ended; }
const song *music_song(void) { return M.s; }

/* `solo <kick|clap|bass|other|off>`: hear one track alone (the sync check records the kick by itself) */
static void cmd_solo(int argc, const char **argv, cw_reply *r) {
  static const char *const N[TRACKS] = { "kick", "clap", "bass", "other" };
  int pick = -1;
  for (int t = 0; argc > 1 && t < TRACKS; t++)
    if (!strcmp(argv[1], N[t])) pick = t;
  if (argc < 2 || (pick < 0 && strcmp(argv[1], "off"))) { cw_reply_error(r, "usage: solo kick|clap|bass|other|off"); return; }
  for (int l = 0; l < SLOTS; l++) cw_bus_set_solo(M.lane_bus[l], pick == LANE_TRACK[l]);
  cw_reply_printf(r, "{\"solo\":\"%s\"}", argv[1]);
}
/* `missall 1|0`: a miss ducks every lane (the mix gate's control, which must fail) */
static void cmd_missall(int argc, const char **argv, cw_reply *r) {
  M.miss_all = argc > 1 && atoi(argv[1]) != 0;
  cw_reply_printf(r, "{\"missall\":%d}", M.miss_all);
}
/* `reward 1|0`: 0 keeps every lane at full between hits (from the next song started; the reward gate's control) */
static void cmd_bassmix(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) M.bass_old = !strcmp(argv[1], "old");
  cw_reply_printf(r, "{\"bassmix\":\"%s\"}", M.bass_old ? "old" : "new");
}
static void cmd_reward(int argc, const char **argv, cw_reply *r) {
  M.no_reward = argc > 1 && atoi(argv[1]) == 0;
  cw_reply_printf(r, "{\"reward\":%d}", !M.no_reward);
}
void music_register_debug(void) {
  cw_debug_command("bassmix", "bassmix old|new: the bass lane at the kick and clap's levels (old, the bass gate's control) or its own wider reward", cmd_bassmix);
  cw_debug_command("reward", "reward 1|0: 0 keeps every lane at full between hits, from the next song (the reward gate's control)", cmd_reward);
  cw_debug_command("solo", "solo kick|clap|bass|other|off: hear one track alone (src/music.c)", cmd_solo);
  cw_debug_command("missall", "missall 1|0: a miss ducks every lane (the mix gate's control)", cmd_missall);
}
