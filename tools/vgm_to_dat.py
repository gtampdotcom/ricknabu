#!/usr/bin/env python3
"""
vgm_to_dat.py -- convert AY8910/YM2149 register writes from a VGM capture
into this project's own (count, (reg,val)*count, delta) tuple encoding,
the format engine/sounds.c's sounds_music_tick() plays back (see that
function's own header, and music_vgm[]'s comment on where the encoding
comes from). Every *.DAT music/intro-theme asset under assets/ (
MUSIC1.DAT, INTRMUS1-5.DAT, etc) was produced this way, but by an earlier
session's own one-off/inline script that was never checked in -- this is
the first durable, reusable copy of that pipeline, written on request so
it doesn't have to be reinvented by hand again next time.

Commands:

    vgm_to_dat.py info FILE.vgm
        Print the header (chip clock, sample rate, length) and a raw
        event count. Sanity-check a capture before doing anything else --
        in particular the AY8910 clock field must be nonzero (SN76489/
        PSG-only captures aren't supported, see parse_events()'s header).

    vgm_to_dat.py segments FILE.vgm [--start SECONDS] [--silence-ms N]
        Reports distinct "bursts" of AY activity after --start (default
        0), separated by a span of at least --silence-ms (default 80) of
        genuine channel silence (AY registers 8/9/10's volume bits all
        reading 0 -- NOT just "no register write happened," which can be
        misleading: a sustained held note may go several frames between
        redundant refreshes of the same register while still audibly
        playing). Use this to find the start/end seconds of one sound
        effect inside a longer capture (a demo reel of several effects
        played back to back, a song with effects layered after the music
        ends, etc) before cutting it out with `extract` below.

    vgm_to_dat.py extract FILE.vgm START END OUTPUT.DAT [--tick-ms N]
                                                         [--exclude-regs R1,R2,...]
                                                         [--force-mixer N]
        Cuts [START, END) seconds out of the capture, dedupes consecutive
        writes to the same register that don't actually change its value
        (see music_vgm[]'s own comment on why real hardware/tracker rips
        are full of these), quantizes to a tick of --tick-ms (default 16,
        matching engine/scr_imap.c's own map-intro music -- see that
        file's own MUSIC2_SIZE comment for why a *short*, punchy sound
        wants the native ~60Hz tick, not GAME_PERIOD's coarser ~66ms one:
        stretching a short effect out over fewer, bigger ticks distorts
        its pitch/decay the same way it did for the old ticked walk_afx[]
        effects, back when those existed), and writes the resulting
        (count,(reg,val)*count,delta) bytes to OUTPUT.DAT.

        This is playable TODAY via the existing sounds_music_load()/
        sounds_music_start()/sounds_music_tick() trio (engine/sounds.c) --
        that player doesn't care whether the data is a whole song or one
        short effect. It is NOT wired to any sounds_play()/e_rick.c call
        site -- every one of those was dropped in an earlier byte-budget
        pass (sounds.c's own file header) and doesn't exist to wire into
        yet; that is a separate, later step.

Register scope: only AY8910 writes (VGM command 0xA0) are read -- every
music/effect capture this project has used so far is AY8910/YM2149 (ZX
Spectrum 128k or MSX rips, see music_vgm[]'s own comment), and engine/
sounds.c's player only ever calls ayWrite()/the mixer helper, nothing
SN76489-shaped. Extend parse_events() if a future capture ever needs the
PSG (0x50) command instead.
"""
import struct
import sys

VGM_SAMPLE_RATE = 44100


def parse_header(data):
    ident = data[0:4]
    if ident != b'Vgm ':
        raise SystemExit(f"not a VGM file (magic={ident!r})")
    eof_offset = struct.unpack_from('<I', data, 0x04)[0] + 0x04
    version = struct.unpack_from('<I', data, 0x08)[0]
    total_samples = struct.unpack_from('<I', data, 0x18)[0]
    if version >= 0x150:
        vgm_data_offset_field = struct.unpack_from('<I', data, 0x34)[0]
        data_offset = (vgm_data_offset_field + 0x34) if vgm_data_offset_field else 0x40
    else:
        data_offset = 0x40
    ay8910_clock = struct.unpack_from('<I', data, 0x74)[0] if len(data) > 0x78 else 0
    sn76489_clock = struct.unpack_from('<I', data, 0x0C)[0]
    return dict(eof_offset=eof_offset, version=version, total_samples=total_samples,
                data_offset=data_offset, ay8910_clock=ay8910_clock,
                sn76489_clock=sn76489_clock)


