#!/usr/bin/env python3
"""
interleave_ecm_planes.py -- builds the F18A ECM sprite page files
(assets/ECMSPR0-4.DAT) from the per-plane source files.

Each ECM sprite has 3 colour bit-planes (BIT0/BIT1/BIT2). The source art
comes as one file per page per plane, SPR<page>B<plane>.DAT (for example
SPR0B0.DAT, SPR0B1.DAT, SPR0B2.DAT for page 0), each plane-major:

    SPR<P>B0.DAT = [frame0.BIT0][frame1.BIT0]...[frameN-1.BIT0]   (N*128 bytes)
    SPR<P>B1.DAT = [frame0.BIT1][frame1.BIT1]...                  (N*128 bytes)
    SPR<P>B2.DAT = [frame0.BIT2][frame1.BIT2]...                  (N*128 bytes)

The game wants all three planes of one frame together, so it can fetch a
sprite in one network read (engine/sprites.c's ecm_fetch_slot()):

    ECMSPR<P>.DAT = [frame0.BIT0][frame0.BIT1][frame0.BIT2]
                    [frame1.BIT0][frame1.BIT1][frame1.BIT2]
                    ...                                          (N*384 bytes)

N is 48 (SPRITE_PAGE_SIZE, engine/include/sprites.h) for pages 0-3, and
21 (SPRITE_FINAL_SIZE) for page 4 -- 213 sprites in total.

Usage:
    interleave_ecm_planes.py SRC_DIR DST_DIR [--pages 0,1,2,3,4]

SRC_DIR must contain the SPR<page>B<plane>.DAT files (the original
per-plane art. DST_DIR gets the ECMSPR<page>.DAT files (normally assets).
Source files are only read.
"""
import argparse
import sys
from pathlib import Path

SPRITE_SIZE = 128
SPRITE_PAGE_SIZE = 48   # frames on pages 0-3 -- engine/include/sprites.h
SPRITE_FINAL_SIZE = 21  # frames on page 4 -- engine/include/sprites.h


def frame_count(page: int) -> int:
    return SPRITE_FINAL_SIZE if page == 4 else SPRITE_PAGE_SIZE


def interleave_page(src_dir: Path, dst_dir: Path, page: int) -> None:
    n = frame_count(page)
    expected = n * SPRITE_SIZE

    planes = []
    for plane in range(3):
        path = src_dir / f"SPR{page}B{plane}.DAT"
        if not path.is_file():
            raise FileNotFoundError(
                f"missing {path} (page {page}, plane {plane}, "
                f"expected {expected} bytes for {n} frames)"
            )
        data = path.read_bytes()
        if len(data) != expected:
            raise ValueError(
                f"{path} is {len(data)} bytes, expected exactly {expected} "
                f"({n} frames * {SPRITE_SIZE} bytes)"
            )
        planes.append(data)

    out = bytearray(n * 3 * SPRITE_SIZE)
    for frame in range(n):
        base_in = frame * SPRITE_SIZE
        base_out = frame * 3 * SPRITE_SIZE
        for plane in range(3):
            out[base_out + plane * SPRITE_SIZE : base_out + (plane + 1) * SPRITE_SIZE] = \
                planes[plane][base_in : base_in + SPRITE_SIZE]

    dst_path = dst_dir / f"ECMSPR{page}.DAT"
    dst_path.write_bytes(out)
    print(f"wrote {dst_path}  ({len(out)} bytes, {n} frames x 384)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("src_dir", type=Path, help="directory holding SPR<page>B<plane>.DAT")
    parser.add_argument("dst_dir", type=Path, help="directory to write ECMSPR<page>.DAT into")
    parser.add_argument("--pages", default="0,1,2,3,4",
                        help="comma-separated page numbers to process (default: 0,1,2,3,4)")
    args = parser.parse_args()

    if not args.src_dir.is_dir():
        print(f"error: {args.src_dir} is not a directory", file=sys.stderr)
        return 1
    args.dst_dir.mkdir(parents=True, exist_ok=True)

    pages = [int(p.strip()) for p in args.pages.split(",") if p.strip()]
    for page in pages:
        try:
            interleave_page(args.src_dir, args.dst_dir, page)
        except (FileNotFoundError, ValueError) as e:
            print(f"error: {e}", file=sys.stderr)
            return 1

    print(f"done -- {len(pages)} page file(s) written to {args.dst_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
