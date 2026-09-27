/*
 * hal/sys_nabu.h
 *
 * NABU-specific additions to ricksystem.h's sys_* contract: the one
 * vblank-synced timer every paced loop in the game uses (sys_nabu.c's own
 * comment).
 */

#ifndef _SYS_NABU_H
#define _SYS_NABU_H

#include "ricksystem.h"

/* Waits out the rest of a period of periodMs (rounded up to whole 16.7ms
 * vblanks; 0 = don't wait), synced to the VDP's 60Hz vblank -- time already
 * spent on the loop's own work counts toward it. Returns the number of
 * vblanks the period actually took. See sys_nabu.c's own comment. */
U8 sys_nabu_waitFrame(U16 periodMs);

/* Counts a vblank toward the current frame if one has happened since the
 * last check. Call only BETWEEN complete VDP operations (it reads the VDP
 * status register, which resets the VDP's address latch). */
void sys_nabu_pollVblank(void);

#endif /* _SYS_NABU_H */
