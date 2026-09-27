/*
 * engine/ents.c -- ported from xrick/src/ents.c
 *
 * Changes from the TI source, confined to ent_actvis():
 *   - Removed `unsigned int nOldBank = nBank;`, `SWITCH_IN_BANK12`,
 *     `SWITCH_IN_BANK14`, and the two `SWITCH_IN_BANK(nOldBank)` restores.
 *     Those existed to page in the TI cart banks holding map_submaps[] and
 *     map_marks[] before touching them, then page back. No cartridge
 *     banking on NABU -- same policy as util.c/config.h, see those files'
 *     header comments for the fuller reasoning. The `tmpMark` temporary
 *     that existed to hold a value read while BANK12 was paged in (before
 *     switching to BANK14 to read map_marks) is no longer needed for that
 *     reason, but is left in place since it doesn't hurt and keeps this
 *     diff smaller/easier to review against the original.
 *   - Dropped `#ifdef CLASSIC99 / #include <vdp.h> #endif` (dead even in
 *     the TI build -- CLASSIC99 never actually defines it there either,
 *     it's commented out) and `#include <math.h>`, which nothing in this
 *     file actually uses.
 *
 * Everything else -- entity creation/allocation, the per-frame action
 * dispatch table, sprite lifecycle on delete_ent() -- is unchanged game
 * logic with no TI-specific content.
 */

#include "config.h"

#include "env.h"

#include "ents.h"

#include "game.h"
#include "debug.h"

#include "e_bullet.h"
#include "e_bomb.h"
#include "e_rick.h"
#include "e_them.h"
#include "e_bonus.h"
#include "e_box.h"
#include "e_sbonus.h"
#include "maps.h"
#include "draw.h"
#include "tiles.h"
#include "sprites.h"
#include "NABU-LIB.h" /* vdp_setWriteAddress()/IO_VDPDATA -- delete_ent()'s
                       * direct-to-hardware sprite hide, see its own comment */

/*
 * global vars
 */
ent_t ent_ents[ENT_ENTSNUM + 1];

/*
 * prototypes
 */
static void ent_addrect(U16, U16, U16, U16);
static U16 ent_creat1(U16*);
static U16 ent_creat2(U16*, U16);

// clean up entity type and associated sprite if they have one
//
// BUG FIXED: this used to only update sprite_table[] (the in-memory
// tracking array), matching xrick/src/ents.c's own delete_ent() exactly --
// verbatim there too, not a porting slip. On the reference platforms
// that's apparently not visibly wrong (whatever else usually reuses a
// slot quickly enough), but this port has no bulk "flush sprite_table[]
// to the real VDP sprite attribute table" pass anywhere -- every hardware
// sprite write happens per-entity, inside sprites_paint()/sprites_paint2()
// (see sprites.c's own header on why its off-screen branch already had to
// write its hide value straight to VDP for exactly this reason: "This
// port has no such bulk flush... so this port's clipped branch does the
// same"). delete_ent() was the one remaining place still only touching
// sprite_table[] -- a deleted entity's *last drawn* hardware sprite image
// stayed on screen untouched until something else happened to reuse that
// exact spriteIndex, which is what showed up as "sprites not being
// cleared until a new one is created" (most visible now that
// e_bullet.c/e_bomb.c create and delete entities far more often than the
// boulder ever did). Completing the same direct-to-hardware pattern here
// closes the gap.
void delete_ent(U16 e) {
    ent_ents[e].n = 0;
    ent_hideSprite(e); /* SIZE PASS: shares the VDP-hide loop below instead
                         * of repeating it -- see ent_hideSprite()'s own
                         * header for what that loop does. */
}

/*
 * Hide an entity's sprite without deactivating it.
 *
 * NOT a port of anything -- same VDP-hide technique as delete_ent() above
 * (blank both the in-memory mirror and the real hardware sprite slots),
 * minus the `.n = 0` that would mark the entity itself inactive/reusable.
 * For Rick specifically: main.c's map_chain() handling calls this the
 * instant e_rick_atExit fires (Rick has already walked off the old
 * submap's edge, but map_init() for the new one hasn't run yet), so he
 * doesn't sit visibly frozen at his old on-screen position/sprite for a
 * frame while the new submap's terrain and his own repositioned x/y load
 * -- he just reappears at the correct spot the moment ents_paintAll()
 * next runs, same as any entity being freshly (re)painted picks up a
 * clean spriteIndex.
 */
