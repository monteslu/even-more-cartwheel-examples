/* pinball's sound: every effect and the music made in code at load (no audio files).
 *   effects   solenoids (flippers, pops, slings, kickers) are a noise crack over a low thump; targets and drops clacks;
 *             the spinner ticks as its plate turns; awards are little synth fanfares; the ball rolls (a looping rumble
 *             whose level and pitch follow its speed).
 *   music     a sequencer of synthesised instruments (bass, a plucked lead, a pad, kick, snare, hats), one style a
 *             table, scheduled a little ahead on the mixer's clock so it keeps time; it grows a layer in a mode and
 *             another in multiball, and drops to the pad and bass between balls. */
#include "sound.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/shell.h"

#define RATE 44100
enum { S_FLIP, S_FLIP_DOWN, S_POP, S_SLING, S_TARGET, S_DROP, S_RESET, S_TICK, S_ROLLOVER, S_RAMP_IN, S_RAMP_DONE, S_HOLE, S_KICK, S_LAUNCH,
       S_DRAIN, S_WALL, S_CLICK, S_JACKPOT, S_MULTIBALL, S_MODE, S_CHIME, S_FANFARE, S_FAIL, S_TILT, S_WARN, S_LOCK, S_ARP, S_BLIP, S_OVER,
       S_THUMP, S_PULL, S_ROLL, S_MENU, S_SELECT, S_BACK, S_COUNT };
enum { I_BASS, I_PLUCK, I_PAD, I_KICK, I_SNARE, I_HAT, I_COUNT };

static cw_sound snd[S_COUNT], ins[I_COUNT];
static cw_voice roll[PB_MAXB];
static float *pcm;
static uint32_t rng = 1;
static float nz(void) { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 8388608.0f - 1.0f; }

/* an envelope'd mix of a swept tone (with a little square in it) and filtered noise */
static cw_sound tone(float secs, float hz0, float hz1, float noise, float decay, float square, float gain) {
  int n = (int)(secs * RATE);
  double ph = 0;
  float lp = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (float)n;
    ph += 2 * 3.14159265358979 * (hz0 * powf(hz1 / hz0, t)) / RATE;
    float s = (float)sin(ph), q = s > 0 ? 1.0f : -1.0f;
    lp += (nz() - lp) * 0.25f;
    float att = i < 60 ? (float)i / 60.0f : 1;
    pcm[i] = ((s * (1 - square) + q * square * 0.5f) * (1 - noise) + lp * noise * 2) * gain * expf(-t * decay) * att;
  }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}
/* a solenoid: a hard crack of noise and a body thump */
static cw_sound coil(float secs, float body_hz, float crack, float gain) {
  int n = (int)(secs * RATE);
  double ph = 0;
  float hp = 0, last = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / RATE;
    ph += 2 * 3.14159265358979 * body_hz * (1 + 2 * expf(-t * 60)) / RATE;
    float x = nz();
    hp = 0.9f * (hp + x - last), last = x;
    pcm[i] = ((float)sin(ph) * expf(-t * 28) * 0.9f + hp * crack * expf(-t * 90)) * gain;
  }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}
/* notes in a row (a fanfare, an arpeggio) */
static cw_sound notes(const float *hz, int count, float each, float decay, float square, float gain) {
  int per = (int)(each * RATE), n = per * count + RATE / 3;
  memset(pcm, 0, sizeof(float) * (size_t)n);
  for (int k = 0; k < count; k++) {
    double ph = 0;
    for (int i = 0; i < n - per * k; i++) {
      float t = (float)i / RATE;
      ph += 2 * 3.14159265358979 * hz[k] / RATE;
      float s = (float)sin(ph), q = s > 0 ? 1.0f : -1.0f, v = (s * (1 - square) + q * square * 0.4f) * expf(-t * decay) * gain;
      if (i < 40) v *= (float)i / 40.0f;
      pcm[per * k + i] += v;
    }
  }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}

