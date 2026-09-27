/*
 * engine/scr_imap.c -- PARTIAL port of xrick/src/scr_imap.c
 *
 * Ported: the whole screen_introMap() state machine (init/nextstep/anim
 * step-sequencing, the animated picture-frame border via drawtb()/
 * drawlr(), the tiled background via drawcenter()), and the map_intros[]
 * title/body text + screen_imapsofs[] step-table offsets (verbatim data,
 * portable as-is -- screen_imapsl[]/screen_imapsteps[] themselves are in
 * the new engine/dat_screens.c, ported from xrick/src/dat_screens.c).
 *
 * STALE NOTE, corrected: this header used to say drawsprite() was a no-op
 * stub and the F18A half-bitmap/palette setup below was "moot until
 * drawsprite() is real" -- both are real now. drawsprite() (further down
 * this file) is wired to a real sprites_paint() call, and seq==0 (below)
 * now calls set_halfbitmap() itself too, matching the reference's own
 * placement, gated the same way every other F18A/9918A split in this port
 * is (config.h has no `#ifdef F18A`; branch on sysvid_nabu_hasF18A at
 * runtime instead) -- see that call site's own "BUG FIXED" comment for
 * why it was missing and what broke without it.
 *
 *   - `sounds_play(map_maps[env_map].tune)` -- replaced by this port's own
 *     music player (engine/sounds.c), one intro tune per map.
 *
 *   - `control_status & CONTROL_FIRE` / `CONTROL_EXIT` -- uses NABU-LIB's
 *     isKeyPressed()/getChar() instead, a buffered "key was typed" queue,
 *     not a held/released line. So the reference's seq==20 ("wait for fire *release*" before allowing exit) has no
 *     buffered-queue equivalent -- there's no "held" state to poll, and
 *     the keypress that ends seq==10 already consumed the one queued
 *     event -- so seq==10 goes straight to done on a keypress instead of
 *     routing through a release-wait step first. CONTROL_EXIT (a
 *     dedicated "quit the whole game" combo) has no stand-in: this never
 *     returns SCREEN_EXIT.
 *
 *   - hchar()/vchar()/vdpwriteinc() -- these come from the TI
 *     cross-compiler's `<vdp.h>`, not defined anywhere in this repo. Reimplemented as
 *     tiny local `nabu_*` statics below rather than invented as engine-
 *     wide primitives -- nothing else needs them yet. If draw.c ever gets
 *     ported and needs the same three operations, promote these there
 *     instead of duplicating.
 */

#include "config.h"
#include "env.h"
#include "screens.h"
#include "sysvid.h"
#include "game.h"
#include "tiles.h"
#include "sprites.h"
#include "fb.h"
#include "NABU-LIB.h"
#include "sysvid_nabu.h"
#include "sys_nabu.h"
#include "sys_nabu_load.h"  /* sys_nabu_loadAsset() -- map0_title/body's
                             * own network load, see their comment below */
#include "sysevt_nabu.h"    /* sysevt_poll() -- joystick skip */
#include "control.h"        /* control_status */
#include "sounds.h"         /* sounds_music_load()/start()/tick()/stop()
                             * -- this screen's own music, see MUSIC2_SIZE
                             * comment below */

/* SCREEN_TRIM_INTRO, on request ("add a define for disabling all
 * code+data related to intros"): temporary byte-budget experiment knob,
 * same family/spirit as SCREEN_TRIM_TITLE/SOUND_TRIM_MUSIC/PCM/FX
 * (build.bat's own comment) -- 0 keeps the real per-map intro screen
 * (title/body text, animated border, walking-Rick preview, its own
 * captured music), 1 strips this whole file's real content down to two
 * stubs (screen_introMap_reset() as a no-op, screen_introMap() returning
 * SCREEN_DONE immediately) so main.c's three call sites need no changes
 * at all -- each already just spins `while (screen_introMap() !=
 * SCREEN_DONE);`, which exits after one call either way. dat_screens.c's
 * own screen_imapsl[]/screen_imapsteps[] (the walking-Rick animation
 * data this file's nextstep()/anim() read) have no other reader, so
 * they're gated the same way over there. */

