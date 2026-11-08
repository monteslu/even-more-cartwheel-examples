/* pinball's rules: one to four players taking turns, three balls each, on one table.
 *
 *   scoring    pops 1,000, slings 110, standups 5,000, drop targets 3,000 (a bank 25,000), rollovers 1,000 to 5,000,
 *              spinner 200 a turn, ramps 20,000, orbits 15,000, holes 10,000; a combo (a major shot within 4 s of the
 *              last) doubles, then trebles.
 *   lanes      the top lanes light as the ball rolls through them; all of them raise the bonus multiplier (to 5x).
 *              The flippers move the lit lanes along. The lane lit as a ball is plunged is the skill shot.
 *   drops      a bank down lights the mode at the scoop (or relights the kickback the second time).
 *   modes      each table's own four (tables.c), started at its mode hole, each played its own way (game.h MK_*): lit
 *              shots, a roving shot, a hurry-up that counts down, a sweep of every target, the table's toy, some with
 *              a second ball. A timer, a goal, each hit worth more than the last, the goal a big award. All four won
 *              lights the extra ball.
 *   wizard     all four modes played (won or not) light the table's wizard mode at the mode hole: four balls, a long
 *              ball save, every shot and the toy lit, each hit worth more than the last (more for each mode won).
 *              When it ends the modes start over.
 *   toys       Coral Deep's creature rises out of the playfield for its toy mode and the wizard mode (a target that
 *              sinks when hit and rises again); Star Foundry's magnet under the forge ring pulls a ball in, holds it
 *              and throws it; Neon Fair's high striker sends its puck up by how hard a ramp was hit, and a hard one
 *              rings the bell.
 *   multiball  ramps (or the lock hole) light the lock; locks are virtual (the ball comes back), the last one starts a
 *              three-ball multiball with a ball save, the jackpot on one shot, doubling each time.
 *   tilt       a nudge shoves the ball; too many in a row warn twice, then tilt: the flippers die and the bonus goes.
 * Deterministic: the same inputs give the same game on every host. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

#define SEC(s) ((int)((s) * 60))
#define BALL_SAVE SEC(10)
#define MB_SAVE SEC(15)
#define WIZ_SAVE SEC(20)
#define LOCKS_NEEDED 2
#define ROVE_TICKS SEC(3.5f)
#define HURRY_FALL SEC(18)      /* a hurry-up falls from 5x to 1x its value over this */
#define TOY_R 0.016f            /* the creature's body (a post the ball hits) */
#define MAGNET_GRAB 0.026f      /* how near the magnet's middle a ball is caught */
#define BELL_AT 0.7f            /* the striker's strength that rings the bell */

static void objective(game *g, int i, bool met);
static void fx(game *g, int kind, int a, float v) {
  if (g->nfx >= 64) return;
  g->fx[g->nfx].kind = (uint8_t)kind, g->fx[g->nfx].a = (int16_t)a, g->fx[g->nfx].v = v;
  g->nfx++;
}

static void say(game *g, int kind, const char *a, const char *b, int64_t value, int ticks) {
  g->msg = kind, g->msg_t = 0, g->msg_len = ticks, g->msg_value = value;
  snprintf(g->msg_a, sizeof g->msg_a, "%s", a ? a : "");
  snprintf(g->msg_b, sizeof g->msg_b, "%s", b ? b : "");
}

static player_state *cur(game *g) { return &g->P[g->cur]; }

static void score(game *g, int64_t v) {
  if (g->tilted) return;
  cur(g)->score += v;
}

static int popcount4(int m) { return (m & 1) + (m >> 1 & 1) + (m >> 2 & 1) + (m >> 3 & 1); }

const char *fmt_score(int64_t v, char *buf, int cap) {
  char raw[32];
  snprintf(raw, sizeof raw, "%lld", (long long)v);
  int n = (int)strlen(raw), o = 0;
  for (int i = 0; i < n && o < cap - 1; i++) {
    if (i && (n - i) % 3 == 0 && o < cap - 1) buf[o++] = ',';
    if (o < cap - 1) buf[o++] = raw[i];
  }
  buf[o] = 0;
  return buf;
}

/* ── balls ───────────────────────────────────────────────────────────── */

static void serve(game *g) { /* a ball onto the plunger */
  pb_ball_add(&g->W, g->T.plunge_x, g->T.plunge_y + 0.002f, 0, 0);
  g->balls_in_play++;
  g->plunge = 0;
}

static int ball_on_plunger(const game *g) {
  for (int i = 0; i < PB_MAXB; i++) {
    const pb_ball *b = &g->W.b[i];
    if (b->state == BS_FREE && b->x > 0.219f && b->y < 0.075f && fabsf(b->vy) < 0.4f) return i;
  }
  return -1;
}

/* ── lamps ───────────────────────────────────────────────────────────── */

static void lamp_set(game *g, int l, int st) {
  if (l >= 0 && l < g->T.nlamp) g->T.lamps[l].state = (int8_t)st;
}

static const mode_def *mode_now(const game *g) { return g->mode >= 0 ? &g->T.modes[g->mode] : NULL; }

static bool shot_lit_for_mode(const game *g, int s) {
  const mode_def *m = mode_now(g);
  if (!m || !(m->shots_mask >> s & 1)) return false;
  switch (m->kind) {
  case MK_ROVE: return s == g->rove_shot;
  case MK_ALL: return !(g->T.shots[s].made & 0x100);
  case MK_SWEEP: return false;
  default: return true;
  }
}

bool game_shot_lit(const game *g, int s) { return (g->wizard && s >= 0 && s < g->T.nshot) || shot_lit_for_mode(g, s); }

static bool sweep_target(const elem *e) { return e->kind == EK_TARGET || e->kind == EK_DROP; }

