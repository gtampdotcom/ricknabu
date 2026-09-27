/*
 * engine/ecm_cache.c -- implementation of the generic rolling (LRU)
 * cache declared in engine/include/ecm_cache.h. See that header for the
 * full "why one cache implementation instead of four" writeup, and for
 * why blockSize is per-instance now instead of a fixed SPRITE_SIZE.
 *
 * Table-driven-linear-scan, same "small and bounded compiles smaller on
 * Z80/SDCC than anything cleverer" reasoning every other small cache
 * scan in this codebase already followed (see engine/sprites.c's old
 * ecm_cache_lookup() comment) -- these scans are bounded by whatever
 * `count` each call site chooses, typically under 50, so an O(n) find is
 * cheap and the code stays tiny, which matters more than the scan speed
 * itself here.
 */

#include "ecm_cache.h"
#include "sys_nabu_load.h" /* sys_nabu_loadAssetOffset() */

void rolling_cache_reset(rolling_cache_t *c) {
  U8 i;
  for (i = 0; i < c->count; i++) {
    c->tag[i] = 0xFFFF;
  }
}

/* Returns the slot already holding `tag`, or 0xFF if none does. */
static U8 rolling_cache_find(rolling_cache_t *c, U16 tag) {
  U8 i;
  for (i = 0; i < c->count; i++) {
    if (c->tag[i] == tag) {
      return i;
    }
  }
  return 0xFF;
}

/* Picks which slot a newly-streamed block should occupy: a still-empty
 * slot if one exists, otherwise whichever slot has gone the longest
 * without being touched (largest wraparound-safe age distance from the
 * shared tick). Caller is responsible for tagging/stamping the slot it
 * gets back once the new pattern is actually in it -- same contract
 * every rolling cache in this project already followed pre-merge. */
static U8 rolling_cache_claim(rolling_cache_t *c) {
  U8 i, worst, worstAge, age;

  for (i = 0; i < c->count; i++) {
    if (c->tag[i] == 0xFFFF) {
      return i;
    }
  }
  worst = 0;
  worstAge = (U8)(*c->tick - c->age[0]);
  for (i = 1; i < c->count; i++) {
    age = (U8)(*c->tick - c->age[i]);
    if (age >= worstAge) {
      worstAge = age;
      worst = i;
    }
  }
  return worst;
}

U8 *rolling_cache_get(rolling_cache_t *c, U16 tag, const char *ext, U16 at4, U16 spriteNumber) {
  U8 slot = rolling_cache_find(c, tag);

  if (slot == 0xFF) {
    slot = rolling_cache_claim(c);
#if DEBUG_LOAD_FILENAMES
    sys_nabu_debugPrintU16(spriteNumber);
#else
    (void)spriteNumber;
#endif
    sys_nabu_loadAssetOffset(ext, &c->slots[(U16)slot * c->blockSize], at4, c->blockSize);
    c->tag[slot] = tag;
  }
  c->age[slot] = ++(*c->tick);
  return &c->slots[(U16)slot * c->blockSize];
}

/* eof */