void ent_hideSprite(U16 e) {
    U16 i;

    /* Free its pattern slot (sprite_table[] bookkeeping, by spriteIndex)... */
    if (ent_ents[e].spriteIndex != 0xff) {
        for (i = 0; i < 4; i++) {
            sprite_table[ent_ents[e].spriteIndex + i].y = 0xd1;
        }
    }
    /* ...and move the 4 hardware sprites it was drawn with last frame
     * off-screen (hwIndex -- still its own until ents_paintAll() re-packs
     * them next frame, see sprites.c's sprites_beginFrame()). */
    if (ent_ents[e].hwIndex != 0xff) {
        NABU_DisableInterrupts();
        vdp_setWriteAddress((U16)(gSprite + (U16)ent_ents[e].hwIndex * 4));
        for (i = 0; i < 4; i++) {
            IO_VDPDATA = 0xd1; /* y */
            IO_VDPDATA = 0;    /* x */
            IO_VDPDATA = 0;    /* name */
            IO_VDPDATA = 0;    /* color */
        }
        NABU_EnableInterrupts();
    }
    ent_ents[e].spriteIndex = 0xff;
    ent_ents[e].lastSpriteDrawn = 0xff;
    ent_ents[e].hwIndex = 0xff;
}

/*
 * Reset entities
 *
 * ASM 2520
 */
void
ent_reset(void) {
    U16 i;

    E_RICK_STRST(E_RICK_STSTOP);
    e_bomb_lethal = FALSE;

    delete_ent(0);
    for (i = 2; ent_ents[i].n != 0xff; i++) {
        delete_ent(i);
    }
}


/*
 * Create an entity on slots 4 to 8 by using the first slot available.
 * Entities of type e_them on slots 4 to 8, when lethal, can kill
 * other e_them (on slots 4 to C) as well as rick.
 *
 * ASM 209C
 *
 * e: anything, CHANGED to the allocated entity number.
 * return: TRUE/OK FALSE/not
 */
static U16
ent_creat1(U16* e) {
  /* look for a slot */
    for (*e = 0x04; *e < 0x09; (*e)++)
        if (ent_ents[*e].n == 0) {  /* if slot available, use it */
            ent_ents[*e].c1 = 0;
            return TRUE;
        }

    return FALSE;
}


/*
 * Create an entity on slots 9 to C by using the first slot available.
 * Entities of type e_them on slots 9 to C can kill rick when lethal,
 * but they can never kill other e_them.
 *
 * ASM 20BC
 *
 * e: anything, CHANGED to the allocated entity number.
 * m: number of the mark triggering the creation of the entity.
 * ret: TRUE/OK FALSE/not
 */
static U16
ent_creat2(U16* e, U16 m) {
  /* make sure the entity created by this mark is not active already */
    for (*e = 0x09; *e < 0x0c; (*e)++)
        if (ent_ents[*e].n != 0 && ent_ents[*e].mark == m)
            return FALSE;

        /* look for a slot */
    for (*e = 0x09; *e < 0x0c; (*e)++)
        if (ent_ents[*e].n == 0) {  /* if slot available, use it */
            ent_ents[*e].c1 = 2;
            return TRUE;
        }

    return FALSE;
}


/*
 * Process marks that are within the visible portion of the map,
 * and create the corresponding entities.
 *
 * absolute map coordinate means that they are not relative to
 * map_frow, as any other coordinates are.
 *
 * ASM 1F40
 *
 * frow: first visible row of the map -- absolute map coordinate
 * lrow: last visible row of the map -- absolute map coordinate
 */
