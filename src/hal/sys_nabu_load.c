/*
 * hal/sys_nabu_load.c
 *
 * See sys_nabu_load.h for why this exists. Implementation notes:
 *
 * - rn_FileRead() (hal/RetroNET-FileRead-only.c -- see that file's header
 *   for why this includes it instead of the full vendored
 *   hal/RetroNET-FileStore.h) is the filename-based, handle-less read --
 *   exactly what a one-shot "load this whole asset at startup" call
 *   wants, so there's no rn_fileOpen()/rn_fileHandleClose() pair to
 *   manage here.
 *
 * - rn_FileRead() writes straight into the caller's buffer over HCCA
 *   (byte-at-a-time, inside its own wait loop) rather than through an
 *   intermediate staging buffer, so a single call handles any of this
 *   port's asset sizes (all a few KB) without needing to chunk the read --
 *   the only hard ceiling is readLength's own uint16_t (65535 bytes),
 *   nowhere close to anything loaded here.
 *
 * - It returns the number of bytes actually transferred, not a
 *   success/failure flag, so "did the load work" means comparing that
 *   count against what was asked for -- see sys_nabu_loadAsset() below.
 */

#include "ricksystem.h"
#include "sys_nabu_load.h"
#include "RetroNET-FileRead-only.c"

void sys_nabu_loadRes(U16 res, U8 *dest, U16 size) {
  sys_nabu_loadAssetOffset(RES_FILE, dest, res, size);
}

/* Direct-to-IA-console debug print: opcode 0xba (IA control), 0x1f
 * (print), a length byte, then that many message bytes -- same HCCA
 * primitives rn_FileRead() above already uses (hcca_DiFocusInterrupts()/
 * hcca_DiWriteByte()/hcca_DiWriteBytes()/hcca_DiRestoreInterrupts(),
 * from RetroNET-FileRead-only.c, already #included into this file). The
 * IA shows non-ASCII bytes as hex automatically, so this same call
 * works for raw data dumps, not just ASCII text. Gated behind
 * DEBUG_LOAD_FILENAMES (config.h) rather than compiled in unconditionally
 * -- an earlier session added this exact function, used it, then pulled
 * it back out solely to reclaim its byte cost once done (SDCC links this
 * whole unity build per translation unit, so an unused function still
 * costs real image bytes) -- gating it means it can be switched back on
 * for a debug build without paying that cost in a normal one. */
#if DEBUG_LOAD_FILENAMES
void sys_nabu_debugPrint(const char *msg, U8 len) {
  hcca_DiFocusInterrupts();
  hcca_DiWriteByte(0xba);
  hcca_DiWriteByte(0x1f);
  hcca_DiWriteByte(len);
  hcca_DiWriteBytes(0, len, (U8 *)msg);
  hcca_DiRestoreInterrupts();
}

/* See this function's own declaration comment (hal/sys_nabu_load.h) --
 * 3 zero-padded decimal digits, manually built the same "no snprintf/
 * sprintf" way sys_nabu_loadAssetOffset() below builds its own path,
 * for the same code-size reason. */
void sys_nabu_debugPrintU16(U16 value) {
  char msg[3];
  msg[0] = (char)('0' + (value / 100) % 10);
  msg[1] = (char)('0' + (value / 10) % 10);
  msg[2] = (char)('0' + value % 10);
  sys_nabu_debugPrint(msg, 3);
}
#endif

/* Every asset is in one of two files, RICK.DAT or RICK.SPR (see
 * sys_nabu_load.h), so the path is fixed apart from its 3-letter
 * extension -- no string building needed. */
static char loadPath[] = "CPM/N/1/RICK.DAT";
#define LOAD_PATH_EXT (sizeof(loadPath) - 4)

static void err_show(const char *why);

U16 sys_nabu_loadAssetOffset(const char *ext, U8 *dest, U16 at4, U16 size) {
  U16 n;

  loadPath[LOAD_PATH_EXT] = ext[0];
  loadPath[LOAD_PATH_EXT + 1] = ext[1];
  loadPath[LOAD_PATH_EXT + 2] = ext[2];

#if DEBUG_LOAD_FILENAMES
  sys_nabu_debugPrint(loadPath, sizeof(loadPath) - 1);
#endif

  n = rn_FileRead(sizeof(loadPath) - 1, (U8 *)loadPath, dest, 0, (U32)at4 << 2, size);
  if (n == RN_NO_REPLY) {
    err_show("NO RETRONET REPLY");
  }
  return n;
}

/* ---------------------------------------------------------------------
 * Boot check (see sys_nabu_load.h). The message's own font: just the
 * characters it uses, 5x7 in 8x8 cells, each put at its own ASCII code's
 * tile so text is written to the name table as-is (every other tile,
 * the space included, stays blank).
 * --------------------------------------------------------------------- */
