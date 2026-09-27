/*
 * engine/include/game.h -- ported from xrick/include/game.h
 *
 * Real change here, not just cosmetic:
 *
 * The TI source does
 *     #define gImage gImageReal ... #include <vdp.h> ... #undef gImage ...
 *     #ifdef F18A
 *       #define gImage 0x1c00 ...
 *     #else
 *       #define gImage 0x1800 ...
 *     #endif
 *
 * `<vdp.h>` is a header from the TI cross-compiler's own library (defines
 * gImageReal/gColorReal/etc as the addresses that compiler's VDP library
 * uses); it doesn't exist for z88dk and wouldn't mean anything there if it
 * did. And the #ifdef F18A block is exactly the compile-time-picks-one-of-
 * two-builds pattern config.h's port explicitly moved away from (see that
 * file's header comment) -- NABU needs one binary that decides its VRAM
 * layout at runtime, once sysvid_nabu_init() has run sysvid_nabu_detectF18A().
 *
 * So: gImage/gColor/gPattern/gSprite/gSpritePat become plain U16 variables
 * here, not #defines, set by sysvid_nabu_init9918()/sysvid_nabu_initF18A()
 * in hal/sysvid_nabu.c (TODO there -- currently only the 9918A path runs).
 * The TI source's two address sets are kept below as the starting point for
 * picking NABU-side values, but they are TI-chosen addresses (see the VRAM
 * layout notes in rickti's own todo.txt) and need to be re-checked against
 * whatever base address nabulib's vdp_initG2Mode() actually uses -- they
 * will not necessarily match, this is a placeholder, not a verified layout.
 */

#ifndef _GAME_H
#define _GAME_H

#include <stddef.h> /* NULL */

#include "ricksystem.h"
#include "maps.h"

/* TI 9918A-fallback addresses, kept as the starting point -- UNVERIFIED
 * against nabulib's actual table placement, see file header. */
#define GAME_VRAM_IMAGE_9918     0x1800
#define GAME_VRAM_COLOR_9918     0x2000
#define GAME_VRAM_PATTERN_9918   0x0000
#define GAME_VRAM_SPRITE_9918    0x1b00
#define GAME_VRAM_SPRITEPAT_9918 0x3800

/* TI cartridge F18A addresses -- kept only for reference/history. DO NOT USE:
 * these are the original TI cart's compile-time layout, not what nabulib's
 * vdp_initG2Mode() sets up on NABU. hal/sysvid_nabu.c's sysvid_nabu_initF18A()
 * calls the identical vdp_initG2Mode() as the 9918A path, so the real,
 * verified addresses for BOTH paths are the GAME_VRAM_*_9918 ones above --
 * that's what sysvid_nabu_initF18A() assigns gImage/gColor/gPattern/gSprite/
 * gSpritePat to now. Using these *_F18A values instead was the cause of the
 * F18A/PICO9918 title-screen and tile-test corruption (name table writes
 * landing at 0x1c00 while the VDP was actually reading it from 0x1800). */
#define GAME_VRAM_IMAGE_F18A     0x1c00
#define GAME_VRAM_COLOR_F18A     0x2000
#define GAME_VRAM_PATTERN_F18A   0x0000
#define GAME_VRAM_SPRITE_F18A    0x1f00
#define GAME_VRAM_SPRITEPAT_F18A 0x2800

/* F18A 8-color sprite mode (ECM=3) support: two EXTRA sprite pattern
 * "color bit" planes, read alongside the existing gSpritePat (which
 * stays the shape/transparency plane, unchanged) to give each "on" pixel
 * a 3-bit (0-7) color index instead of a single fixed attribute color.
 *
 * FIXED, on request ("add support for the TI-99's version multicolor
 * F18A sprites") -- these used to be 0x0800/0x1000, fixed addresses
 * disconnected from where the F18A chip actually looks for them. Real
 * F18A hardware computes these addresses itself as
 * `spritePatternBase + n*ecmOffset` (spritePatternBase = register 6,
 * ecmOffset = 0x800 by default) -- there is no way to place these planes
 * independently of wherever gSpritePat's own base actually is. See
 * engine/sprites.c's own SPRITE_ECM comment (right above
 * SPRITE_ECM_PAL_RICKWALK) for the full root-cause writeup and
 * hal/sysvid_nabu.c's set_halfbitmap() for where register 6 actually
 * gets set to GAME_VRAM_SPRITEPAT_F18A (0x2800) to make these two
 * constants (0x2800+0x800, 0x2800+0x1000) correct. Matches the real F18A
 * cartridge's own verified layout exactly (xrick/src/xrick.c's sys_init()
 * comment: "2800 Sprite colors (2k table) bit 0 / 3000 ...bit 1 / 3800
 * ...bit 2"). Sized/placed to match SPRITE_SIZE-per-frame (128 bytes),
 * same as gSpritePat itself, with 2048 bytes of real spacing between each
 * -- comfortably more than the 1664 bytes all 13 entities' own pattern
 * data together actually needs (ENT_ENTSNUM+1 * 4 real hardware sprites *
 * 32 bytes each), so no entity's own shape data can ever bleed into the
 * next plane's start address. */
#define GAME_VRAM_SPRITEPAT_BIT1 0x3000
#define GAME_VRAM_SPRITEPAT_BIT2 0x3800

/* Set once at startup by sysvid_nabu_init9918()/sysvid_nabu_initF18A(). Every
 * engine file that referenced the old gImage/gColor/gPattern/gSprite/
 * gSpritePat #defines can keep doing so verbatim -- these are the same names,
 * just runtime rather than compile-time now. */
extern U16 gImage;
extern U16 gColor;
extern U16 gPattern;
extern U16 gSprite;
extern U16 gSpritePat;

#define LEFT 1
#define RIGHT 0

// Default gameplay frame time in ms, rounded up to whole 16.7ms vblanks
// (hal/sys_nabu.c's sys_nabu_waitFrame()): 50 = 3 vblanks = 20fps, on
// request (faster than the TI original's 66 = 4 vblanks = 15fps; 33 = 2 =
// 30fps is the next step up). The in-game 'F' key toggles the frame
// limiter off entirely (period 0 -- runs as fast as the game can) and
// back (main.c).
#define GAME_PERIOD 50

#define GAME_BOMBS_INIT 6
#define GAME_BULLETS_INIT 6

typedef struct {
  U16 score_hi; // and above
  U16 score_lo; // 0-9999
  U8 name[11];
} hscore_t;

extern hscore_t game_hscores[8];  /* highest scores (hall of fame) */
/* map_maps[] extern removed -- see maps.c's own comment: it's gone,
 * inlined as a literal into map_loadFirst() (SIZE PASS). */

extern U16 game_dir;        /* direction (LEFT, RIGHT) */

extern U16 game_waitevt;    /* wait for events (TRUE, FALSE) */
extern U16 game_period;     /* time between each frame, in millisecond */
extern U16 game_speedPeriod; /* normal gameplay period ('F' key speed), main.c */

extern void game_run(char *path);
extern void game_toggleCheat(U16);

#endif

/* eof */
