/*
 * engine/e_rick.c -- ported from xrick/src/e_rick.c
 *
 * This is Rick's actual physics/state-machine logic (walk, jump, fall,
 * crawl, climb, environment collision via util.c's u_envtest(), firing).
 * Ported as close to verbatim as this project gets -- no gameplay logic
 * was changed, only what was needed to compile without cartridge-specific
 * or not-yet-ported dependencies:
 *
 *   - `#ifdef ENABLE_SOUND`/`sounds_play()` (the generic TI-cartridge
 *     dispatch, engine/sounds.c's own header) is gone -- every effect
 *     this file plays (JUMP_SND/DIE_SND/WALK_SND/CRAWL_SND/STICK_SND/
 *     PAD_SND) now goes through its own dedicated sounds_play*()/
 *     resident-buffer pair instead, back on request one at a time rather
 *     than reviving the whole dropped dispatch table.
 *   - `UNUSED(U16 e)` in the original `e_rick_action()` signature (a
 *     ricksystem.h macro for suppressing unused-parameter warnings on
 *     compilers where that mattered) is replaced with a plain `U16 e`
 *     parameter -- this port's ricksystem.h still defines UNUSED() but
 *     e_rick.h already declares `void e_rick_action(U16);` without it, so
 *     matching the header exactly is simpler than fighting the macro.
 *   - e_bullet_init()/e_bomb_init() (called from the FIRING section) are
 *     currently stubs (see engine/stub_ebehavior.c) -- pressing fire while
 *     standing still will call them and get a harmless no-op, not a crash,
 *     but nothing will actually fire yet. Real bullets/bombs need
 *     e_bullet.c/e_bomb.c ported, which isn't in scope for "let me control
 *     Rick" -- walking/jumping/falling/crawling/climbing and their
 *     collision against the map are the parts that actually move.
 *   - e_rick_atExit (set when Rick walks off either edge of the current
 *     submap, per the original's own x<0 / x>=0xe8 checks) is NOT acted on
 *     by anything yet -- map_chain()/scroller.c (loading the connecting
 *     submap and scrolling to it) aren't ported. main.c's game loop resets
 *     the flag each frame without transitioning, so walking into an edge
 *     currently just re-teleports Rick to the position the original code
 *     sets (0xe2 near the right edge, 0x04 near the left) against the
 *     SAME still-loaded submap -- a visible wrap-around glitch, not a
 *     crash, and a known limitation until submap chaining exists.
 */

#include "ricksystem.h"
#include "config.h"
#include "env.h"

#include "e_rick.h"

#include "game.h"
#include "ents.h"
#include "e_bullet.h"
#include "e_bomb.h"
#include "control.h"
#include "maps.h"
#include "util.h"
#include "sounds.h"

U16 game_dir = 0; 

/*
 * public vars
 */
U16 e_rick_stop_x = 0;
U16 e_rick_stop_y = 0;
U16 e_rick_state = 0;
U16 e_rick_atExit = FALSE; // TRUE when rick is exiting the submap

/*
 * local vars
 */
static U16 scrawl;
static U16 trigger = FALSE;
static S16 offsx;
static U16 ylow;
static S16 offsy;
static U16 seq;
/* save_crawl/save_x/save_y: used by e_rick_save()/e_rick_restore() below.
 * Un-wrapped together with those -- main.c's main loop now has real
 * death/respawn handling that calls them (see that file's header). */
static U16 save_crawl;
static U16 save_x, save_y;

/*
 * Box test
 *
 * ASM 113E (based on)
 *
 * e: entity to test against (corresponds to SI in asm code -- here DI
 *    is assumed to point to rick).
 * ret: TRUE/intersect, FALSE/not.
 */
