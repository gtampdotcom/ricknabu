/*
 * engine/include/config.h -- ported from xrick/include/config.h
 *
 * Changes from the TI source:
 *   - Dropped the CLASSIC99 branches (that's tursilion's TI dev-environment
 *     emulator target, meaningless on NABU).
 *   - Dropped nBank / SWITCH_IN_BANK* entirely, rather than defining them as
 *     no-ops. They exist solely to select a TI cartridge ROM bank by writing
 *     to a magic address (0x6000 + 2*bank) -- NABU has no cartridge banking,
 *     so there is nothing for these to do. Per-file policy (see engine/util.c):
 *     when porting a file that calls SWITCH_IN_BANK*, delete the calls, don't
 *     stub them -- leaving no-op ceremony around every banked-data access
 *     just obscures which data used to be paged and adds nothing on a flat
 *     64KB target.
 *   - No compile-time F18A macro. The TI build compiles two *separate*
 *     binaries (9918A and F18A) selected by a bootloader; NABU can't afford
 *     two copies of anything in 64KB, so this port is a single binary that
 *     branches at *runtime* on sysvid_nabu_hasF18A (see hal/f18a_nabu.c).
 *     Any file being ported that has `#ifdef F18A ... #else ... #endif`
 *     needs that turned into `if (sysvid_nabu_hasF18A) { ... } else { ... }`
 *     as part of its own port -- there is no macro to lean on here.
 *   - ENABLE_KEYBOARD is left undefined, same call the TI port made (too many
 *     overlapping keys needed for a custom scan/layout it decided not to
 *     build). Worth reconsidering later since nabulib's keyboard support is
 *     more straightforward than the TI's, but keeping parity for now so
 *     ported files don't need their keyboard-optional branches touched yet.
 */

#ifndef _CONFIG_H
#define _CONFIG_H

#include "ricksystem.h"

/* version */
#define VERSION "nabu-0.0.1"

/* joystick support - mandatory, same as the TI build (nabulib has native
 * joystick support, see NABU-LIB.h's joystick/joyStatus). */
#define ENABLE_JOYSTICK

/* keyboard support - disabled, see file header note above. */
#undef ENABLE_KEYBOARD

/* STALE, kept only as a marker of the old design: this used to gate
 * whether sounds_play()'s dropped effects compiled in at all (see git
 * history), back when every real effect call site was wrapped in
 * `#ifdef ENABLE_SOUND`. All of that sound work came back ages ago as
 * direct, unconditional sounds_play*() calls (engine/sounds.c) -- this
 * macro hasn't gated anything real for a long time. SOUND_ENABLED below
 * is the REAL, current master switch. */
#undef ENABLE_SOUND

/* Master switch for ALL sound -- music, every effect, sounds_fx_tick(),
 * the works -- on request ("Sound disabled... cache as much as you can
 * to reduce HCCA use"): a real per-frame/per-event cost (resident sound
 * buffers, the FX-tick player, every network-streamed effect .DAT) that
 * a caching-focused test build doesn't need at all. Every sounds_*()
 * function (engine/sounds.c) keeps its EXACT same declaration in
 * sounds.h either way -- every call site throughout e_rick.c/e_bomb.c/
 * e_box.c/e_them.c/main.c/etc stays completely unchanged and compiles
 * fine regardless -- only sounds.c's own function BODIES (real
 * implementation vs. empty stub) and its resident data switch on this.
 * See that file's own top header for the full "how" this is
 * structured. */
#define SOUND_ENABLED 1

