/*
 * engine/include/sprites.h -- ported from xrick/include/sprites.h, unchanged.
 * No TI-specific content found.
 */

/*
 * NOTES -- PC version
 *
 * A sprite consists in 4 columns and 0x15 rows of (U16 mask, U16 pict),
 * each pair representing 8 pixels (cga encoding, two bits per pixels).
 * Sprites are stored in 'sprites.bin' and are loaded by spr_init. Memory
 * is freed by spr_shutdown.
 *
 * There are four sprites planes. Plane 0 is the raw content of 'sprites.bin',
 * and planes 1, 2 and 3 contain copies of plane 0 with all sprites shifted
 * 2, 4 and 6 pixels to the right.
 */


#ifndef _SPRITES_H_
#define _SPRITES_H_

#include "config.h"
#include "ents.h"

/*
 * methods
 */
void sprites_setDepth(U16);
/* (spriteNumber, spriteIndex = pattern slot, hwIndex = first hardware
 * sprite, x, y, load_pattern) */
void sprites_paint(U16, U16, U8, U16, U16, U16);
void sprites_paint2(U16);

/* Per-frame hardware sprite packing -- see sprites.c. ents_paintAll()
 * brackets its loop with these; begin resets the next free hardware
 * sprite to 0, end writes the VDP's 0xD0 end-of-list marker after the
 * last one used so nothing stale past it can show. sprites_endList(n)
 * is the same marker at an explicit position (scr_imap.c's walking-Rick
 * preview uses hardware sprites 0-3 only). */
void sprites_beginFrame(void);
void sprites_endFrame(void);
void sprites_endList(U8 hwNext);

void sprites_clear(void);

/* Warms the shared, generic ECM plane rolling cache (engine/
 * include/ecm_cache.h) with submap `submap`'s (0-8) own real
 * mark-driven extra sprite numbers -- map_loadMap() calls this once for
 * submap 0 at SAMERICA's own level start, map_chain() calls it for
 * every other submap transition. REPLACES the old ecm_swap_load(): that
 * bulk-fetched a bespoke, per-submap COMPRESSED blob (SWP<n>.DAT) into
 * a dedicated 2000-byte buffer; this instead just fetches each of that
 * submap's own sprite numbers (dat_ecmSwapSamerica.c's *_nums[] tables,
 * unchanged) through the SAME rolling cache every other ECM plane fetch
 * uses (engine/sprites.c's own ecm_plane_cache[]) and the SAME
 * already-existing per-page/plane asset files (SPRxBy.DAT) the
 * on-demand fallback path streams from -- no separate compressed
 * format, offset table, or asset file needed any more. A cache miss
 * during this warm-up is exactly one real HCCA round-trip per (sprite
 * number, plane) pair, same "level start or submap change" allowed
 * trigger as before; anything that doesn't fit in ecm_plane_cache[]'s
 * own slot budget just streams on demand instead, same graceful
 * degradation the old swap buffer's own greedy truncation had, just
 * governed by LRU recency now instead of a build-time greedy prefix.
 * Declared unconditionally (SPRITE_ECM_ENABLED isn't guaranteed defined
 * yet at this header's own first expansion point in the unity build,
 * same reasoning as sprites_clear() etc. above) -- the definition
 * itself is still #if SPRITE_ECM_ENABLED'd in engine/sprites.c, and
 * both call sites (maps.c) gate their own call the same way main.c's
 * own SPRITE_ECM_ENABLED call site does. */
void ecm_prefetchSubmap(U16 submap);

/* One-time boot init for the stock page0 rolling cache (engine/
 * sprites.c's own page0_cache) -- invalidates every slot. UNCONDITIONAL
 * (not #if SPRITE_ECM_ENABLED): needed on every build, since page0 is
 * the ONLY sprite pattern source stock hardware ever has (real 9918A
 * runtime detection forces walkIdx<0 for everything, sprites_paint()'s
 * own comment), and is still the F18A fallback for anything not covered
 * by an ECM page even when SPRITE_ECM_ENABLED is on. This cache's own
 * tag space (sprite number, 0-SPRITE_PAGE_SIZE-1) is GLOBAL, not
 * per-map, so it only needs invalidating once -- unlike
 * sprites_extraCacheReset() just above, which is per-map. Call once at
 * boot, right after sprites_clear(). */
