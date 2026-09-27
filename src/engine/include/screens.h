/*
 * engine/include/screens.h -- PARTIAL port of xrick/include/screens.h
 *
 * Ported so far: everything screen_introMap() (engine/scr_imap.c) needs -- the
 * screen_imapsteps_t struct and the screen_imapsl[]/screen_imapsteps[]
 * table declarations (bodies in engine/dat_screens.c, verbatim from
 * xrick/src/dat_screens.c -- pure data, no TI-specific content).
 *
 * Still not ported (no bodies exist yet):
 *   - screen_gameover()/screen_getname()/screen_pause() declarations.
 *   - screen_imaptext[] -- the TI source declares this but xrick/src never
 *     actually defines or uses it (dead declaration even upstream, per
 *     grep across xrick/src); scr_imap.c uses its own local
 *     maps_intros[].title/.body instead (see that file). Left out rather
 *     than carrying forward a declaration nothing implements.
 *
 * map0_title/map0_body used to be declared here as real `extern U8[]`
 * BSS arrays so main.c's loadAssets() could sys_nabu_loadAsset() into
 * them directly. They're gone from this header now -- see engine/
 * scr_imap.c's own comment on them: BSS costs real bytes in this NABU
 * build just like `const` data does (tiles.h's tiles_banks_shared union
 * comment explains why), so rather than reserving 312 more bytes for
 * them, they're now `#define`d directly onto tiles_banks_shared's own
 * memory -- file-local to scr_imap.c, loaded there (not by main.c)
 * during the one window that memory is safe to repurpose. */

#ifndef _SCREENS_H
#define _SCREENS_H

#include "ricksystem.h"

#define SCREEN_TIMEOUT (U32)4000
#define SCREEN_RUNNING 0
#define SCREEN_DONE 1
#define SCREEN_EXIT 2

typedef struct {
  U16 count;  /* number of loops */
  U16 dx, dy;  /* sprite x and y deltas */
  U16 base;  /* base for sprite numbers table */
} screen_imapsteps_t;  /* description of one step */

extern const U8 screen_imapsl[];  /* sprite lists */
extern const screen_imapsteps_t screen_imapsteps[];  /* map intro steps */

extern U16 screen_introMap(void);  /* map intro */

/* Forces screen_introMap()'s own state machine back to "ready to run
 * case 0 from scratch" -- see main.c's own call site comment (right
 * before the while(screen_introMap()!=SCREEN_DONE) loop) for why this
 * guard exists: something during real gameplay has been observed to
 * leave scr_imap.c's static `seq` at a stray nonzero value that matches
 * none of screen_introMap()'s own switch cases (confirmed via MAME
 * debugger: seq read as 3 in one session, 1 in another, BEFORE any level
 * had even been picked on the level-select screen that follows) --
 * screen_introMap() then spins forever, since nothing in its switch ever
 * changes an unrecognized seq value back to a valid one. Patching seq to
 * 0 live in the debugger immediately unstuck the game both times, which
 * is the direct evidence for this fix. The real corruption source (something
 * overwriting scr_imap.c's `seq` from outside screen_introMap() itself)
 * is still unknown -- this only guards the one confirmed symptom. */
extern void screen_introMap_reset(void);

/* Which map's title/body text (and matching animation-start-step/center-
 * tile) screen_introMap() shows -- 0=SAMERICA, 1=EGYPT, 2=CASTLE,
 * 3=MBASE, 4=the reference's "much much later" epilogue (real xrick text,
 * normally shown after finishing map 4, not before another one -- shown
 * here as a straight preview instead, on request), set by main.c's own
 * screen_titlepage() from a '1'-'5' keypress (see that call site's own
 * comment). Real map 1..4 gameplay data was never ported (maps.h's own
 * comment) -- this only ever previews each map's own title card before
 * map 1's own gameplay begins as usual, on request as a way to see all
 * five without map_chain() or a real ending ever needing to work. */
extern U8 screen_introMap_sel;

#endif

/* eof */
