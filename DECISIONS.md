# Decisions

This file tracks all non-trivial technical decisions made during this project.
Entries are append-only. The stack itself is specified in `DEVPLAN.md`; only deviations and
choices the plan leaves open are logged here.

---

## 2026-11-02 — Repository visibility, branch flow and work tracking
**Chosen:** User decision: public GitHub repository `akinalpfdn/osdrop`, with the same care as Osplay. Every DEVPLAN phase is developed on its own branch (`feature/phase-XX-<slug>`) and lands through a pull request that the user merges once CI is green. Progress is tracked only in `.claude/phases/` and this file.
**Alternatives:** Private repository until MVP; committing to `main`; Lore work items.
**Why:** Matches the sibling project, whose flow worked; public repositories get unlimited Actions minutes; a PR per phase is a reviewable, revertable unit that matches the phase gates.
**Trade-offs:** Work is visible before it is usable; every phase needs a PR round-trip.
**Revisit if:** Outside contributors need a different integration model.

---

## 2026-11-02 — Production grade from the first phase
**Chosen:** User decision: no proof-of-concept or spike code. Platform risks (input capture and injection, drag capture and promise drags on both platforms) are retired by Phases 1–4, which build the production modules and end with a go/no-go gate.
**Alternatives:** Throwaway spikes in `spikes/` before implementation phases.
**Why:** Spike code tends to survive into the product untested; building the real modules behind interfaces gives the same answer and keeps the work.
**Trade-offs:** If a gate fails, more code is discarded than a spike would have produced.
**Revisit if:** A gate fails and the alternative approach is uncertain enough to need a cheap experiment first.

---

## 2026-11-02 — Code shared with Osplay is copied, not linked
**Chosen:** User decision: the reusable parts of Osplay's core (Noise handshake, cipher states, replay window, randomness injection, Asio transport base, control framing, utilities) are copied into Osdrop at one recorded Osplay commit in Phase 6, adapted (names, labels, prologue) and owned by Osdrop from then on. The source commit is recorded here when the copy happens; provenance is noted in `docs/THIRD_PARTY.md`.
**Alternatives:** A third repository with the shared library used by both projects as a submodule; Osplay as a submodule of Osdrop.
**Why:** The shared pieces are small and stable, and the protocols differ (video streams versus input, drags and files). A shared library would tie the two products' release cycles together and force a refactor of a shipped product; a whole-Osplay submodule would pull in an unrelated product and break on its protocol changes.
**Trade-offs:** A security fix has to be applied in both repositories; the copies can drift.
**Revisit if:** The copied code keeps needing the same changes in both projects.

---

## 2026-11-02 — Any number of computers in a free arrangement
**Chosen:** User decision: a group holds up to 8 computers placed freely in one shared arrangement (like a display arrangement), in every direction (Mac ↔ PC, Mac ↔ Mac, PC ↔ PC). Whichever computer's mouse and keyboard are in use drive the others.
**Alternatives:** Exactly two computers.
**Why:** Covers every desk the user has (two Macs, two PCs) and avoids a later protocol and UI change for a third computer.
**Trade-offs:** Needs group membership, a replicated arrangement and a full mesh of sessions from the start.
**Revisit if:** Real use shows groups larger than 8.

---

## 2026-11-02 — First-release scope
**Chosen:** User decision: the first release has cursor and keyboard crossing, file and folder drag and drop, a shared clipboard for text and images, and file copy-paste between computers. Dragging text selections and images, and control of the Windows lock screen and UAC prompts, are post-MVP.
**Alternatives:** Including text and image dragging and secure-desktop control in the first release.
**Why:** Copy-paste of text, images and files is what users expect next to dragging; the other two add platform work (rich drag formats, a secure-desktop input path) that the first release can do without.
**Trade-offs:** A locked PC cannot be unlocked from another computer's keyboard in the first release.
**Revisit if:** Users ask for the deferred items.

---

## 2026-11-02 — Windows input runs as SYSTEM in the user's session
**Chosen:** `OsdropService` (LocalSystem) launches `OsdropInput.exe` with a SYSTEM token into the active console session; only hooks, cursor parking and injection run there. Networking, protocol parsing, files, clipboard and drag and drop stay in the user's medium-integrity app, which talks to the input process over a per-session pipe (ACL: session user and SYSTEM; client image path verified; injection accepted only during active control).
**Alternatives:** Everything in the user's process (no control over elevated windows such as Task Manager, admin terminals and installers, and capture fails while one is focused); `uiAccess` (requires an Authenticode-signed binary, which the project does not have); the whole engine as SYSTEM.
**Why:** UIPI blocks both hooks and injection for elevated windows from a medium-integrity process; a production tool has to work over them. Keeping the network-facing code at medium integrity means a protocol bug never runs as SYSTEM, and OLE drag and drop only works between processes of the same integrity. The same service later carries secure-desktop support.
**Trade-offs:** A privileged component and an install-time service; malware already running as the user could try to drive elevated windows through the pipe (mitigated, documented in `docs/SECURITY_MODEL.md`).
**Revisit if:** A code-signing certificate allows `uiAccess` instead.

---

## 2026-11-02 — Crossing by pushing through the edge
**Chosen:** User decision (from the product description): the cursor crosses after being held at an edge and pushed for about one second; the push time is a setting, and immediate crossing is offered.
**Alternatives:** Immediate crossing as the default; crossing only with a modifier key.
**Why:** Prevents accidental crossings when aiming at an edge or a corner, as Universal Control does.
**Trade-offs:** Deliberate crossings take a moment; the default is tuned in Phase 12 on real hardware.
**Revisit if:** The default feels slow in daily use.

---

## 2026-11-02 — Branch and tag protection
**Chosen:** As in Osplay: ruleset "main protection" blocks deletion, force push and any direct update of `main`, and requires a pull request with green `macOS`, `macOS sanitizers` and `Windows` checks; ruleset "release tag protection" applies the same to creating, moving and deleting `v*` tags. The repository admin role (only `akinalpfdn`) bypasses both.
**Alternatives:** Legacy branch protection; no bypass; requiring only the `macOS` and `Windows` checks as Osplay does.
**Why:** Outside contributors work through forks and PRs; merging stays with the owner. The sanitizer job exists from the first phase here, so it is required too.
**Trade-offs:** The bypass also applies to tools acting with the owner's credentials, so PR-per-phase stays a working rule rather than something GitHub enforces against the owner.
**Revisit if:** Another person gets write access, or releases need technically immutable tags.

---