void sprites_page0CacheInit(void);

#define SPRITES_NBR_SPRITES (0xD5)
#define SPRITE_SIZE (8*4*4)
/* BUG FIXED: this was truncated to 37 (indices 0-36) at one point,
 * covering only e_rick.c/e_bullet.c/e_bomb.c's own sprite numbers --
 * missed that box/bonus/speed-bonus marks (still stub_ebehavior.c
 * no-ops, but their sprite is set once at creation from
 * ent_entdata[].spr and never changes) and the type-1a enemy marks that
 * DON'T get the ENT_FLG_TRIGGERS override (sprites_them_walk[]'s own
 * comment -- not every mark has all four trigger flags) both need real
 * page-0 sprites up to 47 once MAP_NBR_MARKS (maps.h) covers the whole
 * map instead of just submap 0. Back to the real page-0 size (6*8=48,
 * indices 0-47) -- checked directly this time (see sprites.c's
 * sprites_map1_extra[] comment for the full walk of every real mark's
 * sprite needs across the whole map): 47 is the highest page-0 index any
 * of them uses, so 48 is exactly sufficient, not a guess. */
#define SPRITE_PAGE_SIZE (48)
#define SPRITE_FINAL_SIZE (21)

typedef U8 sprite_t;

/* One physical ECM slot's worth of INTERLEAVED color-plane data -- all
 * 3 planes (BIT0/BIT1/BIT2) back to back, 128 bytes each -- rather than
 * 3 separate SPRITE_SIZE-byte files. Added on request ("would a few
 * large .dat files instead of many smaller files offer any negatives/
 * positives to speed"): the fixed per-call overhead of an
 * rn_FileRead() round-trip (opcode+filename+offset+length request,
 * then a synchronous wait for the IA's own response, hal/RetroNET-
 * FileRead-only.c) is paid once per CALL, not once per byte -- 3
 * separate SPRITE_SIZE fetches for one sprite's 3 planes paid that
 * fixed cost 3 times over for the exact same total 384 bytes of real
 * payload. One combined rolling_cache_get() call against
 * SPRITE_PLANES_SIZE-sized blocks (engine/sprites.c's own
 * ecm_fetch_slot(), replacing the old per-plane ecm_fetch_plane())
 * fetches all 3 in a single round-trip on a cold miss.
 *
 * REQUIRES the .DAT files themselves to actually be laid out this way
 * -- plane0/plane1/plane2 interleaved per physical slot, not the old
 * plane-major "<page><plane>" layout -- regenerated from source art,
 * not something this codebase alone can produce. */
#define SPRITE_PLANES_SIZE (3 * SPRITE_SIZE)

#if SPRITE_ECM_ENABLED
/* Backing storage for engine/sprites.c's own ecm_slot_cache -- see
 * engine/include/ecm_cache.h for the rolling-cache mechanism and
 * SPRITE_PLANES_SIZE's own comment just above for why each slot here
 * is a full interleaved 3-plane block instead of one plane.
 * SPRITE_ECM_ENABLED-only (unlike the page0 cache below): nothing ever
 * populates this on a build with no ECM data at all.
 *
 * Extern (not `static` in sprites.c), on request ("would it be possible
 * for the PCM cache to share the same location as ECM sprites?"):
 * engine/sounds.c's WAAAAA cheat-key PCM playback (sounds_playWaaaaa())
 * borrows this exact memory as its own scratch buffer instead of
 * forcing tiles_banks_shared to grow just to fit WAAAAA_PCM_SIZE
 * (tiles.h's own union tiles_banks_shared_u comment has the full
 * account) -- the SAME "time-share one resident buffer across users who
 * are never both live at once" pattern tiles_banks_shared_u already
 * established, just a second instance of it. Safe on the same terms
 * tiles_banks_shared's own reuse already relies on: PCM playback is a
 * rare, deliberate, CPU-blocking one-off, never concurrent with a
 * sprite actually being painted. sounds_playWaaaaa() calls
 * sprites_ecmSlotCacheInvalidate() (below) right after playback so
 * every cache tag is invalidated once its underlying bytes may have
 * been overwritten -- the next sprite draw just streams fresh, same
 * one-time cost a normal cold cache miss already has. Only borrowed
 * when SPRITE_ECM_ENABLED (this array doesn't exist otherwise) --
 * engine/sounds.c falls back to tiles_banks_shared on a build with no
 * ECM at all, same as before the graphics-cache work.
 *
 * SIZE: costs ECM_SLOT_CACHE_SLOTS * (SPRITE_PLANES_SIZE+2+1) = *387
 * bytes total (the +2 is the U16 tag, +1 the U8 age -- ecm_cache.h's
 * own rolling_cache_t comment). Was 23 (~21 slots were seen in use on
 * SAMERICA's first submap); raised to 29 once the game was feature
 * complete, spending the spare stack room build.ps1 reports. Also
 * enlarges the stock-hardware caches carved from this buffer
 * (sprites.c's sprites_page0CacheInit()): 19 -> 37 extra slots. Each
 * slot costs 387 bytes of stack room; build.ps1 fails the build below
 * 512. */
