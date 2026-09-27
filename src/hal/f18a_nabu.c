/*
 * hal/f18a_nabu.c
 *
 * F18A detection and raw enhanced-register access for NABU.
 *
 * This is a direct port of the detection trick in rickti's
 * xrick/src/crt0_rickcart.assembly (the "check for F18A" block): unlock the
 * F18A's on-chip GPU, load a tiny GPU program into VRAM that writes a known
 * byte, trigger it, then read the byte back. On a stock TMS9918A there's no
 * GPU, so the byte never changes; on an F18A it does.
 *
 * The trick lives entirely in VDP register/data port writes, which is chip
 * behavior, not CPU behavior -- so the byte sequences below are copied
 * as-is from the TI assembly, just issued through NABU's I/O ports
 * (IO_VDPDATA = 0xA0 data, IO_VDPLATCH = 0xA1 register/command, per
 * NABU-LIB.h) instead of the TI's memory-mapped >8C00/>8C02.
 *
 * F18A register protocol (same VDP wire encoding):
 *   To write VDP register N with value V: send V to IO_VDPLATCH, then send
 *   (0x80 | N) to IO_VDPLATCH.  Registers 0-7 are standard TMS9918A;
 *   registers 47-57 are F18A extended (accessed after unlock).
 *
 * Palette protocol:
 *   Write reg 24 with the first palette index * 2 (auto-increment enabled),
 *   then stream 12-bit 0RGB values as byte pairs (high nibble, low byte) to
 *   IO_VDPDATA.  F18A palette entries are 12-bit: bits 11-8 = R, 7-4 = G,
 *   3-0 = B (same layout as the rickti source's splashf18_pal[] array).
 *   Each entry is sent as two bytes: high byte = (val >> 8) & 0x0F,
 *   low byte = val & 0xFF.
 **
 * vdp_detect() below is a second, independent detection method (ported from
 * a standalone green.c test, not from rickti) that reads the F18A/Pico9918
 * ID bits out of VDP Status Register 1 instead of running the GPU-program
 * trick above. sysvid_nabu_init() (hal/sysvid_nabu.c) uses *this* one to
 * decide sysvid_nabu_hasF18A/the init path, since it alone can tell a
 * genuine F18A apart from a Pico9918 in F18A-compatible mode -- the
 * GPU-program probe above can't make that distinction, so it's kept for
 * reference/verification but isn't what drives anything anymore.
 */

#define BIN_TYPE BIN_HOMEBREW
#include "sysvid_nabu.h"
#include "NABU-LIB.h"

/* Real runtime variable only when NEITHER real-hardware-only flag makes
 * it a compile-time macro instead (sysvid_nabu.h's own declaration
 * comment) -- VDP_TARGET_9918A_ONLY added, on request ("add a 9918A_ONLY
 * that excludes all the F18A data+code"), mirroring VDP_TARGET_F18A_ONLY
 * exactly. */
#if !VDP_TARGET_F18A_ONLY && !VDP_TARGET_9918A_ONLY
uint8_t sysvid_nabu_hasF18A = 0;
#endif
vdp_type_t gVdpHardwareType = VDP_TYPE_STOCK;

#if !VDP_TARGET_9918A_ONLY
/* Verbatim from f18unlock/f18test/f18run in crt0_rickcart.assembly, just
 * expanded from TI's big-endian words into the individual bytes that were
 * actually shifted out by the `movb` loop. f18_unlock[] is still real --
 * f18a_unlock() below (a real, called function) uses the same unlock
 * sequence. f18_test[]/f18_run[] are only used by
 * sysvid_nabu_detectF18A() below, so they're wrapped out together with it.
 * The whole thing (f18_unlock[] included) is gated out entirely under
 * VDP_TARGET_9918A_ONLY too -- real F18A hardware can never be detected
 * on that build, so nothing below this point (through f18a_loadPalette()'s
 * own closing #endif) has a reachable caller left. */
