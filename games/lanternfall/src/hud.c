/* lanternfall: the HUD (hud.h). Text holds prompts through the engine's markup ({jump}, {dash} ...): the text says
 * what a button does, the glyph says which. Sizes are on the 1080-line scale (u) and never under the body floor. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cartwheel/prompt.h"
#include "hud.h"
#include "view.h"

static cw_font F;
void hud_init(cw_font font) { F = font; }

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static cw_color hexa(uint32_t c, float a) { return (cw_color){ (float)((c >> 16) & 255) / 255.0f, (float)((c >> 8) & 255) / 255.0f, (float)(c & 255) / 255.0f, a }; }
static cw_text_style ts(float size, cw_color c) { return (cw_text_style){ .size = size, .color = c, .outline_width = size * 0.1f, .outline_color = rgba(0.05f, 0.04f, 0.08f, 0.85f) }; }
static cw_text_metrics text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_style st = ts(size, c);
  if (al != CW_ALIGN_LEFT) {
    cw_text_metrics m = cw_font_measure(F, s, &st);
    x -= al == CW_ALIGN_CENTER ? m.width * 0.5f : m.width;
  }
  return cw_r2d_text(F, s, (cw_vec2){ x, y }, &st);
}
static cw_vec2 pill(const char *markup, float x, float y, float ax, float size, cw_color c, cw_color bg) {
  cw_vec2 sz = cw_prompt_text_size(F, markup, -1, size);
  float pad = size * 0.35f, w = sz.x + pad * 2, h = sz.y + pad * 1.2f, l = x - w * ax;
  cw_r2d_round_rect((cw_rect){ l, y, w, h }, h * 0.3f, 6, bg);
  cw_prompt_text(F, markup, -1, (cw_vec2){ l + pad, y + pad * 0.6f }, size, c, CW_ALIGN_LEFT);
  return (cw_vec2){ w, h };
}

/* a leaf of health: full, or an empty outline */
static void leaf(float x, float y, float r, bool full, cw_color c) {
  cw_vec2 p[4] = { { x, y - r }, { x + r * 0.62f, y }, { x, y + r }, { x - r * 0.62f, y } };
  if (full) cw_r2d_polygon(p, 4, c), cw_r2d_polygon((cw_vec2[4]){ { x, y - r * 0.55f }, { x + r * 0.25f, y - r * 0.1f }, { x, y + r * 0.2f }, { x - r * 0.25f, y - r * 0.1f } }, 4, rgba(1, 1, 1, 0.45f));
  else cw_r2d_polygon_lines(p, 4, r * 0.16f, rgba(c.r, c.g, c.b, 0.65f));
}

static const char *const CARD_TITLE[4] = { "", "HEART LEAF", "LANTERN OIL", "OLD RELIC" };
static const char *ability_line(int ab) {
  switch (ab) {
  case AB_BOLT: return "{shoot}: loose a spark of light. It costs energy; strikes fill it again";
  case AB_DJUMP: return "{jump} again in the air for a second jump";
  case AB_DASH: return "{dash}: burst ahead, on the ground or in the air";
  case AB_CLIMB: return "hold toward a wall to cling, {move_up|move_down} to climb, {jump} to leap off";
  case AB_POUND: return "{move_down} + {dash} in the air: plunge. Cracked floors give way";
  case AB_SWIM: return "deep water no longer pushes you back: swim, and {jump} to stroke";
  case AB_GRAPPLE: return "{grapple} under a ring overhead: swing on the line, {jump} to let go";
  default: return "";
  }
}