#define ECM_SLOT_CACHE_SLOTS 29
#define ECM_SLOT_CACHE_TOTAL_BYTES (ECM_SLOT_CACHE_SLOTS * SPRITE_PLANES_SIZE)
extern U8 ecm_slot_cache_slots[ECM_SLOT_CACHE_SLOTS][SPRITE_PLANES_SIZE];
/* Invalidates every ECM slot cache entry -- call this after writing
 * anything into ecm_slot_cache_slots for a borrowed use (PCM playback,
 * so far); the tag/age arrays would otherwise still believe whatever
 * real plane data used to occupy each slot is still there. Cheap
 * (rolling_cache_reset(), engine/include/ecm_cache.h) -- not a real
 * HCCA cost by itself, just makes the NEXT draw of whatever was cached
 * a cold miss again. */
void sprites_ecmSlotCacheInvalidate(void);
#endif

/* Backing storage for engine/sprites.c's own page0_cache -- the stock
 * (SPR0.DAT) single-bitplane pattern cache, SPRITE_SIZE bytes per
 * slot (a page0 pattern is never interleaved with anything -- there's
 * only one plane). UNCONDITIONAL (unlike ecm_slot_cache_slots above):
 * needed on every build, ECM or not -- see sprites_page0CacheInit()'s
 * own comment.
 *
 * SIZE: costs PAGE0_CACHE_SLOTS * (SPRITE_SIZE+2+1) = *131 bytes total.
 * Small by design -- on an F18A-capable build this is only ever the
 * fallback for whatever ECM doesn't cover, not the primary path (the
 * DEBUG_LOAD_FILENAMES session that sized ECM_SLOT_CACHE_SLOTS above
 * never touched this cache at all); on a stock-only build it becomes
 * the ONLY sprite cache, and would want raising -- tune against real
 * build headroom either way.
 *
 * On a build WITH ECM, stock hardware borrows the idle ECM buffer for a
 * full-size cache at runtime instead (sprites.c's sprites_page0CacheInit()),
 * so this stays 1 there. On a 9918A-only build (no ECM buffer to borrow,
 * and none of its 8832 bytes spent) it's sized for all 48 page-0 sprites
 * directly -- on request, "helped with current build but not when changing
 * VDP_TARGET_9918A_ONLY to 1". */
#if SPRITE_ECM_ENABLED
#define PAGE0_CACHE_SLOTS 1
#else
#define PAGE0_CACHE_SLOTS 48 /* SPRITE_PAGE_SIZE: all of SPR0.DAT */
#endif
extern U8 page0_cache_slots[PAGE0_CACHE_SLOTS][SPRITE_SIZE];

/* sprites_data0 is not `const`: loaded at runtime from assets/SPR0.DAT
 * via sys_nabu_loadAsset() -- see engine/dat_spritesTI0.c and
 * hal/sys_nabu_load.h for why. Still read-only in practice everywhere
 * except that one startup load. sprites_data1..4 (pages 1-4) aren't ported
 * yet -- see dat_spritesTI0.c's header -- so they're left `const` here
 * pending dat_spritesTI1..4.c bodies that don't exist yet either way. */
/* Sized via SPRITES_DATA0_RESIDENT_SIZE (hal/sysvid_nabu.h) -- only 1
 * byte when SKIP_SPR0_GAMEPLAY_LOAD is on, so nothing may index it then. */
