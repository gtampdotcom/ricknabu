#!/usr/bin/env python3
"""
dat_to_vgm.py -- the inverse of tools/vgm_to_dat.py's `extract` command:
takes one of this project's own (count, (reg,val)*count, delta) tuple
.DAT files (assets/MUSIC1.DAT, INTRMUS1-5.DAT, JUMP.DAT, etc -- see
engine/sounds.c's music_vgm[]/sounds_music_tick() for what actually reads
this format on real hardware) and rebuilds a standalone, playable AY8910
VGM file from it, so it can be checked in any ordinary VGM player/foobar2000/
vgmplay/etc without needing a NABU or emulator running -- useful for
listening to a .DAT you don't already know the content of (an
unidentified extracted effect, or double-checking a music encode didn't
drift) before deciding what to do with it.

Usage:
    dat_to_vgm.py FILE.DAT OUTPUT.vgm [--tick-ms N] [--ay-clock HZ] [--loop]

    --tick-ms N   Must match whatever tick size the .DAT was ENCODED at
                  (vgm_to_dat.py extract's own --tick-ms, default 16 there
                  too) -- this is not stored in the .DAT file itself (see
                  this project's own README on that format), so get it
                  wrong and playback speed/pitch will be wrong even though
                  the file "plays". engine/sounds.c's own call sites are
                  the ground truth for a given asset: main.c's MUSIC1.DAT
                  load implies 66ms (GAME_PERIOD, see sounds_music_tick()'s
                  own header), engine/scr_imap.c's INTRMUS1-5.DAT and this
                  project's own extracted effects (JUMP.DAT etc, tools/
                  vgm_to_dat.py) all use 16ms.
    --ay-clock HZ Defaults to 1773450 (the clock every capture used by this
                  project so far was made at -- ZX Spectrum 128k/MSX AY-3-
                  8912). Only matters for pitch -- get it wrong and playback
                  is off-key, still structurally correct otherwise.
    --loop        Set the VGM's own loop points to the whole file, matching
                  sounds_music_tick()'s own hard-loop-forever behavior
                  (see that function's own header) -- off by default,
                  since most of this project's short effect extracts were
                  never meant to loop, only the title/intro music was.
"""
import struct
import sys

VGM_SAMPLE_RATE = 44100


def decode_tuples(data):
    """Walks a (count, (reg,val)*count, delta) byte stream (this project's
    own format -- see this file's header) and yields (writes, delta_ticks)
    per tuple, writes being a list of (reg, val) pairs. Mirrors
    vgm_to_dat.py's encode_tuples() exactly, in reverse."""
    pos = 0
    tuples = []
    while pos < len(data):
        count = data[pos]; pos += 1
        writes = []
        for _ in range(count):
            reg, val = data[pos], data[pos + 1]
            pos += 2
            writes.append((reg, val))
        if pos >= len(data):
            raise SystemExit(f"truncated .DAT: expected a delta byte at offset {pos}, "
                              f"file is only {len(data)} bytes")
        delta = data[pos]; pos += 1
        tuples.append((writes, delta))
    return tuples


def build_vgm(tuples, tick_ms, ay_clock, loop):
    tick_samples = tick_ms / 1000.0 * VGM_SAMPLE_RATE

    body = bytearray()
    total_samples = 0
    # Track fractional sample carry so many small ticks don't accumulate a
    # visible rounding drift over a longer (music-length) file.
    carry = 0.0
    for (writes, delta) in tuples:
        for (reg, val) in writes:
            body += bytes((0xA0, reg, val))
        if delta:
            exact = delta * tick_samples + carry
            samples = int(round(exact))
            carry = exact - samples
            total_samples += samples
            while samples > 0:
                chunk = min(samples, 0xFFFF)
                body += bytes((0x61,)) + struct.pack('<H', chunk)
                samples -= chunk
    body += bytes((0x66,))  # end of sound data

    header_size = 0x80
    data_offset_field = header_size - 0x34
    loop_offset_field = (header_size - 0x1C) if loop else 0
    loop_samples = total_samples if loop else 0

    header = bytearray(header_size)
    header[0x00:0x04] = b'Vgm '
    struct.pack_into('<I', header, 0x08, 0x151)                    # version 1.51
    struct.pack_into('<I', header, 0x18, total_samples)             # total # samples
    struct.pack_into('<I', header, 0x1C, loop_offset_field)         # loop offset
    struct.pack_into('<I', header, 0x20, loop_samples)              # loop # samples
    struct.pack_into('<I', header, 0x24, 60)                        # recording rate
    struct.pack_into('<I', header, 0x34, data_offset_field)         # VGM data offset
    struct.pack_into('<I', header, 0x74, ay_clock)                  # AY8910 clock
    header[0x78] = 0x00                                             # AY8910 chip type (AY-3-8910)
    header[0x79] = 0x01                                             # AY8910 flags: legacy output

    vgm = bytearray(header) + body
    eof_offset = len(vgm) - 0x04
    struct.pack_into('<I', vgm, 0x04, eof_offset)
    return bytes(vgm)


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        sys.exit(__doc__)
    dat_path, out_path = args[0], args[1]
    tick_ms = 16.0
    ay_clock = 1773450
    loop = False
    i = 2
    while i < len(args):
        if args[i] == '--tick-ms':
            tick_ms = float(args[i + 1]); i += 2
        elif args[i] == '--ay-clock':
            ay_clock = int(args[i + 1]); i += 2
        elif args[i] == '--loop':
            loop = True; i += 1
        else:
            sys.exit(f"unknown option {args[i]}")

    with open(dat_path, 'rb') as f:
        data = f.read()

    tuples = decode_tuples(data)
    total_writes = sum(len(w) for (w, _) in tuples)
    vgm = build_vgm(tuples, tick_ms, ay_clock, loop)

    with open(out_path, 'wb') as f:
        f.write(vgm)

    total_ticks = sum(d for (_, d) in tuples)
    print(f"{out_path}: wrote {len(vgm)} bytes -- {len(tuples)} tuples, "
          f"{total_writes} register writes, ~{total_ticks * tick_ms / 1000.0:.2f}s "
          f"@ {tick_ms:.0f}ms/tick" + (" (looping)" if loop else ""))


if __name__ == '__main__':
    main()
