/*
 * engine/include/e_sbonus.h -- ported from xrick/include/e_sbonus.h, unchanged.
 * No TI-specific content found.
 */

#ifndef _E_SBONUS_H
#define _E_SBONUS_H

#include "ricksystem.h"

extern U16 e_sbonus_counting;
extern U16 e_sbonus_counter;
extern U16 e_sbonus_bonus;

extern void e_sbonus_start(U16);
extern void e_sbonus_stop(U16);

#endif

/* eof */