U16 e_rick_boxtest(U16 e) {
    /*
     * rick: x+0x05 to x+0x11, y+[0x08 if rick's crawling] to y+0x14
     * entity: x to x+w, y to y+h-1
     *
     * RESTORED to xrick's original box, on request: the TI port had
     * tightened it for "modern" leniency (rick x+0x05 to x+0x0a, bottom
     * y+0x12; entity y+4 to y+h-5), which let low, flat projectiles miss
     * him -- e.g. the soldier's bullet at MBASE's start (y 155, 8 high)
     * passed under a standing Rick (y 139) instead of killing him the way
     * it does on the Atari ST.
     */

    if (E_RICK_ENT.x + 0x11 < ent_ents[e].x ||
        E_RICK_ENT.x + 0x05 > ent_ents[e].x + ent_ents[e].w ||
        E_RICK_ENT.y + 0x14 < ent_ents[e].y ||
        E_RICK_ENT.y + (E_RICK_STTST(E_RICK_STCRAWL) ? 0x08 : 0x00) > ent_ents[e].y + ent_ents[e].h - 1) {
        return FALSE;
    } else {
        return TRUE;
    }
}

/*
 * Go zombie
 *
 * ASM 1851
 */
void e_rick_gozombie(void) {
    if (env_invicible) return;

    /* already zombie? */
    if E_RICK_STTST(E_RICK_STZOMBIE) return;

    sounds_playDie(); // real chiptune effect -- see sounds.c's own header

    E_RICK_STSET(E_RICK_STZOMBIE);
    offsy = -0x0400;
    offsx = (E_RICK_ENT.x > 0x80 ? -3 : +3);
    ylow = 0;
}

/*
 * Action sub-function for e_rick when zombie
 *
 * ASM 17DC
 */
static void e_rick_z_action(void) {
    S32 i;

    /* sprite */
    E_RICK_ENT.sprite = (E_RICK_ENT.x & 0x04) ? 0x1A : 0x19;

    /* x */
    E_RICK_ENT.x += offsx;

    /* y */
    i = ((S32)E_RICK_ENT.y << 8) + offsy + ylow;
    E_RICK_ENT.y = (U16)(i >> 8);
    offsy += 0x80;
    ylow = (U8)i;

    /* dead when out of screen */
    if (E_RICK_ENT.y < 0 || E_RICK_ENT.y > 0xdc) {
        E_RICK_STSET(E_RICK_STDEAD);
    }
}


/*
 * Shared by e_rick_action2()'s horiz: and climbing: cases below (not in
 * the original -- both used to repeat this exact 6-line submap-edge check
 * verbatim). *x is already the candidate new x (post +/-2); on TRUE the
 * caller must return immediately (Rick has walked off this submap's
 * edge, position already reset to the far side for map_chain() to pick
 * up). The "wrong-direction" bound (e.g. >=0xe8 right after a -2 move)
 * can never actually trigger for a given delta -- harmless, same as it
 * being unreachable inline at each original call site.
 */
static U16 e_rick_checkExit(S16 *x) {
    if (*x < 0) {
        e_rick_atExit = TRUE;
        E_RICK_ENT.x = 0xe2;
        return TRUE;
    }
    if (*x >= 0xe8) {
        e_rick_atExit = TRUE;
        E_RICK_ENT.x = 0x04;
        return TRUE;
    }
    return FALSE;
}

/*
 * Action sub-function for e_rick.
 *
 * ASM 13BE
 */
