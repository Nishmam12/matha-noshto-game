Wayfarer — Bug Fix Plan implementation handover
Repo: c:\Users\afnan\Desktop\Projects\Wayfarer · Branch: feat/phase-14-castle-full-set-dev-mode · Base commit: 428c9fd

Working tree is uncommitted. Nothing was staged or pushed.

What changed
All 8 issues in design/Bug Fix Plan.md are implemented. Single translation unit, src/main.c, +259 / −40. Plus a new devlog/2026-08-11-session-01.md and an updated devlog/INDEX.md.

#	Where	Change
1	iso_diamond, iso_diamond_lr	Half-open [i0, i1) loop (i1 = rw + 1, i < i1) matching iso_tile. Was writing past the backbuffer on the last row.
6	gating_selftest, --gating-test dispatch	New int *out_gated out-param; dispatch fails if no seed gated anything.
7	game_load	Validates abilities against ABIL_WADE|ABIL_CLIMB|ABIL_KINDLE; 5th negative control in save_selftest.
8	main(), hud_draw, title bar	Seed is Uint64 end-to-end; formatted %.0f/(double) to avoid a 64-bit integer formatter. New hud_selftest case 1b.
2	audio_selftest	fails counter gates partial writes / NaN / out of range on every invocation, not just --layers.
3	Audio struct, synth_latch, R and F9 handlers	New rng_req / rng_seed_req atomic handoff replacing direct main-thread writes to audio.rng.
4	New CASTLE_WATCHTOWER_X/Y constants	Tower (88,30) → (74,33). Four use sites (build, render dispatch, render sprite id, new land_selftest check) all derive from the constants.
5	place_entities	regions_by_sector hoisted above both loops; grants filtered through over_mask with the same two-tier fallback the entity loop already used.
Build & verify

.\build.ps1 -SelfTest    # 1,100,800 bytes, zero warnings
.\build.ps1              # 1,029,120 bytes (+512), zero warnings, 410,880 under ship target
24 of 25 --*-test flags exit 0. Each fix was proven against a check that fails without it (throwaway build, reverted after): #1 → 8 px past buffer; #6 → gating stubbed to no-op now FAILs; #7 → corrupt save was accepted and mutated live state; #8 → 0 px differ across 2³²; #2 → NaN 190 + clipped 121478 exiting 0; #4 → 9/9 tiles unreserved, 20/20 seeds; #5 → 529 dream-sector grants across 500 seeds.

Three things the next session must handle
1. Open decision — watchtower sits across the forest road. (74,33) makes castle_approach_path_tile(74,33) true, so the tower's 3×3 ring turns (73,33) and (75,33) into rock on the road. --path-test bad tiles go 8 → 10/seed (isolated by rebuilding with only that constant reverted). This is inherent to the plan's own "6 tiles west of the gate on the forest road" brief — at x=74 the shelf spans rows 31–35 only, so a 3×3 tower must centre on 32/33/34 and 33 blocks the fewest. Reachability is unaffected (--reach-test 500 seeds, --play-test, --aether-test all pass). Alternative if the road should stay clear: the wide shelf branch (tx 76–82, local_y 24–36), e.g. (79,25) — reserved, clear of the road, but no longer due west of the gate. Left as specified; needs a call.

2. Two pre-existing failures, confirmed identical at HEAD — not introduced here, not fixed:

--path-test: 21 failures / 20 seeds, ~8 bad path tiles per seed plus a failing negative control.
--land-test --seeds 500: seeds 85, 417, 430 — player reaches 22–23% of open tiles vs a 50% bar. The default 20-seed run passes, which is why this went unnoticed.
Get both green before trusting the suite to catch a new regression.

3. Gaps in verification — do not assume these were checked:

ThreadSanitizer is unavailable in this toolchain (ld: cannot find -ltsan). #3 rests on a stress run (~6000 reseeds in 6 s against a live device under --noise/--sfx/--layers; no crash, no partial writes, no NaN, callback under 2% of deadline) plus the structural argument. Not machine-proven race-free.
The plan's --audio-test teeth check does not work as written. audio_cb hard-clamps at src/main.c:749-750 before the capture reads out, so spiking TONE_AMP above 1.0 leaves out of range at 0. Proven instead by removing the clamp and injecting a NaN. Consequence: the clipped counter guards that the clamp still works, not general amplitude safety.
#4's river-overwrite scenario is not reproducible. No seed in 1..500 routes a river through the old (88,30) — it sits in the ocean gap (approach caps at x≤82, castle_left[28]=102). Asserted at the seed-independent root instead: the tile isn't inside is_castle_reserved.
The watchtower sprite was never seen on screen. Correct by construction (one constant, four sites), but no screenshot taken.