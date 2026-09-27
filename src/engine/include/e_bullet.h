/*
 * engine/include/e_bullet.h -- ported from xrick/include/e_bullet.h, unchanged.
 * No TI-specific content found.
 */

#ifndef _E_BULLET_H
#define _E_BULLET_H

#include "ricksystem.h"

#define E_BULLET_NO 2
#define E_BULLET_ENT ent_ents[E_BULLET_NO]

extern S16 e_bullet_offsx;
extern U16 e_bullet_xc, e_bullet_yc;

extern void e_bullet_init(U16, U16);
extern void e_bullet_action(U16);

#endif

/* eof */
