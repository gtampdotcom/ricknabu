/*
 * engine/dat_maps4.c -- adapted from xrick/data/dat_maps.c
 *
 * map_blocks[] truncated from the real xrick 256 entries to the first 127
 * (indices 0-126) -- the highest block id any of map 1/SAMERICA's 9
 * submaps ever reference, now that map_bnums[] covers all of them -- see
 * maps.h's MAP_NBR_BLOCKS comment for exactly how that range was worked
 * out.
 *
 * CHANGED for the fileload build: no longer a `const` initializer --
 * declared as a plain (BSS) buffer, costing 0 bytes in the .nabu image,
 * and filled in at startup by main.c calling sys_nabu_loadAsset() against
 * assets/MAPBLK.DAT -- block_t is flat U8[0x10], so unlike
 * dat_maps1.c/dat_maps3.c/dat_ents.c's U16-field struct tables it didn't
 * need tools/extract_assets.py's --width 2. See hal/sys_nabu_load.h and
 * assets/README.md for the full rationale.
 *
 * (STALE UNTIL FIXED: this used to be truncated to 119 entries/1904 bytes,
 * matching an older, smaller MAP_NBR_BLOCKS -- widened to 127/2032 bytes
 * alongside map_bnums[]'s own widening, but assets/MAPBLK.DAT wasn't
 * regenerated to match at the same time. See dat_maps2.c's own comment for
 * the full mechanism this caused -- same bug, same fix, same file.)
 */

#include "maps.h"
#include "tiles.h"

block_t map_blocks[MAP_NBR_BLOCKS];

/* eof */
