// ************************************************************************
// Rick Dangerous for the NABU PC
// https://github.com/zpqrtbnk/xrick
// https://github.com/tursilion/rickti
// https://github.com/gtampdotcom/ricknabu
//
// Flow: title screen (with the hall of fame in its attract cycle) ->
// level select -> map intro -> gameplay -> game over / ESC -> name entry
// (top-8 score) -> hall of fame -> level select. Finishing MBASE shows the
// epilogue, then the same game over path.
//
// Hardware: runs on a stock TMS9918A or an F18A (detected at boot; 'D'
// switches between them on an F18A). F18A gets ECM 8-colour sprites and
// its own tile/title art.
//
// Assets: nothing large is compiled in. Everything streams over HCCA from
// two RetroNET files, RICK.DAT (pictures, fonts, music, sound effects, map
// data, tiles) and RICK.SPR (ECM sprite planes), packed from assets/
// by tools/pack_assets.py (run by build.ps1) -- see engine/include/res.h
// for the offsets. Sprites and tiles stream on demand into rolling caches
// (engine/include/ecm_cache.h). build.ps1 checks that code + data + BSS
// leave enough room for the stack below $FF00.
// ************************************************************************

#define BIN_TYPE BIN_HOMEBREW

// engine/scr_imap.c's screen_introMap() is the first caller of NABU-LIB's
// getChar() in this build (isKeyPressed()/getChar() stand in for
// CONTROL_FIRE) -- getChar()'s
// default body includes a blinking-cursor branch (vdp_writeCharAtLocation()
// etc.) for interactive text input, which this project never does anywhere.
// DISABLE_CURSOR (NABU-LIB.h's own documented flag) drops that branch down
// to a plain "wait for a queued key" loop -- real bytes back at zero
// behavior cost here.
#define DISABLE_CURSOR

#include <stdint.h>
#include "NABU-LIB.h"

// z88dk's classic crt0 hardwires TAR__fputc_cons_generic=1 for every nabu
// build (see nabu_crt0.asm), which unconditionally links the whole ANSI/
// VT52 console-emulation driver (fputc_cons_generic_full.asm -- parameter
// tables, cursor/scroll state machine) plus its 8x8 font and a 1920-byte
// shadow text buffer, adding up to several KB, even though nothing in this
// game calls printf/putchar or any other stdio console function -- all
// screen output goes through NABU-LIB's own vdp_* calls (hal/sysvid_nabu.c)
// instead. Redirecting fputc_cons to this one-instruction stub (build.bat's
// -pragma-redirect:fputc_cons=_fputc_cons_stub) drops that entire driver
// from the link.
void fputc_cons_stub(void) __naked {
  __asm
  ret
  __endasm;
}

#include "hal/sysvid_nabu.c"
#include "hal/f18a_nabu.c"
#include "hal/sysevt_nabu.c"
#include "hal/sys_nabu.c"
#include "hal/sys_nabu_load.c"

static void loadTileBanks(void); /* forward decl -- defined further down
                                   * this file, needed here first so
                                   * engine/tiles.c's own tiles_setBank()
                                   * can call it (see that call site's own
                                   * comment for why). */

#include "engine/dat_tilescTI.c"
#include "engine/dat_tilespTI.c"
#include "engine/env.c"
#include "engine/tiles.c"
#include "engine/fb.c"
#include "engine/ecm_cache.c"
#include "engine/sprites.c"

#include "engine/dat_screens.c"
#include "engine/scr_imap.c"

#include "engine/control.c"
#include "engine/util.c"

#include "engine/dat_ents.c"
#include "engine/dat_ents2.c"
#include "engine/sounds.c"
#include "engine/e_them.c"
#include "engine/e_box.c"
#include "engine/e_bonus.c"
#include "engine/e_sbonus.c"
#include "engine/ents.c"
#include "engine/e_rick.c"
#include "engine/e_bullet.c"
#include "engine/e_bomb.c"

#include "engine/dat_maps1.c"
#include "engine/dat_maps2.c"
#include "engine/dat_maps3.c"
#include "engine/dat_maps4.c"
#include "engine/maps.c"
#include "engine/scroller.c"
#include "engine/scr_hof.c"  /* hall of fame + name entry, and the shared
                              * SCRATCH_BUF the title picture also uses */

// Title screen. Based on xrick/src/game.c's draw_titlepage(); the F18A
// palette branch follows C:\nabu\rick-attract's version, which had
// already been proven on real hardware.
//
// Streams straight to VRAM through an idle sprite cache (title_draw()'s
// own comment). Needs sysvid_nabu_init() (gImage/gPattern/gColor, and
// -- for the F18A branch -- ECM1 mode already set by
// sysvid_nabu_initF18A()) to have already run.
//
// The splash image's own real size in bytes, per half (pattern, colour).
#define SPLASH_SCRATCH_SIZE 6144

// The title and level-select loops run once per vblank (one 16.7ms
// sys_nabu_waitFrame() period); their music's tempo is set separately by
// MUSIC_RATE_TITLE (engine/include/sounds.h), independent of gameplay's
// GAME_PERIOD.
#define VBLANK_PERIOD 16

/* SCREEN_TRIM_TITLE, on request ("add a define for disabling all
 * code+data related to title"): temporary byte-budget experiment knob,
 * same family/spirit as SOUND_TRIM_MUSIC/PCM/FX (build.bat's own
 * comment) -- 0 keeps the real title screen, 1 strips its whole splash-
 * screen blit (VDP writes, F18A/9918A filename pick, palette load) down
 * to a stub that always returns FALSE ("no level chosen via digit
 * press"), same as if a plain fire press had skipped it -- the ONE call
 * site (this function's own header comment further down covers what
 * that return value drives) needs no change at all, it already treats
 * FALSE as "fall through to screen_levelSelect()". pic_splashf18_pal[]
 * (only 32 bytes) is gated with it since nothing else reads it. */
#if !SCREEN_TRIM_TITLE
// pic_splashf18_pal[] is only 16 U16 entries (32 bytes) -- cheap enough to
// keep as a real compiled-in const instead of a third network asset.
static const U16 pic_splashf18_pal[16] = {
  0x0000, 0x0420, 0x0F96, 0x0222, 0x0D60, 0x0940, 0x0999, 0x0666,
  0x0BBB, 0x0D00, 0x0B66, 0x0000, 0x0000, 0x0000, 0x0444, 0x0000
};

/* Draws the title picture -- split out of screen_titlepage() so its
 * attract cycle can redraw it after showing the hall of fame. */
static void title_draw(void) {
  U16 i;
  U16 patRes, colRes;

  /* silence the title music (if already playing, i.e. the attract cycle
   * is redrawing) for the picture's load -- sounds_music_hold(). With
   * MUSIC_SMOOTH_LOADS it keeps playing instead (engine/scr_hof.c's
   * stream_to_vram()). */
#if !MUSIC_SMOOTH_LOADS
  sounds_music_hold();
#endif

  /* The picture needs the normal three-thirds layout; after a game on an
   * F18A the VDP is still in set_halfbitmap()'s one-table layout, which
   * showed the picture's top third three times (reported). */
#if SPRITE_ECM_ENABLED
  set_fullbitmap();
#endif

  /* SIT: bitmap-mode addressing needs pattern name == screen position
   * (truncated to a byte, wrapping every 256 -- the "three thirds" of a
   * 32x24=768-cell screen), not the small reused tile set gameplay's own
   * tiles.c writes here later -- maps_paint() overwrites this SIT
   * completely once the real map loads, so there's no lasting conflict. */
  NABU_DisableInterrupts();
  vdp_setWriteAddress(gImage);
  for (i = 0; i < 0x300; i++) {
    IO_VDPDATA = (U8)i;
    VDP_WRITE_SETTLE(); /* see hal/sysvid_nabu.c's own comment */
  }
  NABU_EnableInterrupts();

  if (sysvid_nabu_hasF18A) {
    /* F18A: ECM1 mode (already set by sysvid_nabu_initF18A()) reads the
     * color bytes as 4-bit palette indices instead of 9918A fg/bg pairs,
     * so this needs its own pattern/color data (SF18PAT/COL.DAT, not the
     * plain SPLSHPAT/COL.DAT) plus the palette itself. Palette register
     * writes need the F18A unlocked; it is deliberately never relocked
     * (relocking switches ECM sprites off -- see hal/f18a_nabu.c where
     * f18a_lock() used to be). The actual load+blit below is shared
     * with the 9918A path -- only the filenames differ -- since the two
     * are otherwise identical and duplicating both inline cost more bytes
     * than picking a filename pair up front. */
    f18a_unlock();
    f18a_loadPalette(pic_splashf18_pal, 0, 16);
    patRes = RES_SF18PAT; /* see engine/include/res.h */
    colRes = RES_SF18COL;
  } else {
    patRes = RES_SPLSHPAT;
    colRes = RES_SPLSHCOL;
  }

  /* One read + one VRAM copy per half (pattern, then colour) through a
   * sprite cache that is idle here (no sprites on the title screen): the
   * ECM slot cache, or on a 9918A-only build its full-size page-0 cache
   * (48 x 128 = exactly SPLASH_SCRATCH_SIZE). Its sprite contents are
   * invalidated right after, so they simply re-stream when gameplay next
   * needs them. Same SCRATCH_BUF/scratch_end() pair engine/scr_hof.c uses. */
#if SPRITE_ECM_ENABLED
#if ECM_SLOT_CACHE_TOTAL_BYTES < SPLASH_SCRATCH_SIZE
#error "ECM slot cache too small to hold one title picture half"
#endif
#else
#if PAGE0_CACHE_SLOTS * SPRITE_SIZE < SPLASH_SCRATCH_SIZE
#error "page-0 sprite cache too small to hold one title picture half"
#endif
#endif
  stream_to_vram(patRes, gPattern, SPLASH_SCRATCH_SIZE, FALSE);
  stream_to_vram(colRes, gColor, SPLASH_SCRATCH_SIZE, FALSE);
  scratch_end();

  /* BUG FIXED, reported as "font using title data" after looping back from
   * ENDING to the title screen a second time: this blit writes the splash
   * art directly into gPattern/gColor, completely bypassing tiles_setBank()
   * -- so tiles_setBank()'s own "skip if already this bank" cache
   * (tiles.c's static lastSetBank) never finds out VRAM changed underneath
   * it. First pass through main()'s title->select loop this was harmless
   * (lastSetBank started at its 0xff init value, so screen_levelSelect()'s
   * tiles_setBank(0) always did a real reload) -- but the second pass,
   * lastSetBank was already 0 from that same earlier real reload, so
   * screen_levelSelect()'s tiles_setBank(0) short-circuited and left the
   * splash image's graphics in VRAM instead of the font. Same root cause
   * and same fix as engine/scr_hof.c's hof_setupScreen(). */
  tiles_setBank(0xff);
#if !MUSIC_SMOOTH_LOADS
  sounds_music_resume();
#endif
}

/* Attract cycle, on request (hall of fame back): with no input the title
 * gives way to the hall of fame (engine/scr_hof.c), then the title comes
 * back -- xrick's own title <-> hall-of-fame loop. The title music keeps
 * playing throughout, and each switch happens when it reaches its end
 * (sounds_music_looped(), ~51s per play-through), so the screen loads
 * fall in the gap between play-throughs instead of cutting into the tune. */