void
ent_actvis(U16 frow, U16 lrow) {
    U16 m;
    U16 e;
    U16 y;
    U16 tmpMark;

    tmpMark = map_submaps[env_submap].mark;

    /* BUG GUARD, not in the reference: map_marks[]/MAP_NBR_MARKS are still
     * truncated to submap 0's own 5-entry list (see maps.h's MAP_NBR_MARKS
     * comment) even though map_submaps[] itself now covers all 9 of map
     * 1's submaps (map_chain() can set env_submap to any of them). Any
     * OTHER submap's own .mark base (e.g. submap 1's is 5, submap 2's is
     * 17) points past the end of the array this build actually loaded --
     * reading it would walk off into whatever memory follows map_marks[],
     * not a controlled "no entities" result. Treat "this submap's marks
     * were never loaded" as "this submap has no entities yet" (same
     * "known gap, not a crash" spirit as maps.h's own comment) rather than
     * reading garbage. */
    if (tmpMark >= MAP_NBR_MARKS) {
        return;
    }

    /*
    * go through the list and find the first mark that
    * is visible, i.e. which has a row greater than the
    * first row (marks being ordered by row number).
    */
    for (m = tmpMark;
        map_marks[m].row != 0xff && map_marks[m].row < frow;
        m++);

    if (map_marks[m].row == 0xff)  /* none found */ {
        return;
    }

    /*
    * go through the list and process all marks that are
    * visible, i.e. which have a row lower than the last
    * row (marks still being ordered by row number).
    */
    for (;
        map_marks[m].row != 0xff && map_marks[m].row < lrow;
        m++) {

        /* ignore marks that are not active */
        if (map_marks_ent[m] & MAP_MARK_NACT)
            continue;

        /*
         * allocate a slot to the new entity
         *
         * slot type
         *  0   available for e_them (lethal to other e_them, and stops entities
         *      i.e. entities can't move over them. E.g. moving blocks. But they
         *      can move over entities and kill them!).
         *  1   xrick
         *  2   bullet
         *  3   bomb
         * 4-8  available for e_them, e_box, e_bonus or e_sbonus (lethal to
         *      other e_them, identified by their number being >= 0x10)
         * 9-C  available for e_them, e_box, e_bonus or e_sbonus (not lethal to
         *      other e_them, identified by their number being < 0x10)
         *
         * the type of an entity is determined by its .n as detailed below.
         *
         * 1               xrick
         * 2               bullet
         * 3               bomb
         * 4, 7, a, d      e_them, type 1a
         * 5, 8, b, e      e_them, type 1b
         * 6, 9, c, f      e_them, type 2
         * 10, 11          box
         * 12, 13, 14, 15  bonus
         * 16, 17          speed bonus
         * >17             e_them, type 3
         * 47              zombie
         */

        /* All entity types below 0x18 now have real ent_actf[] handlers
         * (e_them 1a/1b/2 in e_them.c, box/bonus/sbonus in e_box.c/
         * e_bonus.c/e_sbonus.c), and type 3 (>=0x18) is e_them.c too, so
         * the temporary "skip types without a real handler yet" filter
         * that used to sit here is gone -- this now matches the real
         * xrick ent_actvis() exactly, straight from the NACT check above
         * to slot allocation below. */

        if (!(map_marks[m].flags & ENT_FLG_STOPRICK)) {
            if (map_marks_ent[m] >= 0x10) {
                /* boxes, bonuses and type 3 e_them go to slot 4-8 */
                /* (c1 set to 0 -> all type 3 e_them are sleeping) */
                if (!ent_creat1(&e)) {
                    IFDEBUG_SCROLLER(
                      sys_printf("xrick/scroller: failed to create entity1 %#04X\n", e);
                      );
                    continue;
                }

                IFDEBUG_SCROLLER(
                  sys_printf("xrick/scroller: created entity %#04X creat1\n", e);
                );
            } else {
                /* type 1 and 2 e_them go to slot 9-c */
                /* (c1 set to 2) */
                if (!ent_creat2(&e, m)) {
                    IFDEBUG_SCROLLER(
                      sys_printf("xrick/scroller: failed to create entity2 %#04X\n", e);
                      );
                    continue;
                }

                IFDEBUG_SCROLLER(
                  sys_printf("xrick/scroller: created entity %#04X creat2(e,%d)\n", e, m);
                );
            }
        } else {
            /* entities stopping rick (e.g. blocks) go to slot 0 */
            if (ent_ents[0].n) {
                IFDEBUG_SCROLLER(
                  sys_printf("xrick/scroller: brick? already set\n");
                  );
                continue;
            }

            IFDEBUG_SCROLLER(
                sys_printf("xrick/scroller: resetting brick\n");
            );
            e = 0;
            ent_ents[0].c1 = 0;
        }

        /*
         * initialize the entity
         */
        ent_ents[e].mark = m;
        ent_ents[e].flags = map_marks[m].flags;
        ent_ents[e].n = map_marks_ent[m];

        /*
         * if entity is to be already running (i.e. not asleep and waiting
         * for some trigger to move), then use LETHALR i.e. restart flag, right
         * from the beginning
         */
        if (ent_ents[e].flags & ENT_FLG_LETHALR)
            ent_ents[e].n |= ENT_LETHAL;

        ent_ents[e].x = map_marks[m].xy & 0xf8;

        y = (map_marks[m].xy & 0x07) + (map_marks[m].row & 0xf8) - map_frow;
        y <<= 3;
        /* xrick adds 3 to every non-STOPRICK entity, which puts a 21px
         * walker's feet on the 24px grid. Entities that run on their own
         * (LETHALR: bricks, crushers, darts, bullets, other traps) stay
         * tile-aligned instead, as on the Atari ST -- measured on MBASE's
         * first room, where the soldier's bullet (flags 0x8c) flew 3px
         * below his gun and passed under Rick. This also covers the TI
         * port's moving-brick fix (every brick it matched by sprite number
         * has LETHALR), which it replaces. */
        if (!(ent_ents[e].flags & (ENT_FLG_STOPRICK | ENT_FLG_LETHALR))) {
            y += 3;
        }
        ent_ents[e].y = y;

        ent_ents[e].xsave = ent_ents[e].x;
        ent_ents[e].ysave = ent_ents[e].y;

        /*ent_ents[e].w0C = 0;*/  /* in ASM code but never used */

        ent_ents[e].w = ent_entdata[map_marks_ent[m]].w;
        ent_ents[e].h = ent_entdata[map_marks_ent[m]].h;
        ent_ents[e].sprbase = ent_entdata[map_marks_ent[m]].spr;
        ent_ents[e].sprite = ent_entdata[map_marks_ent[m]].spr;
        ent_ents[e].step_no_i = ent_entdata[map_marks_ent[m]].sni;
        ent_ents[e].trigsnd = ent_entdata[map_marks_ent[m]].snd;
        ent_ents[e].spriteIndex = 0xff;
        ent_ents[e].lastSpriteDrawn = 0xff;

        /*
         * FIXME what is this? when all trigger flags are up, then
         * use .sni for sprbase. Why? What is the point? (This is
         * for type 1 and 2 e_them, ...)
         *
         * This also means that as long as sprite has not been
         * recalculated, a wrong value is used. This is normal, see
         * what happens to the falling guy on the right on submap 3:
         * it changes when hitting the ground.
         */
#define ENT_FLG_TRIGGERS \
(ENT_FLG_TRIGBOMB|ENT_FLG_TRIGBULLET|ENT_FLG_TRIGSTOP|ENT_FLG_TRIGRICK)
        if ((ent_ents[e].flags & ENT_FLG_TRIGGERS) == ENT_FLG_TRIGGERS && e >= 0x09) {
            ent_ents[e].sprbase = (U16)(ent_entdata[map_marks_ent[m]].sni & 0x00ff);
        }
#undef ENT_FLG_TRIGGERS

        ent_ents[e].trig_x = map_marks[m].lt & 0xf8;
        ent_ents[e].latency = (map_marks[m].lt & 0x07) << 5;  /* <<5 eq *32 */

        ent_ents[e].trig_y = 3 + 8 * ((map_marks[m].row & 0xf8) - map_frow +
                      (map_marks[m].lt & 0x07));

        ent_ents[e].c2 = 0;
        ent_ents[e].offsy = 0;
        ent_ents[e].ylow = 0;

        // to make sure the correct sprite is loaded, if it's a type three then
        // just run the cycle for it once.
        if (ent_ents[e].n >= 0x18) {
            e_them_t3_action(e);
        }
    }
}