void sound_init(void) {
  pcm = malloc(sizeof(float) * RATE * 6);
  snd[S_FLIP] = coil(0.16f, 70, 0.9f, 0.7f);
  snd[S_FLIP_DOWN] = coil(0.1f, 55, 0.35f, 0.35f);
  snd[S_POP] = coil(0.3f, 95, 1.2f, 0.8f);
  snd[S_SLING] = coil(0.2f, 120, 1.0f, 0.65f);
  snd[S_TARGET] = tone(0.08f, 1800, 900, 0.6f, 30, 0, 0.4f);
  snd[S_DROP] = coil(0.18f, 160, 0.7f, 0.5f);
  snd[S_RESET] = coil(0.25f, 80, 1.1f, 0.6f);
  snd[S_TICK] = tone(0.025f, 2400, 1800, 0.5f, 40, 0, 0.25f);
  snd[S_ROLLOVER] = tone(0.12f, 1400, 1800, 0.05f, 18, 0.3f, 0.25f);
  snd[S_RAMP_IN] = tone(0.5f, 140, 220, 0.6f, 4, 0, 0.35f);
  { static const float a[] = { 523.25f, 659.25f, 783.99f, 1046.5f }; snd[S_RAMP_DONE] = notes(a, 4, 0.06f, 7, 0.3f, 0.3f); }
  snd[S_HOLE] = coil(0.3f, 60, 0.5f, 0.7f);
  snd[S_KICK] = coil(0.25f, 90, 1.3f, 0.8f);
  snd[S_LAUNCH] = tone(0.35f, 300, 90, 0.7f, 6, 0, 0.6f);
  { static const float a[] = { 392, 311.13f, 261.63f, 196 }; snd[S_DRAIN] = notes(a, 4, 0.14f, 4, 0.5f, 0.3f); }
  snd[S_WALL] = coil(0.07f, 220, 0.6f, 0.4f);
  snd[S_CLICK] = tone(0.05f, 2600, 2000, 0.3f, 50, 0, 0.35f);
  { static const float a[] = { 523.25f, 659.25f, 783.99f, 1046.5f, 783.99f, 1046.5f, 1318.5f }; snd[S_JACKPOT] = notes(a, 7, 0.09f, 3, 0.45f, 0.35f); }
  { static const float a[] = { 440, 554.37f, 659.25f, 880, 659.25f, 880, 1108.7f, 1318.5f }; snd[S_MULTIBALL] = notes(a, 8, 0.1f, 3, 0.5f, 0.35f); }
  { static const float a[] = { 392, 523.25f, 659.25f, 783.99f }; snd[S_MODE] = notes(a, 4, 0.11f, 3, 0.35f, 0.35f); }
  snd[S_CHIME] = tone(0.6f, 1046.5f, 1046.5f, 0, 5, 0.2f, 0.3f);
  { static const float a[] = { 523.25f, 523.25f, 523.25f, 698.46f, 880, 1046.5f }; snd[S_FANFARE] = notes(a, 6, 0.12f, 3, 0.5f, 0.35f); }
  { static const float a[] = { 392, 369.99f, 349.23f, 329.63f }; snd[S_FAIL] = notes(a, 4, 0.16f, 3, 0.6f, 0.3f); }
  snd[S_TILT] = tone(1.2f, 70, 60, 0.2f, 1.5f, 0.9f, 0.5f);
  snd[S_WARN] = tone(0.35f, 110, 100, 0.1f, 4, 0.9f, 0.45f);
  snd[S_LOCK] = coil(0.5f, 50, 1.4f, 0.8f);
  { static const float a[] = { 659.25f, 783.99f, 987.77f, 1318.5f, 1567.98f }; snd[S_ARP] = notes(a, 5, 0.05f, 8, 0.3f, 0.3f); }
  snd[S_BLIP] = tone(0.05f, 1318.5f, 1318.5f, 0, 25, 0.5f, 0.2f);
  { static const float a[] = { 523.25f, 392, 329.63f, 261.63f, 196, 130.81f }; snd[S_OVER] = notes(a, 6, 0.2f, 2.5f, 0.4f, 0.3f); }
  snd[S_THUMP] = coil(0.2f, 45, 0.5f, 0.8f);
  snd[S_PULL] = tone(0.4f, 200, 120, 0.85f, 3, 0, 0.25f);
  { /* the roll: low filtered noise with a little rumble, looped */
    int n = RATE;
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
      lp += (nz() - lp) * 0.04f, lp2 += (lp - lp2) * 0.08f;
      float t = (float)i / (float)n;
      pcm[i] = (lp2 * 5 + 0.15f * (float)sin(2 * 3.14159265 * 38 * t * 1.0)) * 0.5f;
    }
    for (int i = 0; i < 400; i++) { float k = (float)i / 400; pcm[i] *= k; pcm[n - 1 - i] *= k; }
    snd[S_ROLL] = cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
  }
  snd[S_MENU] = tone(0.05f, 880, 880, 0, 25, 0.4f, 0.2f);
  snd[S_SELECT] = tone(0.12f, 660, 1320, 0, 12, 0.4f, 0.25f);
  snd[S_BACK] = tone(0.12f, 660, 330, 0, 12, 0.4f, 0.25f);
  /* the music's instruments, each at one pitch (the sequencer plays them at a rate) */
  {
    int n = (int)(0.5f * RATE);
    double ph = 0;
    for (int i = 0; i < n; i++) { /* bass: a saw through a falling filter, at 110 Hz */
      float t = (float)i / RATE;
      ph += 110.0 / RATE;
      float saw = (float)(2 * (ph - floor(ph)) - 1);
      pcm[i] = saw * 0.5f * expf(-t * 5) * (i < 50 ? (float)i / 50 : 1);
    }
    float lp = 0;
    for (int i = 0; i < n; i++) { float t = (float)i / RATE; lp += (pcm[i] - lp) * (0.08f + 0.3f * expf(-t * 12)); pcm[i] = lp * 1.6f; }
    ins[I_BASS] = cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
  }
  ins[I_PLUCK] = tone(0.35f, 440, 440, 0, 9, 0.35f, 0.35f);
  { /* pad: detuned saws, slow */
    int n = (int)(1.6f * RATE);
    double a = 0, b = 0;
    for (int i = 0; i < n; i++) {
      float t = (float)i / (float)n;
      a += 220.0 / RATE, b += 221.3 / RATE;
      float s = (float)((a - floor(a)) + (b - floor(b)) - 1);
      float env = fminf(1, t * 6) * (1 - t);
      pcm[i] = s * 0.22f * env;
    }
    float lp = 0;
    for (int i = 0; i < n; i++) lp += (pcm[i] - lp) * 0.06f, pcm[i] = lp * 2.2f;
    ins[I_PAD] = cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
  }
  ins[I_KICK] = tone(0.3f, 150, 45, 0.05f, 9, 0, 0.9f);
  ins[I_SNARE] = tone(0.2f, 220, 180, 0.75f, 14, 0, 0.45f);
  ins[I_HAT] = tone(0.05f, 8000, 9000, 0.95f, 40, 0, 0.18f);
  free(pcm);
  pcm = NULL;
}

