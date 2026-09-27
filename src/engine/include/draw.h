/*
 * engine/include/draw.h -- ported from xrick/include/draw.h, unchanged.
 * No TI-specific content found.
 */

#ifndef _DRAW_H
#define _DRAW_H

#include "ricksystem.h"

/* map coordinates of the screen */
#define DRAW_XYMAP_SCRLEFT (-0x0020)
#define DRAW_XYMAP_SCRTOP (0x0040)
/* map coordinates of the top of the hidden bottom of the map */
#define DRAW_XYMAP_HBTOP (0x0100)

/* x-position of the fb, expressed in map-coordinates */
#define MAPS_FBX (-0x0020)
/* y-position of the fb, expressed in map-coordinates */
#define MAPS_FBY (0x0040)
/* y-position of the top of the hidden botton of the map, expressed in FIXME */
#define MAPS_FBB (0x0100)

#endif /* _DRAW_H */

/* eof */
