/* jewels' sound (sound.h), all synthesised at load.
 *   effects  soft and tactile: a wooden tick for the cursor, a glass ting to pick a stone up, a velvet slide for a
 *            swap, a muted knock when a swap springs back (never a sad noise), a crystal chime for each clear that
 *            climbs a pentatonic scale with each step of a cascade (so a cascade is heard to go on), a soft landing,
 *            a harp's glissando for a reshuffle and the end of a long cascade.
 *   music    calm: slow chords on a warm pad, a celesta wandering over them and a soft bass, 66 beats a minute,
 *            scheduled ahead on the mixer's clock. */
#include "sound.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/shell.h"

#define RATE 44100
#define PI2 6.283185307f
enum { S_TICK, S_PICK, S_PUT, S_SWAP, S_KNOCK, S_LAND, S_HINT, S_SHUFFLE, S_FINALE, S_MENU, S_SELECT, S_BACK, S_COUNT };
#define N_CHIMES 10
static cw_sound snd[S_COUNT], chime[N_CHIMES], shatter, pad_note, celesta, bass;
static float *pcm;
static uint32_t rng = 7;
static float nz(void) { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 8388608.0f - 1.0f; }

static cw_sound make(int n) { return cw_sound_from_pcm(pcm, n, 1, RATE, NULL); }

/* a bell: inharmonic partials, each its own decay (glass and crystal) */
static int bell(float hz, float secs, float bright, float gain, bool add) {
  static const float ratio[] = { 1.0f, 2.756f, 5.404f, 8.933f, 2.0f }, amp[] = { 1.0f, 0.5f, 0.25f, 0.12f, 0.3f }, dec[] = { 2.2f, 4.0f, 7.0f, 11.0f, 3.0f };
  int n = (int)(secs * RATE);
  if (!add) memset(pcm, 0, sizeof(float) * (size_t)n);
  for (int p = 0; p < 5; p++) {
    double ph = 0;
    float a = amp[p] * (p ? bright : 1);
    for (int i = 0; i < n; i++) {
      float t = (float)i / RATE;
      ph += PI2 * hz * ratio[p] / RATE;
      float att = i < 90 ? (float)i / 90 : 1;
      pcm[i] += (float)sin(ph) * a * expf(-t * dec[p]) * att * gain;
    }
  }
  return n;
}

