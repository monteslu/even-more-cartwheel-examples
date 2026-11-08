/* emberthrone: particles, impact light, sounds and pad rumble. Pools of one-shot bursts (cartwheel/particles.h: one
 * system and one draw each): sparks where a blade lands (a ring of them when a foe's poise breaks), dust under a roll,
 * a heavy landing or a boss's slam, rubble from a broken wall, motes over a shrine when it is used, glints round a
 * pickup, and embers rising from the keep's braziers. A blow that lands lights the spot with a small warm light that
 * swells over a few frames and fades over a third of a second (never a blink, never more than a body's width). Every
 * blow is felt in the hands (pad rumble, by how hard it was; a boss's slam and roar by how near you stand): the camera
 * never shakes. Every sound is computed here (no audio files). Presentation only: the rules never read any of it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/input.h"
#include "cartwheel/particles.h"
#include "cartwheel/shell.h"
#include "game.h"

int g_fx_level = 2;
static cw_node FX_SPARK, FX_DUST, FX_RUBBLE, FX_MOTES, FX_BLOOM, FX_EMBER;
#define NIMP 4
static struct { cw_node node; int tick; float peak; t3_vec3 at; } IMP[NIMP];
static int imp_next;
/* a light where a blow landed: peak energy, from this tick */
static void impact(t3_vec3 at, float peak) {
  int k = imp_next++ % NIMP;
  if (!IMP[k].node.id) return;
  IMP[k].tick = (int)S.tick, IMP[k].peak = peak, IMP[k].at = at;
  cw_node3d_set_position(IMP[k].node, at);
}
/* sparks thrown out level all round (a poise broken, a guard knocked aside) */
static void spark_ring(t3_vec3 at, int n) {
  for (int i = 0; i < n; i++) {
    float a = (float)i / (float)n * CW_TAU;
    cw_particles_burst(FX_SPARK, at, (t3_vec3){ cw_cos(a), 0.35f, cw_sin(a) });
  }
}
static cw_sound SND[32];
enum { SN_SWING, SN_HIT, SN_HIT_HEAVY, SN_HURT, SN_KILL, SN_ROLL, SN_PICK, SN_OPEN, SN_REST, SN_BOSS, SN_ROAR, SN_SHOCK, SN_BREAK, SN_GATE, SN_TELL, SN_DRINK, SN_WIN, SN_STEP,
       SN_CLANK, SN_CRIT, SN_STAGGER, SN_THROW, SN_SPLASH, SN_CLEAR, SN_N };

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
  FX_SPARK = pool(w, "FxSpark", 512, 32, 0.45f, 3, 8, 70, (t3_vec3){ 0, -9, 0 }, 0.05f, cw_val_color(1, 0.75f, 0.35f, 1), 5);
  cw_node_set(FX_SPARK, "stretch", cw_val_float(0.12f));
  FX_DUST = pool(w, "FxDust", 256, 16, 0.9f, 0.6f, 2.2f, 80, (t3_vec3){ 0, 0.4f, 0 }, 0.28f, cw_val_color(0.75f, 0.66f, 0.52f, 0.5f), 1);
  cw_node_set(FX_DUST, "blend", cw_val_enum("ALPHA"));
  FX_RUBBLE = pool(w, "FxRubble", 256, 4, 1.4f, 2, 6, 60, (t3_vec3){ 0, -12, 0 }, 0.16f, cw_val_color(0.6f, 0.56f, 0.5f, 1), 1);
  cw_node_set(FX_RUBBLE, "blend", cw_val_enum("ALPHA"));
  FX_MOTES = pool(w, "FxMotes", 256, 4, 2.2f, 0.5f, 2.0f, 180, (t3_vec3){ 0, 1.2f, 0 }, 0.06f, cw_val_color(0.5f, 1, 1, 1), 6);
  FX_BLOOM = pool(w, "FxBloom", 256, 8, 0.9f, 1, 4, 180, (t3_vec3){ 0, 0.5f, 0 }, 0.09f, cw_val_color(1, 0.4f, 0.55f, 1), 6);
  FX_EMBER = pool(w, "FxEmber", 512, 32, 1.6f, 0.4f, 1.6f, 25, (t3_vec3){ 0, 1.5f, 0 }, 0.05f, cw_val_color(1, 0.55f, 0.15f, 1), 7);
  /* the impact lights: a few small warm lights that swell and fade where blows land (ADV_PROBE_NOIMPACT: none, the
   * look probe) */
  for (int i = 0; i < (getenv("ADV_PROBE_NOIMPACT") ? 0 : NIMP); i++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Impact%d", i);
    IMP[i].node = cw_node_new("OmniLight3D", nm);
    cw_node_add_child(w, IMP[i].node);
    cw_node_set(IMP[i].node, "color", cw_val_color(1, 0.62f, 0.32f, 1));
    cw_node_set(IMP[i].node, "energy", cw_val_float(0));
    cw_node_set(IMP[i].node, "range", cw_val_float(3.2f));
    cw_node_set(IMP[i].node, "visible", cw_val_bool(false));
    IMP[i].tick = -1000;
  }
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
  SND[SN_CLANK] = synth(0.22f, 1800, 1500, 0.35f, 0.5f, 8, 0.005f);
  SND[SN_CRIT] = synth(0.45f, 110, 38, 0.55f, 0.95f, 5, 0.005f);
  SND[SN_STAGGER] = synth(0.4f, 200, 70, 0.45f, 0.6f, 5, 0.01f);
  SND[SN_THROW] = synth(0.2f, 600, 1400, 0.8f, 0.3f, 5, 0.1f);
  SND[SN_SPLASH] = synth(0.7f, 300, 120, 0.95f, 0.6f, 3, 0.02f);
  SND[SN_CLEAR] = synth(0.9f, 523, 784, 0.05f, 0.35f, 2.2f, 0.04f);
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
      if (e->v >= 30) cw_particles_burst(FX_SPARK, at, (t3_vec3){ 0, 0.4f, 0 });
      impact(at, e->v >= 30 ? 3.2f : 2.0f);
      break;
    case EV_CRIT:
      play(SN_CRIT, 1.0f, 1);
      play(SN_HIT_HEAVY, 0.8f, 0.8f);
      spark_ring(at, 6);
      cw_particles_burst(FX_SPARK, at, (t3_vec3){ 0, 1, 0 });
      impact(at, 4.5f);
      break;
    case EV_STAGGER:
      play(SN_STAGGER, 0.8f, e->a >= MAXP && IS_BOSS(S.a[e->a].kind) ? 0.7f : 1);
      spark_ring(at, 5);
      impact(at, 3.0f);
      break;
    case EV_BLOCKED:
      play(SN_CLANK, 0.8f, 1);
      cw_particles_burst(FX_SPARK, at, (t3_vec3){ 0, 0.6f, 0 });
      impact(at, 1.6f);
      break;
    case EV_GUARD_BREAK:
      play(SN_CLANK, 0.9f, 0.6f);
      play(SN_STAGGER, 0.8f, 0.9f);
      spark_ring(at, 8);
      impact(at, 3.6f);
      break;
    case EV_THROW: play(SN_THROW, 0.55f, 1); break;
    case EV_STONE: if (!e->v) cw_particles_burst(FX_DUST, at, (t3_vec3){ 0, 1, 0 }); break;
    case EV_RISE: play(SN_SPLASH, 0.8f, 1); cw_particles_burst(FX_DUST, (t3_vec3){ e->x, e->y + 0.3f, e->z }, (t3_vec3){ 0, 1, 0 }); cw_particles_burst(FX_MOTES, (t3_vec3){ e->x, e->y + 0.3f, e->z }, (t3_vec3){ 0, 1, 0 }); break;
    case EV_CLEAR:
      if (e->v > 0) {
        play(SN_CLEAR, 0.6f, 1);
        for (int i = 0; i < MAXP; i++) if (S.a[i].used && S.a[i].state != ST_DEAD) cw_particles_burst(FX_MOTES, (t3_vec3){ S.a[i].mv.position.x, S.a[i].mv.position.y + 1.0f, S.a[i].mv.position.z }, (t3_vec3){ 0, 1, 0 });
      }
      break;
    case EV_LAND: /* a boss spent after a big blow: its breath steams */
      play(SN_ROLL, 0.5f, 0.6f);
      cw_particles_burst(FX_DUST, at, (t3_vec3){ 0, 1, 0 });
      break;
    case EV_KILL: play(SN_KILL, 0.6f, 1); cw_particles_burst(FX_DUST, (t3_vec3){ e->x, e->y + 0.2f, e->z }, (t3_vec3){ 0, 1, 0 }); break;
    case EV_DODGE: if (!e->b) { play(SN_ROLL, 0.5f, 1); cw_particles_burst(FX_DUST, (t3_vec3){ e->x, e->y + 0.1f, e->z }, (t3_vec3){ 0, 1, 0 }); } break;
    case EV_DOWN: cw_particles_burst(FX_DUST, (t3_vec3){ e->x, e->y + 0.1f, e->z }, (t3_vec3){ 0, 1, 0 }); break;
    case EV_PICK: if (e->b >= 0) { play(SN_PICK, 0.7f, 1); cw_particles_burst(FX_BLOOM, at, (t3_vec3){ 0, 1, 0 }); } else play(SN_SWING, 0.4f, 1.4f); break;
    case EV_OPEN: play(SN_OPEN, 0.7f, 1); break;
    case EV_REST: play(SN_REST, 0.7f, 1); cw_particles_burst(FX_MOTES, (t3_vec3){ e->x, e->y + 1.2f, e->z }, (t3_vec3){ 0, 1, 0 }); break;
    case EV_BOSS: play(SN_BOSS, 0.9f, 1); break;
    case EV_ROAR: play(SN_ROAR, 1.0f, 0.8f); break;
    case EV_SHOCK: play(SN_SHOCK, e->b < 0 ? 0.6f : 0.9f, e->b < 0 ? 1.2f : 1); cw_particles_burst(FX_DUST, (t3_vec3){ e->x, e->y + 0.1f, e->z }, (t3_vec3){ 0, 1, 0 }); break;
    case EV_BREAK: play(SN_BREAK, 1.0f, 1); cw_particles_burst(FX_RUBBLE, at, (t3_vec3){ 0, 1, 0 }); cw_particles_burst(FX_DUST, at, (t3_vec3){ 0, 1, 0 }); break;
    case EV_GATE: case EV_LEVER: play(SN_GATE, 0.8f, 1); break;
    case EV_TELL: { /* every wind-up is heard: lower for the big ones and the bosses */
      const actor *f = &S.a[e->a];
      bool big = IS_BOSS(f->kind) || ATK[e->b].knock || ATK[e->b].ring;
      play(SN_TELL, big ? 0.5f : 0.3f, IS_BOSS(f->kind) ? 0.7f : f->kind == K_BRUTE ? 0.8f : e->b == A_THROW ? 1.3f : 1);
      break;
    }
    case EV_DRINK: play(SN_DRINK, 0.6f, 1); cw_particles_burst(FX_MOTES, at, (t3_vec3){ 0, 1, 0 }); break;
    case EV_WIN: play(SN_WIN, 0.8f, 1); break;
    default: break;
    }
  }
}

