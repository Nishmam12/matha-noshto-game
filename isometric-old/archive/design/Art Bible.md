---
tags: [design, art, wayfarer]
updated: 2026-08-04
status: provisional
---

# Art Bible — Wayfarer

> **PROVISIONAL.** This documents what the current *placeholder* art assumes, so the team's real art
> direction can supersede it cleanly rather than colliding with it. Every value here is a default to
> be overridden, not a decision to be defended. When the team's direction lands, replace sections
> 1–7 wholesale and keep section 8 — the asset contract is the part the engine depends on.

Hub: [[Wayfarer MOC]] · Renderer: [[Isometric Rendering]] · Fog contract: [[Fog and Reveal]] ·
Scope caveats: [[Overview]] · UI restraint: [[Save and UI]] · Running log: [[INDEX]]

**Authoring note.** Structure follows the `art-bible` skill's nine sections. Its mandated agent
delegation (`art-director`, `technical-artist`, `ux-designer`, `creative-director`) and its
prerequisite `design/gdd/game-concept.md` do not exist in this project, so those steps were not run
and **no art-director sign-off has been recorded**. That is an open item, not a silent pass.

---

## 1. Visual Identity Statement

> **Separate by value, never by hue.**

This is not a stylistic preference — it is forced by the game's own mechanic. `fog_lerp` is the
single path from true colour to screen colour, and it drains hue toward a neutral until a memory is
restored. **Anything that relies on hue to be legible becomes invisible in the unrestored world**,
which is most of the world most of the time. A tree that is "green against green" reads as a green
blob at reveal 0.0 and a green blob at reveal 1.0.

Resolve any visual ambiguity by asking: *would this still read in greyscale?* If no, it is wrong.

**Supporting principles**

| # | Principle | The test it settles |
|---|---|---|
| P1 | **Silhouette before detail.** Shape carries identity; colour is decoration on top | When choosing between another palette and another silhouette variant, pick the silhouette. This is why there are 8 canopy palettes and 8,192 tree *shapes* |
| P2 | **Warm is alive, cool is drained.** Restoration moves a region warmwards and raises its contrast | When unsure whether an effect belongs to restoration, ask if it adds warmth or contrast. If neither, it is not a restoration cue |
| P3 | **Two saturated colours in the entire game**, and they are reserved | Amber = life, warmth, interactable. Cyan = memory, fragments. Every other surface is desaturated. This restraint is what makes the two accents carry meaning |

---

## 2. Mood & Atmosphere

Five states. Each must be distinguishable from the others **in a still screenshot**.

| State | Emotional target | Light character | Descriptors | Energy |
|---|---|---|---|---|
| **Unrevealed** | Absence, not menace | Flat, no direction, high-key cool haze | distant · fogged · quiet · weightless · unremembered | Still |
| **Sighted** (walked, not restored) | Recognition without warmth | Directional light returns; contrast rises; hue stays suppressed | outlined · legible · grey-green · pending | Low |
| **Restored** | Relief, inhabitation | Full upper-left key light; warm bounce; windows lit | saturated · lived-in · warm · layered · breathing | Measured |
| **At a fragment** | Pull, invitation | Local cyan emission overrides the ambient | luminous · singular · cold-bright | Focused |
| **Complete** | Earned stillness | Everything warm, everything moving slightly | golden · populated · settled | Contemplative |

**The correction this bans.** Unrevealed land currently renders as a *dark* blue-grey (~58,61,69) —
it reads as a cave, or as night, and it is the direct cause of the "suffocating" complaint. Distance
in the real world goes **lighter, bluer and lower-contrast**, never darker. Unrevealed terrain must
read as **aerial perspective**: mist you have not walked into yet, not a wall you cannot see past.

---

## 3. Shape Language

The projection is a fixed 2:1 diamond (`ISO_HW == TILE`, `ISO_HH == TILE/2`), so the world's base
grammar is set. Everything else is chosen against it.

- **Ground is horizontal, life is vertical.** Terraces, shorelines, paths and cliff strata run along
  the diamond axes. Trees, buildings, the player and crystals oppose that with vertical mass. This
  contrast is the primary depth cue — more than shading.
- **Three geometries, kept distinct.** Architecture is **angular** (straight eaves, hard ridge
  lines). Foliage is **round** (overlapping lobes, no straight edges). Rock is **faceted** (flat
  planes meeting at hard angles — neither soft nor manufactured). A player should never mistake the
  class of a thing at 8 px.
- **Player reads at thumbnail.** The silhouette must be identifiable at native 960×540 with no
  colour: a distinct head, a cloak that breaks the body outline asymmetrically, and one accent that
  belongs to nothing else in the world.
- **Hero shapes vs. supporting shapes.** Buildings, the player, crystals and Found Souls are hero
  shapes — they get outlines by value contrast, a cast shadow, and the largest unbroken masses.
  Grass, pebbles, tufts and grain are supporting — they must never out-contrast a hero shape.
