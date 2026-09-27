/*
 * engine/env.c -- ported from xrick/src/env.c
 *
 * Real changes from the TI source, all confined to env_paintGame():
 *
 *   - Dropped `#include <conio.h>` and the commented-out `<vdp.h>` include --
 *     both TI-cross-compiler-only headers with no z88dk/nabulib equivalent.
 *   - VDP_INT_DISABLE / VDP_INT_ENABLE (from the TI compiler's <vdp.h>, not
 *     defined anywhere in this repo -- it's part of that external compiler's
 *     library) become NABU_DisableInterrupts() / NABU_EnableInterrupts()
 *     from NABU-LIB.h. Same purpose: this function does a burst of VDP
 *     writes and doesn't want a VDP interrupt landing mid-burst.
 *   - VDP_SET_ADDRESS_WRITE(addr) becomes vdp_setWriteAddress(addr).
 *   - VDPWD(byte) (write one byte to VDP data port, auto-increment) becomes
 *     a direct IO_VDPDATA = byte, same as hal/sysvid_nabu.c's
 *     bitmapcharcopy() already does for the same reason -- nabulib's public
 *     vdp_write() prints a text-mode character at the cursor, which is not
 *     what a raw VRAM stream wants.
 *
 * Everything else (the score/lives/bombs/bullets state and addscore()) is
 * unchanged -- no TI-specific content there.
 */

#include "config.h"
#include "env.h"

#include "fb.h"
#include "tiles.h"

#include "game.h"
#include "NABU-LIB.h"

U16 env_trainer = FALSE;
U16 env_invicible = FALSE;
U16 env_highlight = FALSE;

U16 env_depth = TRUE;

U16 env_lives = 0;
U16 env_bombs = 0;
U16 env_bullets = 0;
U16 env_score_lo = 0;
U16 env_score_hi = 0;

U16 env_map = 0;
U16 env_submap = 0;
U16 env_changeSubmap = FALSE;

// not that we'll ever be able to use proper ASCII, but hey...
U8 env_digits = 48;


/*
 * FIXME counters positions in fp/px
 */
// break up the 32 bit score into two 16s
//
// WIRED UP: engine/e_them.c's e_them_gozombie() calls this now (killing an
// e_them type 1a/1b enemy scores 50 points) -- see that file's own header.
// e_bonus.c's own scoring calls still aren't wired in (that file isn't
// ported yet).
void addscore(U16 val) {
    env_score_lo += val;
    if (env_score_lo > 9999) {
        env_score_lo -= 10000;
        env_score_hi++;
    }
}

/*
 * env_paintGame
 *
 * paints the game environment (score, lives, bullets, bombs).
 *
 * main.c's loop calls this once per frame. It writes to
 * gImage's row 0 directly, which maps_paint() (maps.c) already
 * deliberately skips ("23 rows, cause we skip the status row 0") -- the
 * two never fight over the same VRAM bytes.
 */
void env_paintGame(void)
{
    NABU_DisableInterrupts();

    vdp_setWriteAddress(gImage);

    // cheats
    // CHANGED, reported as "shows 11 instead of 1": env_trainer and
    // env_invicible now always toggle together as one combined WAAA MODE
    // cheat (main.c's own 'w' key handling) instead of two independent
    // flags, so writing a separate "1" tile for each showed two adjacent
    // digits for what's really one on/off state. Only the first cell is
    // meaningful now; the other two stay blank (env_highlight has no key
    // bound to it any more either) -- kept as three total writes rather
    // than two so the score/ammo/bomb/life icons after this in the same
    // row don't shift columns.
    IO_VDPDATA = (env_invicible?env_digits+1:0);
    IO_VDPDATA = 0;
    IO_VDPDATA = 0;

    // score
    IO_VDPDATA = (env_digits+(env_score_hi/10));
    IO_VDPDATA = (env_digits+(env_score_hi%10));
    U16 x = env_score_lo;
    IO_VDPDATA = (env_digits+(x/1000));
    x %= 1000;
    IO_VDPDATA = (env_digits+(x/100));
    x %= 100;
    IO_VDPDATA = (env_digits+(x/10));
    IO_VDPDATA = (env_digits+(x%10));

    IO_VDPDATA = (0);

    // bullets
    for (U16 i=0; i<6; ++i) {
        // +9 because the defines are 1 based
        IO_VDPDATA = (env_bullets>i ? TILES_BULLET+env_digits+9 : 0);
    }

    IO_VDPDATA = (0);

    // bombs
    for (U16 i=0; i<6; ++i) {
        IO_VDPDATA = (env_bombs>i ? TILES_BOMB+env_digits+9 : 0);
    }

    IO_VDPDATA = (0);

    // ricks
    for (U16 i=0; i<6; ++i) {
        IO_VDPDATA = (env_lives>i ? TILES_RICK+env_digits+9 : 0);
    }

    /* NABU-specific addition, not in the reference: row 0 is 32 tiles wide
     * (FB_WIDTH=256px / 8px), but everything above only writes 30
     * (3+6+1+6+1+6+1+6) -- verbatim from xrick/src/env.c, which apparently
     * never had to care about the last 2 cells (narrower visible area on
     * that platform, maybe). NABU's Graphics II mode shows the full
     * 256px/32 columns with no overscan crop, so those 2 cells were
     * showing whatever was left in the name table there -- nothing else
     * writes row 0 before gameplay starts (maps_paint() deliberately
     * skips it), so it was stray leftover title-screen content, visible
     * as a few stray pixels just past the lives icons (the last group
     * this function writes). They're written blank. (For a while they
     * showed env_submap as a debug aid -- removed on request, "the submap
     * number in the hud can be removed".) */
    IO_VDPDATA = 0;
    IO_VDPDATA = 0;

    NABU_EnableInterrupts();
}

/* eof */
