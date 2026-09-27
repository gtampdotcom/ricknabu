/*
 * engine/maps.c -- PARTIAL port of xrick/src/maps.c
 *
 * Ported: map_expand(), map_eflg_expand() (static, unchanged from the
 * reference), map_init(), map_resetMarks(), maps_paint(), maps_clip(),
 * map_chain(), maps_paintRect(), and maps_alignRect() -- enough to expand
 * a submap's blocks into map_map[] and blit it to the screen (either
 * wholesale via maps_paint() or a redrawn strip via maps_paintRect() as
 * Rick scrolls the map), plus (via engine/ents.c's ents_paintAll() ->
 * engine/sprites.c's sprites_paint2()) show Rick and this submap's
 * entities at their current positions each frame.
 *
 * maps_clip() is adapted, not verbatim, from the TI source: that version
 * takes x/y/width/height as pointers and can shrink the rect to the
 * visible portion (guarded by `#ifdef ALLOW_CROPS`, which the TI build
 * never defines either) since maps_paintRect() reuses it to redraw a
 * clipped sub-rectangle of tiles. Nothing here needs that cropping --
 * sprites_paint2() only wants a yes/no "is this on screen at all" answer
 * -- so this port takes plain U16 values and drops the ALLOW_CROPS-gated
 * branches entirely rather than carrying dead pointer ceremony. Signature
 * still matches maps.h's existing extern. maps_paintRect() below calls
 * this same U16-only version -- it never needed the pointer/crop path
 * either, since the original's ALLOW_CROPS branches are dead code in the
 * TI build too.
 *
 * map_chain() is otherwise verbatim (see "Real changes" below for what
 * moved). It still doesn't handle the "no next submap -- request next
 * map" (FALSE) return itself; main.c's game loop currently just leaves
 * Rick sitting at the map's edge when that happens (map advancement
 * -- xrick's NEXT_MAP game_run() state -- isn't ported), same
 * "known gap, not a crash" spirit as everything else this file defers.
 *
 * Vertical scrolling (xrick's SCROLL_UP/SCROLL_DOWN game_run() states) is
 * ported now too -- see engine/scroller.c and main.c's main loop (the
 * CTRL_SCROLL-equivalent check) -- bounded to submap 0's own currently-
 * loaded data range (maps.h's MAP_NBR_BNUMS/BLOCKS comment). This file's
 * own map_expand()/maps_paint() needed no changes for that; they already
 * just read whatever map_frow currently is.
 *
 * NOT ported yet, deliberately deferred (nothing currently calls it, so
 * leaving it unimplemented doesn't break the link the way it would for
 * something actually invoked):
 *
 * Real changes from the TI source:
 *   - Dropped `unsigned int nOldBank = nBank;` / SWITCH_IN_BANK* /
 *     SWITCH_IN_BANK(nOldBank) throughout -- no cartridge banking on
 *     NABU, everything's flat-addressed. Same convention as every other
 *     file in this port (see e.g. ents.c's own header note).
 *   - VDP_INT_DISABLE / VDP_INT_ENABLE (TI cross-compiler <vdp.h> macros,
 *     not defined anywhere in this repo) dropped from maps_paint()/
 *     maps_paintRect() -- same reasoning as tiles.c: no music plays
 *     during gameplay to protect a long blocking VRAM write from.
 *   - vdpmemcpy2() calls unchanged in shape -- it's the real NABU
 *     function from hal/sysvid_nabu.c (see scroller.h), not a stub.
 *     maps_paintRect() uses vdpmemcpy2() where the TI source calls a
 *     plain vdpmemcpy() (no "2") -- both TI functions do the same
 *     dest/src/count VRAM copy in this context (vdpmemcpy2()'s own
 *     header note already explains why only one of the two TI variants
 *     got ported), so this is a real simplification, not a typo.
 *   - IFDEBUG_MAPS(...)/sys_printf() calls dropped -- config.h's DEBUG
 *     isn't defined in this port, and sys_printf() has no real body yet
 *     (see hal/sys_nabu.c's own STATUS note), so these would only ever
 *     be dead code here.
 */