def parse_events(data, data_offset, eof_offset):
    """Walks the VGM command stream, returning a list of
    (sample_pos, reg, val) for every AY8910 register write (cmd 0xA0),
    with sample_pos advanced by every wait command in between. Only the
    commands actually seen in real captures so far (see this project's own
    music_vgm[]/introMusicSize[] assets) are handled -- an unrecognized
    command prints a warning with its file offset and stops parsing rather
    than silently misreading the rest of the stream as garbage.

    BUG FIXED, reported as "AY register writes: 0" on a real, correctly-
    structured file (psg2vgm's own output, github.com/tursilion/vgmcomp2):
    this used to loop `while pos < eof_offset` trusting the header's own
    EOF-offset field (0x04) at face value -- but that tool's own output
    writes a bogus, too-small value there (e.g. 70 for a 325-byte file
    whose real command data runs to the very end), so the loop never even
    started even though the command stream itself (0xA0 triples ending in
    a real 0x66) was completely well-formed. The command stream is
    self-terminating (0x66) regardless of what the header claims, so
    bounding by the real file length instead is strictly safer -- it can
    only let a well-formed stream run to its own natural 0x66, never read
    past real data that a trustworthy header would have allowed anyway."""
    pos = data_offset
    sample_pos = 0
    events = []
    end = max(eof_offset, len(data))
    while pos < end:
        cmd = data[pos]
        if cmd == 0xA0:
            reg, val = data[pos + 1], data[pos + 2]
            events.append((sample_pos, reg, val))
            pos += 3
        elif cmd == 0x61:
            sample_pos += struct.unpack_from('<H', data, pos + 1)[0]
            pos += 3
        elif cmd == 0x62:
            sample_pos += 735
            pos += 1
        elif cmd == 0x63:
            sample_pos += 882
            pos += 1
        elif 0x70 <= cmd <= 0x7F:
            sample_pos += (cmd & 0x0F) + 1
            pos += 1
        elif 0x80 <= cmd <= 0x8F:
            sample_pos += (cmd & 0x0F)
            pos += 1
        elif cmd == 0x66:
            break
        elif cmd == 0x67:
            block_size = struct.unpack_from('<I', data, pos + 3)[0]
            pos += 7 + block_size
        elif cmd in (0x30, 0x4F, 0x51, 0x54, 0x55, 0x5A, 0x5B, 0x5C, 0x5D, 0xB0):
            pos += 2
        elif cmd in (0x52, 0x53, 0x56, 0x57, 0x58, 0x59, 0x5E, 0x5F, 0xA1):
            pos += 3
        elif cmd == 0xE0:
            pos += 5
        elif 0x40 <= cmd <= 0x4E:
            pos += 2
        else:
            print(f"WARNING: unrecognized VGM command 0x{cmd:02X} at file "
                  f"offset 0x{pos:X} (sample {sample_pos}, {sample_pos/VGM_SAMPLE_RATE:.3f}s)"
                  f" -- stopping parse here, extend parse_events() for this command.",
                  file=sys.stderr)
            break
    return events, sample_pos


def find_bursts(events, final_sample, start_s=0.0, silence_ms=80):
    """Tracks live AY chip state (13 registers) and reports contiguous
    spans where channel A/B/C volume (registers 8/9/10, low 4 bits) are
    ALL zero for at least silence_ms -- a real "nothing is audible right
    now" gap, not just "no register got rewritten this instant" (a held
    note can go a while between redundant same-value refreshes while
    still sounding). Returns a list of (burst_start_s, burst_end_s)."""
    start_sample = start_s * VGM_SAMPLE_RATE
    silence_samples = silence_ms / 1000.0 * VGM_SAMPLE_RATE

    regs = [0] * 16
    bursts = []
    burst_start = None
    silence_since = None

    def channels_silent():
        return (regs[8] & 0x0F) == 0 and (regs[9] & 0x0F) == 0 and (regs[10] & 0x0F) == 0

    # Walk events plus a final sentinel at final_sample so a trailing
    # burst that runs to the end of the file still gets closed out.
    for (t, reg, val) in events + [(final_sample, None, None)]:
        if reg is not None and reg < 16:
            regs[reg] = val
        if t < start_sample:
            continue
        silent = channels_silent() if reg is not None else True
        if silent:
            if burst_start is not None and silence_since is None:
                silence_since = t
            if (burst_start is not None and silence_since is not None
                    and (t - silence_since) >= silence_samples):
                bursts.append((burst_start / VGM_SAMPLE_RATE, silence_since / VGM_SAMPLE_RATE))
                burst_start = None
                silence_since = None
        else:
            if burst_start is None:
                burst_start = t
            silence_since = None
    if burst_start is not None:
        bursts.append((burst_start / VGM_SAMPLE_RATE, final_sample / VGM_SAMPLE_RATE))
    return bursts


