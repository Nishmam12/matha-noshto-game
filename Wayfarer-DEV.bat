@echo off
REM Wayfarer DEV MODE shortcut — unlocks all progression gates
REM Launches the shipping build with --dev so the Aetherhold causeway,
REM all abilities (WADE/CLIMB/KINDLE), shard requirement and map reveal
REM are open from the start. Same binary, no separate build.
REM In-game: press F12 at any time to trigger the same unlock.
setlocal
set "DIR=%~dp0"
start "" "%DIR%wayfarer.exe" --dev %*
