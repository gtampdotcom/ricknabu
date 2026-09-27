/*
 * engine/e_sbonus.c -- ported from xrick/src/e_sbonus.c.
 * SBONUS_SND/SBONUS2_SND calls are back as direct sounds_playSbonus()/
 * sounds_playSbonus2() calls, real chiptune effects (assets/
 * SBONUS.DAT/SBONUS2.DAT) sharing engine/sounds.c's one ticked AY slot
 * with JUMP_SND/BULLET_SND/etc -- see that file's own header; everything
 * else verbatim, including the "clear sprite cache" delete_ent() dance
 * (the comments in the original explain why -- these entities are
 * invisible triggers, and we still want sprites_paint2()'s per-slot
 * cache cleared when one gets deleted for real).
 */

#include "ricksystem.h"
#include "config.h"
#include "env.h"

#include "game.h"
#include "ents.h"
#include "e_sbonus.h"
#include "util.h"
#include "maps.h"
#include "e_rick.h"
#include "sounds.h"


/*
 * public vars
 */
U16 e_sbonus_counting = FALSE;
U16 e_sbonus_counter = 0;
U16 e_sbonus_bonus = 0;


/*
 * Shared by start/stop below: both refresh the sprite cache the same way
 * every call (delete_ent() clears sprites_paint2()'s per-slot cache, then
 * .n is restored and .sprite forced invisible) before doing their own
 * real/no-op decision. Factored out (not in the original -- upstream just
 * repeats these 4 lines in both functions) to save the duplicate code.
 */
static void refresh_invisible(U16 e) {
    U16 n = ent_ents[e].n;
    delete_ent(e);
    ent_ents[e].n = n;
    ent_ents[e].sprite = 0; /* invisible */
}


/*
 * Entity action / start counting
 *
 * ASM 2182
 */
void e_sbonus_start(U16 e) {
    refresh_invisible(e);

    if (u_trigbox(e, ENT_XRICK.x + 0x0C, ENT_XRICK.y + 0x0A)) {
		/* rick is within trigger box */
        delete_ent(e);
		e_sbonus_counting = TRUE;  /* 6DD5 */
		e_sbonus_counter = 0x1e;  /* 6DDB */
		e_sbonus_bonus = 2000;    /* 291A-291D */
		sounds_playSbonus(); /* back on request, real chiptune effect --
                              * see sounds.c's own header */
	}
}


/*
 * Entity action / stop counting
 *
 * ASM 2143
 */
void e_sbonus_stop(U16 e) {
    refresh_invisible(e);

	if (!e_sbonus_counting) {
		return;
    }

	if (u_trigbox(e, ENT_XRICK.x + 0x0C, ENT_XRICK.y + 0x0A)) {
		/* rick is within trigger box */
		e_sbonus_counting = FALSE;  /* stop counting */
        delete_ent(e);
		addscore(e_sbonus_bonus);  /* add bonus to score */
		sounds_playSbonus2(); /* back on request, real chiptune effect --
                               * see sounds.c's own header */
		/* make sure the entity won't be activated again */
		map_marks_ent[ent_ents[e].mark] |= MAP_MARK_NACT;
	} else {
		/* keep counting */
		if (--e_sbonus_counter == 0) {
			e_sbonus_counter = 0x1e;
			if (e_sbonus_bonus) e_sbonus_bonus--;
		}
	}
}

/* eof */