static void lamps_update(game *g) {
  table *T = &g->T;
  player_state *p = cur(g);
  const mode_def *m = mode_now(g);
  for (int i = 0; i < T->nlamp; i++) T->lamps[i].state = 0;
  for (int i = 0; i < T->lane_n; i++) {
    int l = T->el[T->lane_group + i].lamp;
    lamp_set(g, l, (p->lanes >> i & 1) ? 1 : (g->skill_t > 0 && i == g->skill_lane) ? 3 : 0);
  }
  for (int i = 0; i < 4; i++) lamp_set(g, T->bonus_x_lamp[i], p->bonus_x >= i + 2 ? 1 : 0);
  lamp_set(g, T->kickback_lamp, p->kickback_lit ? 1 : 0);
  lamp_set(g, T->shoot_again_lamp, g->practice ? 1 : g->ball_save_t > 0 ? (g->ball_save_t < SEC(3) ? 3 : 2) : p->extra_balls > 0 ? 1 : 0);
  lamp_set(g, T->extra_ball_lamp, p->extra_lit ? 2 : 0);
  for (int i = 0; i < 3; i++) lamp_set(g, T->lock_lamp[i], p->locks > i ? 1 : (p->lock_lit && p->locks == i) ? 2 : 0);
  for (int i = 0; i < T->nmode; i++)
    lamp_set(g, T->mode_lamp[i], g->wizard ? 3 : p->wizard_lit ? 2 : (p->modes_done >> i & 1) ? 1 : g->mode == i ? 3 : (p->mode_next == i && !(p->modes_played >> i & 1)) ? 2 : 0);
  lamp_set(g, T->mb_lamp, g->multiball || g->wizard ? 3 : 0);
  lamp_set(g, T->jackpot_lamp, g->multiball ? 3 : 0);
  if (T->mode_hole >= 0) lamp_set(g, T->el[T->mode_hole].lamp, (p->mode_lit || p->wizard_lit) && g->mode < 0 && !g->wizard ? 3 : g->mode >= 0 || g->wizard ? 1 : 0);
  for (int s = 0; s < T->nshot; s++) {
    int st = 0;
    if (game_shot_lit(g, s)) st = 3;
    else if (g->multiball && s == T->jackpot_shot) st = 3;
    else if (p->lock_lit && T->el[T->lock_hole].shot == s) st = 2;
    else if (g->combo_t > 0 && T->shots[s].made) st = 2;
    lamp_set(g, T->shots[s].arrow, st);
  }
  for (int i = 0; i < T->nel; i++) {
    const elem *e = &T->el[i];
    if (e->lamp < 0) continue;
    if (m && m->kind == MK_SWEEP && sweep_target(e)) lamp_set(g, e->lamp, e->swept ? 1 : 3);
    else if (e->kind == EK_DROP) lamp_set(g, e->lamp, e->down ? 1 : 0);
    else if (e->kind == EK_TARGET) lamp_set(g, e->lamp, (p->target_count >> (i & 15) & 1) ? 1 : 0);
    else if (e->kind == EK_ROLLOVER && (i == T->inlane_l || i == T->inlane_r)) lamp_set(g, e->lamp, g->combo_t > 0 ? 2 : 0);
    else if (e->kind == EK_ROLLOVER && (i == T->outlane_l || i == T->outlane_r)) lamp_set(g, e->lamp, (i == T->outlane_l && p->kickback_lit) ? 1 : 0);
    else if (e->kind == EK_TOY) lamp_set(g, e->lamp, (m && m->kind == MK_TOY) || g->wizard ? 3 : g->toy_up ? 1 : 0);
  }
  if (g->tilted)
    for (int i = 0; i < T->nlamp; i++) T->lamps[i].state = 0;
}

/* a lamp's level as the rules want it drawn. A pulsing lamp is a smooth wave, never a blink: a slow one (state 2) at
 * 0.6 cycles a second, a quick one (state 3: the shot to make now) at 1.1, each lamp a little later than the one
 * below it so a lane of lit arrows swells up the table. PINBALL_OLD_STROBE: the on-off blink it replaced (the flash
 * checks' control). */
float game_flash(const game *g, int l) {
  if (l < 0 || l >= g->T.nlamp) return 0;
  int st = g->T.lamps[l].state;
#ifdef PINBALL_OLD_STROBE
  switch (st) {
  case 1: return 1;
  case 2: return (g->tick / 16) % 2 ? 1.0f : 0.08f;
  case 3: return (g->tick / 6) % 2 ? 1.0f : 0.08f;
  default: return 0;
  }
#else
  if (st == 1) return 1;
  if (st != 2 && st != 3) return 0;
  float hz = st == 3 ? 1.1f : 0.6f, lo = st == 3 ? 0.3f : 0.25f;
  float ph = (float)g->tick / 60.0f * hz - g->T.lamps[l].y * 1.2f;
  float w = 0.5f - 0.5f * cw_cos(ph * 2.0f * CW_PI);
  return lo + (1 - lo) * w;
#endif
}

/* ── modes and the wizard mode ───────────────────────────────────────── */

static void drop_reset(game *g, int bank) {
  table *T = &g->T;
  int first = T->drop_banks[bank][0], n = T->drop_banks[bank][1];
  for (int i = first; i < first + n; i++) {
    T->el[i].down = false;
    for (int k = 0; k < T->el[i].nshape; k++) g->W.sh[T->el[i].shape + k].flags &= (uint8_t)~SF_OFF;
  }
  fx(g, FX_DROP_RESET, bank, 1);
}

static int64_t hurry_top(const mode_def *m) { return (int64_t)m->value * 5; }

static void rove_next(game *g) { /* the roving shot moves on to the next one in its mask */
  const mode_def *m = mode_now(g);
  if (!m) return;
  for (int k = 1; k <= g->T.nshot; k++) {
    int s = (g->rove_shot + k) % g->T.nshot;
    if (m->shots_mask >> s & 1) { g->rove_shot = s; break; }
  }
  g->rove_t = ROVE_TICKS;
}

static void mode_begin(game *g, int m) {
  player_state *p = cur(g);
  const mode_def *md = &g->T.modes[m];
  g->mode = m, g->mode_t = SEC(md->seconds), g->mode_hits = 0;
  for (int s = 0; s < g->T.nshot; s++) g->T.shots[s].made &= 0xff;
  for (int i = 0; i < g->T.nel; i++) g->T.el[i].swept = false;
  g->hurry = hurry_top(md);
  g->rove_shot = -1;
  if (md->kind == MK_ROVE) rove_next(g);
  if (md->add_balls > 0) g->serve_left += md->add_balls, g->serve_t = SEC(0.6f), g->ball_save_t = fmaxf(g->ball_save_t, SEC(12));
  p->mode_lit = false;
  g->modes_started++;
  say(g, MSG_MODE, md->name, md->goal, md->value, SEC(3));
  fx(g, FX_MODE_START, m, 1);
}

static void mode_start(game *g) { mode_begin(g, cur(g)->mode_next % g->T.nmode); g->ball_modes++; }

static void wizard_light_check(game *g) {
  player_state *p = cur(g);
  if ((p->modes_played & 0xf) == 0xf && !p->wizard_lit && !g->wizard) {
    p->wizard_lit = true;
    say(g, MSG_TEXT, g->T.wizard_name, "IS LIT AT THE SCOOP", 0, SEC(2.5f));
  }
}

