# Phase 019 (MVP-19) — Robustness and diagnostics
Status: ACTIVE

DEVPLAN reference: Phase 18. Depends on: Phases 16, 17

## Goal
Osdrop behaves predictably through everyday disruptions.

## Tasks
- [ ] Sleep and wake, network changes (Wi-Fi ↔ Ethernet, IP changes), monitors plugged, unplugged or rescaled during control, lock and unlock, fast user switching.
- [ ] Coexistence with Osplay: Osplay's virtual displays are ordinary monitors to Osdrop; the arrangement shows them and the behaviour is documented.
- [ ] Structured logging (unified logging; rotating files on Windows; `core` routed into both) and a "Copy diagnostics" action without keystrokes, clipboard or file contents.
- [ ] Manual test matrix in `docs/TESTING.md`.

## Acceptance Criteria
- [ ] Every scenario in the matrix passes on the reference hardware, wired and on Wi-Fi.
- [ ] Waking any computer restores the mesh automatically.
- [ ] A two-hour session with continuous crossings, drags and pastes shows no memory growth and no latency drift.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision (2026-11-20): a locked PC refuses control like a locked Mac; no typing a PC's
  password from the Mac. Another user's session in front counts as locked on both.
- Audit (2026-11-20): sleep/wake, display changes and Copy diagnostics existed on both; Windows did
  not report a locked session and neither side handled fast user switching (fixed). Network
  changes rely on the session's liveness timeout and rediscovery: R2–R4 decide whether more is
  needed.
- 2026-11-21: fixes from the run: a41a456 (locked PC refused), 5d63a03 (keys released on lock),
  61727d5 (displays reported once settled, in order); docs 32a79ac. Matrix: R1–R4, R7, R8 pass
  (user report); R5, R6, R9–R11 not run (user: edge cases, assumed). Open: R12, the two-hour run,
  deferred by the user (2026-11-21) to a day the PC stays on for two hours.

- User decision (2026-11-21): the two-hour run R12 is not needed; a look at the running apps'
  memory replaces it ("a leak would show anyway"). Mac: flat at 52 MB after three hours (TESTING
  Runs). The phase closes once the PC's numbers are in.
