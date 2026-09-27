/*
 * hal/sysvid_nabu.c
 *
 * See sysvid_nabu.h for status. Brings up Graphics II (bitmap) mode and sets
 * the runtime VRAM layout (gImage/gColor/gPattern/gSprite/gSpritePat).
 * Actual pixel content (title screen, tiles, sprites) is uploaded by the
 * engine code that owns that data (main.c's title_draw(), tiles.c,
 * sprites.c).
 */

#define BIN_TYPE BIN_HOMEBREW
#include "sysvid_nabu.h"
#include "NABU-LIB.h"
#include "game.h"
#include "scroller.h" /* for vdpmemcpy2()'s prototype, defined below */

/* Runtime VRAM layout, see engine/include/game.h's header comment for why
 * these are variables now instead of the TI source's compile-time #defines. */
U16 gImage;
U16 gColor;
U16 gPattern;
U16 gSprite;
U16 gSpritePat;

/* TRUE once set_halfbitmap() has switched the F18A's pattern/color tables
 * from three 2KB screen thirds to one shared 2KB block (registers 3/4) --
 * see bitmapcharcopy(). set_fullbitmap() (the title screen) clears it. */
static U8 sysvid_halfBitmap = 0;

void sysvid_nabu_init(void) {
  /* gVdpHardwareType comes from main()'s pre-init vdp_detect() call (see
   * sysvid_nabu.h). A Pico9918 in F18A-compatible mode wants the same
   * enhanced init path as a genuine F18A, so anything other than
   * VDP_TYPE_STOCK takes the F18A branch. This replaces the old
   * sysvid_nabu_detectF18A() GPU-program probe (hal/f18a_nabu.c) as the
   * decision that both sets sysvid_nabu_hasF18A and picks the init path --
   * that function is still there and still works, just no longer wired in
   * here, since vdp_detect() is the more precise of the two (it alone can
   * tell a real F18A from a Pico9918). */
#if !VDP_TARGET_F18A_ONLY && !VDP_TARGET_9918A_ONLY
  sysvid_nabu_hasF18A = (gVdpHardwareType != VDP_TYPE_STOCK) ? 1 : 0;
#endif
  /* CHANGED, on request ("compile for F18A only"): sysvid_nabu_hasF18A
   * is a compile-time `1` in that mode (sysvid_nabu.h's own comment),
   * so this `if` always takes the F18A branch and the compiler drops
   * the `else` -- guarded out below too, so sysvid_nabu_init9918()
   * itself doesn't even get compiled, not just left uncalled.
   *
   * MIRRORED, on request ("add a 9918A_ONLY that excludes all the F18A
   * data+code"): the whole `if (sysvid_nabu_hasF18A) { sysvid_nabu_
   * initF18A(); }` statement is skipped entirely under VDP_TARGET_
   * 9918A_ONLY, not just left as dead code relying on hasF18A's
   * compile-time `0` to eliminate it -- sysvid_nabu_initF18A() itself
   * is #if'd out under that flag (hal/f18a_nabu.c-adjacent discipline),
   * so an un-gated call here would be an undefined-symbol risk, not
   * just wasted bytes. Falls straight through to sysvid_nabu_init9918()
   * unconditionally instead, same as the real dual-hardware case's own
   * runtime `else` would resolve to anyway. */
#if !VDP_TARGET_9918A_ONLY
  if (sysvid_nabu_hasF18A) {
    sysvid_nabu_initF18A();
  }
#if !VDP_TARGET_F18A_ONLY
  else {
    sysvid_nabu_init9918();
  }
#endif
#else
  sysvid_nabu_init9918();
#endif
}