/* See screens.h's own comment -- picks which map's title/body text (and
 * matching step/tn) seq==0/init()/drawcenter() below use. Declared
 * unconditionally (not inside the #if !SCREEN_TRIM_INTRO block below):
 * main.c reads and writes this directly at every one of its own three
 * screen_introMap() call sites regardless of whether the trim flag is
 * on, same reason screen_introMap_reset()/screen_introMap() themselves
 * stay callable either way (as stubs when trimmed). */
U8 screen_introMap_sel = 0;

#if !SCREEN_TRIM_INTRO

/* assets/MUSIC2.DAT's exact byte size -- see this screen's own
 * seq==0 comment on sounds_music_load() for what it is and why it's
 * trimmed/re-timed the way it is. Keep this in sync if that file is
 * ever regenerated at a different tick rate or trim point. */
#define MUSIC2_SIZE 2018

/* assets/EGY.DAT -- EGYPT's own real intro theme, captured live from
 * the actual ZX Spectrum 128 game (MAME VGM dump against a debugger-
 * redirected snapshot -- see this session's own notes) rather than reused
 * from MUSIC2.DAT like the other still-uncaptured maps. Filename kept
 * short (not "MUSICEGY.DAT") purely to save the extra bytes the longer
 * literal would cost in this call site, same reasoning for LON.DAT below.
 * Same encoding as MUSIC2 (16ms/tick, [count,(reg,val)*count,delta]
 * tuples) -- trimmed to start right after the ~2.4s fanfare shared with
 * MUSIC1.DAT (byte-for-byte identical for its first 56 register writes,
 * same shared-intro situation MUSIC2's own header describes) and to end
 * at the tune's own natural silence before it loops; no manual trailing
 * mute baked in since music_tick()'s own hard-mute-on-wrap already covers
 * that.
 *
 * BUG FIXED, reported as "sounds slower and glitchier than title or
 * samerica": the VGM->frame grouping this session's own capture pipeline
 * used (a live real-time MAME rip, unlike MUSIC1/MUSIC2's cleaner source
 * rips) closed a "frame" on ANY wait, even a 1-3 sample gap that's just
 * real Z80 instruction timing between register writes meant to land in
 * the same musical row (confirmed by histogram: writes within a row are
 * 1-18 samples apart, real row-to-row gaps start at ~861 samples -- a
 * huge, clean gap between the two). That splits one real 8-10-register
 * chord into 8-10 separate single-write "frames", each independently
 * quantized to a 16ms tick -- rounding error that should average out
 * once per real row instead adds up once per individual register write,
 * occasionally tipping a spurious extra tick into what should've been a
 * simultaneous chord. Re-encoded with the grouping fixed to coalesce
 * writes separated by under ~100 samples into one frame before
 * quantizing (see this session's own notes) -- 3448 -> 2464 bytes, since
 * far fewer, denser frames are needed once chords are grouped instead of
 * split. */
#define MUSICEGY_SIZE 2464

/* assets/LON.DAT -- EPILOGUE's own real intro theme ("LONDON, MUCH,
 * MUCH LATER" -- this map's own title text), captured the same way as
 * EGY.DAT (MAME VGM dump against a live-debugger-redirected snapshot).
 * This capture shares a much longer stretch with MUSIC1.DAT than Egypt's
 * did -- its first 159 register writes are byte-for-byte identical to
 * MUSIC1's own fanfare -- then drops into ~0.7s of genuine silence (mixer
 * disabled, all channels muted) before its own unique material begins;
 * trimmed to start right after that silence and to run to the capture's
 * own natural end, which is already a fade-to-mute (volume ramped down to
 * 0 and the mixer disabled in the source data) so no manual trailing mute
 * was needed here either. Same frame-grouping bug and fix as EGY.DAT's
 * own header describes -- 620 -> 486 bytes after re-encoding correctly. */
#define MUSICLON_SIZE 486

/* assets/CAS.DAT -- CASTLE's own real intro theme ("EUROPE, LATER
 * THAT WEEK" / "SCHWARZENDUMPF CASTLE" -- this map's own title text),
 * captured and encoded the same way as EGY.DAT/LON.DAT (MAME VGM dump
 * against a live-debugger-redirected snapshot, coalesced-frame grouping
 * fixed as EGY.DAT's own header describes). Shares its first 37 register
 * writes with MUSIC1.DAT's own fanfare (shorter shared intro than Egypt/
 * London's captures, still the same situation) before a brief mute and
 * its own unique material; trimmed to start right after that mute and to
 * run to the capture's own natural end (fade to mute, mixer disabled). */
