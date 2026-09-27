#!/usr/bin/env python3
"""
extract_assets.py -- pull a C `const TYPE name[...] = { 0x.., 0x.., ... };`
byte-array initializer out of a source file and dump it as a raw binary
blob, for turning compiled-in asset tables into files the NABU port loads
at runtime instead (see hal/sys_nabu_load.c and assets/README.md).

Usage:
    extract_assets.py SOURCE.c ARRAY_NAME OUTPUT.bin [EXPECTED_SIZE] [--width N]

--width N sets how many bytes each comma-separated literal in the
initializer expands to in the output (default 1). Every array this tool
originally handled (tiles_banks_col/pat, sprites_dataN) is `U8`/`tile_t`/
`sprite_t` -- one literal, one byte -- so --width 1 (the default) reproduces
the original behavior exactly, literal-for-literal.

engine/dat_maps*.c and engine/dat_ents.c's struct tables (submap_t, mark_t,
entdata_t) and flat U16 array (ent_sprseq) are different: every field in
those structs is U16, not U8, so each literal needs to expand to *two*
bytes in the output, not one, or the loaded buffer won't be the right size
for sys_nabu_loadAsset() to fill it correctly. --width 2 does that, packing
each literal little-endian (low byte first) -- z88dk/sdcc's own U16 layout
on the Z80, matching the byte order dest already has in memory once
loaded, so no unpacking is needed on the NABU side.

Struct field order in the source is preserved as-is (this tool doesn't
know about field names, just reads literals left-to-right), so it only
works if every field in the struct is genuinely the same width -- true for
every table currently extracted this way (submap_t/mark_t/entdata_t are
all-U16; there's no mixed-width struct here yet). If one shows up later,
this tool needs per-field widths, not a single --width for the whole call.

Only handles flat numeric-literal initializers (decimal or 0x hex), which
is what every dat_*.c/dat_maps*.c/dat_ents.c asset table in this repo is --
not struct initializers with non-numeric fields, like dat_screens.c's
screen_imapsteps_t table (it mixes in string-literal pointers).
"""
import re
import sys


def strip_comments(text):
    # Must run on the WHOLE file before searching for the array, not just
    # on the matched body: this codebase's dat_*.c files end every data
    # line with a "// 00000080 ................ //" offset+ASCII-art
    # comment, and the ASCII-art half is a literal rendering of the row's
    # bytes -- e.g. a 0x7D,0x3B pair (}) renders as "};" right there in
    # the comment text. Locating the array's closing brace on uncleaned
    # text can match that decoy instead of the real one.
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    text = re.sub(r'//[^\n]*', '', text)
    return text


def extract(src_text, array_name, width=1):
    src_text = strip_comments(src_text)

    # Find "<array_name> ... = { ... } ;", tolerant of the array being
    # declared over multiple lines (size expression, line break before
    # the opening brace, etc.), and of nested struct-initializer braces
    # inside the body (e.g. submap_t's "{ {0,0,0,0}, };") -- the body is
    # matched lazily, so this stops at the first "}" immediately followed
    # by ";", which nested inner braces (always followed by "," instead)
    # don't satisfy.
    pattern = re.compile(
        r'\b' + re.escape(array_name) + r'\s*(\[[^;{]*\])?\s*=\s*\{(.*?)\}\s*;',
        re.DOTALL,
    )
    m = pattern.search(src_text)
    if not m:
        raise SystemExit(f"couldn't find initializer for '{array_name}'")

    body = m.group(2)
    tokens = re.findall(r'0[xX][0-9a-fA-F]+|-?\d+', body)
    values = [int(t, 0) for t in tokens]

    if width == 1:
        return bytes(v & 0xFF for v in values)

    out = bytearray()
    for v in values:
        # little-endian, matching z88dk/sdcc's own U16 layout on the Z80 --
        # see this file's header for why that's the right order here.
        out += (v & 0xFFFF).to_bytes(2, 'little')
    return bytes(out)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--width')]
    width = 1
    for a in sys.argv[1:]:
        if a.startswith('--width'):
            if '=' in a:
                width = int(a.split('=', 1)[1])
            else:
                idx = sys.argv.index(a)
                width = int(sys.argv[idx + 1])
                args = [x for x in args if x != sys.argv[idx + 1]]

    if len(args) not in (3, 4):
        sys.exit(__doc__)

    src_path, array_name, out_path = args[:3]
    expected = int(args[3]) if len(args) == 4 else None

    with open(src_path, 'r') as f:
        text = f.read()

    data = extract(text, array_name, width=width)

    if expected is not None and len(data) != expected:
        sys.exit(f"{array_name}: extracted {len(data)} bytes, expected {expected}")

    with open(out_path, 'wb') as f:
        f.write(data)

    print(f"{array_name}: wrote {len(data)} bytes -> {out_path}")


if __name__ == '__main__':
    main()