def encode_tuples(events, start_s, end_s, tick_ms, auto_trim=True, exclude_regs=(),
                   force_mixer=None):
    """Dedupes (drop a write that doesn't change the register's last-seen
    value) and quantizes the [start_s, end_s) slice of events to tick_ms-
    sized ticks, producing this project's (count, (reg,val)*count, delta)
    byte stream -- see this file's own header and music_vgm[]'s comment
    (engine/sounds.c) for the format and why dedup/quantization both
    matter. `count`/`delta` are single bytes (0-255) -- a delta needing
    more than 255 ticks would overflow; none of this project's own
    captures have ever needed that at 16ms/tick (a 4+ second silent gap),
    so this doesn't split one, it just raises.

    auto_trim=True (default) additionally drops any leading ticks before
    the first one that actually turns a channel's amplitude (registers
    8/9/10) on, and any trailing ticks after the last one where a channel
    is still audible -- a `segments`-picked [start_s, end_s) window is
    rarely sample-exact, and this removes whatever dead air it grabbed at
    either edge without needing to re-guess the boundaries by hand. Prints
    how much was trimmed so it's visible rather than silently changing the
    requested range. This does NOT shorten the sound itself -- a real
    fade/decay tail whose amplitude only reaches exactly 0 on the very
    last written tick is data, not padding, and is always kept.

    exclude_regs drops any write to one of the listed register numbers
    entirely, for a window that overlaps a DIFFERENT effect on another AY
    channel (a common real-world VGM-demo-reel situation: two effects
    captured close enough together that one's tail decay genuinely
    overlaps the next one's onset, not just close in time but literally
    simultaneous register activity -- `segments`'s own silence detection
    can't separate that, since it only checks whether channels 8/9/10 are
    ALL silent at once, not which one). Figure out which channel is the
    contamination (info/segments plus a manual per-register dump of the
    window) before reaching for this -- it removes those registers for
    the WHOLE window, not just where they overlap, so it's only correct
    when the excluded channel never has genuine content of its own
    anywhere in the requested range either.

    force_mixer, if not None, forces register 7 (the mixer) to this exact
    value on the very first tick, ahead of whatever writes real captured
    at that tick -- for a source that never writes register 7 at all
    (psg2vgm's own output, github.com/tursilion/vgmcomp2, does this: it
    emits tone-period/amplitude/noise-period registers but leaves mixer
    setup up to the caller). Without it, whether tone/noise actually reach
    the speaker depends on whatever this build's shared fx slot last left
    register 7 at -- correct only by coincidence. Remember
    engine/sounds.c's own ay_write_mixer() always forces NABU's required
    I/O bits (0x40) into whatever value it's given, so pick this value the
    same way: e.g. 0x00 enables tone AND noise on all three channels
    (0x40 once ay_write_mixer() forces the I/O bits in)."""
    start_sample = start_s * VGM_SAMPLE_RATE
    end_sample = end_s * VGM_SAMPLE_RATE
    tick_samples = tick_ms / 1000.0 * VGM_SAMPLE_RATE

    last_val = {}
    slice_events = []
    for (t, reg, val) in events:
        if t < start_sample or t >= end_sample:
            continue
        if reg in exclude_regs:
            continue
        if last_val.get(reg) == val:
            continue
        last_val[reg] = val
        tick = int((t - start_sample) / tick_samples)
        slice_events.append((tick, reg, val))

    if not slice_events:
        raise SystemExit("no register writes in that range -- check --start/--end")

    # Group by tick (several writes can land in the same tick), then
    # encode consecutive ticks with their own (count,(reg,val)*count)
    # frame and the gap to the NEXT frame as that frame's delta byte --
    # same layout sounds_music_tick() reads.
    by_tick = {}
    for (tick, reg, val) in slice_events:
        by_tick.setdefault(tick, []).append((reg, val))

    if force_mixer is not None:
        # Some sources never write register 7 at all (see this function's
        # own --force-mixer CLI help) -- without an explicit mixer state,
        # playback depends on whatever this build's shared fx slot last
        # left register 7 at, which is fragile (correct only by
        # coincidence). Force it into the very first tick's own writes so
        # this effect is self-contained regardless of ambient state.
        first_tick = min(by_tick)
        by_tick[first_tick] = [(7, force_mixer)] + [
            (r, v) for (r, v) in by_tick[first_tick] if r != 7
        ]

    if auto_trim:
        amp = {8: 0, 9: 0, 10: 0}
        audible_ticks = []
        for tick in sorted(by_tick):
            for (reg, val) in by_tick[tick]:
                if reg in amp:
                    amp[reg] = val
            if any((v & 0x0F) for v in amp.values()):
                audible_ticks.append(tick)
        if audible_ticks:
            first_audible, last_audible = audible_ticks[0], audible_ticks[-1]
            # Keep every tick up to and including the one right after the
            # last audible tick (if any) -- that is very likely the
            # explicit "set amplitude to 0" mute write, real data (this
            # project's own music_tick() hard-mutes on wrap the same way,
            # see that function's own header), not padding.
            all_ticks_sorted = sorted(by_tick)
            last_keep_idx = all_ticks_sorted.index(last_audible)
            if last_keep_idx + 1 < len(all_ticks_sorted):
                last_keep = all_ticks_sorted[last_keep_idx + 1]
            else:
                last_keep = last_audible
            dropped_leading = sum(1 for t in by_tick if t < first_audible)
            dropped_trailing = sum(1 for t in by_tick if t > last_keep)
            if dropped_leading or dropped_trailing:
                print(f"  auto-trim: dropped {dropped_leading} silent leading tick(s) "
                      f"and {dropped_trailing} silent trailing tick(s) "
                      f"({dropped_leading * tick_ms:.0f}ms / {dropped_trailing * tick_ms:.0f}ms)")
            by_tick = {t - first_audible: v for t, v in by_tick.items()
                       if first_audible <= t <= last_keep}

    ticks = sorted(by_tick)
    out = bytearray()
    for i, tick in enumerate(ticks):
        writes = by_tick[tick]
        if len(writes) > 255:
            raise SystemExit(f"tick {tick}: {len(writes)} writes, more than a byte can count")
        out.append(len(writes))
        for (reg, val) in writes:
            out += bytes((reg, val))
        delta = (ticks[i + 1] - tick) if i + 1 < len(ticks) else 0
        if delta > 255:
            raise SystemExit(f"tick {tick}: delta {delta} to next event overflows a byte -- "
                              f"pick a coarser --tick-ms or split this extraction")
        out.append(delta)
    return bytes(out)


