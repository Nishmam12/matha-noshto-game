---
tags: [design, wayfarer]
---

# Overview — Wayfarer: A Memory Restoration Journey

See [[Wayfarer MOC]] for the project hub, [[Agent Prompt]] for how we work. **Status: game plan approved by the team.**

## Core concept
A single procedurally generated landmass, shrouded in fog and drained of color. The player explores, finds [[Fragments|memory fragments]] and [[Found Souls]], and restores them. Each restoration:
- Permanently clears fog / returns color to that region — see [[Fog and Reveal]]
- Adds one named layer to the live ambient synth mix — see [[Audio and Synth]]
- Sometimes grants a remembered [[Abilities|ability]] (Wade, Climb, Kindle) that gates new terrain — see [[World Generation]]

No combat, no dialogue trees, no farming. The loop is: **see something you can't reach → explore around it → find the memory that lets you reach it → the world audibly and visibly grows.**

## The four pillars (confirmed naming)
- **EXPLORE** — uncover the unknown, shrouded in fog
- **RESTORE** — find memory fragments and restore them
- **AWAKEN** — return color, music, and life to the world
- **REMEMBER** — find lost souls, give them their memories back

## Core gameplay loop
```
Explore fog-shrouded terrain
    ↓
Find a memory fragment or a Found Soul (visual glow / audio cue)
    ↓
Restore it — a small interaction, not a puzzle-minigame
    ↓
Region permanently changes: fog clears, color returns, one synth layer joins the ambient track
    ↓
Restored memory sometimes = a recovered ability
    ↓
New ability opens terrain that was previously unreachable
    ↓
Explore further
```

## World growth framing (confirmed 4-stage read)
Used for the minimap/debug overlay in [[World Generation]] and [[Fog and Reveal]], as a coarse aggregate-restoration% readout on top of the per-region float:
1. **Unexplored** — nothing found
2. **Partly Revealed** — some regions restored
3. **Many Memories Restored** — most regions restored
4. **Fully Restored** — completion state, see win condition in [[Fragments]]

## On the mockup's art style — important scope note
The pitch mockup (painted vistas, atmospheric lighting, detailed character portraits) is **pitch/branding art, not an in-engine target.** Our architecture has zero external image assets — no PNGs, no sprite sheets, no SDL_image (see [[Agent Prompt]]). What renders in-engine is flat-shaded procedural geometry: simple shapes, a fog-to-color blend, generated silhouettes for Found Souls. This is a deliberate, load-bearing constraint, not a shortfall — make sure the whole team knows the shipped game will look markedly simpler than the mockup, on purpose.

> **Superseded in part, 2026-08-04.** The team asked for an isometric 2.5D pixel-art look closer
> to the mockup, and it was built — see [[Isometric Rendering]] and [[2026-08-04-session-01]].
>
> **What changed:** the renderer is no longer flat top-down squares. It is a 2:1 isometric
> projection at 32 px tiles with elevation and cliff faces, per-tile surface detail, layered
> procedural trees, and mix-and-match procedural houses.
>
> **What did NOT change, and is still load-bearing:** zero external asset files. Every one of
> those visuals is hand-written C that draws rectangles. There are still no PNGs, no sprite
> sheets, and no SDL_image. The trees are 8 palettes × shape jitter, not 8,192 drawings; the
> houses are 9 mix-and-match parts, not 640,000 drawings.
>
> **What is still true about the mockup:** its *density of unique painted detail* is not
> reachable and was never the target. No per-leaf shading, no character portrait, no gilded UI
> frames. The shipped game is a stylised member of the same family, not a reproduction. Say so
> to the team rather than letting the screenshots imply otherwise.
>
> The size argument that justified the original constraint turned out to be the weakest part of
> it: the entire isometric renderer cost about 10 KB against 750 KB of headroom. The real
> constraint was always that every visual has to be *written*, not that it has to be *small*.

## HUD / legend (confirmed, with one deliberate change)
- **You** (player marker), **Restored Region**, **Unrestored Region**, **Found Soul**, **Fragment** — kept from the mockup, maps directly onto [[Save and UI]]
- **Hearts/health icons — dropped.** The mockup shows hearts, but we have no combat and nothing that damages the player. Kept in only as a leftover from the pitch template; excluding them removes a whole (fake) system before it gets built.
- **Inventory-bar tool icons — not adopted.** The mockup shows pickaxe/axe/hammer-style icons. Nothing in the approved design calls for crafting or tools. Treating these as unconfirmed pitch-art decoration unless the team explicitly asks for a tools system — see [[Open Decisions]].

## Fragment / Found Soul count — flagged, not copied
The mockup HUD shows a combined counter reading **23/40**. That's roughly 3x the scope-safe default (low teens fragments, 4–6 Found Souls) established earlier, and 40 tracked entities means 40x the reachability-testing surface across seeds. **Default until the team says otherwise: ~12–16 fragments + 4–6 Found Souls**, tracked as two separate counts rather than one combined number (simpler to implement, simpler to debug). See [[Open Decisions]] to formally settle this.

## System map
- [[World Generation]] — landmass, region graph, reachability invariant, world-growth stages
- [[Fog and Reveal]] — the core visual hook
- [[Abilities]] — Wade / Climb / Kindle, terrain gating
- [[Fragments]] — placement + restoration interaction
- [[Found Souls]] — Lost → Found → Remembered subtype of fragments
- [[Audio and Synth]] — softsynth, named layers (Base / Strings / Pad / Bells / Voice of Souls), SFX
- [[Save and UI]] — save/seed system, bitmap font, minimal HUD

## Timeline (5 weeks + buffer, working back from Sept 4, 2026)
- **Week 1** — build pipeline, PRNG, movement, first-pass fog reveal over a placeholder area. Milestone: does the reveal feel good with zero other content?
- **Week 2** — [[World Generation]] (region graph, terrain types, reachability invariant, batch seed testing)
- **Week 3** — [[Fragments]] + [[Found Souls]] + restoration loop wired to real ability flags
- **Week 4** — [[Audio and Synth]] (softsynth, pattern data, layering hook, SFX, callback profiling)
- **Week 5** — [[Save and UI]], win/completion state, full playthroughs, game feel pass
- **Buffer week** — run [[QA Checklist]] in full, final size audit, submit early

## Judging-criteria alignment
| Criterion | How this design serves it |
|---|---|
| Finished | Small, closed system set; no open-ended content systems (farming, relationships) to run out of time on |
| Under size | Zero external assets; procedural everything; C + static SDL2 — see [[Agent Prompt]] |
| Fun | Exploration paced by ability-gating; audio/visual reward tightly coupled to progress |
