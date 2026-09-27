/*
 * engine/include/fb.h -- ported from xrick/include/fb.h, unchanged.
 */

#ifndef _FB_H
#define _FB_H

#include "ricksystem.h"
#include "config.h"

// cells instead of pixels, but we'll maintain the code's pixel assumptions
#define FB_WIDTH 256
#define FB_HEIGHT 192

/*
 * returns the fb pointer at <x>, <y>.
 * <x>, <y> are fb-coordinates.
 */
#define fb_at(x,y) (((y)/8)*32+((x)/8))

/*
 * clears the frame buffer
 */
extern void fb_clear(void);

#endif

/* eof */
