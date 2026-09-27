/*
 * engine/fb.c -- adapted from xrick/src/fb.c
 *
 * Real change from the TI source: xrick's fb.c is a full software
 * framebuffer abstraction -- an off-screen byte array (`fb_buf`) that
 * maps.c/draw.c/ents.c paint into, plus dirty-rectangle tracking
 * (`fb_updatedRects`) that game_loop()'s sysvid_update() call flushes to
 * VDP once per frame. This port writes straight to VRAM instead (the
 * title picture, hall-of-fame banner, tile-bank loads, maps.c's painting).
 *
 * So rather than port the whole RAM-buffer-plus-flush machinery just to
 * give fb_clear() a body, this is a direct adaptation: blank the VDP name
 * table itself (every one of the 768 cells -> tile index '@' (0x40)).
 *
 * NOT tile index ' ' (0x20, ASCII space) -- that was this function's first
 * revision, and it's wrong: tile bank 0 isn't a pure font, it's the whole
 * "main intro" tileset (tiles.h's own bank-0 comment), with letters just
 * placed at their ASCII ordinal slots for convenience. Whatever piece of
 * intro scenery happens to occupy slot 0x20 in that tileset is NOT blank --
 * on real hardware it rendered as a visible green/white decorative tile
 * repeated across the whole background. '@' (0x40) is confirmed blank
 * instead: it's the padding character scr_hof.c's game_hscores[] name
 * strings already use ("TURSILION@", etc., verbatim from the TI source),
 * and on real hardware those padded name cells render as a clean gap, not
 * visible tiles -- so '@' is this font's actual established blank-tile
 * convention, not ' '. Likely the same reason the TI source's own credits
 * screen used hyphens instead of spaces for word breaks.
 *
 * This gives the same visible result (a clear screen to paint fresh
 * content onto) via the mechanism this port actually has, rather than the
 * TI source's RAM-buffer-plus-flush one. Revisit this once maps.c/draw.c
 * bring in the real fb_buf/fb_updatedRects model -- at that point
 * fb_clear() should clear *that* buffer instead, and something else
 * (sysvid_update(), most likely) becomes responsible for the actual VDP
 * write.
 */

#include "config.h"
#include "fb.h"
#include "game.h"
#include "NABU-LIB.h"

/* SIZE PASS: was wrapped out (zero callers) until engine/scr_imap.c came
 * back into the build -- that's the real caller now. */
void fb_clear(void) {
  U16 i;

  vdp_setWriteAddress(gImage);
  for (i = 0; i < 768; i++) {
    IO_VDPDATA = (U8)'@'; /* confirmed-blank tile -- see file header */
  }
}

/* eof */
