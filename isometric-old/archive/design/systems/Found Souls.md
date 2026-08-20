---
tags: [system, wayfarer]
---

# Found Souls

Part of [[Overview]]. Mechanically a subtype of [[Fragments]] — reuses that restoration path rather than being a separate system. "NPC" in earlier planning notes; confirmed in-game name is **Found Souls**.

## Confirmed state naming
**Lost → Found → Remembered.** Maps directly onto the existing two-stage design:
- **Lost** — undiscovered, not yet placed in view
- **Found** — a translucent grey silhouette (procedurally drawn, no sprite) placed like a landmark, discovered but not yet restored
- **Remembered** — restored: gains color, plays **one** procedurally-selected line of text from a small pool keyed to region/ability context, and adds the **Voice of Souls** layer to the audio mix — see [[Audio and Synth]]

Idle sway/breathe only. **No pathing, no schedule, no AI.**

## Count — reconciled against the mockup
Recommend **4–6 Found Souls**, tracked separately from the fragment count — see [[Fragments]]. Formal sign-off in [[Open Decisions]].

## Cap
Single-line restoration text at Remembered. Don't add a third state or branching text without re-justifying the time cost — see [[Cut List]].

## Deliberately excluded, and why
This project explicitly rejected the full ECHOS-style NPC system (trait schemas, friendship meters, marriage, dialogue trees keyed to weather/time/festival) because it doesn't serve the exploration loop and costs real weeks:
- **Schedules/pathing AI** — real time cost for movement logic, collision, edge cases, zero exploration payoff.
- **Branching dialogue** — needs a dialogue-state system and writing volume; a single restoration line gets the same emotional beat for a fraction of the work.
- **Procedural personality generation** — system-building for flavor text nobody reads twice in a jam-length game.
- **Relationships between Found Souls** — no social graph to simulate.

Net cost of this scoped-down version: roughly 3–5 days, slotting into Week 3 alongside regular fragment work, not its own phase.