static void mode_end(game *g, bool won) {
  if (g->mode < 0) return;
  player_state *p = cur(g);
  int m = g->mode;
  p->modes_played |= 1 << m;
  if (won) {
    p->modes_done |= 1 << m;
    g->modes_won++;
    int64_t award = (int64_t)g->T.modes[m].value * 3;
    score(g, award);
    say(g, MSG_BIG, g->T.modes[m].name, "COMPLETE", award, SEC(3));
    fx(g, FX_MODE_DONE, m, 1);
    if ((p->modes_done & 0xf) == 0xf) p->extra_lit = true;
  } else {
    say(g, MSG_TEXT, g->T.modes[m].name, "TIME IS UP", 0, SEC(2));
    fx(g, FX_MODE_FAIL, m, 1);
  }
  for (int k = 1; k <= g->T.nmode; k++) /* the next one not yet played */
    if (!(p->modes_played >> ((m + k) % g->T.nmode) & 1)) { p->mode_next = (m + k) % g->T.nmode; break; }
  g->mode = -1;
  g->rove_shot = -1;
  wizard_light_check(g);
}

/* a hit that counts for the running mode (a lit shot, a target in a sweep, the toy) */
static void mode_hit(game *g, int64_t v) {
  const mode_def *m = mode_now(g);
  if (!m) return;
  g->mode_hits++;
  score(g, v);
  fx(g, FX_MODE_SHOT, g->mode_hits, 1);
  char b[32];
  snprintf(b, sizeof b, "%d TO GO", m->need - g->mode_hits);
  if (g->mode_hits >= m->need) mode_end(g, true);
  else say(g, MSG_BIG, m->name, b, v, SEC(1.5f));
}

static void wizard_start(game *g) {
  player_state *p = cur(g);
  p->wizard_lit = false, p->mode_lit = false;
  g->wizard = true, g->wizard_hits = 0, g->wizard_total = 0;
  g->wizard_value = 1000000 + 500000 * (int64_t)popcount4(p->modes_done);
  g->serve_left += 3, g->serve_t = SEC(0.8f);
  g->ball_save_t = WIZ_SAVE;
  g->tilt_meter = 0;
  g->wizards_total++;
  say(g, MSG_WIZARD, g->T.wizard_name, g->T.wizard_goal, 0, SEC(3.5f));
  fx(g, FX_WIZARD, 0, 1);
}

static void wizard_hit(game *g) {
  score(g, g->wizard_value);
  g->wizard_total += g->wizard_value;
  g->wizard_hits++;
  say(g, MSG_JACKPOT, g->T.wizard_name, "", g->wizard_value, SEC(1.6f));
  fx(g, FX_WIZARD_SHOT, g->wizard_hits, 1);
  g->wizard_value += 250000;
}

static void wizard_end(game *g) {
  if (!g->wizard) return;
  player_state *p = cur(g);
  g->wizard = false;
  g->serve_left = 0;
  p->wizards++;
  p->modes_played = 0, p->modes_done = 0; /* the modes start over */
  p->mode_next = 0;
  say(g, MSG_BIG, g->T.wizard_name, "TOTAL", g->wizard_total, SEC(3));
  fx(g, FX_WIZARD_END, 0, 1);
}

static void multiball_start(game *g) {
  player_state *p = cur(g);
  p->locks = 0, p->lock_lit = false;
  p->mb_count++;
  g->multiball = true, g->mb_t = 0;
  g->jackpot_value = 500000 + 250000 * (p->mb_count - 1);
  g->mbs++;
  g->ball_save_t = MB_SAVE;
  g->tilt_meter = 0;
  say(g, MSG_MULTIBALL, "MULTIBALL", "SHOOT FOR THE JACKPOT", 0, SEC(3));
  fx(g, FX_MULTIBALL, 0, 1);
  /* the two more balls come up the shooter lane by themselves (game_step, by mb_t) */
}

static void lock_ball(game *g) {
  player_state *p = cur(g);
  if (!p->lock_lit || g->multiball || g->wizard) return;
  p->locks++;
  p->lock_lit = false;
  fx(g, FX_LOCK, p->locks, 1);
  if (p->locks >= LOCKS_NEEDED) multiball_start(g);
  else {
    char b[24];
    snprintf(b, sizeof b, "BALL %d LOCKED", p->locks);
    say(g, MSG_LOCK, b, "", 0, SEC(2));
  }
}

static void shot_made(game *g, int s) {
  table *T = &g->T;
  player_state *p = cur(g);
  if (s < 0 || s >= T->nshot) return;
  T->shots[s].made++;
  int mult = 1;
  if (g->combo_t > 0) {
    g->combo_n++;
    mult = g->combo_n >= 2 ? 3 : 2;
    if (g->combo_n >= 2) say(g, MSG_TEXT, "SUPER COMBO", "", 0, SEC(1.2f));
    else say(g, MSG_TEXT, "COMBO", "", 0, SEC(1.2f));
  } else g->combo_n = 0;
  g->combo_t = SEC(4);
  score(g, 15000 * mult);
  p->bonus += 3000;
  if (g->wizard) wizard_hit(g);
  else if (shot_lit_for_mode(g, s) && !(mode_now(g)->kind == MK_TOY && T->toy == TOY_STRIKER)) { /* the striker's ramps
                                                                                                    * count by its bell */
    const mode_def *m = mode_now(g);
    T->shots[s].made |= 0x100;
    int64_t v = m->kind == MK_HURRY ? g->hurry : (int64_t)m->value * (g->mode_hits + 1);
    if (m->kind == MK_HURRY) g->hurry = hurry_top(m) + (int64_t)m->value * (g->mode_hits + 1);
    if (m->kind == MK_ROVE) rove_next(g);
    mode_hit(g, v);
  }
  if (g->multiball && s == T->jackpot_shot) {
    score(g, g->jackpot_value);
    g->jackpots++, g->jackpots_total++;
    say(g, MSG_JACKPOT, "JACKPOT", "", g->jackpot_value, SEC(2.5f));
    fx(g, FX_JACKPOT, 0, 1);
    g->jackpot_value *= 2;
    objective(g, 2, g->mb_t < SEC(30));
  }
  /* the ramps light the lock (three of them, then one per lock) */
  if (!g->multiball && !g->wizard && !p->lock_lit && T->el[T->lock_hole].shot != s) {
    p->ramp_count++;
    if (p->ramp_count >= 3 + p->locks) p->lock_lit = true, p->ramp_count = 0, say(g, MSG_TEXT, "LOCK IS LIT", "", 0, SEC(1.5f));
  } else if (!g->multiball && p->lock_lit && T->el[T->lock_hole].shot == s) lock_ball(g);
  if (p->extra_lit && s == T->el[T->mode_hole].shot && g->mode < 0) {
    p->extra_lit = false, p->extra_balls++;
    say(g, MSG_EXTRA, "EXTRA BALL", "", 0, SEC(2.5f));
    fx(g, FX_EXTRA, 0, 1);
  }
}