#define MUSICCAS_SIZE 2178

/* assets/MBS.DAT -- MBASE's own real intro theme ("EUROPE, EVEN
 * LATER" / "SECRET MISSILE BASE" -- this map's own title text), captured
 * and encoded the same way as the other three real captures above. This
 * capture's own shared prefix runs the ENTIRE MUSIC1.DAT fanfare (all 732
 * writes, not just the ~1-2.4s fragment the other three share) -- this
 * particular capture let the full title theme play out before the map
 * screen loaded, rather than skipping ahead early like the others did;
 * matches byte-for-byte all the same, so the same shared-fanfare-skip
 * logic still applies, just with a longer prefix to skip past. Trimmed to
 * start right after that (and the ~5.9s silence that followed it) and to
 * run to the capture's own natural end (fade to mute, mixer disabled). */
#define MUSICMBS_SIZE 650

/* assets/INTRMUS1-5.DAT -- renamed from MUSIC2/EGY/CAS/MBS/LON.DAT to
 * match MAPTIT1-5.DAT/MAPBOD1-5.DAT's own numbering (screen_introMap_sel
 * + 1), and so screen_introMap()'s own call site below can build the
 * filename with the exact same mutable-single-digit-template trick used
 * for titleFile/bodyFile above instead of a 5-pointer table -- 1 shared
 * 6-byte literal beats 5 separate ones (11+8+8+8+8 bytes) plus the table
 * itself. Sizes still need a real lookup (they're not derivable from the
 * filename), indexed the same way. [0] (SAMERICA/INTRMUS1.DAT) is that map's own
 * real tune (the original level1.vgm rip, MUSIC2_SIZE's own name a
 * leftover from before this rename), not a placeholder. */
static const U16 introMusicSize[5] = {
  MUSIC2_SIZE, MUSICEGY_SIZE, MUSICCAS_SIZE, MUSICMBS_SIZE, MUSICLON_SIZE
};

/*
 * local vars -- unchanged in shape from the reference.
 */
static U16 step;              /* current step */
static U16 count;             /* number of loops for current step */
static U16 run;               /* 1 = run, 0 = no more step */
static U16 flipflop;          /* flipflop for top, bottom, left, right */
static U16 spnum;             /* sprite number */
static U16 spx, spdx;         /* sprite x position and delta */
static U16 spy, spdy;         /* sprite y position and delta */
static U16 spbase, spoffs;    /* base, offset for sprite numbers table */
static U16 seq = 0;           /* anim sequence */
static U16 introLastControl;  /* joystick state last seen -- skip edge detect */

/* See screens.h's own comment on this declaration for why it exists. */
void screen_introMap_reset(void) {
  seq = 0;
}

/*
 * prototypes
 */
static void drawtb(void);
static void drawlr(void);
static void drawsprite(void);
static void drawcenter(void);
static void nextstep(void);
static void anim(void);
static void init(void);

