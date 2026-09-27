/*
 * engine/include/ecm_cache.h -- one generic, reusable rolling (LRU)
 * cache of fixed-size blocks, shared by every "stream a pattern/plane
 * from a named .DAT file on demand, but keep the hottest N resident so
 * repeat access costs no HCCA round-trip" mechanism in this engine.
 *
 * this ONE struct + rolling_cache_get()
 * replaces what used to be several independent, hand-duplicated
 * tag/age/find/claim implementations:
 *   - four ALWAYS-RESIDENT SAMERICA categories (rick/spearguy/misc0/
 *     proj -- 94KB of compiled-in `const` byte arrays, since removed) plus engine/sprites.c's own ecm_cache_lookup()/
 *     ecm_cache_fill()/ecm_cache_copy() dispatch for them,
 *   - the per-submap "swap" mechanism (ecm_swap_load(), a bespoke bulk
 *     buffer + a 16-byte-bitmask compressed format + a per-submap
 *     offset table, engine/dat_ecmSwapSamerica.c/ecm_cache_decompress()),
 *   - sprites_map1_extra_cached[]/_cache_tag[]/_cache_age[] and its own
 *     sprites_extraCache_find()/_claimSlot(),
 *   - sprites_page0_cached[]/_cache_tag[]/_cache_age[] and its own
 *     sprites_page0Cache_find()/_claimSlot() (byte-for-byte the same
 *     logic as the previous bullet, just duplicated).
 *
 * All four of those existed for the exact same reason (avoid re-fetching
 * a pattern over HCCA every time gameplay redraws it) and all four
 * already had access to the SAME underlying fact: every sprite pattern
 * this engine ever draws is just a fixed-size block at some byte offset
 * in some named asset file. One cache implementation, parameterized by
 * (filename, offset, tag), covers all of them. Callers now differ only
 * in WHICH tag space they use, how big their own block is, and how many
 * slots they're willing to spend RAM on -- all just numbers passed in
 * at each call site, not separate code.
 *
 * BLOCK SIZE MADE PER-INSTANCE (not always SPRITE_SIZE), on request
 * ("would a few large .dat files instead of many smaller files offer
 * any negatives/positives to speed"): engine/sprites.c's ECM plane
 * fetch used to be 3 separate SPRITE_SIZE(128)-byte rolling_cache_get()
 * calls per sprite (one per color plane, 3 separate HCCA round-trips
 * for a single cold miss) purely because this cache only ever dealt in
 * SPRITE_SIZE blocks. Now that the 3 planes for one physical slot are
 * meant to live INTERLEAVED together in one combined asset file
 * (SPRITE_PLANES_SIZE = 3*SPRITE_SIZE bytes per slot, sprites.h), one
 * rolling_cache_get() call can fetch all 3 planes in a single
 * round-trip -- but a page0 stock pattern is still genuinely only
 * SPRITE_SIZE bytes, so the two can no longer share one pool sized for
 * either use uniformly. Each rolling_cache_t instance now carries its
 * OWN blockSize instead of a project-wide constant.
 *
 * Backing storage (slots/tag/age) is caller-owned, not allocated here --
 * this project has no heap, and every resident array in it is a visible,
 * individually tunable byte cost at its own declaration site (matching
 * this codebase's established discipline -- see the old
 * MAP_NBR_SPRXTRA_CACHED comment history for what happens when a
 * resident constant isn't kept honest against real build headroom).
 */

#ifndef _ECM_CACHE_H_
#define _ECM_CACHE_H_

#include "config.h"
#include "sprites.h" /* SPRITE_SIZE/SPRITE_PLANES_SIZE -- the two block
                       * sizes this cache's own instances actually use. */

typedef struct {
  U8 *slots;      /* count * blockSize bytes, caller-owned */
  U16 *tag;       /* count U16s, caller-owned; 0xFFFF marks an empty
                    * slot. Wider than strictly needed now that each
                    * instance has its own tag space again (no more
                    * cross-category "kind" namespacing needed, see
                    * this file's own header) -- left at U16 rather than
                    * re-narrowed to U8, since every real tag value
                    * (a physical ECM slot 0-212, or a page0/extra
                    * sprite number 0-212) still fits either way, and
                    * re-narrowing isn't worth the churn for 1 byte/slot. */
  U8 *age;        /* count bytes, caller-owned -- recency stamp per slot */
  U8 *tick;       /* shared monotonic recency clock -- a pointer, not a
                   * value, so unrelated cache instances can share ONE
                   * clock (one shared counter is enough for every
                   * instance at once; nothing compares tags/ages ACROSS
                   * instances, so there's no correctness reason to keep
                   * them separate). */
  U16 blockSize;  /* bytes per slot -- SPRITE_SIZE for a single pattern/
                   * plane cache, SPRITE_PLANES_SIZE for the interleaved
                   * 3-plane ECM cache (engine/sprites.c's own
                   * ecm_slot_cache). U16 (not U8): SPRITE_PLANES_SIZE
                   * (384) doesn't fit a byte. */
  U8 count;       /* number of slots */
} rolling_cache_t;

/* Invalidates every slot in `c` (used at boot for caches that never need
 * re-invalidating again, and at each map transition for caches whose tag
 * space is only meaningful within the currently-loaded map). */
void rolling_cache_reset(rolling_cache_t *c);

/* Returns a pointer to the resident c->blockSize-byte block tagged `tag`
 * within `c`. On a cache hit, this costs nothing but a linear scan of
 * `c->count` slots -- no network access at all. On a miss, streams it
 * fresh via sys_nabu_loadAssetOffset(filename, ..., offset, c->blockSize)
 * into whichever slot is least-recently-used (or a still-empty one),
 * tags that slot, and returns it -- a real HCCA round-trip, same cost a
 * bare on-demand stream always had, just now remembered for next time
 * (and, for a cache whose blockSize spans multiple planes, ONE
 * round-trip covering what used to take several separate calls).
 *
 * `spriteNumber` is purely for DEBUG_LOAD_FILENAMES (added on request,
 * "update the debug print to show the sprite requested") -- the real
 * engine sprite number that caused this fetch, logged via
 * sys_nabu_debugPrintU16() right before the file load so the IA console
 * shows WHICH sprite triggered each real network round-trip, not just
 * which file/offset. Always passed (cheap, one more U16 argument) but
 * only read under that flag; pass whatever the caller's own spriteNumber
 * is, or 0 if none is meaningful for that cache. */
/* `ext`/`at4`: where a miss streams from -- see sys_nabu_loadAssetOffset()
 * (hal/sys_nabu_load.h): RES_FILE or SPRITE_FILE, offset in 4-byte units. */
U8 *rolling_cache_get(rolling_cache_t *c, U16 tag, const char *ext, U16 at4, U16 spriteNumber);

#endif /* _ECM_CACHE_H_ */

/* eof */
