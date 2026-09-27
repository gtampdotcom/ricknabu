/*
 * engine/dat_maps3.c -- adapted from xrick/data/dat_maps.c
 *
 * map_marks[]/map_marks_ent[] truncated from the real xrick 523 entries
 * to just the 5 (indices 0-4) that make up submap 0's own mark list, end-
 * of-list sentinel (row==0xff) included -- the only submap this build
 * ever loads, see maps.c's map_loadFirst(). ent_actvis()/map_resetMarks()
 * (ents.c/maps.c) never walk past that sentinel, so nothing past index 4
 * is ever read. See maps.h's MAP_NBR_MARKS comment for the full
 * reasoning. map_eflg_c[] below is untouched -- both its page-0 and
 * page-1 halves are small enough (32 bytes total) that trimming it
 * wasn't worth the risk of miscounting map_eflg_expand()'s variable-
 * length encoding.
 *
 * CHANGED for the fileload build: map_marks[]/map_eflg_c[] are no longer
 * `const` initializers -- both are plain (BSS) buffers now, costing 0
 * bytes in the .nabu image, filled in at startup by main.c calling
 * sys_nabu_loadAsset() against assets/MAPMARK.DAT and
 * assets/MAPEFLG.DAT. MAPMARK.DAT is mark_t's 5 U16 fields packed 2
 * bytes/field, little-endian (tools/extract_assets.py --width 2);
 * MAPEFLG.DAT is a plain byte-for-byte copy (map_eflg_c[] is flat U8, no
 * --width needed). map_marks_ent[] was already a BSS buffer -- it's
 * runtime-only state (map_resetMarks() fills it from map_marks[].ent each
 * time a submap loads), never loaded from a file. See hal/sys_nabu_load.h
 * and assets/README.md for the full rationale.
 */

#include "maps.h"
#include "tiles.h"

U16 map_marks_ent[MAP_NBR_MARKS];

mark_t map_marks[MAP_NBR_MARKS];

U8 map_eflg_c[MAP_NBR_EFLGC];

/* eof */
