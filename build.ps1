# Wayfarer (top-down) release build. Toolchain notes and rationale: README.md.
# Usage:  .\build.ps1            release build + size check
#         .\build.ps1 -Map       also emit build\wayfarer.map for size forensics
#
# -SelfTest builds a SEPARATE binary (build\wayfarer-selftest.exe) with the
# verification scaffolding and a console attached. The shipping wayfarer.exe
# never contains it - that is the point of keeping them separate.
param([switch]$Map, [switch]$SelfTest)

$ErrorActionPreference = 'Stop'

# Toolchain lives outside the repo. Override the root with $env:WAYFARER_TOOLS.
$TOOLS  = if ($env:WAYFARER_TOOLS) { $env:WAYFARER_TOOLS } else { 'G:\tools' }
$DEVKIT = Join-Path $TOOLS 'w64devkit'
$GCC    = Join-Path $DEVKIT 'bin\gcc.exe'
$SDL    = Join-Path $TOOLS 'SDL2-min'   # our cut-down static SDL2; from build-sdl2.ps1

if (-not (Test-Path $GCC)) {
    Write-Host "gcc not found at $GCC" -ForegroundColor Red
    Write-Host "Install w64devkit under $TOOLS, or set `$env:WAYFARER_TOOLS. See README.md." -ForegroundColor Red
    exit 1
}
if (-not (Test-Path (Join-Path $SDL 'lib\libSDL2.a'))) {
    Write-Host "Minimal SDL2 not found at $SDL - run .\build-sdl2.ps1 first." -ForegroundColor Red
    exit 1
}

# gcc shells out to as.exe / ld.exe by name, so the devkit's bin must be on PATH.
# Normally w64devkit.exe does this; we are invoking gcc directly, so do it here.
if ($env:PATH -notlike "*$DEVKIT\bin*") { $env:PATH = "$DEVKIT\bin;$env:PATH" }
$ROOT = $PSScriptRoot
$OUT  = Join-Path $ROOT ($(if ($SelfTest) { 'build\wayfarer-selftest.exe' } else { 'build\wayfarer.exe' }))

# Contest budget. "1.44MB" has three definitions; HARD is the floppy standard.
$HARD   = 1474560   # absolute ceiling
$TARGET = 1440000   # what we ship under
$WARN   = 1200000   # stop-and-raise threshold

if (-not (Test-Path (Split-Path $OUT))) { New-Item -ItemType Directory -Force (Split-Path $OUT) | Out-Null }

# -Werror because "zero warnings" was a rule nothing enforced: an ungated
# self-test-only helper warned in the SHIPPING build for several phases without
# failing anything, because the rule lived in CLAUDE.md and not in the compiler.
# An uncalled static is a warning AND dead shipped bytes, so it is worth an
# error. The self-test build gets it too - a warning there is the same bug.
$cflags = @(
    '-std=c99', '-Os', '-Wall', '-Wextra', '-Werror',
    '-ffunction-sections', '-fdata-sections',
    '-fno-ident', '-fno-asynchronous-unwind-tables'
)
# NOTE: no -flto. w64devkit's gcc is built without LTO support ("LTO support has
# not been enabled in this configuration"). Costs us little: we are one
# translation unit, and libSDL2.a is prebuilt without GCC IR so LTO could not
# reach into it regardless. If we outgrow one .c, use a unity build rather than
# adding -flto.
$includes = @("-I$SDL\include", "-I$SDL\include\SDL2")
$ldflags = @(
    '-static', '-s',
    '-Wl,--gc-sections',
    "-L$SDL\lib"
)
# Self-test needs a console for its report; the shipping build must not have one.
if ($SelfTest) { $cflags += '-DWAYFARER_SELFTEST=1'; $ldflags += '-mconsole' }
else           { $ldflags += '-mwindows' }
# Static SDL2 dependency list, taken verbatim from $SDL\lib\pkgconfig\sdl2.pc
$libs = @(
    '-lmingw32', '-lSDL2main', '-lSDL2',
    '-Wl,--dynamicbase', '-Wl,--nxcompat', '-Wl,--high-entropy-va',
    '-lm', '-lkernel32', '-luser32', '-lgdi32', '-lwinmm', '-limm32',
    '-lole32', '-loleaut32', '-lversion', '-luuid', '-ladvapi32',
    '-lsetupapi', '-lshell32'
)
# The map is ~1.5 MB of linker diagnostics. Useful, but it must never sit in the
# tree unnoticed, so a normal build deletes any stale one. Regenerate with -Map.
$mapFile = "$ROOT\build\wayfarer.map"
if ($Map) { $ldflags += "-Wl,-Map=$mapFile" }
elseif (Test-Path $mapFile) { Remove-Item $mapFile -Force }

