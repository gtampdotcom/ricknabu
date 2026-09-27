/*
 * engine/include/e_bomb.h -- ported from xrick/include/e_bomb.h, unchanged.
 * No TI-specific content found.
 */

#ifndef _E_BOMB_H
#define _E_BOMB_H

#include "ricksystem.h"

#define E_BOMB_NO 3
#define E_BOMB_ENT ent_ents[E_BOMB_NO]
#define E_BOMB_TICKER (0x2D)

extern U16 e_bomb_lethal;
extern U16 e_bomb_ticker;
extern U16 e_bomb_xc;
extern U16 e_bomb_yc;

extern U16 e_bomb_hit(U16);
extern void e_bomb_init(U16, U16);
extern void e_bomb_action(U16);

#endif

/* eof */
