---
tags: [design, phase, wayfarer]
phase: 11
status: planned
updated: 2026-08-04
---

# Phase 11 — Ship Critical

**Status:** Planned. **Begins 2026-08-14 regardless of the state of Phases 03–10.**
**Depends on:** [[Phase 03 - Legibility Tools]]'s bitmap font (a hard dependency for the HUD and
Found Soul text sub-items below — if Phase 03 hasn't landed by 08-14, font work becomes the first
task inside this phase rather than a prerequisite met in advance).
**Blocks:** Submission.

## Why this phase, and why its start date is not negotiable

[[Overview]] and [[Handover]] both state the judging order as **finished → under size → fun**, in
that priority order. This project's own retrospective on the isometric pivot already names the risk
directly: *"a beautiful isometric village with no audio, no font and no save scores worse than the
flat build with all three."* As of this phase's authoring (2026-08-04), the deadline is 2026-09-04 —
one month out — and Week 4 (a softsynth built from zero) plus the rest of Week 5 (save/load, HUD, win
state, a game-feel pass) were, at the time the isometric pivot was first undertaken, **completely
untouched**. Save/load was pulled forward into [[Phase 08 - Save Load]] specifically to reduce what
this phase still owes, but audio remains the single largest untouched risk in the entire project.

This phase's start date is fixed at 2026-08-14 — ten days from the phase folder's authoring date —
specifically so that art and traversal work (Phases 03–10) cannot consume the remaining runway the
way the isometric pivot already consumed unbudgeted time once before.

## Definition of done

- [ ] A softsynth exists: procedural pattern data, real-time synthesis in the audio callback
      (following the existing `audio_cb` pattern, `src/main.c:293`, which already proves this
      project's callback can do real-time-safe synthesis — the restore confirm beat is a working,
      measured example at 0.141 ms worst case against a 21.333 ms deadline).
- [ ] The layered-music hook already wired in `try_restore` (`src/main.c` — [[Handover]] notes "the
      hook already exists" for audio-layer-per-restore) actually switches on a layer, not just leaves
      the hook unconnected.
- [ ] At minimum the two audio layers named as never-cut in [[Cut List]] (Base + Voice of Souls) are
      implemented; Strings/Pad/Bells are deferred to [[Cut List]]'s descoping order if time is short.
- [ ] SFX beyond the existing restore confirm beat exist for whatever remaining game moments need
      audio feedback (see [[Audio and Synth]] for the full intended scope).
- [ ] Callback profiling under full layered load (not just the single-beat load already measured) —
      re-run the equivalent of `--audio-test --sfx` with every layer active simultaneously, since
      the existing 0.141 ms measurement is for one confirm beat, not five concurrent layers.
- [ ] A minimal HUD exists, built on [[Phase 03 - Legibility Tools]]'s font, respecting [[Save and
      UI]]'s explicit "no HUD clutter" constraint — this is deliberately not the reference image's
      ornate multi-panel HUD; see [[Art Bible]] §7's framing of that as out of scope pending its own
      flagged decision.
- [ ] A win/completion state is wired to actual on-screen feedback (the underlying `game_complete`
      logic already exists per [[Handover]] §2's "what actually works" list — this item is about the
      player being told, not about the logic existing).
- [ ] [[QA Checklist]] is run in full, including its currently-unchecked items: run on a machine
      without dev tools installed, verify no SDL_image/ttf/mixer symbols in the final build, verify
      no shipped asset files.
- [ ] Final size audit: confirm `wayfarer.exe` is comfortably under the 1,440,000-byte ship target
      (749,312 bytes of headroom as of this phase folder's authoring — this is not expected to be
      tight, but confirm rather than assume, especially once audio sample/pattern data is added).
- [ ] Submit with days of margin before 2026-09-04, not at the deadline.

## Concrete tasks

1. **If [[Phase 03 - Legibility Tools]] has not landed by 2026-08-14, build the bitmap font first**,
   as a sub-task of this phase rather than treating its absence as a blocker to wait out — the font
   is needed for both the HUD and Found Soul restoration text, both in this phase's scope.
2. Design the softsynth's pattern/sequencing data format — see [[Audio and Synth]] for whatever
   scope was already specified there; this phase file does not re-derive synthesis design, only
   schedules it.
3. Implement synthesis in `audio_cb` (`src/main.c:293`) or a clearly-separated function it calls,
   following the existing real-time-safety discipline this project already has proof of (no
   allocation, no locking, in the callback — [[QA Checklist]] already names this as a rule).
4. Wire the layer-switch-on logic into `try_restore` (`src/main.c:1285`) where the hook already
   exists per the previous handover's notes.
5. Build the minimal HUD on the Phase 03 font — scope it against [[Save and UI]] before adding
   anything, since that note's "no HUD clutter" instruction is explicit and this project has already
   dropped hearts and tool/inventory icons once as too much.
6. Wire `game_complete`'s existing logic to visible on-screen feedback.
7. Run the full [[QA Checklist]], including the items that require a second machine — this has never
   been done in this project's history and is explicitly named as a standing gap in every prior
   handover.
8. Final build, final size report, tag or note the submission commit clearly, push to the remote
   (`https://github.com/Nishmam12/matha-noshto-game`) — confirm whether the contest requires the repo
   to be public at submission time, since it is currently private, per persisted project memory.

## Verification gate

`--audio-test --sfx` re-run under full multi-layer load (not just the single confirm beat), zero
partial writes / NaN / out-of-range, worst-case callback time reported and compared against the
21.333 ms deadline. Full [[QA Checklist]] run to completion, all items checked, including the
never-before-checked "runs clean on a machine without dev tools" and "verified on a second machine"
items. Final `wayfarer.exe` byte size reported and confirmed under 1,440,000.

## Traps specific to this phase

- **Audio being untouched this long means it is the single largest unknown-unknown risk in the
  project** — every other system has been iterated on and measured at least once; the softsynth has
  not been started. Budget for this phase taking longer than its allotted runway suggests, and treat
  any slip here as the first thing to escalate, not something to silently absorb by cutting QA time.
- **[[Cut List]]'s descoping order exists precisely for this phase.** If time runs short inside
  Phase 11 itself, the pre-committed order (reduce the second ability-gated ring → reduce fragment/
  soul counts → cut Kindle, ship Wade + Climb only → cut to Base + Voice of Souls only → reduce
  polish depth) should be followed rather than improvised under deadline pressure. **Never cut:** the
  fog-reveal core feel, the reachability guarantee, staying under the byte limit, a defined
  completable end state — these four survive every other cut.
- **"Runs clean on a machine without dev tools" and "verified on a second machine" have been
  unchecked items in every single handover this project has produced.** There is a real risk this
  gets deprioritized again under deadline pressure precisely because it always has been before.
  Schedule it explicitly rather than leaving it as the last item that quietly slips.
- **A minimal HUD is easy to over-scope back toward the reference image's ornate version** under the
  pressure of "make it look finished." Re-read [[Save and UI]]'s explicit constraint before adding
  anything beyond what [[QA Checklist]] and basic playability require.

## Evidence

Not yet started.
