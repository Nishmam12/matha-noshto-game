---
tags: [design, system, wayfarer]
updated: 2026-08-04
---

# Isometric Rendering

Hub: [[Wayfarer MOC]] · Constraints: [[Agent Prompt]] · Build: [[Toolchain Setup]] ·
Visual hook: [[Fog and Reveal]] · Session: [[2026-08-04-session-01]]

The renderer. Replaces the flat top-down coloured squares described in [[Overview]], which was a
concession to the asset ban rather than a target anyone wanted.

**Decision, 2026-08-04.** [[Agent Prompt]] names "switching rendering approach" as *the* example
requiring a flagged decision rather than autonomous action. It was raised, three options were put
(wilderness-only, structures-on-restore, full village rewrite), and the largest was chosen.

---

## The projection

Choose `ISO_HW == TILE` and `ISO_HH == TILE/2`. The transform then collapses to

```
sx = wx - wy + ISO_OX
sy = (wx + wy) / 2 + ISO_OY
```

— one subtract and one halve, no multiplies, no matrix, exact on integers. **The identity depends
only on that ratio, so it holds at any tile size.** That is why raising `TILE` from 16 to 32 cost
nothing here, and why changing it again would cost nothing either.

`ISO_OX` pushes the map right by the world's height in pixels, because `wx - wy` is negative over
half the world. `ISO_OY` leaves `ELEV_MAX` of headroom above the north rim for raised tiles.

World footprint is a diamond inside a 4000×2000 box. At the east and west corners the visible
content is a thin wedge and the rest is void — that is the expected isometric read, not a bug, and
`world_gen`'s forced-solid border ring gives it a rock rim.

## The rasteriser — per-column spans, not scanline diamonds

For column `i` of `DIA_W`, with `a = |i - ISO_HW|`: the top face starts at `a>>1` and runs
`DIA_H - a`.

That is the **exact preimage of the tile under the inverse projection**, not a slope walk, which
is what makes the tiling provably gap-free rather than hopefully gap-free. For the east neighbour
`a_A + a_B = DIA_W/2` exactly, and requiring A's bottom edge to meet B's top reduces to an identity
that holds for both parities of `a`. Same for the south neighbour. Total per diamond is
`DIA_W × DIA_H / 2`, which is what a diamond must be.

**Elevation then costs nothing extra.** Raising a tile by `h` leaves a hole exactly `h` tall
starting exactly where the top face ended, so the side face is *the same column immediately below*.
There is no second edge computation that could disagree with the first. Where a neighbour is
higher, its own side face seals the join from the other direction — the seal is mutual, and no
configuration needs back-face code.

**Why not a scanline diamond:** it needs a separate parallelogram routine whose upper edge must
agree to the pixel with the diamond's lower edge. That is precisely the seam bug class. This
choice was made for correctness, not speed.

## Elevation — derived, never collidable

`Sint8 height[][]` on `World`, from a two-pass chamfer distance transform into the solid masses.
Rock terraces one `ELEV_STEP` per ring of distance from open ground, so cliff edges get a rounded
rim and isolated pillars stay short. Water sinks, `TERRAIN_LEDGE` rises — which makes the Climb
gate legible before you have Climb.

**Load-bearing invariant: this is render-only.** `tile_blocked` reads `solid` and
`regions[].terrain` and nothing else, and `world_heights` runs last in `game_init`. Nothing in
movement, reachability, gating or the playthrough walker can observe it. The moment collision reads
it, the 50-seed completability proof has to be re-argued rather than merely re-run.

## Lighting

Two constants. The down-left face at `FACE_L`% of true colour, the down-right at `FACE_R`%. No
normals, no dot products. That is the entire model, and it is what turns flat colour into volume.

## Depth order

Back-to-front by diagonal band (`tx + ty`), two sub-passes per band:

1. ground for the whole band
2. props and buildings for the whole band
3. entities in that band
4. the player, if it is the player's band

The sub-passes must be separate because a prop is tall enough to spill onto the diamond of the tile
to its *right*, which is in the same band and whose ground would otherwise repaint over it.

Result: a tree one tile in front occludes the player, one tile behind does not. **No z-buffer
anywhere.** `prop_at` additionally refuses to stand a tall prop where the tile in front is much
higher — the one artefact a band sweep cannot fix, and three lines beat 230 KB of z-buffer.

## Decoration — the per-tile hash

`tile_hash(seed, tx, ty)`, a stateless SplitMix64 finaliser. **Deliberately not drawn from the
terrain or entity RNG streams**, so decoration cannot perturb a single fragment placement and every
seed-based test result stays valid *by construction* rather than by tracing the call graph. Stable
regardless of draw order or culling, so nothing crawls as the camera moves.

Everything visual is combinatorial rather than drawn:

| | |
|---|---|
| Trees | 8 canopy palettes × 4 trunk × 4 heights × 4 widths × 4 depths × 4 leans = **8,192** |
| Houses | 9 parts at 4–5 variations each = **640,000**, before footprint and storeys |

The eye reads silhouette before colour, which is why 8 palettes is enough and 40 would be waste.

## Buildings

Walls are not drawn by anything. Footprint tiles are solid, `world_heights` gives them a wall
height instead of a rock height, and the tile rasteriser draws their front faces — which *is* the
wall. Only roofs and fittings needed new code.

Roofs are stacked shrinking diamonds, drawn **once** from the `Building` record at the footprint's
front-most corner (per-tile would tear along every internal edge). In a 2:1 projection a 45° roof
over half-width `rw` rises exactly `rw/2` on screen — deriving the rise from `rw` rather than using
a fixed pixel step is what stopped every house reading as an open-topped box.

Placement runs **before** the flood fill and the reachability verifier, so buildings genuinely
change what is walkable and a layout that walls something off is regenerated. The guarantee is used
as the safety net rather than worked around.

## Resolution

Rasterise at `LOGICAL_W × LOGICAL_H`, integer-scale into the window. The doubling is the point: a
1 px trunk highlight is a hairline at native 1080p and a visible 2 px band when drawn at 960×540
and scaled. Scale is chosen at startup from SDL's *usable* display bounds (which exclude the
taskbar) — a 1920×1080 window does not fit a 1920×1080 desktop once chrome is counted. `--scale N`
overrides; **F11** toggles borderless fullscreen. `blit_scale` centres and clears the margin,
because a fullscreen window is rarely an exact multiple of the logical size.

## What this cost

About 10 KB of executable against 750 KB of headroom. **The size constraint was never the binding
one** — the binding constraint is that every visual has to be *written*, in C, by hand.

## Verification

`--iso-test`, headless, four cases plus a negative control:

1. flat ground: `covered == composite == N × 1024` — gaps *and* overdraw both fail it
2. random elevation: zero interior-column gap pixels (no seams)
3. random elevation: zero pixels owned by anything but the nearest covering tile (depth order)
4. the integer upscale at ×1/×2/×3 into a non-multiple destination, margins cleared

The negative control injects a 1 px vertical offset into half the tiles — the smallest error the
exactness claim forbids — and confirms it is rejected. See [[Handover]]: a verifier that never
rejects anything proves nothing.

## Not verified

- **Whether any of it looks or feels good in motion.** All of the above is geometry and byte
  counts. Nobody has played it.
- Whether `fog_lerp` keeps four canopy shades distinguishable at low reveal. Checked by eye at a
  few levels, not measured.
- Perf is measured on one machine with the window unoccluded.