static void lanes_hit(game *g, int lane) {
  player_state *p = cur(g);
  table *T = &g->T;
  if (g->skill_t > 0 && lane == g->skill_lane) {
    score(g, 250000);
    say(g, MSG_SKILL, "SKILL SHOT", "", 250000, SEC(2));
    fx(g, FX_SKILL, 0, 1);
    g->skill_t = 0;
  }
  p->lanes |= 1 << lane;
  score(g, 3000);
  p->bonus += 1000;
  if (p->lanes == (1 << T->lane_n) - 1) {
    p->lanes = 0;
    p->lane_sets++;
    if (p->bonus_x < 5) p->bonus_x++;
    char b[24];
    snprintf(b, sizeof b, "BONUS %dX", p->bonus_x);
    say(g, MSG_TEXT, b, "", 0, SEC(1.5f));
    score(g, 25000);
    fx(g, FX_LANES, p->bonus_x, 1);
  }
}

static void kick_out(game *g, pb_ball *b, const elem *e) {
  /* a real kicker never fires twice the same: a few degrees and a few percent either way */
  uint32_t h = (uint32_t)g->tick * 2654435761u;
  float jit = (float)((h >> 8) & 255) / 255.0f - 0.5f, jit2 = (float)((h >> 16) & 255) / 255.0f - 0.5f;
  float a = (e->ang + jit * 8.0f) * CW_PI / 180.0f, v = e->w * (1 + jit2 * 0.1f);
  b->state = BS_FREE;
  b->x = e->x + cw_cos(a) * 0.004f, b->y = e->y + cw_sin(a) * 0.004f;
  b->vx = cw_cos(a) * v, b->vy = cw_sin(a) * v;
  b->held_tag = -1;
  fx(g, FX_KICKOUT, (int)(e - g->T.el), 1);
}

/* ── the toys ────────────────────────────────────────────────────────── */

static bool toy_mode(const game *g) { const mode_def *m = mode_now(g); return m && m->kind == MK_TOY; }

static void toy_hit(game *g, int el) {
  g->toy_hits++;
  score(g, 25000);
  fx(g, FX_TOY_HIT, el, 1);
  if (g->wizard) wizard_hit(g);
  else if (toy_mode(g)) mode_hit(g, (int64_t)g->T.modes[g->mode].value * (g->mode_hits + 1));
}

/* the creature: up while its mode or the wizard mode runs; a hit sinks it, and it rises again a moment later, never
 * under a ball */
static void creature_tick(game *g) {
  table *T = &g->T;
  elem *e = &T->el[T->toy_el];
  bool want = toy_mode(g) || g->wizard;
  if (g->toy_rest > 0) g->toy_rest--;
  if (g->toy_flinch > 0) g->toy_flinch--;
  if (want && !g->toy_up && g->toy_rest == 0) {
    bool clear = true;
    for (int i = 0; i < PB_MAXB && clear; i++) {
      const pb_ball *b = &g->W.b[i];
      if (b->state != BS_FREE) continue;
      float dx = b->x - e->x, dy = b->y - e->y;
      if (dx * dx + dy * dy < (TOY_R + PB_R + 0.01f) * (TOY_R + PB_R + 0.01f)) clear = false;
    }
    if (clear) {
      g->toy_up = true;
      for (int k = 0; k < e->nshape; k++) g->W.sh[e->shape + k].flags &= (uint8_t)~SF_OFF;
      fx(g, FX_TOY, 1, 1);
      g->toy_events++;
    }
  } else if (!want && g->toy_up) {
    g->toy_up = false;
    for (int k = 0; k < e->nshape; k++) g->W.sh[e->shape + k].flags |= SF_OFF;
    fx(g, FX_TOY, 0, 1);
  }
  g->toy_h += ((g->toy_up ? 1.0f : 0.0f) - g->toy_h) * 0.08f;
}

static void creature_hit(game *g, int el) {
  if (!g->toy_up) return;
  elem *e = &g->T.el[el];
  g->toy_up = false;
  for (int k = 0; k < e->nshape; k++) g->W.sh[e->shape + k].flags |= SF_OFF;
  g->toy_rest = SEC(1.6f), g->toy_flinch = SEC(0.5f);
  toy_hit(g, el);
}

/* the magnet: while its mode or the wizard mode runs its field is on (the solver pulls a ball near it in); a slow
 * enough ball over its middle is caught, held in the forge ring a moment and thrown back up the table */
static void magnet_tick(game *g) {
  table *T = &g->T;
  elem *e = &T->el[T->toy_el];
  bool armed = toy_mode(g) || g->wizard;
  for (int k = 0; k < e->nshape; k++) {
    if (armed && g->magnet_ball < 0) g->W.sh[e->shape + k].flags &= (uint8_t)~SF_OFF;
    else g->W.sh[e->shape + k].flags |= SF_OFF;
  }
  /* the ring turns slowly (its teeth pass a point under once a second at most: nothing flickers) */
  g->magnet_spin_v += ((armed ? 0.5f : 0.15f) + (g->magnet_ball >= 0 ? 0.5f : 0) - g->magnet_spin_v) * 0.03f;
  g->magnet_spin += g->magnet_spin_v * TICK;
  if (g->magnet_cool > 0) g->magnet_cool--;
  if (g->magnet_ball >= 0) {
    pb_ball *b = &g->W.b[g->magnet_ball];
    if (b->state != BS_HELD || b->held_tag != T->toy_el) { g->magnet_ball = -1; return; }
    if (--g->magnet_t <= 0) { /* thrown: up the table, a little either way */
      static const float ANG[3] = { 72, 95, 116 };
      uint32_t h = (uint32_t)g->tick * 2654435761u;
      float a = (ANG[(h >> 9) % 3u] + ((float)((h >> 17) & 255) / 255.0f - 0.5f) * 10.0f) * CW_PI / 180.0f;
      b->state = BS_FREE, b->held_tag = -1;
      b->x = e->x + cw_cos(a) * 0.003f, b->y = e->y + cw_sin(a) * 0.003f;
      b->vx = cw_cos(a) * 1.9f, b->vy = cw_sin(a) * 1.9f;
      g->magnet_ball = -1, g->magnet_cool = SEC(1.5f);
      fx(g, FX_KICKOUT, T->toy_el, 1);
    }
    return;
  }
  if (!armed || g->magnet_cool > 0) return;
  for (int i = 0; i < PB_MAXB; i++) {
    pb_ball *b = &g->W.b[i];
    if (b->state != BS_FREE) continue;
    float dx = b->x - e->x, dy = b->y - e->y;
    if (dx * dx + dy * dy > MAGNET_GRAB * MAGNET_GRAB || b->vx * b->vx + b->vy * b->vy > 3.2f * 3.2f) continue;
    b->state = BS_HELD, b->held_tag = (int16_t)T->toy_el, b->held_ticks = 0;
    b->x = e->x, b->y = e->y, b->vx = b->vy = 0;
    g->magnet_ball = i, g->magnet_t = SEC(1.0f);
    g->magnet_grabs++, g->toy_events++;
    fx(g, FX_TOY, 2, 1);
    toy_hit(g, T->toy_el);
    break;
  }
}

