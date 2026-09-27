/*
 * engine/scroller.c -- PARTIAL port of xrick/src/scroller.c
 *
 * Ported: scroll_up()/scroll_down(), using the reference's own `#else`
 * (non-CLASSIC99) branch for the map_map[] row-shift -- that branch is
 * already a portable memmove()-shaped loop, not TMS9900 assembly (the
 * assembly path, myasmscrup/myasmscrdn/myasmvdpcp loaded via scroll_init(),
 * is what's NOT ported -- see scroller.h's header for why: no Z80
 * equivalent, and this port doesn't need one since the C shape works as-is).
 * scroll_init() itself is therefore a no-op here, kept only so main.c's
 * call site (if any gets added later) doesn't need touching.
 *
 * Real changes from the TI source:
 *   - `env_paintGame()` calls dropped -- main.c's loop repaints the
 *     status bar itself.
 *   - `IFDEBUG_SCROLLER(sys_printf(...))` calls dropped -- same reasoning
 *     as maps.c/ents.c: config.h's DEBUG isn't defined in this port, and
 *     sys_printf() has no real body yet, so these would only ever be dead
 *     code here.
 *   - No cartridge banking to drop (this file never had any).
 *   - The reference saves the *current* game_period into a local static
 *     before overwriting it with SCROLL_PERIOD, then restores that exact
 *     saved value afterward. Here it restores game_speedPeriod (main.c)
 *     instead -- the gameplay speed the 'F' key picked, which is exactly
 *     what game_period holds whenever a scroll starts.
 *
 * NEW, not in the reference: both functions refuse to shift map_frow
 * outside the currently-loaded data range. See maps.h's "SCROLL RANGE"
 * comment (MAP_NBR_BNUMS/BLOCKS) for exactly where 0..127 comes from and
 * why -- the original never needed this guard because xrick always has
 * every submap's full map_bnums[]/map_blocks[] compiled in, all sharing
 * one contiguous table; this port only has submaps 0+1's own slice of
 * that table loaded (currently enough for Rick to fall all the way
 * through submap 0's own open-bottomed lowest rows into submap 1's
 * terrain -- see maps.h), and there's nothing loaded past that. Silently
 * refusing to scroll further at the edge is the same "known gap, not a
 * crash" spirit as e_rick_atExit's horizontal edge case in main.c's loop.
 *
 * BUG FIXED (first version of this port): the guard originally refused to
 * *start* a fresh 8-step cycle unless the whole cycle (map_frow +/- 8)
 * fit inside the loaded range, e.g. scroll_up() 8 rows short of the edge
 * refused outright even though a shorter move was perfectly safe. That
 * made the very first scroll eat almost the entire safe range in one
 * shot, then every later scroll attempt refuse outright -- exactly
 * "works once, never again". Fixed by checking per-step instead: each
 * call takes one more step only if map_frow hasn't already reached the
 * edge, and a cycle that hits the edge before its natural 8th step just
 * runs the same "last step" wrap-up (ent_actvis/map_expand/paint) early,
 * via the `n = 8` forcing below, rather than needing every cycle to be a
 * full 8 steps. */

#include "config.h"

#include "game.h"
#include "env.h"

#include "scroller.h"
#include "debug.h"
#include "maps.h"
#include "ents.h"

/* SIZE PASS: wrapped out (on request, "find more ram without removing any
 * features" -- confirmed zero call sites anywhere in the linked tree).
 * Was a no-op body kept only in case
 * main.c ever added a call site -- see this file's header for why the
 * reference's own TMS9900-assembly version doesn't apply here. Real
 * declaration still in scroller.h; un-wrap if a real caller shows up. */
#if 0
void scroll_init(void) {
}
#endif

/*
 * Scroll up (map content moves up on screen, following Rick down into
 * territory below -- i.e. this is the one "player falls" triggers).
 */