#include "config.h"
#include "env.h"
#include "maps.h"
#include "game.h"
#include "ents.h"
#include "tiles.h"
#include "fb.h"
#include "sprites.h" /* sprites_map1_extra[]/_nums[]/_num_count -- also
                      * streamed per map now, see that file's own header */
#include "e_sbonus.h" /* e_sbonus_counting */
#include "scroller.h" /* vdpmemcpy2() */
#include "sys_nabu_load.h" /* sys_nabu_loadAsset() -- map_loadMap()'s
                            * per-map streaming */
#include "sysvid_nabu.h" /* sysvid_nabu_hasF18A -- map_loadMap()'s own
                          * per-map gameplay tile bank load */

/*
 * global vars -- verbatim from xrick/src/maps.c
 */
U8 map_map[0x2C][0x20];
U8 map_eflg[0x100];
U16 map_frow;
U16 map_tilesBank;

/*
 * map_maps
 *
 * Restored as a real array now that map advancement is real (maps.h's
 * MAP_NBR_MAPS comment, main.c's e_rick_atExit handling). Verbatim
 * xrick/src/game.c values for x/y/row, EXCEPT submap: the reference uses
 * global submap numbers (SAMERICA's own chain starts at submap 0, EGYPT's
 * at global submap 9); this build rebases every map's own submap data to
 * be local/0-based on load (map_loadMap() below), so EGYPT's own entry
 * point is local submap 0, not global 9. `tune` (background music per
 * map) isn't ported -- this build has no gameplay background music, only
 * the map-intro screens' own music (engine/scr_imap.c) -- so that field
 * is dropped from this table entirely rather than carried as dead data.
 */
/* SIZE PASS: U8 fields, not U16 -- every real value here (x/y/row up to
 * 0x8b=139, submap always 0 once rebased) fits comfortably; same
 * "U8 table + cheaper multiply/access code" reasoning as map_dataSize[]
 * below. */
static const struct { U8 x, y, row, submap; } map_maps[MAP_NBR_MAPS] = {
  {0x08, 0x8b, 0x08, 0}, /* SAMERICA */
  {0x08, 0x8b, 0x68, 0}, /* EGYPT -- global submap 9, rebased to 0 */
  {0x10, 0x8b, 0x10, 0}, /* CASTLE -- global submap 20, rebased to 0 */
#if TEST_MBASE_LAST_SUBMAP
  /* TEST ONLY (build.ps1's TEST_MBASE_LAST_SUBMAP, on request "start MBASE
   * on the last submap so I can test the explosions"): MBASE's last
   * submap (8), entered from its left edge exactly as walking in from
   * submap 7 would -- connection 7 -> 8 is rowout 96 -> rowin 32, which
   * from the normal y (0x8b, row 17) gives map_frow 16 (0x10), and
   * e_rick_checkExit() puts Rick at x 0x04 on that side. */
  {0x04, 0x8b, 0x10, 8}, /* MBASE, last submap */
#else
  {0x10, 0x8b, 0x10, 0}, /* MBASE -- global submap 38, rebased to 0 */
#endif
};

/* Byte counts of the CURRENT map's own real submap/connect/bnum/mark/
 * block/sprite-extra data -- map_loadMap() needs these to know how much
 * of each per-map file to actually read (the buffers' own sizeof() is
 * the widened SHARED capacity, sized to the largest map ported so far,
 * not any one specific map's real size -- see maps.h's own header).
 * Indexed by env_map, same as map_maps[] above.
 *
 * ent_entdata[]/ent_sprseq[]/ent_mvstep[] are NOT in this table (BUG
 * FIXED -- see ents.h's own header): they're genuinely shared, whole-
 * game data, loaded once at boot (main.c's loadAssets()), not per-map.
 * A mark's .ent value encodes real semantic meaning via its numeric
 * range (ents.c's own big comment), so it can't be rebased/repacked
 * per map the way block ids or submap targets safely can. */
