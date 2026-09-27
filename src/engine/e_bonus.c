/*
 * engine/e_bonus.c -- ported from xrick/src/e_bonus.c.
 * BONUS_SND is a direct sounds_playBonus() call now (no `#ifdef
 * ENABLE_SOUND` -- that macro's sounds_play() dispatch doesn't exist,
 * see engine/sounds.c's own header), a real chiptune effect (assets/
 * TREASURE.DAT) sharing sounds.c's one ticked AY slot with JUMP_SND/
 * BULLET_SND/EXPLODE_SND. Everything else verbatim.
 */

#include "ricksystem.h"
#include "config.h"
#include "env.h"

#include "game.h"
#include "ents.h"
#include "e_bonus.h"
#include "e_rick.h"
#include "maps.h"
#include "sounds.h"


/*
 * Entity action
 *
 * ASM 242C
 */
void
e_bonus_action(U16 e) {
#define seq c1

    if (ent_ents[e].seq == 0) {
        if (e_rick_boxtest(e)) {
            addscore(500);
            sounds_playBonus(); // real chiptune effect -- see sounds.c's own header
            map_marks_ent[ent_ents[e].mark] |= MAP_MARK_NACT;
            ent_ents[e].seq = 1;
            ent_ents[e].sprite = 0xad;
            ent_ents[e].y -= 0x08;
        }
    } else if (ent_ents[e].seq > 0 && ent_ents[e].seq < 10) {
        ent_ents[e].seq++;
        ent_ents[e].y -= 2;
    } else {
        delete_ent(e);
    }

#undef seq
}


/* eof */
