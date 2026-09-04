# Wayfarer (top-down) - run every self-test, plus the shipping size assertion.
#
# Usage:  .\tools\run-tests.ps1              build what is stale, run everything
#         .\tools\run-tests.ps1 -Quick       skip the tests marked slow
#         .\tools\run-tests.ps1 -Filter a,b  run only tests whose name matches
#         .\tools\run-tests.ps1 -NoBuild     run whatever binaries already exist
#
# Exit code: 0 = all green. N > 0 = N tests failed. 99 = the build failed, so
# nothing ran - deliberately distinct from "some tests failed".
param([switch]$NoBuild, [switch]$Quick, [string]$Filter)

$ErrorActionPreference = 'Stop'

$ROOT  = Split-Path -Parent $PSScriptRoot
$TESTX = Join-Path $ROOT 'build\wayfarer-selftest.exe'
$SHIPX = Join-Path $ROOT 'build\wayfarer.exe'
$BUILD = Join-Path $ROOT 'build.ps1'
# Staleness is checked against the generated art header too, so a re-bake counts.
$SRC   = @((Join-Path $ROOT 'src\main.c'), (Join-Path $ROOT 'src\art_data.h'))

$TARGET = 1440000   # Must match build.ps1.

# Each row: n = test name (also the --flag), a = argv. slow = skipped by -Quick.
$tests = @(
    @{ n = 'sprite';   a = @('--sprite-test')   } # RLE round-trip, records, anchors
    @{ n = 'decode';   a = @('--decode-test')   } # shipping decoder vs malformed streams
    @{ n = 'fog';      a = @('--fog-test')      } # LUT vs fog_lerp, value hierarchy
    @{ n = 'autotile'; a = @('--autotile-test') } # blob slicing truth table, tile tables
    @{ n = 'tile';     a = @('--tile-test')     } # ground-pass coverage, base opacity
    @{ n = 'sort';     a = @('--sort-test')     } # y-sort order/stability, prop ghosting
    @{ n = 'mockup';   a = @('--mockup-test')   } # rendered-pixel census vs the mockups
    @{ n = 'font';     a = @('--font-test')     } # glyph coverage, exact pixel count
    @{ n = 'menu';     a = @('--menu-test')     } # rows, selection, actions, labels, settings file
    @{ n = 'hud';      a = @('--hud-test', '--seed', '1') }      # toast/banner ticks, minimap rule
    @{ n = 'save';     a = @('--save-test', '--seed', '1') }     # round trip + 12 rejection controls
    @{ n = 'map';      a = @('--map-test', '--seeds', '8', '--seed', '1') } # fragment placement, pickup, trails, screen
    @{ n = 'npc';      a = @('--npc-test', '--seeds', '12', '--seed', '1') }
    @{ n = 'story';    a = @('--story-test', '--seeds', '6', '--seed', '1') }  # cast, dialogue gate, castle states, souls, ending # orb-nearest-spawn, cast, Citizen_F forest rule
    # Both audio rows need a real output device. On a machine with none they
    # fail loudly rather than skipping, which is the honest outcome: silence is
    # exactly what this suite exists to catch.
    @{ n = 'audio';    a = @('--audio-test', '600') }            # tone path, deadline, rate handling
    @{ n = 'audiomix'; a = @('--audio-test', '1200', '--layers') } # 5 layers + SFX under contention
    @{ n = 'move';     a = @('--move-test', '--seeds', '20', '--seed', '1') }
    @{ n = 'gating';   a = @('--gating-test', '--seeds', '20', '--seed', '1') }
    @{ n = 'reach';    a = @('--reach-test', '--seeds', '30', '--seed', '1') }
    @{ n = 'play';     a = @('--play-test', '--seeds', '20', '--seed', '1'); slow = $true }
)

function Test-Stale([string]$exe) {
    if (-not (Test-Path $exe)) { return $true }
    $t = (Get-Item $exe).LastWriteTimeUtc
    foreach ($s in $SRC) {
        if ((Test-Path $s) -and ((Get-Item $s).LastWriteTimeUtc -gt $t)) { return $true }
    }
    return $false
}

function Invoke-Build([string[]]$buildArgs) {
    & powershell -ExecutionPolicy Bypass -File $BUILD @buildArgs | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host "BUILD FAILED - no tests run" -ForegroundColor Red
        exit 99
    }
}

# build\ is never cleaned, so running a binary older than src\ is a real failure
# mode rather than a theoretical one.
if (-not $NoBuild) {
    if (Test-Stale $SHIPX) { Invoke-Build @() }
    if (Test-Stale $TESTX) { Invoke-Build @('-SelfTest') }
}
if (-not (Test-Path $TESTX)) {
    Write-Host "self-test binary missing at $TESTX" -ForegroundColor Red
    exit 99
}

$results = @()
$failed  = 0
$skipped = 0

# SAVE_FILENAME is a bare relative path resolved against the process CWD, so the
# save test needs a predictable writable directory.
Push-Location $ROOT
try {
    foreach ($t in $tests) {
        if ($Filter -and -not (($Filter -split '[,\s]+') | Where-Object { $t.n -like "*$_*" })) { continue }
        if ($Quick -and $t.slow) {
            Write-Host ("SKIP  {0}" -f $t.n) -ForegroundColor DarkGray
            $skipped++
            continue
        }
        Write-Host ("RUN   {0}" -f $t.n) -ForegroundColor Cyan
        $sw = [Diagnostics.Stopwatch]::StartNew()
        & $TESTX @($t.a) | Out-Host
        $code = $LASTEXITCODE
        $sw.Stop()
        if ($code -ne 0) { $failed += $code }
        $results += [pscustomobject]@{
            Name = $t.n; Seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
            Status = if ($code -eq 0) { 'PASS' } else { "FAIL($code)" }
        }
    }

    # build.ps1 gates this, but nothing asserted it, so a green suite could still
    # describe an unshippable binary.
    Write-Host "RUN   size" -ForegroundColor Cyan
    $size = (Get-Item $SHIPX).Length
    if ($size -le $TARGET) {
        Write-Host ("size    : {0:N0} bytes, {1:N0} under the {2:N0} target" -f $size, ($TARGET - $size), $TARGET)
        $results += [pscustomobject]@{ Name = 'size'; Seconds = 0.0; Status = 'PASS' }
    } else {
        Write-Host ("size    : {0:N0} bytes - OVER the {1:N0} target" -f $size, $TARGET) -ForegroundColor Red
        $results += [pscustomobject]@{ Name = 'size'; Seconds = 0.0; Status = 'FAIL(1)' }
        $failed += 1
    }
}
finally { Pop-Location }

# Formatted by hand, not with Format-Table: Format-Table emits nothing when the
# host has no console width, which is exactly the case when this is redirected to
# a file or run by an agent - the two situations where the summary matters most.
Write-Host ""
Write-Host "---- summary (slowest first) ----"
foreach ($r in ($results | Sort-Object -Property Seconds -Descending)) {
    $colour = if ($r.Status -eq 'PASS') { 'Green' } else { 'Red' }
    Write-Host ("  {0,-14} {1,7:N1}s  {2}" -f $r.Name, $r.Seconds, $r.Status) -ForegroundColor $colour
}

if ($failed -gt 0) {
    Write-Host ""
    Write-Host ("{0} FAILING TEST(S)" -f $failed) -ForegroundColor Red
    exit $failed
}
Write-Host ""
if ($skipped -gt 0) {
    Write-Host "ALL GREEN (with skips - not a full pass)" -ForegroundColor Yellow
} else {
    Write-Host "ALL GREEN" -ForegroundColor Green
}
exit 0