/*
 * Draw all entities onto the frame buffer.
 *
 * ASM 07a4
 *
 * NOTE This may need to be part of draw.c. Also needs better comments,
 * NOTE and probably better rectangles management.
 *
 * SIZE PASS: wrapped out -- already an empty no-op body with zero
 * callers, so the savings are close to nothing, but it's a free cut.
 */
#if 0
void ents_clearAll() {
}
#endif


// Instead of dealing with rects and redraws and the like, we just need to load up the sprite table
// Don't use the rect system, it's not compiled in.
void ents_paintAll(void) {
    U16 i;

    /* Hardware sprites are re-packed from 0 every frame -- see
     * sprites.c's sprites_beginFrame(). */
    sprites_beginFrame();

    /* foreground loop : draw all entities that are visible */
    for (i = 0; ent_ents[i].n != 0xff; i++)
    {
        if (ent_ents[i].n && (env_highlight || ent_ents[i].sprite))
        {
            /* if entity is active, draw the sprite. */
            sprites_paint2(i);
            /* A pattern upload can be long enough to span a vblank --
             * count it now (safe: the upload's VDP writes are complete). */
            sys_nabu_pollVblank();
        } else {
            ent_ents[i].hwIndex = 0xff;
        }
    }

    sprites_endFrame();
}

