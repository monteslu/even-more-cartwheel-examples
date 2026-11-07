/* skate HUD: each player's corner of the screen (their split view), drawn with the engine's 2D pass and button
 * prompts. The score, the clock, the combo building up (its tricks, its points times the multiplier), the balance
 * meter of a grind (across) or a manual (up and down), the line a landed combo leaves ("+12,400"), the goals of a score
 * attack, the controls at the start of a run. Then the setup screen (mode, park, skaters) and the results.
 * Text sizes are shares of the canvas height (26 px at 1080 lines and up for body text). */
#include <stdio.h>
#include <string.h>

#include "cartwheel/prompt.h"
#include "cartwheel/shell.h"
#include "view.h"

static cw_font F;
typedef struct popup { char text[48]; int points, kind, t; } popup;
static popup POP[MAXP][4];

void hud_init(cw_font font) { F = font; }

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static cw_text_style tstyle(float size, cw_color c) { return (cw_text_style){ .size = size, .color = c, .outline_width = size * 0.1f, .outline_color = rgba(0, 0, 0, 0.85f) }; }
static cw_text_metrics measure(const char *s, float size) {
  cw_text_style st = tstyle(size, rgba(1, 1, 1, 1));
  return cw_font_measure(F, s, &st);
}
static cw_text_metrics text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_style st = tstyle(size, c);
  if (al != CW_ALIGN_LEFT) {
    cw_text_metrics m = cw_font_measure(F, s, &st);
    x -= al == CW_ALIGN_CENTER ? m.width * 0.5f : m.width;
  }
  return cw_r2d_text(F, s, cw_v2(x, y), &st);
}
static cw_text_metrics text_wrap(const char *s, float x, float y, float w, float size, cw_color c) {
  cw_text_style st = tstyle(size, c);
  st.wrap_width = w;
  st.align = CW_ALIGN_CENTER;
  return cw_r2d_text(F, s, cw_v2(x, y), &st);
}
/* a pill of text with prompts, anchored at (ax, ay) of itself */
static cw_vec2 pill(const char *markup, int player, float x, float y, float ax, float ay, float size, cw_color c, cw_color bg) {
  cw_vec2 sz = cw_prompt_text_size(F, markup, player, size);
  float pad = size * 0.3f, w = sz.x + pad * 2, h = sz.y + pad * 1.2f;
  float l = x - w * ax, t = y - h * ay;
  cw_r2d_round_rect(cw_rect_make(l, t, w, h), h * 0.3f, 6, bg);
  cw_prompt_text(F, markup, player, cw_v2(l + pad, t + pad * 0.6f), size, c, CW_ALIGN_LEFT);
  return (cw_vec2){ w, h };
}
static void commas(int v, char *out, size_t cap) {
  char raw[24];
  snprintf(raw, sizeof raw, "%d", v < 0 ? -v : v);
  int n = (int)strlen(raw), o = 0;
  if (v < 0 && o < (int)cap - 1) out[o++] = '-';
  for (int i = 0; i < n && o < (int)cap - 2; i++) {
    out[o++] = raw[i];
    if ((n - i - 1) % 3 == 0 && i < n - 1) out[o++] = ',';
  }
  out[o] = 0;
}

void hud_popup(int player, const char *t, int points, int kind) {
  popup *p = POP[player];
  memmove(&p[1], &p[0], sizeof *p * 3);
  snprintf(p[0].text, sizeof p[0].text, "%s", t);
  p[0].points = points, p[0].kind = kind, p[0].t = 110;
}

static cw_color pcol(int i, float a) { return rgba(0.55f + 0.45f * PLAYER_COL[i][0], 0.55f + 0.45f * PLAYER_COL[i][1], 0.55f + 0.45f * PLAYER_COL[i][2], a); }
static const cw_color BG = { 0.03f, 0.04f, 0.08f, 0.62f };

