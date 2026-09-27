/*
 * engine/dat_ents2.c -- ported from xrick/src/dat_ents2.c, unchanged
 * (data only): the move-step table (ent_mvstep[]) that e_them_t3_action()
 * (engine/e_them.c) plays back to run an entity through a scripted
 * sequence of x/y deltas -- e.g. the boulder in map 1/submap 0 that wakes
 * up and rolls toward Rick once triggered.
 *
 * CHANGED for the fileload build, same as dat_ents.c: not a `const`/
 * initialized definition -- a plain (BSS) buffer, costing 0 bytes in the
 * .nabu image, filled in at startup by main.c calling
 * sys_nabu_loadAsset() against assets/MVSTEP.DAT. mvstep_t is all
 * U16/S16 fields (2 bytes each), so MVSTEP.DAT is packed 2 bytes/field,
 * little-endian (tools/extract_assets.py --width 2, same as ENTDATA.DAT/
 * ENTSPRSQ.DAT) -- see hal/sys_nabu_load.h for the full rationale.
 *
 * ents.h's extern declaration for this was `const mvstep_t
 * ent_mvstep[...]` before this file existed; dropped the `const` here to
 * match the fileload pattern (a BSS buffer can't be const) -- update that
 * declaration alongside this file, not separately.
 */

#include "config.h"
#include "ents.h"

mvstep_t ent_mvstep[ENT_NBR_MVSTEP];

/* eof */
