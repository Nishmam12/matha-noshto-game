# Wayfarer — Progression Handover

> Updated 2026-09-04 following the successful integration and merge of `progression-plan-implementation`
> and `handover-update`. Both the progression overhaul (return travel, partial threshold, castle key,
> gate art transitions) and the Area 4 Dungeon expansion (King audience, beast transformation, god mode)
> are fully unified and passing all 21 test suites.

## Status

Both the progression overhaul and the Dungeon (Area 4) expansion are now live and merged:
- **Return portals**: Area 2 (Lumiara) $\to$ Area 1 (Mainland).
- **Threshold gating**: Area 1 portal opens at 4 memories + all 3 souls (`PORTAL_MEMORY_THRESHOLD`).
- **Castle key**: Fully restoring Area 2 grants the castle key; returning to Area 1's portal now routes directly to Area 3 (Castle).
- **Dungeon (Area 4)**: Completing Area 3 opens the way down to Area 4 (Dungeon). Area 4 is terminal and contains the King's audience and beast confrontation.
- **Silent gate refusal**: Resolved. Pressing E near a closed portal fires `SFX_DENY` and displays contextual feedback ("the way between is still closed" / "the gate does not know you yet").
- **God mode (`Ctrl+G`)**: Allows skipping gate requirements for testing, marking runs as cheated so corrupted saves are refused.
- **Save/load integrity**: Save v4 fully supports all 4 areas, preserves all maps, flags, and abilities, and passes all 17 negative integrity controls.

## Current topology

| Area | Biome | Portal leads to | Gate condition |
|---|---|---|---|
| 1 Mainland | Forest | Area 2 (Lumiara) | `portal_open(g)`: 4 memories + 3 souls |
| 1 Mainland | Forest | Area 3 (Castle) | `castle_key(g)`: Area 2 fully restored (takes priority) |
| 2 Lumiara | Lumiara | Area 1 (Mainland) | Always (return travel) |
| 3 Underworld/Castle | Underworld | Area 4 (Dungeon) | `area_complete(g)`: 7 memories + 3 souls |
| 4 Dungeon | Dungeon | — terminal | King audience gated on Area 3 completion |

## Gate refusal feedback

## Gate refusal feedback

Pressing E while standing in reach of a portal that isn't open yet provides audio and visual feedback:
`sfx_fire(a, SFX_DENY)` and `hud_toast(g->area == 1 ? "the way between is still closed" : "the gate does not know you yet")`, returning 0 on refusal as required by the negative controls.

Reach is checked first before destination validity, preventing spurious toasts when interacting away from the gate.

## Verification

The entire 21-suite test harness and size gate passes cleanly:
```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run-tests.ps1     # 21 suites + the size gate
```
Both shipping binary (`wayfarer.exe`, 879,104 bytes) and self-test binary (`wayfarer-selftest.exe`, 1,006,592 bytes) compile with zero warnings under `-Wall -Wextra -Werror`.
