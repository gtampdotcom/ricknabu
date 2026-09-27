/*
 * engine/util.c -- ported from xrick/src/util.c
 *
 * Changes from the TI source:
 *   - Removed SWITCH_IN_BANK14 / nBank / SWITCH_IN_BANK(nOldBank) around
 *     u_trigbox()'s access to ent_entdata[]. That dance exists purely because
 *     the TI cart is 256KB and ent_entdata lives in a paged-out bank most of
 *     the time. NABU has no cartridge banking -- whatever fits in the 64KB
 *     image is just resident, so the bank switch is a no-op here and is
 *     deleted rather than stubbed out, to avoid leaving dead ceremony in
 *     every file that touched banked data (there will be many).
 *   - U16/U8/etc still come from ricksystem.h's typedefs for now; once more
 *     files are ported it may be worth switching wholesale to <stdint.h>
 *     types, but doing that per-file as each one is touched keeps diffs
 *     against the original small and reviewable.
 *
 * NOT YET COMPILABLE on its own: depends on env.h, game.h, ents.h, e_rick.h,
 * maps.h being ported/copied over unchanged first (they look CPU-agnostic on
 * a first read, but haven't been audited file-by-file yet -- do that before
 * assuming it).
 */

#include "config.h"

#include "env.h"

#include "util.h"

#include "game.h"
#include "ents.h"
#include "e_rick.h"
#include "maps.h"

/*
 * Full box test.
 *
 * e: entity to test against.
 * x,y: coordinates to test.
 * ret: TRUE/(x,y) is within e's space, FALSE/not.
 */
U16 u_fboxtest(U16 e, U16 x, U16 y)
{
  if (ent_ents[e].x >= x ||
      ent_ents[e].x + ent_ents[e].w < x ||
      ent_ents[e].y >= y ||
      ent_ents[e].y + ent_ents[e].h < y)
    return FALSE;
  else
    return TRUE;
}


/*
 * Box test (then whole e2 is checked agains the center of e1).
 *
 * e1: entity to test against (corresponds to DI in asm code).
 * e2: entity to test (corresponds to SI in asm code).
 * ret: TRUE/intersect, FALSE/not.
 */
U16 u_boxtest(U16 e1, U16 e2)
{
  /* rick is special (may be crawling) */
  if (e1 == E_RICK_NO)
    return e_rick_boxtest(e2);

  /*
   * entity 1: x+0x05 to x+0x011, y to y+0x14
   * entity 2: x to x+ .w, y to y+ .h
   */
  if (ent_ents[e1].x + 0x11 < ent_ents[e2].x ||
      ent_ents[e1].x + 0x05 > ent_ents[e2].x + ent_ents[e2].w ||
      ent_ents[e1].y + 0x14 < ent_ents[e2].y ||
      ent_ents[e1].y > ent_ents[e2].y + ent_ents[e2].h - 1)
    return FALSE;
  else
    return TRUE;
}


/*
 * Compute the environment flag.
 *
 * x, y: coordinates where to compute the environment flag
 * crawl: is rick crawling?
 * rc0: anything CHANGED to the environment flag for crawling (6DBA)
 * rc1: anything CHANGED to the environment flag (6DAD)
 */
