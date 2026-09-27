/*
 * engine/include/scroller.h -- ported from xrick/include/scroller.h
 *
 * vdpmemcpy2() itself is implemented in hal/sysvid_nabu.c: a plain
 * sequential RAM-to-VRAM copy (set write address once, stream bytes to
 * IO_VDPDATA, which auto-increments) -- same primitive bitmapcharcopy()
 * already uses, just without bitmapcharcopy's three-way table replication.
 */

#ifndef _SCROLLER_H
#define _SCROLLER_H

#include "ricksystem.h"

#define SCROLL_RUNNING 1
#define SCROLL_DONE 0

/* BUG FIXED: this is xrick's own unchanged constant (24 ticks/scroll-step,
 * same unit as GAME_PERIOD), but hal/sys_nabu.c's sys_nabu_tickDelay() --
 * what a "tick" actually costs in wall-clock time -- got retimed (halved)
 * for overall game speed per direct testing feedback. That sped up
 * scroll_up()/scroll_down()'s own step-to-step pacing by the same factor,
 * and a screen recording caught the result: what should be a gradual,
 * multi-frame reveal as the camera follows a fall now completes within a
 * single video frame (<33ms for all 8 steps) -- Rick and the boulder
 * don't visibly move, but the whole background jumps at once, reading as
 * a "teleport" even though the underlying map_frow/entity-position math
 * checks out (see util.c/main.c's own comments on the *other*,
 * already-fixed teleport causes -- this one is pacing, not state).
 * Doubled to compensate and restore roughly the original wall-clock
 * duration per step; retune the same way if it's still too fast/slow. */
#define SCROLL_PERIOD 24

void scroll_init(void);
extern U16 scroll_up(void);
extern U16 scroll_down(void);
extern void vdpmemcpy2(U16 dest, const U8* src, U16 cnt);

#endif

/* eof */
