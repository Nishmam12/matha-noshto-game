---
tags: [design, wayfarer]
---

# Open Decisions

Resolve before/during Week 1–2. See [[Overview]] for the timeline these block.

- [x] ~~Grid-based vs. continuous movement/collision~~ — **RESOLVED 2026-08-02: continuous.**
  Float position, swept AABB resolved per-axis against a tile grid, on a fixed 60 Hz simulation
  step. Chosen because the core hook is a smooth fog-to-color reveal that follows the player, and
  tile-stepping made that reveal read chunky. Ability gating is still per-tile, so nothing in
  [[Abilities]] is affected. Implemented and verified in [[2026-08-02-session-01]].
- [ ] Exact landmass size (region count) — start small, expand only if generation + pacing tests support it. See [[World Generation]].
- [ ] **Final fragment + Found Soul counts** — default proposed is ~12–16 fragments + 4–6 Found Souls (tracked separately), reconciled down from the mockup's combined 23/40. Needs explicit team sign-off, not just the default. See [[Fragments]], [[Found Souls]].
- [ ] Whether Kindle (dark-region light) uses passive reveal-radius or an active "ping" — active ping is more distinctive but adds input/feedback design work. See [[Abilities]].
- [ ] **Inventory-bar tool icons from the mockup** — currently treated as unconfirmed pitch-art decoration, not built. If the team actually wants a tools/crafting system, that's new scope requiring its own discussion, not something to infer from the mockup. See [[Save and UI]].