static const char errChars[] = "ACDEFIKLMNOPRSTUY1/.";
static const U8 errFont[sizeof(errChars) - 1][7] = {
  { 0x70, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88 }, /* A */
  { 0x70, 0x88, 0x80, 0x80, 0x80, 0x88, 0x70 }, /* C */
  { 0xF0, 0x88, 0x88, 0x88, 0x88, 0x88, 0xF0 }, /* D */
  { 0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0xF8 }, /* E */
  { 0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0x80 }, /* F */
  { 0x70, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70 }, /* I */
  { 0x88, 0x90, 0xA0, 0xC0, 0xA0, 0x90, 0x88 }, /* K */
  { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xF8 }, /* L */
  { 0x88, 0xD8, 0xA8, 0xA8, 0x88, 0x88, 0x88 }, /* M */
  { 0x88, 0xC8, 0xA8, 0x98, 0x88, 0x88, 0x88 }, /* N */
  { 0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70 }, /* O */
  { 0xF0, 0x88, 0x88, 0xF0, 0x80, 0x80, 0x80 }, /* P */
  { 0xF0, 0x88, 0x88, 0xF0, 0xA0, 0x90, 0x88 }, /* R */
  { 0x70, 0x88, 0x80, 0x70, 0x08, 0x88, 0x70 }, /* S */
  { 0xF8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20 }, /* T */
  { 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70 }, /* U */
  { 0x88, 0x88, 0x50, 0x20, 0x20, 0x20, 0x20 }, /* Y */
  { 0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70 }, /* 1 */
  { 0x08, 0x08, 0x10, 0x20, 0x40, 0x80, 0x80 }, /* / */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x60 }, /* . */
};

/* One line of text into the name table at `vram`. */
static void err_print(const char *s, U16 vram) {
  vdp_setWriteAddress(vram);
  while (*s) {
    IO_VDPDATA = (U8)*s++;
    VDP_WRITE_SETTLE();
  }
}

#if !VDP_TARGET_9918A_ONLY
static const U16 errBlack = 0x000, errWhite = 0xFFF;
#endif

/* Shows "<loadPath> / <why>" in the middle third of a Graphics II screen
 * (name table $1800, patterns $0800-, colours $2800- for that third -- see
 * vdp_initG2Mode()) and stops. Can happen mid-game, so it first puts the
 * VDP back to a plain, empty screen: F18A sprite ECM off, all VRAM cleared
 * (tiles and sprites gone), and palette entries 1/15 back to black/white
 * (the title and game load their own palettes). */
static void err_show(const char *why) {
  const U8 *p = &errFont[0][0];
  U16 i;
  U8 k;

  NABU_DisableInterrupts();
#if SPRITE_ECM_ENABLED
  set_fullbitmap();
#endif
#if !VDP_TARGET_9918A_ONLY
  if (sysvid_nabu_hasF18A) {
    f18a_unlock();
    f18a_loadPalette(&errBlack, 1, 1);
    f18a_loadPalette(&errWhite, 15, 1);
  }
#endif
  vdp_clearVRAM();
  vdp_initG2Mode();
  vdp_setWriteAddress(0x2800); /* the whole third white on black */
  for (i = 0; i < 0x800; i++) {
    IO_VDPDATA = 0xF1;
    VDP_WRITE_SETTLE();
  }
  for (i = 0; i < sizeof(errChars) - 1; i++) {
    vdp_setWriteAddress(0x0800 + ((U16)errChars[i] << 3));
    for (k = 0; k < 7; k++) {
      IO_VDPDATA = *p++;
      VDP_WRITE_SETTLE();
    }
  }
  err_print(loadPath, 0x1800 + 10 * 32 + 8);
  err_print(why, 0x1800 + 12 * 32 + 8);
  for (;;) {
  }
}

void sys_nabu_checkFiles(void) {
  U8 probe[4];

  /* loadPath still names the file that failed when err_show() prints it;
   * no reply at all is caught inside sys_nabu_loadAssetOffset() */
  if (sys_nabu_loadAssetOffset(RES_FILE, probe, RES_DAT_LAST, 4) != 4) {
    err_show("NOT FOUND OR OLD");
  }
#if SPRITE_ECM_ENABLED /* a 9918A-only build never reads RICK.SPR */
  if (sys_nabu_loadAssetOffset(SPRITE_FILE, probe, RES_SPR_LAST, 4) != 4) {
    err_show("NOT FOUND OR OLD");
  }
#endif
}

/* sys_nabu_debugWriteByte()/sys_nabu_debugPrint() (a direct-to-IA-
 * console debug print, 0xba/0x1f/<len>/<msg>) lived here temporarily
 * this session to help track down the keyboard-movement OR-in bug (see
 * main.c's own control_status call site comment for the resolved bug
 * and its fix) -- removed now that it's served its purpose, to reclaim
 * its real cost in this build's own tight byte budget (SDCC links this
 * whole unity build per translation unit, not per function, so an
 * unused function still costs real image bytes). If a similar live,
 * no-debugger-needed diagnostic is useful again later: opcode 0xba
 * (IA control) followed by 0x1f (print), a length byte, then that many
 * message bytes, sent over HCCA the same way every other hal/
 * RetroNET-FileRead-only.c call does (hcca_DiFocusInterrupts()/
 * hcca_DiWriteByte()/hcca_DiWriteBytes()/hcca_DiRestoreInterrupts(),
 * already available here) -- the IA shows non-ASCII bytes as hex
 * automatically */