#if !VDP_TARGET_F18A_ONLY
void sysvid_nabu_init9918(void) {
  /* Graphics II (256x192, TMS9918A) is the closest match to the TI 9918A
   * fallback mode rickti already supports.
   *
   * splitThirds=true, and this is now settled, not a TODO: reading the real
   * xrick/src/sysvid.c/tiles.c this round showed gameplay tiles ALSO need
   * split mode -- tiles_setBank() loads one 2KB tile bank via
   * bitmapcharcopy(), which (see that function's comment in this file)
   * replicates the bank into all three 2KB thirds so a given tile index
   * renders identically no matter which third of the screen it's drawn in.
   * So it's not "title screen wants split, gameplay wants non-split" as
   * guessed last round -- it's "always split, gameplay just keeps all three
   * copies in sync." One init path covers both cases.
   *   vdp_initG2Mode(bgColor, bigSprites, scaleSprites, autoScroll, splitThirds)
   *
   * bigSprites=true: engine/sprites.c's sprites_paint() lays out each
   * Rick-sized object as four 16x16 hardware sprites (see that file's
   * header), which on real TMS9918A/F18A hardware requires the VDP's
   * 16x16-sprite mode (register 1's SI bit) -- with it left false, the
   * hardware read each sprite "name" as a single 8x8 pattern instead of
   * the 4-consecutive-pattern 16x16 block the pattern data
   * (dat_spritesTI0.c) and sprites_paint()'s chr/chr+4/chr+8/chr+12
   * addressing both assume, which is what showed up as garbled black-line
   * fragments instead of Rick's silhouette. */
  vdp_initG2Mode();
  //vdp_setBackDropColor(VDP_BLACK);

  gImage     = GAME_VRAM_IMAGE_9918;
  gColor     = GAME_VRAM_COLOR_9918;
  gPattern   = GAME_VRAM_PATTERN_9918;
  gSprite    = GAME_VRAM_SPRITE_9918;
  gSpritePat = GAME_VRAM_SPRITEPAT_9918;
  /* VERIFIED this round: these addresses (carried over from the TI cart's
   * chosen layout) exactly match what vdp_initG2Mode() actually sets --
   * NABU-LIB.c's VDP_MODE_G2 case sets _vdpPatternNameTableAddr=0x1800,
   * _vdpPatternGeneratorTableAddr=0x00, _vdpColorTableAddr=0x2000,
   * _vdpSpriteAttributeTableAddr=0x1b00, _vdpSpriteGeneratorTableAddr=0x3800.
   * No longer a guess. */
}
#endif /* !VDP_TARGET_F18A_ONLY */

#if !VDP_TARGET_9918A_ONLY
void sysvid_nabu_initF18A(void) {
  /* Use the SAME table addresses as the 9918A path, not the GAME_VRAM_*_F18A
   * macros (game.h) -- those are leftover, unverified TI-cartridge addresses
   * (game.h's own header comment calls them "a placeholder, not a verified
   * layout"). This function calls the exact same vdp_initG2Mode() below as
   * sysvid_nabu_init9918() does, with no F18A-specific addressing, so the
   * VDP ends up configured identically either way: name table 0x1800,
   * pattern 0x0000, color 0x2000, sprite attr 0x1b00, sprite pattern 0x3800
   * (see sysvid_nabu_init9918()'s "VERIFIED this round" comment below).
   *
   * The previous code here pointed gImage at GAME_VRAM_IMAGE_F18A (0x1c00)
   * while the VDP was actually configured (by this same vdp_initG2Mode()
   * call) to read its name table from 0x1800. The title screen's identity
   * name-table fill -- and main.c's tile-test fill -- were therefore writing
   * 768 bytes to 0x1c00, a VRAM address the hardware never reads as the name
   * table, while the real name table at 0x1800 was left holding whatever
   * nabulib's own boot-up state put there. Pattern/color addresses happened
   * to already match (both macro sets used 0x0000/0x2000), so the tile
   * *shapes* were right but the *indices* selecting them were garbage --
   * exactly the striped/near-blank corruption seen on F18A/PICO9918, while
   * stock TMS9918A (always using the correct *_9918 addresses) rendered
   * fine. GAME_VRAM_SPRITE_F18A/GAME_VRAM_SPRITEPAT_F18A were wrong the same
   * way (0x1f00/0x2800 vs the real 0x1b00/0x3800); not yet exercised by any
   * code path, but fixed here too before sprites start using gSprite/
   * gSpritePat. */
  gImage     = GAME_VRAM_IMAGE_9918;
  gColor     = GAME_VRAM_COLOR_9918;
  gPattern   = GAME_VRAM_PATTERN_9918;
  gSprite    = GAME_VRAM_SPRITE_9918;
  gSpritePat = GAME_VRAM_SPRITEPAT_9918;

  /* Step 1: bring up plain G2 (Graphics II / bitmap) mode via nabulib.
   * The F18A is fully backward-compatible with the TMS9918A register set, so
   * the same init call works.  We use splitThirds=true for the same reason as
   * the 9918A path, and bigSprites=true for the same reason too -- see
   * sysvid_nabu_init9918()'s comment on both. */
  vdp_initG2Mode();

  /* Step 2: leave F18A extended register 49 (the tile/sprite Enhanced Color
   * Mode register, "F18A_REG_ECM" in the TI source) at ECM=0/disabled.
   */
}
#endif /* !VDP_TARGET_9918A_ONLY */

