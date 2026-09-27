/*
 * engine/include/env.h -- ported from xrick/include/env.h, unchanged.
 * No TI-specific content: cheat-mode flags, score, current map state.
 */

#ifndef _ENV_H_
#define _ENV_H_

#include "ricksystem.h"

/* cheat modes */
extern U16 env_trainer;
extern U16 env_invicible;
extern U16 env_highlight;

/*
 * depth mode, TRUE if managed (sprites can be hidden by foreground tiles)
 * and FALSE if not (sprites are always before anything else).
 */
extern U16 env_depth;

/* number of lives, bombs and bullets currently available */
extern U16 env_lives;
extern U16 env_bombs;
extern U16 env_bullets;

/* game score */
extern U16 env_score_lo;
extern U16 env_score_hi;
extern void addscore(U16 val);

/* current map and submap */
extern U16 env_map;
extern U16 env_submap;
extern U16 env_changeSubmap; /* change submap request (TRUE, FALSE) */

/* offset of digits for status line */
extern U8 env_digits;

extern void env_paintGame(void);

#endif

/* eof */
