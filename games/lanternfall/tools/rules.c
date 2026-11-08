/* lanternfall_rules: the combat rules measured through sim_step on a small test world (tests/lanternfall-rules.mjs
 * runs it, and its controls).
 *
 *   lanternfall_rules [--control hitstop|knockback|recoil|puff|slain|trial|phase|charms|echo]
 *
 * A control turns one rule off (g_rules_off): its check must then fail. Measured: a blow that lands holds the world
 * still (both sides), pushes the enemy away and the striker back; a puff gives only to a blow from above (and that
 * bounces you); creatures struck down stay down until a rest; an upgrade's room locks for its trial and its find
 * waits for the guardians; a warden staggers at half health and adds its second phase's move; a relic's charm (the
 * long wick) reaches what a plain strike cannot; a fall leaves your flame (a measure of energy) where you fell, and
 * touching it gives the measure back. Output: one line each, then "rules ok" or "rules FAIL". */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/sim.h"

static world W;
static sim S;
static int bad;

static void check(const char *name, bool ok, const char *what) {
  printf("%-14s %s   %s\n", name, ok ? "ok  " : "FAIL", what);
  if (!ok) bad++;
}

static void carve(int x0, int y0, int x1, int y1) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) W.t[y][x] = T_EMPTY;
}
static int add_room(int cx, int cy, int cw, int kind) {
  int i = W.nrooms++;
  W.rooms[i] = (room){ .x = (uint8_t)cx, .y = (uint8_t)cy, .w = (uint8_t)cw, .h = 1, .region = R_GATE, .kind = (uint8_t)kind };
  for (int k = 0; k < cw; k++) W.cell_room[cy][cx + k] = (uint16_t)i;
  carve(cx * CELL_W + 2, cy * CELL_H + 2, (cx + cw) * CELL_W - 3, cy * CELL_H + CELL_H - 3);
  return i;
}
static int add_ent(int kind, int sub, int arg, int room, float x, float y, int id) {
  W.ents[W.nents] = (ent){ .kind = (uint8_t)kind, .sub = (uint8_t)sub, .arg = (uint8_t)arg, .hold = HOLD_FLOOR, .room = (uint16_t)room, .id = (uint16_t)id, .x = x, .y = y };
  return W.nents++;
}

/* the test world: a hall (room 0) with a door to an item room (room 1), and a warden's arena (room 2) apart */
static int R_HALLR, R_ITEMR, R_ARENA, E_FOE, E_RELIC, E_SHRINE;
#define FLOOR0 (CELL_H * 2 - 2) /* the hall's and the item room's floor row */
static void build(void) {
  memset(&W, 0, sizeof W);
  memset(W.t, T_SOLID, sizeof W.t);
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) W.cell_room[y][x] = 0xffff;
  R_HALLR = add_room(0, 1, 2, RK_PLAIN);
  R_ITEMR = add_room(2, 1, 1, RK_ITEM);
  R_ARENA = add_room(0, 3, 2, RK_BOSS);
  carve(2 * CELL_W - 3, FLOOR0 - 4, 2 * CELL_W + 2, FLOOR0 - 1); /* the door between hall and item room */
  W.doors[W.ndoors++] = (door){ .a = (uint16_t)R_HALLR, .b = (uint16_t)R_ITEMR, .vertical = 0, .x = (uint16_t)(2 * CELL_W - 2), .y = (uint16_t)(FLOOR0 - 4), .len = 4 };
  W.rooms[R_ARENA].boss = 1;
  add_ent(EK_PICKUP, PK_HEALTH, 0, R_ITEMR, (float)(2 * CELL_W + 16), (float)FLOOR0, 0);
  E_RELIC = add_ent(EK_PICKUP, PK_RELIC, 0, R_HALLR, 50.5f, (float)FLOOR0, 1);
  W.npickups = 2;
  E_SHRINE = add_ent(EK_SAVE, 0, 0, R_HALLR, 8.5f, (float)FLOOR0, 0);
  E_FOE = add_ent(EK_ENEMY, EN_SPITTER, 0, R_HALLR, 40.5f, (float)FLOOR0, 0);
  add_ent(EK_BOSS, 0, 0, R_ARENA, (float)CELL_W, (float)(CELL_H * 4 - 2), 0);
  W.nbosses = 1, W.nsaves = 1;
  W.start_x = 12.5f, W.start_y = (float)FLOOR0;
}

