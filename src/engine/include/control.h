/*
 * engine/include/control.h -- ported from xrick/include/control.h,
 * unchanged. Just the CONTROL_* input bits and the globals hal/sysevt_nabu.c
 * writes them into; no TI-specific content.
 */

#ifndef _CONTROL_H
#define _CONTROL_H

#include "ricksystem.h"

#define CONTROL_UP 0x08
#define CONTROL_DOWN 0x04
#define CONTROL_LEFT 0x02
#define CONTROL_RIGHT 0x01
#define CONTROL_PAUSE 0x80
#define CONTROL_END 0x40
#define CONTROL_EXIT 0x20
#define CONTROL_FIRE 0x10

extern U16 control_status;
extern U16 control_last;
extern U16 control_active;

#endif

/* eof */