- **Everything touches the ground.** Every drawable gets a contact shadow. A sprite without one
  floats, which in an isometric projection is unreadable rather than merely wrong.

---

## 4. Colour System

Four-shade ramps, not single colours. Values are chosen so **adjacent shades differ by ~20–28
luminance** — enough to survive the fog blend, which is the measurable form of §1.

### World ramps

| Ramp | Shadow | Base | Light | Highlight | Role |
|---|---|---|---|---|---|
| **Grass** | `0x2c4429` | `0x3e5c35` | `0x557a45` | `0x6f9455` | Default walkable ground. Sage, not primary green |
| **Foliage** | `0x1e3324` | `0x2b4a2e` | `0x3d663a` | `0x548049` | Canopy. **Deliberately a lower value band than grass** so trees separate from the ground they stand on |
| **Path** | `0x4a3a28` | `0x6b5438` | `0x8c7048` | `0xa89060` | Worn dirt between buildings. Warm — it signals inhabitation |
| **Stone** | `0x33313e` | `0x4a4856` | `0x66646f` | `0x85838d` | Cliffs, terrace faces, rock. Cool grey with a violet cast |
| **Water** | `0x14313f` | `0x1e4a5c` | `0x2f6d7d` | `0x5aa0a8` | Deep teal to a cyan-white crest |
| **Timber** | `0x3a2a1e` | `0x54402c` | `0x71583c` | `0x8f7450` | Trunks, beams, bridges, docks |
| **Plaster** | `0x5c5044` | `0x82735e` | `0xa89478` | `0xc9b694` | Wall material — the lightest large surface in the game |
| **Slate roof** | `0x232836` | `0x333b4f` | `0x4a5468` | `0x687186` | |
| **Terracotta roof** | `0x4a2318` | `0x6d3524` | `0x914a30` | `0xb56746` | |

### The two accents — used nowhere else

| Accent | Colours | Meaning | Rule |
|---|---|---|---|
| **Amber** | `0xffb347` → `0xffca6e` | Life, warmth, interactable | Lit windows, hearths, fireflies, the interact ring. **Never** on terrain |
| **Cyan** | `0x6fd8e8` → `0xa8ecf5` | Memory | Fragments, Found Souls, restoration bursts. The only high-chroma cool in the game |

### Fog destination

Unrevealed land resolves toward a **light cool haze**, not a dark grey. Keep more of the source
luminance than the current `×0.55` and pull toward a *lighter* tint than the current
`(44, 52, 68)` — target roughly `(78, 92, 112)`. Restated as rules the code must satisfy:

1. Unrevealed terrain is **lighter** than the same terrain restored-and-shadowed. Distance recedes by haze.
2. **Relative luminance order is preserved at every reveal level.** A 4-shade canopy stays 4
   distinguishable shades at reveal 0.0. This is what `--fog-test` measures.
3. Contrast compresses toward the haze with distance; it does not collapse to a single value.
4. `fog_lerp` remains the **only** path from true colour to screen colour ([[Fog and Reveal]]).
   Baked sprites route their *palette* through it once per reveal level, never per pixel.

### Accessibility

Ability gating currently reads by terrain colour alone — Wade/Climb/Kindle are blue/tan/purple.
That is a hue-only distinction and fails both §1 and colourblind players. **Backup cue required:**
each gated terrain needs a distinct surface *pattern* (water ripples, ledge strata, dark motes), not
just a distinct hue. Not yet built; tracked here so it is not forgotten.

---

## 5. Character Design Direction

- **Player.** 24×24 collision box (`PLAYER_SIZE`, unchanged — collision must stay scale-invariant),
  drawn taller than wide at roughly 20×34 so it does not read as a cube. Three-value body ramp, a
  cloak in a fourth value, one amber accent. **8-way facing**, 2-frame walk bob, contact shadow.
- **Found Souls.** Silhouette-first, no faces. They are memories, so they read as *implied* figures:
  a lit outline, an idle bob, and cyan emission. Deliberately less resolved than the player.
- **Distinguishing at a glance.** Player = amber accent. Souls = cyan emission. Nothing else in the
  world carries either. This is §1's P3 doing the work of a UI.
- **LOD.** There is one camera distance and one zoom. No LOD system — every asset is authored at the
  distance it will be seen, which is the one advantage of a fixed isometric camera.

---

## 6. Environment Design Language

- **Architecture is subtractive, not additive.** Restoration removes *absences* rather than adding
  ornament: `0.0` walls only and gapped → `0.4` roof partial → `0.7` roof and windows complete →
  `1.0` windows lit and chimney smoking. One asset, four states. See [[Handover]] §11.
- **Texture philosophy: courses, not noise.** Surfaces read through *aligned repetition* — brick
  courses, roof shingle rows, cliff strata, plank lines — because repetition survives the fog blend
  and random noise does not. The current two-tone dither is the weakest thing in the renderer.
