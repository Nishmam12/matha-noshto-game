---
tags: [system, wayfarer]
---

# Save and UI

Part of [[Overview]]. Reads seed state from [[World Generation]] and restored-ID state from [[Fragments]] and [[Found Souls]].

## Save system
Store only: seed, restored-fragment-ID list, restored-Found-Soul-ID list, ability flags. Regenerate everything else from seed on load. Optional: display the seed as a short shareable code (e.g. `8124_07A3`) — cosmetic only, doesn't change the underlying PRNG seed value.

## Font
Hand-rolled bitmap font: encode a 5×7 glyph as a `uint64_t` bitfield, 96 printable ASCII characters = 768 bytes as a static array. No SDL_ttf — see [[Agent Prompt]] for the excluded-libraries rule.

## HUD legend (confirmed, with two deliberate exclusions)
Minimap markers: **You**, **Restored Region**, **Unrestored Region**, **Found Soul**, **Fragment**. Two counters (fragments restored / total, Found Souls remembered / total) shown separately rather than combined — see [[Fragments]].

**Excluded from the HUD:**
- **Hearts/health** — no combat exists in this design; nothing damages the player, so there's nothing for a heart icon to track. Dropped rather than built as dead UI.
- **Tool/inventory icons** — no crafting or tools system is part of the approved design. If the team wants one later, that's new scope to evaluate explicitly, not something to build from the mockup's decoration — see [[Open Decisions]].

Keep the UI minimal beyond this — no HUD clutter; this is an exploration game, the screen should stay uncluttered.