#if SPRITE_ECM_ENABLED
/* Real F18A per-page 8-color sprite palettes, verbatim from
 * ti/f18a/sprf0pal.c/sprf1pal.c/sprf2pal.c/sprf3pal.c -- page 0
 * (SAMERICA/most entities), page 1 (EGYPT), page 2 (CASTLE, plus
 * SAMERICA's own boulder), and page 3 (the "spear guy" enemy), loaded at
 * RAM offsets 16/24/32/40 below, matching the reference's own
 * `loadpal_f18a(sprfNpal, 16/24/32/40, 8)` call sites (xrick/src/
 * scr_imap.c) exactly -- the sprite attribute "color" value engine/
 * sprites.c computes (4 + page*2) is that same offset/4, matching the
 * reference's own `pal = 4 + spritePage*2` formula. EXTENDED, on request
 * ("all sprites, all levels" -> incremental page-by-page: 0, 1, 2, 3).
 * Page 4 (MBASE's remaining page) shares this same page-2 group instead
 * of getting its own real sprf4pal, per the reference's own clamp, "if
 * (pal > 10) pal = 8" (pal=12 for page4, clamped down to 8) -- still to
 * come, needs sprites.c's own spriteColor formula to clamp too, not just
 * more asset files. */
/* Each page's palette (16 real bytes) only compiled in when something
 * actually needs it -- on request ("speed it up... compile time
 * options... free up what we're not using"). Page 2's is needed by
 * SPRITE_ECM_PAGE_MBASE too, not just SPRITE_ECM_PAGE_CASTLE: page 4
 * shares page 2's own group rather than getting a real sprf4pal (the
 * reference's own clamp, "if (pal > 10) pal = 8" -- sprites.c's own
 * spriteColor comment), so turning CASTLE off while leaving MBASE on
 * must still keep this one resident. */
#if SPRITE_ECM_PAGE_SAMERICA
static const U16 sprite_ecm_pal_page0[8] = {
  0x0000, 0x0940, 0x0420, 0x0555, 0x0F96, 0x004B, 0x0D60, 0x0AAA
};
#endif
#if SPRITE_ECM_PAGE_EGYPT
static const U16 sprite_ecm_pal_page1[8] = {
  0x0000, 0x0240, 0x0D60, 0x0AAA, 0x0420, 0x0940, 0x0462, 0x0555
};
#endif
#if SPRITE_ECM_PAGE_CASTLE || SPRITE_ECM_PAGE_MBASE
static const U16 sprite_ecm_pal_page2[8] = {
  0x0000, 0x0555, 0x0999, 0x0940, 0x0D60, 0x0420, 0x0F96, 0x0B66
};
#endif
#if SPRITE_ECM_PAGE_MBASE
static const U16 sprite_ecm_pal_page3[8] = {
  0x0000, 0x0D60, 0x0420, 0x0940, 0x0555, 0x0F96, 0x0AAA, 0x0240
};
#endif

/* NOT from the reference -- this port's own stand-in "plain white" group
 * for every sprite that doesn't have real 3-plane color data (yet):
 * engine/sprites.c sends SPRITE_ECM_PAL_FALLBACK (14, i.e. RAM offset 56)
 * for those, with color-bit planes 1/2 left at 0, so only index 1
 * (bit0's own "on" value) is ever selected -- put white there so those
 * sprites keep looking the way they did before ECM=3 (SPRITE_COLOR_WHITE_
 * F18A, index 10 of the *tile* palette, 0x0FFF) instead of picking up
 * whatever RAM offset 56-63 happened to hold. RELOCATED from offset 24 to
 * 56 (was group 6, EGYPT's own real palette now needs that slot) -- see
 * engine/sprites.c's own SPRITE_ECM_PAL_FALLBACK comment for why 56/group
 * 14 specifically. */
