---
tags: [system, wayfarer]
---

# Fragments

Part of [[Overview]]. Placed via [[World Generation]]; can grant [[Abilities]]; extended by [[Found Souls]]; drives layering in [[Audio and Synth]].

## Structure
A fragment is: position, visual (glow, generated not sprited), restoration state, optional ability grant, optional 1-line text (used by the Found Souls subtype).

## Restoration interaction
Player enters proximity, presses interact, a short (non-puzzle) confirm beat plays, state flips to restored.

## On restore
- Region restoration% updates — see [[Fog and Reveal]]
- One audio layer activates — see [[Audio and Synth]]
- Optional ability flag set — see [[Abilities]]

## Count — reconciled against the mockup
Mockup HUD showed a combined 23/40 counter. **Default: ~12–16 fragments, tracked separately from Found Souls** (see [[Found Souls]] for its own count) rather than one combined number — simpler to implement and debug, and keeps the reachability-testing surface manageable. Formal sign-off in [[Open Decisions]].

## Win condition
Full restoration (all fragments + all Found Souls) maps to the "Fully Restored" world-growth stage — see [[World Generation]] and [[Overview]].