/* the impact lights: up over 3 ticks, down over 20 (eased), so a hit glows and never blinks */
static void lights(void) {
  for (int i = 0; i < NIMP; i++) {
    if (!IMP[i].node.id) continue;
    float age = (float)((int)S.tick - IMP[i].tick), e = 0;
    if (age >= 0 && age < 3) e = (age + 1) / 3;
    else if (age >= 3 && age < 23) { float k = 1 - (age - 3) / 20; e = k * k; }
    e *= IMP[i].peak * (g_fx_level > 0 ? 1 : 0.6f);
    cw_node_set(IMP[i].node, "energy", cw_val_float(e));
    cw_node_set(IMP[i].node, "visible", cw_val_bool(e > 0.01f)); /* an unlit light still costs the clusters */
  }
}

/* pad rumble from the tick's events (after every rule tick, so a host that draws fewer frames than it ticks still
 * feels every blow); the request count is a debug field the gates pin */
int g_rumble_n;
static void rumble(int seat, float low, float high, int ms) {
  if (seat < 0 || seat >= MAXP || !S.a[seat].used) return;
  cw_input_rumble(seat, low, high, ms);
  g_rumble_n++;
}
static float near_k(int seat, float x, float z, float r) {
  const actor *p = &S.a[seat];
  float d = sqrtf(dist2(p->mv.position.x, p->mv.position.z, x, z));
  return cw_clampf(1 - d / r, 0, 1);
}
void fx_feel(void) {
  if (g_rules_off & FR_RUMBLE) return; /* (the rules test's control) */
  for (int k = 0; k < S.ev_n; k++) {
    const event *e = &S.ev[k];
    int by = e->c;
    switch (e->kind) {
    case EV_HIT: case EV_CRIT:
      if (e->a < MAXP) rumble(e->a, 0.75f, 0.35f, 170); /* a blow taken */
      else if (by >= 0 && by < MAXP) {
        if (e->kind == EV_CRIT) rumble(by, 0.9f, 0.6f, 200);
        else if (e->b == A_MAUL2) rumble(by, 0.8f, 0.5f, 160);
        else if (e->b == A_HEAVY || e->b == A_MAUL1) rumble(by, 0.55f, 0.45f, 120);
        else if (e->b == A_SLASH3) rumble(by, 0.3f, 0.5f, 90);
        else rumble(by, 0.18f, 0.45f, 70);
      }
      break;
    case EV_STAGGER: case EV_DOWN:
      if (e->a >= MAXP) { int h = S.a[e->a].last_hit_by; if (h >= 0 && h < MAXP) rumble(h, 0.6f, 0.3f, 150); }
      break;
    case EV_BLOCKED: rumble(by, 0.1f, 0.7f, 70); break;
    case EV_GUARD_BREAK: rumble(by, 0.7f, 0.6f, 180); break;
    case EV_KILL: { int h = S.a[e->a].last_hit_by; if (h >= 0 && h < MAXP) rumble(h, 0.45f, 0.2f, 140); break; }
    case EV_SHOCK: /* a slam or a shockwave: by how near */
      for (int i = 0; i < MAXP; i++) { float n = near_k(i, e->x, e->z, 16); if (n > 0 && (e->a >= MAXP || e->a == i)) rumble(i, 0.85f * n, 0.25f * n, 220); }
      break;
    case EV_ROAR: for (int i = 0; i < MAXP; i++) { float n = near_k(i, e->x, e->z, 28); if (n > 0) rumble(i, 0.55f * n, 0.15f * n, 700); } break;
    case EV_RISE: for (int i = 0; i < MAXP; i++) { float n = near_k(i, e->x, e->z, 10); if (n > 0) rumble(i, 0.4f * n, 0.1f * n, 300); } break;
    case EV_BREAK: rumble(e->a, 1.0f, 0.5f, 350); break;
    case EV_GATE: case EV_LEVER: rumble(e->a, 0.35f, 0.1f, 600); break;
    case EV_PICK: if (e->b > 0) rumble(e->a, 0.2f, 0.35f, 120); break;
    case EV_DODGE: if (e->b) rumble(e->a, 0.05f, 0.3f, 60); break; /* a blow rolled through */
    case EV_CLEAR: for (int i = 0; i < MAXP; i++) if (e->v > 0) rumble(i, 0.15f, 0.35f, 200); break;
    default: break;
    }
  }
}

/* the braziers' embers: a few bursts a second each, by the frame clock (render only) */
void fx_tick(float t) {
  lights();
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