static const uint8_t f18_unlock[6] = { 0x1c, 0xb9, 0x1c, 0xb9, 0x00, 0x50 };
#if 0
static const uint8_t f18_test[8]   = { 0x00, 0x00, 0x07, 0x20, 0x10, 0x00, 0x03, 0x40 };
static const uint8_t f18_run[4]    = { 0x10, 0xb6, 0x02, 0xb7 };
#endif

/* SIZE PASS: wrapped out -- confirmed zero callers project-wide. Superseded
 * by vdp_detect() (hal/sysvid_nabu.c), which sets sysvid_nabu_hasF18A from
 * gVdpHardwareType directly and is what sysvid_nabu_init() actually calls;
 * this GPU-program probe was the original detection method before that,
 * left linkable but unreferenced. f18_unlock/f18_test/f18_run above are
 * only used by this function, so they go dead with it (harmless -- const
 * data with no reader still costs bytes, but the compiler can't know that
 * until the function using it is gone too). */
#if 0
uint8_t sysvid_nabu_detectF18A(void) {
  uint8_t i;
  uint8_t result;

  /* Unlock: 6 bytes to the register/command port. */
  for (i = 0; i < 6; i++) {
    IO_VDPLATCH = f18_unlock[i];
  }

  /* Load the test GPU program: 8 bytes to the data port. */
  for (i = 0; i < 8; i++) {
    IO_VDPDATA = f18_test[i];
  }

  /* Trigger it: 4 bytes back to the register/command port. */
  for (i = 0; i < 4; i++) {
    IO_VDPLATCH = f18_run[i];
  }

  /* li r1,>0010 / movb / swpb / movb -- writes 0x00 then 0x10 to the
   * register port (sets the GPU start address and kicks it off). */
  IO_VDPLATCH = 0x00;
  IO_VDPLATCH = 0x10;

  /* Read back the byte the GPU program should have written. On a stock
   * 9918A nothing ran, so this stays whatever was already there. */
  vdp_setReadAddress(0x0000);
  result = IO_VDPDATA;

  sysvid_nabu_hasF18A = (result != 0);
  return sysvid_nabu_hasF18A;
}
#endif

/* ---------------------------------------------------------------------------
 * F18A extended register helpers
 * ---------------------------------------------------------------------------
 * These must only be called while the F18A is unlocked -- callers call
 * f18a_unlock() first (harmless to repeat). There is deliberately no
 * f18a_lock(): see the comment where it used to be, below.
 * ---------------------------------------------------------------------------
 */

/* Unlock the F18A extended-register set.
 * Identical byte sequence to the detection unlock above -- the F18A
 * requires this pair of identical writes to enter enhanced mode. */
void f18a_unlock(void) {
  uint8_t i;
  for (i = 0; i < 6; i++) {
    IO_VDPLATCH = f18_unlock[i];
  }
}

/* f18a_lock() REMOVED -- never relock. Relocking (any write to VR57) puts
 * the F18A back into TMS9918A-compatible mode, which also turns OFF the
 * enhanced features set while unlocked -- including ECM3 8-colour sprites
 * (VR49), leaving only bit plane 0 drawn: single-colour outlines. Found via
 * Marduk 0.28, which emulates this correctly; Marduk 0.27 and PICO9918
 * firmware before v1.3.0 ignore a manual relock (per the Marduk/PICO9918
 * author), which is why the old lock-after-every-change pattern appeared to
 * work. Once unlocked, the F18A stays unlocked for the whole session; the
 * game only ever writes VDP registers 0-7 through the normal path, which
 * behave the same either way. tools/ecmtest reproduces this. */

/* Write an F18A extended register (0-57).  Must be called while unlocked. */
void f18a_setRegister(uint8_t reg, uint8_t val) {
  IO_VDPLATCH = val;
  IO_VDPLATCH = (uint8_t)(0x80 | reg);
}

