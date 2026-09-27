/*
 * engine/dat_tilescTI.c -- ported from xrick/src/dat_tilescTI.c.
 * TMS9918A tile color bytes for the three 9918A tile banks -- chip format,
 * identical on NABU's real TMS9918A.
 *
 * CHANGED from the original port: this used to be a `const` initializer
 * with all 6144 bytes compiled straight into the .nabu image. It's now an
 * uninitialized (BSS) buffer that costs 0 bytes there, filled in at
 * startup by main.c calling sys_nabu_loadAsset() (hal/sys_nabu_load.c)
 * against assets/TILECOL.DAT -- see that header for why NABU, unlike
 * the TI-99 build, can't afford to just compile every asset table in.
 * TILECOL.DAT is a byte-for-byte copy of this array's original
 * initializer (see tools/extract_assets.py, which is how it was produced,
 * and assets/README.md for the load-time filename convention).
 *
 * tiles_banks_col[] is read (never written) by everything downstream --
 * engine/tiles.c -- so dropping `const` here doesn't change how it's used,
 * only where its contents come from.
 *
 * SIZE PASS: the actual storage for tiles_banks_col[] (and
 * tiles_banks_pat[]/tilesf18_patA/colA/patB/colB) now lives in one shared
 * union, tiles_banks_shared (defined in engine/tiles.c) -- see tiles.h's
 * own comment for why. tiles_banks_col here is just a macro over a member
 * of that union; nothing in this file needs to change to keep using it.
 */

#include "tiles.h"

/* eof */
