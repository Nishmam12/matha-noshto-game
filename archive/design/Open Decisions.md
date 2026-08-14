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
  **Provisional as of 2026-08-02: 16 regions on an 80×45 tile world.** Chosen as the small end
  per the instruction above, not as a decision. Measured across 30 seeds: 16 regions every time,
  graph depth 3–6, 562–1655 walkable tiles. Raising it is cheap (one constant) but `REGION_COUNT`
  **cannot exceed 32** — adjacency and reachability are `Uint32` bitmasks. Needs a pacing test
  once restoration exists before being made final. See [[2026-08-02-session-01]].
  **First pacing number, 2026-08-02:** a shortest-path full clear of all 19 entities takes
  **30–82 seconds of walking** (mean ~52 s), measured over 50 seeds by autopilot. Real play with
  fog will be longer by an unknown multiplier, but if the team wants a longer game this is the
  evidence for raising region count or world size. See [[2026-08-02-session-03]].
- [ ] **Final fragment + Found Soul counts** — default proposed is ~12–16 fragments + 4–6 Found Souls (tracked separately), reconciled down from the mockup's combined 23/40. Needs explicit team sign-off, not just the default. See [[Fragments]], [[Found Souls]].
  **Built to the mid-range default 2026-08-02: 14 fragments + 5 Found Souls**, tracked separately.
  Both are single constants; changing them is trivial, but the combined total **must stay ≤ 32**
  (the reachability simulation uses a `Uint32` restored-mask). Still awaiting sign-off — this is
  the default standing in, not a decision.
- [ ] Whether Kindle (dark-region light) uses passive reveal-radius or an active "ping" — active ping is more distinctive but adds input/feedback design work. See [[Abilities]].
- [ ] **Inventory-bar tool icons from the mockup** — currently treated as unconfirmed pitch-art decoration, not built. If the team actually wants a tools/crafting system, that's new scope requiring its own discussion, not something to infer from the mockup. See [[Save and UI]].
