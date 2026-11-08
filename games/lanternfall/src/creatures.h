/* lanternfall: how every creature looks (creatures.c). Each is a skeleton of painted parts (paint.c: colour, normal
 * and material maps, made in code at start-up), posed every frame from the simulation's state: idle and moving, the
 * tell (an exaggerated pose and a glow that grows as the attack nears), the act, the hurt flash, and death (the parts
 * come apart and burn out). Render code only: nothing here feeds the simulation. */
#ifndef LANTERNFALL_CREATURES_H
#define LANTERNFALL_CREATURES_H

#include "cartwheel/render2d.h"
#include "sim.h"

#define EN_KINDS_PAINTED EN_KINDS /* (the kinds painted; a kind added after them keeps its placeholder shapes until it is) */
void creatures_init(void);
/* into the current (lit) layer */
void creature_enemy(const enemy *e, int region, float t);
void creature_boss(const boss *b, int region, float t);
void creature_hero(const player *p, int i, int region, float t); /* the hooded lamplighter, player i */
/* the deaths of the tick (EV_KILL, EV_BOSS_DOWN): remnants that fall apart and burn out */
void creatures_events(const sim *s, int region);
void creatures_tick(float dt);
void creatures_draw_remnants(float t);
/* glows over the lit scene (the additive layer): eyes and glands during tells */
void creatures_fx(const sim *s, float t);

/* from view.c */
void view_light(float x, float y, float r, cw_color c, float e, bool shadows);
void view_emit(float x, float y, float vx, float vy, float life, float size, cw_color c, bool add, float grav);

#endif
