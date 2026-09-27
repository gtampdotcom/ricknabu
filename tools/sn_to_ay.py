#!/usr/bin/env python3
"""
sn_to_ay.py -- convert a VGM captured against an SN76489 (the TI PSG used
in the Sega Master System, ColecoVision, BBC Micro, etc) into a new VGM
that drives an AY8910/YM2149 instead, by decoding the SN's own logical
channel state (3 tone channels + 1 noise channel, each with its own
frequency/volume) and re-encoding equivalent AY8910 register writes.
Companion to vgm_to_dat.py/dat_to_vgm.py (same VGM_SAMPLE_RATE convention,
same header-building approach) -- run this FIRST on an SN-chip capture,
then feed the resulting AY-chip VGM through vgm_to_dat.py extract like any
other capture this project already uses.

Usage:
    sn_to_ay.py SOURCE.vgm OUTPUT.vgm [--ay-clock HZ]

    --ay-clock HZ defaults to 1773450 -- the clock every AY capture this
    project has used so far was made at (ZX Spectrum 128k/MSX). The
    source SN clock is read from SOURCE.vgm's own header, not guessed.

THIS IS AN APPROXIMATION, not a bit-accurate chip transcode -- the two
chips' hardware genuinely doesn't map 1:1:

  - Tone frequency: SN76489 divides its clock by 32*N per channel; AY8910
    divides by 16*N. Converted so the AUDIBLE PITCH matches:
    N_ay = round(N_sn * 2 * ay_clock / sn_clock). Since this project's own
    1773450 Hz AY clock is very close to half of a standard SN76489's
    3579540/3579545 Hz NTSC clock, that ratio lands close to 1:1 -- pitch
    should come through with minimal drift for a standard-NTSC-clocked
    source, more for an unusual source clock.

  - Volume: SN76489 registers are 4-bit ATTENUATION (0=loudest,
    15=silent); AY8910 registers are 4-bit AMPLITUDE (0=silent,
    15=loudest). Direct inversion (ay = 15 - sn) -- both chips are
    roughly log-scaled over the same 16 steps, so this is the standard
    approximation, not exact dB-for-dB.

  - Noise: this is the real structural mismatch. SN76489 noise is a
    genuinely independent 4th channel with its own volume register; AY8910
    only has ONE noise generator, mixed onto whichever of its 3 tone
    channels the mixer register enables it on -- there is no 4th
    amplitude register to give it. Routed onto AY channel C (arbitrary
    but consistent choice) alongside SN's own tone channel 2: whenever SN
    noise is audible (attenuation < 15), channel C's mixer bits enable
    BOTH tone and noise, and channel C's one amplitude register takes
    whichever of {SN tone2 volume, SN noise volume} is louder -- so a
    quiet tone2 note under a loud noise burst (or vice versa) gets
    overridden by the louder one instead of true independent mixing. SN's
    3 fixed noise shift rates (clock/512, /1024, /2048) and its "sync to
    tone2" mode are each mapped to an approximate equivalent AY noise
    period (5-bit, 0-31) using the same clock-ratio idea as tone
    conversion, clamped into range -- AY's noise period is far more
    limited than SN's slower rates can express, so very low/rumbling SN
    noise will sound higher-pitched here than the original.

Register/state tracking, not raw command translation: SN76489 writes are
a stateful LATCH-then-optional-DATA-byte protocol (only tone frequency
writes take a second byte), so this parses that protocol into the chip's
actual logical state (3x tone frequency+volume, noise control+volume)
before re-deriving AY register values from that state -- a literal
byte-for-byte command translation isn't possible since the two chips
don't share a register layout at all.
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
    if version >= 0x150:
        vgm_data_offset_field = struct.unpack_from('<I', data, 0x34)[0]
        data_offset = (vgm_data_offset_field + 0x34) if vgm_data_offset_field else 0x40
    else:
        data_offset = 0x40
    sn76489_clock = struct.unpack_from('<I', data, 0x0C)[0]
    return dict(eof_offset=eof_offset, version=version, data_offset=data_offset,
                sn76489_clock=sn76489_clock)


def parse_sn_events(data, data_offset, eof_offset):
    """Walks the VGM command stream, decoding SN76489 writes (command
    0x50) through their real LATCH/DATA protocol (see this file's own
    header) into (sample_pos, channel, kind, value) events, channel is
    0/1/2 (tone) or 3 (noise), kind is 'freq' (0-1023, tone channels
    only), 'vol' (0-15, any channel), or 'ctrl' (0-7, noise channel
    only, the shift-rate/FB bits). Also advances sample_pos through the
    same wait commands vgm_to_dat.py's own parser handles.

    BUG FIXED: same fix as vgm_to_dat.py's own parse_events() -- bound by
    the real file length, not the header's own EOF-offset field, since a
    real, correctly-structured file (psg2vgm's own output) has been found
    writing a bogus, too-small value there. See that function's own
    comment for the full account."""
    pos = data_offset
    sample_pos = 0
    events = []
    latched_channel = 0
    latched_is_freq = False
    pending_freq_low = 0
    end = max(eof_offset, len(data))
    while pos < end:
        cmd = data[pos]
        if cmd == 0x50:
            b = data[pos + 1]
            pos += 2
            if b & 0x80:
                # Latch byte: 1 CC T DDDD -- BUG FIXED: T's polarity was
                # backwards (0 was treated as volume, 1 as tone/noise) --
                # the real SN76489 spec (SMS Power's own register
                # reference) is T=0 for the tone/noise register (frequency
                # for channels 0-2, noise control for channel 3), T=1 for
                # that same channel's volume. Inverted, every real
                # frequency write got decoded as a volume write instead
                # (and noise ctrl/vol were swapped too) -- zero real pitch
                # data ever reached convert_to_ay(), which is exactly why
                # the AY output "sounded not even remotely like" the
                # source: only garbled volume changes made it through.
                latched_channel = (b >> 5) & 0x03
                latched_is_freq = not ((b >> 4) & 0x01) and latched_channel != 3
                data4 = b & 0x0F
                if latched_channel == 3:
                    # Noise channel: T=0 selects control, T=1 selects volume.
                    if (b >> 4) & 0x01:
                        events.append((sample_pos, 3, 'vol', data4))
                    else:
                        events.append((sample_pos, 3, 'ctrl', data4 & 0x07))
                elif latched_is_freq:
                    pending_freq_low = data4
                    # Wait for the data byte to complete the 10-bit value;
                    # if none follows (rare/malformed), this channel's
                    # frequency simply never updates from this latch.
                else:
                    events.append((sample_pos, latched_channel, 'vol', data4))
            else:
                # Data byte: 0 X D9..D5 -- only meaningful right after a
                # tone-frequency latch.
                if latched_is_freq and latched_channel != 3:
                    high6 = b & 0x3F
                    freq = (high6 << 4) | pending_freq_low
                    events.append((sample_pos, latched_channel, 'freq', freq))
                    latched_is_freq = False
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
        elif cmd == 0x66:
            break
        elif cmd == 0x67:
            block_size = struct.unpack_from('<I', data, pos + 3)[0]
            pos += 7 + block_size
        else:
            print(f"WARNING: unrecognized VGM command 0x{cmd:02X} at file "
                  f"offset 0x{pos:X} (sample {sample_pos}) -- stopping parse, "
                  f"extend parse_sn_events() for this command.", file=sys.stderr)
            break
    return events, sample_pos


def convert_to_ay(sn_events, sn_clock, ay_clock):
    """Replays the decoded SN76489 event stream against a logical chip
    state and emits equivalent AY8910 (sample_pos, reg, val) writes --
    see this file's own header for the conversion math and the noise
    approximation. Only emits an AY write when the DERIVED register value
    actually changes, same dedup spirit as vgm_to_dat.py's own encoder."""
    freq_ratio = 2.0 * ay_clock / sn_clock  # tone: N_ay = N_sn * this
    noise_fixed_ay = {  # SN shift-rate mode -> approximate AY noise period
        0: max(1, min(31, round(32 * ay_clock / sn_clock))),   # clock/512
        1: max(1, min(31, round(64 * ay_clock / sn_clock))),   # clock/1024
        2: max(1, min(31, round(128 * ay_clock / sn_clock))),  # clock/2048
    }

    sn_tone_freq = [0, 0, 0]
    sn_tone_vol = [15, 15, 15]     # SN power-on state: silent
    sn_noise_ctrl = 0
    sn_noise_vol = 15

    ay_reg = {}  # last emitted value per AY register, for dedup

    def ay_write(events_out, t, reg, val):
        val &= 0xFF
        if ay_reg.get(reg) == val:
            return
        ay_reg[reg] = val
        events_out.append((t, reg, val))

    out = []
    for (t, channel, kind, value) in sn_events:
        if channel in (0, 1, 2) and kind == 'freq':
            sn_tone_freq[channel] = value
        elif channel in (0, 1, 2) and kind == 'vol':
            sn_tone_vol[channel] = value
        elif channel == 3 and kind == 'ctrl':
            sn_noise_ctrl = value
        elif channel == 3 and kind == 'vol':
            sn_noise_vol = value
        else:
            continue

        # Tone A/B (SN channels 0/1) -- direct, independent.
        for ch, (reg_lo, reg_hi, reg_amp) in enumerate([(0, 1, 8), (2, 3, 9)]):
            n_ay = max(1, min(4095, round(sn_tone_freq[ch] * freq_ratio)))
            ay_write(out, t, reg_lo, n_ay & 0xFF)
            ay_write(out, t, reg_hi, (n_ay >> 8) & 0x0F)
            ay_write(out, t, reg_amp, 15 - sn_tone_vol[ch])

        # Channel C: SN tone channel 2 shares this with SN's noise
        # channel (see this file's own header on why) -- louder of the
        # two wins the one amplitude register; mixer enables tone/noise
        # independently based on each one's own audibility.
        n_ay_c = max(1, min(4095, round(sn_tone_freq[2] * freq_ratio)))
        ay_write(out, t, 4, n_ay_c & 0xFF)
        ay_write(out, t, 5, (n_ay_c >> 8) & 0x0F)

        shift_rate = sn_noise_ctrl & 0x03
        if shift_rate == 3:
            # Sync to tone2's own period -- same conversion, clamped to
            # AY's 5-bit noise period range.
            n_ay_noise = max(1, min(31, round(sn_tone_freq[2] * freq_ratio)))
        else:
            n_ay_noise = noise_fixed_ay[shift_rate]
        ay_write(out, t, 6, n_ay_noise)

        tone2_audible = sn_tone_vol[2] < 15
        noise_audible = sn_noise_vol < 15
        tone2_amp = 15 - sn_tone_vol[2]
        noise_amp = 15 - sn_noise_vol
        amp_c = max(tone2_amp if tone2_audible else 0,
                    noise_amp if noise_audible else 0)
        ay_write(out, t, 10, amp_c)

        mixer = 0x40  # NABU I/O bits (bit6=1,bit7=0) -- see engine/sounds.c's
                      # own ay_write_mixer() for why this is forced regardless
        mixer_bits = 0
        # bit0/1: tone A/B disable -- always enabled (SN tone0/1 always active)
        # bit2: tone C disable -- enabled unless tone2 isn't the louder source
        if not (tone2_audible and tone2_amp >= noise_amp):
            mixer_bits |= 0x04
        # bit3/4: noise A/B disable -- SN noise never routes to A/B here
        mixer_bits |= 0x08 | 0x10
        # bit5: noise C disable -- enabled unless noise isn't the louder source
        if not (noise_audible and noise_amp >= tone2_amp):
            mixer_bits |= 0x20
        ay_write(out, t, 7, mixer | mixer_bits)

    return out


def build_vgm(ay_events, ay_clock, final_sample):
    body = bytearray()
    last_t = 0
    by_tick_pos = sorted(ay_events, key=lambda e: e[0])
    i = 0
    while i < len(by_tick_pos):
        t = by_tick_pos[i][0]
        if t > last_t:
            gap = t - last_t
            while gap > 0:
                chunk = min(gap, 0xFFFF)
                body += bytes((0x61,)) + struct.pack('<H', chunk)
                gap -= chunk
            last_t = t
        while i < len(by_tick_pos) and by_tick_pos[i][0] == t:
            _, reg, val = by_tick_pos[i]
            body += bytes((0xA0, reg, val))
            i += 1
    if final_sample > last_t:
        gap = final_sample - last_t
        while gap > 0:
            chunk = min(gap, 0xFFFF)
            body += bytes((0x61,)) + struct.pack('<H', chunk)
            gap -= chunk
    body += bytes((0x66,))

    header_size = 0x80
    header = bytearray(header_size)
    header[0x00:0x04] = b'Vgm '
    struct.pack_into('<I', header, 0x08, 0x151)
    struct.pack_into('<I', header, 0x18, final_sample)
    struct.pack_into('<I', header, 0x24, 60)
    struct.pack_into('<I', header, 0x34, header_size - 0x34)
    struct.pack_into('<I', header, 0x74, ay_clock)
    header[0x78] = 0x00
    header[0x79] = 0x01

    vgm = bytearray(header) + body
    struct.pack_into('<I', vgm, 0x04, len(vgm) - 0x04)
    return bytes(vgm)


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        sys.exit(__doc__)
    src_path, out_path = args[0], args[1]
    ay_clock = 1773450
    i = 2
    while i < len(args):
        if args[i] == '--ay-clock':
            ay_clock = int(args[i + 1]); i += 2
        else:
            sys.exit(f"unknown option {args[i]}")

    with open(src_path, 'rb') as f:
        data = f.read()
    hdr = parse_header(data)
    if not hdr['sn76489_clock']:
        sys.exit(f"{src_path}: header's SN76489 clock is 0 -- this doesn't "
                  f"look like an SN76489 capture (use vgm_to_dat.py directly "
                  f"if it's already AY8910)")

    sn_events, final_sample = parse_sn_events(data, hdr['data_offset'], hdr['eof_offset'])
    print(f"Parsed {len(sn_events)} SN76489 register changes, "
          f"{final_sample} samples ({final_sample/VGM_SAMPLE_RATE:.2f}s), "
          f"source clock {hdr['sn76489_clock']} Hz")

    ay_events = convert_to_ay(sn_events, hdr['sn76489_clock'], ay_clock)
    print(f"Derived {len(ay_events)} AY8910 register writes")

    vgm = build_vgm(ay_events, ay_clock, final_sample)
    with open(out_path, 'wb') as f:
        f.write(vgm)
    print(f"{out_path}: wrote {len(vgm)} bytes (AY8910 clock {ay_clock} Hz)")


if __name__ == '__main__':
    main()
