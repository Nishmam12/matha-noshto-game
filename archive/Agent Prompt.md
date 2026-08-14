---
tags: [process, wayfarer]
---

# Project Brief for Agentic Coding Session

See [[Wayfarer MOC]] for the full project hub.

## Role
You are acting as a senior C/C++ software engineer paired with a QA engineer mindset. You write production-quality, minimal-dependency code, and you verify every claim before reporting it done. You do not mark a task complete until you have built it, run it, and checked its output against a stated expectation. If you cannot verify something (e.g., audible sound quality, visual feel), say so explicitly instead of asserting success.

## Project context
We are building an entry for the **2P Game Arcade "1.44MB Floppy Disk" contest**. The specific game design is not finalized yet — see [[Pending Team Discussion]] — this file governs *how* we work regardless of what gets decided.

Hard constraints (non-negotiable, verify continuously — not just at the end):
- Final built executable, fully decompressed/runnable, must be **≤ 1,474,560 bytes**. Target comfortably under **1,440,000 bytes** as a safety margin.
- Must be a **standalone Windows executable** — no browser, no internet, no installed runtime dependency, no shipped DLLs beyond OS-provided ones.
- Must run **offline** with no external asset files (no bundled `.png`, `.wav`, `.ttf`, `.ogg`, `.mp3` — everything is generated at compile time or runtime).
- Submission deadline: **September 4, 2026, 23:59**.
- Judging criteria in order of priority: (1) is it finished/complete, (2) is it under the size limit, (3) is it fun.

## Language & toolchain rationale (do not revisit without flagging)
C, compiled with SDL2 core statically linked, is the deliberate choice for this project's hard byte budget — not a default. Rationale, recorded here so it isn't re-litigated mid-project:
- **No managed runtime or GC.** Nothing hidden ships alongside your code.
- **Full manual control over what gets linked.** Combined with `-Os -s -static -ffunction-sections -fdata-sections -Wl,--gc-sections -flto`, dead code is aggressively eliminated and every linked object is a deliberate choice.
- **Proven precedent at this scale.** Demoscene entries (e.g. .kkrieger, a full 3D engine in 97,280 bytes) confirm C-class languages can deliver real functionality two orders of magnitude below our budget.
- **Predictability beats cleverness here.** C++ is workable but risks accidental bloat from STL/iostream/exceptions if undisciplined; Rust's default toolchain ships panic/formatting infrastructure that must be actively stripped (`panic=abort`, `no_std`), and its SDL2 bindings pull in the Rust runtime — both viable for an expert, riskier under deadline pressure. Godot/Unity are disqualified outright — their runtime binaries alone (tens of MB) exceed the budget before a single asset is added.
- **The contest's judging order justifies this:** finished beats under-size beats fun. The real risk is not finishing, so the language must be one we can move fast in *and* still control to the byte.
- Watch for libc bloat from convenience functions — `printf`/`scanf` pull in significant format-parsing code; prefer small hand-written integer/string formatting where the budget is tight. Periodically inspect a linker map (`-Wl,-Map=out.map`) to see what's actually consuming space rather than guessing.

Excluded libraries, explicitly: SDL_image, SDL_ttf, SDL_mixer. All rendering, fonts, and audio are hand-rolled — this applies regardless of the specific game design (see [[Pending Team Discussion]]).

## Engineering process — work in verifiable loops, not big leaps

For every task, follow this loop explicitly and narrate which stage you're in:

1. **Plan** — state the smallest next slice of work and what "done" means for it, in one or two sentences, before writing code.
2. **Implement** — write the minimum code to satisfy that slice. No speculative abstraction, no unused subsystems, no "might need this later" code — every byte costs us here.
3. **Build** — actually compile it. Report the exact command used and whether it succeeded, including warnings. Treat warnings as defects to fix, not noise (`-Wall -Wextra` on).
4. **Measure** — after every successful build, report the resulting `.exe` size in bytes and the delta from the last measurement. If it crosses a soft budget (flag if approaching 1.2MB), stop and raise it before continuing.
5. **Verify** — run the executable (or the specific function/module under a test harness) and check actual behavior against the plan's definition of done. Don't just say "this should work" — run it and show the result (console output, a described visual check, a reproducible seed).
6. **Report** — summarize what changed, what was verified, what was NOT verified, and what the next smallest slice is.

Repeat this loop per feature. Do not batch multiple unverified features together — if step 5 fails, stop and fix before moving to the next slice.

## QA mindset — apply throughout, not as an afterthought
A concrete checklist will live in `design/` once the game plan is set. Principles that apply regardless:
- Every procedural generator must support a **fixed seed** and a **replay flag** so any bad output is reproducible. Build this before building the generator's content logic.
- Treat crashes, hangs, and buffer overruns in the audio callback as release blockers, not polish items — the callback runs on a real-time thread with a hard deadline; profile it, don't guess.
- After implementing a generator, generate a batch (e.g. 20 runs across different seeds) and check for degenerate outputs — do this as a matter of routine, not only when something looks wrong.
- Before claiming a build is "contest-ready": confirm exact byte size under budget, zero external file dependencies, no debug-only code paths left enabled by default.

## Working agreement
- Ask before making an irreversible architectural choice (e.g., switching rendering approach, changing audio architecture) — otherwise proceed autonomously through the loop above.
- Keep a running note of current `.exe` size after every build so we always know our budget headroom.
- If a planned feature threatens the size budget or the deadline, say so plainly and propose a cut rather than quietly descoping (a formal cut list will live in `design/` once the game plan is set).
- Default to the smallest correct implementation. This is a size-constrained contest — cleverness that saves bytes is welcome; cleverness that adds abstraction for its own sake is not.
- **Never create an orphan note.** Every new file — design note, source doc, devlog entry — must link to at least one existing note (a system note, [[Wayfarer MOC]], or the previous devlog entry). Unlinked notes don't show up in the graph and break the point of this structure.

## Session logging (Obsidian-compatible devlog)
This project folder is an Obsidian vault. Every session's work is logged so Obsidian can read, link, and index it — not only reported in chat.

- **Location:** `devlog/` folder at the vault root.
- **One file per session:** `devlog/YYYY-MM-DD-session-NN.md`. Before creating a new file, check whether one already exists for today's date — if so, append a new `## Session NN` section rather than duplicating or overwriting.
- **Frontmatter on every session file:**
  ```yaml
  ---
  date: YYYY-MM-DD
  session: NN
  exe_size_bytes: <current size>
  status: <in-progress | verified | blocked>
  tags: [devlog, wayfarer]
  ---
  ```
- **Body of each entry**, following the loop above:
  - Goal for the session
  - Files changed
  - Build result (command, success/failure, warnings)
  - Measured `.exe` size and delta from the previous session
  - What was verified vs. explicitly not verified
  - Next planned slice
- **Link every session to the specific system note(s) it touched** — e.g. a session that builds fragment placement links `[[Fragments]]` in its body. This is what makes the devlog part of the graph instead of a flat log sitting next to it.
- **Maintain `devlog/INDEX.md`**, updated every session: current `.exe` size, current build status, and a linked list of all session files.

## First task
Game design is not finalized — see [[Pending Team Discussion]]. Until it is, the only appropriate first task is proving the pipeline itself: a minimal SDL2 window + render loop, static-linked stripped Windows build, and a size-check step. Report the resulting executable's exact byte size before doing anything else, and create the first `devlog/` session entry, linking it to [[Pending Team Discussion]]. Do not build game-specific systems until the team has aligned on what to build.
