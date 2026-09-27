/*
 * engine/dat_maps2.c -- adapted from xrick/data/dat_maps.c
 *
 * map_bnums[] truncated from the real xrick 8152 entries to the first 2048
 * (indices 0-2047) -- every bnum any of map 1/SAMERICA's 9 submaps can
 * reach via map_chain(), see maps.h's MAP_NBR_BNUMS comment for exactly how
 * that range was worked out. (STALE UNTIL FIXED: MAP_NBR_BNUMS was widened
 * from 104 to 2048 to support that 9-submap traversal, but assets/
 * MAPBNUM.DAT was never regenerated to match -- it stayed the old 104-byte
 * file, which sys_nabu_loadAsset() doesn't catch because the Internet
 * Adapter silently pads a too-short read out to the requested length
 * instead of returning a short count, so `got == size` looks like success.
 * That left map_bnums[104..2047] full of whatever the IA happened to serve
 * past end-of-file -- a real, reproducible cause of "garbled tile at a
 * fixed map position" bugs. Regenerated via tools/extract_assets.py against
 * xrick/src/dat_maps2.c's real array, truncated to the first 2048 bytes.)
 *
 * CHANGED for the fileload build: no longer a `const` initializer --
 * declared as a plain (BSS) buffer, costing 0 bytes in the .nabu image,
 * and filled in at startup by main.c calling sys_nabu_loadAsset() against
 * assets/MAPBNUM.DAT (a byte-for-byte copy of this array's original
 * 104-byte initializer -- map_bnums[] is flat U8, so unlike
 * dat_maps1.c/dat_maps3.c/dat_ents.c's U16-field struct tables it didn't
 * need tools/extract_assets.py's --width 2). See hal/sys_nabu_load.h and
 * assets/README.md for the full rationale.
 */

#include "maps.h"
#include "tiles.h"

U8 map_bnums[MAP_NBR_BNUMS];

/* eof */
