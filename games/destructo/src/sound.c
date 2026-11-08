/* destructo's sound: every effect made in code at load (no audio files).
 *   the launcher   a deep boom with a crack of noise; the charge's fuse fizzes in flight
 *   breaks         wood: a splintering crack over a hollow knock; stone: a dull crunch and grit; glass: a bright
 *                  smash of ringing shards; steel: a clang that rings on
 *   hits           thumps by weight and speed (a collapse is a rumble of them, a few a tick at most)
 *   charges        a big blast with a long low tail
 *   crystals       a chime that climbs as they fall; a fanfare when the last goes, a falling phrase when the ammunition
 *                  runs out
 *   the field      a soft wind loop with birds now and then
 * Each sound is panned by where it happens on screen and quieter far away. */
#include "sound.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/shell.h"
#include "view.h"

#define RATE 44100
enum { S_LAUNCH, S_WOOD, S_STONE, S_GLASS, S_STEEL, S_THUD, S_THUD_SMALL, S_BOOM, S_CHIME, S_WIN, S_LOSE, S_MENU, S_SELECT, S_BACK,
       S_TICK, S_WIND, S_BIRD, S_TURN, S_COUNT };

static cw_sound snd[S_COUNT];
static cw_voice wind;
static float *pcm;
static uint32_t rng = 7;
static float nz(void) { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 8388608.0f - 1.0f; }
static cw_sound make(int n) { return cw_sound_from_pcm(pcm, n, 1, RATE, NULL); }

/* a thump: a falling sine body and a noise attack through a low-pass */
static cw_sound thump(float secs, float hz, float noise, float lp_k, float decay, float gain) {
  int n = (int)(secs * RATE);
  double ph = 0;
  float lp = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / RATE;
    ph += 2 * 3.14159265358979 * hz * (1 + 1.5f * expf(-t * 40)) / RATE;
    lp += (nz() - lp) * lp_k;
    float a = i < 30 ? (float)i / 30 : 1;
    pcm[i] = ((float)sin(ph) * expf(-t * decay) + lp * noise * expf(-t * decay * 1.6f)) * gain * a;
  }
  return make(n);
}
/* wood breaking: crackles (sparse clicks) over a hollow knock */
static cw_sound splinter(void) {
  int n = (int)(0.55f * RATE);
  double ph = 0;
  float hp = 0, last = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / RATE;
    ph += 2 * 3.14159265358979 * 180 * (1 + expf(-t * 30)) / RATE;
    float x = nz();
    hp = 0.85f * (hp + x - last), last = x;
    float click = (nz() > 0.985f - 0.05f * expf(-t * 8)) ? 1.0f : 0.0f;
    pcm[i] = ((float)sin(ph) * expf(-t * 22) * 0.7f + hp * expf(-t * 14) * 0.35f + click * expf(-t * 6) * 0.6f) * 0.8f;
  }
  return make(n);
}
/* glass: bright noise bursts and ringing partials */
static cw_sound smash(void) {
  int n = (int)(1.0f * RATE);
  static const float hz[5] = { 2630, 3170, 4120, 5280, 6210 };
  double ph[5] = { 0 };
  float hp = 0, last = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / RATE;
    float x = nz();
    hp = 0.6f * (hp + x - last), last = x;
    float v = hp * expf(-t * 9) * 0.5f;
    for (int k = 0; k < 5; k++) {
      ph[k] += 2 * 3.14159265358979 * hz[k] / RATE;
      v += (float)sin(ph[k]) * expf(-t * (5 + k * 2)) * 0.08f * (nz() > -0.6f ? 1 : 0.3f);
    }
    pcm[i] = v * 0.8f;
  }
  return make(n);
}
/* steel: inharmonic partials that ring */
static cw_sound clang(void) {
  int n = (int)(1.4f * RATE);
  static const float hz[4] = { 410, 1087, 1840, 2930 };
  double ph[4] = { 0 };
  for (int i = 0; i < n; i++) {
    float t = (float)i / RATE;
    float v = nz() * expf(-t * 60) * 0.3f;
    for (int k = 0; k < 4; k++) {
      ph[k] += 2 * 3.14159265358979 * hz[k] / RATE;
      v += (float)sin(ph[k]) * expf(-t * (2.5f + k * 1.8f)) * (0.3f - k * 0.05f);
    }
    pcm[i] = v * 0.7f;
  }
  return make(n);
}
/* a blast: a deep sweep and rolling noise */
static cw_sound blast(float secs, float hz, float gain) {
  int n = (int)(secs * RATE);
  double ph = 0;
  float lp = 0, lp2 = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / RATE;
    ph += 2 * 3.14159265358979 * hz * (0.6f + 1.4f * expf(-t * 6)) / RATE;
    lp += (nz() - lp) * (0.06f + 0.4f * expf(-t * 12)), lp2 += (lp - lp2) * 0.2f;
    float a = i < 40 ? (float)i / 40 : 1;
    pcm[i] = ((float)sin(ph) * expf(-t * 3.5f) * 0.8f + lp2 * 3.5f * expf(-t * 1.6f)) * gain * a;
  }
  return make(n);
}
static cw_sound notes(const float *hz, int count, float each, float decay, float square, float gain) {
  int per = (int)(each * RATE), n = per * count + RATE / 2;
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
  return make(n);
}
static cw_sound tone(float secs, float hz0, float hz1, float decay, float gain) {
  int n = (int)(secs * RATE);
  double ph = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (float)n;
    ph += 2 * 3.14159265358979 * (hz0 + (hz1 - hz0) * t) / RATE;
    float a = i < 60 ? (float)i / 60 : 1;
    pcm[i] = (float)sin(ph) * expf(-t * decay) * gain * a;
  }
  return make(n);
}

