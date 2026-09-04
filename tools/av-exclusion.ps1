# Teach Windows Defender that our own build output is not malware.
#
# WHY THIS EXISTS
# ---------------
# A freshly linked build\wayfarer.exe is detected as Trojan:Win32/Wacatac.B!ml
# and quarantined, usually a minute or two after the link finishes. The symptom
# is not an antivirus warning - it is the build silently disappearing:
#
#   * `ld.exe: cannot open output file ...\wayfarer.exe: Permission denied`
#     when the scanner holds the file open mid-link, and
#   * `run-tests.ps1` failing at its LAST step with
#     `Get-Item : Cannot find path ...\build\wayfarer.exe` - twenty green
#     suites and then a missing binary, which reads like a broken test harness
#     rather than like an antivirus.
#
# The `!ml` suffix means this is a machine-learning verdict, not a signature
# match. It is a false positive, and it was measured rather than assumed:
#
#   statically linked SDL2 hello-world      669,184 bytes   CLEAN
#   the same plus the whole 138 KB art blob 807,424 bytes   CLEAN
#   the shipping game                       877,056 bytes   DETECTED
#
# So it is neither the toolchain, nor static linking, nor the packed art - it
# is the game's own code. These were tried and did NOT clear it, so do not
# reach for them again: adding a VERSIONINFO resource, keeping the symbol
# table (`-s` removed - which also puts the binary at 1,459,442 bytes, over
# the 1,440,000 ship target), and dropping --dynamicbase/--nxcompat/
# --high-entropy-va. All five variants were still detected.
#
# WHAT THIS DOES
# --------------
# Adds a Defender exclusion for the two build outputs BY FULL PATH - not the
# folder, not the repo, not the drive. A path exclusion keeps working when the
# file is rebuilt, so this is a one-off. Narrow on purpose: everything else
# under build\ (saves, screenshots, game.zip) stays scanned.
#
# THIS IS A LOCAL WORKAROUND, NOT THE FIX. It silences the detection on THIS
# machine; a player who downloads the game still gets it. The actual fix is to
# report the false positive to Microsoft - see -Report.
#
# Usage:  (in an ELEVATED PowerShell - Defender exclusions require admin)
#         powershell -ExecutionPolicy Bypass -File .\tools\av-exclusion.ps1
#         ...\av-exclusion.ps1 -Remove    undo it
#         ...\av-exclusion.ps1 -Report    print what Microsoft's form needs
param([switch]$Remove, [switch]$Report)

$ErrorActionPreference = 'Stop'
$ROOT  = Split-Path $PSScriptRoot -Parent
$PATHS = @(
    (Join-Path $ROOT 'build\wayfarer.exe'),
    (Join-Path $ROOT 'build\wayfarer-selftest.exe')
)

if ($Report) {
    Write-Host "Submit at: https://www.microsoft.com/en-us/wdsi/filesubmission" -ForegroundColor Cyan
    Write-Host "  Choose 'Software developer', then 'Incorrectly detected as malware'."
    Write-Host ""
    $st = Get-MpComputerStatus
    Write-Host ("Engine    : {0}" -f $st.AMEngineVersion)
    Write-Host ("Signatures: {0}" -f $st.AntivirusSignatureVersion)
    Write-Host ("Product   : {0}" -f $st.AMProductVersion)
    Write-Host ("Detection : Trojan:Win32/Wacatac.B!ml")
    foreach ($p in $PATHS) {
        if (Test-Path $p) {
            $h = (Get-FileHash $p -Algorithm SHA256).Hash
            Write-Host ("{0,-22} {1} bytes  SHA256 {2}" -f (Split-Path $p -Leaf), (Get-Item $p).Length, $h)
        } else {
            Write-Host ("{0,-22} not built (or already quarantined)" -f (Split-Path $p -Leaf)) -ForegroundColor Yellow
        }
    }
    exit 0
}

# Checked up front rather than left to Add-MpPreference's own error, which is
# the unhelpful "Access denied" and does not say what to do about it.
$isAdmin = ([Security.Principal.WindowsPrincipal] `
            [Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "Defender exclusions require an elevated shell." -ForegroundColor Red
    Write-Host "Right-click PowerShell -> Run as administrator, then:" -ForegroundColor Red
    Write-Host ("  powershell -ExecutionPolicy Bypass -File `"{0}`"" -f $PSCommandPath)
    exit 1
}

foreach ($p in $PATHS) {
    if ($Remove) {
        Remove-MpPreference -ExclusionPath $p
        Write-Host ("removed exclusion  {0}" -f $p) -ForegroundColor Yellow
    } else {
        # Idempotent: Add-MpPreference on an existing entry is a no-op, but say
        # which it was, so running this twice does not read as having done
        # something twice.
        $have = @((Get-MpPreference).ExclusionPath) -contains $p
        Add-MpPreference -ExclusionPath $p
        if ($have) { Write-Host ("already excluded   {0}" -f $p) }
        else       { Write-Host ("excluded           {0}" -f $p) -ForegroundColor Green }
    }
}

if (-not $Remove) {
    Write-Host ""
    Write-Host "This machine will stop quarantining the build. Players will not -" -ForegroundColor Cyan
    Write-Host "report the false positive too:  .\tools\av-exclusion.ps1 -Report" -ForegroundColor Cyan
}