U16 scroll_up(void)
{
  U16 i;
  static U16 n = 0;

  /* last call: restore */
  if (n == 8) {
    n = 0;
    game_period = game_speedPeriod;
    return SCROLL_DONE;
  }

  /* first call: prepare. Refuse outright only if there's no room left to
   * take even one more step -- see this file's header on why this checks
   * per-step, not per-cycle. */
  if (n == 0) {
    if (map_frow >= 983) {
      return SCROLL_DONE;
    }
    game_period = SCROLL_PERIOD;
  }

  /* translate map -- portable row-shift (reference's non-CLASSIC99,
   * non-assembly branch), unchanged in shape. */
  for (i = MAP_ROW_SCRTOP; i < MAP_ROW_HBBOT; i++) {
    U16 j;
    for (j = 0; j < 0x20; j++) {
      map_map[i][j] = map_map[i+1][j];
    }
  }

  /* translate entities */
  for (i = 0; ent_ents[i].n != 0xFF; i++) {
    if (ent_ents[i].n) {
      ent_ents[i].ysave -= 8;
      ent_ents[i].trig_y -= 8;
      ent_ents[i].y -= 8;
      if (ent_ents[i].y < 0) {  /* map coord. from 0x0000 to 0x0140 */
        delete_ent(i);
      }
    }
  }

  /* display */
  maps_paint();
  ents_paintAll();
  map_frow++;

  /* loop -- end this cycle (same wrap-up as the reference's natural 8th
   * step) either on schedule or as soon as map_frow hits the edge,
   * whichever comes first. Forcing n to 8 (instead of just returning
   * SCROLL_DONE straight away) keeps the next call's "last call: restore"
   * branch as the one place that resets n/game_period, same as every
   * other exit from this function.
   *
   * BUG FIXED: map_expand() must NOT run on an edge-cut-short cycle.
   * map_expand()'s pbnum formula only depends on map_frow rounded down to
   * a multiple of 4 (`(2*map_frow)&0xfff8`) -- across a full natural
   * 8-step cycle that's harmless, since 8 is itself a multiple of 4, so
   * the fresh recompute always lands exactly where the 8 incremental
   * per-row shifts above already put the data. But this port's own
   * "safe range 0..983" cap (maps.h's SCROLL RANGE comment) can end a
   * cycle after as few as 1 step, at a map_frow that ISN'T a multiple of
   * 4 -- e.g. a cycle starting at map_frow=980 (rounds to 980) that only
   * gets to take 3 steps before hitting the map_frow>=983 edge still
   * rounds to 980 (983&~3-ish quantizing the same way), so map_expand()
   * would re-derive the SAME starting window the cycle began from and
   * blit it over rows 0-38 -- discarding the 3 rows of real progress the
   * incremental shift above just made and leaving map_map briefly
   * showing terrain 3 rows "too high" for where Rick's own (correctly
   * shifted) position now is. That's a real, reproducible terrain/Rick
   * misalignment right at the loaded range's lower edge -- not a data or
   * collision bug, a pure scroll-bookkeeping one -- and it doesn't need
   * util.c's u_envtest() safety clamp to fix, since the clamp only
   * guards reads past row 39; this misalignment happens at whatever row
   * Rick actually is, which can be well within the normal visible range.
   *
   * Only the natural n==8 completion is safe to run map_expand() on.
   * Ending early via the edge just stops here: rows 0-38 are already
   * exactly correct (the incremental shifts above), and rows 39-43 (the
   * hidden-bottom band map_expand() would otherwise refresh) simply keep
   * whatever the last natural expand already put there -- since map_frow
   * capping at 983 means those rows are at/past map 1's own actual
   * combined bottom (row 1023 of 1024, the whole first level's own
   * vertical extent) anyway, there's no genuinely new data below them to
   * reveal. */
  n++;
  if (map_frow >= 983) {
    n = 8;
  } else if (n == 8) {
    /* activate visible entities */
    ent_actvis(map_frow + MAP_ROW_HBTOP, map_frow + MAP_ROW_HBBOT);

    /* prepare map */
    map_expand();

    /* display */
    maps_paint();
    ents_paintAll();
  }

  return SCROLL_RUNNING;
}

/*
 * Scroll down (map content moves down on screen, following Rick up --
 * climbing/jumping toward the top of the visible area).
 */
U16 scroll_down(void)
{
  U16 i;
  static U16 n = 0;

  /* last call: restore */
  if (n == 8) {
    n = 0;
    game_period = game_speedPeriod;
    return SCROLL_DONE;
  }

  /* first call: prepare. Refuse outright only if there's no room left to
   * take even one more step (map_frow is U16/unsigned -- checking `== 0`
   * before decrementing, not after, avoids underflowing it) -- see this
   * file's header and scroll_up()'s matching comment on why this checks
   * per-step, not per-cycle. */
  if (n == 0) {
    if (map_frow == 0) {
      return SCROLL_DONE;
    }
    game_period = SCROLL_PERIOD;
  }

  /* translate map -- portable row-shift (reference's non-CLASSIC99,
   * non-assembly branch), unchanged in shape. */
  for (i = MAP_ROW_SCRBOT; i > MAP_ROW_HTTOP; i--) {
    U16 j;
    for (j = 0; j < 0x20; j++) {
      map_map[i][j] = map_map[i-1][j];
    }
  }

  /* translate entities */
  for (i = 0; ent_ents[i].n != 0xFF; i++) {
    if (ent_ents[i].n) {
      ent_ents[i].ysave += 8;
      ent_ents[i].trig_y += 8;
      ent_ents[i].y += 8;
      if (ent_ents[i].y > 0x140) {  /* map coord. from 0x0000 to 0x0140 */
        delete_ent(i);
      }
    }
  }

  /* display */
  maps_paint();
  ents_paintAll();
  map_frow--;

  /* loop -- see scroll_up()'s matching comment (both on why this ends the
   * cycle as soon as map_frow hits the edge, not only on schedule, AND on
   * why map_expand() must be skipped -- not just have its row-range
   * shifted -- on an edge-cut-short cycle: it would re-round map_frow=0
   * down to the same bucket the cycle started from and stomp the rows
   * the shift loop above already correctly moved). */
  n++;
  if (map_frow == 0) {
    n = 8;
  } else if (n == 8) {
    /* activate visible entities */
    ent_actvis(map_frow + MAP_ROW_HTTOP, map_frow + MAP_ROW_HTBOT);

    /* prepare map */
    map_expand();

    /* display */
    maps_paint();
    ents_paintAll();
  }

  return SCROLL_RUNNING;
}

/* eof */
