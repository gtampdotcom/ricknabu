#!/usr/bin/env python3
"""build.py -- builds RICK.NABU with z88dk; the Python version of build.ps1
(same steps and output). Keep the two in step when changing switches.

Unity build: src/main.c #includes everything else.

Compiler messages are rewritten so their file paths are real: z88dk on
Windows strips the directory separators out of SDCC's warning paths
("srcengine/maps.c" instead of "src/engine/maps.c"), which would otherwise
break click-to-source in editors.

Usage:
    python build.py
    python build.py --no-deploy --asset-store D:\\ia\\store\\cpm\\n\\1
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))

# Build-time switches -- see src/engine/include/config.h for what each does.
DEFINES = {
    "BUILD_NABU": None,
    "DEBUG_LOAD_FILENAMES": 0,
    "SKIP_SPR0_GAMEPLAY_LOAD": 1,
    "SOUND_TRIM_MUSIC": 0,
    "SOUND_TRIM_PCM": 0,
    "SOUND_TRIM_FX": 0,
    "SCREEN_TRIM_TITLE": 0,
    "SCREEN_TRIM_INTRO": 0,
    "VDP_TARGET_F18A_ONLY": 0,
    "VDP_TARGET_9918A_ONLY": 0,
    "TEST_MBASE_LAST_SUBMAP": 0,  # TEST: MBASE (level 4) starts on its last submap -- set 0 to restore
    "MUSIC_SMOOTH_LOADS": 0,      # 0: title/hall-of-fame load in big reads with the music paused (they switch when the music ends); 1: 512-byte chunks, music keeps playing
}

# The CPU stack starts at $FF00 (crt0 REGISTER_SP, just below NABU-LIB's IM2
# interrupt vector table at $FF00) and grows down toward the end of BSS.
# If code + data + BSS leave less than this much room, the stack overwrites
# the last BSS variables and the game fails to launch -- so fail the build.
STACK_TOP = 0xFF00
MIN_STACK_BYTES = 512

INCLUDES = ["src", "src/engine", "src/engine/include", "src/hal"]


def fail(msg):
    print(msg)
    sys.exit(1)


def zcc_args():
    args = [
        "-v", "+nabu", "-vn", "-create-app", "-compiler=sdcc",
        "-O3", "--opt-code-size", "-SO3", "-m",
        # Use the project's own start-up code instead of z88dk's (it relocates
        # the program to $0000; see src/nabu_crt0.asm), so a stock z88dk
        # works. __CPU_CLOCK is set here because this crt0 doesn't define it.
        "-crt0=" + os.path.join(ROOT, "src", "nabu_crt0"),
        "-startup=0", "-pragma-define:__CPU_CLOCK=3579545",
        "-pragma-define:CLIB_DEFAULT_SCREEN_MODE=-1",
        "-pragma-define:CRT_ENABLE_STDIO=0",
        # Leave BSS (zeroed at start-up) out of rick.nabu, so the file fits
        # the boot ROM's load window. Relies on src/nabu_crt0.asm
        # relocating only up to __DATA_END_tail.
        "-pragma-define:CRT_TRIM_BSS=1",
        "-pragma-redirect:fputc_cons=_fputc_cons_stub",
    ]
    for k, v in DEFINES.items():
        args.append(f"-D{k}" if v is None else f"-D{k}={v}")
    args += [f"-I{i}" for i in INCLUDES]
    args += ["src/main.c", "-o", "rick.nabu"]
    return args


def path_repairer():
    """Map "separator-stripped" directory names back to real ones, longest
    first so "srcengineinclude" wins over "srcengine" and "src"."""
    dir_map = {}
    for dirpath, _, _ in os.walk("src"):
        rel = dirpath.replace("\\", "/")
        dir_map[rel.replace("/", "")] = rel
    keys = sorted(dir_map, key=len, reverse=True)

    def repair(p):
        if os.path.exists(p):
            return p
        for k in keys:
            if p.startswith(k):
                candidate = dir_map[k] + "/" + p[len(k):].lstrip("/\\")
                if os.path.exists(candidate):
                    return candidate
        return p

    return repair


def main():
    ap = argparse.ArgumentParser(description="Build RICK.NABU")
    ap.add_argument("--deploy", default=r"C:\nabu\local\rick.nabu",
                    help="where the built image is copied for the NABU Internet Adapter")
    ap.add_argument("--no-deploy", action="store_true", help="don't copy the built image")
    ap.add_argument("--asset-store", default=r"c:\nabu\store\cpm\n\1",
                    help="RetroNET store folder RICK.DAT and RICK.SPR are written to")
    ap.add_argument("--z88dk", default=os.environ.get("Z88DK", r"C:\z88dk"),
                    help="z88dk install folder (default: $Z88DK or C:\\z88dk)")
    opts = ap.parse_args()

    os.chdir(ROOT)
    env = dict(os.environ)
    env["ZCCCFG"] = os.path.join(opts.z88dk, "lib", "config") + os.sep
    env["PATH"] = os.path.join(opts.z88dk, "bin") + os.pathsep + env.get("PATH", "")

    # Pack assets/* into the two RetroNET files the game reads (RICK.DAT
    # and RICK.SPR) and regenerate src/engine/include/res.h with their
    # offsets, so the code and the packed files can never drift apart.
    if subprocess.call([sys.executable, os.path.join("tools", "pack_assets.py"), opts.asset_store]) != 0:
        fail("BUILD FAILED (pack_assets.py)")

    print("Compiling: src/main.c", flush=True)
    zcc = shutil.which("zcc", path=env["PATH"])
    if not zcc:
        fail(f"BUILD FAILED (zcc not found in {opts.z88dk}\\bin)")
    repair = path_repairer()
    msg = re.compile(r"^(.+?):(\d+):(.*)$")
    proc = subprocess.Popen([zcc] + zcc_args(), env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors="replace")
    for line in proc.stdout:
        line = line.rstrip("\r\n")
        m = msg.match(line)
        if m and not re.fullmatch(r"[A-Za-z]", m.group(1)):
            line = f"{repair(m.group(1))}:{m.group(2)}:{m.group(3)}"
        print(line, flush=True)
    if proc.wait() != 0:
        fail(f"BUILD FAILED (zcc exit {proc.returncode})")

    # CRT_TRIM_BSS writes BSS (all zeros) to this separate file instead of
    # into rick.nabu; nothing uses it.
    if os.path.exists("rick_BSS.bin"):
        os.remove("rick_BSS.bin")

    with open("rick.map", encoding="utf-8", errors="replace") as f:
        m = re.search(r"^__BSS_END_tail\s*= \$([0-9A-Fa-f]+)", f.read(), re.M)
    if not m:
        fail("BUILD FAILED (no __BSS_END_tail in rick.map)")
    stack_room = STACK_TOP - int(m.group(1), 16)
    if stack_room < MIN_STACK_BYTES:
        fail(f"BUILD FAILED: only {stack_room} bytes left for the stack below "
             f"${STACK_TOP:04X} (need {MIN_STACK_BYTES}) -- free RAM or code first")
    print(f"Stack room: {stack_room} bytes")

    if opts.deploy and not opts.no_deploy:
        shutil.copyfile("rick.nabu", opts.deploy)
        print(f"Deployed to {opts.deploy}")
    print("BUILD OK")


if __name__ == "__main__":
    main()
