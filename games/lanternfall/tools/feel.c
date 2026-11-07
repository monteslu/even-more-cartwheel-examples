/* lanternfall_feel: measures the movement through player_step on a test strip of tiles, the numbers the platforming
 * is tuned by, and checks them against their bounds (tests/lanternfall-feel.mjs runs it, and its controls).
 *
 *   lanternfall_feel [--control coyote|buffer|cut]     a control turns one feature off: its check must then fail
 *
 * Measured: the height of a held jump and of a tapped one (variable height), the run's jump distance, the second
 * jump's height, the dash's reach, the coyote window (a jump pressed n ticks after running off a ledge still
 * jumps), the jump buffer (a jump pressed n ticks before landing jumps on landing), the turn-round time, the climb
 * speed. Output: one "name value" line each, then "feel ok" or "feel FAIL". */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/sim.h"

extern int g_coyote, g_jbuf;
extern float g_grav_cut;

static uint8_t T[TH][TW];
static pworld PWD;
#define FLOOR 200

static void flat(void) {
  memset(T, T_EMPTY, sizeof T);
  for (int x = 0; x < TW; x++) T[FLOOR][x] = T_SOLID, T[FLOOR + 1][x] = T_SOLID;
  PWD.t = (const uint8_t(*)[TW])T;
}

static player spawn(float x, uint32_t ab) {
  player p;
  memset(&p, 0, sizeof p);
  player_spawn(&p, x, FLOOR);
  PWD.abilities = ab;
  pinput none = { 0 };
  for (int i = 0; i < 3; i++) player_step(&p, &none, &PWD);
  return p;
}

/* a jump from standing (run: run first), held for hold ticks; returns the peak rise; *dist: where it lands */
static float jump(int run, int hold, int dj_at, uint32_t ab, float *dist) {
  flat();
  player p = spawn(100.5f, ab);
  pinput in = { 0 };
  for (int t = 0; t < run; t++) in.mx = 1, player_step(&p, &in, &PWD);
  float x0 = p.x, peak = 0;
  bool left = false;
  for (int t = 0; t < 400; t++) {
    memset(&in, 0, sizeof in);
    in.mx = run ? 1 : 0;
    in.jump = t == 0 || (dj_at && t == dj_at);
    in.jump_held = t < hold || (dj_at && t >= dj_at && t < dj_at + 20);
    player_step(&p, &in, &PWD);
    float rise = FLOOR - p.y;
    if (rise > peak) peak = rise;
    if (!p.on_ground) left = true;
    if (left && p.on_ground) break;
  }
  if (dist) *dist = p.x - x0;
  return peak;
}

/* run off a ledge, press jump `late` ticks after the ground went: did it jump? */
static bool coyote_jump(int late) {
  flat();
  for (int x = 100; x < TW; x++) T[FLOOR][x] = T[FLOOR + 1][x] = T_EMPTY;
  for (int x = 0; x < TW; x++) T[FLOOR + 30][x] = T_SOLID;
  player p = spawn(97.5f, 0);
  pinput in = { 0 };
  int k = -1; /* steps taken since the body last had ground under it */
  for (int t = 0; t < 200; t++) {
    memset(&in, 0, sizeof in);
    in.mx = 1;
    if (k < 0 && p.x - PW >= 100.0f) k = 0;
    in.jump = k == late;
    in.jump_held = k >= late;
    player_step(&p, &in, &PWD);
    if (p.ev & PE_JUMP) return true;
    if (k >= 0 && ++k > late + 2) return false;
  }
  return false;
}

