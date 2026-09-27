/*
 * engine/dat_maps1.c -- adapted from xrick/data/dat_maps.c
 *
 * map_submaps[] covers all 9 of map 1/SAMERICA's own submaps (indices
 * 0-8) -- see maps.h's MAP_NBR_SUBMAPS comment for the full reasoning
 * (short version: map_chain() now really switches env_submap between
 * them, so every submap it can land on needs its own real entry).
 *

 * map_connect[] is back to its full real xrick size/content (153 entries,
 * every map/submap's connectors, not just submap 0's) now that map_chain()
 * (maps.c) actually reads it -- see maps.h's MAP_NBR_CONNECT comment for
 * why this one wasn't re-truncated to submap 0 only the way the others
 * still are.
 *
 * CHANGED for the fileload build: neither array is a `const` initializer
 * any more -- both are plain (BSS) buffers, costing 0 bytes in the .nabu
 * image, and filled in at startup by main.c calling sys_nabu_loadAsset()
 * against assets/MAPSUB.DAT and assets/MAPCONN.DAT (STALE UNTIL
 * FIXED: MAPSUB.DAT itself was never regenerated when MAP_NBR_SUBMAPS grew
 * from 1 to 9 -- it stayed the old 8-byte, submap-0-only file, silently
 * padded out to 72 bytes by the Internet Adapter on every load; see
 * dat_maps2.c's own comment for the general mechanism and why
 * sys_nabu_loadAsset() didn't catch it. Regenerated via
 * tools/extract_assets.py --width 2 against xrick/src/dat_maps1.c's real
 * array, truncated to the first 9 entries/72 bytes) -- see
 * hal/sys_nabu_load.h and assets/README.md for the full rationale
 * (same one tiles/sprites already use). Both .DAT files are submap_t's/
 * connect_t's U16 fields packed 2 bytes/field, little-endian
 * (tools/extract_assets.py --width 2), not a straight byte-for-byte copy
 * like the tile/sprite .DAT files -- see that script's own header for why
 * struct tables need --width 2 and flat byte tables (map_bnums,
 * map_blocks, map_eflg_c) don't. MAPCONN.DAT was generated straight from
 * xrick's own upstream dat_maps1.c (the last place this repo had
 * map_connect[]'s full real content as a const initializer to extract
 * from), not from a copy kept in this file.
 */

#include "maps.h"
#include "tiles.h"

submap_t map_submaps[MAP_NBR_SUBMAPS];

connect_t map_connect[MAP_NBR_CONNECT];

/* eof */
