/*
 * hal/RetroNET-FileRead-only.c
 *
 * WHY THIS FILE EXISTS: hal/RetroNET-FileStore.h (DJ Sures/NABU-LIB,
 * vendored unmodified -- see that file's header) ends with
 * `#include "RetroNET-FileStore.c"`, which is roughly 40 rn_-prefixed and
 * hcca_Di-prefixed functions covering the entire RetroNET protocol: file
 * open/replace/insert/delete/copy/move/list/details, directory
 * create/delete, TCP client and server sockets, and printer/punch
 * output. hal/sys_nabu_load.c (the only caller anywhere in this repo --
 * verified by grepping the whole tree for "rn_") uses exactly ONE of
 * them: rn_FileRead().
 *
 * That wouldn't matter on a toolchain that garbage-collects unreferenced
 * functions, but this whole port is a single unity-build translation unit
 * (main.c #includes every .c file directly, see that file's own header),
 * and z88dk/sdcc links per translation unit, not per function -- so
 * #including RetroNET-FileStore.h compiled in all ~40 functions
 * regardless of what's actually called, at a real cost against NABU's
 * ~60KB flat image budget (see hal/sys_nabu_load.h's header) for
 * functionality (TCP sockets, file writing, directory listing, ...) this
 * build has no use for and doesn't even link the callers of.
 *
 * This file is hand-extracted from that vendored copy: rn_FileRead()
 * itself plus its exact transitive call graph, unchanged byte-for-byte in
 * logic (see hal/RetroNET-FileStore.c for the original if this ever needs
 * re-diffing against an upstream update):
 *
 *   rn_FileRead
 *     |-- hcca_DiFocusInterrupts
 *     |-- hcca_DiWriteByte           (also used directly below)
 *     |     `-- (needs _rnFS_INT_BACKUP for the Focus/Restore pair)
 *     |-- hcca_DiWriteBytes  --> hcca_DiWriteByte
 *     |-- hcca_DiWriteUInt32 --> hcca_DiWriteByte
 *     |-- hcca_DiWriteUInt16 --> hcca_DiWriteByte
 *     |-- hcca_DiReadUInt16  --> hcca_DiReadByte (inlined below, matching
 *     |                          the original's `inline` on this one)
 *     `-- hcca_DiRestoreInterrupts
 *
 * If a future change needs another rn_* call (e.g. rn_fileSize()), pull
 * that function (and anything new it needs) over from
 * hal/RetroNET-FileStore.c the same way rather than switching back to the
 * full header -- that's the whole point of this file.
 */

#include <stdint.h>
#include "NABU-LIB.h"

uint8_t _rnFS_INT_BACKUP = 0;

/* rn_FileRead()'s result when the adapter never answers; each try is one
 * 65536-pass wait loop, roughly a second or more. */
#define RN_NO_REPLY 0xFFFF
#define RN_REPLY_TRIES 5

void hcca_DiFocusInterrupts(void) {

  NABU_DisableInterrupts();

  _rnFS_INT_BACKUP = ayRead(IOPORTA);

  ayWrite(IOPORTA, INT_MASK_HCCARX);
}

void hcca_DiRestoreInterrupts(void) {

  ayWrite(IOPORTA, _rnFS_INT_BACKUP);

  NABU_EnableInterrupts();
}

void hcca_DiWriteByte(uint8_t c) {

  ayWrite(IOPORTA, INT_MASK_HCCATX);

  IO_AYLATCH = IOPORTB;
  while (IO_AYDATA & 0x04);

  IO_HCCA = c;

  ayWrite(IOPORTA, INT_MASK_HCCARX);
}

void hcca_DiWriteUInt32(uint32_t val) {

  hcca_DiWriteByte(val & 0xff);
  hcca_DiWriteByte((val >> 8) & 0xff);
  hcca_DiWriteByte((val >> 16) & 0xff);
  hcca_DiWriteByte((val >> 24) & 0xff);
}

void hcca_DiWriteUInt16(uint16_t val) {

  hcca_DiWriteByte(val & 0xff);
  hcca_DiWriteByte((val >> 8) & 0xff);
}

void hcca_DiWriteBytes(uint16_t offset, uint16_t length, uint8_t *bytes) {

  uint8_t *start = bytes + offset;
  uint8_t *end   = start + length;

  while (start != end) {

    hcca_DiWriteByte(*start);

    start++;
  }
}

// -----------------------------------------------------------------------------------

inline uint8_t hcca_DiReadByte(void) {

  IO_AYLATCH = IOPORTB;
  while (IO_AYDATA & 0x02);
  return IO_HCCA;
}

uint16_t hcca_DiReadUInt16(void) {

  return  (uint16_t)hcca_DiReadByte() |
         ((uint16_t)hcca_DiReadByte() << 8);
}

// **************************************************************************
// Read `readLength` bytes at `readOffset` from `filename` on the Internet
// Adapter's RetroNET file store into `buffer` (at `bufferOffset`). Returns
// the number of bytes actually transferred, or RN_NO_REPLY. Unchanged from
// hal/RetroNET-FileStore.c's rn_FileRead() apart from that timeout -- see that file's own comment
// for the wire protocol (opcode 0xe7) if this ever needs re-verifying
// against upstream.
// **************************************************************************
uint16_t rn_FileRead(uint8_t filenameLen, uint8_t* filename, uint8_t* buffer, uint16_t bufferOffset, uint32_t readOffset, uint16_t readLength) {

  //0xe7

  hcca_DiFocusInterrupts();

  uint8_t *start = buffer + bufferOffset;

  hcca_DiWriteByte(0xe7);

  hcca_DiWriteByte(filenameLen);

  hcca_DiWriteBytes(0, filenameLen, filename);

  hcca_DiWriteUInt32(readOffset);

  hcca_DiWriteUInt16(readLength);

  /* CHANGED from upstream (rick-dangerous): give up if the reply doesn't
   * start within ~5 seconds -- an adapter without RetroNET file reads, or
   * none listening any more, would otherwise leave this waiting forever.
   * Returns RN_NO_REPLY then. */
  {
    uint16_t t = 0;
    uint8_t tries = RN_REPLY_TRIES;

    IO_AYLATCH = IOPORTB;
    while (IO_AYDATA & 0x02) {
      if (++t == 0 && --tries == 0) {
        hcca_DiRestoreInterrupts();
        return RN_NO_REPLY;
      }
    }
  }

  uint16_t toRead = hcca_DiReadUInt16();
  uint8_t *end    = start + toRead;

  /* Verified byte-for-byte against hal/RetroNET-FileStore.c's rn_FileRead():
   * this loop deliberately does NOT re-latch IO_AYLATCH per byte -- the
   * preceding hcca_DiReadUInt16() -> hcca_DiReadByte() call already left
   * IOPORTB selected, and nothing else touches IO_AYLATCH between bytes
   * here, so it stays valid for the whole run. (A previous "fix" added a
   * redundant per-iteration IO_AYLATCH write, on the theory that this was
   * an extraction bug -- it wasn't: the original upstream function has the
   * exact same bare loop, and it's proven corruption-free in rick-map-load,
   * which #includes the real RetroNET-FileStore.c unmodified. Reverted;
   * did not fix the reported VRAM corruption anyway.) */
  while (start != end) {

    while (IO_AYDATA & 0x02);
    *start = IO_HCCA;

    start++;
  }

  hcca_DiRestoreInterrupts();

  return toRead;
}

/* eof */