- **Prop density by meaning.** Dense at the village edge (the treeline that frames it), sparse on
  open ground (so paths read), near-zero on plateaus reserved for building placement. Density is a
  composition tool, not a realism tool.
- **Environmental storytelling.** Paths worn between buildings say *inhabited* louder than any
  amount of prop detail. Bridges say *someone crossed here*. A ruined house next to a whole one says
  what the game is about without a line of text.
- **Water is a body, not a tile type.** It must have a shoreline, a direction of flow, and a
  destination. Scattered blue tiles read as puddles and cost the world its coherence.

---

## 7. UI Visual Language

**The reference image's ornate HUD is out of scope.** [[Save and UI]] is explicit: *"no HUD clutter;
this is an exploration game, the screen should stay uncluttered."* Hearts, tool slots, portraits,
gilded frames and a minimap were all previously dropped on purpose. Reinstating any of them is a
**flagged decision**, not an art choice.

What is in scope: a 6×8 bitmap font; text with a 1 px dark offset shadow so it stays legible over
any terrain; the amber accent for interactables; the cyan accent for memory events. UI shares the
world's palette rather than introducing a second one — there is no separate UI colour system.

---

## 8. Asset Standards — **the contract the engine depends on**

Keep this section when the rest is superseded.

### Hard rules

1. **No external asset file ever ships.** No PNG, WAV, TTF, OGG, MP3 or GLB beside the exe. Art
   enters through a **build-time bake** into a compiled-in header. See the memory note
   `wayfarer-asset-pipeline`.
2. **No SDL_image, SDL_ttf or SDL_mixer.** Excluded libraries, non-negotiable.
3. `art/` source files are **tracked in git** — they are inputs, not shipped output.

### Pipeline

```
art/<category>_<name>[_<variant>].png  ->  tools/bake.ps1  ->  src/art_data.h  ->  wayfarer.exe
```

`src/art_data.h` is **committed**, so a clean build never requires the tool. Regenerate only when
art changes.

### Sprite format

| Field | Rule |
|---|---|
| Palette | ≤16 entries, indexed, per sprite. Index 0 is **always** transparent |
| Encoding | RLE over the index stream |
| Record | `{ w, h, anchor_x, anchor_y, pal_off, data_off }` |
| **Anchor** | The **ground-contact point** — where the sprite meets the tile it stands on. Bottom-centre for uprights. This is what makes it drop into the band sweep correctly |
| Fog | Applied to the **palette**, once per sprite per reveal level. Never per pixel |
| Budget | ~200–600 bytes per 32×32 sprite against ~750 KB headroom. Report measured cost per asset |

### What stays procedural

Terrain, cliff faces, water and the ground grain are **not** sprites. They must tile seamlessly at
arbitrary elevation and route through fog per tile. Sprites are for **characters, props and
buildings** only.

### Naming

`category_name_variant.png` — lowercase, underscores. Categories: `tree`, `bush`, `rock`, `prop`,
`bld`, `char`, `fx`, `ui`. The bake tool derives the C identifier from the filename, so a rename is
a code change.

---

## 9. Reference Direction

The user's reference image is the primary source. `design/Overview.md` already fixes the framing:
*"a stylised member of the same family, not a reproduction."*

| Source | Take this | Explicitly avoid |
|---|---|---|
| **The supplied reference image** | The *density gradient* — dense treeline framing an open, walkable, path-laced centre. And the water: it defines the island's shape rather than decorating it | Its painted per-leaf detail, its portrait, and its gilded multi-panel HUD. All three are named exclusions in [[Overview]] and [[Save and UI]] |
| **Aerial perspective (landscape painting)** | Distance goes lighter, bluer, lower-contrast. This is the whole fix for §2's "unrevealed" state | Literal atmospheric scattering maths. Two constants and a lerp is the entire model, matching the two-constant face-shading model already in place |
| **Limited-palette pixel art discipline** | Four-shade ramps, hard value steps, no anti-aliasing, no gradients. Readability at 1:1 before beauty at 4:1 | Dithered gradients as a substitute for a real value step. The existing two-tone dither is already the weakest surface treatment in the renderer |
| **Architectural cutaway drawing** | Repetition as texture — courses, rows, strata, planks. Survives the fog blend where noise does not | Technical-illustration flatness. Our surfaces still need the upper-left key light |

**Divergence rule.** Where the reference and the byte/authoring budget conflict, the budget wins on
*detail density* and the reference wins on *composition and palette*. Detail is what costs authoring
time; composition is free.

---

## Open items

- [ ] **No art-director sign-off recorded** — the skill's review gate could not run (§ authoring note).
- [ ] Colourblind backup cues for the three gated terrains (§4) are specified but not built.
- [ ] Every ramp here is unverified in motion. Nobody has played this build by hand — see
      [[Handover]] §8. Screenshot every slice; the last session found two proportion bugs by eye that
      no test caught.
