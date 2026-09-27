/*
 * engine/dat_tilespTI.c -- ported from xrick/src/dat_tilespTI.c
 *
 * Change from the TI source: the original has BOTH an F18A tiles_banks_pat
 * (compile-time #ifdef F18A block, lines 20-405 of the original) and a plain
 * 9918A one (#else block) in the same file, selected at compile time. Same
 * policy as every other F18A compile-time branch in this port (see
 * config.h/game.h/tiles.h) -- this is a single runtime-branched
 * binary, so only the 9918A data is kept here.
 *
 * CHANGED again since then: this used to be a `const` initializer with all
 * 6144 bytes compiled straight into the .nabu image. It's now an
 * uninitialized (BSS) buffer that costs 0 bytes there, filled in at
 * startup by main.c calling sys_nabu_loadAsset() (hal/sys_nabu_load.c)
 * against assets/TILEPAT.DAT -- see that header for why NABU, unlike
 * the TI-99 build, can't afford to just compile every asset table in.
 * TILEPAT.DAT is a byte-for-byte copy of this array's original
 * initializer (see tools/extract_assets.py, which is how it was produced,
 * and assets/README.md for the load-time filename convention).
 *
 * tiles_banks_pat[] is read (never written) by everything downstream --
 * engine/tiles.c -- so dropping `const` here doesn't change how it's used,
 * only where its contents come from.
 *
 * SIZE PASS: the actual storage for tiles_banks_pat[] (and
 * tiles_banks_col[]/tilesf18_patA/colA/patB/colB) now lives in one shared
 * union, tiles_banks_shared (defined in engine/tiles.c) -- see tiles.h's
 * own comment for why. tiles_banks_pat here is just a macro over a member
 * of that union; nothing in this file needs to change to keep using it.
 */

#include "tiles.h"

/* eof */