/* CUT for the fileload build's size budget: the reference has 5 sets of
 * title/body text (south america/egypt/europe castle/europe missile
 * base/much much later) in a maps_intros[] array, plus a matching
 * screen_imapsofs[] step-table-offset entry and drawcenter()'s tn0[]
 * entry, all three indexed by env_map. This screen's own init()/
 * drawcenter() below still never index those tables by env_map (they
 * use screen_introMap_sel instead, set independently by main.c's title
 * screen -- see the paragraph below) -- so all three tables' env_map==
 * 1..4 entries (maps_intros[]'s ~1KB of title/body text included) were
 * dead weight, compiled in but structurally unreachable through env_map
 * specifically, AND indexing any of them by the env_map *variable*
 * (rather than the literal 0 it's providably always holding AT THE TIME
 * THIS SCREEN RUNS -- see below) cost real bytes for a computed-offset
 * load SDCC can't fold away on its own. Cut down to their single
 * index-0 entry, and every read of them replaced with that entry
 * directly (map0_title/map0_body below) instead of keeping single-
 * element arrays around just to index into with 0.
 *
 * PARTIALLY UN-CUT since: main.c's title screen can now request any of
 * all five maps_intros[] entries (screen_introMap_sel, screens.h) as a
 * preview -- env_map itself is STILL always 0 for the whole duration
 * this screen runs (map_loadMap(), maps.c, is what actually changes it,
 * and main.c only calls that AFTER this screen's own loop below exits),
 * regardless of which map got previewed. '1'-'5' are real level-select
 * keys now, not preview-only (main.c's own header) -- EGYPT (env_map 1)
 * really does load and become playable after its own intro finishes --
 * but that happens entirely after this screen hands control back, so
 * nothing in init()/drawcenter() below needed to change; the reference's
 * own 5th entry, "much much later", is still an epilogue normally shown
 * after finishing map 4 rather than a map intro at all, so init()'s
 * step and drawcenter()'s
 * tn now index a small local lookup table by that selector instead of
 * reading a live env_map index. maps_intros[]'s own text stayed cut down
 * to five named .DAT-streamed entry pairs (MAPTIT1/MAPBOD1 through
 * MAPTIT5/MAPBOD5, all loaded into the same map0_title/map0_body scratch
 * space in turn) rather than restoring the full compiled-in array --
 * screen_imapsteps[]/screen_imapsl[] below never needed cutting in the
 * first place (already one small shared table, not indexed per-map at
 * the top level).
 *
 * '@' is bank 0's blank-fill glyph (see fb.c's own header
 * comments on this font's ' '-isn't-blank quirk); TILES_CRLFCHAR/
 * TILES_NULLCHAR are tiles.h's list-terminator convention, unchanged.
 *
 * The title/body stay in their own named static const arrays rather than
 * sitting inline inside maps_intros[]'s aggregate initializer -- z88dk's
 * sdcc backend (-O3 --opt-code-size) pools string literals into __str_N
 * labels, and an inline literal in a struct-array initializer like this
 * one can get pooled twice (once per internal pass), producing a bogus
 * "duplicate definition: __str_N" link error even though the literal only
 * appears once in the source. Naming the string first and referencing the
 * pointer below avoids the double-pool entirely. */
/* CHANGED for the fileload build: no longer `const`-initialized string
 * literals, and (this round) no longer dedicated BSS buffers either --
 * BSS costs real bytes in this NABU build just like `const` data does
 * (tiles.h's tiles_banks_shared union comment explains why: there's no
 * loader-level "uninitialized" concept for a flat memory image), so 312
 * more bytes of dedicated storage was 312 bytes this build doesn't have
 * to spare. Aliased directly onto tiles_banks_shared's own memory
 * instead -- safe because of a real ordering constraint, not a
 * coincidence:
 *
 *   - screen_introMap() (below) is the ONLY thing that calls
 *     tiles_setBank(0), and it does so once, at the very top of seq==0,
 *     before anything reads map0_title/map0_body. That call consumes
 *     tiles_banks_shared's bank-0 slice (copies it to VRAM) -- once it
 *     returns, that memory has nothing left to give until the NEXT
 *     tiles_setBank() call needs a DIFFERENT bank.
 *   - main.c's map_loadFirst() (which always requests bank 1 -- see
 *     tiles.h's TILES_BANKS_COUNT comment) is that next call, and it
 *     doesn't run until after this whole screen's wait loop finishes.
 *
 *   So there's a real window, spanning this entire screen, where
 *   tiles_banks_shared's original tile data is not needed by anything
 *   -- exactly long enough for the map-intro text to live there instead.
 *   main.c reloads the real tile banks right after this screen's loop
 *   exits and before calling map_loadFirst(), so tiles_setBank(1) still
 *   sees real tile data, not stale text. See main.c's own comment at
 *   that reload call for the other half of this contract; if the two
 *   drift out of sync (this screen's loop moving relative to that
 *   reload, or a third tiles_setBank() call appearing somewhere) this
 *   whole scheme breaks.
 *
 * Sized to hold exactly the original string literals' content, INCLUDING
 * the trailing TILES_NULLCHAR sentinel tiles_paintListAt() scans for but
 * EXCLUDING the extra implicit '\0' C used to tack onto the end of a
 * string-literal initializer (harmless, but never read by anything --
 * see assets/README.md's "Adding more assets" section: unlike the
 * numeric-literal tables there, tools/extract_assets.py can't pull a
 * string literal out on its own, so these two .DAT files were produced
 * by hand from this exact text -- regenerate the same way if the text
 * ever changes, and keep the sizes below in sync with it). */
