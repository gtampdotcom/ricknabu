/*
 * engine/dat_ents.c -- ported from xrick/src/dat_ents.c, unchanged (data
 * only).
 *
 * Entity type table (ent_entdata[]) and sprite-sequence table
 * (ent_sprseq[]) -- pure data, no TI-specific content. Was missing from
 * the initial ents.c/maps.c port pass, which declared these extern
 * (engine/include/ents.h) and used them (ents.c's ent_actvis()) but never
 * defined them -- link-time "undefined symbol: _ent_entdata" /
 * "_ent_sprseq" errors, not a logic bug. ent_mvstep[] (xrick/src/
 * dat_ents2.c) is a separate file (engine/dat_ents2.c) -- ents.h declared
 * it extern from the start too, but nothing referenced it until
 * e_them_t3_action() (engine/e_them.c) did.
 *
 * CHANGED for the fileload build: neither table is a `const`/initialized
 * definition anymore -- both are plain (BSS) buffers, costing 0 bytes in
 * the .nabu image, filled in at startup by main.c calling
 * sys_nabu_loadAsset() against assets/ENTDATA.DAT and
 * assets/ENTSPRSQ.DAT. entdata_t is all-U16 fields, so ENTDATA.DAT
 * is packed 2 bytes/field, little-endian (tools/extract_assets.py
 * --width 2); ent_sprseq[] is flat U16 too, so ENTSPRSQ.DAT uses the same
 * --width 2 (not a byte-for-byte copy like the tile/sprite/map_bnums/
 * map_blocks/map_eflg_c .DAT files, which are all flat U8). ent_sprseq[]
 * was already declared non-const (see ents.h) even before this -- nothing
 * in this repo writes to it at runtime, but the reference source doesn't
 * mark it const either, so this build doesn't add one. See
 * hal/sys_nabu_load.h and assets/README.md for the full rationale.
 */

#include "ents.h"

entdata_t ent_entdata[ENT_NBR_ENTDATA];
U16 ent_sprseq[ENT_NBR_SPRSEQ];

/* eof */