static U8 screen_titlepage(void) {
  U16 lastControl;
  U8 levelChosen = FALSE;

  title_draw();

  /* tiles_banks_col (tiles_banks_shared) is safe to overwrite again right
   * here -- the splash color data it just held is already blitted to
   * VRAM above, and loadAssets() (this function's own caller, right
   * after it returns) is the next thing to load real data into that
   * memory. See engine/sounds.c's music_vgm/sounds_music_load() headers
   * for the rest of this scratch-space handoff chain (this screen ->
   * loadAssets() -> engine/scr_imap.c's map0_title/map0_body -> back to
   * loadAssets() again before map_loadFirst()). */
  /* CHANGED, on request ("Sound disabled... reduce HCCA use"): guarded,
   * not just left as a now-harmless stub call -- MUSIC1_SIZE itself only
   * exists inside engine/sounds.c's own SOUND_ENABLED block (config.h),
   * so this whole call site needs to disappear with it, not merely
   * become a no-op, or MUSIC1_SIZE would be an undefined identifier. */
#if SOUND_ENABLED
  sounds_music_load(RES_MUSIC1, MUSIC1_SIZE);
#endif

  /* wait for fire, same as xrick's own title screen waiting for a
   * keypress/fire before continuing -- now with a real AY hardware rip
   * of the title theme playing, see engine/sounds.c's music_vgm/
   * sounds_music_load()/sounds_music_start()/sounds_music_tick()/
   * sounds_music_stop() headers for why it's a separate system from
   * sounds_play()'s effects.
   *
   * On request: '1'-'5', pressed anytime while this loop runs, picks
   * which map's title card screen_introMap() shows later (screens.h's
   * screen_introMap_sel -- '5' is the reference's own "much much later"
   * epilogue, normally shown after finishing map 4, previewed directly
   * here instead) AND exits this loop immediately -- no separate
   * fire-press needed, unlike the normal joystick-fire exit. Forces
   * CONTROL_FIRE on directly (SIZE PASS: reuses the loop's own existing
   * exit check instead of a second flag/condition) rather than a plain
   * `break` -- sounds_music_stop() used to run right here either way;
   * see this function's own return value and screen_levelSelect() below
   * for why it doesn't anymore. Gameplay itself is unaffected either
   * way -- maps 1..4 have no real level data (maps.h's own comment), so
   * this only ever previews a title card before map 1's own gameplay
   * begins as usual. */
  sounds_music_start();
  /* BUG FIXED, reported as "return to title screen from ending" skipping
   * it too: same class of bug, same fix, as screen_levelSelect()'s own
   * lastControl seeding and screen_gameOver()'s own (both this file's own
   * comments there) -- `control_status & CONTROL_FIRE` below used to be
   * checked with no edge-detection at all, so a FIRE already held the
   * instant this loop starts (very plausible looping back here straight
   * from the ENDING intro screen, screen_introMap_sel==4's own
   * `goto restart_title`) exited on the very first iteration, before the
   * title music or splash even had a moment on screen. Poll once and seed
   * lastControl with whatever's already held, then only react to a NEW
   * joystick press from here on -- the keyboard's own "any key skips"
   * path further down stays independent of this, deliberately: a real
   * keypress should always skip immediately regardless of whatever the
   * joystick happens to be doing. */
  sysevt_poll();
  lastControl = control_status;
  do {
    U16 pressed;
    U8 exitNow = FALSE;

    sysevt_poll();
    sounds_music_tick(MUSIC_RATE_TITLE);
    if (isKeyPressed()) {
      U8 key = getChar();
      if (key >= '1' && key <= '5') {
        screen_introMap_sel = key - '1';
        levelChosen = TRUE;
      }
      // CHANGED, on request ("make any key skip title screen"): used to
      // only set CONTROL_FIRE (the loop's own exit condition) for a real
      // '1'-'5' digit press -- any other key was silently swallowed here
      // and otherwise ignored. Now every key exits the loop the same way
      // a real joystick FIRE press already does; only a real digit press
      // also sets levelChosen, matching this function's own header on
      // what its return value means.
      exitNow = TRUE;
    }
    pressed = control_status & ~lastControl;
    lastControl = control_status;
    if (pressed) { /* any new joystick direction or button, like any key */
      exitNow = TRUE;
    }
    /* One loop per vblank; the music's own tempo is MUSIC_RATE_TITLE. */
    sys_nabu_waitFrame(VBLANK_PERIOD);
    if (exitNow) {
      break;
    }
    /* Attract cycle: hall of fame when the music ends, then the title
     * again when it ends next -- unless input ends it, which counts like
     * input here. */
    if (sounds_music_looped()) {
      U8 key = screen_hallOfFame();
      if (key) {
        if (key >= '1' && key <= '5') {
          screen_introMap_sel = key - '1';
          levelChosen = TRUE;
        }
        break;
      }
      title_draw();
      sysevt_poll();
      lastControl = control_status;
    }
  } while (TRUE);
  return levelChosen;
}
#else
static U8 screen_titlepage(void) {
  return FALSE;
}
#endif /* SCREEN_TRIM_TITLE */

/* On request: reached only when screen_titlepage() above was left by a
 * plain fire press, WITHOUT ever choosing a level via '1'-'5' (that
 * function's own return value, checked at this screen's one call site
 * below) -- "skipping" the title screen this way used to silently
 * default straight into SAMERICA with no real choice made. The same
 * title music keeps playing -- continued from wherever
 * screen_titlepage()'s own sounds_music_tick() calls left it, NOT
 * restarted (sounds_music_start() is not called again here) -- just a
 * stricter wait loop: only a real '1'-'5' digit press can end it, no
 * plain-fire shortcut, so reaching gameplay always means a level was
 * actually chosen either here or on the title screen itself.
 * sounds_music_stop() still isn't called here either -- see this
 * screen's own call site in main() for where that finally runs, once,
 * regardless of which of the two screens actually picked the level.
 *
 * On request: shows the actual level list now, not just the splash
 * screen silently waiting -- fb_clear() first (engine/fb.c), since the
 * splash art is still on VRAM at this point and painting text over that
 * busy background would be unreadable, same reasoning engine/
 * scr_imap.c's own text screens already use. levelSelectText[] below is
 * the exact same tile-font encoding convention as that file's own
 * maps_intros[] ('@' for space, TILES_CRLFCHAR for a new line,
 * TILES_NULLCHAR to terminate) and the exact same tiles_paintListAt()
 * (engine/tiles.c) call it uses to paint it -- reusing that already-
 * resident font/renderer instead of anything new. Painted once, before
 * the wait loop, not every frame -- nothing here ever needs to redraw
 * it. EPILOGUE's own selector value here is 4 ('5' - '1'), matching
 * screen_introMap_sel's own convention throughout this file.
 *
 * BUG FIXED, reported as "displayed garbage instead of text": fb_clear()
 * + tiles_paintListAt() alone isn't enough -- screen_titlepage() above
 * left the SPLASH IMAGE's own custom graphics loaded into VRAM's
 * pattern/color memory (gPattern/gColor), not the real font glyphs.
 * screen_introMap() only ever renders correctly because main()'s own
 * loadAssets()/loadTileBanks() call loads the real tile bank data (the
 * font included) and screen_introMap() itself calls tiles_setBank(0) to
 * push it to VRAM before painting anything (see that screen's own
 * comment on exactly this contract) -- neither of those had happened
 * yet at this point in the boot sequence. Fixed by doing the same two
 * steps here: loadTileBanks() (forward-declared below, defined further
 * down this file) loads the real tile data into tiles_banks_shared,
 * tiles_setBank(0) pushes it to VRAM.
 *
 * That collides with something else, though: tiles_banks_shared is the
 * exact same memory as sounds.c's own music_vgm (see that macro's own
 * header) -- the title music currently playing is being read live out
 * of that memory by sounds_music_tick() below, so loadTileBanks()
 * overwriting it with font data would corrupt mid-song playback (best
 * case: silence; worst case: garbage register writes reading font bytes
 * as if they were music commands). Fixed by reloading MUSIC1.DAT right
 * back into that memory (sounds_music_load(), same call
 * screen_titlepage() already made once) immediately after painting the
 * text -- music_pos/music_wait (sounds.c) are separate small variables,
 * untouched by any of this, so playback resumes from exactly the tick
 * it was on, not from the top of the song. The brief network re-fetch
 * this needs happens once, here, not every frame. */
static const char levelSelectText[] =
  "1@SOUTH@AMERICA" TILES_CRLFCHAR TILES_CRLFCHAR
  "2@EGYPT" TILES_CRLFCHAR TILES_CRLFCHAR
  "3@SCHWARZENDUMPF@CASTLE" TILES_CRLFCHAR TILES_CRLFCHAR
  "4@MISSILE@BASE" TILES_CRLFCHAR TILES_CRLFCHAR
  "5@ENDING" TILES_CRLFCHAR TILES_CRLFCHAR
  "ORIGINAL@CREATOR@SIMON@PHIPPS" TILES_CRLFCHAR
  "XRICK@BY@STEPHAN@GAY" TILES_CRLFCHAR
  "NABU@PORT@BY@GTAMP@AND@CLAUDE" TILES_CRLFCHAR
  "GTAMP.COM/NABU" TILES_CRLFCHAR
  "BASED@ON@RICKTI@BY@TURSI" TILES_CRLFCHAR
  "GITHUB.COM/TURSILION/RICKTI" TILES_CRLFCHAR
  "9918A@GFX@BY@TI99IUC" TILES_CRLFCHAR TILES_CRLFCHAR
  "F@TOGGLE@FRAME@LIMITER" TILES_CRLFCHAR
  "D@TOGGLE@DLSS@5" TILES_CRLFCHAR
  "C@TOGGLE@CHEATS" TILES_NULLCHAR;

/* levelSelectCredits[]/levelSelectCheats[] (the attribution text and the
 * 'C TOGGLE CHEATS' legend that used to paint below the menu above) were
 * REMOVED, on request ("add back the stock TMS9918 compatibility" needed
 * more real headroom than the map/sound trims alone provided, "remove...
 * most of the text from the level select screen"): ~130 bytes of RODATA
 * between the two strings plus their own tiles_paintListAt() call sites.
 * The cheat itself ('c'/'C', main.c's gameplay-loop godKey handling)
 * still works -- only its on-screen legend is gone, along with the S-key
 * stock-sprite toggle's own removal (see that call site's own comment)
 * and SOUND_TRIM_FX staying off, the other two levers this same request
 * weighed against trading away. See hal/sysvid_nabu.h's own VDP_TARGET_
 * F18A_ONLY comment for the full budget account. */

/* Joystick cursor for screen_levelSelect(), on request ("can the level
 * select be controllable with joystick without adding much ram usage").
 * CHANGED, on request ("is there a lives glyth or little guy we can use
 * instead of x"): tile bank 0 -- this screen's own font, already loaded
 * -- turns out to already contain a real little-Rick-figure sprite at
 * index 3 (TILES_RICK, tiles.h): loadDigitTiles() (tiles.c) copies the
 * HUD's own lives-counter icon FROM this exact bank/index, so it's
 * already resident here at zero extra cost -- decoded its actual pattern
 * and color bytes directly (assets/TILEPATA.DAT/TILECOLA.DAT) to
 * confirm it's a real, multi-color humanoid figure, not a blank/
 * degenerate slot (same verification this session's own '='/'>' checks
 * used). Drawn/erased via tiles_paintListAt(), the exact same call this
 * screen already uses for its own text -- no new VDP-writing code, just
 * two 2-byte string constants and two more call sites, so the only real
 * cost is a handful of code bytes plus this function's own two new
 * locals (sel, lastControl -- 3 bytes total). */
static const char levelCursorOn[] = "\3" TILES_NULLCHAR;
static const char levelCursorOff[] = "@" TILES_NULLCHAR;

/* Idle timeout, on request ("make the level select screen go back to
 * title or hall of fame if no activity"): with no key or joystick input
 * for this long, screen_levelSelect() returns FALSE and main() goes back
 * to the title, whose attract cycle then shows the hall of fame. */
#define LEVEL_SELECT_IDLE_FRAMES 1200 /* 20 seconds */

/* TRUE = a level was chosen (screen_introMap_sel), FALSE = idle timeout. */
static U8 screen_levelSelect(void) {
  U8 chosen = FALSE;
  U8 sel = 0;
  U16 lastControl;
  U16 idle = 0;

  sounds_music_hold(); /* title music continues here: no droning during the loads */
  loadTileBanks();
  tiles_setBank(0);
  fb_clear();
  tiles_paintListAt((const U8 *)levelSelectText, 8, 8);
  // BUG FIXED, reported as "skipping the title screen often skips the
  // level select too": lastControl used to start at a hardcoded 0, but
  // whatever joystick FIRE press just skipped screen_titlepage() (this
  // file's own header -- any key/fire there exits it) is often STILL
  // physically held down the instant this screen's own loop starts.
  // pressed = control_status & ~lastControl (this loop's own edge-
  // detect, right below) would then see that already-held FIRE bit as
  // newly pressed on frame 1 (0 has no bits to exclude), instantly
  // choosing whatever level the cursor defaults to before the player
  // ever saw this screen. Poll once and seed lastControl with whatever's
  // ALREADY held right now, so the loop's first real check only reacts
  // to a genuinely new press/release from here on -- same "flush the
  // stale input" precaution this file's own header already uses for the
  // shared keyboard buffer, just for the joystick's own held-state
  // equivalent instead.
  sysevt_poll();
  lastControl = control_status;
  // Column 0 -- one tile-column left of levelSelectText's own x=8 start,
  // so the cursor never overlaps the level number/name it's pointing at.
  // Row spacing matches levelSelectText's own layout (each entry is 2
  // rows apart -- its own title line plus one blank CRLF line).
  tiles_paintListAt((const U8 *)levelCursorOn, 0, 8);
#if SOUND_ENABLED
  sounds_music_load(RES_MUSIC1, MUSIC1_SIZE);
#endif
  sounds_music_resume();

  do {
    U16 pressed;
    U16 keyPressed = 0;

    sysevt_poll();
    sounds_music_tick(MUSIC_RATE_TITLE);
    if (isKeyPressed()) {
      U8 key = getChar();
      idle = 0;
      if (key >= '1' && key <= '5') {
        screen_introMap_sel = key - '1';
        chosen = TRUE;
      } else if (key == 0xE2) {                /* up arrow (press code) */
        keyPressed = CONTROL_UP;
      } else if (key == 0xE3) {                /* down arrow */
        keyPressed = CONTROL_DOWN;
      } else if (key == 0x0D || key == 0xE7) { /* RETURN or YES */
        keyPressed = CONTROL_FIRE;
      }
    }

    // Edge-triggered (only the frame a direction/fire newly becomes
    // pressed), not level-triggered -- control_status stays set for as
    // long as the stick is held, and this screen's own loop runs once
    // per vblank, so reacting to the raw level every iteration would fly through all
    // 5 entries in well under a second while the stick is held. One press
    // now moves exactly one slot, matching ordinary menu conventions.
    // Keyboard keys arrive as one queued event per press, so they're
    // already edge-triggered and just join in.
    pressed = (control_status & ~lastControl) | keyPressed;
    lastControl = control_status;

    if ((pressed & CONTROL_UP) && sel > 0) {
      tiles_paintListAt((const U8 *)levelCursorOff, 0, 8 + sel * 16);
      sel--;
      tiles_paintListAt((const U8 *)levelCursorOn, 0, 8 + sel * 16);
    } else if ((pressed & CONTROL_DOWN) && sel < 4) {
      tiles_paintListAt((const U8 *)levelCursorOff, 0, 8 + sel * 16);
      sel++;
      tiles_paintListAt((const U8 *)levelCursorOn, 0, 8 + sel * 16);
    } else if (pressed & CONTROL_FIRE) {
      screen_introMap_sel = sel;
      chosen = TRUE;
    }
    if (pressed) {
      idle = 0;
    }

    sys_nabu_waitFrame(VBLANK_PERIOD);
    if (++idle >= LEVEL_SELECT_IDLE_FRAMES) {
      return FALSE; /* no activity: back to the title / attract cycle */
    }
  } while (!chosen);
  return TRUE;
}