#define map0_title ((U8 *)&tiles_banks_shared)
#define map0_body ((U8 *)&tiles_banks_shared + 31)

/* ---------------------------------------------------------------------
 * Minimal NABU stand-ins for the TI cross-compiler's <vdp.h> hchar()/
 * vchar()/vdpwriteinc() -- see file header. Coordinates are tile
 * row/column (0-23, 0-31), matching how the reference calls them.
 * ---------------------------------------------------------------------
 */

/* write `count` copies of `ch` horizontally, starting at (row, col) */
static void nabu_hchar(U16 row, U16 col, U8 ch, U16 count_) {
  U16 i;
  NABU_DisableInterrupts();
  vdp_setWriteAddress((U16)(gImage + row * 32 + col));
  for (i = 0; i < count_; i++) {
    IO_VDPDATA = ch;
  }
  NABU_EnableInterrupts();
}

/* write `count` copies of `ch` vertically, starting at (row, col) */
static void nabu_vchar(U16 row, U16 col, U8 ch, U16 count_) {
  U16 i;
  NABU_DisableInterrupts();
  for (i = 0; i < count_; i++) {
    vdp_setWriteAddress((U16)(gImage + (row + i) * 32 + col));
    IO_VDPDATA = ch;
  }
  NABU_EnableInterrupts();
}

/* write `count` consecutive tile numbers starting at `startVal`, into
 * `count` consecutive VRAM bytes starting at `addr` */
static void nabu_vdpwriteinc(U16 addr, U16 startVal, U16 count_) {
  U16 i;
  vdp_setWriteAddress(addr);
  for (i = 0; i < count_; i++) {
    IO_VDPDATA = (U8)(startVal + i);
  }
}

/*
 * Map introduction
 *
 * return: SCREEN_RUNNING, SCREEN_DONE (see file header: SCREEN_EXIT is
 * never returned yet -- no CONTROL_EXIT stand-in)
 */
