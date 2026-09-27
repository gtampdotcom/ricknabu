/*
 * engine/tiles.c -- ported from xrick/src/tiles.c
 *
 * Real changes from the TI source:
 *   - F18A vs 9918A tile data is a runtime branch on sysvid_nabu_hasF18A
 *     (config.h) instead of `#ifdef F18A`. F18A banks come from
 *     ti/f18a/tilesf18_split{pat,col}{1,2,3}.c (same arrays the TI cart
 *     included); 9918A banks stay in dat_tilespTI.c / dat_tilescTI.c.
 *   - Removed `unsigned int nOldBank = nBank;` / `SWITCH_IN_BANK8` /
 *     `SWITCH_IN_BANK9` / `SWITCH_IN_BANK(nOldBank)` throughout -- no
 *     cartridge banking on NABU, same policy as util.c/ents.c/config.h.
 *     The split files are therefore compiled in (not HEADER_ONLY).
 *   - `VDP_INT_DISABLE`/`VDP_INT_ENABLE` (TI cross-compiler <vdp.h> macros,
 *     not defined anywhere in this repo) become
 *     `NABU_DisableInterrupts()`/`NABU_EnableInterrupts()`, same as env.c.
 *   - `VDP_INT_POLL` (same TI header, used mid-copy to let interrupt-driven
 *     music keep playing during a long blocking write) has no NABU
 *     equivalent -- not needed, tile loads are short. Dropped.
 *   - `VDP_SET_ADDRESS_WRITE(x)` becomes `vdp_setWriteAddress(x)`.
 *   - `VDPWD(x)` becomes a direct `IO_VDPDATA = x`, same reasoning as
 *     env.c/sysvid_nabu.c's bitmapcharcopy(): nabulib's public vdp_write()
 *     is a text-mode character print, not a raw VRAM byte stream.
 *   - `vdpmemcpy2()` calls are unchanged in shape -- it's now a real NABU
 *     function (hal/sysvid_nabu.c) rather than a TI intrinsic, with the same
 *     name and signature, so these call sites needed no edits.
 *   - `bitmapcharcopy()` calls are also unchanged in shape for the same
 *     reason -- see hal/sysvid_nabu.c's bitmapcharcopy() for what it now
 *     actually does (three-way table replication, corrected this round).
 *   - F18A bank B (tilesf18_patB/colB -- the map/cavern tile graphics, as
 *     opposed to bank A/0's title-screen font) is now linked in alongside
 *     bank A, so tiles_setBank() can serve both instead of always
 *     silently substituting the font. Needed once engine/maps.c's
 *     maps_paint() started actually being called. Bank B's own CONTENT is
 *     no longer fixed at link time either -- engine/maps.c's
 *     map_loadMap() now streams whichever real gameplay tile bank the
 *     current map needs into this same slot at every map transition
 *     (CASTLE needs a real third bank's worth of different graphics here,
 *     see tiles_setBank()'s own header) -- so "bank B" now means "the
 *     current map's own gameplay tiles," not a single fixed asset.
 */

#include "config.h"
#include "tiles.h"
#include "fb.h"
#include "sysvid.h"
#include "env.h"
#include "scroller.h"

#include "game.h"
#include "NABU-LIB.h"
#include "sysvid_nabu.h"

/*
 * SIZE PASS: tilesf18_patA/colA/patB/colB used to be `#include`d straight
 * from ti/f18a/tilesf18_split{pat,col}{1,2}.c -- the real, unconverted TI
 * source files, each a `const unsigned char [2048]` initializer. That's
 * ~8KB of real image bytes sitting right next to a codebase where every
 * other asset table (tiles_banks_col/pat, sprites_data0, every map/entity
 * table) had already moved to the BSS+RetroNET-load pattern (see
 * hal/sys_nabu_load.h). These four were the one place that pattern hadn't
 * reached yet. Converted the same way, with real bytes extracted by
 * tools/extract_assets.py into assets/TF18PATA.DAT/TF18COLA.DAT/
 * TF18PATB.DAT/TF18COLB.DAT, loaded by main.c's loadAssets() at startup.
 *
 * SIZE PASS (later round): these four buffers and tiles_banks_col/pat
 * (dat_tilescTI.c/dat_tilespTI.c) are mutually exclusive at runtime --
 * exactly one hardware path is active per boot, see tiles.h's own comment
 * -- so this is now just the storage for tiles.h's tiles_banks_shared
 * union rather than four separate arrays; tilesf18_patA etc. are macros
 * over its members (tiles.h), same names, no call site changes needed.
 */