static void play(int s, float vol, float pitch) {
  cw_audio_play(snd[s], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = vol, .pitch = pitch > 0 ? pitch : 1 });
}

void sound_menu(int which) { play(which == 0 ? S_MENU : which == 1 ? S_SELECT : S_BACK, 0.8f, 1); }

void sound_events(const game *g) {
  for (int k = 0; k < g->nfx; k++) {
    float v = g->fx[k].v;
    switch (g->fx[k].kind) {
    case FX_FLIP: play(S_FLIP, 0.7f, g->fx[k].a == 1 ? 1.05f : 1.0f); break;
    case FX_FLIP_DOWN: play(S_FLIP_DOWN, 0.4f, 1); break;
    case FX_POP: play(S_POP, 0.8f, 0.95f + 0.05f * (float)(g->fx[k].a % 3)); break;
    case FX_SLING: play(S_SLING, 0.75f, 1); break;
    case FX_TARGET: play(S_TARGET, 0.6f, 1); break;
    case FX_DROP: play(S_DROP, 0.7f, 1); break;
    case FX_DROP_RESET: play(S_RESET, 0.6f, 1); break;
    case FX_SPIN: play(S_TICK, 0.5f, 1); break;
    case FX_ROLLOVER: play(S_ROLLOVER, 0.45f, 1); break;
    case FX_RAMP_IN: play(S_RAMP_IN, 0.6f, 1); break;
    case FX_RAMP_DONE: play(S_RAMP_DONE, 0.6f, 1); break;
    case FX_HOLE: play(S_HOLE, 0.8f, 1); break;
    case FX_KICKOUT: play(S_KICK, 0.8f, 1); break;
    case FX_LAUNCH: play(S_LAUNCH, 0.5f + 0.5f * v, 1); break;
    case FX_DRAIN: play(S_DRAIN, 0.6f, 1); break;
    case FX_WALL: if (v > 0.3f) play(S_WALL, fminf(0.7f, v * 0.25f), g->fx[k].a == -2 ? 0.7f : 1); break;
    case FX_BALLS: play(S_CLICK, fminf(0.7f, v * 0.3f), 1); break;
    case FX_JACKPOT: play(S_JACKPOT, 0.9f, 1); break;
    case FX_MULTIBALL: play(S_MULTIBALL, 0.9f, 1); break;
    case FX_MODE_START: play(S_MODE, 0.8f, 1); break;
    case FX_MODE_SHOT: play(S_CHIME, 0.7f, 1); break;
    case FX_MODE_DONE: case FX_EXTRA: case FX_SKILL: play(S_FANFARE, 0.9f, 1); break;
    case FX_MODE_FAIL: play(S_FAIL, 0.7f, 1); break;
    case FX_TILT: play(S_TILT, 0.9f, 1); break;
    case FX_WARN: play(S_WARN, 0.8f, 1); break;
    case FX_SAVE: play(S_CHIME, 0.8f, 1.5f); break;
    case FX_KICKBACK: play(S_KICK, 0.9f, 0.8f); break;
    case FX_LOCK: play(S_LOCK, 0.9f, 1); break;
    case FX_LANES: play(S_ARP, 0.7f, 1); break;
    case FX_BONUS_TICK: play(S_BLIP, 0.6f, 1); break;
    case FX_GAME_OVER: play(S_OVER, 0.8f, 1); break;
    case FX_NUDGE: play(S_THUMP, 0.8f, 1); break;
    case FX_PLUNGER_PULL: play(S_PULL, 0.5f, 1); break;
    case FX_START: play(S_SELECT, 0.6f, 1); break;
    default: break;
    }
  }
  /* each ball on the table rolls: its rumble follows its speed */
  for (int i = 0; i < PB_MAXB; i++) {
    const pb_ball *b = &g->W.b[i];
    float sp = b->state == BS_FREE ? sqrtf(b->vx * b->vx + b->vy * b->vy) : b->state == BS_PATH ? fabsf(b->sv) : 0;
    if (sp > 0.05f && !cw_voice_is_playing(roll[i]))
      roll[i] = cw_audio_play(snd[S_ROLL], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = 0.01f, .loop = true });
    if (cw_voice_is_playing(roll[i])) {
      cw_voice_set_volume(roll[i], fminf(0.55f, sp * 0.18f) * (b->state == BS_PATH ? 1.3f : 1));
      cw_voice_set_pitch(roll[i], 0.6f + fminf(1.4f, sp * 0.35f));
    }
  }
}

