/*
 * hal/sysvid_nabu.h
 *
 * NABU implementation of rickti's sysvid.h HAL contract, targeting nabulib's
 * TMS9918A Graphics II driver for the base path, plus an F18A-enhanced path
 * mirroring rickti's own TI design (runtime-detect F18A, fall back to plain
 * 9918A if it's not installed). NABU can genuinely take an F18A as a
 * pin-compatible VDP swap (it's used there for 80-column Cloud CP/M), so
 * this dual-path split is worth preserving rather than dropping.
 *
 * STATUS: G2 bring-up, F18A detect/palette, title pics, and tile banks are
 * wired. Still outstanding: sprites (ti/spr*.c and ti/f18a/sprf*.c), maps,
 * and draw.c's remaining VDP traffic.
 */

#ifndef _SYSVID_NABU_H
#define _SYSVID_NABU_H

/* VDP_TARGET_9918A_ONLY -- mirror of VDP_TARGET_F18A_ONLY further down,
 * on request ("we have a VDP_TARGET_F18A_ONLY, add a 9918A_ONLY that
 * excludes all the F18A data+code"). Declared first, before anything
 * that reads it (SPRITE_ECM_ENABLED just below, sysvid_nabu_hasF18A
 * further down) -- a -D on the command line would already be visible
 * regardless of ordering, but this file's own #ifndef default needs to
 * exist before its first real use to avoid depending on "undefined in
 * #if means 0" preprocessor behavior. See VDP_TARGET_F18A_ONLY's own
 * comment for why mutual exclusivity is enforced (right after both
 * flags are defined) and what this one strips: sysvid_nabu_hasF18A
 * becomes a compile-time `0` (mirrors the `1` case exactly -- every
 * `if (sysvid_nabu_hasF18A)` throughout the codebase becomes `if (0)`
 * and the compiler drops the F18A branch on its own), SPRITE_ECM_ENABLED
 * defaults to 0 with it (ECM is 100% F18A-specific -- sprite_index_map[],
 * the per-page palettes, every ecm_* function would be pure dead weight
 * on hardware that can never report hasF18A true), and the F18A
 * register-access primitives themselves (hal/f18a_nabu.c's
 * f18a_unlock()/f18a_setRegister()/f18a_loadPalette(),
 * hal/sysvid_nabu.c's sysvid_nabu_initF18A()) are fully #if'd out, not
 * just uncalled -- same "definition, not just call site" discipline
 * VDP_TARGET_F18A_ONLY's own sysvid_nabu_init9918() already uses, so
 * none of their code costs image bytes either (vdp_detect() itself
 * stays -- see its own declaration comment for why it's not worth
 * gating).. */
#ifndef VDP_TARGET_9918A_ONLY
#define VDP_TARGET_9918A_ONLY 0
#endif

/* Master switch for F18A 8-color sprite mode (ECM=3) -- FIXED, on
 * request ("add support for the TI-99's version multicolor F18A
 * sprites"), after two earlier attempts (see engine/sprites.c's own
 * comment right above SPRITE_ECM_PAL_RICKWALK for the full root-cause
 * writeup: register 6, the sprite pattern table base, was never actually
 * written, so the chip's own ECM color-plane address computation never
 * matched where the software placed the data). Shared across
 * engine/sprites.c, hal/sysvid_nabu.c, and main.c's one call site, same
 * as before. Needs real hardware/MAME confirmation -- this fix is based
 * on the real F18A hardware spec (visrealm/pico9918 wiki) and the
 * reference cartridge's own verified VRAM map, not another guess, but
 * neither of those substitutes for actually seeing it render. */
#ifndef SPRITE_ECM_ENABLED
#define SPRITE_ECM_ENABLED (!VDP_TARGET_9918A_ONLY)
#endif

