/*
 * engine/sprites.c -- PARTIAL port of xrick/src/sprites.c, TMS9918A/G2 only.
 *
 * Ported: sprites_clear(), sprites_paint(), and sprites_paint2() -- enough
 * for real gameplay entities (via engine/ents.c's ents_paintAll(), called
 * every frame now that engine/e_rick.c actually moves Rick) as well as the
 * original map-intro walking-silhouette use. One "pseudo-sprite" (a
 * Rick-sized 32x32 object) is drawn as four real 16x16 hardware sprites in
 * a 2x2 grid (top-left/bottom-left/top-right/bottom-right), matching the
 * reference's chr/chr+4/chr+8/chr+12 pattern-name split and sprite_table[]
 * layout bit-for-bit.
 *
 * sprites_paint2() is adapted, not verbatim, from xrick/src/sprites.c's
 * version:
 *   - The off-screen (maps_clip() true) branch there only marks
 *     sprite_table[]'s in-memory y=0xd2 and relies on some later bulk
 *     flush of the whole table to VRAM (part of a frame-loop structure
 *     this port doesn't have) to actually hide the hardware sprites. This
 *     port has no such bulk flush -- sprites_paint() below writes VDP
 *     attribute bytes directly, immediately, per pseudo-sprite -- so this
 *     port's clipped branch does the same: write the hide value straight
 *     to the VDP sprite attribute table, not just sprite_table[].
 *   - Everything else (slot assignment/reuse via the y==0xd1 free
 *     marker, fb-coordinate conversion, the sprites_paint() call itself)
 *     is unchanged logic.
 *
 * NOT ported, both deliberately deferred rather than half-done:
 *
 *   - The F18A #ifdef block (per-page palette remap via sprite_index_map[],
 *     the sprf00..sprf42.c 8-color pattern banks, and the bank-switched
 *     vdpmemcpy2() calls that load them). hal/sysvid_nabu.c's F18A path
 *     currently runs in plain (ECM off) G2-compatible mode with the same
 *     VRAM layout as stock TMS9918A -- see that file's header -- so the
 *     TMS9918A path below already works unmodified on F18A/PICO9918 too,
 *     just without the enhanced per-sprite color. Add the F18A block back
 *     once hal/sysvid_nabu.c actually turns ECM on for sprites.
 *
 *   - Bank switching / multi-page pattern data (SWITCH_IN_BANK() plus
 *     sprites_data1..data4 in the reference) -- not needed *yet*: Rick's
 *     own sprite numbers (walk/jump/crawl/climb/stop/shoot/zombie, all of
 *     engine/e_rick.c's E_RICK_ENT.sprite assignments) and every
 *     ent_entdata[].spr this map's marks reference are all < 48, i.e.
 *     entirely within page 0 (dat_spritesTI0.c). Other maps' entities, or
 *     enemy behavior once e_them.c/e_box.c/e_bonus.c get ported, can pick
 *     higher sprite numbers via ent_sprseq[] and will need
 *     sprites_data1..data4 (dat_spritesTI1-4.c) wired in with a
 *     `switch(spriteNumber/48)` in sprites_paint()'s pattern-load block
 *     below, same shape as the reference's CLASSIC99 branch (the one that
 *     doesn't rely on TI cartridge bank-switch aliasing, since NABU has no
 *     banking either).
 *
 *   - The reference's two-54-byte-chunk pattern copy (skipping a few
 *     always-blank trailing rows since Rick's sprites are 21 rows tall,
 *     not the full 32 four packed 8x8 patterns cover) -- copied here as a
 *     single straight 128-byte loop instead. Simpler and correctness-first;
 *     it's a few dozen extra byte-writes per pattern reload at runtime, not
 *     extra ROM size, so revisit only if frame timing ever demands it.
 *
 *   - Early-clock (x<32) sprite positioning -- the reference's real
 *     vdp_setSpritePosition()-style handling of TMS9918A's "shift 32px
 *     left" attribute bit isn't implemented. Rick's map-1 starting
 *     position and this map's entities all stay right of x=32 in
 *     fb-coordinates, so it's never exercised yet -- Rick walking to the
 *     very left edge of the screen will clip oddly until this is added.
 */

#include "config.h"
#include "sprites.h"
#include "ents.h"    /* ent_ents[], ENT_ENTSNUM -- sprites_paint2() */
#include "env.h"     /* env_highlight -- sprites_paint2() */
#include "maps.h"    /* maps_clip(), MAPS_FB_X/Y -- sprites_paint2() */
#include "game.h" /* gSprite/gSpritePat -- see hal/sysvid_nabu.c's header */
#include "NABU-LIB.h"
#include "sysvid_nabu.h" /* sysvid_nabu_hasF18A -- see SPRITE_COLOR_WHITE below */
#include "sys_nabu_load.h" /* sys_nabu_loadAssetOffset() -- sprites_map1_extra_scratch's own on-demand streaming */

/* Page 0 of the TI sprite pattern data -- see this file's header for why
 * only page 0 is included so far. */
#include "dat_spritesTI0.c"