void e_rick_action2(void) {
    U16 env0, env1;
    S16 x, y;
    S32 i;

    E_RICK_STRST(E_RICK_STSTOP | E_RICK_STSHOOT);

    /* if zombie, run dedicated function and return */
    if E_RICK_STTST(E_RICK_STZOMBIE) {
        e_rick_z_action();
        return;
    }

    /* climbing? */
    if E_RICK_STTST(E_RICK_STCLIMB) {
        goto climbing;
    }

    /*
    * NOT CLIMBING
    */
    E_RICK_STRST(E_RICK_STJUMP);
    /* calc y */
    i = ((S32)E_RICK_ENT.y << 8) + offsy + ylow;
    y = (U16)(i >> 8);
    /* test environment */
    u_envtest(E_RICK_ENT.x, y, E_RICK_STTST(E_RICK_STCRAWL), &env0, &env1);
    /* stand up, if possible */
    if (E_RICK_STTST(E_RICK_STCRAWL) && !env0) {
        E_RICK_STRST(E_RICK_STCRAWL);
    }
    /* can move vertically? */
    if (env1 & (offsy < 0 ?
        MAP_EFLG_VERT | MAP_EFLG_SOLID | MAP_EFLG_SPAD :
        MAP_EFLG_VERT | MAP_EFLG_SOLID | MAP_EFLG_SPAD | MAP_EFLG_WAYUP)) {
        goto vert_not;
    }

    /*
    * VERTICAL MOVE
    */
    E_RICK_STSET(E_RICK_STJUMP);
    /* killed? */
    if (env1 & MAP_EFLG_LETHAL) {
        e_rick_gozombie();
        return;
    }
    /* save */
    E_RICK_ENT.y = y;
    ylow = (U8)i;
    /* climb? */
    if ((env1 & MAP_EFLG_CLIMB) && (control_status & (CONTROL_UP | CONTROL_DOWN))) {
        offsy = 0x0100;
        E_RICK_STSET(E_RICK_STCLIMB);
        return;
    }
    /* fall */
    offsy += 0x0080;
    if (offsy > 0x0800) {
        offsy = 0x0800;
        ylow = 0;
    }

    /*
    * HORIZONTAL MOVE
    */
horiz:
    /* should move? */
    if (!(control_status & (CONTROL_LEFT | CONTROL_RIGHT))) {
        seq = 2; /* no: reset seq and return */
        return;
    }
    if (control_status & CONTROL_LEFT) {  /* move left */
        x = E_RICK_ENT.x - 2;
        game_dir = LEFT;
    } else {  /* move right */
        x = E_RICK_ENT.x + 2;
        game_dir = RIGHT;
    }
    if (e_rick_checkExit(&x)) {  /* prev/next submap */
        return;
    }

    /* still within this map: test environment */
    u_envtest(x, E_RICK_ENT.y, E_RICK_STTST(E_RICK_STCRAWL), &env0, &env1);

    /* save x-position if it is possible to move */
    if (!(env1 & (MAP_EFLG_SOLID | MAP_EFLG_SPAD | MAP_EFLG_WAYUP))) {
        E_RICK_ENT.x = x;
        if (env1 & MAP_EFLG_LETHAL) e_rick_gozombie();
    }

    /* end */
    return;

  /*
   * NO VERTICAL MOVE
   */
vert_not:
    if (offsy < 0) {
      /* not climbing + trying to go _up_ not possible -> hit the roof */
        E_RICK_STSET(E_RICK_STJUMP);  /* fall back to the ground */
        E_RICK_ENT.y &= 0xF8;
        offsy = 0;
        ylow = 0;
        goto horiz;
    }
    /* else: not climbing + trying to go _down_ not possible -> standing */
    /* align to ground */
    E_RICK_ENT.y &= 0xF8;
    E_RICK_ENT.y |= 0x03;
    ylow = 0;

    /* standing on a super pad? */
    if ((env1 & MAP_EFLG_SPAD) && offsy >= 0X0200) {
        offsy = (control_status & CONTROL_UP) ? 0xf800 : 0x00fe - offsy;
        sounds_playPad(); /* back on request, real chiptune effect --
                            * see sounds.c's own header */
        goto horiz;
    }

    offsy = 0x0100;  /* reset*/

    /* standing. firing ? */
    if (scrawl || !(control_status & CONTROL_FIRE)) {
        goto firing_not;
    }

    /*
    * FIRING
    */
    if (control_status & (CONTROL_LEFT | CONTROL_RIGHT)) {  /* stop */
        if (control_status & CONTROL_RIGHT)
        {
            game_dir = RIGHT;
            e_rick_stop_x = E_RICK_ENT.x + 0x17;
        } else {
            game_dir = LEFT;
            e_rick_stop_x = E_RICK_ENT.x;
        }
        e_rick_stop_y = E_RICK_ENT.y + 0x000E;
        E_RICK_STSET(E_RICK_STSTOP);
        return;
    }

    if (control_status == (CONTROL_FIRE | CONTROL_UP)) {  /* bullet */
        E_RICK_STSET(E_RICK_STSHOOT);
        /* not an automatic gun: shoot once only */
        if (trigger) {
            return;
        } else {
            trigger = TRUE;
        }
        /* already a bullet in the air ... that's enough */
        if (E_BULLET_ENT.n) {
            return;
        }
        /* else use a bullet, if any available */
        if (!env_bullets) {
            return;
        }
        if (!env_trainer) {
            env_bullets--;
        }

        /* initialize bullet */
        e_bullet_init(E_RICK_ENT.x, E_RICK_ENT.y);
        return;
    }

    trigger = FALSE; /* not shooting means trigger is released */
    seq = 0; /* reset */

    if (control_status == (CONTROL_FIRE | CONTROL_DOWN)) {  /* bomb */
        /* already a bomb ticking ... that's enough */
        if (E_BOMB_ENT.n) {
            return;
        }
        /* else use a bomb, if any available */
        if (!env_bombs) {
            return;
        }
        if (!env_trainer) {
            env_bombs--;
        }

        /* initialize bomb */
        e_bomb_init(E_RICK_ENT.x, E_RICK_ENT.y);
        return;
    }

    return;

    /*
     * NOT FIRING
     */
firing_not:
    if (control_status & CONTROL_UP) {  /* jump or climb */
        if (env1 & MAP_EFLG_CLIMB) {  /* climb */
            E_RICK_STSET(E_RICK_STCLIMB);
            return;
        }
        offsy = -0x0580;  /* jump */
        ylow = 0;
        sounds_playJump(); /* back on request, real chiptune effect this
                             * time -- see sounds.c's own header */
        goto horiz;
    }
    if (control_status & CONTROL_DOWN) {  /* crawl or climb */
        if ((env1 & MAP_EFLG_VERT) &&  /* can go down */
           !(control_status & (CONTROL_LEFT | CONTROL_RIGHT)) &&  /* + not moving horizontaly */
           (E_RICK_ENT.x & 0x1f) < 0x0a) {  /* + aligned -> climb */
            E_RICK_ENT.x &= 0xf0;
            E_RICK_ENT.x |= 0x04;
            E_RICK_STSET(E_RICK_STCLIMB);
        } else {  /* crawl */
            E_RICK_STSET(E_RICK_STCRAWL);
            goto horiz;
        }

    }
    goto horiz;

    /*
    * CLIMBING
    */
climbing:
    /* should move? */
    if (!(control_status & (CONTROL_UP | CONTROL_DOWN | CONTROL_LEFT | CONTROL_RIGHT))) {
        seq = 0; /* no: reset seq and return */
        return;
    }

    if (control_status & (CONTROL_UP | CONTROL_DOWN)) {
        /* up-down: calc new y and test environment */
        y = E_RICK_ENT.y + ((control_status & CONTROL_UP) ? -0x02 : 0x02);
        u_envtest(E_RICK_ENT.x, y, E_RICK_STTST(E_RICK_STCRAWL), &env0, &env1);
        if (env1 & (MAP_EFLG_SOLID | MAP_EFLG_SPAD | MAP_EFLG_WAYUP) && !(control_status & CONTROL_UP)) {
            /* FIXME what? */
            E_RICK_STRST(E_RICK_STCLIMB);
            return;
        }
        if (!(env1 & (MAP_EFLG_SOLID | MAP_EFLG_SPAD | MAP_EFLG_WAYUP)) || (env1 & MAP_EFLG_WAYUP)) {
            /* ok to move, save */
            E_RICK_ENT.y = y;
            if (env1 & MAP_EFLG_LETHAL) {
                e_rick_gozombie();
                return;
            }
            if (!(env1 & (MAP_EFLG_VERT | MAP_EFLG_CLIMB))) {
                /* reached end of climb zone */
                offsy = (control_status & CONTROL_UP) ? -0x0300 : 0x0100;
                if (control_status & CONTROL_UP) {
                    sounds_playJump();
                }
                E_RICK_STRST(E_RICK_STCLIMB);
                return;
            }
        }
    }
    if (control_status & (CONTROL_LEFT | CONTROL_RIGHT)) {
      /* left-right: calc new x and test environment */
        if (control_status & CONTROL_LEFT) {
            x = E_RICK_ENT.x - 0x02;
        } else {
            x = E_RICK_ENT.x + 0x02;
        }
        if (e_rick_checkExit(&x)) {  /* prev/next submap */
            return;
        }
        u_envtest(x, E_RICK_ENT.y, E_RICK_STTST(E_RICK_STCRAWL), &env0, &env1);
        if (env1 & (MAP_EFLG_SOLID | MAP_EFLG_SPAD)) {
            return;
        }
        E_RICK_ENT.x = x;
        if (env1 & MAP_EFLG_LETHAL) {
            e_rick_gozombie();
            return;
        }

        if (env1 & (MAP_EFLG_VERT | MAP_EFLG_CLIMB)) {
            return;
        }
        E_RICK_STRST(E_RICK_STCLIMB);
        if (control_status & CONTROL_UP) {
            offsy = -0x0300;
        }
    }
}