/* the high striker: a ramp hit hard sends the puck up its pole; strong enough, it rings the bell */
static void striker_ramp(game *g, float speed) {
  float strength = cw_clampf((speed - 1.1f) / 1.5f, 0.12f, 1.0f);
  g->puck_v = sqrtf(2 * 9.0f * strength) * 1.02f; /* a throw under 9 units/s^2 tops out at `strength` */
  g->toy_events++;
  if (strength >= BELL_AT) {
    g->bells++;
    g->bell_t = 0;
    fx(g, FX_TOY, 3, strength);
    if (toy_mode(g) || g->wizard) toy_hit(g, g->T.toy_el);
    else score(g, 10000);
  }
}

static void striker_tick(game *g) {
  if (g->bell_t < 10000) g->bell_t++;
  if (g->puck_v != 0 || g->puck_h > 0) {
    g->puck_v -= 9.0f * TICK;
    g->puck_h += g->puck_v * TICK;
    if (g->puck_h >= 1) g->puck_h = 1, g->puck_v = -fabsf(g->puck_v) * 0.3f;
    if (g->puck_h <= 0) g->puck_h = 0, g->puck_v = 0;
  }
}

/* ── goals ───────────────────────────────────────────────────────────── */

const char *game_objective(const table *T, int i) {
  static const char *const COMMON[NOBJ] = { "SCORE 1,000,000 IN A GAME", "START TWO MODES ON ONE BALL", "A JACKPOT IN MULTIBALL'S FIRST 30 SECONDS", "", "PLAY THE WIZARD MODE" };
  if (i != 3) return COMMON[i];
  return T->toy == TOY_CREATURE ? "HIT THE LEVIATHAN 3 TIMES IN A GAME" : T->toy == TOY_MAGNET ? "3 MAGNET CATCHES IN A GAME"
       : T->toy == TOY_STRIKER ? "RING THE BELL 5 TIMES IN A GAME" : "MAKE 10 RAMPS IN A GAME";
}

static void objective(game *g, int i, bool met) {
  if (!met || (g->objectives >> i & 1) || g->practice || g->tilted) return;
  g->objectives |= 1 << i;
  say(g, MSG_BIG, "GOAL MET", game_objective(&g->T, i), 0, SEC(2.5f));
  fx(g, FX_OBJECTIVE, i, 1);
}

static void objectives_tick(game *g) {
  const table *T = &g->T;
  objective(g, 0, cur(g)->score >= 1000000);
  objective(g, 1, g->ball_modes >= 2);
  objective(g, 3, T->toy == TOY_CREATURE ? g->toy_hits >= 3 : T->toy == TOY_MAGNET ? g->magnet_grabs >= 3 : T->toy == TOY_STRIKER ? g->bells >= 5 : g->ramps_made >= 10);
  objective(g, 4, g->wizard);
}

/* ── switch events ───────────────────────────────────────────────────── */

