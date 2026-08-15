# Wayfarer self-test runner. Closes QA-1 in docs/production-gap-analysis.md.
#
# Runs every self-test entry point in one pass and returns the number of failing
# tests as the process exit code, so "25/25 green" becomes a checked property of
# the current commit rather than a hand-recorded claim about a past moment.
#
# Every test already returns a correct exit code, so this needs no change to
# src/main.c. Test 26 asserts the shipping binary's size budget (QA-13).
#
# Usage:  .\tools\run-tests.ps1                build if stale, run all 26
#         .\tools\run-tests.ps1 -NoBuild       run whatever binaries exist
#         .\tools\run-tests.ps1 -Quick         skip the two long batch tests
#         .\tools\run-tests.ps1 -Filter land   run only tests matching a name
#         .\tools\run-tests.ps1 -Filter 'land,reach,play'
#
# Exit code: 0 = all green. N > 0 = N tests failed.

param(
    [switch]$NoBuild,
    [switch]$Quick,
    [string]$Filter
)

$ErrorActionPreference = 'Stop'

$ROOT   = Split-Path -Parent $PSScriptRoot
$TESTX  = Join-Path $ROOT 'build\wayfarer-selftest.exe'
$SHIPX  = Join-Path $ROOT 'build\wayfarer.exe'
$BUILD  = Join-Path $ROOT 'build.ps1'
$SRC    = @(
    (Join-Path $ROOT 'src\main.c'),
    (Join-Path $ROOT 'src\art_data.h')
)

# Must match build.ps1. The floppy standard ($HARD) is 1474560; we ship under
# $TARGET so there is always slack for a last-minute fix.
$TARGET = 1440000

# ---------------------------------------------------------------------------
# The suite.
#
# Batch sizes are the documented invocations from README.md, deliberately
# unchanged so this script measures the same thing the project has always
# claimed. Two known gaps, both tracked, neither silently papered over here:
#
#   QA-2  --land-test fails seeds 85, 417 and 430 (22-23% reach against a 50%
#         bar). All three are past the default --seeds 30 window, so this suite
#         is green while a known generation-quality defect exists. Fix the
#         generator or promote those seeds to explicit regression cases; do not
#         widen --seeds here without deciding which.
#
#   --input-test and --autoplay are excluded: both open a window and depend on
#         wall-clock duration, so they are not unattended-safe.
# ---------------------------------------------------------------------------
$tests = @(
    @{ n = 'iso';      a = @('--iso-test') }
    @{ n = 'font';     a = @('--font-test') }
    @{ n = 'fog';      a = @('--fog-test') }
    @{ n = 'sprite';   a = @('--sprite-test') }
    @{ n = 'fade';     a = @('--fade-test') }
    @{ n = 'rebuild';  a = @('--rebuild-test') }
    @{ n = 'ground';   a = @('--ground-test') }
    @{ n = 'motion';   a = @('--motion-test') }
    @{ n = 'hud';      a = @('--hud-test') }
    @{ n = 'rng';      a = @('--rng-test', '--seed', '1') }
    @{ n = 'save';     a = @('--save-test') }
    @{ n = 'land';     a = @('--land-test',    '--seeds', '30',  '--seed', '1') }
    @{ n = 'village';  a = @('--village-test', '--seeds', '30',  '--seed', '1') }
    @{ n = 'path';     a = @('--path-test',    '--seeds', '20') }
    @{ n = 'move';     a = @('--move-test',    '--seeds', '20',  '--seed', '1') }
    @{ n = 'region';   a = @('--region-test',  '--seeds', '30',  '--seed', '1') }
    @{ n = 'reach';    a = @('--reach-test',   '--seeds', '50',  '--seed', '1') }
    @{ n = 'bridge';   a = @('--bridge-test',  '--seeds', '200', '--seed', '1'); slow = $true }
    @{ n = 'sector';   a = @('--sector-test',  '--seeds', '30',  '--seed', '1') }
    @{ n = 'portal';   a = @('--portal-test',  '--seeds', '30',  '--seed', '1') }
    @{ n = 'shard';    a = @('--shard-test',   '--seeds', '30',  '--seed', '1') }
    @{ n = 'gating';   a = @('--gating-test',  '--seeds', '30',  '--seed', '1') }
    @{ n = 'aether';   a = @('--aether-test',  '--seeds', '20') }
    @{ n = 'play';     a = @('--play-test',    '--seeds', '50',  '--seed', '1'); slow = $true }
    @{ n = 'audio';    a = @('--audio-test', '3000', '--sfx') }
)

$names = @()
if ($Filter) { $names = $Filter -split '[,\s]+' | Where-Object { $_ } }

function Test-Stale([string]$exe) {
    if (-not (Test-Path $exe)) { return $true }
    $t = (Get-Item $exe).LastWriteTimeUtc
    foreach ($s in $SRC) {
        if ((Test-Path $s) -and ((Get-Item $s).LastWriteTimeUtc -gt $t)) { return $true }
    }
    return $false
}

function Invoke-Build([string]$label, [string[]]$buildArgs) {
    Write-Host ("building   {0} ({1})" -f $label, ($buildArgs -join ' ')) -ForegroundColor Cyan
    & $BUILD @buildArgs | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("BUILD FAILED for {0} (exit {1}) - cannot run the suite" -f $label, $LASTEXITCODE) -ForegroundColor Red
        exit 99
    }
}

# ---------------------------------------------------------------------------
# Build if needed. QA-12: build\ is never cleaned and has held a stale
# wayfarer-selftest.old.exe, so running a binary older than src\ is a real
# failure mode, not a theoretical one.
# ---------------------------------------------------------------------------
if (-not $NoBuild) {
    if (Test-Stale $TESTX) { Invoke-Build 'self-test' @('-SelfTest') }
    else { Write-Host "self-test binary is current" -ForegroundColor DarkGray }

    if (Test-Stale $SHIPX) { Invoke-Build 'shipping' @() }
    else { Write-Host "shipping binary is current" -ForegroundColor DarkGray }
    Write-Host ""
}

