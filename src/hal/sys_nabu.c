/*
 * hal/sys_nabu.c
 *
 * STATUS: partial. sys_panic() and sys_nabu_waitFrame()/sys_nabu_pollVblank()
 * are real; sys_init/sys_shutdown/sys_printf/sys_sleep aren't -- deferred
 * until something actually needs them (sys_printf is the next one likely to
 * bite, once IFDEBUG_* macros or ents.c's sys_printf calls get compiled in).
 * sys_gettime()/sys_resettime() have no definition: nothing in this
 * build calls them (screen timeouts count frames instead).
 *
 * sys_panic() intentionally ignores the format string/varargs rather than
 * trying to print them -- there's no text output path wired up yet to
 * print to. A panic halting the machine is arguably correct behavior on
 * its own anyway; making it *informative* is the next step once there's
 * somewhere to show the message.
 */

#include "ricksystem.h"
#include "sys_nabu.h"

void sys_panic(char *fmt, ...) {
  (void)fmt;
  for (;;) {
    /* halt -- something the game engine considers unrecoverable happened.
     * No visible indication yet (see file header); that's the next thing
     * to add once there's a text/console output path. */
  }
}

/* The one timer: every paced loop in the game (gameplay, scrolling, the
 * title/level-select/intro music, game over) waits on the VDP's own 60Hz
 * vertical blank through sys_nabu_waitFrame().
 *
 * REPLACES three busy-waits (sys_nabu_tick()/sys_nabu_tickDelay() for
 * gameplay, sys_nabu_musicTickDelay() for music -- see backups): identical
 * ~0.69ms-per-call loops (148 T-states per inner pass at 3.58MHz, counted
 * from the compiled asm) run AFTER each loop's work, so time was work + a
 * fixed delay -- every extra sprite upload or scroll repaint slowed the
 * whole game, and tempos depended on the CPU/compiler. This waits for a
 * *deadline* instead: work done during the period is absorbed into it.
 *
 * How: VDP register 1 already has the frame interrupt enabled (vdp_initG2Mode()),
 * but the NABU interrupt mask (initNABULib()) never routes it to the CPU,
 * so the VDP just leaves status bit 7 (F) set at each vblank until someone
 * reads the status register. Polled here, and nowhere else reads status
 * during gameplay, so there's no ISR/address-latch race to worry about
 * (reading status resets the VDP's address latch, but this only runs
 * between frames, never mid-write). The F18A's status selector is back at
 * SR0 by the time this runs (vdp_detect() restores it).
 *
 * F is one bit, so two vblanks between reads count as one. The frame's
 * own work can take longer than a vblank (sprite uploads, scrolling), so
 * sys_nabu_pollVblank() is called at a few safe points DURING the frame
 * (between whole VDP operations, never mid-write -- see its call sites)
 * to catch each vblank as it happens. Anything still missed only ever
 * makes that one frame longer, never shorter. Exact counting would need a
 * VDP interrupt handler, which is unsafe here while some VDP writers still
 * run with interrupts enabled. Reading status in the same cycle the VDP
 * sets F can also swallow that vblank (a known TMS9918A race) -- rare,
 * costs one extra vblank.
 *
 * periodMs is rounded UP to whole vblanks (16.7ms each): GAME_PERIOD 50 ->
 * 3 (20fps; the TI original used 66 = 4 = 15fps; the 'F' key sets 0 = no frame limit),
 * GAMEOVER_PERIOD 33 -> 2, SCROLL_PERIOD 24 -> 2, the music screens' 16 ->
 * 1 (their tempo is sounds.h's MUSIC_RATE_xx, not the loop period).
 *
 * Returns how many vblanks this frame actually took (0 is possible with no
 * limit), so time-based things like sound effects can follow real time
 * rather than the nominal period -- main.c's main loop. */
static U8 sys_nabu_vblanks;

void sys_nabu_pollVblank(void) {
  if (IO_VDPLATCH & 0x80) {
    sys_nabu_vblanks++;
  }
}

U8 sys_nabu_waitFrame(U16 periodMs) {
  U8 target = (U8)((periodMs + 16) / 17);
  U8 took;

  do {
    sys_nabu_pollVblank();
  } while (sys_nabu_vblanks < target);
  took = sys_nabu_vblanks;
  sys_nabu_vblanks = 0;
  return took;
}