/* SIZE PASS: one U16 count per streamed table/map (order matches
 * map_loadTemplate[]/map_loadDest[]/map_loadElemSize[] below exactly)
 * instead of named struct fields -- lets map_loadMap()'s loop compute
 * every size as one array-indexed multiply, no switch/jump table needed
 * at all. A tighter U8-plus-bnum-split version was tried and measured
 * WORSE (the extra branching to un-skip bnum's slot cost more than the
 * narrower table saved) -- stayed with this simpler, uniform version.
 * xnum only, not xpat too -- SPXPAT is NOT bulk-loaded here anymore
 * (sprites.h's own MAP_NBR_SPRXTRA comment: EGYPT's own 32 extra sprites
 * stream on demand, one 128-byte pattern at a time, instead of all
 * being resident at once). */
/* xnum (column 5) trimmed off entirely only when NEITHER a disabled ECM
 * page NOR real stock-hardware support could ever make map_loadMap()'s
 * loop below reach index 5 -- same condition as map_loadTemplate[]/
 * map_loadDigitPos[]/map_loadDest[]'s own comment just above, and
 * sprites_map1_extra_nums[]'s own declaration comment (sprites.h).
 * sprites_map1_extra_num_count's own assignment (this function, below)
 * is gated the same way for the same reason. */
#if SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY
#define MAP_DATASIZE_COLS 5
#else
#define MAP_DATASIZE_COLS 6
#endif
static const U16 map_dataSize[MAP_NBR_MAPS][MAP_DATASIZE_COLS] = {
  /* sub  conn  bnum  mark block xnum */
  {  9,   27,   2048, 101, 58,
#if !(SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY)
     24,
#endif
  }, /* SAMERICA -- 22 real marks' worth plus the boulder's own 2 rolling
     * frames (97/105), folded into this list on request instead of
     * their own separate always-resident const table -- see sprites.h's
     * MAP_NBR_SPRXTRA_CACHED header and sprites.c's own dispatch */
  {  11,  33,   2144, 141, 64,
#if !(SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY)
     28,
#endif
  }, /* EGYPT -- see sprites.h's MAP_NBR_SPRXTRA header */
  {  18,  66,   2602, 174, 60,
#if !(SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY)
     43,
#endif
  }, /* CASTLE -- see sprites.h's MAP_NBR_SPRXTRA header */
  {  9,   27,   1358, 107, 73,
#if !(SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY)
     44,
#endif
  }, /* MBASE -- see sprites.h's MAP_NBR_SPRXTRA header */
};
/* SIZE PASS: U8, not U16 -- every real value here (max 16, block_t)
 * fits comfortably, same reasoning as map_maps[]/map_dataSize[] above. */
static const U8 map_loadElemSize[] = {
  8 /*submap_t*/, 8 /*connect_t*/, 1 /*U8*/, 10 /*mark_t*/, 16 /*block_t*/,
#if !(SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY)
  2 /*U16, SPXNUM*/,
#endif
};

static void map_eflg_expand(U16 offs) {
  U16 i, j, k;

  for (i = 0, k = 0; i < 0x10; i++) {
    j = map_eflg_c[offs + i++];
    while (j--) map_eflg[k++] = map_eflg_c[offs + i];
  }
}

/*
 * Fill in map_map with tile numbers by expanding blocks.
 *
 * add map_submaps[].bnum to map_frow to find out where to start from.
 * We need to /4 map_frow to convert from tile rows to block rows, then
 * we need to *8 to convert from block rows to block numbers (there
 * are 8 blocks per block row). This is achieved by *2 then &0xfff8.
 */
void map_expand(void) {
  U16 i, j, k, l;
  U16 row, col;
  U16 pbnum;
  int tmpbnum;

  pbnum = map_submaps[env_submap].bnum + ((2 * map_frow) & 0xfff8);
  row = col = 0;

  tmpbnum = map_bnums[pbnum];

  for (i = 0; i < 0x0b; i++) {   /* 0x0b rows of blocks */
    for (j = 0; j < 0x08; j++) { /* 0x08 blocks per row */
      for (k = 0, l = 0; k < 0x04; k++) { /* expand one block */
        map_map[row][col++] = map_blocks[tmpbnum][l++];
        map_map[row][col++] = map_blocks[tmpbnum][l++];
        map_map[row][col++] = map_blocks[tmpbnum][l++];
        map_map[row][col]   = map_blocks[tmpbnum][l++];
        row += 1; col -= 3;
      }
      row -= 4; col += 4;
      pbnum++;
      tmpbnum = map_bnums[pbnum];
    }
    row += 4; col = 0;
  }
}