union tiles_banks_shared_u tiles_banks_shared;

/* How many of tiles_gameplay_sparse's own TILES_SPARSE_MAX slots are
 * real entries for the CURRENT map -- set by engine/maps.c's
 * map_loadMap() right before it calls tiles_setBank(0xff), read by
 * tiles_setBank()'s own gameplay-bank loop (tiles.h's own header). */
U16 tiles_gameplay_sparse_count;

/* tilesf18_pal[16] was also defined inside tilesf18_splitcol1.c, which the
 * removed #include above also dropped -- unlike the four 2KB tables it
 * shared a file with, this one is 32 bytes total, nowhere near worth the
 * added RetroNET-load complexity/risk (sys_nabu_loadAsset() panics on any
 * failure -- fine to accept for multi-KB tables, not worth it for
 * something this small). Kept as a real compiled-in const instead, values
 * copied verbatim from ti/f18a/tilesf18_splitcol1.c. */
const unsigned int tilesf18_pal[16] = {
    0x0000,0x0222,0x0420,0x0940,0x0666,0x0999,0x0D60,0x0BBB,
    0x004B,0x006D,0x0FFF,0x0F96,0x0240,0x0462,0x0444,0x0000
};
/* CASTLE's own real bank-2 tile graphics (originally ti/f18a/
 * tilesf18_split{pat,col}3.c on the F18A side, dat_tilespTI.c/
 * dat_tilescTI.c's own third chunk on the 9918A side) are NOT a separate
 * compiled-in bank here -- see tiles_setBank()'s own header on why: they
 * stream into the same physical "gameplay bank" slot patB/colB (or
 * tiles_banks_pat/col's second half) already holds for SAMERICA/EGYPT,
 * loaded fresh per map by engine/maps.c's map_loadMap() instead of once
 * at link time. */

// load the digits from tile bank 0 to the correct place set by env_digits
void loadDigitTiles(void) {
    NABU_DisableInterrupts();

    // chars 8-20. Don't use bitmapcharcopy, only want the first page
    // assumes gPattern is 0!
    if (sysvid_nabu_hasF18A) {
        vdpmemcpy2(env_digits*8, tilesf18_patA+48*8, 10*8); // digits
        vdpmemcpy2((env_digits+10)*8, tilesf18_patA+1*8, 3*8);  // icons
        vdpmemcpy2(gColor+env_digits*8, tilesf18_colA+48*8, 10*8); // digits
        vdpmemcpy2(gColor+(env_digits+10)*8, tilesf18_colA+1*8, 3*8);  // icons
    } else {
        vdpmemcpy2(env_digits*8, tiles_banks_pat+48*8, 10*8); // digits
        vdpmemcpy2((env_digits+10)*8, tiles_banks_pat+1*8, 3*8);  // icons
        vdpmemcpy2(gColor+env_digits*8, tiles_banks_col+48*8, 10*8); // digits
        vdpmemcpy2(gColor+(env_digits+10)*8, tiles_banks_col+1*8, 3*8);  // icons
    }

    NABU_EnableInterrupts();
}

/*
 * tiles_setBank
 *
 * sets current tiles bank to <bank>.
 */
