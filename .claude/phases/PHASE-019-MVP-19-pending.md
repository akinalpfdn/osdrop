# Phase 019 (MVP-19) — Robustness and diagnostics
Status: PENDING

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