/* TEMPORARY test-build knobs, orthogonal to SOUND_ENABLED above and to
 * each other: on request ("what can you disable temporarily... to free
 * enough space to compile and run", later "split music and pcm
 * streaming defines") while the real sound-enable byte-budget work is
 * still in progress. Both are real CODE, not just resident data, so
 * either buys real headroom a data-only cut can't. Gameplay sound
 * EFFECTS (JUMP/BULLET/EXPLODE/WALK/STICK/BOX/SBONUS/etc) are untouched
 * by either one, so this is meant for testing that the game actually
 * boots and plays with real per-action sound feedback, not a silent
 * build. Same "no-op either way" contract as SOUND_ENABLED -- every
 * sounds_music_*()/sounds_playWaaaaa() call site (main.c, engine/
 * scr_imap.c) keeps working unchanged; only sounds.c's own bodies for
 * those specific functions switch to empty stubs. Flip back to 0 (or
 * delete the -D) once real byte trims close the budget gap for good --
 * neither is meant to become a permanent feature cut.
 *
 * SOUND_TRIM_MUSIC: 1 strips music playback (title theme + each map's
 * own intro tune) -- sounds_music_load()/tick()/start()/stop(). Set via
 * build.bat's -DSOUND_TRIM_MUSIC=1.
 *
 * SOUND_TRIM_PCM: 1 strips the WAAAAA cheat-key PCM effect (the bit-
 * banged AY-amplitude sample playback, sounds_playWaaaaa()) -- separate
 * from music since it's a different mechanism (one-shot streamed PCM,
 * not the ticked AY-register player) with a different byte cost. Set
 * via build.bat's -DSOUND_TRIM_PCM=1.
 *
 * SOUND_TRIM_FX: 1 strips every gameplay sound EFFECT (JUMP/BULLET/
 * EXPLODE/WALK/CRAWL/STICK/BOX/SBONUS/SBONUS2/GAMEOVER/PAD/BONUS/DIE/
 * ENT0-8) -- all of them streamed now (SIZE PASS, on request "find
 * enough ram... even if they have to be streamed"), so this frees code
 * only (the two shared streaming helpers plus fourteen thin wrappers),
 * not resident data. Set via build.bat's -DSOUND_TRIM_FX=1. */
#ifndef SOUND_TRIM_MUSIC
#define SOUND_TRIM_MUSIC 0
#endif
#ifndef SOUND_TRIM_PCM
#define SOUND_TRIM_PCM 0
#endif
#ifndef SOUND_TRIM_FX
#define SOUND_TRIM_FX 0
#endif

/* SCREEN_TRIM_TITLE/SCREEN_TRIM_INTRO -- same family/spirit as the
 * SOUND_TRIM_* knobs just above: temporary byte-budget experiment
 * switches, not meant to become permanent feature cuts.
 *
 * SCREEN_TRIM_TITLE: 1 strips the whole title splash screen (main.c's
 * screen_titlepage() -- VDP writes, F18A/9918A splash streaming,
 * palette load, the "press fire"/'1'-'5' wait loop) down to a stub that
 * always returns FALSE, same as a plain fire press with no digit chosen
 * -- the game lands straight on screen_levelSelect() instead. Set via
 * build.bat's -DSCREEN_TRIM_TITLE=1.
 *
 * SCREEN_TRIM_INTRO: 1 strips the whole per-map intro screen (engine/
 * scr_imap.c's screen_introMap() -- title/body text, animated border,
 * walking-Rick preview, its own captured music, plus dat_screens.c's
 * screen_imapsl[]/screen_imapsteps[] animation data, which has no other
 * reader) down to a stub that reports done immediately -- level select
 * and map transitions jump straight into gameplay with no map-name
 * card shown first. Set via build.bat's -DSCREEN_TRIM_INTRO=1. */
#ifndef SCREEN_TRIM_TITLE
#define SCREEN_TRIM_TITLE 0
#endif
#ifndef SCREEN_TRIM_INTRO
#define SCREEN_TRIM_INTRO 0
#endif

/* cheats support */
#define ENABLE_CHEATS

/* debug support (was Classic99-only on the TI side; no NABU equivalent yet) */
#undef DEBUG /* see include/debug.h */

/* OFF BY DEFAULT, on request ("send the filename being loaded to the
 * HCCA using the debug output"): reintroduces the direct-to-IA-console
 * debug print (hal/sys_nabu_load.c's sys_nabu_debugPrint(), opcode
 * 0xba/0x1f/<len>/<msg>) that a previous session added, used, then
 * pulled back out purely to reclaim its byte cost once it had served
 * its purpose (see that file's own history comment on this) -- same
 * "real code cost, unused or not, because this is one unity-build
 * translation unit" reasoning as the other TRIM/ENABLED knobs in
 * this file. 1 makes sys_nabu_loadAssetOffset() print the full resolved
 * path (e.g. "CPM/N/1/RICK.DAT") to the IA console -- right before every
 * network fetch, cached or not (sys_nabu_loadAssetCached() calls
 * through to the same function, so both paths are covered by this one
 * call site). Non-ASCII bytes show as hex automatically, so this same
 * sys_nabu_debugPrint() works for dumping raw data, not just filenames,
 * if that's useful later. Set via build.bat's -DDEBUG_LOAD_FILENAMES=1;
 * pull it back to 0 once done, same as every knob above. */
#ifndef DEBUG_LOAD_FILENAMES
#define DEBUG_LOAD_FILENAMES 0
#endif

#endif

/* eof */