/*
 * Initialize a new submap
 */
void map_init(void) {
  map_tilesBank = map_submaps[env_submap].page == 1 ? 2 : 1;
  map_eflg_expand((map_submaps[env_submap].page == 1) ? 0x10 : 0x00);

  map_expand();
  ent_reset();

  /* entities that are in the visible part of the map */
  ent_actvis(
      map_frow + MAPS_TOPHEIGHT_TL,
      map_frow + MAPS_TOPHEIGHT_TL + MAPS_VISHEIGHT_TL - 1);

  /* entities that are in the hidden top of the map */
  ent_actvis(
      map_frow + 0,
      map_frow + MAPS_TOPHEIGHT_TL - 1);

  /* entities that are in the hidden bottom of the map */
  ent_actvis(
      map_frow + MAPS_TOPHEIGHT_TL + MAPS_VISHEIGHT_TL,
      map_frow + MAPS_TOPHEIGHT_TL + MAPS_VISHEIGHT_TL + MAPS_BOTHEIGHT_TL - 1);
}

/*
 * Chain (sub)maps
 *
 * return: TRUE/next submap OK, FALSE/map finished
 *
 * WIRED UP: main.c's main loop now calls this when e_rick_atExit is set
 * (Rick walked off either horizontal edge of the current submap), the
 * same way xrick/src/game.c's NEXT_SUBMAP state does -- see that file's
 * header for what happens on each return value. Needed map_submaps[] to
 * actually hold every submap this can land on (maps.h's MAP_NBR_SUBMAPS
 * comment), not just index 0. Verbatim otherwise.
 */
/* SDCC warning 110 ("conditional flow changed by optimizer") is purely
 * informational -- it fires on map_chain()'s bounded connector search
 * below. Every restructuring that avoided it either kept the warning or
 * cost ~36 bytes, so it's silenced here instead (rest of this file too --
 * SDCC has no push/pop for this pragma). */
#pragma disable_warning 110

U16 map_chain(void) {
  U16 c, t;

  env_changeSubmap = 0;
  e_sbonus_counting = FALSE;

  c = map_submaps[env_submap].connect;
  t = 3;

  /*
   * look for the first connector with compatible row number. if none
   * found, then panic
   *
   * BUG FIXED, reported as "crawling into submap 9" (env_submap has no
   * valid value past 8 -- MAP_NBR_SUBMAPS below) with Rick stuck in a
   * wall: this loop used to run unbounded (`for (c = ...; ; c++)`),
   * trusting map_connect[]'s own dir==0xff sentinel to always be hit
   * before c ran past the table's real MAP_NBR_CONNECT entries. Real
   * connector data was checked byte-for-byte against the reference and
   * never contains a literal submap value of 9 anywhere, so the actual
   * trigger for this (crawling specifically, per the report) wasn't
   * pinned down -- but whatever it is, an unbounded c has no floor under
   * it: past the real table it just keeps reading whatever bytes follow
   * in memory as if they were more connect_t entries, and a coincidental
   * byte pattern there is a perfectly good way to manufacture a
   * "submap 9" that was never in any real map_connect[] row. Capped at
   * MAP_NBR_CONNECT now -- c can never leave the real table, so this
   * class of bug can't manufacture an out-of-range submap value even if
   * its root cause (still unconfirmed) fires again. */
  for (c = map_submaps[env_submap].connect; ; c++) {
    if (c >= MAP_NBR_CONNECT || map_connect[c].dir == 0xff)
      sys_panic("(map_chain) can not find connector\n");
    if (map_connect[c].dir != game_dir) continue;
    t = (ent_ents[1].y >> 3) + map_frow - map_connect[c].rowout;
    if (t < 3) break;
  }

  if (map_connect[c].submap == 0xff) {
    /* no next submap - request next map */
    return FALSE;
  } else {
    /* next submap */
    map_frow = map_frow - map_connect[c].rowout + map_connect[c].rowin;
    env_submap = map_connect[c].submap;
    /* Warms the shared ECM plane rolling cache with whichever submap
     * Rick just walked into's own extra sprites -- see engine/
     * include/sprites.h's own ecm_prefetchSubmap() comment. A submap
     * transition is an already-allowed HCCA trigger (this project's own
     * "level start or submap change" rule), so these fetches trade a
     * handful of real network round-trips here for avoiding however
     * many on-demand SPRxBy.DAT streams that submap's own entities
     * would otherwise cause during actual gameplay. Gated the same way
     * every other SPRITE_ECM_ENABLED call site in this build is. */
#if SPRITE_ECM_ENABLED
    ecm_prefetchSubmap(env_submap);
#endif
    return TRUE;
  }
}