U16 screen_introMap(void) {
  switch (seq) {
    case 0: /* initialize */
      fb_clear();
      sysvid_setGamma(GAMMA_OFF);

      /* BUG FIXED, reported directly ("the first time an intro is played,
       * the sprites aren't using the F18A colors. Later intro plays the
       * colors are good"): this file's own header (see the top of this
       * file) used to defer this call as "moot until drawsprite() is
       * real" -- drawsprite() below IS real now (wired to sprites_paint()
       * a while ago), but this call was never added back here to match.
       * Register 49 (ECM)/the sprite palettes were only ever being set up
       * by main.c's own set_halfbitmap() call, which happens AFTER this
       * whole intro screen finishes (right before real gameplay's
       * map_loadMap()) -- so drawsprite() below, on a screen_introMap()
       * this build hasn't shown before, ran with ECM still off and no
       * palettes loaded (plain/wrong colors). Once any gameplay has
       * happened once, register 49 stays at ECM=3 with real palettes
       * still resident (nothing ever turns it back off -- set_fullbitmap()
       * is #if 0'd, no callers), so every intro shown AFTER that first
       * real gameplay session looks correct by accident. Matches the
       * reference's own placement (xrick/src/scr_imap.c calls
       * set_halfbitmap() + the sprfNpal loads at this exact spot, this
       * file's own header comment already said so) -- idempotent, same
       * register/palette writes main.c's own call already makes, so
       * calling it again there after map_loadMap() is harmless. */
#if SPRITE_ECM_ENABLED
      if (sysvid_nabu_hasF18A) {
        set_halfbitmap();
      }
#endif

      tiles_setBank(0);
      /* Safe to overwrite tiles_banks_shared now -- see map0_title/
       * map0_body's own comment above for why this exact spot (right
       * after tiles_setBank(0), before map_loadFirst()'s tiles_setBank(1))
       * is the one place that's true. This screen is re-entrant (seq
       * resets to 0 at its own seq==30 below) -- main.c's end-of-level-1
       * call, the only other caller, relies on that, and on the same
       * tiles_banks_shared window being safe again by then (gameplay's
       * own tile-bank load only ever happens once, at boot). */
      {
        /* SIZE PASS: filenames built from one mutable template each
         * (poking the map digit into place) instead of 8 separate string
         * literals (renamed assets/MAPTITL.DAT/MAPBODY.DAT to
         * MAPTIT1.DAT/MAPBOD1.DAT to fit the same "digit at index 6"
         * pattern as MAPTIT2/3/4.DAT and MAPBOD2/3/4.DAT) -- 8 filename
         * literals cost far more than these two 12-byte templates, same
         * size-saving reasoning as sprites.c's own sprite-number dispatch
         * table. MAPBOD2/3/4.DAT (naturally 250/250/219 bytes) are also
         * zero-padded to MAPBOD1.DAT's own 281 -- tiles_paintListAt()
         * below stops at each file's real TILES_NULLCHAR terminator
         * regardless of trailing padding, so this trades a few bytes of
         * harmless network-asset padding (not ROM) for one shared literal
         * size instead of a 4-entry lookup table. */
        /* MAPTITn.DAT/MAPBODn.DAT, packed into RICK.DAT one fixed stride
         * apart (res.h). */
        sys_nabu_loadRes(RES_MAPTIT_BASE + screen_introMap_sel * RES_MAPTIT_STRIDE, map0_title, 31);
        sys_nabu_loadRes(RES_MAPBOD_BASE + screen_introMap_sel * RES_MAPBOD_STRIDE, map0_body, 281);
      }
      tiles_paintListAt(map0_title, 0, 0);
      tiles_paintListAt(map0_body, 0, 96);

      /* map0_title/map0_body are already painted into VRAM above -- safe
       * to overwrite tiles_banks_shared YET AGAIN with this screen's own
       * music now (real hardware rip of level1.vgm, same technique as
       * main.c's MUSIC1.DAT -- see engine/sounds.c's music_vgm header).
       * MUSIC2_SIZE (2018 bytes) fits easily alongside map0_title/body's
       * own 312 in the same 8192-byte scratch space since they're only
       * ever needed sequentially, never at once.
       *
       * BUG FIXED, reported as "duplicate notes" right after skipping the
       * title screen: level1.vgm and rick-theme.vgm (MUSIC1) share a
       * ~1.2s intro fanfare, byte-for-byte identical in both rips (found
       * by diffing their deduplicated register-write sequences) -- almost
       * certainly a shared logo/startup jingle both tracks were cut from
       * the same recording session with. Playing it again right after
       * MUSIC1 already played it sounded like a skip/repeat. MUSIC2.DAT
       * is encoded starting right after that shared fanfare (sample
       * 53051 in the source VGM, where the two tracks' write sequences
       * actually diverge), not from the file's own real start.
       *
       * Encoded at 16ms/tick, not MUSIC1's 66ms -- this screen's own
       * pacing loop below runs every ~16ms (16 sys_nabu_musicTickDelay()
       * calls, that function's own ~1ms/call calibration -- see
       * sys_nabu.h, NOT GAME_PERIOD/sys_nabu_tick() at all, so this
       * screen's music tempo stays independent of gameplay pacing), so
       * sounds_music_tick() needs to advance this data at that faster
       * native rate to play at the right speed. Retune the same way
       * MUSIC1's own encoding was if it sounds off -- see this screen's
       * pacing loop and sys_nabu_musicTickDelay()'s own header. */
      /* Every map now has its own real captured theme -- see
       * MUSICEGY_SIZE/MUSICCAS_SIZE/MUSICMBS_SIZE/MUSICLON_SIZE's own
       * headers ("MUSIC2.DAT" was always SAMERICA/sel==0's own real tune,
       * the original level1.vgm rip -- see introMusicSize's own header
       * for the file's real current name). Same mutable-single-digit-
       * template trick as titleFile/bodyFile above, now that all five
       * intro tunes exist -- see introMusicSize's own header. */
      /* INTRMUS1-5.DAT, packed into RICK.DAT one fixed stride apart
       * (res.h). */
      sounds_music_load(RES_INTRMUS_BASE + screen_introMap_sel * RES_INTRMUS_STRIDE,
                        introMusicSize[screen_introMap_sel]);
      sounds_music_start();

      init();
      nextstep();
      drawcenter();
      drawtb();
      drawlr();
      drawsprite();

      sysvid_setGamma(GAMMA_ON);
      /* Seed the joystick edge detect below with whatever is already held
       * (e.g. the FIRE/direction that picked this level), so only a NEW
       * press skips the screen. */
      sysevt_poll();
      introLastControl = control_status;
      seq = 10;
      break;

    case 10: /* top and bottom borders */
      /* Skip on a key OR a new joystick direction/button press, on request
       * ("sometimes the joystick directions or button doesn't skip the
       * intro screens") -- this only checked the keyboard, so the joystick
       * only skipped it by accident (a stray joystick byte leaking into the
       * keyboard queue, now fixed in hal/NABU-LIB.c's isrKeyboard()). */
      sysevt_poll();
      if (isKeyPressed() || (control_status & ~introLastControl)) {
        /* BUG FIXED, SIZE PASS: used to call getChar() here to consume
         * the buffered key -- correct in principle, but nothing else in
         * this build ever reads the keyboard buffer afterward (the main
         * game loop only polls the joystick), so the leftover queued key
         * is silently harmless and getChar() was 100+ bytes of dead
         * weight (a whole function, its only other reference already
         * gone) for a cleanup nothing needed. */
        seq = 30;
      } else {
        drawtb();
        seq = 12;
      }
      introLastControl = control_status;
      break;

    case 12: /* background and sprite */
      anim();
      drawcenter();
      drawsprite();
      seq = 13;
      break;

    case 13: /* all borders */
      drawtb();
      drawlr();
      seq = 10;
      break;
  }

  if (seq == 30) {
    fb_clear();
    /* BUG FIXED, reported as "intro sprites... visible on a blank screen
     * before the level loads": this used to be left to main.c's own
     * sprites_clear() call, made only after this whole screen's while()
     * loop returned AND loadTileBanks() finished re-fetching real tile
     * data over the network -- a real, possibly-long gap during which
     * fb_clear() above had already blanked the tile background but the
     * walking-Rick hardware sprite (drawsprite()'s pseudo-sprite slot 0)
     * was still fully visible, since sprites are a separate VDP resource
     * fb_clear() never touches. Hidden right here instead, the instant
     * this screen actually ends, before any of that delay. */
    sprites_clear();
    sysvid_setGamma(GAMMA_ON);
    sounds_music_stop();
    seq = 0;
    return SCREEN_DONE;
  }

  /* Steps this screen's own music forward by MUSIC_RATE_INTRO/256 encoded
   * ticks per call (engine/include/sounds.h -- the data is encoded at one
   * tick per vblank; the rate sets the tempo). Runs every call including
   * the very first (seq==0, right after sounds_music_start() resets
   * playback to the top) -- stepping early is inaudible and simpler than
   * special-casing it out. */
  sounds_music_tick(MUSIC_RATE_INTRO);

  /* One seq step (animation) per vsync frame, same as the reference.
   * Was a 16 x sys_nabu_musicTickDelay() busy-wait (~11ms plus this
   * screen's own drawing); now waits for the real vblank (hal/sys_nabu.c's
   * sys_nabu_waitFrame(), 16ms rounds up to 1 vblank), so drawing time is
   * absorbed instead of added on top. */
  sys_nabu_waitFrame(16);

  return SCREEN_RUNNING;
}