void sound_spin_tick(void) { play(S_TICK, 0.45f, 1); }

void sound_silence_balls(void) {
  for (int i = 0; i < PB_MAXB; i++) if (cw_voice_is_playing(roll[i])) cw_voice_stop(roll[i], 0.1f);
}

/* ── the music ───────────────────────────────────────────────────────── */

static struct { int64_t next; int step; int style; float root; int level; } M;

/* a few bars per style: scale degrees for the bass (one a beat) and the lead (one an eighth; -1 rest) */
static const int MAJ[7] = { 0, 2, 4, 5, 7, 9, 11 }, MIN[7] = { 0, 2, 3, 5, 7, 8, 10 };
static const int BASS[3][16] = { { 0, 0, 5, 5, 3, 3, 4, 4, 0, 0, 5, 5, 3, 4, 4, 4 },
                                 { 0, 0, 0, 0, 5, 5, 6, 6, 0, 0, 0, 0, 3, 3, 4, 4 },
                                 { 0, 4, 0, 4, 3, 7, 3, 7, 5, 9, 5, 9, 4, 8, 4, 8 } };
static const int LEAD[3][32] = { { 7, -1, 9, 7, 4, -1, 2, 4, 5, -1, 7, -1, 4, 2, 0, -1, 7, -1, 9, 11, 12, -1, 11, 9, 7, -1, 4, 5, 4, 2, 4, -1 },
                                 { 0, 3, 7, 3, 10, 7, 3, 7, 0, 3, 7, 3, 12, 10, 7, 3, 5, 8, 12, 8, 3, 7, 10, 7, 2, 5, 9, 5, 4, 7, 11, 7 },
                                 { 4, 4, 7, 4, 9, 7, 4, -1, 5, 5, 9, 5, 11, 9, 7, -1, 4, 4, 7, 4, 9, 7, 12, 11, 9, 7, 5, 4, 2, -1, 0, -1 } };

