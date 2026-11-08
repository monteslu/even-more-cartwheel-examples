/* lanternfall: the HUD (hud.h). Text holds prompts through the engine's markup ({jump}, {dash} ...): the text says
 * what a button does, the glyph says which. Sizes are on the 1080-line scale (u) and never under the body floor. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/prompt.h"
#include "hud.h"
#include "scenery.h"
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

static const char *const CARD_TITLE[PK_KINDS] = { "", "HEART LEAF", "LANTERN OIL", "OLD RELIC", "MAP STONE", "A TABLET" };
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

static bool seen_room(const sim *s, int r) { return (s->seen[r >> 3] >> (r & 7)) & 1; }
/* a room is on the map once seen, or (faint) once its region's map stone is found */
static bool mapped_room(const sim *s, int r) { return ((s->maps >> s->w->rooms[r].region) & 1) != 0; }

/* the map, drawn as a painted chart (scenery.c: vellum, a gold border, a compass rose): each region you know a soft wash
 * of its colour, its landmark drawn small where it stands; the rooms seen fill in as you go (the rooms of a region whose map stone you hold show faint before you
 * see them); doors; each region's landmark; shrines and wardens; the finds you have SEEN: a pulsing ring round a
 * dot while not yet reached (a reward seen across a gap waits there for the ability that reaches it), a faint ring
 * once taken; you */