static void fresh(void) {
  sim_init(&S, &W, 1);
  S.god = true;
  S.abilities = AB_BOLT;
  S.p[0].hp = S.p[0].hp_max = 5;
}
static pinput NONE;
static void step(const pinput *in) {
  pinput a[MAXP] = { *in, NONE };
  sim_step(&S, a);
}
static void idle(int n) {
  for (int i = 0; i < n; i++) step(&NONE);
}
static enemy *put_enemy(int kind, float x, float y, int hp) {
  enemy *e = &S.en[S.nen++];
  memset(e, 0, sizeof *e);
  e->kind = (uint8_t)kind, e->alive = 1, e->x = e->hx = x, e->y = e->hy = y, e->hp = e->hp_max = (int16_t)hp, e->ent = -1, e->facing_left = 1;
  return e;
}
static bool event_seen(int kind) {
  for (int i = 0; i < S.nev; i++)
    if (S.ev[i].kind == kind) return true;
  return false;
}

/* a strike at a crawler in front: the hold, its push away, the striker's push back */
static void blow(void) {
  fresh();
  S.nen = 0;
  idle(5);
  enemy *e = put_enemy(EN_CRAWLER, S.p[0].x + 1.7f, (float)FLOOR0, 9);
  pinput a = { .attack = true };
  step(&a);
  int t = 0;
  while (t++ < 20 && !event_seen(EV_HIT)) idle(1);
  float ex = e->x, px = S.p[0].x;
  int held = 0;
  for (int k = 0; k < 8; k++) {
    idle(1);
    if (e->x == ex && S.p[0].x == px) held++;
    else break;
  }
  idle(16);
  char b[160];
  snprintf(b, sizeof b, "the world held %d ticks after the blow landed (both sides still)", held);
  check("hitstop", held >= 3 && held <= 8, b);
  snprintf(b, sizeof b, "the crawler pushed %.2f tiles away", e->x - ex);
  check("knockback", e->x - ex >= 0.8f, b);
  snprintf(b, sizeof b, "the striker pushed %.2f tiles back", px - S.p[0].x);
  check("recoil", px - S.p[0].x >= 0.12f, b);
}

/* a puff: a strike from the side is turned; one from above hurts it and bounces you */
static void puff(void) {
  fresh();
  S.nen = 0;
  idle(5);
  enemy *e = put_enemy(EN_PUFF, S.p[0].x + 1.6f, (float)FLOOR0 - 0.2f, 3);
  pinput a = { .attack = true };
  step(&a);
  idle(24);
  bool side_turned = e->hp == 3;
  e->state = ES_IDLE, e->x = e->hx = S.p[0].x, e->y = e->hy = (float)FLOOR0 - 3;
  S.p[0].y = (float)FLOOR0 - 5.2f, S.p[0].vy = 0, S.p[0].on_ground = 0;
  pinput d = { .attack = true, .my = 1 };
  step(&d);
  float vy = 0;
  for (int k = 0; k < 12; k++) {
    idle(1);
    if (S.p[0].vy < vy) vy = S.p[0].vy;
  }
  char b[160];
  snprintf(b, sizeof b, "a strike from the side %s it", side_turned ? "was turned by" : "hurt");
  check("puff_side", side_turned, b);
  snprintf(b, sizeof b, "a strike from above: health %d of 3, bounce %.1f tiles/s", e->hp, -vy);
  check("puff_above", e->hp < 3 && vy < -14, b);
}

/* creatures struck down stay down until a rest */
static void slain(void) {
  fresh();
  int n0 = 0;
  for (int i = 0; i < S.nen; i++) n0 += S.en[i].alive && S.en[i].ent == E_FOE;
  for (int i = 0; i < S.nen; i++)
    if (S.en[i].ent == E_FOE) S.en[i].hp = 1, S.p[0].x = S.en[i].x - 1.6f, S.p[0].facing_left = 0;
  pinput a = { .attack = true };
  step(&a);
  idle(20);
  sim_enter_room(&S, R_ITEMR);
  sim_enter_room(&S, R_HALLR);
  int n1 = 0;
  for (int i = 0; i < S.nen; i++) n1 += S.en[i].alive && S.en[i].ent == E_FOE;
  sim_teleport(&S, W.ents[E_SHRINE].x, W.ents[E_SHRINE].y);
  idle(3);
  pinput up = { .interact = true };
  step(&up);
  idle(3);
  sim_enter_room(&S, R_ITEMR);
  sim_enter_room(&S, R_HALLR);
  int n2 = 0;
  for (int i = 0; i < S.nen; i++) n2 += S.en[i].alive && S.en[i].ent == E_FOE;
  char b[160];
  snprintf(b, sizeof b, "there at first %d, after it fell and the room was left and entered again %d, after a rest %d", n0, n1, n2);
  check("slain", n0 == 1 && n1 == 0 && n2 == 1, b);
}