static void draw_map(const sim *s, const hud_state *h, float t, cw_vec2 cs, float u) {
  const world *w = s->w;
  cw_r2d_rect((cw_rect){ 0, 0, cs.x, cs.y }, rgba(0.05f, 0.06f, 0.1f, 0.86f));
  /* the grid's used span */
  int gx0 = GW, gy0 = GH, gx1 = 0, gy1 = 0;
  for (int r = 0; r < NREGIONS; r++) {
    const region_info *g = &REGIONS[r];
    if (g->x < gx0) gx0 = g->x;
    if (g->y < gy0) gy0 = g->y;
    if (g->x + g->w > gx1) gx1 = g->x + g->w;
    if (g->y + g->h > gy1) gy1 = g->y + g->h;
  }
  float top = 110 * u, bottom = 90 * u;
  float cell = fminf((cs.x - 80 * u) / (float)(gx1 - gx0), (cs.y - top - bottom) / (float)(gy1 - gy0));
  float ox = (cs.x - cell * (float)(gx1 - gx0)) * 0.5f - (float)gx0 * cell, oy = top - (float)gy0 * cell;
  float th = cell * (1.0f / CELL_H); /* one tile in map units, vertically */
  for (int i = 0; i < w->nrooms; i++) {
    if (!((s->seen[i >> 3] >> (i & 7)) & 1)) continue;
    const room *r = &w->rooms[i];
    cw_color c = hexa(REGIONS[r->region].top, 0.85f);
    cw_rect rc = { ox + (float)r->x * cell + 1.5f * u, oy + (float)r->y * cell + 1.5f * u, (float)r->w * cell - 3 * u, (float)r->h * cell - 3 * u };
    cw_r2d_rect(rc, rgba(c.r * 0.45f, c.g * 0.45f, c.b * 0.45f, 0.95f));
    cw_r2d_rect_lines(rc, 2 * u, c);
    if (i == s->room && (int)(t * 3) % 2) cw_r2d_rect(rc, rgba(1, 1, 1, 0.18f));
  }
  /* doors of seen rooms: gaps of light across the wall */
  for (int i = 0; i < w->ndoors; i++) {
    const door *d = &w->doors[i];
    bool sa = (s->seen[d->a >> 3] >> (d->a & 7)) & 1, sb = (s->seen[d->b >> 3] >> (d->b & 7)) & 1;
    if (!sa && !sb) continue;
    float x = ox + ((float)d->x + (d->vertical ? (float)d->len * 0.5f : 2)) / CELL_W * cell, y = oy + ((float)d->y + (d->vertical ? 2 : 2)) * th;
    cw_color c = d->pad == T_SEAL ? rgba(1, 0.8f, 0.35f, 1) : d->pad == T_BOSSDOOR ? rgba(0.95f, 0.45f, 1, 1) : rgba(0.95f, 0.95f, 0.85f, 1);
    if (d->vertical) cw_r2d_rect((cw_rect){ x - cell * 0.14f, y - 2 * u, cell * 0.28f, 4 * u }, c);
    else cw_r2d_rect((cw_rect){ x - 2 * u, y - cell * 0.12f, 4 * u, cell * 0.24f }, c);
  }
  /* pickups, shrines and wardens in rooms you have seen: a dot until taken, a ring after */
  for (int i = 0; i < w->nents; i++) {
    const ent *e = &w->ents[i];
    if (!((s->seen[e->room >> 3] >> (e->room & 7)) & 1)) continue;
    float x = ox + e->x / CELL_W * cell, y = oy + e->y * th;
    if (e->kind == EK_PICKUP) {
      bool got = sim_got(s, e->id);
      cw_color c = e->sub == PK_ABILITY ? rgba(1, 0.85f, 0.35f, 1) : e->sub == PK_HEALTH ? rgba(1, 0.5f, 0.55f, 1) : e->sub == PK_ENERGY ? rgba(0.5f, 0.85f, 1, 1) : rgba(0.85f, 0.65f, 1, 1);
      if (got) cw_r2d_circle_lines((cw_vec2){ x, y }, cell * 0.12f, 1.5f * u, 10, rgba(c.r, c.g, c.b, 0.6f));
      else cw_r2d_circle((cw_vec2){ x, y }, cell * 0.14f, 10, c);
    } else if (e->kind == EK_SAVE) {
      bool lit = (s->saves_lit[e->id >> 3] >> (e->id & 7)) & 1;
      cw_r2d_circle_lines((cw_vec2){ x, y - cell * 0.1f }, cell * 0.16f, 2.5f * u, 12, lit ? rgba(0.55f, 1, 0.9f, 1) : rgba(0.6f, 0.65f, 0.65f, 1));
    } else if (e->kind == EK_BOSS) {
      bool down = (s->bosses_down >> e->sub) & 1;
      cw_r2d_circle((cw_vec2){ x, y - cell * 0.2f }, cell * 0.2f, 12, down ? rgba(0.5f, 0.5f, 0.55f, 1) : rgba(1, 0.35f, 0.35f, 1));
    }
  }
  /* you */
  for (int i = 0; i < s->players; i++) {
    const player *p = &s->p[i];
    if (!p->active) continue;
    float x = ox + p->x / CELL_W * cell, y = oy + (p->y - 0.9f) * th;
    cw_r2d_circle((cw_vec2){ x, y }, cell * 0.22f * (1 + 0.15f * sinf(t * 8)), 14, i ? rgba(1, 0.6f, 0.85f, 1) : rgba(1, 1, 1, 1));
  }
  char b[160];
  int region = s->room >= 0 ? w->rooms[s->room].region : 0;
  text(REGIONS[region].name, cs.x * 0.5f, 30 * u, 44 * u, hexa(REGIONS[region].top, 1), CW_ALIGN_CENTER);
  int got = 0, all = w->npickups, seen = 0;
  for (int k = 0; k < 4; k++) got += sim_pickups_got(s, k);
  for (int r = 0; r < w->nrooms; r++) seen += (s->seen[r >> 3] >> (r & 7)) & 1;
  snprintf(b, sizeof b, "rooms %d of %d    finds %d of %d    beacons %d of 5", seen, w->nrooms, got, all, sim_beacons(s));
  text(b, cs.x * 0.5f, cs.y - bottom + 12 * u, 28 * u, rgba(0.9f, 0.92f, 1, 1), CW_ALIGN_CENTER);
  pill("{map}: close the map", cs.x * 0.5f, cs.y - bottom + 50 * u, 0.5f, 26 * u, rgba(1, 1, 1, 1), rgba(0.1f, 0.12f, 0.2f, 0.85f));
  (void)h;
}