extern sprite_t sprites_data0[SPRITES_DATA0_RESIDENT_SIZE];
extern const sprite_t sprites_data1[SPRITE_PAGE_SIZE*SPRITE_SIZE];
extern const sprite_t sprites_data2[SPRITE_PAGE_SIZE*SPRITE_SIZE];
extern const sprite_t sprites_data3[SPRITE_PAGE_SIZE*SPRITE_SIZE];
extern const sprite_t sprites_data4[SPRITE_FINAL_SIZE*SPRITE_SIZE];

/* "Extra" sprites beyond page 0 that specific marks need (sprites.c's
 * sprites_paint() own dispatch comment) -- sprites_map1_extra_nums[] is
 * the lookup table of which sprite NUMBERS are extras for whichever map
 * is currently loaded (streamed per map like every other per-map table,
 * maps.c's map_loadMap()), sized to the larger of any map ported so
 * far's own extra-sprite count (22, SAMERICA; 32, EGYPT).
 * sprites_map1_extra_num_count is how many of these MAP_NBR_SPRXTRA
 * slots are actually valid right now.
 *
 * BUG FIXED, reported as "missing enemy sprites on level 2": EGYPT's
 * own 16-entry extra list only ever covered type-3 (chain-walked)
 * sprite needs -- missed that EGYPT also has one each of type-1a/1b/2
 * "them" entities (ents 7/8/9), which use a completely different sprite
 * formula (e_them.c's e_them_t1_action2()/e_them_t2_action2()/
 * e_them_z_action(): sprbase + ent_sprseq[a small fixed index] + a
 * direction/state offset, NOT a chain walk from sprbase at all) --
 * these types weren't ported yet when this session's own earlier sprite
 * audit was done, only became real partway through (e_them.c's own
 * header). Recomputed EGYPT's real sprite need against all three real
 * formulas (walk, climb, zombie-bounce) for ents 7/8/9 specifically and
 * found 16 more sprite numbers (55/56/58/60-64/126-133) -- 32 total,
 * which would have cost 4096 resident bytes kept all at once (BUG FIXED,
 * real address-space overflow: SIZE PASS below fixes this the same way
 * as maps.h's own MAP_NBR_BLOCKS incident).
 *
 * SIZE PASS: the actual PATTERN bytes are NOT kept resident for every
 * extra sprite -- only this number-lookup table is unconditionally.
 * On a cache miss (not one of the MAP_NBR_SPRXTRA_CACHED currently-
 * resident entries -- sprites_map1_extra_cached's own header,
 * engine/sprites.c, has the current mechanism), sprites.c streams a
 * single sprite's own 128-byte pattern straight into the cache slot it
 * picks to hold it, exactly when sprites_paint()'s own `load_pattern`
 * flag says a VRAM slot is actually switching to a different sprite
 * (not every frame) -- see sys_nabu_loadAssetOffset() (hal/
 * sys_nabu_load.h) for how one combined SPXPAT file serves as
 * independently-readable 128-byte slices. Trades a network round-trip
 * for less resident RAM only the FIRST time each distinct sprite number
 * is needed; a repeat is a cache hit with no network at all.
 *
 * BUG FIXED, reported as "flickering walking animation, worse on real
 * hardware than Marduk": pure on-demand streaming re-fetches over the
 * network every time an animated entity's sprite number changes --
 * type-1a/1b/2 "them" entities cycle through several different extra
 * sprite numbers just from walking (e_them_t1_action2()'s own
 * `(x&0x1c)>>3` term), so their VRAM slot switches sprites far more
 * often than the type-3/boulder-style sprites this mechanism was
 * originally sized around (those trigger once into a scripted
 * sequence, not a tight per-step cycle) -- frequent enough that real
 * network latency (Marduk's emulated HCCA timing is apparently more
 * forgiving than the real Internet Adapter's) became visible as
 * flicker/slowdown. Fix: sprites_map1_extra_cached[] below keeps the
 * first MAP_NBR_SPRXTRA_CACHED entries of whichever map's own
 * SPXNUM/SPXPAT genuinely resident (each map's own list is ordered
 * with its highest-frequency-changing sprites first, tools/'s
 * extraction script's own job) -- only entries past that still stream
 * on demand. SAMERICA also caches its own list now rather than leaving
 * any reserved cache space unused -- the buffer has to exist at
 * MAP_NBR_SPRXTRA_CACHED's size regardless of which map is loaded, so
 * there's no separate cost to using it for both. That includes the
 * boulder's own two rolling-animation sprites (97/105) as of this
 * session -- see maps.c's map_dataSize[0] comment and sprites.c's own
 * former sprites_boulder_roll[] header for why those moved into
 * SAMERICA's own list (SPXNUM1.DAT/SPXPAT1.DAT) instead of their own
 * always-resident table, bringing SAMERICA's own real count to 24 (was
 * 22) -- exactly this cache's own size, so nothing streams for SAMERICA
 * at all any more.
 *
 * CHANGED since this account was written: sprites_map1_extra_cached[]
 * no longer keeps a fixed first-N-by-list-position prefix -- it's a
 * rolling LRU cache keyed by actual sprite NUMBER now, filled by
 * whatever gameplay really requests rather than by list order (this
 * define's own comment further down has the full account; the
 * flicker/slowdown problem this paragraph describes is exactly what
 * that rolling behavior targets too, just tracked by real recency
 * instead of an author's guess at which list positions would be
 * hottest).
 *
 * BUG FIXED, EGYPT's own list (found auditing CASTLE's equivalent list
 * before porting it): the shipped SPXNUM2.DAT had drifted from what
 * e_them_t1_action2()'s real walk formula (sprbase + ent_sprseq[(x&0x1c)
 * >>3] + (offsx<0?3:0)) and e_bonus_action()'s literal `sprite=0xad`
 * actually need -- missing 57/59 (two of the six real walk-cycle frames;
 * verified by directly computing the formula against ent_sprseq[0..3]
 * from the shipped SPRSEQ.DAT, not by inspection) and missing 173 (the
 * "bonus collected" sprite -- EGYPT's own MAPMARK2.DAT has 6 real bonus
 * marks, so this was a genuine, reachable wrong-sprite bug, not
 * theoretical). Also shipped but never actually reachable from any of
 * EGYPT's real marks: 61/62/132/133 (no formula produces them) and
 * 97/105 (SAMERICA's own boulder-roll sprites, back when they lived in
 * sprites.c's own sprites_boulder_roll[] special-case rather than a
 * per-map list -- dead weight for EGYPT specifically either way).
 * Corrected list is 28 entries, not 32.
 *
 * Widened 32 -> 43 for CASTLE: its own type-1a/1b/2 "them" family
 * (marks 0xa/0xb/0xc/0xe, sharing entdata.spr=65/75 with some overridden
 * to sprbase 134 via the same ENT_FLG_TRIGGERS mechanism as EGYPT's own
 * override case) plus its type-3/zombie roster's chain-walked sprites
 * adds up to 43 real extra sprite numbers.
 *
 * Widened again, 43 -> 44, for MBASE: shares the SAME global entdata rows
 * 0xa/0xb/0xc/0xe as CASTLE (ent_entdata[] is a shared, global, not-
 * per-map table -- see ents.h's own header -- so reusing the same rows
 * across maps is expected, not a coincidence), needing the same 65-74/
 * 75-84/134-141 walk+zombie+climb ranges, PLUS its own 18-entry type-3
 * chain-walked set (verified index-by-index against the real ent_sprseq[]
 * chain semantics -- see ents.h's own ENT_NBR_SPRSEQ comment on the
 * "awake chain starts at sprbase+1, not sprbase+0" subtlety this walk
 * has to get right) plus the same bonus-collected sprite 173 EGYPT needs
 * -- 44 total, one more than CASTLE. This per-map-max buffer is sized to
 * MBASE's own real need, the largest of any map ported so far (EGYPT's
 * own corrected total, 28, and CASTLE's 43, both fit comfortably under
 * it). */