/* GAME OVER screen -- ported from xrick/src/scr_gameover.c's own
 * screen_gameover() (screen_gameovertxt[]/tiles_setBank(0)/
 * sounds_play(GAMEOVER_SND)/tiles_paintListAt() call shape), on request
 * ("implement the game over code and music"). Shown once env_lives runs
 * out (this file's own STDEAD handling block, further down, mirrors the
 * reference's CTRL_RICK `if (env_trainer || --env_lives) RESTART; else
 * FADEOUT__GAMEOVER;` -- see that call site's own comment) instead of
 * respawning forever, which is what this build did before this existed.
 *
 * GAMEOVER_SND itself is a genuinely new asset for this port (assets/
 * nabu/GAMEOVER.DAT) -- see sounds_playGameover()'s own header (sounds.c)
 * for the conversion pipeline. STREAMED, not resident (same
 * sounds_playFxStreamed() mechanism as BONUS_SND/DIE_SND/etc) -- a game
 * over is as one-shot an event as this build has.
 *
 * ORDERING MATTERS: loadTileBanks()/tiles_setBank(0) must finish (pushing
 * the real font to VRAM) BEFORE sounds_playGameover() starts -- both
 * touch tiles_banks_shared (tiles_setBank()'s bank==0 path reads the font
 * straight out of it; sounds_playFxStreamed() then overwrites that same
 * memory with GAMEOVER.DAT for the whole ~3.9s it plays), the same class
 * of corruption already found and fixed for the in-game HUD (engine/
 * tiles.c's own tiles_setBank() comment). Painting the text BEFORE
 * starting the sound (reference does it the other way around, but its
 * sounds_play() doesn't share this memory) sidesteps it entirely:
 * tiles_paintListAt() only ever writes character codes to VRAM's name
 * table using the pattern data tiles_setBank(0) already pushed there --
 * it never reads tiles_banks_shared itself, so it doesn't matter that the
 * sound clobbers that memory moments later. Same reasoning
 * screen_levelSelect() above already relies on for its own post-paint
 * sounds_music_load() call.
 *
 * Ticks sounds_fx_tick() (not sounds_music_tick(), unlike
 * screen_titlepage()/screen_levelSelect() above) -- GAMEOVER_SND is a
 * one-shot effect in the shared FX slot, not a looping VGM song; nothing
 * re-arms it once it finishes, it just goes quiet and stays quiet until
 * this screen's own wait-for-a-key loop below ends it. */
/* BUG FIXED, reported as "wasn't centered": this used to be
 * "@@@GAME@OVER@@@" (15 chars, padded with 3 blank '@' tiles on each
 * side) -- tiles_paintListAt() has no centering logic of its own, it just
 * starts painting at the (x,y) it's given, so that padding shifted the
 * actual "GAME@OVER" text 3 tile-columns right of wherever the call
 * site's x was actually centered for. Fixed by dropping the padding and
 * centering the real 9-character text directly: this font is a 32-column
 * screen (fb.c's own 768-cell/32-wide comment), (32-9)/2 = 11.5 -> column
 * 11 (fb_at()'s own x/8 integer division rounds the same way), x = 88. */
static const char gameOverText[] =
  "GAME@OVER" TILES_NULLCHAR;

/* This screen's loop does nothing but tick GAMEOVER_SND, so it gets its
 * own period rather than gameplay's game_period (66ms, 4 vblanks):
 * sounds_fx_tick(2) advances 2 of the effect's 16.7ms ticks per call,
 * so 2 vblanks per call plays it at its encoded
 * speed (273 ticks, ~4.6s). With 66ms frames it ran at half speed (~9.1s).
 * Before the vblank frame timing this loop's busy-wait was ~45ms (~6.1s).
 * (ms, rounded up to whole 16.7ms vblanks by sys_nabu_waitFrame()) */
#define GAMEOVER_PERIOD 33

/* End of a game (game over, or ESC), on request: name entry if the score
 * made the top 8, then the hall of fame -- with the title music playing,
 * (re)started from the top here (the level-select screen that follows
 * carries it on). Both screens are engine/scr_hof.c. */
static void hof_afterGame(void) {
#if SOUND_ENABLED
  sounds_music_load(RES_MUSIC1, MUSIC1_SIZE);
#endif
  sounds_music_start();
  if (hof_qualifies()) {
    screen_enterName();
  }
  screen_hallOfFame();
}

static void screen_gameOver(void) {
  U16 lastControl;

  loadTileBanks();
  tiles_setBank(0);
  fb_clear();
  tiles_paintListAt((const U8 *)gameOverText, 88, 96);
  sounds_playGameover();

  /* Same "drain whatever gameplay left queued" precaution as every other
   * wait-loop screen this file enters (this file's own header, several
   * other drains) -- without it, a held direction/fire from the death
   * that got us here could register as this screen's own exit press
   * before the player ever sees GAME OVER. */
  while (isKeyPressed()) {
    getChar();
  }

  /* BUG FIXED, reported as "ESC auto skips the game over": same class of
   * bug, same fix, as screen_levelSelect()'s own lastControl seeding
   * (this file's own comment there) -- `control_status & CONTROL_FIRE`
   * below used to be checked with no edge-detection at all, so a FIRE
   * already held the instant this screen starts (Rick died mid-shot, or
   * ESC's own keypress landing right as the joystick is also held) exited
   * on the very first loop iteration, barely showing this screen at all.
   * Poll once and seed lastControl with whatever's already held, then
   * only react to a NEW press from here on. */
  sysevt_poll();
  lastControl = control_status;

  for (;;) {
    U16 pressed;

    sysevt_poll();
    sounds_fx_tick(2);
    if (isKeyPressed()) {
      getChar();
      break;
    }
    pressed = control_status & ~lastControl;
    lastControl = control_status;
    if (pressed) { /* any new joystick direction or button, like any key */
      break;
    }
    sys_nabu_waitFrame(GAMEOVER_PERIOD);
  }

  /* BUG FIXED, reported as "game over music still plays and gets stuck on
   * notes when skipped": the loop above breaks the instant a key/fire
   * skips this screen, but GAMEOVER_SND (sounds_playGameover() above) is
   * a streamed FX effect with no natural silence guarantee if it's cut
   * off mid-note -- fx_tick() only clears the AY registers on a real
   * natural end (sounds_fx_tick()'s own fx_stop() calls, engine/
   * sounds.c), never on an external skip like this one. Same silence-on-
   * exit call sounds_reset_all() already provides everywhere else this
   * file leaves a sound-playing screen (e.g. right before this function
   * is even called, main()'s own STDEAD block). */
  sounds_reset_all();

  /* Drain again -- whatever exited the loop above (a keyboard key, or a
   * held joystick fire) shouldn't leak into screen_levelSelect()'s own
   * '1'-'5' check right after this returns (goto restart_level_select,
   * this file's own STDEAD handling block) -- same reasoning as the ESC
   * handler's own pre-screen_levelSelect() drain elsewhere in this file. */
  while (isKeyPressed()) {
    getChar();
  }
}

// Loads every asset table this build currently needs -- see the file
// header above. Panics (via sys_nabu_loadAsset(), see hal/sys_nabu_load.h)
// rather than returning an error if any .DAT file is missing or short;
// there's no sensible way to keep running with e.g. half of map_blocks[]
// zeroed out, so a loud halt at boot beats silently corrupted tiles/
// sprites/map data later.
//
// Add a line here alongside the corresponding dat_*.c BSS buffer each
// time another asset gets converted from compiled-in to loaded-at-runtime.
//
// SIZE PASS (later round): the TI-only and F18A-only tile bank loads below
// are now mutually exclusive (branched on sysvid_nabu_hasF18A) -- see
// tiles.h's tiles_banks_shared union comment for why they now share
// memory and must not both be loaded.
/* Split out of loadAssets() (still its only caller at boot) so
 * screen_introMap() can also call it a second time, right after its own
 * wait loop exits -- that screen repurposes tiles_banks_shared for its
 * title/body text for the screen's whole lifetime (see engine/
 * scr_imap.c's map0_title/map0_body comment for the full contract), so
 * the real tile-bank data needs reloading before map_loadFirst() below
 * calls tiles_setBank(1) and expects to find it. */
static void loadTileBanks(void) {
  /* SIZE PASS: tiles_banks_col/pat (TMS9918A-only) and
   * tilesf18_patA/colA/patB/colB (F18A-only) now share the same backing
   * memory (tiles.h's tiles_banks_shared union) since exactly one of the
   * two is ever read in a given run -- see that union's own comment.
   * Loading both sets would silently corrupt one with the other's bytes,
   * since they're now the same bytes -- load only the set the detected
   * hardware will actually use.
   *
   * BUG FIXED: this used to check sysvid_nabu_hasF18A here, which reads as
   * plausible (main() calls vdp_detect() before loadAssets()) but is
   * wrong -- vdp_detect() (hal/f18a_nabu.c) only sets gVdpHardwareType;
   * sysvid_nabu_hasF18A itself isn't derived from that until
   * sysvid_nabu_init() runs (hal/sysvid_nabu.c), which happens AFTER
   * loadAssets(). So this always read sysvid_nabu_hasF18A's global
   * initializer (0/false) here regardless of real hardware, always took
   * the non-F18A branch, and on real F18A hardware left tiles_setBank()'s
   * F18A branch reading TMS9918A-layout data as if it were F18A-layout --
   * exactly the garbled-tiles/wrong-colors symptom this was caught by.
   * gVdpHardwareType is what's actually valid this early; compare it
   * directly instead, same test sysvid_nabu_init() uses.
   *
   * BUG FIXED / SIZE PASS, on request ("time to add level 3"): this used
   * to also load the GAMEPLAY tile bank (patB/colB, or tiles_banks_pat/
   * col's second half) here, from one fixed filename shared by every map
   * -- true only because SAMERICA and EGYPT happen to both use bank 1.
   * CASTLE needs different bytes there. That load moved to engine/
   * maps.c's map_loadMap(), which now streams whichever gameplay bank
   * the CURRENT map needs, fresh, at every map transition -- see that
   * call site and tiles_setBank()'s own header for the full reasoning.
   * This function now loads ONLY the font (slot 0/bank A), which really
   * is fixed for the whole game regardless of map. */
  if (gVdpHardwareType != VDP_TYPE_STOCK) {
    sys_nabu_loadRes(RES_TF18PATA, tilesf18_patA, 0x800);
    sys_nabu_loadRes(RES_TF18COLA, tilesf18_colA, 0x800);
  } else {
    sys_nabu_loadRes(RES_TILECOLA, tiles_banks_col, 0x800);
    sys_nabu_loadRes(RES_TILEPATA, tiles_banks_pat, 0x800);
  }
}