void tiles_setBank(U16 bank)
{
    static U16 lastSetBank = 0xff;

    if (lastSetBank == bank) {
        return;
    }
    lastSetBank = bank;
    if (bank == 0xff) {
        // magic for reset - needed for the title page and HOF
        return;
    }

    /* BUG FIXED / SIZE PASS, on request ("time to add level 3"): this used
     * to switch on the real semantic bank number (0=font, 1=SAMERICA/
     * EGYPT's shared gameplay tiles, panicking on anything else -- "bank C
     * /index 2 isn't linked in yet"). CASTLE needs a genuinely different
     * gameplay tile bank (every one of its submaps has page==1, i.e.
     * map_tilesBank==2, maps.h's own MAP_NBR_SUBMAPS comment) -- growing
     * tiles_banks_shared to hold a real third resident bank would cost
     * another 4096 bytes.
     *
     * Instead, engine/maps.c's map_loadMap() now streams whichever real
     * gameplay tile bank the CURRENT map needs fresh at every map
     * transition -- same "streamed, not globally resident" trick already
     * used for blocks/submaps/connect/bnums/marks. So there is no longer a
     * real "bank 2" to dispatch on here: ANY non-zero bank number means
     * "the gameplay slot," which already holds the right bytes for
     * whichever map is actually loaded by the time this runs. map_loadMap()
     * also calls tiles_setBank(0xff) right after that load to force a real
     * reload here even when the new map's own bank number happens to equal
     * the previous map's (e.g. SAMERICA->EGYPT, both page==0/bank==1) --
     * without that, this function's own lastSetBank cache would wrongly
     * skip pushing the freshly-streamed-but-differently-numbered bytes to
     * VRAM.
     *
     * SIZE PASS, on request ("would splitting tiles into level used rather
     * than banks help?"): the gameplay slot (bank != 0) is no longer a
     * flat 0x800-byte block copied in one shot -- it's SPARSE now (tiles.h's
     * own header), holding only this level's own real distinct tile set.
     * Loop over tiles_gameplay_sparse's own tiles_gameplay_sparse_count
     * entries and place each one at its own real (absolute, un-rebased)
     * VRAM offset instead. Whatever this level's own map_map[] doesn't
     * reference just keeps the previous map's leftover bytes in that VRAM
     * slot -- harmless, since nothing ever looks a tile index up that this
     * level's own terrain never draws. Font (bank == 0) is unchanged --
     * still one full 256-tile block, same for every level. */
    if (bank == 0) {
        const U8 *pat, *col;
        if (sysvid_nabu_hasF18A) {
            pat = tilesf18_patA; col = tilesf18_colA;
        } else {
            pat = tiles_banks_pat; col = tiles_banks_col;
        }
        bitmapcharcopy(gPattern, pat, 0x800);
        bitmapcharcopy(gColor, col, 0x800);
    } else {
        const U8 *entry = tiles_gameplay_sparse;
        U16 i;
        for (i = 0; i < tiles_gameplay_sparse_count; i++) {
            U16 idx = entry[0];
            bitmapcharcopy(gPattern + idx*8, entry+1, 8);
            bitmapcharcopy(gColor + idx*8, entry+9, 8);
            entry += TILES_SPARSE_ENTRY_SIZE;
        }
    }

    if (sysvid_nabu_hasF18A) {
        f18a_unlock();
        f18a_loadPalette((const U16 *)tilesf18_pal, 0, 16);
        /* no f18a_lock(): relocking switches ECM sprites off, see f18a_nabu.c */
    }

    // okay, screw it. Just find 13 characters we can overwrite, something we KNOW
    // is only for a different map.
    if (bank != 0) {
        /* BUG FIXED, reported as "HUD icons garbled, level 3/4 only": found
         * via live debugger (tiles_banks_shared's font slot held ENT6.DAT's
         * exact bytes instead of real font pattern data). CASTLE/MBASE's
         * own real map data has type-3 "scripted/triggered" entities
         * (boulders, dart/arrow traps -- e_them.c's e_them_t3_action2()
         * wakeup: label) that can fire within the very first frame or two
         * of real gameplay -- confirmed directly, CASTLE's own submap 0 has
         * two of them. Their sound (sounds_playEnt(), engine/sounds.c) is
         * STREAMED into this exact same tiles_banks_shared memory and
         * occupies it for the sound's ENTIRE playback, not just the
         * initial load. If tiles_setBank() ever re-runs (a real, non-
         * cached bank switch -- exactly what's happening right here) while
         * a streamed effect is still mid-playback, loadDigitTiles() below
         * would read that leftover sound data as if it were font glyphs --
         * SAMERICA/EGYPT never hit this because this project's own type-3
         * traps happen not to fire quite this early there, not because of
         * anything structurally different about bank 1 vs bank 2.
         *
         * Fix: refresh tiles_banks_shared's font slot right here,
         * immediately before loadDigitTiles() reads it, instead of trusting
         * whatever was loaded there ages ago by a completely different call
         * site. One extra network round-trip on a real (non-cached) bank
         * switch -- a relatively rare, map/submap-transition-scale event,
         * not a per-frame cost -- buys real protection against this whole
         * class of "something else claimed this shared scratch space
         * first" bug, regardless of the exact timing that triggers it. */
        loadTileBanks();

        /* BUG FIXED, reported as "numbers appearing on level 4" (MBASE) --
         * a scatter of digit-shaped glyphs baked into the middle of real
         * level terrain. This `switch` used to be collapsed to a flat
         * `env_digits = 8` (SIZE PASS, see git history): env_map was
         * provably always 0 at the time that collapse was made -- map
         * advancement/direct level-select into EGYPT/CASTLE/MBASE didn't
         * exist yet, so cases 1-3 below could never be taken and were
         * genuinely dead code then. That stopped being true once this
         * session restored real map-to-map progression and MBASE became
         * directly reachable (env_map now legitimately varies 0-3) --
         * loadDigitTiles() (below) always overwrites tile CHARACTER codes
         * env_digits..env_digits+12 in the current gameplay bank's own
         * VRAM pattern/color tables with digit/icon glyph shapes; 8..20 is
         * only safe to sacrifice for SAMERICA's own real terrain, not
         * every map's. Restored verbatim from the real xrick reference
         * (xrick/src/tiles.c) -- each value is that map's own real "13
         * consecutive character codes its terrain art never uses". */
        switch (env_map) {
            case 0: // cavern
                env_digits = 8;
                break;

            case 1: // egypt
                env_digits = 60;
                break;

            case 2: // castle
                env_digits = 160;
                break;

            case 3: // missile
                env_digits = 26;
                break;
        }

        loadDigitTiles();
    }
}