/* Compile-time hardware/level scoping for the ECM feature above, on
 * request ("speed it up now by doing less streaming data... compile
 * time options... TMS9918, F18A or both... level 1 only, level 2 only
 * etc"):
 *
 *   TMS9918 only: set SPRITE_ECM_ENABLED to 0 above (already existed) --
 *     no ECM code, no sprite_index_map[], no per-page palette loads or
 *     streaming at all, on either hardware.
 *
 *   F18A only: VDP_TARGET_F18A_ONLY below, on request ("compile for F18A
 *     only") -- REVISED from this comment's own earlier claim that
 *     there was "nothing real to trim" here: sysvid_nabu_hasF18A becomes
 *     a compile-time `1` instead of a runtime variable (see its own
 *     declaration further down), so every `if (sysvid_nabu_hasF18A)`
 *     throughout the whole codebase (main.c/tiles.c/sprites.c/maps.c/
 *     game.c) becomes `if (1)` at compile time and the compiler dead-
 *     code-eliminates the stock-9918A `else` branch automatically, no
 *     per-call-site edits needed anywhere. sysvid_nabu_init9918() itself
 *     (hal/sysvid_nabu.c) is also fully #if'd out, not just uncalled, so
 *     its own code doesn't cost image bytes either. A build made this
 *     way will misbehave (or simply show no color) on real stock
 *     TMS9918A hardware -- this is a deliberate one-way trade for a
 *     smaller, faster F18A-only build, not a safe default.
 *
 *   Which PAGES' real color data get streamed, when SPRITE_ECM_ENABLED
 *   is on -- the real lever for "less streaming data". NOT quite the
 *   same thing as "which level", even though the naming below groups
 *   them that way for convenience: sprite_index_map[] (engine/
 *   sprites.c) is one GLOBAL table shared by every map, not one per
 *   level, so a few sprites genuinely live on a page that doesn't match
 *   where they're actually used -- confirmed directly: the "spear guy"
 *   enemy appears on SAMERICA (level 1) but its real color data is on
 *   page 3, nominally grouped under MBASE below. Turning off
 *   SPRITE_ECM_PAGE_MBASE to speed up a SAMERICA-only build/test session
 *   would ALSO turn that one SAMERICA enemy back to plain white -- a
 *   real, known exception, not a bug in this scoping mechanism. Each
 *   page turned off here skips BOTH its own streaming attempts
 *   (sprite_ecm_lookup(), engine/sprites.c, returns "no color data"
 *   immediately, no network round-trip) AND its own palette load at
 *   boot (set_halfbitmap(), hal/sysvid_nabu.c) -- a sprite on a disabled
 *   page still renders correctly, just single-color (the same white
 *   fallback every sprite used before this whole feature existed).
 *   Default: everything on (this session's own "all sprites, all
 *   levels" state).
 *
 *   CHANGED, on request ("F18A only, SAMERICA only... cache as much as
 *   you can"): EGYPT off below to free real budget for the caching work
 *   this same request asked for -- nothing reported so far shows
 *   SAMERICA using page 1 for anything. CASTLE and MBASE both stay ON
 *   despite "SAMERICA only", though: page 2 is where SAMERICA's OWN
 *   boulder lives, and page 3 (grouped under MBASE) is where SAMERICA's
 *   OWN "spear guy" enemy lives -- both cross-page exceptions this
 *   comment already documents above. Turning either off would undo an
 *   already-confirmed-working SAMERICA fix, not just drop unused CASTLE/
 *   MBASE content. */
/* CORRECTED, on request (auditing "what's taking up BSS"/"is any boulder
 * data still streaming" led to actually computing sprite_index_map[]'s
 * real page for every one of SAMERICA's own 24 real "extra" sprite
 * numbers, not just assuming): "not used by anything on SAMERICA so far"
 * below was WRONG -- 12 of those 24 (48-55, 86, 87, 93, 121) genuinely
 * map to page 1/EGYPT. With EGYPT off, every one of those 12 entities
 * rendered in ECM's white/single-color fallback instead of its real
 * F18A colors -- a real, live correctness bug, not just an unnecessary
 * page. Back on. */
#define SPRITE_ECM_PAGE_SAMERICA 1  /* page 0 -- most entities on every map */
#define SPRITE_ECM_PAGE_EGYPT    1  /* page 1 -- 12 of SAMERICA's own real 24 extra sprites live here */
/* TRIED AND REVERTED, on request ("add back the stock TMS9918
 * compatibility" left only 61 bytes of headroom, confirmed real
 * corruption at gameplay start): turning ANY page off here is now WORSE
 * than useless for headroom, not a saving -- it makes ECM_ALL_PAGES_
 * ENABLED false, which reactivates the WHOLE pre-this-session fallback
 * architecture that engine/sprites.c's walkIdx redesign (see that file's
 * own comment, "add back the stock TMS9918 compatibility") was
 * specifically built to keep dead: sprites_them_walk[] (512 bytes),
 * sprites_map1_extra_nums[]'s full 44-entry size (88 bytes) instead of
 * its 1-entry placeholder, sprites_map1_extra_cached[]/_scratch[] (256
 * bytes), AND -- by far the worst -- hal/sysvid_nabu.h's own
 * SPRITES_DATA0_RESIDENT_SIZE condition includes ECM_ALL_PAGES_ENABLED,
 * so it falls back to a literal 6144 bytes instead of its placeholder
 * size. Tried CASTLE=0 expecting to free bytes;
 * measured -6940 bytes (went from +61 headroom to badly over) instead.
 * Confirmed by direct rebuild, not guessed -- do not use this lever for
 * headroom under the current design; any real margin has to come from
 * elsewhere (or a further redesign of the walkIdx<0 fallback itself to
 * also stream instead of needing full residency, not attempted here). */
