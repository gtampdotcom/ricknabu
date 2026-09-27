/*
 * engine/e_bomb.c -- ported from xrick/src/e_bomb.c
 *
 * Real changes from the TI source:
 *   - EXPLODE_SND (on detonation) is a real chiptune effect now -- direct
 *     sounds_playExplode() call (assets/EXPLODE.DAT), sharing
 *     sounds.c's one ticked AY slot with JUMP_SND/BULLET_SND/etc -- see
 *     that file's own header for why one shared slot, not a separate one
 *     per effect. `#ifdef ENABLE_SOUND`/sounds_play() (the generic
 *     TI-cartridge dispatch) is gone -- see sounds.c's own header.
 *     BOMBSHHT_SND (while ticking) DROPPED again (SIZE PASS, on request,
 *     closing part of the sound-enable budget gap) -- sounds.c no longer
 *     defines sounds_playBombshht() at all, not even a stub, since this
 *     was its only call site.
 *   - `UNUSED(U16 e)` in e_bomb_action()'s signature replaced with a plain
 *     `U16 e` parameter, same convention as e_bullet_action()/
 *     e_rick_action().
 *
 * Everything else is unchanged game logic, sprite numbers included --
 * this file's own "ST/TI bomb sprites sequence is longer" comment
 * (unchanged from the reference) explains why the ticking/exploding
 * sprites (0x99-0xA2, 0xA8-0xAC) are much higher than the plain two-frame
 * PC blink (0x22/0x23): those higher numbers are in sprite page 3
 * (dat_spritesTI3.c), which this port has never loaded as a whole page --
 * see engine/sprites.c's sprites_bomb_tail[]/sprites_bomb_boom[] comment
 * for the 15 specific frames extracted instead (same pattern as that
 * file's sprites_boulder_roll[] for the boulder's rolling animation).
 * e_rick.c's FIRING section already calls e_bomb_init() (see that file's
 * header) and ents.c's ent_actf[] dispatch table already calls
 * e_bomb_action() every frame once E_BOMB_ENT.n is non-zero -- both were
 * just waiting on this file to exist.
 */

#include "config.h"
#include "env.h"

#include "ents.h"
#include "e_bomb.h"
#include "e_rick.h"
#include "sounds.h"

#include "game.h"

/*
 * public vars (for performance reasons)
 */
U16 e_bomb_lethal;
U16 e_bomb_xc;
U16 e_bomb_yc;

/*
 * private vars
 */
U16 e_bomb_ticker;

/*
 * Bomb hit test
 *
 * ASM 11CD
 * returns: TRUE/hit, FALSE/not
 */
U16 e_bomb_hit(U16 e) {
    if (ent_ents[e].x > (E_BOMB_ENT.x >= 0xE0 ? 0xFF : E_BOMB_ENT.x + 0x20)) {
        return FALSE;
    }
    if (ent_ents[e].x + ent_ents[e].w < (E_BOMB_ENT.x > 0x04 ? E_BOMB_ENT.x - 0x04 : 0)) {
        return FALSE;
    }
    if (ent_ents[e].y > (E_BOMB_ENT.y + 0x1D)) {
        return FALSE;
    }
    if (ent_ents[e].y + ent_ents[e].h < (E_BOMB_ENT.y > 0x0004 ? E_BOMB_ENT.y - 0x0004 : 0)) {
        return FALSE;
    }
    return TRUE;
}

/*
 * Initialize bomb
 */
void e_bomb_init(U16 x, U16 y) {
    E_BOMB_ENT.n = 0x03;
    E_BOMB_ENT.x = x;
    E_BOMB_ENT.y = y;
    e_bomb_ticker = E_BOMB_TICKER;
    e_bomb_lethal = FALSE;

    /*
     * Atari ST & TI dynamite sprites are not centered the
     * way IBM PC sprites were ... need to adjust things a little bit
     */
    E_BOMB_ENT.x += 4;
    E_BOMB_ENT.y += 5;

}


/*
 * Entity action
 *
 * ASM 18CA
 */
void e_bomb_action(U16 e) {
    (void)e;

    /* tick */
    e_bomb_ticker--;

    if (e_bomb_ticker == 0)
    {
        /*
         * end: deactivate
         */
        delete_ent(E_BOMB_NO);
        e_bomb_lethal = FALSE;
    } else if (e_bomb_ticker >= 0x0A) {
        /*
         * ticking
         */
        /* BUG FIXED, not in the reference: the real ST/TI fuse-burning
         * sequence (sprites 0x99-0xA2, ticker 10-39) lives in page 3
         * (dat_spritesTI3.c), which this build no longer loads even the
         * hand-picked frames of -- see sprites.c's own header on why they
         * were cut (byte budget, to make room for engine/e_them.c's
         * type-1a/1b enemy). Sending those sprite numbers to sprites.c's
         * pattern dispatch now just clamps to sprite 0 (its documented
         * safe fallback for anything past page 0), which reads as the
         * bomb going invisible for the entire fuse-burning phase --
         * reported directly as "vanishes too quickly". Keep the visible
         * page-0 blink (0x22/0x23) going through this whole phase
         * instead: not the original animation, but continuously visible
         * beats silently vanishing for ~30 frames, and costs zero extra
         * bytes since 0x22/0x23 are already loaded (sprites_data0). */
        E_BOMB_ENT.sprite = (e_bomb_ticker & 0x01) ? 0x23 : 0x22;
        /* Fuse sound restored (was dropped for size): same cadence as the
         * reference, once every 4 ticks. */
        if ((e_bomb_ticker & 0x03) == 0x02) {
          sounds_playBombshht();
        }
    } else if (e_bomb_ticker == 0x09) {
        /*
         * explode
         */
        sounds_playExplode(); // real chiptune effect -- see sounds.c's own header
        /* See above: fixing alignment */
        E_BOMB_ENT.x -= 4;
        E_BOMB_ENT.y -= 5;
        /* BUG FIXED, not in the reference: same reasoning as the ticking
         * phase above -- the real explosion sprites (0xA8-0xAC) are also
         * page 3 and no longer loaded, so they'd clamp to blank sprite 0
         * too. Sprite 36 is already confirmed elsewhere in this port as a
         * real page-0 explosion-look sprite (sprites_paint2()'s
         * env_highlight fallback comment: "for highlight, just load one
         * of the explosion sprites"), so reuse it here instead of going
         * invisible right as the bomb actually detonates -- zero extra
         * bytes, and visually closer to "explosion" than blank. */
        E_BOMB_ENT.sprite = 36;

        e_bomb_xc = E_BOMB_ENT.x + 0x0C;
        e_bomb_yc = E_BOMB_ENT.y + 0x000A;
        e_bomb_lethal = TRUE;
        if (e_bomb_hit(E_RICK_NO)) {
            e_rick_gozombie();
        }
    } else {
        /*
         * exploding
         */
        E_BOMB_ENT.sprite = 36; /* see the ticker==0x09 case just above */

        /* exploding, hence lethal */
        if (e_bomb_hit(E_RICK_NO)) {
            e_rick_gozombie();
        }
    }
}

/* eof */
