# Phase 012 (MVP-12) — Cursor and keyboard across computers
Status: BACKLOG — core and Mac done (PR #14, merged); the Windows controller and endpoint are in
the Phase 3 PR (#18); TESTING I1–I9 and W8–W14 wait for the devices

DEVPLAN reference: Phase 11. Depends on: Phases 1, 3, 10

## Goal
One computer's mouse and keyboard move across every computer in the arrangement, in every direction.

## Tasks
- [x] Controller orchestration (core and Mac; Windows in the Phase 3 PR): capture on crossing, virtual cursor, Enter and Leave, motion datagrams, keys, buttons and scroll with positions, held-key ledger and release-all on every exit path.
- [x] Endpoint orchestration (core and Mac): injection, refusals (locked, asleep, not permitted), cursor reports.
- [x] Safety (core and Mac): input returns home within one second on peer loss; local input on a controlled computer takes it back; hotkeys for "return to this computer" and "lock input here"; a visible indicator on a computer while it is being controlled.
- [x] A default side-by-side arrangement (members place themselves beside the others since Phase 9) until the arrangement editor exists (Phases 16 and 17).
- [x] Input latency measurement through the session clock offset, shown in the debug view.

## Acceptance Criteria
- [ ] With at least three computers (Mac, PC and a second Mac or PC), the cursor and keyboard cross in every direction by pushing through an edge, and stop at walls.
- [ ] Input latency on wired gigabit: median under 8 ms, p99 under 20 ms; on 5 GHz Wi-Fi: median under 15 ms. Method documented in `docs/TESTING.md`.
- [ ] Unplugging the network during control returns the input to the controller within one second.
- [ ] A scripted stress run of 1000 crossings with keys and buttons held leaves no stuck key or button on any computer. (Passes over the simulated network: `InputControl.AThousandCrossingsLeaveNoKeyOrButtonPressed`; real computers wait for the devices.)
- [ ] Moving the local mouse of a controlled computer takes control back at once.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision (2026-11-04): this phase is done now for `core` and the Mac only, stacked on
  `feature/phase-15-clipboard`. The Windows controller and endpoint wait for Phase 3 and the PC;
  Mac ↔ Mac hardware rows wait for the second Mac.
- The controller and endpoint logic lives in a `control` layer of `core`, after the clipboard in
  the observer chain, and is tested over the simulated network; the platforms capture, inject,
  hide the cursor, register hotkeys and show the indicator.
- No crossing while a mouse button is held, until Phase 14 carries drags; one controller at a
  time (busy); keys held while crossing are released where the cursor leaves; default hotkeys
  ⌃⌥⌘⎋ (return) and ⌃⌥⌘L (lock). Details in DECISIONS.md.
- Latency is measured from the moment the controller's event tap received the event: CGEvent
  timestamps are not in one unit on every Mac.
- Scroll values pass unchanged Mac ↔ Mac; normalisation between platforms is Phase 12.
