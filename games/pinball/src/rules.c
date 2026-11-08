/* pinball's rules: one to four players taking turns, three balls each, on one table.
 *
 *   scoring    pops 1,000, slings 110, standups 5,000, drop targets 3,000 (a bank 25,000), rollovers 1,000 to 5,000,
 *              spinner 200 a turn, ramps 20,000, orbits 15,000, holes 10,000; a combo (a major shot within 4 s of the
 *              last) doubles, then trebles.
 *   lanes      the top lanes light as the ball rolls through them; all of them raise the bonus multiplier (to 5x).
 *              The flippers move the lit lanes along. The lane blinking as a ball is plunged is the skill shot.
 *   drops      a bank down lights the mode at the scoop (or relights the kickback the second time).
 *   modes      each table's four, started at its mode hole: a timer, the shots it lights, a goal; each shot scores
 *              more than the last, the goal a big award. All four played lights the extra ball.
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
#define LOCKS_NEEDED 2

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

static bool shot_lit_for_mode(const game *g, int s) {
  if (g->mode < 0) return false;
  const mode_def *m = &g->T.modes[g->mode];
  if (!(m->shots_mask >> s & 1)) return false;
  if (g->mode == 2) return (s + g->mode_hits) % 2 == 0;               /* "any lit": every other shot, moving on */
  if (g->mode == 3) return !(g->T.shots[s].made & 0x100);              /* all five: the ones not yet made in it */
  return true;
}

static void lamps_update(game *g) {
  table *T = &g->T;
  player_state *p = cur(g);
  for (int i = 0; i < T->nlamp; i++) T->lamps[i].state = 0;
  for (int i = 0; i < T->lane_n; i++) {
    int l = T->el[T->lane_group + i].lamp;
    lamp_set(g, l, (p->lanes >> i & 1) ? 1 : (g->skill_t > 0 && i == g->skill_lane) ? 3 : 0);
  }
  for (int i = 0; i < 4; i++) lamp_set(g, T->bonus_x_lamp[i], p->bonus_x >= i + 2 ? 1 : 0);
  lamp_set(g, T->kickback_lamp, p->kickback_lit ? 1 : 0);
  lamp_set(g, T->shoot_again_lamp, g->ball_save_t > 0 ? (g->ball_save_t < SEC(3) ? 3 : 2) : p->extra_balls > 0 ? 1 : 0);
  lamp_set(g, T->extra_ball_lamp, p->extra_lit ? 2 : 0);
  for (int i = 0; i < 3; i++) lamp_set(g, T->lock_lamp[i], p->locks > i ? 1 : (p->lock_lit && p->locks == i) ? 2 : 0);
  for (int i = 0; i < T->nmode; i++) lamp_set(g, T->mode_lamp[i], (p->modes_done >> i & 1) ? 1 : g->mode == i ? 3 : (p->mode_next == i) ? 2 : 0);
  lamp_set(g, T->mb_lamp, g->multiball ? 3 : 0);
  lamp_set(g, T->jackpot_lamp, g->multiball ? 3 : 0);
  if (T->mode_hole >= 0) lamp_set(g, T->el[T->mode_hole].lamp, p->mode_lit && g->mode < 0 ? 3 : g->mode >= 0 ? 1 : 0);
  for (int s = 0; s < T->nshot; s++) {
    int st = 0;
    if (shot_lit_for_mode(g, s)) st = 3;
    else if (g->multiball && s == T->jackpot_shot) st = 3;
    else if (p->lock_lit && T->el[T->lock_hole].shot == s) st = 2;
    else if (g->combo_t > 0 && T->shots[s].made) st = 2;
    lamp_set(g, T->shots[s].arrow, st);
  }
  for (int i = 0; i < T->nel; i++) {
    const elem *e = &T->el[i];
    if (e->lamp < 0) continue;
    if (e->kind == EK_DROP) lamp_set(g, e->lamp, e->down ? 1 : 0);
    else if (e->kind == EK_TARGET) lamp_set(g, e->lamp, (p->target_count >> (i & 15) & 1) ? 1 : 0);
    else if (e->kind == EK_ROLLOVER && (i == T->inlane_l || i == T->inlane_r)) lamp_set(g, e->lamp, g->combo_t > 0 ? 2 : 0);
    else if (e->kind == EK_ROLLOVER && (i == T->outlane_l || i == T->outlane_r)) lamp_set(g, e->lamp, (i == T->outlane_l && p->kickback_lit) ? 1 : 0);
  }
  if (g->tilted)
    for (int i = 0; i < T->nlamp; i++) T->lamps[i].state = 0;
}

float game_flash(const game *g, int l) {
  if (l < 0 || l >= g->T.nlamp) return 0;
  switch (g->T.lamps[l].state) {
  case 1: return 1;
  case 2: return (g->tick / 16) % 2 ? 1.0f : 0.08f;
  case 3: return (g->tick / 6) % 2 ? 1.0f : 0.08f;
  default: return 0;
  }
}