/* Load `count` palette entries starting at palette register `first`.
 *
 * Real F18A palette protocol (per matthew180's own description of VR47,
 * atariage "F18A programming, info, and resources"): there is no separate
 * "palette address register" at 24 -- that was wrong. Register 47 is a
 * combined control register:
 *
 *   bits 0-5 : PR0-5     starting palette register (0-63)
 *   bit  6   : AUTO INC  keep incrementing PR after each 2-byte entry
 *   bit  7   : DPM       Data Port Mode -- while set, ordinary VDP
 *                        data-port writes (the same IO_VDPDATA port
 *                        normal VRAM writes use) go to palette RAM
 *                        instead of VRAM
 *
 * So loading a palette means: unlock, write VR47 = 0xC0 | first (DPM=1,
 * AUTO INC=1, starting register = first), then stream count*2 bytes to
 * IO_VDPDATA exactly the way a VRAM write would -- except while DPM is set
 * those bytes latch into palette registers instead. Each entry is two
 * bytes, high nibble then low byte of the 12-bit 0RGB value (matches
 * pic_splashf18_pal[]'s 0x0RGB layout exactly).
 *
 * The previous version of this function instead did f18a_setRegister(24,
 * first*2) -- register 24 isn't a palette register at all here, and that
 * call is a plain register write (via the reg|0x80 latch protocol), which
 * does NOT touch DPM. With DPM never set, the "palette" bytes that
 * followed just went out as ordinary IO_VDPDATA writes to whatever VRAM
 * address vdp_setWriteAddress() had last left the pointer at (in
 * title_draw()'s case, that's the tail end of the identity name-table
 * fill, i.e. it was quietly corrupting ~32 bytes right around the sprite
 * attribute table). The actual F18A palette registers were never touched,
 * so the chip kept rendering with its power-on default palette -- which
 * approximates the standard TMS9918A 16-color set. That's exactly why the
 * title screen came out with a blue-on-white "RICK DANGEROUS" and a green
 * face instead of the intended colors: index 4 is Dark Blue and index 15
 * is White in that default set, index 2 is Medium Green, etc. -- not an
 * inverted palette, just the *wrong* (default) one.
 *
 * DPM stays active across all `count` entries because AUTO INC is set;
 * per matthew180, once in that mode it only exits on: rewriting VR47,
 * the palette address rolling over to 0, a VDP status-register read, or
 * an external VDP reset. title_draw() does plain VRAM writes (pattern/
 * color tables) right after this call, so DPM must be turned back off
 * explicitly here (relocking via VR57 is not a documented DPM-exit
 * condition, and this port never relocks anyway -- see f18a_lock()'s
 * removal note above). */
void f18a_loadPalette(const U16 *pal, uint8_t first, uint8_t count) {
  uint8_t i;
  U16 v;

  /* Enter Data Port Mode: DPM=1, AUTO INC=1, starting palette register =
   * `first` (bits 0-5). */
  f18a_setRegister(47, (uint8_t)(0xC0 | (first & 0x3F)));

  for (i = 0; i < count; i++) {
    v = pal[i];
    IO_VDPDATA = (uint8_t)((v >> 8) & 0x0F);  /* high nibble (R) */
    IO_VDPDATA = (uint8_t)(v & 0xFF);          /* low byte  (GB) */
  }

  /* Exit Data Port Mode now, rather than leaving it to whichever of
   * matthew180's documented exit conditions happens to fire next.  Without
   * this, DPM stays on after we return -- a VR57 write is NOT one of the
   * documented exit conditions, so the *next* IO_VDPDATA
   * writes a caller makes (e.g. title_draw()'s pattern-table blit)
   * keep going to palette RAM instead of VRAM until PR happens to wrap
   * from 63 back to 0. That silently ate the first (64-first-count)*2
   * bytes of whatever was written right after this call -- which, for the
   * splash screen, is the start of pic_splashf18_pat[], i.e. the top of
   * the picture where the title sits. Rewriting VR47 with DPM=0 is one of
   * the documented exit conditions, so this reliably drops us back into
   * normal VRAM-write mode regardless of first/count. */
  f18a_setRegister(47, 0x00);
}
#endif /* !VDP_TARGET_9918A_ONLY */