/* a balance meter: across (a grind) or up and down (a manual), the needle at v (-1..1) */
static void meter(float cx, float cy, float len, float th, float v, bool vertical, float u) {
  float a = fabsf(v);
  cw_color nc = a < 0.5f ? rgba(0.4f, 1, 0.45f, 1) : a < 0.8f ? rgba(1, 0.85f, 0.25f, 1) : rgba(1, 0.3f, 0.25f, 1);
  if (vertical) {
    cw_r2d_round_rect(cw_rect_make(cx - th * 0.5f, cy - len * 0.5f, th, len), th * 0.5f, 6, BG);
    cw_r2d_rect(cw_rect_make(cx - th * 0.5f, cy - 1.5f * u, th, 3 * u), rgba(1, 1, 1, 0.5f));
    float y = cy - v * len * 0.46f;
    cw_r2d_circle(cw_v2(cx, y), th * 0.7f, 16, nc);
  } else {
    cw_r2d_round_rect(cw_rect_make(cx - len * 0.5f, cy - th * 0.5f, len, th), th * 0.5f, 6, BG);
    cw_r2d_rect(cw_rect_make(cx - 1.5f * u, cy - th * 0.5f, 3 * u, th), rgba(1, 1, 1, 0.5f));
    float x = cx + v * len * 0.46f;
    cw_r2d_circle(cw_v2(x, cy), th * 0.7f, 16, nc);
  }
}