/* ── the rules ───────────────────────────────────────────────────────── */

static void drop_reset(game *g, int bank) {
  table *T = &g->T;
  int first = T->drop_banks[bank][0], n = T->drop_banks[bank][1];
  for (int i = first; i < first + n; i++) {
    T->el[i].down = false;
    for (int k = 0; k < T->el[i].nshape; k++) g->W.sh[T->el[i].shape + k].flags &= (uint8_t)~SF_OFF;
  }
  fx(g, FX_DROP_RESET, bank, 1);
}

static void mode_start(game *g) {
  player_state *p = cur(g);
  int m = p->mode_next % g->T.nmode;
  g->mode = m, g->mode_t = SEC(g->T.modes[m].seconds), g->mode_hits = 0;
  for (int s = 0; s < g->T.nshot; s++) g->T.shots[s].made &= 0xff;
  p->mode_lit = false;
  g->modes_started++;
  say(g, MSG_MODE, g->T.modes[m].name, g->T.modes[m].goal, g->T.modes[m].value, SEC(3));
  fx(g, FX_MODE_START, m, 1);
}

static void mode_end(game *g, bool won) {
  if (g->mode < 0) return;
  player_state *p = cur(g);
  int m = g->mode;
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
  for (int k = 1; k <= g->T.nmode; k++) /* the next one not yet won */
    if (!(p->modes_done >> ((m + k) % g->T.nmode) & 1)) { p->mode_next = (m + k) % g->T.nmode; break; }
  g->mode = -1;
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
  if (!p->lock_lit || g->multiball) return;
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
  if (shot_lit_for_mode(g, s)) {
    const mode_def *m = &T->modes[g->mode];
    g->mode_hits++;
    T->shots[s].made |= 0x100;
    int64_t v = (int64_t)m->value * g->mode_hits;
    score(g, v);
    fx(g, FX_MODE_SHOT, s, 1);
    char b[32];
    snprintf(b, sizeof b, "%d TO GO", m->need - g->mode_hits);
    if (g->mode_hits >= m->need) mode_end(g, true);
    else say(g, MSG_BIG, m->name, b, v, SEC(1.5f));
  }
  if (g->multiball && s == T->jackpot_shot) {
    score(g, g->jackpot_value);
    g->jackpots++, g->jackpots_total++;
    say(g, MSG_JACKPOT, "JACKPOT", "", g->jackpot_value, SEC(2.5f));
    fx(g, FX_JACKPOT, 0, 1);
    g->jackpot_value *= 2;
  }
  /* the ramps light the lock (three of them, then one per lock) */
  if (!g->multiball && !p->lock_lit && T->el[T->lock_hole].shot != s) {
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

static void events(game *g) {
  table *T = &g->T;
  player_state *p = cur(g);
  for (int k = 0; k < g->W.nev; k++) {
    const pb_event *ev = &g->W.ev[k];
    pb_ball *b = &g->W.b[ev->ball];
    elem *e = ev->tag >= 0 && ev->tag < T->nel ? &T->el[ev->tag] : NULL;
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
      if (e->kind == EK_TARGET) {
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
          if (!p->mode_lit && g->mode < 0) p->mode_lit = true, say(g, MSG_TEXT, "MODE IS LIT", "SHOOT THE SCOOP", 0, SEC(2));
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
    case EV_RAMP_IN: fx(g, FX_RAMP_IN, ev->tag, ev->speed); break;
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
      if (ev->tag == T->mode_hole && p->mode_lit && g->mode < 0 && !g->multiball) mode_start(g);
      if (e->shot >= 0) shot_made(g, e->shot);
      break;
    case EV_DRAIN:
      g->balls_in_play--;
      fx(g, FX_DRAIN, 0, 1);
      if (g->ball_save_t > 0 && !g->tilted) {
        g->saves++;
        say(g, MSG_BALLSAVE, "BALL SAVED", "", 0, SEC(2));
        fx(g, FX_SAVE, 0, 1);
        serve(g);
        int i = ball_on_plunger(g);
        if (i >= 0) g->W.b[i].vy = 4.2f + 0.3f * (float)(g->tick % 3);
        if (!g->multiball) g->ball_save_t = 0;
      } else if (g->multiball && g->balls_in_play <= 1) {
        g->multiball = false;
        say(g, MSG_TEXT, "MULTIBALL OVER", "", 0, SEC(1.5f));
      }
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
    if (b->state != BS_HELD) continue;
    int hold = g->mode >= 0 && g->mode_t > g->T.modes[g->mode].seconds * 60 - SEC(3) ? SEC(2.6f) : SEC(1.1f);
    if (b->held_ticks >= hold && b->held_tag >= 0 && b->held_tag < T->nel) kick_out(g, b, &T->el[b->held_tag]);
  }
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
  g->mode = -1, g->multiball = false, g->combo_t = 0;
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