/*
 * Reset all marks, i.e. make them all active again.
 */
void map_resetMarks(void) {
  U16 i;
  for (i = 0; i < MAP_NBR_MARKS; i++) {
    map_marks_ent[i] = map_marks[i].ent;
  }
}

/*
 * maps_paint
 *
 * paints the current map to the frame buffer.
 */
void maps_paint(void) {
  U16 i;
  int f;

  tiles_setBank(map_tilesBank); /* only reloads if it needs to */

  for (i = 1; i < 24; i++) { /* 23 rows, cause we skip the status row 0 */
    f = fb_at(0, i * 8);     /* gets VDP offset into gImage */
    vdpmemcpy2(gImage + f, map_map[i + 8], 32);
  }
}

/*
 * maps_paintRect
 *
 * paints a portion of the map at <x>, <y> of size <width>, <height>.
 * <x>, <y> expressed in map/px.
 *
 * SIZE PASS: wrapped out along with maps_alignRect() below, not deleted
 * -- zero callers in this build. Scrolling (engine/scroller.c) always
 * redraws the whole visible map via maps_paint() instead, and
 * sprites_paint2() (engine/sprites.c) doesn't clip entities off-screen
 * yet either -- see that file's own header. Un-wrap both together once
 * something needs a partial-map redraw.
 */
#if 0
void maps_paintRect(U16 x, U16 y, U16 width, U16 height) {
  U16 x_fb, y_fb;
  int fb;
  U16 r;

  /* align to tiles */
  maps_alignRect(&x, &y, &width, &height);

  /* clip */
  if (maps_clip(x, y, width, height)) /* return if not visible */
    return;

  /* convert to fb/px */
  x_fb = x - MAPS_FB_X;
  y_fb = y - MAPS_FB_Y;

  /* convert map/px to map/tl */
  x >>= 3;
  y >>= 3;
  width >>= 3;
  height >>= 3;

  /* draw */
  for (r = 0; r < height; r++) { /* for each tile row */
    fb = fb_at(x_fb, 8 + y_fb + r * 8); /* FIXME +8? */
    vdpmemcpy2(fb + gImage, &map_map[y + r][x], width);
  }
}

/*
 * maps_alignRect
 *
 * aligns a rectangle at <x>, <y> of size <width>, <height> to tiles.
 * coordinates expressed in map/px.
 * resulting rectangle might be bigger.
 */
void maps_alignRect(U16 *x, U16 *y, U16 *width, U16 *height) {
  U16 xa, ya;
  U16 wa, ha;

  /* align to column and row */
  xa = *x & 0xfff8;
  ya = *y & 0xfff8;

  /* grow width and height to cover tiles */
  *width += *x - xa;
  *height += *y - ya;
  wa = *width + 8 - (*width % 8);
  ha = *height + 8 - (*height % 8);

  *x = xa;
  *y = ya;
  *width = wa;
  *height = ha;
}
#endif

