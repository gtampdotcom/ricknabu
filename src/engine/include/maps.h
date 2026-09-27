/*
 * engine/include/maps.h -- ported from xrick/include/maps.h, unchanged.
 * Map/submap/block/mark data structures and constants; no TI-specific
 * content (these describe the game's own tile/map format, not hardware).
 */

#ifndef _MAPS_H
#define _MAPS_H

#include "ricksystem.h"

/*
 * methods
 */
extern void maps_paint(void);
extern void maps_paintRect(U16, U16, U16, U16);
extern void maps_alignRect(U16 *, U16 *, U16 *, U16 *);
extern U16 maps_clip(U16, U16, U16, U16);

/* map dimensions */
#define MAPS_WIDTH_PX 0x0100
#define MAPS_TOPHEIGHT_PX 0x40
#define MAPS_VISHEIGHT_PX 0xc0
#define MAPS_BOTHEIGHT_PX 0x40
#define MAPS_TOPHEIGHT_TL 0x08
#define MAPS_VISHEIGHT_TL 0x18  /* 0xc0 px = 0x18 rows; was 0x20, which made
                                 * map_init activate marks up to 8 rows below
                                 * the 0x28-row map_map window -- entities
                                 * spawned at y > 0x140 and ran u_envtest past
                                 * the end of map_map (garbage collision) */
#define MAPS_BOTHEIGHT_TL 0x08

/* position of the fb origin, expressed in map/px */
/* MAPS_FB_X = (MAPS_WIDTH_PX - FB_WIDTH) / 2 */
/* MAPS_FB_Y = MAPS_TOPHEIGHT_PX + (MAPS_HEIGHT_PX - FB_HEIGHT) / 2 */
#define MAPS_FB_X (-0x20)
#define MAPS_FB_Y (0x40)

/* Widened from 3 to 4 (SAMERICA, EGYPT, CASTLE, MBASE) -- see maps.c's
 * map_maps[] and map_loadMap(), and main.c's e_rick_atExit handling of
 * map_chain()'s FALSE return. EPILOGUE has no map data (none in the
 * reference either) -- it's only an intro screen, then game over. */
#define MAP_NBR_MAPS 0x04

/*
 * STREAMED PER MAP (widened for EGYPT): SUBMAPS/CONNECT/BNUMS/MARKS are
 * no longer one cumulative table spanning every loaded map's submaps --
 * maps.c's map_loadMap() reloads them fresh from that map's own numbered
 * asset file (MAPSUB2.DAT etc.) at every map transition, with every
 * submap_t.bnum/connect/mark and connect_t.submap value REBASED to be
 * relative to 0 for whichever map is currently loaded (tools this
 * session's own extraction script did the rebasing -- see
 * assets/README.md). That keeps RAM flat regardless of how many
 * maps eventually exist: each constant below is sized to the LARGEST
 * single map ported so far (EGYPT needs more than SAMERICA did on every
 * one of these), not their sum. Widen to whichever new map needs more as
 * CASTLE/MBASE get ported.
 *
 *   MAP_NBR_SUBMAPS: 18 -- CASTLE's own submap count, still the largest
 *     (SAMERICA needs 9, EGYPT needs 11, MBASE needs 9 -- MBASE fits
 *     within CASTLE's already-widened size, no change needed here).
 *   MAP_NBR_CONNECT: 66 (0x42) -- CASTLE's own connect-row count, exactly
 *     2x EGYPT's own 33 (SAMERICA needs 27, MBASE needs 27 -- fits as-is).
 *   MAP_NBR_BNUMS: 2602 -- CASTLE's own combined bnum span, still the
 *     largest (SAMERICA needs 2048, EGYPT needs 2144, MBASE needs 1358 --
 *     fits as-is).
 *   MAP_NBR_MARKS: 174 -- CASTLE's own mark count, 156 real + 18
 *     sentinels, still the largest (SAMERICA needs 101, EGYPT needs 141,
 *     MBASE needs 107 -- fits as-is).
 *
 * MAP_NBR_BLOCKS is ALSO streamed per map now (BUG FIXED, real hardware/
 * address-space CRASH: a first attempt kept this at the full real 256 --
 * "shared, not cleanly partitionable" seemed true since raw block ids
 * aren't map-exclusive, but EGYPT's own real bnums reference block ids up
 * to 255, and simply widening every one of these tables at once pushed
 * this build's total memory footprint 3505 bytes past the hard 64KB Z80
 * ceiling -- see git history/session notes; the .nabu file "succeeded"
 * silently past that point instead of erroring, with map_blocks itself
 * literally wrapping past $FFFF into low memory). The real fix: block
 * IDs don't need to be globally shared at all -- each map's own bnums
 * only ever reference a small DISTINCT SUBSET of the real 256-entry
 * table (58 for SAMERICA, 64 for EGYPT, 60 for CASTLE, 73 for MBASE), so
 * map_loadMap() dedupes each map's own bnums down to just its own
 * distinct block ids, rebases them to a fresh local 0-based numbering,
 * and streams in ONLY that map's own MAPBLK2.DAT-style slice (tools/'s
 * extraction script this session did the dedup+rebase, verified byte-for-
 * byte equivalent to the original global table's actual block PATTERNS --
 * see assets/README.md). Sized to the larger of the ported maps' own
 * distinct-block counts, same "streamed, max-not-sum" convention as
 * SUBMAPS/CONNECT/BNUMS/MARKS above -- MBASE's own 73 distinct blocks are
 * a new high (widened from EGYPT's 64). Block ids
 * are safe to rebase this way because map_expand() only ever uses one as
 * a plain lookup index (maps.c) -- nothing branches on a block id's
 * numeric VALUE the way ents.c's entity-type dispatch does with mark.ent
 * (see ents.h's own header for why ENT_NBR_ENTDATA/SPRSEQ/MVSTEP
 * deliberately do NOT get this same per-map-rebase treatment -- that one
 * broke real gameplay the first time it was tried).
 */