if (-not (Test-Path $TESTX)) {
    Write-Host "self-test binary not found at $TESTX - run .\build.ps1 -SelfTest" -ForegroundColor Red
    exit 99
}

# ---------------------------------------------------------------------------
# Run. CWD is the repo root on purpose: SAVE_FILENAME is the bare relative path
# "wayfarer.sav" resolved against the process CWD (SEC-4), so --save-test must
# run from a predictable, writable directory.
# ---------------------------------------------------------------------------
$results = @()
$suite = [Diagnostics.Stopwatch]::StartNew()

Push-Location $ROOT
try {
    foreach ($t in $tests) {
        if ($names.Count -gt 0 -and -not ($names | Where-Object { $t.n -like "*$_*" })) { continue }
        if ($Quick -and $t.slow) {
            Write-Host ("SKIP  {0,-9} (-Quick)" -f $t.n) -ForegroundColor DarkGray
            $results += [pscustomobject]@{ Name = $t.n; Status = 'SKIP'; Code = 0; Seconds = 0.0 }
            continue
        }

        $a = @($t.a)
        Write-Host ""
        Write-Host ("--- {0}  [{1}]" -f $t.n, ($a -join ' ')) -ForegroundColor Cyan

        $sw = [Diagnostics.Stopwatch]::StartNew()
        & $TESTX @a | Out-Host
        $rc = $LASTEXITCODE
        $sw.Stop()

        $status = if ($rc -eq 0) { 'PASS' } else { 'FAIL' }
        $colour = if ($rc -eq 0) { 'Green' } else { 'Red' }
        Write-Host ("{0}  {1,-9} {2,7:N1}s" -f $status, $t.n, $sw.Elapsed.TotalSeconds) -ForegroundColor $colour
        $results += [pscustomobject]@{
            Name    = $t.n
            Status  = $status
            Code    = $rc
            Seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
        }
    }
}
finally {
    Pop-Location
}

# ---------------------------------------------------------------------------
# Test 26 - the size budget (QA-13). build.ps1 gates this, but nothing asserted
# it, so a green suite could still describe an unshippable binary.
# ---------------------------------------------------------------------------
if ($names.Count -eq 0 -or ($names | Where-Object { 'size' -like "*$_*" })) {
    Write-Host ""
    Write-Host "--- size  [build\wayfarer.exe <= $('{0:N0}' -f $TARGET) bytes]" -ForegroundColor Cyan
    if (-not (Test-Path $SHIPX)) {
        Write-Host "FAIL  size      shipping binary not found - run .\build.ps1" -ForegroundColor Red
        $results += [pscustomobject]@{ Name = 'size'; Status = 'FAIL'; Code = 1; Seconds = 0.0 }
    }
    else {
        $size = (Get-Item $SHIPX).Length
        if ($size -le $TARGET) {
            Write-Host ("PASS  size      {0:N0} bytes, {1:N0} under target" -f $size, ($TARGET - $size)) -ForegroundColor Green
            $results += [pscustomobject]@{ Name = 'size'; Status = 'PASS'; Code = 0; Seconds = 0.0 }
        }
        else {
            Write-Host ("FAIL  size      {0:N0} bytes, {1:N0} OVER target" -f $size, ($size - $TARGET)) -ForegroundColor Red
            $results += [pscustomobject]@{ Name = 'size'; Status = 'FAIL'; Code = 1; Seconds = 0.0 }
        }
    }
}

$suite.Stop()

# ---------------------------------------------------------------------------
# Summary. QA-7: suite runtime is significant (~5 min) and was untracked, so a
# generation change that doubles it went unnoticed until someone waited.
# ---------------------------------------------------------------------------
$pass = @($results | Where-Object { $_.Status -eq 'PASS' }).Count
$fail = @($results | Where-Object { $_.Status -eq 'FAIL' }).Count
$skip = @($results | Where-Object { $_.Status -eq 'SKIP' }).Count

# Rows are formatted by hand rather than with Format-Table: Format-Table emits
# nothing when the host has no console width, which is exactly the case when
# this script is redirected to a file or run by a CI agent - i.e. the two
# situations where the summary matters most.
Write-Host ""
Write-Host "==================== SUMMARY ====================" -ForegroundColor White
Write-Host ("{0,-10} {1,-6} {2,4} {3,8}" -f 'test', 'result', 'exit', 'sec')
foreach ($r in ($results | Sort-Object -Property Seconds -Descending)) {
    $colour = switch ($r.Status) { 'PASS' { 'Green' } 'FAIL' { 'Red' } default { 'DarkGray' } }
    Write-Host ("{0,-10} {1,-6} {2,4} {3,8:N1}" -f $r.Name, $r.Status, $r.Code, $r.Seconds) -ForegroundColor $colour
}
Write-Host "------------------------------------------------"
Write-Host ("{0} passed, {1} failed, {2} skipped in {3:N1}s" -f $pass, $fail, $skip, $suite.Elapsed.TotalSeconds)

if ($fail -gt 0) {
    Write-Host ""
    foreach ($r in $results | Where-Object { $_.Status -eq 'FAIL' }) {
        Write-Host ("FAILED: {0} (exit {1})" -f $r.Name, $r.Code) -ForegroundColor Red
    }
    exit $fail
}

if ($skip -gt 0) {
    Write-Host "ALL GREEN (with skips - not a full pass)" -ForegroundColor Yellow
    exit 0
}

Write-Host "ALL GREEN" -ForegroundColor Green
exit 0