/* ---------------------------------------------------------------------------
 * vdp_detect() -- Status Register 1 ID-bit probe (ported from green.c)
 * ---------------------------------------------------------------------------
 * See the file header comment above for how this relates to
 * sysvid_nabu_detectF18A().
 *
 * #if'd out entirely under VDP_TARGET_9918A_ONLY, on request ("VDP
 * detection also needs to be skipped"): that build already knows the
 * answer at compile time (sysvid_nabu_hasF18A is a `0` macro there),
 * so this whole probe -- and its one caller, main.c -- would just be
 * wasted boot-time work and wasted image bytes for a question that's
 * already answered. Definition gated, not just the call site, same
 * "don't rely on dead-code elimination alone" discipline every other
 * F18A-only primitive in this project uses.
 */
#if !VDP_TARGET_9918A_ONLY

/* NOP delay to satisfy VDP port recovery timing (~8us), same as green.c. */
#define VDP_DETECT_WAIT() __asm__("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n")

/* Direct VDP register write via the control/latch port. Same wire protocol
 * as f18a_setRegister() above (value byte, then reg | 0x80), just with the
 * recovery delays this detection method relies on. */
static void vdp_detect_write_reg(uint8_t reg, uint8_t val) {
  IO_VDPLATCH = val;
  VDP_DETECT_WAIT();
  IO_VDPLATCH = (uint8_t)(reg | 0x80);
  VDP_DETECT_WAIT();
}

/*
 * Probes hardware for F18A / Pico9918 features and distinguishes between a
 * genuine F18A and a Pico9918 running in F18A-compatible mode.
 *
 * Method: the F18A and Pico9918 both force the top bits of Status
 * Register 1 (the "ID" register) to identify themselves. The status
 * register to read back is selected via VR15 (NOT VR14 - VR14 has a
 * different purpose). Reading is done twice, since the first read after
 * selecting can still carry stale reset-on-read flag bits.
 *
 * - Stock TMS9918A: top 3 bits of SR1 are never all "111".
 * - F18A / Pico9918: top 3 bits of SR1 = "111" (mask 0xE0 == 0xE0).
 * - Pico9918 additionally sets bit 3 high (mask 0xE8 == 0xE8),
 *   where a genuine F18A leaves it low.
 *
 * Must run before initNABULib()/sysvid_nabu_init() touch the VDP -- those
 * calls put the VDP into a mode/register state this probe doesn't expect.
 * main() calls this first thing, before anything else.
 */
vdp_type_t vdp_detect(void) {
  uint8_t id_val;

  /* 1. Disable interrupts to prevent ISRs from breaking timing */
  __asm__("di");

  /* 2. Clear VDP byte latch */
  (void)IO_VDPLATCH;
  VDP_DETECT_WAIT();

  /* 3. Send unlock sequence to VR57 (0x39) twice */
  vdp_detect_write_reg(57, 0x1C);
  vdp_detect_write_reg(57, 0x1C);

  /* 4. Select Status Register 1 (the ID register) via VR15 */
  vdp_detect_write_reg(15, 1);

  /* 5. First read can carry stale/reset-on-read bits from before the
   *    select took effect - discard it. */
  (void)IO_VDPLATCH;
  VDP_DETECT_WAIT();

  /* 6. Second read holds the stable ID bits */
  id_val = IO_VDPLATCH;
  VDP_DETECT_WAIT();

  /* 7. Restore the status register selector back to SR0 (default) */
  vdp_detect_write_reg(15, 0);

  /* 8. Re-enable interrupts */
  __asm__("ei");

  if ((id_val & 0xE8) == 0xE8) {
    return VDP_TYPE_PICO9918;
  } else if ((id_val & 0xE0) == 0xE0) {
    return VDP_TYPE_F18A;
  }

  return VDP_TYPE_STOCK;
}
#endif /* !VDP_TARGET_9918A_ONLY */