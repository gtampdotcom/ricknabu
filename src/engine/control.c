/*
 * engine/control.c -- ported from xrick/src/control.c, unchanged.
 *
 * Just the three globals -- control_status is what engine/e_rick.c reads
 * every frame, and what hal/sysevt_nabu.c's sysevt_poll() writes into
 * based on the joystick. control_last/control_active are carried over
 * from the reference for source parity; nothing in this port reads them
 * yet (the TI/SDL sysevt.c used them for edge-detecting newly-pressed
 * keys, which this port's joystick-only sysevt_nabu.c doesn't need).
 */

#include "control.h"

U16 control_status = 0;
U16 control_last = 0;
U16 control_active = TRUE;

/* eof */
