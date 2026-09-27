/*
 * engine/e_box.c -- ported from xrick/src/e_box.c.
 * Dropped: the SWITCH_IN_BANK cartridge-bank-switching macros and nBank
 * (no bank switching on NABU -- this file was never banked upstream
 * anyway). ENABLE_SOUND's BOX_SND/EXPLODE_SND calls are both back now as
 * direct sounds_playBox()/sounds_playExplode() calls, real chiptune
 * effects (assets/BOX.DAT/EXPLODE.DAT) sharing engine/sounds.c's one
 * ticked AY slot with JUMP_SND/BULLET_SND/etc (see that file's own header
 * for why one shared slot instead of one per effect). Everything else
 * verbatim.
 */

#include "ricksystem.h"
#include "config.h"
#include "env.h"

#include "game.h"
#include "ents.h"
#include "e_box.h"
#include "e_bullet.h"
#include "e_bomb.h"
#include "e_rick.h"
#include "maps.h"
#include "util.h"
#include "sounds.h"

/*
 * FIXME this is because the same structure is used
 * for all entities. Need to replace this w/ an inheritance
 * solution.
 */
#define cnt c1

/*
 * Constants
 */
#define SEQ_INIT 0x0A

/*
 * Prototypes
 */
static void explode(U16);

/*
 * Entity action
 *
 * ASM 245A
 */
void
e_box_action(U16 e) {
    static U16 sp[] = { 0x24, 0x25, 0x26, 0x27, 0x28 };  /* explosion sprites sequence */

    if (ent_ents[e].n & ENT_LETHAL) {
        /*
         * box is lethal i.e. exploding
         * play sprites sequence then stop
         */
        ent_ents[e].sprite = sp[ent_ents[e].cnt >> 1];
        if (--ent_ents[e].cnt == 0) {
            delete_ent(e);
            map_marks_ent[ent_ents[e].mark] |= MAP_MARK_NACT;
        }
    } else {
        /*
         * not lethal: check to see if triggered
         */
        if (e_rick_boxtest(e)) {
            /* rick: collect bombs or bullets and stop */
            sounds_playBox(); /* back on request, real chiptune effect --
                                * see sounds.c's own header */
            if (ent_ents[e].n == 0x10) {
                env_bombs = GAME_BOMBS_INIT;
            } else { /* 0x11 */
                env_bullets = GAME_BULLETS_INIT;
            }
            delete_ent(e);
            map_marks_ent[ent_ents[e].mark] |= MAP_MARK_NACT;
        } else if (E_RICK_STTST(E_RICK_STSTOP) && u_fboxtest(e, e_rick_stop_x, e_rick_stop_y)) {
            /* rick's stick: explode */
            explode(e);
        } else if (E_BULLET_ENT.n && u_fboxtest(e, e_bullet_xc, e_bullet_yc)) {
            /* bullet: explode (and stop bullet) */
            delete_ent(E_BULLET_NO);
            explode(e);
        } else if (e_bomb_lethal && e_bomb_hit(e)) {
            /* bomb: explode */
            explode(e);
        }
    }
}


/*
 * Explode when
 */
static void explode(U16 e) {
    ent_ents[e].cnt = SEQ_INIT;
    ent_ents[e].n |= ENT_LETHAL;
    sounds_playExplode(); // real chiptune effect -- see sounds.c's own header
}

/* eof */
