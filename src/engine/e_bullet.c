/*
 * engine/e_bullet.c -- ported from xrick/src/e_bullet.c
 *
 * Real changes from the TI source:
 *   - The original `sounds_play(BULLET_SND)` call is now a direct
 *     sounds_playBullet() call (no `#ifdef ENABLE_SOUND` -- that macro's
 *     sounds_play() dispatch still doesn't exist, see engine/sounds.c's
 *     own header) -- a real chiptune gunshot effect (assets/
 *     BULLET.DAT), back on request the same way JUMP_SND came back in
 *     e_rick.c: its own dedicated function sharing one ticked AY slot
 *     with JUMP_SND, not the old generic dispatch table. See sounds.c's
 *     own header for why one shared slot, not two independent ones.
 *   - `UNUSED(U16 e)` in e_bullet_action()'s signature (a ricksystem.h
 *     macro for suppressing unused-parameter warnings on compilers where
 *     that mattered) is replaced with a plain `U16 e` parameter -- same
 *     convention e_rick.c already established for e_rick_action().
 *
 * Everything else is unchanged game logic: e_rick.c's FIRING section
 * calls e_bullet_init() (already real, see that file's header) with
 * Rick's position and game_dir picks the sprite/direction; ent_action()'s
 * dispatch table (ents.c's ent_actf[]) already calls e_bullet_action()
 * every frame once E_BULLET_ENT.n is non-zero -- both were just waiting
 * on this file to exist, no wiring changes needed elsewhere except
 * removing this pair's stubs from stub_ebehavior.c and sprites.c's
 * pattern-load dispatch not needing changes at all: sprites 0x20/0x21 are
 * both within page 0 (sprites_data0), already resident.
 */

#include "ricksystem.h"
#include "game.h"
#include "ents.h"
#include "e_bullet.h"
#include "sysvid.h"
#include "sounds.h"

#include "maps.h"

/*
 * public vars (for performance reasons)
 */
S16 e_bullet_offsx;
U16 e_bullet_xc, e_bullet_yc;

/*
 * Initialize bullet
 */
void e_bullet_init(U16 x, U16 y) {
    E_BULLET_ENT.n = 0x02;
    E_BULLET_ENT.x = x;
    E_BULLET_ENT.y = y + 0x0006;
    if (game_dir == LEFT) {
        e_bullet_offsx = -0x08;
        E_BULLET_ENT.sprite = 0x21;
    } else {
        e_bullet_offsx = 0x08;
        E_BULLET_ENT.sprite = 0x20;
    }
    sounds_playBullet(); // back on request, real chiptune effect -- see sounds.c's own header
}


/*
 * Entity action
 *
 * ASM 1883, 0F97
 */
void e_bullet_action(U16 e) {
    (void)e;

    /* move bullet */
    E_BULLET_ENT.x += e_bullet_offsx;

    if (E_BULLET_ENT.x <= -0x10 || E_BULLET_ENT.x > 0xe8) {
        /* out: deactivate */
        delete_ent(E_BULLET_NO);
    } else {
      /* update bullet center coordinates */
        e_bullet_xc = E_BULLET_ENT.x + 0x0c;
        e_bullet_yc = E_BULLET_ENT.y + 0x05;
        if (map_eflg[map_map[e_bullet_yc >> 3][e_bullet_xc >> 3]] & MAP_EFLG_SOLID) {
            /* hit something: deactivate */
            delete_ent(E_BULLET_NO);
        }
    }
}

/* eof */