static float deg_hz(float root, const int *sc, int d) {
  int oct = d >= 0 ? d / 7 : -((6 - d) / 7);
  int s = sc[((d % 7) + 7) % 7] + 12 * oct;
  return root * powf(2.0f, (float)s / 12.0f);
}

void music_start(int style, float root_hz) {
  M.style = style % 3, M.root = root_hz, M.step = 0;
  M.next = cw_audio_mixed_frames() + RATE / 10;
}

void music_level(int level) { M.level = level; }

void music_stop(void) { M.next = -1; }

void music_update(void) {
  if (M.next < 0 || !cw_audio_sample_rate()) return;
  int64_t now = cw_audio_mixed_frames();
  int sr = cw_audio_sample_rate();
  float bpm = M.style == 1 ? 112.0f : M.style == 2 ? 128.0f : 118.0f;
  int64_t step = (int64_t)((float)sr * 60.0f / bpm / 4.0f); /* a sixteenth */
  if (M.next < now) M.next = now + sr / 20;
  const int *sc = M.style == 1 ? MIN : MAJ;
  cw_bus bus = cw_shell_music_bus();
  while (M.next < now + sr / 5) { /* keep 200 ms scheduled */
    int s = M.step % 64, beat = s / 4, bar = s / 16;
    int64_t at = M.next;
    if (s % 4 == 0) { /* bass on the beat */
      float hz = deg_hz(M.root, sc, BASS[M.style][(bar * 4 + (s % 16) / 4) % 16]);
      cw_audio_play(ins[I_BASS], &(cw_play_desc){ .bus = bus, .volume = 0.55f, .pitch = hz / 110.0f, .start_frame = at });
    }
    if (s % 16 == 0) { /* the pad, a chord a bar */
      int d = BASS[M.style][(bar * 4) % 16];
      for (int k = 0; k < 3; k++)
        cw_audio_play(ins[I_PAD], &(cw_play_desc){ .bus = bus, .volume = 0.22f, .pitch = deg_hz(M.root * 2, sc, d + k * 2) / 220.0f, .start_frame = at });
    }
    if (M.level >= 1) { /* the beat */
      if (s % 8 == 0) cw_audio_play(ins[I_KICK], &(cw_play_desc){ .bus = bus, .volume = 0.6f, .start_frame = at });
      if (s % 8 == 4) cw_audio_play(ins[I_SNARE], &(cw_play_desc){ .bus = bus, .volume = 0.45f, .start_frame = at });
      if (s % 2 == 0) cw_audio_play(ins[I_HAT], &(cw_play_desc){ .bus = bus, .volume = s % 4 == 2 ? 0.35f : 0.2f, .start_frame = at });
    }
    if (M.level >= 2 && s % 2 == 0) { /* the lead, in modes and multiball */
      int d = LEAD[M.style][(s / 2) % 32];
      if (d >= 0) cw_audio_play(ins[I_PLUCK], &(cw_play_desc){ .bus = bus, .volume = M.level >= 3 ? 0.4f : 0.3f, .pitch = deg_hz(M.root * 4, sc, d) / 440.0f, .start_frame = at });
    }
    if (M.level >= 3 && s % 2 == 1) cw_audio_play(ins[I_HAT], &(cw_play_desc){ .bus = bus, .volume = 0.15f, .start_frame = at });
    (void)beat;
    M.step++;
    M.next += step;
  }
}