#include "ecm_cache.h" /* rolling_cache_t / rolling_cache_get() -- see
                         * that header for the full "merge the multiple
                         * cache systems" writeup. Replaced the old
                         * compiled-in SAMERICA ECM frame data (a
                         * duplicate of what's in the sprite files) --
                         * the cache streams it on demand instead. */

sprite_data_t sprite_table[(ENT_ENTSNUM+1)*4];

/* BUG FIXED: this used to be a single `#define SPRITE_COLOR_WHITE 0x0F`,
 * on the theory that "black backdrop, white sprite" are the two hardware
 * palette entries "true across TMS9918A/F18A regardless of what NABU-LIB
 * happens to name the rest of the 16-color table". That's true for a
 * genuinely hardwired TMS9918A palette, but not once anything loads a
 * custom one -- and engine/tiles.c's tiles_setBank() does exactly that for
 * F18A hardware every time it runs, via f18a_loadPalette(tilesf18_pal, 0,
 * 16) (see that file's own comment). tilesf18_pal[] (also tiles.c) isn't
 * the standard TMS9918A color order: index 15 there is 0x0000 (black),
 * and the actual white entry this game's own F18A art was authored against
 * is index 10 (0x0FFF). Sending 0x0F unconditionally therefore drew every
 * sprite in *black* on F18A -- since sprite pattern data is 1-bit
 * (color-or-transparent, see this file's header), a black sprite against
 * this game's mostly-dark backgrounds read as a bare outline wherever it
 * happened to overlap a lighter tile, not a solid fill. Stock TMS9918A has
 * no loadable palette, so 0x0F is still correct there. */
#define SPRITE_COLOR_WHITE_9918 0x0F
#define SPRITE_COLOR_WHITE_F18A 0x0A

/*
 * F18A 8-color sprite mode (ECM=3) -- FIXED, on request ("add support for
 * the TI-99's version multicolor F18A sprites"). Two earlier attempts
 * (see git history) produced "scrambled orange/black noise": Rick's
 * outline (bit 0, the original shape/transparency pattern) was correct,
 * but the interior colors were garbage.
 *
 * ROOT CAUSE, finally confirmed against the real F18A hardware spec
 * (visrealm/pico9918 wiki, "F18A Programmer's Reference" -- sprite
 * enhancements section) rather than guessing from the reference's own
 * cartridge-banked C source: F18A's sprite ECM mode does NOT let software
 * place the two extra color-bit planes at arbitrary VRAM addresses. The
 * chip itself computes their locations as `spritePatternBase + ecmOffset`
 * and `+ 2*ecmOffset`, where spritePatternBase comes from register 6 (the
 * SAME register that already points gSpritePat at its normal location)
 * and ecmOffset is `0x800 >> SPGS` (register 29 bits 7-6, default/SPGS=00
 * giving 0x800). Both earlier attempts wrote color-plane DATA to fixed
 * addresses (GAME_VRAM_SPRITEPAT_BIT1/BIT2) but never actually changed
 * register 6 -- so with gSpritePat left at this port's normal 0x3800,
 * the chip was computing plane reads at 0x3800+0x800=0x4000 and
 * 0x3800+0x1000=0x4800, both past the 16KB VRAM boundary, wrapping
 * around to 0x0000 (the tile pattern table) and 0x0800 (wherever the
 * software-placed data actually was) -- reading unrelated VRAM content
 * as color data. Exactly "scrambled noise."
 *
 * THE FIX: move gSpritePat itself (register 6) to 0x2800, matching the
 * real F18A cartridge's own verified working layout (xrick/src/xrick.c's
 * sys_init() comment: "2800 Sprite colors (2k table) bit 0 / 3000 ...bit
 * 1 / 3800 ...bit 2") -- with the default 0x800 ecmOffset, planes land at
 * 0x2800/0x3000/0x3800, spanning exactly the top 6KB of VRAM (up to but
 * not past 0x4000). This only fits because set_halfbitmap() (hal/
 * sysvid_nabu.c) ALSO switches the tile color table to non-split
 * addressing (register 3=0x9F, matching the reference's own value
 * exactly) -- shrinking it from 6KB to 2KB (0x2000-0x2800) and freeing
 * the 0x2800-0x3800 range this needs. GAME_VRAM_SPRITEPAT_BIT1/BIT2
 * (game.h) are now 0x3000/0x3800 -- real offsets from the new base, not
 * disconnected fixed addresses.
 *
 * All 13 entities (ENT_ENTSNUM+1, ents.h) share this one physical
 * pattern table (52 real hardware sprites x 32 bytes = 1664 bytes total)
 * -- comfortably under the 2048-byte ecmOffset, so entity N's own plane-0
 * data can never bleed into plane 1's own start address. Since ECM=3 is
 * chip-wide (every sprite gets 3-bit color once it's on, not just Rick's),
 * every entity needs REAL plane-1/plane-2 data at its own slot -- Rick's
 * real color data where available (rick_walk_bit1/bit2[] below,
 * dispatched by rick_walk_bit_index()), zero bytes for everyone else
 * (sprites_paint()'s own load_pattern block, further down) so their
 * color index always resolves to just bit 0's own value (0=transparent,
 * forced by F18A hardware regardless of palette content; 1=white, via
 * sprite_ecm_pal_fallback[]'s own index-1 entry) -- unaffected,
 * regardless of how their real shape data happens to look.
 *
 * The attribute "color" byte becomes a palette-GROUP selector, multiplied
 * by 4 by hardware to get the base offset into F18A's 64-entry palette
 * RAM (this part matched the reference exactly: xrick/src/sprites.c's own
 * `pal = 4 + spritePage*2` lines up with xrick/src/scr_imap.c's own
 * `loadpal_f18a(sprfNpal, 16/24/32/40, 8)` RAM offsets when multiplied by
 * 4).
 *
 * Data below (rick_walk_bit1/bit2[], sprite_ecm_pal_rickwalk/fallback[]
 * in hal/sysvid_nabu.c) is real, extracted from the actual F18A cartridge
 * (ti/f18a/sprf01.c/sprf02.c, xrick/src/scr_imap.c's palette offsets).
 * rick_walk_bit1/bit2[] are streamed BSS now, not compiled-in -- see
 * their own declaration comment just below for why. */
/* Real per-page palette group is now computed directly (4 + page*2,
 * xrick/src/sprites.c's own formula -- sprites_paint()'s own spriteColor
 * assignment) since covering more than one page means it's no longer a
 * single fixed constant. SPRITE_ECM_PAL_FALLBACK moved from RAM offset
 * 24 to 56 (RELOCATED, on request "all sprites, all levels") -- offset
 * 24 (group 6) is EGYPT's own real palette (sprf1pal[], hal/
 * sysvid_nabu.c) now, not available for the white stand-in any more;
 * offset 56 (group 14) is genuinely unused by any real page palette this
 * build loads (pages 2-4 share group 8/offset32 per the reference's own
 * clamp, "if (pal > 10) pal = 8" -- group 10/12 are the only other real
 * gaps, 14 chosen arbitrarily among them). */
#define SPRITE_ECM_PAL_FALLBACK 14

/* Shared recency clock -- one counter is enough for every rolling cache
 * in this file at once (page0_cache here, sprites_extra_cache further
 * down, and ecm_slot_cache further down still when SPRITE_ECM_ENABLED)
 * -- see ecm_cache.h's own rolling_cache_t.tick comment for why sharing
 * one clock across unrelated instances is safe. Declared UNCONDITIONALLY
 * since page0_cache (below) needs it on every build, ECM or not. */
static U8 shared_cache_tick;

/* Rolling LRU cache for SPR0.DAT stock page-0 patterns --
 * UNCONDITIONAL (not #if SPRITE_ECM_ENABLED): this is the ONLY sprite
 * pattern source real stock 9918A hardware ever has (sysvid_nabu_hasF18A
 * false at runtime forces walkIdx<0 for everything, sprites_paint()'s
 * own comment), and remains the fallback for anything an F18A build's
 * own ECM pages don't cover either way -- so it exists and works
 * whether or not this build has any F18A/ECM support compiled in at
 * all. REPORTED DIRECTLY ("XJ sprite is being downloaded many times
 * over") is the original motivation for caching this at all -- Rick's
 * own walk/crawl/climb/shoot frames and other common page-0 objects
 * cycle through VRAM slots constantly.
 *
 * SPLIT BACK OUT from the ECM plane cache, on the later "interleaved
 * .dat files" request ("would a few large .dat files instead of many
 * smaller files offer any negatives/positives to speed"): once ECM
 * planes moved to one combined SPRITE_PLANES_SIZE(384)-byte fetch per
 * physical slot (ecm_slot_cache, further down, #if SPRITE_ECM_ENABLED),
 * a single shared pool could no longer size its slots uniformly for
 * both a 384-byte ECM entry and a 128-byte page0 pattern without either
 * wasting 256 bytes per page0 entry or needing variable-size slots --
 * so each again gets its own pool, sized for its own real unit. Unlike
 * sprites_map1_extra_cached[], SPR0.DAT's own content is NOT per-map, so
 * this cache is never invalidated at a map transition, only once at
 * boot (sprites_page0CacheInit(), engine/include/sprites.h). */
U8 page0_cache_slots[PAGE0_CACHE_SLOTS][SPRITE_SIZE];
static U16 page0_cache_tag[PAGE0_CACHE_SLOTS];
static U8 page0_cache_age[PAGE0_CACHE_SLOTS];
static rolling_cache_t page0_cache;

/* One-time boot init (engine/include/sprites.h's own comment) -- wires
 * the rolling_cache_t's pointers at its backing arrays and invalidates
 * every slot. Can't be a static initializer: SDCC's own const-initializer
 * support for structs full of array-decay pointers is exactly the kind
 * of thing this project's own established practice (see this file's
 * header, "SIZE PASS" comments throughout) says not to trust without
 * checking, and an explicit init function costs nothing extra here --
 * every other cache in this file already has one of these. */
/* STOCK 9918A CACHES IN THE IDLE ECM BUFFER, on request ("stock 9918A
 * does far more HCCA reads than with an F18A"): page0_cache and
 * sprites_extra_cache are only 1 slot each (PAGE0_CACHE_SLOTS/
 * MAP_NBR_SPRXTRA_CACHED -- sized for the F18A, where they're a rarely
 * used fallback), so on stock hardware nearly every animation frame
 * change was a network read. ecm_slot_cache_slots (ECM_SLOT_CACHE_TOTAL_
 * BYTES, 11136) is never touched on stock hardware -- ECM needs an F18A --
 * so there both caches are carved out of it at runtime instead, tag and
 * age arrays included (count * (SPRITE_SIZE + 2 + 1) bytes each):
 *   page0_cache: all SPRITE_PAGE_SIZE (48) page-0 sprites -- the whole of
 *     SPR0.DAT, so once seen a page-0 frame never streams again.
 *   sprites_extra_cache: whatever's left (37 slots).
 * Same RAM, no build change, F18A behaviour unchanged. The WAAAAA PCM
 * cheat borrows the same buffer -- sprites_ecmSlotCacheInvalidate() re-
 * carves both caches after it plays. */
#if SPRITE_ECM_ENABLED
#define STOCK_SLOT_BYTES   (SPRITE_SIZE + 2 + 1)
#define STOCK_PAGE0_SLOTS  SPRITE_PAGE_SIZE
#define STOCK_EXTRA_SLOTS  ((ECM_SLOT_CACHE_TOTAL_BYTES - STOCK_PAGE0_SLOTS * STOCK_SLOT_BYTES) / STOCK_SLOT_BYTES)
#define STOCK_EXTRA_BASE   (&ecm_slot_cache_slots[0][0] + STOCK_PAGE0_SLOTS * STOCK_SLOT_BYTES)

static void stock_cache_carve(rolling_cache_t *c, U8 *at, U8 count) {
  c->slots = at;
  c->tag = (U16 *)(at + (U16)count * SPRITE_SIZE);
  c->age = at + (U16)count * (SPRITE_SIZE + 2);
  c->count = count;
}
#endif

void sprites_page0CacheInit(void) {
  page0_cache.slots = &page0_cache_slots[0][0];
  page0_cache.tag = page0_cache_tag;
  page0_cache.age = page0_cache_age;
  page0_cache.count = PAGE0_CACHE_SLOTS;
#if SPRITE_ECM_ENABLED && !VDP_TARGET_F18A_ONLY
  if (!sysvid_nabu_hasF18A) {
    stock_cache_carve(&page0_cache, &ecm_slot_cache_slots[0][0], STOCK_PAGE0_SLOTS);
  }
#endif
  page0_cache.tick = &shared_cache_tick;
  page0_cache.blockSize = SPRITE_SIZE;
  rolling_cache_reset(&page0_cache);
}

#if SPRITE_ECM_ENABLED

/*
 * Real sprite_index_map[213], verbatim from the reference
 * (xrick/src/sprites.c) -- translates an engine sprite number into the TI
 * cartridge's own SHUFFLED physical slot before indexing sprf00-42.c.
 * EXTENDED, on request ("all sprites, all levels, how feasible?" ->
 * "yes"): the previous 9-entry exception switch only handled page-0
 * numbers correctly (enough to fix the "bonus turned into a dog" bug,
 * engine sprite 43 needing physical slot 32) -- covering more pages
 * needs the real, full table, since pages 1+ have their own non-identity
 * entries too (e.g. 109->43, 150->109, etc). Physical slot / 48 gives the
 * page (0-3 have 48 slots each, page 4 only 21 -- SPRITE_FINAL_SIZE,
 * sprites.h -- but 192/48=4 and 212/48=4 still divide correctly into
 * page 4 with no special-casing needed); slot % 48 gives the frame index
 * within whichever page's own SPRxBy.DAT. */
static const U8 sprite_index_map[213] = {
  0,
  1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
  17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,142,
  143,34,35,36,37,38,39,40,91,92,32,33,115,47,85,48,
  49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,
  65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,
  81,82,83,84,121,86,87,88,89,90,41,42,93,111,112,96,
  97,98,99,100,101,102,103,104,105,106,107,108,43,44,45,46,
  113,114,95,116,117,118,119,120,94,122,123,124,125,126,127,128,
  129,130,131,132,133,134,135,136,137,138,139,140,141,150,151,144,
  145,146,147,148,149,109,110,152,153,154,155,156,157,158,159,160,
  161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,
  177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,
  193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,
  209,210,211,212
};

/* How many pages actually have real streamed color-plane data
 * (SPRxB0/1/2.DAT) right now -- 0 (SAMERICA/most entities), 1 (EGYPT),
 * 2 (CASTLE, plus SAMERICA's own boulder), 3 (the "spear guy" enemy),
 * and now 4 (MBASE's own remaining sprites -- "complete the
 * conversion") -- every page now covered, "all sprites, all levels" is
 * done. Page 4 is smaller than the others (SPRITE_FINAL_SIZE, sprites.h
 * -- 21 sprites, not 48) but needs no special-casing here: physical
 * slots 192-212 divide/modulo by SPRITE_PAGE_SIZE (48) into page 4,
 * local index 0-20, exactly like every other page. Page 4 does NOT get
 * its own palette group, unlike pages 0-3 -- see spriteColor's own
 * clamp comment just below in sprites_paint(). */
#define SPRITE_ECM_MAX_PAGE 4

/*
 * Looks up spriteNumber's real color-plane data location: returns the
 * frame index within its page's own SPRxB0/1/2.DAT (0-47), or -1 if none
 * exists yet (page beyond SPRITE_ECM_MAX_PAGE, or spriteNumber outside
 * sprite_index_map[]'s own 0-212 range). Writes the page number (0-4) to
 * *pageOut whenever it returns >= 0.
 */
static S16 sprite_ecm_lookup(U16 spriteNumber, U8 *pageOut) {
  U16 slot;
  U8 page;

  if (spriteNumber >= 213) {
    return -1;
  }
  slot = sprite_index_map[spriteNumber];
  page = (U8)(slot / SPRITE_PAGE_SIZE);

  /* Per-page compile-time scoping, sysvid_nabu.h's own SPRITE_ECM_PAGE_*
   * comment has the full "why page, not level" writeup. When every flag
   * is on (the default), every one of these `#if` blocks below vanishes
   * entirely at preprocess time -- this costs NOTHING beyond the
   * original SPRITE_ECM_MAX_PAGE check in that configuration; the extra
   * per-page `if`s only exist in a build that actually turned one off. */
#if !SPRITE_ECM_PAGE_SAMERICA
  if (page == 0) {
    return -1;
  }
#endif
#if !SPRITE_ECM_PAGE_EGYPT
  if (page == 1) {
    return -1;
  }
#endif
#if !SPRITE_ECM_PAGE_CASTLE
  if (page == 2) {
    return -1;
  }
#endif
#if !SPRITE_ECM_PAGE_MBASE
  if (page == 3 || page == 4) {
    return -1;
  }
#endif

  if (page > SPRITE_ECM_MAX_PAGE) {
    return -1;
  }
  *pageOut = page;
  return (S16)(slot - (U16)page * SPRITE_PAGE_SIZE);
}

/* ECM slot rolling cache -- one INTERLEAVED SPRITE_PLANES_SIZE(384)-byte
 * entry per physical (page,frame) slot, all 3 color planes together, in
 * place of the old per-plane design (3 separate SPRITE_SIZE(128)-byte
 * entries, 3 separate rolling_cache_get() calls, 3 separate HCCA
 * round-trips per cold miss). Added on request ("would a few large .dat
 * files instead of many smaller files offer any negatives/positives to
 * speed") -- see SPRITE_PLANES_SIZE's own comment (engine/include/
 * sprites.h) for the full "why interleaving beats plain consolidation"
 * reasoning: the fixed per-call rn_FileRead() overhead is paid per
 * CALL, not per byte, so combining 3 calls into 1 for the same total
 * payload is a real round-trip win, not just fewer distinct filenames.
 *
 * SPLIT from the stock page0_cache above (was one shared pool briefly,
 * "graphics_cache") precisely because this cache's own slot size
 * (384) and page0's (128) can no longer share one pool without either
 * wasting bytes or needing variable-size slots -- see page0_cache's own
 * comment for the fuller account. #if SPRITE_ECM_ENABLED only: nothing
 * ever populates this without real ECM data to interleave. */
U8 ecm_slot_cache_slots[ECM_SLOT_CACHE_SLOTS][SPRITE_PLANES_SIZE];
static U16 ecm_slot_cache_tag[ECM_SLOT_CACHE_SLOTS];
static U8 ecm_slot_cache_age[ECM_SLOT_CACHE_SLOTS];
static rolling_cache_t ecm_slot_cache;

/* One-time boot init -- see sprites_page0CacheInit()'s own comment just
 * above for why this can't be a static initializer. */
void sprites_ecmSlotCacheInit(void) {
  ecm_slot_cache.slots = &ecm_slot_cache_slots[0][0];
  ecm_slot_cache.tag = ecm_slot_cache_tag;
  ecm_slot_cache.age = ecm_slot_cache_age;
  ecm_slot_cache.tick = &shared_cache_tick;
  ecm_slot_cache.blockSize = SPRITE_PLANES_SIZE;
  ecm_slot_cache.count = ECM_SLOT_CACHE_SLOTS;
  rolling_cache_reset(&ecm_slot_cache);
}

/* sprites_ecmSlotCacheInvalidate -- see engine/include/sprites.h's own
 * declaration comment. Just re-runs rolling_cache_reset(); the
 * rolling_cache_t struct itself is already wired (sprites_
 * ecmSlotCacheInit() ran once at boot), so there's nothing to re-point
 * here, only tags to clear. */
void sprites_ecmSlotCacheInvalidate(void) {
  rolling_cache_reset(&ecm_slot_cache);
  /* On stock hardware the same buffer holds page0_cache and
   * sprites_extra_cache (tags and all -- sprites_page0CacheInit()'s own
   * comment), so rebuild those too. */
#if !VDP_TARGET_F18A_ONLY
  if (!sysvid_nabu_hasF18A) {
    sprites_page0CacheInit();
    sprites_extraCacheReset();
  }
#endif
}

/*
 * Physical-slot-keyed ECM fetch -- looks up (or streams and caches) ALL
 * 3 real color planes of spriteNumber's data at page walkPage/frame
 * walkIdx TOGETHER, via ecm_slot_cache above, in a single
 * rolling_cache_get() call. Returns a pointer to SPRITE_PLANES_SIZE
 * (384) bytes: [0..127]=plane0(BIT0), [128..255]=plane1(BIT1),
 * [256..383]=plane2(BIT2) -- callers slice out whichever plane(s) they
 * need from the one returned pointer.
 *
 * REPLACES ecm_fetch_plane(): that made one separate rolling_cache_get()
 * call PER PLANE (3 calls, 3 possible cold-miss round-trips, for a
 * single sprite) against 3 separate "<page><plane>" files. This makes
 * ONE call against ONE per-page file holding all 3 planes INTERLEAVED
 * per physical slot -- REQUIRES that file to actually be laid out that
 * way (SPRITE_PLANES_SIZE's own comment, engine/include/sprites.h);
 * this is a source-asset change this codebase alone can't produce.
 *
 * physSlot is sprite_index_map[]'s own raw output (0-212, unique across
 * every page) -- reconstructed here from (walkPage, walkIdx) rather
 * than threading a third value through every caller, since
 * walkPage*SPRITE_PAGE_SIZE+walkIdx is exactly that value again (the
 * same arithmetic sprite_ecm_lookup() used in reverse to split it).
 *
 * spriteNumber (added on request, "update the debug print to show the
 * sprite requested") is the real engine sprite number this fetch is
 * for -- unlike physSlot/tag (this cache's own internal key), it's the
 * one value that actually matches sprite_index_map[]'s own INPUT side,
 * so it's what gets logged under DEBUG_LOAD_FILENAMES
 * (rolling_cache_get()'s own comment), not physSlot.
 */
static const U8 *ecm_fetch_slot(U8 walkPage, S16 walkIdx, U16 spriteNumber) {
  /* All 3 planes of a frame are interleaved (SPRITE_PLANES_SIZE's own
   * comment). PACKED: the five per-page files ("0".."4") are now one file,
   * rick.spr, pages back to back at SPRITE_PAGE_SIZE frames each -- so a
   * frame's byte offset is just its physical slot * SPRITE_PLANES_SIZE
   * (tools/pack_assets.py). */
  U8 physSlot = (U8)((U16)walkPage * SPRITE_PAGE_SIZE + (U16)walkIdx);

  return rolling_cache_get(&ecm_slot_cache, physSlot, SPRITE_FILE,
                            (U16)physSlot * (SPRITE_PLANES_SIZE / 4), spriteNumber);
}
/* Per-submap metadata: which real, mark-driven engine sprite NUMBERS
 * each of SAMERICA's 9 submaps needs beyond whatever's already
 * resident/cached -- dat_ecmSwapSamerica.c's own *_nums[] arrays,
 * unchanged in content, TRIMMED (SIZE PASS, this merge) to drop the
 * per-plane byte-offset/length tables the old compressed-swap-blob
 * mechanism needed: physical location is looked up fresh via
 * sprite_ecm_lookup()/sprite_index_map[] below instead now, the SAME
 * global table every other ECM dispatch in this file already uses, so
 * there's nothing per-submap left to precompute. */
#include "dat_ecmSwapSamerica.c"

static const U8 * const ecm_swap_nums_table[9] = {
  ecm_swap0_nums, ecm_swap1_nums, ecm_swap2_nums, ecm_swap3_nums,
  ecm_swap4_nums, ecm_swap5_nums, ecm_swap6_nums, ecm_swap7_nums, ecm_swap8_nums
};
static const U8 ecm_swap_counts_table[9] = { 2, 11, 10, 11, 11, 1, 11, 11, 5 };

/*
 * ecm_prefetchSubmap -- see engine/include/sprites.h's own declaration
 * comment for the full account of what this replaces (ecm_swap_load()).
 * For each of submap `submap`'s own real extra sprite numbers, looks up
 * its physical (page, frame) location the normal way (sprite_ecm_lookup(),
 * same as sprites_paint() itself) and warms it into ecm_slot_cache -- one
 * ecm_fetch_slot() call per sprite number (all 3 planes in that one
 * call now, not a separate call per plane), result pointer discarded:
 * the point here is purely the side effect of populating the cache
 * before gameplay needs it, not the returned data itself. A sprite
 * number sprite_ecm_lookup() can't place on a page this build actually
 * streams (walkIdx<0 -- e.g. a disabled SPRITE_ECM_PAGE_*) is silently
 * skipped, same "falls through safely" behavior every other ECM call
 * site already has.
 */
void ecm_prefetchSubmap(U16 submap) {
  const U8 *nums = ecm_swap_nums_table[submap];
  U8 count = ecm_swap_counts_table[submap];
  U8 i, walkPage;
  S16 walkIdx;

  /* BUG FIXED, reported as "stock 9918A does far more HCCA reads than with
   * an F18A": sprite_ecm_lookup() doesn't check the hardware itself
   * (sprites_paint() gates its own call on sysvid_nabu_hasF18A), and both
   * callers (maps.c) are only compile-time gated -- so on a stock 9918A
   * every submap change streamed ECM colour planes it can never display. */
#if !VDP_TARGET_F18A_ONLY
  if (!sysvid_nabu_hasF18A) {
    return;
  }
#endif

  for (i = 0; i < count; i++) {
    walkIdx = sprite_ecm_lookup(nums[i], &walkPage);
    if (walkIdx < 0) {
      continue;
    }
    ecm_fetch_slot(walkPage, walkIdx, nums[i]);
  }
}
#endif /* SPRITE_ECM_ENABLED */

/*
 * The boulder (e_them.c's type-3 TRIGRICK mark, the "chases you at the
 * start" entity -- see that file's header) is the only entity this map
 * ever animates outside page 0: once awake, its sprbase=44 (from
 * ent_entdata[0x2a].spr) and ent_sprseq[45]/[46] (0x61/0x69) give it a
 * 2-frame rolling animation using sprite numbers 97 and 105 -- both from
 * page 2 (xrick/src/dat_spritesTI2.c), which this port has never loaded
 * as a whole page.
 *
 * USED TO be a small dedicated compiled-in const table (256 bytes,
 * always resident for the entire game regardless of map -- see git
 * history) rather than a whole page for 2 frames. BUG FIXED, on request
 * ("cache the boulder sprites for... submap 0, boulder is never used
 * again after that, so can be removed from cache"): that const table
 * cost the same 256 bytes permanently, for every map, even though the
 * boulder only ever exists during SAMERICA's own submap 0. Folded these
 * two sprite numbers into SAMERICA's own sprites_map1_extra_nums[] list
 * instead (maps.c's map_dataSize[0], SPXNUM1.DAT/SPXPAT1.DAT) -- SAMERICA
 * only had 22 *real* mark-driven extra sprites against a 24-slot cache
 * (sprites.h's MAP_NBR_SPRXTRA_CACHED), so the 2 boulder frames now
 * exactly fill the 2 cache slots that were otherwise sitting unused for
 * this specific map, at no new resident-byte cost at all -- freeing the
 * old table's 256 bytes outright. No special-case dispatch needed here
 * any more either: the generic sprites_map1_extra_nums[] search below
 * already finds them like any other map's own extra sprite, genuinely
 * resident (not on-demand-streamed, which would have reintroduced the
 * exact flicker/slowdown MAP_NBR_SPRXTRA_CACHED's own header describes --
 * a 2-frame animation this tight is exactly the "cycles every few steps"
 * case that mechanism exists for) for the whole time SAMERICA is loaded.
 * Once a different map loads, map_loadMap() overwrites the whole cache
 * with that map's own sprites regardless (EGYPT/CASTLE/MBASE all need
 * more than 24 anyway) -- nothing left over to explicitly "uncache". */

/*
 * engine/e_them.c's type-1a/1b enemy walk-cycle sprites: NOT sprites
 * 47-52 as first assumed. ents.c's mark-creation code has a special case
 * (the ENT_FLG_TRIGGERS block, right after this table's own site) that
 * overrides .sprbase to ent_entdata[].sni instead of .spr whenever ALL
 * FOUR trigger flags are set on a slot-9+ entity -- this level's own
 * type-1a mark has flags=0xF0 (TRIGRICK|TRIGSTOP|TRIGBULLET|TRIGBOMB, all
 * four), so that's exactly what happens: real sprbase is
 * ent_entdata[4].sni=0x8E=142, not .spr=47. Confirmed by instrumenting
 * ents_paintAll() and reading back the actual runtime sprite numbers
 * (142-146) rather than trusting the formula alone -- the first version
 * of this fix (extracting sprites 48-52) was silently a no-op the whole
 * time, since the dispatch check for 48-52 never matched anything real.
 *
 * Real range: sprbase(142) + ent_sprseq[0..3]'s lookup ({0,1,0,2}) + a +3
 * direction offset gives {142,143,144} facing right and {145,146,147}
 * facing left -- all in pages 2 (dat_spritesTI2.c, local 46-47) and 3
 * (dat_spritesTI3.c, local 0-3), neither of which this port loads as a
 * whole page. All 6 distinct frames would cost 768 bytes.
 *
 * Now have 4 of the 6 (142/143 right, 145/146 left) -- a real 2-pose walk
 * cycle per direction, not just a static stance -- once
 * TILES_BANKS_COUNT's own size correction (tiles.h) freed enough budget.
 * 144/147 (the sequence's third, least-visited pose -- ent_sprseq's own
 * {0,1,0,2} pattern only reaches offset 2 a quarter of the time) still
 * alias to 142/145 respectively in the dispatch below, rather than
 * costing another 256 bytes for a marginal improvement. */
#define SPRITE_THEM_WALK_FIRST 142
#define SPRITE_THEM_WALK_LAST 147
/* DROPPED, on request ("all of spear guy [fully cached]") once
 * VDP_TARGET_F18A_ONLY is set (sysvid_nabu.h): this table's own result
 * (`src` below) always gets thrown away anyway on an F18A-only build --
 * spriteNumber 142-147 fall inside SPRITE_ECM_MAX_PAGE's covered range
 * (page3, SPRITE_ECM_PAGE_MBASE=1), so sprites_paint()'s own walkIdx>=0
 * override unconditionally replaces `src` with real color-matched data a
 * few lines later regardless of what this table provided -- 512 bytes of
 * permanently-resident dead weight on THIS build specifically.
 */
#if !SPRITE_ECM_ENABLED || !VDP_TARGET_F18A_ONLY || !ECM_ALL_PAGES_ENABLED
static const sprite_t sprites_them_walk[4][SPRITE_SIZE] = {
  { /* sprite 142 (0x8E) -- page2 local 46 */
    0x00, 0x00, 0x03, 0x04, 0x09, 0x0A, 0x04, 0x0D, 0x0B, 0x03, 0x03, 0x03, 0x03, 0x01, 0x1B, 0x01,
    0x00, 0xD6, 0x09, 0xA4, 0x22, 0x4F, 0xBF, 0xFB, 0xFD, 0x7E, 0xFF, 0xBF, 0x9F, 0x6F, 0x4F, 0xFA,
    0x05, 0x02, 0x01, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x55, 0xAA, 0xEE, 0xE7, 0xFB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x60, 0x98, 0x30, 0xE8, 0x70, 0xF0, 0xE0, 0x00, 0x80, 0x00, 0x00, 0x88, 0xDE, 0x88,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x40, 0x00, 0x80, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  },
  { /* sprite 143 (0x8F) -- page2 local 47, second right-facing walk frame */
    0x00, 0x00, 0x03, 0x04, 0x09, 0x0A, 0x04, 0x0D, 0x0B, 0x03, 0x03, 0x03, 0x03, 0x01, 0x1B, 0x01,
    0x00, 0xD6, 0x09, 0xA4, 0x22, 0x4F, 0xBF, 0xFB, 0xFD, 0x7E, 0xFF, 0xBF, 0x9F, 0xCF, 0xEF, 0xFA,
    0x05, 0x02, 0x03, 0x0F, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x55, 0xAA, 0xEF, 0xC5, 0xE3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x60, 0x98, 0x30, 0xE8, 0x70, 0xF0, 0xE0, 0x00, 0x80, 0x00, 0x00, 0x88, 0xDE, 0x88,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x40, 0x30, 0xF0, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  },
  { /* sprite 145 (0x91) -- page3 local 1 */
    0x00, 0x01, 0x06, 0x19, 0x0C, 0x17, 0x0E, 0x0F, 0x07, 0x00, 0x01, 0x00, 0x00, 0x11, 0x7F, 0x11,
    0x00, 0x6B, 0x90, 0x25, 0x44, 0xF2, 0xFD, 0xDF, 0xBF, 0x7E, 0xFF, 0xFD, 0xF9, 0xF7, 0xF3, 0x5F,
    0x01, 0x02, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x55, 0x77, 0xE7, 0xDF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xC0, 0x20, 0x90, 0x50, 0x20, 0xB0, 0xD0, 0xC0, 0xC0, 0xC0, 0xC0, 0x80, 0xF8, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xA0, 0x40, 0x80, 0xC0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  },
  { /* sprite 146 (0x92) -- page3 local 2, second left-facing walk frame */
    0x00, 0x01, 0x06, 0x19, 0x0C, 0x17, 0x0E, 0x0F, 0x07, 0x00, 0x01, 0x00, 0x00, 0x11, 0x7F, 0x11,
    0x00, 0x6B, 0x90, 0x25, 0x44, 0xF2, 0xFD, 0xDF, 0xBF, 0x7E, 0xFF, 0xFD, 0xF9, 0xF3, 0xF7, 0x5D,
    0x01, 0x02, 0x0C, 0x0F, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x55, 0xF7, 0xA3, 0xC7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xC0, 0x20, 0x90, 0x50, 0x20, 0xB0, 0xD0, 0xC0, 0xC0, 0xC0, 0xC0, 0x80, 0xF8, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xA0, 0x40, 0xC0, 0xF0, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  }
};
#endif /* !SPRITE_ECM_ENABLED || !VDP_TARGET_F18A_ONLY || !ECM_ALL_PAGES_ENABLED */

/*
 * MAP_NBR_MARKS widened to cover map 1's whole 9 submaps (maps.h's own
 * comment) exposed real enemies elsewhere in the level that this port had
 * never actually rendered before -- reported directly as an invisible
 * (but still lethal) enemy. Computed every sprite number every real mark
 * across the whole map can ever need (walking each type-1/1b's
 * ENT_FLG_TRIGGERS-or-not sprbase and each type-3's own ent_sprseq
 * sequence against the real, untruncated xrick data) and found several
 * outside both SPRITE_PAGE_SIZE and the two existing hand-picked tables
 * above: 48-52 and 86-87 (page 1, dat_spritesTI1.c) and 98, 99, 104, 121
 * (page 2, dat_spritesTI2.c) -- the type-1a enemy that DOESN'T get the
 * ENT_FLG_TRIGGERS override (some of this level's marks have it, some
 * don't -- see sprites_them_walk[]'s own comment on why that override
 * isn't universal) genuinely does need 47-52 directly, and several other
 * type-3 (boulder-like) marks need their own distinct frames the same way
 * the boulder needed 97/105. Sprite 47 itself is already covered by
 * SPRITE_PAGE_SIZE (48, sprites.h) without needing a hand-picked entry
 * here. Not a contiguous range like sprites_them_walk[], so dispatched by
 * explicit sprite-number comparison below instead of index arithmetic.
 *
 * STREAMED PER MAP now (BUG FIXED twice over, both real address-space
 * overflows -- see git history for the blow-by-blow). This used to be a
 * real `const` initializer, then a per-map-streamed-but-fully-resident
 * BSS array -- EGYPT alone needs 32 entries (4096 bytes) once its real
 * type-1a/1b/2 sprite need was found (sprites.h's own MAP_NBR_SPRXTRA
 * comment), too much to keep resident at once. Now only
 * sprites_map1_extra_nums[] (which sprite NUMBERS are extras) stays
 * resident, streamed fresh at every map transition from that map's own
 * SPXNUM1.DAT/SPXNUM2.DAT; the actual 128-byte PATTERN for whichever one
 * is about to be drawn streams on demand into sprites_map1_extra_scratch
 * (below) instead -- see that array's own comment.
 * sprites_map1_extra_num_count tracks how many of the nums[] slots are
 * actually valid for whichever map is currently loaded -- SAMERICA uses
 * all 22, EGYPT all 32, and the dispatch loop must stop there instead of
 * scanning stale leftover slots from whichever map loaded previously. */
U16 sprites_map1_extra_nums[SPRITES_MAP1_EXTRA_NUMS_SIZE];
U16 sprites_map1_extra_num_count;

/* Genuinely resident ROLLING cache of the MAP_NBR_SPRXTRA_CACHED most
 * recently DOWNLOADED distinct extra sprites for whichever map is
 * currently loaded -- see sprites.h's MAP_NBR_SPRXTRA_CACHED comment
 * for why any caching exists here at all (flicker/slowdown fix: these
 * are the sprites that change VRAM slot often enough from an animated
 * entity's own walk-cycle that on-demand streaming every single time
 * was visible).
 *
 * MERGED onto the shared rolling_cache_t (engine/include/ecm_cache.h),
 * on request ("there are multiple cache systems, it would be great to
 * merge them all") -- this and the page0 cache just below used to each
 * carry their own hand-written tag[]/age[]/find()/claimSlot(), identical
 * in everything but which arrays they closed over. Storage stays exactly
 * where it always was (sprites_map1_extra_cached[]/_tag[]/_age[], same
 * extern surface sprites.h already exposes) -- only the find/claim/reset
 * logic itself is shared now, via rolling_cache_get()/rolling_cache_reset().
 *
 * Indexed by CACHE SLOT: sprites_map1_extra_cache_tag[] records which
 * real sprite NUMBER (0xFF = empty) each slot currently holds,
 * sprites_map1_extra_cache_age[] records how recently that slot was last
 * used. sprites_paint()'s own dispatch checks the tags first -- a match
 * is a cache hit, no network at all; a miss streams that one sprite's
 * pattern straight into whichever slot is least-recently-used, so it's
 * cached from then on. The result rolls with actual play: whichever
 * distinct extra sprites were most recently downloaded stay resident,
 * bounded by MAP_NBR_SPRXTRA_CACHED slots, no matter which map or which
 * order its own list happens to put them in.
 */
#if !SPRITE_ECM_ENABLED || !ECM_ALL_PAGES_ENABLED || !VDP_TARGET_F18A_ONLY
sprite_t sprites_map1_extra_cached[MAP_NBR_SPRXTRA_CACHED][SPRITE_SIZE];
/* 0xFFFF (never a valid sprite number -- SPRITES_NBR_SPRITES is 0xD5)
 * marks an empty slot. WIDENED from U8 to U16, matching
 * rolling_cache_t.tag's own widening (engine/include/ecm_cache.h) --
 * this cache doesn't itself need the extra range (every real sprite
 * number still fits in a byte), but the field type is shared across
 * every rolling_cache_t instance in this file, so it has to match.
 * Costs 1 extra byte per slot; MAP_NBR_SPRXTRA_CACHED is small enough
 * that this is noise. */
static U16 sprites_map1_extra_cache_tag[MAP_NBR_SPRXTRA_CACHED];
/* Recency stamp per slot -- see engine/include/ecm_cache.h's own
 * rolling_cache_t.age comment for the wraparound-safe comparison this
 * feeds. */
static U8 sprites_map1_extra_cache_age[MAP_NBR_SPRXTRA_CACHED];
static rolling_cache_t sprites_extra_cache;
#endif

/*
 * sprites_extraCacheReset -- called by maps.c's map_loadMap() at every
 * map transition. Sprite NUMBERS are only meaningful within whichever
 * map's own SPXPAT file is currently loaded (the same number means a
 * completely different pattern in a different map), so every tag has
 * to be invalidated here rather than carried over -- the cache then
 * rolls forward from empty, filling itself in from whatever the new
 * map's own gameplay actually requests, same as it does for the rest
 * of that map's lifetime.
 *
 * Kept as its OWN rolling_cache_t instance, not folded into
 * ecm_slot_cache/page0_cache above, precisely BECAUSE of that per-map
 * reset: this is the one cache in the file whose tag space is only
 * valid for the currently-loaded map, so it's the one cache that
 * genuinely needs wiping on a schedule shorter than "once at boot" --
 * folding it into either of those would mean every map transition also
 * evicting every hot ECM slot / page0 pattern for no reason, undoing
 * exactly the "stays resident across the whole game" property those are
 * for. Still shares their own recency clock (shared_cache_tick, at the
 * top of this file) -- one shared counter is enough for every instance
 * at once (ecm_cache.h's own rolling_cache_t.tick comment). */
#if !SPRITE_ECM_ENABLED || !ECM_ALL_PAGES_ENABLED || !VDP_TARGET_F18A_ONLY
void sprites_extraCacheReset(void) {
  sprites_extra_cache.slots = &sprites_map1_extra_cached[0][0];
  sprites_extra_cache.tag = sprites_map1_extra_cache_tag;
  sprites_extra_cache.age = sprites_map1_extra_cache_age;
  sprites_extra_cache.count = MAP_NBR_SPRXTRA_CACHED;
#if SPRITE_ECM_ENABLED
  /* Stock 9918A: the rest of the idle ECM buffer -- see
   * sprites_page0CacheInit()'s own comment. */
  if (!sysvid_nabu_hasF18A) {
    stock_cache_carve(&sprites_extra_cache, STOCK_EXTRA_BASE, STOCK_EXTRA_SLOTS);
  }
#endif
  sprites_extra_cache.tick = &shared_cache_tick;
  sprites_extra_cache.blockSize = SPRITE_SIZE;
  rolling_cache_reset(&sprites_extra_cache);
}
#endif

/* SAMERICA's real content -- see git history. Now streamed at runtime
 * from assets/SPXPAT1.DAT/SPXNUM1.DAT instead (see the STREAMED PER
 * MAP comment above). */

/*
 * e_bomb.c's fuse-burning-down (0x99-0xA2) and explosion (0xA8-0xAC)
 * sprite ranges (page 3, dat_spritesTI3.c) were extracted here as 15
 * hand-picked frames, the same way sprites_boulder_roll[] above was --
 * REMOVED (not replaced with a stub) to make room for engine/e_them.c's
 * type-1a/1b enemy code: at 128 bytes/frame, keeping even a couple of
 * frames left too little headroom to be worth it. e_bomb.c's own sprite
 * numbers for these ranges are unchanged (still 0x99-0xA2/0xA8-0xAC) --
 * they just no longer resolve to real pattern data below, so they fall
 * through to the same safe SPRITE_PAGE_SIZE clamp (-> sprite 0) as any
 * other out-of-range number. Bombs still tick down and explode/kill on
 * contact correctly (that's e_bomb.c's own logic, untouched); only the
 * dedicated fuse/explosion animation is gone, back to whatever sprite 0
 * looks like for those frames. Re-add by reverting this cut if bytes
 * free up elsewhere.
 */

/*
 * sprites_clear
 *
 * Marks every pseudo-sprite slot free (0xD1, the reference's own "unused"
 * marker) and every entity's spriteIndex unassigned, so sprites_paint2()
 * hands out fresh slots from scratch. Plain loop instead of the
 * reference's memset() -- avoids pulling in string.h for one caller.
 */
void sprites_clear(void) {
  U16 i;
  for (i = 0; i < (ENT_ENTSNUM + 1) * 4; i++) {
    sprite_table[i].y = 0xd1;
  }
  for (i = 0; i < ENT_ENTSNUM + 1; i++) {
    ent_ents[i].spriteIndex = 0xff;
    ent_ents[i].hwIndex = 0xff;
  }

  /* BUG FIXED, reported as "intro screen sprites aren't being correctly
   * hidden/erased on skip ... eventually get erased after gameplay loads
   * but that's too late for ending": the two loops above only ever
   * touched the CPU-side sprite_table[]/ent_ents[].spriteIndex mirrors,
   * never real VRAM -- despite every caller (main.c's one-time boot call,
   * engine/scr_imap.c's screen_introMap() seq==30 exit, whose own comment
   * already claims this hides the walking-Rick pseudo-sprite "the instant
   * this screen actually ends") assuming it actually hides whatever's
   * currently on screen the way ent_hideSprite()/delete_ent() (ents.c) do.
   * Without a real VRAM write, the previous screen's hardware sprite
   * bytes just sit there until something else happens to overwrite that
   * same slot -- normally gameplay's own first ents_paintAll() a moment
   * later, which is what made this look "eventually" fixed for the
   * ordinary map-intro-into-gameplay path but never for ENDING, which
   * loops back to the title screen instead of ever reaching gameplay.
   * TMS9918A sprites stop scanning the whole attribute table at the
   * first Y>=0xD0 entry (same convention ent_hideSprite() already relies
   * on) -- so one real write of 0xd1 to hardware sprite slot 0's Y byte
   * hides every currently-visible sprite at once, cheaper than looping
   * every slot the way ent_hideSprite() does per entity. Direct write,
   * not sprite_table[]-mediated, same reasoning sprites_paint()'s own
   * header gives for writing straight to VRAM.
   *
   * TRIED AND REVERTED TWICE now, for two different reported symptoms
   * (originally "the sprites in the samerica intro still stay for a
   * while [into gameplay]", not meaningfully changed by this guard;
   * then re-added for "ESC leaves sprites on screen" and REVERTED AGAIN,
   * this time because it produced a real regression -- "push 1 [after
   * ESC] freezes the level-select screen", root-caused via a MAME
   * debugger trace showing the CPU still correctly executing
   * sys_nabu_musicTickDelay()'s own busy-wait over and over, i.e.
   * screen_levelSelect()'s wait loop never receiving a matching
   * keypress again -- consistent with interrupts (specifically NABU-LIB's
   * own keyboard ISR, which pushes bytes into _kbdBuffer[] for
   * isKeyPressed()/getChar() to see) staying disabled from this exact
   * call site onward. Given TWO real regressions from this guard against
   * ZERO confirmed fixes, do not re-add it a third time without first
   * confirming NABU_DisableInterrupts()/EnableInterrupts() actually nest/
   * balance safely in whatever calls this from inside an already-live
   * gameplay frame -- unlike its other callers (env_paintGame(), tiles.c,
   * screen boot-time code), THIS call site sits deep inside the running
   * per-frame gameplay loop, an environment nothing else wrapping this
   * pattern has ever run in before.
   *
   * BUG FIXED: this wrote 0xd1, but only a Y of exactly 0xD0 ends the
   * VDP's sprite list -- 0xd1 just moved hardware sprite 0 off-screen and
   * left sprites 1-31 showing. 0xD0 now, as the comment above intended. */
  vdp_setWriteAddress(gSprite);
  IO_VDPDATA = 0xd0;
}

/* Per-frame hardware sprite packing.
 *
 * BUG FIXED, reported as "on level 4 with 4 enemies alive, Rick's bullet
 * and dynamite don't appear (sound/logic still work)": every entity used
 * to own 4 hardware sprites at spriteIndex for its whole life, and 13
 * entities x 4 = 52, but the VDP attribute table only has 32. Entities
 * also kept their slots while off-screen. Once 8 slot groups were taken,
 * the next entity (typically the bullet or bomb, created last) got
 * hardware sprites 32+, which the VDP never draws.
 *
 * Now spriteIndex only owns the PATTERN slot (208 of 256 names for all
 * 13, fits), and hardware sprites are handed out fresh every frame, only
 * to entities actually on screen, packed from 0 in ent_ents[] order --
 * Rick (1), bullet (2), bomb (3) always come first. Anything past 8
 * visible entities isn't drawn that frame. The 0xD0 end-of-list marker
 * after the last one used hides every stale slot beyond it in one write. */
static U8 sprites_hwNext;

void sprites_beginFrame(void) {
  sprites_hwNext = 0;
}

void sprites_endList(U8 hwNext) {
  if (hwNext < 32) {
    NABU_DisableInterrupts();
    vdp_setWriteAddress((U16)(gSprite + (U16)hwNext * 4));
    IO_VDPDATA = 0xd0;
    NABU_EnableInterrupts();
  }
}

void sprites_endFrame(void) {
  sprites_endList(sprites_hwNext);
}

#if SPRITE_ECM_ENABLED
/* F18A fast path: blasts one SPRITE_SIZE (128-byte) plane to the VDP data
 * port with OTIR (21 T-states/byte). The F18A has no VRAM access-window
 * limit, so it needs none of VDP_WRITE_SETTLE()'s padding -- that and the
 * C loop overhead cost ~5x more per byte. NEVER call this on a stock
 * TMS9918A: OTIR is faster than its active-display write window. Caller
 * sets the VRAM write address and holds interrupts off. */
static void vdp_outPlaneF18A(const U8 *src) __z88dk_fastcall __naked {
  (void)src;
  __asm
    ld c, 0xA0      ; IO_VDPDATA
    ld b, 128       ; SPRITE_SIZE
    otir
    ret
  __endasm;
}
#endif

/*
 * sprites_paint
 *
 * Paints sprite <spriteNumber> as 4 hardware sprites at pseudo-sprite slot
 * <spriteIndex>, positioned at fb-coordinates <x>,<y>; reloads the VDP
 * pattern data too when <load_pattern> is set (skipped otherwise -- the
 * same pattern is already resident from the previous frame).
 *
 * Adapted from the reference: no F18A per-page palette/pattern remap (see
 * this file's header's "NOT ported" section), no cartridge bank switching
 * (SWITCH_IN_BANK and nOldBank dropped, same as every other file in this
 * port), and the attribute bytes (y/x/ch/col) are written straight to the
 * VDP sprite attribute table here, immediately, in addition to
 * sprite_table[] -- the reference only updates sprite_table[] and relies
 * on a later bulk flush (part of a frame-loop structure this port doesn't
 * have) to actually reach VRAM; this port has no such flush, so it writes
 * directly instead, the same way ents.c's delete_ent()/ent_hideSprite()
 * do for the hide case.
 */
void sprites_paint(U16 spriteNumber, U16 spriteIndex, U8 hwIndex, U16 x, U16 y, U16 load_pattern) {
  U8 chr = (U8)(spriteIndex * 4);
#if SPRITE_ECM_ENABLED
  U8 walkPage = 0;
  S16 walkIdx = sysvid_nabu_hasF18A ? sprite_ecm_lookup(spriteNumber, &walkPage) : -1;
  /* Holds the one combined ecm_fetch_slot() result (all 3 interleaved
   * planes, SPRITE_PLANES_SIZE bytes) once walkIdx>=0 -- fetched ONCE,
   * right where plane0/shape used to be fetched alone, then sliced for
   * plane1/plane2 further down instead of each doing its own separate
   * fetch. See ecm_fetch_slot()'s own header for the "why one combined
   * fetch instead of 3" writeup. */
  const U8 *ecmSlot = NULL;
  /* Caching itself no longer branches here at all -- every walkIdx>=0
   * sprite (not just the old hardcoded rick/spearguy/misc0/proj
   * categories) goes through the SAME ecm_slot_cache now
   * (ecm_fetch_slot(), further below); see engine/include/ecm_cache.h
   * for the merge writeup. */
  U8 spriteColor;

  if (walkIdx >= 0) {
    /* Real per-page palette group -- xrick/src/sprites.c's own
     * `pal = 4 + spritePage*2` formula, verbatim (page0->4, page1->6,
     * page2->8, page3->10, matching sprf0-3pal loaded at F18A RAM
     * offsets 16/24/32/40 -- hal/sysvid_nabu.c's set_halfbitmap()).
     * Page 4 would compute 12, but the reference clamps anything past
     * 10 back down to 8 ("if (pal > 10) pal = 8") -- page 4 shares page
     * 2's own real palette group instead of getting a dedicated one, and
     * this build follows that exactly rather than loading a 5th group
     * for a page whose colors the original authors already chose to
     * reuse. */
    spriteColor = (U8)(4 + walkPage * 2);
    if (spriteColor > 10) {
      spriteColor = 8;
    }
  } else if (sysvid_nabu_hasF18A) {
    spriteColor = SPRITE_ECM_PAL_FALLBACK;
  } else {
    spriteColor = SPRITE_COLOR_WHITE_9918;
  }
#else
  U8 spriteColor = sysvid_nabu_hasF18A ? SPRITE_COLOR_WHITE_F18A : SPRITE_COLOR_WHITE_9918;
#endif

  /* Quadrant k: +16 down for odd k, +16 right for k >= 2, pattern
   * chr + 4*k (top-left, bottom-left, top-right, bottom-right). Filled
   * into sprite_table[] (RAM) here; copied to the VDP at the end. */
  {
    sprite_data_t *t = &sprite_table[spriteIndex];
    U8 k;

    for (k = 0; k < 4; k++, t++) {
      /* - 1: the TMS9918A (and F18A/Pico9918) draws a sprite one line
       * BELOW its Y value, so without it every sprite sat 1px lower than
       * in xrick -- noticed on MBASE's first projectile, which then
       * missed its gun barrel (the TI version has the same offset).
       * Can't produce the 0xD0 end-of-list value: y is at most 191 after
       * sprites_paint2()'s clipping. */
      t->y = (U8)y - 1 + ((k & 1) << 4);
      t->x = (U8)x + ((k & 2) << 3);
      t->ch = chr + (k << 2);
      t->col = spriteColor;
    }
  }
  /* Pattern first, attributes (position/colour) after -- see the
   * attribute write at the end of this function. */
  if (load_pattern) {
    /* BUG FIXED: sprite numbers 97/105 (the boulder's own rolling
     * animation -- see maps.c's own map_dataSize[0] comment on why
     * they're just two more entries in SAMERICA's own extra-sprite list
     * now, not a special case here) used to read past sprites_data0[]'s
     * end when sent straight through the SPRITE_PAGE_SIZE fallback below
     * -- that's what showed up as a garbled/hollow outline instead of a
     * filled rolling boulder, not a color problem (see
     * SPRITE_COLOR_WHITE_F18A/_9918 above). Anything still >=
     * SPRITE_PAGE_SIZE and not found in the extra-sprite search below
     * safely clamps to sprite 0 instead of reading garbage. */
    /* Initialized here (not left for the walkIdx<0 block below to set)
     * purely to silence SDCC's "may be used before initialization"
     * warning -- it can't see that walkIdx>=0 (skipping that block) and
     * the walkIdx>=0 block a few lines down (which always assigns src)
     * are mutually exclusive/exhaustive. Functionally dead: always
     * overwritten before use either way. */
    const sprite_t *src = NULL;
    U16 safeSpriteNumber;
    U8 j; /* SIZE PASS: U8 not U16 -- every use here stays within 0-127
           * (SPRITE_SIZE's own loops, sprites_map1_extra_num_count's max
           * of 24) */

    /* BUG FIXED, reported directly ("movement is still very slow... is
     * any boulder data still streaming?"): this whole block used to run
     * UNCONDITIONALLY, even though its result (`src`) gets thrown away a
     * few lines below whenever walkIdx>=0 -- the F18A ECM override
     * always wins (this function's own SPRITE_ECM_ENABLED block further
     * down). On THIS build (F18A-only, SAMERICA/CASTLE/MBASE pages all
     * on) walkIdx is >=0 for nearly everything SAMERICA ever draws, so
     * this was a genuine, wasted sys_nabu_loadAssetOffset() network
     * round-trip (sprites_map1_extra_scratch's own stream below) on
     * every single frame change for the boulder and any other "extra"
     * entity not in ecm_cache_*_nums[] -- real HCCA traffic for a result
     * that was never actually drawn. Skipping this entire search
     * whenever walkIdx>=0 removes that wasted stream; `src` still ends
     * up correctly set either way, since the walkIdx>=0 block right
     * after this one unconditionally assigns it from ecm_slot_cache
     * (ecm_fetch_slot(), a cache hit or a fresh stream either way)
     * regardless of what (if anything) this block would have found. */
#if SPRITE_ECM_ENABLED
    if (walkIdx < 0) {
#endif
    /* Whole body below only compiled when !ECM_ALL_PAGES_ENABLED or this
     * build can genuinely run on stock hardware (!VDP_TARGET_F18A_ONLY)
     * (hal/sysvid_nabu.h) -- walkIdx<0 can't happen otherwise
     * (sprite_ecm_lookup() always finds a real page, and hasF18A is
     * compile-time-guaranteed true), so this whole block would be
     * genuinely unreachable dead code otherwise -- left empty rather
     * than compiled-but-unreachable so it doesn't need sprites_map1_
     * extra_cached[]/_scratch[]'s own (now conditionally-removed)
     * declarations to exist. `src` stays correctly NULL-initialized
     * either way -- the walkIdx>=0 block right after this one
     * unconditionally overwrites it when this body is empty, since
     * walkIdx<0 is the only way to reach here.
     */
#if !SPRITE_ECM_ENABLED || !ECM_ALL_PAGES_ENABLED || !VDP_TARGET_F18A_ONLY
    if (spriteNumber >= SPRITE_THEM_WALK_FIRST && spriteNumber <= SPRITE_THEM_WALK_LAST) {
      /* 142/144 (right, pose A) -> 0, 143 (right, pose B) -> 1,
       * 145/147 (left, pose A) -> 2, 146 (left, pose B) -> 3 -- see this
       * table's own comment above */
      if (spriteNumber < 145) {
        src = sprites_them_walk[(spriteNumber == 143) ? 1 : 0];
      } else {
        src = sprites_them_walk[(spriteNumber == 146) ? 3 : 2];
      }
    } else
    {
      /* SIZE PASS: table + linear search over it compiles smaller on
       * Z80/SDCC than one inline compare-and-branch per entry, same
       * lookup, same result. sprites_map1_extra_nums[] is this file's own
       * top-level streamed buffer now (STREAMED PER MAP comment up
       * there) -- only search the first sprites_map1_extra_num_count of
       * them, not the full MAP_NBR_SPRXTRA capacity: whichever map isn't
       * currently loaded left its own stale entries in the unused tail
       * slots. On a match: check the rolling LRU cache
       * (sprites_map1_extra_cached's own header) by real sprite NUMBER,
       * not by `j` -- already cached is a hit with zero network traffic;
       * a miss streams that ONE sprite's own 128-byte pattern straight
       * into whichever cache slot the shared rolling cache picks
       * (rolling_cache_get(), engine/include/ecm_cache.h), so a repeat
       * request for the same sprite number is a hit from then on.
       * Filename built the same mutable-single-digit-template way as
       * maps.c's map_loadMap() -- env_map (env.h) is which map is
       * currently loaded. */
      src = NULL;
      for (j = 0; j < sprites_map1_extra_num_count; j++) {
        if (spriteNumber == sprites_map1_extra_nums[j]) {
          /* SPXPATn.DAT, packed into RICK.DAT one fixed stride per map
           * (res.h). */
          src = rolling_cache_get(&sprites_extra_cache, (U8)spriteNumber, RES_FILE,
                                   RES_SPXPAT_BASE + env_map * RES_SPXPAT_STRIDE +
                                   j * (SPRITE_SIZE / 4), spriteNumber);
          break;
        }
      }
      if (src == NULL) {
        /* Own dedicated rolling LRU cache now */
        safeSpriteNumber = (spriteNumber < SPRITE_PAGE_SIZE) ? spriteNumber : 0;
        src = rolling_cache_get(&page0_cache, safeSpriteNumber, RES_FILE,
                                 RES_SPR0 + safeSpriteNumber * (SPRITE_SIZE / 4),
                                 spriteNumber);
      }
    }
#endif /* !SPRITE_ECM_ENABLED || !ECM_ALL_PAGES_ENABLED || !VDP_TARGET_F18A_ONLY */
#if SPRITE_ECM_ENABLED
    }
#endif

#if SPRITE_ECM_ENABLED
    /* BUG FIXED, reported ("close, his hat color is right, some of his
     * skin is correct... blue on his face that shouldn't be there"):
     * sprites_data0 (dat_spritesTI0.c) is this project's OWN single-color
     * TMS9918A rendering of Rick -- a SEPARATE drawing from the F18A
     * cartridge's own multicolor version (ti/f18a/sprf00/01/02.c), not
     * the same artwork split into a shape layer plus two color layers.
     * Directly compared the actual bytes: 28-37 of every 128 differ, for
     * EVERY ONE of Rick's 10 walk frames, between sprf00.c (bit 0's real
     * source in the reference -- xrick/src/sprites.c's own F18A path
     * copies bit0 from sprf00.c, never from dat_spritesTI0.c) and
     * sprites_data0. Once ECM=3 is on, "bit 0" isn't a separate opacity
     * flag any more -- ONLY the all-zero index is hardware-forced
     * transparent, so mismatched shape data doesn't just misalign an
     * outline, it puts WRONG COLORS on real, opaque pixels wherever the
     * two drawings disagree (exactly "blue on his face"). Fixed by
     * overriding the shape source with RICKBIT0.DAT (sprf00.c's own
     * Rick frames, extracted the same way as RICKBIT1/2.DAT) whenever
     * walkIdx>=0, so all three planes come from the same matched,
     * artist-drawn source -- same on-demand-scratch streaming as
     * bit1/bit2 below, reusing the same buffer (loaded, used, then
     * reloaded) rather than adding a third resident/scratch array.
     *
     * EXTENDED to a real per-page filename ("all sprites, all levels"),
     * not just page 0's own RICKBIT0.DAT -- SPRxB0.DAT, x=walkPage
     * (0=SAMERICA/page0, 1=EGYPT/page1 so far, SPRITE_ECM_MAX_PAGE's own
     * comment). */
    if (walkIdx >= 0) {
      /* walkIdx>=0 only ever happens on F18A (see its declaration), so
       * all 3 interleaved planes go out through the OTIR fast path --
       * plane0/shape is the first SPRITE_SIZE bytes, then BIT1, BIT2. */
      ecmSlot = ecm_fetch_slot(walkPage, walkIdx, spriteNumber);
      NABU_DisableInterrupts();
      vdp_setWriteAddress((U16)(gSpritePat + (U16)chr * 8));
      vdp_outPlaneF18A(ecmSlot);
      vdp_setWriteAddress((U16)(GAME_VRAM_SPRITEPAT_BIT1 + (U16)chr * 8));
      vdp_outPlaneF18A(ecmSlot + SPRITE_SIZE);
      vdp_setWriteAddress((U16)(GAME_VRAM_SPRITEPAT_BIT2 + (U16)chr * 8));
      vdp_outPlaneF18A(ecmSlot + 2 * SPRITE_SIZE);
      NABU_EnableInterrupts();
      goto write_attrs;
    }
#endif

    NABU_DisableInterrupts();
    vdp_setWriteAddress((U16)(gSpritePat + (U16)chr * 8));
    for (j = 0; j < SPRITE_SIZE; j++) {
      IO_VDPDATA = src[j];
      VDP_WRITE_SETTLE(); /* see hal/sysvid_nabu.c's own comment */
    }
    NABU_EnableInterrupts();

#if SPRITE_ECM_ENABLED
    /* F18A 8-color sprite mode's two extra color-bit planes -- see this
     * file's own SPRITE_ECM_PAL_FALLBACK comment for the full root-cause
     * writeup and fix. Real data for walkIdx>=0 (any covered sprite on a
     * page SPRITE_ECM_MAX_PAGE handles), all-zero otherwise -- every
     * other sprite still needs
     * SOMETHING written here every reload, since this VRAM slot
     * (chr-indexed, shared/rotated across whichever entities are
     * currently on screen) may hold a PREVIOUS entity's real color data
     * from a few frames ago otherwise; with all-zero color-bit planes,
     * that entity's final 3-bit color index reduces to just bit 0's own
     * value (0=transparent, forced by F18A hardware regardless of
     * palette content; 1=white via sprite_ecm_pal_fallback[]'s own
     * index-1 entry) -- unaffected by whatever shape it actually is.
     * Skipped entirely on stock TMS9918A, which has no such planes to
     * write.
     *
     * walkIdx>=0 fetched this frame's real color-plane data (all 3
     * planes together) through ecm_fetch_slot() up above -- a hit costs
     * no network access at all; only a genuine cache miss is a real
     * round-trip, and that round-trip now covers all 3 planes at once
     * instead of one per plane. plane1/plane2 here are just slices of
     * that ONE already-fetched 384-byte block, not separate fetches --
     * confirmed the plane assignment below (BIT1=middle third,
     * BIT2=last third) is correct already (tried swapping it once,
     * reported "that made it worse"). */
    /* Only reached with walkIdx<0 now (walkIdx>=0 returned early through
     * the OTIR fast path above), so on F18A both color-bit planes are
     * always all-zero here. */
    if (sysvid_nabu_hasF18A) {
      NABU_DisableInterrupts();
      vdp_setWriteAddress((U16)(GAME_VRAM_SPRITEPAT_BIT1 + (U16)chr * 8));
      for (j = 0; j < SPRITE_SIZE; j++) {
        IO_VDPDATA = 0;
      }
      vdp_setWriteAddress((U16)(GAME_VRAM_SPRITEPAT_BIT2 + (U16)chr * 8));
      for (j = 0; j < SPRITE_SIZE; j++) {
        IO_VDPDATA = 0;
      }
      NABU_EnableInterrupts();
    }
#endif
  }

#if SPRITE_ECM_ENABLED
write_attrs:
#endif
  /* BUG FIXED, reported as a one-frame "small Rick sprite and some blue
   * sprite" on entering a submap: the attributes used to be written
   * BEFORE the pattern upload above. On a new submap every entity gets a
   * fresh pattern slot -- often one Rick or an old enemy just freed -- so
   * the new position/colour went live pointing at the slot's OLD picture
   * until the upload finished (longest when that upload is a network
   * fetch), and a frame could show it. Writing the attributes last means
   * a slot is only ever shown once it holds the right picture.
   *
   * The 4 hardware sprites are consecutive in the attribute table, so one
   * address setup covers all 16 bytes (VDP auto-increments). Written at
   * hwIndex (this frame's packed hardware position), NOT spriteIndex --
   * see sprites_beginFrame(). sprite_table[] stays indexed by spriteIndex:
   * it's the pattern-slot bookkeeping sprites_paint2() allocates from. */
  {
    const U8 *t = (const U8 *)&sprite_table[spriteIndex];
    U8 k;

    NABU_DisableInterrupts();
    vdp_setWriteAddress((U16)(gSprite + (U16)hwIndex * 4));
    for (k = 0; k < 16; k++) {
      IO_VDPDATA = t[k];
    }
    NABU_EnableInterrupts();
  }
}

/*
 * sprites_paint2
 *
 * Paints entity <entityNumber> (an index into ent_ents[]) at its current
 * map position, assigning it a pseudo-sprite slot on first use and
 * reusing the same slot every call after (so a moving entity doesn't leak
 * a new hardware sprite every frame). See this file's header for how the
 * off-screen branch differs from the reference.
 */
void sprites_paint2(U16 entityNumber) {
  U16 x_map, y_map;
  U16 x_fb, y_fb;
  const U16 width = 0x20, height = 0x15;
  U16 idx;
  U16 i;

  /* Not drawn unless it gets hardware sprites below -- see
   * sprites_beginFrame(). */
  ent_ents[entityNumber].hwIndex = 0xff;

  if (env_highlight) {
    if (ent_ents[entityNumber].sprite == 0) {
      /* for highlight, just load one of the explosion sprites */
      ent_ents[entityNumber].sprite = 36;
    }
  }

  /* get map/px */
  x_map = ent_ents[entityNumber].x;
  y_map = ent_ents[entityNumber].y;

  idx = ent_ents[entityNumber].spriteIndex;

  /* clip */
  if (maps_clip(x_map, y_map, width, height)) { /* return if not visible */
    if (idx != 0xff) {
      /* 0xd2, not 0xd1: 0xd1 means "pattern slot free for reuse by any
       * entity", 0xd2 means "this entity still owns it but it's
       * temporarily offscreen" -- same distinction the reference makes.
       * No VDP write any more: an off-screen entity just doesn't get
       * hardware sprites this frame (sprites_beginFrame()). */
      for (i = 0; i < 4; i++) {
        sprite_table[idx + i].y = 0xd2;
      }
    }
    return;
  }

  /* Out of hardware sprites (8 entities already on screen): skip this one
   * for this frame. lastSpriteDrawn is left alone so its pattern still
   * loads when it does get drawn. */
  if (sprites_hwNext >= 32) {
    return;
  }

  /* assign a sprite index if necessary */
  if (ent_ents[entityNumber].spriteIndex == 0xff) {
    U16 i;
    for (i = 0; i < (ENT_ENTSNUM + 1) * 4; i += 4) {
      if (sprite_table[i].y == 0xd1) {
        ent_ents[entityNumber].spriteIndex = i;
        break;
      }
    }
  }

  /* convert to fb/px */
  x_fb = x_map + MAPS_FB_X + 32;
  y_fb = y_map - MAPS_FB_Y;

  ent_ents[entityNumber].hwIndex = sprites_hwNext;
  sprites_hwNext += 4;

  sprites_paint(ent_ents[entityNumber].sprite,        /* pattern to draw */
                ent_ents[entityNumber].spriteIndex,   /* pattern slot */
                ent_ents[entityNumber].hwIndex,       /* hardware sprites */
                x_fb, y_fb,                           /* location */
                ent_ents[entityNumber].lastSpriteDrawn != ent_ents[entityNumber].sprite);

  ent_ents[entityNumber].lastSpriteDrawn = ent_ents[entityNumber].sprite;
}

/* eof */