/* 'D' key (was 'V', moved on request): switch between F18A and stock-
 * TMS9918A rendering mid-game, on request ("add v key to switch between
 * F18A/stock (only if F18A is detected)"). Only in a dual-VDP build -- the single-target builds
 * hardwire sysvid_nabu_hasF18A to a constant, so there's nothing to switch.
 *
 * Flipping sysvid_nabu_hasF18A/gVdpHardwareType makes every existing
 * hardware branch follow the new mode; what's already on the chip or in
 * memory from the old mode is redone here, in place (a few network reads,
 * no level restart):
 *   - F18A registers: to F18A, set_halfbitmap() turns ECM3 sprites back
 *     on and reloads the sprite palettes (the tile palette comes with
 *     tiles_setBank() below); to stock, ECM off (VR49=0) and palette 0-15
 *     set to the standard TMS9918A colours the stock art expects. Never
 *     relocked (hal/f18a_nabu.c: relocking turns ECM off).
 *   - Tiles: the font (loadTileBanks()) and the current map's tile set
 *     (map_loadTileSet()) differ per mode; maps_paint() pushes and redraws.
 *   - Sprites: the caches share one buffer differently per mode (sprites.c's
 *     sprites_page0CacheInit()), so all are rebuilt, and every entity's
 *     lastSpriteDrawn is cleared so it re-uploads in the new mode's art. */
#define VDP_MODE_SWITCH (!VDP_TARGET_F18A_ONLY && !VDP_TARGET_9918A_ONLY && SPRITE_ECM_ENABLED)
#if VDP_MODE_SWITCH
static vdp_type_t vdpDetected = VDP_TYPE_STOCK;

/* Standard TMS9918A colours, F18A 12-bit 0RGB. */
static const U16 tms9918_palette[16] = {
  0x0000, 0x0000, 0x02C3, 0x05D6, 0x054F, 0x076F, 0x0D54, 0x04EF,
  0x0F54, 0x0F76, 0x0DC3, 0x0ED6, 0x02B2, 0x0C5C, 0x0CCC, 0x0FFF
};

static void vdp_switchMode(void) {
  U8 i;

  if (vdpDetected == VDP_TYPE_STOCK) {
    return; /* no F18A: nothing to switch to */
  }
  sysvid_nabu_hasF18A = !sysvid_nabu_hasF18A;
  gVdpHardwareType = sysvid_nabu_hasF18A ? vdpDetected : VDP_TYPE_STOCK;

  if (sysvid_nabu_hasF18A) {
    set_halfbitmap();
  } else {
    f18a_unlock();
    f18a_setRegister(49, 0x00); /* ECM off: plain 1-colour sprites */
    f18a_loadPalette(tms9918_palette, 0, 16);
  }

  loadTileBanks();
  map_loadTileSet();
  maps_paint();

  sprites_ecmSlotCacheInvalidate(); /* on stock this also re-carves the stock caches */
  sprites_page0CacheInit();
#if !ECM_ALL_PAGES_ENABLED || !VDP_TARGET_F18A_ONLY
  sprites_extraCacheReset();
#endif
  for (i = 0; i < ENT_ENTSNUM + 1; i++) {
    ent_ents[i].lastSpriteDrawn = 0xff;
  }
}
#endif

static void loadAssets(void) {
  loadTileBanks();
  /* SKIP_SPR0_GAMEPLAY_LOAD (hal/sysvid_nabu.h): nothing reads
   * sprites_data0[] at runtime any more, so its ~6144-byte load is
   * normally skipped. */
#if !SKIP_SPR0_GAMEPLAY_LOAD
  sys_nabu_loadRes(RES_SPR0, sprites_data0, sizeof(sprites_data0));
#endif

  /* assets/SPRxB0/1/2.DAT (F18A 8-color-sprite color-plane data,
   * one triplet per page) are NOT loaded here -- unlike every other
   * asset in this function, they're streamed on demand, one 128-byte
   * frame at a time, right in engine/sprites.c's own sprites_paint() --
   * see that call site's own comment for why (this build has nowhere
   * near enough RAM headroom to keep any of it resident at once). */

  /* map_submaps/map_connect/map_bnums/map_marks/map_blocks are NOT
   * loaded here -- they're per-map (maps.h's own header), reloaded fresh
   * by maps.c's map_loadMap() at boot and at every map transition.
   * map_eflg_c/ent_entdata/ent_sprseq/ent_mvstep ARE loaded here, once:
   * all genuinely shared, whole-game data now (ents.h's own header on
   * why entdata/sprseq/mvstep specifically can't safely rebase per map
   * the way blocks can -- a mark's .ent value encodes real entity-type
   * information via its numeric range, so every map's marks must resolve
   * through the exact same unrebased, shared tables). */
  sys_nabu_loadRes(RES_MAPEFLG, map_eflg_c, sizeof(map_eflg_c));
  sys_nabu_loadRes(RES_ENTDATA, (U8 *)ent_entdata, sizeof(ent_entdata));
  sys_nabu_loadRes(RES_SPRSEQ, (U8 *)ent_sprseq, sizeof(ent_sprseq));
  sys_nabu_loadRes(RES_MVSTEP, (U8 *)ent_mvstep, sizeof(ent_mvstep));

  // Every gameplay sound EFFECT streams on demand now (engine/sounds.c's
  // own sounds_playJump()/Bullet()/Explode()/Walk()/Crawl()/Stick()/etc)
  // -- nothing left to resident-load here at all. JUMP_SND/BULLET_SND/
  // WALK_SND were the last three still resident (SIZE PASS, on request
  // "find enough ram... even if they have to be streamed", once BONUS/
  // DIE/EXPLODE/STICK/PAD/etc had already moved the same way for the
  // same reason -- see sounds.c's own header for the full history and
  // the real tradeoff this accepts: every jump/shot/step now pays a
  // network round-trip before the sound starts, freeing the last
  // 76+82+36 bytes of BSS this function used to load into). See
  // sounds_playJump()/sounds_playBullet()/sounds_playWalk() in
  // engine/sounds.c.
}

/* game.h declares this `extern` for every screen to share -- it needs a
 * real definition
 * somewhere now that engine/scroller.c's scroll_up()/scroll_down() read
 * and temporarily override it (to run the scroll animation faster than
 * normal gameplay, then restore it). Defined here, next to the main loop
 * that's the other reader/writer of it. */
U16 game_period = GAME_PERIOD;

/* Gameplay speed picked with the 'F' key (main loop): the period normal
 * gameplay runs at, which game_period returns to after every scroll
 * (engine/scroller.c). Music has its own periods and is unaffected. */
U16 game_speedPeriod = GAME_PERIOD;

/* Vblanks the last gameplay frame really took (sys_nabu_waitFrame()'s
 * return) -- sound effects advance by this, so they keep real-time speed
 * even with the frame limiter off. */
static U8 lastFrameVblanks = (GAME_PERIOD + 16) / 17;

/* TRUE for the one frame a new submap was loaded -- skip drawing
 * entities until they've had their first ent_action() (main loop). */
static U8 submapJustLoaded = FALSE;

// xrick/src/game.c's own save_map_row, same purpose: the map_frow to
// return to on respawn, updated at every checkpoint (game_save()
// equivalent, below) and read back by the main loop's death check.
static U16 save_map_row;

/*
 *
 * Horizontal (LEFT/RIGHT) and vertical (UP/DOWN) are tracked as two
 * independent axes so a diagonal (e.g. UP+LEFT while climbing, which
 * e_rick.c's own control_status checks assume can combine) works
 * whenever both are genuinely held at once -- true multi-key state now,
 * not dependent on overlapping queued events the way the timeout guess
 * was. */
#define KEY_ARROW_UP_DOWN     0xE2
#define KEY_ARROW_UP_UP       0xF2
#define KEY_ARROW_DOWN_DOWN   0xE3
#define KEY_ARROW_DOWN_UP     0xF3
#define KEY_ARROW_LEFT_DOWN   0xE1
#define KEY_ARROW_LEFT_UP     0xF1
#define KEY_ARROW_RIGHT_DOWN  0xE0
#define KEY_ARROW_RIGHT_UP    0xF0
#define KEY_YES_DOWN          0xE7
#define KEY_YES_UP            0xF7

/* volatile: tried during diagnosis of a real bug where control_status
 * never ended up with a keyboard-set bit (see the main loop's own
 * OR-in call site, further down, for the actual root cause and fix --
 * volatile alone did NOT fix it, the real fix was how that OR-in
 * expression itself is written). Left in place since it's harmless and
 * these are read/written from the main loop every frame regardless. */
static volatile U8 kbd_horiz = 0; /* CONTROL_LEFT, CONTROL_RIGHT, or 0 */
static volatile U8 kbd_vert = 0;  /* CONTROL_UP, CONTROL_DOWN, or 0 */
static volatile U8 kbd_fire = 0;  /* CONTROL_FIRE or 0 */



// BUG FIXED, not in the reference (which has no submap concept to save):
// the death-respawn check below restores Rick's x/y/map_frow to the last
// checkpoint but never touched env_submap, which stayed wherever
// map_chain() had last carried Rick -- checkpoint coordinates from a
// DIFFERENT submap's layout, landing Rick inside solid terrain on
// whichever submap he actually died in (anywhere past the very first
// one). Tracked the same way save_map_row already is.
static U16 save_submap;