void jw_sound_init(void) {
  pcm = calloc(RATE * 8, sizeof(float));
  int n;
  /* the cursor: a small wooden tick */
  n = (int)(0.05f * RATE);
  {
    float lp = 0;
    for (int i = 0; i < n; i++) {
      float t = (float)i / RATE;
      lp += (nz() - lp) * 0.35f;
      pcm[i] = (lp * 0.6f + 0.6f * sinf(PI2 * 820 * t)) * expf(-t * 140) * 0.35f;
    }
  }
  snd[S_TICK] = make(n);
  n = bell(1567.98f, 0.5f, 0.7f, 0.16f, false), snd[S_PICK] = make(n);
  n = bell(1174.66f, 0.35f, 0.4f, 0.12f, false), snd[S_PUT] = make(n);
  /* the swap: a soft slide across velvet */
  n = (int)(0.22f * RATE);
  {
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
      float t = (float)i / (float)n;
      lp += (nz() - lp) * (0.05f + 0.15f * t), lp2 += (lp - lp2) * 0.3f;
      pcm[i] = lp2 * sinf(3.14159f * t) * 0.9f;
    }
  }
  snd[S_SWAP] = make(n);
  /* the spring back: a muted knock */
  n = (int)(0.12f * RATE);
  for (int i = 0; i < n; i++) {
    float t = (float)i / RATE;
    pcm[i] = sinf(PI2 * 190 * t * (1 + 0.5f * expf(-t * 40))) * expf(-t * 38) * 0.4f;
  }
  snd[S_KNOCK] = make(n);
  /* the landing: a velvet thud */
  n = (int)(0.16f * RATE);
  {
    float lp = 0;
    for (int i = 0; i < n; i++) {
      float t = (float)i / RATE;
      lp += (nz() - lp) * 0.06f;
      pcm[i] = (sinf(PI2 * 95 * t) * 0.7f + lp * 1.5f) * expf(-t * 30) * 0.3f;
    }
  }
  snd[S_LAND] = make(n);
  /* the clears: a chime a step of a cascade, up a pentatonic scale from C5 */
  static const float penta[N_CHIMES] = { 523.25f, 587.33f, 659.25f, 783.99f, 880.0f, 1046.5f, 1174.66f, 1318.51f, 1567.98f, 1760.0f };
  for (int k = 0; k < N_CHIMES; k++) {
    n = bell(penta[k], 1.4f, 0.8f, 0.14f, false);
    bell(penta[k] * 1.5f, 1.4f, 0.4f, 0.05f, true); /* a fifth over it, quietly */
    chime[k] = make(n);
  }
  /* the stone breaking: a glassy spray of tiny high partials */
  n = (int)(0.5f * RATE);
  memset(pcm, 0, sizeof(float) * (size_t)n);
  for (int k = 0; k < 24; k++) {
    float hz = 2500 + 5500 * (nz() * 0.5f + 0.5f), at = (nz() * 0.5f + 0.5f) * 0.12f;
    int s = (int)(at * RATE);
    for (int i = s; i < n; i++) {
      float t = (float)(i - s) / RATE;
      pcm[i] += sinf(PI2 * hz * t) * expf(-t * 30) * 0.035f;
    }
  }
  shatter = make(n);
  /* the hint: two soft tones */
  n = bell(1318.51f, 0.9f, 0.5f, 0.10f, false);
  {
    int off = (int)(0.12f * RATE);
    double ph = 0;
    for (int i = off; i < n; i++) {
      float t = (float)(i - off) / RATE;
      ph += PI2 * 1567.98 / RATE;
      pcm[i] += (float)sin(ph) * expf(-t * 3) * 0.09f;
    }
  }
  snd[S_HINT] = make(n);
  /* a harp's glissando (reshuffle: down; the finale: up, longer) */
  for (int which = 0; which < 2; which++) {
    int notes = which ? 14 : 9;
    float each = which ? 0.07f : 0.05f;
    n = (int)((each * (float)notes + 1.6f) * RATE);
    memset(pcm, 0, sizeof(float) * (size_t)n);
    for (int k = 0; k < notes; k++) {
      int d = which ? k : notes - 1 - k;
      static const int sc[5] = { 0, 2, 4, 7, 9 };
      float hz = 261.63f * powf(2.0f, (float)(sc[d % 5] + 12 * (d / 5)) / 12.0f) * (which ? 2 : 2);
      int s = (int)((float)k * each * RATE);
      double ph = 0;
      for (int i = s; i < n; i++) {
        float t = (float)(i - s) / RATE;
        ph += PI2 * hz / RATE;
        float v = (float)(sin(ph) + 0.3 * sin(2 * ph) + 0.1 * sin(3 * ph));
        pcm[i] += v * expf(-t * 3.2f) * 0.08f * (i - s < 40 ? (float)(i - s) / 40 : 1);
      }
    }
    snd[which ? S_FINALE : S_SHUFFLE] = make(n);
  }
  n = bell(880, 0.12f, 0.3f, 0.12f, false), snd[S_MENU] = make(n);
  n = bell(1318.51f, 0.4f, 0.5f, 0.14f, false), snd[S_SELECT] = make(n);
  n = bell(659.25f, 0.3f, 0.4f, 0.12f, false), snd[S_BACK] = make(n);
  /* the music's instruments, each at one pitch: a pad (C4), a celesta (C5), a bass (C2) */
  n = (int)(7.4f * RATE);
  {
    double ph[4] = { 0 };
    static const float det[4] = { 1.0f, 1.004f, 0.997f, 2.001f };
    float lp = 0;
    for (int i = 0; i < n; i++) {
      float t = (float)i / (float)n, s = 0;
      for (int k = 0; k < 4; k++) {
        ph[k] += 261.63 * det[k] / RATE;
        s += (float)(2 * (ph[k] - floor(ph[k])) - 1) * (k == 3 ? 0.3f : 1);
      }
      float env = fminf(1, t * 3.5f) * fminf(1, (1 - t) * 2.5f);
      lp += (s - lp) * 0.035f;
      pcm[i] = lp * env * 0.09f;
    }
  }
  pad_note = make(n);
  n = bell(523.25f, 2.0f, 0.35f, 0.12f, false), celesta = make(n);
  n = (int)(2.0f * RATE);
  for (int i = 0; i < n; i++) {
    float t = (float)i / RATE;
    pcm[i] = (sinf(PI2 * 65.41f * t) + 0.25f * sinf(PI2 * 130.81f * t)) * expf(-t * 1.6f) * fminf(1, t * 60) * 0.3f;
  }
  bass = make(n);
  free(pcm);
  pcm = NULL;
}

static void play(cw_sound s, float vol, float pitch) {
  cw_audio_play(s, &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = vol, .pitch = pitch > 0 ? pitch : 1 });
}

