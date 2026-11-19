/* The song playing (music.h).
 *
 * The four tracks start on the same mixer frame (cw_play_desc.start_frame), so they stay sample-locked. The song
 * clock is the "other" track's cw_voice_frames_played over its rate: exact to the sample, but only as of the last
 * mixed block, so between blocks the clock runs on by the tick's time and is pulled toward the audio a little at
 * each new block (a jump larger than RESYNC_MS snaps it: a stall, a seek). Before the song starts the clock counts
 * the mixer's frames up to the start frame, so it runs smoothly from the lead-in into the song.
 *
 * Bus graph: music (the shell's, the Music volume) <- song (the song's own level, the failed song's fade) <- kick,
 * clap, bass (one bus a lane, each with a low-pass) and the other track straight into song. */
#include "music.h"

#include <math.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/shell.h"

#define RESYNC_MS 120.0
#define PULL 0.12     /* share of the clock's error to the audio taken at each new block */
#define DUCK_LEVEL 0.14f
#define DUCK_DOWN_S 0.04f /* a miss fades the track down over 40 ms */
#define DUCK_UP_S 0.03f   /* and a hit brings it back over 30 ms */

static const int LANE_TRACK[LANES] = { TRACK_KICK, TRACK_CLAP, TRACK_BASS };

static struct {
  cw_bus song_bus, lane_bus[LANES], other_bus;
  int lowpass_slot[LANES];
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
  float level[LANES], target[LANES], cutoff[LANES];
  float song_level, song_target, song_rate;
} M;

void music_init(void) {
  M.song_bus = cw_bus_create("song", cw_shell_music_bus());
  M.other_bus = cw_bus_create("other", M.song_bus);
  static const char *const N[LANES] = { "kick", "clap", "bass" };
  for (int l = 0; l < LANES; l++) {
    M.lane_bus[l] = cw_bus_create(N[l], M.song_bus);
    M.lowpass_slot[l] = cw_bus_add_fx(M.lane_bus[l], &(cw_fx_desc){ .type = CW_FX_LOWPASS, .filter = { .cutoff_hz = 20000 } });
    cw_bus_set_fx_bypass(M.lane_bus[l], M.lowpass_slot[l], true);
    M.level[l] = M.target[l] = 1;
    M.cutoff[l] = 20000;
  }
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
  for (int l = 0; l < LANES; l++) {
    M.level[l] = M.target[l] = 1;
    cw_bus_set_volume(M.lane_bus[l], 1);
    M.cutoff[l] = 20000;
    cw_bus_set_fx_bypass(M.lane_bus[l], M.lowpass_slot[l], true);
  }
  M.song_level = M.song_target = volume;
  cw_bus_set_volume(M.song_bus, volume);
  for (int t = 0; t < TRACKS; t++) {
    cw_bus bus = t == TRACK_OTHER ? M.other_bus : M.lane_bus[t == TRACK_KICK ? LANE_KICK : t == TRACK_CLAP ? LANE_CLAP : LANE_BASS];
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

void music_duck(int lane, bool ducked) {
  if (lane >= 0 && lane < LANES) M.target[lane] = ducked ? DUCK_LEVEL : 1;
}
float music_lane_level(int lane) { return lane >= 0 && lane < LANES ? M.level[lane] : 0; }
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

void music_tick(float dt) {
  /* the ducks: a level per lane toward its target, the low-pass closing as it falls */
  for (int l = 0; l < LANES; l++) {
    float t = M.target[l], v = M.level[l];
    float step = dt * (1 - DUCK_LEVEL) / (t < v ? DUCK_DOWN_S : DUCK_UP_S);
    v = t < v ? fmaxf(t, v - step) : fminf(t, v + step);
    if (v != M.level[l]) {
      M.level[l] = v;
      cw_bus_set_volume(M.lane_bus[l], v);
    }
    float k = (v - DUCK_LEVEL) / (1 - DUCK_LEVEL); /* 0 ducked .. 1 full */
    float cutoff = 350.0f * powf(20000.0f / 350.0f, k);
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
  for (int l = 0; l < LANES; l++) cw_bus_set_solo(M.lane_bus[l], pick == LANE_TRACK[l]);
  cw_bus_set_solo(M.other_bus, pick == TRACK_OTHER);
  cw_reply_printf(r, "{\"solo\":\"%s\"}", argv[1]);
}
void music_register_debug(void) { cw_debug_command("solo", "solo kick|clap|bass|other|off: hear one track alone (src/music.c)", cmd_solo); }
