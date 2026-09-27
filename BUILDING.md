# Building Rick Dangerous for the NABU PC

Simple answer:

z88dk to build rick.nabu
Python 3 to build rick.dat and rick.spr

edit build.ps1 on Windows or build.py on Linux to change paths and then run it

Long answer:

This builds `RICK.NABU` (the program) and packs the game's data into
`RICK.DAT` and `RICK.SPR`, which the game streams over the network while it
runs. Builds are done on Windows.

## 1. What you need

| Tool | Notes |
|---|---|
| Windows with PowerShell 5.1 | Built into Windows 10/11. |
| [z88dk](https://z88dk.org) installed at `C:\z88dk` | Tested with nightly v25851 (2026-09-09). The path is set in `build.ps1` (`$z88dk`); `build.py` takes `--z88dk`. |
| Python 3 | On the `PATH` as `python`. Only the standard library is used. |
| NABU Internet Adapter (IA) | To run it: in an emulator (Marduk, MAME) or on a real NABU. |

## 2. About the start-up code (no setup needed)

A stock z88dk install works; nothing in it needs changing. The project
brings its own start-up code, `src/nabu_crt0.asm`, which `build.ps1`
uses instead of z88dk's (`-crt0=`).

Why: z88dk's own start-up code runs the program where the NABU boot ROM
loads it (`$140D`), and the game no longer fits there. The project's
version moves the program down to `$0000` after loading. The build also
leaves the zero-filled variables (BSS) out of the output file
(`CRT_TRIM_BSS=1`) so the boot ROM can load it; that only works because
this start-up code copies no more than what's in the file.

Where it comes from:
- **Relocation:** the `nabu_crt0.asm` from the `nabu_bare` branch of
  [agmsmith/z88dk](https://github.com/agmsmith/z88dk/tree/nabu_bare). That
  branch's `nabu.cfg` also adds `-pragma-define:__CPU_CLOCK=3579545` and
  `-startup=0`, so `build.ps1` passes those itself.
- **Two extra lines for this project:** `EXTERN __DATA_END_tail`, and the
  relocation copy size changed from `__BSS_END_tail-CRT_ORG_CODE` to
  `__DATA_END_tail-CRT_ORG_CODE`, so it only copies what's in the file
  (BSS isn't).
- **Keep `call cpm_platform_init`.** It looks like it does nothing, but
  removing it stops the game from booting.

## 3. Set the output folders

`build.ps1` has two parameters. Change the defaults at the top of the file,
or pass them on the command line:

| Parameter | Default | What goes there |
|---|---|---|
| `-Deploy` | `C:\nabu\local\rick.nabu` | Where the built program is copied. Point your IA at this file as its local homebrew program. Pass `-Deploy ''` to skip copying. |
| `-assetStore` | `c:\nabu\store\cpm\n\1` | The IA's RetroNET store folder `CPM/N/1`, where `RICK.DAT` and `RICK.SPR` are written. The game reads them from `CPM/N/1/RICK.DAT` and `CPM/N/1/RICK.SPR`. |

The folders must already exist.

## 4. Build

Pick one:

- **VS Code:** open the folder and press **Ctrl+Shift+B** (task "Build
  RICK.NABU"). Errors and warnings show in the Problems panel.
- **Command prompt:** `build.bat`
- **PowerShell:** `.\build.ps1` (or e.g.
  `.\build.ps1 -Deploy D:\ia\rick.nabu -assetStore D:\ia\store\cpm\n\1`)
- **Python:** `python build.py` does the same as `build.ps1`, with
  `--deploy`, `--no-deploy`, `--asset-store` and `--z88dk` (default:
  the `Z88DK` environment variable, else `C:\z88dk`).

A good build ends like this:

```
packed RICK.DAT (151936 bytes) and RICK.SPR (81792 bytes) into c:\nabu\store\cpm\n\1
Compiling: src/main.c
Stack room: 1000 bytes
Deployed to C:\nabu\local\rick.nabu
BUILD OK
```

What the build does, in order:

1. `tools/pack_assets.py` packs `assets/*` into `RICK.DAT` and
   `RICK.SPR` in the store folder, and regenerates
   `src/engine/include/res.h` (the data offsets the code uses).
2. `zcc` compiles `src/main.c`. It's a single-file build: `main.c`
   `#include`s every other `.c` file. BSS is left out of `rick.nabu`
   (`CRT_TRIM_BSS=1`); z88dk writes it to `rick_BSS.bin` instead, which
   isn't needed and is deleted.
3. A stack check: the program and its variables must leave at least 512
   bytes below `$FF00` for the stack, or the build fails. Too little stack
   room makes the game crash in odd ways, so treat this as a hard limit.
4. The program is copied to `-Deploy`.

Outputs in the project folder: `rick.nabu` (program), `rick.map` (linker
map, for sizes and addresses).

## 5. Run

1. Start the NABU Internet Adapter with its store folder set to the one
   above, and the local homebrew program set to the `-Deploy` file.
2. Start the NABU (or Marduk / MAME connected to the IA). It loads
   `RICK.NABU`, which then fetches `RICK.DAT` and `RICK.SPR` from the store.

Both an F18A (or Pico9918) and a stock TMS9918A video chip are supported
and detected at boot.

Publishing: nabu.ca uppercases uploaded file names, which is why the files
are called `RICK.DAT` and `RICK.SPR` (the game asks for uppercase names).
Upload `RICK.NABU`, `RICK.DAT` and `RICK.SPR` together; a new build usually
needs the matching `RICK.DAT`.

## 6. Build switches

Set in `build.ps1` (`$defines`) and `build.py` (`DEFINES`); change both.
Each is explained in
`src/engine/include/config.h` or the file that uses it. The normal build
uses these values:

| Switch | Normal | What it does |
|---|---|---|
| `VDP_TARGET_F18A_ONLY` | 0 | 1 = F18A-only build (drops stock TMS9918A support). |
| `VDP_TARGET_9918A_ONLY` | 0 | 1 = stock-TMS9918A-only build (drops F18A code and data). |
| `SKIP_SPR0_GAMEPLAY_LOAD` | 1 | Skips loading the 6 KB stock sprite table at boot; stock sprites stream on demand instead. |
| `MUSIC_SMOOTH_LOADS` | 0 | 0 = title/hall-of-fame screens load fast (big reads, music paused); they switch when the music ends, so the pause falls between play-throughs. 1 = loads in 512-byte pieces with the music playing (slower). |
| `SOUND_TRIM_MUSIC` / `_PCM` / `_FX` | 0 | 1 = leave out music / the PCM sample / sound effects, to save space. |
| `SCREEN_TRIM_TITLE` / `_INTRO` | 0 | 1 = leave out the title picture / level intro screens. |
| `DEBUG_LOAD_FILENAMES` | 0 | 1 = print every network read to the IA console. |
| `TEST_MBASE_LAST_SUBMAP` | 0 | Test only: level 4 starts on its last screen. |

Also keep `.vscode/c_cpp_properties.json`'s `defines` in step with these,
or VS Code will grey out the wrong code.

## 7. Changing the game data

The data files in `assets/` and their formats are described in
`assets/readme.txt`. Edit or replace a file there and rebuild; the
packer picks it up. A new file also has to be added to `SINGLES` or
`GROUPS` in `tools/pack_assets.py`. Other helpers in `tools/`:

- `interleave_ecm_planes.py`: rebuilds `ECMSPR0-4.DAT` from per-plane
  sprite art.
- `vgm_to_dat.py`, `dat_to_vgm.py`: convert music and sound effects to and
  from `.VGM`.
- `sn_to_ay.py`: converts a `.VGM` recorded on an SN76489 sound chip (TI,
  ColecoVision, ...) into one for the NABU's AY-3-8910; run it before
  `vgm_to_dat.py`.
- `extract_assets.py`: pulls a C array out of a source file into a binary
  file.
- `trim_nabu.ps1`: cuts the zero-byte tail off any `.nabu` program, even
  without its source, after checking in the program's code that it's safe.
  Not needed for this build, which leaves the tail out with `CRT_TRIM_BSS`.

## Troubleshooting

| Problem | Likely cause |
|---|---|
| `zcc` not found | z88dk isn't at `C:\z88dk`; fix `$z88dk` in `build.ps1`, or pass `--z88dk` to `build.py`. |
| `BUILD FAILED (pack_assets.py)` | Python missing, the store folder doesn't exist, or a file listed in `pack_assets.py` is missing from `assets/`. |
| `BUILD FAILED: only N bytes left for the stack` | The program grew too big. Free RAM (e.g. a smaller cache, `ECM_SLOT_CACHE_SLOTS` in `src/engine/include/sprites.h`) or code. |
| Builds fine but the NABU shows nothing / resets | The IA is serving an old `RICK.NABU`, or `src/nabu_crt0.asm` was changed (see step 2). |
| NABU shows `CPM/N/1/RICK.DAT` (or `.SPR`) `NOT FOUND OR OLD` | That file isn't in the IA's store folder, or it's shorter than this build expects (an older build's). Rebuild, or copy both files from the `-assetStore` folder. |
| NABU shows `... NO RETRONET REPLY` | The adapter didn't answer a file read within ~5 seconds: it doesn't support RetroNET file reads (use DJ Sures' Internet Adapter), or it stopped (restart it). |
| Game starts but graphics, music or levels are wrong | `RICK.DAT`/`RICK.SPR` in the store don't match this build; rebuild so they're repacked, and check `-assetStore`. |