static void events(game *g) {
  table *T = &g->T;
  player_state *p = cur(g);
  for (int k = 0; k < g->W.nev; k++) {
    const pb_event *ev = &g->W.ev[k];
    pb_ball *b = &g->W.b[ev->ball];
    elem *e = ev->tag >= 0 && ev->tag < T->nel ? &T->el[ev->tag] : NULL;
    const mode_def *md = mode_now(g);
    switch (ev->kind) {
    case EV_FLIP: fx(g, FX_WALL, -2, ev->speed); break;
    case EV_BALLS: fx(g, FX_BALLS, 0, ev->speed); break;
    case EV_KICK:
      if (!e) break;
      e->flash = 1, g->hits++;
      if (e->kind == EK_POP) {
        score(g, 1000), p->pop_count++, p->bonus += 200;
        fx(g, FX_POP, ev->tag, ev->speed);
      } else {
        score(g, 110);
        fx(g, FX_SLING, ev->tag, ev->speed);
      }
      break;
    case EV_HIT:
      if (!e) { fx(g, FX_WALL, -1, ev->speed); break; }
      e->flash = 1, g->hits++;
      if (md && md->kind == MK_SWEEP && sweep_target(e) && !e->swept) { /* a target sweep: each target once */
        e->swept = true;
        mode_hit(g, (int64_t)md->value * (g->mode_hits + 1));
        md = mode_now(g);
      }
      if (e->kind == EK_TOY) creature_hit(g, ev->tag);
      else if (e->kind == EK_TARGET) {
        score(g, 5000), p->bonus += 1000;
        p->target_count |= 1 << (ev->tag & 15);
        fx(g, FX_TARGET, ev->tag, ev->speed);
        if (g->mode >= 0) g->mode_t += SEC(2); /* a standup in a mode buys time */
      } else if (e->kind == EK_DROP && !e->down) {
        e->down = true;
        for (int s = 0; s < e->nshape; s++) g->W.sh[e->shape + s].flags |= SF_OFF;
        score(g, 3000), p->bonus += 1000;
        fx(g, FX_DROP, ev->tag, ev->speed);
        int bank = e->group, first = T->drop_banks[bank][0], n = T->drop_banks[bank][1], down = 0;
        for (int i = first; i < first + n; i++) down += T->el[i].down;
        if (down == n) {
          p->drop_sets++;
          score(g, 25000);
          if (!p->mode_lit && g->mode < 0 && !p->wizard_lit && !g->wizard) p->mode_lit = true, say(g, MSG_TEXT, "MODE IS LIT", "SHOOT THE SCOOP", 0, SEC(2));
          else if (!p->kickback_lit) p->kickback_lit = true, say(g, MSG_TEXT, "KICKBACK LIT", "", 0, SEC(1.5f));
          else say(g, MSG_BIG, "DROP BANK", "", 25000, SEC(1.5f));
          g->T.el[first].flash = 2; /* the reset follows in drops_tick */
        }
      } else fx(g, FX_WALL, ev->tag, ev->speed);
      break;
    case EV_CROSS:
      if (!e) break;
      e->flash = 1;
      if (e->kind == EK_ROLLOVER) {
        fx(g, FX_ROLLOVER, ev->tag, ev->speed);
        if (e->group >= 100) lanes_hit(g, e->group - 100);
        else if (ev->tag == T->outlane_l || ev->tag == T->outlane_r) score(g, 5000), p->bonus += 2000;
        else score(g, 1000), p->bonus += 500;
      } else if (e->kind == EK_SPINNER) {
        int turns = (int)(ev->speed * 9.0f) + 1;
        e->spin_v += ev->speed * 60.0f;
        p->spin_count += turns;
        score(g, 200 * turns);
        fx(g, FX_SPIN, ev->tag, ev->speed);
      } else if (e->kind == EK_LOOP) {
        if (e->dir == 0 || e->dir == ev->dir) {
          p->loop_count++;
          score(g, 5000);
          if (e->shot >= 0) shot_made(g, e->shot);
        }
      } else if (e->kind == EK_KICKBACK) {
        if (p->kickback_lit && !g->tilted && b->vy < 0) {
          b->vx = 0.05f, b->vy = 3.4f;
          p->kickback_lit = false;
          fx(g, FX_KICKBACK, ev->tag, 1);
          say(g, MSG_TEXT, "KICKBACK", "", 0, SEC(1));
        }
      }
      break;
    case EV_RAMP_IN:
      fx(g, FX_RAMP_IN, ev->tag, ev->speed);
      if (T->toy == TOY_STRIKER && !g->tilted) striker_ramp(g, ev->speed);
      break;
    case EV_RAMP_BACK: fx(g, FX_WALL, ev->tag, 0.5f); break;
    case EV_RAMP_DONE:
      if (!e) break;
      e->flash = 1;
      g->ramps_made++;
      score(g, 20000), p->bonus += 4000;
      fx(g, FX_RAMP_DONE, ev->tag, ev->speed);
      if (e->shot >= 0) shot_made(g, e->shot);
      break;
    case EV_HOLE:
      if (!e) break;
      e->flash = 1;
      score(g, 10000);
      fx(g, FX_HOLE, ev->tag, ev->speed);
      b->held_ticks = 0;
      if (e->shot >= 0) shot_made(g, e->shot);
      if (ev->tag == T->mode_hole && g->mode < 0 && !g->multiball && !g->wizard) {
        if (p->wizard_lit) wizard_start(g);
        else if (p->mode_lit) mode_start(g);
      }
      break;
    case EV_DRAIN:
      g->balls_in_play--;
      fx(g, FX_DRAIN, 0, 1);
      if ((g->ball_save_t > 0 || g->practice) && !g->tilted) {
        g->saves++;
        say(g, MSG_BALLSAVE, "BALL SAVED", "", 0, SEC(2));
        fx(g, FX_SAVE, 0, 1);
        serve(g);
        int i = ball_on_plunger(g);
        if (i >= 0) g->W.b[i].vy = 4.2f + 0.3f * (float)(g->tick % 3);
        if (!g->multiball && !g->wizard) g->ball_save_t = 0;
      } else if (g->multiball && g->balls_in_play <= 1) {
        g->multiball = false;
        say(g, MSG_TEXT, "MULTIBALL OVER", "", 0, SEC(1.5f));
      } else if (g->wizard && g->balls_in_play <= 1 && g->serve_left == 0) wizard_end(g);
      break;
    default: break;
    }
  }
}

/* the drop banks reset a second after the last one falls, the holes kick their balls out */
static void timers(game *g) {
  table *T = &g->T;
  for (int bank = 0; bank < T->ndrop_banks; bank++) {
    int first = T->drop_banks[bank][0], n = T->drop_banks[bank][1], down = 0;
    for (int i = first; i < first + n; i++) down += T->el[i].down;
    int *wait = g->drop_wait;
    if (down == n) {
      if (++wait[bank] > SEC(1.2f)) {
        bool clear = true; /* never raise a target under a ball */
        for (int i = first; i < first + n && clear; i++)
          for (int bi = 0; bi < PB_MAXB; bi++) {
            const pb_ball *b = &g->W.b[bi];
            if (b->state != BS_FREE) continue;
            float nx, ny;
            if (pb_dist_shape(&g->W.sh[T->el[i].shape], b->x, b->y, &nx, &ny) < PB_R + 0.003f) clear = false;
          }
        if (clear) drop_reset(g, bank), wait[bank] = 0;
      }
    } else wait[bank] = 0;
  }
  for (int i = 0; i < PB_MAXB; i++) {
    pb_ball *b = &g->W.b[i];
    if (b->state != BS_HELD || b->held_tag < 0 || b->held_tag >= T->nel || T->el[b->held_tag].kind == EK_TOY) continue;
    int hold = g->mode >= 0 && g->mode_t > g->T.modes[g->mode].seconds * 60 - SEC(3) ? SEC(2.6f) : SEC(1.1f);
    if (g->wizard && g->ball_save_t > WIZ_SAVE - SEC(4)) hold = SEC(2.6f);
    if (b->held_ticks >= hold) kick_out(g, b, &T->el[b->held_tag]);
  }
  /* a running mode's clock: a roving shot moves on, a hurry-up falls */
  const mode_def *m = mode_now(g);
  if (m && m->kind == MK_ROVE && --g->rove_t <= 0) rove_next(g);
  if (m && m->kind == MK_HURRY) {
    int64_t fall = (hurry_top(m) - m->value) / HURRY_FALL;
    g->hurry = g->hurry - fall > m->value ? g->hurry - fall : m->value;
  }
  if (T->toy == TOY_CREATURE) creature_tick(g);
  else if (T->toy == TOY_MAGNET) magnet_tick(g);
  else if (T->toy == TOY_STRIKER) striker_tick(g);
}

