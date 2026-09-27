/*
 * engine/include/sysvid.h -- ported from xrick/include/sysvid.h
 *
 * This is the portable HAL contract engine files include (`#include
 * "sysvid.h"`), kept under its original name so ported files don't need
 * their includes touched. hal/sysvid_nabu.c already implements exactly
 * these three functions (same names, same signatures) plus the NABU-only
 * init/detection extras declared separately in hal/sysvid_nabu.h -- that
 * header is for main.c and other NABU glue, this one is for engine code.
 *
 * Change from the TI source: dropped `#ifdef F18A / void set_halfbitmap();
 * void set_fullbitmap();` -- those are F18A-only VDP mode switches for a
 * feature (the F18A path splitting the title image across banks) that
 * doesn't apply here (hal/sysvid_nabu.h declares this port's own).
 */

#ifndef _SYSVID_H
#define _SYSVID_H

#include "config.h"
// was originally 255 in original game (to allow fade in from 0-255)
#define GAMMA_ON 1
#define GAMMA_OFF 0

extern void sysvid_update(void);
extern void sysvid_setGamma(U16 g);
extern void bitmapcharcopy(U16 adr, const U8* buf, U16 size);

#endif /* _SYSVID_H */

/* eof */
