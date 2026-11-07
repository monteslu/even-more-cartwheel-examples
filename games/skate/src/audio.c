/* skate audio: every sound synthesised at load (the repository carries no audio files). Wheels roll on a loop whose
 * level and pitch follow each board's speed (louder on wood, quiet in the air), a grind scrapes on its own loop, the
 * pop clicks, a landing slaps, a bail thumps, a token chimes and a banked combo rings up. The music is a sunny loop of
 * bass, chords and drums, made here from sines and noise. Presentation only. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/shell.h"
#include "view.h"

#define RATE 48000
static cw_sound s_roll, s_grind, s_pop, s_land, s_bail, s_token, s_bank, s_lost, s_push, s_wall, s_move, s_select, s_back, s_music, s_sketch;
static cw_voice v_roll[MAXP], v_grind[MAXP], v_music;
static uint32_t nz = 12345;
static float noise(void) {
  nz = nz * 1664525u + 1013904223u;
  return (float)(nz >> 8) / 8388608.0f - 1.0f;
}

static cw_sound make(float *pcm, int n) { return cw_sound_from_pcm(pcm, n, 1, RATE, NULL); }

/* a tone from hz0 to hz1, some noise, decaying */
static cw_sound tone(float seconds, float hz0, float hz1, float nmix, float gain, float decay) {
  int n = (int)(seconds * RATE);
  float *pcm = malloc(sizeof(float) * (size_t)n);
  double ph = 0;
  float lp = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (float)n;
    ph += 2 * 3.14159265358979 * (hz0 + (hz1 - hz0) * t) / RATE;
    lp += (noise() - lp) * 0.2f;
    pcm[i] = ((float)sin(ph) * (1 - nmix) + lp * nmix * 2.5f) * gain * expf(-t * decay) * (i < 120 ? (float)i / 120.0f : 1);
  }
  cw_sound s = make(pcm, n);
  free(pcm);
  return s;
}
/* a seamless loop of filtered noise with a low hum: the wheels on concrete */
static cw_sound roll_loop(void) {
  int n = RATE * 2;
  float *pcm = malloc(sizeof(float) * (size_t)n);
  float a = 0, b = 0;
  for (int i = 0; i < n; i++) {
    float x = noise();
    a += (x - a) * 0.08f;
    b += (a - b) * 0.3f;
    float hum = 0.25f * (float)sin(2 * 3.14159265 * 92.0 * i / RATE) + 0.1f * (float)sin(2 * 3.14159265 * 184.0 * i / RATE);
    float clack = ((i % (RATE / 6)) < 200) ? 0.6f * (1 - (float)(i % (RATE / 6)) / 200.0f) * noise() : 0; /* the joints of the slabs */
    pcm[i] = (b * 2.0f + hum * 0.4f + clack * 0.3f) * 0.5f;
  }
  /* fade the seam */
  for (int i = 0; i < 2000; i++) { float k = (float)i / 2000.0f; pcm[i] = pcm[i] * k + pcm[n - 2000 + i] * (1 - k); }
  cw_sound s = make(pcm, n - 2000);
  free(pcm);
  return s;
}
/* metal on metal: a ringing band of partials over gritty noise */
static cw_sound grind_loop(void) {
  int n = RATE;
  float *pcm = malloc(sizeof(float) * (size_t)n);
  float a = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / RATE, x = noise();
    a += (x - a) * 0.5f;
    float ring = 0;
    static const float P[5] = { 1230, 1870, 2650, 3410, 4720 };
    for (int k = 0; k < 5; k++) ring += sinf(2 * 3.14159265f * P[k] * t + (float)k) * (0.25f / (float)(k + 1));
    pcm[i] = (a * 0.55f + ring * (0.6f + 0.4f * sinf(t * 37))) * 0.35f;
  }
  for (int i = 0; i < 1500; i++) { float k = (float)i / 1500.0f; pcm[i] = pcm[i] * k + pcm[n - 1500 + i] * (1 - k); }
  cw_sound s = make(pcm, n - 1500);
  free(pcm);
  return s;
}
/* the music: 8 bars at 112 bpm, a major key; bass, chord stabs, kick, snare and hats */
static cw_sound music_loop(void) {
  const float bpm = 112;
  const int beats = 32, n = (int)((float)beats * 60.0f / bpm * RATE);
  float *pcm = calloc((size_t)n, sizeof(float));
  static const float ROOT[8] = { 98.0f, 98.0f, 130.8f, 130.8f, 110.0f, 110.0f, 146.8f, 146.8f }; /* G G C C A A D D (bars) */
  static const float THIRD[8] = { 1.26f, 1.26f, 1.26f, 1.26f, 1.19f, 1.19f, 1.26f, 1.26f };
  int beat_len = (int)(60.0f / bpm * RATE);
  for (int i = 0; i < n; i++) {
    int beat = i / beat_len, bar = beat / 4;
    float bt = (float)(i % beat_len) / (float)RATE, t = (float)i / RATE;
    float r = ROOT[bar % 8];
    /* bass: root on the beat, the octave on the off */
    float bf = (beat & 1) ? r * 2 : r;
    float bass = sinf(2 * 3.14159265f * bf * t) * expf(-bt * 5) * 0.45f;
    /* chord stabs on the off-beats */
    float half = (float)(i % (beat_len / 2)) / RATE;
    bool off = ((i / (beat_len / 2)) & 1) == 1;
    float chord = 0;
    if (off) for (int k = 0; k < 3; k++) { float f = r * 2 * (k == 0 ? 1 : k == 1 ? THIRD[bar % 8] : 1.5f); chord += sinf(2 * 3.14159265f * f * t) + 0.3f * sinf(4 * 3.14159265f * f * t); }
    chord *= off ? expf(-half * 9) * 0.12f : 0;
    float kick = (beat % 2 == 0) ? sinf(2 * 3.14159265f * (55 + 90 * expf(-bt * 30)) * bt) * expf(-bt * 9) * 0.8f : 0;
    float snare = (beat % 2 == 1) ? noise() * expf(-bt * 18) * 0.35f : 0;
    float hat = noise() * expf(-half * 60) * 0.08f;
    pcm[i] = (bass + chord + kick + snare + hat) * 0.55f;
  }
  cw_sound s = make(pcm, n);
  free(pcm);
  return s;
}