/* the item room locks for its trial; the find waits for the guardians */
static void trial(void) {
  fresh();
  float px = (float)(2 * CELL_W + 8) + 0.5f;
  sim_teleport(&S, px, (float)FLOOR0);
  idle(10);
  const door *d = &W.doors[0];
  bool locked = S.trial_room == R_ITEMR && S.t[d->y + 1][d->x + 1] == T_BOSSDOOR;
  S.p[0].x = W.ents[0].x; /* onto the find while its guardians stand */
  idle(60);
  bool waited = !sim_got(&S, 0);
  int guards = 0;
  for (int t = 0; t < 2000 && S.trial_room >= 0; t++) { /* strike them down (a test's hand) */
    for (int i = 0; i < S.nen; i++)
      if (S.en[i].alive) S.en[i].alive = 0, guards++;
    idle(1);
  }
  S.p[0].x = W.ents[0].x + 0.3f;
  idle(5);
  bool open = S.t[d->y + 1][d->x + 1] != T_BOSSDOOR;
  char b[200];
  snprintf(b, sizeof b, "doors %s, the find %s while %d guardians stood, then %s with the doors %s", locked ? "locked" : "open", waited ? "waited" : "was taken",
           guards, sim_got(&S, 0) ? "taken" : "not taken", open ? "open" : "shut");
  check("trial", locked && waited && guards >= 4 && sim_got(&S, 0) && open, b);
}

/* the warden staggers at half health, then uses its second phase's move */
static void phase(void) {
  fresh();
  S.abilities = AB_ALL;
  sim_teleport(&S, (float)(CELL_W - 8), (float)(CELL_H * 4 - 2));
  for (int t = 0; t < 200 && !S.b.active; t++) idle(1);
  if (!S.b.active) { check("phase", false, "the warden never woke"); return; }
  S.b.hp = (int16_t)(S.b.hp_max / 2 - 1);
  bool stagger = false, rain = false;
  for (int t = 0; t < 4000 && S.b.active; t++) {
    idle(1);
    if (S.b.state == ES_HURT && S.b.phase == 1) stagger = true;
    if (S.b.state == ES_TELL && S.b.move == BM_RAIN) rain = true;
  }
  char b[160];
  snprintf(b, sizeof b, "at half health it %s, and its second phase's move (rain) %s", stagger ? "staggered" : "fought on", rain ? "came" : "never came");
  check("phase", stagger && rain, b);
}

/* the first relic: its charm (the long wick) reaches what a plain strike cannot */
static bool reach_hits(void) {
  S.nen = 0;
  S.p[0].x = 30.5f, S.p[0].y = (float)FLOOR0, S.p[0].vx = 0, S.p[0].facing_left = 0, S.p[0].atk_cd = 0;
  enemy *e = put_enemy(EN_SPITTER, S.p[0].x + 3.25f, (float)FLOOR0, 5);
  pinput a = { .attack = true };
  step(&a);
  idle(10);
  return e->hp < 5;
}
static void charms(void) {
  fresh();
  bool plain = reach_hits();
  sim_teleport(&S, W.ents[E_RELIC].x, W.ents[E_RELIC].y);
  idle(4);
  bool got = (S.charms & CHARM_BIT(CH_REACH)) != 0;
  bool wick = reach_hits();
  char b[160];
  snprintf(b, sizeof b, "plain strike %s at 3.25 tiles; the relic %s the long wick; then the strike %s", plain ? "hit" : "missed", got ? "gave" : "did not give", wick ? "hit" : "missed");
  check("charms", !plain && got && wick, b);
}

/* a fall leaves your flame with a measure of energy; touching it gives it back */
static void echo(void) {
  fresh();
  S.god = false;
  idle(5);
  int en0 = S.p[0].en_max;
  float fx = S.p[0].x, fy = S.p[0].y;
  S.p[0].x = 30.5f, S.p[0].safe_x = 30.5f; /* (where it falls) */
  S.p[0].hp = 0;
  idle(2);
  int en1 = S.p[0].en_max;
  bool left = S.echo && fabsf(S.echo_x - 30.5f) < 1;
  sim_teleport(&S, S.echo_x, S.echo_y);
  idle(3);
  int en2 = S.p[0].en_max;
  (void)fx, (void)fy;
  char b[160];
  snprintf(b, sizeof b, "energy measures %d, after the fall %d (the flame %s), after touching it %d", en0, en1, left ? "left where it fell" : "nowhere", en2);
  check("echo", en1 == en0 - 1 && left && en2 == en0 && !S.echo, b);
}

int main(int argc, char **argv) {
  static const char *const NAMES[] = { "hitstop", "knockback", "recoil", "puff", "slain", "trial", "phase", "charms", "echo" };
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--control") && i + 1 < argc)
      for (int k = 0; k < 9; k++)
        if (!strcmp(argv[i + 1], NAMES[k])) g_rules_off |= 1 << k;
  build();
  blow();
  puff();
  slain();
  trial();
  phase();
  charms();
  echo();
  printf("rules %s\n", bad ? "FAIL" : "ok");
  return bad ? 1 : 0;
}