/*
 * Display top and bottom borders (0x1B1F)
 */
static void drawtb(void) {
  flipflop++;

  if (flipflop & 0x01) {
    nabu_hchar(2, 13, 0x40, 6);
    nabu_hchar(9, 13, 0x06, 6);
  } else {
    nabu_hchar(2, 13, 0x05, 6);
    nabu_hchar(9, 13, 0x40, 6);
  }
}

/*
 * Display left and right borders (0x1B7C)
 */
static void drawlr(void) {
  if (flipflop & 0x02) {
    nabu_vchar(2, 12, 4, 8);
    nabu_vchar(2, 19, 4, 8);
  } else {
    nabu_vchar(2, 12, 0x2b, 8);
    nabu_vchar(2, 19, 0x2b, 8);
  }
}

/*
 * Draw the sprite (0x19C6) -- now wired to engine/sprites.c's
 * sprites_paint() (TMS9918A-only port, see that file's header). Pseudo-
 * sprite slot 0 is reserved for this one walking silhouette.
 *
 * BUG FIXED, reported directly ("SPR0B0/1/2.DAT are still being
 * requested" after the gameplay-side fix): load_pattern used to be a
 * literal 1 on every call, on the theory that "spnum can change every
 * call and the pattern reload is cheap" -- true for the VRAM-write cost
 * itself, but NOT for what feeds it now: sprites_paint()'s own F18A ECM
 * color-plane logic (engine/sprites.c) treats every load_pattern==1 call
 * as a real cache-or-stream decision, so reloading unconditionally meant
 * re-fetching color-plane data even on ticks where spnum hadn't actually
 * changed (several of screen_imapsl[]'s own steps, dat_screens.c, are
 * single-frame loops that hold the same spnum across many consecutive
 * anim() ticks). Gated on an actual change the same way tiles_setBank()
 * (engine/tiles.c) already gates its own reloads -- lastSpnum starts at
 * a value (0xffff) no real spnum (a U8 range table) can ever equal, so
 * the very first real draw still correctly reloads once. */