#define SPRITE_ECM_PAGE_CASTLE   1  /* page 2 -- needed for SAMERICA's own boulder */
#define SPRITE_ECM_PAGE_MBASE    1  /* pages 3+4 -- needed for SAMERICA's own "spear guy" (page 3) */

/* Derived: true only when every page above is on, meaning
 * sprite_ecm_lookup() (engine/sprites.c) can never return -1 for any
 * real spriteNumber (0-212) -- every sprite's real F18A color data is
 * reachable regardless of which page it lives on. When true, the
 * generic "extra sprite" fallback mechanism (sprites_map1_extra_cached/
 * _scratch, engine/sprites.c) becomes permanently unreachable: its
 * entire reason to exist is serving spriteNumbers whose page is
 * disabled (walkIdx<0), and there are none left. Guards that whole
 * mechanism's resident buffers out when true (256 bytes: a 128-byte
 * cache slot + a 128-byte scratch slot). */
#define ECM_ALL_PAGES_ENABLED \
  (SPRITE_ECM_PAGE_SAMERICA && SPRITE_ECM_PAGE_EGYPT && \
   SPRITE_ECM_PAGE_CASTLE && SPRITE_ECM_PAGE_MBASE)

/* F18A-only build, on request ("compile for F18A only") -- see this
 * file's own SPRITE_ECM_PAGE_* comment above for the full "how" and
 * "why this trades away stock TMS9918A support" writeup.
 *
 * Real runtime sysvid_nabu_hasF18A / genuine sysvid_nabu_init9918()
 * dual-hardware support, on request ("add back the stock TMS9918
 * compatibility") -- took two real false starts to land safely, both
 * worth remembering before touching this flag again:
 *
 *   1. engine/sprites.c's own walkIdx redesign (see that file's own
 *      comment) kept the direct cost down to ~880 bytes instead of
 *      sprites_data0[]'s full 6144, and two free trims (sprites_them_
 *      walk[]/sprites_map1_extra_nums[] gating, engine/sprites.c +
 *      engine/maps.c -- these stay regardless of this flag, they're
 *      correct either way) covered most of that, landing at +61 bytes
 *      headroom. NOT ENOUGH: confirmed real corruption at gameplay
 *      start (see [[project_byte_budget_corruption]], a 4th confirmed
 *      instance of this project's own established failure mode).
 *
 *   2. Turning an ECM page off to chase more margin measured -6940
 *      bytes -- actively catastrophic, not just insufficient. Any
 *      SPRITE_ECM_PAGE_* at 0 makes ECM_ALL_PAGES_ENABLED false, which
 *      reactivates the WHOLE pre-redesign fallback architecture the
 *      walkIdx change was built to keep dead, including SPRITES_DATA0_
 *      RESIDENT_SIZE's own fallback to a literal 6144 bytes. This lever
 *      is off the table entirely under the current walkIdx design --
 *      real stock-hardware support would need that walkIdx<0 fallback
 *      path itself redesigned to stream (matching how sprite_ecm_
 *      lookup()'s own shape source already does) before an ECM page
 *      could safely go off; not attempted.
 *
 * What actually closed the gap, on request ("keep FX and stock/f18a
 * hardware support... remove the S key code and most of the text from
 * the level select screen"): removed the 's'/'S' stock-sprite-color
 * cheat key entirely (was engine/sprites.c's sprites_stockColor,
 * main.c's own key handler) and most of the level-select screen's own
 * text (levelSelectCredits[]/levelSelectCheats[], main.c -- the real
 * menu, levelSelectText[], stayed). SOUND_TRIM_FX=1 was measured too
 * (1153 bytes, far more than needed) but declined -- costs every
 * gameplay sound effect again, a bigger trade than this needed. Landed
 * at 283 bytes headroom, comfortably clear of the proven danger zone
 * (corruption confirmed at both 86 and 61 bytes in this project's own
 * history). */