#define MAP_NBR_SUBMAPS 18
#define MAP_NBR_CONNECT 0x42
#define MAP_NBR_BNUMS 2602
#define MAP_NBR_BLOCKS 73
#define MAP_NBR_MARKS 174
#define MAP_NBR_EFLGC 0x0020

/*
 * map row definitions, for three zones : hidden top, screen, hidden bottom
 * the three zones compose map_map, which contains the definition of the
 * current portion of the submap.
 */
#define MAP_ROW_HTTOP 0x00
#define MAP_ROW_HTBOT 0x07
#define MAP_ROW_SCRTOP 0x08
#define MAP_ROW_SCRBOT 0x1F
#define MAP_ROW_HBTOP 0x20
#define MAP_ROW_HBBOT 0x27

extern U8 map_map[0x2c][0x20];

/*
 * main maps
 */
typedef struct {
  U16 x, y;		/* initial position for rick */
  U16 row;		/* initial map_map top row within the submap */
  U16 submap;	/* initial submap */
  U16 tune;	    /* map tune */
} map_t;

/*
 * sub maps
 */
typedef struct {
  U16 page;            /* tiles page */
  U16 bnum;            /* first block number */
  U16 connect;         /* first connection */
  U16 mark;            /* first entity mark */
} submap_t;

extern submap_t map_submaps[MAP_NBR_SUBMAPS]; /* fileload: BSS, see dat_maps1.c */

/*
 * connections
 */
typedef struct {
  U16 dir;
  U16 rowout;
  U16 submap;
  U16 rowin;
} connect_t;

extern connect_t map_connect[MAP_NBR_CONNECT]; /* fileload: BSS, see dat_maps1.c */

/*
 * blocks - one block is 4 by 4 tiles.
 */
typedef U8 block_t[0x10];

extern block_t map_blocks[MAP_NBR_BLOCKS]; /* fileload: BSS, see dat_maps4.c */

/*
 * flags for map_marks[].ent ("yes" when set)
 *
 * MAP_MARK_NACT: this mark is not active anymore.
 */
#define MAP_MARK_NACT (0x80)

/*
 * mark structure
 */
typedef struct {
  U16 row;
  U16 ent;     // the only non-const value, gets copied out
  U16 flags;
  U16 xy;  /* bits XXXX XYYY (from b03) with X->x, Y->y */
  U16 lt;  /* bits XXXX XNNN (from b04) with X->trig_x, NNN->lat & trig_y */
} mark_t;

extern mark_t map_marks[MAP_NBR_MARKS]; /* fileload: BSS, see dat_maps3.c */
extern U16 map_marks_ent[MAP_NBR_MARKS];

/*
 * block numbers, i.e. array of rows of 8 blocks
 */
extern U8 map_bnums[MAP_NBR_BNUMS]; /* fileload: BSS, see dat_maps2.c */

/*
 * flags for map_eflg[map_map[row][col]]  ("yes" when set)
 *
 * MAP_EFLG_VERT: vertical move only (usually on top of _CLIMB).
 * MAP_EFLG_SOLID: solid block, can't go through.
 * MAP_EFLG_SPAD: super pad. can't go through, but sends entities to the sky.
 * MAP_EFLG_WAYUP: solid block, can't go through except when going up.
 * MAP_EFLG_FGND: foreground (hides entities).
 * MAP_EFLG_LETHAL: lethal (kill entities).
 * MAP_EFLG_CLIMB: entities can climb here.
 * MAP_EFLG_01:
 */
#define MAP_EFLG_VERT (0x80)
#define MAP_EFLG_SOLID (0x40)
#define MAP_EFLG_SPAD (0x20)
#define MAP_EFLG_WAYUP (0x10)
#define MAP_EFLG_FGND (0x08)
#define MAP_EFLG_LETHAL (0x04)
#define MAP_EFLG_CLIMB (0x02)
#define MAP_EFLG_01 (0x01)

extern U8 map_eflg_c[MAP_NBR_EFLGC];  /* compressed; fileload: BSS, see dat_maps3.c */
extern U8 map_eflg[0x100];  /* current */

/*
 * map_map top row within the submap
 */
extern U16 map_frow;

/*
 * tiles offset
 */
extern U16 map_tilesBank;

extern void map_expand(void);
extern void map_init(void);
extern U16 map_chain(void);
extern void map_resetMarks(void);
extern void map_loadMap(U16 mapIndex); /* not in the TI original -- see maps.c */
extern void map_loadTileSet(void);      /* current map's tiles for the active VDP mode */


#endif

/* eof */