static void drawsprite(void) {
  static U16 lastSpnum = 0xffff;
  U16 x = 104 + ((spx << 1) & 0x1C);
  U16 y = 24 + (spy << 1);
  U16 changed = (spnum != lastSpnum);
  lastSpnum = spnum;
  sprites_paint(spnum, 0, 0, x, y, changed);
  /* Hardware sprites 0-3 only -- end the VDP's sprite list right after so
   * nothing left over from gameplay shows through. */
  sprites_endList(4);
}

/*
 * Draw the background (0x1AF1)
 */
static void drawcenter(void) {
  /* The reference's real tn0[] (xrick/src/scr_imap.c), unchanged, one
   * center-background tile number per map -- all 5 entries now reachable
   * via screen_introMap_sel (screens.h), the 5th ("much much later") an
   * epilogue shown on request rather than after a real map 4. */
  static const U8 tn0[5] = { 0x07, 0x5B, 0x7F, 0xA3, 0xC7 };
  U16 i;
  U16 tn;

  tn = tn0[screen_introMap_sel];

  for (i = 0; i < 6; i++) {
    nabu_vdpwriteinc((U16)(fb_at(104, 24 + 8 * i) + gImage), tn, 6);
    tn += 6;
  }
}

/*
 * Next Step (0x1A74)
 */
static void nextstep(void) {
  if (screen_imapsteps[step].count) {
    count = screen_imapsteps[step].count;
    spdx = screen_imapsteps[step].dx;
    spdy = screen_imapsteps[step].dy;
    spbase = screen_imapsteps[step].base;
    spoffs = 0;
    step++;
  } else {
    run = 0;
  }
}

/*
 * Anim (0x1AA8)
 */
static void anim(void) {
  U16 i;

  if (run) {
    i = screen_imapsl[spbase + spoffs];
    if (i == 0) {
      spoffs = 0;
      i = screen_imapsl[spbase];
    }
    spnum = i;
    spoffs++;
    spx = (U16)(spx + spdx);
    spy = (U16)(spy + spdy);
    count--;
    if (count == 0) {
      nextstep();
    }
  }
}

/*
 * Initialize (0x1A43)
 */
static void init(void) {
  run = 0;
  run--; /* verbatim from the reference: sets run = 0xffff (i.e. "true"
          * as an unsigned wraparound) -- see xrick/src/scr_imap.c */
  /* The real screen_imapsofs[] (xrick/src/scr_imap.c, dropped from this
   * port along with the other four maps' own text -- see this file's
   * header) is { 0x00, 0x03, 0x07, 0x0a, 0x0f }, one starting index per
   * map into this SAME shared screen_imapsteps[]/screen_imapsl[] table
   * below (pure data, unchanged from the reference, dat_screens.c's own
   * header) -- no new asset needed, every map's animation was already
   * compiled in, just never reachable past index 0 before this build let
   * screen_introMap_sel pick a map (all 5 entries now, see that
   * variable's own comment, screens.h). */
  static const U8 stepofs[5] = { 0x00, 0x03, 0x07, 0x0a, 0x0f };
  step = stepofs[screen_introMap_sel];
  spx = screen_imapsteps[step].dx;
  spy = screen_imapsteps[step].dy;
  step++;
  spnum = 0; /* NOTE spnum in [8728] is never initialized ? (reference's
              * own comment, carried over verbatim) */
}

#else /* SCREEN_TRIM_INTRO */

void screen_introMap_reset(void) {
}

U16 screen_introMap(void) {
  return SCREEN_DONE;
}

#endif /* SCREEN_TRIM_INTRO */

/* eof */