void main(void) {
  vdp_clearVRAM();
  // Run VDP hardware detection before touching NABU-LIB/VDP init (see
  // vdp_detect()'s comment in hal/f18a_nabu.c for why the ordering
  // matters). sysvid_nabu_init() below reads gVdpHardwareType to pick its
  // init path and set sysvid_nabu_hasF18A.
  //
  // SKIPPED, on request ("VDP detection also needs to be skipped when
  // VDP_TARGET_9918A_ONLY=1"): this build already knows the answer at
  // compile time (sysvid_nabu_hasF18A is a `0` macro, not a runtime
  // variable -- hal/sysvid_nabu.h's own comment), so probing real
  // hardware for it is pure wasted boot-time work, not just wasted
  // image bytes -- vdp_detect() itself is #if'd out entirely under this
  // flag (hal/f18a_nabu.c), matching every other F18A-only primitive.
  // gVdpHardwareType stays at its real default (VDP_TYPE_STOCK, hal/
  // f18a_nabu.c's own initializer), which is already the correct
  // answer for every other reader of it (this file's own loadAssets()
  // check, sysvid_nabu_init()'s now-#if'd-out assignment).
#if !VDP_TARGET_9918A_ONLY
  gVdpHardwareType = vdp_detect();
#endif
#if VDP_MODE_SWITCH
  vdpDetected = gVdpHardwareType; /* the 'D' key's own permission check */
#endif

  initNABULib();

  // Stop with a message if RICK.DAT/RICK.SPR are missing or out of date
  // (hal/sys_nabu_load.c) -- before sysvid_nabu_init(), since the message
  // uses plain Graphics II mode.
  sys_nabu_checkFiles();

  // REORDERED: sysvid_nabu_init() now runs before loadAssets(), not after
  // -- it only sets up VDP mode/registers and the gImage/gColor/gPattern/
  // gSprite/gSpritePat addresses (hal/sysvid_nabu.c), none of which reads
  // any loaded asset data, so the ordering was never load-bearing for it.
  // Moved so screen_titlepage() below (which needs gImage/gPattern/gColor
  // already set) can run before loadAssets() populates sprites_data0 with
  // real sprite data -- see that function's own header for why the
  // ordering specifically matters there.
  sysvid_nabu_init();

  // On request: the whole title->select->intro sequence is a loop now,
  // not a straight line -- reaching gameplay used to be the only way
  // out, so finishing EPILOGUE's own "ENDING" intro (screen_introMap_sel
  // == 4, no real map behind it -- see map_loadMap()'s own call site
  // below) fell through to SAMERICA's gameplay same as any other
  // still-unported selection, which reads as "the ending starts a new
  // game" -- confusing, since ENDING is conceptually never a real level
  // (the reference's own data has no submaps for it at all, unlike
  // CASTLE/MBASE which will eventually get real ones). Selecting ENDING
  // and finishing its text now loops back to the title screen instead
  // -- see the `continue` at the bottom of this loop.
  //
  // restart_title: (BUG FIXED, on request -- "progression from 4 to
  // ending didn't work"): the gameplay loop far below also jumps back up
  // here once Rick walks off MBASE's own last submap (env_map+1 ==
  // MAP_NBR_MAPS, no fifth real map to advance into) -- it shows the same
  // ENDING intro first, then lands here via goto instead of `continue`
  // since that trigger fires from a whole separate loop nested after this
  // one exits (`break` below), not from inside this loop itself. Restarts
  // the entire title->select->intro sequence from scratch (screen_
  // titlepage() first), same as a fresh boot -- xrick's own game_run()
  // returns to its GAMEOVER/attract-mode state the same way once a run
  // ends, and this build has no separate attract/hall-of-fame screen to
  // land on instead (see this file's own header on what's not ported).
restart_title:
  for (;;) {
  // On request: screen_titlepage() returning FALSE means it was left by
  // a plain fire press without ever choosing a level via '1'-'5' --
  // screen_levelSelect() (this file's own header) picks up from there,
  // same splash/music already running, until a real digit choice is
  // made. Either way, sounds_music_stop() only needs to run once, here,
  // after whichever screen actually made the choice.
  if (!screen_titlepage()) {
restart_level_select:
    if (!screen_levelSelect()) {
      continue; /* idle timeout: back to the title (and its attract cycle) */
    }
  }
  sounds_music_stop();

  // BUG FIXED, reported as "the intro screen gets skipped too easily with
  // the previous button press": whatever press exits screen_titlepage()'s
  // own wait loop (real joystick FIRE, via control_status -- see hal/
  // sysevt_nabu.c) also leaves a queued NABU-LIB keyboard-buffer event
  // behind, confirmed by testing rather than obvious from source alone --
  // screen_titlepage() itself never touches the keyboard buffer at all.
  // engine/scr_imap.c's screen_introMap() checks isKeyPressed() (that same
  // keyboard buffer, not the joystick) to decide when to skip itself, so
  // that stale event was immediately visible the instant the intro screen
  // started, skipping it almost before it could show. Drain it here so
  // the intro only ever reacts to a genuinely new press.
  //
  // BUG FIXED, on request: this used to also flip env_invicible on here
  // if the key draining the title screen's own stale keypress was 'g' --
  // a title-screen god-mode shortcut. Removed; the in-game '1' toggle
  // further down (see that check's own comment) is now the only way to
  // turn it on. The '1'-'4' map-intro selection also moved out of here --
  // screen_titlepage() itself now checks for those every iteration of its
  // own wait loop (see that function's own comment) so a digit press
  // exits immediately instead of needing a separate fire-press too; this
  // loop is back to a pure drain, just for whatever residual keyboard-
  // buffer event the joystick-fire press that exits that loop queues
  // (see this comment block's own header on why that happens).
  while (isKeyPressed()) {
    getChar();
  }

  // Load RetroNET-backed asset data before anything touches it. Deliberately
  // done right after initNABULib() (HCCA needs its interrupt-driven state
  // set up first, see hal/RetroNET-FileStore.h) and before
  // sprites_clear()/map_loadFirst() -- all of tiles.c,
  // sprites.c, ents.c, and maps.c below assume these buffers
  // are already populated by the time they run.
  loadAssets();

  // One-time hardware init: hide all 32 real TMS9918A sprite slots (see
  // sprites_clear()'s own comment on why this can't just be left to
  // whatever the VDP RAM-clear step happened to leave behind). Needs to
  // run after sysvid_nabu_init() has set gSprite, and before
  // map_loadFirst() below ever calls drawsprite().
  sprites_clear();

  // One-time init for the stock page0 rolling cache (engine/sprites.c's
  // own page0_cache, engine/include/ecm_cache.h) -- UNCONDITIONAL: needed
  // on every build, ECM or not (sprites_page0CacheInit()'s own comment).
  // Only needs to run ONCE: SPR0.DAT's own content isn't per-map, so nothing
  // ever needs to invalidate this cache again after boot -- unlike
  // sprites_extraCacheReset() (engine/maps.c's map_loadMap(), every map
  // transition), whose own tag space IS per-map. Placed here rather than
  // inside sprites_clear() itself specifically so it does NOT re-run at
  // that function's other call sites (engine/scr_imap.c's own screen
  // transition) -- those would just needlessly discard an already-warm
  // cache.
  sprites_page0CacheInit();

  // One-time init for the ECM slot rolling cache (engine/sprites.c's own
  // ecm_slot_cache) -- SPRITE_ECM_ENABLED-only, same "run once at boot"
  // reasoning as sprites_page0CacheInit() just above, split back into its
  // own call (was one merged call, sprites_graphicsCacheInit(), briefly)
  // once ECM planes moved to their own interleaved-block cache with a
  // different slot size than page0's (engine/include/sprites.h's own
  // SPRITE_PLANES_SIZE comment has the full "why split again" writeup).
#if SPRITE_ECM_ENABLED
  sprites_ecmSlotCacheInit();
#endif

  // New-game starting counts (xrick/src/game.c's own NEW_GAME/restart()
  // states both set these three the same way -- env.h's other env_*
  // globals, score included, are already correctly zero-initialized).
  // Reset again at each new game (after map_loadMap() below).
  env_lives = 6;
  env_bombs = 6;
  env_bullets = 6;

  // God mode: env_invicible already does exactly what the real trainer/
  // cheat mode does -- e_rick_gozombie() (e_rick.c) returns immediately
  // whenever it's set, and u_envtest() (util.c) clears any MAP_EFLG_LETHAL
  // bit it finds. Used to be hardwired TRUE/FALSE here as a source-edit-
  // and-rebuild testing aid with no real toggle; the main loop's own '1'
  // keypress check (further down) is the only way to turn it on now --
  // nothing to set here.

  // BUG FIXED, reported as "the intro is skipped sometimes even though I
  // don't push anything, especially in MAME stock VDP": the drain right
  // after screen_titlepage() above only covers whatever queued keyboard-
  // buffer event was left by the press that exited THAT screen -- it
  // does not cover anything that arrives during loadAssets() below,
  // which streams a dozen-plus files over the network adapter and can
  // take a real, variable amount of time (longer/less predictable under
  // some emulators/VDP configs than others, matching "especially in MAME
  // stock VDP"). NABU-LIB's own header describes isKeyPressed()/
  // getChar() as reading from "the keyboard, which is also the
  // joysticks" -- the same shared buffer screen_introMap()'s own
  // isKeyPressed() skip check reads from (engine/scr_imap.c) -- so any
  // joystick activity (or idle-state noise from one, under some
  // emulators) during that whole loading window could queue an event
  // that reads as "skip" the instant the intro screen's first frame
  // checks, before the player had any real chance to react. Draining
  // again right here, after loadAssets() and everything else that runs
  // before the intro screen's own loop starts, closes that gap the same
  // way the first drain closes the title-screen one.
  while (isKeyPressed()) {
    getChar();
  }

  // Map intro: title/body text and the animated border, engine/
  // scr_imap.c's screen_introMap() -- back in this build now. It repurposes
  // tiles_banks_shared (loaded by loadTileBanks() above) for its own
  // title/body text for as long as this loop runs -- see that file's
  // map0_title/map0_body comment for the full contract -- so
  // loadTileBanks() runs again right after, to put real tile-bank data
  // back before map_loadFirst() below calls tiles_setBank(1) and expects
  // to find it.
  //
  // DEFENSIVE, on request after live MAME-debugger evidence: something
  // during real gameplay corrupts scr_imap.c's own static `seq` to a
  // stray nonzero value screen_introMap()'s switch doesn't recognize
  // (read as 3 in one session, 1 in another -- confirmed already wrong on
  // the level-select screen, before any level was even picked, so the
  // corruption isn't in this ESC/restart path itself). An unrecognized
  // seq makes screen_introMap() spin forever since nothing in its switch
  // ever corrects it -- see screens.h's own screen_introMap_reset()
  // comment for the full story and the live-patch test that confirmed
  // this fixes it. Every other call site below gets the same guard,
  // since the real corruption source is still unknown and this project's
  // own testing found the same symptom on ordinary level-to-level
  // progression too, not just this ESC/restart path.
  screen_introMap_reset();
  while (screen_introMap() != SCREEN_DONE);
  // screen_introMap() itself now hides the walking-Rick sprite it drew
  // (sprites_clear(), its own seq==30 branch) the instant it ends -- see
  // that call site's own comment for why that has to happen there and
  // not here, after loadTileBanks()'s network round-trip below.
  //
  // TRIED AND REVERTED, on request ("maybe turn off the screen during
  // loading"): briefly blanked the real display here for the whole
  // loadTileBanks()/map_loadMap() stretch (hal/sysvid_nabu.c's
  // sysvid_setGamma()) to paper over the leftover-sprite-into-gameplay
  // artifact below. Reverted -- the blank-screen flash read as more
  // jarring than the artifact it covered, and every byte mattered this
  // close to the CRT_ORG_CODE budget for a change
  // that was, per its own report, only a partial fix anyway.
  //
  // CHANGED, on request ("add a vdpclear call between intro and
  // gameplay"): a real fix for the same artifact, not another attempt to
  // paper over it -- vdp_clearVRAM() wipes pattern/color/name tables and
  // the sprite attribute table outright (same call this file's ESC
  // handler and screen_gameOver() transition already use for the exact
  // same "leftover graphics from the previous screen" class of bug), so
  // whatever the intro's own scenery left behind is actually gone before
  // loadTileBanks()'s network round-trip below, instead of just being
  // hidden behind a dimmed screen for that stretch. tiles_setBank(0xff)
  // right after is required, not optional -- vdp_clearVRAM() is a direct
  // VRAM write that completely bypasses tiles_setBank()'s own "skip if
  // already this bank" cache, so without invalidating it here, whatever
  // tiles_setBank() call gameplay setup makes next could see the same
  // bank number already cached and short-circuit, leaving the now-blanked
  // VRAM without real pattern/color data -- same bug class (and same fix)
  // as this file's own ESC handler comment already documents in full.
  vdp_clearVRAM();
  tiles_setBank(0xff);
  loadTileBanks();

  // ENDING (screen_introMap_sel == 4, EPILOGUE) never has real gameplay
  // behind it -- loop back to the title screen instead of falling
  // through to map_loadMap() below (see this loop's own header for why).
  // BUG FIXED: `for(;;)` only exits via an explicit break -- simply not
  // taking the `continue` branch does NOT fall out of the loop on its
  // own, it just re-enters at the top, so the non-ENDING case needs its
  // own explicit break here too, not just the absence of a continue.
  //
  // BUG FIXED, reported as "wire the ending to show game over too...
  // didn't work... must be some earlier code in ENDING intro that is
  // jumping directly to title -- I've been testing by launching the
  // ENDING from the level select": this is exactly why -- level-select's
  // own "5 ENDING" preview reaches this `continue` here, a COMPLETELY
  // SEPARATE code path from the real "walk off MBASE's last submap"
  // ending (the e_rick_atExit-triggered branch further down this file).
  // Same leftover-key drain fix applied here too (this file's own
  // show_game_over_then_level_select comment below has the full
  // explanation of why screen_introMap()'s own skip check leaves a key
  // queued).
  while (isKeyPressed()) {
    getChar();
  }
  if (screen_introMap_sel == 4) {
    goto show_game_over_then_level_select;
  }
  break;
  }
  goto after_game_over_helper;

show_game_over_then_level_select:
  /* Shared by every "show the ending's game-over screen" call site in
   * this file (level-select's own "5 ENDING" preview just above, the
   * real walk-off-MBASE's-last-submap ending further down, and
   * STDEAD/death further down still) -- on request ("jump to the same
   * game over code as usual and not duplicate as much code"). Also
   * changes where the real-ending and preview paths land afterward: they
   * used to `goto restart_title` (straight back to the title screen,
   * which then hit the exact same leftover-key-skips-it-instantly bug
   * class all over again) -- now goes to restart_level_select instead,
   * same as every other way of reaching this screen (ESC, dying with no
   * lives left), on request ("I think I want it to return to level
   * select just like skipping game over normally does").
   *
   * BUG FIXED, reported as "music is a bit broken depending on what note
   * was playing last" going from game over to level select:
   * screen_levelSelect() deliberately never calls sounds_music_start()
   * itself (screen_titlepage()'s own comment on that same restraint --
   * needed so its OWN fire-press-skip path can continue MUSIC1 seamlessly
   * mid-song instead of restarting it). screen_gameOver() never ticks
   * music_vgm at all -- GAMEOVER_SND plays through the separate FX-slot
   * player (sounds_fx_tick(), not sounds_music_tick()) -- so music_pos/
   * music_wait were left wherever the LAST real music_vgm playback
   * stopped: some earlier map's own intro theme (engine/scr_imap.c's
   * INTRMUS1-5.DAT, a different size than MUSIC1_SIZE every time). Reading
   * MUSIC1.DAT from that stale, wrong-sized offset explains "depends on
   * what note was playing last" exactly -- same root cause, same fix, as
   * the ESC handler's own "PSG reg address > 0x1f" bug (that call site's
   * own comment has the full account) -- just never applied here since
   * this path didn't exist yet when that fix was written. */
  vdp_clearVRAM();
  tiles_setBank(0xff);
  sounds_reset_all();
  screen_gameOver();
  /* Hall of fame, on request: name entry for a top-8 score, then the
   * table, with the title music -- hof_afterGame(). It (re)starts the
   * music, which also covers the music_pos fix above. */
  hof_afterGame();
  goto restart_level_select;

after_game_over_helper:

  // Load and paint whichever map the title screen's own '1'-'5' key
  // check (screen_titlepage(), see that function's own comment) just
  // showed the intro for -- '1'-'5' are real intro/level-select keys
  // now, not just a preview: screen_introMap_sel becomes the map that
  // actually loads next, same as if the player had played through to
  // reach it normally. All four real maps (MAP_NBR_MAPS -- SAMERICA/
  // EGYPT/CASTLE/MBASE, maps.h) can be selected this way now; only
  // EPILOGUE/ENDING (sel==4) falls outside MAP_NBR_MAPS, and that never
  // reaches here at all -- see the `continue` above.
  // See maps.c's own comment on map_loadMap() for exactly what loading a
  // map does and doesn't set up (vertical scrolling is real now, see the
  // main loop below and engine/scroller.c's header; the status bar is
  // real now too, see env_paintGame() below).
  map_loadMap((screen_introMap_sel < MAP_NBR_MAPS) ? screen_introMap_sel : 0);

  // BUG FIXED, reported as "finished the last level, then went to level 1
  // and it started me on the wrong submap": walking off MBASE's last
  // submap sets e_rick_atExit, and the ending's goto (the e_rick_atExit
  // branch in the main loop below) jumps out before that branch's own
  // `e_rick_atExit = FALSE` -- so the next game's very first frame still
  // saw it set, ran map_chain() and moved Rick straight into the next
  // submap. Every new game starts here, so clear it here.
  e_rick_atExit = FALSE;

  // BUG FIXED, reported as "Rick's score needs to be reset on restart":
  // the new-game counts below were only ever set once at boot, so a game
  // started from level select (after game over or ESC) carried the last
  // game's score -- and lives/ammo -- over. Reset here, where every new
  // game starts (moving on to the next level doesn't come through here,
  // so the score still carries across levels within one game).
  env_score_hi = 0;
  env_score_lo = 0;
  env_lives = 6;
  env_bombs = 6;
  env_bullets = 6;

  // F18A 8-color sprite mode, gameplay only -- xrick/src/game.c's own
  // INIT_MAP calls set_halfbitmap() at this exact equivalent point (right
  // after the map loads, before the main loop starts drawing entities).
  // FIXED, on request ("add support for the TI-99's version multicolor
  // F18A sprites") -- see hal/sysvid_nabu.h's SPRITE_ECM_ENABLED comment
  // and engine/sprites.c's own SPRITE_ECM_PAL_RICKWALK comment for the
  // full root-cause writeup (register 6 was never being set). Needs real
  // hardware/MAME confirmation (set_halfbitmap() itself has no
  // definition while SPRITE_ECM_ENABLED is 0 -- gate the call the same
  // way, not just the function body, or this won't link).
#if SPRITE_ECM_ENABLED
  if (sysvid_nabu_hasF18A) {
    set_halfbitmap();
  }
#endif

  // Respawn checkpoint -- xrick/src/game.c's game_save(), called once at
  // INIT_SUBMAP time in the original. Establishes what the main loop's
  // death check below (e_rick_restore()/map_frow=save_map_row) returns
  // Rick to. e_rick_save() itself needed no changes -- see e_rick.c's own
  // comment on why it was dormant until now.
  e_rick_save();
  save_map_row = map_frow;
  save_submap = env_submap;

  // Per-frame loop: poll the joystick, run every active entity's action
  // function once (ent_actf[]), then push updated sprite positions to the
  // VDP.
  //
  // Pacing: sys_nabu_waitFrame(game_period) at the bottom of the loop
  // waits for a vblank-synced frame deadline (GAME_PERIOD 50ms -> 3
  // vblanks, 20fps) -- the frame's own work counts toward it instead of
  // being added on top, see hal/sys_nabu.c's own comment. Reads the *variable* now
  // (not the GAME_PERIOD constant directly) so engine/scroller.c's
  // temporary SCROLL_PERIOD override actually speeds up the ticks between
  // its own animation steps below, same as it would in xrick's original
  // game_run() loop.
  for (;;) {
    sysevt_poll(); /* joystick -> control_status, see hal/sysevt_nabu.c */

    // God mode toggle, checked every frame so it can flip on/off mid-run.
    // BUG FIXED, on request: key changed from 'g' to '1' (and the
    // title-screen 'g' shortcut removed entirely, see loadAssets()'s own
    // caller further up) -- this in-game check is now the only way to
    // toggle it.
    //
    // CHANGED, on request ("add keyboard support"): while, not if -- see
    // kbd_horiz's own header above (right before the main loop) for the
    // whole keyboard-movement scheme this now also handles. A single `if`
    // only ever consumed one queued key per frame; with arrow-key events
    // potentially queuing every frame a direction is held (same "shared
    // with the joystick" buffer this file's own header already warns
    // about elsewhere), that let a backlog build up and made movement lag
    // behind real presses. Draining the whole queue each frame instead
    // keeps it caught up; '1'/ESC handling below is unchanged other than
    // now being reachable from inside this loop instead of a single check.
    while (isKeyPressed()) {
      U8 godKey = getChar();
      if (godKey == 'c' || godKey == 'C') {
        // CHANGED, on request ("Turn godmode into WAAA MODE... unlimited
        // ammo, bombs, lives and invincibility... use w key instead of
        // 1"): one combined cheat now, replacing the separate '1'
        // (invincible-only) key -- env_trainer and env_invicible toggle
        // together in lockstep instead of two independent flags, so
        // there's only ever one on/off state to reason about. Lives/
        // bombs/bullets refill to 6 on every toggle (matching the
        // reference's own game_toggleCheat(1) "trainer" behavior,
        // xrick/src/game.c) -- "unlimited lives" itself doesn't need its
        // own separate flag or code path: e_rick_gozombie() (e_rick.c)
        // already returns immediately whenever env_invicible is set, so
        // E_RICK_STDEAD (and the life-losing STDEAD handling block
        // further down) can never even trigger while this is on.
        //
        // CHANGED AGAIN, on request ("Change W to C for cheats"): moved
        // off 'w'/'W' onto 'c'/'C' -- key itself, not the cheat's own
        // behavior above.
        env_invicible = !env_invicible;
        env_trainer = env_invicible;
        env_lives = 6;
        env_bombs = 6;
        env_bullets = 6;
        sounds_playWaaaaa(); // on request: WAAAAA.PCM, see sounds.c
        /* BUG FIXED, reported as "PCM playback crashes/goes on forever":
         * this whole block sits inside a `while (isKeyPressed())` that
         * drains the ENTIRE queued keyboard buffer every frame (this
         * loop's own header comment on why "while, not if"), and
         * sounds_playWaaaaa() is a hard ~0.7s CPU-blocking busy-wait
         * (that function's own header) -- keyboard auto-repeat can queue
         * several 'w' events from a single held press, and NABU's
         * keyboard buffer keeps filling from more auto-repeat WHILE this
         * function blocks, so the very next iteration of this same
         * drain loop often found another 'w' waiting and played the
         * whole clip again, and again, for as long as the key stayed
         * down -- not an infinite loop, just several real seconds of
         * back-to-back replays that looked and felt exactly like a
         * hang. Drain whatever queued up before or during that playback
         * right here, same "flush stale input" precaution this file
         * uses at every other wait-loop transition, so one physical
         * keypress can only ever trigger one playback. */
        while (isKeyPressed()) {
          getChar();
        }
        // REMOVED, on request ("add back the stock TMS9918 compatibility"
        // needed more headroom than the map/sound trims alone provided,
        // "remove the S key code"): this used to be a real, working cheat
        // ('s'/'S') that toggled every sprite between its real F18A ECM
        // color and a flat stock-white look, skipping the two color-bit-
        // plane HCCA fetches per sprite change when off (a real, measured
        // speed win -- see sprites_paint()'s own comment, sprites.h git
        // history if this needs reviving). Traded for real budget margin
        // instead of trading away gameplay sound effects (SOUND_TRIM_FX)
        // or stock-hardware support itself -- see hal/sysvid_nabu.h's own
        // VDP_TARGET_F18A_ONLY comment for the full account of what else
        // was tried first.
#if VDP_MODE_SWITCH
      } else if (godKey == 'd' || godKey == 'D') {
        vdp_switchMode(); /* F18A <-> stock rendering, see its own comment */
#endif
      } else if (godKey == 'f' || godKey == 'F') {
        // Frame limiter toggle, on request ("make the F key disable the
        // frame limiter completely so I can see max speed"): switches
        // between the normal GAME_PERIOD (50ms, 20fps) and 0 -- no wait
        // at all, each frame as fast as the game can run it. Only
        // gameplay's period changes -- title/intro/game-over music have
        // their own (MUSIC_RATE_xx etc.), and sound effects follow the
        // vblanks each frame really took (lastFrameVblanks), so neither
        // speeds up. Stays set across scrolls and levels
        // (game_speedPeriod's own comment).
        game_speedPeriod = game_speedPeriod ? 0 : GAME_PERIOD;
        game_period = game_speedPeriod;
      } else if (godKey == 0x1B) {
        // ESC, on request: bail out of gameplay straight back to the
        // level-select screen (restart_level_select:, main()'s own
        // title/select loop above) -- same goto-based jump the MBASE->
        // ENDING transition already uses to reach that outer loop from
        // this one (see restart_title's own comment for why a goto, not
        // a `continue`, is what's needed here). Skips screen_titlepage()
        // deliberately, straight to the level list, since the player
        // already knows they want a different level, not the splash.
        //
        // BUG FIXED, reported as "leaves sprites on screen": unlike every
        // other exit from gameplay (map advance, death, MBASE->ENDING),
        // this jump skipped the real-VDP sprites_clear() call outright --
        // screen_levelSelect() itself never touches the sprite list, so
        // whatever was on screen (Rick, enemies) just kept showing right
        // through the level-select text until a level was actually
        // chosen and the ordinary boot-path sprites_clear() finally ran,
        // much later. Hide them the instant ESC is pressed instead.
        //
        // BUG FIXED, reported as "PSG reg address > 0x1f" on selecting a
        // level afterward: screen_levelSelect() calls sounds_music_load()
        // but deliberately never calls sounds_music_start() -- by design,
        // for the ONE path it was written for (screen_titlepage()'s own
        // fire-press skip), where MUSIC1 is already the actively-playing
        // song and music_pos/music_wait are already mid-stream in valid,
        // continuously-advancing positions ("same splash/music already
        // running", that call site's own comment). Gameplay never ticks
        // music at all, so jumping here from mid-gameplay instead left
        // music_pos wherever the last map-intro theme's own (smaller,
        // differently-encoded) playback stopped -- sounds_music_tick()
        // then started reading MUSIC1.DAT from that same stale byte
        // offset, almost certainly not a real tuple boundary in MUSIC1's
        // own data, misreading whatever garbage was there as a register
        // number. Resetting playback here (not inside screen_
        // levelSelect() itself, which would also reset it for the fire-
        // press path and break that path's own intentional seamless
        // continuation) fixes it for this path specifically.
        //
        // BUG FIXED, reported as "freezes on the level-select screen
        // after picking a level": this is the one place in the whole
        // file that enters a wait-loop screen without first draining
        // isKeyPressed()/getChar() -- every other one (screen_titlepage()
        // ->screen_introMap(), the map-advance transition, and
        // screen_levelSelect() itself being entered normally) explicitly
        // does this first, precisely because that buffer is shared with
        // the joystick (this file's own header, several other drains
        // above) -- whatever was queued from active gameplay the instant
        // ESC was pressed (held-direction joystick activity, not just
        // the ESC keypress itself) was sitting there for
        // screen_levelSelect()'s own loop to read BEFORE any real digit
        // press, and apparently never actually cleared enough for a
        // real '1'-'5' press to land as anything screen_levelSelect()'s
        // own `key >= '1' && key <= '5'` check would ever match.
        while (isKeyPressed()) {
          getChar();
        }
        // CHANGED, on request: vdp_clearVRAM() (hal/NABU-LIB.c) wipes
        // all 16KB of VRAM outright -- pattern/color/name tables and the
        // sprite attribute table included -- rather than just the 32
        // hardware sprite slots sprites_clear() targets, so it also
        // covers this same "leaves sprites on screen" bug for any stale
        // tile/pattern content, not sprites alone.
        //
        // This is a direct VRAM write that completely bypasses
        // tiles_setBank()'s own "skip if already this bank" cache
        // (tiles.c's static lastSetBank) -- same class of bug as main.c's
        // title picture blit (title_draw()'s own tiles_setBank(0xff)
        // comment). Invalidating here the same way so screen_gameOver()'s
        // own tiles_setBank(0) call (right below) can't short-circuit and
        // leave the now-blanked VRAM without real pattern/color data.
        vdp_clearVRAM();
        tiles_setBank(0xff);

        // REVERTED, on request ("make ESC just return to level select
        // without game over"): this used to route through
        // show_game_over_then_level_select (main()'s title/select loop)
        // so ESC would show the same GAME OVER screen/music a real
        // run-ending death does -- back to a straight, silent jump to
        // level select. Still need two of that shared label's steps done
        // here directly, not just its final goto restart_level_select,
        // since screen_levelSelect() itself deliberately performs
        // neither (see that function's own restraint, and this block's
        // still-true "PSG reg address > 0x1f" comment above for why
        // sounds_music_start() specifically has to happen on THIS path):
        // sounds_reset_all() stops whatever FX/PCM/music was mid-
        // playback when ESC was pressed, and sounds_music_start() resets
        // music_pos/music_wait to a fresh, valid MUSIC1.DAT offset so
        // level-select's own music_vgm ticking doesn't pick up wherever
        // the last map-intro theme happened to leave it.
        sounds_reset_all();
        // CHANGED, on request ("make the ESC key quit also go to high
        // score entry or HOF and play the music on those screens"): name
        // entry if the score made the top 8, then the hall of fame, with
        // the title music -- hof_afterGame() also (re)starts that music
        // from the top, which covers the stale-music_pos fix above.
        hof_afterGame();
        goto restart_level_select;
      } else if (godKey == KEY_ARROW_UP_DOWN) {
        kbd_vert = CONTROL_UP;
      } else if (godKey == KEY_ARROW_UP_UP) {
        if (kbd_vert == CONTROL_UP) kbd_vert = 0;
      } else if (godKey == KEY_ARROW_DOWN_DOWN) {
        kbd_vert = CONTROL_DOWN;
      } else if (godKey == KEY_ARROW_DOWN_UP) {
        if (kbd_vert == CONTROL_DOWN) kbd_vert = 0;
      } else if (godKey == KEY_ARROW_LEFT_DOWN) {
        kbd_horiz = CONTROL_LEFT;
      } else if (godKey == KEY_ARROW_LEFT_UP) {
        if (kbd_horiz == CONTROL_LEFT) kbd_horiz = 0;
      } else if (godKey == KEY_ARROW_RIGHT_DOWN) {
        kbd_horiz = CONTROL_RIGHT;
      } else if (godKey == KEY_ARROW_RIGHT_UP) {
        if (kbd_horiz == CONTROL_RIGHT) kbd_horiz = 0;
      } else if (godKey == KEY_YES_DOWN) {
        kbd_fire = CONTROL_FIRE;
      } else if (godKey == KEY_YES_UP) {
        kbd_fire = 0;
      }
    }

    // OR'd into control_status (already set from the joystick by
    // sysevt_poll() above, not overwritten) so keyboard and joystick
    // input combine rather than one replacing the other -- see
    // kbd_horiz's own header (right before this loop's own declaration,
    // above) for the real press/release tracking this reads.
    //
    // BUG FIXED, on request ("nothing now" -- control_status never
    // showed a keyboard-set bit, even with kbd_vert directly confirmed
    // nonzero via the debugger/IA console at that exact instant):
    // OR-ing the kbd_horiz/kbd_vert/kbd_fire variables' own values
    // directly into control_status -- as one compound `control_status
    // |= kbd_horiz | kbd_vert | kbd_fire;`, and separately as a chain of
    // `new_status = new_status | kbd_X;` reassignments to a local U16 --
    // both silently produced 0x00 regardless of the variables' real
    // values, confirmed via a live IA-console byte dump
    // (C:\nabu\Console.txt) that isolated the OR computation itself as
    // wrong, not the assignment afterward. Root cause not fully
    // identified (a real SDCC miscompilation of this expression shape
    // is the leading theory, but unconfirmed), but this form -- compare
    // each kbd_* variable against its own known constant, then OR in
    // that literal constant rather than the variable's value -- fixed
    // it, confirmed working by direct playtest. If this class of bug
    // resurfaces elsewhere, this exact working pattern is the
    // established workaround.
    if (kbd_horiz == CONTROL_LEFT) control_status = control_status | CONTROL_LEFT;
    if (kbd_horiz == CONTROL_RIGHT) control_status = control_status | CONTROL_RIGHT;
    if (kbd_vert == CONTROL_UP) control_status = control_status | CONTROL_UP;
    if (kbd_vert == CONTROL_DOWN) control_status = control_status | CONTROL_DOWN;
    if (kbd_fire == CONTROL_FIRE) control_status = control_status | CONTROL_FIRE;

    ent_clprev();
    ent_action(); /* runs e_rick_action() for Rick's slot; see file header
                   * for why every other entity's action is still a no-op */
    sys_nabu_pollVblank(); /* see hal/sys_nabu.c's frame-pacing comment */

    /* CTRL_RICK's NEXT_SUBMAP case (xrick/src/game.c) equivalent: e_rick.c
     * sets e_rick_atExit when Rick walks off either horizontal edge of the
     * current submap (see that file's own header) -- map_chain() (maps.c)
     * picks the matching connector for game_dir/Rick's row and reports
     * whether it found a real next submap. TRUE: re-run the same
     * map_init()/paint sequence map_loadFirst() and the death/respawn
     * branch above already use, so the new submap's terrain/entities
     * actually appear. FALSE ("no next submap - request next map"): the
     * next map's intro and load (game.c's NEXT_MAP state), or the ending
     * after MBASE -- see the else branch below.
     *
     * game_save() equivalent here now (BUG FIXED, was NOT ported -- see
     * save_submap's own comment above): makes this new submap the respawn
     * checkpoint too, so dying restores to wherever Rick last entered a
     * submap, not always the very start of the level regardless of how
     * far map_chain() has carried him.
     *
     * BUG FIXED, reported as "the sprite flickers moving from submap 0 to
     * 1": this whole block used to run AFTER the per-frame ents_paintAll()
     * below, so on the very frame e_rick_atExit fired, that earlier
     * ents_paintAll() call already painted Rick at his post-action edge
     * position (e_rick.c set x to 0xe2/0x04 the instant it set
     * e_rick_atExit) against the OLD submap's still-current terrain --
     * genuinely displayed for one video frame's worth of real time, since
     * nothing here waits for vsync (see sys_nabu.c's own header). Only
     * THEN did this block hide him, load the new submap, and repaint --
     * a real old-position-then-blank-then-new-position flicker, not just
     * a same-frame logic race. Moved the whole transition here, before
     * that ents_paintAll() call runs at all, so a submap change is fully
     * resolved before anything gets painted this frame -- Rick appears
     * exactly once, already in the right place. */
    if (e_rick_atExit) {
      /* Hide Rick's sprite immediately -- map_init() below hasn't run yet,
       * so without this he'd still show his old sprite/position for a
       * moment. The single ents_paintAll() call further down (now after
       * this whole block, not before it) picks him back up with a fresh
       * sprite slot at his real, already-updated position. */
      ent_hideSprite(E_RICK_NO);
      // BUG FIXED, on request ("sounds should mute during... submap
      // load"): mute BEFORE either branch below starts doing real work,
      // not just after (each branch's own later sounds_reset_all() call
      // still runs too, for a clean state once loading finishes) --
      // otherwise a JUMP_SND/BULLET_SND/EXPLODE_SND effect mid-playback
      // the instant a transition begins would freeze at its last AY
      // register state for however long map_init()/map_loadMap() take
      // (the map-advance branch below in particular streams a whole new
      // map's worth of data over the network -- easily the longest
      // blocking stretch in this loop), droning the whole time instead of
      // going silent. Nothing needed to "unmute" afterward -- this is a
      // one-shot stop, not a persistent mute flag, so normal ticking (and
      // any later effect) just resumes on its own next frame.
      sounds_pause_fx(); /* keeps a priority (speed-bonus) sound -- sounds.c */
      if (map_chain()) {
        map_init();
        ent_clprev();
        maps_paint();
        e_rick_save();
        save_map_row = map_frow;
        save_submap = env_submap;
        submapJustLoaded = TRUE;
        // BUG FIXED, reported as "a constant tone stuck playing" on
        // entering a submap 3 room -- see sounds_reset_all()'s own
        // comment. Every submap transition now forces a clean AY state
        // instead of relying on whatever was mid-playback to finish on
        // its own.
        sounds_pause_fx(); /* keeps a priority (speed-bonus) sound -- sounds.c */
      } else if (env_map + 1 < MAP_NBR_MAPS) {
        // FALSE ("no next submap - request next map", e.g. walking off
        // submap 8's own right edge, the end of SAMERICA): map advancement
        // (xrick's NEXT_MAP state). Restored on request to match xrick's
        // real progression: shows the next map's own intro/title card
        // (screen_introMap(), engine/scr_imap.c) before loading it, exactly
        // like the title screen's own level-select path does (see main()'s
        // own `while (screen_introMap() != SCREEN_DONE); loadTileBanks();
        // map_loadMap(...)` sequence above) -- not a fresh invention, the
        // same three-call contract, just triggered by walking off a map's
        // last submap instead of a title-screen keypress.
        //
        // BUG FIXED (this used to be reverted, see git history): an
        // earlier attempt at exactly this crashed real hardware/Marduk
        // twice with a PSG register error. That's music_tick()'s own
        // wraparound bug (sounds.c's music_size comment -- reading past a
        // shorter song's real data as if it were still (reg,val) pairs
        // once music_pos ran past it) -- already fixed elsewhere in this
        // file's own #include chain since that attempt, not something new
        // needed here. The second symptom reported then, garbled tile
        // graphics, matches this session's own font/HUD-corruption fixes
        // (tiles_setBank(0xff) cache invalidation after any direct VRAM
        // blit, and sprites_clear() actually hitting real VRAM) -- both
        // landed after that original attempt too.
        //
        // BUG FIXED, reported as "the sprite block from the end of level 1
        // stayed there" plus "wrong palette" on this exact intro screen:
        // this comment used to claim screen_introMap() "already clears
        // sprites (its own seq==30) and repaints the whole screen, so
        // nothing here needs to duplicate that" -- wrong. seq==30 is that
        // screen's own EXIT case, which only clears sprites once ITS run
        // ends; nothing clears whatever gameplay sprites (Rick, enemies)
        // were still on screen the instant level 1 ended, so they sat
        // there, visible, for this entire intro screen's run. Same
        // vdp_clearVRAM()+tiles_setBank(0xff) fix as the ESC handler
        // above (see that call site's own comment for the full
        // reasoning).
        //
        // BUG FIXED, reported as "wrong palette" surviving the above:
        // vdp_clearVRAM()+tiles_setBank(0xff) alone force a REAL reload
        // (no cache short-circuit possible) but don't explain a
        // persistently wrong result -- proves the SOURCE bytes
        // tiles_setBank(0) copies from (tiles_banks_shared's font slot,
        // in RAM, not VRAM) were already wrong before any of this ran.
        // Root cause: screen_levelSelect() calls loadTileBanks() as its
        // own very first statement, before it ever calls tiles_setBank(0)
        // itself, and boot's own loadAssets() also calls loadTileBanks()
        // internally before main()'s own screen_introMap() call -- so
        // both of THOSE paths' tiles_banks_shared font slot is always
        // freshly reloaded right before anything reads it. This map-
        // advance branch never called loadTileBanks() until AFTER
        // screen_introMap() already ran (the call further below, there to
        // serve the following map_loadMap() call) -- so this screen was
        // reading whatever had been sitting in that memory since boot,
        // never refreshed for this specific call. Reload it here too,
        // matching what the working paths already do.
        loadTileBanks();
        vdp_clearVRAM();
        tiles_setBank(0xff);
        //
        // Drain any queued keyboard-buffer event before starting, same
        // reasoning as this file's two other pre-screen_introMap() drains
        // above: the player is very likely still holding a direction on
        // the joystick the instant Rick walks off the map edge, and that
        // shares the same buffer isKeyPressed()/getChar() read (this
        // file's own header) -- without draining it first, that held
        // input could read as "skip" on the intro's very first frame.
        while (isKeyPressed()) {
          getChar();
        }
        screen_introMap_sel = env_map + 1;
        // DEFENSIVE -- see main()'s own screen_introMap_reset() call site
        // comment (right after the boot-time loadAssets()) for why this
        // guard exists.
        screen_introMap_reset();
        while (screen_introMap() != SCREEN_DONE);
        // CHANGED, on request ("add a vdpclear call between intro and
        // gameplay"): same fix, same reason, as main()'s own boot-time
        // screen_introMap() call site above -- wipe whatever the intro's
        // own scenery left in VRAM before gameplay repaints over it,
        // instead of letting it linger through the loadTileBanks()/
        // map_loadMap() stretch below. tiles_setBank(0xff) right after is
        // required, not optional -- see that call site's own comment for
        // why (vdp_clearVRAM() bypasses tiles_setBank()'s own bank-cache
        // check, so leaving it un-invalidated risks a later tiles_setBank()
        // call short-circuiting against the now-blanked VRAM).
        vdp_clearVRAM();
        tiles_setBank(0xff);
        // screen_introMap() repurposed tiles_banks_shared for its own
        // title/body text and music (that screen's own map0_title/
        // map0_body comment) -- reload the real font before map_loadMap()
        // below streams the new map's own gameplay bank in and tiles_
        // setBank() (inside map_init(), called from map_loadMap()) reads
        // tiles_banks_shared's FONT slice to redraw the HUD digits
        // (tiles.c's loadDigitTiles()). Same reload main()'s own boot
        // sequence does right after this exact same screen, for the same
        // reason -- skipping it is what left stale/overwritten bytes there
        // for loadDigitTiles() to read as glyph shapes, corrupting the HUD.
        loadTileBanks();
        // Score (env_score_lo/hi) is untouched by any of the above or by
        // map_loadMap() below -- addscore() is its only writer anywhere in
        // this build, so it already carries over map to map with no extra
        // code needed here; env_lives/bombs/bullets carry over the same
        // way, matching xrick's own NEXT_MAP (it only resets those on a
        // brand new game, not between levels).
        map_loadMap(env_map + 1);
        ent_clprev();
        maps_paint();
        e_rick_save();
        save_map_row = map_frow;
        save_submap = env_submap;
        sounds_reset_all();
      } else {
        // BUG FIXED, reported as "progression from 4 to ending didn't
        // work": past the last real map (env_map+1 == MAP_NBR_MAPS, i.e.
        // walking off MBASE's own last submap) used to just leave Rick
        // sitting at the edge forever -- a real gap, not by design, left
        // over from when this was still true for EVERY map past whichever
        // one hadn't been ported yet. Now shows the real ENDING/EPILOGUE
        // intro (screen_introMap_sel == 4, same text/theme the title
        // screen's own '5' key shows) and returns to the title screen,
        // same as xrick's own ending -- reusing main()'s own restart_title
        // label (see that jump's own comment up top for why a goto, not a
        // `continue`, is what reaches it from here).
        //
        // Same vdp_clearVRAM()+tiles_setBank(0xff)+loadTileBanks() fix as
        // the level-advance branch just above -- see that call site's own
        // comment for why (leftover gameplay sprites, and a stale/never-
        // refreshed tiles_banks_shared font slot, otherwise carry through
        // this entire intro screen too).
        loadTileBanks();
        vdp_clearVRAM();
        tiles_setBank(0xff);
        while (isKeyPressed()) {
          getChar();
        }
        screen_introMap_sel = 4;
        // DEFENSIVE -- see main()'s own screen_introMap_reset() call site
        // comment (right after the boot-time loadAssets()) for why this
        // guard exists.
        screen_introMap_reset();
        while (screen_introMap() != SCREEN_DONE);
        // BUG FIXED, reported as "didn't work, just went to title":
        // screen_introMap()'s own seq==10 skip check (engine/scr_imap.c)
        // deliberately does NOT call getChar() to consume the key that
        // ends it -- that call site's own comment says so explicitly,
        // reasoning "nothing else in this build ever reads the keyboard
        // buffer afterward." True when written; false now that
        // screen_gameOver() (called right below) drains and checks
        // isKeyPressed() as its own exit condition -- the leftover queued
        // key from skipping the epilogue was being read as an immediate
        // "skip this too" the instant screen_gameOver() started, before
        // the player ever saw it. Same drain this file already uses at
        // every other back-to-back-screen transition (this file's own
        // header, several other drains) fixes it here too.
        while (isKeyPressed()) {
          getChar();
        }
        // CHANGED, on request ("wire the ending to show game over too"):
        // confirmed directly against the reference (xrick/src/game.c) --
        // `if (env_map >= 0x04) { ... game_state = FADEOUT__GAMEOVER; }`
        // runs right after the epilogue's own MAP_INTRO finishes, i.e.
        // finishing MBASE and its "LONDON, MUCH MUCH LATER" epilogue
        // really does lead to the same GAMEOVER screen a real death does
        // in the reference, not straight back to the title/attract loop.
        // FADEOUT__GAMEOVER itself is just a gamma fade this port has no
        // equivalent knob for (sysvid_setGamma(GAMMA_OFF), immediately
        // followed by GAMEOVER) -- skipped, straight to screen_gameOver()
        // via show_game_over_then_level_select (main()'s title/select
        // loop above), shared with every other "show the ending's game
        // over screen" call site rather than repeating its four lines a
        // third time here (on request, "jump to the same game over code
        // as usual and not duplicate as much code"). GETNAME (name entry,
        // next in the reference's own chain) follows there too, via
        // hof_afterGame().
        goto show_game_over_then_level_select;
      }
      e_rick_atExit = FALSE;
    }

    /* BUG FIXED, reported as a one-frame "small Rick sprite and some blue
     * sprite" entering a submap: map_init() just created the new submap's
     * entities in their spawn state, and entities that stay hidden until
     * triggered (the wall trap's dart, ...) only hide themselves in their
     * first ent_action(). Drawing them now showed that spawn state for a
     * frame. Like xrick (NEXT_SUBMAP, then draw only after the next
     * PLAY frame's actions), show no sprites this frame instead. */
    if (submapJustLoaded) {
      submapJustLoaded = FALSE;
      sprites_endList(0);
    } else {
      ents_paintAll();
    }

    /* sound_afx_tick_all() used to advance DIE_SND/JUMP_SND/BULLET_SND's
     * own non-blocking AY effects here every frame -- DIE_SND is still
     * dropped (engine/sounds.c's own header), but JUMP_SND/BULLET_SND are
     * both back now, sharing one ticked slot (sounds.c's own header on
     * why one shared slot instead of reviving the whole table this
     * comment used to describe).
     *
     * One effect tick per vblank the previous frame really took -- the
     * effect data is encoded at one tick per 1/60s, so this plays it at
     * its real speed at any frame rate or with the 'F' key's limit off.
     * BUG FIXED, reported as "the death sound and the level bonus sound
     * are much slower than the TI99 version": this used to be one tick
     * per 2 vblanks (a leftover of the old FX_FRAMES_PER_TICK = 2 per
     * ~66ms frame), i.e. every in-game effect at half speed. The game
     * over screen already ticked at the real rate and sounded right. */
    sounds_fx_tick(lastFrameVblanks);

    /* Status bar: score/lives/bombs/bullets, row 0 -- see env.c's own
     * comment on env_paintGame() for why this needed no porting work,
     * just a caller. Nothing here changes env_lives/bombs/bullets/score
     * yet (see this file's env_lives=6 init above), so today this mainly
     * just keeps the bar visible; it'll start reflecting real pickups/
     * hits once e_bonus.c/e_bullet.c/e_bomb.c are wired in.
     */
    env_paintGame();
    sys_nabu_pollVblank();

    /* CTRL_RICK (xrick/src/game.c) equivalent: if Rick died this frame
     * (e_rick_z_action() sets E_RICK_STDEAD once his zombie/death-bounce
     * fall goes off-screen -- see e_rick.c), respawn at the last
     * checkpoint instead of leaving him stuck in zombie state forever,
     * which is what this build did before this existed (no game_run()
     * state machine to drive the original's RESTART state). Mirrors the
     * real restart(): clear STDEAD/STZOMBIE, refill ammo, restore
     * position/map_frow via e_rick_restore() and the save_map_row/
     * e_rick_save() pair set up after map_loadFirst() above, then redo
     * map_init()/paint the same way INIT_SUBMAP would.
     *
     * env_lives now really does something once it hits 0 (on request,
     * "implement the game over code and music") -- mirrors the
     * reference's own CTRL_RICK exactly: `if (env_trainer || --env_lives)
     * RESTART; else FADEOUT__GAMEOVER;`. This port's env_invicible is the
     * trainer-equivalent, but it doesn't need an explicit check here the
     * way the reference's env_trainer does -- e_rick_gozombie() (e_rick.c)
     * already returns immediately whenever env_invicible is set, so
     * E_RICK_STDEAD can never even trigger while it's on. */
    if (E_RICK_STTST(E_RICK_STDEAD)) {
      E_RICK_STRST(E_RICK_STDEAD | E_RICK_STZOMBIE);
      if (env_lives) {
        env_lives--;
      }

      if (!env_lives) {
        // GAME OVER: "don't leave Rick's last frame frozen on screen"
        // fix the ordinary respawn path below already needed (see that
        // call site's own comment) -- specific to dying, so kept inline
        // rather than folded into the shared helper below. The rest
        // (stop whatever's mid-playback, full-VRAM wipe, show the game
        // over screen, land on level select) is identical to every other
        // way of reaching this screen -- shares show_game_over_then_
        // level_select (main()'s title/select loop above) instead of
        // repeating it a third time (on request, "jump to the same game
        // over code as usual and not duplicate as much code").
        ENT_XRICK.n = 1;
        ent_hideSprite(E_RICK_NO);
        goto show_game_over_then_level_select;
      }

      env_bullets = 6;
      env_bombs = 6;
      // BUG FIXED, reported as "the explode sound lags/extends after Rick
      // falls off screen": respawn never stopped whatever JUMP_SND/
      // BULLET_SND/EXPLODE_SND effect might still be mid-playback (e.g.
      // an explosion that killed Rick), so it kept ticking right through
      // into the new scene -- see sounds_reset_all()'s own comment
      // (sounds.c) for why this is the one place that stops an effect,
      // not just silences it for a moment.
      sounds_reset_all();
      ENT_XRICK.n = 1;
      e_rick_restore();
      /* Same hide-before-repaint as the e_rick_atExit branch above (its
       * own comment has the full reasoning) -- without it, Rick's last
       * zombie/death-bounce sprite position would sit frozen on screen
       * against the OLD submap's still-current terrain for a frame
       * before map_init()/maps_paint() below repaint everything and
       * ents_paintAll() (top of this loop, next frame) picks him back up
       * at his real, already-restored checkpoint position. */
      ent_hideSprite(E_RICK_NO);
      map_frow = save_map_row;
      env_submap = save_submap;
      map_init();
      ent_clprev();
      maps_paint();
    }

    /* CTRL_SCROLL (xrick/src/game.c) equivalent: once Rick nears the
     * bottom of the visible screen (falling, mainly) or the top (jumping/
     * climbing), scroll the map to follow him instead of letting him walk
     * off the edge of the drawn view. Each call runs one 8-tile-row scroll
     * step and repaints; looping here until SCROLL_DONE matches how
     * xrick's own game_run() state machine drives it (call once per
     * frame, across several frames) since this build has no such state
     * machine to hang it on otherwise. See engine/scroller.c's header for
     * why either call can also just refuse to move (already at submap
     * 0's currently-loaded edge) and return SCROLL_DONE immediately.
     *
     * BUG FIXED: this used to check ENT_XRICK.y unconditionally -- but
     * the original's matching CTRL_SCROLL state explicitly skips this
     * whole check while zombie (`if (!E_RICK_STTST(E_RICK_STZOMBIE)) {
     * ... } else { game_state = CTRL_ACTION; }`), which this port missed
     * porting. Without it, Rick's death-bounce/fall (e_rick_z_action(),
     * a completely different y-update path from normal falling) could
     * push his y past 0xcc and scroll the camera during what should be a
     * stationary death animation -- the screen scrolling when it
     * shouldn't, reported directly. */
    if (!E_RICK_STTST(E_RICK_STZOMBIE)) {
      if (ENT_XRICK.y >= 0xcc) {
        // BUG FIXED, on request ("sounds should mute during map scroll"):
        // this while loop below doesn't call sounds_fx_tick() at all
        // (only the frame loop at the very bottom of this function does),
        // so any JUMP_SND/BULLET_SND/EXPLODE_SND effect mid-playback the
        // instant a scroll starts would just freeze at whatever AY
        // register state it last wrote and drone there for the whole
        // scroll -- sounds_reset_all() (sounds.c) stops it outright
        // before that can happen; normal ticking (and any later effect)
        // resumes on its own once this loop ends, nothing to "unmute".
        sounds_pause_fx();
        while (scroll_up() == SCROLL_RUNNING) {
          sys_nabu_waitFrame(game_period);
        }
      } else if (ENT_XRICK.y <= 0x60) {
        sounds_pause_fx(); // see the scroll_up() call site's own comment above
        while (scroll_down() == SCROLL_RUNNING) {
          sys_nabu_waitFrame(game_period);
        }
      }
    }

    /* BUG FIXED (previous round): this used to clamp ENT_XRICK.y directly
     * here as a band-aid against util.c's u_envtest() reading past
     * map_map[]/map_eflg[]'s bounds once Rick's fall outran submap 0's
     * loaded data -- but that ran *after* this frame's own ent_action()
     * had already called u_envtest() with the unclamped, out-of-range y,
     * so the bad read (and its side effect, a garbage MAP_EFLG_LETHAL bit
     * triggering e_rick_gozombie()'s offsy=-0x0400 death-bounce --
     * exactly what "teleports back up" looks like) had already happened
     * by the time this code ran. Real fix is now in u_envtest() itself
     * (util.c) -- it clamps the row there, at the one place every caller
     * funnels through, so it can never see an out-of-range row regardless
     * of how far Rick's own (unmodified, verbatim-ported) physics let him
     * fall. Nothing needed here any more. */

    lastFrameVblanks = sys_nabu_waitFrame(game_period);
  }
}