void audio_init(void) {
  s_roll = roll_loop();
  s_grind = grind_loop();
  s_pop = tone(0.09f, 1800, 600, 0.7f, 0.6f, 9);
  s_land = tone(0.22f, 140, 60, 0.8f, 0.8f, 7);
  s_bail = tone(0.5f, 90, 40, 0.85f, 0.9f, 5);
  s_token = tone(0.6f, 1320, 1980, 0.0f, 0.35f, 4);
  s_bank = tone(0.5f, 660, 1320, 0.05f, 0.35f, 3);
  s_lost = tone(0.5f, 330, 140, 0.2f, 0.35f, 3);
  s_push = tone(0.18f, 300, 200, 0.9f, 0.25f, 8);
  s_wall = tone(0.12f, 200, 120, 0.9f, 0.4f, 9);
  s_sketch = tone(0.15f, 500, 300, 0.9f, 0.3f, 8);
  s_move = tone(0.05f, 660, 660, 0, 0.25f, 2);
  s_select = tone(0.12f, 520, 1040, 0, 0.3f, 3);
  s_back = tone(0.12f, 520, 260, 0, 0.3f, 3);
  s_music = music_loop();
}

static void play(cw_sound s, float gain, float pitch) { cw_audio_play(s, &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = gain, .pitch = pitch }); }

void audio_menu(int which) { play(which == 0 ? s_move : which == 1 ? s_select : s_back, 1, 1); }

void audio_music(bool on) {
  if (on && !cw_voice_is_playing(v_music)) v_music = cw_audio_play(s_music, &(cw_play_desc){ .bus = cw_shell_music_bus(), .loop = true, .volume = 0.35f, .fade_in = 1.0f });
  if (!on && cw_voice_is_playing(v_music)) cw_voice_stop(v_music, 0.5f);
}

void audio_events(const sim *S, int players) {
  float g = players > 2 ? 0.6f : players > 1 ? 0.8f : 1.0f;
  for (int i = 0; i < players; i++) {
    uint32_t e = S->s[i].ev;
    if (!e) continue;
    if (e & EV_POP) play(s_pop, 0.5f * g, 1);
    if (e & EV_LAND) play(s_land, 0.6f * g, 1);
    if (e & EV_BAIL) play(s_bail, 0.8f * g, 1);
    if (e & EV_TOKEN) play(s_token, 0.7f * g, 1);
    if (e & EV_COMBO) play(s_bank, 0.5f * g, 1);
    if (e & EV_LOST) play(s_lost, 0.5f * g, 1);
    if (e & EV_PUSH) play(s_push, 0.35f * g, 1);
    if (e & EV_WALL) play(s_wall, 0.4f * g, 1);
    if (e & EV_SKETCH) play(s_sketch, 0.4f * g, 1);
  }
}

void audio_roll(const sim *S, int players, bool playing) {
  for (int i = 0; i < MAXP; i++) {
    const skater *s = &S->s[i];
    bool on = playing && i < players;
    float sp = vlen(s->v);
    float roll = on && s->st == SK_ROLL ? clampf(sp / 9.0f, 0, 1.2f) : 0;
    float grind = on && s->st == SK_GRIND ? 0.9f : 0;
    float g = players > 2 ? 0.55f : players > 1 ? 0.75f : 1.0f;
    if (roll > 0.02f && !cw_voice_is_playing(v_roll[i])) v_roll[i] = cw_audio_play(s_roll, &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .loop = true, .volume = 0.01f });
    if (cw_voice_is_playing(v_roll[i])) {
      cw_voice_set_volume(v_roll[i], roll * 0.55f * g);
      cw_voice_set_pitch(v_roll[i], 0.7f + 0.5f * clampf(sp / 12.0f, 0, 1.4f));
      if (roll <= 0.02f && !on) cw_voice_stop(v_roll[i], 0.2f);
    }
    if (grind > 0 && !cw_voice_is_playing(v_grind[i])) v_grind[i] = cw_audio_play(s_grind, &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .loop = true, .volume = 0.01f });
    if (cw_voice_is_playing(v_grind[i])) {
      cw_voice_set_volume(v_grind[i], grind * 0.5f * g);
      cw_voice_set_pitch(v_grind[i], 0.8f + 0.25f * clampf(fabsf(s->gv) / 8.0f, 0, 1.5f));
      if (grind <= 0) cw_voice_stop(v_grind[i], 0.08f);
    }
  }
}