static const U16 sprite_ecm_pal_fallback[8] = {
  0x0000, 0x0FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

/* F18A 8-color sprite mode, gameplay-only -- see game.h's
 * GAME_VRAM_SPRITEPAT_BIT1/BIT2 comment for the VRAM-freeing reasoning,
 * and engine/sprites.c's own header for the rendering side. Verbatim
 * register values from xrick/src/sysvid.c's set_halfbitmap() (its own
 * "no sprite limits with F18A!" -- register 3=0x9F/4=0x00 double as
 * NABU-LIB's own splitThirds=false values, register 49=3 is F18A's own
 * ECM "8-color sprites" mode) -- the earlier rejected attempt (see
 * sysvid_nabu.h's comment on this function) only ever wired the register
 * toggle alone; this version adds the two palette loads and the real
 * per-sprite pattern-plane data (engine/sprites.c) that toggle actually
 * needs to mean anything. F18A-only: stock TMS9918A has no ECM/8-color
 * sprite concept, so this is never called on that path (see the one call
 * site, main.c). */
void set_halfbitmap(void) {
  vdp_setRegister(3, 0x9F);
  vdp_setRegister(4, 0x00);
  sysvid_halfBitmap = 1;

  /* BUG FIXED, on request ("add support for the TI-99's version
   * multicolor F18A sprites") -- register 6 (sprite pattern table base)
   * was NEVER written here in either of the two earlier attempts, so the
   * chip kept computing its two ECM color planes relative to whatever
   * gSpritePat/register 6 was already set to (this port's normal 0x3800)
   * instead of the 0x2800 the rest of this function -- and
   * GAME_VRAM_SPRITEPAT_BIT1/BIT2 (game.h) -- assume. See engine/
   * sprites.c's own SPRITE_ECM comment for the full root-cause writeup.
   * Register 6's real format is (R06&0x07)<<11 -- value 5 gives
   * 5<<11=0x2800, matching GAME_VRAM_SPRITEPAT_F18A and the real F18A
   * cartridge's own verified layout (xrick/src/xrick.c's sys_init()
   * comment) exactly. gSpritePat itself must be updated to match, since
   * every other engine file addresses sprite pattern VRAM through that
   * runtime variable, not the register value directly. */
  vdp_setRegister(6, 5);
  gSpritePat = GAME_VRAM_SPRITEPAT_F18A;

  f18a_unlock();
  f18a_setRegister(49, 0x03); /* ECM=3: enable 8-color sprites */
  /* SPGS (bits 7-6): sprite pattern generator offset size, i.e. the
   * spacing between the ECM color planes above -- 00=2048 bytes is both
   * the hardware's own power-on default and what every address/size
   * calculation in this file and game.h assumes. Written explicitly
   * rather than relied upon, matching this project's own "verify, don't
   * assume" discipline -- nothing else in this codebase touches register
   * 29, but a future change might, and silence here would make that a
   * much harder bug to find than one explicit write now. */
  f18a_setRegister(29, 0x00);

  /* No f18a_lock() after any of this, on purpose: relocking puts the F18A
   * back in TMS9918A mode and switches the ECM3 sprites above off again
   * (outline-only sprites) -- see f18a_nabu.c where f18a_lock() used to be. */
#if SPRITE_ECM_PAGE_SAMERICA
  f18a_loadPalette(sprite_ecm_pal_page0, 16, 8);
#endif
#if SPRITE_ECM_PAGE_EGYPT
  f18a_loadPalette(sprite_ecm_pal_page1, 24, 8);
#endif
#if SPRITE_ECM_PAGE_CASTLE || SPRITE_ECM_PAGE_MBASE
  f18a_loadPalette(sprite_ecm_pal_page2, 32, 8);
#endif
#if SPRITE_ECM_PAGE_MBASE
  f18a_loadPalette(sprite_ecm_pal_page3, 40, 8);
#endif
  f18a_loadPalette(sprite_ecm_pal_fallback, 56, 8);
}
#endif /* SPRITE_ECM_ENABLED */

/* Tested and rejected as a standalone fix -- see sysvid_nabu.h's comment on
 * these for the full finding (ECM=3 needs 8-color sprite pattern data this
 * port doesn't have yet; wiring just the register toggle in reinterpreted
 * the existing monochrome sprite patterns as noise instead of fixing them).
 * set_halfbitmap() above is #if SPRITE_ECM_ENABLED'd, currently 0 (see
 * sysvid_nabu.h) -- a second real attempt at wiring the palette/pattern
 * data in also produced garbage (engine/sprites.c's own SPRITE_ECM_*
 * comment has the full writeup), so it's back to disabled pending real
 * F18A sprite-ECM documentation.
 *
 * set_fullbitmap() -- ENABLED, reported as "after the first game, the title
 * screen gets corrupted": set_halfbitmap() leaves the VDP with ONE shared
 * 2KB pattern/colour table (registers 3/4), and the game now does return
 * to the title screen, whose full-screen picture needs the normal three
 * screen thirds -- in half mode the picture's top third showed three
 * times. Called by main.c's title_draw(); undoes everything
 * set_halfbitmap() changed: tables back to three thirds, sprite patterns
 * back to 0x3800 (0x2800 lies inside the three-thirds colour table), ECM
 * off. set_halfbitmap() redoes it all when gameplay/the intro starts on
 * an F18A. Verbatim register 3/4/49 values from xrick/src/sysvid.c. */
#if SPRITE_ECM_ENABLED
void set_fullbitmap(void) {
  if (!sysvid_halfBitmap) {
    return;
  }
  vdp_setRegister(3, 0xFF);
  vdp_setRegister(4, 0x03);
  vdp_setRegister(6, 7); /* sprite patterns at 0x3800 */
  gSpritePat = GAME_VRAM_SPRITEPAT_9918;
  sysvid_halfBitmap = 0;

  f18a_unlock();
  f18a_setRegister(49, 0x00); /* ECM=0: disable 8-color sprites */
}
#endif

/* SIZE PASS: both wrapped out -- zero callers project-wide. */
#if 0
void sysvid_update(void) {
  /* TODO(phase 3): flush any dirty tile/sprite state to VRAM. In the TI port
   * this is where the frame's accumulated draw.c changes get committed;
   * on NABU this should happen right after vdp_waitVDPReadyInt() so writes
   * land in the vertical blank window (see NABU-LIB.h's DEBUG_VDP_INT notes). */
}
#endif

/* TRIED AND REVERTED: this briefly did a real VDP register-1 BL-bit
 * blank/unblank (on request, "turn off the screen during loading") to
 * paper over lingering-sprite artifacts during loadTileBanks()/
 * map_loadMap()'s network loading -- reverted on request, both for the
 * visible blank-screen flash it traded in (felt more jarring than the
 * artifact it covered) and because every byte mattered this close to the
 * CRT_ORG_CODE budget. Back to the original no-op.
 *
 * SIZE PASS: was wrapped out alongside sysvid_update() above -- engine/
 * scr_imap.c is a real caller now. */
void sysvid_setGamma(U16 g) {
  (void)g;
  /* TMS9918A has no gamma/fade hardware; the TI port likely uses this for a
   * software fade-in/out on the F18A path only. On plain 9918A, this needs
   * either a software palette-less fade (brightness isn't controllable on
   *9918A -- consider a fade via backdrop/blank instead) or can be a no-op. */
}

/* Two defenses against VRAM corruption reported on MAME/real hardware
 * (not visible on Marduk) after screen_titlepage() (main.c) landed --
 * that title screen's two 6144-byte splash blits are by far the longest
 * sustained VDP writes anywhere in this port, long enough to make an
 * existing, narrow hazard actually visible:
 *
 * 1. VDP_WRITE_SETTLE(): one NOP between writes, in case the chip needs
 *    more settle time than incidental surrounding code happened to give
 *    it before. Left in as a cheap, harmless margin, but NOT confirmed as
 *    the actual cause -- the same unthrottled-loop technique, same
 *    6144-byte size, exists in a separate reference project
 *    (C:\nabu\rick-attract) WITHOUT this corruption, though that build has
 *    no HUD/sprite-attribute writes during actual gameplay to interact
 *    with, so it's not a clean disproof either.
 *
 * 2. NABU_DisableInterrupts()/EnableInterrupts() around the whole write
 *    loop, matching the protection engine/sprites.c's own bulk pattern
 *    upload already has (see that file's sprites_paint()). This is the
 *    leading theory: an interrupt firing mid-sequence that itself touches
 *    the VDP (a status-register read, which every documented TMS9918A/
 *    F18A vsync handler does to acknowledge the interrupt) resets the
 *    port's address-latch byte-pair state, desynchronizing every write
 *    after that point from its intended VRAM address -- producing exactly
 *    the reported symptom: a localized garbled patch partway through an
 *    otherwise-correct image, not a full-image shift or random noise, and
 *    only visible on cycle-accurate timing (MAME/real hardware) where the
 *    interrupt actually has a chance to land mid-write. Neither of this
 *    pair had this protection before -- only the sprite-pattern path did. */
/* UPDATED: 1 NOP wasn't enough to change anything on real hardware/MAME --
 * this codebase already has a documented, working VDP recovery delay
 * (hal/f18a_nabu.c's VDP_DETECT_WAIT(), "~8us, same as green.c", used
 * successfully for VDP register-write recovery during hardware
 * detection). Matching that proven constant here instead of the
 * under-dosed 1-NOP guess. */
#define VDP_WRITE_SETTLE() __asm__("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n")

void bitmapcharcopy(U16 adr, const U8 *buf, U16 size) {
  /* Real semantics, corrected this round -- see xrick/src/sysvid.c's actual
   * bitmapcharcopy(): this does NOT just write `size` bytes once. Because
   * VDP is always initialized with splitThirds=true (three independent 2KB
   * pattern/color blocks addressed by screen-row-third, not by name value
   * alone), a tile bank has to be written into ALL THREE 2KB blocks
   * identically, or the same tile index would show different graphics
   * depending which third of the screen it's drawn in. That's what the TI
   * version's `for (outer=adr; outer<adr+0x1800; outer+=0x800)` loop does.
   * This is why title_draw() (a full unique 256x192 bitmap, not a
   * reusable tileset) writes its 6144 bytes directly via IO_VDPDATA instead
   * of through this function -- it wants three *different* thirds, not the
   * same block repeated.
   *
   * The TI original also chunks the copy in 0x200-byte pieces with a
   * VDP_INT_POLL between them (an external TI-compiler macro, not defined
   * in this repo) so title music kept playing during a long blocking copy.
   * Not needed here: the long title/hall-of-fame loads keep the music
   * going themselves (engine/scr_hof.c's stream_to_vram()).
   *
   * BUG FIXED (F18A): after set_halfbitmap() the pattern and color tables
   * are ONE 2KB block (registers 3/4 masks), so only the first third is
   * real. Writing thirds 2 and 3 of the color table (gColor 0x2000 ->
   * 0x2800, 0x3000) landed right on the ECM sprite planes 0 and 1
   * (gSpritePat 0x2800, GAME_VRAM_SPRITEPAT_BIT1 0x3000), overwriting
   * whatever sprite frames were resident on every tile-bank load -- and
   * cost 3x the writes. One third only in that mode. The F18A also needs
   * no VDP_WRITE_SETTLE() padding between writes (stock TMS9918A does). */
  U16 third, i;
  U8 thirds = sysvid_halfBitmap ? 1 : 3;
  NABU_DisableInterrupts();
  for (third = 0; third < thirds; third++) {
    vdp_setWriteAddress(adr + third * 0x0800);
#if !VDP_TARGET_9918A_ONLY
    if (sysvid_nabu_hasF18A) {
      for (i = 0; i < size; i++) {
        IO_VDPDATA = buf[i];
      }
    }
#endif
#if !VDP_TARGET_F18A_ONLY
#if !VDP_TARGET_9918A_ONLY
    else
#endif
    {
      for (i = 0; i < size; i++) {
        IO_VDPDATA = buf[i];
        VDP_WRITE_SETTLE();
      }
    }
#endif
  }
  NABU_EnableInterrupts();
}

void vdpmemcpy2(U16 dest, const U8 *src, U16 cnt) {
  /* Plain sequential RAM-to-VRAM copy, no replication -- unlike
   * bitmapcharcopy() above, this writes to exactly one address range. Used
   * by tiles.c's loadDigitTiles() to patch a handful of
   * glyphs into the status-bar area, which only ever displays in the top
   * third of the screen, so it never needs the 3-way copy. Also maps.c's
   * maps_paint() (23 rows per call, every scroll step and submap change),
   * so the F18A skipping VDP_WRITE_SETTLE() -- it has no write-timing
   * limit -- roughly halves scroll repaint time there. */
  U16 i;
  NABU_DisableInterrupts();
  vdp_setWriteAddress(dest);
#if !VDP_TARGET_9918A_ONLY
  if (sysvid_nabu_hasF18A) {
    for (i = 0; i < cnt; i++) {
      IO_VDPDATA = src[i];
    }
  }
#endif
#if !VDP_TARGET_F18A_ONLY
#if !VDP_TARGET_9918A_ONLY
  else
#endif
  {
    for (i = 0; i < cnt; i++) {
      IO_VDPDATA = src[i];
      VDP_WRITE_SETTLE();
    }
  }
#endif
  NABU_EnableInterrupts();
}