void jw_sound_menu(int what) { play(snd[what == 0 ? S_MENU : what == 1 ? S_SELECT : S_BACK], 0.7f, 1); }

void jw_sound_events(const jw_game *g) {
  int bursts = 0;
  for (int i = 0; i < g->nev; i++) {
    const jw_event *e = &g->ev[i];
    switch (e->kind) {
    case JW_EV_CURSOR: play(snd[S_TICK], 0.5f, 1); break;
    case JW_EV_PICK: play(snd[S_PICK], 0.6f, 1); break;
    case JW_EV_PUTDOWN: play(snd[S_PUT], 0.5f, 1); break;
    case JW_EV_SWAP: play(snd[S_SWAP], 0.6f, 1); break;
    case JW_EV_BUMP: play(snd[S_KNOCK], 0.6f, 1); break;
    case JW_EV_CLEAR: {
      int step = e->b - 1;
      if (step >= N_CHIMES) step = N_CHIMES - 1;
      play(chime[step], fminf(1.0f, 0.55f + 0.08f * (float)e->a), 1);
      break;
    }
    case JW_EV_BURST:
      if (bursts++ < 3) play(shatter, 0.5f, 0.9f + 0.1f * (float)bursts);
      break;
    case JW_EV_LAND: if (e->a) play(snd[S_LAND], fminf(0.7f, 0.25f + 0.03f * (float)e->a), 1); break;
    case JW_EV_RESHUFFLE: play(snd[S_SHUFFLE], 0.8f, 1); break;
    case JW_EV_HINT: play(snd[S_HINT], 0.7f, 1); break;
    case JW_EV_SETTLE: if (e->a >= 4) play(snd[S_FINALE], 0.85f, 1); break;
    default: break;
    }
  }
}

/* ── the music ───────────────────────────────────────────────────────── */

static struct { int64_t next; int step; bool on; uint32_t r; } M = { -1, 0, false, 99 };

/* eight bars of chords (semitones over C), the bass's root and a celesta line in the C major pentatonic */
static const int CHORD[8][4] = { { 0, 4, 7, 11 }, { -3, 0, 4, 7 }, { -7, -3, 0, 4 }, { -5, -1, 2, 5 },
                                 { 0, 4, 7, 11 }, { -8, -4, -1, 3 }, { -7, -3, 0, 4 }, { -5, 2, 5, 9 } };
static const int ROOT[8] = { 0, -3, -7, -5, 0, -8, -7, -5 };

void jw_sound_music(bool on) {
  M.on = on;
  if (on && M.next < 0) M.next = cw_audio_mixed_frames() + RATE / 5, M.step = 0;
  if (!on) M.next = -1;
}

void jw_sound_update(void) {
  if (!M.on || M.next < 0 || !cw_audio_sample_rate()) return;
  int64_t now = cw_audio_mixed_frames();
  int sr = cw_audio_sample_rate();
  int64_t eighth = (int64_t)((float)sr * 60.0f / 66.0f / 2.0f);
  if (M.next < now) M.next = now + sr / 20;
  cw_bus bus = cw_shell_music_bus();
  while (M.next < now + sr / 4) {
    int s = M.step % 128, bar = s / 16;
    int64_t at = M.next;
    if (s % 16 == 0) {
      for (int k = 0; k < 4; k++)
        cw_audio_play(pad_note, &(cw_play_desc){ .bus = bus, .volume = 0.55f, .pitch = powf(2.0f, (float)CHORD[bar][k] / 12.0f), .start_frame = at });
      cw_audio_play(bass, &(cw_play_desc){ .bus = bus, .volume = 0.5f, .pitch = powf(2.0f, (float)ROOT[bar] / 12.0f), .start_frame = at });
    }
    if (s % 16 == 8) cw_audio_play(bass, &(cw_play_desc){ .bus = bus, .volume = 0.3f, .pitch = powf(2.0f, (float)(ROOT[bar] + 7) / 12.0f), .start_frame = at });
    /* the celesta: a few notes a bar, chosen from the chord, sparse and unhurried */
    M.r = M.r * 1664525u + 1013904223u;
    if ((s % 4 == 0 && (M.r >> 24) < 110) || (s % 16 == 0)) {
      int k = (int)((M.r >> 8) % 4);
      int semi = CHORD[bar][k] + 12 * (int)((M.r >> 16) % 2);
      cw_audio_play(celesta, &(cw_play_desc){ .bus = bus, .volume = 0.22f + 0.1f * (float)((M.r >> 4) & 1), .pitch = powf(2.0f, (float)semi / 12.0f),
                                              .start_frame = at });
    }
    M.step++;
    M.next += eighth;
  }
}