#ifndef VDP_TARGET_F18A_ONLY
#define VDP_TARGET_F18A_ONLY 0
#endif

#if VDP_TARGET_F18A_ONLY && VDP_TARGET_9918A_ONLY
#error "VDP_TARGET_F18A_ONLY and VDP_TARGET_9918A_ONLY are mutually exclusive -- pick at most one real-hardware-only build, or leave both at 0 for genuine dual-hardware detection."
#endif

/* Whether main.c's loadAssets() can skip the SPR0.DAT network fetch (page
 * 0 gameplay sprite shapes, dat_spritesTI0.c's sprites_data0[]). Nothing
 * reads sprites_data0[] at runtime any more: F18A draws page 0 from its
 * ECM data, and stock hardware streams page 0 through sprites.c's
 * page0_cache instead. build.ps1 sets this to 1; 0 forces the load (and
 * the full 6144-byte array) back on. */
#ifndef SKIP_SPR0_GAMEPLAY_LOAD
#define SKIP_SPR0_GAMEPLAY_LOAD (VDP_TARGET_F18A_ONLY && SPRITE_ECM_PAGE_SAMERICA)
#endif

/* sprites_data0[]'s resident size (dat_spritesTI0.c): a 1-byte placeholder
 * when its load is skipped. Literal 6144, not SPRITE_PAGE_SIZE*SPRITE_SIZE
 * (engine/include/sprites.h) -- this file is included earlier in the
 * unity build than sprites.h is, so those macros aren't visible here yet. */
#if SKIP_SPR0_GAMEPLAY_LOAD
#define SPRITES_DATA0_RESIDENT_SIZE 1
#else
#define SPRITES_DATA0_RESIDENT_SIZE 6144
#endif

#include <stdint.h>
#include "ricksystem.h"

/* Matches engine/include/sysvid.h's contract exactly (U16/U8, not
 * uint16_t/uint8_t). These used to be declared with stdint.h types, back
 * before ricksystem.h existed in this port -- harmless while nothing else
 * declared them, but once scroller.h/sysvid.h started declaring the same
 * functions with U16/U8, SDCC treats U16 (unsigned short) and uint16_t
 * (unsigned int, on this target) as genuinely different types and refuses
 * to compile the conflicting prototypes. Fixed here instead of papering
 * over it at just the one call site that happened to surface it. */
void sysvid_update(void);
void sysvid_setGamma(U16 g);
void bitmapcharcopy(U16 adr, const U8 *buf, U16 size);

/* VDP hardware type, distinguished by vdp_detect() (hal/f18a_nabu.c) via the
 * F18A/Pico9918 ID bits in VDP Status Register 1. This is what
 * sysvid_nabu_init() now reads to pick an init path -- see vdp_detect()'s
 * comment for why it's a separate, more precise probe than the old
 * GPU-program trick sysvid_nabu_detectF18A() still does below. */
typedef enum {
  VDP_TYPE_STOCK = 0,  /* Plain TMS9918A (or unrecognized clone) */
  VDP_TYPE_F18A,       /* Genuine F18A */
  VDP_TYPE_PICO9918    /* Pico9918 running in F18A-compatible mode */
} vdp_type_t;

/* Result of the most recent vdp_detect() call. main() sets this by calling
 * vdp_detect() before initNABULib()/sysvid_nabu_init() touch the VDP (see
 * vdp_detect()'s comment for why the ordering matters), so it's already
 * valid by the time sysvid_nabu_init() reads it below. */
extern vdp_type_t gVdpHardwareType;

/* Probes VDP Status Register 1 for F18A/Pico9918 ID bits. Must be called
 * before initNABULib()/sysvid_nabu_init() touch the VDP. Declared
 * (prototype) unconditionally so a stray un-gated call elsewhere would
 * compile-error loudly rather than silently link against a function
 * that no longer exists, but the real DEFINITION (hal/f18a_nabu.c) and
 * its one caller (main.c) are both #if'd out under VDP_TARGET_9918A_ONLY
 * -- on request ("VDP detection also needs to be skipped when
 * VDP_TARGET_9918A_ONLY=1"): that build already knows the answer at
 * compile time (sysvid_nabu_hasF18A is a `0` macro there), so probing
 * real hardware for it is wasted boot-time work on top of wasted image
 * bytes, not just the latter. Still always compiled/called under plain
 * VDP_TARGET_F18A_ONLY, though -- that build's own sysvid_nabu_init()
 * assignment to sysvid_nabu_hasF18A is #if'd out the same way, so the
 * probe's result goes unused there too, but nobody's asked to skip the
 * call itself on that side yet. */