/*
 * Action function for e_rick
 *
 * ASM 12CA
 */
void e_rick_action(U16 e) {
    (void)e;

    static U16 stopped = FALSE; /* is this the most elegant way? */

    e_rick_action2();

    scrawl = E_RICK_STTST(E_RICK_STCRAWL);

    if E_RICK_STTST(E_RICK_STZOMBIE) {
        return;
    }

    /*
     * set sprite
     */

    if E_RICK_STTST(E_RICK_STSTOP) {
        E_RICK_ENT.sprite = (game_dir ? 0x17 : 0x0B);
        if (!stopped)
        {
            sounds_playStick(); // real chiptune effect -- see sounds.c's own header
            stopped = TRUE;
        }
        return;
    }

    stopped = FALSE;

    if E_RICK_STTST(E_RICK_STSHOOT) {
        E_RICK_ENT.sprite = (game_dir ? 0x16 : 0x0A);
        return;
    }

    if E_RICK_STTST(E_RICK_STCLIMB) {
        E_RICK_ENT.sprite = (((E_RICK_ENT.x ^ E_RICK_ENT.y) & 0x04) ? 0x18 : 0x0c);
        seq = (seq + 1) & 0x03;
        if (seq == 0) sounds_playWalk(); // real chiptune effect -- see sounds.c's own header
        return;
    }

    if E_RICK_STTST(E_RICK_STCRAWL) {
        E_RICK_ENT.sprite = (game_dir ? 0x13 : 0x07);
        if (E_RICK_ENT.x & 0x04) E_RICK_ENT.sprite++;
        seq = (seq + 1) & 0x03;
        if (seq == 0) sounds_playCrawl(); // real chiptune effect -- see sounds.c's own header
        return;
    }

    if E_RICK_STTST(E_RICK_STJUMP) {
        E_RICK_ENT.sprite = (game_dir ? 0x15 : 0x06);
        return;
    }

    seq++;

    if (seq >= 0x14) {
        sounds_playWalk(); // real chiptune effect -- see sounds.c's own header
        seq = 0x04;
    } else {
        if (seq == 0x0C) {
            sounds_playWalk();
        }
    }

    E_RICK_ENT.sprite = (seq >> 2) + 1 + (game_dir ? 0x0c : 0x00);
}


/*
 * Save status
 *
 * ASM part of 0x0BBB
 *
 * Un-wrapped: main.c's main loop now calls this once after map_loadFirst()
 * (establishing the respawn checkpoint) -- see that file's header. Needed
 * no changes itself.
 */
void e_rick_save(void) {
    save_x = E_RICK_ENT.x;
    save_y = E_RICK_ENT.y;
    save_crawl = E_RICK_STTST(E_RICK_STCRAWL);
    /* FIXME
     * save_C0 = E_RICK_ENT.b0C;
     * plus some 6DBC stuff?
     */
}


/*
 * Restore status
 *
 * ASM part of 0x0BDC
 */
void e_rick_restore(void) {
    E_RICK_ENT.x = save_x;
    E_RICK_ENT.y = save_y;
    if (save_crawl) {
        E_RICK_STSET(E_RICK_STCRAWL);
    } else {
        E_RICK_STRST(E_RICK_STCRAWL);
    }
    /* FIXME
     * E_RICK_ENT.b0C = save_C0;
     * plus some 6DBC stuff?
     */
}


/* eof */
