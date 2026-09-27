/*
 * engine/include/tiles.h -- ported from xrick/include/tiles.h
 *
 * Change from the TI source: dropped `#ifdef F18A / #include <f18a.h>`.
 * `<f18a.h>` is a TI-cross-compiler-only header (like `<vdp.h>` was for
 * game.h), and the compile-time F18A macro doesn't exist in this port
 * (see config.h -- NABU is one binary, runtime-branched on
 * sysvid_nabu_hasF18A). F18A tile pattern/color banks and `tilesf18_pal`
 * are declared unconditionally and live in ti/f18a/tilesf18_split*.c.
 */

#ifndef _TILES_H
#define _TILES_H

#include "config.h"

/*
 * methods
 */
void loadDigitTiles(void);
void tiles_setBank(U16);
void tiles_setFilter(U16);
int tiles_paintList(const U8* , int);
void tiles_paintListAt(const U8* , U16, U16);

/*
 * one single tile
 *
 * a tile is 8x8 pixels.
 * PC: CGA encoding = 2 bits per pixel, one U16 per line.
 * ST: encoding = 4 bits per pixel, one U32 per line.
 * TI: bitmap encoding, 8 bytes pattern then 8 bytes color in two separate tables, tile_t here is one byte, not a full tile
 * (NABU: same TMS9918A-family bitmap encoding as the TI 9918A path -- this
 * format is chip format, unchanged here.)
 */
typedef U8 tile_t;

/*
 * banks (each bank contains 256 (0x100) tiles)
 *
 * This no longer means "how many banks are simultaneously resident" the
 * way it did when CASTLE (the first page==1 map, needing map_tilesBank==2)
 * got ported -- see tiles_setBank()'s own header. There are only ever 2
 * PHYSICAL slots: slot 0 (font, always resident, loaded once at boot,
 * still a full 256-tile bank -- every level uses the same font/HUD
 * glyphs) and slot 1 ("the current map's own gameplay tiles," reloaded
 * fresh at every map transition by engine/maps.c's map_loadMap()).
 *
 * SIZE PASS, on request ("would splitting tiles into level used rather
 * than banks help?"): slot 1 used to ALSO be a full 256-tile bank, same
 * as slot 0, even though no real level comes close to using all 256 --
 * this session's own audit of each level's real distinct block content
 * (assets/MAPBLKn.DAT) found only 102/154/84/150 distinct tile
 * indices for SAMERICA/EGYPT/CASTLE/MBASE. Slot 1 is now SPARSE: instead
 * of a fixed 256-entry array, map_loadMap() streams in just that level's
 * own real distinct tiles (pre-built from the full-bank art + MAPBLKn.DAT
 * into TILESPn.DAT/TF18SPn.DAT), sized
 * to the largest real map ported so far (EGYPT's
 * 154, same "streamed, max-not-sum" convention as maps.h's own
 * MAP_NBR_BLOCKS etc.) -- 2618 bytes instead of 4096, a 1478-byte saving.
 * Each entry keeps its real ABSOLUTE tile index (tiles_setBank() writes
 * it to that same real VRAM offset) rather than being rebased/compacted
 * -- unlike map_blocks[]'s own rebase (maps.h's own header), this index
 * is also used as-is by engine/util.c's collision check
 * (map_eflg[map_map[row][col]]), so renumbering it would have meant
 * rebuilding a parallel map_eflg table too. Tiles this level's own
 * map_map[] never references just keep whatever the previous map left in
 * that VRAM slot -- harmless, since nothing ever looks it up. */
#define TILES_SPARSE_MAX 154 /* EGYPT, the largest so far -- widen if a
                               * future map needs more */
#define TILES_SPARSE_ENTRY_SIZE (1+8+8) /* real tile index + 8 pattern
                                          * bytes + 8 color bytes */
#define TILES_SPARSE_BYTES (TILES_SPARSE_MAX*TILES_SPARSE_ENTRY_SIZE)

/* SIZE PASS: tiles_banks_col/pat (TMS9918A-only path) and
 * tilesf18_patA/colA (F18A-only path) used to be two separate BSS
 * reservations, even though exactly one of the two is ever read: which
 * hardware is present is decided once, at boot, by hal/f18a_nabu.c's
 * vdp_detect() (sets gVdpHardwareType only -- see main.c's loadAssets() for
 * why that, not sysvid_nabu_hasF18A, is the flag to check that early: the
 * latter isn't derived from it until sysvid_nabu_init() runs, later),
 * and every reader of either set (tiles_setBank(), loadDigitTiles())
 * branches on sysvid_nabu_hasF18A once it's valid.
 * Since BSS costs real bytes in this NABU homebrew image just like `const`
 * data does (there's no loader-level "uninitialized" concept for a flat
 * memory blob -- see hal/sys_nabu_load.h), the two sets are overlaid in
 * one union instead of reserved side by side, sized to the larger. Each
 * hardware struct's own gameplaySparse field sits at the identical
 * offset (right after that struct's own 2048+2048 font bytes), so
 * tiles_gameplay_sparse below works as a single alias regardless of
 * which hardware struct is "really" active -- see that macro's own
 * comment. main.c's loadAssets() only fills the font member the detected
 * hardware actually reads; filling both would silently corrupt one with
 * the other's bytes, since they're now the same memory. */
