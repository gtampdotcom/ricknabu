/*
 * engine/dat_spritesTI0.c -- ported from xrick/src/dat_spritesTI0.c.
 * Raw TMS9918A sprite pattern bytes (8 bytes per 8x8 pattern), identical
 * on NABU's real TMS9918A/F18A -- "TI" in the filename is about where the
 * tool that generated it lived, not the byte format itself. Sprite
 * numbers 0-47 (page 0) only; every frame number engine/dat_screens.c's
 * screen_imapsl[] references for the map intro's walk cycle is under 48,
 * so pages 1-4 (dat_spritesTI1..4.c, gameplay's larger sprite set) aren't
 * ported yet -- add them the same way once something needs sprite
 * numbers >= 48 (see hal/sys_nabu_load.h for what "the same way" means
 * now that this port loads sprite/tile data at runtime instead of
 * compiling it in).
 *
 * CHANGED from the original port: this used to be a `const` initializer
 * with all 6144 bytes compiled straight into the .nabu image. It's now an
 * uninitialized (BSS) buffer that costs 0 bytes there, filled in at
 * startup by main.c calling sys_nabu_loadAsset() (hal/sys_nabu_load.c)
 * against assets/SPR0.DAT -- a byte-for-byte copy of this array's
 * original initializer (see tools/extract_assets.py, which is how it was
 * produced, and assets/README.md for the load-time filename
 * convention). This is really just reverting to the original PC xrick's
 * own approach -- see engine/include/sprites.h's "PC version" comment:
 * sprite data there was always meant to live in a file ('sprites.bin')
 * loaded at startup, not compiled in; the TI port only diverged from that
 * because cartridge banking made compiling it in free.
 *
 * sprites_data0[] is read (never written) by everything downstream --
 * engine/sprites.c -- so dropping `const` here doesn't change how it's
 * used, only where its contents come from.
 *
 * Original copyright (C) 1998-2019 bigorno (bigorno@bigorno.net); see
 * xrick/README for the license this data is distributed under.
 */

#include "config.h"

#include "ricksystem.h"
#include "sprites.h"

// TI sprites are ordered:
//     02
//     13
// Rick sprites are 2 sprites x 2 sprites, and I guess we'll order the same way
// (down, then over)
//
// So to make that work, my tool has converted the sprites that way from the
// pattern table.
/* Sized via SPRITES_DATA0_RESIDENT_SIZE (hal/sysvid_nabu.h): 1 byte
 * when SKIP_SPR0_GAMEPLAY_LOAD skips its load, else the full 6144. */
sprite_t sprites_data0[SPRITES_DATA0_RESIDENT_SIZE];

/* eof */