void sound_init(void) {
  pcm = malloc(sizeof(float) * RATE * 6);
  snd[S_LAUNCH] = blast(1.2f, 60, 0.9f);
  snd[S_WOOD] = splinter();
  snd[S_STONE] = thump(0.6f, 90, 1.4f, 0.12f, 9, 0.75f);
  snd[S_GLASS] = smash();
  snd[S_STEEL] = clang();
  snd[S_THUD] = thump(0.35f, 70, 0.9f, 0.08f, 14, 0.8f);
  snd[S_THUD_SMALL] = thump(0.18f, 140, 0.8f, 0.2f, 26, 0.5f);
  snd[S_BOOM] = blast(3.0f, 45, 1.0f);
  { static const float a[] = { 1318.5f, 1760, 2093 }; snd[S_CHIME] = notes(a, 3, 0.07f, 4, 0.1f, 0.3f); }
  { static const float a[] = { 523.25f, 659.25f, 783.99f, 1046.5f, 783.99f, 1046.5f, 1318.5f }; snd[S_WIN] = notes(a, 7, 0.11f, 2.5f, 0.35f, 0.32f); }
  { static const float a[] = { 392, 349.23f, 311.13f, 261.63f }; snd[S_LOSE] = notes(a, 4, 0.2f, 2.5f, 0.4f, 0.3f); }
  snd[S_MENU] = tone(0.05f, 880, 880, 6, 0.2f);
  snd[S_SELECT] = tone(0.12f, 660, 1320, 4, 0.25f);
  snd[S_BACK] = tone(0.12f, 660, 330, 4, 0.22f);
  snd[S_TICK] = tone(0.03f, 1800, 1700, 8, 0.12f);
  { static const float a[] = { 587.33f, 880 }; snd[S_TURN] = notes(a, 2, 0.09f, 5, 0.3f, 0.25f); }
  { /* wind: slow filtered noise, looped */
    int n = RATE * 4;
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
      float t = (float)i / (float)n;
      lp += (nz() - lp) * 0.02f, lp2 += (lp - lp2) * 0.03f;
      pcm[i] = lp2 * 6 * (0.6f + 0.4f * sinf(t * 6.2831853f * 2));
    }
    for (int i = 0; i < 2000; i++) { float k = (float)i / 2000; pcm[i] *= k; pcm[n - 1 - i] *= k; }
    snd[S_WIND] = make(n);
  }
  { /* a bird: two quick falling chirps */
    int n = RATE / 3;
    double ph = 0;
    for (int i = 0; i < n; i++) {
      float t = (float)i / RATE, c = fmodf(t, 0.12f);
      ph += 2 * 3.14159265358979 * (4200 - c * 14000) / RATE;
      pcm[i] = (float)sin(ph) * (c < 0.07f ? expf(-c * 30) : 0) * 0.12f;
    }
    snd[S_BIRD] = make(n);
  }
}