$sources = @("$ROOT\src\main.c")
$argList = $cflags + $includes + $sources + '-o' + $OUT + $ldflags + $libs

Write-Host "gcc $($argList -join ' ')" -ForegroundColor DarkGray
& $GCC @argList
if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

# A successful link whose output is GONE is not a build system problem: it is
# Windows Defender quarantining wayfarer.exe as Trojan:Win32/Wacatac.B!ml, an
# ML false positive that lands a minute or two after the link. Without this
# check the next thing to touch the file reports the confusing half of the
# story - `Get-Item : Cannot find path` from run-tests.ps1's size step, after
# twenty green suites - and the antivirus is never mentioned. Same story behind
# an intermittent `ld.exe: cannot open output file ...: Permission denied`,
# which is the scanner holding the file open mid-link.
if (-not (Test-Path $OUT)) {
    Write-Host ""
    Write-Host ("LINKED OK BUT THE OUTPUT IS GONE: {0}" -f $OUT) -ForegroundColor Red
    Write-Host "gcc reported success, so something removed it after the link." -ForegroundColor Red
    Write-Host "This is almost certainly Windows Defender - see tools\av-exclusion.ps1," -ForegroundColor Red
    Write-Host "which excludes this one path (run it once, elevated)." -ForegroundColor Red
    exit 4
}

$size = (Get-Item $OUT).Length

Write-Host ""
Write-Host ("BUILD OK   {0}" -f $OUT) -ForegroundColor Green
Write-Host ("size       {0:N0} bytes" -f $size)

# Only the shipping binary is tracked and budget-gated. A self-test build is not
# a deliverable, so it must not move the recorded size or trip the budget.
if ($SelfTest) {
    Write-Host "note       self-test build - not budget-tracked, not shippable" -ForegroundColor Yellow
    exit 0
}

$sizeFile = Join-Path $ROOT 'build\.last_size'
$prev = if (Test-Path $sizeFile) { [int](Get-Content $sizeFile -Raw).Trim() } else { $null }
Set-Content -Path $sizeFile -Value $size -Encoding ascii

if ($null -ne $prev) {
    $d = $size - $prev
    $sign = if ($d -ge 0) { '+' } else { '' }
    Write-Host ("delta      {0}{1:N0} bytes (prev {2:N0})" -f $sign, $d, $prev)
} else {
    Write-Host "delta      n/a (first recorded build)"
}
Write-Host ("headroom   {0:N0} bytes under the {1:N0} ship target" -f ($TARGET - $size), $TARGET)
Write-Host ("           {0:N0} bytes under the {1:N0} hard limit" -f ($HARD - $size), $HARD)

if ($size -gt $HARD) {
    Write-Host "OVER HARD LIMIT - not shippable" -ForegroundColor Red
    exit 2
}
if ($size -gt $TARGET) {
    Write-Host "OVER SHIP TARGET - cut before continuing" -ForegroundColor Red
    exit 3
}
if ($size -gt $WARN) {
    Write-Host ("WARNING: past the {0:N0}-byte raise threshold - flag this before adding features" -f $WARN) -ForegroundColor Yellow
}