/*
 * Clear entities previous state
 *
 */
void
ent_clprev(void) {
    U16 i;

    for (i = 0; ent_ents[i].n != 0xff; i++) {
        ent_ents[i].prev_n = 0;
    }
}

/*
 * Table containing entity action function pointers.
 */
void (*ent_actf[])(U16) = {
  NULL,        /* 00 - zero means that the slot is free */
  e_rick_action,   /* 01 - 12CA */
  e_bullet_action,  /* 02 - 1883 */
  e_bomb_action,  /* 03 - 18CA */
  e_them_t1a_action,  /* 04 - 2452 */
  e_them_t1b_action,  /* 05 - 21CA */
  e_them_t2_action,  /* 06 - 2718 */
  e_them_t1a_action,  /* 07 - 2452 */
  e_them_t1b_action,  /* 08 - 21CA */
  e_them_t2_action,  /* 09 - 2718 */
  e_them_t1a_action,  /* 0A - 2452 */
  e_them_t1b_action,  /* 0B - 21CA */
  e_them_t2_action,  /* 0C - 2718 */
  e_them_t1a_action,  /* 0D - 2452 */
  e_them_t1b_action,  /* 0E - 21CA */
  e_them_t2_action,  /* 0F - 2718 */
  e_box_action,  /* 10 - 245A */
  e_box_action,  /* 11 - 245A */
  e_bonus_action,  /* 12 - 242C */
  e_bonus_action,  /* 13 - 242C */
  e_bonus_action,  /* 14 - 242C */
  e_bonus_action,  /* 15 - 242C */
  e_sbonus_start,  /* 16 - 2182 */
  e_sbonus_stop  /* 17 - 2143 */
};


/*
 * Run entities action function
 *
 */
void
ent_action(void) {
    U16 i, k;

    IFDEBUG_ENTS(
      sys_printf("xrick/ents: --------- action ----------------\n");
    for (i = 0; ent_ents[i].n != 0xff; i++)
        if (ent_ents[i].n) {
            sys_printf("xrick/ents: slot %#04x, entity %#04x", i, ent_ents[i].n);
            sys_printf(" (%#06x, %#06x), sprite %#04x.\n",
                   ent_ents[i].x, ent_ents[i].y, ent_ents[i].sprite);
        }
            );

    for (i = 0; ent_ents[i].n != 0xff; i++) {
        if (ent_ents[i].n) {
            k = ent_ents[i].n & 0x7f;
            if (k == 0x47)
                e_them_z_action(i);
            else if (k >= 0x18)
                e_them_t3_action(i);
            else
                ent_actf[k](i);
        }
    }
}


/* eof */