static float master = 1;
static void play(int s, float vol, float pitch, float pan) {
  cw_audio_play(snd[s], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = vol * master, .pitch = pitch > 0 ? pitch : 1, .pan = pan });
}
/* volume and pan from where the sound is on screen and how far */
static bool placed(t3_vec3 p, t3_vec3 cam, float *vol, float *pan) {
  float u = 0.5f, v = 0.5f;
  bool on = view_screen(p, &u, &v);
  float dx = p.x - cam.x, dy = p.y - cam.y, dz = p.z - cam.z, d = sqrtf(dx * dx + dy * dy + dz * dz);
  *vol = 1.0f / (1.0f + d * 0.04f);
  *pan = on ? fmaxf(-0.8f, fminf(0.8f, (u - 0.5f) * 1.6f)) : 0;
  return true;
}

void sound_menu(int which) { play(which == 0 ? S_MENU : which == 1 ? S_SELECT : S_BACK, 0.8f, 1, 0); }
void sound_turn(void) { play(S_TURN, 0.7f, 1, 0); }
void sound_result(bool won) { play(won ? S_WIN : S_LOSE, 0.8f, 1, 0); }

void sound_events(const game *g, t3_vec3 cam) {
  int per[S_COUNT] = { 0 };
  static const int limit[S_COUNT] = { [S_WOOD] = 2, [S_STONE] = 2, [S_GLASS] = 2, [S_STEEL] = 2, [S_THUD] = 3, [S_THUD_SMALL] = 3 };
  for (int k = 0; k < g->nev; k++) {
    const sim_event *e = &g->ev[k];
    float vol, pan;
    placed(e->p, cam, &vol, &pan);
    float vary = 0.92f + 0.16f * hashf((uint32_t)g->tick * 31u + (uint32_t)k);
    int s = -1;
    float v = vol;
    switch (e->kind) {
    case EV_LAUNCH: s = S_LAUNCH, v = 0.9f; break;
    case EV_BOOM: s = S_BOOM, v = fminf(1, vol * 2.2f); break;
    case EV_TARGET: play(S_CHIME, 0.8f, 1 + 0.12f * (float)(g->ntgt - g->tgt_left), pan); break;
    case EV_BREAK: s = e->mat == M_WOOD ? S_WOOD : e->mat == M_STEEL ? S_STEEL : S_STONE; v = vol * fminf(1, 0.4f + e->size); break;
    case EV_SHATTER: s = S_GLASS; v = vol * 0.8f; break;
    case EV_STRIKE: play(S_THUD, fminf(1, vol * 2.0f), 0.8f, pan); s = e->mat == M_WOOD ? S_WOOD : e->mat == M_STEEL ? S_STEEL : S_STONE, v = fminf(1, vol * 1.6f); break; /* the ball crushing in: a deep thud and the crack of what it broke */
    case EV_HIT: case EV_LAND:
      if (e->mat == M_STEEL && e->speed > 3) s = S_STEEL, v = vol * fminf(0.6f, e->speed * 0.06f);
      else s = e->a > 8000 ? S_THUD : S_THUD_SMALL, v = vol * fminf(1, 0.25f + e->a / 30000.0f);
      break;
    default: break;
    }
    if (s < 0) continue;
    if (limit[s] && per[s] >= limit[s]) continue;
    per[s]++;
    play(s, v, vary, pan);
  }
}

void sound_field(bool on) {
  if (on && !cw_voice_is_playing(wind)) wind = cw_audio_play(snd[S_WIND], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = 0.35f, .loop = true, .fade_in = 1.5f });
  if (!on && cw_voice_is_playing(wind)) cw_voice_stop(wind, 0.5f);
}
void sound_tick(uint32_t tick) {
  if (cw_voice_is_playing(wind) && hash32(tick * 2654435761u) % 900u == 0) play(S_BIRD, 0.5f, 0.9f + 0.2f * hashf(tick), hashf(tick * 3u) - 0.5f);
}
