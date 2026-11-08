/* pinball's backbox (backbox.c): the lit picture over the dot display that every table has, painted in code from the
 * table's own theme (a creature of the deep, a forge ring pouring suns, a fair's wheel and striker), and the panel
 * that holds it beside the table */
#ifndef PINBALL_BACKBOX_H
#define PINBALL_BACKBOX_H
#include "cartwheel/render2d.h"
#include "game.h"

#define BB_W 640
#define BB_H 400

void backbox_table(const table *T);   /* paint this table's picture (once per table) */
cw_texture backbox_texture(void);
uint8_t *backbox_paint(const table *T); /* malloc'd BB_W x BB_H RGBA (also the launcher art's tool) */

#endif