static void next_ball(game *g) {
  player_state *p = cur(g);
  if (p->extra_balls > 0) {
    p->extra_balls--;
    say(g, MSG_TEXT, "SHOOT AGAIN", "", 0, SEC(2));
  } else {
    p->ball++;
    int tries = 0;
    do { g->cur = (g->cur + 1) % g->players; } while (g->P[g->cur].ball > BALLS_PER_GAME && ++tries < g->players);
    if (g->P[g->cur].ball > BALLS_PER_GAME) {
      g->state = GS_OVER, g->state_t = 0;
      say(g, MSG_TEXT, "GAME OVER", "", 0, SEC(4));
      fx(g, FX_GAME_OVER, 0, 1);
      return;
    }
    if (g->players > 1) {
      char b[24];
      snprintf(b, sizeof b, "PLAYER %d", g->cur + 1);
      char c[24];
      snprintf(c, sizeof c, "BALL %d", g->P[g->cur].ball);
      say(g, MSG_TEXT, b, c, 0, SEC(2));
    }
  }
  /* a fresh ball: the lit things that last a ball reset */
  p = cur(g);
  p->bonus = 0, p->bonus_x = 1;
  g->tilted = false, g->tilt_warn = 0, g->tilt_meter = 0;
  g->mode = -1, g->multiball = false, g->combo_t = 0, g->wizard = false, g->serve_left = 0, g->rove_shot = -1, g->ball_modes = 0;
  for (int f = 0; f < g->W.nfl; f++) g->W.fl[f].on = true;
  for (int bank = 0; bank < g->T.ndrop_banks; bank++) drop_reset(g, bank);
  g->state = GS_PLUNGE, g->state_t = 0;
  g->skill_lane = (int)(g->tick % (uint32_t)(g->T.lane_n > 0 ? g->T.lane_n : 1));
  g->skill_t = 1;
  serve(g);
}

void game_new(game *g, int table_id, int players, uint32_t seed) {
  memset(g, 0, sizeof *g);
  table_build(&g->T, table_id);
  g->table_id = table_id;
  g->W = g->T.W;
  g->players = players < 1 ? 1 : players > MAXP ? MAXP : players;
  for (int i = 0; i < g->players; i++) g->P[i].ball = 1, g->P[i].bonus_x = 1, g->P[i].mode_next = (int)((seed + (uint32_t)i) % 2u);
  g->mode = -1;
  g->rove_shot = -1, g->magnet_ball = -1, g->bell_t = 10000;
  g->tick = (int)(seed % 977u);
  g->state = GS_PLUNGE;
  g->skill_lane = (int)(seed % (uint32_t)(g->T.lane_n > 0 ? g->T.lane_n : 1));
  g->skill_t = 1;
  g->hash = 2166136261u;
  g->launch_was = true; /* the button that started the game may still be down */
  serve(g);
  if (g->players > 1) say(g, MSG_TEXT, "PLAYER 1", "BALL 1", 0, SEC(2));
  else say(g, MSG_TEXT, g->T.name, "PLUNGE TO START", 0, SEC(3));
  fx(g, FX_START, 0, 1);
}

bool game_over(const game *g) { return g->state == GS_OVER && g->state_t > SEC(3); }

static void hash_u32(game *g, uint32_t v) { g->hash = (g->hash ^ v) * 16777619u; }

/* when the flipper buttons work. As on a real machine: from the plunge until the ball has gone down the drain and
 * through the bonus count after it, in ball save and multiball alike, a ball past the flippers included (it may come
 * back: a kickback, a bounce, a nudge). Only a tilt kills them. PINBALL_OLD_FLIPPER_RULE builds the rule this game
 * had before (dead from the moment a ball crossed the drain line, which is just under the flippers): the control
 * tests/pinball-drain.mjs runs, which must fail. */
static bool flippers_live(const game *g) {
#ifdef PINBALL_OLD_FLIPPER_RULE
  return !g->tilted && g->state != GS_BONUS;
#else
  return !g->tilted;
#endif
}

