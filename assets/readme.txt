Rick Dangerous NABU PC data files
=================================

These are the source files for everything the game loads over the network.
tools/pack_assets.py packs them into RICK.DAT and RICK.SPR

  RICK.DAT  every file below except the ECM sprite pages, each aligned to
            4 bytes. Numbered sets (MAPSUB1-4, ENT0-8, ...) are stored one
            fixed stride apart. pack_assets.py also writes
            src/engine/include/res.h, whose RES_<name> constants are each
            file's offset (RES_MUSIC1, RES_MAPBLK_BASE + n * RES_MAPBLK_STRIDE).
  RICK.SPR  ECMSPR0.DAT to ECMSPR4.DAT back to back. A sprite's offset is
            its physical slot number * 384, so no table is needed.

To add or change a file: edit it here, add it to SINGLES or GROUPS in
tools/pack_assets.py if it is new, and rebuild. Keep file names 8.3.

Numbering used below
--------------------
  Maps (1-4):    1 SAMERICA (South America), 2 EGYPT, 3 CASTLE
                 (Schwarzendumpf castle), 4 MBASE (missile base).
  Intros (1-5):  the four maps above, then 5 = EPILOGUE ("London, much
                 much later"), shown after finishing MBASE.
  "Stock" means the plain TMS9918A video chip; "F18A" means the F18A
  upgrade (or a Pico9918 in F18A mode). Files marked with one are only read
  on that hardware. The game picks at boot, and 'D' switches in-game.

Sound format (music and effects)
--------------------------------
Every .DAT sound is a list of steps played one tick (1/60 s) at a time:

  [count] then count pairs of [AY register][value], then [wait]

The player writes the register pairs to the AY-3-8910 sound chip, then
waits [wait] extra ticks. Registers 8-10 are the three channel volumes
(0-15). tools/dat_to_vgm.py converts a file to .VGM to listen to it, and
tools/vgm_to_dat.py makes new ones from a .VGM capture.


TITLE SCREEN AND HALL OF FAME
=============================

SF18PAT.DAT    6144  Title picture for F18A, pattern table: a full-screen
                     256x192 Graphics II bitmap, 8 bytes per 8x8 cell.
SF18COL.DAT    6144  Title picture for F18A, colour table. Its colours are
                     indices into the F18A palette the game loads
                     (pic_splashf18_pal in main.c), not stock colours.
SPLSHPAT.DAT   6144  Title picture for stock, pattern table (same layout).
SPLSHCOL.DAT   6144  Title picture for stock, colour table (standard
                     TMS9918A fg/bg nibbles).
HAFPAT.DAT      768  "Hall of Fame" banner, pattern table: 96 tiles, i.e.
                     the top 3 text rows of the screen. Loaded into tiles
                     128-223 behind the font. Extracted from the TI port's
                     pic_haf_pat. Used on both chips.
HAFCOL.DAT      768  Hall of Fame banner, colour table (pic_haf_col).
MUSIC1.DAT     2108  Title music. Also plays on level select, name entry
                     and the hall of fame.


FONTS
=====
The font bank is tile bank 0: letters and digits at their ASCII positions,
plus the status-bar icons and the level-intro scenery. '@' is the blank
glyph (' ' is not blank in this set). 256 tiles x 8 bytes.

TF18PATA.DAT   2048  Font / bank 0 for F18A, pattern table.
TF18COLA.DAT   2048  Font / bank 0 for F18A, colour table (F18A palette).
TILEPATA.DAT   2048  Font / bank 0 for stock, pattern table.
TILECOLA.DAT   2048  Font / bank 0 for stock, colour table.


LEVEL INTRO SCREENS
===================

MAPTIT1-5.DAT    31  Intro title text (the level's name and year), one per
                     intro, in font tile codes.
MAPBOD1-5.DAT   281  Intro body text (the short story shown under the
                     title), padded to the same size for all five. Uses
                     the TILES_CRLFCHAR line break / TILES_NULLCHAR end
                     markers from tiles.h.
INTRMUS1.DAT   2018  Intro music for SAMERICA (originally MUSIC2.DAT).
INTRMUS2.DAT   2464  Intro music for EGYPT, captured from the ZX Spectrum
                     128 version.
INTRMUS3.DAT   2178  Intro music for CASTLE.
INTRMUS4.DAT    650  Intro music for MBASE.
INTRMUS5.DAT    486  Intro music for the EPILOGUE (London), captured from
                     the ZX Spectrum 128 version.


MAP DATA (one file per map, 1-4)
================================
Converted from xrick's map tables and rebased so each map's own numbers
start at 0. Multi-byte values are little-endian U16.

MAPSUB1-4.DAT        Submaps (the screens a level is split into): 8 bytes
                     each -- tile page, first block-number row, first
                     connection, first entity mark. 9/11/18/9 submaps.
MAPCONN1-4.DAT       Connections between submaps: 8 bytes each -- exit
                     direction, row out, target submap, row in.
MAPBNUM1-4.DAT       Block numbers: the level layout, rows of 8 blocks,
                     1 byte per block.
MAPBLK1-4.DAT        Block definitions: 16 bytes each, a 4x4 group of tile
                     numbers.
MAPMARK1-4.DAT       Entity marks (enemies, bonuses, boxes, traps and where
                     they appear): 10 bytes each -- row, entity type,
                     flags, x/y and trigger info.
TF18SP1-4.DAT        Level tile set for F18A, sparse: only the tiles that
                     level really uses. 17-byte records: tile number,
                     8 pattern bytes, 8 colour bytes.
TILESP1-4.DAT        Level tile set for stock, same sparse format.
SPXNUM1-4.DAT        Stock only: list of the extra sprite numbers this
                     level uses beyond the page-0 set, U16 each. Position
                     in this list = position in SPXPAT.
SPXPAT1-4.DAT        Stock only: the single-colour patterns for those
                     extra sprites, 128 bytes each (a 32x32 sprite as
                     four 16x16 quadrants of 32 bytes).


GAME DATA (whole game, loaded once at boot)
===========================================

MAPEFLG.DAT      32  Tile flags, compressed. Expanded at each level to one
                     byte per tile: solid, climbable, lethal, foreground,
                     one-way-up, super pad, vertical-only.
ENTDATA.DAT    1036  Entity types: 74 records of 7 U16s -- size, sprite,
                     sprite sequence, trigger box and kill sound.
SPRSEQ.DAT      272  Sprite animation sequences: 136 U16 sprite numbers.
MVSTEP.DAT     1566  Entity movement paths: 261 steps of [repeat count,
                     dx, dy], U16/S16 each.


SPRITES
=======

ECMSPR0.DAT   18432  F18A only. ECM 8-colour sprite page 0: Rick, his
                     bullets and dynamite, and the sprites most levels
                     share. 48 sprites x 384 bytes -- each sprite's three
                     colour bit-planes (128 bytes each) back to back.
ECMSPR1.DAT   18432  ECM sprite page 1 (mostly EGYPT).
ECMSPR2.DAT   18432  ECM sprite page 2 (mostly CASTLE, plus the SAMERICA
                     boulder).
ECMSPR3.DAT   18432  ECM sprite page 3 (mostly MBASE, plus the spear guy).
ECMSPR4.DAT    8064  ECM sprite page 4 (rest of MBASE): 21 sprites.
                     All five are made from the original per-plane files
                     (SPR<page>B<plane>.DAT by
                     tools/interleave_ecm_planes.py. They go into RICK.SPR,
                     not RICK.DAT.
SPR0.DAT       6144  Stock only. The 48 page-0 sprites (Rick and the common
                     sprites) in single colour, 128 bytes each. Streamed a
                     sprite at a time into a cache as needed.


SOUND EFFECTS
=============
All share one playback slot. Explosions, enemy kills and the bonus
sounds have priority, so a footstep can't cut them off.

JUMP.DAT         76  Rick jumps.
BULLET.DAT       82  Rick fires his gun.
EXPLODE.DAT      94  Dynamite explodes.
WALK.DAT         36  Footstep. Also used for crawling (CRAWL was
                     byte-identical). Volume raised to 10 for the NABU.
STICK.DAT        44  Rick's stick attack.
BOMBSHHT.DAT     34  Dynamite fuse ticking. Volume raised to 12.
BOX.DAT         150  Box hit (boxes hold bullets or dynamite).
PAD.DAT         132  Super pad (a spring pad that launches entities).
TREASURE.DAT    244  Treasure / bonus collected.
SBONUS.DAT       94  Speed bonus sequence starts.
SBONUS2.DAT     134  Speed bonus collected.
DIE.DAT         446  Rick dies.
GAMEOVER.DAT    178  Game over jingle (plays through the effect player).
ENT0-8.DAT  34-1564  Enemy / entity kill sounds, one per sound number in
                     ENTDATA.DAT (ENT7 is the long one). Streamed when
                     played, not kept in memory.
WAAAAA.PCM     8159  A raw sample of the "WAAAAA" scream, played through
                     the AY volume registers by the 'C' cheat key. Blocks
                     the game while it plays.
