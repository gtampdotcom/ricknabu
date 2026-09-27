/*
 * engine/dat_ecmSwapSamerica.c -- per-submap metadata for SAMERICA
 * (level 1): which real, mark-driven engine sprite NUMBERS each of its
 * 9 submaps needs, used by engine/sprites.c's ecm_prefetchSubmap() to
 * warm the shared ECM plane rolling cache (engine/include/ecm_cache.h)
 * right after a submap transition or at level start.
 *
 * TRIMMED, as part of merging this project's several ECM/sprite caches
 * into one shared rolling cache (see ecm_cache.h's own header for the
 * full writeup): this file used to also carry, per submap, a
 * SWP<n>.DAT-specific compressed-blob byte-length and a per-plane
 * offset table (ecm_swap<n>_planelen[]/_off0/1/2[]), because the old
 * ecm_swap_load() read a bespoke, per-submap-curated, 16-byte-bitmask-
 * compressed asset file with its own internal layout. That's gone now:
 * ecm_prefetchSubmap() looks up each sprite number's real physical
 * location via sprite_ecm_lookup()/sprite_index_map[] (engine/
 * sprites.c) -- the SAME global lookup every other ECM dispatch in this
 * engine already uses -- and streams straight from the SAME
 * "<page><plane>" asset files (SPRxBy.DAT) the on-demand fallback path
 * always has, so there's no separate compressed format or per-submap
 * byte offset left to precompute or store here. Only the sprite NUMBER
 * lists themselves -- genuinely per-submap information, not derivable
 * from anything else -- remain.
 *
 * Originally generated; now maintained by hand here (the generator is
 * gone). Counts (2,11,10,11,11,1,11,11,5) live alongside the table that indexes this one, in
 * engine/sprites.c's own ecm_swap_counts_table -- keep the two in sync
 * if a submap's own real count ever changes.
 */

static const U8 ecm_swap0_nums[2] = { 97,105 };
static const U8 ecm_swap1_nums[11] = { 43,47,48,49,50,51,52,53,54,86,87 };
static const U8 ecm_swap2_nums[10] = { 43,47,48,49,50,51,52,53,54,55 };
static const U8 ecm_swap3_nums[11] = { 42,43,47,48,49,50,51,52,53,54,86 };
static const U8 ecm_swap4_nums[11] = { 41,43,47,48,49,50,51,52,53,54,86 };
static const U8 ecm_swap5_nums[1] = { 101 };
static const U8 ecm_swap6_nums[11] = { 41,42,43,47,48,49,50,51,52,53,86 };
static const U8 ecm_swap7_nums[11] = { 42,43,47,48,49,50,51,52,53,54,86 };
static const U8 ecm_swap8_nums[5] = { 43,86,87,101,121 };

/* eof */