/*
 * tiles_setFilter
 *
 * sets current tiles display filter to <filter>
 */
/* SIZE PASS: wrapped out -- zero callers project-wide (no-op body anyway). */
#if 0
void tiles_setFilter(U16 filter)
{
    (void)filter;
}
#endif

/*
 * tiles_paintList
 *
 * paints list of tiles <tilesList> at the position indicated by <fb>. the
 * list must be TILES_NULL terminated and can contain TILES_CRLF elements
 * to produce crlf.
 *
 * returns next <fb> value.
 *
 * SIZE PASS: was wrapped out (zero callers) until engine/scr_imap.c came
 * back in -- its screen_introMap() is the real caller now, via
 * tiles_paintListAt() below.
 */
int tiles_paintList(const U8* tilesList, int fb)
{
	int f;

	f = fb&0x3fff;

    NABU_DisableInterrupts();

    vdp_setWriteAddress(f+gImage);

	while (1)
	{
		if (*tilesList == TILES_NULL) /* end of list */ {
            NABU_EnableInterrupts();
			return f;
        }

		if (*tilesList == TILES_CRLF) /* crlf */
		{
			fb += 32;
			f = fb&0x3fff;
            vdp_setWriteAddress(f+gImage);
			tilesList++;
			continue;
		}

		/* else paint */
        IO_VDPDATA = (*(tilesList++));
        ++f;
	}
}

/*
 * tiles_paintListAt
 *
 * paints list of tiles <tilesList> at the position indicated by <x>, <y>. the
 * list must be TILES_NULL terminated and can contain TILES_CRLF elements to
 * produce crlf.
 * <x>, <y> are fb-coordinates.
 */
void tiles_paintListAt(const U8* tilesList, U16 x, U16 y)
{
	tiles_paintList(tilesList, fb_at(x, y));
}



/* eof */