void u_envtest(U16 x, U16 y, U16 crawl, U16* rc0, U16* rc1)
{
  U16 i;
  U16 xx;
  U16 atDataEdge;

  /* prepare for ent #0 test */
  ent_ents[ENT_ENTSNUM].x = x;
  ent_ents[ENT_ENTSNUM].y = y;

  i = 1;
  if (!crawl) i++;
  if (y & 0x0004) i++;

  x += 4;
  xx = (U16)x; /* FIXME? */

  x = x >> 3;  /* from pixels to tiles */
  y = y >> 3;  /* from pixels to tiles */

  /* BUG FIXED, two layers -- see this function's git history/prior
   * comments for the two wrong attempts before this one (HBBOT, then
   * SCRBOT); a specific reproducible stuck-in-roof spot (a narrow
   * multi-row-tall vertical shaft, verified by reconstructing the real
   * submap-0 tile data and cross-checking it against the reported stuck
   * location) is what finally pinned this down:
   *
   * Layer 1 (hard safety, unconditional): map_map[]/map_eflg[] have valid
   * rows 0..43, and this function reads up to 3 rows past whatever row it
   * starts on (the crawl branch's own +1, then the do/while running `i`
   * (1-3) more times) -- so any y whose row exceeds MAP_ROW_HBBOT (39,
   * deliberately 3-4 rows short of 43 to leave exactly this margin) risks
   * reading past the end of those arrays into whatever memory follows.
   * That's real memory corruption, not a gameplay quirk, so this clamp
   * always applies regardless of the layer-2 logic below.
   *
   * Layer 2 (make the edge act like ground, only where layer 1 actually
   * had to fabricate data): the SCRBOT (31, "last visible row") attempt
   * was still wrong, just more subtly than the original HBBOT one -- this
   * function's own lookahead (the `i` rows above, plus the do/while's
   * trailing extra check) routinely reads a few rows *below* Rick's own
   * feet even while he's still well within the visible screen (e.g. feet
   * at row 28-31 reading down to row 32-35 to see what's coming). Rows
   * 32-39 (MAP_ROW_HBTOP..HBBOT, the "hidden bottom" band) are real,
   * actively-maintained data -- engine/scroller.c's row-shift keeps them
   * valid every single scroll step, and map_expand() refreshes them
   * outright at the end of each scroll cycle -- not fabricated filler.
   * Forcing solid ground there (as SCRBOT did) fires during completely
   * ordinary floor/ceiling checks near the bottom of the screen, planting
   * phantom floors inside real, open passages -- exactly what made a
   * narrow vertical shaft (open only a few columns wide, several rows
   * tall) act like a solid ceiling partway down.
   *
   * The real fix: only treat the read as "off the edge of real data" when
   * layer 1 above actually had to clamp it, i.e. row > MAP_ROW_HBBOT.
   * That's the one case where the y being tested is fabricated (clamped)
   * rather than real map data, so it's the only case that should get the
   * synthetic "solid, non-lethal" floor. Everything at or below HBBOT
   * uses its own real flags, same as the original engine, which never
   * clamped this at all -- see xrick/src/util.c's own u_envtest(), which
   * has no equivalent of either layer, and gets away with it because the
   * original never lets y run past what its scroll mechanism keeps valid.
   * This still keeps that guarantee for the genuine "outran the loaded
   * submap-0 scroll range" case (maps.h's "SCROLL RANGE" comment) --
   * that's the one scenario that can still clamp all the way to HBBOT. */
  atDataEdge = (y > MAP_ROW_HBBOT);
  if (y > MAP_ROW_HBBOT) {
    y = MAP_ROW_HBBOT;
  }

  *rc0 = *rc1 = 0;

  if (xx & 0x07) {  /* tiles columns alignment */
    if (crawl) {
      *rc0 |= (map_eflg[map_map[y][x]] &
	   (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP));
      *rc0 |= (map_eflg[map_map[y][x + 1]] &
	   (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP));
      *rc0 |= (map_eflg[map_map[y][x + 2]] &
	   (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP));
      y++;
    }
    do {
      *rc1 |= (map_eflg[map_map[y][x]] &
	       (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_FGND|
		MAP_EFLG_LETHAL|MAP_EFLG_01));
      *rc1 |= (map_eflg[map_map[y][x + 1]] &
	       (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_FGND|
		MAP_EFLG_LETHAL|MAP_EFLG_CLIMB|MAP_EFLG_01));
      *rc1 |= (map_eflg[map_map[y][x + 2]] &
	       (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_FGND|
		MAP_EFLG_LETHAL|MAP_EFLG_01));
      y++;
    } while (--i > 0);

    *rc1 |= (map_eflg[map_map[y][x]] &
	     (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP|MAP_EFLG_FGND|
	      MAP_EFLG_LETHAL|MAP_EFLG_01));
    *rc1 |= (map_eflg[map_map[y][x + 1]]);
    *rc1 |= (map_eflg[map_map[y][x + 2]] &
	     (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP|MAP_EFLG_FGND|
	      MAP_EFLG_LETHAL|MAP_EFLG_01));
  }
  else {
    if (crawl) {
      *rc0 |= (map_eflg[map_map[y][x]] &
	   (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP));
      *rc0 |= (map_eflg[map_map[y][x + 1]] &
	   (MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP));
      y++;
    }
    do {
      *rc1 |= (map_eflg[map_map[y][x]] &
	       (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_FGND|
		MAP_EFLG_LETHAL|MAP_EFLG_CLIMB|MAP_EFLG_01));
      *rc1 |= (map_eflg[map_map[y][x + 1]] &
	       (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_FGND|
		MAP_EFLG_LETHAL|MAP_EFLG_CLIMB|MAP_EFLG_01));
      y++;
    } while (--i > 0);

    *rc1 |= (map_eflg[map_map[y][x]]);
    *rc1 |= (map_eflg[map_map[y][x + 1]]);
  }

  /* Invisible floor at the edge of what's visible/scrollable -- see this
   * function's header ("Layer 2"). Overrides whatever real tile flags
   * this row actually has at this x (irrelevant here: this call passed
   * MAP_ROW_SCRBOT only because the caller's y was already past it, not
   * because it actually meant to test that row). Also clear LETHAL -- an
   * open tile that's lethal when fallen INTO shouldn't be lethal when
   * used as a last-resort floor to stand ON. */
  if (atDataEdge) {
    *rc1 |= MAP_EFLG_SOLID;
    *rc1 &= ~MAP_EFLG_LETHAL;
  }

  /*
   * If not lethal yet, and there's an entity on slot zero, and (x,y)
   * boxtests this entity, then raise SOLID flag. This is how we make
   * sure that no entity can move over the entity that is on slot zero.
   *
   * Beware! When env_invicible is set, this means that a block can
   * move over rick without killing him -- but then rick is trapped
   * because the block is solid.
   */
  if (!(*rc1 & MAP_EFLG_LETHAL)
      && ent_ents[0].n
      && u_boxtest(ENT_ENTSNUM, 0)) {
    *rc1 |= MAP_EFLG_SOLID;
  }

  /* When invicible, the environment can not be lethal. */
  if (env_invicible) *rc1 &= ~MAP_EFLG_LETHAL;
}


/*
 * Check if x,y is within e trigger box.
 *
 * return: FALSE if not in box, TRUE if in box.
 *
 * NABU: the TI version wraps ent_entdata[] access here in
 * SWITCH_IN_BANK14/SWITCH_IN_BANK(nOldBank) because that table lives in a
 * paged cartridge bank. No cartridge banking on NABU, so that's gone -- see
 * file header.
 */
U16 u_trigbox(U16 e, U16 x, U16 y)
{
  U16 xmax, ymax;

  xmax = ent_ents[e].trig_x + (ent_entdata[ent_ents[e].n & 0x7F].trig_w << 3);
  ymax = ent_ents[e].trig_y + (ent_entdata[ent_ents[e].n & 0x7F].trig_h << 3);

  if (xmax > 0xFF) xmax = 0xFF;

  if (x <= ent_ents[e].trig_x || x > xmax ||
      y <= ent_ents[e].trig_y || y > ymax)
    return FALSE;
  else
    return TRUE;
}


/* eof */