static void player_hud(const sim *S, int i, cw_rect R, float u, int players, float t) {
  const skater *s = &S->s[i];
  char b[160], n[32];
  float pad = 16 * u, big = 44 * u, body = 30 * u;
  float x = R.x + pad, y = R.y + pad;
  /* the score: P1 and the points */
  commas(s->score, n, sizeof n);
  snprintf(b, sizeof b, "P%d", i + 1);
  cw_text_metrics pm = measure(b, body), sm = measure(n, big);
  float w = pm.width + sm.width + 40 * u, h = sm.height + 10 * u;
  cw_r2d_round_rect(cw_rect_make(x, y, w, h), 10 * u, 6, BG);
  cw_r2d_rect(cw_rect_make(x, y + 6 * u, 6 * u, h - 12 * u), pcol(i, 1));
  text(b, x + 16 * u, y + (h - pm.height) * 0.5f, body, pcol(i, 1), CW_ALIGN_LEFT);
  text(n, x + 26 * u + pm.width, y + 5 * u, big, rgba(1, 0.96f, 0.85f, 1), CW_ALIGN_LEFT);
  y += h + 6 * u;
  /* the clock (timed modes): over the view's top middle */
  if (S->mode != MODE_FREE) {
    int secs = (S->clock + TPS - 1) / TPS;
    snprintf(b, sizeof b, "%d:%02d", secs / 60, secs % 60);
    bool low = secs <= 10 && S->state == RUN_PLAY;
    cw_text_metrics cm = measure(b, big);
    float cx = R.x + R.w * 0.5f;
    cw_r2d_round_rect(cw_rect_make(cx - cm.width * 0.5f - 14 * u, R.y + pad, cm.width + 28 * u, cm.height + 8 * u), 10 * u, 6, BG);
    text(b, cx, R.y + pad + 4 * u, big, low && (S->tick / 15) % 2 ? rgba(1, 0.35f, 0.3f, 1) : rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
  /* the best combo, under the score */
  if (s->best_combo > 0) {
    commas(s->best_combo, n, sizeof n);
    snprintf(b, sizeof b, "best combo %s", n);
    text(b, x + 4 * u, y, body * 0.9f, rgba(0.85f, 0.9f, 1, 0.9f), CW_ALIGN_LEFT);
    y += body * 1.2f;
  }
  /* the goals (score attack): ticked off as they go */
  if (S->mode == MODE_ATTACK && players <= 2) {
    float gy = y + 6 * u;
    for (int g = 0; g < GOAL_COUNT; g++) {
      bool done = S->goals[i] & (1u << g);
      sim_goal_text(S, g, n, sizeof n);
      snprintf(b, sizeof b, "%s %s", done ? "+" : "-", n);
      gy += text(b, x + 4 * u, gy, 26 * u, done ? rgba(0.55f, 1, 0.55f, 1) : rgba(1, 1, 1, 0.85f), CW_ALIGN_LEFT).height;
    }
  } else if (S->mode == MODE_ATTACK) {
    int k = 0;
    for (int g = 0; g < GOAL_COUNT; g++) k += (S->goals[i] >> g) & 1;
    snprintf(b, sizeof b, "goals %d of %d", k, GOAL_COUNT);
    text(b, x + 4 * u, y, 26 * u, rgba(0.85f, 1, 0.85f, 1), CW_ALIGN_LEFT);
  }
  /* the combo under way: its tricks, then points x multiplier */
  float cx = R.x + R.w * 0.5f, cy = R.y + R.h - pad;
  if (s->cb.n > 0 && s->st != SK_BAIL) {
    int base = 0;
    for (int k = 0; k < s->cb.n; k++) base += s->cb.it[k].points;
    commas(base, n, sizeof n);
    snprintf(b, sizeof b, "%s x %d", n, s->cb.n);
    cw_text_metrics m = measure(b, big);
    cy -= m.height;
    text(b, cx, cy, big, rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER);
    char line[400];
    sk_combo_text(s, line, sizeof line);
    /* the newest tricks (the line keeps its end) */
    const char *show = line;
    while (strlen(show) > 90) { const char *p = strstr(show + 1, " + "); if (!p) break; show = p + 3; }
    cw_text_style st = tstyle(body, rgba(1, 1, 1, 1));
    st.wrap_width = R.w - pad * 4;
    cw_text_metrics lm = cw_font_measure(F, show, &st);
    cy -= lm.height + 2 * u;
    text_wrap(show, R.x + pad * 2, cy, R.w - pad * 4, body, rgba(1, 1, 1, 1));
  }
  /* the balance meter */
  if (s->st == SK_GRIND) meter(cx, cy - 40 * u, fminf(R.w * 0.4f, 360 * u), 16 * u, clampf(s->bal, -1, 1), false, u);
  else if (s->manual) meter(cx + fminf(R.w * 0.22f, 220 * u), R.y + R.h * 0.55f, fminf(R.h * 0.35f, 260 * u), 16 * u, clampf(s->mbal, -1, 1), true, u);
  /* what a combo left when it banked, or the bail */
  for (int k = 0; k < 4; k++) {
    popup *p = &POP[i][k];
    if (p->t <= 0) continue;
    p->t--;
    float a = clampf((float)p->t / 30.0f, 0, 1), rise = (float)(110 - p->t) * 0.4f * u;
    float py = R.y + R.h * 0.38f - rise - (float)k * 50 * u;
    cw_color c = p->kind == 1 ? rgba(1, 0.4f, 0.35f, a) : p->kind == 2 ? rgba(0.6f, 1, 0.7f, a) : rgba(1, 0.92f, 0.45f, a);
    text(p->text, cx, py, p->kind == 0 ? big : body * 1.1f, c, CW_ALIGN_CENTER);
  }
  /* the controls, for the first seconds of a run */
  if (S->tick < TPS * 9 && S->state == RUN_PLAY) {
    float a = clampf((float)(TPS * 9 - S->tick) / 40.0f, 0, 1);
    float hy = R.y + R.h * 0.62f;
    cw_color fg = rgba(1, 1, 1, a), bg = rgba(BG.r, BG.g, BG.b, BG.a * a);
    cw_vec2 s1 = pill("{steer_up} push   {ollie} hold, let go: pop   {grind} near a rail: grind", i, cx, hy, 0.5f, 0, 26 * u, fg, bg);
    pill("{flip} flip   {grab} grab   {manual} manual   {reset} start point", i, cx, hy + s1.y + 6 * u, 0.5f, 0, 26 * u, fg, bg);
  }
  (void)t;
}

void hud_draw(const sim *S, int players, int phase, float t) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 1080.0f;
  if (phase != 1) return;
  for (int i = 0; i < players; i++) {
    cw_rect R = cw_shell_player_rect(i);
    player_hud(S, i, R, u, players, t);
  }
  /* three players: the fourth quarter is the scoreboard */
  if (players == 3) {
    cw_rect R = cw_shell_shared_rect();
    cw_r2d_rect(R, rgba(0.05f, 0.07f, 0.14f, 1));
    float y = R.y + R.h * 0.18f;
    y += text(S->mode == MODE_FREE ? PARK_NAMES[S->park_id] : MODE_NAMES[S->mode], R.x + R.w * 0.5f, y, 40 * u, rgba(1, 0.82f, 0.3f, 1), CW_ALIGN_CENTER).height + 16 * u;
    for (int i = 0; i < 3; i++) {
      char b[64], n[32];
      commas(S->s[i].score, n, sizeof n);
      snprintf(b, sizeof b, "P%d  %s", i + 1, n);
      y += text(b, R.x + R.w * 0.5f, y, 40 * u, pcol(i, 1), CW_ALIGN_CENTER).height + 6 * u;
    }
  }
  /* split lines between the views */
  if (players > 1) {
    cw_r2d_rect(cw_rect_make(cs.x * 0.5f - 2 * u, 0, 4 * u, cs.y), rgba(0.02f, 0.02f, 0.04f, 1));
    if (players > 2) cw_r2d_rect(cw_rect_make(0, cs.y * 0.5f - 2 * u, cs.x, 4 * u), rgba(0.02f, 0.02f, 0.04f, 1));
  }
}

/* ── setup and results ─────────────────────────────────────────────────── */

void hud_setup(const ui_state *ui, int players) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 1080.0f, body = 32 * u, big = 54 * u;
  char b[128];
  float y = cs.y * 0.08f;
  text("SKATE", cs.x * 0.5f, y, 96 * u, rgba(1, 0.82f, 0.3f, 1), CW_ALIGN_CENTER);
  y += 110 * u;
  float pw = fminf(cs.x * 0.6f, 900 * u), px = cs.x * 0.5f - pw * 0.5f;
  const char *labels[3] = { "MODE", "PARK", "SKATER" };
  char vals[3][64];
  snprintf(vals[0], sizeof vals[0], "%s", MODE_NAMES[ui->mode]);
  snprintf(vals[1], sizeof vals[1], "%s", PARK_NAMES[ui->park]);
  snprintf(vals[2], sizeof vals[2], "%s", SKIN_NAMES[ui->skin[0]]);
  for (int r = 0; r < 3; r++) {
    bool sel = ui->row == r;
    float h = big + 24 * u;
    cw_r2d_round_rect(cw_rect_make(px, y, pw, h), 14 * u, 6, sel ? rgba(0.1f, 0.12f, 0.2f, 0.85f) : BG);
    if (sel) cw_r2d_round_rect_lines(cw_rect_make(px, y, pw, h), 14 * u, 3 * u, 6, rgba(1, 0.82f, 0.3f, 1));
    text(labels[r], px + 30 * u, y + (h - measure(labels[r], body).height) * 0.5f, body, rgba(0.8f, 0.85f, 1, 1), CW_ALIGN_LEFT);
    snprintf(b, sizeof b, "%s", vals[r]);
    text(b, px + pw - 30 * u, y + 12 * u, big, sel ? rgba(1, 1, 1, 1) : rgba(0.9f, 0.9f, 0.9f, 1), CW_ALIGN_RIGHT);
    y += h + 12 * u;
  }
  /* what the mode and the park are */
  const char *mode_line = ui->mode == MODE_FREE ? "no clock: skate the park, find its lines" : ui->mode == MODE_ATTACK ? "two minutes: the park's goals, a high score to beat" : "two minutes: the most points wins";
  text(mode_line, cs.x * 0.5f, y + 6 * u, body, rgba(1, 1, 1, 0.95f), CW_ALIGN_CENTER);
  y += body * 1.4f;
  if (ui->mode == MODE_ATTACK && ui->best > 0) {
    char n[32];
    commas(ui->best, n, sizeof n);
    snprintf(b, sizeof b, "high score here: %s", n);
    text(b, cs.x * 0.5f, y + 6 * u, body, rgba(1, 0.85f, 0.4f, 1), CW_ALIGN_CENTER);
    y += body * 1.4f;
  }
  /* the other players pick their own skater */
  if (players > 1) {
    float cw = fminf(cs.x / (float)players, 420 * u), x0 = cs.x * 0.5f - cw * (float)players * 0.5f;
    for (int i = 0; i < players; i++) {
      float x = x0 + cw * (float)i + 8 * u, w = cw - 16 * u, h = 120 * u;
      cw_r2d_round_rect(cw_rect_make(x, y + 20 * u, w, h), 12 * u, 6, BG);
      snprintf(b, sizeof b, "P%d", i + 1);
      text(b, x + w * 0.5f, y + 28 * u, body, pcol(i, 1), CW_ALIGN_CENTER);
      text(SKIN_NAMES[ui->skin[i]], x + w * 0.5f, y + 28 * u + body * 1.2f, body * 1.2f, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
    }
    y += 150 * u;
    pill("{steer_left|steer_right} each player: your skater", -1, cs.x * 0.5f, y, 0.5f, 0, 28 * u, rgba(1, 1, 1, 1), BG);
    y += 50 * u;
  }
  pill("{ui_up|ui_down} choose   {ui_left|ui_right} change   {ui_accept} skate", 0, cs.x * 0.5f, cs.y - 90 * u, 0.5f, 0, 30 * u, rgba(1, 1, 1, 1), BG);
}

void hud_results(const sim *S, int players, int best, bool new_best) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 1080.0f, body = 32 * u, big = 60 * u;
  char b[128], n[32];
  cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), rgba(0.02f, 0.03f, 0.06f, 0.55f));
  float y = cs.y * 0.16f;
  if (S->mode == MODE_BATTLE && players > 1 && S->winner >= 0) snprintf(b, sizeof b, "P%d WINS", S->winner + 1);
  else snprintf(b, sizeof b, "TIME");
  y += text(b, cs.x * 0.5f, y, big * 1.4f, S->mode == MODE_BATTLE && S->winner >= 0 ? pcol(S->winner, 1) : rgba(1, 0.85f, 0.35f, 1), CW_ALIGN_CENTER).height + 20 * u;
  /* the scores, best first */
  int order[MAXP];
  for (int i = 0; i < players; i++) order[i] = i;
  for (int i = 0; i < players; i++)
    for (int j = i + 1; j < players; j++)
      if (S->s[order[j]].score > S->s[order[i]].score) { int t = order[i]; order[i] = order[j]; order[j] = t; }
  for (int r = 0; r < players; r++) {
    int i = order[r];
    commas(S->s[i].score, n, sizeof n);
    snprintf(b, sizeof b, "P%d  %s", i + 1, n);
    y += text(b, cs.x * 0.5f, y, big, pcol(i, 1), CW_ALIGN_CENTER).height;
    commas(S->s[i].best_combo, n, sizeof n);
    int k = 0;
    for (int g = 0; g < GOAL_COUNT; g++) k += (S->goals[i] >> g) & 1;
    if (S->mode == MODE_ATTACK) snprintf(b, sizeof b, "best combo %s   goals %d of %d", n, k, GOAL_COUNT);
    else snprintf(b, sizeof b, "best combo %s   %d tricks   %d bails", n, S->s[i].tricks, S->s[i].bails);
    y += text(b, cs.x * 0.5f, y, body, rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER).height + 14 * u;
  }
  if (S->mode == MODE_ATTACK) {
    commas(best, n, sizeof n);
    snprintf(b, sizeof b, new_best ? "a new high score for %s: %s" : "high score for %s: %s", PARK_NAMES[S->park_id], n);
    text(b, cs.x * 0.5f, y + 10 * u, body, new_best ? rgba(0.6f, 1, 0.6f, 1) : rgba(1, 0.85f, 0.4f, 1), CW_ALIGN_CENTER);
  }
  pill("{ui_accept} again   {ui_back} change the run", -1, cs.x * 0.5f, cs.y - 100 * u, 0.5f, 0, 30 * u, rgba(1, 1, 1, 1), BG);
}