/*
 * map_loadMap
 *
 * NOT a port of anything -- xrick/src/game.c's static init() is the real
 * equivalent (sets up score/lives/bombs/bullets, picks env_map from
 * sysarg_args_map or a saved submap, then does the same env_submap/
 * map_frow/ent_ents[1] block below before calling map_resetMarks()), but
 * init() lives in the not-yet-ported game_run() state machine and pulls
 * in a lot more than just "boot into a map" (game state enum, pause/
 * restart handling, sysarg.c's command-line-args stand-in). This is the
 * minimal slice of it that map_init()/maps_paint() actually need to run
 * without crashing or reading garbage, PLUS (new) the actual per-map
 * asset streaming xrick's own init() never needed (the TI build kept
 * every map's data compiled in at once) -- see maps.h's own header on
 * why map_submaps/map_connect/map_bnums/map_marks reload fresh here
 * instead of staying resident for every map at once.
 *
 * Does NOT itself show the new map's own intro screen (screen_introMap(),
 * engine/scr_imap.c) the way xrick's own MAP_INTRO game_state would --
 * this function just loads and paints the map. main.c's e_rick_atExit
 * handling (the map-advancement branch) is what calls screen_introMap()
 * first, then loadTileBanks(), then this function, on request to restore
 * that real xrick progression -- see that call site's own comment (an
 * earlier attempt at the same sequence was reverted after crashing real
 * hardware/Marduk twice, but that was music_tick()'s own wraparound bug,
 * since fixed elsewhere -- see that comment for the full account). The
 * title screen's own level-select path (main()) already did call
 * screen_introMap() before this function every time, unchanged.
 *
 *   - env_map/env_submap/map_frow: without these, map_init() would
 *     expand whatever env_submap happened to already be -- setting them
 *     explicitly from map_maps[mapIndex] is what makes this "load map N"
 *     rather than "assume map_submaps[0] is already correct."
 *   - ent_ents[1] (Rick) position/size/n/sprite fields and
 *     ent_ents[ENT_ENTSNUM].n = 0xFF: the struct fields are exactly
 *     init()'s own values for a fresh Rick entity. The 0xFF terminator
 *     is the important part functionally -- ent_reset() (called from
 *     map_init()) does `for (i = 2; ent_ents[i].n != 0xff; i++)` with no
 *     bound other than that sentinel; without it planted here first,
 *     ent_reset() reads off the end of ent_ents[] into whatever memory
 *     follows it.
 *
 * Not included (genuinely deferred, not silently dropped):
 *   - env_lives/bombs/bullets/score -- status-bar state, unrelated to
 *     whether the map itself loads/displays. env_paintGame() (the status
 *     bar) isn't called here either.
 *   - The `sysarg_args_submap != 0` branch (resuming mid-submap) --
 *     sysarg.c isn't ported; this always starts a map at its own map_t
 *     entry, same as the sysarg_args_submap == 0 path always would.
 */
/* SIZE PASS: fully table-driven now -- filename template, digit
 * position, destination pointer, and per-entry byte size are all just
 * array[i] lookups, so the loop body is one multiply + one call, no
 * switch/jump table at all. Same mutable-single-digit-template trick as
 * engine/scr_imap.c's titleFile/bodyFile -- one template string per
 * table beats a whole separate literal per map. SPRSEQ (not ENTSPRSQ,
 * its old un-numbered name) is 6 letters + digit = 7 chars, keeping
 * every one of these filenames within the 8.3 DOS limit once the map
 * digit is appended. Order matches map_dataSize[]/map_loadElemSize[]
 * above exactly. */
static const U16 map_loadResBase[] = {
  RES_MAPSUB_BASE, RES_MAPCONN_BASE, RES_MAPBNUM_BASE, RES_MAPMARK_BASE,
  RES_MAPBLK_BASE,
#if !(SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY)
  RES_SPXNUM_BASE,
#endif
};
static const U16 map_loadResStride[] = {
  RES_MAPSUB_STRIDE, RES_MAPCONN_STRIDE, RES_MAPBNUM_STRIDE, RES_MAPMARK_STRIDE,
  RES_MAPBLK_STRIDE,
#if !(SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY)
  RES_SPXNUM_STRIDE,
#endif
};
static U8 *const map_loadDest[] = {
  (U8 *)map_submaps, (U8 *)map_connect, /*map_bnums filled below*/ 0, (U8 *)map_marks,
  (U8 *)map_blocks,
#if !(SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY)
  (U8 *)sprites_map1_extra_nums,
#endif
};

/* Streams the current map's (env_map) gameplay tile set for whichever
 * VDP mode is active (F18A or stock -- different art) and forces
 * tiles_setBank() to push it on its next call. Split out of map_loadMap()
 * so main.c's 'D' key (F18A/stock switch) can reload it mid-game. */
