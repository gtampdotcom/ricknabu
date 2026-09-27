# build.ps1 -- builds RICK.NABU with z88dk (unity build: src/main.c #includes
# everything else). Run directly, via build.bat, or with Ctrl+Shift+B in VS Code
# (.vscode/tasks.json). build.py does the same in Python; keep the two in step.
#
# Compiler messages are rewritten so their file paths are real: z88dk on
# Windows strips the directory separators out of SDCC's warning paths
# ("srcengine/maps.c" instead of "src/engine/maps.c"), which would otherwise
# break click-to-source in VS Code's Problems panel.

param(
  # Where the built image is copied for the NABU Internet Adapter.
  [string]$Deploy = 'C:\nabu\local\rick.nabu',
  # RetroNET store folder the packed asset files (RICK.DAT, RICK.SPR) are written to.
  [string]$assetStore = 'c:\nabu\store\cpm\n\1'
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# .NET file calls resolve relative paths against the PROCESS directory, not
# PowerShell's location -- keep them in sync.
[Environment]::CurrentDirectory = $PSScriptRoot

$z88dk = 'C:\z88dk'
$env:ZCCCFG = "$z88dk\lib\config\"
$env:PATH = "$z88dk\bin;$env:PATH"

# Build-time switches -- see src/engine/include/config.h for what each does.
$defines = [ordered]@{
  BUILD_NABU              = $null
  DEBUG_LOAD_FILENAMES    = 0
  SKIP_SPR0_GAMEPLAY_LOAD = 1
  SOUND_TRIM_MUSIC        = 0
  SOUND_TRIM_PCM          = 0
  SOUND_TRIM_FX           = 0
  SCREEN_TRIM_TITLE       = 0
  SCREEN_TRIM_INTRO       = 0
  VDP_TARGET_F18A_ONLY    = 0
  VDP_TARGET_9918A_ONLY   = 0
  TEST_MBASE_LAST_SUBMAP  = 0   # TEST: MBASE (level 4) starts on its last submap -- set 0 to restore
  MUSIC_SMOOTH_LOADS      = 0   # 0: title/hall-of-fame load in big reads with the music paused (they switch when the music ends); 1: 512-byte chunks, music keeps playing
}

# The CPU stack starts at $FF00 (crt0 REGISTER_SP, just below NABU-LIB's IM2
# interrupt vector table at $FF00) and grows down toward the end of BSS.
# If code + data + BSS leave less than this much room, the stack overwrites
# the last BSS variables and the game fails to launch -- so fail the build.
$stackTop = 0xFF00
$minStackBytes = 512

$includes = @('src', 'src/engine', 'src/engine/include', 'src/hal')

$zccArgs = @(
  '-v', '+nabu', '-vn', '-create-app', '-compiler=sdcc',
  '-O3', '--opt-code-size', '-SO3', '-m',
  # Use the project's own start-up code instead of z88dk's (it relocates the
  # program to $0000; see src/nabu_crt0.asm), so a stock z88dk works.
  # __CPU_CLOCK is set here because this crt0 doesn't define it.
  "-crt0=$PSScriptRoot\src\nabu_crt0", '-startup=0', '-pragma-define:__CPU_CLOCK=3579545',
  '-pragma-define:CLIB_DEFAULT_SCREEN_MODE=-1',
  '-pragma-define:CRT_ENABLE_STDIO=0',
  # Leave BSS (zeroed at start-up) out of rick.nabu, so the file fits the
  # boot ROM's load window. Relies on src/nabu_crt0.asm relocating
  # only up to __DATA_END_tail.
  '-pragma-define:CRT_TRIM_BSS=1',
  '-pragma-redirect:fputc_cons=_fputc_cons_stub'
)
foreach ($k in $defines.Keys) {
  if ($null -eq $defines[$k]) { $zccArgs += "-D$k" } else { $zccArgs += "-D$k=$($defines[$k])" }
}
foreach ($i in $includes) { $zccArgs += "-I$i" }
$zccArgs += @('src/main.c', '-o', 'rick.nabu')

# Map "separator-stripped" directory names back to real ones, longest first
# so "srcengineinclude" wins over "srcengine" and "src".
$dirMap = @{}
foreach ($d in @('src') + @(Get-ChildItem src -Recurse -Directory | ForEach-Object {
      $_.FullName.Substring($PSScriptRoot.Length + 1) })) {
  $dirMap[($d -replace '[\\/]', '')] = ($d -replace '\\', '/')
}
$dirKeys = $dirMap.Keys | Sort-Object Length -Descending

function Repair-Path([string]$p) {
  if (Test-Path -LiteralPath $p) { return $p }
  foreach ($k in $dirKeys) {
    if ($p.StartsWith($k)) {
      $candidate = $dirMap[$k] + '/' + $p.Substring($k.Length).TrimStart('/', '\')
      if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
  }
  return $p
}

# Pack assets/* into the two RetroNET files the game reads (RICK.DAT
# and RICK.SPR) and regenerate src/engine/include/res.h with their offsets, so the
# code and the packed files can never drift apart.
& python "$PSScriptRoot\tools\pack_assets.py" $assetStore
if ($LASTEXITCODE -ne 0) { Write-Host 'BUILD FAILED (pack_assets.py)' -ForegroundColor Red; exit 1 }

Write-Host 'Compiling: src/main.c'
$ErrorActionPreference = 'Continue'   # zcc writes progress to stderr
& zcc @zccArgs 2>&1 | ForEach-Object {
  $line = "$_"
  if ($line -match '^(.+?):(\d+):(.*)$' -and $Matches[1] -notmatch '^[A-Za-z]$') {
    $line = (Repair-Path $Matches[1]) + ':' + $Matches[2] + ':' + $Matches[3]
  }
  $line
}
$zccExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'

if ($zccExit -ne 0) {
  Write-Host "BUILD FAILED (zcc exit $zccExit)" -ForegroundColor Red
  exit 1
}

# CRT_TRIM_BSS writes BSS (all zeros) to this separate file instead of into
# rick.nabu; nothing uses it.
Remove-Item rick_BSS.bin -ErrorAction SilentlyContinue

$bssEnd = Select-String -Path rick.map -Pattern '^__BSS_END_tail\s*= \$([0-9A-Fa-f]+)' | Select-Object -First 1
if (-not $bssEnd) { Write-Host 'BUILD FAILED (no __BSS_END_tail in rick.map)' -ForegroundColor Red; exit 1 }
$stackRoom = $stackTop - [Convert]::ToInt32($bssEnd.Matches[0].Groups[1].Value, 16)
if ($stackRoom -lt $minStackBytes) {
  Write-Host ("BUILD FAILED: only {0} bytes left for the stack below `${1:X4} (need {2}) -- free RAM or code first" -f $stackRoom, $stackTop, $minStackBytes) -ForegroundColor Red
  exit 1
}
Write-Host "Stack room: $stackRoom bytes"

if ($Deploy) {
  Copy-Item rick.nabu $Deploy -Force
  Write-Host "Deployed to $Deploy"
}
Write-Host 'BUILD OK' -ForegroundColor Green