def cmd_info(args):
    with open(args[0], 'rb') as f:
        data = f.read()
    hdr = parse_header(data)
    print(f"File: {args[0]} ({len(data)} bytes)")
    print(f"VGM version: 0x{hdr['version']:03X}")
    print(f"AY8910 clock: {hdr['ay8910_clock']} Hz"
          + ("" if hdr['ay8910_clock'] else "  <-- ZERO: this capture may not use AY8910 at all"))
    print(f"SN76489 clock: {hdr['sn76489_clock']} Hz")
    events, final_sample = parse_events(data, hdr['data_offset'], hdr['eof_offset'])
    print(f"AY register writes: {len(events)}")
    print(f"Length: {final_sample} samples = {final_sample / VGM_SAMPLE_RATE:.2f}s")


def cmd_segments(args):
    path = args[0]
    start_s = 0.0
    silence_ms = 80
    rest = args[1:]
    i = 0
    while i < len(rest):
        if rest[i] == '--start':
            start_s = float(rest[i + 1]); i += 2
        elif rest[i] == '--silence-ms':
            silence_ms = float(rest[i + 1]); i += 2
        else:
            raise SystemExit(f"unknown option {rest[i]}")

    with open(path, 'rb') as f:
        data = f.read()
    hdr = parse_header(data)
    events, final_sample = parse_events(data, hdr['data_offset'], hdr['eof_offset'])
    bursts = find_bursts(events, final_sample, start_s=start_s, silence_ms=silence_ms)
    print(f"{len(bursts)} burst(s) found after {start_s:.2f}s "
          f"(silence threshold {silence_ms:.0f}ms):")
    for (b_start, b_end) in bursts:
        print(f"  {b_start:.3f}s -> {b_end:.3f}s  (duration {b_end - b_start:.3f}s)")


def cmd_extract(args):
    path, start_s, end_s, out_path = args[0], float(args[1]), float(args[2]), args[3]
    tick_ms = 16
    exclude_regs = ()
    rest = args[4:]
    i = 0
    while i < len(rest):
        if rest[i] == '--tick-ms':
            tick_ms = float(rest[i + 1]); i += 2
        elif rest[i] == '--exclude-regs':
            exclude_regs = tuple(int(x) for x in rest[i + 1].split(','))
            i += 2
        else:
            raise SystemExit(f"unknown option {rest[i]}")

    with open(path, 'rb') as f:
        data = f.read()
    hdr = parse_header(data)
    events, _ = parse_events(data, hdr['data_offset'], hdr['eof_offset'])
    out = encode_tuples(events, start_s, end_s, tick_ms, exclude_regs=exclude_regs)
    with open(out_path, 'wb') as f:
        f.write(out)
    print(f"{out_path}: wrote {len(out)} bytes "
          f"({start_s:.3f}s-{end_s:.3f}s @ {tick_ms:.0f}ms/tick)")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd, rest = sys.argv[1], sys.argv[2:]
    if cmd == 'info':
        cmd_info(rest)
    elif cmd == 'segments':
        cmd_segments(rest)
    elif cmd == 'extract':
        cmd_extract(rest)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
