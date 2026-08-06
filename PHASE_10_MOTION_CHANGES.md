# Phase 10: Motion — Changes & Additions Summary

## Overview
Phase 10 introduces ambient motion across the world of **Wayfarer**, transforming static landscapes into dynamic, living environments. All motion effects are implemented as pure functions of `(tile_hash, Game.clock)` evaluated at render time. They add **zero simulation state**, touch **zero RNG streams**, and preserve 100% determinism.

---

## What Was Added & Changed

### 1. Visual Motion Effects (`src/main.c`)
- **Tree Sway (`tree_sway`)**:
  - Unified sway offset shared between baked tree sprites and procedural tree fallbacks (`draw_tree`).
  - Per-instance phase offset derived from `tile_hash` bits 27–31 (±1–2 px displacement).
  - Multi-lobe phase rippling (`i * 0.55` phase offset per lobe) to simulate breathing crowns.
- **Water Shimmer (`water_ripple`)**:
  - Dynamic brightness shimmer (±5 px) across river and ocean surface tiles.
  - Constrained within single depth-ramp steps to prevent depth-band clipping.
  - Excluded from debug overlay views (`--overlay`) and dream void starfields.
- **Waterfall Fall-Lines (`waterfall_dash`)**:
  - Animated pale 1-px streak scrolling down terraced waterfall side-faces (`iso_tile`).
  - Uses `fmod`-free wrap arithmetic to avoid floating-point math libraries.
- **Chimney Smoke (`draw_smoke`)**:
  - Rebuilding structures (restoration 0.7–1.0) emit 1 rising puff of procedural smoke.
  - Fully restored structures (1.0) emit 2 rising smoke puffs over the chimney sprite.
- **Fireflies / Motes (`mote_gate`)**:
  - Sparse ambient fireflies on grass tiles (1 in 8 tiles, non-path, non-footprint).
  - Fade-in threshold gated at restoration ≥ 70%, reaching max intensity at 100% restoration.
- **Found Souls Idle Bob (`soul_bob`)**:
  - Gentle vertical bobbing (±2 px) for idle Found Souls, synchronized with their prompt icons (`draw_prompt`).
  - Memory fragments remain static to differentiate them from active souls.

### 2. Automated Test Suite (`--motion-test`)
- **New Test Harness**:
  - **Determinism Check**: Ensures render consistency at identical clocks.
  - **Live-Diff Validation**: Verifies 23,552 px change across time without simulation drift.
  - **Helper Bounds**: Validates tree sway (≤ 1 px), soul bob (≤ 2 px), and waterfall dash face constraints.
  - **Firefly Gate Truth Table**: 8/8 test cases validating restoration-gated visibility.

### 3. Documentation & Roadmap Updates
- **`Handover.md`**: Updated agent log, status tables, and binary metrics for post-Phase 10 state.
- **`design/phases/Phase 10 - Motion.md`**: Documented completed DoD items, technical design, and test results.
- **`design/phases/Phase Roadmap.md`**: Marked Phase 10 as complete.
- **`devlog/2026-08-06-session-05.md`**: Created session devlog recording Phase 10 implementation & verification.
- **`devlog/INDEX.md`**: Added Session 05 entry to the master devlog index.

---

## Build & Headroom Metrics
- **Executable Size**: `781,824 bytes` (+2,048 bytes from merged Phase 08+09 baseline).
- **Headroom**: `658,176 bytes` remaining under the 1,440,000 byte ship target.
- **Performance**: Render mean **1.123 ms** (max 1.926 ms) against 16.67 ms budget (~15× headroom).
- **Test Suite Status**: All 20+ automated test harnesses **PASS**.

---

## File Summary
| File | Status | Description |
|---|---|---|
| `src/main.c` | **Modified** | Implemented `tree_sway`, `water_ripple`, `waterfall_dash`, `draw_smoke`, `mote_gate`, `soul_bob`, and `--motion-test`. |
| `Handover.md` | **Modified** | Updated handover state, agent log, and roadmap references. |
| `design/phases/Phase 10 - Motion.md` | **Modified** | Updated Phase 10 specification with completed tasks and DoD status. |
| `design/phases/Phase Roadmap.md` | **Modified** | Updated Phase 10 status to DONE. |
| `devlog/2026-08-06-session-05.md` | **New** | Session 05 devlog detailing Phase 10 work. |
| `devlog/INDEX.md` | **Modified** | Added Session 05 entry. |
| `PHASE_10_MOTION_CHANGES.md` | **New** | Complete overview of Phase 10 additions and modifications. |