void map_loadTileSet(void) {
  static const U16 tilesSparseCount[4] = { 102, 154, 84, 150 }; /* SAMERICA/EGYPT/CASTLE/MBASE -- see tiles.h's TILES_SPARSE_MAX */
  /* TF18SPn.DAT/TILESPn.DAT, packed into RICK.DAT one fixed stride
   * apart (res.h). */
  tiles_gameplay_sparse_count = tilesSparseCount[env_map];
  sys_nabu_loadRes(sysvid_nabu_hasF18A ? RES_TF18SP_BASE + env_map * RES_TF18SP_STRIDE
                                       : RES_TILESP_BASE + env_map * RES_TILESP_STRIDE,
                   tiles_gameplay_sparse,
                   tiles_gameplay_sparse_count * TILES_SPARSE_ENTRY_SIZE);
  tiles_setBank(0xff);
}

void map_loadMap(U16 mapIndex) {
  U8 i;

  env_map = mapIndex;
  env_submap = map_maps[mapIndex].submap;
  map_frow = map_maps[mapIndex].row;
#if !(SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY)
  sprites_map1_extra_num_count = map_dataSize[mapIndex][5];
#endif

  /* Entry 5 (SPXNUM, the sprites_map1_extra_nums[] loader) is only
   * unreachable dead data when NEITHER a disabled ECM page NOR real
   * stock-hardware support can ever make sprites.c's walkIdx<0 fallback
   * run -- see that array's own declaration comment, sprites.h. Loop
   * bound stops one entry short only in that fully-dead case;
   * map_loadResBase[]/map_loadResStride[]/map_loadDest[]/
   * map_loadElemSize[] all stay their full 6-entry length otherwise. */
  for (i = 0; i < ((SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY) ? 5 : 6); i++) {
    sys_nabu_loadRes(map_loadResBase[i] + mapIndex * map_loadResStride[i],
                     (i == 2) ? map_bnums : map_loadDest[i],
                     map_dataSize[mapIndex][i] * map_loadElemSize[i]);
  }

  /* CHANGED, on request ("sprites don't get read constantly from
   * network... most recently downloaded sprites should stay in
   * memory"): this used to bulk-read the first MAP_NBR_SPRXTRA_CACHED
   * entries of this map's own extra-sprite list up front, unconditionally,
   * regardless of whether gameplay ever actually used them -- see
   * sprites.h's MAP_NBR_SPRXTRA_CACHED comment for why a fixed
   * by-list-position prefix doesn't actually track which sprites are
   * "hot" during real play. sprites_map1_extra_cached[] is a real
   * rolling LRU cache now (engine/sprites.c's own header on that array),
   * so there's nothing to eagerly bulk-load here any more -- just
   * invalidate every slot, since sprite NUMBERS only mean the same
   * pattern within the map that's about to become current. The cache
   * then fills itself in from whatever this map's own gameplay actually
   * requests, same discipline as sprites_map1_extra_nums[] itself. This
   * also removes a per-level-transition HCCA round-trip outright (the
   * old bulk read), on top of the reduced in-gameplay streaming the
   * rolling cache itself provides. */
#if !(SPRITE_ECM_ENABLED && ECM_ALL_PAGES_ENABLED && VDP_TARGET_F18A_ONLY)
  sprites_extraCacheReset();
#endif

  /* Gameplay tile bank -- streamed fresh per map now, same "streamed, not
   * globally resident" trick as every table above. See tiles_setBank()'s
   * own header for the full reasoning and why the tiles_setBank(0xff)
   * call below is required, not optional, even when the new map's own
   * bank number matches the previous map's.
   *
   * SIZE PASS, on request ("would splitting tiles into level used rather
   * than banks help?"): sparse now, not a flat 256-tile block -- see
   * tiles.h's own header for the full reasoning (1478-byte saving). One
   * combined file per level/hardware (TILESPn.DAT/TF18SPn.DAT, now
   * groups P/F of rick.dat) replaces the old four separate
   * TILEPATn.DAT/TILECOLn.DAT/TF18PBn.DAT/TF18CBn.DAT loads -- one HCCA
   * round-trip instead of two, and both hardware paths' filenames now
   * happen to share the same digit position (6), so this no longer needs
   * a per-hardware digitPos the way the old four-file version did. */
  map_loadTileSet();

  /* Warms the shared ECM plane rolling cache with SAMERICA's own submap
   * 0 extra sprites (sprite numbers 97/105, the boulder's 2
   * rolling-animation frames) -- see engine/include/sprites.h's own
   * ecm_prefetchSubmap() comment for the full "why here, why
   * mapIndex==0" reasoning. map_chain() calls the same function for
   * every OTHER submap transition (its own comment); this is just the
   * level-start equivalent, since SAMERICA always begins at submap 0.
   * Gated the same way main.c's own SPRITE_ECM_ENABLED call site is --
   * no definition exists to link against when that flag is 0. */
#if SPRITE_ECM_ENABLED
  if (mapIndex == 0) {
    ecm_prefetchSubmap(0);
  }
#endif

  ent_ents[1].x = map_maps[mapIndex].x;
  ent_ents[1].y = map_maps[mapIndex].y;
  ent_ents[1].w = 0x18;
  ent_ents[1].h = 0x15;
  ent_ents[1].n = 0x01;
  /* BUG FIXED, reported directly ("When I jump, the HCCA receives this
   * data" -- a raw trace showing a real SPR0B2.DAT fetch at offset 128,
   * length 128, i.e. engine sprite NUMBER 1): this used to be a literal
   * 0x01 placeholder, a value e_rick.c's own real state machine (see its
   * own sprite-assignment comments) never actually produces and isn't
   * one of Rick's cached poses either. ents_paintAll()'s very first
   * frame after a level loads draws this placeholder before
   * e_rick_action() ever runs (lastSpriteDrawn starts at 0xff, differs
   * from whatever this is, so load_pattern fires) -- uncached, so it
   * streamed all 3 ECM planes once per level/map load for a sprite
   * value nobody ever intended to actually show. 11 (0x0B, "standing,
   * facing right" -- e_rick.c's own E_RICK_STSTOP sprite) is both a
   * real, sensible initial pose AND one of Rick's own genuinely
   * high-frequency poses, so this placeholder now settles into the
   * shared ECM plane rolling cache (engine/include/ecm_cache.h) after
   * its first real frame like every other real pose, instead of
   * streaming freshly every single level load. */
  ent_ents[1].sprite = 0x0B;
  ent_ents[1].spriteIndex = 0xff;
  ent_ents[1].lastSpriteDrawn = 0xff;
  ent_ents[ENT_ENTSNUM].n = 0xff;

  map_resetMarks();
  map_init();
  maps_paint();
}

