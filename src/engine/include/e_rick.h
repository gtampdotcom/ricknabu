/*
 * engine/include/e_rick.h -- ported from xrick/include/e_rick.h, unchanged.
 * Rick's own state flags/API; no TI-specific content.
 */

#ifndef _E_RICK_H
#define _E_RICK_H

#include "ricksystem.h"

#define E_RICK_NO 1
#define E_RICK_ENT ent_ents[E_RICK_NO]

extern U16 e_rick_state, e_rick_atExit;
extern U16 e_rick_stop_x, e_rick_stop_y;

#define E_RICK_STSTOP 0x01
#define E_RICK_STSHOOT 0x02
#define E_RICK_STCLIMB 0x04
#define E_RICK_STJUMP 0x08
#define E_RICK_STZOMBIE 0x10
#define E_RICK_STDEAD 0x20
#define E_RICK_STCRAWL 0x40

#define E_RICK_STSET(X) e_rick_state |= (X)
#define E_RICK_STRST(X) e_rick_state &= ~(X)
#define E_RICK_STTST(X) (e_rick_state & (X))

extern void e_rick_save(void);
extern void e_rick_restore(void);
extern void e_rick_action(U16);
extern void e_rick_gozombie(void);
extern U16 e_rick_boxtest(U16);

#endif

/* eof */