/* Widened 16 -> 24, then briefly pushed to 28 ("try increasing cache to
 * the max") -- REVERTED back to 24, real hardware/Marduk CRASH: 28 left
 * only 86 bytes of real headroom (rick.map's own __BSS_END_tail vs
 * TAR__register_sp), and reported directly as "something went very
 * wrong" -- a fully black level 1 with no map tiles drawn at all, Rick's
 * sprite floating on nothing. This build's own header (build.bat) already
 * documents exactly this failure mode: exceeding the real ceiling doesn't
 * reliably fail to link, it silently corrupts memory instead.
 *
 * TRIMMED AGAIN, 24 -> 21, same crash reported a second time: adding the
 * WALK_SND/CRAWL_SND/STICK_SND effects (engine/sounds.c) on top of an
 * already-24 build left only 37 bytes of real headroom -- thinner than
 * the 86-byte build that already corrupted memory once, so of course it
 * did again. 21 (real headroom checked after this change, see build
 * output) restores a comfortable margin without giving up the whole
 * streaming improvement -- CASTLE (43 total) and MBASE (44 total) still
 * leave more of their own extra sprites past the
 * cutoff streaming on demand, the exact mechanism this constant's own BUG
 * FIXED comment above describes, but that's a real tradeoff against a
 * hard memory ceiling, not something to push closer to the edge again
 * without finding headroom elsewhere first.
 *
 * TRIMMED AGAIN, 21 -> 20, same corruption reported a third time (this
 * time as "level 1 is corrupt" -- the repeating-tile scrolling glitch,
 * not a black screen): adding PAD_SND (engine/sounds.c) on top of an
 * already-21 build left only 68 bytes of real headroom, too thin against
 * this build's own established danger zone (86 bytes already corrupted
 * memory once). Each unit here costs exactly SPRITE_SIZE (128) bytes, so
 * trimming just one restores a real margin (196 bytes, checked after this
 * change) without another whole streaming-tier step down.
 *
 * TRIMMED AGAIN, 20 -> 19, on request ("need to free up more space")
 * before adding the level-select screen's own credits text (main.c's
 * levelSelectCredits[]), which on its own left headroom at 148 bytes --
 * not yet corrupted, but the explicit ask was for a real margin, not
 * just clearing this build's own established danger zone by a little.
 * 276 bytes headroom, checked after this change.
 *
 * TRIMMED AGAIN, 19 -> 18: wiring the last 13 sound effects (engine/
 * sounds.c's own STREAMED-effects header -- BOMBSHHT/BOX/SBONUS/SBONUS2/
 * ENT0..ENT8) pushed this build 15 bytes PAST the real ceiling even after
 * streaming instead of keeping them resident, and even after switching
 * ENT0..ENT8's own filenames to a shared mutable-digit template instead
 * of 9 separate string literals (that alone recovered 55 of the 70 bytes
 * originally over). One more unit here clears it with real margin again.
 *
 * TRIMMED AGAIN, 18 -> 17: the env.c 2-digit submap HUD fix (BUG FIXED,
 * that file's own comment) left only 67 bytes of real headroom -- back
 * in this build's own established danger zone (86 bytes already
 * corrupted memory once). One more unit restores real margin.
 *
 * BUMPED BACK UP, 17 -> 22, on request ("on metal, even the first level
 * there's too much loading just in the starting boulder bit"): swapping
 * BULLET_SND/EXPLODE_SND/BONUS_SND to their smaller TI-99 versions
 * (engine/sounds.c's own header) freed 674 bytes, more than reversing
 * every trim this constant took over the course of this session to fund
 * earlier additions. SAMERICA's own real extra-sprite need is 24 (this
 * header's own "bringing SAMERICA's own real count to 24" comment
 * above) -- at 17, most of that streamed on demand instead of staying
 * cached, exactly the kind of real-network-latency stutter Marduk's own
 * more-forgiving emulated HCCA timing wouldn't have shown. 22 doesn't
 * quite reach SAMERICA's full 24 (that would cost 896 of the 832 bytes
 * freed, leaving this build over budget again) but closes most of the
 * gap while keeping 192 bytes of real headroom -- solidly inside this
 * session's own proven-safe range, not pushed back into the danger zone
 * the extra 2 units would risk.
 *
 * TRIMMED AGAIN, 22 -> 16, reported as "level 3/4 always break on the
 * first scroll" (CASTLE/MBASE only, SAMERICA/EGYPT fine): tiles.c's own
 * tiles_setBank() fix (loadTileBanks() call, that file's own comment)
 * added a real loadTileBanks()->sys_nabu_loadAsset()->rn_FileRead()->
 * hcca_Di*() call chain reachable from a scroll-triggered tiles_setBank()
 * re-load -- extra STACK depth on top of whatever's already nested during
 * scrolling, not extra static size (this build's own __BSS_END_tail-vs-
 * TAR__register_sp headroom checked positive, 189 bytes, right after that
 * fix -- this isn't that same metric). Same class of problem as this
 * constant's own prior trims, same fix: shrinking static BSS further
 * buys the stack more room to grow into during a deep call chain, even
 * though nothing here directly measures stack depth the way it measures
 * static headroom. Trimmed further than the immediate deficit needed
 * (down to 16, not just enough to clear it) since a full break is a worse
 * failure mode than the cosmetic loading-stutter tradeoff this constant
 * otherwise balances -- real margin against a cost gameplay-blocking bug
 * outweighs a few more sprites streaming on demand for CASTLE/MBASE.
 *
 * TRIMMED AGAIN, 16 -> 15, on request ("add support for the TI-99's
 * version multicolor F18A sprites"): F18A's ECM=3 8-color sprite mode
 * fix (engine/sprites.c's own SPRITE_ECM comment) left this build 118
 * bytes over budget even after streaming its color-plane data on demand
 * instead of keeping all 10 walk-cycle frames resident (that alone was
 * the difference between a catastrophic multi-KB overflow and this small
 * one). One unit only cleared it to 10 bytes of headroom -- deep inside
 * this build's own established danger zone (86 bytes already corrupted
 * memory once, this session's own history) -- so trimmed one further for
 * a real margin instead of just barely fitting.
 *
 * TRIMMED AGAIN, 14 -> 13: the RICKBIT0.DAT fix (this file's own
 * SPRITE_ECM comment in engine/sprites.c, "blue on his face") added a
 * third on-demand-streamed plane and pushed this build back down to 89
 * bytes -- back in the danger zone. One more unit restores real margin.
 *
 * TRIMMED AGAIN, 13 -> 12: the sprite_index_map fix (engine/sprites.c's
 * own rick_walk_bit_index() comment, "bonus turned into a dog") added a
 * 9-case switch and left only 113 bytes -- above the proven corruption
 * point but thinner than this project's own comfort margin. One more
 * unit restores real margin.
 *
 * TRIMMED AGAIN, 12 -> 10, on request ("all sprites, all levels" -> EGYPT
 * first): the real 213-entry sprite_index_map[] (replacing the old
 * 9-case switch) plus a second page's own palette (sprite_ecm_pal_page1[],
 * hal/sysvid_nabu.c) and the per-page filename dispatch left only 14
 * bytes -- deep in the danger zone. Trimmed two units, not one, since
 * CASTLE/MBASE (more pages, more palettes) are still coming and will
 * cost more of the same.
 *
 * BUMPED WAY UP, 10 -> 24, on request ("Compile for F18A only, SAMERICA
 * only. Sound disabled. Cache as much as you can to reduce HCCA use
 * during gameplay."): F18A-only (VDP_TARGET_F18A_ONLY, hal/sysvid_nabu.h)
 * and SOUND_ENABLED=0 (engine/include/config.h) together freed this
 * build to 3202 bytes of real headroom -- by far the largest surplus
 * this constant's own history has ever seen. SAMERICA's real need is
 * exactly 24 (this header's own "bringing SAMERICA's own real count to
 * 24" comment above), so 24 is the actual ceiling for this build --
 * nothing past it caches anything real for a SAMERICA-only compile.
 * Reaching it spends 1792 of the 3202 bytes (14 units * SPRITE_SIZE),
 * leaving 1410 bytes -- still deep inside this session's own proven-
 * safe range even after the largest single bump this constant has ever
 * taken. Zero SPXNUM/SPXPAT streaming happens for SAMERICA during actual
 * gameplay any more -- the exact "reduce HCCA use during gameplay"
 * mechanism this constant's own BUG FIXED comment above was written to
 * describe, now maxed out because the budget finally allows it. This is
 * a test/experiment configuration (F18A+SAMERICA+no-sound only), not the
 * shipped default -- CASTLE/MBASE/EGYPT builds need less than their own
 * 43/44/28 real counts and should keep using a smaller value sized to
 * their own real budget, not this one. */
