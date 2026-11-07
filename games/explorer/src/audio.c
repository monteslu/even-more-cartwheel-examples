/* explorer: sound and music, synthesised at start-up so the repository holds no audio files.
 *
 * Effects are short shaped tones and filtered noise. The music is one loop per mood (each region names one): a
 * chord progression in the mood's key and tempo, played as a soft pad, an arpeggio, a bass line and light
 * percussion, mixed down to mono; the wardens have their own driving loop. Render-side: libm is fine. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "cartwheel/shell.h"
#include "world.h"

static cw_sound S[SFX_COUNT];
static cw_sound music[MOOD_COUNT];
static cw_voice music_v;
static int music_now = -1;

static float rnd_state = 0.5f;
static uint32_t nseed = 7;
static float noise1(void) {
  nseed = nseed * 1664525u + 1013904223u;
  return (float)(nseed >> 8) / 8388608.0f - 1.0f;
}

/* a tone sweeping hz0 -> hz1 with noise mixed in, an attack, an exponential decay */
static cw_sound synth(float seconds, float hz0, float hz1, float noise, float gain, float decay, float shape) {
  enum { RATE = 44100 };
  int n = (int)(seconds * RATE);
  float *pcm = malloc(sizeof(float) * (size_t)n);
  double ph = 0;
  float lp = 0;
  nseed = 7;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (float)n;
    ph += 2 * 3.14159265358979 * (hz0 + (hz1 - hz0) * t) / RATE;
    float tone = (float)sin(ph);
    if (shape > 0) tone = tanhf(tone * (1 + shape * 4)); /* a little edge */
    lp += (noise1() - lp) * 0.15f;
    float env = expf(-t * decay) * (i < 120 ? (float)i / 120.0f : 1);
    pcm[i] = (tone * (1 - noise) + lp * noise * 2.5f) * gain * env;
  }
  cw_sound s = cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
  free(pcm);
  return s;
}

/* ── music ── */
typedef struct mood { float bpm; int root; int minor; int prog[4]; float bright, drive; } mood;
static const mood MOODS[MOOD_COUNT] = {
  [MOOD_GATE] = { 92, 57, 0, { 0, 5, 3, 4 }, 0.5f, 0.3f },     /* A, gentle: the start */
  [MOOD_ROOTS] = { 100, 55, 0, { 0, 3, 4, 3 }, 0.55f, 0.45f },   /* G, warm */
  [MOOD_CAVES] = { 84, 62, 1, { 0, 5, 3, 6 }, 0.7f, 0.25f },     /* D minor, glassy */
  [MOOD_DEEP] = { 96, 52, 1, { 0, 3, 4, 0 }, 0.4f, 0.5f },       /* E minor: brass and tide */
  [MOOD_HIGH] = { 108, 60, 0, { 0, 4, 5, 3 }, 0.8f, 0.4f },      /* C, airy: sky, stars, frost */
  [MOOD_BLOOM] = { 104, 65, 0, { 0, 5, 1, 4 }, 0.65f, 0.4f },    /* F, bright */
  [MOOD_HEART] = { 76, 50, 1, { 0, 6, 5, 4 }, 0.45f, 0.35f },    /* D minor, solemn */
  [MOOD_BOSS] = { 138, 52, 1, { 0, 0, 6, 5 }, 0.6f, 1.0f },      /* E minor, driving */
};
int region_mood(int r) {
  switch (r) {
  case R_HOLLOW: return MOOD_GATE;
  case R_ROOTWAY: case R_FEN: return MOOD_ROOTS;
  case R_GLASS: case R_ARCHIVE: return MOOD_CAVES;
  case R_CLOCK: case R_TIDAL: case R_FORGE: return MOOD_DEEP;
  case R_SKY: case R_STAR: case R_FROST: return MOOD_HIGH;
  case R_BLOOM: return MOOD_BLOOM;
  default: return MOOD_HEART;
  }
}