/* map_loadFirst() (used to just call map_loadMap(0) at startup) is gone
 * now -- SIZE PASS, no live callers: main.c calls map_loadMap() directly
 * with whichever map the title screen's own '1'-'5' key selected
 * (falling back to 0/SAMERICA for a still-unported selection), '1'-'5'
 * being real level-select keys now, not preview-only -- see main.c's
 * own header and call site comment. */

/*
 * maps_clip
 *
 * Clips a rectangle at <x>, <y> (map/px) of size <width>, <height>.
 * Returns TRUE if fully off the visible screen, FALSE if at least
 * partly visible. See this file's header for how this differs from the
 * TI source's version.
 */
U16 maps_clip(U16 x, U16 y, U16 width, U16 height) {
  (void)width; /* only used by the TI source's ALLOW_CROPS branches, which
                * this port drops -- see this file's header. Kept as a
                * parameter to match maps.h's existing extern signature. */

  /* x is unsigned, so the TI source's "x < 0" side of this check can
   * never fire here -- entities never have negative map x, nothing to
   * clip on that side. */
  if (x >= MAPS_WIDTH_PX)
    return TRUE;

  if (y < MAPS_TOPHEIGHT_PX) {
    if ((y + height) <= MAPS_TOPHEIGHT_PX)
      return TRUE;
  } else {
    if (y >= MAPS_TOPHEIGHT_PX + MAPS_VISHEIGHT_PX)
      return TRUE;
  }

  return FALSE;
}

/* eof */
