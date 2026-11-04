# Phase 012 (MVP-12) — Cursor and keyboard across computers
Status: ACTIVE

DEVPLAN reference: Phase 11. Depends on: Phases 1, 3, 10

## Goal
One computer's mouse and keyboard move across every computer in the arrangement, in every direction.

## Tasks
- [ ] Controller orchestration on both platforms: capture on crossing, virtual cursor, Enter and Leave, motion datagrams, keys, buttons and scroll with positions, held-key ledger and release-all on every exit path.
- [ ] Endpoint orchestration: injection, refusals (locked, asleep, not permitted), cursor reports.
- [ ] Safety: input returns home within one second on peer loss; local input on a controlled computer takes it back; hotkeys for "return to this computer" and "lock input here"; a visible indicator on a computer while it is being controlled.
- [ ] A default side-by-side arrangement until the arrangement editor exists (Phases 16 and 17).
- [ ] Input latency measurement through the session clock offset, shown in the debug view.

## Acceptance Criteria
- [ ] With at least three computers (Mac, PC and a second Mac or PC), the cursor and keyboard cross in every direction by pushing through an edge, and stop at walls.
- [ ] Input latency on wired gigabit: median under 8 ms, p99 under 20 ms; on 5 GHz Wi-Fi: median under 15 ms. Method documented in `docs/TESTING.md`.
- [ ] Unplugging the network during control returns the input to the controller within one second.
- [ ] A scripted stress run of 1000 crossings with keys and buttons held leaves no stuck key or button on any computer.
- [ ] Moving the local mouse of a controlled computer takes control back at once.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision (2026-11-04): this phase is done now for `core` and the Mac only, stacked on
  `feature/phase-15-clipboard`. The Windows controller and endpoint wait for Phase 3 and the PC;
  Mac ↔ Mac hardware rows wait for the second Mac.
- The controller and endpoint logic lives in a `control` layer of `core`, after the clipboard in
  the observer chain, and is tested over the simulated network; the platforms capture, inject,
  hide the cursor, register hotkeys and show the indicator.