/* press jump `early` ticks before the landing of a fall: did the landing jump? */
static bool buffered_jump(int early) {
  flat();
  /* a drop from a height whose landing tick is known: measure it first */
  int land_tick = -1;
  for (int pass = 0; pass < 2; pass++) {
    player p;
    memset(&p, 0, sizeof p);
    player_spawn(&p, 100.5f, FLOOR - 6);
    PWD.abilities = 0;
    pinput in = { 0 };
    for (int t = 0; t < 120; t++) {
      memset(&in, 0, sizeof in);
      if (pass == 1) {
        in.jump = t == land_tick - early;
        in.jump_held = t >= land_tick - early;
      }
      player_step(&p, &in, &PWD);
      if (pass == 0 && p.on_ground) { land_tick = t + 1; break; }
      if (pass == 1 && (p.ev & PE_JUMP)) return true;
      if (pass == 1 && t > land_tick + 3) return false;
    }
  }
  return false;
}

static float dash_reach(bool air) {
  flat();
  player p = spawn(100.5f, AB_DASH);
  pinput in = { 0 };
  if (air) {
    in.jump = in.jump_held = true;
    player_step(&p, &in, &PWD);
    for (int t = 0; t < 20; t++) in.jump = false, player_step(&p, &in, &PWD);
  }
  float x0 = p.x;
  memset(&in, 0, sizeof in);
  in.dash = true, in.mx = 1;
  player_step(&p, &in, &PWD);
  for (int t = 0; t < DASH_T + 2; t++) {
    memset(&in, 0, sizeof in);
    player_step(&p, &in, &PWD);
  }
  return p.x - x0;
}

static int turn_ticks(void) {
  flat();
  player p = spawn(100.5f, 0);
  pinput in = { 0 };
  for (int t = 0; t < 40; t++) in.mx = 1, player_step(&p, &in, &PWD);
  for (int t = 0; t < 60; t++) {
    in.mx = -1;
    player_step(&p, &in, &PWD);
    if (p.vx <= -RUN + 0.01f) return t + 1;
  }
  return 99;
}

static int bad;
static void check(const char *name, float v, float lo, float hi) {
  bool ok = v >= lo && v <= hi;
  printf("%-22s %8.3f   [%g .. %g]%s\n", name, v, lo, hi, ok ? "" : "   FAIL");
  if (!ok) bad++;
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--control") && i + 1 < argc) {
      const char *c = argv[++i];
      if (!strcmp(c, "coyote")) g_coyote = 0;
      else if (!strcmp(c, "buffer")) g_jbuf = 0;
      else if (!strcmp(c, "cut")) g_grav_cut = GRAV_UP; /* letting go no longer cuts the jump */
    }
  float d_full, d_tap, d_dj, d_still;
  float h_full = jump(0, 60, 0, 0, &d_still);
  float h_tap = jump(0, 1, 0, 0, NULL);
  jump(20, 60, 0, 0, &d_full);
  jump(20, 1, 0, 0, &d_tap);
  float h_dj = jump(0, 60, 22, AB_DJUMP, NULL);
  jump(20, 60, 22, AB_DJUMP, &d_dj);
  int coyote = 0, buffer = 0;
  for (int k = 0; k <= 12; k++)
    if (coyote_jump(k)) coyote = k;
    else break;
  for (int k = 1; k <= 14; k++)
    if (buffered_jump(k)) buffer = k;
    else break;
  check("jump_height_held", h_full, 4.3f, 5.0f);
  check("jump_height_tapped", h_tap, 0.8f, 2.2f);
  check("jump_cut_ratio", h_tap / h_full, 0.15f, 0.5f);
  check("run_jump_distance", d_full, 7.0f, 9.5f);
  check("tap_jump_distance", d_tap, 2.0f, 5.5f);
  check("double_jump_height", h_dj, 6.6f, 8.6f);
  check("double_jump_distance", d_dj, 10.0f, 14.5f);
  check("dash_ground", dash_reach(false), 4.0f, 6.0f);
  check("dash_air", dash_reach(true), 4.0f, 6.0f);
  check("coyote_ticks", (float)(coyote + 1), 5, 7);
  check("buffer_ticks", (float)buffer, 6, 9);
  check("turn_ticks", (float)turn_ticks(), 6, 14);
  printf("feel %s\n", bad ? "FAIL" : "ok");
  return bad ? 1 : 0;
}