vdp_type_t vdp_detect(void);

/* NABU-specific: called once at startup, not part of the original HAL.
 * Reads gVdpHardwareType (set by main()'s vdp_detect() call) and dispatches
 * to one of the two init paths below. */
void sysvid_nabu_init(void);

/* True if F18A-class hardware (genuine F18A or a Pico9918 in F18A-compatible
 * mode) was detected in place of the stock TMS9918A -- set by
 * sysvid_nabu_init() from gVdpHardwareType. Valid only after
 * sysvid_nabu_init() has run. This is the flag title_draw() (main.c) and
 * friends branch on to pick F18A vs plain-9918A graphics/palette data.
 *
 * CHANGED, on request ("compile for F18A only"): a compile-time `1`
 * instead of a real variable when VDP_TARGET_F18A_ONLY is set (this
 * file's own comment above has the full "why" and "what this trades
 * away") -- every `if (sysvid_nabu_hasF18A)` anywhere in the codebase
 * becomes `if (1)` and the compiler drops the dead stock-9918A branch on
 * its own, no per-call-site changes needed. sysvid_nabu_init() (hal/
 * sysvid_nabu.c) still guards its own ASSIGNMENT to this name (can't
 * assign to a macro) the same way.
 *
 * MIRRORED, on request ("add a 9918A_ONLY that excludes all the F18A
 * data+code"): a compile-time `0` under VDP_TARGET_9918A_ONLY, same
 * mechanism in the opposite direction -- every `if (sysvid_nabu_hasF18A)`
 * becomes `if (0)` and the compiler drops the dead F18A branch instead. */
#if VDP_TARGET_F18A_ONLY
#define sysvid_nabu_hasF18A 1
#elif VDP_TARGET_9918A_ONLY
#define sysvid_nabu_hasF18A 0
#else
extern uint8_t sysvid_nabu_hasF18A;
#endif

/* F18A hardware helpers */
void f18a_unlock(void);
/* No f18a_lock(): relocking turns ECM sprites off -- see f18a_nabu.c. */
void f18a_setRegister(uint8_t reg, uint8_t val);
void f18a_loadPalette(const U16 *pal, uint8_t first, uint8_t count);

/* Plain TMS9918A Graphics II bring-up (always available, matches rickti's
 * "9918A fallback" mode, sprites all one color like the TI build's fallback
 * notes describe). */
void sysvid_nabu_init9918(void);

/* F18A-enhanced bring-up (15-color backgrounds, 8-color sprites w/ 4
 * palettes, per the original readme). */
void sysvid_nabu_initF18A(void);

/* Switches the F18A into 8-color sprite mode (ECM=3) for the rest of
 * gameplay -- call once, F18A-only, when gameplay starts (see main.c's
 * one call site). See hal/sysvid_nabu.c's own header on set_halfbitmap()
 * for the full history: an earlier attempt just flipped the ECM=3
 * register on top of this port's existing monochrome sprite pattern data
 * and got "arbitrary red/white noise" -- ECM=3 reinterprets sprite
 * pattern data as multi-bit-per-pixel color-index data, not the classic
 * 1-bit "color or transparent" scheme, so it needs real per-sprite color-
 * plane data (engine/sprites.c's rick_walk_bit1/bit2[], currently just
 * Rick's own walk cycle) and a matching palette group (sprite_ecm_pal_
 * rickwalk[] below) to mean anything -- both are wired in now, not just
 * the register toggle. Sprites this port hasn't given real color-plane
 * data to fall back to a dedicated plain-white palette group
 * (sprite_ecm_pal_fallback[]) instead of showing the real per-page
 * colors the reference cartridge would (this port hasn't ported those
 * other pages' sprf*pal groups), so they keep looking the way they did
 * before ECM=3 turned on.
 *
 * set_fullbitmap() (the reference's own counterpart) switches back to the
 * normal three-thirds bitmap layout -- main.c's title_draw() calls it,
 * since the title picture needs it after a game has run. No-op if already
 * there. */
void set_halfbitmap(void);
void set_fullbitmap(void);

#endif /* _SYSVID_NABU_H */