/* BUG FIXED, reported as "PCM playback... garbage" and extensively
 * bisected (test_pcm/main.c's own header has the full 15-variant test
 * matrix): splitting engine/sounds.c's WAAAAA cheat-key PCM playback
 * into two fetch+play passes (to fit inside the sparse-tile-sized union
 * below, 6714 bytes) reliably corrupted the second half's playback --
 * ruled out settle time (both directions, microseconds AND ~50ms),
 * fetch mechanism (offset-based/separate-file/handle-based), and mixer
 * register state, all with no effect. On request ("accept it and drop
 * chunking"): reverted WAAAAA to one single fetch + one continuous
 * playback pass (proven correct, the version that predates this
 * session's whole WAAAAA-shrinking effort), which at the time needed
 * WAAAAA_PCM_SIZE (engine/sounds.c) contiguous bytes available here --
 * more than the sparse-tile members below need on their own -- via a
 * rawWatermark member that forced this union's own size up to that
 * floor regardless of how small the tile-bank members got.
 *
 * REVERTED to conditional, on the later "split the ECM plane cache back
 * out from page0" change (engine/include/sprites.h's own
 * SPRITE_PLANES_SIZE comment): sounds_playWaaaaa() only streams+plays
 * out of engine/sprites.c's own ecm_slot_cache_slots when
 * SPRITE_ECM_ENABLED now (that array doesn't exist otherwise, unlike
 * the briefly-unconditional graphics_cache_slots this used to borrow
 * instead) -- so a build with NO ECM at all is back to needing this
 * union forced up to WAAAAA_PCM_SIZE, exactly like before that whole
 * merge. Still recovers the ~1445-byte gap between the sparse tile-bank
 * members' real size (6714) and this floor (8159) on any build that DOES
 * have SPRITE_ECM_ENABLED, where sounds_playWaaaaa() borrows
 * ecm_slot_cache_slots instead and never touches this union at all. */
#define WAAAAA_RAW_WATERMARK_SIZE 8159 /* must match engine/sounds.c's
  own WAAAAA_PCM_SIZE -- not #include'd from there to avoid a tiles.h ->
  sounds.h dependency for one constant; keep the two in sync by hand. */

union tiles_banks_shared_u {
    struct {
        tile_t col[256*8];
        tile_t pat[256*8];
        U8 gameplaySparse[TILES_SPARSE_BYTES];
    } ti;
    struct {
        unsigned char patA[0x800];
        unsigned char colA[0x800];
        U8 gameplaySparse[TILES_SPARSE_BYTES];
    } f18a;
#if !SOUND_TRIM_PCM && !SPRITE_ECM_ENABLED
    U8 rawWatermark[WAAAAA_RAW_WATERMARK_SIZE];
#endif
};
extern union tiles_banks_shared_u tiles_banks_shared;

#define tiles_banks_col (tiles_banks_shared.ti.col)
#define tiles_banks_pat (tiles_banks_shared.ti.pat)
#define tilesf18_patA (tiles_banks_shared.f18a.patA)
#define tilesf18_colA (tiles_banks_shared.f18a.colA)
/* Aliases tiles_banks_shared.ti.gameplaySparse -- identical byte offset
 * (2048+2048) to tiles_banks_shared.f18a.gameplaySparse in the other
 * struct, so this one macro is correct no matter which hardware path is
 * actually active; unlike the font members above, the gameplay bank
 * never needs a hardware-specific name since tiles_setBank() reads it
 * through the same code path either way (see that function's own
 * header). */
#define tiles_gameplay_sparse (tiles_banks_shared.ti.gameplaySparse)
extern U16 tiles_gameplay_sparse_count;

extern const unsigned int tilesf18_pal[16];

/*
 * special tile numbers
 */
#define TILES_BULLET 0x01
#define TILES_BOMB 0x02
#define TILES_RICK 0x03

#define TILES_NULL 0xfe
#define TILES_NULLCHAR "\376"
#define TILES_CRLF 0xff
#define TILES_CRLFCHAR "\377"

#endif

/* eof */