void hud_menu_build(const sim *s, hud_state *h) {
  h->menu_n = 0;
  for (int i = 0; i < s->w->nents && h->menu_n < 64; i++) {
    const ent *e = &s->w->ents[i];
    if (e->kind == EK_SAVE && ((s->saves_lit[e->id >> 3] >> (e->id & 7)) & 1)) {
      if (i == s->save_ent) h->menu_sel = h->menu_n;
      h->menu_ents[h->menu_n++] = i;
    }
  }
}

void hud_draw(const sim *s, const hud_state *h, float t) {
  cw_vec2 cs = view_canvas();
  float u = cs.y / 1080.0f;
  char b[200];
  cw_r2d_begin(NULL);
  cw_r2d_layer_set(0, NULL);
  if (h->map_open) {
    draw_map(s, h, t, cs, u);
    cw_r2d_end();
    return;
  }
  /* health and energy, top left, for each player */
  for (int i = 0; i < s->players; i++) {
    const player *p = &s->p[i];
    if (!p->active) continue;
    float x0 = 40 * u + (float)i * cs.x * 0.5f, y0 = 44 * u;
    cw_color hc = i ? rgba(1, 0.55f, 0.8f, 1) : rgba(1, 0.42f, 0.42f, 1);
    if (s->players > 1) text(i ? "P2" : "P1", x0 - 8 * u, y0 - 18 * u, 26 * u, rgba(1, 1, 1, 0.9f), CW_ALIGN_LEFT), x0 += 48 * u;
    for (int k = 0; k < p->hp_max; k++) leaf(x0 + (float)k * 34 * u, y0, 15 * u, k < p->hp, hc);
    float ey = y0 + 30 * u;
    for (int k = 0; k < p->en_max; k++) {
      cw_rect r = { x0 - 10 * u + (float)k * 26 * u, ey, 20 * u, 12 * u };
      cw_r2d_round_rect(r, 4 * u, 3, k < p->en ? rgba(0.45f, 0.85f, 1, 1) : rgba(0.25f, 0.3f, 0.4f, 0.7f));
    }
    if (p->en < p->en_max) {
      float k = (float)p->en_frac / 300.0f;
      cw_r2d_rect((cw_rect){ x0 - 10 * u + (float)p->en * 26 * u, ey + 14 * u, 20 * u * k, 3 * u }, rgba(0.45f, 0.85f, 1, 0.8f));
    }
  }
  /* finds, top right */
  snprintf(b, sizeof b, "relics %d", s->relics);
  if (s->relics) text(b, cs.x - 40 * u, 30 * u, 28 * u, rgba(0.9f, 0.8f, 1, 1), CW_ALIGN_RIGHT);
  /* a region's name as you enter it */
  if (h->banner_t > 0 && h->banner_region >= 0) {
    float a = fminf(1, h->banner_t * 1.5f) * fminf(1, (3.5f - (3.5f - h->banner_t)) * 1.0f);
    const region_info *g = &REGIONS[h->banner_region];
    float y = cs.y * 0.18f;
    cw_r2d_rect((cw_rect){ cs.x * 0.25f, y + 70 * u, cs.x * 0.5f, 3 * u }, hexa(g->top, 0.8f * a));
    text(g->name, cs.x * 0.5f, y, 64 * u, hexa(g->top, a), CW_ALIGN_CENTER);
  }
  /* the card of something found */
  if (h->card_t > 0) {
    float a = fminf(1, h->card_t * 2);
    float y = cs.y * 0.62f;
    const char *title = h->card_kind == PK_ABILITY || h->card_kind > PK_RELIC ? "" : CARD_TITLE[h->card_kind];
    if (h->card_kind == 4 && h->card_arg >= 0 && h->card_arg < CH_KINDS) title = CHARM_NAME[h->card_arg];
    if (h->card_kind == PK_ABILITY)
      for (int k = 0; k < NABILITIES; k++)
        if (h->card_arg == (1 << k)) title = ABILITY_NAME[k];
    cw_r2d_round_rect((cw_rect){ cs.x * 0.2f, y - 16 * u, cs.x * 0.6f, 170 * u }, 18 * u, 6, rgba(0.06f, 0.07f, 0.12f, 0.78f * a));
    text(title, cs.x * 0.5f, y, 56 * u, rgba(1, 0.88f, 0.5f, a), CW_ALIGN_CENTER);
    const char *line = h->card_kind == 4 ? CHARM_LINE[h->card_arg] : h->card_kind == PK_ABILITY ? ability_line(h->card_arg) : h->card_kind == PK_HEALTH ? "one more leaf of health" : h->card_kind == PK_ENERGY ? "one more measure of energy" : "a relic of the old city";
    cw_vec2 sz = cw_prompt_text_size(F, line, -1, 30 * u);
    cw_prompt_text(F, line, -1, (cw_vec2){ cs.x * 0.5f - sz.x * 0.5f, y + 80 * u }, 30 * u, rgba(1, 1, 1, a), CW_ALIGN_LEFT);
  }
  /* a trial: the doors shut on an upgrade's guardians */
  if (h->trial_t > 0) {
    static const char *const MSG[3] = { "THE DOORS SEAL: ITS GUARDIANS WAKE", "MORE COME", "THE WAY IS OPEN" };
    float a = fminf(1, h->trial_t * 2);
    text(MSG[h->trial_msg % 3], cs.x * 0.5f, cs.y * 0.3f, 40 * u, rgba(1, 0.8f, 0.6f, a), CW_ALIGN_CENTER);
  }
  /* the warden's name and its health */
  if (s->b.active) {
    float w = cs.x * 0.5f, x = cs.x * 0.25f, y = cs.y - 70 * u;
    text(BOSS_NAME[s->b.idx], cs.x * 0.5f, y - 46 * u, 34 * u, rgba(1, 0.85f, 0.75f, 1), CW_ALIGN_CENTER);
    cw_r2d_round_rect((cw_rect){ x - 4 * u, y - 4 * u, w + 8 * u, 22 * u }, 8 * u, 4, rgba(0.05f, 0.04f, 0.08f, 0.85f));
    cw_r2d_round_rect((cw_rect){ x, y, w * fmaxf(0, (float)s->b.hp / (float)s->b.hp_max), 14 * u }, 6 * u, 4, rgba(1, 0.35f, 0.4f, 1));
  }
  /* a shrine at hand */
  for (int i = 0; i < s->w->nents && !h->menu_open; i++) {
    const ent *e = &s->w->ents[i];
    if (e->kind == EK_SAVE && fabsf(e->x - s->p[0].x) < 1.8f && fabsf(e->y - s->p[0].y) < 1.2f) {
      cw_vec2 sp = view_world_to_screen((cw_vec2){ e->x, e->y - 3.0f });
      pill("{move_up}: rest at the shrine", sp.x, sp.y - 40 * u, 0.5f, 26 * u, rgba(1, 1, 1, 1), rgba(0.06f, 0.08f, 0.12f, 0.8f));
    }
  }
  /* the shrine's menu: stay, or travel to another lit shrine */
  if (h->menu_open) {
    float w = cs.x * 0.42f, rh = 44 * u, x = cs.x * 0.5f - w * 0.5f;
    int n = h->menu_n;
    float y = cs.y * 0.5f - (float)n * rh * 0.5f - 60 * u;
    cw_r2d_round_rect((cw_rect){ x - 20 * u, y - 70 * u, w + 40 * u, (float)n * rh + 170 * u }, 18 * u, 6, rgba(0.05f, 0.07f, 0.1f, 0.88f));
    text("THE SHRINE: RESTED AND SAVED", cs.x * 0.5f, y - 56 * u, 34 * u, rgba(0.6f, 1, 0.9f, 1), CW_ALIGN_CENTER);
    for (int k = 0; k < n; k++) {
      const ent *e = &s->w->ents[h->menu_ents[k]];
      int region = s->w->rooms[e->room].region;
      int idx = 0;
      for (int j = 0; j < h->menu_ents[k]; j++) idx += s->w->ents[j].kind == EK_SAVE && s->w->rooms[s->w->ents[j].room].region == region;
      snprintf(b, sizeof b, "%s%s", REGIONS[region].name, h->menu_ents[k] == s->save_ent ? "  (here)" : idx ? "  (the second shrine)" : "");
      bool sel = k == h->menu_sel;
      if (sel) cw_r2d_round_rect((cw_rect){ x, y + (float)k * rh, w, rh - 4 * u }, 8 * u, 4, hexa(REGIONS[region].top, 0.35f));
      text(b, x + 20 * u, y + (float)k * rh + 6 * u, 28 * u, sel ? rgba(1, 1, 1, 1) : rgba(0.8f, 0.82f, 0.9f, 1), CW_ALIGN_LEFT);
    }
    pill("{move_up|move_down}: choose   {jump}: travel there   {attack}: stay", cs.x * 0.5f, y + (float)n * rh + 20 * u, 0.5f, 26 * u, rgba(1, 1, 1, 1),
         rgba(0.1f, 0.12f, 0.18f, 0.9f));
  }
  /* the opening lines */
  if (h->intro_t > 0) {
    float a = fminf(1, h->intro_t);
    cw_r2d_rect((cw_rect){ 0, cs.y * 0.12f, cs.x, 200 * u }, rgba(0.04f, 0.05f, 0.08f, 0.6f * a));
    text("The lanterns of the old city have gone out.", cs.x * 0.5f, cs.y * 0.12f + 24 * u, 36 * u, rgba(1, 0.92f, 0.75f, a), CW_ALIGN_CENTER);
    text("Wake the five beacons, and the heart beneath them.", cs.x * 0.5f, cs.y * 0.12f + 76 * u, 36 * u, rgba(1, 0.92f, 0.75f, a), CW_ALIGN_CENTER);
    pill("{map}: the map   {move_up}: rest at a shrine", cs.x * 0.5f, cs.y * 0.12f + 130 * u, 0.5f, 26 * u, rgba(1, 1, 1, a), rgba(0.1f, 0.12f, 0.18f, 0.8f * a));
  }
  if (s->won) {
    float a = fminf(1, h->won_t);
    cw_r2d_rect((cw_rect){ 0, 0, cs.x, cs.y }, rgba(1, 0.95f, 0.8f, 0.35f * a));
    text("THE HEART WAKES", cs.x * 0.5f, cs.y * 0.35f, 84 * u, rgba(1, 0.9f, 0.6f, a), CW_ALIGN_CENTER);
    snprintf(b, sizeof b, "the lanterns are lit again.   finds %d of %d   relics %d", sim_pickups_got(s, 0) + sim_pickups_got(s, 1) + sim_pickups_got(s, 2) + sim_pickups_got(s, 3),
             s->w->npickups, s->relics);
    text(b, cs.x * 0.5f, cs.y * 0.35f + 110 * u, 32 * u, rgba(1, 1, 1, a), CW_ALIGN_CENTER);
  }
  cw_r2d_end();
}
