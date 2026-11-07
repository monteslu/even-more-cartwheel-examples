/* lanternfall: the HUD, the map and the shrine's menu (hud.c). */
#ifndef LANTERNFALL_HUD_H
#define LANTERNFALL_HUD_H

#include "cartwheel/render2d.h"
#include "sim.h"

typedef struct hud_state {
  int banner_region;      /* the region whose name shows (-1 none) */
  float banner_t;
  int card_kind, card_arg; /* the pickup card */
  float card_t;
  bool map_open;
  bool menu_open;         /* the shrine: rest, or travel to another lit shrine */
  int menu_sel, menu_n;
  int menu_ents[64];
  float intro_t;          /* the opening lines */
  float boss_t;           /* the warden's name rises */
  int boss_idx;
  float won_t;
} hud_state;

void hud_init(cw_font font);
void hud_draw(const sim *s, const hud_state *h, float t);
void hud_menu_build(const sim *s, hud_state *h);

#endif