void game_step(game *g, const input *in) {
  table *T = &g->T;
  player_state *p = cur(g);
  g->nfx = 0;
  g->tick++, g->state_t++;
  if (g->msg && ++g->msg_t > g->msg_len) g->msg = MSG_NONE;
  if (g->state == GS_OVER) { /* the game is over: the flippers fall back (nothing else moves) */
    for (int f = 0; f < g->W.nfl; f++) g->W.fl[f].held = false;
    pb_step(&g->W, TICK);
    return;
  }
  /* the flippers (dead only when tilted); a flip moves the lit top lanes along. The launch button flips both once no
   * ball waits on the plunger, from a fresh press (the one that pulled the plunger never flips) */
  int pb = ball_on_plunger(g);
  if (!in->both) g->both_ok = true;
  else if (pb >= 0 || g->pull_armed) g->both_ok = false;
  bool both = in->both && g->both_ok;
  bool want[3] = { in->left || both, in->right || both, T->upper_flipper >= 0 ? (in->upper || in->right || both) : false };
  int fi[3] = { T->left_flipper, T->right_flipper, T->upper_flipper };
  for (int k = 0; k < 3; k++) {
    if (fi[k] < 0) continue;
    pb_flipper *F = &g->W.fl[fi[k]];
    bool was = F->held;
    F->held = want[k] && flippers_live(g);
    if (F->held && !was) {
      g->flips++;
      fx(g, FX_FLIP, k, 1);
      if (k < 2 && T->lane_n > 1 && g->state == GS_PLAY) { /* lane change */
        int n = T->lane_n, l = p->lanes;
        p->lanes = k == 0 ? ((l >> 1) | ((l & 1) << (n - 1))) : (((l << 1) | (l >> (n - 1))) & ((1 << n) - 1));
      }
    } else if (!F->held && was) fx(g, FX_FLIP_DOWN, k, 1);
  }
  /* the cabinet: a nudge shoves every ball on the table; nudging too hard warns, then tilts */
  if (g->nudge_t > 0) g->nudge_t--;
  g->tilt_meter = fmaxf(0, g->tilt_meter - 0.35f * TICK);
  if ((in->nudge_l || in->nudge_r || in->nudge_up) && g->nudge_t == 0 && g->state != GS_BONUS) {
    float dx = in->nudge_l ? 0.22f : in->nudge_r ? -0.22f : 0, dy = 0.16f; /* the table moves; the ball keeps going: relative to the table it goes the other way */
    for (int i = 0; i < PB_MAXB; i++)
      if (g->W.b[i].state == BS_FREE) g->W.b[i].vx += dx, g->W.b[i].vy += dy;
    g->nudge_x = in->nudge_l ? -1.0f : in->nudge_r ? 1.0f : 0, g->nudge_y = 1.0f;
    g->nudge_t = SEC(0.3f);
    g->tilt_meter += 1.0f;
    fx(g, FX_NUDGE, 0, 1);
    if (g->tilt_meter > 2.4f && !g->tilted) {
      g->tilt_warn++;
      g->tilt_meter = 1.0f;
      if (g->tilt_warn >= 3) {
        g->tilted = true, g->tilts++;
        for (int f = 0; f < g->W.nfl; f++) g->W.fl[f].on = false, g->W.fl[f].held = false;
        mode_end(g, false);
        g->multiball = false, g->ball_save_t = 0;
        if (g->wizard) wizard_end(g);
        g->serve_left = 0;
        say(g, MSG_TILT, "TILT", "", 0, SEC(4));
        fx(g, FX_TILT, 0, 1);
      } else {
        say(g, MSG_WARN, "DANGER", g->tilt_warn == 1 ? "WARNING 1" : "WARNING 2", 0, SEC(1.5f));
        fx(g, FX_WARN, g->tilt_warn, 1);
      }
    }
  }
  /* the plunger: hold to pull it back, let go to fire (a pull starts only with a ball waiting on it) */
  pb = ball_on_plunger(g); /* after the nudge */
  if (in->launch && !g->tilted && (g->pull_armed || (!g->launch_was && pb >= 0))) {
    if (!g->launch_was) fx(g, FX_PLUNGER_PULL, 0, 1), g->pull_armed = true;
    g->plunge = fminf(1, g->plunge + 1.3f * TICK);
  } else if (g->launch_was) {
    if (pb >= 0 && g->pull_armed) {
      g->W.b[pb].vy = 0.8f + 4.4f * g->plunge;
      g->launches++;
      fx(g, FX_LAUNCH, 0, g->plunge);
      if (g->state == GS_PLUNGE) {
        g->state = GS_PLAY, g->state_t = 0;
        g->ball_save_t = BALL_SAVE;
        g->skill_t = SEC(4);
      }
    }
    g->plunge = 0, g->pull_armed = false;
  } else g->plunge = fmaxf(0, g->plunge - 4 * TICK);
  g->launch_was = in->launch;
  /* multiball's extra balls come up the shooter lane by themselves, a second apart */
  if (g->multiball && (g->mb_t == SEC(1) || g->mb_t == SEC(2.2f)) && g->balls_in_play < 3) {
    serve(g);
    int i = ball_on_plunger(g);
    if (i >= 0) g->W.b[i].vy = 4.0f + 0.25f * (float)(g->mb_t % 3);
    fx(g, FX_LAUNCH, 1, 1);
  }
  if (g->multiball) g->mb_t++;
  /* a two-ball mode's ball and the wizard mode's three come up the same way, a second apart */
  if (g->serve_left > 0 && g->state == GS_PLAY && !g->tilted) {
    if (--g->serve_t <= 0 && ball_on_plunger(g) < 0 && g->balls_in_play < PB_MAXB - 1) {
      serve(g);
      int i = ball_on_plunger(g);
      if (i >= 0) g->W.b[i].vy = 4.0f + 0.25f * (float)(g->tick % 3);
      fx(g, FX_LAUNCH, 1, 1);
      g->serve_left--, g->serve_t = SEC(1.1f);
    }
  }
  pb_step(&g->W, TICK);
  events(g);
  p = cur(g);
  timers(g);
  if (g->ball_save_t > 0 && g->state == GS_PLAY) g->ball_save_t--;
  if (g->skill_t > 0 && g->state == GS_PLAY) g->skill_t--;
  if (g->combo_t > 0) g->combo_t--;
  if (g->mode >= 0 && --g->mode_t <= 0) mode_end(g, false);
  for (int i = 0; i < T->nel; i++) {
    elem *e = &T->el[i];
    if (e->flash > 0) e->flash = fmaxf(0, e->flash - 4 * TICK);
    e->spin += e->spin_v * TICK;
    e->spin_v *= 0.985f;
  }
  if (g->state == GS_PLAY || g->state == GS_PLUNGE) {
    g->ball_ticks++;
    if (g->balls_in_play <= 0) {
      g->drains++;
      g->state = GS_BONUS, g->state_t = 0;
      g->mode = -1;
      g->multiball = false;
      if (g->wizard) wizard_end(g);
      g->serve_left = 0;
      int64_t bonus = g->tilted ? 0 : (int64_t)p->bonus * p->bonus_x;
      g->msg_value = bonus;
      char b[24];
      snprintf(b, sizeof b, "BONUS X%d", p->bonus_x);
      say(g, MSG_BONUS, g->tilted ? "TILTED" : "BONUS", g->tilted ? "" : b, bonus, SEC(2.5f));
    }
  } else if (g->state == GS_BONUS) {
    if (g->state_t % 6 == 0 && g->state_t < SEC(2)) fx(g, FX_BONUS_TICK, 0, 1);
    if (g->state_t == SEC(2)) score(g, g->tilted ? 0 : (int64_t)p->bonus * p->bonus_x);
    if (g->state_t > SEC(2.7f)) next_ball(g);
  }
  objectives_tick(g);
  lamps_update(g);
  for (int i = 0; i < PB_MAXB; i++) {
    const pb_ball *b = &g->W.b[i];
    if (b->state == BS_OFF) continue;
    union { float f; uint32_t u; } a = { b->x }, c = { b->y };
    hash_u32(g, a.u), hash_u32(g, c.u);
  }
  hash_u32(g, (uint32_t)(p->score & 0xffffffff));
}

/* a ball launched from the shooter lane by the machine (the bench keeps three in play) */
void game_launch_ball(game *g) {
  serve(g);
  int i = ball_on_plunger(g);
  if (i >= 0) g->W.b[i].vy = 4.0f + 0.3f * (float)(g->tick % 3);
  if (g->state == GS_PLUNGE) g->state = GS_PLAY, g->state_t = 0;
}

/* ── tests ───────────────────────────────────────────────────────────── */

void game_test_event(game *g, int ev_kind, int tag, float speed) {
  int ball = 0;
  for (int i = 0; i < PB_MAXB; i++) if (g->W.b[i].state != BS_OFF) { ball = i; break; }
  g->nfx = 0;
  g->W.nev = 1;
  g->W.ev[0] = (pb_event){ .kind = (uint8_t)ev_kind, .ball = (uint8_t)ball, .tag = (int16_t)tag, .speed = speed, .dir = 1 };
  events(g);
  g->W.nev = 0;
  lamps_update(g);
}

void game_test_start_mode(game *g, int m) {
  if (g->mode >= 0) mode_end(g, false);
  mode_begin(g, m % g->T.nmode);
  lamps_update(g);
}

void game_test_light_wizard(game *g) {
  cur(g)->modes_played = 0xf;
  wizard_light_check(g);
  lamps_update(g);
}