#define MAP_NBR_SPRXTRA 44
/* 1 on a build with ECM (stock hardware borrows the idle ECM buffer for a
 * 19-slot cache at runtime, sprites.c's sprites_extraCacheReset()); on a
 * 9918A-only build, the same 19 slots directly -- see PAGE0_CACHE_SLOTS. */
#if SPRITE_ECM_ENABLED
#define MAP_NBR_SPRXTRA_CACHED 1
#else
#define MAP_NBR_SPRXTRA_CACHED 19
#endif
/* sprites_map1_extra_cached[]/_scratch[] exist whenever the walkIdx<0
 * fallback (engine/sprites.c's sprites_paint()) is reachable -- some
 * ECM page off, OR real stock hardware is possible. REVERTED to `||`
 * CHANGED, on request ("sprites don't get read constantly from
 * network... most recently downloaded sprites should stay in memory"):
 * sprites_map1_extra_cached[] is now a real rolling LRU cache keyed by
 * sprite NUMBER, not a fixed first-N-by-list-position prefix -- see
 * engine/sprites.c's own header on this array for the full account of
 * why the old scheme didn't actually keep the most-recently-fetched
 * sprites resident. sprites_map1_extra_cached_count (the old "how many
 * of the prefix are valid" count, set once per map load) is gone;
 * sprites_extraCacheReset() replaces it, invalidating every cache slot
 * at each map transition (sprite numbers only mean the same pattern
 * within the currently-loaded map's own SPXPAT file) so the cache then
 * fills itself in from whatever that map's own gameplay actually
 * requests, same as before this array existed at all -- the tag/age
 * bookkeeping itself stays private to sprites.c. */