static void draw_map(const sim *s, const hud_state *h, float t, cw_vec2 cs, float u) {
  const world *w = s->w;
  scenery_map_paper(cs.x, cs.y, 0.97f);
  /* the grid's used span */
  int gx0 = GW, gy0 = GH, gx1 = 0, gy1 = 0;
  for (int r = 0; r < NREGIONS; r++) {
    const region_info *g = &REGIONS[r];
    if (g->x < gx0) gx0 = g->x;
    if (g->y < gy0) gy0 = g->y;
    if (g->x + g->w > gx1) gx1 = g->x + g->w;
    if (g->y + g->h > gy1) gy1 = g->y + g->h;
  }
  float top = 110 * u, bottom = 120 * u;
  float cell = fminf((cs.x - 80 * u) / (float)(gx1 - gx0), (cs.y - top - bottom) / (float)(gy1 - gy0));
  float ox = (cs.x - cell * (float)(gx1 - gx0)) * 0.5f - (float)gx0 * cell, oy = top - (float)gy0 * cell;
  float th = cell * (1.0f / CELL_H); /* one tile in map units, vertically */
  float pulse = 0.5f + 0.5f * sinf(t * 5);
  /* a wash of each known region's colour under its rooms, as watercolour on the chart */
  for (int r = 0; r < NREGIONS; r++) {
    int x0 = GW, y0 = GH, x1 = -1, y1 = -1;
    for (int i = 0; i < w->nrooms; i++) {
      const room *rm = &w->rooms[i];
      if (rm->region != r || (!seen_room(s, i) && !mapped_room(s, i))) continue;
      if (rm->x < x0) x0 = rm->x;
      if (rm->y < y0) y0 = rm->y;
      if (rm->x + rm->w > x1) x1 = rm->x + rm->w;
      if (rm->y + rm->h > y1) y1 = rm->y + rm->h;
    }
    if (x1 < 0) continue;
    for (int k = 0; k < 3; k++) { /* (three soft layers: a wash that fades out past the rooms) */
      float pad = (float)(22 - 8 * k) * u;
      cw_r2d_round_rect((cw_rect){ ox + (float)x0 * cell - pad, oy + (float)y0 * cell - pad, (float)(x1 - x0) * cell + pad * 2, (float)(y1 - y0) * cell + pad * 2 }, pad + cell * 0.4f, 10,
                        hexa(REGIONS[r].top, 0.06f));
    }
  }
  for (int i = 0; i < w->nrooms; i++) {
    bool seen = seen_room(s, i);
    if (!seen && !mapped_room(s, i)) continue;
    const room *r = &w->rooms[i];
    cw_color c = hexa(REGIONS[r->region].top, seen ? 0.85f : 0.35f);
    cw_rect rc = { ox + (float)r->x * cell + 1.5f * u, oy + (float)r->y * cell + 1.5f * u, (float)r->w * cell - 3 * u, (float)r->h * cell - 3 * u };
    if (seen) cw_r2d_rect(rc, rgba(c.r * 0.45f, c.g * 0.45f, c.b * 0.45f, 0.95f));
    cw_r2d_rect_lines(rc, (seen ? 2 : 1.2f) * u, c);
    if (r->kind == RK_GATE && r->gate >= 1 && r->gate <= 6) { /* the obstacle across a gate room: the ability it wants, by colour and word */
      static const char *const WORD[7] = { "", "jump", "dash", "climb", "plunge", "tide", "line" };
      static const uint32_t COL[7] = { 0, 0x9fd8ff, 0xffa060, 0x8fe08a, 0xff7070, 0x60e8d8, 0xffd860 };
      cw_vec2 m = { rc.x + rc.w * 0.5f, rc.y + rc.h * 0.5f };
      cw_color gc = hexa(COL[r->gate], seen ? 0.95f : 0.45f);
      cw_r2d_circle_lines(m, cell * 0.2f, 2.5f * u, 10, gc);
      text(WORD[r->gate], m.x, m.y - cell * 0.2f - 26 * u, 22 * u, gc, CW_ALIGN_CENTER);
    }
    if (i == s->room) cw_r2d_rect(rc, rgba(1, 1, 1, 0.08f + 0.08f * (0.5f + 0.5f * sinf(t * 3)))); /* (the room you are in: a soft swell, never a blink) */
  }
  /* doors of seen rooms: gaps of light across the wall */
  for (int i = 0; i < w->ndoors; i++) {
    const door *d = &w->doors[i];
    if (!seen_room(s, d->a) && !seen_room(s, d->b)) continue;
    float x = ox + ((float)d->x + (d->vertical ? (float)d->len * 0.5f : 2)) / CELL_W * cell, y = oy + ((float)d->y + (d->vertical ? 2 : 2)) * th;
    cw_color c = d->pad == T_SEAL ? rgba(1, 0.8f, 0.35f, 1) : d->pad == T_BOSSDOOR ? rgba(0.95f, 0.45f, 1, 1) : rgba(0.95f, 0.95f, 0.85f, 1);
    if (d->vertical) cw_r2d_rect((cw_rect){ x - cell * 0.14f, y - 2 * u, cell * 0.28f, 4 * u }, c);
    else cw_r2d_rect((cw_rect){ x - 2 * u, y - cell * 0.12f, 4 * u, cell * 0.24f }, c);
  }
  /* landmarks (a tall diamond in the region's accent), shrines, wardens, finds */
  for (int i = 0; i < w->nents; i++) {
    const ent *e = &w->ents[i];
    bool seen = seen_room(s, e->room);
    float x = ox + e->x / CELL_W * cell, y = oy + e->y * th;
    if (ent_is_landmark(e) && (seen || mapped_room(s, e->room))) { /* the region's own beacon tower, small, its lamp a dot of its accent */
      cw_color c = hexa(REGIONS[e->arg % NREGIONS].accent, seen ? 1 : 0.5f);
      float hgt = cell * 0.62f;
      if (!scenery_landmark_icon(e->arg % NREGIONS, x, y, hgt, rgba(1, 0.95f, 0.85f, seen ? 1 : 0.5f))) {
        float r = cell * 0.26f;
        cw_vec2 p[4] = { { x, y - r * 2.2f }, { x + r * 0.6f, y - r * 1.1f }, { x, y }, { x - r * 0.6f, y - r * 1.1f } };
        cw_r2d_polygon(p, 4, c);
      } else
        cw_r2d_circle((cw_vec2){ x, y - hgt * 0.84f }, cell * 0.06f, 8, c);
    }
    if (!seen) continue;
    if (e->kind == EK_PICKUP) {
      bool got = sim_got(s, e->id);
      if (!got && !sim_pk_seen(s, e->id)) continue; /* (only what you have seen: no spoilers) */
      cw_color c = e->sub == PK_ABILITY ? rgba(1, 0.85f, 0.35f, 1) : e->sub == PK_HEALTH ? rgba(1, 0.5f, 0.55f, 1) : e->sub == PK_ENERGY ? rgba(0.5f, 0.85f, 1, 1)
                   : e->sub == PK_MAPSTONE ? rgba(0.6f, 1, 0.75f, 1) : e->sub == PK_LORE ? rgba(0.95f, 0.9f, 0.7f, 1) : rgba(0.85f, 0.65f, 1, 1);
      if (got) cw_r2d_circle_lines((cw_vec2){ x, y }, cell * 0.12f, 1.5f * u, 10, rgba(c.r, c.g, c.b, 0.45f));
      else {
        cw_r2d_circle((cw_vec2){ x, y }, cell * 0.13f, 10, c);
        cw_r2d_circle_lines((cw_vec2){ x, y }, cell * (0.22f + 0.08f * pulse), 2 * u, 14, rgba(c.r, c.g, c.b, 0.5f + 0.4f * pulse));
      }
    } else if (e->kind == EK_SAVE) {
      bool lit = (s->saves_lit[e->id >> 3] >> (e->id & 7)) & 1;
      cw_r2d_circle_lines((cw_vec2){ x, y - cell * 0.1f }, cell * 0.16f, 2.5f * u, 12, lit ? rgba(0.55f, 1, 0.9f, 1) : rgba(0.6f, 0.65f, 0.65f, 1));
    } else if (e->kind == EK_BOSS) {
      bool down = (s->bosses_down >> e->sub) & 1;
      cw_r2d_circle((cw_vec2){ x, y - cell * 0.2f }, cell * 0.2f, 12, down ? rgba(0.5f, 0.5f, 0.55f, 1) : rgba(1, 0.35f, 0.35f, 1));
    }
  }
  /* the names of the regions you have been in, over their middle */
  for (int r = 0; r < NREGIONS; r++) {
    bool any = false;
    float sx = 0, sy = 0;
    int n = 0;
    for (int i = 0; i < w->nrooms; i++)
      if (w->rooms[i].region == r && seen_room(s, i)) any = true, sx += (float)w->rooms[i].x + (float)w->rooms[i].w * 0.5f, sy += (float)w->rooms[i].y + (float)w->rooms[i].h * 0.5f, n++;
    if (!any) continue;
    text(REGIONS[r].name, ox + sx / (float)n * cell, oy + sy / (float)n * cell - 12 * u, 24 * u, hexa(REGIONS[r].top, 0.75f), CW_ALIGN_CENTER);
  }
  /* you */
  for (int i = 0; i < s->players; i++) {
    const player *p = &s->p[i];
    if (!p->active) continue;
    float x = ox + p->x / CELL_W * cell, y = oy + (p->y - 0.9f) * th;
    cw_r2d_circle((cw_vec2){ x, y }, cell * 0.22f * (1 + 0.15f * sinf(t * 8)), 14, i ? rgba(1, 0.6f, 0.85f, 1) : rgba(1, 1, 1, 1));
  }
  char b[200];
  int region = s->room >= 0 ? w->rooms[s->room].region : 0;
  text(REGIONS[region].name, cs.x * 0.5f, 30 * u, 44 * u, hexa(REGIONS[region].top, 1), CW_ALIGN_CENTER);
  int all = w->npickups, seen = 0, waiting = 0;
  for (int r = 0; r < w->nrooms; r++) seen += seen_room(s, r);
  for (int i = 0; i < w->nents; i++)
    if (w->ents[i].kind == EK_PICKUP && sim_pk_seen(s, w->ents[i].id) && !sim_got(s, w->ents[i].id)) waiting++;
  snprintf(b, sizeof b, "rooms %d of %d    finds %d of %d    seen, not yet reached %d    tablets %d    beacons %d of 5", seen, w->nrooms, sim_finds(s), all, waiting,
           sim_lore_found(s), sim_beacons(s));
  text(b, cs.x * 0.5f, cs.y - bottom + 12 * u, 26 * u, rgba(0.9f, 0.92f, 1, 1), CW_ALIGN_CENTER);
  /* the key: a pulsing ring is a find seen but not yet reached */
  float kx = cs.x * 0.5f - 260 * u, ky = cs.y - bottom + 62 * u;
  cw_r2d_circle((cw_vec2){ kx, ky }, 7 * u, 10, rgba(1, 0.85f, 0.35f, 1));
  cw_r2d_circle_lines((cw_vec2){ kx, ky }, (12 + 4 * pulse) * u, 2 * u, 14, rgba(1, 0.85f, 0.35f, 0.5f + 0.4f * pulse));
  text("seen, not yet reached", kx + 24 * u, ky - 14 * u, 24 * u, rgba(0.9f, 0.92f, 1, 0.9f), CW_ALIGN_LEFT);
  pill("{map}: close the map", cs.x * 0.5f + 200 * u, ky - 18 * u, 0.5f, 24 * u, rgba(1, 1, 1, 1), rgba(0.1f, 0.12f, 0.2f, 0.85f));
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
  if (h->banner_t > 0 && h->banner_region >= 0 && h->intro_t <= 0) {
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
    const char *title = h->card_kind >= PK_HEALTH && h->card_kind <= PK_LORE ? CARD_TITLE[h->card_kind] : "";
    if (h->card_kind == CARD_CHARM && h->card_arg >= 0 && h->card_arg < CH_KINDS) title = CHARM_NAME[h->card_arg];
    if (h->card_kind == CARD_KEEPSAKE) title = "A KEEPSAKE";
    char keepline[200];
    if (h->card_kind == CARD_KEEPSAKE) {
      if (h->card_dye > 0 && h->card_dye < NDYES) snprintf(keepline, sizeof keepline, "%s. Your scarf takes its dye: %s", KEEPSAKE_LINE[h->card_arg % NKEEPSAKE], DYE_NAME[h->card_dye]);
      else snprintf(keepline, sizeof keepline, "%s", KEEPSAKE_LINE[h->card_arg % NKEEPSAKE]);
    }
    if (h->card_kind == PK_ABILITY)
      for (int k = 0; k < NABILITIES; k++)
        if (h->card_arg == (1 << k)) title = ABILITY_NAME[k];
    char mapline[120];
    if (h->card_kind == PK_MAPSTONE) snprintf(mapline, sizeof mapline, "the rooms of %s are on your map", REGIONS[h->card_arg % NREGIONS].name);
    const char *line = h->card_kind == CARD_KEEPSAKE ? keepline : h->card_kind == CARD_CHARM ? CHARM_LINE[h->card_arg] : h->card_kind == PK_ABILITY ? ability_line(h->card_arg) : h->card_kind == PK_HEALTH ? "one more leaf of health"
                       : h->card_kind == PK_ENERGY ? "one more measure of energy" : h->card_kind == PK_MAPSTONE ? mapline : h->card_kind == PK_LORE ? LORE[h->card_arg % NLORE]
                       : "a relic of the old city";
    /* a long line in two or three, at the spaces nearest the even cuts */
    char ls[3][240];
    int n = (int)strlen(line), nl = n > 128 ? 3 : n > 64 ? 2 : 1, at = 0;
    for (int k = 0; k < nl; k++) {
      int cut = n;
      if (k < nl - 1) {
        int want = n * (k + 1) / nl;
        cut = -1;
        for (int i = at; i < n; i++)
          if (line[i] == ' ' && (cut < 0 || abs(i - want) < abs(cut - want))) cut = i;
        if (cut < 0) cut = n;
      }
      snprintf(ls[k], sizeof ls[k], "%.*s", cut - at, line + at);
      at = cut < n ? cut + 1 : n;
    }
    for (int k = nl; k < 3; k++) ls[k][0] = 0;
    float boxh = (130 + 40 * (float)nl) * u;
    cw_r2d_round_rect((cw_rect){ cs.x * 0.2f, y - 16 * u, cs.x * 0.6f, boxh }, 18 * u, 6, rgba(0.06f, 0.07f, 0.12f, 0.78f * a));
    text(title, cs.x * 0.5f, y, 56 * u, rgba(1, 0.88f, 0.5f, a), CW_ALIGN_CENTER);
    for (int k = 0; k < 3; k++) {
      const char *ln = ls[k];
      if (!ln[0]) continue;
      cw_vec2 sz = cw_prompt_text_size(F, ln, -1, 30 * u);
      cw_prompt_text(F, ln, -1, (cw_vec2){ cs.x * 0.5f - sz.x * 0.5f, y + (80 + 40 * (float)k) * u }, 30 * u, rgba(1, 1, 1, a), CW_ALIGN_LEFT);
    }
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
    snprintf(b, sizeof b, "the lanterns are lit again.   finds %d of %d   relics %d", sim_finds(s),
             s->w->npickups, s->relics);
    text(b, cs.x * 0.5f, cs.y * 0.35f + 110 * u, 32 * u, rgba(1, 1, 1, a), CW_ALIGN_CENTER);
  }
  cw_r2d_end();
}
