/* emberthrone: particles and sounds. Pools of one-shot bursts (cartwheel/particles.h: one system and one draw each):
 * sparks where a blade lands, dust under a roll or a heavy landing, rubble from a broken wall, motes over a shrine
 * when it is used, glints round a pickup, and embers rising from the keep's braziers. Every sound is computed here
 * (no audio files). Presentation only: the rules never read any of it. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/particles.h"
#include "cartwheel/shell.h"
#include "game.h"

int g_fx_level = 2;
static cw_node FX_SPARK, FX_DUST, FX_RUBBLE, FX_MOTES, FX_BLOOM, FX_EMBER;
static cw_sound SND[24];
enum { SN_SWING, SN_HIT, SN_HIT_HEAVY, SN_HURT, SN_KILL, SN_ROLL, SN_PICK, SN_OPEN, SN_REST, SN_BOSS, SN_ROAR, SN_SHOCK, SN_BREAK, SN_GATE, SN_TELL, SN_DRINK, SN_WIN, SN_STEP, SN_N };

static cw_sound synth(float seconds, float hz0, float hz1, float noise, float gain, float decay, float attack) {
  enum { RATE = 48000 };
  static float pcm[RATE * 3];
  int n = CW_MIN((int)(seconds * RATE), RATE * 3);
  double phase = 0, ph2 = 0;
  uint32_t r = 7;
  float lp = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (float)n;
    phase += 2 * 3.14159265358979 * (hz0 + (hz1 - hz0) * t) / RATE;
    ph2 += 2 * 3.14159265358979 * (hz0 + (hz1 - hz0) * t) * 1.5 / RATE;
    r = r * 1664525u + 1013904223u;
    float nz = ((float)(r >> 8) / 8388608.0f - 1.0f);
    lp += (nz - lp) * 0.15f;
    float tone = (float)(sin(phase) * 0.7 + sin(ph2) * 0.3);
    float env = (float)exp(-t * decay) * (t < attack ? t / attack : 1);
    pcm[i] = (tone * (1 - noise) + lp * noise * 2.4f) * gain * env;
  }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}
static void play(int s, float vol, float pitch) {
  if (s < 0 || s >= SN_N || !SND[s].id) return;
  cw_audio_play(SND[s], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = vol, .pitch = pitch });
}

static cw_node pool(cw_node root, const char *name, int amount, int bursts, float life, float speed0, float speed1, float spread, t3_vec3 grav, float size, cw_value col, float energy) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_add_child(root, n);
  cw_node_set(n, "amount", cw_val_int(amount));
  cw_node_set(n, "bursts", cw_val_int(bursts));
  cw_node_set(n, "one_shot", cw_val_bool(true));
  cw_node_set(n, "explosiveness", cw_val_float(1));
  cw_node_set(n, "lifetime", cw_val_float(life));
  cw_node_set(n, "lifetime_random", cw_val_float(0.4f));
  cw_node_set(n, "speed_min", cw_val_float(speed0));
  cw_node_set(n, "speed_max", cw_val_float(speed1));
  cw_node_set(n, "spread", cw_val_float(spread));
  cw_node_set(n, "gravity", cw_val_vec3(grav.x, grav.y, grav.z));
  cw_node_set(n, "size", cw_val_float(size));
  cw_node_set(n, "size_random", cw_val_float(0.5f));
  cw_node_set(n, "color", col);
  cw_node_set(n, "energy", cw_val_float(energy));
  cw_node_set(n, "damping", cw_val_float(1.5f));
  cw_node_set(n, "emitting", cw_val_bool(false));
  return n;
}

void fx_init(cw_node root) {
  cw_node w = cw_node_get(root, "World");
  FX_SPARK = pool(w, "FxSpark", 384, 16, 0.45f, 3, 8, 70, (t3_vec3){ 0, -9, 0 }, 0.05f, cw_val_color(1, 0.75f, 0.35f, 1), 5);
  cw_node_set(FX_SPARK, "stretch", cw_val_float(0.12f));
  FX_DUST = pool(w, "FxDust", 256, 16, 0.9f, 0.6f, 2.2f, 80, (t3_vec3){ 0, 0.4f, 0 }, 0.28f, cw_val_color(0.75f, 0.66f, 0.52f, 0.5f), 1);
  cw_node_set(FX_DUST, "blend", cw_val_enum("ALPHA"));
  FX_RUBBLE = pool(w, "FxRubble", 256, 4, 1.4f, 2, 6, 60, (t3_vec3){ 0, -12, 0 }, 0.16f, cw_val_color(0.6f, 0.56f, 0.5f, 1), 1);
  cw_node_set(FX_RUBBLE, "blend", cw_val_enum("ALPHA"));
  FX_MOTES = pool(w, "FxMotes", 256, 4, 2.2f, 0.5f, 2.0f, 180, (t3_vec3){ 0, 1.2f, 0 }, 0.06f, cw_val_color(0.5f, 1, 1, 1), 6);
  FX_BLOOM = pool(w, "FxBloom", 256, 8, 0.9f, 1, 4, 180, (t3_vec3){ 0, 0.5f, 0 }, 0.09f, cw_val_color(1, 0.4f, 0.55f, 1), 6);
  FX_EMBER = pool(w, "FxEmber", 512, 32, 1.6f, 0.4f, 1.6f, 25, (t3_vec3){ 0, 1.5f, 0 }, 0.05f, cw_val_color(1, 0.55f, 0.15f, 1), 7);
  SND[SN_SWING] = synth(0.16f, 900, 300, 0.85f, 0.35f, 6, 0.3f);
  SND[SN_HIT] = synth(0.14f, 220, 90, 0.55f, 0.55f, 9, 0.02f);
  SND[SN_HIT_HEAVY] = synth(0.32f, 140, 50, 0.6f, 0.8f, 6, 0.02f);
  SND[SN_HURT] = synth(0.2f, 260, 140, 0.4f, 0.5f, 7, 0.02f);
  SND[SN_KILL] = synth(0.6f, 160, 40, 0.5f, 0.6f, 4, 0.02f);
  SND[SN_ROLL] = synth(0.3f, 120, 80, 0.9f, 0.3f, 5, 0.1f);
  SND[SN_PICK] = synth(0.7f, 660, 1320, 0.05f, 0.4f, 2.5f, 0.05f);
  SND[SN_OPEN] = synth(0.8f, 90, 160, 0.5f, 0.6f, 2, 0.05f);
  SND[SN_REST] = synth(1.6f, 330, 660, 0.05f, 0.45f, 1.2f, 0.2f);
  SND[SN_BOSS] = synth(1.6f, 55, 45, 0.3f, 0.8f, 1.0f, 0.15f);
  SND[SN_ROAR] = synth(1.2f, 90, 60, 0.7f, 0.9f, 1.2f, 0.1f);
  SND[SN_SHOCK] = synth(0.9f, 70, 30, 0.8f, 0.9f, 2.5f, 0.02f);
  SND[SN_BREAK] = synth(1.0f, 80, 40, 0.9f, 1.0f, 2.5f, 0.01f);
  SND[SN_GATE] = synth(1.6f, 60, 110, 0.6f, 0.7f, 1.0f, 0.1f);
  SND[SN_TELL] = synth(0.35f, 520, 780, 0.1f, 0.35f, 3, 0.05f);
  SND[SN_DRINK] = synth(0.5f, 400, 700, 0.3f, 0.3f, 3, 0.1f);
  SND[SN_WIN] = synth(2.6f, 262, 523, 0.05f, 0.5f, 0.8f, 0.2f);
  SND[SN_STEP] = synth(0.08f, 100, 60, 0.9f, 0.18f, 10, 0.01f);
}

void fx_events(void) {
  for (int k = 0; k < S.ev_n; k++) {
    const event *e = &S.ev[k];
    t3_vec3 at = { e->x, e->y, e->z };
    switch (e->kind) {
    case EV_SWING: play(SN_SWING, e->a < MAXP ? 0.5f : 0.35f, e->b == A_HEAVY || e->b == A_SLAM || e->b == A_HAMMER ? 0.7f : 1.0f); break;
    case EV_HIT:
      play(e->v >= 30 ? SN_HIT_HEAVY : SN_HIT, 0.8f, 1);
      if (e->a < MAXP) play(SN_HURT, 0.5f, 1);
      cw_particles_burst(FX_SPARK, at, (t3_vec3){ 0, 1, 0 });
      break;
    case EV_KILL: play(SN_KILL, 0.6f, 1); cw_particles_burst(FX_DUST, (t3_vec3){ e->x, e->y + 0.2f, e->z }, (t3_vec3){ 0, 1, 0 }); break;
    case EV_DODGE: if (!e->b) { play(SN_ROLL, 0.5f, 1); cw_particles_burst(FX_DUST, (t3_vec3){ e->x, e->y + 0.1f, e->z }, (t3_vec3){ 0, 1, 0 }); } break;
    case EV_DOWN: cw_particles_burst(FX_DUST, (t3_vec3){ e->x, e->y + 0.1f, e->z }, (t3_vec3){ 0, 1, 0 }); break;
    case EV_PICK: if (e->b >= 0) { play(SN_PICK, 0.7f, 1); cw_particles_burst(FX_BLOOM, at, (t3_vec3){ 0, 1, 0 }); } else play(SN_SWING, 0.4f, 1.4f); break;
    case EV_OPEN: play(SN_OPEN, 0.7f, 1); break;
    case EV_REST: play(SN_REST, 0.7f, 1); cw_particles_burst(FX_MOTES, (t3_vec3){ e->x, e->y + 1.2f, e->z }, (t3_vec3){ 0, 1, 0 }); break;
    case EV_BOSS: play(SN_BOSS, 0.9f, 1); break;
    case EV_ROAR: play(SN_ROAR, 1.0f, 0.8f); break;
    case EV_SHOCK: play(SN_SHOCK, 0.9f, 1); cw_particles_burst(FX_DUST, (t3_vec3){ e->x, e->y + 0.1f, e->z }, (t3_vec3){ 0, 1, 0 }); break;
    case EV_BREAK: play(SN_BREAK, 1.0f, 1); cw_particles_burst(FX_RUBBLE, at, (t3_vec3){ 0, 1, 0 }); cw_particles_burst(FX_DUST, at, (t3_vec3){ 0, 1, 0 }); break;
    case EV_GATE: case EV_LEVER: play(SN_GATE, 0.8f, 1); break;
    case EV_TELL: if (IS_BOSS(S.a[e->a].kind) || e->b == A_SLAM || e->b == A_CHOP) play(SN_TELL, 0.45f, IS_BOSS(S.a[e->a].kind) ? 0.7f : 1); break;
    case EV_DRINK: play(SN_DRINK, 0.6f, 1); cw_particles_burst(FX_MOTES, at, (t3_vec3){ 0, 1, 0 }); break;
    case EV_WIN: play(SN_WIN, 0.8f, 1); break;
    default: break;
    }
  }
}

/* the braziers' embers: a few bursts a second each, by the frame clock (render only) */
void fx_tick(float t) {
  static float next;
  if (t < next) return;
  next = t + 0.12f;
  const wprop *p;
  int n = world_props(&p);
  static int k;
  for (int tries = 0; tries < n; tries++) {
    k = (k + 1) % n;
    if (strcmp(p[k].what, "brazier") || !((g_world_paged >> p[k].region) & 1)) continue;
    cw_particles_burst(FX_EMBER, (t3_vec3){ p[k].x, p[k].y + 1.1f, p[k].z }, (t3_vec3){ 0, 1, 0 });
    break;
  }
}