#if !SPRITE_ECM_ENABLED || !ECM_ALL_PAGES_ENABLED || !VDP_TARGET_F18A_ONLY
extern sprite_t sprites_map1_extra_cached[MAP_NBR_SPRXTRA_CACHED][SPRITE_SIZE];
void sprites_extraCacheReset(void);
#endif
/* sprites_map1_extra_nums[]'s real declared size -- shrinks to a
 * 1-entry placeholder only when its reader (sprites_paint()'s walkIdx<0
 * fallback) is truly unreachable, i.e. every ECM page is on AND this
 * build can't run on stock hardware at all -- either one being false
 * means real stock-hardware gameplay can hit this path, needing the
 * full MAP_NBR_SPRXTRA (44) entries. engine/maps.c's own loading loop
 * uses the exact same condition to decide whether to populate it. */
#if SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY
#define SPRITES_MAP1_EXTRA_NUMS_SIZE 1
#else
#define SPRITES_MAP1_EXTRA_NUMS_SIZE MAP_NBR_SPRXTRA
#endif
extern U16 sprites_map1_extra_nums[SPRITES_MAP1_EXTRA_NUMS_SIZE];
extern U16 sprites_map1_extra_num_count;

/* Page-0 (spriteNumber < SPRITE_PAGE_SIZE) patterns are cached via
 * page0_cache above -- see that cache's own declaration comment in
 * sprites.c for the full account (it briefly shared a pool with the ECM
 * plane data too, "graphics_cache", before the interleaved-.dat-file
 * change split them back apart). sprites_paint()'s own walkIdx<0
 * fallback still streams from SPR0.DAT whenever it needs a
 * page-0 pattern that isn't one of the current map's own "extra"
 * sprites and isn't sprites_them_walk[]'s hand-picked range either --
 * Rick's own walk/crawl/climb/shoot frames and other common page-0
 * objects are exactly this kind of frequently-cycling sprite; REPORTED
 * DIRECTLY ("XJ sprite is being downloaded many times over") is the
 * original motivation for caching this at all. Unlike
 * sprites_map1_extra_cached[], SPR0.DAT's own content is NOT per-map, so it
 * is never invalidated at a map transition, only once at boot
 * (sprites_page0CacheInit(), above).
 */



typedef struct spr_ {
    U8 y;
    U8 x;
    U8 ch;
    U8 col;
} sprite_data_t;

extern sprite_data_t sprite_table[(ENT_ENTSNUM+1)*4];


#endif

/* eof */

