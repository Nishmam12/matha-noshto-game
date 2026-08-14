---
tags: [system, wayfarer]
---

# Abilities

Part of [[Overview]]. Gates terrain in [[World Generation]]; granted by [[Fragments]].

## The set (confirmed naming and flavor text)
Exactly three:
- **Wade** — "Cross shallow waters." Gates shallow-water terrain.
- **Climb** — "Climb up short ledges." Gates short-ledge terrain.
- **Kindle** — "Light the dark and reveal hidden paths." Gates dark regions.

## Mechanics
- Each is a boolean flag on player state, granted by restoring a specific tagged fragment.
- Terrain tiles/regions carry a required-ability tag; movement code checks the tag against player flags. That's the entire ability system — no upgrade trees, no leveling.

## Open question
Whether Kindle uses a passive reveal-radius or an active "ping" (sonar-style) is unresolved — see [[Open Decisions]]. Active ping is more distinctive but adds input/feedback design work.