static float midi_hz(float m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

static cw_sound make_music(const mood *m) {
  enum { RATE = 22050 };
  static const int MAJOR[7] = { 0, 2, 4, 5, 7, 9, 11 }, MINOR[7] = { 0, 2, 3, 5, 7, 8, 10 };
  const int *sc = m->minor ? MINOR : MAJOR;
  float beat = 60.0f / m->bpm;
  int bars = 8, beats = bars * 4;
  int n = (int)((float)beats * beat * RATE);
  float *pcm = calloc((size_t)n, sizeof(float));
  nseed = (uint32_t)(m->root * 31 + m->prog[1]);
  for (int b = 0; b < beats; b++) {
    int chord = m->prog[(b / 8) % 4], bar = b / 4;
    int deg[3] = { chord, chord + 2, chord + 4 };
    float notes[3];
    for (int k = 0; k < 3; k++) notes[k] = (float)(m->root + sc[deg[k] % 7] + 12 * (deg[k] / 7));
    int s0 = (int)((float)b * beat * RATE), len = (int)(beat * RATE);
    /* pad: the chord held across the bar, from its first beat */
    if (b % 4 == 0)
      for (int i = 0; i < len * 4 && s0 + i < n; i++) {
        float t = (float)i / RATE, env = fminf(1, t * 3) * fminf(1, (4 * beat - t) * 2);
        float v = 0;
        for (int k = 0; k < 3; k++) {
          float hz = midi_hz(notes[k]);
          v += sinf(6.2832f * hz * t) * 0.5f + sinf(6.2832f * hz * 1.003f * t) * 0.35f + sinf(6.2832f * hz * 2 * t) * 0.12f * m->bright;
        }
        pcm[s0 + i] += v * env * 0.045f;
      }
    /* arpeggio: eighth notes up the chord, an octave above */
    for (int e = 0; e < 2; e++) {
      int st = s0 + e * len / 2, idx = (b * 2 + e) % 4;
      float hz = midi_hz(notes[idx % 3] + 12 + (idx == 3 ? 12 : 0));
      for (int i = 0; i < len / 2 && st + i < n; i++) {
        float t = (float)i / RATE, env = expf(-t * (6 - m->bright * 2)) * fminf(1, t * 200);
        float v = sinf(6.2832f * hz * t) + 0.3f * sinf(6.2832f * hz * 3 * t) * m->bright;
        pcm[st + i] += v * env * 0.06f;
      }
    }
    /* bass: the root on beats one and three, a fifth between when the music drives */
    if (b % 2 == 0 || m->drive > 0.6f) {
      float hz = midi_hz(notes[0] - 24 + (b % 4 == 3 && m->drive > 0.6f ? 7 : 0));
      for (int i = 0; i < len && s0 + i < n; i++) {
        float t = (float)i / RATE, env = expf(-t * 3) * fminf(1, t * 100);
        pcm[s0 + i] += (sinf(6.2832f * hz * t) + 0.25f * sinf(6.2832f * hz * 2 * t)) * env * 0.16f;
      }
    }
    /* percussion: a soft kick on one, a shaker on the offbeats, more as the drive rises */
    if (b % 4 == 0 || (m->drive > 0.6f && b % 2 == 0))
      for (int i = 0; i < len / 2 && s0 + i < n; i++) {
        float t = (float)i / RATE, f = 110 * expf(-t * 18) + 45;
        pcm[s0 + i] += sinf(6.2832f * f * t) * expf(-t * 9) * 0.22f * (0.5f + m->drive);
      }
    for (int e = 0; e < 2; e++) {
      if (e == 0 && m->drive < 0.4f) continue;
      int st = s0 + len / 2 * e + (e ? 0 : len / 4);
      float lp = 0;
      for (int i = 0; i < len / 6 && st + i < n; i++) {
        float t = (float)i / RATE, nz = noise1();
        lp += (nz - lp) * 0.5f;
        pcm[st + i] += (nz - lp) * expf(-t * 40) * 0.05f * (0.4f + m->drive);
      }
    }
    (void)bar;
  }
  /* soften the loop seam */
  for (int i = 0; i < 400 && i < n; i++) pcm[i] *= (float)i / 400.0f, pcm[n - 1 - i] *= (float)i / 400.0f;
  cw_sound s = cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
  free(pcm);
  return s;
}

void audio_init(void) {
  S[SFX_JUMP] = synth(0.12f, 300, 620, 0.1f, 0.22f, 5, 0);
  S[SFX_DJUMP] = synth(0.18f, 500, 1100, 0.15f, 0.22f, 4, 0);
  S[SFX_LAND] = synth(0.1f, 120, 70, 0.6f, 0.3f, 8, 0);
  S[SFX_STRIKE] = synth(0.14f, 900, 250, 0.75f, 0.3f, 6, 0);
  S[SFX_HIT] = synth(0.12f, 220, 110, 0.5f, 0.4f, 6, 0.5f);
  S[SFX_KILL] = synth(0.3f, 400, 80, 0.6f, 0.4f, 4, 0.3f);
  S[SFX_BOLT] = synth(0.22f, 1400, 500, 0.2f, 0.25f, 5, 0.2f);
  S[SFX_DASH] = synth(0.2f, 200, 900, 0.8f, 0.3f, 4, 0);
  S[SFX_HURT] = synth(0.3f, 300, 120, 0.4f, 0.45f, 4, 0.6f);
  S[SFX_DIE] = synth(1.2f, 330, 80, 0.3f, 0.5f, 2, 0.2f);
  S[SFX_PICKUP] = synth(1.1f, 520, 1560, 0.05f, 0.35f, 1.6f, 0);
  S[SFX_SAVE] = synth(1.4f, 392, 784, 0.02f, 0.3f, 1.4f, 0);
  S[SFX_BREAK] = synth(0.6f, 90, 40, 0.85f, 0.6f, 3, 0);
  S[SFX_POUND] = synth(0.35f, 70, 35, 0.7f, 0.65f, 4, 0.3f);
  S[SFX_SPLASH] = synth(0.4f, 600, 200, 0.9f, 0.3f, 4, 0);
  S[SFX_STROKE] = synth(0.18f, 300, 200, 0.85f, 0.15f, 6, 0);
  S[SFX_GRAPPLE] = synth(0.16f, 1800, 900, 0.3f, 0.2f, 5, 0);
  S[SFX_CLING] = synth(0.08f, 200, 180, 0.8f, 0.2f, 8, 0);
  S[SFX_TELL] = synth(0.5f, 180, 260, 0.3f, 0.35f, 2.5f, 0.6f);
  S[SFX_BOSS_ACT] = synth(0.5f, 120, 60, 0.6f, 0.5f, 3, 0.5f);
  S[SFX_BOSS_DOWN] = synth(2.5f, 220, 660, 0.2f, 0.55f, 1.0f, 0.1f);
  S[SFX_ENEMY_TELL] = synth(0.25f, 500, 700, 0.2f, 0.18f, 4, 0.4f);
  S[SFX_SHOT] = synth(0.2f, 700, 400, 0.3f, 0.18f, 5, 0);
  S[SFX_SECRET] = synth(1.2f, 260, 520, 0.4f, 0.4f, 1.8f, 0);
  S[SFX_DOOR] = synth(0.8f, 80, 60, 0.7f, 0.5f, 2, 0);
  S[SFX_MOVE] = synth(0.05f, 660, 660, 0, 0.2f, 2, 0);
  S[SFX_SELECT] = synth(0.14f, 520, 1040, 0, 0.25f, 3, 0);
  S[SFX_BACK] = synth(0.14f, 520, 260, 0, 0.25f, 3, 0);
  S[SFX_MAP] = synth(0.25f, 300, 450, 0.3f, 0.2f, 3, 0);
  for (int m = 0; m < MOOD_COUNT; m++) music[m] = make_music(&MOODS[m]);
  (void)rnd_state;
}

void sfx(int id, float gain) { cw_audio_play(S[id], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = gain }); }
void sfx_pitch(int id, float gain, float pitch) { cw_audio_play(S[id], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = gain, .pitch = pitch }); }

void music_play(int mood) {
  if (mood == music_now && cw_voice_is_playing(music_v)) return;
  if (cw_voice_is_playing(music_v)) cw_voice_stop(music_v, 1.2f);
  music_now = mood;
  if (mood < 0) return;
  music_v = cw_audio_play(music[mood], &(cw_play_desc){ .bus = cw_shell_music_bus(), .loop = true, .volume = 0.55f, .fade_in = 1.5f });
}
